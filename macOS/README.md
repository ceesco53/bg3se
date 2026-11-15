# Script Extender for the macOS version of Baldur's Gate 3

This is a native macOS port of [Norbyte's BG3 Script Extender](https://github.com/Norbyte/bg3se).
It builds the extender's own source code as an arm64 dylib that loads into the macOS game
(the Steam version), instead of the Windows DLL.

**Status: early.** The Lua console works in the macOS game (client and server side, at the main
menu and in a loaded save), but only when the experimental engine integration is switched on, and
large parts of the API that touch game data are not verified. Mods that depend on the extender
should not be expected to work yet. See [What works](#what-works) before installing.

## What works

Checked in game against version 4.1.1.7631656 (Apple Silicon, Steam).

| Area | State |
|--|--|
| Build | All of the extender (CoreLib, Lua, Osiris, IMGUI and game definition code) compiles and links with Apple clang for arm64 |
| Loading | Injected with `DYLD_INSERT_LIBRARIES`; initializes at the game's `main()`. Works |
| Function hooking | arm64 inline hook engine with instruction relocation; covered by tests and used on live engine functions |
| Engine symbols | Resolved by name from the game's symbol table; 39 of the extender's 99 engine symbols are mapped |
| Game state tracking | Works (stage 1): client and server state changes are observed |
| Lua console | Works (stage 2): client and server Lua states are created, and commands run through the console socket at the main menu and in a loaded save |
| `Ext.Utils`, `Ext.Json`, `Ext.Math`, `Ext.Enums`, `Ext.Types` | Work as far as tested from the console |
| Entities and components (`Ext.Entity`) | **Not working**: component and system indices are not mapped on macOS yet (the log lists each one) |
| Stats, templates, resources, levels | Unverified: the symbols are mapped, the structure layouts behind them are not checked |
| Mod loading (`ScriptExtender/Config.json`, bootstrap scripts) | Unverified |
| Engine hooks (file reader, ECS update, stats; stage 3) | Untested in game |
| Osiris (`Osi.*`, story events) | Not ported: the library's internal layouts differ on macOS |
| Client/server messaging (`Ext.Net`) | Disabled: the net message factory layout is unverified |
| IMGUI overlay | Not ported: needs a Metal backend (the Windows build hooks DX11 / Vulkan) |
| Input hooks | Not ported: the macOS game does not use SDL |
| Version banner, virtual texture merging | Disabled: they write to game structures whose layout is unverified |
| Crash reporter, auto-updater | Not ported |
| Intel Macs | Not supported: the hook engine is arm64-only |

By default the extender runs in **diagnostic mode**: it resolves engine symbols, writes what it
found to the log, and installs no hooks, so it cannot affect the game. Everything beyond that is
opt-in through `BG3SE_ENGINE` (below).

## Install

1. Download and unzip a release, or build it (below). Keep `libBG3ScriptExtender.dylib` and
   `bg3se-launch.sh` in the same folder.
2. In Steam, open the game's Properties and set **Launch Options** to:

   ```
   /full/path/to/bg3se-launch.sh %command%
   ```

   The launcher has to be the first word: Steam on macOS runs it as a program, so the
   `VAR=value %command%` form used on other platforms fails with "OS Error 260".

3. Start the game from Steam, then read `~/Library/Application Support/BG3ScriptExtender/bg3se.log`.

To get the Lua console, pass the engine stage and the console switch to the launcher:

```
/full/path/to/bg3se-launch.sh --engine 2 --console %command%
```

and, once the game is running, connect from a terminal:

```sh
./bg3se-console.sh        # or macOS/scripts/bg3se-console.sh in a source checkout
```

Type Lua code, or `server` / `client` to choose which Lua state runs it, `reset` to reload, `help`
for the rest. The console is served on a Unix socket in
`~/Library/Application Support/BG3ScriptExtender/` that only your user can open. The game can also
be started from a terminal (`./bg3se-launch.sh --engine 2 --console`, with Steam running); the
console is then available directly in that terminal as well.

Steam passes the app bundle to the launcher and runs it under Rosetta; the launcher starts the
game's arm64 build regardless. (If the game shows a "Rosetta - Intel / ARM64" chooser that keeps
coming back, it was started as the Intel build: check that the launch options start with the
launcher's path.)

If the game does not start from Steam, `~/Library/Application Support/BG3ScriptExtender/launcher.log`
records what the launcher was given and why it stopped.

If the game crashes with the extender loaded, close Larian's crash reporter with **Quit without
sending**: the crash is the extender's, not the game's.

To remove it, clear the launch options. Nothing inside the game's app bundle is modified.

Settings go in `~/Library/Application Support/BG3ScriptExtender/ScriptExtenderSettings.json`
and use the same keys as the Windows version.

| Launcher option | Environment variable | Effect |
|--|--|--|
| `--console` | `BG3SE_CONSOLE=1` | Enable the console: on the terminal the game was started from, and on the console socket |
| `--engine 1` | `BG3SE_ENGINE=1` | Bind engine symbols; hook and log game state changes |
| `--engine 2` | `BG3SE_ENGINE=2` | Also run extender startup (type tables, Lua libraries) and create Lua states |
| `--engine 3` | `BG3SE_ENGINE=3` | Also install every engine hook (file reader, ECS update, stats, …) |

The engine stages are experimental and exist so that unverified parts can be switched on
one layer at a time. Stages 1 and 2 have been run in game; stage 3 has not.

## Build

Requires Xcode (or its command line tools), CMake and Python 3.

```sh
./pull-externals.sh                         # third-party sources; builds protobuf (a few minutes)
cmake -S macOS -B build/macos -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/macos -j
```

The result is `build/macos/libBG3ScriptExtender.dylib`. To run the game with it:

```sh
macOS/scripts/bg3se-launch.sh        # picks up build/macos/libBG3ScriptExtender.dylib
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
  the inline hook engine behind the Detours API, Mach-O symbol lookup, the console socket, and
  stand-ins for the crash reporter, the SDL input layer and the IMGUI renderer.
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
- `ls::STDString` is libc++'s `basic_string` with a 32-bit size type (16 bytes); the extender's
  `STDString` compiles to the same layout.
- `ecl::EoCClient`, `esv::EoCServer`, the game state machines and the game state enums match the
  Windows definitions, apart from one field missing at the start of the client object.
- Game state listeners are registered by object address (Itanium vtable order).
- The client state machine's update is inlined; `App::Update` is used as the per-frame client hook.
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
