cmake_minimum_required(VERSION 3.22)
project(Forja LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Motor ----
add_library(forja STATIC
    engine/src/Engine.cpp
    engine/src/Renderer.cpp
    engine/src/stb_impl.cpp
)
target_include_directories(forja
    PUBLIC engine/include
    PRIVATE engine/third_party/stb
)
if(MSVC)
    target_compile_options(forja PRIVATE /W4)
else()
    target_compile_options(forja PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Juego (cambia esto para hacer tu propio juego) ----
set(GAME_SOURCES sample/src/SwarmGame.cpp)

if(ANDROID)
    add_library(native_app_glue STATIC
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue)

    target_link_libraries(forja PUBLIC GLESv3 log)

    add_library(forja_game SHARED engine/platform/android/main_android.cpp ${GAME_SOURCES})
    target_link_libraries(forja_game PRIVATE forja native_app_glue android EGL)
    # Evita que el enlazador descarte el punto de entrada de NativeActivity.
    target_link_options(forja_game PRIVATE "-u" "ANativeActivity_onCreate")
else()
    find_package(glfw3 3.3 REQUIRED)
    target_link_libraries(forja PUBLIC GLESv2)

    add_executable(forja_desktop engine/platform/desktop/main_desktop.cpp ${GAME_SOURCES})
    target_include_directories(forja_desktop PRIVATE engine/third_party/stb)
    target_link_libraries(forja_desktop PRIVATE forja glfw)
    target_compile_definitions(forja_desktop PRIVATE
        FORJA_GAME_ASSETS="${CMAKE_SOURCE_DIR}/sample/assets"
        FORJA_ENGINE_ASSETS="${CMAKE_SOURCE_DIR}/engine/assets")
endif()
