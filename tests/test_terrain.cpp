#include <algorithm>

#include "Test.h"
#include "titan/core/Serialization.h"
#include "titan/core/TerrainTools.h"
#include "titan/physics/PhysicsWorld.h"

using namespace titan;

TEST("terreno: base64 ida y vuelta") {
    const uint8_t data[] = {0, 1, 2, 250, 251, 252, 253};
    for (size_t n = 0; n <= sizeof(data); ++n) {
        std::vector<uint8_t> back;
        CHECK(base64Decode(base64Encode(data, n), back));
        CHECK_EQ(back.size(), n);
        CHECK(std::equal(back.begin(), back.end(), data));
    }
    std::vector<uint8_t> out;
    CHECK(!base64Decode("abc", out));   // longitud incorrecta
    CHECK(!base64Decode("ab$=", out));  // carácter no válido
}

TEST("terreno: esculpir sube, baja, aplana y suaviza con caída suave") {
    Terrain t;
    t.size = 32;
    t.resolution = 33; // un vértice por metro
    terrainEnsureData(t);
    CHECK_NEAR(terrainHeightAt(t, 0, 0), 0, 1e-6);
    const uint32_t rev = t.revision;
    sculptTerrain(t, SculptTool::Raise, 0, 0, 4, 2, 1.0f); // 2 m en el centro
    CHECK(t.revision != rev);
    const float center = terrainHeightAt(t, 0, 0), side = terrainHeightAt(t, 3, 0), far = terrainHeightAt(t, 8, 0);
    CHECK_NEAR(center, 2, 0.01);
    CHECK(side > 0 && side < center); // la caída es suave
    CHECK_NEAR(far, 0, 1e-6);         // fuera del pincel no cambia
    sculptTerrain(t, SculptTool::Lower, 0, 0, 4, 1, 1.0f);
    CHECK_NEAR(terrainHeightAt(t, 0, 0), 1, 0.01);
    sculptTerrain(t, SculptTool::Flatten, 0, 0, 6, 1, 100.0f, 0.5f); // todo el pincel a 0,5 m
    CHECK_NEAR(terrainHeightAt(t, 0, 0), 0.5, 0.01);
    sculptTerrain(t, SculptTool::Raise, 10, 10, 1, 5, 1.0f); // un pico
    const float peak = terrainHeightAt(t, 10, 10);
    sculptTerrain(t, SculptTool::Smooth, 10, 10, 3, 1, 1.0f);
    CHECK(terrainHeightAt(t, 10, 10) < peak);
    sculptTerrain(t, SculptTool::Lower, 0, 0, 4, 100, 1.0f); // no baja de 0
    CHECK(terrainHeightAt(t, 0, 0) >= 0);
}

TEST("terreno: pintura de capas, rayo, montañas, mapa de alturas y cambio de resolución") {
    Terrain t;
    t.resolution = 65;
    terrainEnsureData(t);
    sculptTerrain(t, SculptTool::Paint, 0, 0, 5, 1, 10.0f, 0, 2);
    const size_t mid = (size_t(32) * 65 + 32) * 4;
    CHECK(t.splat[mid + 2] > 240 && t.splat[mid] < 15);
    int sum = 0;
    for (int k = 0; k < 4; ++k) sum += t.splat[mid + k];
    CHECK(sum >= 250 && sum <= 260); // los pesos siguen sumando ~1

    generateTerrain(t, 42, 0.5f);
    Terrain t2;
    t2.resolution = 65;
    generateTerrain(t2, 42, 0.5f);
    CHECK(t.heights == t2.heights); // misma semilla, mismo relieve
    Vec3 hit;
    CHECK(raycastTerrain(t, {3, 100, -2}, {0, -1, 0}, 500, hit));
    CHECK_NEAR(hit.y, terrainHeightAt(t, 3, -2), 0.05);
    CHECK(!raycastTerrain(t, {3, 100, -2}, {0, 1, 0}, 500, hit)); // hacia arriba no toca

    std::vector<float> gray = {0, 1, 0, 1}; // 2x2: izquierda baja, derecha alta
    importHeightmap(t, gray, 2, 2);
    CHECK_NEAR(terrainHeightAt(t, -t.size * 0.5f, 0), 0, 0.01);
    CHECK_NEAR(terrainHeightAt(t, t.size * 0.5f, 0), t.maxHeight, 0.01);
    const float before = terrainHeightAt(t, 5, 5);
    resizeTerrain(t, 129);
    CHECK_EQ(t.resolution, 129);
    CHECK_NEAR(terrainHeightAt(t, 5, 5), before, 0.05);
    const TerrainMesh m = buildTerrainMesh(t);
    CHECK_EQ(m.vertices.size(), size_t(129) * 129);
    CHECK_EQ(m.indices.size(), size_t(128) * 128 * 6);
}

TEST("terreno: se guarda con la escena y tiene colisión") {
    ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene a("a");
    Entity e = a.createEntity("Terreno");
    Terrain t;
    t.resolution = 33;
    t.size = 32;
    terrainEnsureData(t);
    sculptTerrain(t, SculptTool::Raise, 0, 0, 8, 3, 1.0f);
    sculptTerrain(t, SculptTool::Paint, 5, 5, 3, 1, 10.0f, 0, 1);
    t.layerColors[2] = {0.1f, 0.2f, 0.3f, 1};
    a.world().add<Terrain>(e, t);
    Scene b("b");
    CHECK(deserializeScene(serializeScene(a, reg), b, reg).ok());
    const Terrain* tb = b.world().get<Terrain>(b.findByName("Terreno"));
    CHECK(tb != nullptr);
    if (!tb) return;
    CHECK(tb->heights == t.heights && tb->splat == t.splat);
    CHECK_NEAR(tb->layerColors[2].b, 0.3, 1e-5);

    Entity box = b.createEntity("Caja"); // cae sobre la colina del centro
    b.transform(box).setPosition({0, 10, 0});
    b.world().add<Collider>(box);
    b.world().add<Rigidbody>(box);
    PhysicsWorld w;
    w.build(b);
    for (int i = 0; i < 180; ++i) w.step(b, 1.0f / 60);
    CHECK_NEAR(b.transform(box).position().y, terrainHeightAt(*tb, 0, 0) + 0.5f, 0.15);
}
