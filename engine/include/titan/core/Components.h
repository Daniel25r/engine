#pragma once
#include <cstdint>
#include <memory>
// Componentes integrados del motor. Son datos; el comportamiento vive en los sistemas.
// Toda entidad creada por una Scene tiene siempre EntityInfo, Hierarchy y Transform.
#include <string>
#include <vector>

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
    // ---- Material avanzado (0.13) ----
    AssetRef material;           // material compartido (.mat): si no es nulo, sus valores sustituyen a los de aquí
    AssetRef normalMap;          // relieve (mapa normal en convención OpenGL/glTF: verde = arriba)
    float normalStrength = 1.0f;
    AssetRef metallicRoughnessMap; // convención glTF: G = rugosidad, B = metálico (multiplican a los valores)
    AssetRef emissionMap;        // multiplica al color de emisión (si este es negro, se usa blanco)
    AssetRef occlusionMap;       // R = oclusión ambiental
    Vec2 tiling{1, 1};           // repetición de las texturas
    Vec2 offset;                 // desplazamiento de las texturas
    bool alphaClip = false;      // recorte: los píxeles con alfa menor que alphaCutoff no se dibujan (hojas, rejas)
    float alphaCutoff = 0.5f;
    bool doubleSided = false;    // se ven las dos caras (los planos siempre lo son)
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
    // Posprocesado de la escena (el perfil de calidad puede desactivar el bloom). Por defecto, desactivado:
    // los proyectos anteriores a 0.13 se ven igual que antes.
    bool bloom = false;
    float bloomIntensity = 0.6f;
    float bloomThreshold = 0.75f; // brillo (0..1) a partir del cual brilla
    float vignette = 0;           // 0..1: oscurece los bordes
    float contrast = 1;
    float saturation = 1;
    float temperature = 0;        // -1 frío .. 1 cálido
};

struct Light {
    enum class Type { Directional, Point, Spot };
    Type type = Type::Directional; // la direccional y el foco iluminan hacia su forward() (-Z local)
    Color color;
    float intensity = 1.0f;
    float range = 10.0f;           // puntual y foco
    float spotAngle = 45.0f;       // foco: apertura total del cono en grados
    float spotSoftness = 0.25f;    // foco: fracción del borde del cono que se difumina (0 = borde duro)
};

// Reproduce los clips de animación de un modelo glTF con esqueleto (MeshRenderer.model).
// ---- Máquina de estados de animación (opcional: si no tiene estados, el Animator reproduce "clip") ----
struct AnimParam {
    enum class Type { Float, Int, Bool, Trigger };
    std::string name;
    Type type = Type::Float;
    float value = 0;        // bool/trigger: 0 o 1
};
struct AnimCondition {
    enum class Op { Greater, Less, Equal, NotEqual, IsTrue, IsFalse };
    std::string param;
    Op op = Op::Greater;
    float value = 0;
};
struct AnimTransition {
    std::string from;       // "*" = desde cualquier estado
    std::string to;
    std::vector<AnimCondition> conditions; // todas deben cumplirse (lista vacía = siempre)
    float duration = 0.2f;  // segundos de mezcla
    bool hasExitTime = false; // esperar a que el clip llegue a exitTime (0..1) antes de salir
    float exitTime = 1.0f;
};
struct AnimBlendPoint {
    float threshold = 0;
    std::string clip;
};
struct AnimEvent {
    float time = 0.5f;      // 0..1 del clip
    std::string name;       // llega a los scripts como onAnimationEvent(nombre)
};
struct AnimState {
    std::string name;
    std::string clip;       // clip simple; o, si "blend" tiene puntos, mezcla 1D
    float speed = 1.0f;
    bool loop = true;
    std::string blendParam; // mezcla 1D (blend tree): parámetro que elige entre los clips
    std::vector<AnimBlendPoint> blend;
    std::vector<AnimEvent> events;
};
struct AnimStateMachine {
    std::vector<AnimParam> params;
    std::vector<AnimState> states; // el primero es el de entrada
    std::vector<AnimTransition> transitions;
    // En ejecución (no se guarda)
    std::string current;
    float normalizedTime = 0; // ciclos completados + fracción
    std::vector<std::string> firedEvents; // eventos de este paso (los reparte el runtime)
    bool empty() const { return states.empty(); }
    AnimParam* param(const std::string& n) {
        for (AnimParam& p : params)
            if (p.name == n) return &p;
        return nullptr;
    }
    const AnimState* state(const std::string& n) const {
        for (const AnimState& s : states)
            if (s.name == n) return &s;
        return nullptr;
    }
};

struct Animator {
    std::string clip;      // nombre del clip ("Walk", "Run"...); vacío = el primero
    bool playing = true;
    bool loop = true;
    float speed = 1.0f;
    float fadeDuration = 0.25f; // segundos de transición al cambiar de clip
    float time = 0.0f;     // segundos dentro del clip
    // Estado de la transición (no se guarda): clip anterior, su tiempo y su peso restante (1 → 0).
    std::string fromClip;
    float fromTime = 0.0f;
    float fromWeight = 0.0f;
    // Mezcla 1D de la máquina de estados (no se guarda): segundo clip, su tiempo y su peso (0..1).
    std::string blendClip;
    float blendTime = 0.0f;
    float blendWeight = 0.0f;
    AnimStateMachine machine; // vacía = sin máquina de estados
};

// Avanza el tiempo del Animator y de su transición (lo usan el juego y la vista previa del editor).
inline void advanceAnimator(Animator& a, float dt) {
    if (a.playing) a.time += dt * a.speed;
    if (a.fromWeight > 0) {
        a.fromTime += dt * a.speed;
        a.fromWeight -= a.fadeDuration > 0 ? dt / a.fadeDuration : 1.0f;
        if (a.fromWeight < 0) a.fromWeight = 0;
    }
}

// Cambia de clip con transición suave desde el actual.
inline void switchAnimation(Animator& a, const std::string& clip) {
    if (clip == a.clip) return;
    if (a.fadeDuration > 0) {
        a.fromClip = a.clip;
        a.fromTime = a.time;
        a.fromWeight = 1.0f;
    }
    a.clip = clip;
    a.time = 0;
}

// Fuente de sonido (como AudioSource de Unity). El oyente es la cámara activa.
struct AudioSource {
    AssetRef clip;           // .wav, .mp3 o .flac del proyecto
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = false;    // 3D: suena desde la posición de la entidad y se atenúa con la distancia
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
};

// ---- Física (mismos conceptos que Rigidbody/Collider de Unity) ----
// Una entidad con Collider y sin Rigidbody es un sólido estático (suelo, paredes).
struct Rigidbody {
    enum class Type { Static, Dynamic, Kinematic };
    Type type = Type::Dynamic;    // Kinematic: lo mueve su Transform (scripts, animación) y empuja a los demás
    float mass = 1.0f;            // kg
    float friction = 0.5f;
    float restitution = 0.0f;     // rebote (0..1)
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;
    bool lockRotation = false;    // no gira (personajes)
};

struct Collider {
    enum class Shape { Box, Sphere, Capsule };
    Shape shape = Shape::Box;
    Vec3 size{1, 1, 1};           // caja: tamaño completo (antes de la escala del Transform)
    float radius = 0.5f;          // esfera y cápsula
    float height = 2.0f;          // cápsula: altura total
    Vec3 center;                  // desplazamiento local
    bool isTrigger = false;       // sensor: detecta pero no choca (onTriggerEnter/Exit)
    int layer = 0;                // capa de colisión 0..15 (Physics.setLayerCollision decide qué capas chocan)
};

// Controlador de personaje (Jolt CharacterVirtual): sube escalones y pendientes, no rueda ni se cae, y se mueve
// con e:move(x, z) / e:jump(v). El origen de la entidad es la base (los pies). No necesita Rigidbody ni Collider.
struct CharacterController {
    float height = 1.8f;
    float radius = 0.35f;
    float maxSlope = 50.0f;       // grados que puede subir
    float stepHeight = 0.35f;     // escalones que sube sin saltar
    float gravityScale = 1.0f;
    int layer = 0;
    // En ejecución (no se guarda): lo que piden los scripts para el siguiente paso.
    Vec3 desiredVelocity;         // horizontal (x, z) en m/s
    float jumpSpeed = 0;          // > 0: saltar en el próximo paso si está en el suelo
    bool grounded = false;
    Vec3 velocity;                // la que tuvo en el último paso
};

// Unión física entre esta entidad (necesita Rigidbody + Collider) y otra, o el mundo si "connected" está vacío.
struct Joint {
    enum class Type { Fixed, Hinge, Distance };
    Type type = Type::Hinge;
    UUID connected;               // id de la otra entidad (vacío = sujeta al mundo)
    Vec3 anchor;                  // punto de unión, en el espacio local de esta entidad
    Vec3 axis{0, 1, 0};           // bisagra: eje de giro (local)
    bool limitAngle = false;      // bisagra: limitar el giro
    float minAngle = -45, maxAngle = 45;
    float minDistance = 0, maxDistance = 2; // distancia: cuerda (min 0) o barra (min = max)
    float springFrequency = 0;    // distancia: > 0 = muelle (Hz)
    float springDamping = 0.5f;
};

struct Script {
    AssetRef script; // archivo .lua del proyecto
    bool enabled = true;
    // Valores de las variables "props" del script cambiados en el editor para esta entidad (objeto JSON).
    std::string props = "{}";
};

// ---- Interfaz de juego (UI) ----
// Las entidades con UIRect se dibujan encima del juego, en píxeles de pantalla, y no usan su Transform.
// Coordenadas: origen arriba a la izquierda, Y hacia abajo. Las medidas están en "píxeles de referencia"
// de una pantalla de 720 de alto; se escalan con la altura real (ver UISystem::scaleFor).
// Un UIRect hijo de otro UIRect se coloca dentro del rectángulo del padre.
struct UIRect {
    Vec2 anchorMin{0.5f, 0.5f}; // punto del padre (0..1) al que se ancla; min != max estira
    Vec2 anchorMax{0.5f, 0.5f};
    Vec2 pivot{0.5f, 0.5f};     // punto propio que se coloca en el ancla
    Vec2 position;              // desplazamiento desde el ancla
    Vec2 size{240, 80};         // tamaño (si se estira: lo que se suma al área de los anclajes)
};

struct UIImage {
    AssetRef texture;           // vacío = color liso
    Color color{1, 1, 1, 1};
    float cornerRadius = 0;     // esquinas redondeadas (px de referencia); muy grande = cápsula o círculo
    float fill = 1;             // 0..1: parte visible de izquierda a derecha (barras de vida)
};

struct UIText {
    std::string text = "Texto";
    AssetRef font;              // vacío = fuente del motor
    bool bold = false;          // solo con la fuente del motor
    float size = 36;            // px de referencia
    Color color{1, 1, 1, 1};
    int align = 1;              // 0 izquierda, 1 centro, 2 derecha
    int verticalAlign = 1;      // 0 arriba, 1 centro, 2 abajo
    bool shadow = true;         // sombra suave detrás del texto
};

struct UIButton {
    bool interactable = true;
    Color pressedTint{0.75f, 0.75f, 0.75f, 1}; // multiplica el color de su UIImage mientras se pulsa
    // Estado en ejecución (no se guarda)
    bool pressed = false;
    bool clicked = false;       // se soltó encima en este fotograma
    int pointer = -1;
};

struct UIJoystick {
    Color baseColor{1, 1, 1, 0.18f};
    Color handleColor{1, 1, 1, 0.65f};
    float handleSize = 0.45f;   // fracción del tamaño del rectángulo
    float deadZone = 0.1f;
    // Estado en ejecución (no se guarda)
    Vec2 value;                 // -1..1; Y positiva = arriba
    int pointer = -1;
};

// ---- Partículas ----
struct ParticleState; // simulación en ejecución (titan/core/Particles.h)

// Emisor de partículas: fuego, humo, chispas, lluvia, magia... Se simula en la CPU y se dibuja con cuadrados
// orientados a la cámara. Emite hacia +Y local, dentro de un cono de apertura "spread".
struct ParticleSystem {
    bool playOnStart = true;
    bool looping = true;
    float duration = 5.0f;       // segundos de cada ciclo (sin repetir: deja de emitir al terminar)
    float rate = 20.0f;          // partículas por segundo
    float ratePerMeter = 0.0f;   // partículas por metro recorrido por el emisor (estelas)
    int burst = 0;               // partículas de golpe al empezar cada ciclo (explosiones)
    int maxParticles = 300;
    float lifetimeMin = 1.0f, lifetimeMax = 2.0f;
    float speedMin = 1.0f, speedMax = 3.0f;
    float spread = 25.0f;        // grados: 0 = todas hacia arriba, 180 = en todas direcciones
    int shape = 0;               // 0 punto, 1 esfera, 2 caja
    Vec3 shapeSize{0.5f, 0.5f, 0.5f}; // radio (esfera) o tamaño (caja)
    Vec3 acceleration;           // gravedad o viento (m/s²)
    float drag = 0.0f;           // frenado por segundo
    float sizeStart = 0.3f, sizeEnd = 0.1f;
    Color colorStart{1, 1, 1, 1}, colorEnd{1, 1, 1, 0};
    float rotationSpeed = 0.0f;  // grados por segundo (aleatorio entre -v y v)
    AssetRef texture;            // vacía = punto suave del motor
    int sheetColumns = 1, sheetRows = 1; // hoja de animación: recorre los cuadros durante la vida
    bool additive = false;       // mezcla aditiva (fuego, chispas, magia); si no, alfa (humo, polvo)
    bool worldSpace = true;      // las partículas se quedan en el mundo aunque el emisor se mueva
    // En ejecución (no se guarda)
    std::shared_ptr<ParticleState> state;
};

// ---- Terreno ----
// Rejilla de alturas centrada en el origen de su entidad (de -size/2 a size/2 en X y Z), con hasta 4 capas de
// material pintadas (pesos por vértice). Tiene colisión estática propia: no necesita Collider.
struct Terrain {
    float size = 64.0f;          // lado en metros
    float maxHeight = 20.0f;     // altura que corresponde al valor máximo del mapa de alturas
    int resolution = 65;         // vértices por lado (17..257)
    std::vector<uint16_t> heights; // resolution², 0..65535 → 0..maxHeight (vacío = llano)
    std::vector<uint8_t> splat;  // resolution² × 4: peso de cada capa (vacío = todo capa 0)
    AssetRef layerTextures[4];
    Color layerColors[4] = {Color::hex(0x5E8C3A), Color::hex(0x8A7350), Color::hex(0x8C8C8C), Color::hex(0xE8EEF2)};
    float layerTiling = 6.0f;    // metros que ocupa cada repetición de las texturas de capa
    uint32_t revision = 1;       // cambia al editar (el render y la física se rehacen)
};

} // namespace titan
