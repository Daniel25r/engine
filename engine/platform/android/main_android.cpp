// Capa de plataforma Android: ciclo de vida de la app, EGL/OpenGL ES 3, táctil y archivos del APK.
// Ejecuta el paquete de juego que va dentro de los assets del APK con el mismo Runtime que el escritorio.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>

#include <cstring>

#include "titan/core/Log.h"
#include "titan/runtime/Runtime.h"
#include "AndroidInput.h"

namespace {

class ApkSource final : public titan::FileSource {
public:
    AAssetManager* manager = nullptr;

    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        AAsset* a = AAssetManager_open(manager, path.c_str(), AASSET_MODE_BUFFER);
        if (!a) return false;
        const off_t size = AAsset_getLength(a);
        out.resize(static_cast<size_t>(size));
        const int got = AAsset_read(a, out.data(), out.size());
        AAsset_close(a);
        return got == size;
    }
};

struct App {
    android_app* android = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool focused = false;
    bool contextWasLost = false;
    ApkSource files;
    titan::Runtime engine;
};

bool createSurface(App& app) {
    if (app.display == EGL_NO_DISPLAY) {
        app.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(app.display, nullptr, nullptr)) {
            TITAN_ERROR("android", "eglInitialize falló");
            return false;
        }
        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                                  EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                                  EGL_RED_SIZE,        8,
                                  EGL_GREEN_SIZE,      8,
                                  EGL_BLUE_SIZE,       8,
                                  EGL_DEPTH_SIZE,      0,
                                  EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(app.display, attribs, &app.config, 1, &count) || count < 1) {
            TITAN_ERROR("android", "Este dispositivo no ofrece OpenGL ES 3.0");
            return false;
        }
    }
    bool freshContext = false;
    if (app.context == EGL_NO_CONTEXT) {
        const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        app.context = eglCreateContext(app.display, app.config, EGL_NO_CONTEXT, ctxAttribs);
        if (app.context == EGL_NO_CONTEXT) {
            TITAN_ERROR("android", "No se pudo crear el contexto de OpenGL ES 3");
            return false;
        }
        freshContext = true;
    }
    app.surface = eglCreateWindowSurface(app.display, app.config, app.android->window, nullptr);
    if (app.surface == EGL_NO_SURFACE || !eglMakeCurrent(app.display, app.surface, app.surface, app.context)) {
        TITAN_ERROR("android", "No se pudo crear la superficie de dibujo (0x%x)", eglGetError());
        return false;
    }
    eglSwapInterval(app.display, 1);

    if (!app.engine.running()) {
        if (app.android->activity->internalDataPath) // almacenamiento privado de la app: solo este juego lo lee
            app.engine.setSavePath(std::string(app.android->activity->internalDataPath) + "/partida.json");
        if (!app.engine.start(app.files)) TITAN_ERROR("android", "No se pudo arrancar el juego");
    } else if (freshContext && app.contextWasLost) {
        app.engine.renderer().onContextRestored();
        app.contextWasLost = false;
    }
    return true;
}

void destroySurface(App& app) {
    if (app.display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(app.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (app.surface != EGL_NO_SURFACE) eglDestroySurface(app.display, app.surface);
    app.surface = EGL_NO_SURFACE;
}

void destroyAll(App& app) {
    if (app.display == EGL_NO_DISPLAY) return;
    if (app.context != EGL_NO_CONTEXT && app.surface != EGL_NO_SURFACE)
        eglMakeCurrent(app.display, app.surface, app.surface, app.context);
    app.engine.shutdown();
    destroySurface(app);
    if (app.context != EGL_NO_CONTEXT) eglDestroyContext(app.display, app.context);
    eglTerminate(app.display);
    app.context = EGL_NO_CONTEXT;
    app.display = EGL_NO_DISPLAY;
}

void drawFrame(App& app) {
    EGLint w = 0, h = 0;
    eglQuerySurface(app.display, app.surface, EGL_WIDTH, &w);
    eglQuerySurface(app.display, app.surface, EGL_HEIGHT, &h);
    app.engine.frame(w, h);
    if (eglSwapBuffers(app.display, app.surface)) return;

    const EGLint err = eglGetError();
    if (err == EGL_CONTEXT_LOST) {
        TITAN_INFO("android", "Se perdió el contexto de OpenGL; se recreará");
        app.engine.renderer().onContextLost();
        app.contextWasLost = true;
        destroySurface(app);
        eglDestroyContext(app.display, app.context);
        app.context = EGL_NO_CONTEXT;
        createSurface(app);
    } else if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) {
        destroySurface(app);
        createSurface(app);
    }
}

void onCommand(android_app* android, int32_t cmd) {
    App& app = *static_cast<App*>(android->userData);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (android->window) createSurface(app);
        break;
    case APP_CMD_TERM_WINDOW:
        destroySurface(app); // el contexto (y con él las texturas) se conserva
        break;
    case APP_CMD_GAINED_FOCUS:
        app.focused = true;
        app.engine.resume();
        break;
    case APP_CMD_LOST_FOCUS:
        app.focused = false;
        app.engine.pause();
        break;
    default:
        break;
    }
}

int32_t onInput(android_app* android, AInputEvent* event) {
    App& app = *static_cast<App*>(android->userData);
    titan::Input& input = app.engine.input();
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) { // teclado y botones del mando
        const int32_t code = AKeyEvent_getKeyCode(event);
        if (code == AKEYCODE_BACK) return 0; // lo gestiona el sistema
        const int32_t act = AKeyEvent_getAction(event);
        if (act == AKEY_EVENT_ACTION_MULTIPLE) return 0;
        return titan::androidinput::applyKey(input, code, act == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
    }
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    if ((AInputEvent_getSource(event) & AINPUT_SOURCE_CLASS_JOYSTICK) != 0) { // sticks y gatillos
        auto ax = [&](int32_t a) { return AMotionEvent_getAxisValue(event, a, 0); };
        titan::androidinput::applyAxes(input, ax(AMOTION_EVENT_AXIS_X), ax(AMOTION_EVENT_AXIS_Y), ax(AMOTION_EVENT_AXIS_Z),
                                       ax(AMOTION_EVENT_AXIS_RZ),
                                       std::max(ax(AMOTION_EVENT_AXIS_LTRIGGER), ax(AMOTION_EVENT_AXIS_BRAKE)),
                                       std::max(ax(AMOTION_EVENT_AXIS_RTRIGGER), ax(AMOTION_EVENT_AXIS_GAS)));
        return 1;
    }
    const int32_t action = AMotionEvent_getAction(event);
    const int32_t kind = action & AMOTION_EVENT_ACTION_MASK;
    const size_t index =
        (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    auto pos = [&](size_t i) { return titan::Vec2{AMotionEvent_getX(event, i), AMotionEvent_getY(event, i)}; };

    switch (kind) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        input.touchBegan(AMotionEvent_getPointerId(event, index), pos(index));
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i)
            input.touchMoved(AMotionEvent_getPointerId(event, i), pos(i));
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
        input.touchEnded(AMotionEvent_getPointerId(event, index), pos(index));
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        input.cancelAll();
        break;
    default:
        return 0;
    }
    return 1;
}

} // namespace

void android_main(android_app* android) {
    App app;
    app.android = android;
    app.files.manager = android->activity->assetManager;
    android->userData = &app;
    android->onAppCmd = onCommand;
    android->onInputEvent = onInput;
    ANativeActivity_setWindowFlags(android->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);

    while (true) {
        int events = 0;
        android_poll_source* source = nullptr;
        // Si la app está visible se dibuja sin esperar; si no, se duerme hasta el siguiente evento.
        while (true) {
            const bool animating = app.focused && app.surface != EGL_NO_SURFACE;
            const int ident = ALooper_pollOnce(animating ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source));
            if (ident == ALOOPER_POLL_TIMEOUT || ident == ALOOPER_POLL_ERROR) break;
            if (source) source->process(android, source);
            if (android->destroyRequested) {
                destroyAll(app);
                return;
            }
        }
        if (app.engine.quitRequested()) ANativeActivity_finish(android->activity);
        if (app.focused && app.surface != EGL_NO_SURFACE) drawFrame(app);
    }
}
