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
// La API pública está documentada en docs/TITAN_ENGINE_API.md.
//
// Seguridad: sin io, os, package, load ni dofile; límite de memoria del intérprete y
// límite de tiempo por llamada para que un bucle infinito no congele el juego ni el editor.
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/Events.h"
#include "titan/core/Input.h"
#include "titan/core/InputActions.h"
#include "titan/core/Navigation.h"
#include "titan/core/PhysicsApi.h"
#include "titan/core/Quality.h"
#include "titan/core/Scene.h"
#include "titan/core/Serialization.h"

struct lua_State;

namespace titan {

// Lo que el sistema de scripts necesita del runtime que lo aloja.
class VoxelWorld;
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
    // Crea una instancia de un prefab (nombre o ruta) en la posición dada. Nula si no existe.
    virtual Entity scriptInstantiate(const std::string& prefab, const Vec3& position) { return {}; }
    // Sonido: path = recurso del proyecto ("sonidos/disparo.wav"); owner nulo = sonido suelto.
    virtual bool scriptPlaySound(const std::string& path, Entity owner, bool spatial, const Vec3& at, float volume,
                                 const std::string& bus) { return false; }
    virtual class AudioEngine* scriptAudio() { return nullptr; } // mezclador (Audio.setBusVolume...)
    virtual void scriptPlayEntitySound(Entity e) {}
    virtual void scriptStopSound(Entity e) {}
    virtual void scriptSetVolume(float v) {}
    // Calidad gráfica (Quality.*). Sin renderer (pruebas) no hace nada.
    virtual bool scriptSetQuality(const std::string& name) { return false; }
    virtual std::string scriptQuality() const { return ""; }
    virtual DeviceCaps scriptDeviceCaps() const { return {}; }
    // Acciones de entrada (Input.action, Input.axis...). Nulo = sin acciones.
    virtual InputActions* scriptActions() { return nullptr; }
    // Archivo de la partida guardada (lo decide la plataforma, nunca el script). Vacío = solo en memoria.
    virtual std::string scriptSavePath() const { return ""; }
    // Estadísticas del último fotograma (Debug.stats): lo que mide el motor, nunca valores inventados.
    struct FrameStats {
        float fps = 0, frameMs = 0, scriptMs = 0, renderMs = 0;
        int drawCalls = 0, triangles = 0, meshes = 0, meshesVisible = 0, particles = 0, bodies = 0, entities = 0;
    };
    virtual FrameStats scriptFrameStats() const { return {}; }
    // Delante y derecha de la cámara activa, en horizontal (para mover al jugador respecto a la cámara).
    virtual bool scriptCameraBasis(Vec3& forward, Vec3& right) const { return false; }
    // Navegación: la rejilla de la escena (nula si no hay) y hornearla de nuevo (devuelve las celdas caminables).
    virtual NavGrid* scriptNavGrid() { return nullptr; }
    virtual int scriptBakeNav(const NavSettings*) { return 0; }
    // Mundo de bloques de la escena (nulo si no tiene VoxelTerrain) y aviso tras cambiar un bloque (rehace la malla).
    virtual VoxelWorld* scriptVoxelWorld() { return nullptr; }
    virtual void scriptVoxelChanged(int x, int y, int z) {}
    virtual int scriptVoxelSave() { return 0; }
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
    // ---- Partida guardada (Save.* en Lua): datos JSON en memoria que se escriben de forma atómica. ----
    Json& saveData();                 // se carga del archivo la primera vez
    bool writeSave(std::string* error = nullptr);
    bool saveDirty() const { return saveDirty_; }
    void markSaveDirty() { saveDirty_ = true; }

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
    // Llama a un método sin argumentos (p. ej. "onClick") en los scripts de esa entidad.
    void dispatchEvent(Entity self, const char* method, const char* arg = nullptr);
    // Crea los scripts de una entidad nueva y de sus hijos (prefabs instanciados durante el juego).
    void attachSubtree(Entity root);
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
    Json save_;
    bool saveLoaded_ = false, saveDirty_ = false;
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
    bool call(Instance& inst, const char* method, int nargs, const float* arg, const Entity* entityArg = nullptr,
              const char* stringArg = nullptr);
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
    // deque: crear instancias mientras se recorren (Scene.instantiate en onUpdate) no invalida referencias.
    std::deque<Instance> instances_;
    std::unordered_map<uint64_t, int> classRefs_;     // AssetRef -> tabla de la clase
    std::unordered_map<uint64_t, std::string> classNames_;
    std::vector<ScriptError> errors_;
    double timeBudget_ = 0.25;
    size_t memoryLimit_ = 256u * 1024u * 1024u;
    size_t memoryUsed_ = 0;
    double deadline_ = 0;
};

} // namespace titan
