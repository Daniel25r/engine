#pragma once
// Teclas y mandos de Android → entrada de Titán. Lo usan el reproductor (NativeActivity) y el editor (JNI).
// Los códigos son los de android.view.KeyEvent / MotionEvent (estables desde Android 3.1).
#include "titan/core/Input.h"

namespace titan::androidinput {

inline Key keyFromAndroid(int k) {
    if (k >= 29 && k <= 54) return static_cast<Key>(static_cast<int>(Key::A) + (k - 29));       // KEYCODE_A..Z
    if (k >= 7 && k <= 16) return static_cast<Key>(static_cast<int>(Key::Num0) + (k - 7));      // KEYCODE_0..9
    if (k >= 131 && k <= 142) return static_cast<Key>(static_cast<int>(Key::F1) + (k - 131));    // KEYCODE_F1..F12
    switch (k) {
    case 62: return Key::Space;
    case 66: case 160: return Key::Enter;
    case 111: return Key::Escape;
    case 61: return Key::Tab;
    case 67: return Key::Backspace;
    case 21: return Key::Left;
    case 22: return Key::Right;
    case 19: return Key::Up;
    case 20: return Key::Down;
    case 59: return Key::LeftShift;
    case 60: return Key::RightShift;
    case 113: return Key::LeftCtrl;
    case 114: return Key::RightCtrl;
    case 57: return Key::LeftAlt;
    case 58: return Key::RightAlt;
    default: return Key::Unknown;
    }
}

inline bool padButtonFromAndroid(int k, GamepadButton& b) {
    switch (k) {
    case 96: b = GamepadButton::A; return true;          // BUTTON_A
    case 97: b = GamepadButton::B; return true;
    case 99: b = GamepadButton::X; return true;
    case 100: b = GamepadButton::Y; return true;
    case 102: b = GamepadButton::LeftBumper; return true;  // BUTTON_L1
    case 103: b = GamepadButton::RightBumper; return true; // BUTTON_R1
    case 109: b = GamepadButton::Back; return true;        // BUTTON_SELECT
    case 108: b = GamepadButton::Start; return true;
    case 106: b = GamepadButton::LeftStick; return true;   // BUTTON_THUMBL
    case 107: b = GamepadButton::RightStick; return true;
    case 19: b = GamepadButton::DpadUp; return true;       // las crucetas llegan como DPAD_*
    case 20: b = GamepadButton::DpadDown; return true;
    case 21: b = GamepadButton::DpadLeft; return true;
    case 22: b = GamepadButton::DpadRight; return true;
    default: return false;
    }
}

// Una pulsación: tecla y/o botón de mando. Devuelve false si no es de ninguno de los dos.
inline bool applyKey(Input& in, int keycode, bool down) {
    bool used = false;
    const Key k = keyFromAndroid(keycode);
    if (k != Key::Unknown) { in.setKey(k, down); used = true; }
    GamepadButton b;
    if (padButtonFromAndroid(keycode, b)) { in.setGamepadButton(b, down); used = true; }
    return used;
}

// Ejes de un mando (AXIS_X, Y, Z, RZ, LTRIGGER/BRAKE, RTRIGGER/GAS). Y invertida: positivo = arriba.
inline void applyAxes(Input& in, float x, float y, float z, float rz, float lt, float rt) {
    in.setGamepadAxis(GamepadAxis::LeftX, x);
    in.setGamepadAxis(GamepadAxis::LeftY, -y);
    in.setGamepadAxis(GamepadAxis::RightX, z);
    in.setGamepadAxis(GamepadAxis::RightY, -rz);
    in.setGamepadAxis(GamepadAxis::LeftTrigger, lt);
    in.setGamepadAxis(GamepadAxis::RightTrigger, rt);
}

} // namespace titan::androidinput
