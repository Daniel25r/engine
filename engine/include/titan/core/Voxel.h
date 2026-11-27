#pragma once
// Mundo de bloques (voxels) para juegos sandbox y de construcción.
//   - Columnas ("chunks") de 16 × 64 × 16 bloques; un bloque ocupa 1 byte (hasta 255 tipos + aire).
//   - Generación procedural reproducible por semilla: relieve por ruido, biomas por temperatura y humedad, cuevas,
//     playas, océanos y árboles.
//   - Mallas por chunk con caras ocultas eliminadas y fusión de caras iguales (greedy meshing): un chunk de terreno
//     son cientos de triángulos, no miles de cubos.
//   - Rayo por la rejilla (DDA) para elegir el bloque que se rompe o junto al que se coloca.
//   - Guardado incremental: solo los chunks modificados, comprimidos por series (RLE), con escritura atómica.
// Es independiente del render y de la física: el Runtime (VoxelSystem) lo conecta con ellos y con el streaming.
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "titan/core/MeshBuild.h"

namespace titan {

constexpr int kChunkSize = 16;   // X y Z
constexpr int kChunkHeight = 64; // Y
constexpr int kChunkVolume = kChunkSize * kChunkHeight * kChunkSize;

using BlockId = uint8_t;
constexpr BlockId kAir = 0;

struct BlockType {
    std::string name;
    Color top, side;   // color de la cara de arriba y de los lados/abajo
    bool solid = true; // choca y oculta las caras vecinas
};

// Tipos de bloque (0 = aire). Los juegos pueden añadir los suyos con add().
class BlockRegistry {
public:
    BlockRegistry(); // tipos por defecto: hierba, tierra, piedra, arena, nieve, agua, madera, hojas, roca volcánica...
    BlockId add(const BlockType& t);
    const BlockType& get(BlockId id) const { return types_[id < types_.size() ? id : 0]; }
    BlockId find(const std::string& name) const; // 0 si no existe
    size_t size() const { return types_.size(); }

private:
    std::vector<BlockType> types_;
};

enum class Biome { Ocean, Beach, Plains, Forest, Desert, Mountains, Snow, Volcanic };
const char* biomeName(Biome b);

struct VoxelChunk {
    int cx = 0, cz = 0;
    std::vector<BlockId> blocks = std::vector<BlockId>(kChunkVolume, kAir);
    bool modified = false; // cambiado por el jugador: hay que guardarlo
    static int index(int x, int y, int z) { return (y * kChunkSize + z) * kChunkSize + x; }
    BlockId get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockId b);
};

struct VoxelGenSettings {
    uint32_t seed = 1337;
    int seaLevel = 20;
    float terrainScale = 1.0f; // >1 montañas más altas
    bool caves = true, trees = true;
};

// Ruido de gradiente 2D/3D (tipo Perlin) determinista por semilla, y su suma en octavas.
float noise2(uint32_t seed, float x, float z);
float noise3(uint32_t seed, float x, float y, float z);
float fbm2(uint32_t seed, float x, float z, int octaves);

Biome biomeAt(const VoxelGenSettings& s, int wx, int wz);
// Genera el chunk completo (no depende de los vecinos: se puede hacer en paralelo).
void generateChunk(const VoxelGenSettings& s, const BlockRegistry& reg, VoxelChunk& c);

// Bloques de un chunk con un borde de 1 bloque de sus vecinos (para saber qué caras se ven en los bordes).
struct PaddedChunk {
    int cx = 0, cz = 0;
    std::vector<BlockId> b = std::vector<BlockId>(size_t(kChunkSize + 2) * (kChunkHeight + 2) * (kChunkSize + 2), kAir);
    static int index(int x, int y, int z) { // x, z en -1..16; y en -1..64
        return ((y + 1) * (kChunkSize + 2) + (z + 1)) * (kChunkSize + 2) + (x + 1);
    }
};
// Malla del chunk en coordenadas del mundo: una parte por tipo de bloque (su color como material).
// solidOnly: solo los bloques sólidos (malla de colisión: el agua y la lava no frenan).
MeshBuild meshChunk(const PaddedChunk& p, const BlockRegistry& reg, bool solidOnly = false);

// Datos del mundo cargado: chunks en memoria, acceso por coordenadas de bloque del mundo.
class VoxelWorld {
public:
    VoxelGenSettings gen;
    BlockRegistry blocks;

    VoxelChunk* chunk(int cx, int cz);
    const VoxelChunk* chunk(int cx, int cz) const;
    VoxelChunk& ensureChunk(int cx, int cz); // la genera (o la lee del disco) si no está
    void insert(std::unique_ptr<VoxelChunk> c);
    std::unique_ptr<VoxelChunk> remove(int cx, int cz);
    size_t loadedChunks() const { return chunks_.size(); }
    std::vector<std::pair<int, int>> chunkCoords() const;

    BlockId get(int x, int y, int z) const; // aire si el chunk no está cargado o fuera de altura
    // Pone un bloque (el chunk debe estar cargado). Marca el chunk como modificado. false si no se pudo.
    bool set(int x, int y, int z, BlockId b);
    PaddedChunk padded(int cx, int cz) const;
    int surfaceHeight(int x, int z) const; // y del bloque sólido más alto, -1 si no hay

    // Rayo por la rejilla: primer bloque sólido. normal = cara por la que entra (para colocar al lado).
    struct RayHit {
        int x = 0, y = 0, z = 0;
        int nx = 0, ny = 0, nz = 0;
        BlockId block = kAir;
        float distance = 0;
    };
    bool raycast(Vec3 origin, Vec3 dir, float maxDistance, RayHit& hit) const;

    // ---- Persistencia (carpeta del mundo): world.json + chunks/<cx>_<cz>.chunk solo de los modificados.
    std::string folder; // vacío = no se guarda
    bool saveSettings(std::string* error = nullptr) const;
    bool loadSettings(std::string* error = nullptr); // false si no existe: mundo nuevo
    bool saveChunk(const VoxelChunk& c, std::string* error = nullptr) const;
    bool loadChunk(int cx, int cz, VoxelChunk& out) const; // false si no hay archivo (se genera)
    int saveModified(); // guarda los chunks modificados; devuelve cuántos

private:
    std::unordered_map<int64_t, std::unique_ptr<VoxelChunk>> chunks_;
    static int64_t key(int cx, int cz) { return (int64_t(cx) << 32) ^ int64_t(uint32_t(cz)); }
};

// Compresión por series (RLE) de los bloques de un chunk: el terreno tiene largas series iguales.
std::vector<uint8_t> encodeChunk(const VoxelChunk& c);
bool decodeChunk(const std::vector<uint8_t>& data, VoxelChunk& c);

} // namespace titan
