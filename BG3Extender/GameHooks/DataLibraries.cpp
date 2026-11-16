#include "stdafx.h"
#include <GameHooks/DataLibraries.h>
#include <Extender/Shared/ExtensionState.h>
#include <Extender/ScriptExtender.h>
#include <GameDefinitions/Symbols.h>
#include <string>
#include <functional>
#include <chrono>
#include <psapi.h>
#include <DbgHelp.h>
#include "resource.h"

namespace bg3se
{
    _Post_writable_byte_size_(size) void* BG3Alloc(std::size_t size)
    {
        auto const& sym = GetStaticSymbols();
        // Fallback in case allocator is not available
        if (sym.ls__GlobalAllocator__Alloc && sym.ls__GlobalAllocator__Free && sym.ls__GlobalAllocator__Get) {
            return sym.ls__GlobalAllocator__Alloc(sym.ls__GlobalAllocator__Get(), size, 2, 0, 8);
        } else {
            return malloc(size);
        }
    }

    void BG3Free(_Post_ptr_invalid_ _Pre_valid_ void* ptr) noexcept
    {
        auto const& sym = GetStaticSymbols();
        if (sym.ls__GlobalAllocator__Alloc && sym.ls__GlobalAllocator__Free && sym.ls__GlobalAllocator__Get) {
            sym.ls__GlobalAllocator__Free(sym.ls__GlobalAllocator__Get(), ptr);
        } else {
            return free(ptr);
        }
    }

    LibraryManager::LibraryManager()
        : symbolMapper_(mappings_)
    {}

#if defined(__APPLE__)
}
#include <Extender/Shared/tinyxml2.h>
#include <cxxabi.h>
#include <deque>
namespace bg3se {
    namespace {
        using GstGetProc = LSStringView (uint32_t id);
        GstGetProc* gGstGet{ nullptr };

        // The macOS build has no out-of-line FixedString::GetString; ls::gst::Get does the lookup
        LSStringView* DarwinFixedStringGetString(FixedStringBase const* self, LSStringView& sv)
        {
            sv = gGstGet(self->Index);
            return &sv;
        }
    }

    // Binds the engine's type indices (components, systems, replicated types, static data).
    //
    // Windows finds these by scanning the registration code of every type. The macOS binary has
    // a named symbol for each one instead: ls::TypeId<T, Context>::m_TypeIndex, where Context says
    // which index space T is numbered in. The variables are filled in by the game's static
    // initializers, so only their addresses are recorded here; EntitySystemHelpers reads the
    // values later, exactly as it does on Windows.
    void LibraryManager::BindTypeIndicesDarwin()
    {
        struct ContextInfo
        {
            char const* SymbolName;   // as it appears in the macOS symbol
            char const* ExtenderName; // as EntitySystemHelpers expects it
            int32_t Key;              // its address stands in for the Windows context counter
            unsigned Count;
        };

        static ContextInfo contexts[] = {
            { "ecs::ComponentTypeIdContext", "ecs::ComponentTypeIdContext", 0, 0 },
            { "ecs::OneFrameComponentTypeIdContext", "ecs::OneFrameComponentTypeIdContext", 0, 0 },
            { "ecs::SystemsContext", "ecs::EntityWorld::SystemsContext", 0, 0 },
            { "ecs::sync::ReplicatedTypeContext", "ecs::sync::ReplicatedTypeContext", 0, 0 },
            { "ls::ImmutableDataHeadmaster", "ls::ImmutableDataHeadmaster", 0, 0 },
        };
        // Type names have to outlive the maps that point at them
        static std::deque<std::string> names;

        auto& sym = GetStaticSymbols();
        for (auto& context : contexts) {
            context.Count = 0;
            sym.IndexSymbolToContextMaps.insert(std::make_pair(&context.Key, context.ExtenderName));
        }

        static constexpr std::string_view Prefix = "ls::TypeId<";
        static constexpr std::string_view Suffix = ">::m_TypeIndex";

        darwin::ForEachSymbolWithPrefix(nullptr, "_ZN2ls6TypeIdI", [&](char const* mangled, void* address) {
            std::string_view mangledName(mangled);
            if (!mangledName.ends_with("11m_TypeIndexE")) return;

            int status = 0;
            auto demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
            if (demangled == nullptr) return;
            std::string full(demangled);
            free(demangled);

            if (full.size() <= Prefix.size() + Suffix.size()
                || full.compare(0, Prefix.size(), Prefix) != 0
                || full.compare(full.size() - Suffix.size(), Suffix.size(), Suffix) != 0) {
                return;
            }

            // "T, Context": split at the last comma that is not inside template brackets
            auto inner = full.substr(Prefix.size(), full.size() - Prefix.size() - Suffix.size());
            int depth = 0;
            size_t split = std::string::npos;
            for (size_t i = 0; i < inner.size(); i++) {
                if (inner[i] == '<') depth++;
                else if (inner[i] == '>') depth--;
                else if (inner[i] == ',' && depth == 0) split = i;
            }
            if (split == std::string::npos || split + 2 >= inner.size()) return;

            auto contextName = inner.substr(split + 2);
            for (auto& context : contexts) {
                if (contextName == context.SymbolName) {
                    names.push_back(inner.substr(0, split));
                    sym.IndexSymbolToNameMaps.insert(std::make_pair(reinterpret_cast<int32_t*>(address),
                        IndexSymbolInfo{ names.back().c_str(), &context.Key }));
                    context.Count++;
                    break;
                }
            }
        });

        INFO("Engine type indices by name: %d components, %d one-frame components, %d systems, %d replicated types, %d static data types",
            contexts[0].Count, contexts[1].Count, contexts[2].Count, contexts[3].Count, contexts[4].Count);
    }

    bool LibraryManager::FindLibrariesDarwin()
    {
        auto xml = GetExeResource(IDR_BINARY_MAPPINGS);
        tinyxml2::XMLDocument doc;
        if (!xml || doc.Parse(xml->c_str(), xml->size()) != tinyxml2::XML_SUCCESS) {
            ERR("Failed to load symbol mapping table");
            CriticalInitFailed = true;
            return false;
        }

        if (!BindApp()) {
            ERR("LibraryManager::FindLibraries(): Unable to bind BG3 module.");
            CriticalInitFailed = true;
            return false;
        }

        // Engine integration on macOS is experimental: struct layouts and hook signatures are still
        // being verified against the macOS binary. Unless it is switched on explicitly, symbols are
        // resolved and reported but nothing is bound, so no hook is installed and no game memory is
        // touched (the extender behaves as it does on an unsupported game version).
        GameVersionInfo version;
        if (GetGameVersion(version)) {
            INFO("macOS game build %d.%d.%d.%u", version.Major, version.Minor, version.Revision, version.BundleBuild);
        }

        bool bindSymbols = darwin::EngineStage() >= 1;

        // <Symbol Name="(StaticSymbols member)" Mangled="(Mach-O symbol)" [Image="..."] [Critical="true"] />
        unsigned mapped{ 0 }, missing{ 0 };
        auto root = doc.FirstChildElement("BinaryMappings");
        auto mappings = root ? root->FirstChildElement("Mappings") : nullptr;
        for (auto sym = mappings ? mappings->FirstChildElement("Symbol") : nullptr; sym != nullptr; sym = sym->NextSiblingElement("Symbol")) {
            auto name = sym->Attribute("Name");
            auto mangled = sym->Attribute("Mangled");
            auto image = sym->Attribute("Image");
            bool critical = sym->BoolAttribute("Critical", false);
            if (!name || !mangled) {
                ERR("Symbol mapping must have a Name and a Mangled attribute");
                continue;
            }

            auto staticSym = mappings_.StaticSymbols.find(name);
            if (staticSym == mappings_.StaticSymbols.end()) {
                ERR("Symbol mapping references unknown engine symbol: %s", name);
                continue;
            }

            auto address = darwin::FindSymbol(image, mangled);
            if (address == nullptr) {
                missing++;
                if (critical) {
                    ERR("Critical engine symbol not found: %s (%s)", name, mangled);
                    CriticalInitFailed = true;
                } else {
                    WARN("Engine symbol not found: %s (%s)", name, mangled);
                    InitFailed = true;
                }
                continue;
            }

            mapped++;
            if (bindSymbols) {
                auto target = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(&GetStaticSymbols()) + staticSym->second.Offset);
                *target = address;
                staticSym->second.Bound = true;
            }
        }

        INFO("Engine symbols: %d resolved, %d missing", mapped, missing);

        if (!bindSymbols) {
            auto gstOk = darwin::FindSymbol(nullptr, "_ZN2ls3gst3GetEj") != nullptr
                && darwin::FindSymbol(nullptr, "_ZN2ls3gst3AddERKNS_11_StringViewIcEE") != nullptr
                && darwin::FindSymbol(nullptr, "_ZN2ls3gst10s_InstanceE") != nullptr;
            INFO("Global string table: %s", gstOk ? "resolved" : "NOT FOUND");
            INFO("Running in diagnostic mode: engine integration is disabled (see BG3SE_ENGINE in macOS/README.md for the experimental stages).");
            CriticalInitFailed = true;
            DiagnosticMode = true;
            return false;
        }

        INFO("Engine integration stage %d (experimental)", darwin::EngineStage());
        BindTypeIndicesDarwin();

        // The game allocates with the system allocator (ls::MemoryManager wraps malloc / free),
        // which BG3Alloc / BG3Free fall back to when no allocator symbols are bound.
        gCoreLibPlatformInterface.Alloc = &BG3Alloc;
        gCoreLibPlatformInterface.Free = &BG3Free;

        // Global string table: same layout as on Windows, reached through ls::gst
        auto& sym = GetStaticSymbols();
        gGstGet = reinterpret_cast<GstGetProc*>(darwin::FindSymbol(nullptr, "_ZN2ls3gst3GetEj"));
        auto gstAdd = darwin::FindSymbol(nullptr, "_ZN2ls3gst3AddERKNS_11_StringViewIcEE");
        auto gstAcquire = darwin::FindSymbol(nullptr, "_ZN2ls3gst7AcquireEj");
        auto gstRelease = darwin::FindSymbol(nullptr, "_ZN2ls3gst3Map7ReleaseERKNS0_12NodePoolDataE");
        auto gstInstance = darwin::FindSymbol(nullptr, "_ZN2ls3gst10s_InstanceE");
        if (!gGstGet || !gstAdd || !gstAcquire || !gstRelease || !gstInstance) {
            ERR("Global string table functions not found; FixedString support is unavailable");
            CriticalInitFailed = true;
        } else {
            sym.ls__FixedString__GetString = &DarwinFixedStringGetString;
            sym.ls__FixedString__IncRef = reinterpret_cast<FixedString::IncRefProc*>(gstAcquire);
            sym.ls__GlobalStringTable__MainTable__DecRef = reinterpret_cast<GlobalStringTable::MainTable::DecRefProc*>(gstRelease);
            sym.ls__gGlobalStringTable = reinterpret_cast<GlobalStringTable**>(gstInstance);

            gCoreLibPlatformInterface.ls__FixedString__CreateFromString = reinterpret_cast<FixedString::CreateFromStringProc*>(gstAdd);
            gCoreLibPlatformInterface.ls__FixedString__GetString = sym.ls__FixedString__GetString;
            gCoreLibPlatformInterface.ls__FixedString__IncRef = sym.ls__FixedString__IncRef;
            gCoreLibPlatformInterface.ls__GlobalStringTable__MainTable__DecRef = sym.ls__GlobalStringTable__MainTable__DecRef;
            gCoreLibPlatformInterface.ls__gGlobalStringTable = sym.ls__gGlobalStringTable;
        }

        return !CriticalInitFailed;
    }
#endif

    bool LibraryManager::FindLibraries(uint32_t gameRevision)
    {
        RegisterSymbols();

#if defined(__APPLE__)
        return FindLibrariesDarwin();
#endif

        SymbolMappingLoader loader(mappings_);
        PreRegisterLibraries(loader);
        if (!loader.LoadBuiltinMappings(IDR_BINARY_MAPPINGS)) {
            ERR("Failed to load symbol mapping table");
            CriticalInitFailed = true;
        }

#if defined(_DEBUG)
        if (!CriticalInitFailed) {
            // Debug check for symbol xml -> engine bindings
            for (auto const& sym : mappings_.StaticSymbols) {
                if (!sym.second.Bound) {
                    WARN("No mapping exists for engine symbol: %s", sym.first.c_str());
                }
            }
        }
#endif

        if (!BindApp()) {
            ERR("LibraryManager::FindLibraries(): Unable to bind BG3 module.");
            return false;
        }

        RegisterLibraries(symbolMapper_);
        symbolMapper_.MapAllSymbols(false);

        CriticalInitFailed = CriticalInitFailed || symbolMapper_.HasFailedCriticalMappings();
        InitFailed = InitFailed || symbolMapper_.HasFailedMappings();

        if (gExtender->GetConfig().DisableLauncher && !CriticalInitFailed) {
            ApplyCodePatch("KillLauncher");
        }

        auto const& sym = GetStaticSymbols();
        gCoreLibPlatformInterface.Alloc = &BG3Alloc;
        gCoreLibPlatformInterface.Free = &BG3Free;
        gCoreLibPlatformInterface.ls__GlobalStringTable__MainTable__CreateFromString = sym.ls__GlobalStringTable__MainTable__CreateFromString;
        gCoreLibPlatformInterface.ls__FixedString__GetString = sym.ls__FixedString__GetString;
        gCoreLibPlatformInterface.ls__FixedString__IncRef = sym.ls__FixedString__IncRef;
        gCoreLibPlatformInterface.ls__GlobalStringTable__MainTable__DecRef = sym.ls__GlobalStringTable__MainTable__DecRef;
        gCoreLibPlatformInterface.ls__gGlobalStringTable = sym.ls__gGlobalStringTable;

        return !CriticalInitFailed;
    }

    bool LibraryManager::PostStartupFindLibraries()
    {
        if (PostLoaded) {
            return !CriticalInitFailed;
        }

        auto initStart = std::chrono::high_resolution_clock::now();

        symbolMapper_.MapAllSymbols(true);

        if (!CriticalInitFailed) {
            GFS.Initialize();
            InitializeEnumerations();
        }

        auto initEnd = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(initEnd - initStart).count();
        DEBUG("LibraryManager::PostStartupFindLibraries() took %lld ms", ms);

        PostLoaded = true;
        return !CriticalInitFailed;
    }

    void LibraryManager::ApplyCodePatches()
    {
        if (gExtender->GetConfig().EnableAchievements && !WasPatchApplied("ls::ModuleSettings::IsModded")) {
            if (ApplyCodePatch("ls::ModuleSettings::IsModded") && ApplyCodePatch("esv::SavegameManager::ThrowError")) {
                DEBUG("Modded achievements enabled.");
            } else {
                ERR("Couldn't enable modded achievements (symbol not mapped)");
            }
        }

        if (gExtender->GetConfig().DisableStoryPatching && GetStaticSymbols().ls__GlobalSwitches) {
            (*GetStaticSymbols().ls__GlobalSwitches)->DisableStoryPatching = true;
        }
    }

    bool LibraryManager::WasPatchApplied(std::string const& mapping)
    {
        auto it = mappings_.Mappings.find(mapping);
        if (it == mappings_.Mappings.end()) {
            ERR("Cannot apply patch - no such mapping: '%s'", mapping.c_str());
            return false;
        }

        bool applied = false;
        for (auto& patch : it->second.Patches) {
            applied = patch.WasApplied || applied;
        }

        return applied;
    }

    bool LibraryManager::ApplyCodePatch(std::string const& mapping)
    {
        auto it = mappings_.Mappings.find(mapping);
        if (it == mappings_.Mappings.end()) {
            ERR("Cannot apply patch - no such mapping: '%s'", mapping.c_str());
            return false;
        }

        if (it->second.Patches.empty()) {
            ERR("Cannot apply patch - mapping '%s' contains no patch entries", mapping.c_str());
            return false;
        }

        bool hasChanges = false;
        for (auto& patch : it->second.Patches) {
            hasChanges = ApplyPatch(patch) || hasChanges;
        }

        return hasChanges;
    }


    bool LibraryManager::UndoCodePatch(std::string const& mapping)
    {
        auto it = mappings_.Mappings.find(mapping);
        if (it == mappings_.Mappings.end()) {
            ERR("Cannot undo patch - no such mapping: '%s'", mapping.c_str());
            return false;
        }

        bool hasChanges = false;
        for (auto& patch : it->second.Patches) {
            hasChanges = UndoPatch(patch) || hasChanges;
        }

        return hasChanges;
    }


    bool LibraryManager::ApplyPatch(SymbolMappings::Patch& patch)
    {
        if (patch.WasApplied) {
            return true;
        }
        
        if (patch.ResolvedRef == nullptr) {
            return false;
        }

        WriteAnchor code(patch.ResolvedRef, patch.Bytes.size());
        patch.OriginalBytes.resize(patch.Bytes.size());
        memcpy(patch.OriginalBytes.data(), patch.ResolvedRef, patch.OriginalBytes.size());
        memcpy(const_cast<uint8_t*>(patch.ResolvedRef), patch.Bytes.data(), patch.Bytes.size());
        patch.WasApplied = true;
        return true;
    }


    bool LibraryManager::UndoPatch(SymbolMappings::Patch& patch)
    {
        if (!patch.WasApplied) return false;

        WriteAnchor code(patch.ResolvedRef, patch.OriginalBytes.size());
        memcpy(const_cast<uint8_t*>(patch.ResolvedRef), patch.OriginalBytes.data(), patch.OriginalBytes.size());
        patch.WasApplied = false;
        return true;
    }

#if defined(__APPLE__)
    bool LibraryManager::GetGameVersion(GameVersionInfo & version)
    {
        uint32_t major, minor, revision, build;
        if (!darwin::GetGameBundleVersion(major, minor, revision, build)) return false;
        version.Major = (uint16_t)major;
        version.Minor = (uint16_t)minor;
        version.Revision = (uint16_t)revision;
        version.Build = (uint16_t)(build & 0xffff);
        version.BundleBuild = build;
        return true;
    }
#else
    bool LibraryManager::GetGameVersion(GameVersionInfo & version)
    {
        HMODULE hGameModule = GetModuleHandleW(L"bg3.exe");
        if (hGameModule == NULL) {
            hGameModule = GetModuleHandleW(L"bg3_dx11.exe");
        }

        if (hGameModule == NULL) {
            return false;
        }

        auto hResource = FindResource(hGameModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
        if (hResource == NULL) return false;
        auto dwSize = SizeofResource(hGameModule, hResource);
        auto hData = LoadResource(hGameModule, hResource);
        if (hData == NULL) return false;
        auto pRes = LockResource(hData);
        if (pRes == NULL) return false;

        auto pResCopy = LocalAlloc(LMEM_FIXED, dwSize);
        CopyMemory(pResCopy, pRes, dwSize);

        UINT verLength;
        VS_FIXEDFILEINFO * fixedFileInfo;
        if (VerQueryValue(pResCopy, L"\\", (LPVOID*)&fixedFileInfo, &verLength) != TRUE) return false;

        version.Major = HIWORD(fixedFileInfo->dwFileVersionMS);
        version.Minor = LOWORD(fixedFileInfo->dwFileVersionMS);
        version.Revision = HIWORD(fixedFileInfo->dwFileVersionLS);
        version.Build = LOWORD(fixedFileInfo->dwFileVersionLS);

        LocalFree(pResCopy);
        FreeResource(hData);
        return true;
    }
#endif

    void LibraryManager::ShowStartupError(STDString const & msg, bool wait, bool exitGame)
    {
        ERR("STARTUP ERROR: %s", msg.c_str());

        if (GetStaticSymbols().ecl__EoCClient == nullptr
            || GetStaticSymbols().ecl__EoCClient__HandleError == nullptr
            || GetStaticSymbols().ls__GlobalAllocator__Alloc == nullptr) {
            return;
        }

        if (wait) {
            std::thread messageThread([this, msg, exitGame]() {
                unsigned retries{ 0 };
                while (!CanShowError() && retries < 1200) {
                    Sleep(100);
                    retries++;
                }

                if (retries < 1200) {
                    ShowStartupError(msg, exitGame);
                } else {
                    MessageBoxA(NULL, msg.c_str(), "Script Extender Error", 
                        MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
                }
            });
            messageThread.detach();
        } else {
            if (CanShowError()) {
                ShowStartupError(msg, exitGame);
            }
        }
    }

    void LibraryManager::ShowStartupError(STDString const & msg, bool exitGame)
    {
        if (!CanShowMessages()) return;

        // Abuse a deprecated string key for displaying custom text
        TranslatedString ts;
        ts.Handle.Handle = FixedString("h11018635g3003g46c6g8013g4630abe55cad");
        ts.Handle.Version = 1;

        // Create a new entry in the string repository text pool
        auto& texts = (*GetStaticSymbols().ls__gTranslatedStringRepository)->TranslatedStrings[0];
        auto tskRef = texts->Texts.try_get(ts.Handle);
        if (!tskRef) return;

        auto str = GameAlloc<STDString>(msg);
        texts->Strings.push_back(str);

        // Update reference to new string
        *tskRef = LSStringView(str->data(), str->size());

        GetStaticSymbols().ecl__EoCClient__HandleError(*GetStaticSymbols().ecl__EoCClient, ts, exitGame, ts, 0);
    }

    bool LibraryManager::CanShowMessages()
    {
        return GetStaticSymbols().GetClientState()
            && GetStaticSymbols().ecl__EoCClient__HandleError != nullptr
            && GetStaticSymbols().ls__GlobalAllocator__Alloc != nullptr;
    }

    bool LibraryManager::CanShowError()
    {
        if (!CanShowMessages()) return false;

        auto state = GetStaticSymbols().GetClientState();
        return state == ecl::GameState::Running
            || state == ecl::GameState::Paused
            || state == ecl::GameState::Menu
            || state == ecl::GameState::Lobby;
    }
}
