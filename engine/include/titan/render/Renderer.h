#pragma once
// Renderizador sobre OpenGL ES 3.0: mallas 3D iluminadas, sprites por lotes, líneas y texto.
// No sabe nada de escenas ni entidades: recibe cámaras, luces, matrices y primitivas.
// El SceneRenderer (módulo runtime) traduce una escena a llamadas de este renderizador.
//
// Orden dentro de un fotograma:
//   beginFrame(viewport) → setCamera → setLights → drawMesh... → drawSprite... → drawLine... → drawText... → endFrame
// Las mallas se dibujan al llamar; sprites, líneas y texto se acumulan y se vuelcan en endFrame.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "titan/core/Components.h"
#include "titan/core/Math.h"
#include "titan/ui/Font.h"

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
    int drawCalls = 0;       // todas, incluidas las de la pasada de sombras
    int shadowDrawCalls = 0;
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
    // joints: matrices de los huesos (computePose); nulo = pose de reposo.
    void drawModel(int model, const Mat4& world, const MeshRenderer& material, TextureHandle overrideTex,
                   const std::vector<Mat4>* joints = nullptr);
    void drawModelShadow(int model, const Mat4& world, const std::vector<Mat4>* joints = nullptr);

    // ---- Animación esquelética ----
    static constexpr int kMaxJoints = 256;
    int modelAnimationCount(int model) const;
    std::string modelAnimationName(int model, int anim) const;
    float modelAnimationDuration(int model, int anim) const;
    int modelAnimationIndex(int model, const std::string& name) const; // -1 si no existe; "" = la primera
    bool modelHasSkin(int model) const;
    // Calcula las matrices de los huesos del primer esqueleto del modelo en el instante "time".
    bool computePose(int model, int anim, float time, bool loop, std::vector<Mat4>& joints) const;
    // Igual, mezclando con otro clip (transición suave): fromWeight 1 = solo el anterior, 0 = solo el nuevo.
    bool computeBlendedPose(int model, int anim, float time, int fromAnim, float fromTime, float fromWeight, bool loop,
                            std::vector<Mat4>& joints) const;
    // Sprite con su matriz de mundo completa: respeta posición, rotación, escala y jerarquía.
    void drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color);
    void drawLine(const Vec3& a, const Vec3& b, Color color);
    // Texto en píxeles del viewport (origen arriba a la izquierda), encima de todo, al final del fotograma.
    void drawText(Vec2 pos, float size, std::string_view text, Color color);

    // ---- Interfaz 2D en píxeles del viewport (origen arriba a la izquierda) ----
    // Entre beginOverlay y endOverlay todo se dibuja encima de la escena, sin profundidad.
    void beginOverlay();
    void endOverlay();
    void drawRect(TextureHandle tex, const Rect& px, const Rect& uv, Color color);
    // Rectángulo de esquinas redondeadas (radio en píxeles; se limita a la mitad del lado menor).
    void drawRoundedRect(const Rect& px, float radius, Color color);
    // Fuentes: 0 = Outfit normal, 1 = Outfit negrita (las del motor). loadFont devuelve -1 si falla.
    int loadFont(const std::string& path);
    const FontAtlas* font(int f);
    Vec2 measureText(int font, float size, std::string_view text);
    // align: 0 izquierda, 1 centro, 2 derecha dentro de boxWidth.
    void drawTextNow(int font, Vec2 pos, float size, std::string_view text, Color color, int align = 0,
                     float boxWidth = 0);
    // Textura creada desde memoria (se conserva una copia para recrearla si Android pierde el contexto).
    // channels: 1 (se usa como alfa/distancia) o 4. sdf: el canal es un campo de distancia (texto).
    TextureHandle createTexture(int width, int height, int channels, const uint8_t* pixels, bool sdf = false);
    TextureHandle discTexture() const { return disc_; }

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
        std::vector<uint8_t> memory; // texturas creadas desde memoria (createTexture)
        int channels = 4;
        bool sdf = false;
    };
    struct LoadedFont {
        std::string path;
        std::unique_ptr<FontAtlas> atlas;
        TextureHandle texture = 0;
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
        bool skinned = false;
        uint32_t skinVbo = 0; // huesos y pesos por vértice
        Color color;
        float metallic = 0, roughness = 1;
        Color emission{0, 0, 0, 1};
        TextureHandle texture = 0;
        bool doubleSided = false;
    };
    struct AnimNode {
        int parent = -1;
        Vec3 t;
        Quat r;
        Vec3 s{1, 1, 1};
    };
    struct AnimChannel {
        int node = -1;
        int path = 0; // 0 traslación, 1 rotación, 2 escala
        bool step = false;
        std::vector<float> times, values;
    };
    struct AnimClip {
        std::string name;
        float duration = 0;
        std::vector<AnimChannel> channels;
    };
    struct Skin {
        std::vector<int> joints;
        std::vector<Mat4> inverseBind;
    };
    struct Model {
        std::string path;
        std::vector<ModelPart> parts;
        std::vector<AnimNode> nodes;
        std::vector<Skin> skins;
        std::vector<AnimClip> clips;
        Vec3 min, max;
        int triangles = 0;
        bool ok = false;
    };
    bool buildModel(Model& m);
    void sampleClip(const Model& m, int anim, float time, bool loop, std::vector<AnimNode>& local) const;
    void uploadJoints(const std::vector<Mat4>& joints);
    void drawSurface(uint32_t vao, int count, bool wideIndices, const Mat4& world, Color color, float metallic,
                     float roughness, Color emission, bool grid, bool unlit, TextureHandle tex, bool doubleSided);

    bool createGpuObjects();
    void createMeshes();
    bool uploadTexture(Texture& t);
    bool uploadMemoryTexture(Texture& t);
    void pushQuad(TextureHandle tex, const Vec3 corners[4], Rect uv, Color color);
    void flushSprites();
    void flushLines();

    static constexpr int kMaxQuads = 8192;
    static constexpr int kMaxLineVertices = 16384;

    FileSource* files_ = nullptr;
    // Programa de sprites/líneas/texto
    uint32_t spriteProgram_ = 0, spriteVao_ = 0, spriteVbo_ = 0, spriteIbo_ = 0, lineVao_ = 0, lineVbo_ = 0;
    int uSpriteViewProj_ = -1, uSpriteTex_ = -1, uSpriteSdf_ = -1;
    // Programa de mallas iluminadas
    uint32_t meshProgram_ = 0;
    int uViewProj_ = -1, uModel_ = -1, uNormalMat_ = -1, uColor_ = -1, uTex_ = -1, uDirDir_ = -1, uDirColor_ = -1,
        uPointCount_ = -1, uPointPosRange_ = -1, uPointColor_ = -1, uCameraPos_ = -1, uUnlit_ = -1, uMetallic_ = -1,
        uRoughness_ = -1, uEmission_ = -1, uGrid_ = -1, uSkyZenith_ = -1, uSkyHorizon_ = -1, uGround_ = -1,
        uAmbientI_ = -1, uFogColor_ = -1, uFogDensity_ = -1, uExposure_ = -1, uLightVP_ = -1, uShadowMap_ = -1,
        uShadowsOn_ = -1, uSkinned_ = -1, uJoints_ = -1;
    // Sombras y cielo
    uint32_t shadowProgram_ = 0, shadowFbo_ = 0, shadowTex_ = 0;
    int uShadowLVP_ = -1, uShadowModel_ = -1, uShadowSkinned_ = -1, uShadowJoints_ = -1;
    const std::vector<Mat4>* currentJoints_ = nullptr;
    uint32_t jointTex_ = 0;
    int prevFbo_ = 0;
    int shadowDrawStart_ = 0, shadowDrawCalls_ = 0; // la pasada de sombras va antes de beginFrame
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
    std::vector<LoadedFont> fonts_;
    std::vector<GlyphQuad> glyphScratch_;
    TextureHandle disc_ = kWhiteTexture;
    Mat4 overlaySavedViewProj_;
    bool inOverlay_ = false;
    RenderStats stats_, lastStats_;
};

} // namespace titan
