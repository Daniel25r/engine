#include "titan/core/Scene.h"

#include <algorithm>

#include "titan/core/Log.h"

namespace titan {

Scene::Scene(std::string name, EventBus* events) : name_(std::move(name)), events_(events) {}

Scene::~Scene() {
    // Al destruir la escena no se emiten eventos: los sistemas suscritos pueden haber muerto ya.
    events_ = nullptr;
    clear();
}

Entity Scene::createEntity(const std::string& name, Entity parent, UUID id) {
    if (id.isNull()) {
        do id = UUID::generate();
        while (byId_.count(id));
    } else if (byId_.count(id)) {
        TITAN_ERROR("scene", "Ya existe una entidad con id %s en la escena '%s'", id.toString().c_str(), name_.c_str());
        return {};
    }
    if (!parent.isNull() && !isValid(parent)) parent = {};

    Entity e = world_.create();
    world_.add<EntityInfo>(e, {id, name, true});
    world_.add<Hierarchy>(e);
    world_.add<Transform>(e);
    byId_[id] = e;
    link(e, parent);
    if (events_) events_->emit(EntityCreatedEvent{this, e});
    return e;
}

void Scene::link(Entity child, Entity parent) {
    Hierarchy& h = *world_.get<Hierarchy>(child);
    h.parent = parent;
    h.nextSibling = {};
    Entity& head = parent.isNull() ? firstRoot_ : world_.get<Hierarchy>(parent)->firstChild;
    if (head.isNull()) {
        h.prevSibling = {};
        head = child;
        return;
    }
    Entity last = head;
    while (!world_.get<Hierarchy>(last)->nextSibling.isNull()) last = world_.get<Hierarchy>(last)->nextSibling;
    world_.get<Hierarchy>(last)->nextSibling = child;
    h.prevSibling = last;
}

void Scene::unlink(Entity child) {
    Hierarchy& h = *world_.get<Hierarchy>(child);
    if (!h.prevSibling.isNull()) world_.get<Hierarchy>(h.prevSibling)->nextSibling = h.nextSibling;
    else if (h.parent.isNull()) firstRoot_ = h.nextSibling;
    else world_.get<Hierarchy>(h.parent)->firstChild = h.nextSibling;
    if (!h.nextSibling.isNull()) world_.get<Hierarchy>(h.nextSibling)->prevSibling = h.prevSibling;
    h.parent = h.prevSibling = h.nextSibling = {};
}

void Scene::collectSubtree(Entity e, std::vector<Entity>& out) const {
    out.push_back(e);
    for (Entity c = world_.get<Hierarchy>(e)->firstChild; !c.isNull(); c = world_.get<Hierarchy>(c)->nextSibling)
        collectSubtree(c, out);
}

void Scene::destroyEntity(Entity e) {
    if (isValid(e) && std::find(pendingDestroy_.begin(), pendingDestroy_.end(), e) == pendingDestroy_.end())
        pendingDestroy_.push_back(e);
}

void Scene::flushDestroyed() {
    // Un evento de destrucción puede pedir más destrucciones: se procesan en bucle.
    while (!pendingDestroy_.empty()) {
        std::vector<Entity> batch;
        batch.swap(pendingDestroy_);
        for (Entity e : batch) destroyEntityImmediate(e);
    }
}

void Scene::destroyEntityImmediate(Entity e) {
    if (!isValid(e)) return;
    std::vector<Entity> subtree;
    collectSubtree(e, subtree);
    // Primero se avisa (los hijos antes que el padre) mientras los datos aún existen.
    if (events_)
        for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
            if (isValid(*it)) events_->emit(EntityDestroyedEvent{this, *it});
    unlink(e);
    for (Entity d : subtree) {
        if (!isValid(d)) continue;
        byId_.erase(world_.get<EntityInfo>(d)->id);
        world_.destroy(d);
    }
}

void Scene::clear() {
    while (!firstRoot_.isNull()) destroyEntityImmediate(firstRoot_);
    pendingDestroy_.clear();
    world_.clear();
    byId_.clear();
}

bool Scene::isDescendantOf(Entity e, Entity ancestor) const {
    for (Entity p = parent(e); !p.isNull(); p = parent(p))
        if (p == ancestor) return true;
    return false;
}

bool Scene::setParent(Entity child, Entity newParent, bool keepWorldTransform) {
    if (!isValid(child) || (!newParent.isNull() && !isValid(newParent))) return false;
    if (child == newParent || (!newParent.isNull() && isDescendantOf(newParent, child))) {
        TITAN_WARN("scene", "No se puede emparentar '%s' con su propio descendiente",
                   world_.get<EntityInfo>(child)->name.c_str());
        return false;
    }
    Mat4 world;
    if (keepWorldTransform) world = computeWorldMatrix(child);
    unlink(child);
    link(child, newParent);
    if (keepWorldTransform) {
        const Mat4 local = newParent.isNull() ? world : computeWorldMatrix(newParent).inverted() * world;
        Transform& t = *world_.get<Transform>(child);
        local.decompose(t.position_, t.rotation_, t.scale_);
    }
    world_.get<Transform>(child)->dirty_ = true;
    return true;
}

Entity Scene::parent(Entity e) const {
    const Hierarchy* h = world_.get<Hierarchy>(e);
    return h ? h->parent : Entity{};
}

std::vector<Entity> Scene::children(Entity e) const {
    std::vector<Entity> out;
    const Hierarchy* h = world_.get<Hierarchy>(e);
    if (!h) return out;
    for (Entity c = h->firstChild; !c.isNull(); c = world_.get<Hierarchy>(c)->nextSibling) out.push_back(c);
    return out;
}

std::vector<Entity> Scene::roots() const {
    std::vector<Entity> out;
    for (Entity r = firstRoot_; !r.isNull(); r = world_.get<Hierarchy>(r)->nextSibling) out.push_back(r);
    return out;
}

bool Scene::isActiveInHierarchy(Entity e) const {
    for (Entity c = e; !c.isNull(); c = parent(c)) {
        const EntityInfo* info = world_.get<EntityInfo>(c);
        if (!info || !info->active) return false;
    }
    return true;
}

Entity Scene::findById(UUID id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? Entity{} : it->second;
}

Entity Scene::findByName(const std::string& name) const {
    Entity found;
    traverse([&](Entity e, int) {
        if (found.isNull() && world_.get<EntityInfo>(e)->name == name) found = e;
    });
    return found;
}

Mat4 Scene::computeWorldMatrix(Entity e) const {
    Mat4 m;
    for (Entity c = e; !c.isNull(); c = parent(c)) m = world_.get<Transform>(c)->localMatrix() * m;
    return m;
}

size_t TransformSystem::update(Scene& scene) {
    World& world = scene.world();
    size_t recomputed = 0;
    struct Item {
        Entity e;
        const Mat4* parentWorld;
        bool parentChanged;
    };
    std::vector<Item> stack;
    for (Entity r : scene.roots()) stack.push_back({r, nullptr, false});
    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        Transform& t = *world.get<Transform>(it.e);
        const bool changed = t.dirty_ || it.parentChanged;
        if (changed) {
            t.world_ = it.parentWorld ? *it.parentWorld * t.localMatrix() : t.localMatrix();
            t.dirty_ = false;
            ++recomputed;
        }
        for (Entity c = world.get<Hierarchy>(it.e)->firstChild; !c.isNull(); c = world.get<Hierarchy>(c)->nextSibling)
            stack.push_back({c, &t.world_, changed});
    }
    return recomputed;
}

} // namespace titan
