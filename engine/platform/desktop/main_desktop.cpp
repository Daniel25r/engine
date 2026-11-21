// Capa de plataforma de escritorio (Linux/Windows/macOS con GLFW).
// Usa el mismo OpenGL ES 3.0 que Android, así lo que funciona aquí funciona en el móvil.
// El ratón hace de dedo (puntero 0).
//
// Variables de entorno útiles para pruebas automáticas:
//   FORJA_FRAMES=N           cierra tras N fotogramas
//   FORJA_SCREENSHOT=a.png   guarda una captura del último fotograma
//   FORJA_SIZE=540x960       tamaño de la ventana
#define GLFW_INCLUDE_ES3
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "forja/Engine.h"
#include "forja/Log.h"

namespace {

class FolderAssets final : public forja::Assets {
public:
    std::vector<std::string> roots;

    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        for (const auto& root : roots) {
            std::ifstream f(root + "/" + path, std::ios::binary);
            if (!f) continue;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
        return false;
    }
};

forja::Engine* gEngine = nullptr;
bool gMouseDown = false;

forja::Vec2 cursorPixels(GLFWwindow* w) {
    double x, y;
    int ww, wh, fw, fh;
    glfwGetCursorPos(w, &x, &y);
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    return {static_cast<float>(x * fw / (ww ? ww : 1)), static_cast<float>(y * fh / (wh ? wh : 1))};
}

void saveScreenshot(const char* path, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path, w, h, 4, px.data(), w * 4);
    FORJA_LOG("Captura guardada en %s", path);
}

} // namespace

int main() {
    int width = 540, height = 960;
    if (const char* s = std::getenv("FORJA_SIZE")) std::sscanf(s, "%dx%d", &width, &height);
    const long maxFrames = std::getenv("FORJA_FRAMES") ? std::atol(std::getenv("FORJA_FRAMES")) : 0;
    const char* screenshot = std::getenv("FORJA_SCREENSHOT");

    if (!glfwInit()) {
        FORJA_ERROR("No se pudo iniciar GLFW");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(width, height, "Forja", nullptr, nullptr);
    if (!window) {
        FORJA_ERROR("No se pudo crear una ventana con OpenGL ES 3.0");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    FolderAssets assets;
    assets.roots = {FORJA_GAME_ASSETS, FORJA_ENGINE_ASSETS};

    forja::Engine engine;
    gEngine = &engine;
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT) return;
        gMouseDown = action == GLFW_PRESS;
        if (gMouseDown) gEngine->input().touchBegan(0, cursorPixels(w));
        else gEngine->input().touchEnded(0, cursorPixels(w));
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double, double) {
        if (gMouseDown) gEngine->input().touchMoved(0, cursorPixels(w));
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow*, int focused) {
        if (focused) gEngine->resume();
        else gEngine->pause();
    });

    if (!engine.startup(assets, forja::createGame())) return 1;

    long frames = 0;
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        engine.frame(fw, fh);
        if (maxFrames && ++frames >= maxFrames) {
            if (screenshot) saveScreenshot(screenshot, fw, fh);
            break;
        }
        glfwSwapBuffers(window);
    }

    engine.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
