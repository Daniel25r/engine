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
};

struct Pointer {
    int id;
    Vec2 position;
    Vec2 start;
    bool justBegan; // empezó en este paso de lógica
};

class Input {
public:
    const std::vector<TouchEvent>& events() const { return events_; }
    const std::vector<Pointer>& pointers() const { return pointers_; }

    // --- Lo llama la plataforma ---
    void touchBegan(int id, Vec2 p) {
        removePointer(id);
        pointers_.push_back({id, p, p, true});
        events_.push_back({TouchPhase::Began, id, p});
    }
    void touchMoved(int id, Vec2 p) {
        for (auto& ptr : pointers_)
            if (ptr.id == id && !(ptr.position == p)) {
                ptr.position = p;
                events_.push_back({TouchPhase::Moved, id, p});
            }
    }
    void touchEnded(int id, Vec2 p) {
        if (removePointer(id)) events_.push_back({TouchPhase::Ended, id, p});
    }
    void cancelAll() {
        for (auto& ptr : pointers_) events_.push_back({TouchPhase::Ended, ptr.id, ptr.position});
        pointers_.clear();
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
