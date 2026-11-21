#pragma once
// Renderizador sobre OpenGL ES 3.0: mallas 3D iluminadas, sprites por lotes, líneas y texto.
// No sabe nada de escenas ni entidades: recibe cámaras, luces, matrices y primitivas.
// El SceneRenderer (módulo runtime) traduce una escena a llamadas de este renderizador.
//
// Orden dentro de un fotograma:
//   beginFrame(viewport) → setCamera → setLights → drawMesh... → drawSprite... → drawLine... → drawText... → endFrame
// Las mallas se dibujan al llamar; sprites, líneas y texto se acumulan y se vuelcan en endFrame.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "titan/core/Components.h"
#include "titan/core/Math.h"

namespace titan {

class FileSource;

using TextureHandle = uint32_t;
constexpr TextureHandle kWhiteTexture = 0; // textura blanca 1x1: rectángulos de color

struct TextureOptions {
    bool nearest = false; // filtrado "pixel art"
    bool mipmaps = true;
};

// Rectángulo de dibujo en píxeles, origen ABAJO a la izquierda (convención de OpenGL).
struct Viewport {
    int x = 0, y = 0, width = 1, height = 1;
};

struct RenderStats {
    int drawCalls = 0;
    int sprites = 0;
    int meshes = 0;
    int triangles = 0;
    int textures = 0;
};

struct PointLightData {
    Vec3 position;
    float range = 10;
    Color color;
};

struct LightSetup {
    Color ambient{0.25f, 0.27f, 0.32f, 1};
    Vec3 directionalDir{-0.4f, -1.0f, -0.6f};
    Color directionalColor{0.9f, 0.88f, 0.82f, 1};
    std::vector<PointLightData> points; // como máximo 4 se usan
};

class Renderer {
public:
    static constexpr int kMaxPointLights = 4;

    bool init(FileSource& files);
    void shutdown();

    // Android puede destruir el contexto de OpenGL. Los recursos se recrean desde los archivos.
    void onContextLost();
    bool onContextRestored();

    TextureHandle loadTexture(const std::string& path, TextureOptions options = {});
    Vec2 textureSize(TextureHandle t) const;

    void beginFrame(const Viewport& viewport, Color clear);
    void setCamera(const Mat4& view, const Mat4& projection);
    void setLights(const LightSetup& lights);

    void drawMesh(MeshPrimitive mesh, const Mat4& world, Color color, TextureHandle tex, float shininess, bool unlit);
    // Sprite con su matriz de mundo completa: respeta posición, rotación, escala y jerarquía.
    void drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color);
    void drawLine(const Vec3& a, const Vec3& b, Color color);
    // Texto de depuración en píxeles del viewport (origen arriba a la izquierda).
    void drawText(Vec2 pos, float height, std::string_view text, Color color);
    void endFrame();

    const RenderStats& stats() const { return lastStats_; }
    const Mat4& viewProjection() const { return viewProj_; }

private:
    struct Vertex {
        float x, y, z, u, v;
        uint8_t r, g, b, a;
    };
    struct Texture {
        uint32_t gl = 0;
        int width = 0, height = 0;
        std::string path;
        TextureOptions options;
    };
    struct TextCmd {
        Vec2 pos;
        float height;
        std::string text;
        Color color;
    };
    struct GpuMesh {
        uint32_t vao = 0, vbo = 0, ibo = 0;
        int indexCount = 0;
    };

    bool createGpuObjects();
    void createMeshes();
    bool uploadTexture(Texture& t);
    void pushQuad(TextureHandle tex, const Vec3 corners[4], Rect uv, Color color);
    void flushSprites();
    void flushLines();

    static constexpr int kMaxQuads = 8192;
    static constexpr int kMaxLineVertices = 16384;

    FileSource* files_ = nullptr;
    // Programa de sprites/líneas/texto
    uint32_t spriteProgram_ = 0, spriteVao_ = 0, spriteVbo_ = 0, spriteIbo_ = 0, lineVao_ = 0, lineVbo_ = 0;
    int uSpriteViewProj_ = -1, uSpriteTex_ = -1;
    // Programa de mallas iluminadas
    uint32_t meshProgram_ = 0;
    int uViewProj_ = -1, uModel_ = -1, uNormalMat_ = -1, uColor_ = -1, uTex_ = -1, uAmbient_ = -1, uDirDir_ = -1,
        uDirColor_ = -1, uPointCount_ = -1, uPointPosRange_ = -1, uPointColor_ = -1, uCameraPos_ = -1,
        uShininess_ = -1, uUnlit_ = -1;
    GpuMesh meshes_[6];

    std::vector<Texture> textures_;
    std::vector<Vertex> vertices_;
    std::vector<Vertex> lines_;
    std::vector<TextCmd> text_;
    TextureHandle batchTexture_ = 0;
    int quadCount_ = 0;
    Viewport viewport_;
    Mat4 viewProj_;
    Vec3 cameraPos_;
    TextureHandle font_ = kWhiteTexture;
    RenderStats stats_, lastStats_;
};

} // namespace titan
