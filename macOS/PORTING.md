# macOS port: what is left

This is the working list for the macOS port. It lives on the `macos-port` branch and is updated
in the same commit as the work it describes: when an item is done, tick it and move any facts
worth keeping into [README.md](README.md).

Last updated 2026-11-15 (entity access and fault containment), against game version 4.1.1.7631656 (Steam, Apple Silicon).

- [x] done and checked in the running game
- [ ] not done
- **(untested)** next to an item means the code is in place but has never run in the game

For how to build, launch and use the port, see [README.md](README.md).

## Where things stand

The extender's whole source builds for arm64 macOS and loads into the game. Lua runs on both the
client and the server side, driven through the console socket, at the main menu and in a loaded
save. Osiris functions that the game implements can be called (`Osi.AddGold`, `Osi.GetGold`,
`Osi.GetHostCharacter` were run in a save).

Entities can be read from Lua on both sides (`_C()`, `Ext.Entity.Get`, component fields,
`GetAllEntitiesWithComponent`), and a fault in extender code is contained as a Lua error instead
of crashing the game.

Most other game data access is still open. The extender's Windows code
describes about a thousand game structures by hand; each one a feature touches has to be checked
against the macOS binary before that feature can be switched on. So far the checked ones are the
string table, `STDString`, the path roots, the client and server singletons, the game state
machines, the file reader's first fields, the start of the mod manager, the Osiris function,
type and parameter tables, the entity world with its component and query registries, and the
engine's critical section.

## 1. Game data access

These are what most mods use, and none of them work yet.

- [x] **Entity component indices.** Bound by name from the `ls::TypeId<T, Context>::m_TypeIndex`
  symbols (2107 components, 540 one-frame components, 934 systems, 582 replicated types, 121
  static data types). The index bound this way is the registry's own index.
- [x] **Entity world layout.** `ecs::EntityWorld`, the component registry and the query registry
  match the Windows definitions once cache-line alignment is 128 bytes and critical sections have
  the engine's macOS layout; pinned with `static_assert`s. Entity storage is exercised by the
  reads below but not individually pinned.
- [x] **Reading entities from Lua.** `_C()`, `Ext.Entity.Get(guid)`, component fields,
  `GetAllComponentNames`, `GetAllEntitiesWithComponent`, on server and client, in a loaded save.
- [ ] **Component layouts, field by field.** By size, 907 of 909 server components and 722 of
  722 client components match what the game registered (checked at every start; the list is
  written to `component-layouts-*.txt` in the data directory and mismatches are disabled).
  A matching size does not prove every field is where the extender thinks it is; only a handful
  of components (health, level, stats, display name, resistances) have been read and looked right.
  - Known size mismatches, all 8 bytes off: `ServerDelayDeath`, `ServerDeathRequest`, and the
    out-of-line data of `esv::Projectile`, `esv::projectile::InitializationComponent`,
    `esv::SoundVolumeTrigger`, `esv::TimelineSceneTrigger`, `ecl::TimelineSceneTrigger`,
    `ls::Construction`, `ls::Scene`.
- [ ] **Writing components and creating or removing them** (`entity:CreateComponent`,
  `Replicate`, field assignment) **(untested)**.
- [ ] **Component events** (`Ext.Entity.Subscribe`, `OnCreate`, …). Need the entity world update
  hook (stage 3).
- [ ] **Mod list and mod scripts.** The mod manager is verified only up to the first module's
  name. Loading a mod's `ScriptExtender/Config.json` and bootstrap scripts through the game's
  file reader is **(untested)**. Needed for `Ext.Mod` and for any mod to load at all.
- [ ] **Stats.** `RPGStats::m_ptr` and the spell, status, boost and interrupt prototype managers
  are mapped, but their layouts are unverified, and `RPGStats::Load`, `ParseDataBuffers`,
  `Object::SetPropertyString`, the passive prototype manager and `ParseStaticBoosts` are not
  located. Needed for `Ext.Stats`.
- [ ] **Static data and resources.** The GUID resource manager and the texture atlas map are not
  located; the resource manager is mapped but unverified. Needed for `Ext.StaticData`,
  `Ext.Resource`.
- [ ] **Templates.** The global template bank is picked by a thread-local slot whose location on
  macOS is unknown (`GameHelpers.cpp`, returns null for now). Needed for `Ext.Template`.
- [ ] **Levels, AI grid, pathfinding.** Level managers are mapped, unverified.
  `AiGrid::FindPathImmediate` takes an extra argument on macOS and is not bound.
- [ ] **Localization.** Reading works (`Ext.Loca.GetTranslatedString` returned a character's
  name). Writing trips the extender's lock check, so the repository's lock layout is wrong; that
  is also why the version banner is disabled.

## 2. Osiris

Calls and queries that the game implements work. The rest goes through Osiris's own node and
adapter objects, which are built against libc++ on macOS and are unverified.

- [x] Function, type and parameter tables (libc++ `std::map` / `std::list` layouts)
- [x] Calls and queries implemented by the game (`FunctionType::Call`, `Query`)
- [x] Story load detection (`COsiris::Load`)
- [ ] **Event listeners** (`Ext.Osiris.RegisterListener`). They hook the virtual tables of
  Osiris node classes; the node classes and their vtable order (Itanium, not MSVC) need mapping.
  Subscriptions are accepted today but never fire.
- [ ] **Databases** (`Osi.DB_…:Get`, inserts, deletes). Needs the database, node and adapter
  factories (`ReteDBaseFactory`, `ReteNodeFactory`, `ReteAdaptorFactory`; the adapter factory's
  outer shape is confirmed), tuples and typed values, and the Osiris string table
  (`COsiStringTable::GetStr` and friends are exported).
- [ ] **Story procedures and user queries** (`PROC_…`, `QRY_…`). Same dependencies as databases.
- [ ] **Custom Osiris functions and story extension.** On Windows the extender injects its own
  functions when the story is compiled or merged, by hooking `COsiris::Compile`, `Merge` and a
  Windows file call. `Compile` and `Merge` are exported on macOS; the file hook needs a macOS
  equivalent.
- [ ] **Osiris logging and error callbacks.** Not bound; extender messages are not forwarded to
  the story log.
- [ ] **Osiris debugger.** Compiles; needs everything above.

## 3. Engine hooks (stage 3)

Stage 3 installs every hook in `GameHooks/EngineHooks.inl`. It has never been run in the game.
Each hook's handler reads game structures, so each needs its symbol, its signature and those
structures checked first.

- [ ] File reader hook (path overrides, `Ext.IO.AddPathOverride`) — symbol mapped **(untested)**
- [ ] Entity world update and command buffer flush hooks (component events, replication events)
  — symbols mapped **(untested)**; depends on section 1
- [ ] Savegame visit hook (persistent variables, `Ext.Vars`, `ModVars`) — symbol mapped **(untested)**
- [ ] Stats load hooks — `RPGStats::Load` and `ParseDataBuffers` not located
- [ ] Spell and status prototype hooks — symbols mapped **(untested)**; passive and interrupt
  prototypes not located
- [ ] Damage and functor hooks (`DealDamageFunctor::ApplyDamage`, `ThrowDamageEvent`, the nine
  `ExecuteStatsFunctor` variants) — none located. Needed for the damage and functor events.
- [ ] Pathfinding hook — `FindPath` mapped **(untested)**
- [ ] Surface action hooks — factory global and `TransformSurfaceAction::Init` mapped
  **(untested)**; `DoCreateAction` not located
- [ ] Dialog load, blendshape update, UI command queue hooks — not located
- [ ] `App` constructor and `App::UpdatePaths` — not located. Startup currently runs from the
  first game state change instead, which works; `UpdatePaths` is only needed for custom profiles.

## 4. Networking

- [ ] **The extender's network channel.** Disabled: it adds a message type to the game's net
  message factory, whose layout is unverified (it crashed on load). Needed for `Ext.Net`, for
  client/server Lua messaging, for synced user variables, and for resetting the client Lua state
  from the server.
- [ ] Multiplayer in general is untested.

## 5. Client side and UI

- [ ] **IMGUI overlay.** Needs a Metal renderer backend (the Windows build hooks DX11 and
  Vulkan). A placeholder backend is in place, so IMGUI Lua code runs but draws nothing.
- [ ] **Input.** The macOS game does not use SDL, which is what the Windows build hooks. Needs
  hooks at the AppKit or GameController level. Needed for `Ext.Input`, key events, and IMGUI input.
- [ ] **Noesis UI access** (`Ext.UI`). None of the Noesis functions or reflection tables are
  bound: several return objects by value (a different calling convention on arm64), and the
  reflection data sits behind locks whose layout is unknown. The Noesis type info the extender
  links against is a stand-in (`Platform/NoesisTypeInfo.cpp`).
- [ ] **In-game error and message boxes.** `EocClient::HandleError` is mapped **(untested)**.
- [ ] **Virtual texture merging.** Disabled: crashed reading the virtual texture manager.
- [ ] **Version banner** on the main menu. Disabled (localization lock, section 1).
- [ ] **Texture and material overrides, cursor and drag-drop access.** Globals partly mapped;
  `LoadTexture`, `UnloadTexture` not located; `TryOverrideTexture2DParameter` has an extra argument.

## 6. Cross-cutting

- [x] **Critical sections inside game structures.** `CRITICAL_SECTION` is the engine's
  `ls::CriticalSection` on macOS (20 bytes; spins, then waits on a Mach semaphore) and is locked
  through the game's own `Lock` / `Unlock`.
- [x] **Cache-line alignment.** Engine structures aligned to a cache line use 128 bytes on Apple
  Silicon (`SE_CACHE_LINE_SIZE`).
- [ ] **Reader/writer locks inside game structures.** Members declared as Windows SRW locks (and
  the extender's `SRWSpinLock`) still carry Windows layouts; what the macOS build uses in their
  place is unknown. This blocks localization writes and Noesis, and probably more.
- [ ] **Function signatures.** Each engine function still to be bound must be compared with the
  type the extender declares for it: on arm64, functions returning non-trivial objects use a
  hidden result register where the Windows declarations pass an explicit pointer.
- [ ] **Virtual classes.** Where the extender declares a game class with virtual methods and
  calls through it, the method order must follow the Itanium ABI (two destructor slots, declared
  order). Confirmed only for the game state listener so far.
- [ ] **Remaining engine symbols.** 44 of the 98 engine symbols are bound (39 in
  `Platform/SymbolMappings.xml`, 5 string table entries in code); 3 are Windows-only file calls
  and 3 are allocator entries not needed on macOS (the game uses the system allocator). The other
  48 are not bound; the ones that matter are named in the sections above.
- [ ] **Achievements with mods, launcher skip.** These patch game code on Windows
  (`ls::ModuleSettings::IsModded`, `KillLauncher`); neither has a macOS equivalent yet.

## 7. Platform and tooling

- [ ] **Intel Macs.** The hook engine is arm64-only; the launcher starts the game without the
  extender on Intel. Needs an x86_64 hook engine and a universal build.
- [x] **Fault containment.** A memory fault or failed assertion in extender code, under a Lua
  protected call or a guarded handler, is caught and reported instead of crashing the game
  (`Platform/FaultGuard.h`). Verified in game with `Ext.Debug.Crash(2)`.
- [ ] **What containment does not cover.** Faults in game code that the extender called with bad
  data, faults on threads with no guard active, and stack overflows still crash the game and
  bring up Larian's reporter. Contained faults skip destructors, so a lock held at the time stays
  held. There is no crash report upload.
- [ ] **Updater.** Not ported (the Windows updater is a separate DLL).
- [ ] **Lua debugger.** Compiles **(untested)**.
- [x] **Log noise** from unmapped components and systems: gone now that they are mapped.
- [ ] **Default stage.** The default is still diagnostic mode; the console needs
  `--engine 2 --console`. Revisit once more of stage 2 is exercised.
- [ ] **Release.** The signed and notarized archive in `build/release` predates the console and
  Osiris work. Cut a new one with `scripts/release.sh` when the state is worth publishing.
- [ ] **Case-sensitive volumes.** `Compat/` relies on the default case-insensitive file system
  (`windows.h` also answers to `Windows.h`).
- [ ] **Windows build.** The shared-code changes are guarded, but the Windows build has not been
  rebuilt since they were made.
- [ ] **Upstream.** No merge from Norbyte's repository has been done since the port started.

## How to take an item

1. Find the symbol: `nm -arch arm64 "…/Baldur's Gate 3" | c++filt | grep Name`. Add it to
   `Platform/SymbolMappings.xml` only after comparing its signature with the extender's `*Proc`
   type, then run `scripts/check-symbols.py`.
2. Check the structures it touches against the live game: start the game, attach `lldb`, and
   read the object with `memory read -fA -s8` (vtable pointers are shown by name, which makes
   fields easy to line up). Record what you confirm as a `static_assert` next to the definition.
3. Switch the feature on behind the lowest stage it is safe at, and run it: main menu first,
   then a loaded save. Crashes are in `~/Library/Logs/DiagnosticReports/Baldur's Gate 3-*.ips`.
4. Update this file and the table in [README.md](README.md) in the same commit.
