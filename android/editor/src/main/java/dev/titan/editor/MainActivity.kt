package dev.titan.editor

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import java.io.File

/** Actividad del editor de Titán. La interfaz entera la dibuja el editor en C++. */
class MainActivity : Activity() {
    private lateinit var view: EditorView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        // Los proyectos viven en el almacenamiento privado de la app: no hacen falta permisos.
        val projects = File(filesDir, "proyectos").apply { mkdirs() }
        NativeBridge.nativeCreate(this, assets, projects.absolutePath, resources.displayMetrics.density)
        view = EditorView(this)
        setContentView(view)
    }

    override fun onPause() {
        view.queueEvent { NativeBridge.nativePause() } // guarda el proyecto
        view.onPause()
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        view.onResume()
        view.queueEvent { NativeBridge.nativeResume() }
    }

    @Deprecated("Se mantiene por compatibilidad con Android 7-12")
    override fun onBackPressed() {
        view.queueEvent {
            if (!NativeBridge.nativeBack()) runOnUiThread { finish() }
        }
    }

    // ---------------------------------------------------------------- Llamadas desde C++

    fun setKeyboardVisible(visible: Boolean) = runOnUiThread {
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        if (visible) {
            view.requestFocus()
            imm.showSoftInput(view, 0)
        } else {
            imm.hideSoftInputFromWindow(view.windowToken, 0)
        }
    }

    fun requestImport() = runOnUiThread {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(intent, REQUEST_IMPORT)
    }

    fun getClipboardText(): String {
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = cm.primaryClip ?: return ""
        return if (clip.itemCount > 0) clip.getItemAt(0).coerceToText(this).toString() else ""
    }

    fun setClipboardText(text: String) = runOnUiThread {
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("Titán", text))
    }

    // ---------------------------------------------------------------- Importar archivos (Storage Access Framework)

    @Deprecated("API clásica: disponible en todas las versiones soportadas")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode != REQUEST_IMPORT || resultCode != RESULT_OK || uri == null) return
        Thread {
            val name = displayName(uri)
            val dir = File(cacheDir, "importar").apply { mkdirs() }
            val target = File(dir, name)
            contentResolver.openInputStream(uri)?.use { input -> target.outputStream().use { input.copyTo(it) } }
            view.queueEvent { NativeBridge.nativeFileImported(target.absolutePath, name) }
        }.start()
    }

    private fun displayName(uri: Uri): String {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) {
                val n = c.getString(0)
                if (!n.isNullOrBlank()) return n.replace('/', '_')
            }
        }
        return "archivo_importado"
    }

    companion object {
        private const val REQUEST_IMPORT = 42
    }
}
