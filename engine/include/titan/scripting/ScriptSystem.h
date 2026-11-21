#pragma once
// Sistema de scripts en Lua 5.4.
//
// Un script es un archivo .lua que devuelve una tabla (una "clase"):
//
//     local Nave = {}
//     function Nave:onStart() Debug.log("hola desde " .. self.entity:name()) end
//     function Nave:onUpdate(dt) self.entity:translate(2 * dt, 0, 0) end
//     return Nave
//
// Cada entidad con componente Script recibe su propia instancia (self) con self.entity.
// La API pública está documentada en docs/ENGINE_API.md.
//
// Seguridad: sin io, os, package, load ni dofile; límite de memoria del intérprete y
// límite de tiempo por llamada para que un bucle infinito no congele el juego ni el editor.
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/Events.h"
#include "titan/core/Input.h"
#include "titan/core/PhysicsApi.h"
#include "titan/core/Scene.h"
#include "titan/core/Serialization.h"

struct lua_State;

namespace titan {

// Lo que el sistema de scripts necesita del runtime que lo aloja.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    virtual double scriptTime() const = 0;
    virtual float scriptDeltaTime() const = 0;
    virtual float scriptFixedDeltaTime() const = 0;
    virtual uint64_t scriptFrame() const = 0;
    virtual const Input& scriptInput() const = 0;
    virtual Vec2 scriptScreenSize() const = 0;
    virtual bool scriptScreenToWorld(Vec2 pixels, Vec3& world) const = 0;
    virtual bool scriptLoadScene(const std::string& nameOrPath) = 0;
    // Texto en pantalla durante este fotograma (píxeles, origen arriba a la izquierda).
    virtual void scriptDrawText(float x, float y, float size, const std::string& text, Color color) = 0;
    // Física de la escena (nulo si no hay).
    virtual PhysicsApi* scriptPhysics() { return nullptr; }
};

struct ScriptError {
    std::string script;   // nombre del archivo
    std::string entity;   // entidad que lo ejecutaba
    std::string function; // onUpdate, onStart...
    std::string message;  // incluye "archivo:línea:"
    std::string traceback;
};

class ScriptSystem {
public:
    // Devuelve el nombre del archivo (para los mensajes de error) y su código fuente.
    using SourceLoader = std::function<bool(AssetRef, std::string& chunkName, std::string& source)>;

    ScriptSystem(ScriptHost& host, const ComponentRegistry& registry, SourceLoader loader);
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // Crea las instancias de todos los Script de la escena y llama onCreate y luego onStart.
    void attach(Scene& scene, EventBus& events);
    // Llama onDestroy en todas las instancias y las libera.
    void detach();

    void fixedUpdate(float dt);
    // Llama onCollisionEnter/Exit u onTriggerEnter/Exit(other) en los scripts de "self".
    void dispatchContact(Entity self, Entity other, bool begin, bool trigger);
    void update(float dt);
    void lateUpdate(float dt);

    // Ejecuta código suelto (pruebas, consola). Devuelve false y anota el error si falla.
    bool runString(const std::string& code, const std::string& chunkName = "consola");

    // Límite de tiempo de cada llamada a un script, en segundos.
    void setTimeBudget(double seconds) { timeBudget_ = seconds; }
    void setMemoryLimit(size_t bytes) { memoryLimit_ = bytes; }

    const std::vector<ScriptError>& errors() const { return errors_; }
    void clearErrors() { errors_.clear(); }
    size_t instanceCount() const;
    size_t memoryUsed() const { return memoryUsed_; }

    // --- Uso interno de los enlaces con Lua ---
    Scene* scene() const { return scene_; }
    ScriptHost& host() { return host_; }
    const ComponentRegistry& registry() const { return registry_; }
    int instanceRef(Entity e) const;
    void setEntityActive(Entity e, bool active);
    void armDeadline();
    bool deadlinePassed() const;
    void* allocate(void* ptr, size_t oldSize, size_t newSize);

private:
    struct Instance {
        Entity entity;
        int ref = -1; // referencia a la tabla self en el registro de Lua
        std::string script;
        bool started = false;
        bool alive = true;
        bool failed = false;
    };

    int loadClass(AssetRef id, std::string& chunkName);
    void createInstance(Entity e, const Script& component);
    bool call(Instance& inst, const char* method, int nargs, const float* arg, const Entity* entityArg = nullptr);
    void callAll(const char* method, float dt);
    void destroyInstance(Instance& inst);
    void reportError(const std::string& script, const std::string& entity, const std::string& function,
                     const std::string& message);
    void registerApi();

    ScriptHost& host_;
    const ComponentRegistry& registry_;
    SourceLoader loader_;
    lua_State* L_ = nullptr;
    Scene* scene_ = nullptr;
    Subscription destroyedSub_;
    std::vector<Instance> instances_;
    std::unordered_map<uint64_t, int> classRefs_;     // AssetRef -> tabla de la clase
    std::unordered_map<uint64_t, std::string> classNames_;
    std::vector<ScriptError> errors_;
    double timeBudget_ = 0.25;
    size_t memoryLimit_ = 256u * 1024u * 1024u;
    size_t memoryUsed_ = 0;
    double deadline_ = 0;
};

} // namespace titan
