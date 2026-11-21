#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "forja/Math.h"

namespace forja {

class Assets;

// 0 es siempre una textura blanca de 1x1 (sirve para dibujar rectángulos de color).
using TextureId = uint32_t;
constexpr TextureId kWhiteTexture = 0;

// Cámara ortográfica 2D. El mundo usa Y hacia arriba.
// viewSize = unidades de mundo que caben en el lado CORTO de la pantalla con zoom 1,
// así el juego se ve igual en vertical, en horizontal y en cualquier resolución.
struct Camera2D {
    Vec2 position;
    float zoom = 1.0f;
    float viewSize = 720.0f;
    Vec2 viewport{1.0f, 1.0f}; // píxeles; lo actualiza el motor cada fotograma

    Vec2 halfExtents() const {
        const float half = viewSize * 0.5f / zoom;
        if (viewport.x <= viewport.y) return {half, half * viewport.y / viewport.x};
        return {half * viewport.x / viewport.y, half};
    }
    Mat4 matrix() const {
        const Vec2 h = halfExtents();
        return Mat4::ortho(position.x - h.x, position.x + h.x, position.y - h.y, position.y + h.y);
    }
    Vec2 screenToWorld(Vec2 px) const {
        const Vec2 h = halfExtents();
        return {position.x + (px.x / viewport.x - 0.5f) * 2.0f * h.x,
                position.y + (0.5f - px.y / viewport.y) * 2.0f * h.y};
    }
};

struct RenderStats {
    int drawCalls = 0;
    int quads = 0;
};

// Renderizador 2D por lotes sobre OpenGL ES 3.0: agrupa miles de sprites en
// pocas llamadas de dibujo (una por cambio de textura).
class Renderer {
public:
    bool init(Assets& assets);
    void shutdown();

    // Android puede destruir el contexto de OpenGL (p. ej. al salir de la app).
    // El motor marca los recursos como perdidos y los vuelve a crear desde los assets.
    void onContextLost();
    bool onContextRestored();

    TextureId loadTexture(const std::string& path, bool pixelArt = false);
    Vec2 textureSize(TextureId id) const;

    void beginFrame(int width, int height, Color clear);
    void setProjection(const Mat4& m);
    // Cuadrado en coordenadas de mundo. pivot (0..1) es el punto de giro dentro del sprite.
    void drawQuad(TextureId tex, Vec2 pos, Vec2 size, float rotation, Vec2 pivot, Rect uv, Color color);
    // Texto de depuración en píxeles de pantalla (origen arriba a la izquierda).
    void drawText(Vec2 pos, float height, std::string_view text, Color color);
    void endFrame();

    const RenderStats& stats() const { return lastStats_; }

private:
    struct Vertex {
        float x, y, u, v;
        uint8_t r, g, b, a;
    };
    struct Texture {
        uint32_t gl = 0;
        int width = 0, height = 0;
        std::string path;
        bool pixelArt = false;
    };
    struct TextCmd {
        Vec2 pos;
        float height;
        std::string text;
        Color color;
    };

    bool createGpuObjects();
    bool uploadTexture(Texture& t);
    void pushQuad(TextureId tex, const Vec2 corners[4], Rect uv, Color color);
    void flush();

    static constexpr int kMaxQuads = 8192;

    Assets* assets_ = nullptr;
    uint32_t program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0;
    int uProj_ = -1, uTex_ = -1;
    std::vector<Texture> textures_;
    std::vector<Vertex> vertices_;
    std::vector<TextCmd> text_;
    TextureId batchTexture_ = 0;
    int quadCount_ = 0;
    int width_ = 1, height_ = 1;
    TextureId font_ = 0;
    RenderStats stats_, lastStats_;
};

} // namespace forja
