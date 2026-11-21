#pragma once
// Fuentes TrueType/OpenType (stb_truetype) convertidas en un atlas de campo de distancia (SDF).
// Un único atlas sirve para cualquier tamaño de texto con bordes nítidos. No usa OpenGL: el renderizador
// sube los píxeles del atlas como textura de un canal.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

struct Glyph {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // caja del cuadrado respecto al punto de escritura, para tamaño 1
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // coordenadas en el atlas
    float advance = 0;                    // avance para tamaño 1
    bool visible = false;
};

struct GlyphQuad {
    Rect rect; // píxeles, origen arriba a la izquierda
    Rect uv;
};

class FontAtlas {
public:
    static constexpr float kBakeSize = 48.0f; // píxeles del atlas por em
    static constexpr int kPadding = 6;        // margen del campo de distancia

    FontAtlas();
    ~FontAtlas();
    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Construye el atlas: ASCII, Latin-1 (á é ñ ¿ ¡ ü ç...), Latin Extended-A y algunos símbolos.
    bool build(std::vector<uint8_t> ttf, std::string* error = nullptr);
    bool ok() const { return ok_; }

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return pixels_; }

    // Métricas para tamaño 1 (multiplicar por el tamaño en píxeles).
    float ascent() const { return ascent_; }
    float descent() const { return descent_; } // negativo
    float lineHeight() const { return ascent_ - descent_ + lineGap_; }
    const Glyph* glyph(uint32_t codepoint) const;
    float kerning(uint32_t a, uint32_t b) const;
    size_t glyphCount() const { return glyphs_.size(); }

    // Ancho de la línea más larga y alto total del texto (varias líneas con '\n').
    Vec2 measure(std::string_view utf8, float size) const;
    // Cuadrados de cada letra. pos = esquina superior izquierda de la primera línea.
    // align: 0 izquierda, 1 centro, 2 derecha (cada línea respecto a pos.x .. pos.x + boxWidth).
    void layout(std::string_view utf8, float size, Vec2 pos, std::vector<GlyphQuad>& out, int align = 0,
                float boxWidth = 0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<uint8_t> pixels_;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    int width_ = 0, height_ = 0;
    float ascent_ = 0, descent_ = 0, lineGap_ = 0;
    bool ok_ = false;
};

// Decodifica el siguiente carácter UTF-8; las secuencias no válidas devuelven U+FFFD y avanzan un byte.
uint32_t decodeUtf8(std::string_view s, size_t& i);

} // namespace titan
