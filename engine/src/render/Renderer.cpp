#include "titan/render/Renderer.h"

#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>

#include "stb_image.h"
#include "titan/core/FileSource.h"
#include "titan/core/Geometry.h"
#include "titan/core/Log.h"

namespace titan {

namespace {

// ---------------------------------------------------------------- Shaders

const char* kSpriteVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kSpriteFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 outColor;
void main() {
    outColor = texture(uTex, vUV) * vColor;
}
)";

// Iluminación Blinn-Phong: ambiente + 1 direccional + hasta 4 puntuales.
// Preparado para sustituirse por PBR sin cambiar la interfaz del renderizador.
const char* kMeshVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = uNormalMat * aNormal;
    vUV = aUV;
    gl_Position = uViewProj * w;
}
)";

const char* kMeshFS = R"(#version 300 es
precision highp float;
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform vec3 uAmbient;
uniform vec3 uDirDir;
uniform vec3 uDirColor;
uniform int uPointCount;
uniform vec4 uPointPosRange[4];
uniform vec3 uPointColor[4];
uniform vec3 uCameraPos;
uniform float uShininess;
uniform int uUnlit;
out vec4 outColor;

vec3 shade(vec3 n, vec3 v, vec3 l, vec3 color) {
    float d = max(dot(n, l), 0.0);
    float s = d > 0.0 ? pow(max(dot(n, normalize(l + v)), 0.0), uShininess) : 0.0;
    return color * (d + 0.35 * s);
}

void main() {
    vec4 base = texture(uTex, vUV) * uColor;
    if (uUnlit == 1) { outColor = base; return; }
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(uCameraPos - vWorld);
    vec3 light = uAmbient + shade(n, v, normalize(-uDirDir), uDirColor);
    for (int i = 0; i < 4; ++i) {
        if (i >= uPointCount) break;
        vec3 toLight = uPointPosRange[i].xyz - vWorld;
        float dist = length(toLight);
        float att = clamp(1.0 - dist / uPointPosRange[i].w, 0.0, 1.0);
        light += shade(n, v, toLight / max(dist, 1e-4), uPointColor[i]) * att * att;
    }
    outColor = vec4(base.rgb * light, base.a);
}
)";

constexpr int kFontCols = 16, kFontRows = 6;

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        TITAN_ERROR("render", "Error compilando shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        TITAN_ERROR("render", "Error enlazando shader: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

uint8_t toByte(float v) { return static_cast<uint8_t>(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f + 0.5f); }

} // namespace

// ---------------------------------------------------------------- Ciclo de vida

bool Renderer::init(FileSource& files) {
    files_ = &files;
    textures_.clear();
    textures_.push_back({}); // textura blanca
    vertices_.resize(kMaxQuads * 4);
    if (!createGpuObjects()) return false;
    font_ = loadTexture("engine/font.png", {false, true});
    TITAN_INFO("render", "OpenGL listo: %s | %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

bool Renderer::createGpuObjects() {
    spriteProgram_ = linkProgram(kSpriteVS, kSpriteFS);
    meshProgram_ = linkProgram(kMeshVS, kMeshFS);
    if (!spriteProgram_ || !meshProgram_) return false;
    uSpriteViewProj_ = glGetUniformLocation(spriteProgram_, "uViewProj");
    uSpriteTex_ = glGetUniformLocation(spriteProgram_, "uTex");
    uViewProj_ = glGetUniformLocation(meshProgram_, "uViewProj");
    uModel_ = glGetUniformLocation(meshProgram_, "uModel");
    uNormalMat_ = glGetUniformLocation(meshProgram_, "uNormalMat");
    uColor_ = glGetUniformLocation(meshProgram_, "uColor");
    uTex_ = glGetUniformLocation(meshProgram_, "uTex");
    uAmbient_ = glGetUniformLocation(meshProgram_, "uAmbient");
    uDirDir_ = glGetUniformLocation(meshProgram_, "uDirDir");
    uDirColor_ = glGetUniformLocation(meshProgram_, "uDirColor");
    uPointCount_ = glGetUniformLocation(meshProgram_, "uPointCount");
    uPointPosRange_ = glGetUniformLocation(meshProgram_, "uPointPosRange");
    uPointColor_ = glGetUniformLocation(meshProgram_, "uPointColor");
    uCameraPos_ = glGetUniformLocation(meshProgram_, "uCameraPos");
    uShininess_ = glGetUniformLocation(meshProgram_, "uShininess");
    uUnlit_ = glGetUniformLocation(meshProgram_, "uUnlit");

    // Sprites: búfer dinámico + índices fijos.
    glGenVertexArrays(1, &spriteVao_);
    glBindVertexArray(spriteVao_);
    glGenBuffers(1, &spriteVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, kMaxQuads * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    std::vector<uint16_t> indices(kMaxQuads * 6);
    for (int i = 0; i < kMaxQuads; ++i) {
        const uint16_t b = static_cast<uint16_t>(i * 4);
        uint16_t* q = &indices[i * 6];
        q[0] = b; q[1] = b + 1; q[2] = b + 2;
        q[3] = b; q[4] = b + 2; q[5] = b + 3;
    }
    glGenBuffers(1, &spriteIbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, spriteIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);
    auto spriteAttribs = [] {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    };
    spriteAttribs();

    // Líneas: mismo formato de vértice, sin índices.
    glGenVertexArrays(1, &lineVao_);
    glBindVertexArray(lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, kMaxLineVertices * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    spriteAttribs();
    glBindVertexArray(0);

    createMeshes();

    if (textures_.empty()) textures_.push_back({});
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

void Renderer::createMeshes() {
    for (int i = 0; i < 6; ++i) {
        const MeshData data = buildPrimitive(static_cast<MeshPrimitive>(i));
        GpuMesh& g = meshes_[i];
        glGenVertexArrays(1, &g.vao);
        glBindVertexArray(g.vao);
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(MeshVertex), data.vertices.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &g.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint16_t), data.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, uv));
        g.indexCount = static_cast<int>(data.indices.size());
    }
    glBindVertexArray(0);
}

bool Renderer::uploadTexture(Texture& t) {
    std::vector<uint8_t> bytes;
    if (!files_->read(t.path, bytes)) {
        TITAN_ERROR("render", "No se encontró la textura '%s'", t.path.c_str());
        return false;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        TITAN_ERROR("render", "No se pudo decodificar '%s': %s", t.path.c_str(), stbi_failure_reason());
        return false;
    }
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (w > maxSize || h > maxSize) {
        TITAN_ERROR("render", "'%s' mide %dx%d y esta GPU admite como máximo %d", t.path.c_str(), w, h, maxSize);
        stbi_image_free(pixels);
        return false;
    }
    glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    const GLint mag = t.options.nearest ? GL_NEAREST : GL_LINEAR;
    GLint min = mag;
    if (t.options.mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        min = t.options.nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    t.width = w;
    t.height = h;
    return true;
}

TextureHandle Renderer::loadTexture(const std::string& path, TextureOptions options) {
    for (TextureHandle i = 1; i < textures_.size(); ++i)
        if (textures_[i].path == path && textures_[i].options.nearest == options.nearest &&
            textures_[i].options.mipmaps == options.mipmaps)
            return i;
    Texture t;
    t.path = path;
    t.options = options;
    if (!uploadTexture(t)) return kWhiteTexture;
    textures_.push_back(t);
    TITAN_INFO("render", "Textura '%s' cargada (%dx%d)", path.c_str(), t.width, t.height);
    return static_cast<TextureHandle>(textures_.size() - 1);
}

Vec2 Renderer::textureSize(TextureHandle id) const {
    if (id >= textures_.size()) return {};
    return {static_cast<float>(textures_[id].width), static_cast<float>(textures_[id].height)};
}

void Renderer::onContextLost() {
    spriteProgram_ = meshProgram_ = spriteVao_ = spriteVbo_ = spriteIbo_ = lineVao_ = lineVbo_ = 0;
    for (auto& m : meshes_) m = {};
    for (auto& t : textures_) t.gl = 0;
    quadCount_ = 0;
    lines_.clear();
}

bool Renderer::onContextRestored() {
    if (!createGpuObjects()) return false;
    for (size_t i = 1; i < textures_.size(); ++i) uploadTexture(textures_[i]);
    TITAN_INFO("render", "Contexto de OpenGL restaurado (%zu texturas recargadas)", textures_.size() - 1);
    return true;
}

void Renderer::shutdown() {
    for (auto& t : textures_)
        if (t.gl) glDeleteTextures(1, &t.gl);
    textures_.clear();
    for (auto& m : meshes_) {
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        m = {};
    }
    for (uint32_t* b : {&spriteVbo_, &spriteIbo_, &lineVbo_})
        if (*b) glDeleteBuffers(1, b);
    for (uint32_t* v : {&spriteVao_, &lineVao_})
        if (*v) glDeleteVertexArrays(1, v);
    if (spriteProgram_) glDeleteProgram(spriteProgram_);
    if (meshProgram_) glDeleteProgram(meshProgram_);
    spriteProgram_ = meshProgram_ = spriteVao_ = spriteVbo_ = spriteIbo_ = lineVao_ = lineVbo_ = 0;
}

// ---------------------------------------------------------------- Fotograma

void Renderer::beginFrame(const Viewport& vp, Color clear) {
    viewport_ = vp;
    if (viewport_.width < 1) viewport_.width = 1;
    if (viewport_.height < 1) viewport_.height = 1;
    stats_ = {};
    glViewport(vp.x, vp.y, viewport_.width, viewport_.height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp.x, vp.y, viewport_.width, viewport_.height);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::setCamera(const Mat4& view, const Mat4& projection) {
    flushSprites();
    flushLines();
    viewProj_ = projection * view;
    cameraPos_ = view.inverted().translation();
}

void Renderer::setLights(const LightSetup& l) {
    glUseProgram(meshProgram_);
    glUniform3f(uAmbient_, l.ambient.r, l.ambient.g, l.ambient.b);
    const Vec3 d = l.directionalDir.normalized();
    glUniform3f(uDirDir_, d.x, d.y, d.z);
    glUniform3f(uDirColor_, l.directionalColor.r, l.directionalColor.g, l.directionalColor.b);
    float posRange[kMaxPointLights * 4] = {};
    float colors[kMaxPointLights * 3] = {};
    const int n = static_cast<int>(std::min<size_t>(l.points.size(), kMaxPointLights));
    for (int i = 0; i < n; ++i) {
        const PointLightData& p = l.points[i];
        posRange[i * 4] = p.position.x; posRange[i * 4 + 1] = p.position.y; posRange[i * 4 + 2] = p.position.z;
        posRange[i * 4 + 3] = std::max(p.range, 0.001f);
        colors[i * 3] = p.color.r; colors[i * 3 + 1] = p.color.g; colors[i * 3 + 2] = p.color.b;
    }
    glUniform1i(uPointCount_, n);
    glUniform4fv(uPointPosRange_, kMaxPointLights, posRange);
    glUniform3fv(uPointColor_, kMaxPointLights, colors);
}

void Renderer::drawMesh(MeshPrimitive type, const Mat4& world, Color color, TextureHandle tex, float shininess, bool unlit) {
    const GpuMesh& mesh = meshes_[static_cast<int>(type)];
    if (!mesh.vao) return;
    if (tex >= textures_.size()) tex = kWhiteTexture;
    // Matriz de normales: inversa traspuesta de la parte 3x3 (correcta también con escala no uniforme).
    const Mat4 inv = world.inverted();
    const float normalMat[9] = {inv.m[0], inv.m[4], inv.m[8], inv.m[1], inv.m[5], inv.m[9], inv.m[2], inv.m[6], inv.m[10]};
    glUseProgram(meshProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, world.m);
    glUniformMatrix3fv(uNormalMat_, 1, GL_FALSE, normalMat);
    glUniform4f(uColor_, color.r, color.g, color.b, color.a);
    glUniform3f(uCameraPos_, cameraPos_.x, cameraPos_.y, cameraPos_.z);
    glUniform1f(uShininess_, std::max(shininess, 1.0f));
    glUniform1i(uUnlit_, unlit ? 1 : 0);
    glUniform1i(uTex_, 0);
    glBindTexture(GL_TEXTURE_2D, textures_[tex].gl);
    // Una escala con determinante negativo invierte el sentido de los triángulos.
    const float det = world.m[0] * (world.m[5] * world.m[10] - world.m[9] * world.m[6]) -
                      world.m[4] * (world.m[1] * world.m[10] - world.m[9] * world.m[2]) +
                      world.m[8] * (world.m[1] * world.m[6] - world.m[5] * world.m[2]);
    glFrontFace(det < 0 ? GL_CW : GL_CCW);
    if (type == MeshPrimitive::Plane) glDisable(GL_CULL_FACE); // el plano se ve por las dos caras
    const bool transparent = color.a < 0.999f;
    if (transparent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
    if (transparent) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    stats_.drawCalls++;
    stats_.meshes++;
    stats_.triangles += mesh.indexCount / 3;
}

void Renderer::pushQuad(TextureHandle tex, const Vec3 c[4], Rect uv, Color color) {
    if (tex >= textures_.size()) tex = kWhiteTexture;
    if (quadCount_ > 0 && (tex != batchTexture_ || quadCount_ == kMaxQuads)) flushSprites();
    batchTexture_ = tex;
    const uint8_t r = toByte(color.r), g = toByte(color.g), b = toByte(color.b), a = toByte(color.a);
    const float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
    Vertex* v = &vertices_[quadCount_ * 4];
    v[0] = {c[0].x, c[0].y, c[0].z, u0, v0, r, g, b, a};
    v[1] = {c[1].x, c[1].y, c[1].z, u1, v0, r, g, b, a};
    v[2] = {c[2].x, c[2].y, c[2].z, u1, v1, r, g, b, a};
    v[3] = {c[3].x, c[3].y, c[3].z, u0, v1, r, g, b, a};
    ++quadCount_;
}

void Renderer::drawSprite(TextureHandle tex, const Mat4& world, Vec2 size, Vec2 pivot, Rect uv, Color color) {
    const float l = -pivot.x * size.x, r = (1.0f - pivot.x) * size.x;
    const float b = -pivot.y * size.y, t = (1.0f - pivot.y) * size.y;
    const Vec3 c[4] = {world.transformPoint({l, t, 0}), world.transformPoint({r, t, 0}),
                       world.transformPoint({r, b, 0}), world.transformPoint({l, b, 0})};
    pushQuad(tex, c, uv, color);
    stats_.sprites++;
}

void Renderer::flushSprites() {
    if (quadCount_ == 0) return;
    glUseProgram(spriteProgram_);
    glUniformMatrix4fv(uSpriteViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uSpriteTex_, 0);
    // Los sprites se ocultan detrás de las mallas, pero no se ocultan entre sí (se ordenan por sortingOrder).
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, textures_[batchTexture_].gl);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    // "Orphaning": pedir un búfer nuevo evita que la CPU espere a la GPU.
    glBufferData(GL_ARRAY_BUFFER, kMaxQuads * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, quadCount_ * 4 * sizeof(Vertex), vertices_.data());
    glDrawElements(GL_TRIANGLES, quadCount_ * 6, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    stats_.drawCalls++;
    stats_.triangles += quadCount_ * 2;
    quadCount_ = 0;
}

void Renderer::drawLine(const Vec3& a, const Vec3& b, Color color) {
    if (lines_.size() + 2 > static_cast<size_t>(kMaxLineVertices)) flushLines();
    const uint8_t r = toByte(color.r), g = toByte(color.g), bl = toByte(color.b), al = toByte(color.a);
    lines_.push_back({a.x, a.y, a.z, 0, 0, r, g, bl, al});
    lines_.push_back({b.x, b.y, b.z, 0, 0, r, g, bl, al});
}

void Renderer::flushLines() {
    if (lines_.empty()) return;
    glUseProgram(spriteProgram_);
    glUniformMatrix4fv(uSpriteViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uSpriteTex_, 0);
    glBindTexture(GL_TEXTURE_2D, textures_[kWhiteTexture].gl);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, kMaxLineVertices * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, lines_.size() * sizeof(Vertex), lines_.data());
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines_.size()));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    stats_.drawCalls++;
    lines_.clear();
}

void Renderer::drawText(Vec2 pos, float height, std::string_view text, Color color) {
    text_.push_back({pos, height, std::string(text), color});
}

void Renderer::endFrame() {
    flushSprites();
    flushLines();
    if (!text_.empty() && font_ != kWhiteTexture) {
        const Mat4 saved = viewProj_;
        viewProj_ = Mat4::ortho(0.0f, float(viewport_.width), float(viewport_.height), 0.0f, -1.0f, 1.0f);
        glDisable(GL_DEPTH_TEST);
        const float cw = 1.0f / kFontCols, ch = 1.0f / kFontRows;
        for (const auto& cmd : text_) {
            const float cellW = cmd.height * 0.5f;
            float x = cmd.pos.x, y = cmd.pos.y;
            for (unsigned char chr : cmd.text) {
                if (chr == '\n') { x = cmd.pos.x; y += cmd.height; continue; }
                if (chr < 32 || chr > 126) chr = '?';
                const int i = chr - 32;
                if (chr != ' ') {
                    const Rect uv{(i % kFontCols) * cw, (i / kFontCols) * ch, cw, ch};
                    const Vec3 c[4] = {{x, y, 0}, {x + cellW, y, 0}, {x + cellW, y + cmd.height, 0}, {x, y + cmd.height, 0}};
                    pushQuad(font_, c, uv, cmd.color);
                }
                x += cellW;
            }
        }
        flushSprites();
        viewProj_ = saved;
    }
    text_.clear();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    stats_.textures = static_cast<int>(textures_.size());
    lastStats_ = stats_;
}

} // namespace titan
