#include "titan/core/Project.h"

#include <algorithm>
#include <filesystem>

#include "titan/core/FileIO.h"
#include "titan/core/Log.h"
#include "titan/core/Version.h"

namespace fs = std::filesystem;

namespace titan {

// ---------------------------------------------------------------- Tipos de recurso

const char* assetTypeName(AssetType t) {
    switch (t) {
    case AssetType::Scene: return "scene";
    case AssetType::Texture: return "texture";
    case AssetType::Script: return "script";
    case AssetType::Audio: return "audio";
    case AssetType::Model: return "model";
    case AssetType::Font: return "font";
    case AssetType::Data: return "data";
    default: return "unknown";
    }
}

AssetType assetTypeFromName(const std::string& n) {
    for (AssetType t : {AssetType::Scene, AssetType::Texture, AssetType::Script, AssetType::Audio, AssetType::Model,
                        AssetType::Font, AssetType::Data})
        if (n == assetTypeName(t)) return t;
    return AssetType::Unknown;
}

AssetType assetTypeFromExtension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".scene") return AssetType::Scene;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return AssetType::Texture;
    if (ext == ".lua") return AssetType::Script;
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return AssetType::Audio;
    if (ext == ".gltf" || ext == ".glb" || ext == ".obj") return AssetType::Model;
    if (ext == ".ttf" || ext == ".otf") return AssetType::Font;
    if (ext == ".json") return AssetType::Data;
    return AssetType::Unknown;
}

namespace {

Json defaultImportSettings(AssetType t) {
    if (t == AssetType::Texture) return Json{{"filter", "linear"}, {"mipmaps", true}};
    return Json::object();
}

bool isInternalFile(const fs::path& p) {
    const std::string ext = p.extension().string();
    const std::string name = p.filename().string();
    return ext == ".meta" || ext == ".tmp" || ext == ".bak" || (!name.empty() && name[0] == '.');
}

std::string toRelative(const fs::path& file, const fs::path& root) { return fs::relative(file, root).generic_string(); }

} // namespace

// ---------------------------------------------------------------- AssetDatabase

bool AssetDatabase::saveMeta(const AssetRecord& r, std::string* error) {
    Json meta{{"format", "titan.meta"},
              {"version", 1},
              {"id", r.id.toString()},
              {"type", assetTypeName(r.type)},
              {"importSettings", r.importSettings}};
    return fileio::writeAtomic(root_ + "/" + r.path + ".meta", meta.dump(2), false, error);
}

UUID AssetDatabase::import(const std::string& relativePath, std::string* error) {
    if (const AssetRecord* existing = findByPath(relativePath)) return existing->id;
    if (!fs::exists(root_ + "/" + relativePath)) {
        if (error) *error = "no existe assets/" + relativePath;
        return {};
    }
    AssetRecord r;
    do r.id = UUID::generate();
    while (records_.count(r.id));
    r.path = relativePath;
    r.type = assetTypeFromExtension(relativePath);
    r.importSettings = defaultImportSettings(r.type);
    if (!saveMeta(r, error)) return {};
    records_[r.id] = r;
    TITAN_INFO("assets", "Importado %s (%s, %s)", r.path.c_str(), assetTypeName(r.type), r.id.toString().c_str());
    return r.id;
}

ScanReport AssetDatabase::refresh() {
    ScanReport report;
    records_.clear();
    std::error_code ec;
    if (!fs::exists(root_, ec)) {
        report.warnings.push_back("no existe la carpeta de recursos " + root_);
        return report;
    }
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(root_, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file() && !isInternalFile(it->path())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end()); // orden estable: el mismo resultado en cada escaneo

    for (const fs::path& file : files) {
        const std::string rel = toRelative(file, root_);
        const std::string metaPath = file.string() + ".meta";
        AssetRecord r;
        r.path = rel;
        bool needsMeta = true;
        std::string text;
        if (fileio::readText(metaPath, text)) {
            Json meta = Json::parse(text, nullptr, false);
            UUID id;
            if (!meta.is_discarded() && meta.is_object() && meta.value("format", "") == "titan.meta" &&
                meta.contains("id") && meta["id"].is_string() && UUID::parse(meta["id"].get<std::string>(), id) && !id.isNull()) {
                r.id = id;
                r.type = assetTypeFromName(meta.value("type", "unknown"));
                if (r.type == AssetType::Unknown) r.type = assetTypeFromExtension(rel);
                if (meta.contains("importSettings") && meta["importSettings"].is_object()) r.importSettings = meta["importSettings"];
                else r.importSettings = defaultImportSettings(r.type);
                needsMeta = false;
            } else {
                // No se borra el .meta dañado: se aparta para poder revisarlo.
                fs::copy_file(metaPath, metaPath + ".bak", fs::copy_options::overwrite_existing, ec);
                report.warnings.push_back(rel + ".meta estaba dañado; se creó uno nuevo (el anterior quedó en .meta.bak)");
            }
        }
        if (!needsMeta && records_.count(r.id)) {
            // Típico al copiar un archivo junto con su .meta: el duplicado recibe un id nuevo.
            report.warnings.push_back(rel + " tenía el mismo id que " + records_[r.id].path + "; se le asignó uno nuevo");
            needsMeta = true;
        }
        if (needsMeta) {
            do r.id = UUID::generate();
            while (records_.count(r.id));
            r.type = assetTypeFromExtension(rel);
            r.importSettings = defaultImportSettings(r.type);
            std::string err;
            if (!saveMeta(r, &err)) report.warnings.push_back(err);
            report.imported.push_back(rel);
        }
        records_[r.id] = r;
    }
    return report;
}

bool AssetDatabase::loadIndex(const std::string& text, std::string* error) {
    records_.clear();
    Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded() || doc.value("format", "") != "titan.assetindex" || !doc.contains("assets") || !doc["assets"].is_array()) {
        if (error) *error = "asset_index.json no válido";
        return false;
    }
    for (const Json& a : doc["assets"]) {
        AssetRecord r;
        if (!a.is_object() || !a.contains("id") || !UUID::parse(a.value("id", ""), r.id)) continue;
        r.path = a.value("path", "");
        r.type = assetTypeFromName(a.value("type", "unknown"));
        if (a.contains("importSettings") && a["importSettings"].is_object()) r.importSettings = a["importSettings"];
        records_[r.id] = r;
    }
    return true;
}

std::string AssetDatabase::buildIndex() const {
    Json list = Json::array();
    for (const AssetRecord* r : all())
        list.push_back({{"id", r->id.toString()}, {"path", r->path}, {"type", assetTypeName(r->type)}, {"importSettings", r->importSettings}});
    return Json{{"format", "titan.assetindex"}, {"version", 1}, {"assets", list}}.dump(2);
}

const AssetRecord* AssetDatabase::find(UUID id) const {
    auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

const AssetRecord* AssetDatabase::findByPath(const std::string& rel) const {
    for (auto& [id, r] : records_)
        if (r.path == rel) return &r;
    return nullptr;
}

std::vector<const AssetRecord*> AssetDatabase::all() const {
    std::vector<const AssetRecord*> out;
    for (auto& [id, r] : records_) out.push_back(&r);
    std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->path < b->path; });
    return out;
}

// ---------------------------------------------------------------- Project

namespace {
const char* kProjectFile = "project.titan";
}

std::string Project::toJsonText() const {
    return Json{{"format", "titan.project"},
                {"version", kProjectFormatVersion},
                {"engineVersion", kEngineVersion},
                {"name", name_},
                {"dimension", dimension_ == ProjectDimension::ThreeD ? "3d" : "2d"},
                {"startScene", assetToJson(startScene_)}}
        .dump(2);
}

bool Project::parseProjectFile(const std::string& text, std::string& name, ProjectDimension& dim, UUID& startScene,
                               std::string* error) {
    Json doc = Json::parse(text, nullptr, false);
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    if (doc.is_discarded() || !doc.is_object()) return fail("project.titan no es JSON válido");
    if (doc.value("format", "") != "titan.project") return fail("project.titan no es un proyecto de Titán");
    const int version = doc.value("version", 0);
    if (version > kProjectFormatVersion)
        return fail("el proyecto es de una versión más nueva del motor (formato " + std::to_string(version) + ")");
    if (version < 1) return fail("versión de proyecto no válida");
    name = doc.value("name", "Sin nombre");
    dim = doc.value("dimension", "2d") == "3d" ? ProjectDimension::ThreeD : ProjectDimension::TwoD;
    startScene = {};
    if (doc.contains("startScene") && doc["startScene"].is_string()) UUID::parse(doc["startScene"].get<std::string>(), startScene);
    return true;
}

std::unique_ptr<Project> Project::create(const std::string& dir, const std::string& name, ProjectDimension dim,
                                         std::string* error) {
    std::error_code ec;
    if (fs::exists(fs::path(dir) / kProjectFile, ec)) {
        if (error) *error = "ya existe un proyecto en " + dir;
        return nullptr;
    }
    fs::create_directories(fs::path(dir) / "assets", ec);
    if (ec) {
        if (error) *error = "no se pudo crear " + dir + ": " + ec.message();
        return nullptr;
    }
    auto p = std::unique_ptr<Project>(new Project());
    p->root_ = fs::absolute(dir).lexically_normal().generic_string();
    p->name_ = name;
    p->dimension_ = dim;
    p->assets_.setRoot(p->assetsDir());
    if (!p->save(error)) return nullptr;
    TITAN_INFO("project", "Proyecto '%s' creado en %s", name.c_str(), p->root_.c_str());
    return p;
}

std::unique_ptr<Project> Project::open(const std::string& dir, std::string* error) {
    std::string text;
    const std::string file = (fs::path(dir) / kProjectFile).string();
    if (!fileio::readText(file, text)) {
        if (error) *error = "no hay ningún proyecto en " + dir;
        return nullptr;
    }
    auto p = std::unique_ptr<Project>(new Project());
    if (!parseProjectFile(text, p->name_, p->dimension_, p->startScene_, error)) return nullptr;
    p->root_ = fs::absolute(dir).lexically_normal().generic_string();
    p->assets_.setRoot(p->assetsDir());
    const ScanReport scan = p->assets_.refresh();
    for (auto& w : scan.warnings) TITAN_WARN("assets", "%s", w.c_str());
    for (auto& i : scan.imported) TITAN_INFO("assets", "Importado %s", i.c_str());
    TITAN_INFO("project", "Proyecto '%s' abierto (%zu recursos)", p->name_.c_str(), p->assets_.size());
    return p;
}

bool Project::save(std::string* error) { return fileio::writeAtomic(root_ + "/" + kProjectFile, toJsonText(), true, error); }

UUID Project::saveScene(Scene& scene, const ComponentRegistry& registry, const std::string& relativePath, std::string* error) {
    if (!saveSceneFile(scene, registry, assetsDir() + "/" + relativePath, error)) return {};
    return assets_.import(relativePath, error);
}

LoadReport Project::loadScene(UUID id, Scene& scene, const ComponentRegistry& registry) {
    const AssetRecord* r = assets_.find(id);
    if (!r || r->type != AssetType::Scene) {
        LoadReport report;
        report.errors.push_back("no existe ninguna escena con id " + id.toString());
        return report;
    }
    return loadSceneFile(assets_.absolutePath(*r), scene, registry);
}

bool Project::package(const std::string& outDir, const ComponentRegistry& registry, std::vector<std::string>& errors,
                      std::vector<std::string>& warnings) {
    // 1. Validación del proyecto.
    const ScanReport scan = assets_.refresh();
    warnings.insert(warnings.end(), scan.warnings.begin(), scan.warnings.end());
    if (startScene_.isNull() || !assets_.find(startScene_)) errors.push_back("el proyecto no tiene escena inicial válida");

    // 2. Resolución de recursos: cada escena debe cargar y sus referencias deben existir.
    for (const AssetRecord* r : assets_.all()) {
        if (r->type != AssetType::Scene) continue;
        Scene scene(r->path);
        const LoadReport rep = loadSceneFile(assets_.absolutePath(*r), scene, registry);
        for (auto& e : rep.errors) errors.push_back(r->path + ": " + e);
        for (auto& w : rep.warnings) warnings.push_back(r->path + ": " + w);
        for (UUID ref : rep.assetRefs)
            if (!assets_.find(ref)) errors.push_back(r->path + ": usa el recurso " + ref.toString() + ", que no existe en el proyecto");
    }
    if (!errors.empty()) return false;

    // 3. Copia de recursos y generación del índice.
    std::error_code ec;
    fs::remove_all(fs::path(outDir) / "assets", ec);
    fs::create_directories(fs::path(outDir) / "assets", ec);
    for (const AssetRecord* r : assets_.all()) {
        const fs::path dst = fs::path(outDir) / "assets" / r->path;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(assets_.absolutePath(*r), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) errors.push_back("no se pudo copiar " + r->path + ": " + ec.message());
    }
    std::string err;
    if (!fileio::writeAtomic(outDir + "/asset_index.json", assets_.buildIndex(), false, &err)) errors.push_back(err);
    if (!fileio::writeAtomic(outDir + "/project.titan", toJsonText(), false, &err)) errors.push_back(err);
    if (errors.empty())
        TITAN_INFO("project", "Paquete de '%s' generado en %s (%zu recursos)", name_.c_str(), outDir.c_str(), assets_.size());
    return errors.empty();
}

} // namespace titan
