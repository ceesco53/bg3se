// Containment of faults in the extender's own code.
//
// A bad pointer in extender code (typically a game structure whose macOS layout is not what the
// extender expects) would otherwise take the whole game down. While a FaultGuard is alive on a
// thread, a SIGSEGV / SIGBUS / SIGTRAP raised by extender code on that thread is caught and
// control returns to the guard's sigsetjmp point, where the caller reports it and carries on:
//
//     darwin::FaultGuard guard;
//     if (sigsetjmp(guard.Buf, 0) == 0) {
//         ... code that may fault ...
//     } else {
//         ... guard.Signal / FaultAddress / FaultPC describe what happened ...
//     }
//
// This is damage limitation, not recovery: the frames between the fault and the guard are
// abandoned without running destructors, so locks they held stay held and their state is
// whatever it was. Faults in the game's own code are never contained; they go to the game's
// crash handler exactly as before.
//
// This header has no dependencies so that the Lua library can include it (see LuaFaultGuard.h).
#pragma once

#include <setjmp.h>

namespace bg3se::darwin {

struct FaultGuard
{
    sigjmp_buf Buf;
    FaultGuard* Previous{ nullptr };
    int Signal{ 0 };
    void* FaultAddress{ nullptr };
    void* FaultPC{ nullptr };

    FaultGuard();
    ~FaultGuard();
    FaultGuard(FaultGuard const&) = delete;
    FaultGuard& operator = (FaultGuard const&) = delete;
};

// Puts the extender's handler in front of whatever is installed for SIGSEGV, SIGBUS and SIGTRAP
// (the game installs its crash reporter's handlers during startup). Safe to call repeatedly.
void InstallFaultHandler();

// "SIGSEGV at address 0x10 in bg3se::Foo() + 24"
void DescribeFault(FaultGuard const& guard, char* buffer, unsigned long size);

// Number of faults contained since startup
unsigned ContainedFaultCount();

}
