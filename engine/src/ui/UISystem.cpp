#include "titan/ui/UISystem.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace titan {

namespace {
bool contains(const Rect& r, Vec2 p) { return p.x >= r.x && p.y >= r.y && p.x <= r.x + r.w && p.y <= r.y + r.h; }
} // namespace

Rect UISystem::computeRect(const UIRect& u, const Rect& parent, float scale) {
    // Igual que el RectTransform de Unity: el área de los anclajes más "size" (sizeDelta), con el pivote
    // colocado en el punto del área indicado por el propio pivote, desplazado por "position".
    const float ax0 = parent.x + u.anchorMin.x * parent.w, ax1 = parent.x + u.anchorMax.x * parent.w;
    const float ay0 = parent.y + u.anchorMin.y * parent.h, ay1 = parent.y + u.anchorMax.y * parent.h;
    const float w = std::max(0.0f, (ax1 - ax0) + u.size.x * scale);
    const float h = std::max(0.0f, (ay1 - ay0) + u.size.y * scale);
    const float px = ax0 + (ax1 - ax0) * u.pivot.x + u.position.x * scale;
    const float py = ay0 + (ay1 - ay0) * u.pivot.y + u.position.y * scale;
    return {px - u.pivot.x * w, py - u.pivot.y * h, w, h};
}

void UISystem::layout(const Scene& scene, Vec2 screen) {
    items_.clear();
    scale_ = scaleFor(screen);
    const Rect screenRect{0, 0, screen.x, screen.y};
    const World& w = scene.world();
    std::function<void(Entity, const Rect&)> visit = [&](Entity e, const Rect& parent) {
        const EntityInfo* info = w.get<EntityInfo>(e);
        if (info && !info->active) return; // inactiva: ni ella ni sus hijos
        Rect mine = parent;
        if (const UIRect* r = w.get<UIRect>(e)) {
            mine = computeRect(*r, parent, scale_);
            items_.push_back({e, mine});
        }
        for (Entity c : scene.children(e)) visit(c, mine);
    };
    for (Entity r : scene.roots()) visit(r, screenRect);
}

bool UISystem::rectOf(Entity e, Rect& out) const {
    for (const UIItem& it : items_)
        if (it.entity == e) { out = it.rect; return true; }
    return false;
}

Entity UISystem::pick(const Scene& scene, Vec2 p, bool interactiveOnly) const {
    const World& w = scene.world();
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        if (!contains(it->rect, p)) continue;
        if (interactiveOnly) {
            const UIButton* b = w.get<UIButton>(it->entity);
            if (!(b && b->interactable) && !w.has<UIJoystick>(it->entity)) continue;
        }
        return it->entity;
    }
    return {};
}

void UISystem::update(Scene& scene, Input& input, std::vector<Entity>* clicked) {
    World& w = scene.world();
    auto pointerPos = [&](int id, Vec2& p) {
        for (const Pointer& ptr : input.pointers())
            if (ptr.id == id) { p = ptr.position; return true; }
        return false;
    };
    w.each<UIButton>([](Entity, UIButton& b) { b.clicked = false; });

    // 1. Dedos que se levantan: un botón cuenta como pulsado si se suelta encima (como en Unity).
    for (const TouchEvent& ev : input.events()) {
        if (ev.phase != TouchPhase::Ended) continue;
        w.each<UIButton>([&](Entity e, UIButton& b) {
            if (b.pointer != ev.id) return;
            Rect r;
            if (b.interactable && rectOf(e, r) && contains(r, ev.position)) {
                b.clicked = true;
                if (clicked) clicked->push_back(e);
            }
            b.pointer = -1;
            b.pressed = false;
        });
        w.each<UIJoystick>([&](Entity, UIJoystick& j) {
            if (j.pointer == ev.id) { j.pointer = -1; j.value = {}; }
        });
    }

    // 2. Dedos nuevos sobre un botón o joystick.
    for (const TouchEvent& ev : input.events()) {
        if (ev.phase != TouchPhase::Began) continue;
        Vec2 now;
        if (!pointerPos(ev.id, now)) continue; // empezó y terminó en el mismo fotograma: ya se trató arriba
        const Entity target = pick(scene, ev.position, true);
        if (target.isNull()) continue;
        if (UIButton* b = w.get<UIButton>(target); b && b->interactable) b->pointer = ev.id;
        else if (UIJoystick* j = w.get<UIJoystick>(target)) j->pointer = ev.id;
        input.captureForUI(ev.id);
    }

    // 3. Estado mientras el dedo sigue en la pantalla.
    w.each<UIButton>([&](Entity e, UIButton& b) {
        Vec2 p;
        Rect r;
        if (b.pointer >= 0 && (!pointerPos(b.pointer, p) || !b.interactable || !rectOf(e, r))) b.pointer = -1;
        b.pressed = b.pointer >= 0 && contains(r, p);
    });
    w.each<UIJoystick>([&](Entity e, UIJoystick& j) {
        Vec2 p;
        Rect r;
        if (j.pointer < 0 || !pointerPos(j.pointer, p) || !rectOf(e, r)) {
            j.pointer = -1;
            j.value = {};
            return;
        }
        const float radius = std::max(1.0f, std::min(r.w, r.h) * 0.5f);
        Vec2 d{(p.x - (r.x + r.w * 0.5f)) / radius, -(p.y - (r.y + r.h * 0.5f)) / radius};
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0f) d = {d.x / len, d.y / len};
        if (len < j.deadZone) d = {};
        j.value = d;
    });
}

} // namespace titan
