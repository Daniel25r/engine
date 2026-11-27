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
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "titan/core/Log.h"
#include "titan/core/TerrainTools.h"

namespace titan {

namespace {

// ---- Capas. Cada ObjectLayer de Jolt codifica (capa del usuario 0..15) * 2 + (0 estático, 1 móvil).
// Los estáticos no chocan entre sí; dos móviles (o móvil y estático) chocan si su matriz de capas lo permite.
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::uint kBroadCount = 2;
constexpr int kUserLayers = 16;
inline JPH::ObjectLayer make(int userLayer, bool moving) {
    return static_cast<JPH::ObjectLayer>(std::clamp(userLayer, 0, kUserLayers - 1) * 2 + (moving ? 1 : 0));
}
inline int user(JPH::ObjectLayer l) { return static_cast<int>(l >> 1); }
inline bool moving(JPH::ObjectLayer l) { return (l & 1) != 0; }
} // namespace Layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return Layers::kBroadCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(l & 1));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l.GetValue() == 0 ? "estatico" : "movil"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return Layers::moving(a) || b.GetValue() == Layers::kMoving;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    const uint16_t* matrix = nullptr; // matrix[a] bit b = las capas a y b chocan
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (!Layers::moving(a) && !Layers::moving(b)) return false;
        return (matrix[Layers::user(a)] >> Layers::user(b)) & 1u;
    }
};

// Filtro de consultas por máscara de capas (bit i = capa i).
class MaskFilter final : public JPH::ObjectLayerFilter {
public:
    explicit MaskFilter(uint32_t m) : mask(m) {}
    uint32_t mask;
    bool ShouldCollide(JPH::ObjectLayer l) const override { return (mask >> Layers::user(l)) & 1u; }
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

struct CharacterData {
    JPH::Ref<JPH::CharacterVirtual> character;
    float verticalVelocity = 0;
    float height = 1.8f;
};

struct PhysicsWorld::Impl {
    uint16_t layerMatrix[Layers::kUserLayers];
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
    std::unordered_map<uint64_t, CharacterData> characters; // entidad -> personaje
    std::vector<JPH::Ref<JPH::Constraint>> constraints;

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
    for (uint16_t& row : impl_->layerMatrix) row = 0xFFFF; // por defecto todas las capas chocan entre sí
    impl_->objPairs.matrix = impl_->layerMatrix;
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
    for (auto& c : impl_->constraints) impl_->system->RemoveConstraint(c);
    impl_->constraints.clear();
    for (auto& [key, ch] : impl_->characters) {
        const JPH::BodyID inner = ch.character->GetInnerBodyID();
        if (!inner.IsInvalid()) impl_->owners.erase(inner.GetIndexAndSequenceNumber());
    }
    impl_->characters.clear(); // la CharacterVirtual destruye su cuerpo interior
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
    scene.world().each<CharacterController>([&](Entity e, CharacterController&) {
        if (!scene.world().has<Collider>(e)) withCollider.push_back(e);
    });
    createBodies(scene, withCollider);
    scene.world().each<Terrain>([&](Entity e, Terrain& t) { createTerrainBody(scene, e, t); });
    createJoints(scene);
    impl_->system->OptimizeBroadPhase();
    TITAN_INFO("physics", "%zu cuerpos físicos en '%s'", impl_->bodies.size(), scene.name().c_str());
}

void PhysicsWorld::addSubtree(Scene& scene, Entity root) {
    std::vector<Entity> list;
    scene.traverse([&](Entity e, int) {
        if ((e == root || scene.isDescendantOf(e, root)) &&
            (scene.world().has<Collider>(e) || scene.world().has<CharacterController>(e)))
            list.push_back(e);
    });
    createBodies(scene, list);
}

void PhysicsWorld::createBodies(Scene& scene, const std::vector<Entity>& list) {
    World& world = scene.world();
    for (Entity e : list) {
        if (!scene.isActiveInHierarchy(e)) continue;
        if (const CharacterController* cc = world.get<CharacterController>(e)) {
            createCharacter(scene, e, *cc);
            continue; // el personaje sustituye a Collider/Rigidbody en esa entidad
        }
        const Collider& c = *world.get<Collider>(e);
        const Rigidbody* rb = world.get<Rigidbody>(e);
        Vec3 pos, scl;
        Quat rot;
        scene.computeWorldMatrix(e).decompose(pos, rot, scl);
        const Vec3 s{std::fabs(scl.x), std::fabs(scl.y), std::fabs(scl.z)};

        // Forma con la escala del Transform ya aplicada.
        JPH::Ref<JPH::ShapeSettings> shape;
        Rigidbody::Type bodyType = rb ? rb->type : Rigidbody::Type::Static;
        if (c.shape == Collider::Shape::Mesh || c.shape == Collider::Shape::ConvexHull) {
            std::vector<Vec3> verts;
            std::vector<uint32_t> idx;
            if (!meshProvider_ || !meshProvider_(e, verts, idx) || verts.empty()) {
                TITAN_WARN("physics", "'%s': collider de malla sin MeshRenderer con malla o modelo; se ignora",
                           scene.info(e).name.c_str());
                continue;
            }
            bool convex = c.shape == Collider::Shape::ConvexHull;
            if (!convex && bodyType == Rigidbody::Type::Dynamic) {
                // Jolt (como PhysX) no simula mallas cóncavas como cuerpos dinámicos: se usa su envolvente.
                TITAN_WARN("physics", "'%s': una malla dinámica usa su envolvente convexa", scene.info(e).name.c_str());
                convex = true;
            }
            if (convex) {
                JPH::Array<JPH::Vec3> pts;
                pts.reserve(verts.size());
                for (const Vec3& v : verts) pts.push_back(JPH::Vec3(v.x * s.x, v.y * s.y, v.z * s.z));
                shape = new JPH::ConvexHullShapeSettings(pts, 0.01f);
            } else {
                JPH::VertexList vl;
                vl.reserve(verts.size());
                for (const Vec3& v : verts) vl.push_back(JPH::Float3(v.x * s.x, v.y * s.y, v.z * s.z));
                JPH::IndexedTriangleList tl;
                tl.reserve(idx.size() / 3);
                for (size_t k = 0; k + 2 < idx.size(); k += 3) tl.push_back(JPH::IndexedTriangle(idx[k], idx[k + 1], idx[k + 2], 0));
                shape = new JPH::MeshShapeSettings(std::move(vl), std::move(tl));
                if (bodyType == Rigidbody::Type::Dynamic) bodyType = Rigidbody::Type::Static;
            }
        } else if (c.shape == Collider::Shape::Sphere) {
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

        const Rigidbody::Type type = bodyType;
        const JPH::EMotionType motion = type == Rigidbody::Type::Dynamic   ? JPH::EMotionType::Dynamic
                                      : type == Rigidbody::Type::Kinematic ? JPH::EMotionType::Kinematic
                                                                           : JPH::EMotionType::Static;
        JPH::BodyCreationSettings bs(result.Get(), JPH::RVec3(pos.x, pos.y, pos.z), toJ(rot), motion,
                                     Layers::make(c.layer, motion != JPH::EMotionType::Static));
        bs.mIsSensor = c.isTrigger;
        bs.mUserData = pack(e);
        if (c.isTrigger && motion == JPH::EMotionType::Static) bs.mObjectLayer = Layers::make(c.layer, true); // que detecte a todos
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

void PhysicsWorld::createCharacter(Scene& scene, Entity e, const CharacterController& cc) {
    const float r = std::max(0.05f, cc.radius), h = std::max(cc.height, 2 * r + 0.01f);
    const float half = h * 0.5f - r;
    // Cápsula con la base en el origen de la entidad (los pies).
    JPH::Ref<JPH::Shape> shape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0, h * 0.5f, 0), JPH::Quat::sIdentity(),
                                                                      new JPH::CapsuleShape(half, r)).Create().Get();
    JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
    s->mShape = shape;
    s->mMaxSlopeAngle = std::clamp(cc.maxSlope, 1.0f, 89.0f) * kDegToRad;
    s->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -r); // solo cuenta como suelo la parte baja
    // Cuerpo interior cinemático: los triggers, los rayos y los cuerpos dinámicos "ven" al personaje.
    s->mInnerBodyShape = shape;
    s->mInnerBodyLayer = Layers::make(cc.layer, true);
    const Vec3 p = scene.computeWorldMatrix(e).translation();
    CharacterData d;
    d.height = h;
    d.character = new JPH::CharacterVirtual(s, JPH::RVec3(p.x, p.y, p.z), JPH::Quat::sIdentity(), pack(e), impl_->system.get());
    const JPH::BodyID inner = d.character->GetInnerBodyID();
    if (!inner.IsInvalid()) {
        impl_->owners[inner.GetIndexAndSequenceNumber()] = e;
        impl_->bi().SetUserData(inner, pack(e));
    }
    impl_->characters[pack(e)] = d;
}

void PhysicsWorld::createTerrainBody(Scene& scene, Entity e, const Terrain& t) {
    if (!scene.isActiveInHierarchy(e)) return;
    Vec3 pos, scl;
    Quat rot;
    scene.computeWorldMatrix(e).decompose(pos, rot, scl);
    const TerrainMesh mesh = buildTerrainMesh(t);
    JPH::VertexList verts;
    verts.reserve(mesh.vertices.size());
    for (const TerrainVertex& v : mesh.vertices) verts.push_back(JPH::Float3(v.position.x * scl.x, v.position.y * scl.y, v.position.z * scl.z));
    JPH::IndexedTriangleList tris;
    tris.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        tris.push_back(JPH::IndexedTriangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2], 0));
    JPH::MeshShapeSettings settings(std::move(verts), std::move(tris));
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        TITAN_ERROR("physics", "Terreno '%s': %s", scene.info(e).name.c_str(), result.GetError().c_str());
        return;
    }
    JPH::BodyCreationSettings bs(result.Get(), JPH::RVec3(pos.x, pos.y, pos.z), toJ(rot), JPH::EMotionType::Static, Layers::kStatic);
    bs.mUserData = pack(e);
    const JPH::BodyID id = impl_->bi().CreateAndAddBody(bs, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return;
    impl_->bodies[pack(e)] = id;
    impl_->owners[id.GetIndexAndSequenceNumber()] = e;
}

void PhysicsWorld::createJoints(Scene& scene) {
    World& world = scene.world();
    world.each<Joint>([&](Entity e, Joint& j) {
        JPH::BodyID a;
        if (!impl_->find(e, a) || !scene.isActiveInHierarchy(e)) {
            TITAN_WARN("physics", "Joint en '%s': necesita Rigidbody y Collider", scene.info(e).name.c_str());
            return;
        }
        JPH::BodyID b;
        if (!j.connected.isNull()) {
            const Entity other = scene.findById(j.connected);
            if (other.isNull() || !impl_->find(other, b)) {
                TITAN_WARN("physics", "Joint en '%s': la entidad conectada no existe o no tiene física", scene.info(e).name.c_str());
                return;
            }
        }
        const Mat4 wa = scene.computeWorldMatrix(e);
        const Vec3 anchor = wa.transformPoint(j.anchor);
        const Vec3 axis = wa.transformDirection(j.axis).normalized();
        JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
        if (j.type == Joint::Type::Fixed) {
            auto* f = new JPH::FixedConstraintSettings();
            f->mAutoDetectPoint = true;
            settings = f;
        } else if (j.type == Joint::Type::Hinge) {
            auto* h = new JPH::HingeConstraintSettings();
            h->mPoint1 = h->mPoint2 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
            h->mHingeAxis1 = h->mHingeAxis2 = toJ(axis);
            const Vec3 normal = std::fabs(axis.y) < 0.9f ? cross(axis, {0, 1, 0}).normalized() : cross(axis, {1, 0, 0}).normalized();
            h->mNormalAxis1 = h->mNormalAxis2 = toJ(normal);
            if (j.limitAngle) {
                h->mLimitsMin = std::min(j.minAngle, j.maxAngle) * kDegToRad;
                h->mLimitsMax = std::max(j.minAngle, j.maxAngle) * kDegToRad;
            }
            settings = h;
        } else {
            auto* d = new JPH::DistanceConstraintSettings();
            d->mPoint1 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
            Vec3 pb = anchor;
            if (!b.IsInvalid()) {
                const JPH::RVec3 c = impl_->bi().GetCenterOfMassPosition(b);
                pb = {static_cast<float>(c.GetX()), static_cast<float>(c.GetY()), static_cast<float>(c.GetZ())};
            }
            d->mPoint2 = JPH::RVec3(pb.x, pb.y, pb.z);
            d->mMinDistance = std::max(0.0f, j.minDistance);
            d->mMaxDistance = std::max(d->mMinDistance, j.maxDistance);
            if (j.springFrequency > 0) {
                d->mLimitsSpringSettings.mFrequency = j.springFrequency;
                d->mLimitsSpringSettings.mDamping = j.springDamping;
            }
            settings = d;
        }
        JPH::Body* body1 = impl_->system->GetBodyLockInterfaceNoLock().TryGetBody(a);
        JPH::Body* body2 = b.IsInvalid() ? &JPH::Body::sFixedToWorld : impl_->system->GetBodyLockInterfaceNoLock().TryGetBody(b);
        if (!body1 || !body2) return;
        JPH::Ref<JPH::Constraint> c = settings->Create(*body1, *body2);
        impl_->system->AddConstraint(c);
        impl_->constraints.push_back(c);
    });
}

void PhysicsWorld::removeEntity(Entity e) {
    if (auto it = impl_->characters.find(pack(e)); it != impl_->characters.end()) {
        const JPH::BodyID inner = it->second.character->GetInnerBodyID();
        if (!inner.IsInvalid()) impl_->owners.erase(inner.GetIndexAndSequenceNumber());
        impl_->characters.erase(it);
        return;
    }
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

    // Personajes: velocidad pedida por los scripts + gravedad propia; Jolt resuelve escalones y pendientes.
    const Vec3 g = gravity();
    for (auto& [key, ch] : impl_->characters) {
        const Entity e = unpack(key);
        CharacterController* cc = scene.isValid(e) ? scene.world().get<CharacterController>(e) : nullptr;
        if (!cc) continue;
        JPH::CharacterVirtual& c = *ch.character;
        // Si el Transform se movió desde fuera (teleport, editor, script), el personaje va allí.
        const Vec3 tp = scene.computeWorldMatrix(e).translation();
        const JPH::RVec3 cp = c.GetPosition();
        if ((Vec3{float(cp.GetX()), float(cp.GetY()), float(cp.GetZ())} - tp).length() > 0.001f) c.SetPosition(JPH::RVec3(tp.x, tp.y, tp.z));
        c.UpdateGroundVelocity();
        const bool grounded = c.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        if (grounded && ch.verticalVelocity < 0) ch.verticalVelocity = 0;
        if (grounded && cc->jumpSpeed > 0) ch.verticalVelocity = cc->jumpSpeed;
        cc->jumpSpeed = 0;
        ch.verticalVelocity += g.y * cc->gravityScale * dt;
        const JPH::Vec3 ground = grounded ? c.GetGroundVelocity() : JPH::Vec3::sZero();
        c.SetLinearVelocity(JPH::Vec3(cc->desiredVelocity.x, ch.verticalVelocity, cc->desiredVelocity.z) + ground);
        JPH::CharacterVirtual::ExtendedUpdateSettings us;
        us.mWalkStairsStepUp = JPH::Vec3(0, std::max(0.0f, cc->stepHeight), 0);
        us.mStickToFloorStepDown = JPH::Vec3(0, -std::max(0.05f, cc->stepHeight), 0);
        const JPH::ObjectLayer layer = Layers::make(cc->layer, true);
        c.ExtendedUpdate(dt, toJ(g * cc->gravityScale), us, impl_->system->GetDefaultBroadPhaseLayerFilter(layer),
                         impl_->system->GetDefaultLayerFilter(layer), {}, {}, *impl_->temp);
        const JPH::Vec3 v = c.GetLinearVelocity();
        if (c.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && v.GetY() <= 0.01f) ch.verticalVelocity = 0;
        else ch.verticalVelocity = v.GetY();
        cc->grounded = c.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        cc->velocity = fromJ(v);
        const JPH::RVec3 np = c.GetPosition();
        const Vec3 wp{float(np.GetX()), float(np.GetY()), float(np.GetZ())};
        const Entity parent = scene.parent(e);
        scene.transform(e).setPosition(parent.isNull() ? wp : scene.computeWorldMatrix(parent).inverted().transformPoint(wp));
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
    if (auto it = impl_->characters.find(pack(e)); it != impl_->characters.end())
        return fromJ(it->second.character->GetLinearVelocity());
    JPH::BodyID id;
    return impl_->find(e, id) ? fromJ(impl_->bi().GetLinearVelocity(id)) : Vec3{};
}

void PhysicsWorld::teleport(Entity e, const Vec3& p) {
    if (auto it = impl_->characters.find(pack(e)); it != impl_->characters.end()) {
        it->second.character->SetPosition(JPH::RVec3(p.x, p.y, p.z));
        it->second.verticalVelocity = 0;
        return;
    }
    JPH::BodyID id;
    if (impl_->find(e, id)) impl_->bi().SetPosition(id, JPH::RVec3(p.x, p.y, p.z), JPH::EActivation::Activate);
}

bool PhysicsWorld::raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                           Entity ignore) const {
    return raycastMasked(origin, direction, maxDistance, hit, 0xFFFF, ignore);
}

bool PhysicsWorld::raycastMasked(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit& hit,
                                 uint32_t layerMask, Entity ignore) const {
    const Vec3 dir = direction.normalized() * maxDistance;
    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), toJ(dir));
    JPH::RayCastResult result;
    JPH::BodyID ignoreId;
    const bool hasIgnore = !ignore.isNull() && impl_->find(ignore, ignoreId);
    JPH::IgnoreSingleBodyFilter filter(ignoreId);
    const JPH::BodyFilter defaultFilter;
    const MaskFilter mask(layerMask);
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, result, {}, mask, hasIgnore ? static_cast<const JPH::BodyFilter&>(filter) : defaultFilter))
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

void PhysicsWorld::setLayerCollision(int a, int b, bool collide) {
    if (a < 0 || b < 0 || a >= Layers::kUserLayers || b >= Layers::kUserLayers) return;
    const uint16_t bitB = static_cast<uint16_t>(1u << b), bitA = static_cast<uint16_t>(1u << a);
    if (collide) {
        impl_->layerMatrix[a] |= bitB;
        impl_->layerMatrix[b] |= bitA;
    } else {
        impl_->layerMatrix[a] &= static_cast<uint16_t>(~bitB);
        impl_->layerMatrix[b] &= static_cast<uint16_t>(~bitA);
    }
}

bool PhysicsWorld::layersCollide(int a, int b) const {
    if (a < 0 || b < 0 || a >= Layers::kUserLayers || b >= Layers::kUserLayers) return false;
    return (impl_->layerMatrix[a] >> b) & 1u;
}

bool PhysicsWorld::sphereCast(const Vec3& origin, float radius, const Vec3& direction, float maxDistance, RaycastHit& hit,
                              uint32_t layerMask, Entity ignore) const {
    JPH::SphereShape sphere(std::max(0.001f, radius));
    sphere.SetEmbedded();
    const Vec3 dir = direction.normalized();
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sReplicate(1), JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
                               toJ(dir * maxDistance));
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::BodyID ignoreId;
    const bool hasIgnore = !ignore.isNull() && impl_->find(ignore, ignoreId);
    JPH::IgnoreSingleBodyFilter filter(ignoreId);
    const JPH::BodyFilter defaultFilter;
    const MaskFilter mask(layerMask);
    impl_->system->GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, mask,
                                                   hasIgnore ? static_cast<const JPH::BodyFilter&>(filter) : defaultFilter);
    if (!collector.HadHit()) return false;
    const JPH::ShapeCastResult& r = collector.mHit;
    auto it = impl_->owners.find(r.mBodyID2.GetIndexAndSequenceNumber());
    if (it == impl_->owners.end()) return false;
    hit.entity = it->second;
    hit.distance = r.mFraction * maxDistance;
    hit.point = fromJ(r.mContactPointOn2);
    const JPH::Vec3 n = -r.mPenetrationAxis;
    hit.normal = n.LengthSq() > 1e-12f ? fromJ(n.Normalized()) : Vec3{0, 1, 0};
    return true;
}

namespace {
std::vector<Entity> collectOverlaps(const JPH::PhysicsSystem& system, const std::unordered_map<uint32_t, Entity>& owners,
                                    const JPH::Shape& shape, const JPH::RMat44& transform, uint32_t layerMask) {
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const MaskFilter mask(layerMask);
    system.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sReplicate(1), transform, settings, JPH::RVec3::sZero(),
                                              collector, {}, mask);
    std::vector<Entity> out;
    for (const JPH::CollideShapeResult& r : collector.mHits) {
        auto it = owners.find(r.mBodyID2.GetIndexAndSequenceNumber());
        if (it != owners.end() && std::find(out.begin(), out.end(), it->second) == out.end()) out.push_back(it->second);
    }
    return out;
}
} // namespace

std::vector<Entity> PhysicsWorld::overlapSphere(const Vec3& c, float radius, uint32_t layerMask) const {
    JPH::SphereShape sphere(std::max(0.001f, radius));
    sphere.SetEmbedded();
    return collectOverlaps(*impl_->system, impl_->owners, sphere, JPH::RMat44::sTranslation(JPH::RVec3(c.x, c.y, c.z)), layerMask);
}

std::vector<Entity> PhysicsWorld::overlapBox(const Vec3& c, const Vec3& h, const Quat& q, uint32_t layerMask) const {
    const float convex = std::min(0.05f, std::min(h.x, std::min(h.y, h.z)) * 0.5f);
    JPH::BoxShape box(JPH::Vec3(std::max(0.001f, h.x), std::max(0.001f, h.y), std::max(0.001f, h.z)), std::max(0.0f, convex));
    box.SetEmbedded();
    return collectOverlaps(*impl_->system, impl_->owners, box, JPH::RMat44::sRotationTranslation(toJ(q), JPH::RVec3(c.x, c.y, c.z)),
                           layerMask);
}

bool PhysicsWorld::isCharacter(Entity e) const { return impl_->characters.count(pack(e)) > 0; }

void PhysicsWorld::setGravity(const Vec3& g) { impl_->system->SetGravity(toJ(g)); }
Vec3 PhysicsWorld::gravity() const { return fromJ(impl_->system->GetGravity()); }

} // namespace titan
