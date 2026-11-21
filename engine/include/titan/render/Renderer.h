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

// Todo lo que afecta a la luz de un fotograma: ambiente (cielo, niebla, exposición), sol y luces puntuales.
struct SceneLighting {
    Environment env;
    bool drawSky = true;
    Vec3 sunDirection{-0.4f, -1.0f, -0.6f}; // hacia dónde viaja la luz
    Color sunColor{1, 0.96f, 0.9f, 1};      // ya multiplicado por la intensidad
    std::vector<PointLightData> points;     // como máximo 4 se usan
    bool shadows = false;
    Mat4 lightViewProjection;               // para el mapa de sombras
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

    // Mapa de sombras del sol: se rellena ANTES de beginFrame.
    static constexpr int kShadowMapSize = 2048;
    void beginShadowPass(const Mat4& lightViewProjection);
    void drawShadowCaster(MeshPrimitive mesh, const Mat4& world);
    void endShadowPass();

    void beginFrame(const Viewport& viewport, Color clear);
    void setCamera(const Mat4& view, const Mat4& projection);
    void setLighting(const SceneLighting& lighting);
    void drawSky();
    // "Sin iluminación" del editor: muestra solo el color base.
    void setUnlitOverride(bool on) { unlitOverride_ = on; }

    void drawMesh(MeshPrimitive mesh, const Mat4& world, const MeshRenderer& material, TextureHandle tex);

    // ---- Modelos importados (glTF 2.0 / GLB) ----
    // Devuelve -1 si el archivo no existe o no se puede interpretar (el motivo va al registro).
    int loadModel(const std::string& path);
    bool modelBounds(int model, Vec3& min, Vec3& max) const;
    int modelTriangles(int model) const;
    // El color del MeshRenderer tiñe el del modelo; su textura, si la tiene, sustituye a la del modelo.
    void drawModel(int model, const Mat4& world, const MeshRenderer& material, TextureHandle overrideTex);
    void drawModelShadow(int model, const Mat4& world);
    // Sprite con su matriz de mundo completa: respeta posición, rotación, escala y jerarquía.
    void drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color);
    void drawLine(const Vec3& a, const Vec3& b, Color color);
    // Texto de depuración en píxeles del viewport (origen arriba a la izquierda).
    void drawText(Vec2 pos, float height, std::string_view text, Color color);
    void endFrame();

    const RenderStats& stats() const { return lastStats_; }
    const Mat4& viewProjection() const { return viewProj_; }
    uint32_t glTexture(TextureHandle t) const { return t < textures_.size() ? textures_[t].gl : 0; }

private:
    struct Vertex {
        float x, y, z, u, v;
        uint8_t r, g, b, a;
    };
    struct Texture {
        uint32_t gl = 0;
        int width = 0, height = 0;
        std::string path;          // vacío: textura incrustada en un modelo (se recrea con el modelo)
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
    struct ModelPart {
        GpuMesh mesh;
        Color color;
        float metallic = 0, roughness = 1;
        Color emission{0, 0, 0, 1};
        TextureHandle texture = 0;
        bool doubleSided = false;
    };
    struct Model {
        std::string path;
        std::vector<ModelPart> parts;
        Vec3 min, max;
        int triangles = 0;
        bool ok = false;
    };
    bool buildModel(Model& m);
    void drawSurface(uint32_t vao, int count, bool wideIndices, const Mat4& world, Color color, float metallic,
                     float roughness, Color emission, bool grid, bool unlit, TextureHandle tex, bool doubleSided);

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
    int uViewProj_ = -1, uModel_ = -1, uNormalMat_ = -1, uColor_ = -1, uTex_ = -1, uDirDir_ = -1, uDirColor_ = -1,
        uPointCount_ = -1, uPointPosRange_ = -1, uPointColor_ = -1, uCameraPos_ = -1, uUnlit_ = -1, uMetallic_ = -1,
        uRoughness_ = -1, uEmission_ = -1, uGrid_ = -1, uSkyZenith_ = -1, uSkyHorizon_ = -1, uGround_ = -1,
        uAmbientI_ = -1, uFogColor_ = -1, uFogDensity_ = -1, uExposure_ = -1, uLightVP_ = -1, uShadowMap_ = -1,
        uShadowsOn_ = -1;
    // Sombras y cielo
    uint32_t shadowProgram_ = 0, shadowFbo_ = 0, shadowTex_ = 0;
    int uShadowLVP_ = -1, uShadowModel_ = -1;
    int prevFbo_ = 0;
    uint32_t skyProgram_ = 0, skyVao_ = 0;
    int uSkyInvVP_ = -1, uSkyCam_ = -1, uSkyZ_ = -1, uSkyH_ = -1, uSkyG_ = -1, uSkySun_ = -1, uSkySunColor_ = -1;
    SceneLighting lighting_;
    std::vector<Model> models_;
    bool unlitOverride_ = false;
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
