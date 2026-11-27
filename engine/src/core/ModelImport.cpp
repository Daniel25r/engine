// Lectores de OBJ/MTL, STL y PLY escritos para Titán, y conversión de .blend con el Blender instalado.
#include "titan/core/ModelImport.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

#include "titan/core/FileIO.h"

namespace fs = std::filesystem;

namespace titan {

namespace {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string lowerExt(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

void warn(ImportReport* r, const std::string& w) {
    if (r) r->warnings.push_back(w);
}

bool fail(std::string* e, const std::string& why) {
    if (e) *e = why;
    return false;
}

// ---------------------------------------------------------------- MTL
void loadMtl(const fs::path& path, std::vector<MeshBuildMaterial>& mats, std::map<std::string, int>& byName, ImportReport* r) {
    std::ifstream f(path);
    if (!f) {
        warn(r, "no se encontró la biblioteca de materiales " + path.filename().string());
        return;
    }
    std::string line;
    MeshBuildMaterial* cur = nullptr;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string k;
        s >> k;
        if (k == "newmtl") {
            std::string name;
            std::getline(s >> std::ws, name);
            mats.push_back({});
            mats.back().name = name;
            byName[name] = int(mats.size() - 1);
            cur = &mats.back();
        } else if (!cur) {
            continue;
        } else if (k == "Kd") {
            s >> cur->color.r >> cur->color.g >> cur->color.b;
        } else if (k == "d") {
            s >> cur->color.a;
        } else if (k == "Tr") {
            float t = 0;
            s >> t;
            cur->color.a = 1 - t;
        } else if (k == "Ns") { // brillo Phong → rugosidad aproximada
            float ns = 0;
            s >> ns;
            cur->roughness = std::clamp(std::sqrt(2.0f / (std::max(ns, 0.0f) + 2.0f)), 0.04f, 1.0f);
        } else if (k == "Pm") {
            s >> cur->metallic;
        } else if (k == "Pr") {
            s >> cur->roughness;
        } else if (k == "map_Kd") {
            std::string rest, tok;
            std::getline(s >> std::ws, rest);
            std::istringstream rs(rest);
            while (rs >> tok) {} // las opciones (-s, -o...) van antes; el archivo es lo último
            fs::path tex = path.parent_path() / tok;
            const std::string e = lowerExt(tex.string());
            if (e != ".png" && e != ".jpg" && e != ".jpeg") {
                warn(r, "textura " + tok + ": formato no admitido todavía (solo PNG y JPG)");
            } else if (!readFile(tex.string(), cur->image)) {
                warn(r, "no se encontró la textura " + tok);
            } else {
                cur->imageMime = e == ".png" ? "image/png" : "image/jpeg";
                cur->color = {1, 1, 1, cur->color.a}; // con textura, el color base la multiplica
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------- OBJ
bool loadObj(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error) {
    std::ifstream f(path);
    if (!f) return fail(error, "no se pudo abrir " + path);
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> tex;
    std::map<std::string, int> matByName;
    out = {};
    int curMat = -1;
    std::map<int, size_t> partByMat;
    std::vector<std::map<std::tuple<int, int, int>, uint32_t>> dedupe;
    bool missingNormals = false;
    std::string line;
    int lineNo = 0;
    auto partFor = [&](int mat) -> MeshBuildPart& {
        auto it = partByMat.find(mat);
        if (it == partByMat.end()) {
            it = partByMat.emplace(mat, out.parts.size()).first;
            out.parts.push_back({});
            out.parts.back().material = std::max(mat, 0);
            dedupe.emplace_back();
        }
        return out.parts[it->second];
    };
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream s(line);
        std::string k;
        s >> k;
        if (k == "v") {
            Vec3 p;
            s >> p.x >> p.y >> p.z;
            pos.push_back(p);
        } else if (k == "vt") {
            Vec2 t;
            s >> t.x >> t.y;
            tex.push_back({t.x, 1.0f - t.y}); // OBJ: V hacia arriba; glTF: hacia abajo
        } else if (k == "vn") {
            Vec3 n;
            s >> n.x >> n.y >> n.z;
            nrm.push_back(n);
        } else if (k == "mtllib") {
            std::string name;
            std::getline(s >> std::ws, name);
            loadMtl(fs::path(path).parent_path() / name, out.materials, matByName, report);
        } else if (k == "usemtl") {
            std::string name;
            std::getline(s >> std::ws, name);
            auto it = matByName.find(name);
            if (it == matByName.end()) {
                out.materials.push_back({});
                out.materials.back().name = name;
                it = matByName.emplace(name, int(out.materials.size() - 1)).first;
            }
            curMat = it->second;
        } else if (k == "f") {
            std::vector<uint32_t> corner;
            std::string tok;
            MeshBuildPart& part = partFor(curMat);
            auto& map = dedupe[partByMat[curMat]];
            while (s >> tok) {
                int vi = 0, ti = 0, ni = 0;
                const size_t a = tok.find('/'), b = a == std::string::npos ? a : tok.find('/', a + 1);
                vi = std::atoi(tok.substr(0, a).c_str());
                if (a != std::string::npos && b != a + 1) ti = std::atoi(tok.substr(a + 1, b - a - 1).c_str());
                if (b != std::string::npos) ni = std::atoi(tok.substr(b + 1).c_str());
                auto fix = [](int i, size_t n) { return i < 0 ? int(n) + i : i - 1; }; // índices negativos = desde el final
                vi = fix(vi, pos.size());
                ti = ti ? fix(ti, tex.size()) : -1;
                ni = ni ? fix(ni, nrm.size()) : -1;
                if (vi < 0 || size_t(vi) >= pos.size()) return fail(error, "línea " + std::to_string(lineNo) + ": vértice inexistente");
                const auto key = std::make_tuple(vi, ti, ni);
                auto found = map.find(key);
                if (found == map.end()) {
                    MeshBuildVertex mv;
                    mv.position = pos[size_t(vi)];
                    if (ti >= 0 && size_t(ti) < tex.size()) mv.uv = tex[size_t(ti)];
                    if (ni >= 0 && size_t(ni) < nrm.size()) mv.normal = nrm[size_t(ni)].normalized();
                    else missingNormals = true;
                    found = map.emplace(key, uint32_t(part.vertices.size())).first;
                    part.vertices.push_back(mv);
                }
                corner.push_back(found->second);
            }
            for (size_t i = 1; i + 1 < corner.size(); ++i) part.indices.insert(part.indices.end(), {corner[0], corner[i], corner[i + 1]});
        }
    }
    if (out.materials.empty()) out.materials.push_back({});
    if (missingNormals) out.computeFlatNormals();
    if (out.triangleCount() == 0) return fail(error, "el OBJ no tiene caras");
    return true;
}

// ---------------------------------------------------------------- STL
bool loadStl(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error) {
    std::vector<uint8_t> b;
    if (!readFile(path, b)) return fail(error, "no se pudo abrir " + path);
    out = {};
    out.materials.push_back({});
    out.parts.push_back({});
    MeshBuildPart& part = out.parts.back();
    // Z arriba (convención de STL/CAD) → Y arriba (glTF): (x, y, z) → (x, z, -y)
    auto toY = [](float x, float y, float z) { return Vec3{x, z, -y}; };
    uint32_t n = 0;
    if (b.size() >= 84) std::memcpy(&n, b.data() + 80, 4);
    const bool binary = b.size() >= 84 && b.size() == 84 + size_t(n) * 50;
    if (binary) {
        for (uint32_t t = 0; t < n; ++t) {
            const uint8_t* p = b.data() + 84 + size_t(t) * 50 + 12; // se salta la normal: se recalcula
            for (int k = 0; k < 3; ++k) {
                float v[3];
                std::memcpy(v, p + k * 12, 12);
                part.vertices.push_back({toY(v[0], v[1], v[2]), {}, {}});
                part.indices.push_back(uint32_t(part.vertices.size() - 1));
            }
        }
    } else {
        std::istringstream s(std::string(b.begin(), b.end()));
        std::string tok;
        while (s >> tok)
            if (tok == "vertex") {
                float x, y, z;
                s >> x >> y >> z;
                part.vertices.push_back({toY(x, y, z), {}, {}});
                part.indices.push_back(uint32_t(part.vertices.size() - 1));
            }
        part.indices.resize(part.indices.size() / 3 * 3);
    }
    if (part.indices.empty()) return fail(error, "el STL no tiene triángulos");
    out.computeFlatNormals();
    warn(report, "STL: sin UV ni materiales (el formato no los tiene); se ha girado de Z arriba a Y arriba");
    return true;
}

// ---------------------------------------------------------------- PLY
bool loadPly(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error) {
    std::vector<uint8_t> b;
    if (!readFile(path, b)) return fail(error, "no se pudo abrir " + path);
    const std::string head(b.begin(), b.begin() + std::min<size_t>(b.size(), 65536));
    const size_t endHeader = head.find("end_header");
    if (head.rfind("ply", 0) != 0 || endHeader == std::string::npos) return fail(error, "no es un archivo PLY");
    size_t dataStart = head.find('\n', endHeader) + 1;
    struct Prop {
        std::string name, type, countType;
        bool list = false;
    };
    struct Element {
        std::string name;
        size_t count = 0;
        std::vector<Prop> props;
    };
    std::vector<Element> elements;
    std::string format;
    std::istringstream hs(head.substr(0, endHeader));
    std::string line;
    while (std::getline(hs, line)) {
        std::istringstream s(line);
        std::string k;
        s >> k;
        if (k == "format") s >> format;
        else if (k == "element") {
            elements.push_back({});
            s >> elements.back().name >> elements.back().count;
        } else if (k == "property" && !elements.empty()) {
            Prop p;
            s >> p.type;
            if (p.type == "list") {
                p.list = true;
                s >> p.countType >> p.type;
            }
            s >> p.name;
            elements.back().props.push_back(p);
        }
    }
    const bool ascii = format == "ascii", big = format == "binary_big_endian";
    if (!ascii && format != "binary_little_endian" && !big) return fail(error, "formato PLY desconocido: " + format);
    auto typeSize = [](const std::string& t) -> size_t {
        if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
        if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
        if (t == "double" || t == "float64") return 8;
        return 4;
    };
    size_t at = dataStart;
    std::istringstream as(ascii ? std::string(b.begin() + long(dataStart), b.end()) : std::string());
    auto readNum = [&](const std::string& t) -> double {
        if (ascii) {
            double v = 0;
            as >> v;
            return v;
        }
        const size_t n = typeSize(t);
        if (at + n > b.size()) return 0;
        uint8_t tmp[8];
        std::memcpy(tmp, b.data() + at, n);
        if (big) std::reverse(tmp, tmp + n);
        at += n;
        if (t == "float" || t == "float32") { float v; std::memcpy(&v, tmp, 4); return v; }
        if (t == "double" || t == "float64") { double v; std::memcpy(&v, tmp, 8); return v; }
        if (t == "char" || t == "int8") return double(int8_t(tmp[0]));
        if (t == "uchar" || t == "uint8") return double(tmp[0]);
        if (t == "short" || t == "int16") { int16_t v; std::memcpy(&v, tmp, 2); return v; }
        if (t == "ushort" || t == "uint16") { uint16_t v; std::memcpy(&v, tmp, 2); return v; }
        if (t == "uint" || t == "uint32") { uint32_t v; std::memcpy(&v, tmp, 4); return v; }
        int32_t v;
        std::memcpy(&v, tmp, 4);
        return v;
    };
    out = {};
    out.materials.push_back({});
    out.parts.push_back({});
    MeshBuildPart& part = out.parts.back();
    bool haveNormals = false, haveColors = false;
    for (const Element& e : elements) {
        for (size_t i = 0; i < e.count; ++i) {
            MeshBuildVertex v;
            std::vector<uint32_t> face;
            for (const Prop& p : e.props) {
                if (p.list) {
                    const size_t n = size_t(readNum(p.countType));
                    for (size_t k = 0; k < n; ++k) {
                        const double x = readNum(p.type);
                        if (e.name == "face" && (p.name == "vertex_indices" || p.name == "vertex_index")) face.push_back(uint32_t(x));
                    }
                    continue;
                }
                const double x = readNum(p.type);
                if (e.name != "vertex") continue;
                if (p.name == "x") v.position.x = float(x);
                else if (p.name == "y") v.position.y = float(x);
                else if (p.name == "z") v.position.z = float(x);
                else if (p.name == "nx") v.normal.x = float(x), haveNormals = true;
                else if (p.name == "ny") v.normal.y = float(x);
                else if (p.name == "nz") v.normal.z = float(x);
                else if (p.name == "s" || p.name == "u" || p.name == "texture_u") v.uv.x = float(x);
                else if (p.name == "t" || p.name == "v" || p.name == "texture_v") v.uv.y = 1.0f - float(x);
                else if (p.name == "red" || p.name == "green" || p.name == "blue") haveColors = true;
            }
            if (e.name == "vertex") part.vertices.push_back(v);
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                if (face[0] >= part.vertices.size() || face[k] >= part.vertices.size() || face[k + 1] >= part.vertices.size())
                    return fail(error, "cara con un vértice inexistente");
                part.indices.insert(part.indices.end(), {face[0], face[k], face[k + 1]});
            }
        }
    }
    if (part.indices.empty()) return fail(error, "el PLY no tiene caras (las nubes de puntos no se pueden convertir en malla)");
    if (!haveNormals) {
        // Normales suaves: promedio de las de las caras (los escaneos se ven mejor así que facetados).
        std::vector<Vec3> acc(part.vertices.size());
        for (size_t i = 0; i + 2 < part.indices.size(); i += 3) {
            const Vec3 &a = part.vertices[part.indices[i]].position, &bb = part.vertices[part.indices[i + 1]].position,
                       &c = part.vertices[part.indices[i + 2]].position;
            const Vec3 n = cross(bb - a, c - a);
            for (int k = 0; k < 3; ++k) acc[part.indices[i + size_t(k)]] += n;
        }
        for (size_t i = 0; i < acc.size(); ++i) part.vertices[i].normal = acc[i].length() > 0 ? acc[i].normalized() : Vec3{0, 1, 0};
    }
    if (haveColors) warn(report, "PLY: los colores por vértice todavía no se importan (solo forma, normales y UV)");
    return true;
}

// ---------------------------------------------------------------- conversión
bool isConvertibleModel(const std::string& path) {
    const std::string e = lowerExt(path);
    return e == ".obj" || e == ".stl" || e == ".ply" || e == ".blend";
}

std::string findBlender() {
    if (const char* env = std::getenv("TITAN_BLENDER"); env && fs::exists(env)) return env;
    std::vector<fs::path> candidates;
#if defined(_WIN32)
    for (const char* root : {"C:/Program Files/Blender Foundation", "C:/Program Files (x86)/Blender Foundation"}) {
        std::error_code ec;
        for (auto& d : fs::directory_iterator(root, ec)) candidates.push_back(d.path() / "blender.exe");
    }
    std::sort(candidates.rbegin(), candidates.rend()); // la versión más nueva primero
    const char* exe = "blender.exe";
    const char sep = ';';
#elif defined(__APPLE__)
    candidates.push_back("/Applications/Blender.app/Contents/MacOS/Blender");
    const char* exe = "blender";
    const char sep = ':';
#else
    for (const char* p : {"/usr/bin/blender", "/usr/local/bin/blender", "/snap/bin/blender"}) candidates.push_back(p);
    const char* exe = "blender";
    const char sep = ':';
#endif
    if (const char* pathEnv = std::getenv("PATH")) {
        std::string all = pathEnv;
        size_t start = 0;
        while (start <= all.size()) {
            const size_t end = all.find(sep, start);
            const std::string dir = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!dir.empty()) candidates.insert(candidates.begin(), fs::path(dir) / exe);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    std::error_code ec;
    for (const fs::path& c : candidates)
        if (fs::is_regular_file(c, ec)) return c.string();
    return {};
}

namespace {

bool convertBlend(const std::string& src, const std::string& dst, std::string* error) {
#if defined(__ANDROID__)
    (void)src;
    (void)dst;
    return fail(error, "los .blend solo se pueden importar en el ordenador (hace falta Blender instalado)");
#else
    const std::string blender = findBlender();
    if (blender.empty())
        return fail(error, "para importar .blend hace falta Blender instalado (blender.org). Si está en otra carpeta, pon su ruta "
                           "en la variable de entorno TITAN_BLENDER. También puedes exportar desde Blender a glTF (.glb).");
    // Las rutas van entre comillas en una orden del sistema: se rechazan caracteres que podrían romperla.
    for (const std::string* s : {&src, &dst, &blender})
        if (s->find_first_of("\"`$%\n\r") != std::string::npos)
            return fail(error, "la ruta contiene caracteres no admitidos (comillas, $, %, `): renombra el archivo");
    // Blender en segundo plano exporta a glTF binario con animaciones y modificadores aplicados.
    auto quote = [](const std::string& s) { return "\"" + s + "\""; };
    std::string out = fs::absolute(dst).generic_string();
    const std::string script = "import bpy; bpy.ops.export_scene.gltf(filepath=r'" + out +
                               "', export_format='GLB', export_animations=True, export_apply=True)";
    std::string cmd = quote(blender) + " --background " + quote(fs::absolute(src).string()) + " --python-expr " + quote(script);
#if defined(_WIN32)
    cmd = "\"" + cmd + "\""; // cmd.exe quita las comillas exteriores
#endif
    std::error_code ec;
    fs::remove(dst, ec);
    const int rc = std::system(cmd.c_str());
    if (rc != 0 || !fs::exists(dst)) return fail(error, "Blender no pudo exportar el archivo (código " + std::to_string(rc) + ")");
    return true;
#endif
}

} // namespace

bool convertModelToGlb(const std::string& src, const std::string& dst, ImportReport* report, std::string* error) {
    const std::string e = lowerExt(src);
    if (e == ".blend") return convertBlend(src, dst, error);
    MeshBuild mesh;
    bool ok = false;
    if (e == ".obj") ok = loadObj(src, mesh, report, error);
    else if (e == ".stl") ok = loadStl(src, mesh, report, error);
    else if (e == ".ply") ok = loadPly(src, mesh, report, error);
    else return fail(error, "formato no admitido: " + e);
    if (!ok) return false;
    std::vector<uint8_t> glb;
    if (!writeGlb(mesh, glb, error)) return false;
    std::string err;
    if (!fileio::writeAtomic(dst, std::string(glb.begin(), glb.end()), false, &err)) return fail(error, err);
    return true;
}

} // namespace titan
