#include <filesystem>
#include <fstream>

#include "Test.h"
#include "titan/core/FileIO.h"
#include "titan/core/Serialization.h"

using namespace titan;

namespace {

// La escena de la demostración técnica: raíz, objeto visual, cámara, hijo y un recurso.
void buildDemoScene(Scene& s, AssetRef texture) {
    Entity root = s.createEntity("Mundo");
    Entity cam = s.createEntity("Cámara", root);
    s.transform(cam).setPosition({0, 0, 10});
    Camera c;
    c.orthographicSize = 6;
    c.clearColor = Color::hex(0x223344);
    s.world().add<Camera>(cam, c);

    Entity ship = s.createEntity("Nave", root);
    s.transform(ship).setPosition({1.5f, -2, 0});
    s.transform(ship).setEulerDegrees({0, 0, 30});
    s.transform(ship).setScale({2, 2, 1});
    SpriteRenderer sp;
    sp.texture = texture;
    sp.color = Color::hex(0xF2B54A);
    sp.sortingOrder = 3;
    s.world().add<SpriteRenderer>(ship, sp);

    Entity engine = s.createEntity("Motor", ship);
    s.transform(engine).setPosition({0, -0.6f, 0});
    s.info(engine).active = false;
}

} // namespace

TEST("serialización: guardar y cargar conserva entidades, jerarquía y componentes") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const AssetRef tex = UUID::generate();
    Scene a("Demo");
    buildDemoScene(a, tex);
    const std::string text = serializeScene(a, reg);

    Scene b;
    LoadReport rep = deserializeScene(text, b, reg);
    CHECK(rep.ok());
    CHECK(rep.warnings.empty());
    CHECK_EQ(b.name(), std::string("Demo"));
    CHECK_EQ(b.entityCount(), 4u);

    Entity ship = b.findByName("Nave");
    CHECK(!ship.isNull());
    CHECK(b.info(ship).id == a.info(a.findByName("Nave")).id);
    CHECK(b.info(b.parent(ship)).name == "Mundo");
    CHECK_VEC3(b.transform(ship).position(), 1.5f, -2, 0);
    CHECK_VEC3(b.transform(ship).eulerDegrees(), 0, 0, 30);
    CHECK_VEC3(b.transform(ship).scale(), 2, 2, 1);
    const SpriteRenderer* sp = b.world().get<SpriteRenderer>(ship);
    CHECK(sp && sp->texture == tex && sp->sortingOrder == 3);
    CHECK_EQ(rep.assetRefs.size(), 1u);

    const Camera* cam = b.world().get<Camera>(b.findByName("Cámara"));
    CHECK(cam && cam->orthographicSize == 6.0f);
    CHECK(!b.info(b.findByName("Motor")).active);

    // Volver a serializar da exactamente el mismo texto: nada se pierde ni cambia.
    CHECK(serializeScene(b, reg) == text);
}

TEST("serialización: los componentes desconocidos se conservan") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const std::string text = R"({"format":"titan.scene","version":1,"name":"X","entities":[
        {"id":"00000000000000aa","name":"E","parent":null,
         "components":{"Transform":{"position":[1,2,3]},"Plugin.Vida":{"max":5}}}]})";
    Scene s;
    LoadReport rep = deserializeScene(text, s, reg);
    CHECK(rep.ok());
    CHECK_EQ(rep.warnings.size(), 1u);
    const std::string again = serializeScene(s, reg);
    CHECK(again.find("Plugin.Vida") != std::string::npos);
    CHECK(again.find("\"max\": 5") != std::string::npos);
}

TEST("serialización: datos incorrectos dan avisos, no se cuelga ni inventa") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const std::string text = R"({"format":"titan.scene","version":1,"entities":[
        {"id":"zz","name":"SinId","components":{"Transform":{"position":"arriba","scale":[1,1]}}},
        {"id":"00000000000000bb","name":"Huérfano","parent":"00000000000000cc"},
        42]})";
    Scene s;
    LoadReport rep = deserializeScene(text, s, reg);
    CHECK(rep.ok());
    CHECK_EQ(s.entityCount(), 2u);
    CHECK(rep.warnings.size() >= 4u);
    Entity e = s.findByName("SinId");
    CHECK_VEC3(s.transform(e).position(), 0, 0, 0);
    CHECK_VEC3(s.transform(e).scale(), 1, 1, 1);
    CHECK(s.parent(s.findByName("Huérfano")).isNull());
}

TEST("serialización: se rechazan archivos ajenos, rotos y de versiones futuras") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene s;
    CHECK(!deserializeScene("{ esto no es json", s, reg).ok());
    CHECK(!deserializeScene(R"({"format":"otra.cosa","version":1})", s, reg).ok());
    const LoadReport future = deserializeScene(R"({"format":"titan.scene","version":99,"entities":[]})", s, reg);
    CHECK(!future.ok());
    CHECK(future.errors[0].find("más nueva") != std::string::npos);
}

TEST("archivos: guardado atómico con copia de seguridad y recuperación") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    const std::string dir = titan_test::tempDir("atomic");
    const std::string path = dir + "/nivel.scene";

    Scene v1("v1");
    v1.createEntity("Primera");
    CHECK(saveSceneFile(v1, reg, path));
    Scene v2("v2");
    v2.createEntity("Segunda");
    CHECK(saveSceneFile(v2, reg, path));
    CHECK(std::filesystem::exists(path + ".bak"));
    CHECK(!std::filesystem::exists(path + ".tmp"));

    // Simular un archivo corrompido (p. ej. cierre a mitad de una herramienta externa).
    { std::ofstream(path, std::ios::trunc) << "{\"format\":\"titan.sce"; }
    Scene loaded;
    LoadReport rep = loadSceneFile(path, loaded, reg);
    CHECK(rep.ok());
    CHECK(rep.recoveredFromBackup);
    CHECK(!loaded.findByName("Primera").isNull()); // la .bak era la versión anterior
}
