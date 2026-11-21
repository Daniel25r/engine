#include "titan/ui/Font.h"

#include <algorithm>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"

namespace titan {

struct FontAtlas::Impl {
    std::vector<uint8_t> ttf;
    stbtt_fontinfo info{};
    float scale = 1; // de unidades de la fuente a "tamaño 1"
};

FontAtlas::FontAtlas() = default;
FontAtlas::~FontAtlas() = default;

uint32_t decodeUtf8(std::string_view s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    auto bits = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[i + k]) & 0x3F); };
    if (c < 0x80) { i += 1; return c; }
    if ((c & 0xE0) == 0xC0 && cont(1)) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | bits(1);
        i += 2;
        return cp >= 0x80 ? cp : 0xFFFD;
    }
    if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | (bits(1) << 6) | bits(2);
        i += 3;
        return cp >= 0x800 ? cp : 0xFFFD;
    }
    if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        const uint32_t cp = ((c & 0x07u) << 18) | (bits(1) << 12) | (bits(2) << 6) | bits(3);
        i += 4;
        return cp >= 0x10000 && cp <= 0x10FFFF ? cp : 0xFFFD;
    }
    i += 1;
    return 0xFFFD;
}

bool FontAtlas::build(std::vector<uint8_t> ttf, std::string* error) {
    ok_ = false;
    glyphs_.clear();
    impl_ = std::make_unique<Impl>();
    impl_->ttf = std::move(ttf);
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        impl_.reset();
        return false;
    };
    if (impl_->ttf.size() < 12) return fail("el archivo está vacío o no es una fuente");
    const unsigned char* data = impl_->ttf.data();
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&impl_->info, data, offset)) return fail("no es una fuente TrueType/OpenType válida");
    const stbtt_fontinfo& info = impl_->info;

    // "Tamaño 1" = 1 píxel por em, como en CSS: el tamaño del texto es el del em.
    impl_->scale = stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    ascent_ = asc * impl_->scale;
    descent_ = desc * impl_->scale;
    lineGap_ = gap * impl_->scale;

    std::vector<uint32_t> codepoints;
    for (uint32_t c = 32; c < 127; ++c) codepoints.push_back(c);
    for (uint32_t c = 160; c < 384; ++c) codepoints.push_back(c);
    for (uint32_t c : {0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u, 0x20ACu, 0x2190u, 0x2191u,
                       0x2192u, 0x2193u, 0x2605u, 0x2665u, 0x00D7u, 0xFFFDu})
        codepoints.push_back(c);

    struct Pending {
        uint32_t cp;
        unsigned char* sdf;
        int w, h, xoff, yoff;
    };
    std::vector<Pending> bitmaps;
    const float bakeScale = stbtt_ScaleForMappingEmToPixels(&info, kBakeSize);
    const unsigned char onEdge = 128;
    const float distScale = 128.0f / kPadding;
    for (uint32_t cp : codepoints) {
        const int g = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        if (g == 0 && cp != 32) continue; // la fuente no tiene ese carácter
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, g, &adv, &lsb);
        Glyph gl;
        gl.advance = adv * impl_->scale;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetGlyphSDF(&info, bakeScale, g, kPadding, onEdge, distScale, &w, &h, &xo, &yo);
        if (sdf && w > 0 && h > 0) {
            gl.visible = true;
            gl.x0 = xo / kBakeSize;
            gl.y0 = yo / kBakeSize;
            gl.x1 = (xo + w) / kBakeSize;
            gl.y1 = (yo + h) / kBakeSize;
            bitmaps.push_back({cp, sdf, w, h, xo, yo});
        } else if (sdf) {
            stbtt_FreeSDF(sdf, nullptr);
        }
        glyphs_[cp] = gl;
    }
    if (glyphs_.find('A') == glyphs_.end()) {
        for (auto& b : bitmaps) stbtt_FreeSDF(b.sdf, nullptr);
        return fail("la fuente no tiene letras latinas");
    }

    // Empaquetado por estantes, de más alto a más bajo, en un atlas de 1024 de ancho.
    std::sort(bitmaps.begin(), bitmaps.end(), [](const Pending& a, const Pending& b) { return a.h > b.h; });
    width_ = 1024;
    int x = 1, y = 1, shelf = 0;
    struct Placed { int x, y; };
    std::vector<Placed> placed(bitmaps.size());
    for (size_t i = 0; i < bitmaps.size(); ++i) {
        if (x + bitmaps[i].w + 1 > width_) { x = 1; y += shelf + 1; shelf = 0; }
        placed[i] = {x, y};
        x += bitmaps[i].w + 1;
        shelf = std::max(shelf, bitmaps[i].h);
    }
    height_ = 64;
    while (height_ < y + shelf + 1) height_ *= 2;
    pixels_.assign(static_cast<size_t>(width_) * height_, 0);
    for (size_t i = 0; i < bitmaps.size(); ++i) {
        const Pending& b = bitmaps[i];
        for (int row = 0; row < b.h; ++row)
            std::memcpy(&pixels_[static_cast<size_t>(placed[i].y + row) * width_ + placed[i].x], b.sdf + row * b.w, b.w);
        Glyph& gl = glyphs_[b.cp];
        gl.u0 = static_cast<float>(placed[i].x) / width_;
        gl.v0 = static_cast<float>(placed[i].y) / height_;
        gl.u1 = static_cast<float>(placed[i].x + b.w) / width_;
        gl.v1 = static_cast<float>(placed[i].y + b.h) / height_;
        stbtt_FreeSDF(b.sdf, nullptr);
    }
    ok_ = true;
    return true;
}

const Glyph* FontAtlas::glyph(uint32_t cp) const {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return &it->second;
    it = glyphs_.find(0xFFFD);
    if (it == glyphs_.end()) it = glyphs_.find('?');
    return it == glyphs_.end() ? nullptr : &it->second;
}

float FontAtlas::kerning(uint32_t a, uint32_t b) const {
    if (!impl_) return 0;
    return stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(a), static_cast<int>(b)) * impl_->scale;
}

Vec2 FontAtlas::measure(std::string_view s, float size) const {
    float widest = 0, line = 0;
    int lines = 1;
    uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = decodeUtf8(s, i);
        if (cp == '\n') { widest = std::max(widest, line); line = 0; prev = 0; ++lines; continue; }
        const Glyph* g = glyph(cp);
        if (!g) continue;
        if (prev) line += kerning(prev, cp) * size;
        line += g->advance * size;
        prev = cp;
    }
    widest = std::max(widest, line);
    return {widest, (lines - 1) * lineHeight() * size + (ascent_ - descent_) * size};
}

void FontAtlas::layout(std::string_view s, float size, Vec2 pos, std::vector<GlyphQuad>& out, int align,
                       float boxWidth) const {
    float y = pos.y + ascent_ * size; // línea base
    size_t lineStart = 0;
    while (lineStart <= s.size()) {
        size_t lineEnd = s.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = s.size();
        const std::string_view line = s.substr(lineStart, lineEnd - lineStart);
        float x = pos.x;
        if (align != 0) {
            const float w = measure(line, size).x;
            x += align == 1 ? (boxWidth - w) * 0.5f : boxWidth - w;
        }
        uint32_t prev = 0;
        for (size_t i = 0; i < line.size();) {
            const uint32_t cp = decodeUtf8(line, i);
            const Glyph* g = glyph(cp);
            if (!g) continue;
            if (prev) x += kerning(prev, cp) * size;
            if (g->visible)
                out.push_back({{x + g->x0 * size, y + g->y0 * size, (g->x1 - g->x0) * size, (g->y1 - g->y0) * size},
                               {g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0}});
            x += g->advance * size;
            prev = cp;
        }
        y += lineHeight() * size;
        lineStart = lineEnd + 1;
    }
}

} // namespace titan
