// IMGUI rendering backend for the macOS build.
//
// The Windows build draws the extender's IMGUI overlay by hooking the game's DX11 / Vulkan
// swap chain. The macOS game renders with Metal; a Metal backend (hooking -[CAMetalLayer nextDrawable]
// / presentDrawable) is not implemented yet, so this backend reports itself as uninitialized and
// the overlay stays disabled. Lua code that builds IMGUI windows still runs; nothing is drawn.
#pragma once

#include <GameDefinitions/Base/Base.h>
#include <Extender/Client/IMGUI/Backends.h>
#include <Extender/ScriptExtender.h>
#include <CoreLib/Wrappers.h>
#include <imgui.h>
#include <imgui_internal.h>

BEGIN_NS(extui)

class NullBackend : public RenderingBackend
{
public:
    NullBackend(IMGUIManager& ui) {}
    ~NullBackend() override {}

    void EnableHooks() override {}
    void DisableHooks() override {}
    void InitializeUI() override {}
    void DestroyUI() override {}
    void NewFrame() override {}
    void FinishFrame() override {}
    void ClearFrame() override {}

    std::optional<TextureLoadResult> RegisterTexture(TextureDescriptor* descriptor) override
    {
        return {};
    }

    void UnregisterTexture(TextureOpaqueHandle opaqueHandle) override {}

    std::optional<ImTextureID> BindTexture(TextureOpaqueHandle opaqueHandle) override
    {
        return {};
    }

    bool IsInitialized() override
    {
        return false;
    }

    glm::ivec2 GetViewportSize() override
    {
        return glm::ivec2(0, 0);
    }
};

END_NS()
