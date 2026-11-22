#pragma once
// Traducción de teclas y mandos de GLFW a la entrada de Titán (reproductor y editor de escritorio).
#include <GLFW/glfw3.h>

#include "titan/core/Input.h"

namespace titan::glfwinput {

inline Key keyFromGlfw(int k) {
    if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z) return static_cast<Key>(static_cast<int>(Key::A) + (k - GLFW_KEY_A));
    if (k >= GLFW_KEY_0 && k <= GLFW_KEY_9) return static_cast<Key>(static_cast<int>(Key::Num0) + (k - GLFW_KEY_0));
    if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (k - GLFW_KEY_F1));
    switch (k) {
    case GLFW_KEY_SPACE: return Key::Space;
    case GLFW_KEY_ENTER: return Key::Enter;
    case GLFW_KEY_ESCAPE: return Key::Escape;
    case GLFW_KEY_TAB: return Key::Tab;
    case GLFW_KEY_BACKSPACE: return Key::Backspace;
    case GLFW_KEY_LEFT: return Key::Left;
    case GLFW_KEY_RIGHT: return Key::Right;
    case GLFW_KEY_UP: return Key::Up;
    case GLFW_KEY_DOWN: return Key::Down;
    case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return Key::LeftCtrl;
    case GLFW_KEY_RIGHT_CONTROL: return Key::RightCtrl;
    case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
    case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
    default: return Key::Unknown;
    }
}

// Lee el primer mando con mapeo estándar (GLFW 3.3+). Y de los sticks invertida: positivo = arriba.
inline void pollGamepad(Input& in) {
    GLFWgamepadstate st;
    if (!glfwJoystickIsGamepad(GLFW_JOYSTICK_1) || !glfwGetGamepadState(GLFW_JOYSTICK_1, &st)) {
        if (in.gamepadConnected()) in.setGamepadConnected(false);
        return;
    }
    static const int buttons[] = {GLFW_GAMEPAD_BUTTON_A, GLFW_GAMEPAD_BUTTON_B, GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_Y,
                                  GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GLFW_GAMEPAD_BUTTON_BACK,
                                  GLFW_GAMEPAD_BUTTON_START, GLFW_GAMEPAD_BUTTON_LEFT_THUMB, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,
                                  GLFW_GAMEPAD_BUTTON_DPAD_UP, GLFW_GAMEPAD_BUTTON_DPAD_DOWN, GLFW_GAMEPAD_BUTTON_DPAD_LEFT,
                                  GLFW_GAMEPAD_BUTTON_DPAD_RIGHT};
    for (int i = 0; i < static_cast<int>(GamepadButton::Count); ++i)
        in.setGamepadButton(static_cast<GamepadButton>(i), st.buttons[buttons[i]] == GLFW_PRESS);
    in.setGamepadAxis(GamepadAxis::LeftX, st.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
    in.setGamepadAxis(GamepadAxis::LeftY, -st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
    in.setGamepadAxis(GamepadAxis::RightX, st.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
    in.setGamepadAxis(GamepadAxis::RightY, -st.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
    in.setGamepadAxis(GamepadAxis::LeftTrigger, (st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1) * 0.5f);
    in.setGamepadAxis(GamepadAxis::RightTrigger, (st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1) * 0.5f);
}

} // namespace titan::glfwinput
