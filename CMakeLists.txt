cmake_minimum_required(VERSION 3.22)
project(Titan VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TITAN_BUILD_TESTS "Compilar las pruebas" ON)
option(TITAN_BUILD_TOOLS "Compilar la herramienta de línea de comandos" ON)

if(MSVC)
    set(TITAN_WARNINGS /W4)
else()
    set(TITAN_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------ Dependencias

# Lua 5.4.7 (MIT). Ver docs/DEPENDENCIES.md
file(GLOB LUA_SOURCES ${CMAKE_SOURCE_DIR}/third_party/lua/*.c)
add_library(titan_lua STATIC ${LUA_SOURCES})
target_include_directories(titan_lua PUBLIC third_party/lua)
if(NOT WIN32)
    target_compile_definitions(titan_lua PRIVATE LUA_USE_POSIX)
endif()
# Lua se compila como C++: así sus errores son excepciones y los destructores de C++
# se ejecutan al propagarse un error desde las funciones del motor.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
target_compile_options(titan_lua PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fexceptions -w>)

# ------------------------------------------------------------------ titan_core (sin GPU ni plataforma)

add_library(titan_core STATIC
    engine/src/core/Math.cpp
    engine/src/core/Log.cpp
    engine/src/core/Uuid.cpp
    engine/src/core/FileIO.cpp
    engine/src/core/Scene.cpp
    engine/src/core/Serialization.cpp
    engine/src/core/Project.cpp
)
target_include_directories(titan_core PUBLIC engine/include third_party)
target_compile_options(titan_core PRIVATE ${TITAN_WARNINGS})
if(ANDROID)
    target_link_libraries(titan_core PUBLIC log)
endif()

# ------------------------------------------------------------------ titan_scripting (Lua)

add_library(titan_scripting STATIC engine/src/scripting/ScriptSystem.cpp)
target_link_libraries(titan_scripting PUBLIC titan_core titan_lua)
target_compile_options(titan_scripting PRIVATE ${TITAN_WARNINGS} $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fexceptions>)

# ------------------------------------------------------------------ titan_render (OpenGL ES 3.0)

add_library(titan_render STATIC
    engine/src/render/Renderer.cpp
    engine/src/render/stb_impl.cpp
)
target_include_directories(titan_render PRIVATE third_party/stb)
target_link_libraries(titan_render PUBLIC titan_core)
target_compile_options(titan_render PRIVATE ${TITAN_WARNINGS})
if(ANDROID)
    target_link_libraries(titan_render PUBLIC GLESv3)
else()
    target_link_libraries(titan_render PUBLIC GLESv2)
endif()

# ------------------------------------------------------------------ titan_runtime (bucle + sistemas)

add_library(titan_runtime STATIC engine/src/runtime/Runtime.cpp)
target_link_libraries(titan_runtime PUBLIC titan_core titan_render titan_scripting)
target_compile_options(titan_runtime PRIVATE ${TITAN_WARNINGS})

# ------------------------------------------------------------------ Plataformas

if(ANDROID)
    add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

    add_library(titan_player SHARED engine/platform/android/main_android.cpp)
    target_link_libraries(titan_player PRIVATE titan_runtime native_app_glue android EGL)
    target_link_options(titan_player PRIVATE "-u" "ANativeActivity_onCreate")
else()
    find_package(glfw3 3.3 REQUIRED)
    add_executable(titan_player engine/platform/desktop/main_desktop.cpp)
    target_include_directories(titan_player PRIVATE third_party/stb)
    target_link_libraries(titan_player PRIVATE titan_runtime glfw)
    target_compile_definitions(titan_player PRIVATE TITAN_ENGINE_ASSETS="${CMAKE_SOURCE_DIR}/engine/assets")

    if(TITAN_BUILD_TOOLS)
        add_executable(titan tools/titan/main.cpp)
        target_link_libraries(titan PRIVATE titan_core titan_scripting)
        target_compile_options(titan PRIVATE ${TITAN_WARNINGS})
    endif()

    if(TITAN_BUILD_TESTS)
        enable_testing()
        add_executable(titan_tests
            tests/TestMain.cpp
            tests/test_math.cpp
            tests/test_world.cpp
            tests/test_scene.cpp
            tests/test_events.cpp
            tests/test_serialization.cpp
            tests/test_project.cpp
            tests/test_scripting.cpp
        )
        target_link_libraries(titan_tests PRIVATE titan_core titan_scripting)
        add_test(NAME titan_tests COMMAND titan_tests)
    endif()
endif()
