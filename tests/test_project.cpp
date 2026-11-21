#include <filesystem>
#include <fstream>

#include "Test.h"
#include "titan/core/FileIO.h"
#include "titan/core/Project.h"

using namespace titan;
namespace fs = std::filesystem;

namespace {
void writeFile(const std::string& path, const std::string& data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}
} // namespace

TEST("proyecto: crear, guardar escena, cerrar y volver a abrir (demostración técnica)") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const std::string dir = titan_test::tempDir("proyecto_demo");
    UUID texId, sceneId, shipId;
    {
        auto p = Project::create(dir, "Demo", ProjectDimension::TwoD);
        CHECK(p != nullptr);
        writeFile(p->assetsDir() + "/textures/nave.png", "PNG falso para la prueba");
        texId = p->assets().import("textures/nave.png");
        CHECK(!texId.isNull());

        Scene s("Principal");
        Entity root = s.createEntity("Mundo");
        Entity cam = s.createEntity("Cámara", root);
        s.world().add<Camera>(cam);
        Entity ship = s.createEntity("Nave", root);
        s.transform(ship).setPosition({3, 4, 0});
        SpriteRenderer sp;
        sp.texture = texId;
        s.world().add<SpriteRenderer>(ship, sp);
        s.createEntity("Escudo", ship);
        shipId = s.info(ship).id;

        sceneId = p->saveScene(s, reg, "scenes/principal.scene");
        CHECK(!sceneId.isNull());
        p->setStartScene(sceneId);
        CHECK(p->save());
    } // el proyecto se cierra aquí

    auto p = Project::open(dir);
    CHECK(p != nullptr);
    CHECK_EQ(p->name(), std::string("Demo"));
    CHECK(p->startScene() == sceneId);
    CHECK(p->assets().find(texId) != nullptr);
    CHECK(p->assets().find(texId)->type == AssetType::Texture);
    Scene s;
    LoadReport rep = p->loadScene(p->startScene(), s, reg);
    CHECK(rep.ok());
    CHECK_EQ(s.entityCount(), 4u);
    Entity ship = s.findById(shipId);
    CHECK(!ship.isNull());
    CHECK_VEC3(s.transform(ship).position(), 3, 4, 0);
    CHECK(s.world().get<SpriteRenderer>(ship)->texture == texId);
    CHECK_EQ(s.children(ship).size(), 1u);
}

TEST("recursos: los ids sobreviven a mover un archivo con su .meta") {
    const std::string dir = titan_test::tempDir("proyecto_mover");
    auto p = Project::create(dir, "Mover", ProjectDimension::TwoD);
    writeFile(p->assetsDir() + "/a.png", "x");
    const UUID id = p->assets().import("a.png");
    fs::create_directories(p->assetsDir() + "/sub");
    fs::rename(p->assetsDir() + "/a.png", p->assetsDir() + "/sub/b.png");
    fs::rename(p->assetsDir() + "/a.png.meta", p->assetsDir() + "/sub/b.png.meta");
    p->assets().refresh();
    CHECK(p->assets().find(id) != nullptr);
    CHECK_EQ(p->assets().find(id)->path, std::string("sub/b.png"));
}

TEST("recursos: un archivo copiado con su .meta recibe un id nuevo") {
    const std::string dir = titan_test::tempDir("proyecto_dup");
    auto p = Project::create(dir, "Dup", ProjectDimension::TwoD);
    writeFile(p->assetsDir() + "/a.png", "x");
    p->assets().import("a.png");
    fs::copy_file(p->assetsDir() + "/a.png", p->assetsDir() + "/c.png");
    fs::copy_file(p->assetsDir() + "/a.png.meta", p->assetsDir() + "/c.png.meta");
    const ScanReport rep = p->assets().refresh();
    CHECK_EQ(p->assets().size(), 2u);
    CHECK_EQ(rep.warnings.size(), 1u);
    CHECK(p->assets().findByPath("a.png")->id != p->assets().findByPath("c.png")->id);
}

TEST("paquete: detecta referencias rotas y genera el índice si todo es correcto") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const std::string dir = titan_test::tempDir("proyecto_paquete");
    auto p = Project::create(dir, "Paquete", ProjectDimension::TwoD);
    Scene s("S");
    Entity e = s.createEntity("Sprite");
    SpriteRenderer sp;
    sp.texture = UUID::generate(); // no existe
    s.world().add<SpriteRenderer>(e, sp);
    p->setStartScene(p->saveScene(s, reg, "s.scene"));

    std::vector<std::string> errors, warnings;
    CHECK(!p->package(dir + "/out", reg, errors, warnings));
    CHECK_EQ(errors.size(), 1u);

    s.world().get<SpriteRenderer>(e)->texture = {};
    p->saveScene(s, reg, "s.scene");
    errors.clear();
    CHECK(p->package(dir + "/out", reg, errors, warnings));
    CHECK(fs::exists(dir + "/out/asset_index.json"));
    CHECK(fs::exists(dir + "/out/project.titan"));
    CHECK(fs::exists(dir + "/out/assets/s.scene"));
    CHECK(!fs::exists(dir + "/out/assets/s.scene.meta"));

    std::string index;
    fileio::readText(dir + "/out/asset_index.json", index);
    AssetDatabase db;
    CHECK(db.loadIndex(index));
    CHECK(db.find(p->startScene()) != nullptr);
}

TEST("proyecto: se rechaza un proyecto de una versión futura sin tocarlo") {
    const std::string dir = titan_test::tempDir("proyecto_futuro");
    const std::string text = R"({"format":"titan.project","version":7,"name":"Futuro"})";
    writeFile(dir + "/project.titan", text);
    std::string err;
    CHECK(Project::open(dir, &err) == nullptr);
    CHECK(err.find("más nueva") != std::string::npos);
    std::string after;
    fileio::readText(dir + "/project.titan", after);
    CHECK(after == text);
}
