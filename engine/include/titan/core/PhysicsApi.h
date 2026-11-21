#pragma once
// Interfaz de física que ven los scripts y el editor. La implementa titan_physics (Jolt);
// así el resto del motor no depende de la biblioteca de física concreta.
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
};

} // namespace titan
