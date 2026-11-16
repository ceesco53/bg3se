// Mach-O image inspection: locating loaded images, their code ranges and their symbols.
//
// On Windows the extender finds engine functions by scanning for byte patterns, because bg3.exe
// ships without symbols. The macOS build of the game keeps its full symbol table (local symbols
// included), so here functions and globals are resolved by (mangled) name instead. dlsym() only
// sees exported symbols, hence the manual LC_SYMTAB walk.
#include "Darwin.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>
#include <cstring>
#include <functional>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bg3se::darwin {

namespace {

struct LoadedImage
{
    mach_header_64 const* Header{ nullptr };
    std::intptr_t Slide{ 0 };
    std::string Path;
    // Symbol name (without the leading underscore) -> address; built on first lookup
    std::unordered_map<std::string_view, void*> Symbols;
    bool SymbolsIndexed{ false };
};

// These are function-local statics on purpose: the platform layer is used from the dylib's load
// constructor, which can run before the dynamic initializers of other translation units.
std::mutex& ImagesMutex()
{
    static std::mutex mutex;
    return mutex;
}

// Keyed by the name the caller asked for ("" = main executable)
std::unordered_map<std::string, LoadedImage>& Images()
{
    static auto images = new std::unordered_map<std::string, LoadedImage>();
    return *images;
}

LoadedImage* FindImage(char const* name)
{
    std::string key = name ? name : "";
    auto& images = Images();
    auto it = images.find(key);
    if (it != images.end()) {
        return &it->second;
    }

    auto count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        auto header = _dyld_get_image_header(i);
        auto path = _dyld_get_image_name(i);
        if (!header || !path || header->magic != MH_MAGIC_64) continue;

        bool matches = name
            ? strstr(path, name) != nullptr
            : header->filetype == MH_EXECUTE;
        if (!matches) continue;

        LoadedImage image;
        image.Header = reinterpret_cast<mach_header_64 const*>(header);
        image.Slide = _dyld_get_image_vmaddr_slide(i);
        image.Path = path;
        return &images.insert(std::make_pair(key, std::move(image))).first->second;
    }

    return nullptr;
}

template <class TCommand, class TFunc>
void ForEachLoadCommand(mach_header_64 const* header, uint32_t type, TFunc&& func)
{
    auto cmd = reinterpret_cast<load_command const*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (cmd->cmd == type) {
            func(reinterpret_cast<TCommand const*>(cmd));
        }
        cmd = reinterpret_cast<load_command const*>(reinterpret_cast<uint8_t const*>(cmd) + cmd->cmdsize);
    }
}

void IndexSymbols(LoadedImage& image)
{
    image.SymbolsIndexed = true;

    symtab_command const* symtab{ nullptr };
    segment_command_64 const* linkedit{ nullptr };
    ForEachLoadCommand<symtab_command>(image.Header, LC_SYMTAB, [&](symtab_command const* cmd) { symtab = cmd; });
    ForEachLoadCommand<segment_command_64>(image.Header, LC_SEGMENT_64, [&](segment_command_64 const* cmd) {
        if (strcmp(cmd->segname, SEG_LINKEDIT) == 0) linkedit = cmd;
    });

    if (!symtab || !linkedit) return;

    // File offsets in LC_SYMTAB are relative to __LINKEDIT's file offset
    auto linkeditBase = static_cast<std::intptr_t>(linkedit->vmaddr) + image.Slide - static_cast<std::intptr_t>(linkedit->fileoff);
    auto symbols = reinterpret_cast<nlist_64 const*>(linkeditBase + symtab->symoff);
    auto strings = reinterpret_cast<char const*>(linkeditBase + symtab->stroff);

    image.Symbols.reserve(symtab->nsyms);
    for (uint32_t i = 0; i < symtab->nsyms; i++) {
        auto const& sym = symbols[i];
        // Skip debugging entries and anything that isn't defined in a section of this image
        if ((sym.n_type & N_STAB) != 0 || (sym.n_type & N_TYPE) != N_SECT) continue;
        if (sym.n_un.n_strx == 0 || sym.n_un.n_strx >= symtab->strsize) continue;

        auto name = strings + sym.n_un.n_strx;
        if (name[0] == '_') name++;
        if (name[0] == 0) continue;

        image.Symbols.insert(std::make_pair(std::string_view(name), reinterpret_cast<void*>(sym.n_value + image.Slide)));
    }
}

}

void* FindSymbol(char const* imageName, char const* name)
{
    std::lock_guard lock(ImagesMutex());
    auto image = FindImage(imageName);
    if (!image) return nullptr;

    if (!image->SymbolsIndexed) {
        IndexSymbols(*image);
    }

    if (name[0] == '_' && name[1] == '_') {
        // Accept names as nm prints them (with the extra Mach-O underscore)
        name++;
    }

    auto it = image->Symbols.find(std::string_view(name));
    return it != image->Symbols.end() ? it->second : nullptr;
}

void ForEachSymbolWithPrefix(char const* imageName, char const* prefix, std::function<void(char const*, void*)> const& callback)
{
    std::lock_guard lock(ImagesMutex());
    auto image = FindImage(imageName);
    if (!image) return;

    if (!image->SymbolsIndexed) {
        IndexSymbols(*image);
    }

    std::string_view wanted(prefix);
    for (auto const& symbol : image->Symbols) {
        if (symbol.first.size() >= wanted.size() && symbol.first.compare(0, wanted.size(), wanted) == 0) {
            // Names are views into the image's (null-terminated) string table
            callback(symbol.first.data(), symbol.second);
        }
    }
}

bool GetImageInfo(char const* imageName, ImageInfo& info)
{
    std::lock_guard lock(ImagesMutex());
    auto image = FindImage(imageName);
    if (!image) return false;

    uint64_t lowest = ~0ull, highest = 0;
    ForEachLoadCommand<segment_command_64>(image->Header, LC_SEGMENT_64, [&](segment_command_64 const* seg) {
        // __PAGEZERO is unmapped and __LINKEDIT holds no code or data of interest
        if (strcmp(seg->segname, SEG_PAGEZERO) == 0 || strcmp(seg->segname, SEG_LINKEDIT) == 0) return;
        lowest = std::min(lowest, seg->vmaddr);
        highest = std::max(highest, seg->vmaddr + seg->vmsize);

        if (strcmp(seg->segname, SEG_TEXT) == 0) {
            auto sect = reinterpret_cast<section_64 const*>(seg + 1);
            for (uint32_t i = 0; i < seg->nsects; i++) {
                if (strncmp(sect[i].sectname, SECT_TEXT, sizeof(sect[i].sectname)) == 0) {
                    info.TextStart = reinterpret_cast<uint8_t const*>(sect[i].addr + image->Slide);
                    info.TextSize = sect[i].size;
                }
            }
        }
    });

    if (highest == 0) return false;

    info.Start = reinterpret_cast<uint8_t const*>(lowest + image->Slide);
    info.Size = highest - lowest;
    info.Slide = image->Slide;
    info.Path = image->Path;
    if (!info.TextStart) {
        info.TextStart = info.Start;
        info.TextSize = info.Size;
    }
    return true;
}

void* GetMainEntryPoint()
{
    std::lock_guard lock(ImagesMutex());
    auto image = FindImage(nullptr);
    if (!image) return nullptr;

    void* entry{ nullptr };
    ForEachLoadCommand<entry_point_command>(image->Header, LC_MAIN, [&](entry_point_command const* cmd) {
        entry = const_cast<uint8_t*>(reinterpret_cast<uint8_t const*>(image->Header)) + cmd->entryoff;
    });
    return entry;
}

void const* GetImageHeader(char const* imageName)
{
    std::lock_guard lock(ImagesMutex());
    auto image = FindImage(imageName);
    return image ? image->Header : nullptr;
}

std::optional<std::string> GetEmbeddedResource(char const* name)
{
    // Resources are linked in with `-sectcreate __SE_RES <name> <file>`
    Dl_info self;
    if (!dladdr(reinterpret_cast<void const*>(&GetEmbeddedResource), &self) || !self.dli_fbase) {
        return {};
    }

    unsigned long size{ 0 };
    auto data = getsectiondata(reinterpret_cast<mach_header_64 const*>(self.dli_fbase), "__SE_RES", name, &size);
    if (!data) return {};

    return std::string(reinterpret_cast<char const*>(data), size);
}

}
