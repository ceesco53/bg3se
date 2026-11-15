// Microsoft Detours API surface, implemented by the macOS inline-hook engine
// (macOS/Platform/Hooking.cpp). Only the calls the extender uses are provided.
#pragma once

#include "windows.h"

struct DETOUR_TRAMPOLINE;
typedef DETOUR_TRAMPOLINE* PDETOUR_TRAMPOLINE;

LONG DetourTransactionBegin();
LONG DetourTransactionCommit();
LONG DetourTransactionAbort();
LONG DetourUpdateThread(HANDLE thread);

// *ppPointer holds the address of the function to hook; on success it is updated to point at a
// trampoline that calls the original, unhooked function.
LONG DetourAttach(PVOID* ppPointer, PVOID pDetour);
LONG DetourAttachEx(PVOID* ppPointer, PVOID pDetour, PDETOUR_TRAMPOLINE* ppRealTrampoline, PVOID* ppRealTarget, PVOID* ppRealDetour);
LONG DetourDetach(PVOID* ppPointer, PVOID pDetour);
