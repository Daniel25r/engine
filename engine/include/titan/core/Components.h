#pragma once
// Componentes integrados del motor. Son datos; el comportamiento vive en los sistemas.
// Toda entidad creada por una Scene tiene siempre EntityInfo, Hierarchy y Transform.
#include <string>

#include "titan/core/Math.h"
#include "titan/core/Uuid.h"
#include "titan/core/World.h"

namespace titan {

// Referencia persistente a un recurso del proyecto (textura, script, escena...).
// Se resuelve con la base de datos de recursos; un UUID nulo significa "ninguno".
using AssetRef = UUID;

struct EntityInfo {
    UUID id;
    std::string name;
    bool active = true;
};

// Enlaces de la jerarquía. Los hijos forman una lista doblemente enlazada ordenada.
// Las entidades raíz se enlazan igual, con parent nulo. Solo Scene modifica esto.
struct Hierarchy {
    Entity parent;
    Entity firstChild;
    Entity prevSibling;
    Entity nextSibling;
};

// Transformación local (relativa al padre). Los setters marcan la entidad como
// modificada; TransformSystem recalcula la matriz de mundo solo cuando hace falta.
class Transform {
public:
    const Vec3& position() const { return position_; }
    const Quat& rotation() const { return rotation_; }
    const Vec3& scale() const { return scale_; }
    Vec3 eulerDegrees() const { return rotation_.toEuler() * kRadToDeg; }

    void setPosition(const Vec3& p) { position_ = p; dirty_ = true; }
    void setRotation(const Quat& q) { rotation_ = q.normalized(); dirty_ = true; }
    void setEulerDegrees(const Vec3& e) { setRotation(Quat::fromEuler(e * kDegToRad)); }
    void setScale(const Vec3& s) { scale_ = s; dirty_ = true; }
    void translate(const Vec3& d) { setPosition(position_ + d); }
    void reset() { position_ = {}; rotation_ = {}; scale_ = {1, 1, 1}; dirty_ = true; }

    Mat4 localMatrix() const { return Mat4::trs(position_, rotation_, scale_); }
    // Matriz de mundo calculada en la última actualización del TransformSystem.
    const Mat4& worldMatrix() const { return world_; }
    Vec3 worldPosition() const { return world_.translation(); }
    Vec3 forward() const { return world_.transformDirection({0, 0, -1}).normalized(); }
    Vec3 up() const { return world_.transformDirection({0, 1, 0}).normalized(); }
    Vec3 right() const { return world_.transformDirection({1, 0, 0}).normalized(); }
    bool isDirty() const { return dirty_; }

private:
    friend class TransformSystem;
    friend class Scene;
    Vec3 position_;
    Quat rotation_;
    Vec3 scale_{1, 1, 1};
    Mat4 world_;
    bool dirty_ = true;
};

struct Camera {
    enum class Projection { Orthographic, Perspective };
    Projection projection = Projection::Orthographic;
    float orthographicSize = 5.0f; // media altura visible, en unidades de mundo
    float fieldOfView = 60.0f;     // vertical, en grados
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    int priority = 0;              // se usa la cámara activa de mayor prioridad
    Color clearColor = Color::hex(0x1B2230);

    Mat4 projectionMatrix(float aspect) const {
        if (projection == Projection::Perspective)
            return Mat4::perspective(fieldOfView * kDegToRad, aspect, nearClip, farClip);
        const float h = orthographicSize, w = orthographicSize * aspect;
        return Mat4::ortho(-w, w, -h, h, nearClip, farClip);
    }
};

struct SpriteRenderer {
    AssetRef texture;          // nulo = rectángulo de color sólido
    Color color;               // se multiplica con la textura
    Vec2 size{1.0f, 1.0f};     // en unidades de mundo, antes de la escala del Transform
    Vec2 pivot{0.5f, 0.5f};
    Rect uv{0.0f, 0.0f, 1.0f, 1.0f};
    int sortingOrder = 0;      // mayor = se dibuja encima
    bool visible = true;
};

// Primitivas de geometría generadas por el motor (los modelos importados llegarán con glTF).
enum class MeshPrimitive { Cube, Sphere, Plane, Cylinder, Cone, Capsule };

// Material PBR (metálico/rugosidad). Los colores se indican en sRGB, como en cualquier editor.
struct MeshRenderer {
    MeshPrimitive mesh = MeshPrimitive::Cube;
    AssetRef model;              // modelo importado (glTF/GLB); si no es nulo, sustituye a la primitiva
    AssetRef texture;            // textura de color base (nulo = sin textura)
    Color color;                 // color base
    float metallic = 0.0f;       // 0 = dieléctrico (plástico, piedra), 1 = metal
    float roughness = 0.6f;      // 0 = espejo, 1 = mate
    Color emission{0, 0, 0, 1};  // luz propia
    bool prototypeGrid = false;  // rejilla de prototipado alineada al mundo
    bool castShadows = true;
    bool unlit = false;          // ignora las luces
    bool visible = true;
};

// Ambiente de la escena: cielo, luz ambiental, niebla, exposición y sombras.
// Si una escena 3D no tiene ninguno, se usan estos valores por defecto.
struct Environment {
    bool sky = true;
    Color skyZenith = Color::hex(0x2F6DB5);
    Color skyHorizon = Color::hex(0xB8D4EC);
    Color ground = Color::hex(0x4D463E);
    float ambientIntensity = 1.0f;
    Color fogColor = Color::hex(0xB8D4EC);
    float fogDensity = 0.008f;   // niebla exponencial; 0 = sin niebla
    float exposure = 1.0f;
    bool shadows = true;
    float shadowDistance = 50.0f;
};

struct Light {
    enum class Type { Directional, Point };
    Type type = Type::Directional; // la direccional ilumina hacia su forward() (-Z local)
    Color color;
    float intensity = 1.0f;
    float range = 10.0f;           // solo puntual
};

struct Script {
    AssetRef script; // archivo .lua del proyecto
    bool enabled = true;
};

} // namespace titan
