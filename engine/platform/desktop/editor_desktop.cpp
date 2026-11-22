// Editor de Titán para escritorio (GLFW). El mismo editor que en Android.
//
// Uso normal:  titan_editor [--projects carpeta]
// Automatización (pruebas): --open ruta | --new nombre --template 0..3, --panel nombre, --select entidad, --create "Botón;Joystick",
//   --script ruta.lua, --play-at N, --stop-at N, --taps "x,y@N;...", --drag "x1,y1,x2,y2@N",
//   --frames N, --screenshot archivo.png, --size 1280x800, --scale 1.0, --touch
#define GLFW_INCLUDE_ES3
#include <GLFW/glfw3.h>

#include "GlfwInput.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "titan/core/FileSource.h"
#include "titan/core/Log.h"
#include "titan/editor/Editor.h"

namespace {

class FolderSource final : public titan::FileSource {
public:
    std::string root;
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        std::ifstream f(root + "/" + path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
};

class DesktopPlatform final : public titan::EditorPlatform {
public:
    GLFWwindow* window = nullptr;
    std::string projects;
    FolderSource engine;
    float scale = 1;
    bool touch = false;

    std::string projectsDirectory() override { return projects; }
    titan::FileSource& engineFiles() override { return engine; }
    void setTextInputActive(bool) override {}
    bool requestImportFile() override { return false; }
    std::string clipboard() override {
        const char* t = glfwGetClipboardString(window);
        return t ? t : "";
    }
    void setClipboard(const std::string& text) override { glfwSetClipboardString(window, text.c_str()); }
    std::vector<std::string> uiFontCandidates() override {
        return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "C:/Windows/Fonts/segoeui.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"};
    }
    std::vector<std::string> monoFontCandidates() override {
        return {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "C:/Windows/Fonts/consola.ttf",
                "/System/Library/Fonts/Menlo.ttc"};
    }
    float uiScale() override { return scale; }
    bool isTouchScreen() override { return touch; }
};

titan::Editor* gEditor = nullptr;
float gContentScale = 1;

titan::EditorKey mapKey(int key, bool& ok) {
    using K = titan::EditorKey;
    ok = true;
    switch (key) {
    case GLFW_KEY_TAB: return K::Tab;
    case GLFW_KEY_LEFT: return K::Left;
    case GLFW_KEY_RIGHT: return K::Right;
    case GLFW_KEY_UP: return K::Up;
    case GLFW_KEY_DOWN: return K::Down;
    case GLFW_KEY_PAGE_UP: return K::PageUp;
    case GLFW_KEY_PAGE_DOWN: return K::PageDown;
    case GLFW_KEY_HOME: return K::Home;
    case GLFW_KEY_END: return K::End;
    case GLFW_KEY_DELETE: return K::Delete;
    case GLFW_KEY_BACKSPACE: return K::Backspace;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return K::Enter;
    case GLFW_KEY_ESCAPE: return K::Escape;
    case GLFW_KEY_LEFT_CONTROL: case GLFW_KEY_RIGHT_CONTROL: return K::Ctrl;
    case GLFW_KEY_LEFT_SHIFT: case GLFW_KEY_RIGHT_SHIFT: return K::Shift;
    case GLFW_KEY_LEFT_ALT: case GLFW_KEY_RIGHT_ALT: return K::Alt;
    case GLFW_KEY_A: return K::A;
    case GLFW_KEY_C: return K::C;
    case GLFW_KEY_V: return K::V;
    case GLFW_KEY_X: return K::X;
    case GLFW_KEY_Y: return K::Y;
    case GLFW_KEY_Z: return K::Z;
    case GLFW_KEY_S: return K::S;
    case GLFW_KEY_D: return K::D;
    case GLFW_KEY_F: return K::F;
    case GLFW_KEY_P: return K::P;
    case GLFW_KEY_N: return K::N;
    case GLFW_KEY_Q: return K::Q;
    case GLFW_KEY_W: return K::W;
    case GLFW_KEY_E: return K::E;
    case GLFW_KEY_R: return K::R;
    default: ok = false; return K::Tab;
    }
}

void cursor(GLFWwindow* w, double& x, double& y) {
    glfwGetCursorPos(w, &x, &y);
    int ww, wh, fw, fh;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    x *= double(fw) / (ww ? ww : 1);
    y *= double(fh) / (wh ? wh : 1);
}

void encodeUtf8(unsigned int c, std::string& out) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
}

struct ScriptedTouch {
    float x, y, x2, y2;
    long frame;
    bool drag;
};

} // namespace

int main(int argc, char** argv) {
    DesktopPlatform platform;
    platform.projects = (std::filesystem::current_path() / "TitanProyectos").generic_string();
    platform.engine.root = TITAN_ENGINE_ASSETS;
    std::string openPath, newName, panel, selectName, createKinds, scriptPath, screenshot;
    int templ = 2;
    long frames = 0;
    std::vector<long> playAt, stopAt; // admiten varias: --play-at 10,80
    auto frameList = [](const std::string& s) {
        std::vector<long> out;
        for (size_t i = 0; i < s.size();) {
            size_t e = s.find(',', i);
            if (e == std::string::npos) e = s.size();
            out.push_back(std::atol(s.substr(i, e - i).c_str()));
            i = e + 1;
        }
        return out;
    };
    int width = 1280, height = 800;
    std::vector<ScriptedTouch> scripted;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--projects") platform.projects = next();
        else if (a == "--open") openPath = next();
        else if (a == "--new") newName = next();
        else if (a == "--template") templ = std::atoi(next().c_str());
        else if (a == "--panel") panel = next();
        else if (a == "--select") selectName = next();
        else if (a == "--create") createKinds = next();
        else if (a == "--script") scriptPath = next();
        else if (a == "--play-at") playAt = frameList(next());
        else if (a == "--stop-at") stopAt = frameList(next());
        else if (a == "--frames") frames = std::atol(next().c_str());
        else if (a == "--screenshot") screenshot = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height);
        else if (a == "--scale") platform.scale = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--touch") platform.touch = true;
        else if (a == "--taps" || a == "--drag") {
            const std::string list = next();
            for (const char* p = list.c_str(); p && *p; p = std::strchr(p, ';') ? std::strchr(p, ';') + 1 : nullptr) {
                ScriptedTouch t{};
                if (a == "--drag" && std::sscanf(p, "%f,%f,%f,%f@%ld", &t.x, &t.y, &t.x2, &t.y2, &t.frame) == 5) {
                    t.drag = true;
                    scripted.push_back(t);
                } else if (a == "--taps" && std::sscanf(p, "%f,%f@%ld", &t.x, &t.y, &t.frame) == 3) {
                    scripted.push_back(t);
                }
            }
        }
    }

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    GLFWwindow* window = glfwCreateWindow(width, height, "Titán — Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "No se pudo crear una ventana con OpenGL ES 3.0\n");
        return 1;
    }
    platform.window = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    int exitCode = 0;
    {
        titan::Editor editor(platform);
        gEditor = &editor;
        if (!editor.init()) return 1;
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
            if (button != GLFW_MOUSE_BUTTON_LEFT) return;
            double x, y;
            cursor(w, x, y);
            if (action == GLFW_PRESS) gEditor->onPointerDown(0, float(x), float(y));
            else gEditor->onPointerUp(0, float(x), float(y));
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double, double) {
            double x, y;
            cursor(w, x, y);
            gEditor->onPointerMove(0, float(x), float(y));
        });
        glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy) {
            double x, y;
            cursor(w, x, y);
            gEditor->onScroll(float(x), float(y), float(dy));
        });
        glfwSetCharCallback(window, [](GLFWwindow*, unsigned int c) {
            std::string s;
            encodeUtf8(c, s);
            gEditor->onText(s);
        });
        glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int) {
            if (titan::Input* in = gEditor->playInput(); in && action != GLFW_REPEAT)
                in->setKey(titan::glfwinput::keyFromGlfw(key), action == GLFW_PRESS); // el juego también recibe la tecla
            bool ok;
            const titan::EditorKey k = mapKey(key, ok);
            if (ok && action != GLFW_REPEAT) gEditor->onKey(k, action == GLFW_PRESS);
            else if (ok && action == GLFW_REPEAT) gEditor->onKey(k, true);
        });
        glfwSetWindowFocusCallback(window, [](GLFWwindow*, int focused) {
            if (focused) gEditor->onResume();
            else gEditor->onPause();
        });

        if (!newName.empty()) editor.createProject(newName, templ == 1 || templ == 3, templ >= 2);
        if (!openPath.empty()) editor.openProject(openPath);
        for (size_t s = 0; s < createKinds.size();) {
            size_t e = createKinds.find(';', s);
            if (e == std::string::npos) e = createKinds.size();
            editor.createEntity(createKinds.substr(s, e - s));
            s = e + 1;
        }
        if (!selectName.empty()) editor.selectEntity(selectName);
        if (!scriptPath.empty()) editor.openScript(scriptPath);
        if (!panel.empty()) editor.showPanel(panel);

        auto last = std::chrono::steady_clock::now();
        long frame = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (titan::Input* in = editor.playInput()) titan::glfwinput::pollGamepad(*in);
            for (const ScriptedTouch& t : scripted) {
                const int id = 50;
                if (!t.drag) {
                    if (frame == t.frame) editor.onPointerDown(id, t.x, t.y);
                    if (frame == t.frame + 2) editor.onPointerUp(id, t.x, t.y);
                } else {
                    const long d = frame - t.frame;
                    if (d == 0) editor.onPointerDown(id, t.x, t.y);
                    if (d >= 1 && d <= 10) editor.onPointerMove(id, t.x + (t.x2 - t.x) * d / 10.0f, t.y + (t.y2 - t.y) * d / 10.0f);
                    if (d == 11) editor.onPointerUp(id, t.x2, t.y2);
                }
            }
            for (long f : playAt)
                if (frame == f) editor.play();
            for (long f : stopAt)
                if (frame == f) editor.stop();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            editor.frame(fw, fh, frames ? 1.0f / 60.0f : dt);
            ++frame;
            if (frames && frame >= frames) {
                if (!screenshot.empty()) {
                    std::vector<uint8_t> px(size_t(fw) * fh * 4);
                    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    stbi_flip_vertically_on_write(1);
                    stbi_write_png(screenshot.c_str(), fw, fh, 4, px.data(), fw * 4);
                }
                exitCode = static_cast<int>(editor.scriptErrorCount());
                break;
            }
            glfwSwapBuffers(window);
        }
        editor.shutdown();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
