#pragma once
// Interfaz de física que ven los scripts y el editor. La implementa titan_physics (Jolt);
// así el resto del motor no depende de la biblioteca de física concreta.
#include <cstdint>
#include <vector>

#include "titan/core/Math.h"
#include "titan/core/World.h"

namespace titan {

struct RaycastHit {
    Entity entity;
    Vec3 point;
    Vec3 normal;
    float distance = 0;
};

class PhysicsApi {
public:
    virtual ~PhysicsApi() = default;
    virtual bool hasBody(Entity e) const = 0;
    virtual void addForce(Entity e, const Vec3& force, bool impulse) = 0;
    virtual void addTorque(Entity e, const Vec3& torque) = 0;
    virtual void setVelocity(Entity e, const Vec3& velocity) = 0;
    virtual Vec3 velocity(Entity e) const = 0;
    // Mueve el cuerpo al instante (y su Transform), sin atravesar el tiempo de simulación.
    virtual void teleport(Entity e, const Vec3& position) = 0;
    // ignore: entidad que el rayo atraviesa (normalmente quien dispara).
    virtual bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                         Entity ignore = {}) const = 0;
    virtual void setGravity(const Vec3& g) = 0;
    virtual Vec3 gravity() const = 0;

    // ---- Capas y consultas (0.13). layerMask: bit i = capa i (0xFFFF = todas). ----
    virtual void setLayerCollision(int a, int b, bool collide) = 0;
    virtual bool layersCollide(int a, int b) const = 0;
    virtual bool raycastMasked(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                               uint32_t layerMask, Entity ignore = {}) const = 0;
    // Lanza una esfera a lo largo de una dirección: lo primero que tocaría.
    virtual bool sphereCast(const Vec3& origin, float radius, const Vec3& direction, float maxDistance, RaycastHit& hit,
                            uint32_t layerMask = 0xFFFF, Entity ignore = {}) const = 0;
    // Entidades cuyo collider toca la esfera o la caja (sin repetir).
    virtual std::vector<Entity> overlapSphere(const Vec3& center, float radius, uint32_t layerMask = 0xFFFF) const = 0;
    virtual std::vector<Entity> overlapBox(const Vec3& center, const Vec3& halfExtents, const Quat& rotation,
                                           uint32_t layerMask = 0xFFFF) const = 0;
    // ---- Controlador de personaje ----
    virtual bool isCharacter(Entity e) const = 0;
};

} // namespace titan
