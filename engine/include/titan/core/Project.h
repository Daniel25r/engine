#pragma once
// Proyecto de juego en disco y su base de datos de recursos.
//
//   MiJuego/
//     project.titan          configuración del proyecto (JSON versionado)
//     assets/                todo lo que usa el juego: escenas, texturas, scripts...
//       nivel1.scene
//       nivel1.scene.meta    id persistente + tipo + ajustes de importación de cada recurso
//
// Las referencias entre archivos usan el id del .meta, no la ruta: mover o renombrar un
// archivo junto con su .meta no rompe nada.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/Serialization.h"
#include "titan/core/Uuid.h"

namespace titan {

enum class AssetType { Unknown, Scene, Texture, Script, Audio, Model, Font, Data };

const char* assetTypeName(AssetType type);
AssetType assetTypeFromName(const std::string& name);
AssetType assetTypeFromExtension(const std::string& path);

struct AssetRecord {
    UUID id;
    AssetType type = AssetType::Unknown;
    std::string path; // relativa a la carpeta assets/, con '/'
    Json importSettings = Json::object();
};

struct ScanReport {
    std::vector<std::string> imported; // recursos nuevos que recibieron un .meta
    std::vector<std::string> warnings;
};

class AssetDatabase {
public:
    // Modo editor: trabaja sobre una carpeta assets/ real.
    void setRoot(const std::string& assetsDir) { root_ = assetsDir; }
    const std::string& root() const { return root_; }
    ScanReport refresh();
    // Importa un archivo que ya está dentro de assets/ (crea su .meta si no tiene).
    UUID import(const std::string& relativePath, std::string* error = nullptr);
    bool saveMeta(const AssetRecord& record, std::string* error = nullptr);

    // Modo runtime: carga el índice generado al empaquetar (no hay .meta en el paquete).
    bool loadIndex(const std::string& text, std::string* error = nullptr);
    std::string buildIndex() const;

    const AssetRecord* find(UUID id) const;
    const AssetRecord* findByPath(const std::string& relativePath) const;
    std::vector<const AssetRecord*> all() const;
    size_t size() const { return records_.size(); }
    std::string absolutePath(const AssetRecord& r) const { return root_ + "/" + r.path; }

private:
    std::string root_;
    std::unordered_map<UUID, AssetRecord> records_;
};

enum class ProjectDimension { TwoD, ThreeD };

constexpr int kProjectFormatVersion = 1;

class Project {
public:
    static std::unique_ptr<Project> create(const std::string& dir, const std::string& name, ProjectDimension dim,
                                           std::string* error = nullptr);
    static std::unique_ptr<Project> open(const std::string& dir, std::string* error = nullptr);

    bool save(std::string* error = nullptr);

    const std::string& root() const { return root_; }
    std::string assetsDir() const { return root_ + "/assets"; }
    const std::string& name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }
    ProjectDimension dimension() const { return dimension_; }
    UUID startScene() const { return startScene_; }
    void setStartScene(UUID id) { startScene_ = id; }
    AssetDatabase& assets() { return assets_; }

    // Guarda la escena en assets/<relativePath> y la registra como recurso.
    UUID saveScene(Scene& scene, const ComponentRegistry& registry, const std::string& relativePath,
                   std::string* error = nullptr);
    LoadReport loadScene(UUID id, Scene& scene, const ComponentRegistry& registry);

    // Prepara un paquete de juego ejecutable por el runtime: valida escenas y referencias,
    // copia los recursos y genera asset_index.json. Devuelve false si hay errores.
    bool package(const std::string& outDir, const ComponentRegistry& registry, std::vector<std::string>& errors,
                 std::vector<std::string>& warnings);

    // Serialización de project.titan (la usa también el runtime para leer el paquete).
    static bool parseProjectFile(const std::string& text, std::string& name, ProjectDimension& dim, UUID& startScene,
                                 std::string* error);

private:
    std::string toJsonText() const;

    std::string root_;
    std::string name_;
    ProjectDimension dimension_ = ProjectDimension::TwoD;
    UUID startScene_;
    AssetDatabase assets_;
};

} // namespace titan
