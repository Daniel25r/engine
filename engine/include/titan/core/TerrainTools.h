#pragma once
// Terreno sin GPU: alturas, normales, malla, rayos, esculpido, pintura de capas, generación y mapas de alturas.
#include <cstdint>
#include <vector>

#include "titan/core/Bounds.h"
#include "titan/core/Components.h"

namespace titan {

struct TerrainVertex {
    Vec3 position, normal;
    Vec2 uv;
    uint8_t splat[4];
};
struct TerrainMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
};

void terrainEnsureData(Terrain& t);                       // rellena alturas/capas que falten (llano, capa 0)
float terrainHeightAt(const Terrain& t, float x, float z); // altura (local) con interpolación bilineal
Vec3 terrainNormalAt(const Terrain& t, float x, float z);
AABB terrainBounds(const Terrain& t);                     // caja local
TerrainMesh buildTerrainMesh(const Terrain& t);
// Rayo en espacio local; devuelve el punto de impacto local.
bool raycastTerrain(const Terrain& t, const Vec3& origin, const Vec3& dir, float maxDistance, Vec3& hit);

enum class SculptTool { Raise, Lower, Smooth, Flatten, Paint };
// Pincel circular con caída suave en (x, z) local. strength: metros por segundo (o fuerza de pintura) × dt.
void sculptTerrain(Terrain& t, SculptTool tool, float x, float z, float radius, float strength, float dt,
                   float flattenHeight = 0, int paintLayer = 0);
// Relieve de montañas y valles (ruido fractal) con semilla; amount 0..1 de la altura máxima.
void generateTerrain(Terrain& t, uint32_t seed, float amount = 0.6f, float scale = 1.0f);
// Mapa de alturas en escala de grises (0..1), de cualquier tamaño: se remuestrea a la resolución del terreno.
void importHeightmap(Terrain& t, const std::vector<float>& gray, int width, int height);
// Cambia la resolución conservando la forma.
void resizeTerrain(Terrain& t, int resolution);

// Briznas de hierba (coordenadas locales del terreno): donde la capa 0 pesa y la pendiente es suave (< ~35°).
// Deterministas (misma posición siempre) y como mucho maxBlades.
struct GrassBlade {
    float x, y, z;   // base
    float yaw;       // radianes
    float scale;     // 0,6..1,3 de la altura
    float phase;     // desfase del viento
};
std::vector<GrassBlade> scatterGrass(const Terrain& t, size_t maxBlades = 250000);

// Texto base64 (para guardar alturas y capas dentro del JSON de la escena).
std::string base64Encode(const uint8_t* data, size_t size);
bool base64Decode(const std::string& text, std::vector<uint8_t>& out);

} // namespace titan
