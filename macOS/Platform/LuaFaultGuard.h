// Force-included into every file of the Lua library on macOS (see macOS/CMakeLists.txt).
//
// Lua runs each protected call (lua_pcall, lua_resume, lua_load, ...) through
// luaD_rawrunprotected(), whose LUAI_TRY macro is the try/catch that Lua errors land in. This
// version also arms a FaultGuard there, so that a memory fault in extender code called from Lua
// surfaces as an ordinary Lua error at that same boundary instead of crashing the game. Lua's
// normal error recovery then puts the Lua state back in order.
#pragma once

#if defined(__cplusplus)

#include "FaultGuard.h"

struct lua_State;

// Pushes the error message describing the fault onto L (implemented in LuaFaultSupport.cpp)
void BG3SELuaFaultToError(lua_State* L, bg3se::darwin::FaultGuard const& guard);

#define LUAI_THROW(L,c) throw(c)

#define LUAI_TRY(L,c,a) \
    try { \
        bg3se::darwin::FaultGuard bg3seFaultGuard; \
        if (sigsetjmp(bg3seFaultGuard.Buf, 0) == 0) { \
            a \
        } else { \
            BG3SELuaFaultToError(L, bg3seFaultGuard); \
            if ((c)->status == 0) (c)->status = LUA_ERRRUN; \
        } \
    } catch(...) { if ((c)->status == 0) (c)->status = -1; }

#define luai_jmpbuf int  /* dummy variable */

#endif
