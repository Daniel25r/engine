#include "Test.h"
#include "titan/core/Serialization.h"

using namespace titan;

TEST("materiales: .mat ida y vuelta con mapas, repetición y recorte") {
    MeshRenderer m;
    m.color = {0.2f, 0.4f, 0.6f, 1};
    m.metallic = 0.9f;
    m.roughness = 0.3f;
    m.normalMap = UUID::generate();
    m.normalStrength = 1.5f;
    m.metallicRoughnessMap = UUID::generate();
    m.tiling = {4, 2};
    m.offset = {0.5f, 0.25f};
    m.alphaClip = true;
    m.alphaCutoff = 0.33f;
    m.doubleSided = true;
    const std::string text = serializeMaterial(m);
    MeshRenderer back;
    const LoadReport rep = parseMaterial(text, back);
    CHECK(rep.ok());
    CHECK(back.normalMap == m.normalMap && back.metallicRoughnessMap == m.metallicRoughnessMap);
    CHECK_NEAR(back.metallic, 0.9, 1e-6);
    CHECK_NEAR(back.normalStrength, 1.5, 1e-6);
    CHECK_NEAR(back.tiling.x, 4, 1e-6);
    CHECK_NEAR(back.offset.y, 0.25, 1e-6);
    CHECK(back.alphaClip && back.doubleSided);
    CHECK_EQ(rep.assetRefs.size(), 2u); // dependencias declaradas: las dos texturas
}

TEST("materiales: archivos ajenos o futuros se rechazan; escenas antiguas cargan con valores por defecto") {
    MeshRenderer m;
    CHECK(!parseMaterial("{\"format\": \"otra.cosa\"}", m).ok());
    CHECK(!parseMaterial("no es json", m).ok());
    CHECK(!parseMaterial("{\"format\": \"titan.material\", \"version\": 99}", m).ok());

    // MeshRenderer guardado por la 0.12 (sin campos de material avanzado).
    const char* old = R"({"format": "titan.scene", "version": 1, "name": "vieja", "entities": [
        {"id": "00000000000000aa", "name": "Caja", "active": true, "parent": null, "components": {
            "Transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
            "MeshRenderer": {"mesh": "cube", "color": [1, 0, 0, 1], "metallic": 0.5}}}]})";
    ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene s("vieja");
    const LoadReport rep = deserializeScene(old, s, reg);
    for (auto& e : rep.errors) std::printf("      %s\n", e.c_str());
    CHECK(rep.ok());
    const MeshRenderer* mr = s.world().get<MeshRenderer>(s.findByName("Caja"));
    CHECK(mr != nullptr);
    if (mr) {
        CHECK_NEAR(mr->metallic, 0.5, 1e-6);
        CHECK(mr->material.isNull() && mr->normalMap.isNull() && !mr->alphaClip);
        CHECK_NEAR(mr->tiling.x, 1, 1e-6);
    }
}

TEST("luces: focos se guardan y cargan") {
    ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene a("a");
    Entity e = a.createEntity("Foco");
    Light l;
    l.type = Light::Type::Spot;
    l.spotAngle = 30;
    l.spotSoftness = 0.5f;
    a.world().add<Light>(e, l);
    Scene b("b");
    CHECK(deserializeScene(serializeScene(a, reg), b, reg).ok());
    const Light* lb = b.world().get<Light>(b.findByName("Foco"));
    CHECK(lb && lb->type == Light::Type::Spot);
    if (lb) CHECK_NEAR(lb->spotAngle, 30, 1e-5);
}
