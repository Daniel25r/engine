#include "titan/core/Serialization.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/TerrainTools.h"
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
        if (j->is_object() || j->is_array()) v = j->dump();
        else warn(key, "se esperaba un objeto o una lista");
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

// Máquina de estados de animación (dentro del Animator). Lectura tolerante: lo que falte toma su valor por defecto.
Json machineToJson(const AnimStateMachine& m) {
    static const char* ptypes[] = {"float", "int", "bool", "trigger"};
    static const char* ops[] = {">", "<", "==", "!=", "true", "false"};
    Json params = Json::array(), states = Json::array(), transitions = Json::array();
    for (const AnimParam& p : m.params)
        params.push_back({{"name", p.name}, {"type", ptypes[static_cast<int>(p.type)]}, {"value", toJson(p.value)}});
    for (const AnimState& s : m.states) {
        Json blend = Json::array(), events = Json::array();
        for (const AnimBlendPoint& b : s.blend) blend.push_back({{"threshold", toJson(b.threshold)}, {"clip", b.clip}});
        for (const AnimEvent& e : s.events) events.push_back({{"time", toJson(e.time)}, {"name", e.name}});
        states.push_back({{"name", s.name}, {"clip", s.clip}, {"speed", toJson(s.speed)}, {"loop", s.loop},
                          {"blendParam", s.blendParam}, {"blend", blend}, {"events", events}});
    }
    for (const AnimTransition& t : m.transitions) {
        Json conds = Json::array();
        for (const AnimCondition& c : t.conditions)
            conds.push_back({{"param", c.param}, {"op", ops[static_cast<int>(c.op)]}, {"value", toJson(c.value)}});
        transitions.push_back({{"from", t.from}, {"to", t.to}, {"conditions", conds}, {"duration", toJson(t.duration)},
                               {"hasExitTime", t.hasExitTime}, {"exitTime", toJson(t.exitTime)}});
    }
    return Json{{"params", params}, {"states", states}, {"transitions", transitions}};
}

void machineFromJson(const Json& j, AnimStateMachine& m) {
    m = {};
    if (!j.is_object()) return;
    auto f = [](const Json& o, const char* k, float d) { return o.contains(k) && o[k].is_number() ? o[k].get<float>() : d; };
    auto str = [](const Json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    auto b = [](const Json& o, const char* k, bool d) { return o.contains(k) && o[k].is_boolean() ? o[k].get<bool>() : d; };
    if (j.contains("params") && j["params"].is_array())
        for (const Json& p : j["params"]) {
            AnimParam ap;
            ap.name = str(p, "name");
            const std::string t = str(p, "type");
            ap.type = t == "int" ? AnimParam::Type::Int : t == "bool" ? AnimParam::Type::Bool
                    : t == "trigger" ? AnimParam::Type::Trigger : AnimParam::Type::Float;
            ap.value = f(p, "value", 0);
            if (!ap.name.empty()) m.params.push_back(ap);
        }
    if (j.contains("states") && j["states"].is_array())
        for (const Json& s : j["states"]) {
            AnimState st;
            st.name = str(s, "name");
            st.clip = str(s, "clip");
            st.speed = f(s, "speed", 1);
            st.loop = b(s, "loop", true);
            st.blendParam = str(s, "blendParam");
            if (s.contains("blend") && s["blend"].is_array())
                for (const Json& p : s["blend"]) st.blend.push_back({f(p, "threshold", 0), str(p, "clip")});
            if (s.contains("events") && s["events"].is_array())
                for (const Json& e : s["events"]) st.events.push_back({f(e, "time", 0.5f), str(e, "name")});
            if (!st.name.empty()) m.states.push_back(st);
        }
    if (j.contains("transitions") && j["transitions"].is_array())
        for (const Json& t : j["transitions"]) {
            AnimTransition tr;
            tr.from = str(t, "from");
            tr.to = str(t, "to");
            tr.duration = f(t, "duration", 0.2f);
            tr.hasExitTime = b(t, "hasExitTime", false);
            tr.exitTime = f(t, "exitTime", 1);
            if (t.contains("conditions") && t["conditions"].is_array())
                for (const Json& c : t["conditions"]) {
                    AnimCondition ac;
                    ac.param = str(c, "param");
                    const std::string op = str(c, "op");
                    ac.op = op == "<" ? AnimCondition::Op::Less : op == "==" ? AnimCondition::Op::Equal
                          : op == "!=" ? AnimCondition::Op::NotEqual : op == "true" ? AnimCondition::Op::IsTrue
                          : op == "false" ? AnimCondition::Op::IsFalse : AnimCondition::Op::Greater;
                    ac.value = f(c, "value", 0);
                    tr.conditions.push_back(ac);
                }
            if (!tr.to.empty()) m.transitions.push_back(tr);
        }
}

// Campos de material de un MeshRenderer. Los usan el componente y los archivos de material (.mat).
void writeMaterialFields(const MeshRenderer& m, Json& j) {
    j["texture"] = assetToJson(m.texture);
    j["color"] = toJson(m.color);
    j["metallic"] = toJson(m.metallic);
    j["roughness"] = toJson(m.roughness);
    j["emission"] = toJson(m.emission);
    j["prototypeGrid"] = m.prototypeGrid;
    j["unlit"] = m.unlit;
    j["normalMap"] = assetToJson(m.normalMap);
    j["normalStrength"] = toJson(m.normalStrength);
    j["clearcoat"] = toJson(m.clearcoat);
    j["clearcoatRoughness"] = toJson(m.clearcoatRoughness);
    if (m.graph) j["graph"] = Json::parse(m.graph->toJson());
    j["metallicRoughnessMap"] = assetToJson(m.metallicRoughnessMap);
    j["emissionMap"] = assetToJson(m.emissionMap);
    j["occlusionMap"] = assetToJson(m.occlusionMap);
    j["tiling"] = toJson(m.tiling);
    j["offset"] = toJson(m.offset);
    j["alphaClip"] = m.alphaClip;
    j["alphaCutoff"] = toJson(m.alphaCutoff);
    j["doubleSided"] = m.doubleSided;
}

void readMaterialFields(MeshRenderer& m, FieldReader& rd) {
    rd.readAsset("texture", m.texture);
    rd.read("color", m.color);
    rd.read("metallic", m.metallic);
    rd.read("roughness", m.roughness);
    rd.read("emission", m.emission);
    rd.read("prototypeGrid", m.prototypeGrid);
    rd.read("unlit", m.unlit);
    rd.readAsset("normalMap", m.normalMap);
    rd.read("normalStrength", m.normalStrength);
    rd.read("clearcoat", m.clearcoat);
    rd.read("clearcoatRoughness", m.clearcoatRoughness);
    std::string graph;
    rd.readRaw("graph", graph);
    if (!graph.empty() && graph != "null") {
        auto g = std::make_shared<MaterialGraph>();
        if (g->fromJson(graph)) m.graph = g;
    }
    rd.readAsset("metallicRoughnessMap", m.metallicRoughnessMap);
    rd.readAsset("emissionMap", m.emissionMap);
    rd.readAsset("occlusionMap", m.occlusionMap);
    rd.read("tiling", m.tiling);
    rd.read("offset", m.offset);
    rd.read("alphaClip", m.alphaClip);
    rd.read("alphaCutoff", m.alphaCutoff);
    rd.read("doubleSided", m.doubleSided);
}

std::string serializeMaterial(const MeshRenderer& m) {
    Json j{{"format", "titan.material"}, {"version", 1}, {"engineVersion", kEngineVersion}};
    writeMaterialFields(m, j);
    return j.dump(2);
}

LoadReport parseMaterial(const std::string& text, MeshRenderer& out) {
    LoadReport report;
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", "") != "titan.material") {
        report.errors.push_back("no es un archivo de material de Titán");
        return report;
    }
    if (doc.value("version", 0) > 1) {
        report.errors.push_back("el material es de una versión más nueva del formato");
        return report;
    }
    FieldReader rd(doc, "material", report);
    readMaterialFields(out, rd);
    return report;
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
            Json j{{"mesh", names[static_cast<int>(m.mesh)]}, {"model", assetToJson(m.model)},
                   {"material", assetToJson(m.material)}, {"castShadows", m.castShadows}, {"visible", m.visible}};
            writeMaterialFields(m, j);
            return j;
        },
        [](MeshRenderer& m, FieldReader& rd) {
            rd.readEnum("mesh", m.mesh,
                        {{"cube", MeshPrimitive::Cube}, {"sphere", MeshPrimitive::Sphere}, {"plane", MeshPrimitive::Plane},
                         {"cylinder", MeshPrimitive::Cylinder}, {"cone", MeshPrimitive::Cone}, {"capsule", MeshPrimitive::Capsule}});
            rd.readAsset("model", m.model);
            rd.readAsset("material", m.material);
            rd.read("castShadows", m.castShadows);
            rd.read("visible", m.visible);
            readMaterialFields(m, rd);
        });

    r.addSimple<ReflectionProbe>(
        "ReflectionProbe",
        [](const ReflectionProbe& p) { return Json{{"intensity", toJson(p.intensity)}, {"resolution", p.resolution}}; },
        [](ReflectionProbe& p, FieldReader& rd) {
            rd.read("intensity", p.intensity);
            rd.read("resolution", p.resolution);
        });

    r.addSimple<Environment>(
        "Environment",
        [](const Environment& e) {
            return Json{{"sky", e.sky}, {"skyZenith", toJson(e.skyZenith)}, {"skyHorizon", toJson(e.skyHorizon)},
                        {"ground", toJson(e.ground)}, {"ambientIntensity", toJson(e.ambientIntensity)},
                        {"fogColor", toJson(e.fogColor)}, {"fogDensity", toJson(e.fogDensity)},
                        {"exposure", toJson(e.exposure)}, {"shadows", e.shadows}, {"shadowDistance", toJson(e.shadowDistance)},
                        {"bloom", e.bloom}, {"bloomIntensity", toJson(e.bloomIntensity)},
                        {"bloomThreshold", toJson(e.bloomThreshold)}, {"vignette", toJson(e.vignette)},
                        {"contrast", toJson(e.contrast)}, {"saturation", toJson(e.saturation)},
                        {"temperature", toJson(e.temperature)}, {"ssao", e.ssao},
                        {"ssaoIntensity", toJson(e.ssaoIntensity)}, {"ssaoRadius", toJson(e.ssaoRadius)},
                        {"ssr", e.ssr}, {"ssrIntensity", toJson(e.ssrIntensity)}, {"taa", e.taa}, {"hdr", e.hdr},
                        {"autoExposure", e.autoExposure}, {"adaptationSpeed", toJson(e.adaptationSpeed)}};
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
            rd.read("bloom", e.bloom);
            rd.read("bloomIntensity", e.bloomIntensity);
            rd.read("bloomThreshold", e.bloomThreshold);
            rd.read("vignette", e.vignette);
            rd.read("contrast", e.contrast);
            rd.read("saturation", e.saturation);
            rd.read("temperature", e.temperature);
            rd.read("ssao", e.ssao);
            rd.read("ssaoIntensity", e.ssaoIntensity);
            rd.read("ssaoRadius", e.ssaoRadius);
            rd.read("ssr", e.ssr);
            rd.read("ssrIntensity", e.ssrIntensity);
            rd.read("taa", e.taa);
            rd.read("hdr", e.hdr);
            rd.read("autoExposure", e.autoExposure);
            rd.read("adaptationSpeed", e.adaptationSpeed);
        });

    r.addSimple<Light>(
        "Light",
        [](const Light& l) {
            static const char* types[] = {"directional", "point", "spot"};
            return Json{{"type", types[static_cast<int>(l.type)]}, {"color", toJson(l.color)},
                        {"intensity", toJson(l.intensity)}, {"range", toJson(l.range)},
                        {"spotAngle", toJson(l.spotAngle)}, {"spotSoftness", toJson(l.spotSoftness)}};
        },
        [](Light& l, FieldReader& rd) {
            rd.readEnum("type", l.type, {{"directional", Light::Type::Directional}, {"point", Light::Type::Point},
                                         {"spot", Light::Type::Spot}});
            rd.read("spotAngle", l.spotAngle);
            rd.read("spotSoftness", l.spotSoftness);
            rd.read("color", l.color);
            rd.read("intensity", l.intensity);
            rd.read("range", l.range);
        });

    r.addSimple<Animator>(
        "Animator",
        [](const Animator& a) {
            Json j{{"clip", a.clip}, {"playing", a.playing}, {"loop", a.loop}, {"speed", toJson(a.speed)},
                   {"fadeDuration", toJson(a.fadeDuration)}};
            if (!a.machine.empty()) j["machine"] = machineToJson(a.machine);
            if (!a.animationSources.empty()) {
                Json src = Json::array();
                for (AssetRef r : a.animationSources) src.push_back(assetToJson(r));
                j["animationSources"] = src;
            }
            return j;
        },
        [](Animator& a, FieldReader& rd) {
            rd.read("clip", a.clip);
            rd.read("playing", a.playing);
            rd.read("loop", a.loop);
            rd.read("speed", a.speed);
            rd.read("fadeDuration", a.fadeDuration);
            std::string raw;
            rd.readRaw("machine", raw);
            if (!raw.empty()) machineFromJson(Json::parse(raw, nullptr, false), a.machine);
            std::string src;
            rd.readRaw("animationSources", src);
            const Json js = src.empty() ? Json() : Json::parse(src, nullptr, false);
            if (js.is_array())
                for (const Json& v : js) {
                    UUID id;
                    if (v.is_string() && UUID::parse(v.get<std::string>(), id)) a.animationSources.push_back(id);
                }
        });

    r.addSimple<AudioSource>(
        "AudioSource",
        [](const AudioSource& a) {
            Json j{{"clip", assetToJson(a.clip)}, {"volume", toJson(a.volume)}, {"pitch", toJson(a.pitch)},
                   {"loop", a.loop}, {"playOnStart", a.playOnStart}, {"spatial", a.spatial},
                   {"minDistance", toJson(a.minDistance)}, {"maxDistance", toJson(a.maxDistance)}};
            if (!a.bus.empty()) j["bus"] = a.bus; // las escenas sin buses se guardan igual que antes
            return j;
        },
        [](AudioSource& a, FieldReader& rd) {
            rd.readAsset("clip", a.clip);
            rd.read("volume", a.volume);
            rd.read("pitch", a.pitch);
            rd.read("loop", a.loop);
            rd.read("playOnStart", a.playOnStart);
            rd.read("spatial", a.spatial);
            rd.read("minDistance", a.minDistance);
            rd.read("maxDistance", a.maxDistance);
            rd.read("bus", a.bus);
        });

    r.addSimple<Terrain>(
        "Terrain",
        [](const Terrain& t) {
            Json layers = Json::array();
            for (int i = 0; i < 4; ++i)
                layers.push_back({{"texture", assetToJson(t.layerTextures[i])}, {"color", toJson(t.layerColors[i])}});
            // Alturas (uint16, little endian) y pesos de capas en base64: compactos y dentro de la escena.
            std::vector<uint8_t> h(t.heights.size() * 2);
            for (size_t i = 0; i < t.heights.size(); ++i) {
                h[i * 2] = static_cast<uint8_t>(t.heights[i] & 0xFF);
                h[i * 2 + 1] = static_cast<uint8_t>(t.heights[i] >> 8);
            }
            return Json{{"size", toJson(t.size)}, {"maxHeight", toJson(t.maxHeight)}, {"resolution", t.resolution},
                        {"layerTiling", toJson(t.layerTiling)}, {"layers", layers},
                        {"heights", base64Encode(h.data(), h.size())}, {"splat", base64Encode(t.splat.data(), t.splat.size())}};
        },
        [](Terrain& t, FieldReader& rd) {
            rd.read("size", t.size);
            rd.read("maxHeight", t.maxHeight);
            rd.read("resolution", t.resolution);
            t.resolution = std::clamp(t.resolution, 17, 257);
            rd.read("layerTiling", t.layerTiling);
            std::string raw;
            rd.readRaw("layers", raw);
            const Json layers = Json::parse(raw.empty() ? "[]" : raw, nullptr, false);
            if (layers.is_array())
                for (size_t i = 0; i < layers.size() && i < 4; ++i) {
                    LoadReport ignored;
                    FieldReader lr(layers[i], "Terrain.layers", ignored);
                    lr.readAsset("texture", t.layerTextures[i]);
                    lr.read("color", t.layerColors[i]);
                }
            std::string hb, sb;
            rd.read("heights", hb);
            rd.read("splat", sb);
            const size_t n = size_t(t.resolution) * t.resolution;
            std::vector<uint8_t> bytes;
            if (base64Decode(hb, bytes) && bytes.size() == n * 2) {
                t.heights.resize(n);
                for (size_t i = 0; i < n; ++i) t.heights[i] = static_cast<uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
            }
            if (base64Decode(sb, bytes) && bytes.size() == n * 4) t.splat = bytes;
            terrainEnsureData(t);
            ++t.revision;
        });

    r.addSimple<VoxelTerrain>(
        "VoxelTerrain",
        [](const VoxelTerrain& v) {
            return Json{{"seed", v.seed}, {"viewDistance", v.viewDistance}, {"worldName", v.worldName}, {"seaLevel", v.seaLevel},
                        {"terrainScale", toJson(v.terrainScale)}, {"caves", v.caves}, {"trees", v.trees}};
        },
        [](VoxelTerrain& v, FieldReader& rd) {
            rd.read("seed", v.seed);
            rd.read("viewDistance", v.viewDistance);
            rd.read("worldName", v.worldName);
            rd.read("seaLevel", v.seaLevel);
            rd.read("terrainScale", v.terrainScale);
            rd.read("caves", v.caves);
            rd.read("trees", v.trees);
        });

    r.addSimple<NavAgent>(
        "NavAgent",
        [](const NavAgent& a) {
            return Json{{"speed", toJson(a.speed)}, {"stoppingDistance", toJson(a.stoppingDistance)},
                        {"repathInterval", toJson(a.repathInterval)}};
        },
        [](NavAgent& a, FieldReader& rd) {
            rd.read("speed", a.speed);
            rd.read("stoppingDistance", a.stoppingDistance);
            rd.read("repathInterval", a.repathInterval);
        });

    r.addSimple<BlobShadow>(
        "BlobShadow",
        [](const BlobShadow& b) {
            return Json{{"radius", toJson(b.radius)}, {"opacity", toJson(b.opacity)}, {"maxHeight", toJson(b.maxHeight)}};
        },
        [](BlobShadow& b, FieldReader& rd) {
            rd.read("radius", b.radius);
            rd.read("opacity", b.opacity);
            rd.read("maxHeight", b.maxHeight);
        });

    r.addSimple<CameraFollow>(
        "CameraFollow",
        [](const CameraFollow& c) {
            return Json{{"target", c.target.isNull() ? Json(nullptr) : Json(c.target.toString())},
                        {"distance", toJson(c.distance)}, {"minDistance", toJson(c.minDistance)},
                        {"maxDistance", toJson(c.maxDistance)}, {"height", toJson(c.height)}, {"yaw", toJson(c.yaw)},
                        {"pitch", toJson(c.pitch)}, {"minPitch", toJson(c.minPitch)}, {"maxPitch", toJson(c.maxPitch)},
                        {"rotateWithInput", c.rotateWithInput}, {"touchSensitivity", toJson(c.touchSensitivity)},
                        {"padSpeed", toJson(c.padSpeed)}, {"smoothing", toJson(c.smoothing)},
                        {"avoidWalls", c.avoidWalls}, {"wallRadius", toJson(c.wallRadius)}};
        },
        [](CameraFollow& c, FieldReader& rd) {
            std::string id;
            rd.read("target", id);
            if (!id.empty()) UUID::parse(id, c.target);
            rd.read("distance", c.distance);
            rd.read("minDistance", c.minDistance);
            rd.read("maxDistance", c.maxDistance);
            rd.read("height", c.height);
            rd.read("yaw", c.yaw);
            rd.read("pitch", c.pitch);
            rd.read("minPitch", c.minPitch);
            rd.read("maxPitch", c.maxPitch);
            rd.read("rotateWithInput", c.rotateWithInput);
            rd.read("touchSensitivity", c.touchSensitivity);
            rd.read("padSpeed", c.padSpeed);
            rd.read("smoothing", c.smoothing);
            rd.read("avoidWalls", c.avoidWalls);
            rd.read("wallRadius", c.wallRadius);
        });

    r.addSimple<CharacterController>(
        "CharacterController",
        [](const CharacterController& c) {
            return Json{{"height", toJson(c.height)}, {"radius", toJson(c.radius)}, {"maxSlope", toJson(c.maxSlope)},
                        {"stepHeight", toJson(c.stepHeight)}, {"gravityScale", toJson(c.gravityScale)}, {"layer", c.layer},
                        {"acceleration", toJson(c.acceleration)}, {"deceleration", toJson(c.deceleration)},
                        {"airControl", toJson(c.airControl)}, {"maxJumps", c.maxJumps}, {"jumpCut", toJson(c.jumpCut)},
                        {"faceMovement", c.faceMovement}, {"turnSpeed", toJson(c.turnSpeed)}};
        },
        [](CharacterController& c, FieldReader& rd) {
            rd.read("height", c.height);
            rd.read("radius", c.radius);
            rd.read("maxSlope", c.maxSlope);
            rd.read("stepHeight", c.stepHeight);
            rd.read("gravityScale", c.gravityScale);
            rd.read("layer", c.layer);
            c.layer = std::clamp(c.layer, 0, 15);
            rd.read("acceleration", c.acceleration);
            rd.read("deceleration", c.deceleration);
            rd.read("airControl", c.airControl);
            rd.read("maxJumps", c.maxJumps);
            rd.read("jumpCut", c.jumpCut);
            rd.read("faceMovement", c.faceMovement);
            rd.read("turnSpeed", c.turnSpeed);
        });

    r.addSimple<Joint>(
        "Joint",
        [](const Joint& j) {
            static const char* types[] = {"fixed", "hinge", "distance"};
            return Json{{"type", types[static_cast<int>(j.type)]}, {"connected", j.connected.isNull() ? Json(nullptr) : Json(j.connected.toString())},
                        {"anchor", toJson(j.anchor)}, {"axis", toJson(j.axis)}, {"limitAngle", j.limitAngle},
                        {"minAngle", toJson(j.minAngle)}, {"maxAngle", toJson(j.maxAngle)},
                        {"minDistance", toJson(j.minDistance)}, {"maxDistance", toJson(j.maxDistance)},
                        {"springFrequency", toJson(j.springFrequency)}, {"springDamping", toJson(j.springDamping)}};
        },
        [](Joint& j, FieldReader& rd) {
            rd.readEnum("type", j.type, {{"fixed", Joint::Type::Fixed}, {"hinge", Joint::Type::Hinge}, {"distance", Joint::Type::Distance}});
            std::string id;
            rd.read("connected", id);
            if (!id.empty()) UUID::parse(id, j.connected);
            rd.read("anchor", j.anchor);
            rd.read("axis", j.axis);
            rd.read("limitAngle", j.limitAngle);
            rd.read("minAngle", j.minAngle);
            rd.read("maxAngle", j.maxAngle);
            rd.read("minDistance", j.minDistance);
            rd.read("maxDistance", j.maxDistance);
            rd.read("springFrequency", j.springFrequency);
            rd.read("springDamping", j.springDamping);
        });

    r.addSimple<ParticleSystem>(
        "ParticleSystem",
        [](const ParticleSystem& p) {
            return Json{{"playOnStart", p.playOnStart}, {"looping", p.looping}, {"duration", toJson(p.duration)},
                        {"rate", toJson(p.rate)}, {"ratePerMeter", toJson(p.ratePerMeter)}, {"burst", p.burst},
                        {"maxParticles", p.maxParticles}, {"lifetimeMin", toJson(p.lifetimeMin)},
                        {"lifetimeMax", toJson(p.lifetimeMax)}, {"speedMin", toJson(p.speedMin)},
                        {"speedMax", toJson(p.speedMax)}, {"spread", toJson(p.spread)}, {"shape", p.shape},
                        {"shapeSize", toJson(p.shapeSize)}, {"acceleration", toJson(p.acceleration)},
                        {"drag", toJson(p.drag)}, {"sizeStart", toJson(p.sizeStart)}, {"sizeEnd", toJson(p.sizeEnd)},
                        {"colorStart", toJson(p.colorStart)}, {"colorEnd", toJson(p.colorEnd)},
                        {"rotationSpeed", toJson(p.rotationSpeed)}, {"texture", assetToJson(p.texture)},
                        {"sheetColumns", p.sheetColumns}, {"sheetRows", p.sheetRows}, {"additive", p.additive},
                        {"worldSpace", p.worldSpace}};
        },
        [](ParticleSystem& p, FieldReader& rd) {
            rd.read("playOnStart", p.playOnStart);
            rd.read("looping", p.looping);
            rd.read("duration", p.duration);
            rd.read("rate", p.rate);
            rd.read("ratePerMeter", p.ratePerMeter);
            rd.read("burst", p.burst);
            rd.read("maxParticles", p.maxParticles);
            p.maxParticles = std::clamp(p.maxParticles, 0, 10000);
            rd.read("lifetimeMin", p.lifetimeMin);
            rd.read("lifetimeMax", p.lifetimeMax);
            rd.read("speedMin", p.speedMin);
            rd.read("speedMax", p.speedMax);
            rd.read("spread", p.spread);
            rd.read("shape", p.shape);
            rd.read("shapeSize", p.shapeSize);
            rd.read("acceleration", p.acceleration);
            rd.read("drag", p.drag);
            rd.read("sizeStart", p.sizeStart);
            rd.read("sizeEnd", p.sizeEnd);
            rd.read("colorStart", p.colorStart);
            rd.read("colorEnd", p.colorEnd);
            rd.read("rotationSpeed", p.rotationSpeed);
            rd.readAsset("texture", p.texture);
            rd.read("sheetColumns", p.sheetColumns);
            rd.read("sheetRows", p.sheetRows);
            rd.read("additive", p.additive);
            rd.read("worldSpace", p.worldSpace);
        });

    r.addSimple<UIRect>(
        "UIRect",
        [](const UIRect& u) {
            return Json{{"anchorMin", toJson(u.anchorMin)}, {"anchorMax", toJson(u.anchorMax)}, {"pivot", toJson(u.pivot)},
                        {"position", toJson(u.position)}, {"size", toJson(u.size)}};
        },
        [](UIRect& u, FieldReader& rd) {
            rd.read("anchorMin", u.anchorMin);
            rd.read("anchorMax", u.anchorMax);
            rd.read("pivot", u.pivot);
            rd.read("position", u.position);
            rd.read("size", u.size);
        });

    r.addSimple<UIImage>(
        "UIImage",
        [](const UIImage& u) {
            return Json{{"texture", assetToJson(u.texture)}, {"color", toJson(u.color)},
                        {"cornerRadius", toJson(u.cornerRadius)}, {"fill", toJson(u.fill)}};
        },
        [](UIImage& u, FieldReader& rd) {
            rd.readAsset("texture", u.texture);
            rd.read("color", u.color);
            rd.read("cornerRadius", u.cornerRadius);
            rd.read("fill", u.fill);
        });

    r.addSimple<UIText>(
        "UIText",
        [](const UIText& u) {
            return Json{{"text", u.text}, {"font", assetToJson(u.font)}, {"bold", u.bold}, {"size", toJson(u.size)},
                        {"color", toJson(u.color)}, {"align", u.align}, {"verticalAlign", u.verticalAlign},
                        {"shadow", u.shadow}};
        },
        [](UIText& u, FieldReader& rd) {
            rd.read("text", u.text);
            rd.readAsset("font", u.font);
            rd.read("bold", u.bold);
            rd.read("size", u.size);
            rd.read("color", u.color);
            rd.read("align", u.align);
            rd.read("verticalAlign", u.verticalAlign);
            rd.read("shadow", u.shadow);
        });

    r.addSimple<UIButton>(
        "UIButton",
        [](const UIButton& u) { return Json{{"interactable", u.interactable}, {"pressedTint", toJson(u.pressedTint)}}; },
        [](UIButton& u, FieldReader& rd) {
            rd.read("interactable", u.interactable);
            rd.read("pressedTint", u.pressedTint);
        });

    r.addSimple<UIJoystick>(
        "UIJoystick",
        [](const UIJoystick& u) {
            return Json{{"baseColor", toJson(u.baseColor)}, {"handleColor", toJson(u.handleColor)},
                        {"handleSize", toJson(u.handleSize)}, {"deadZone", toJson(u.deadZone)}};
        },
        [](UIJoystick& u, FieldReader& rd) {
            rd.read("baseColor", u.baseColor);
            rd.read("handleColor", u.handleColor);
            rd.read("handleSize", u.handleSize);
            rd.read("deadZone", u.deadZone);
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
            static const char* shapes[] = {"box", "sphere", "capsule", "mesh", "convex"};
            return Json{{"shape", shapes[static_cast<int>(c.shape)]}, {"size", toJson(c.size)}, {"radius", toJson(c.radius)},
                        {"height", toJson(c.height)}, {"center", toJson(c.center)}, {"isTrigger", c.isTrigger}, {"layer", c.layer}};
        },
        [](Collider& c, FieldReader& rd) {
            rd.readEnum("shape", c.shape, {{"box", Collider::Shape::Box}, {"sphere", Collider::Shape::Sphere}, {"capsule", Collider::Shape::Capsule}, {"mesh", Collider::Shape::Mesh}, {"convex", Collider::Shape::ConvexHull}});
            rd.read("size", c.size);
            rd.read("radius", c.radius);
            rd.read("height", c.height);
            rd.read("center", c.center);
            rd.read("isTrigger", c.isTrigger);
            rd.read("layer", c.layer);
            c.layer = std::clamp(c.layer, 0, 15);
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
