// Escritor de glTF 2.0 binario (GLB): cabecera, fragmento JSON y fragmento binario con posiciones, normales, UV,
// índices de 32 bits e imágenes incrustadas. Especificación: registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
#include <cstring>

#include "titan/core/MeshBuild.h"
#include "titan/core/Serialization.h"

namespace titan {

size_t MeshBuild::triangleCount() const {
    size_t n = 0;
    for (const MeshBuildPart& p : parts) n += p.indices.size() / 3;
    return n;
}

void MeshBuild::computeFlatNormals() {
    for (MeshBuildPart& p : parts)
        for (size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            MeshBuildVertex& a = p.vertices[p.indices[i]];
            MeshBuildVertex& b = p.vertices[p.indices[i + 1]];
            MeshBuildVertex& c = p.vertices[p.indices[i + 2]];
            Vec3 n = cross(b.position - a.position, c.position - a.position);
            n = n.length() > 1e-12f ? n.normalized() : Vec3{0, 1, 0};
            a.normal = b.normal = c.normal = n;
        }
}

namespace {

void align4(std::vector<uint8_t>& b, uint8_t pad = 0) {
    while (b.size() % 4) b.push_back(pad);
}

template <class T>
size_t append(std::vector<uint8_t>& bin, const T* data, size_t count) {
    align4(bin);
    const size_t offset = bin.size();
    bin.resize(offset + sizeof(T) * count);
    std::memcpy(bin.data() + offset, data, sizeof(T) * count);
    return offset;
}

} // namespace

bool writeGlb(const MeshBuild& mesh, std::vector<uint8_t>& out, std::string* error) {
    if (mesh.triangleCount() == 0) {
        if (error) *error = "la malla no tiene triángulos";
        return false;
    }
    std::vector<uint8_t> bin;
    Json bufferViews = Json::array(), accessors = Json::array(), primitives = Json::array();
    Json materials = Json::array(), images = Json::array(), textures = Json::array();
    auto view = [&](size_t offset, size_t length, int target) {
        Json v{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
        if (target) v["target"] = target;
        bufferViews.push_back(v);
        return static_cast<int>(bufferViews.size() - 1);
    };
    auto accessor = [&](int bufferView, int componentType, size_t count, const char* type) {
        accessors.push_back(Json{{"bufferView", bufferView}, {"componentType", componentType}, {"count", count}, {"type", type}});
        return static_cast<int>(accessors.size() - 1);
    };

    for (const MeshBuildMaterial& m : mesh.materials) {
        Json pbr{{"baseColorFactor", {m.color.r, m.color.g, m.color.b, m.color.a}},
                 {"metallicFactor", m.metallic},
                 {"roughnessFactor", m.roughness}};
        if (!m.image.empty()) {
            const size_t off = append(bin, m.image.data(), m.image.size());
            images.push_back(Json{{"bufferView", view(off, m.image.size(), 0)}, {"mimeType", m.imageMime.empty() ? "image/png" : m.imageMime}});
            textures.push_back(Json{{"source", images.size() - 1}});
            pbr["baseColorTexture"] = Json{{"index", textures.size() - 1}};
        }
        materials.push_back(Json{{"name", m.name}, {"pbrMetallicRoughness", pbr}, {"doubleSided", false}});
    }
    if (materials.empty()) materials.push_back(Json{{"name", "Material"}, {"pbrMetallicRoughness", Json{{"baseColorFactor", {0.8, 0.8, 0.8, 1}}}}});

    for (const MeshBuildPart& p : mesh.parts) {
        if (p.indices.empty() || p.vertices.empty()) continue;
        std::vector<float> pos, nrm, uv;
        Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (const MeshBuildVertex& v : p.vertices) {
            pos.insert(pos.end(), {v.position.x, v.position.y, v.position.z});
            nrm.insert(nrm.end(), {v.normal.x, v.normal.y, v.normal.z});
            uv.insert(uv.end(), {v.uv.x, v.uv.y});
            mn = {std::min(mn.x, v.position.x), std::min(mn.y, v.position.y), std::min(mn.z, v.position.z)};
            mx = {std::max(mx.x, v.position.x), std::max(mx.y, v.position.y), std::max(mx.z, v.position.z)};
        }
        const size_t n = p.vertices.size();
        const int aPos = accessor(view(append(bin, pos.data(), pos.size()), pos.size() * 4, 34962), 5126, n, "VEC3");
        accessors[aPos]["min"] = {mn.x, mn.y, mn.z}; // obligatorios en POSITION
        accessors[aPos]["max"] = {mx.x, mx.y, mx.z};
        const int aNrm = accessor(view(append(bin, nrm.data(), nrm.size()), nrm.size() * 4, 34962), 5126, n, "VEC3");
        const int aUv = accessor(view(append(bin, uv.data(), uv.size()), uv.size() * 4, 34962), 5126, n, "VEC2");
        const int aIdx = accessor(view(append(bin, p.indices.data(), p.indices.size()), p.indices.size() * 4, 34963), 5125,
                                  p.indices.size(), "SCALAR");
        const int mat = p.material >= 0 && size_t(p.material) < materials.size() ? p.material : 0;
        primitives.push_back(Json{{"attributes", {{"POSITION", aPos}, {"NORMAL", aNrm}, {"TEXCOORD_0", aUv}}},
                                  {"indices", aIdx}, {"material", mat}, {"mode", 4}});
    }
    align4(bin);

    Json gltf{{"asset", {{"version", "2.0"}, {"generator", "Titán Engine"}}},
              {"scene", 0},
              {"scenes", Json::array({Json{{"nodes", {0}}}})},
              {"nodes", Json::array({Json{{"mesh", 0}, {"name", "Malla"}}})},
              {"meshes", Json::array({Json{{"primitives", primitives}}})},
              {"materials", materials},
              {"buffers", Json::array({Json{{"byteLength", bin.size()}}})},
              {"bufferViews", bufferViews},
              {"accessors", accessors}};
    if (!images.empty()) {
        gltf["images"] = images;
        gltf["textures"] = textures;
        gltf["samplers"] = Json::array({Json{{"magFilter", 9729}, {"minFilter", 9987}}});
        for (Json& t : gltf["textures"]) t["sampler"] = 0;
    }
    std::string json = gltf.dump();
    while (json.size() % 4) json.push_back(' '); // el fragmento JSON se rellena con espacios

    out.clear();
    auto u32 = [&](uint32_t v) {
        const size_t at = out.size();
        out.resize(at + 4);
        std::memcpy(out.data() + at, &v, 4);
    };
    u32(0x46546C67); // "glTF"
    u32(2);
    u32(uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    u32(uint32_t(json.size()));
    u32(0x4E4F534A); // "JSON"
    out.insert(out.end(), json.begin(), json.end());
    u32(uint32_t(bin.size()));
    u32(0x004E4942); // "BIN\0"
    out.insert(out.end(), bin.begin(), bin.end());
    return true;
}

} // namespace titan
