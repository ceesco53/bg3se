// Input layer for the macOS build (replaces Extender/Client/SDL.cpp).
//
// On Windows the game drives input through SDL2, and the extender hooks SDL_PollEvent to observe
// and inject events. The macOS game does not link SDL at all (it uses AppKit / GameController
// directly), so there is nothing to hook: input observation and injection are not available yet.
// The SDL event *types* are still used by the Lua input API, which is why the headers remain.
#include <stdafx.h>
#include <Extender/Client/SDLManager.h>

BEGIN_SE()

SDLManager::SDLManager() {}
SDLManager::~SDLManager() {}

void SDLManager::EnableHooks() {}
void SDLManager::DisableHooks() {}
void SDLManager::InitializeUI() {}
void SDLManager::DestroyUI() {}
void SDLManager::NewFrame() {}

void SDLManager::InjectEvent(SDL_Event const& evt)
{
    WARN_ONCE("Input event injection is not supported on macOS yet");
}

END_SE()

// Timestamp source for synthesized SDL events
extern "C" Uint32 SDL_GetTicks(void)
{
    return static_cast<Uint32>(GetTickCount64());
}
