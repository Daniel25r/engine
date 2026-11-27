#pragma once
// Traduce una escena a llamadas del Renderer. Lo usan el juego (con la cámara de la escena)
// y el editor (con su cámara de edición): un único camino de render para los dos.
#include <deque>
#include <functional>
#include <vector>

#include "titan/core/Bounds.h"
#include "titan/core/Scene.h"
#include "titan/render/Renderer.h"

namespace titan {

struct SceneView {
    Mat4 view;       // inversa de la matriz de mundo de la cámara
    Mat4 projection;
    Color clearColor = Color::hex(0x1B2230);
    bool perspective = false;
};

// Cuántos objetos había y cuántos se han dibujado de verdad en el último render.
struct CullStats {
    int meshes = 0, meshesVisible = 0;       // pasada principal
    int shadowCasters = 0, shadowCastersVisible = 0;
    int sprites = 0, spritesVisible = 0;
    int posesComputed = 0;                   // esqueletos calculados (solo los que se ven o dan sombra)
    int particleSystems = 0, particleSystemsVisible = 0, particlesDrawn = 0;
    int instanced = 0, instanceBatches = 0; // objetos dibujados con instancing y en cuántos lotes
};

class SceneRenderer {
public:
    // Descartar lo que queda fuera de la cámara (y de la zona de sombras). Solo se apaga para depurar o medir.
    bool culling = true;
    // Agrupar las primitivas iguales con el mismo material (sin texturas) y dibujarlas con GPU instancing.
    bool instancing = true;
    const CullStats& cullStats() const { return stats_; }

    using TextureResolver = std::function<TextureHandle(AssetRef)>;
    using ModelResolver = std::function<int(AssetRef)>; // -1 = sin modelo
    using MaterialResolver = std::function<const MeshRenderer*(AssetRef)>; // nulo = sin material

    // Vista desde la cámara activa de mayor prioridad. Devuelve false si la escena no tiene ninguna.
    static bool cameraView(Scene& scene, float aspect, SceneView& out, Entity* cameraEntity = nullptr);

    // beginFrame + cámara + luces + mallas opacas, transparentes y sprites.
    // NO llama a endFrame: quien llama puede añadir líneas o texto (rejilla, selección, estadísticas) y luego cerrarlo.
    void render(Scene& scene, Renderer& renderer, const Viewport& viewport, const SceneView& view,
                const TextureResolver& textures, const ModelResolver& models = {},
                const MaterialResolver& materials = {});

private:
    struct MeshItem {
        const MeshRenderer* mesh;   // con el material (.mat) ya aplicado
        const Mat4* world;
        MaterialTextures maps;
        float depth;
        int model;
        int pose; // índice en poses_ o -1
    };
    std::vector<std::vector<Mat4>> poses_;
    size_t poseCount_ = 0;
    struct SpriteItem {
        int order;
        TextureHandle texture;
        const Mat4* world;
        const SpriteRenderer* sprite;
    };
    struct Candidate {
        MeshItem item;
        AABB bounds;     // en el mundo
        bool skinned;
        Entity entity;
    };
    std::vector<Candidate> candidates_;
    struct TerrainItem {
        int handle;
        const Mat4* world;
        const Terrain* terrain;
        AABB bounds;
        TextureHandle layers[4];
    };
    std::vector<TerrainItem> terrains_;
    struct InstanceBatch {
        const MeshRenderer* material;
        std::vector<Mat4> worlds;
    };
    std::vector<InstanceBatch> batches_, shadowBatches_;
    // Reparte "items": las primitivas instanciables van a lotes; el resto se queda en la lista.
    void groupInstances(std::vector<MeshItem>& items, std::vector<InstanceBatch>& out);
    std::deque<MeshRenderer> effective_; // copias con el material compartido aplicado (direcciones estables)
    std::vector<MeshItem> opaque_, transparent_, casters_;
    CullStats stats_;
    std::vector<SpriteItem> sprites_;
    std::vector<std::pair<float, int>> particleOrder_;
};

} // namespace titan
