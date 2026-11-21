#include "titan/physics/PhysicsWorld.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "titan/core/Log.h"

namespace titan {

namespace {

// ---- Capas: estáticos frente a móviles (los estáticos no chocan entre sí).
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::uint kCount = 2;
} // namespace Layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return Layers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(l)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l.GetValue() == 0 ? "estatico" : "movil"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a == Layers::kMoving || b.GetValue() == Layers::kMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

JPH::Vec3 toJ(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
Vec3 fromJ(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat toJ(const Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
Quat fromJ(const JPH::Quat& q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

uint64_t pack(Entity e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; }
Entity unpack(uint64_t v) { return {static_cast<uint32_t>(v & 0xFFFFFFFFu), static_cast<uint32_t>(v >> 32)}; }

void initJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

} // namespace

// Recoge contactos desde los hilos de Jolt; se leen al terminar cada paso.
class ContactCollector final : public JPH::ContactListener {
public:
    std::mutex mutex;
    std::vector<std::pair<JPH::BodyID, JPH::BodyID>> added, removed;
    std::unordered_map<uint32_t, bool> sensors; // BodyID -> es sensor

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
        std::lock_guard<std::mutex> lock(mutex);
        added.emplace_back(a.GetID(), b.GetID());
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        std::lock_guard<std::mutex> lock(mutex);
        removed.emplace_back(pair.GetBody1ID(), pair.GetBody2ID());
    }
};

struct PhysicsWorld::Impl {
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objVsBp;
    ObjectPairs objPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system;
    ContactCollector contacts;
    std::unordered_map<uint64_t, JPH::BodyID> bodies;  // entidad -> cuerpo
    std::unordered_map<uint32_t, Entity> owners;       // cuerpo -> entidad
    std::unordered_map<uint64_t, int> pairCount;       // contactos activos por par de entidades (varios puntos)
    std::vector<ContactEvent> events;

    JPH::BodyInterface& bi() { return system->GetBodyInterface(); }
    const JPH::BodyInterface& bi() const { return system->GetBodyInterface(); }
    bool find(Entity e, JPH::BodyID& id) const {
        auto it = bodies.find(pack(e));
        if (it == bodies.end()) return false;
        id = it->second;
        return true;
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {
    initJolt();
    impl_->temp = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 3);
    impl_->jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    impl_->system = std::make_unique<JPH::PhysicsSystem>();
    impl_->system->Init(4096, 0, 8192, 4096, impl_->bpLayers, impl_->objVsBp, impl_->objPairs);
    impl_->system->SetContactListener(&impl_->contacts);
}

PhysicsWorld::~PhysicsWorld() { clear(); }

void PhysicsWorld::clear() {
    if (!impl_->system) return;
    for (auto& [key, id] : impl_->bodies) {
        impl_->bi().RemoveBody(id);
        impl_->bi().DestroyBody(id);
    }
    impl_->bodies.clear();
    impl_->owners.clear();
    impl_->pairCount.clear();
    impl_->events.clear();
    impl_->contacts.added.clear();
    impl_->contacts.removed.clear();
    impl_->contacts.sensors.clear();
}

void PhysicsWorld::build(Scene& scene) {
    clear();
    std::vector<Entity> withCollider;
    scene.world().each<Collider>([&](Entity e, Collider&) { withCollider.push_back(e); });
    createBodies(scene, withCollider);
    impl_->system->OptimizeBroadPhase();
    TITAN_INFO("physics", "%zu cuerpos físicos en '%s'", impl_->bodies.size(), scene.name().c_str());
}

void PhysicsWorld::addSubtree(Scene& scene, Entity root) {
    std::vector<Entity> list;
    scene.traverse([&](Entity e, int) {
        if ((e == root || scene.isDescendantOf(e, root)) && scene.world().has<Collider>(e)) list.push_back(e);
    });
    createBodies(scene, list);
}

void PhysicsWorld::createBodies(Scene& scene, const std::vector<Entity>& list) {
    World& world = scene.world();
    for (Entity e : list) {
        if (!scene.isActiveInHierarchy(e)) continue;
        const Collider& c = *world.get<Collider>(e);
        const Rigidbody* rb = world.get<Rigidbody>(e);
        Vec3 pos, scl;
        Quat rot;
        scene.computeWorldMatrix(e).decompose(pos, rot, scl);
        const Vec3 s{std::fabs(scl.x), std::fabs(scl.y), std::fabs(scl.z)};

        // Forma con la escala del Transform ya aplicada.
        JPH::Ref<JPH::ShapeSettings> shape;
        if (c.shape == Collider::Shape::Sphere) {
            shape = new JPH::SphereShapeSettings(std::max(0.005f, c.radius * std::max(s.x, std::max(s.y, s.z))));
        } else if (c.shape == Collider::Shape::Capsule) {
            const float r = std::max(0.005f, c.radius * std::max(s.x, s.z));
            const float half = std::max(0.005f, c.height * s.y * 0.5f - r);
            shape = new JPH::CapsuleShapeSettings(half, r);
        } else {
            const Vec3 h{std::max(0.005f, c.size.x * s.x * 0.5f), std::max(0.005f, c.size.y * s.y * 0.5f),
                         std::max(0.005f, c.size.z * s.z * 0.5f)};
            const float convex = std::min(0.05f, std::min(h.x, std::min(h.y, h.z)) * 0.5f);
            shape = new JPH::BoxShapeSettings(toJ(h), convex);
        }
        const Vec3 off{c.center.x * s.x, c.center.y * s.y, c.center.z * s.z};
        if (off.length() > 1e-6f) shape = new JPH::RotatedTranslatedShapeSettings(toJ(off), JPH::Quat::sIdentity(), shape);
        JPH::ShapeSettings::ShapeResult result = shape->Create();
        if (result.HasError()) {
            TITAN_ERROR("physics", "'%s': forma no válida (%s)", scene.info(e).name.c_str(), result.GetError().c_str());
            continue;
        }

        const Rigidbody::Type type = rb ? rb->type : Rigidbody::Type::Static;
        const JPH::EMotionType motion = type == Rigidbody::Type::Dynamic   ? JPH::EMotionType::Dynamic
                                      : type == Rigidbody::Type::Kinematic ? JPH::EMotionType::Kinematic
                                                                           : JPH::EMotionType::Static;
        JPH::BodyCreationSettings bs(result.Get(), JPH::RVec3(pos.x, pos.y, pos.z), toJ(rot), motion,
                                     motion == JPH::EMotionType::Static ? Layers::kStatic : Layers::kMoving);
        bs.mIsSensor = c.isTrigger;
        bs.mUserData = pack(e);
        if (c.isTrigger && motion == JPH::EMotionType::Static) bs.mObjectLayer = Layers::kMoving; // que detecte a todos
        if (rb) {
            bs.mFriction = rb->friction;
            bs.mRestitution = rb->restitution;
            bs.mLinearDamping = rb->linearDamping;
            bs.mAngularDamping = rb->angularDamping;
            bs.mGravityFactor = rb->gravityScale;
            if (motion == JPH::EMotionType::Dynamic) {
                bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                bs.mMassPropertiesOverride.mMass = std::max(0.001f, rb->mass);
            }
            if (rb->lockRotation) bs.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }
        const JPH::BodyID id = impl_->bi().CreateAndAddBody(bs, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                                                                    : JPH::EActivation::Activate);
        if (id.IsInvalid()) {
            TITAN_ERROR("physics", "Demasiados cuerpos: '%s' no tendrá física", scene.info(e).name.c_str());
            continue;
        }
        impl_->bodies[pack(e)] = id;
        impl_->owners[id.GetIndexAndSequenceNumber()] = e;
        if (c.isTrigger) impl_->contacts.sensors[id.GetIndexAndSequenceNumber()] = true;
    }
}

void PhysicsWorld::removeEntity(Entity e) {
    JPH::BodyID id;
    if (!impl_->find(e, id)) return;
    impl_->bi().RemoveBody(id);
    impl_->bi().DestroyBody(id);
    impl_->owners.erase(id.GetIndexAndSequenceNumber());
    impl_->bodies.erase(pack(e));
}

void PhysicsWorld::step(Scene& scene, float dt) {
    if (dt <= 0) return;
    JPH::BodyInterface& bi = impl_->bi();
    // Cinemáticos: siguen a su Transform (los mueven scripts o animaciones).
    for (auto& [key, id] : impl_->bodies) {
        if (bi.GetMotionType(id) != JPH::EMotionType::Kinematic) continue;
        const Entity e = unpack(key);
        if (!scene.isValid(e)) continue;
        Vec3 p, s;
        Quat q;
        scene.computeWorldMatrix(e).decompose(p, q, s);
        bi.MoveKinematic(id, JPH::RVec3(p.x, p.y, p.z), toJ(q), dt);
    }

    impl_->system->Update(dt, 1, impl_->temp.get(), impl_->jobs.get());

    // Dinámicos: escriben su posición y rotación en el Transform.
    for (auto& [key, id] : impl_->bodies) {
        if (bi.GetMotionType(id) != JPH::EMotionType::Dynamic || !bi.IsActive(id)) continue;
        const Entity e = unpack(key);
        if (!scene.isValid(e)) continue;
        JPH::RVec3 p;
        JPH::Quat q;
        bi.GetPositionAndRotation(id, p, q);
        const Vec3 wp{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
        Transform& t = scene.transform(e);
        const Entity parent = scene.parent(e);
        if (parent.isNull()) {
            t.setPosition(wp);
            t.setRotation(fromJ(q));
        } else {
            Vec3 pp, ps;
            Quat pq;
            const Mat4 pw = scene.computeWorldMatrix(parent);
            pw.decompose(pp, pq, ps);
            t.setPosition(pw.inverted().transformPoint(wp));
            t.setRotation(pq.conjugate() * fromJ(q));
        }
    }

    // Contactos: se agrupan por par de entidades para emitir un solo Enter y un solo Exit.
    std::vector<std::pair<JPH::BodyID, JPH::BodyID>> added, removed;
    {
        std::lock_guard<std::mutex> lock(impl_->contacts.mutex);
        added.swap(impl_->contacts.added);
        removed.swap(impl_->contacts.removed);
    }
    auto handle = [&](JPH::BodyID a, JPH::BodyID b, bool begin) {
        auto ia = impl_->owners.find(a.GetIndexAndSequenceNumber()), ib = impl_->owners.find(b.GetIndexAndSequenceNumber());
        if (ia == impl_->owners.end() || ib == impl_->owners.end()) return;
        Entity ea = ia->second, eb = ib->second;
        if (pack(eb) < pack(ea)) std::swap(ea, eb);
        const uint64_t key = pack(ea) * 1000003ull ^ pack(eb);
        int& n = impl_->pairCount[key];
        const bool trigger = impl_->contacts.sensors.count(a.GetIndexAndSequenceNumber()) ||
                             impl_->contacts.sensors.count(b.GetIndexAndSequenceNumber());
        if (begin) {
            if (n++ == 0) impl_->events.push_back({ea, eb, true, trigger});
        } else if (n > 0 && --n == 0) {
            impl_->events.push_back({ea, eb, false, trigger});
            impl_->pairCount.erase(key);
        }
    };
    for (auto& [a, b] : added) handle(a, b, true);
    for (auto& [a, b] : removed) handle(a, b, false);
}

std::vector<ContactEvent> PhysicsWorld::takeEvents() {
    std::vector<ContactEvent> out;
    out.swap(impl_->events);
    return out;
}

size_t PhysicsWorld::bodyCount() const { return impl_->bodies.size(); }

bool PhysicsWorld::hasBody(Entity e) const {
    JPH::BodyID id;
    return impl_->find(e, id);
}

void PhysicsWorld::addForce(Entity e, const Vec3& f, bool impulse) {
    JPH::BodyID id;
    if (!impl_->find(e, id)) return;
    if (impulse) impl_->bi().AddImpulse(id, toJ(f));
    else impl_->bi().AddForce(id, toJ(f));
}

void PhysicsWorld::addTorque(Entity e, const Vec3& t) {
    JPH::BodyID id;
    if (impl_->find(e, id)) impl_->bi().AddTorque(id, toJ(t));
}

void PhysicsWorld::setVelocity(Entity e, const Vec3& v) {
    JPH::BodyID id;
    if (!impl_->find(e, id)) return;
    impl_->bi().SetLinearVelocity(id, toJ(v));
    impl_->bi().ActivateBody(id);
}

Vec3 PhysicsWorld::velocity(Entity e) const {
    JPH::BodyID id;
    return impl_->find(e, id) ? fromJ(impl_->bi().GetLinearVelocity(id)) : Vec3{};
}

void PhysicsWorld::teleport(Entity e, const Vec3& p) {
    JPH::BodyID id;
    if (impl_->find(e, id)) impl_->bi().SetPosition(id, JPH::RVec3(p.x, p.y, p.z), JPH::EActivation::Activate);
}

bool PhysicsWorld::raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                           Entity ignore) const {
    const Vec3 dir = direction.normalized() * maxDistance;
    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), toJ(dir));
    JPH::RayCastResult result;
    JPH::BodyID ignoreId;
    const bool hasIgnore = !ignore.isNull() && impl_->find(ignore, ignoreId);
    JPH::IgnoreSingleBodyFilter filter(ignoreId);
    const JPH::BodyFilter defaultFilter;
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, hasIgnore ? static_cast<const JPH::BodyFilter&>(filter) : defaultFilter))
        return false;
    auto it = impl_->owners.find(result.mBodyID.GetIndexAndSequenceNumber());
    if (it == impl_->owners.end()) return false;
    hit.entity = it->second;
    hit.distance = result.mFraction * maxDistance;
    hit.point = origin + direction.normalized() * hit.distance;
    // Normal de la superficie en el punto de impacto.
    JPH::BodyLockRead lock(impl_->system->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
        hit.normal = fromJ(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    return true;
}

void PhysicsWorld::setGravity(const Vec3& g) { impl_->system->SetGravity(toJ(g)); }
Vec3 PhysicsWorld::gravity() const { return fromJ(impl_->system->GetGravity()); }

} // namespace titan
