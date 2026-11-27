// Mundo de bloques: cada prueba comprueba una propiedad medible (reproducibilidad, caras, rayo, guardado).
#include <filesystem>
#include <set>

#include "Test.h"
#include "titan/core/Voxel.h"

using namespace titan;
namespace fs = std::filesystem;

namespace {
size_t quads(const MeshBuild& m) { return m.triangleCount() / 2; }
}

TEST("voxel: la misma semilla genera el mismo mundo y otra semilla otro") {
    BlockRegistry reg;
    VoxelGenSettings s;
    VoxelChunk a, b, c;
    a.cx = b.cx = c.cx = 3;
    a.cz = b.cz = c.cz = -2;
    generateChunk(s, reg, a);
    generateChunk(s, reg, b);
    CHECK(a.blocks == b.blocks);
    s.seed = 999;
    generateChunk(s, reg, c);
    CHECK(a.blocks != c.blocks);
    // El terreno tiene suelo y aire: no es un bloque lleno ni vacío.
    size_t solid = 0;
    for (BlockId x : a.blocks) solid += reg.get(x).solid ? 1 : 0;
    CHECK(solid > size_t(kChunkVolume) / 8 && solid < size_t(kChunkVolume) * 7 / 8);
}

TEST("voxel: biomas variados en una zona grande del mundo") {
    VoxelGenSettings s;
    std::set<int> found;
    for (int z = -2000; z <= 2000; z += 40)
        for (int x = -2000; x <= 2000; x += 40) found.insert(int(biomeAt(s, x, z)));
    CHECK(found.size() >= 5); // océano, playa, llanura, bosque, desierto, montaña, nieve, volcánico...
}

TEST("voxel: malla con caras ocultas eliminadas y caras iguales fusionadas") {
    BlockRegistry reg;
    VoxelWorld w;
    VoxelChunk& c = w.ensureChunk(0, 0);
    std::fill(c.blocks.begin(), c.blocks.end(), kAir);
    // Un solo bloque: 6 caras.
    c.set(5, 5, 5, reg.find("Piedra"));
    CHECK_EQ(quads(meshChunk(w.padded(0, 0), w.blocks)), 6u);
    // Dos bloques iguales juntos: la cara compartida no se dibuja y las demás se fusionan (6 cuadriláteros, no 10).
    c.set(6, 5, 5, reg.find("Piedra"));
    CHECK_EQ(quads(meshChunk(w.padded(0, 0), w.blocks)), 6u);
    // Una losa de 16 × 16: 6 cuadriláteros gracias a la fusión (sin ella serían 16·16·2 + 16·4 = 576).
    std::fill(c.blocks.begin(), c.blocks.end(), kAir);
    for (int z = 0; z < kChunkSize; ++z)
        for (int x = 0; x < kChunkSize; ++x) c.set(x, 0, z, reg.find("Piedra"));
    CHECK_EQ(quads(meshChunk(w.padded(0, 0), w.blocks)), 6u);
    // Un chunk de terreno generado: fusionar reduce mucho respecto a una cara por bloque visible.
    VoxelWorld g;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) g.ensureChunk(dx, dz);
    const MeshBuild terrain = meshChunk(g.padded(0, 0), g.blocks);
    CHECK(quads(terrain) > 50u);
    CHECK(quads(terrain) < 6000u);
    // Las caras en el borde con un chunk vecino lleno no se dibujan (la malla mira a los vecinos).
    VoxelWorld two;
    VoxelChunk& l = two.ensureChunk(0, 0);
    VoxelChunk& r = two.ensureChunk(1, 0);
    std::fill(l.blocks.begin(), l.blocks.end(), kAir);
    std::fill(r.blocks.begin(), r.blocks.end(), kAir);
    l.set(15, 0, 0, 3);
    r.set(0, 0, 0, 3);
    CHECK_EQ(quads(meshChunk(two.padded(0, 0), two.blocks)), 5u); // la cara +X toca al vecino
}

TEST("voxel: rayo por la rejilla, colocar y romper bloques") {
    VoxelWorld w;
    VoxelChunk& c = w.ensureChunk(0, 0);
    std::fill(c.blocks.begin(), c.blocks.end(), kAir);
    CHECK(w.set(4, 10, 4, w.blocks.find("Ladrillo")));
    VoxelWorld::RayHit hit;
    CHECK(w.raycast({4.5f, 20, 4.5f}, {0, -1, 0}, 30, hit));
    CHECK(hit.x == 4 && hit.y == 10 && hit.z == 4);
    CHECK(hit.ny == 1); // entra por la cara de arriba
    CHECK_NEAR(hit.distance, 9.0f, 1e-4f);
    // Colocar al lado de la cara: (4, 11, 4).
    CHECK(w.set(hit.x + hit.nx, hit.y + hit.ny, hit.z + hit.nz, w.blocks.find("Tablones")));
    CHECK_EQ(int(w.get(4, 11, 4)), int(w.blocks.find("Tablones")));
    // Romper.
    CHECK(w.set(4, 11, 4, kAir));
    CHECK(w.raycast({0.5f, 10.5f, 4.5f}, {1, 0, 0}, 30, hit));
    CHECK(hit.x == 4 && hit.nx == -1);
    CHECK(!w.raycast({0.5f, 30.5f, 0.5f}, {1, 0, 0}, 10, hit)); // nada en ese camino
    CHECK(!w.set(4, 200, 4, 3));                                 // fuera de altura
    CHECK(c.modified);
    // Coordenadas negativas: chunk -1.
    w.ensureChunk(-1, -1);
    CHECK(w.set(-1, 5, -1, 3));
    CHECK_EQ(int(w.get(-1, 5, -1)), 3);
    CHECK_EQ(int(w.chunk(-1, -1)->get(15, 5, 15)), 3);
}

TEST("voxel: guardado incremental, RLE y archivos dañados") {
    const std::string dir = (fs::temp_directory_path() / "titan_voxel_mundo").string();
    fs::remove_all(dir);
    {
        VoxelWorld w;
        w.folder = dir;
        w.gen.seed = 4242;
        CHECK(w.saveSettings());
        w.ensureChunk(0, 0);
        w.ensureChunk(1, 0);
        CHECK(w.set(3, 40, 3, w.blocks.find("Ladrillo")));
        CHECK_EQ(w.saveModified(), 1); // solo el chunk tocado
        CHECK_EQ(w.saveModified(), 0); // ya estaba guardado
    }
    {
        VoxelWorld w;
        w.folder = dir;
        CHECK(w.loadSettings());
        CHECK_EQ(w.gen.seed, 4242u);
        w.ensureChunk(0, 0); // se lee del disco, no se regenera
        CHECK_EQ(int(w.get(3, 40, 3)), int(w.blocks.find("Ladrillo")));
        w.ensureChunk(1, 0); // sin archivo: se genera igual que la primera vez
        CHECK(w.chunk(1, 0) != nullptr);
    }
    VoxelChunk c;
    generateChunk(VoxelGenSettings{}, BlockRegistry{}, c);
    const std::vector<uint8_t> enc = encodeChunk(c);
    CHECK(enc.size() < size_t(kChunkVolume) / 4); // el terreno se comprime mucho
    VoxelChunk back;
    CHECK(decodeChunk(enc, back));
    CHECK(back.blocks == c.blocks);
    std::vector<uint8_t> broken(enc.begin(), enc.begin() + long(enc.size() / 2));
    CHECK(!decodeChunk(broken, back)); // dañado: se rechaza (se regenerará) en vez de cargar basura
}
