// macOS platform services used by the shared extender code where the Windows implementation
// has no direct equivalent. Implemented in macOS/Platform/*.cpp|mm.
#pragma once

#if !defined(__APPLE__)
#error "macOS/Platform is only built on macOS"
#endif

#include <cstdint>
#include <string>
#include <optional>

namespace bg3se::darwin {

// Version of the running game, from the app bundle's Info.plist (CFBundleVersion, "4.1.1.7631656")
bool GetGameBundleVersion(uint32_t& major, uint32_t& minor, uint32_t& revision, uint32_t& build);

// Absolute path of the directory containing the game executable (…/Contents/MacOS)
std::string GetGameExecutableDir();

// Directory the extender keeps its own files in (~/Library/Application Support/BG3ScriptExtender)
std::string GetExtenderDataDir();

// Resolves a (mangled or demangled) symbol from the symbol table of a loaded image, including
// non-exported local symbols that dlsym() cannot see. `image` is a substring of the image path;
// nullptr means the main executable. Returns nullptr if the symbol is not present.
void* FindSymbol(char const* image, char const* name);

// Address range of a loaded Mach-O image and of its __TEXT,__text section
struct ImageInfo
{
    uint8_t const* Start{ nullptr };
    std::size_t Size{ 0 };
    uint8_t const* TextStart{ nullptr };
    std::size_t TextSize{ 0 };
    std::intptr_t Slide{ 0 };
    std::string Path;
};

// `image` is a substring of the image path; nullptr means the main executable
bool GetImageInfo(char const* image, ImageInfo& info);

// mach_header of a loaded image (the macOS notion of a module handle)
void const* GetImageHeader(char const* image);

// Address of the main executable's entry point (LC_MAIN, i.e. main())
void* GetMainEntryPoint();

// Contents of a resource embedded in the extender dylib (section in the __SE_RES segment)
std::optional<std::string> GetEmbeddedResource(char const* name);

// Blocking native alert; safe to call from any thread
void ShowAlert(char const* title, char const* message);

// True while the Shift key is held down
bool IsShiftKeyDown();

// Inline hooking (see Hooking.cpp). `target` is patched to jump to `detour`; `*original` receives
// a trampoline that runs the unhooked function. Returns false if the function can't be hooked.
bool InstallHook(void* target, void* detour, void** original);
bool RemoveHook(void* target);

// Makes [address, address+size) writable (copy-on-write for code / constant pages) or restores
// read+execute / read-only protection afterwards.
bool MakeWritable(void const* address, std::size_t size);
bool RestoreProtection(void const* address, std::size_t size, bool executable);

// True if [address, address+size) is readable in this process
bool IsReadable(void const* address, std::size_t size);

}
