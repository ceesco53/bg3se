// Turns a contained fault into a Lua error (see LuaFaultGuard.h).
#include "stdafx.h"
#include <macOS/Platform/FaultGuard.h>
#include <lua.h>

void BG3SELuaFaultToError(lua_State* L, bg3se::darwin::FaultGuard const& guard)
{
    using namespace bg3se;
    char description[768];
    bg3se::darwin::DescribeFault(guard, description, sizeof(description));

    ERR("Extender fault contained: %s", description);
    ERR("The Lua call was aborted. Extender state may be inconsistent; if things misbehave, save and restart the game.");

    // The message becomes the error value of the protected call that was running
    lua_pushfstring(L, "Extender fault: %s", description);
}
