#pragma once
// Traduce una escena a llamadas del Renderer. Lo usan el juego (con la cámara de la escena)
// y el editor (con su cámara de edición): un único camino de render para los dos.
#include <functional>
#include <vector>

#include "titan/core/Scene.h"
#include "titan/render/Renderer.h"

namespace titan {

struct SceneView {
    Mat4 view;       // inversa de la matriz de mundo de la cámara
    Mat4 projection;
    Color clearColor = Color::hex(0x1B2230);
    bool perspective = false;
};

class SceneRenderer {
public:
    using TextureResolver = std::function<TextureHandle(AssetRef)>;

    // Vista desde la cámara activa de mayor prioridad. Devuelve false si la escena no tiene ninguna.
    static bool cameraView(Scene& scene, float aspect, SceneView& out, Entity* cameraEntity = nullptr);

    // beginFrame + cámara + luces + mallas opacas, transparentes y sprites.
    // NO llama a endFrame: quien llama puede añadir líneas o texto (rejilla, selección, estadísticas) y luego cerrarlo.
    void render(Scene& scene, Renderer& renderer, const Viewport& viewport, const SceneView& view,
                const TextureResolver& textures);

private:
    struct MeshItem {
        const MeshRenderer* mesh;
        const Mat4* world;
        TextureHandle texture;
        float depth;
    };
    struct SpriteItem {
        int order;
        TextureHandle texture;
        const Mat4* world;
        const SpriteRenderer* sprite;
    };
    std::vector<MeshItem> opaque_, transparent_;
    std::vector<SpriteItem> sprites_;
};

} // namespace titan
