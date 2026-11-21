#pragma once
// Una escena: entidades, sus componentes y su jerarquía.
// Scene es la única que crea/destruye entidades y cambia padres, para mantener
// siempre coherentes EntityInfo, Hierarchy, Transform y el índice por UUID.
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/Components.h"
#include "titan/core/Events.h"
#include "titan/core/World.h"

namespace titan {

class Scene {
public:
    explicit Scene(std::string name = "Escena", EventBus* events = nullptr);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    World& world() { return world_; }
    EventBus* events() const { return events_; }

    // Crea una entidad con EntityInfo, Hierarchy y Transform. Si id es nulo se genera uno.
    // Devuelve una entidad nula si el id ya existe en la escena.
    Entity createEntity(const std::string& name, Entity parent = {}, UUID id = {});

    // Destrucción diferida (con sus hijos): la entidad sigue siendo válida hasta flushDestroyed().
    // Es la forma segura de destruir desde scripts o sistemas mientras se recorre la escena.
    void destroyEntity(Entity e);
    void flushDestroyed();
    // Destrucción inmediata (con sus hijos). Solo cuando nadie está recorriendo la escena.
    void destroyEntityImmediate(Entity e);
    void clear();

    bool isValid(Entity e) const { return world_.alive(e); }
    size_t entityCount() const { return world_.count(); }

    // Cambia el padre (parent nulo = raíz). Rechaza ciclos. Si keepWorldTransform,
    // recalcula la transformación local para que el objeto no se mueva en el mundo.
    bool setParent(Entity child, Entity parent, bool keepWorldTransform = true);
    Entity parent(Entity e) const;
    std::vector<Entity> children(Entity e) const;
    std::vector<Entity> roots() const;
    bool isDescendantOf(Entity e, Entity ancestor) const;
    // Activa si ella y todos sus antepasados están activos.
    bool isActiveInHierarchy(Entity e) const;

    Entity findById(UUID id) const;
    Entity findByName(const std::string& name) const;

    EntityInfo& info(Entity e) { return *world_.get<EntityInfo>(e); }
    Transform& transform(Entity e) { return *world_.get<Transform>(e); }

    // Matriz de mundo calculada en el momento, recorriendo los padres (no usa la caché).
    Mat4 computeWorldMatrix(Entity e) const;

    // Recorre la jerarquía en profundidad, en orden: fn(Entity, int profundidad).
    template <class F>
    void traverse(F&& fn) const {
        for (Entity r = firstRoot_; !r.isNull(); r = world_.get<Hierarchy>(r)->nextSibling) traverseFrom(r, 0, fn);
    }

private:
    template <class F>
    void traverseFrom(Entity e, int depth, F& fn) const {
        fn(e, depth);
        for (Entity c = world_.get<Hierarchy>(e)->firstChild; !c.isNull(); c = world_.get<Hierarchy>(c)->nextSibling)
            traverseFrom(c, depth + 1, fn);
    }
    void link(Entity child, Entity parent);
    void unlink(Entity child);
    void collectSubtree(Entity e, std::vector<Entity>& out) const;

    std::string name_;
    EventBus* events_;
    World world_;
    std::unordered_map<UUID, Entity> byId_;
    std::vector<Entity> pendingDestroy_;
    Entity firstRoot_;
};

// Recalcula las matrices de mundo de las entidades modificadas y de sus descendientes.
class TransformSystem {
public:
    // Devuelve cuántas matrices se recalcularon (para el perfilador y las pruebas).
    static size_t update(Scene& scene);
};

} // namespace titan
