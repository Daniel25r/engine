// Puente JNI entre la app Android del editor (Kotlin) y el editor en C++.
// Todas las llamadas nativeXxx (salvo nativeCreate) llegan desde el hilo de OpenGL de GLSurfaceView.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <chrono>
#include <cstdarg>
#include <memory>
#include <string>

#include "titan/core/FileSource.h"
#include "titan/core/Log.h"
#include "titan/editor/Editor.h"
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

class AndroidPlatform final : public titan::EditorPlatform {
public:
    JavaVM* vm = nullptr;
    jobject activity = nullptr; // referencia global
    jobject assetRef = nullptr; // mantiene vivo el AssetManager de Java
    ApkSource engine;
    std::string projects;
    float density = 1;

    JNIEnv* env() {
        JNIEnv* e = nullptr;
        if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) vm->AttachCurrentThread(&e, nullptr);
        return e;
    }
    void callVoid(const char* name, const char* sig, ...) {
        JNIEnv* e = env();
        jclass cls = e->GetObjectClass(activity);
        jmethodID m = e->GetMethodID(cls, name, sig);
        if (!m) {
            e->ExceptionClear();
            return;
        }
        va_list args;
        va_start(args, sig);
        e->CallVoidMethodV(activity, m, args);
        va_end(args);
        if (e->ExceptionCheck()) e->ExceptionClear();
        e->DeleteLocalRef(cls);
    }

    std::string projectsDirectory() override { return projects; }
    titan::FileSource& engineFiles() override { return engine; }
    void setTextInputActive(bool active) override { callVoid("setKeyboardVisible", "(Z)V", static_cast<jboolean>(active)); }
    bool requestImportFile() override {
        callVoid("requestImport", "()V");
        return true;
    }
    std::string clipboard() override {
        JNIEnv* e = env();
        jclass cls = e->GetObjectClass(activity);
        jmethodID m = e->GetMethodID(cls, "getClipboardText", "()Ljava/lang/String;");
        std::string out;
        if (m) {
            auto s = static_cast<jstring>(e->CallObjectMethod(activity, m));
            if (s) {
                const char* c = e->GetStringUTFChars(s, nullptr);
                out = c;
                e->ReleaseStringUTFChars(s, c);
                e->DeleteLocalRef(s);
            }
        }
        if (e->ExceptionCheck()) e->ExceptionClear();
        e->DeleteLocalRef(cls);
        return out;
    }
    void setClipboard(const std::string& text) override {
        JNIEnv* e = env();
        jstring s = e->NewStringUTF(text.c_str());
        callVoid("setClipboardText", "(Ljava/lang/String;)V", s);
        e->DeleteLocalRef(s);
    }
    std::vector<std::string> uiFontCandidates() override {
        return {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf"};
    }
    std::vector<std::string> monoFontCandidates() override {
        return {"/system/fonts/DroidSansMono.ttf", "/system/fonts/NotoSansMono-Regular.ttf", "/system/fonts/CutiveMono.ttf"};
    }
    float uiScale() override { return density * 0.8f; }
    bool isTouchScreen() override { return true; }
};

std::unique_ptr<AndroidPlatform> gPlatform;
std::unique_ptr<titan::Editor> gEditor;
std::chrono::steady_clock::time_point gLast;

std::string toString(JNIEnv* e, jstring s) {
    if (!s) return {};
    const char* c = e->GetStringUTFChars(s, nullptr);
    std::string out = c;
    e->ReleaseStringUTFChars(s, c);
    return out;
}

} // namespace

#define BRIDGE(name) Java_dev_titan_editor_NativeBridge_##name

extern "C" {

JNIEXPORT void JNICALL BRIDGE(nativeCreate)(JNIEnv* e, jclass, jobject activity, jobject assets, jstring projects, jfloat density) {
    if (!gPlatform) gPlatform = std::make_unique<AndroidPlatform>();
    e->GetJavaVM(&gPlatform->vm);
    if (gPlatform->activity) e->DeleteGlobalRef(gPlatform->activity);
    if (gPlatform->assetRef) e->DeleteGlobalRef(gPlatform->assetRef);
    gPlatform->activity = e->NewGlobalRef(activity);
    gPlatform->assetRef = e->NewGlobalRef(assets);
    gPlatform->engine.manager = AAssetManager_fromJava(e, gPlatform->assetRef);
    gPlatform->projects = toString(e, projects);
    gPlatform->density = density;
}

JNIEXPORT void JNICALL BRIDGE(nativeSurfaceCreated)(JNIEnv*, jclass) {
    if (!gEditor) {
        gEditor = std::make_unique<titan::Editor>(*gPlatform);
        if (!gEditor->init()) TITAN_ERROR("android", "No se pudo iniciar el editor");
    } else {
        // GLSurfaceView creó un contexto nuevo: los objetos de la GPU anteriores ya no existen.
        gEditor->onContextLost();
        gEditor->onContextRestored();
    }
    gLast = std::chrono::steady_clock::now();
}

JNIEXPORT void JNICALL BRIDGE(nativeFrame)(JNIEnv*, jclass, jint w, jint h) {
    if (!gEditor) return;
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - gLast).count();
    gLast = now;
    gEditor->frame(w, h, dt > 0.25f ? 0.25f : dt);
}

JNIEXPORT void JNICALL BRIDGE(nativeTouch)(JNIEnv*, jclass, jint action, jint id, jfloat x, jfloat y) {
    if (!gEditor) return;
    if (action == 0) gEditor->onPointerDown(id, x, y);
    else if (action == 1) gEditor->onPointerMove(id, x, y);
    else gEditor->onPointerUp(id, x, y);
}

JNIEXPORT void JNICALL BRIDGE(nativeText)(JNIEnv* e, jclass, jstring text) {
    if (gEditor) gEditor->onText(toString(e, text));
}

JNIEXPORT void JNICALL BRIDGE(nativeKey)(JNIEnv*, jclass, jint key, jboolean down) {
    if (gEditor && key >= 0 && key <= static_cast<int>(titan::EditorKey::R))
        gEditor->onKey(static_cast<titan::EditorKey>(key), down);
}

// Teclado físico y mando mientras se juega en el editor (códigos de android.view.KeyEvent).
JNIEXPORT void JNICALL BRIDGE(nativeGameKey)(JNIEnv*, jclass, jint keycode, jboolean down) {
    if (titan::Input* in = gEditor ? gEditor->playInput() : nullptr) titan::androidinput::applyKey(*in, keycode, down);
}

JNIEXPORT void JNICALL BRIDGE(nativeGameAxes)(JNIEnv*, jclass, jfloat x, jfloat y, jfloat z, jfloat rz, jfloat lt, jfloat rt) {
    if (titan::Input* in = gEditor ? gEditor->playInput() : nullptr) titan::androidinput::applyAxes(*in, x, y, z, rz, lt, rt);
}

JNIEXPORT void JNICALL BRIDGE(nativeScroll)(JNIEnv*, jclass, jfloat x, jfloat y, jfloat amount) {
    if (gEditor) gEditor->onScroll(x, y, amount);
}

JNIEXPORT void JNICALL BRIDGE(nativePause)(JNIEnv*, jclass) {
    if (gEditor) gEditor->onPause();
}

JNIEXPORT void JNICALL BRIDGE(nativeResume)(JNIEnv*, jclass) {
    if (gEditor) gEditor->onResume();
    gLast = std::chrono::steady_clock::now();
}

JNIEXPORT jboolean JNICALL BRIDGE(nativeBack)(JNIEnv*, jclass) { return gEditor && gEditor->onBack(); }

JNIEXPORT void JNICALL BRIDGE(nativeFileImported)(JNIEnv* e, jclass, jstring path, jstring name) {
    if (gEditor) gEditor->onFileImported(toString(e, path), toString(e, name));
}

} // extern "C"
