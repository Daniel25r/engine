// Reproductor de escritorio (Linux con GLFW). Ejecuta un paquete de juego generado con
// "titan package", con el mismo OpenGL ES 3.0 y el mismo Runtime que Android.
// El ratón hace de dedo (puntero 0).
//
// Uso: titan_player <carpeta_del_paquete>
// Variables de entorno para pruebas automáticas:
//   TITAN_FRAMES=N          cierra tras N fotogramas
//   TITAN_SCREENSHOT=a.png  guarda una captura del último fotograma
//   TITAN_SIZE=960x540      tamaño de la ventana
//   TITAN_TAPS=x,y@N;...    simula toques en el píxel (x,y) en el fotograma N
#define GLFW_INCLUDE_ES3
#include <GLFW/glfw3.h>

#include "EngineAssets.h"
#include "GlfwInput.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "titan/core/Log.h"
#include "titan/runtime/Runtime.h"

namespace {

class FolderSource final : public titan::FileSource {
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

titan::Runtime* gRuntime = nullptr;
bool gMouseDown = false;

titan::Vec2 cursorPixels(GLFWwindow* w) {
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
    TITAN_INFO("player", "Captura guardada en %s", path);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Uso: titan_player <carpeta_del_paquete>\n");
        return 2;
    }
    int width = 960, height = 540;
    if (const char* s = std::getenv("TITAN_SIZE")) std::sscanf(s, "%dx%d", &width, &height);
    const long maxFrames = std::getenv("TITAN_FRAMES") ? std::atol(std::getenv("TITAN_FRAMES")) : 0;
    const char* screenshot = std::getenv("TITAN_SCREENSHOT");
    struct Tap { float x, y; long frame; };
    std::vector<Tap> taps;
    if (const char* s = std::getenv("TITAN_TAPS"))
        for (const char* p = s; p && *p; p = std::strchr(p, ';') ? std::strchr(p, ';') + 1 : nullptr) {
            Tap t{};
            if (std::sscanf(p, "%f,%f@%ld", &t.x, &t.y, &t.frame) == 3) taps.push_back(t);
        }

    struct KeyPress { titan::Key key; long from, to; };
    std::vector<KeyPress> keyScript;
    if (const char* s = std::getenv("TITAN_KEYS"))
        for (const char* p = s; p && *p; p = std::strchr(p, ';') ? std::strchr(p, ';') + 1 : nullptr) {
            char name[32] = {};
            long a = 0, b = -1;
            const int n = std::sscanf(p, "%31[^@]@%ld-%ld", name, &a, &b);
            if (n >= 2) keyScript.push_back({titan::keyFromName(name), a, n == 3 ? b : a + 2});
        }
    if (!glfwInit()) {
        TITAN_ERROR("player", "No se pudo iniciar GLFW");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(width, height, "Titán", nullptr, nullptr);
    if (!window) {
        TITAN_ERROR("player", "No se pudo crear una ventana con OpenGL ES 3.0");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    FolderSource files;
    files.roots = {argv[1], titan::desktop::engineAssetsDir(argv[0], TITAN_ENGINE_ASSETS)};

    int exitCode = 0;
    {
        titan::Runtime runtime;
        gRuntime = &runtime;
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
            if (button != GLFW_MOUSE_BUTTON_LEFT) return;
            gMouseDown = action == GLFW_PRESS;
            if (gMouseDown) gRuntime->input().touchBegan(0, cursorPixels(w));
            else gRuntime->input().touchEnded(0, cursorPixels(w));
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double, double) {
            if (gMouseDown) gRuntime->input().touchMoved(0, cursorPixels(w));
        });
        glfwSetWindowFocusCallback(window, [](GLFWwindow*, int focused) {
            if (focused) gRuntime->resume();
            else gRuntime->pause();
        });
        glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int) {
            if (action == GLFW_REPEAT) return;
            gRuntime->input().setKey(titan::glfwinput::keyFromGlfw(key), action == GLFW_PRESS);
        });

        // Diagnóstico: TITAN_CULLING=0 dibuja todo; TITAN_BENCH=1 imprime medias al terminar (con TITAN_FRAMES).
        const bool bench = std::getenv("TITAN_BENCH") != nullptr;
        if (const char* c = std::getenv("TITAN_CULLING")) runtime.sceneRenderer().culling = std::atoi(c) != 0;
        if (const char* c = std::getenv("TITAN_INSTANCING")) runtime.sceneRenderer().instancing = std::atoi(c) != 0;
        if (const char* c = std::getenv("TITAN_PARALLEL")) runtime.parallel = std::atoi(c) != 0;
        double sumFrame = 0, sumRender = 0, sumParticles = 0, sumPhysics = 0;
        // Carga: cuánto tarda en volver start() + el primer fotograma, el peor fotograma y cuándo quedan todas
        // las texturas en la GPU (mide la carga asíncrona).
        const auto benchStart = std::chrono::steady_clock::now();
        double startMs = 0, worstMs = 0, readyMs = -1;
        long worstFrame = 0, readyFrame = -1;
        long measured = 0;
        runtime.setSavePath(std::getenv("TITAN_SAVE") ? std::getenv("TITAN_SAVE") : std::string(argv[1]) + "/partida.json");
        if (!runtime.start(files)) {
            exitCode = 1;
        } else {
            if (const char* q = std::getenv("TITAN_QUALITY")) runtime.scriptSetQuality(q); // "bajo", "alto"...
            long frames = 0;
            while (!glfwWindowShouldClose(window) && !runtime.quitRequested()) {
                glfwPollEvents();
                titan::glfwinput::pollGamepad(runtime.input());
                for (const KeyPress& k : keyScript) { // TITAN_KEYS="D@40-90;Space@60": teclas simuladas para pruebas
                    if (frames == k.from) runtime.input().setKey(k.key, true);
                    if (frames == k.to) runtime.input().setKey(k.key, false);
                }
                for (const Tap& t : taps) {
                    if (frames == t.frame) runtime.input().touchBegan(99, {t.x, t.y});
                    if (frames == t.frame + 2) runtime.input().touchEnded(99, {t.x, t.y});
                }
                int fw, fh;
                glfwGetFramebufferSize(window, &fw, &fh);
                const auto f0 = std::chrono::steady_clock::now();
                runtime.frame(fw, fh);
                const double fMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count();
                if (frames == 0) startMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchStart).count();
                if (fMs > worstMs) { worstMs = fMs; worstFrame = frames; }
                if (readyFrame < 0 && runtime.pendingTextures() == 0) {
                    readyFrame = frames;
                    readyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchStart).count();
                }
                if (bench && frames >= 30) { // se descartan los primeros fotogramas (carga)
                    sumFrame += runtime.profile().frameMs;
                    sumRender += runtime.profile().renderMs;
                    sumParticles += runtime.profile().particlesMs;
                    sumPhysics += runtime.profile().physicsMs;
                    ++measured;
                }
                if (maxFrames && ++frames >= maxFrames) {
                    if (screenshot) saveScreenshot(screenshot, fw, fh);
                    break;
                }
                if (!maxFrames) ++frames;
                glfwSwapBuffers(window);
            }
            exitCode = runtime.scripts().errors().empty() ? 0 : 3;
            if (bench && measured > 0) {
                const titan::CullStats& cs = runtime.sceneRenderer().cullStats();
                const titan::RenderStats& rs = runtime.renderer().stats();
                std::printf("BENCH culling=%d fotogramas=%ld fotograma=%.2f ms render=%.2f ms draw_calls=%d "
                            "(sombras %d) mallas=%d/%d sombras=%d/%d triangulos=%d posproceso=%d escena=%dx%d calidad=%s instanciados=%d en %d lotes\n",
                            runtime.sceneRenderer().culling ? 1 : 0, measured, sumFrame / measured, sumRender / measured,
                            rs.drawCalls, rs.shadowDrawCalls, cs.meshesVisible, cs.meshes, cs.shadowCastersVisible, cs.shadowCasters, rs.triangles,
                            rs.postPasses, rs.sceneWidth, rs.sceneHeight, runtime.scriptQuality().c_str(), cs.instanced,
                            cs.instanceBatches);
                std::printf("BENCH paralelo=%d hilos=%d particulas=%.3f ms fisica=%.3f ms\n", runtime.parallel ? 1 : 0,
                            runtime.jobs().workerCount(), sumParticles / measured, sumPhysics / measured);
                std::printf("BENCH carga: arranque+1er fotograma=%.0f ms peor fotograma=%.0f ms (n.%ld) texturas listas en "
                            "el fotograma %ld (%.0f ms)\n", startMs, worstMs, worstFrame, readyFrame, readyMs);
            }
        }
        runtime.shutdown();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
