#include "forja/Renderer.h"

#include <GLES3/gl3.h>

#include <cmath>

#include "forja/Assets.h"
#include "forja/Log.h"
#include "stb_image.h"

namespace forja {

namespace {

const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 outColor;
void main() {
    outColor = texture(uTex, vUV) * vColor;
}
)";

// La fuente de depuración es una rejilla de 16x6 celdas con ASCII 32..127.
constexpr int kFontCols = 16, kFontRows = 6; // celdas de 1:2 (ancho = mitad del alto)

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        FORJA_ERROR("Error compilando shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f + 0.5f); }

} // namespace

bool Renderer::init(Assets& assets) {
    assets_ = &assets;
    textures_.clear();
    textures_.push_back({}); // textura blanca, id 0
    vertices_.resize(kMaxQuads * 4);
    if (!createGpuObjects()) return false;
    font_ = loadTexture("engine/font.png");
    FORJA_LOG("Renderer listo: %s | %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

bool Renderer::createGpuObjects() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        FORJA_ERROR("Error enlazando shader: %s", log);
        return false;
    }
    uProj_ = glGetUniformLocation(program_, "uProj");
    uTex_ = glGetUniformLocation(program_, "uTex");

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, kMaxQuads * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);

    std::vector<uint16_t> indices(kMaxQuads * 6);
    for (int i = 0; i < kMaxQuads; ++i) {
        const uint16_t b = static_cast<uint16_t>(i * 4);
        uint16_t* q = &indices[i * 6];
        q[0] = b; q[1] = b + 1; q[2] = b + 2;
        q[3] = b; q[4] = b + 2; q[5] = b + 3;
    }
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glBindVertexArray(0);

    // Textura blanca de 1x1.
    Texture& white = textures_[0];
    const uint32_t px = 0xFFFFFFFFu;
    glGenTextures(1, &white.gl);
    glBindTexture(GL_TEXTURE_2D, white.gl);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    white.width = white.height = 1;
    return true;
}

bool Renderer::uploadTexture(Texture& t) {
    std::vector<uint8_t> bytes;
    if (!assets_->read(t.path, bytes)) {
        FORJA_ERROR("No se encontró la textura '%s'", t.path.c_str());
        return false;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        FORJA_ERROR("No se pudo decodificar '%s': %s", t.path.c_str(), stbi_failure_reason());
        return false;
    }
    glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    if (t.pixelArt) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.width = w;
    t.height = h;
    return true;
}

TextureId Renderer::loadTexture(const std::string& path, bool pixelArt) {
    for (TextureId i = 1; i < textures_.size(); ++i)
        if (textures_[i].path == path && textures_[i].pixelArt == pixelArt) return i;
    Texture t;
    t.path = path;
    t.pixelArt = pixelArt;
    if (!uploadTexture(t)) return kWhiteTexture;
    textures_.push_back(t);
    FORJA_LOG("Textura '%s' cargada (%dx%d)", path.c_str(), t.width, t.height);
    return static_cast<TextureId>(textures_.size() - 1);
}

Vec2 Renderer::textureSize(TextureId id) const {
    if (id >= textures_.size()) return {};
    return {static_cast<float>(textures_[id].width), static_cast<float>(textures_[id].height)};
}

void Renderer::onContextLost() {
    // El contexto ya no existe: los identificadores de GL no valen nada, no se borran.
    program_ = vao_ = vbo_ = ibo_ = 0;
    for (auto& t : textures_) t.gl = 0;
    quadCount_ = 0;
}

bool Renderer::onContextRestored() {
    if (!createGpuObjects()) return false;
    for (size_t i = 1; i < textures_.size(); ++i) uploadTexture(textures_[i]);
    FORJA_LOG("Contexto de OpenGL restaurado (%zu texturas recargadas)", textures_.size() - 1);
    return true;
}

void Renderer::shutdown() {
    for (auto& t : textures_)
        if (t.gl) glDeleteTextures(1, &t.gl);
    textures_.clear();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    program_ = vao_ = vbo_ = ibo_ = 0;
}

void Renderer::beginFrame(int width, int height, Color clear) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    stats_ = {};
    glViewport(0, 0, width_, height_);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
}

void Renderer::setProjection(const Mat4& m) {
    flush();
    glUniformMatrix4fv(uProj_, 1, GL_FALSE, m.m);
}

void Renderer::pushQuad(TextureId tex, const Vec2 c[4], Rect uv, Color color) {
    if (tex >= textures_.size()) tex = kWhiteTexture;
    if (quadCount_ > 0 && (tex != batchTexture_ || quadCount_ == kMaxQuads)) flush();
    batchTexture_ = tex;
    const uint8_t r = toByte(color.r), g = toByte(color.g), b = toByte(color.b), a = toByte(color.a);
    const float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
    Vertex* v = &vertices_[quadCount_ * 4];
    v[0] = {c[0].x, c[0].y, u0, v0, r, g, b, a};
    v[1] = {c[1].x, c[1].y, u1, v0, r, g, b, a};
    v[2] = {c[2].x, c[2].y, u1, v1, r, g, b, a};
    v[3] = {c[3].x, c[3].y, u0, v1, r, g, b, a};
    ++quadCount_;
}

void Renderer::drawQuad(TextureId tex, Vec2 pos, Vec2 size, float rotation, Vec2 pivot, Rect uv, Color color) {
    // Esquinas locales: arriba-izq, arriba-der, abajo-der, abajo-izq (Y hacia arriba).
    const float l = -pivot.x * size.x, r = (1.0f - pivot.x) * size.x;
    const float b = -pivot.y * size.y, t = (1.0f - pivot.y) * size.y;
    Vec2 c[4] = {{l, t}, {r, t}, {r, b}, {l, b}};
    if (rotation != 0.0f) {
        const float cs = std::cos(rotation), sn = std::sin(rotation);
        for (auto& p : c) p = {p.x * cs - p.y * sn, p.x * sn + p.y * cs};
    }
    for (auto& p : c) p += pos;
    pushQuad(tex, c, uv, color);
}

void Renderer::drawText(Vec2 pos, float height, std::string_view text, Color color) {
    text_.push_back({pos, height, std::string(text), color});
}

void Renderer::flush() {
    if (quadCount_ == 0) return;
    glBindTexture(GL_TEXTURE_2D, textures_[batchTexture_].gl);
    // "Orphaning": pedir un búfer nuevo evita que la CPU espere a la GPU.
    glBufferData(GL_ARRAY_BUFFER, kMaxQuads * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, quadCount_ * 4 * sizeof(Vertex), vertices_.data());
    glDrawElements(GL_TRIANGLES, quadCount_ * 6, GL_UNSIGNED_SHORT, nullptr);
    stats_.drawCalls++;
    stats_.quads += quadCount_;
    quadCount_ = 0;
}

void Renderer::endFrame() {
    if (!text_.empty() && font_ != kWhiteTexture) {
        setProjection(Mat4::ortho(0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f));
        const float cw = 1.0f / kFontCols, ch = 1.0f / kFontRows;
        for (const auto& cmd : text_) {
            const float cellW = cmd.height * 0.5f;
            float x = cmd.pos.x;
            float y = cmd.pos.y;
            for (unsigned char chr : cmd.text) {
                if (chr == '\n') { x = cmd.pos.x; y += cmd.height; continue; }
                if (chr < 32 || chr > 127) chr = '?';
                const int i = chr - 32;
                if (chr != ' ') {
                    const Rect uv{(i % kFontCols) * cw, (i / kFontCols) * ch, cw, ch};
                    const Vec2 c[4] = {{x, y}, {x + cellW, y}, {x + cellW, y + cmd.height}, {x, y + cmd.height}};
                    pushQuad(font_, c, uv, cmd.color);
                }
                x += cellW;
            }
        }
    }
    text_.clear();
    flush();
    glBindVertexArray(0);
    lastStats_ = stats_;
}

} // namespace forja
