// Crash reporting hooks for the macOS build.
//
// The Windows build installs an unhandled-exception filter that writes minidumps and uploads them
// to the extender's crash server. Neither minidumps nor that upload path exist on macOS; crashes
// are reported by the system (~/Library/Logs/DiagnosticReports) instead.
#include "stdafx.h"
#include <CoreLib/Wrappers.h>

BEGIN_SE()

std::unordered_set<void const*> gRegisteredTrampolines;
std::atomic<uint32_t> gDisableCrashReportingCount{ 0 };

void InitCrashReporting()
{}

void ShutdownCrashReporting()
{}

LONG HandleGuardedException(DWORD code, EXCEPTION_POINTERS* info)
{
    return EXCEPTION_CONTINUE_SEARCH;
}

END_SE()
