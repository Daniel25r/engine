#pragma once
#include <algorithm>
#include <vector>

#include "forja/Math.h"

namespace forja {

enum class TouchPhase { Began, Moved, Ended };

// Posiciones en píxeles de pantalla, origen arriba a la izquierda.
// Usa Camera2D::screenToWorld() para pasarlas al mundo.
struct TouchEvent {
    TouchPhase phase;
    int id;
    Vec2 position;
};

struct Pointer {
    int id;
    Vec2 position;
    Vec2 start;
};

class Input {
public:
    // Eventos ocurridos desde el último paso de lógica.
    const std::vector<TouchEvent>& events() const { return events_; }
    // Dedos que están tocando la pantalla ahora mismo.
    const std::vector<Pointer>& pointers() const { return pointers_; }

    // --- Lo llama la capa de plataforma ---
    void touchBegan(int id, Vec2 p) {
        removePointer(id);
        pointers_.push_back({id, p, p});
        events_.push_back({TouchPhase::Began, id, p});
    }
    void touchMoved(int id, Vec2 p) {
        for (auto& ptr : pointers_)
            if (ptr.id == id && (ptr.position.x != p.x || ptr.position.y != p.y)) {
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
    void clearEvents() { events_.clear(); }

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

} // namespace forja
