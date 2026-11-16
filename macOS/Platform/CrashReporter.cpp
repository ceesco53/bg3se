// Crash handling for the macOS build.
//
// The Windows build installs an unhandled-exception filter that writes minidumps and uploads them
// to the extender's crash server. Neither minidumps nor that upload path exist on macOS. Instead,
// faults in the extender's own code are contained where possible (FaultGuard.h); anything else is
// left to the game's crash reporter and the system (~/Library/Logs/DiagnosticReports).
#include "stdafx.h"
#include <CoreLib/Wrappers.h>

BEGIN_SE()

std::unordered_set<void const*> gRegisteredTrampolines;
std::atomic<uint32_t> gDisableCrashReportingCount{ 0 };

void InitCrashReporting()
{
    // Called at several points during startup; (re)asserts the extender's fault handler in front
    // of the game's crash reporter, which installs its own handlers while the game starts.
    darwin::InstallFaultHandler();
}

void ReportGuardedFault(darwin::FaultGuard const& guard, char const* where)
{
    char description[768];
    darwin::DescribeFault(guard, description, sizeof(description));
    ERR("Extender fault contained in %s: %s", where, description);
    ERR("The rest of that handler was skipped. Extender state may be inconsistent; if things misbehave, save and restart the game.");
}

void ShutdownCrashReporting()
{}

LONG HandleGuardedException(DWORD code, EXCEPTION_POINTERS* info)
{
    return EXCEPTION_CONTINUE_SEARCH;
}

END_SE()
