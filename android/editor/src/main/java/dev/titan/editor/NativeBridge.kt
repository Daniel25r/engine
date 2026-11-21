package dev.titan.editor

import android.content.res.AssetManager

/** Funciones del editor en C++ (libtitan_editor_jni.so). Salvo nativeCreate, se llaman en el hilo de OpenGL. */
object NativeBridge {
    init {
        System.loadLibrary("titan_editor_jni")
    }

    @JvmStatic external fun nativeCreate(activity: MainActivity, assets: AssetManager, projectsDir: String, density: Float)
    @JvmStatic external fun nativeSurfaceCreated()
    @JvmStatic external fun nativeFrame(width: Int, height: Int)
    @JvmStatic external fun nativeTouch(action: Int, id: Int, x: Float, y: Float)
    @JvmStatic external fun nativeText(text: String)
    @JvmStatic external fun nativeKey(key: Int, down: Boolean)
    @JvmStatic external fun nativeScroll(x: Float, y: Float, amount: Float)
    @JvmStatic external fun nativePause()
    @JvmStatic external fun nativeResume()
    @JvmStatic external fun nativeBack(): Boolean
    @JvmStatic external fun nativeFileImported(path: String, name: String)
}
