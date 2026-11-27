#pragma once
// Renderizador sobre OpenGL ES 3.0: mallas 3D iluminadas, sprites por lotes, líneas y texto.
// No sabe nada de escenas ni entidades: recibe cámaras, luces, matrices y primitivas.
// El SceneRenderer (módulo runtime) traduce una escena a llamadas de este renderizador.
//
// Orden dentro de un fotograma:
//   beginFrame(viewport) → setCamera → setLights → drawMesh... → drawSprite... → drawLine... → drawText... → endFrame
// Las mallas se dibujan al llamar; sprites, líneas y texto se acumulan y se vuelcan en endFrame.
#include <chrono>
#include <set>

#include "titan/core/MaterialGraph.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "titan/core/Components.h"
#include "titan/core/Math.h"
#include "titan/core/Quality.h"
#include "titan/core/TerrainTools.h"
#include "titan/ui/Font.h"

namespace titan {

class FileSource;
class PostProcessor;

using TextureHandle = uint32_t;
constexpr TextureHandle kWhiteTexture = 0; // textura blanca 1x1: rectángulos de color

struct TextureOptions {
    bool nearest = false; // filtrado "pixel art"
    bool mipmaps = true;
};

// Imagen decodificada en la CPU (RGBA de 8 bits). decodeImage no toca OpenGL: se puede llamar desde cualquier hilo
// (lectura del FileSource + stb_image). Subirla a la GPU sí debe hacerse en el hilo del render (addDecodedTexture).
struct DecodedImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    std::string error;
};
bool decodeImage(FileSource& files, const std::string& path, DecodedImage& out);

// Rectángulo de dibujo en píxeles, origen ABAJO a la izquierda (convención de OpenGL).
struct Viewport {
    int x = 0, y = 0, width = 1, height = 1;
};

struct RenderStats {
    int drawCalls = 0;       // todas, incluidas las de la pasada de sombras (sin contar el posprocesado)
    int shadowDrawCalls = 0;
    int postPasses = 0;      // pasadas a pantalla completa del posprocesado
    int instances = 0;       // objetos dibujados con instancing (dentro de drawCalls cuentan una vez por lote)
    int sceneWidth = 0, sceneHeight = 0; // resolución a la que se dibujó la escena
    int sprites = 0;
    int meshes = 0;
    int triangles = 0;
    int textures = 0;
};

// Luz local: puntual o foco.
struct PointLightData {
    Vec3 position;
    float range = 10;
    Color color;
    bool spot = false;
    Vec3 direction{0, 0, -1}; // foco: hacia dónde apunta
    float cosOuter = 0.7f;    // foco: coseno del borde del cono
    float cosInner = 0.8f;    // foco: coseno donde empieza el difuminado
};

// Texturas de un material ya resueltas a handles (kWhiteTexture = sin textura).
struct MaterialTextures {
    TextureHandle base = kWhiteTexture, normal = kWhiteTexture, metallicRoughness = kWhiteTexture,
                  emission = kWhiteTexture, occlusion = kWhiteTexture;
    TextureHandle graph0 = kWhiteTexture, graph1 = kWhiteTexture; // texturas del material por nodos
};

// Todo lo que afecta a la luz de un fotograma: ambiente (cielo, niebla, exposición), sol y luces puntuales.
struct SceneLighting {
    Environment env;
    bool drawSky = true;
    Vec3 sunDirection{-0.4f, -1.0f, -0.6f}; // hacia dónde viaja la luz
    Color sunColor{1, 0.96f, 0.9f, 1};      // ya multiplicado por la intensidad
    std::vector<PointLightData> points;     // como máximo 4 se usan
    bool shadows = false;
    Mat4 lightViewProjection;               // para el mapa de sombras (la última cascada si hay cascadas)
    // Sombras en cascada (CSM): hasta 4 mapas del sol, cada uno para un tramo de distancia a la cámara, en un
    // atlas 2×2 dentro del mapa de sombras. 0 = un solo mapa (lightViewProjection).
    int cascades = 0;
    Mat4 cascadeVP[4];
    float cascadeSplit[4] = {0, 0, 0, 0}; // distancia a la cámara donde termina cada cascada
    float cascadeTexel[4] = {0, 0, 0, 0}; // tamaño de un texel en metros (sesgo por la normal)
};

class Renderer {
public:
    static constexpr int kMaxPointLights = 8; // luces locales (puntuales + focos); el perfil de calidad limita cuántas

    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(FileSource& files);
    void shutdown();

    // ---- Calidad y capacidades ----
    const DeviceCaps& caps() const { return caps_; }  // se rellena en init()
    void setQuality(const QualitySettings& q);         // se aplica al siguiente fotograma
    const QualitySettings& quality() const { return quality_; }
    int shadowMapSize() const { return shadowSize_; }

    // Android puede destruir el contexto de OpenGL. Los recursos se recrean desde los archivos.
    void onContextLost();
    bool onContextRestored();

    TextureHandle loadTexture(const std::string& path, TextureOptions options = {});
    // Ya cargada con esa ruta y opciones (kWhiteTexture si no).
    TextureHandle findTexture(const std::string& path, TextureOptions options) const;
    // Sube una imagen decodificada en otro hilo (carga asíncrona); si ya estaba, devuelve la existente.
    TextureHandle addDecodedTexture(const std::string& path, TextureOptions options, const DecodedImage& image);
    Vec2 textureSize(TextureHandle t) const;

    // Mapa de sombras del sol: se rellena ANTES de beginFrame.
    static constexpr int kShadowMapSize = 2048; // por defecto; el perfil de calidad lo cambia (shadowMapSize())
    // cascade >= 0: dibuja en su cuarto del atlas (la 0 limpia el mapa entero).
    void beginShadowPass(const Mat4& lightViewProjection, int cascade = -1);
    // Antialiasing temporal para la próxima cámara en perspectiva (antes de setCamera).
    void setTemporalAA(bool on) { taaRequested_ = on; }
    // HDR para el próximo beginFrame (la escena en coma flotante, tonemapping al final).
    void setHdr(bool on) { hdrRequested_ = on; }
    // Niveles de detalle automáticos de los modelos (activados por defecto).
    void setLodEnabled(bool on);
    bool hdrActive() const { return hdrActive_; }
    void drawShadowCaster(MeshPrimitive mesh, const Mat4& world);
    void endShadowPass();

    void beginFrame(const Viewport& viewport, Color clear);
    // Sonda de reflexión: beginProbeCapture crea el cubemap; para cada cara se llama a probeFace y se dibuja la escena
    // (sin posprocesado); endProbeCapture genera los niveles de rugosidad y la activa para los materiales.
    bool beginProbeCapture(int size);
    void probeFace(int face);
    void endProbeCapture(float intensity);
    void clearProbe() { probeOn_ = false; }
    bool probeActive() const { return probeOn_; }
    void setCamera(const Mat4& view, const Mat4& projection);
    void setLighting(const SceneLighting& lighting);
    void drawSky();
    // "Sin iluminación" del editor: muestra solo el color base.
    void setUnlitOverride(bool on) { unlitOverride_ = on; }

    void drawMesh(MeshPrimitive mesh, const Mat4& world, const MeshRenderer& material, const MaterialTextures& tex);
    // GPU instancing: muchas copias de la misma primitiva con el mismo material (sin texturas) en una llamada.
    static constexpr int kMaxInstances = 1024;
    void drawMeshInstanced(MeshPrimitive mesh, const std::vector<Mat4>& worlds, const MeshRenderer& material);
    void drawShadowCastersInstanced(MeshPrimitive mesh, const std::vector<Mat4>& worlds); // dentro de la pasada de sombras

    // ---- Terreno ----
    // Malla de un terreno en la GPU, identificada por "key" (p. ej. la entidad). Solo se reconstruye si cambia
    // la revisión. Devuelve el handle para drawTerrain.
    int terrainMesh(uint64_t key, uint32_t revision, const std::function<TerrainMesh()>& build);
    void drawTerrain(int handle, const Mat4& world, const Terrain& t, const TextureHandle layers[4]);
    void drawTerrainShadow(int handle, const Mat4& world);

    // ---- Modelos importados (glTF 2.0 / GLB) ----
    // Devuelve -1 si el archivo no existe o no se puede interpretar (el motivo va al registro).
    int loadModel(const std::string& path);
    // Sustituye (o crea) el modelo de esa ruta con un GLB en memoria: el modelado del editor lo usa para ver cada
    // cambio al momento sin escribir en disco. Libera los búferes de la GPU de la versión anterior.
    int setModelData(const std::string& path, std::vector<uint8_t> glb);
    // Libera la GPU y la memoria de un modelo (chunks que se descargan); su ranura se reutiliza con la misma ruta.
    void unloadModel(const std::string& path);
    bool modelBounds(int model, Vec3& min, Vec3& max) const;
    // Triángulos del modelo en la CPU (espacio del modelo), para la colisión de malla.
    bool modelTriangles(int model, std::vector<Vec3>& verts, std::vector<uint32_t>& indices) const;
    int modelTriangles(int model) const;
    // El color del MeshRenderer tiñe el del modelo; su textura, si la tiene, sustituye a la del modelo.
    // joints: matrices de los huesos (computePose); nulo = pose de reposo.
    // Las texturas del MeshRenderer, si las tiene, sustituyen a las del modelo, una por una.
    void drawModel(int model, const Mat4& world, const MeshRenderer& material, const MaterialTextures& overrideTex,
                   const std::vector<Mat4>* joints = nullptr);
    void drawModelShadow(int model, const Mat4& world, const std::vector<Mat4>* joints = nullptr);

    // ---- Animación esquelética ----
    static constexpr int kMaxJoints = 256;
    int modelAnimationCount(int model) const;
    std::string modelAnimationName(int model, int anim) const;
    float modelAnimationDuration(int model, int anim) const;
    int modelAnimationIndex(int model, const std::string& name) const; // -1 si no existe; "" = la primera
    // Añade a "target" los clips de "source" cuyos huesos coinciden por nombre (animaciones de Mixamo descargadas
    // aparte). Cada pareja se aplica una sola vez; devuelve cuántos clips se añadieron.
    int addAnimationsFrom(int target, int source);
    bool modelHasSkin(int model) const;
    // Calcula las matrices de los huesos del primer esqueleto del modelo en el instante "time".
    bool computePose(int model, int anim, float time, bool loop, std::vector<Mat4>& joints) const;
    // Igual, mezclando con otro clip (transición suave): fromWeight 1 = solo el anterior, 0 = solo el nuevo.
    bool computeBlendedPose(int model, int anim, float time, int fromAnim, float fromTime, float fromWeight, bool loop,
                            std::vector<Mat4>& joints) const;
    // Sprite con su matriz de mundo completa: respeta posición, rotación, escala y jerarquía.
    void drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color);
    void drawLine(const Vec3& a, const Vec3& b, Color color);
    // Cuadrado orientado a la cámara (partículas): centro en el mundo, lado, giro en grados, UV y color.
    // additive = mezcla aditiva (fuego, chispas); si no, mezcla alfa.
    void drawBillboard(TextureHandle tex, const Vec3& center, float size, float rotationDeg, Rect uv, Color color,
                       bool additive);
    TextureHandle softParticleTexture() const { return softDot_; }
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
        // Niveles de detalle: en el mismo búfer de índices van el original y dos versiones simplificadas.
        int lodFirst[3] = {0, 0, 0}, lodCount[3] = {0, 0, 0};
        int lods = 1;
        bool skinned = false;
        uint32_t skinVbo = 0; // huesos y pesos por vértice
        Color color;
        float metallic = 0, roughness = 1;
        Color emission{0, 0, 0, 1};
        MaterialTextures maps;
        bool doubleSided = false;
        float alphaCutoff = -1; // glTF alphaMode MASK
    };
    struct Surface {
        Color color;
        float metallic = 0, roughness = 1;
        Color emission{0, 0, 0, 1};
        bool grid = false, unlit = false, doubleSided = false;
        MaterialTextures maps;
        Vec2 tiling{1, 1}, offset;
        float alphaCutoff = -1;
        float normalStrength = 1;
        float clearcoat = 0, clearcoatRoughness = 0.05f;
        const MatGraphProgram* graph = nullptr; // material por nodos (compilado)
        int firstIndex = 0;                     // primer índice (niveles de detalle)
        bool water = false;
        float waveScale = 1, waveSpeed = 1;
    };
    struct AnimNode {
        std::string name; // para pasar animaciones entre modelos con el mismo esqueleto (Mixamo)
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
        std::vector<uint8_t> memory;           // GLB en memoria (setModelData); vacío = se lee del FileSource
        std::vector<Vec3> collisionVerts;      // copia para la física (colliders de malla)
        std::vector<uint32_t> collisionIndices;
    };
    bool buildModel(Model& m);
    void releaseModelGpu(Model& m);
    void sampleClip(const Model& m, int anim, float time, bool loop, std::vector<AnimNode>& local) const;
    void uploadJoints(const std::vector<Mat4>& joints);
    void drawSurface(uint32_t vao, int count, bool wideIndices, const Mat4& world, const Surface& s);

    bool createGpuObjects();
    void createShadowMap();
    void resolveScene(); // compone la escena del render target en pantalla (una vez por fotograma)
    void createMeshes();
    bool uploadTexture(Texture& t);
    bool uploadPixels(Texture& t, const DecodedImage& image);
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
        uShadowsOn_ = -1, uSkinned_ = -1, uJoints_ = -1, uUVTransform_ = -1, uNormalTex_ = -1, uMRTex_ = -1,
        uEmissiveTex_ = -1, uOcclusionTex_ = -1, uHasNormal_ = -1, uHasMR_ = -1, uHasOcclusion_ = -1,
        uNormalStrength_ = -1, uClearcoat_ = -1, uCascades_ = -1, uCascadeVP_ = -1, uCascadeSplit_ = -1,
        uCascadeTexel_ = -1, uGraphCount_ = -1, uGraph_ = -1, uGraphOut_ = -1, uGraphTex0_ = -1, uGraphTex1_ = -1,
        uTime_ = -1, uWater_ = -1, uProbe_ = -1, uProbeOn_ = -1, uProbeMips_ = -1, uProbeIntensity_ = -1, uAlphaCutoff_ = -1, uSpot_ = -1, uSpotInner_ = -1, uTerrain_ = -1, uLayerColor_ = -1,
        uLayerTiling_ = -1;
    int uLayers_[4] = {-1, -1, -1, -1};
    int uInstanced_ = -1, uShadowInstanced_ = -1;
    uint32_t instanceVbo_ = 0, instancedVao_[6] = {};
    int instanceCount_ = 0;
    void uploadInstances(const Mat4* worlds, int count);
    bool terrainDraw_ = false;
    struct TerrainGpu {
        uint64_t key = 0;
        uint32_t revision = 0;
        GpuMesh mesh;
        uint64_t lastUsed = 0; // fotograma; los que nadie dibuja en un rato se liberan
    };
    uint64_t frameCounter_ = 0;
    std::vector<TerrainGpu> terrains_;
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
    Mat4 projection_;
    bool specBuffersOn_ = false;
    uint32_t probeTex_ = 0, probeFbo_ = 0, probeDepth_ = 0;
    int probeSize_ = 0;
    int probePrevFbo_ = 0; // propio: la pasada de sombras usa prevFbo_ durante la captura
    bool probeOn_ = false, forceDirect_ = false;
    float probeIntensity_ = 1.0f;
    bool lodEnabled_ = true;
    std::set<std::pair<int, int>> mergedAnimations_;
    int chooseLod(const Model& m, const ModelPart& p, const Mat4& world) const;
    // TAA: proyección desplazada una fracción de píxel cada fotograma; la cámara del fotograma anterior reproyecta.
    bool taaRequested_ = false, taaActive_ = false, prevValid_ = false;
    bool hdrRequested_ = false, hdrActive_ = false;
    std::chrono::steady_clock::time_point lastResolve_{}; // dt de la exposición automática
    void setHdrUniforms(bool on);
    Mat4 unjitteredVP_, prevVP_;
    uint32_t taaFrame_ = 0;
    float jitterUV_[2] = {0, 0};
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now(); // uTime de los materiales // se dibuja también en el destino de reflectancia (SSR) // de la cámara actual (el SSAO reconstruye posiciones desde la profundidad)
    Vec3 cameraPos_;
    Vec3 camRight_{1, 0, 0}, camUp_{0, 1, 0};
    std::vector<LoadedFont> fonts_;
    QualitySettings quality_;
    DeviceCaps caps_;
    int shadowSize_ = kShadowMapSize;
    std::unique_ptr<PostProcessor> post_;
    bool sceneInTarget_ = false; // la escena de este fotograma va al render target
    int targetFbo_ = 0;          // framebuffer final (0 en Android; el del editor si dibuja en otro)
    std::vector<GlyphQuad> glyphScratch_;
    TextureHandle disc_ = kWhiteTexture;
    TextureHandle softDot_ = kWhiteTexture; // punto con borde difuminado: partícula por defecto
    bool batchAdditive_ = false;
    Mat4 overlaySavedViewProj_;
    bool inOverlay_ = false;
    RenderStats stats_, lastStats_;
};

} // namespace titan
