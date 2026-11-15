# Script Extender for the macOS version of Baldur's Gate 3

This is a native macOS port of [Norbyte's BG3 Script Extender](https://github.com/Norbyte/bg3se).
It builds the extender's own source code as an arm64 dylib that loads into the macOS game
(the Steam version), instead of the Windows DLL.

**Status: early, not usable for mods yet.** The extender compiles, links, loads and resolves
engine symbols in the macOS game binary, but the engine integration that mods depend on is not
verified and is switched off by default. See [What works](#what-works) before installing.

## What works

| Area | State |
|--|--|
| Build | All of the extender (CoreLib, Lua, Osiris, IMGUI and game definition code) compiles and links with Apple clang for arm64 |
| Loading | Injected with `DYLD_INSERT_LIBRARIES`; initializes at the game's `main()` |
| Function hooking | arm64 inline hook engine with instruction relocation; covered by tests |
| Engine symbols | Resolved by name from the game's symbol table; 36 of the extender's 99 engine symbols are mapped so far |
| Console / log | Console output is written to `~/Library/Application Support/BG3ScriptExtender/bg3se.log` |
| Lua, `Ext.*`, mods | **Not working yet.** Needs the engine hooks below |
| Engine hooks (game state, ECS, stats) | Experimental and **untested in game**; off unless `BG3SE_ENGINE=1` |
| Osiris (`Osi.*`, story events) | Not ported: the library's internal layouts differ on macOS |
| IMGUI overlay | Not ported: needs a Metal backend (the Windows build hooks DX11 / Vulkan) |
| Input hooks | Not ported: the macOS game does not use SDL |
| Crash reporter, auto-updater | Not ported |
| Intel Macs | Not supported: the hook engine is arm64-only |

By default the extender runs in **diagnostic mode**: it resolves engine symbols, writes what it
found to the log, and installs no hooks, so it cannot affect the game. Checked against game
version 4.1.1.7631656.

## Install

1. Download and unzip a release, or build it (below). Keep `libBG3ScriptExtender.dylib` and
   `bg3se-launch.sh` in the same folder.
2. In Steam, open the game's Properties and set **Launch Options** to:

   ```
   "/full/path/to/bg3se-launch.sh" %command%
   ```

3. Start the game from Steam, then read `~/Library/Application Support/BG3ScriptExtender/bg3se.log`.

To start it from a terminal instead (with Steam running), and see the console there:

```sh
BG3SE_CONSOLE=1 ./bg3se-launch.sh
```

To remove it, clear the launch options. Nothing inside the game's app bundle is modified.

Settings go in `~/Library/Application Support/BG3ScriptExtender/ScriptExtenderSettings.json`
and use the same keys as the Windows version.

| Environment variable | Effect |
|--|--|
| `BG3SE_CONSOLE=1` | Print the console to the terminal and accept Lua input from it |
| `BG3SE_ENGINE=1` | Bind engine symbols and install the engine hooks. Experimental: expect crashes |

## Build

Requires Xcode (or its command line tools), CMake and Python 3.

```sh
./pull-externals.sh                         # third-party sources; builds protobuf (a few minutes)
cmake -S macOS -B build/macos -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/macos -j
```

The result is `build/macos/libBG3ScriptExtender.dylib`. To run the game with it:

```sh
BG3SE_DYLIB=build/macos/libBG3ScriptExtender.dylib macOS/scripts/bg3se-launch.sh
```

Tests that need no game:

```sh
cmake --build build/macos --target SEPlatformTests SELoadTest
build/macos/SEPlatformTests                                   # hook engine, symbol lookup
build/macos/SELoadTest build/macos/libBG3ScriptExtender.dylib # startup path
macOS/scripts/check-symbols.py                                # mapped symbols vs. the installed game
```

## How the port works

The upstream sources are kept as they are wherever possible, so upstream changes stay easy to merge.

- **`macOS/Compat/`** is put first on the include path. Its `windows.h`, `detours.h`,
  `concurrent_queue.h` and friends provide the small Win32 / MSVC surface the sources use,
  implemented on POSIX and Mach APIs.
- **`macOS/Platform/`** replaces the Windows-only translation units: the dylib entry point,
  the inline hook engine behind the Detours API, Mach-O symbol lookup, and stand-ins for the
  crash reporter, the SDL input layer and the IMGUI renderer.
- **Compiler flags**: `-fms-extensions` and `-fdelayed-template-parsing` make clang accept the
  MSVC dialect the code is written in. The remaining changes to shared code are small and
  guarded (`#if defined(__APPLE__)` / `_MSC_VER`).
- **Engine symbols**: the Windows build finds engine functions by byte pattern, because
  `bg3.exe` has no symbols. The macOS game binary ships its full symbol table, so
  `macOS/Platform/SymbolMappings.xml` names the Mach-O symbol for each engine symbol instead.

### What is different on macOS

Things established from the macOS game binary so far:

- The game allocates with the system allocator (`ls::MemoryManager` wraps `malloc` / `free`).
- The global string table has the Windows layout and is reached through `ls::gst::Add / Get /
  Acquire` and `ls::gst::Map::Release`.
- Game thread ids are Mach thread ports (`pthread_mach_thread_np`).
- `ls::CriticalSection` is a custom lock, not a `pthread_mutex_t`; game structures that embed
  one (or an SRW lock) still carry Windows layouts in `GameDefinitions/` and are unverified.
- Functions returning non-trivial objects use `x8` for the result on arm64, where the Windows
  declarations pass an explicit return pointer. Each hooked or called function has to be
  checked for this before it is mapped.

### Adding an engine symbol

1. Find the symbol in the game binary (`nm -arch arm64 "…/Baldur's Gate 3" | c++filt | grep …`).
2. Compare its signature with the `*Proc` type the extender declares for it.
3. Add a `<Symbol>` line to `macOS/Platform/SymbolMappings.xml` and run
   `macOS/scripts/check-symbols.py`.

## Releasing

`macOS/scripts/release.sh` builds a Release dylib, runs the tests, signs it with a Developer ID
Application certificate, notarizes the archive with `notarytool`, and creates a GitHub release.
Pass `--no-publish` to stop after notarization. Raise `macOS/VERSION` for each release.

One-time setup: a Developer ID Application certificate in your Keychain, and notarization
credentials stored with `xcrun notarytool store-credentials bg3se-notary --apple-id … --team-id …`.
To use a profile with another name, set `BG3SE_NOTARY_PROFILE` in the environment or in
`macOS/Local.env` (git-ignored).

## License

Same as upstream: MIT with the Commons Clause, © Norbyte. See [LICENSE](../LICENSE).
