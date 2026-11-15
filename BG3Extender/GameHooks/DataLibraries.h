#pragma once

#include <CoreLib/SymbolMapper.h>
#include <CoreLib/Wrappers.h>
#include <optional>

namespace bg3se {

    struct GameVersionInfo
    {
        uint16_t Major{ 0 }, Minor{ 0 }, Revision{ 0 }, Build{ 0 };

#if defined(__APPLE__)
        // The macOS game is versioned by its bundle (CFBundleVersion, e.g. 4.1.1.7631656), not by
        // the Windows file version. The build number doesn't fit Build, so it is kept separately.
        uint32_t BundleBuild{ 0 };

        inline bool IsSupported() const
        {
            return Major == 4 && Minor >= 1;
        }
#else
        inline bool IsSupported() const
        {
            // We need v4.69 or later for game
            return (Major == 4 && ((Minor == 69 && Revision >= 95) || (Minor > 69)));
        }
#endif
    };

    class LibraryManager
    {
    public:
        LibraryManager();
        bool FindLibraries(uint32_t gameRevision);
#if defined(__APPLE__)
        // Resolves engine symbols by name from the game's Mach-O symbol table
        bool FindLibrariesDarwin();
        // Symbols were resolved but deliberately left unbound (see FindLibrariesDarwin)
        bool DiagnosticMode{ false };
#endif
        bool PostStartupFindLibraries();
        void ApplyCodePatches();
        bool GetGameVersion(GameVersionInfo & version);
        HMODULE GetAppHandle();

        void ShowStartupError(STDString const & msg, bool wait, bool exitGame);
        void ShowStartupError(STDString const & msg, bool exitGame);

        bool WasPatchApplied(std::string const& mapping);
        bool ApplyCodePatch(std::string const& mapping);
        bool UndoCodePatch(std::string const& mapping);

        inline bool CriticalInitializationFailed() const
        {
            return CriticalInitFailed;
        }

        inline bool InitializationFailed() const
        {
            return InitFailed;
        }

        inline bool IsDX11() const
        {
            return DX11;
        }

        inline SymbolMapper& Mapper()
        {
            return symbolMapper_;
        }

    private:
        void PreRegisterLibraries(SymbolMappingLoader& loader);
        void RegisterLibraries(SymbolMapper& mapper);
        void RegisterSymbols();
        bool BindApp();
        SymbolMapper::MappingResult BindECSContext(uint8_t const*);
        SymbolMapper::MappingResult BindECSIndex(uint8_t const*);
        SymbolMapper::MappingResult BindECSStaticStringConstructor(uint8_t const*);
        SymbolMapper::MappingResult BindECSStaticRegistrant(uint8_t const*);
        SymbolMapper::MappingResult BindComponentReplicationIDRef(uint8_t const*);

        bool CanShowError();
        bool CanShowMessages();

        bool ApplyPatch(SymbolMappings::Patch& patch);
        bool UndoPatch(SymbolMappings::Patch& patch);

        SymbolMappings mappings_;
        SymbolMapper symbolMapper_;
        SymbolMapper::ModuleInfo appModule_;

        bool InitFailed{ false };
        bool CriticalInitFailed{ false };
        bool PostLoaded{ false };
        bool EnabledCustomStats{ false };
        bool DX11{ false };
    };
}
