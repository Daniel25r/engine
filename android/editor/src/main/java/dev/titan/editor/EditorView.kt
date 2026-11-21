package dev.titan.editor

import android.content.Context
import android.opengl.GLSurfaceView
import android.text.InputType
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Superficie OpenGL ES 3 donde dibuja el editor. Reenvía al C++ el táctil multipunto,
 * el teclado en pantalla y el teclado físico. Todo se encola en el hilo de OpenGL.
 */
class EditorView(context: Context) : GLSurfaceView(context) {
    private var surfaceWidth = 1
    private var surfaceHeight = 1

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 24, 0)
        preserveEGLContextOnPause = true
        setRenderer(object : Renderer {
            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) = NativeBridge.nativeSurfaceCreated()
            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                surfaceWidth = width
                surfaceHeight = height
            }
            override fun onDrawFrame(gl: GL10?) = NativeBridge.nativeFrame(surfaceWidth, surfaceHeight)
        })
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val mask = e.actionMasked
        when (mask) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN,
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i)
                val y = e.getY(i)
                val action = if (mask == MotionEvent.ACTION_DOWN || mask == MotionEvent.ACTION_POINTER_DOWN) 0 else 2
                queueEvent { NativeBridge.nativeTouch(action, id, x, y) }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val x = e.getX(i)
                    val y = e.getY(i)
                    queueEvent { NativeBridge.nativeTouch(1, id, x, y) }
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val x = e.getX(i)
                    val y = e.getY(i)
                    queueEvent { NativeBridge.nativeTouch(2, id, x, y) }
                }
            }
        }
        return true
    }

    override fun onGenericMotionEvent(e: MotionEvent): Boolean {
        if (e.actionMasked == MotionEvent.ACTION_SCROLL) {
            val amount = e.getAxisValue(MotionEvent.AXIS_VSCROLL)
            val x = e.x
            val y = e.y
            queueEvent { NativeBridge.nativeScroll(x, y, amount) }
            return true
        }
        return super.onGenericMotionEvent(e)
    }

    // ---------------------------------------------------------------- Teclado en pantalla

    override fun onCheckIsTextEditor() = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        // Sin sugerencias ni texto "en composición": el teclado entrega cada carácter al momento,
        // que es lo que necesita un editor de código.
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_FLAG_NO_FULLSCREEN or
            EditorInfo.IME_ACTION_NONE
        return object : BaseInputConnection(this, false) {
            override fun commitText(text: CharSequence, newCursorPosition: Int): Boolean {
                sendText(text.toString())
                return true
            }
            override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                repeat(beforeLength.coerceAtLeast(1)) { sendKey(KEY_BACKSPACE) }
                repeat(afterLength) { sendKey(KEY_DELETE) }
                return true
            }
            override fun sendKeyEvent(event: KeyEvent): Boolean {
                handleKey(event)
                return true
            }
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean = handleKey(event) || super.onKeyDown(keyCode, event)
    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean = handleKey(event) || super.onKeyUp(keyCode, event)

    private fun sendText(text: String) {
        val clean = text.replace("\r", "")
        if (clean.isEmpty()) return
        // El salto de línea llega como texto en algunos teclados: se convierte en la tecla Intro.
        val parts = clean.split("\n")
        parts.forEachIndexed { i, part ->
            if (part.isNotEmpty()) queueEvent { NativeBridge.nativeText(part) }
            if (i < parts.size - 1) sendKey(KEY_ENTER)
        }
    }

    private fun sendKey(key: Int) {
        queueEvent {
            NativeBridge.nativeKey(key, true)
            NativeBridge.nativeKey(key, false)
        }
    }

    private fun handleKey(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK) return false
        val down = event.action != KeyEvent.ACTION_UP
        val key = mapKey(event.keyCode)
        if (key >= 0) queueEvent { NativeBridge.nativeKey(key, down) }
        // Texto de teclados físicos (sin Ctrl, que se reserva para atajos).
        if (down && !event.isCtrlPressed && key != KEY_ENTER && key != KEY_TAB && key != KEY_BACKSPACE && key != KEY_DELETE) {
            val c = event.getUnicodeChar(event.metaState)
            if (c > 0) {
                val s = String(Character.toChars(c))
                queueEvent { NativeBridge.nativeText(s) }
            }
        }
        if (event.action == KeyEvent.ACTION_MULTIPLE && event.characters != null) sendText(event.characters)
        return key >= 0 || event.unicodeChar > 0
    }

    companion object {
        // Mismo orden que titan::EditorKey.
        const val KEY_TAB = 0
        const val KEY_BACKSPACE = 10
        const val KEY_ENTER = 11
        const val KEY_DELETE = 9

        fun mapKey(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_TAB -> 0
            KeyEvent.KEYCODE_DPAD_LEFT -> 1
            KeyEvent.KEYCODE_DPAD_RIGHT -> 2
            KeyEvent.KEYCODE_DPAD_UP -> 3
            KeyEvent.KEYCODE_DPAD_DOWN -> 4
            KeyEvent.KEYCODE_PAGE_UP -> 5
            KeyEvent.KEYCODE_PAGE_DOWN -> 6
            KeyEvent.KEYCODE_MOVE_HOME -> 7
            KeyEvent.KEYCODE_MOVE_END -> 8
            KeyEvent.KEYCODE_FORWARD_DEL -> 9
            KeyEvent.KEYCODE_DEL -> 10
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 11
            KeyEvent.KEYCODE_ESCAPE -> 12
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 13
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 14
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> 15
            KeyEvent.KEYCODE_A -> 16
            KeyEvent.KEYCODE_C -> 17
            KeyEvent.KEYCODE_V -> 18
            KeyEvent.KEYCODE_X -> 19
            KeyEvent.KEYCODE_Y -> 20
            KeyEvent.KEYCODE_Z -> 21
            KeyEvent.KEYCODE_S -> 22
            KeyEvent.KEYCODE_D -> 23
            KeyEvent.KEYCODE_F -> 24
            KeyEvent.KEYCODE_P -> 25
            KeyEvent.KEYCODE_N -> 26
            KeyEvent.KEYCODE_Q -> 27
            KeyEvent.KEYCODE_W -> 28
            KeyEvent.KEYCODE_E -> 29
            KeyEvent.KEYCODE_R -> 30
            else -> -1
        }
    }
}
