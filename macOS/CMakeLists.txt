# macOS build of the BG3 Script Extender.
#
#   ./pull-externals.sh
#   cmake -S macOS -B build/macos -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build/macos -j
#
# Produces build/macos/libBG3ScriptExtender.dylib, which is loaded into the game with
# DYLD_INSERT_LIBRARIES (see macOS/README.md).
cmake_minimum_required(VERSION 3.24)

# Only the game's arm64 slice is supported for now: the hook engine is arm64-only.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
# std::format with floating point arguments needs the libc++ shipped with macOS 13.3
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "")

project(BG3ScriptExtender LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

get_filename_component(SE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SE_EXT "${SE_ROOT}/External")

if(NOT EXISTS "${SE_EXT}/lua/src/lua.h")
    message(FATAL_ERROR "External dependencies are missing; run ./pull-externals.sh first")
endif()

# The upstream sources are written for MSVC. -fms-extensions provides its keywords and builtins,
# -fdelayed-template-parsing its (non two-phase) template name lookup.
set(SE_COMPILE_OPTIONS
    -fms-extensions
    -fdelayed-template-parsing
    -fno-strict-aliasing
    -Wno-everything
)

set(SE_DEFINITIONS
    NDEBUG
    ENABLE_IMGUI
    ENABLE_UI
    NS_STATIC_LIBRARY
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    OSI_EOCAPP
    OSI_EXTENSION_BUILD
    GLM_ENABLE_EXPERIMENTAL
    # Makes GLM enable aligned types exactly as it does under MSVC (see CoreLib/Base/BaseTypes.h)
    _MSC_EXTENSIONS=1
)

set(SE_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/Compat"
    "${SE_ROOT}"
    "${SE_EXT}"
    "${SE_EXT}/glm"
    "${SE_EXT}/lua/src"
    "${SE_EXT}/rapidjson/include"
    "${SE_EXT}/imgui"
    "${SE_EXT}/NoesisGUI-NativeSDK-win-3.1.7-Indie/Include"
    "${SE_EXT}/tinycrypt/lib/include"
    "${SE_EXT}/SDL/include"
    "${SE_EXT}/optick/src"
    "${SE_EXT}/protobuf/src"
    "${SE_EXT}/protobuf-build/_deps/absl-src"
    "${SE_EXT}/protobuf/third_party/utf8_range"
)

# ---------------------------------------------------------------------------------------------
# Generated sources
# ---------------------------------------------------------------------------------------------

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SE_PROTOC "${SE_EXT}/protobuf-build/protoc")
set(SE_PROTOS
    Extender/Shared/ExtenderProtocol
    Lua/Debugger/LuaDebug
    Osiris/Debugger/osidebug
)
set(SE_PROTO_SOURCES)
foreach(proto ${SE_PROTOS})
    add_custom_command(
        OUTPUT "${SE_ROOT}/BG3Extender/${proto}.pb.cc" "${SE_ROOT}/BG3Extender/${proto}.pb.h"
        COMMAND "${SE_PROTOC}" -I. "-I${SE_EXT}/protobuf/src" --cpp_out=. "${proto}.proto"
        DEPENDS "${SE_ROOT}/BG3Extender/${proto}.proto"
        WORKING_DIRECTORY "${SE_ROOT}/BG3Extender"
        VERBATIM)
    list(APPEND SE_PROTO_SOURCES "${SE_ROOT}/BG3Extender/${proto}.pb.cc")
endforeach()

# Enumeration, event and property map tables are generated from the game definition headers
file(GLOB_RECURSE SE_DEFINITION_HEADERS CONFIGURE_DEPENDS
    "${SE_ROOT}/BG3Extender/GameDefinitions/*.h"
    "${SE_ROOT}/BG3Extender/GameDefinitions/*.inl"
    "${SE_ROOT}/BG3Extender/Lua/*.h"
    "${SE_ROOT}/BG3Extender/Extender/*.h")
list(FILTER SE_DEFINITION_HEADERS EXCLUDE REGEX "/Generated/")
set(SE_GENERATED_STAMP "${CMAKE_CURRENT_BINARY_DIR}/generated.stamp")
add_custom_command(
    OUTPUT "${SE_GENERATED_STAMP}"
    COMMAND "${Python3_EXECUTABLE}" make_enumerations.py
    COMMAND "${Python3_EXECUTABLE}" make_events.py
    COMMAND "${Python3_EXECUTABLE}" make_property_map.py
    COMMAND "${CMAKE_COMMAND}" -E touch "${SE_GENERATED_STAMP}"
    DEPENDS ${SE_DEFINITION_HEADERS}
    WORKING_DIRECTORY "${SE_ROOT}/BG3Extender"
    VERBATIM)
add_custom_target(SEGenerated DEPENDS "${SE_GENERATED_STAMP}" ${SE_PROTO_SOURCES})

# Resources that the Windows build embeds as PE resources (ids from BG3Extender/resource.h)
file(GLOB_RECURSE SE_LUA_SCRIPTS CONFIGURE_DEPENDS "${SE_ROOT}/BG3Extender/LuaScripts/*")
set(SE_LUA_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/LuaScripts.bundle")
add_custom_command(
    OUTPUT "${SE_LUA_BUNDLE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make-lua-bundle.py"
            "${SE_ROOT}/BG3Extender/LuaScripts" "${SE_LUA_BUNDLE}"
    DEPENDS ${SE_LUA_SCRIPTS} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make-lua-bundle.py"
    VERBATIM)
add_custom_target(SEResources DEPENDS "${SE_LUA_BUNDLE}")
set(SE_SYMBOL_MAPPINGS "${CMAKE_CURRENT_SOURCE_DIR}/Platform/SymbolMappings.xml")

# ---------------------------------------------------------------------------------------------
# Third-party libraries
# ---------------------------------------------------------------------------------------------

# Lua is built as C++ (as on Windows) so that errors unwind with exceptions
file(GLOB LUA_SOURCES "${SE_EXT}/lua/src/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(LuaLib STATIC ${LUA_SOURCES})
target_compile_options(LuaLib PRIVATE -Wno-everything -x c++)
target_compile_definitions(LuaLib PRIVATE NDEBUG LUA_USE_MACOSX)
target_include_directories(LuaLib PUBLIC "${SE_EXT}/lua/src")

add_library(imgui STATIC
    "${SE_EXT}/imgui/imgui.cpp"
    "${SE_EXT}/imgui/imgui_demo.cpp"
    "${SE_EXT}/imgui/imgui_draw.cpp"
    "${SE_EXT}/imgui/imgui_tables.cpp"
    "${SE_EXT}/imgui/imgui_widgets.cpp")
target_compile_options(imgui PRIVATE -Wno-everything)
target_compile_definitions(imgui PRIVATE NDEBUG IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_include_directories(imgui PUBLIC "${SE_EXT}/imgui")

add_library(SEThirdParty STATIC
    "${SE_EXT}/FastLZ/fastlz.c"
    "${SE_EXT}/lz4/lib/lz4.c"
    "${SE_EXT}/tinycrypt/lib/source/ecc.c"
    "${SE_EXT}/tinycrypt/lib/source/ecc_dh.c"
    "${SE_EXT}/tinycrypt/lib/source/ecc_dsa.c"
    "${SE_EXT}/tinycrypt/lib/source/ecc_platform_specific.c"
    "${SE_EXT}/tinycrypt/lib/source/sha256.c"
    "${SE_EXT}/tinycrypt/lib/source/utils.c")
target_compile_options(SEThirdParty PRIVATE -Wno-everything)
target_include_directories(SEThirdParty PRIVATE "${SE_EXT}/tinycrypt/lib/include")

# ---------------------------------------------------------------------------------------------
# CoreLib
# ---------------------------------------------------------------------------------------------

add_library(CoreLib STATIC
    "${SE_ROOT}/CoreLib/Base/Base.cpp"
    "${SE_ROOT}/CoreLib/Console.cpp"
    "${SE_ROOT}/CoreLib/Crypto.cpp"
    "${SE_ROOT}/CoreLib/MurmurHash3.cpp"
    "${SE_ROOT}/CoreLib/SymbolMapper.cpp"
    "${SE_ROOT}/CoreLib/tinyxml2.cpp"
    "${SE_ROOT}/CoreLib/Utils.cpp")
target_compile_options(CoreLib PRIVATE ${SE_COMPILE_OPTIONS})
target_compile_definitions(CoreLib PRIVATE ${SE_DEFINITIONS})
target_include_directories(CoreLib PRIVATE "${SE_ROOT}/CoreLib" ${SE_INCLUDES})
target_precompile_headers(CoreLib PRIVATE "${SE_ROOT}/CoreLib/stdafx.h")

# ---------------------------------------------------------------------------------------------
# The extender
# ---------------------------------------------------------------------------------------------

# Shared sources: the translation units of BG3Extender.vcxproj minus the Windows-only ones
# (dllmain, DWriteWrapper, CrashReporter, SDL), which Platform/ replaces.
set(SE_SOURCES
    ${SE_ROOT}/BG3Extender/Extender/Client/ExtensionStateClient.cpp
    ${SE_ROOT}/BG3Extender/Extender/Client/ScriptExtenderClient.cpp
    ${SE_ROOT}/BG3Extender/Extender/ScriptExtender.cpp
    ${SE_ROOT}/BG3Extender/Extender/Server/ScriptExtenderServer.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/Console.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/ExtenderNet.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/ExtenderProtocol.pb.cc
    ${SE_ROOT}/BG3Extender/Extender/Shared/ExtensionState.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/Hooks.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/ScriptHelpers.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/tinyxml2.cpp
    ${SE_ROOT}/BG3Extender/Extender/Shared/Utils.cpp
    ${SE_ROOT}/BG3Extender/Extender/Client/IMGUI/IMGUI.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/Base/Base.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/EntitySystem.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/Enumerations.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/GameDefinitionHelpers.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/GameHelpers.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/GlobalFixedStrings.cpp
    ${SE_ROOT}/BG3Extender/GameDefinitions/Stats/Stats.cpp
    ${SE_ROOT}/BG3Extender/GameHooks/DataLibraries.cpp
    ${SE_ROOT}/BG3Extender/GameHooks/DataLibrariesBG3Game.cpp
    ${SE_ROOT}/BG3Extender/GameHooks/EngineHooks.cpp
    ${SE_ROOT}/BG3Extender/GameHooks/OsirisWrappers.cpp
    ${SE_ROOT}/BG3Extender/Lua/Client/LuaClient.cpp
    ${SE_ROOT}/BG3Extender/Lua/Debugger/LuaDebug.pb.cc
    ${SE_ROOT}/BG3Extender/Lua/Debugger/LuaDebugger.cpp
    ${SE_ROOT}/BG3Extender/Lua/Debugger/LuaDebugMessages.cpp
    ${SE_ROOT}/BG3Extender/Lua/Libs/LuaSharedLibs.cpp
    ${SE_ROOT}/BG3Extender/Lua/LuaBinding.cpp
    ${SE_ROOT}/BG3Extender/Lua/LuaSerializers.cpp
    ${SE_ROOT}/BG3Extender/Lua/Osiris/LuaOsirisBinding.cpp
    ${SE_ROOT}/BG3Extender/Lua/Server/LuaServer.cpp
    ${SE_ROOT}/BG3Extender/Lua/Shared/LuaBundle.cpp
    ${SE_ROOT}/BG3Extender/Lua/Shared/LuaInternalHelpers.cpp
    ${SE_ROOT}/BG3Extender/Lua/Shared/Proxies/LuaEventRegistrations.cpp
    ${SE_ROOT}/BG3Extender/Lua/Shared/Proxies/LuaObjectProxies.cpp
    ${SE_ROOT}/BG3Extender/Lua/Shared/Proxies/LuaTypeInformation.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Debugger/Debugger.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Debugger/DebugInterface.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Debugger/DebugMessages.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Debugger/osidebug.pb.cc
    ${SE_ROOT}/BG3Extender/Osiris/Functions/FunctionLibrary.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Functions/FunctionUtilities.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Functions/LuaFunctions.cpp
    ${SE_ROOT}/BG3Extender/Osiris/OsirisExtender.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Shared/CustomFunctions.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Shared/NodeHooks.cpp
    ${SE_ROOT}/BG3Extender/Osiris/Shared/OsirisHelpers.cpp
    ${SE_ROOT}/BG3Extender/Extender/Client/VisualHelpers.cpp
)

set(SE_PLATFORM_SOURCES
    Platform/Entry.cpp
    Platform/CrashReporter.cpp
    Platform/Hooking.cpp
    Platform/MachO.cpp
    Platform/NoesisTypeInfo.cpp
    Platform/SDLManager.cpp
    Platform/Win32Compat.cpp
    Platform/Darwin.mm
)

add_library(BG3ScriptExtender SHARED ${SE_SOURCES} ${SE_PROTO_SOURCES} ${SE_PLATFORM_SOURCES})
add_dependencies(BG3ScriptExtender SEGenerated SEResources)
target_compile_options(BG3ScriptExtender PRIVATE ${SE_COMPILE_OPTIONS})
target_compile_definitions(BG3ScriptExtender PRIVATE ${SE_DEFINITIONS})
target_include_directories(BG3ScriptExtender PRIVATE "${SE_ROOT}/BG3Extender" ${SE_INCLUDES})
target_precompile_headers(BG3ScriptExtender PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${SE_ROOT}/BG3Extender/stdafx.h>")
# Generated protobuf code and the small platform files don't use the extender's prefix header
set_source_files_properties(${SE_PROTO_SOURCES}
    Platform/Hooking.cpp Platform/MachO.cpp Platform/NoesisTypeInfo.cpp Platform/Win32Compat.cpp Platform/Darwin.mm
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(BG3ScriptExtender PRIVATE
    CoreLib LuaLib imgui SEThirdParty
    "${SE_EXT}/protobuf-build/libprotobuf-lite.a"
    "-framework Foundation" "-framework AppKit" "-framework CoreGraphics")

# abseil is built alongside protobuf; link every archive it produced
file(GLOB_RECURSE SE_ABSL_LIBS "${SE_EXT}/protobuf-build/_deps/absl-build/*.a")
file(GLOB SE_UTF8_LIBS "${SE_EXT}/protobuf-build/third_party/utf8_range/*.a")
target_link_libraries(BG3ScriptExtender PRIVATE ${SE_ABSL_LIBS} ${SE_UTF8_LIBS})

target_link_options(BG3ScriptExtender PRIVATE
    "LINKER:-sectcreate,__SE_RES,res101,${SE_LUA_BUNDLE}"
    "LINKER:-sectcreate,__SE_RES,res107,${SE_SYMBOL_MAPPINGS}"
    "LINKER:-dead_strip")
set_target_properties(BG3ScriptExtender PROPERTIES
    LINK_DEPENDS "${SE_LUA_BUNDLE};${SE_SYMBOL_MAPPINGS}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------------------------
# Tests for the platform layer (hook engine, Mach-O symbol lookup); no game needed
# ---------------------------------------------------------------------------------------------

add_executable(SEPlatformTests EXCLUDE_FROM_ALL
    Tests/PlatformTests.cpp
    Platform/Hooking.cpp
    Platform/MachO.cpp)
target_compile_options(SEPlatformTests PRIVATE -fms-extensions -Wno-everything)
target_include_directories(SEPlatformTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Compat")
# The game is not signed with the hardened runtime; neither is the test, so that patching its own
# code behaves the way it will inside the game.
set_target_properties(SEPlatformTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(SELoadTest EXCLUDE_FROM_ALL Tests/LoadTest.c)
set_target_properties(SELoadTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
