#pragma once
// Física 3D de Titán sobre Jolt Physics (MIT). Jolt queda oculto aquí dentro: los juegos y el editor
// solo ven los componentes Rigidbody/Collider y la interfaz PhysicsApi.
//
// Ciclo: build(scene) crea un cuerpo por cada entidad con Collider → step(dt) a paso fijo:
//   cuerpos cinemáticos siguen a su Transform, se simula, los dinámicos escriben su Transform y se
//   recogen los contactos que empiezan y terminan (para onCollisionEnter/Exit y onTriggerEnter/Exit).
#include <memory>
#include <vector>

#include "titan/core/PhysicsApi.h"
#include "titan/core/Scene.h"

namespace titan {

struct ContactEvent {
    Entity a, b;
    bool begin;   // true = empieza, false = termina
    bool trigger; // uno de los dos es un sensor
};

class PhysicsWorld final : public PhysicsApi {
public:
    PhysicsWorld();
    ~PhysicsWorld() override;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Crea los cuerpos de todas las entidades con Collider (activas). Borra los anteriores.
    void build(Scene& scene);
    void clear();
    void step(Scene& scene, float dt);
    void removeEntity(Entity e);
    std::vector<ContactEvent> takeEvents();
    size_t bodyCount() const;

    // PhysicsApi
    bool hasBody(Entity e) const override;
    void addForce(Entity e, const Vec3& force, bool impulse) override;
    void addTorque(Entity e, const Vec3& torque) override;
    void setVelocity(Entity e, const Vec3& velocity) override;
    Vec3 velocity(Entity e) const override;
    void teleport(Entity e, const Vec3& position) override;
    bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                 Entity ignore = {}) const override;
    void setGravity(const Vec3& g) override;
    Vec3 gravity() const override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace titan
