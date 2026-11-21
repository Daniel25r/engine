#pragma once
// Renderizador 2D por lotes sobre OpenGL ES 3.0.
// No sabe nada de escenas ni entidades: recibe matrices y sprites. El RenderSystem del
// runtime traduce la escena a llamadas de este renderizador (ver docs/ARCHITECTURE.md).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

class FileSource;

using TextureHandle = uint32_t;
constexpr TextureHandle kWhiteTexture = 0; // textura blanca 1x1: rectángulos de color

struct TextureOptions {
    bool nearest = false; // filtrado "pixel art"
    bool mipmaps = true;
};

struct RenderStats {
    int drawCalls = 0;
    int quads = 0;
    int textures = 0;
};

class Renderer {
public:
    bool init(FileSource& files);
    void shutdown();

    // Android puede destruir el contexto de OpenGL. Los recursos se recrean desde los archivos.
    void onContextLost();
    bool onContextRestored();

    // Devuelve kWhiteTexture (y lo registra) si el archivo no existe o no se puede decodificar.
    TextureHandle loadTexture(const std::string& path, TextureOptions options = {});
    Vec2 textureSize(TextureHandle t) const;

    void beginFrame(int width, int height, Color clear);
    void setViewProjection(const Mat4& viewProj);
    // Sprite con su matriz de mundo completa: respeta posición, rotación, escala y jerarquía.
    void drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color);
    // Texto de depuración en píxeles de pantalla (origen arriba a la izquierda).
    void drawText(Vec2 pos, float height, std::string_view text, Color color);
    void endFrame();

    const RenderStats& stats() const { return lastStats_; }

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

    bool createGpuObjects();
    bool uploadTexture(Texture& t);
    void pushQuad(TextureHandle tex, const Vec3 corners[4], Rect uv, Color color);
    void flush();

    static constexpr int kMaxQuads = 8192;

    FileSource* files_ = nullptr;
    uint32_t program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    int uViewProj_ = -1, uTex_ = -1;
    std::vector<Texture> textures_;
    std::vector<Vertex> vertices_;
    std::vector<TextCmd> text_;
    TextureHandle batchTexture_ = 0;
    int quadCount_ = 0;
    int width_ = 1, height_ = 1;
    TextureHandle font_ = kWhiteTexture;
    RenderStats stats_, lastStats_;
};

} // namespace titan
