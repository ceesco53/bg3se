#pragma once

#include <CoreLib/Base/Base.h>
#include <CoreLib/Utils.h>
// Needed for enum definitions
#include <NsGui/GridLength.h>
#include <GameDefinitions/Base/TypeId.h>
#include <GameDefinitions/Enumerations.h>
#include <GameDefinitions/Base/ForwardDeclarations.h>

// The game has a jank SEH handler that silently swallows STATUS_CPP_EH_EXCEPTION without passing it
// to the top-level handler, so we need to have additional checks at the common SE entry points
#if defined(_WIN32)
#define BEGIN_GUARDED() __try {
#define END_GUARDED() } __except (HandleGuardedException(GetExceptionCode(), GetExceptionInformation())) {}
#elif defined(__APPLE__)
// No SEH here. A fault in extender code inside the guarded block is caught by the extender's
// signal handler and execution continues after the block (see macOS/Platform/FaultGuard.h).
#include <macOS/Platform/FaultGuard.h>
#define BEGIN_GUARDED() { ::bg3se::darwin::FaultGuard bg3seFaultGuard; if (sigsetjmp(bg3seFaultGuard.Buf, 0) == 0) {
#define END_GUARDED() } else { ::bg3se::ReportGuardedFault(bg3seFaultGuard, __func__); } }
#else
#define BEGIN_GUARDED() {
#define END_GUARDED() }
#endif

// Alignment the engine uses for structures that must not share a cache line.
// Apple Silicon has 128-byte cache lines; verified against ecs::EntityWorld in the macOS game.
#if defined(__APPLE__) && defined(__aarch64__)
#define SE_CACHE_LINE_SIZE 128
#else
#define SE_CACHE_LINE_SIZE 64
#endif

BEGIN_SE()
LONG HandleGuardedException(DWORD code, EXCEPTION_POINTERS* info);
#if defined(__APPLE__)
void ReportGuardedFault(darwin::FaultGuard const& guard, char const* where);
#endif
END_SE()

#include <NsCore/Symbol.h>
#include <NsCore/String.h>
#include <NsCore/Vector.h>
#include <NsCore/Interface.h>
#include <NsMath/Vector.h>
#include <NsDrawing/Color.h>
#include <NsDrawing/Point.h>
#include <NsDrawing/Rect.h>
#include <NsDrawing/Thickness.h>
#include <NsDrawing/CornerRadius.h>
#include <NsGui/BaseCollection.h>

#include <Extender/Client/IMGUI/IMGUIShared.h>

#include <GameDefinitions/Base/TypeMetadata.h>
#include <GameDefinitions/Base/LuaAnnotations.h>
#include <GameDefinitions/Base/BaseTypeInformation.h>
#include <GameDefinitions/Base/Lock.h>
#include <GameDefinitions/Base/CommonTypes.h>
#include <GameDefinitions/Base/ExposedTypes.h>
#include <GameDefinitions/Base/Genome.h>
#include <GameDefinitions/Base/Serialization.h>
#include <GameDefinitions/TranslatedString.h>
#include <GameDefinitions/GlobalFixedStrings.h>
#include <GameHooks/EngineHooksFwdDecl.h>

#define DEFINE_COMPONENT(componentType, cls) \
    static constexpr ExtComponentType ComponentType = ExtComponentType::componentType; \
    static constexpr auto ComponentName = #componentType; \
    static constexpr auto EngineClass = cls; \
    static constexpr auto OneFrame = false;

#define DEFINE_PROXY_COMPONENT(componentType, cls) \
    static constexpr ExtComponentType ComponentType = ExtComponentType::componentType; \
    static constexpr auto ComponentName = #componentType; \
    static constexpr auto EngineClass = cls; \
    static constexpr auto OneFrame = false; \
    static constexpr auto ForceProxy = true;

#define DEFINE_ONEFRAME_COMPONENT(componentType, cls) \
    static constexpr ExtComponentType ComponentType = ExtComponentType::componentType; \
    static constexpr auto ComponentName = #componentType; \
    static constexpr auto EngineClass = cls; \
    static constexpr auto OneFrame = true;

#define DEFINE_TAG_COMPONENT(ns, name, componentType) \
    struct name : public BaseComponent \
    { \
        static constexpr ExtComponentType ComponentType = ExtComponentType::componentType; \
        static constexpr auto ComponentName = #componentType; \
        static constexpr auto EngineClass = #ns "::" #name; \
        static constexpr auto OneFrame = false; \
        uint8_t Dummy; \
    };

#define DEFINE_ONEFRAME_TAG_COMPONENT(ns, name, componentType) \
    struct name : public BaseComponent \
    { \
        static constexpr ExtComponentType ComponentType = ExtComponentType::componentType; \
        static constexpr auto ComponentName = #componentType; \
        static constexpr auto EngineClass = #ns "::" #name; \
        static constexpr auto OneFrame = true; \
        uint8_t Dummy; \
    };

#define DEFN_BOOST(name, boostType, defn) \
    struct name##BoostComponent_Base : public BaseComponent \
    defn; \
    struct name##BoostComponent : public name##BoostComponent_Base \
    { \
        static constexpr ExtComponentType ComponentType = ExtComponentType::name##Boost; \
        static constexpr auto ComponentName = #name "Boost"; \
        static constexpr auto BoostType = BoostType::boostType; \
        static constexpr auto EngineClass = "eoc::" #name "BoostComponent"; \
        static constexpr auto OneFrame = false; \
    };

#define DEFINE_SYSTEM(systemType, cls) \
    static constexpr auto SystemType = ExtSystemType::systemType; \
    static constexpr auto SystemName = #systemType; \
    static constexpr auto EngineClass = cls;

BEGIN_SE()

enum class ContextType
{
    None,
    Server,
    Client
};

END_SE()
