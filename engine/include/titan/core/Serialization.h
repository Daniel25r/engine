#pragma once
// Serialización de escenas a JSON versionado (formato documentado en docs/FILE_FORMATS.md).
// - Cada tipo de componente se registra con un nombre estable y sus funciones de guardar/cargar.
// - Los componentes desconocidos NO se pierden: se conservan tal cual y se vuelven a guardar.
// - Los archivos de versiones antiguas pasan por migraciones; los de versiones futuras se rechazan.
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "titan/core/Scene.h"

namespace titan {

using Json = nlohmann::json;

struct LoadReport {
    std::vector<std::string> errors;   // impiden cargar el archivo
    std::vector<std::string> warnings; // se cargó, pero algo se ignoró o corrigió
    std::vector<AssetRef> assetRefs;   // recursos citados por el archivo (dependencias)
    int fileVersion = 0;
    bool recoveredFromBackup = false;
    bool ok() const { return errors.empty(); }
};

// Lectura tolerante de campos: si falta, se deja el valor por defecto; si el tipo es
// incorrecto, se deja el valor por defecto y se anota un aviso con la ruta del campo.
class FieldReader {
public:
    FieldReader(const Json& obj, std::string where, LoadReport& report) : obj_(obj), where_(std::move(where)), report_(report) {}
    void read(const char* key, float& v);
    void read(const char* key, int& v);
    void read(const char* key, bool& v);
    void read(const char* key, std::string& v);
    void read(const char* key, Vec2& v);
    void read(const char* key, Vec3& v);
    void read(const char* key, Quat& v);
    void read(const char* key, Color& v);
    void read(const char* key, Rect& v);
    void readAsset(const char* key, AssetRef& v);
    // Copia un objeto o una lista JSON tal cual, como texto.
    void readRaw(const char* key, std::string& v);
    template <class E>
    void readEnum(const char* key, E& v, std::initializer_list<std::pair<const char*, E>> names) {
        std::string s;
        read(key, s);
        if (s.empty()) return;
        for (auto& [n, val] : names)
            if (s == n) { v = val; return; }
        warn(key, "valor desconocido '" + s + "'");
    }

private:
    const Json* field(const char* key);
    void warn(const char* key, const std::string& msg);
    const Json& obj_;
    std::string where_;
    LoadReport& report_;
};

struct ComponentType {
    std::string name;
    std::function<bool(Scene&, Entity)> has;
    std::function<Json(Scene&, Entity)> save;
    std::function<void(Scene&, Entity, FieldReader&)> load;
    std::function<void(Scene&, Entity)> remove; // vacío = no se puede quitar (p. ej. Transform)
};

class ComponentRegistry {
public:
    // Registro con los componentes integrados (Transform, Camera, SpriteRenderer, Script).
    static ComponentRegistry createDefault();

    void add(ComponentType type);
    // Atajo para componentes que son structs simples.
    template <class T>
    void addSimple(const std::string& name, std::function<Json(const T&)> save, std::function<void(T&, FieldReader&)> load) {
        add({name, [](Scene& s, Entity e) { return s.world().has<T>(e); },
             [save](Scene& s, Entity e) { return save(*s.world().get<T>(e)); },
             [load](Scene& s, Entity e, FieldReader& r) {
                 T value = s.world().has<T>(e) ? *s.world().get<T>(e) : T{};
                 load(value, r);
                 s.world().add<T>(e, value);
             },
             [](Scene& s, Entity e) { s.world().remove<T>(e); }});
    }
    const ComponentType* find(const std::string& name) const;
    const std::vector<ComponentType>& types() const { return types_; }

private:
    std::vector<ComponentType> types_;
};

// Componentes de tipos no registrados, guardados tal cual para no perder datos.
struct PreservedComponents {
    Json data = Json::object();
};

constexpr int kSceneFormatVersion = 1;

// Copia una entidad con todos sus hijos y componentes (con ids nuevos) a otra escena, o a la misma.
// Es la base de Duplicar y de los prefabs.
Entity copySubtree(Scene& src, Entity root, Scene& dst, Entity dstParent, const ComponentRegistry& registry);
// Prefab: un archivo de escena cuyo contenido es un único subárbol.
std::string serializePrefab(Scene& scene, Entity root, const ComponentRegistry& registry);
// Crea en "scene" una instancia del prefab (texto) bajo "parent". Devuelve la raíz creada o nula si falla.
Entity instantiatePrefab(const std::string& text, Scene& scene, Entity parent, const ComponentRegistry& registry,
                         LoadReport* report = nullptr);

std::string serializeScene(Scene& scene, const ComponentRegistry& registry);

// Máquina de estados de animación (campo "machine" del Animator).
Json machineToJson(const AnimStateMachine& m);
void machineFromJson(const Json& j, AnimStateMachine& m);

// Materiales (.mat): los campos de material de un MeshRenderer (color, mapas, tiling...), en JSON versionado.
void writeMaterialFields(const MeshRenderer& m, Json& j);
void readMaterialFields(MeshRenderer& m, FieldReader& rd);
std::string serializeMaterial(const MeshRenderer& m);
LoadReport parseMaterial(const std::string& text, MeshRenderer& out); // solo cambia los campos de material
// Vacía la escena y la rellena desde el texto. Si hay errores la escena queda vacía.
LoadReport deserializeScene(const std::string& text, Scene& scene, const ComponentRegistry& registry);

bool saveSceneFile(Scene& scene, const ComponentRegistry& registry, const std::string& path, std::string* error = nullptr);
// Si el archivo está dañado y existe "<ruta>.bak", intenta recuperar desde ella.
LoadReport loadSceneFile(const std::string& path, Scene& scene, const ComponentRegistry& registry);

// Conversión de tipos básicos a JSON (también la usan proyecto y recursos).
Json toJson(float f);
Json toJson(const Vec2& v);
Json toJson(const Vec3& v);
Json toJson(const Quat& q);
Json toJson(const Color& c);
Json toJson(const Rect& r);
Json assetToJson(AssetRef ref);

} // namespace titan
