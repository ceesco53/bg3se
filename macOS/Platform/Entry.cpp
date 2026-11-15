// Entry point of the macOS build (the counterpart of Extender/Shared/dllmain.cpp).
//
// The dylib is loaded into the game with DYLD_INSERT_LIBRARIES, so this constructor runs before
// the game's own initializers and before main(), with every image the game links against already
// mapped. That is the same point in startup at which DllMain runs on Windows.
#include "stdafx.h"
#include <Extender/Shared/ExtenderConfig.h>
#include <Extender/ScriptExtender.h>
#include <Extender/Version.h>
#include <CoreLib/JsonLibs.h>
#include <sstream>
#include <fstream>
#include <Extender/Shared/ExtenderConfig.inl>

#include <dlfcn.h>
#include <mach-o/dyld.h>

using namespace bg3se;

namespace {

bool IsGameProcess()
{
    // DYLD_INSERT_LIBRARIES is inherited by every child process (crash reporters, helpers, ...)
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return false;
    return strstr(path, "Baldur's Gate 3.app/Contents/MacOS/Baldur's Gate 3") != nullptr;
}

void SetupScriptExtender(bool testing = false)
{
    Dl_info self;
    if (dladdr(reinterpret_cast<void const*>(&SetupScriptExtender), &self)) {
        gCoreLibPlatformInterface.ThisModule = self.dli_fbase;
    }

    gExtender = std::make_unique<ScriptExtender>();
    auto& config = gExtender->GetConfig();

    // Settings live in the extender's data directory; a copy next to the game executable
    // (the Windows location) takes precedence if present.
    auto dataDir = darwin::GetExtenderDataDir();
    LoadConfig(FromStdUTF8(dataDir + "/ScriptExtenderSettings.json"), config);
    LoadConfig(FromStdUTF8(darwin::GetGameExecutableDir() + "/ScriptExtenderSettings.json"), config);

    // Enable console if shift is down when the game is launched
    if (GetAsyncKeyState(VK_SHIFT) < 0 || getenv("BG3SE_CONSOLE") != nullptr) {
        config.CreateConsole = true;
    }

    if (config.CreateConsole) {
        gCoreLibPlatformInterface.GlobalConsole->Create();
        darwin::StartConsoleSocket();
    }

    // A game started from Steam or Finder has no visible stdout, so the console is always
    // mirrored to a log file (the previous run's log is kept alongside it).
    // (tests get their own file so that they never rotate the log of a running game)
    auto logPath = dataDir + (testing ? "/bg3se-test.log" : "/bg3se.log");
    if (testing) {
        unlink(logPath.c_str());
    } else {
        rename(logPath.c_str(), (dataDir + "/bg3se.previous.log").c_str());
    }
    gCoreLibPlatformInterface.GlobalConsole->OpenLogFile(FromStdUTF8(logPath));
    INFO("BG3 Script Extender v%d for macOS (%s), built on " __DATE__ " " __TIME__, CurrentVersion,
#if defined(__aarch64__)
        "arm64"
#else
        "x86_64"
#endif
    );

    if (config.DebugFlags == 0) {
        // Disabled: DF_FunctionList, DF_NodeList ,DF_LogSuccessfulFacts, DF_LogFailedFacts, DB_LogFactFailures, DF_DumpDatabases, DF_DebugFacts, DF_LogRuleFailures
        config.DebugFlags = DF_DebugTrace | DF_SuppressInitLog;
    }

    gExtender->Initialize();
}

using MainProc = int (int argc, char** argv, char** envp, char** apple);
MainProc* gOriginalMain{ nullptr };

int MainHook(int argc, char** argv, char** envp, char** apple)
{
    SetupScriptExtender();
    return gOriginalMain(argc, argv, envp, apple);
}

// Runs when dyld loads the dylib. Nothing is initialized here: this can run before the C++
// static initializers of the rest of the extender (Windows guarantees the opposite order for
// DllMain). Instead the game's main() is hooked, and setup happens when it is entered - after
// every static initializer in the process, and before the game creates its application object.
__attribute__((constructor))
void ExtenderLoaded()
{
    if (!IsGameProcess()) return;

    // Don't pass the injection on to processes the game spawns
    unsetenv("DYLD_INSERT_LIBRARIES");

    auto entry = darwin::GetMainEntryPoint();
    if (entry == nullptr || !darwin::InstallHook(entry, reinterpret_cast<void*>(&MainHook), reinterpret_cast<void**>(&gOriginalMain))) {
        fprintf(stderr, "[BG3SE] Could not hook the game's entry point; the Script Extender is not active.\n");
    }
}

}

// Runs the startup path in a process that loaded the dylib with dlopen() (see macOS/Tests)
extern "C" __attribute__((visibility("default"))) void BG3SEInitializeForTesting()
{
    SetupScriptExtender(true);
}
