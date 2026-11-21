#include "titan/core/Serialization.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/Version.h"

namespace titan {

// ---------------------------------------------------------------- JSON básico

// El número decimal más corto que vuelve a dar exactamente el mismo float:
// los archivos guardan 0.9 y no 0.8999999761581421, sin perder precisión.
Json toJson(float f) {
    char buf[32];
    for (int precision = 6; precision <= 9; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, static_cast<double>(f));
        if (std::strtof(buf, nullptr) == f) break;
    }
    return Json(std::strtod(buf, nullptr));
}
Json toJson(const Vec2& v) { return Json::array({toJson(v.x), toJson(v.y)}); }
Json toJson(const Vec3& v) { return Json::array({toJson(v.x), toJson(v.y), toJson(v.z)}); }
Json toJson(const Quat& q) { return Json::array({toJson(q.x), toJson(q.y), toJson(q.z), toJson(q.w)}); }
Json toJson(const Color& c) { return Json::array({toJson(c.r), toJson(c.g), toJson(c.b), toJson(c.a)}); }
Json toJson(const Rect& r) { return Json::array({toJson(r.x), toJson(r.y), toJson(r.w), toJson(r.h)}); }
Json assetToJson(AssetRef ref) { return ref.isNull() ? Json(nullptr) : Json(ref.toString()); }

const Json* FieldReader::field(const char* key) {
    if (!obj_.is_object()) return nullptr;
    auto it = obj_.find(key);
    if (it == obj_.end() || it->is_null()) return nullptr;
    return &*it;
}

void FieldReader::warn(const char* key, const std::string& msg) {
    report_.warnings.push_back(where_ + "." + key + ": " + msg);
}

void FieldReader::read(const char* key, float& v) {
    if (auto* j = field(key)) {
        if (j->is_number()) v = j->get<float>();
        else warn(key, "se esperaba un número");
    }
}
void FieldReader::read(const char* key, int& v) {
    if (auto* j = field(key)) {
        if (j->is_number_integer()) v = j->get<int>();
        else warn(key, "se esperaba un entero");
    }
}
void FieldReader::read(const char* key, bool& v) {
    if (auto* j = field(key)) {
        if (j->is_boolean()) v = j->get<bool>();
        else warn(key, "se esperaba true o false");
    }
}
void FieldReader::read(const char* key, std::string& v) {
    if (auto* j = field(key)) {
        if (j->is_string()) v = j->get<std::string>();
        else warn(key, "se esperaba un texto");
    }
}

namespace {
bool readFloats(const Json& j, float* out, size_t n) {
    if (!j.is_array() || j.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!j[i].is_number()) return false;
        out[i] = j[i].get<float>();
    }
    return true;
}
} // namespace

#define TITAN_READ_FLOATS(TYPE, N, ...)                                                    \
    void FieldReader::read(const char* key, TYPE& v) {                                     \
        if (auto* j = field(key)) {                                                        \
            float f[N];                                                                    \
            if (readFloats(*j, f, N)) v = TYPE{__VA_ARGS__};                               \
            else warn(key, "se esperaba una lista de " #N " números");                     \
        }                                                                                  \
    }
TITAN_READ_FLOATS(Vec2, 2, f[0], f[1])
TITAN_READ_FLOATS(Vec3, 3, f[0], f[1], f[2])
TITAN_READ_FLOATS(Color, 4, f[0], f[1], f[2], f[3])
TITAN_READ_FLOATS(Rect, 4, f[0], f[1], f[2], f[3])
#undef TITAN_READ_FLOATS

void FieldReader::read(const char* key, Quat& v) {
    if (auto* j = field(key)) {
        float f[4];
        if (readFloats(*j, f, 4)) v = Quat{f[0], f[1], f[2], f[3]}.normalized();
        else warn(key, "se esperaba un cuaternión [x, y, z, w]");
    }
}

void FieldReader::readRaw(const char* key, std::string& v) {
    if (auto* j = field(key)) {
        if (j->is_object()) v = j->dump();
        else warn(key, "se esperaba un objeto");
    }
}

void FieldReader::readAsset(const char* key, AssetRef& v) {
    if (auto* j = field(key)) {
        UUID id;
        if (j->is_string() && UUID::parse(j->get<std::string>(), id)) {
            v = id;
            report_.assetRefs.push_back(id);
        } else {
            warn(key, "referencia a recurso no válida");
        }
    }
}

// ---------------------------------------------------------------- Registro

void ComponentRegistry::add(ComponentType type) {
    for (auto& t : types_)
        if (t.name == type.name) {
            t = std::move(type);
            return;
        }
    types_.push_back(std::move(type));
}

const ComponentType* ComponentRegistry::find(const std::string& name) const {
    for (auto& t : types_)
        if (t.name == name) return &t;
    return nullptr;
}

ComponentRegistry ComponentRegistry::createDefault() {
    ComponentRegistry r;

    r.add({"Transform", [](Scene& s, Entity e) { return s.world().has<Transform>(e); },
           [](Scene& s, Entity e) {
               const Transform& t = s.transform(e);
               return Json{{"position", toJson(t.position())}, {"rotation", toJson(t.rotation())}, {"scale", toJson(t.scale())}};
           },
           [](Scene& s, Entity e, FieldReader& rd) {
               Transform& t = s.transform(e);
               Vec3 p = t.position(), sc = t.scale();
               Quat q = t.rotation();
               rd.read("position", p);
               rd.read("rotation", q);
               rd.read("scale", sc);
               t.setPosition(p);
               t.setRotation(q);
               t.setScale(sc);
           },
           nullptr});

    r.addSimple<Camera>(
        "Camera",
        [](const Camera& c) {
            return Json{{"projection", c.projection == Camera::Projection::Perspective ? "perspective" : "orthographic"},
                        {"orthographicSize", toJson(c.orthographicSize)}, {"fieldOfView", toJson(c.fieldOfView)},
                        {"near", toJson(c.nearClip)}, {"far", toJson(c.farClip)}, {"priority", c.priority},
                        {"clearColor", toJson(c.clearColor)}};
        },
        [](Camera& c, FieldReader& rd) {
            rd.readEnum("projection", c.projection,
                        {{"orthographic", Camera::Projection::Orthographic}, {"perspective", Camera::Projection::Perspective}});
            rd.read("orthographicSize", c.orthographicSize);
            rd.read("fieldOfView", c.fieldOfView);
            rd.read("near", c.nearClip);
            rd.read("far", c.farClip);
            rd.read("priority", c.priority);
            rd.read("clearColor", c.clearColor);
        });

    r.addSimple<SpriteRenderer>(
        "SpriteRenderer",
        [](const SpriteRenderer& sp) {
            return Json{{"texture", assetToJson(sp.texture)}, {"color", toJson(sp.color)}, {"size", toJson(sp.size)},
                        {"pivot", toJson(sp.pivot)}, {"uv", toJson(sp.uv)}, {"sortingOrder", sp.sortingOrder},
                        {"visible", sp.visible}};
        },
        [](SpriteRenderer& sp, FieldReader& rd) {
            rd.readAsset("texture", sp.texture);
            rd.read("color", sp.color);
            rd.read("size", sp.size);
            rd.read("pivot", sp.pivot);
            rd.read("uv", sp.uv);
            rd.read("sortingOrder", sp.sortingOrder);
            rd.read("visible", sp.visible);
        });

    r.addSimple<MeshRenderer>(
        "MeshRenderer",
        [](const MeshRenderer& m) {
            static const char* names[] = {"cube", "sphere", "plane", "cylinder", "cone", "capsule"};
            return Json{{"mesh", names[static_cast<int>(m.mesh)]}, {"model", assetToJson(m.model)}, {"texture", assetToJson(m.texture)},
                        {"color", toJson(m.color)}, {"metallic", toJson(m.metallic)}, {"roughness", toJson(m.roughness)},
                        {"emission", toJson(m.emission)}, {"prototypeGrid", m.prototypeGrid},
                        {"castShadows", m.castShadows}, {"unlit", m.unlit}, {"visible", m.visible}};
        },
        [](MeshRenderer& m, FieldReader& rd) {
            rd.readEnum("mesh", m.mesh,
                        {{"cube", MeshPrimitive::Cube}, {"sphere", MeshPrimitive::Sphere}, {"plane", MeshPrimitive::Plane},
                         {"cylinder", MeshPrimitive::Cylinder}, {"cone", MeshPrimitive::Cone}, {"capsule", MeshPrimitive::Capsule}});
            rd.readAsset("model", m.model);
            rd.readAsset("texture", m.texture);
            rd.read("color", m.color);
            rd.read("metallic", m.metallic);
            rd.read("roughness", m.roughness);
            rd.read("emission", m.emission);
            rd.read("prototypeGrid", m.prototypeGrid);
            rd.read("castShadows", m.castShadows);
            rd.read("unlit", m.unlit);
            rd.read("visible", m.visible);
        });

    r.addSimple<Environment>(
        "Environment",
        [](const Environment& e) {
            return Json{{"sky", e.sky}, {"skyZenith", toJson(e.skyZenith)}, {"skyHorizon", toJson(e.skyHorizon)},
                        {"ground", toJson(e.ground)}, {"ambientIntensity", toJson(e.ambientIntensity)},
                        {"fogColor", toJson(e.fogColor)}, {"fogDensity", toJson(e.fogDensity)},
                        {"exposure", toJson(e.exposure)}, {"shadows", e.shadows}, {"shadowDistance", toJson(e.shadowDistance)}};
        },
        [](Environment& e, FieldReader& rd) {
            rd.read("sky", e.sky);
            rd.read("skyZenith", e.skyZenith);
            rd.read("skyHorizon", e.skyHorizon);
            rd.read("ground", e.ground);
            rd.read("ambientIntensity", e.ambientIntensity);
            rd.read("fogColor", e.fogColor);
            rd.read("fogDensity", e.fogDensity);
            rd.read("exposure", e.exposure);
            rd.read("shadows", e.shadows);
            rd.read("shadowDistance", e.shadowDistance);
        });

    r.addSimple<Light>(
        "Light",
        [](const Light& l) {
            return Json{{"type", l.type == Light::Type::Point ? "point" : "directional"}, {"color", toJson(l.color)},
                        {"intensity", toJson(l.intensity)}, {"range", toJson(l.range)}};
        },
        [](Light& l, FieldReader& rd) {
            rd.readEnum("type", l.type, {{"directional", Light::Type::Directional}, {"point", Light::Type::Point}});
            rd.read("color", l.color);
            rd.read("intensity", l.intensity);
            rd.read("range", l.range);
        });

    r.addSimple<Rigidbody>(
        "Rigidbody",
        [](const Rigidbody& b) {
            static const char* types[] = {"static", "dynamic", "kinematic"};
            return Json{{"type", types[static_cast<int>(b.type)]}, {"mass", toJson(b.mass)}, {"friction", toJson(b.friction)},
                        {"restitution", toJson(b.restitution)}, {"linearDamping", toJson(b.linearDamping)},
                        {"angularDamping", toJson(b.angularDamping)}, {"gravityScale", toJson(b.gravityScale)},
                        {"lockRotation", b.lockRotation}};
        },
        [](Rigidbody& b, FieldReader& rd) {
            rd.readEnum("type", b.type, {{"static", Rigidbody::Type::Static}, {"dynamic", Rigidbody::Type::Dynamic},
                                         {"kinematic", Rigidbody::Type::Kinematic}});
            rd.read("mass", b.mass);
            rd.read("friction", b.friction);
            rd.read("restitution", b.restitution);
            rd.read("linearDamping", b.linearDamping);
            rd.read("angularDamping", b.angularDamping);
            rd.read("gravityScale", b.gravityScale);
            rd.read("lockRotation", b.lockRotation);
        });

    r.addSimple<Collider>(
        "Collider",
        [](const Collider& c) {
            static const char* shapes[] = {"box", "sphere", "capsule"};
            return Json{{"shape", shapes[static_cast<int>(c.shape)]}, {"size", toJson(c.size)}, {"radius", toJson(c.radius)},
                        {"height", toJson(c.height)}, {"center", toJson(c.center)}, {"isTrigger", c.isTrigger}};
        },
        [](Collider& c, FieldReader& rd) {
            rd.readEnum("shape", c.shape, {{"box", Collider::Shape::Box}, {"sphere", Collider::Shape::Sphere},
                                           {"capsule", Collider::Shape::Capsule}});
            rd.read("size", c.size);
            rd.read("radius", c.radius);
            rd.read("height", c.height);
            rd.read("center", c.center);
            rd.read("isTrigger", c.isTrigger);
        });

    r.addSimple<Script>(
        "Script",
        [](const Script& sc) {
            Json props = Json::parse(sc.props, nullptr, false);
            if (props.is_discarded() || !props.is_object()) props = Json::object();
            return Json{{"script", assetToJson(sc.script)}, {"enabled", sc.enabled}, {"props", props}};
        },
        [](Script& sc, FieldReader& rd) {
            rd.readAsset("script", sc.script);
            rd.read("enabled", sc.enabled);
            rd.readRaw("props", sc.props);
        });
    return r;
}

// ---------------------------------------------------------------- Migraciones

namespace {

// migrations[v] convierte un documento de la versión v a la v+1.
// Al cambiar el formato: subir kSceneFormatVersion y añadir aquí la migración.
const std::map<int, std::function<void(Json&)>>& sceneMigrations() {
    static const std::map<int, std::function<void(Json&)>> m;
    return m;
}

const char* kSceneFormatName = "titan.scene";

} // namespace

// ---------------------------------------------------------------- Escenas

std::string serializeScene(Scene& scene, const ComponentRegistry& registry) {
    Json entities = Json::array();
    scene.traverse([&](Entity e, int) {
        const EntityInfo& info = scene.info(e);
        const Entity parent = scene.parent(e);
        Json components = Json::object();
        for (const auto& type : registry.types())
            if (type.has(scene, e)) components[type.name] = type.save(scene, e);
        if (const auto* kept = scene.world().get<PreservedComponents>(e))
            for (auto it = kept->data.begin(); it != kept->data.end(); ++it)
                if (!components.contains(it.key())) components[it.key()] = it.value();
        entities.push_back({{"id", info.id.toString()},
                            {"name", info.name},
                            {"active", info.active},
                            {"parent", parent.isNull() ? Json(nullptr) : Json(scene.info(parent).id.toString())},
                            {"components", std::move(components)}});
    });
    Json doc{{"format", kSceneFormatName},
             {"version", kSceneFormatVersion},
             {"engineVersion", kEngineVersion},
             {"name", scene.name()},
             {"entities", std::move(entities)}};
    return doc.dump(2);
}

LoadReport deserializeScene(const std::string& text, Scene& scene, const ComponentRegistry& registry) {
    LoadReport report;
    scene.clear();

    Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        report.errors.push_back("el archivo no es JSON válido (¿está dañado o incompleto?)");
        return report;
    }
    if (!doc.is_object() || doc.value("format", "") != kSceneFormatName) {
        report.errors.push_back("no es un archivo de escena de Titán (falta \"format\": \"titan.scene\")");
        return report;
    }
    if (!doc.contains("version") || !doc["version"].is_number_integer()) {
        report.errors.push_back("falta la versión del formato");
        return report;
    }
    int version = doc["version"].get<int>();
    report.fileVersion = version;
    if (version > kSceneFormatVersion) {
        report.errors.push_back("la escena es de una versión más nueva del formato (" + std::to_string(version) +
                                ") que este motor (" + std::to_string(kSceneFormatVersion) +
                                "). Actualiza el motor; el archivo no se ha modificado.");
        return report;
    }
    while (version < kSceneFormatVersion) {
        auto it = sceneMigrations().find(version);
        if (it == sceneMigrations().end()) {
            report.errors.push_back("no hay migración desde la versión " + std::to_string(version));
            return report;
        }
        it->second(doc);
        ++version;
        report.warnings.push_back("escena migrada a la versión " + std::to_string(version));
    }

    if (doc.contains("name") && doc["name"].is_string()) scene.setName(doc["name"].get<std::string>());
    const Json& list = doc.contains("entities") ? doc["entities"] : Json::array();
    if (!list.is_array()) {
        report.errors.push_back("\"entities\" debe ser una lista");
        return report;
    }

    // 1) Crear todas las entidades (como raíces, en el orden del archivo).
    std::vector<std::pair<Entity, const Json*>> created;
    for (size_t i = 0; i < list.size(); ++i) {
        const Json& je = list[i];
        const std::string where = "entities[" + std::to_string(i) + "]";
        if (!je.is_object()) {
            report.warnings.push_back(where + ": no es un objeto, se ignora");
            continue;
        }
        UUID id;
        if (!je.contains("id") || !je["id"].is_string() || !UUID::parse(je["id"].get<std::string>(), id) || id.isNull()) {
            report.warnings.push_back(where + ": id no válido, se genera uno nuevo");
            id = {};
        } else if (!scene.findById(id).isNull()) {
            report.warnings.push_back(where + ": id " + id.toString() + " repetido, se genera uno nuevo");
            id = {};
        }
        const std::string name = je.contains("name") && je["name"].is_string() ? je["name"].get<std::string>() : "Entidad";
        Entity e = scene.createEntity(name, {}, id);
        if (je.contains("active") && je["active"].is_boolean()) scene.info(e).active = je["active"].get<bool>();
        created.emplace_back(e, &je);
    }

    // 2) Jerarquía.
    for (auto& [e, je] : created) {
        const Json& jp = (*je).contains("parent") ? (*je)["parent"] : Json(nullptr);
        if (jp.is_null()) continue;
        UUID pid;
        Entity parent;
        if (jp.is_string() && UUID::parse(jp.get<std::string>(), pid)) parent = scene.findById(pid);
        if (parent.isNull() || !scene.setParent(e, parent, false))
            report.warnings.push_back("'" + scene.info(e).name + "': su padre no existe, queda como raíz");
    }

    // 3) Componentes.
    for (auto& [e, je] : created) {
        if (!(*je).contains("components")) continue;
        const Json& comps = (*je)["components"];
        if (!comps.is_object()) {
            report.warnings.push_back("'" + scene.info(e).name + "': \"components\" no es un objeto");
            continue;
        }
        for (auto it = comps.begin(); it != comps.end(); ++it) {
            const ComponentType* type = registry.find(it.key());
            const std::string where = scene.info(e).name + "." + it.key();
            if (!type) {
                auto& kept = scene.world().has<PreservedComponents>(e) ? *scene.world().get<PreservedComponents>(e)
                                                                       : scene.world().add<PreservedComponents>(e);
                kept.data[it.key()] = it.value();
                report.warnings.push_back(where + ": tipo de componente desconocido, se conserva sin usar");
                continue;
            }
            if (!it.value().is_object()) {
                report.warnings.push_back(where + ": datos no válidos, se ignora");
                continue;
            }
            FieldReader reader(it.value(), where, report);
            type->load(scene, e, reader);
        }
    }
    return report;
}

Entity copySubtree(Scene& src, Entity from, Scene& dst, Entity dstParent, const ComponentRegistry& registry) {
    Entity to = dst.createEntity(src.info(from).name, dstParent);
    dst.info(to).active = src.info(from).active;
    for (const auto& type : registry.types()) {
        if (!type.has(src, from)) continue;
        const Json data = type.save(src, from);
        LoadReport ignored;
        FieldReader reader(data, type.name, ignored);
        type.load(dst, to, reader);
    }
    if (auto* kept = src.world().get<PreservedComponents>(from)) dst.world().add<PreservedComponents>(to, *kept);
    for (Entity c : src.children(from)) copySubtree(src, c, dst, to, registry);
    return to;
}

std::string serializePrefab(Scene& scene, Entity root, const ComponentRegistry& registry) {
    Scene tmp(scene.info(root).name);
    Entity r = copySubtree(scene, root, tmp, {}, registry);
    tmp.transform(r).setPosition({}); // el prefab se guarda en el origen; al instanciarlo se coloca donde toque
    return serializeScene(tmp, registry);
}

Entity instantiatePrefab(const std::string& text, Scene& scene, Entity parent, const ComponentRegistry& registry,
                         LoadReport* report) {
    Scene tmp("prefab");
    LoadReport rep = deserializeScene(text, tmp, registry);
    if (report) *report = rep;
    const auto roots = tmp.roots();
    if (!rep.ok() || roots.empty()) return {};
    return copySubtree(tmp, roots.front(), scene, parent, registry);
}

bool saveSceneFile(Scene& scene, const ComponentRegistry& registry, const std::string& path, std::string* error) {
    const bool ok = fileio::writeAtomic(path, serializeScene(scene, registry), true, error);
    if (ok) TITAN_INFO("scene", "Escena '%s' guardada en %s (%zu entidades)", scene.name().c_str(), path.c_str(), scene.entityCount());
    return ok;
}

LoadReport loadSceneFile(const std::string& path, Scene& scene, const ComponentRegistry& registry) {
    std::string text;
    LoadReport report;
    if (!fileio::readText(path, text)) {
        report.errors.push_back("no se puede leer " + path);
    } else {
        report = deserializeScene(text, scene, registry);
    }
    if (!report.ok() && std::filesystem::exists(path + ".bak")) {
        std::string backup;
        LoadReport fromBackup;
        if (fileio::readText(path + ".bak", backup)) fromBackup = deserializeScene(backup, scene, registry);
        if (fromBackup.ok()) {
            fromBackup.recoveredFromBackup = true;
            fromBackup.warnings.insert(fromBackup.warnings.begin(),
                                       "el archivo estaba dañado (" + report.errors.front() +
                                           "); se ha cargado la copia de seguridad .bak");
            TITAN_WARN("scene", "%s dañado; recuperado desde la copia de seguridad", path.c_str());
            return fromBackup;
        }
    }
    for (auto& e : report.errors) TITAN_ERROR("scene", "%s: %s", path.c_str(), e.c_str());
    for (auto& w : report.warnings) TITAN_WARN("scene", "%s: %s", path.c_str(), w.c_str());
    return report;
}

} // namespace titan
