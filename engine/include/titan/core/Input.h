#pragma once
// Estado de entrada táctil (y ratón como puntero 0). La plataforma lo rellena;
// el runtime y los scripts lo leen. Posiciones en píxeles, origen arriba a la izquierda.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

enum class TouchPhase { Began, Moved, Ended };

// Teclas (teclado físico o Bluetooth). Los nombres son los que usan los scripts: "Space", "W", "Left"...
enum class Key : uint8_t {
    Unknown, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace, Left, Right, Up, Down, LeftShift, RightShift, LeftCtrl, RightCtrl,
    LeftAlt, RightAlt, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Count
};
enum class GamepadButton : uint8_t {
    A, B, X, Y, LeftBumper, RightBumper, Back, Start, LeftStick, RightStick, DpadUp, DpadDown, DpadLeft, DpadRight, Count
};
enum class GamepadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

const char* keyName(Key k);
Key keyFromName(const std::string& name);   // "Space", "space", "W", "LeftShift"... Key::Unknown si no existe
const char* gamepadButtonName(GamepadButton b);
bool gamepadButtonFromName(const std::string& name, GamepadButton& out);

struct TouchEvent {
    TouchPhase phase;
    int id;
    Vec2 position;
    bool ui = false; // el dedo empezó sobre un botón o joystick de la interfaz
};

struct Pointer {
    int id;
    Vec2 position;
    Vec2 start;
    bool justBegan; // empezó en este paso de lógica
    bool ui = false; // lo está usando la interfaz (botón, joystick): los scripts no lo ven como toque del juego
};

class Input {
public:
    const std::vector<TouchEvent>& events() const { return events_; }
    const std::vector<Pointer>& pointers() const { return pointers_; }

    // --- Lo llama la plataforma ---
    void touchBegan(int id, Vec2 p) {
        removePointer(id);
        pointers_.push_back({id, p, p, true, false});
        events_.push_back({TouchPhase::Began, id, p, false});
    }
    void touchMoved(int id, Vec2 p) {
        for (auto& ptr : pointers_)
            if (ptr.id == id && !(ptr.position == p)) {
                ptr.position = p;
                events_.push_back({TouchPhase::Moved, id, p, ptr.ui});
            }
    }
    void touchEnded(int id, Vec2 p) {
        bool ui = false;
        for (auto& ptr : pointers_)
            if (ptr.id == id) ui = ptr.ui;
        if (removePointer(id)) events_.push_back({TouchPhase::Ended, id, p, ui});
    }
    void cancelAll() {
        for (auto& ptr : pointers_) events_.push_back({TouchPhase::Ended, ptr.id, ptr.position, ptr.ui});
        pointers_.clear();
        releaseAllKeys(); // al pausar la app no deben quedarse teclas "pulsadas"
    }
    // --- Teclado y mando (los llama la plataforma) ---
    void setKey(Key k, bool down) {
        if (k != Key::Unknown && k != Key::Count) keys_[static_cast<size_t>(k)] = down;
    }
    void setGamepadButton(GamepadButton b, bool down) {
        if (b != GamepadButton::Count) pad_[static_cast<size_t>(b)] = down;
        gamepadConnected_ = true;
    }
    // Ejes de -1 a 1 (Y del stick: positivo = arriba); gatillos de 0 a 1.
    void setGamepadAxis(GamepadAxis a, float v) {
        if (a != GamepadAxis::Count) axes_[static_cast<size_t>(a)] = v;
        gamepadConnected_ = true;
    }
    void setGamepadConnected(bool c) {
        gamepadConnected_ = c;
        if (!c) { pad_.fill(false); axes_.fill(0); }
    }
    bool keyDown(Key k) const { return k < Key::Count && keys_[static_cast<size_t>(k)]; }
    bool keyPressed(Key k) const { return keyDown(k) && !prevKeys_[static_cast<size_t>(k)]; }
    bool keyReleased(Key k) const { return k < Key::Count && !keys_[static_cast<size_t>(k)] && prevKeys_[static_cast<size_t>(k)]; }
    bool buttonDown(GamepadButton b) const { return b < GamepadButton::Count && pad_[static_cast<size_t>(b)]; }
    bool buttonPressed(GamepadButton b) const { return buttonDown(b) && !prevPad_[static_cast<size_t>(b)]; }
    float axis(GamepadAxis a) const { return a < GamepadAxis::Count ? axes_[static_cast<size_t>(a)] : 0.0f; }
    bool gamepadConnected() const { return gamepadConnected_; }
    void releaseAllKeys() { keys_.fill(false); pad_.fill(false); axes_.fill(0); }

    // --- Lo llama la interfaz de juego: ese dedo es suyo ---
    void captureForUI(int id) {
        for (auto& ptr : pointers_)
            if (ptr.id == id) ptr.ui = true;
        for (auto& ev : events_)
            if (ev.id == id) ev.ui = true;
    }
    // --- Lo llama el runtime al terminar el fotograma ---
    void endFrame() {
        events_.clear();
        for (auto& p : pointers_) p.justBegan = false;
        prevKeys_ = keys_;
        prevPad_ = pad_;
    }

private:
    bool removePointer(int id) {
        auto it = std::find_if(pointers_.begin(), pointers_.end(), [id](const Pointer& p) { return p.id == id; });
        if (it == pointers_.end()) return false;
        pointers_.erase(it);
        return true;
    }
    std::vector<TouchEvent> events_;
    std::vector<Pointer> pointers_;
    std::array<bool, static_cast<size_t>(Key::Count)> keys_{}, prevKeys_{};
    std::array<bool, static_cast<size_t>(GamepadButton::Count)> pad_{}, prevPad_{};
    std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes_{};
    bool gamepadConnected_ = false;
};

} // namespace titan
