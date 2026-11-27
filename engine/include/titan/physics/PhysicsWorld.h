#pragma once
// Física 3D de Titán sobre Jolt Physics (MIT). Jolt queda oculto aquí dentro: los juegos y el editor
// solo ven los componentes Rigidbody/Collider y la interfaz PhysicsApi.
//
// Ciclo: build(scene) crea un cuerpo por cada entidad con Collider → step(dt) a paso fijo:
//   cuerpos cinemáticos siguen a su Transform, se simula, los dinámicos escriben su Transform y se
//   recogen los contactos que empiezan y terminan (para onCollisionEnter/Exit y onTriggerEnter/Exit).
#include <functional>
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
    // Triángulos para los colliders de malla y envolvente convexa de una entidad (espacio local de la entidad,
    // sin su escala). Lo conecta quien tiene los modelos (runtime o editor). Sin él, esos colliders no se crean.
    using MeshProvider = std::function<bool(Entity, std::vector<Vec3>& verts, std::vector<uint32_t>& indices)>;
    void setMeshProvider(MeshProvider p) { meshProvider_ = std::move(p); }

    PhysicsWorld();
    ~PhysicsWorld() override;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Crea los cuerpos de todas las entidades con Collider (activas). Borra los anteriores.
    void build(Scene& scene);
    void clear();
    void step(Scene& scene, float dt);
    void removeEntity(Entity e);
    // Crea los cuerpos de una entidad nueva y de sus hijos (prefabs instanciados durante el juego).
    void addSubtree(Scene& scene, Entity root);
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
    void setLayerCollision(int a, int b, bool collide) override;
    bool layersCollide(int a, int b) const override;
    bool raycastMasked(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit, uint32_t layerMask,
                       Entity ignore = {}) const override;
    bool sphereCast(const Vec3& origin, float radius, const Vec3& direction, float maxDistance, RaycastHit& hit,
                    uint32_t layerMask = 0xFFFF, Entity ignore = {}) const override;
    std::vector<Entity> overlapSphere(const Vec3& center, float radius, uint32_t layerMask = 0xFFFF) const override;
    std::vector<Entity> overlapBox(const Vec3& center, const Vec3& halfExtents, const Quat& rotation,
                                   uint32_t layerMask = 0xFFFF) const override;
    bool isCharacter(Entity e) const override;

    struct Impl;

private:
    void createBodies(Scene& scene, const std::vector<Entity>& list);
    void createCharacter(Scene& scene, Entity e, const CharacterController& cc);
    void createJoints(Scene& scene);
    void createTerrainBody(Scene& scene, Entity e, const Terrain& t);
    std::unique_ptr<Impl> impl_;
    MeshProvider meshProvider_;
};

} // namespace titan
