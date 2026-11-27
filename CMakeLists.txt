cmake_minimum_required(VERSION 3.22)
project(Titan VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TITAN_BUILD_TESTS "Compilar las pruebas" ON)
option(TITAN_BUILD_TOOLS "Compilar la herramienta de línea de comandos" ON)

if(MSVC)
    set(TITAN_WARNINGS /W4)
    # Los fuentes son UTF-8 (textos con acentos y eñes): sin /utf-8, MSVC los leería con la página de códigos local.
    add_compile_options(/utf-8 /bigobj)
else()
    set(TITAN_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS) # windows.h no debe definir min/max
endif()

# ------------------------------------------------------------------ Dependencias

# OpenGL ES 3.0: en Android y Linux lo da el sistema; en Windows, ANGLE (traduce GLES a Direct3D 11, lo mismo que
# usan Chrome y Godot). Se instala con vcpkg (vcpkg.json); ver .github/workflows/build_windows.yml.
if(WIN32 AND NOT ANDROID)
    find_package(unofficial-angle CONFIG REQUIRED)
    # Las cabeceras GLES3/ y KHR/ las instalan los puertos opengl-registry y egl-registry (dependencias de angle);
    # los objetivos de ANGLE no las exportan, así que se buscan aparte.
    find_path(TITAN_GLES_INCLUDE_DIR GLES3/gl3.h REQUIRED)
    add_library(titan_gles INTERFACE)
    target_include_directories(titan_gles INTERFACE ${TITAN_GLES_INCLUDE_DIR})
    target_link_libraries(titan_gles INTERFACE unofficial::angle::libGLESv2 unofficial::angle::libEGL)
    set(TITAN_GLES_LIBS titan_gles)
elseif(ANDROID)
    set(TITAN_GLES_LIBS GLESv3)
else()
    set(TITAN_GLES_LIBS GLESv2)
endif()

# Lua 5.4.7 (MIT). Ver docs/TITAN_ENGINE_DEPENDENCIES.md
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
    engine/src/core/Bounds.cpp
    engine/src/core/Quality.cpp
    engine/src/core/Particles.cpp
    engine/src/core/InputActions.cpp
    engine/src/core/AnimationGraph.cpp
    engine/src/core/TerrainTools.cpp
    engine/src/core/CameraSystem.cpp
    engine/src/core/Navigation.cpp
    engine/src/core/JobSystem.cpp
    engine/src/core/Log.cpp
    engine/src/core/Uuid.cpp
    engine/src/core/FileIO.cpp
    engine/src/core/Scene.cpp
    engine/src/core/Serialization.cpp
    engine/src/core/Project.cpp
    engine/src/core/Geometry.cpp
    engine/src/core/Templates.cpp
)
target_include_directories(titan_core PUBLIC engine/include third_party)
target_compile_options(titan_core PRIVATE ${TITAN_WARNINGS})
if(ANDROID)
    target_link_libraries(titan_core PUBLIC log)
endif()

# ------------------------------------------------------------------ titan_scripting (Lua)

add_library(titan_scripting STATIC engine/src/scripting/ScriptSystem.cpp engine/src/scripting/ScriptApiWorld.cpp)
target_link_libraries(titan_scripting PUBLIC titan_core titan_lua titan_audio)
target_compile_options(titan_scripting PRIVATE ${TITAN_WARNINGS} $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fexceptions>)

# ------------------------------------------------------------------ Jolt Physics 5.2.0 (MIT) y titan_physics

file(GLOB_RECURSE JOLT_SOURCES ${CMAKE_SOURCE_DIR}/third_party/jolt/Jolt/*.cpp)
add_library(titan_jolt STATIC ${JOLT_SOURCES})
target_include_directories(titan_jolt PUBLIC third_party/jolt)
target_compile_options(titan_jolt PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
find_package(Threads REQUIRED)
target_link_libraries(titan_jolt PUBLIC Threads::Threads)
target_link_libraries(titan_core PUBLIC Threads::Threads) # JobSystem

add_library(titan_physics STATIC engine/src/physics/PhysicsWorld.cpp)
target_link_libraries(titan_physics PUBLIC titan_core PRIVATE titan_jolt)
target_compile_options(titan_physics PRIVATE ${TITAN_WARNINGS})

# ------------------------------------------------------------------ titan_audio (miniaudio 0.11.21, dominio público / MIT-0)

add_library(titan_audio STATIC engine/src/audio/AudioEngine.cpp engine/src/audio/SoundCheck.cpp
    engine/src/audio/AudioEffects.cpp engine/src/audio/AudioBus.cpp)
target_link_libraries(titan_audio PUBLIC titan_core)
target_compile_options(titan_audio PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>)
if(NOT WIN32)
    target_link_libraries(titan_audio PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# ------------------------------------------------------------------ titan_render (OpenGL ES 3.0)

# ------------------------------------------------------------------ titan_ui (fuentes SDF, diseño y toques; sin OpenGL)

add_library(titan_ui STATIC engine/src/ui/Font.cpp engine/src/ui/UISystem.cpp)
target_include_directories(titan_ui PRIVATE third_party/stb)
target_link_libraries(titan_ui PUBLIC titan_core)
target_compile_options(titan_ui PRIVATE ${TITAN_WARNINGS})

add_library(titan_render STATIC
    engine/src/render/Renderer.cpp
    engine/src/render/stb_impl.cpp
    engine/src/render/Model.cpp
    engine/src/render/PostProcess.cpp
)
target_include_directories(titan_render PRIVATE third_party/stb)
target_link_libraries(titan_render PUBLIC titan_core titan_ui)
target_compile_options(titan_render PRIVATE ${TITAN_WARNINGS})
target_link_libraries(titan_render PUBLIC ${TITAN_GLES_LIBS})

# ------------------------------------------------------------------ titan_runtime (bucle + sistemas)

add_library(titan_runtime STATIC engine/src/runtime/Runtime.cpp engine/src/runtime/SceneRenderer.cpp
    engine/src/runtime/UIRenderer.cpp engine/src/runtime/ResourceCache.cpp)
target_link_libraries(titan_runtime PUBLIC titan_core titan_render titan_scripting titan_physics titan_audio)
target_compile_options(titan_runtime PRIVATE ${TITAN_WARNINGS})

# ------------------------------------------------------------------ Dear ImGui 1.91.5 (MIT): interfaz del editor

add_library(titan_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_stdlib.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(titan_imgui PUBLIC third_party/imgui)
target_compile_definitions(titan_imgui PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(titan_imgui PUBLIC ${TITAN_GLES_LIBS})

# ------------------------------------------------------------------ titan_editor (el editor, común a Android y escritorio)

add_library(titan_editor STATIC
    engine/src/editor/EditorCore.cpp
    engine/src/editor/EditorPanels.cpp
    engine/src/editor/EditorViewport.cpp
    engine/src/editor/EditorAnimation.cpp
    engine/src/editor/EditorTerrain.cpp
    engine/src/editor/EditorDiagnostics.cpp
    engine/src/editor/EditorMixer.cpp
)
target_link_libraries(titan_editor PUBLIC titan_runtime titan_imgui titan_lua)
target_include_directories(titan_editor PRIVATE third_party/stb)
target_compile_options(titan_editor PRIVATE ${TITAN_WARNINGS})

# ------------------------------------------------------------------ Plataformas

if(ANDROID)
    add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

    add_library(titan_player SHARED engine/platform/android/main_android.cpp)
    target_link_libraries(titan_player PRIVATE titan_runtime native_app_glue android EGL)
    target_link_options(titan_player PRIVATE "-u" "ANativeActivity_onCreate")

    add_library(titan_editor_jni SHARED engine/platform/android/editor_jni.cpp)
    target_link_libraries(titan_editor_jni PRIVATE titan_editor android log)
else()
    find_package(glfw3 3.3 REQUIRED)
    add_executable(titan_player engine/platform/desktop/main_desktop.cpp)
    target_include_directories(titan_player PRIVATE third_party/stb)
    target_link_libraries(titan_player PRIVATE titan_runtime glfw)
    target_compile_definitions(titan_player PRIVATE TITAN_ENGINE_ASSETS="${CMAKE_SOURCE_DIR}/engine/assets")

    add_executable(titan_editor_desktop engine/platform/desktop/editor_desktop.cpp)
    set_target_properties(titan_editor_desktop PROPERTIES OUTPUT_NAME titan_editor)
    target_include_directories(titan_editor_desktop PRIVATE third_party/stb)
    target_link_libraries(titan_editor_desktop PRIVATE titan_editor glfw)
    target_compile_definitions(titan_editor_desktop PRIVATE TITAN_ENGINE_ASSETS="${CMAKE_SOURCE_DIR}/engine/assets")

    # Windows: los .exe se distribuyen con "engine_assets" al lado (el programa la busca allí antes que en la ruta
    # de compilación) y con las DLL de ANGLE y GLFW.
    if(WIN32)
        foreach(_exe titan_player titan_editor_desktop)
            add_custom_command(TARGET ${_exe} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/engine/assets $<TARGET_FILE_DIR:${_exe}>/engine_assets)
        endforeach()
    endif()

    if(TITAN_BUILD_TOOLS)
        # Los scripts de la demo de plataformas son archivos .lua normales; se incrustan en la herramienta.
        file(GLOB TITAN_DEMO_LUA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/titan/demo/plataformas/*.lua)
        set(_demo_inc "")
        foreach(_lua ${TITAN_DEMO_LUA})
            get_filename_component(_name ${_lua} NAME)
            file(READ ${_lua} _code)
            string(APPEND _demo_inc "{\"${_name}\", R\"TITANLUA(${_code})TITANLUA\"},\n")
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TITAN_DEMO_LUA})
        file(WRITE ${CMAKE_BINARY_DIR}/generated/DemoPlataformasScripts.inc.tmp "${_demo_inc}")
        configure_file(${CMAKE_BINARY_DIR}/generated/DemoPlataformasScripts.inc.tmp
                       ${CMAKE_BINARY_DIR}/generated/DemoPlataformasScripts.inc COPYONLY)
        add_executable(titan tools/titan/main.cpp tools/titan/Benchmark.cpp tools/titan/DemoPlatformer.cpp
                             tools/titan/DemoSounds.cpp tools/titan/WinConsole.cpp)
        target_include_directories(titan PRIVATE ${CMAKE_BINARY_DIR}/generated tools/titan)
        target_link_libraries(titan PRIVATE titan_core titan_scripting titan_audio)
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
            tests/test_geometry.cpp
            tests/test_physics.cpp
            tests/test_audio.cpp
            tests/test_audio_mixer.cpp
            tests/test_jobs.cpp
            tests/test_ui.cpp
            tests/test_bounds.cpp
            tests/test_quality.cpp
            tests/test_materials.cpp
            tests/test_particles.cpp
            tests/test_input_actions.cpp
            tests/test_animation_graph.cpp
            tests/test_terrain.cpp
        )
        target_compile_definitions(titan_tests PRIVATE TITAN_ENGINE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/engine/assets"
            TITAN_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
        target_link_libraries(titan_tests PRIVATE titan_core titan_scripting titan_physics titan_audio titan_ui titan_render)
        target_include_directories(titan_tests PRIVATE third_party/stb)
        add_test(NAME titan_tests COMMAND titan_tests)
    endif()
endif()
