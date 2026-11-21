#pragma once
// Estado de entrada táctil (y ratón como puntero 0). La plataforma lo rellena;
// el runtime y los scripts lo leen. Posiciones en píxeles, origen arriba a la izquierda.
#include <algorithm>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

enum class TouchPhase { Began, Moved, Ended };

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
    }
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
};

} // namespace titan
