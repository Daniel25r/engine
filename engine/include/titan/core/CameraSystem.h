#pragma once
// Sistema de cámaras de seguimiento (CameraFollow): gira con el dedo o el mando, evita paredes y suaviza.
#include <unordered_map>

#include "titan/core/Input.h"
#include "titan/core/InputActions.h"
#include "titan/core/PhysicsApi.h"
#include "titan/core/Scene.h"

namespace titan {

class CameraSystem {
public:
    // Una vez por fotograma, después de la lógica y la física y antes del render.
    void update(Scene& scene, const Input& input, const InputActions* actions, const PhysicsApi* physics, float dt);
    void reset() { lastPointer_.clear(); }

private:
    std::unordered_map<int, Vec2> lastPointer_; // posición anterior de cada dedo del juego (para arrastrar)
    uint32_t rng_ = 1234567u;
};

} // namespace titan
