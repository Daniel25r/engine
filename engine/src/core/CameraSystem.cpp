#include "titan/core/CameraSystem.h"

#include <algorithm>
#include <cmath>

namespace titan {

namespace {
// Orientación que mira desde "eye" hacia "target" (la cámara mira hacia su -Z local).
Quat lookRotation(const Vec3& eye, const Vec3& target) {
    const Vec3 f = (target - eye).normalized();
    const float yaw = std::atan2(-f.x, -f.z);
    const float pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    return Quat::fromEuler({pitch, yaw, 0});
}
} // namespace

void CameraSystem::update(Scene& scene, const Input& input, const InputActions* actions, const PhysicsApi* physics, float dt) {
    // Arrastre de un dedo que no está usando la UI (el joystick y los botones se quedan sus dedos).
    Vec2 drag;
    int gameFingers = 0;
    for (const Pointer& p : input.pointers()) {
        if (p.ui) continue;
        ++gameFingers;
        auto it = lastPointer_.find(p.id);
        if (it != lastPointer_.end() && gameFingers == 1) drag = drag + (p.position - it->second);
    }
    lastPointer_.clear();
    for (const Pointer& p : input.pointers())
        if (!p.ui) lastPointer_[p.id] = p.position;
    const Vec2 look = actions ? actions->axis("Mirar") : Vec2{};

    scene.world().each<CameraFollow>([&](Entity cam, CameraFollow& c) {
        if (!scene.isActiveInHierarchy(cam) || c.target.isNull()) return;
        const Entity target = scene.findById(c.target);
        if (target.isNull()) return;
        if (c.rotateWithInput) {
            c.yaw -= drag.x * c.touchSensitivity;
            c.pitch += drag.y * c.touchSensitivity;
            c.yaw -= look.x * c.padSpeed * dt;
            c.pitch += -look.y * c.padSpeed * dt;
        }
        c.pitch = std::clamp(c.pitch, c.minPitch, c.maxPitch);
        c.distance = std::clamp(c.distance, c.minDistance, std::max(c.minDistance, c.maxDistance));

        const Vec3 pivot = scene.computeWorldMatrix(target).translation() + Vec3{0, c.height, 0};
        const float yaw = c.yaw * kDegToRad, pitch = c.pitch * kDegToRad;
        const Vec3 back{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        float dist = c.distance;
        bool blocked = false;
        if (c.avoidWalls && physics) { // una esfera desde el objetivo hacia la cámara: si choca, la cámara se acerca
            RaycastHit hit;
            if (physics->sphereCast(pivot, c.wallRadius, back, c.distance, hit, 0xFFFF, target)) {
                dist = std::max(0.3f, hit.distance - 0.05f);
                blocked = true;
            }
        }
        const Vec3 desired = pivot + back * dist;
        if (!c.initialized || c.smoothing <= 0) {
            c.position = desired;
            c.initialized = true;
        } else if (blocked && (desired - pivot).length() < (c.position - pivot).length()) {
            c.position = desired; // acercarse por una pared es inmediato (si no, se vería a través de ella)
        } else {
            const float k = 1.0f - std::exp(-c.smoothing * dt);
            c.position = c.position + (desired - c.position) * k;
        }
        Vec3 eye = c.position;
        if (c.shakeTime > 0) { // temblor (golpes, explosiones)
            c.shakeTime -= dt;
            auto r = [&] {
                rng_ = rng_ * 1664525u + 1013904223u;
                return ((rng_ >> 9) & 0xFFFF) / 32768.0f - 1.0f;
            };
            eye = eye + Vec3{r(), r(), r()} * c.shake;
        }
        Transform& t = scene.transform(cam);
        const Entity parent = scene.parent(cam);
        const Quat rot = lookRotation(eye, pivot);
        if (parent.isNull()) {
            t.setPosition(eye);
            t.setRotation(rot);
        } else {
            Vec3 pp, ps;
            Quat pq;
            const Mat4 pw = scene.computeWorldMatrix(parent);
            pw.decompose(pp, pq, ps);
            t.setPosition(pw.inverted().transformPoint(eye));
            t.setRotation(pq.conjugate() * rot);
        }
    });
}

} // namespace titan
