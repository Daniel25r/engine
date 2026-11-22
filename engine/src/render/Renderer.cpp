#include "titan/render/Renderer.h"

#include "PostProcess.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

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
uniform int uSdf;
out vec4 outColor;
void main() {
    if (uSdf == 1) {
        // Texto: el canal rojo es la distancia al borde de la letra (0,5 = borde).
        float d = texture(uTex, vUV).r;
        float w = max(fwidth(d) * 0.7, 0.002);
        outColor = vec4(vColor.rgb, vColor.a * smoothstep(0.5 - w, 0.5 + w, d));
    } else {
        outColor = texture(uTex, vUV) * vColor;
    }
}
)";

// Mallas: PBR metálico/rugosidad (GGX + Smith + Schlick), sol con sombras PCF, 4 luces puntuales,
// ambiente de cielo hemisférico, niebla exponencial y tonemapping ACES. Los colores de entrada son sRGB.
const char* kMeshVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec4 aSplat; // terreno: peso de cada capa
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat4 uLightVP;
uniform int uSkinned;
uniform vec4 uUVTransform; // xy = repetición, zw = desplazamiento
uniform highp sampler2D uJointTex; // 4 texels RGBA32F por hueso (columnas de su matriz)
mat4 jointMatrix(float i) {
    int r = int(i);
    return mat4(texelFetch(uJointTex, ivec2(0, r), 0), texelFetch(uJointTex, ivec2(1, r), 0),
                texelFetch(uJointTex, ivec2(2, r), 0), texelFetch(uJointTex, ivec2(3, r), 0));
}
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec4 vShadow;
out vec4 vSplat;
void main() {
    vSplat = aSplat;
    vec4 local = vec4(aPos, 1.0);
    vec3 normal = aNormal;
    if (uSkinned == 1) {
        // Skinning: cada vértice sigue a hasta 4 huesos según sus pesos.
        mat4 skin = aWeights.x * jointMatrix(aJoints.x) + aWeights.y * jointMatrix(aJoints.y) +
                    aWeights.z * jointMatrix(aJoints.z) + aWeights.w * jointMatrix(aJoints.w);
        local = skin * local;
        normal = mat3(skin) * normal;
    }
    vec4 w = uModel * local;
    vWorld = w.xyz;
    vNormal = normalize(uNormalMat * normal);
    vUV = aUV * uUVTransform.xy + uUVTransform.zw;
    // Desplazar el punto un poco por la normal evita el "acné" de sombras.
    vShadow = uLightVP * vec4(w.xyz + vNormal * 0.04, 1.0);
    gl_Position = uViewProj * w;
}
)";

const char* kMeshFS = R"(#version 300 es
precision highp float;
precision highp sampler2DShadow;
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec4 vShadow;
in vec4 vSplat;
uniform int uTerrain;
uniform sampler2D uLayer0;
uniform sampler2D uLayer1;
uniform sampler2D uLayer2;
uniform sampler2D uLayer3;
uniform vec3 uLayerColor[4];
uniform float uLayerTiling;
uniform sampler2D uTex;
uniform sampler2D uNormalTex;
uniform sampler2D uMRTex;
uniform sampler2D uEmissiveTex;
uniform sampler2D uOcclusionTex;
uniform int uHasNormal;
uniform int uHasMR;
uniform int uHasOcclusion;
uniform float uNormalStrength;
uniform float uAlphaCutoff; // < 0: sin recorte
uniform sampler2DShadow uShadowMap;
uniform vec4 uColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmission;
uniform int uGrid;
uniform int uUnlit;
uniform vec3 uDirDir;
uniform vec3 uDirColor;
uniform int uPointCount;
uniform vec4 uPointPosRange[8];
uniform vec3 uPointColor[8];
uniform vec4 uSpot[8]; // xyz = dirección del foco, w = coseno del borde exterior (-2 = luz puntual)
uniform float uSpotInner[8]; // coseno donde empieza a difuminarse
uniform vec3 uCameraPos;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uGround;
uniform float uAmbientI;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uExposure;
uniform int uShadowsOn;
out vec4 outColor;

const float PI = 3.14159265;
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

vec3 brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float metal, float rough, vec3 radiance) {
    float ndl = dot(n, l);
    if (ndl <= 0.0) return vec3(0.0);
    vec3 h = normalize(l + v);
    float ndv = max(dot(n, v), 1e-4), ndh = max(dot(n, h), 0.0), vdh = max(dot(v, h), 0.0);
    float a = max(rough * rough, 0.002);
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * d * d);
    float k = a * 0.5;
    float V = 0.25 / ((ndv * (1.0 - k) + k) * (ndl * (1.0 - k) + k));
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0);
    vec3 kd = (1.0 - F) * (1.0 - metal);
    return (kd * albedo / PI + D * V * F) * radiance * ndl;
}

float shadowFactor() {
    if (uShadowsOn == 0) return 1.0;
    vec3 p = vShadow.xyz / vShadow.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float texel = 1.0 / float(textureSize(uShadowMap, 0).x);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            sum += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - 0.0008));
    return sum / 9.0;
}

float gridLines(vec3 n) {
    // Rejilla de prototipado alineada al mundo: líneas cada metro y más marcadas cada 5.
    vec3 an = abs(n);
    vec2 uv = an.y > an.x && an.y > an.z ? vWorld.xz : (an.x > an.z ? vWorld.zy : vWorld.xy);
    vec2 g1 = abs(fract(uv - 0.5) - 0.5) / fwidth(uv);
    vec2 g5 = abs(fract(uv / 5.0 - 0.5) - 0.5) / fwidth(uv / 5.0);
    float l1 = 1.0 - min(min(g1.x, g1.y), 1.0);
    float l5 = 1.0 - min(min(g5.x, g5.y), 1.0);
    return max(l1 * 0.18, l5 * 0.35);
}

// Base tangente calculada por píxel con derivadas (Schüler, "Normal mapping without precomputed tangents"):
// sirve para cualquier malla con UV, sin tener que generar ni guardar tangentes.
mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
    return mat3(t * invmax, b * invmax, n);
}

void main() {
    vec4 tex = texture(uTex, vUV);
    vec3 albedo = toLinear(tex.rgb * uColor.rgb);
    float alpha = tex.a * uColor.a;
    if (uTerrain == 1) {
        // Terreno: mezcla de 4 capas pintadas; las texturas se repiten en el plano XZ del mundo.
        vec2 wuv = vWorld.xz / max(uLayerTiling, 0.01);
        vec4 w = vSplat / max(vSplat.x + vSplat.y + vSplat.z + vSplat.w, 1e-4);
        vec3 c = texture(uLayer0, wuv).rgb * uLayerColor[0] * w.x + texture(uLayer1, wuv).rgb * uLayerColor[1] * w.y +
                 texture(uLayer2, wuv).rgb * uLayerColor[2] * w.z + texture(uLayer3, wuv).rgb * uLayerColor[3] * w.w;
        albedo = toLinear(c);
        alpha = 1.0;
    }
    if (uAlphaCutoff >= 0.0) {
        if (alpha < uAlphaCutoff) discard;
        alpha = 1.0;
    }
    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    if (uHasNormal == 1) {
        vec3 tn = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
        tn.xy *= uNormalStrength;
        n = normalize(cotangentFrame(n, vWorld, vUV) * tn);
    }
    if (uGrid == 1) albedo *= 1.0 - gridLines(n);
    if (uUnlit == 1) { outColor = vec4(pow(albedo, vec3(1.0 / 2.2)), alpha); return; }

    float metal = uMetallic, rough = uRoughness;
    if (uHasMR == 1) {
        vec4 mr = texture(uMRTex, vUV);
        rough *= mr.g;
        metal *= mr.b;
    }
    float ao = uHasOcclusion == 1 ? texture(uOcclusionTex, vUV).r : 1.0;
    vec3 v = normalize(uCameraPos - vWorld);
    rough = clamp(rough, 0.04, 1.0);
    vec3 color = brdf(n, v, normalize(-uDirDir), albedo, metal, rough, uDirColor * 2.4) * shadowFactor();
    for (int i = 0; i < 8; ++i) {
        if (i >= uPointCount) break;
        vec3 toLight = uPointPosRange[i].xyz - vWorld;
        float dist = length(toLight);
        vec3 l = toLight / max(dist, 1e-4);
        float att = clamp(1.0 - dist / uPointPosRange[i].w, 0.0, 1.0);
        att *= att;
        if (uSpot[i].w > -1.5) // foco: cono con borde suave
            att *= smoothstep(uSpot[i].w, uSpotInner[i], dot(-l, uSpot[i].xyz));
        color += brdf(n, v, l, albedo, metal, rough, uPointColor[i] * 3.0 * att);
    }
    // Ambiente: cielo arriba, suelo abajo; reflejo especular aproximado del cielo.
    vec3 sky = toLinear(uSkyZenith), horizon = toLinear(uSkyHorizon), ground = toLinear(uGround);
    vec3 hemi = mix(ground, mix(horizon, sky, max(n.y, 0.0)), n.y * 0.5 + 0.5);
    hemi = mix(vec3(dot(hemi, vec3(0.3, 0.59, 0.11))), hemi, 0.45); // luz ambiental menos saturada
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    float ndv = max(dot(n, v), 0.0);
    vec3 F = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - ndv, 5.0);
    vec3 r = reflect(-v, n);
    vec3 env = mix(ground, mix(horizon, sky, max(r.y, 0.0)), r.y * 0.5 + 0.5);
    color += (hemi * albedo * (1.0 - metal) * (1.0 - F) * 0.45 + env * F * (1.0 - rough * 0.7)) * uAmbientI * ao;
    color += toLinear(uEmission * texture(uEmissiveTex, vUV).rgb) * 2.0;

    color = pow(aces(color * uExposure), vec3(1.0 / 2.2));
    float fog = 1.0 - exp(-uFogDensity * length(uCameraPos - vWorld));
    outColor = vec4(mix(color, uFogColor, clamp(fog, 0.0, 1.0)), alpha);
}
)";

const char* kShadowVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
uniform mat4 uLightVP;
uniform mat4 uModel;
uniform int uSkinned;
uniform highp sampler2D uJointTex; // 4 texels RGBA32F por hueso (columnas de su matriz)
mat4 jointMatrix(float i) {
    int r = int(i);
    return mat4(texelFetch(uJointTex, ivec2(0, r), 0), texelFetch(uJointTex, ivec2(1, r), 0),
                texelFetch(uJointTex, ivec2(2, r), 0), texelFetch(uJointTex, ivec2(3, r), 0));
}
void main() {
    vec4 local = vec4(aPos, 1.0);
    if (uSkinned == 1)
        local = (aWeights.x * jointMatrix(aJoints.x) + aWeights.y * jointMatrix(aJoints.y) +
                 aWeights.z * jointMatrix(aJoints.z) + aWeights.w * jointMatrix(aJoints.w)) * local;
    gl_Position = uLightVP * uModel * local;
}
)";

const char* kShadowFS = R"(#version 300 es
precision mediump float;
void main() {}
)";

// Cielo: triángulo a pantalla completa; el color depende de la dirección de la vista.
const char* kSkyVS = R"(#version 300 es
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
)";

const char* kSkyFS = R"(#version 300 es
precision highp float;
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uCam;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGroundC;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
out vec4 outColor;
void main() {
    vec4 far = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - uCam);
    vec3 c = dir.y > 0.0 ? mix(uHorizon, uZenith, pow(dir.y, 0.55)) : mix(uHorizon, uGroundC, pow(-dir.y, 0.35));
    float s = max(dot(dir, -uSunDir), 0.0);
    c += uSunColor * (pow(s, 900.0) * 3.0 + pow(s, 12.0) * 0.18);
    outColor = vec4(c, 1.0);
}
)";


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

Renderer::Renderer() : post_(std::make_unique<PostProcessor>()) {}
Renderer::~Renderer() = default;

void Renderer::setQuality(const QualitySettings& q) {
    quality_ = q;
    quality_.renderScale = std::fmin(std::fmax(q.renderScale, 0.25f), 1.0f);
    int size = std::max(256, std::min(q.shadowMapSize, caps_.maxTextureSize > 0 ? caps_.maxTextureSize : 2048));
    if (size != shadowSize_) {
        shadowSize_ = size;
        if (shadowFbo_) createShadowMap(); // ya hay contexto: se recrea ahora
    }
    TITAN_INFO("render", "Calidad '%s': escala %.0f %%, sombras %s %d, FXAA %s, bloom %s, %d luces",
               qualityName(quality_.level), quality_.renderScale * 100, quality_.shadows ? "sí" : "no", shadowSize_,
               quality_.fxaa ? "sí" : "no", quality_.bloom ? "permitido" : "no", quality_.maxPointLights);
}

bool Renderer::init(FileSource& files) {
    files_ = &files;
    textures_.clear();
    textures_.push_back({}); // textura blanca
    vertices_.resize(kMaxQuads * 4);
    // Capacidades medibles de la GPU (para recomendar un perfil de calidad).
    auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return s ? std::string(reinterpret_cast<const char*>(s)) : std::string(); };
    caps_.vendor = str(GL_VENDOR);
    caps_.renderer = str(GL_RENDERER);
    caps_.version = str(GL_VERSION);
    glGetIntegerv(GL_MAJOR_VERSION, &caps_.glesMajor);
    glGetIntegerv(GL_MINOR_VERSION, &caps_.glesMinor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps_.maxTextureSize);
    GLint extCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
    for (GLint i = 0; i < extCount; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!e) continue;
        if (!std::strcmp(e, "GL_KHR_texture_compression_astc_ldr")) caps_.astc = true;
        if (!std::strcmp(e, "GL_EXT_color_buffer_half_float") || !std::strcmp(e, "GL_EXT_color_buffer_float"))
            caps_.halfFloatTargets = true;
    }
    caps_.memoryMB = systemMemoryMB();
    shadowSize_ = std::min(std::max(256, quality_.shadowMapSize), caps_.maxTextureSize);
    if (!createGpuObjects()) return false;
    fonts_.clear();
    if (loadFont("engine/fonts/Outfit-Regular.ttf") != 0) TITAN_ERROR("render", "No se pudo cargar la fuente del motor");
    // La negrita se prepara la primera vez que se usa (crear el atlas cuesta unas décimas de segundo).
    // Disco blanco con borde suavizado: círculos y esquinas redondeadas de la interfaz.
    {
        constexpr int N = 128;
        std::vector<uint8_t> px(N * N * 4, 255);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float dx = x + 0.5f - N * 0.5f, dy = y + 0.5f - N * 0.5f;
                const float a = std::fmin(std::fmax(N * 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f), 1.0f);
                px[(y * N + x) * 4 + 3] = static_cast<uint8_t>(a * 255.0f + 0.5f);
            }
        disc_ = createTexture(N, N, 4, px.data());
        // Punto suave (partículas): blanco con alfa que cae como una campana desde el centro.
        constexpr int S = 64;
        std::vector<uint8_t> dot(S * S * 4, 255);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x) {
                const float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
                const float d = std::fmin(1.0f, std::sqrt(dx * dx + dy * dy));
                const float a = (1 - d) * (1 - d);
                dot[(y * S + x) * 4 + 3] = static_cast<uint8_t>(a * 255.0f + 0.5f);
            }
        softDot_ = createTexture(S, S, 4, dot.data());
    }
    TITAN_INFO("render", "OpenGL listo: %s | %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

void Renderer::createShadowMap() {
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    shadowFbo_ = shadowTex_ = 0;
    // Mapa de sombras: textura de profundidad con comparación por hardware (PCF 2x2 gratis).
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) TITAN_WARN("render", "Mapa de sombras incompleto");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

bool Renderer::createGpuObjects() {
    spriteProgram_ = linkProgram(kSpriteVS, kSpriteFS);
    meshProgram_ = linkProgram(kMeshVS, kMeshFS);
    if (!spriteProgram_ || !meshProgram_) return false;
    uSpriteViewProj_ = glGetUniformLocation(spriteProgram_, "uViewProj");
    uSpriteTex_ = glGetUniformLocation(spriteProgram_, "uTex");
    uSpriteSdf_ = glGetUniformLocation(spriteProgram_, "uSdf");
    shadowProgram_ = linkProgram(kShadowVS, kShadowFS);
    skyProgram_ = linkProgram(kSkyVS, kSkyFS);
    if (!shadowProgram_ || !skyProgram_) return false;
    auto u = [this](const char* n) { return glGetUniformLocation(meshProgram_, n); };
    uViewProj_ = u("uViewProj"); uModel_ = u("uModel"); uNormalMat_ = u("uNormalMat"); uColor_ = u("uColor");
    uTex_ = u("uTex"); uDirDir_ = u("uDirDir"); uDirColor_ = u("uDirColor"); uPointCount_ = u("uPointCount");
    uPointPosRange_ = u("uPointPosRange"); uPointColor_ = u("uPointColor"); uCameraPos_ = u("uCameraPos");
    uUnlit_ = u("uUnlit"); uMetallic_ = u("uMetallic"); uRoughness_ = u("uRoughness"); uEmission_ = u("uEmission");
    uGrid_ = u("uGrid"); uSkyZenith_ = u("uSkyZenith"); uSkyHorizon_ = u("uSkyHorizon"); uGround_ = u("uGround");
    uAmbientI_ = u("uAmbientI"); uFogColor_ = u("uFogColor"); uFogDensity_ = u("uFogDensity");
    uExposure_ = u("uExposure"); uLightVP_ = u("uLightVP"); uShadowMap_ = u("uShadowMap"); uShadowsOn_ = u("uShadowsOn");
    uSkinned_ = u("uSkinned"); uJoints_ = u("uJointTex");
    uUVTransform_ = u("uUVTransform"); uNormalTex_ = u("uNormalTex"); uMRTex_ = u("uMRTex");
    uEmissiveTex_ = u("uEmissiveTex"); uOcclusionTex_ = u("uOcclusionTex"); uHasNormal_ = u("uHasNormal");
    uHasMR_ = u("uHasMR"); uHasOcclusion_ = u("uHasOcclusion"); uNormalStrength_ = u("uNormalStrength");
    uAlphaCutoff_ = u("uAlphaCutoff"); uSpot_ = u("uSpot"); uSpotInner_ = u("uSpotInner");
    uTerrain_ = u("uTerrain"); uLayerColor_ = u("uLayerColor"); uLayerTiling_ = u("uLayerTiling");
    for (int i = 0; i < 4; ++i) uLayers_[i] = u(("uLayer" + std::to_string(i)).c_str());
    uShadowSkinned_ = glGetUniformLocation(shadowProgram_, "uSkinned");
    uShadowJoints_ = glGetUniformLocation(shadowProgram_, "uJointTex");
    // Textura de huesos: 4 x kMaxJoints texels RGBA32F, leída con texelFetch (sin filtrado).
    glGenTextures(1, &jointTex_);
    glBindTexture(GL_TEXTURE_2D, jointTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, kMaxJoints, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    uShadowLVP_ = glGetUniformLocation(shadowProgram_, "uLightVP");
    uShadowModel_ = glGetUniformLocation(shadowProgram_, "uModel");
    uSkyInvVP_ = glGetUniformLocation(skyProgram_, "uInvViewProj");
    uSkyCam_ = glGetUniformLocation(skyProgram_, "uCam");
    uSkyZ_ = glGetUniformLocation(skyProgram_, "uZenith");
    uSkyH_ = glGetUniformLocation(skyProgram_, "uHorizon");
    uSkyG_ = glGetUniformLocation(skyProgram_, "uGroundC");
    uSkySun_ = glGetUniformLocation(skyProgram_, "uSunDir");
    uSkySunColor_ = glGetUniformLocation(skyProgram_, "uSunColor");
    glGenVertexArrays(1, &skyVao_);

    createShadowMap();

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

bool Renderer::uploadMemoryTexture(Texture& t) {
    if (t.memory.empty()) return false;
    glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (t.channels == 1)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, t.width, t.height, 0, GL_RED, GL_UNSIGNED_BYTE, t.memory.data());
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.memory.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

TextureHandle Renderer::createTexture(int width, int height, int channels, const uint8_t* pixels, bool sdf) {
    Texture t;
    t.width = width;
    t.height = height;
    t.channels = channels == 1 ? 1 : 4;
    t.sdf = sdf;
    t.options.mipmaps = false;
    t.memory.assign(pixels, pixels + static_cast<size_t>(width) * height * t.channels);
    if (!uploadMemoryTexture(t)) return kWhiteTexture;
    textures_.push_back(std::move(t));
    return static_cast<TextureHandle>(textures_.size() - 1);
}

int Renderer::loadFont(const std::string& path) {
    for (size_t i = 0; i < fonts_.size(); ++i)
        if (fonts_[i].path == path) return fonts_[i].atlas ? static_cast<int>(i) : -1;
    LoadedFont f;
    f.path = path;
    std::vector<uint8_t> bytes;
    std::string error;
    auto atlas = std::make_unique<FontAtlas>();
    if (!files_->read(path, bytes)) {
        TITAN_ERROR("render", "No se encontró la fuente '%s'", path.c_str());
    } else if (!atlas->build(std::move(bytes), &error)) {
        TITAN_ERROR("render", "Fuente '%s': %s", path.c_str(), error.c_str());
    } else {
        f.texture = createTexture(atlas->width(), atlas->height(), 1, atlas->pixels().data(), true);
        TITAN_INFO("render", "Fuente '%s' cargada (%zu caracteres, atlas %dx%d)", path.c_str(), atlas->glyphCount(),
                   atlas->width(), atlas->height());
        f.atlas = std::move(atlas);
    }
    fonts_.push_back(std::move(f));
    return fonts_.back().atlas ? static_cast<int>(fonts_.size() - 1) : -1;
}

const FontAtlas* Renderer::font(int f) {
    if (f == 1 && fonts_.size() == 1) loadFont("engine/fonts/Outfit-Bold.ttf");
    if (f < 0 || f >= static_cast<int>(fonts_.size()) || !fonts_[f].atlas) f = 0;
    return f < static_cast<int>(fonts_.size()) ? fonts_[f].atlas.get() : nullptr;
}

Vec2 Renderer::measureText(int f, float size, std::string_view text) {
    const FontAtlas* a = font(f);
    return a ? a->measure(text, size) : Vec2{};
}

void Renderer::resolveScene() {
    if (!sceneInTarget_) return;
    flushSprites();
    flushLines();
    sceneInTarget_ = false;
    const Environment& e = lighting_.env;
    PostSettings s;
    s.fxaa = quality_.fxaa;
    s.bloom = quality_.bloom && e.bloom;
    s.bloomIntensity = e.bloomIntensity;
    s.bloomThreshold = e.bloomThreshold;
    s.vignette = e.vignette;
    s.contrast = e.contrast;
    s.saturation = e.saturation;
    s.temperature = e.temperature;
    post_->resolve(static_cast<uint32_t>(targetFbo_), viewport_.x, viewport_.y, viewport_.width, viewport_.height, s);
    stats_.postPasses = post_->lastPasses();
    glUseProgram(0);
}

void Renderer::beginOverlay() {
    if (inOverlay_) return;
    flushSprites();
    flushLines();
    resolveScene(); // la interfaz va encima de la imagen final y a resolución completa
    overlaySavedViewProj_ = viewProj_;
    viewProj_ = Mat4::ortho(0.0f, float(viewport_.width), float(viewport_.height), 0.0f, -1.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    inOverlay_ = true;
}

void Renderer::endOverlay() {
    if (!inOverlay_) return;
    flushSprites();
    viewProj_ = overlaySavedViewProj_;
    glEnable(GL_DEPTH_TEST);
    inOverlay_ = false;
}

void Renderer::drawRect(TextureHandle tex, const Rect& r, const Rect& uv, Color color) {
    if (batchAdditive_) flushSprites();
    const Vec3 c[4] = {{r.x, r.y, 0}, {r.x + r.w, r.y, 0}, {r.x + r.w, r.y + r.h, 0}, {r.x, r.y + r.h, 0}};
    pushQuad(tex, c, uv, color);
}

void Renderer::drawRoundedRect(const Rect& r, float radius, Color color) {
    radius = std::fmin(radius, std::fmin(r.w, r.h) * 0.5f);
    if (radius < 0.5f) {
        drawRect(kWhiteTexture, r, {0, 0, 1, 1}, color);
        return;
    }
    // Nueve partes: las esquinas son cuartos del disco; los bordes y el centro, su parte central.
    const float xs[4] = {r.x, r.x + radius, r.x + r.w - radius, r.x + r.w};
    const float ys[4] = {r.y, r.y + radius, r.y + r.h - radius, r.y + r.h};
    const float us[4] = {0.0f, 0.5f, 0.5f, 1.0f};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            const Rect part{xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]};
            if (part.w <= 0 || part.h <= 0) continue;
            drawRect(disc_, part, {us[i], us[j], us[i + 1] - us[i], us[j + 1] - us[j]}, color);
        }
}

void Renderer::drawTextNow(int f, Vec2 pos, float size, std::string_view text, Color color, int align, float boxWidth) {
    if (f == 1 && fonts_.size() == 1) loadFont("engine/fonts/Outfit-Bold.ttf");
    if (f < 0 || f >= static_cast<int>(fonts_.size()) || !fonts_[f].atlas) f = 0;
    if (fonts_.empty() || !fonts_[f].atlas) return;
    glyphScratch_.clear();
    fonts_[f].atlas->layout(text, size, pos, glyphScratch_, align, boxWidth);
    for (const GlyphQuad& q : glyphScratch_) drawRect(fonts_[f].texture, q.rect, q.uv, color);
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
    post_->onContextLost();
    for (TerrainGpu& t : terrains_) t = {t.key, 0, {}, t.lastUsed}; // se vuelve a subir en el siguiente dibujo
    spriteProgram_ = meshProgram_ = spriteVao_ = spriteVbo_ = spriteIbo_ = lineVao_ = lineVbo_ = 0;
    shadowProgram_ = shadowFbo_ = shadowTex_ = skyProgram_ = skyVao_ = jointTex_ = 0;
    for (auto& m : meshes_) m = {};
    for (auto& t : textures_) t.gl = 0;
    for (auto& m : models_) m.parts.clear();
    quadCount_ = 0;
    lines_.clear();
}

bool Renderer::onContextRestored() {
    if (!createGpuObjects()) return false;
    for (size_t i = 1; i < textures_.size(); ++i) {
        if (!textures_[i].path.empty()) uploadTexture(textures_[i]);
        else if (!textures_[i].memory.empty()) uploadMemoryTexture(textures_[i]);
    }
    for (Model& m : models_) buildModel(m); // las texturas incrustadas se vuelven a crear con su modelo
    TITAN_INFO("render", "Contexto de OpenGL restaurado (%zu texturas recargadas)", textures_.size() - 1);
    return true;
}

void Renderer::shutdown() {
    fonts_.clear();
    for (TerrainGpu& t : terrains_) {
        if (t.mesh.vbo) glDeleteBuffers(1, &t.mesh.vbo);
        if (t.mesh.ibo) glDeleteBuffers(1, &t.mesh.ibo);
        if (t.mesh.vao) glDeleteVertexArrays(1, &t.mesh.vao);
    }
    terrains_.clear();
    post_->shutdown();
    for (auto& t : textures_)
        if (t.gl) glDeleteTextures(1, &t.gl);
    textures_.clear();
    for (auto& model : models_)
        for (auto& part : model.parts) {
            glDeleteBuffers(1, &part.mesh.vbo);
            if (part.skinVbo) glDeleteBuffers(1, &part.skinVbo);
            glDeleteBuffers(1, &part.mesh.ibo);
            glDeleteVertexArrays(1, &part.mesh.vao);
        }
    models_.clear();
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
    if (shadowProgram_) glDeleteProgram(shadowProgram_);
    if (skyProgram_) glDeleteProgram(skyProgram_);
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (skyVao_) glDeleteVertexArrays(1, &skyVao_);
    if (jointTex_) glDeleteTextures(1, &jointTex_);
    spriteProgram_ = meshProgram_ = spriteVao_ = spriteVbo_ = spriteIbo_ = lineVao_ = lineVbo_ = 0;
    shadowProgram_ = shadowFbo_ = shadowTex_ = skyProgram_ = skyVao_ = 0;
}

// ---------------------------------------------------------------- Fotograma

void Renderer::beginFrame(const Viewport& vp, Color clear) {
    viewport_ = vp;
    if (viewport_.width < 1) viewport_.width = 1;
    if (viewport_.height < 1) viewport_.height = 1;
    stats_ = {};
    stats_.drawCalls = stats_.shadowDrawCalls = shadowDrawCalls_;
    shadowDrawCalls_ = 0;
    // Con posprocesado la escena se dibuja en un render target (a la escala del perfil) y se compone al final.
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &targetFbo_);
    sceneInTarget_ = false;
    if (quality_.postProcess) {
        const int sw = std::max(1, static_cast<int>(viewport_.width * quality_.renderScale + 0.5f));
        const int sh = std::max(1, static_cast<int>(viewport_.height * quality_.renderScale + 0.5f));
        sceneInTarget_ = post_->begin(sw, sh);
        if (!sceneInTarget_) glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(targetFbo_));
    }
    if (sceneInTarget_) {
        stats_.sceneWidth = post_->width();
        stats_.sceneHeight = post_->height();
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, post_->width(), post_->height());
    } else {
        stats_.sceneWidth = viewport_.width;
        stats_.sceneHeight = viewport_.height;
        glViewport(vp.x, vp.y, viewport_.width, viewport_.height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vp.x, vp.y, viewport_.width, viewport_.height);
    }
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
    const Mat4 camWorld = view.inverted();
    cameraPos_ = camWorld.translation();
    camRight_ = camWorld.transformDirection({1, 0, 0}).normalized();
    camUp_ = camWorld.transformDirection({0, 1, 0}).normalized();
}

void Renderer::beginShadowPass(const Mat4& lightVP) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
    shadowDrawStart_ = stats_.drawCalls;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE); // las caras traseras también proyectan sombra (planos, objetos abiertos)
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUseProgram(shadowProgram_);
    glUniformMatrix4fv(uShadowLVP_, 1, GL_FALSE, lightVP.m);
}

void Renderer::drawShadowCaster(MeshPrimitive type, const Mat4& world) {
    const GpuMesh& mesh = meshes_[static_cast<int>(type)];
    if (!mesh.vao) return;
    glUniformMatrix4fv(uShadowModel_, 1, GL_FALSE, world.m);
    glUniform1i(uShadowSkinned_, 0);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
    stats_.drawCalls++;
}

void Renderer::endShadowPass() {
    shadowDrawCalls_ += stats_.drawCalls - shadowDrawStart_;
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
}

void Renderer::setLighting(const SceneLighting& l) {
    lighting_ = l;
    const Environment& e = l.env;
    glUseProgram(meshProgram_);
    const Vec3 d = l.sunDirection.normalized();
    glUniform3f(uDirDir_, d.x, d.y, d.z);
    glUniform3f(uDirColor_, l.sunColor.r, l.sunColor.g, l.sunColor.b);
    float posRange[kMaxPointLights * 4] = {};
    float colors[kMaxPointLights * 3] = {};
    float spot[kMaxPointLights * 4] = {};
    float spotInner[kMaxPointLights] = {};
    const int n = static_cast<int>(std::min<size_t>(l.points.size(), kMaxPointLights));
    for (int i = 0; i < n; ++i) {
        const PointLightData& p = l.points[i];
        posRange[i * 4] = p.position.x; posRange[i * 4 + 1] = p.position.y; posRange[i * 4 + 2] = p.position.z;
        posRange[i * 4 + 3] = std::max(p.range, 0.001f);
        colors[i * 3] = p.color.r; colors[i * 3 + 1] = p.color.g; colors[i * 3 + 2] = p.color.b;
        const Vec3 d = p.direction.normalized();
        spot[i * 4] = d.x; spot[i * 4 + 1] = d.y; spot[i * 4 + 2] = d.z;
        spot[i * 4 + 3] = p.spot ? p.cosOuter : -2.0f;
        spotInner[i] = p.spot ? std::max(p.cosInner, p.cosOuter + 1e-4f) : 1.0f;
    }
    glUniform1i(uPointCount_, n);
    glUniform4fv(uPointPosRange_, kMaxPointLights, posRange);
    glUniform3fv(uPointColor_, kMaxPointLights, colors);
    glUniform4fv(uSpot_, kMaxPointLights, spot);
    glUniform1fv(uSpotInner_, kMaxPointLights, spotInner);
    glUniform3f(uSkyZenith_, e.skyZenith.r, e.skyZenith.g, e.skyZenith.b);
    glUniform3f(uSkyHorizon_, e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b);
    glUniform3f(uGround_, e.ground.r, e.ground.g, e.ground.b);
    glUniform1f(uAmbientI_, e.ambientIntensity);
    glUniform3f(uFogColor_, e.fogColor.r, e.fogColor.g, e.fogColor.b);
    glUniform1f(uFogDensity_, std::max(e.fogDensity, 0.0f));
    glUniform1f(uExposure_, std::max(e.exposure, 0.01f));
    glUniform1i(uShadowsOn_, l.shadows ? 1 : 0);
    glUniformMatrix4fv(uLightVP_, 1, GL_FALSE, l.lightViewProjection.m);
    glUniform1i(uShadowMap_, 1);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::uploadJoints(const std::vector<Mat4>& joints) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, jointTex_);
    const int n = static_cast<int>(std::min<size_t>(joints.size(), kMaxJoints));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, n, GL_RGBA, GL_FLOAT, joints.front().m);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::drawSky() {
    if (!lighting_.drawSky) return;
    const Environment& e = lighting_.env;
    const Vec3 d = lighting_.sunDirection.normalized();
    glUseProgram(skyProgram_);
    const Mat4 inv = viewProj_.inverted();
    glUniformMatrix4fv(uSkyInvVP_, 1, GL_FALSE, inv.m);
    glUniform3f(uSkyCam_, cameraPos_.x, cameraPos_.y, cameraPos_.z);
    glUniform3f(uSkyZ_, e.skyZenith.r, e.skyZenith.g, e.skyZenith.b);
    glUniform3f(uSkyH_, e.skyHorizon.r, e.skyHorizon.g, e.skyHorizon.b);
    glUniform3f(uSkyG_, e.ground.r, e.ground.g, e.ground.b);
    glUniform3f(uSkySun_, d.x, d.y, d.z);
    glUniform3f(uSkySunColor_, lighting_.sunColor.r, lighting_.sunColor.g, lighting_.sunColor.b);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    stats_.drawCalls++;
}

int Renderer::terrainMesh(uint64_t key, uint32_t revision, const std::function<TerrainMesh()>& build) {
    int slot = -1;
    for (size_t i = 0; i < terrains_.size(); ++i)
        if (terrains_[i].key == key) slot = static_cast<int>(i);
    if (slot >= 0) terrains_[slot].lastUsed = frameCounter_;
    if (slot >= 0 && terrains_[slot].revision == revision && terrains_[slot].mesh.vao) return slot;
    if (slot < 0) {
        terrains_.push_back({});
        slot = static_cast<int>(terrains_.size() - 1);
        terrains_[slot].key = key;
        terrains_[slot].lastUsed = frameCounter_;
    }
    TerrainGpu& g = terrains_[slot];
    const TerrainMesh m = build();
    if (!g.mesh.vao) {
        glGenVertexArrays(1, &g.mesh.vao);
        glGenBuffers(1, &g.mesh.vbo);
        glGenBuffers(1, &g.mesh.ibo);
    }
    glBindVertexArray(g.mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, m.vertices.size() * sizeof(TerrainVertex), m.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.indices.size() * sizeof(uint32_t), m.indices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void*)offsetof(TerrainVertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void*)offsetof(TerrainVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void*)offsetof(TerrainVertex, uv));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TerrainVertex), (void*)offsetof(TerrainVertex, splat));
    glBindVertexArray(0);
    g.mesh.indexCount = static_cast<int>(m.indices.size());
    g.revision = revision;
    return slot;
}

void Renderer::drawTerrain(int handle, const Mat4& world, const Terrain& t, const TextureHandle layers[4]) {
    if (handle < 0 || handle >= static_cast<int>(terrains_.size()) || !terrains_[handle].mesh.vao) return;
    glUseProgram(meshProgram_);
    float colors[12];
    for (int i = 0; i < 4; ++i) {
        colors[i * 3] = t.layerColors[i].r;
        colors[i * 3 + 1] = t.layerColors[i].g;
        colors[i * 3 + 2] = t.layerColors[i].b;
        glActiveTexture(GL_TEXTURE7 + i);
        glBindTexture(GL_TEXTURE_2D, textures_[layers[i] < textures_.size() ? layers[i] : kWhiteTexture].gl);
        glUniform1i(uLayers_[i], 7 + i);
    }
    glUniform3fv(uLayerColor_, 4, colors);
    glUniform1f(uLayerTiling_, t.layerTiling);
    glActiveTexture(GL_TEXTURE0);
    Surface s;
    s.roughness = 0.9f;
    terrainDraw_ = true;
    drawSurface(terrains_[handle].mesh.vao, terrains_[handle].mesh.indexCount, true, world, s);
    terrainDraw_ = false;
}

void Renderer::drawTerrainShadow(int handle, const Mat4& world) {
    if (handle < 0 || handle >= static_cast<int>(terrains_.size()) || !terrains_[handle].mesh.vao) return;
    glUniformMatrix4fv(uShadowModel_, 1, GL_FALSE, world.m);
    glUniform1i(uShadowSkinned_, 0);
    glBindVertexArray(terrains_[handle].mesh.vao);
    glDrawElements(GL_TRIANGLES, terrains_[handle].mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    stats_.drawCalls++;
}

void Renderer::drawMesh(MeshPrimitive type, const Mat4& world, const MeshRenderer& mat, const MaterialTextures& tex) {
    const GpuMesh& mesh = meshes_[static_cast<int>(type)];
    if (!mesh.vao) return;
    Surface s;
    s.color = mat.color;
    s.metallic = mat.metallic;
    s.roughness = mat.roughness;
    s.emission = mat.emission;
    if (tex.emission != kWhiteTexture && s.emission.r + s.emission.g + s.emission.b <= 0) s.emission = {1, 1, 1, 1};
    s.grid = mat.prototypeGrid;
    s.unlit = mat.unlit;
    s.doubleSided = mat.doubleSided || type == MeshPrimitive::Plane;
    s.maps = tex;
    s.tiling = mat.tiling;
    s.offset = mat.offset;
    s.alphaCutoff = mat.alphaClip ? mat.alphaCutoff : -1.0f;
    s.normalStrength = mat.normalStrength;
    drawSurface(mesh.vao, mesh.indexCount, false, world, s);
}

void Renderer::drawSurface(uint32_t vao, int count, bool wideIndices, const Mat4& world, const Surface& s) {
    const Color color = s.color;
    const float metallic = s.metallic, roughness = s.roughness;
    const Color emission = s.emission;
    const bool grid = s.grid, unlit = s.unlit, doubleSided = s.doubleSided;
    TextureHandle tex = s.maps.base;
    if (tex >= textures_.size()) tex = kWhiteTexture;
    auto bindMap = [&](int unit, TextureHandle h) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, textures_[h < textures_.size() ? h : kWhiteTexture].gl);
        return h != kWhiteTexture && h < textures_.size();
    };
    // Matriz de normales: inversa traspuesta de la parte 3x3 (correcta también con escala no uniforme).
    const Mat4 inv = world.inverted();
    const float normalMat[9] = {inv.m[0], inv.m[4], inv.m[8], inv.m[1], inv.m[5], inv.m[9], inv.m[2], inv.m[6], inv.m[10]};
    glUseProgram(meshProgram_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, world.m);
    glUniformMatrix3fv(uNormalMat_, 1, GL_FALSE, normalMat);
    glUniform4f(uColor_, color.r, color.g, color.b, color.a);
    glUniform3f(uCameraPos_, cameraPos_.x, cameraPos_.y, cameraPos_.z);
    glUniform1f(uMetallic_, metallic);
    glUniform1f(uRoughness_, roughness);
    glUniform3f(uEmission_, emission.r, emission.g, emission.b);
    glUniform1i(uGrid_, grid ? 1 : 0);
    glUniform1i(uUnlit_, (unlit || unlitOverride_) ? 1 : 0);
    const bool skinned = currentJoints_ && !currentJoints_->empty();
    glUniform1i(uSkinned_, skinned ? 1 : 0);
    glUniform1i(uJoints_, 2);
    if (skinned) uploadJoints(*currentJoints_);
    glUniform4f(uUVTransform_, s.tiling.x, s.tiling.y, s.offset.x, s.offset.y);
    glUniform1i(uTerrain_, terrainDraw_ ? 1 : 0);
    glUniform1f(uAlphaCutoff_, s.alphaCutoff);
    glUniform1f(uNormalStrength_, s.normalStrength);
    glUniform1i(uNormalTex_, 3);
    glUniform1i(uMRTex_, 4);
    glUniform1i(uEmissiveTex_, 5);
    glUniform1i(uOcclusionTex_, 6);
    glUniform1i(uHasNormal_, bindMap(3, s.maps.normal) ? 1 : 0);
    glUniform1i(uHasMR_, bindMap(4, s.maps.metallicRoughness) ? 1 : 0);
    bindMap(5, s.maps.emission);
    glUniform1i(uHasOcclusion_, bindMap(6, s.maps.occlusion) ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[tex].gl);
    // Una escala con determinante negativo invierte el sentido de los triángulos.
    const float det = world.m[0] * (world.m[5] * world.m[10] - world.m[9] * world.m[6]) -
                      world.m[4] * (world.m[1] * world.m[10] - world.m[9] * world.m[2]) +
                      world.m[8] * (world.m[1] * world.m[6] - world.m[5] * world.m[2]);
    glFrontFace(det < 0 ? GL_CW : GL_CCW);
    if (doubleSided) glDisable(GL_CULL_FACE);
    const bool transparent = color.a < 0.999f && s.alphaCutoff < 0;
    if (transparent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, wideIndices ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
    if (transparent) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    stats_.drawCalls++;
    stats_.meshes++;
    stats_.triangles += count / 3;
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
    if (batchAdditive_) flushSprites();
    const Vec3 c[4] = {world.transformPoint({l, t, 0}), world.transformPoint({r, t, 0}),
                       world.transformPoint({r, b, 0}), world.transformPoint({l, b, 0})};
    pushQuad(tex, c, uv, color);
    stats_.sprites++;
}

void Renderer::drawBillboard(TextureHandle tex, const Vec3& c, float size, float rotationDeg, Rect uv, Color color,
                             bool additive) {
    if (additive != batchAdditive_) {
        flushSprites();
        batchAdditive_ = additive;
    }
    // Ejes de la cámara (guardados en setCamera): el cuadrado siempre mira a la cámara.
    const float a = rotationDeg * kDegToRad, h = size * 0.5f;
    const float ca = std::cos(a) * h, sa = std::sin(a) * h;
    const Vec3 r = camRight_ * ca + camUp_ * sa, u = camUp_ * ca - camRight_ * sa;
    const Vec3 q[4] = {c - r + u, c + r + u, c + r - u, c - r - u};
    pushQuad(tex, q, uv, color);
}

void Renderer::flushSprites() {
    if (quadCount_ == 0) return;
    glUseProgram(spriteProgram_);
    glUniformMatrix4fv(uSpriteViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uSpriteTex_, 0);
    glUniform1i(uSpriteSdf_, textures_[batchTexture_].sdf ? 1 : 0);
    // Los sprites se ocultan detrás de las mallas, pero no se ocultan entre sí (se ordenan por sortingOrder).
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    if (batchAdditive_) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
    batchAdditive_ = false; // el siguiente lote empieza en mezcla alfa salvo que drawBillboard diga otra cosa
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
    glUniform1i(uSpriteSdf_, 0);
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
    endOverlay();
    resolveScene();
    if (!text_.empty()) {
        beginOverlay();
        for (const auto& cmd : text_) {
            // Sombra de 1 px para que se lea sobre cualquier fondo.
            const float off = std::fmax(1.0f, cmd.height * 0.06f);
            drawTextNow(0, {cmd.pos.x + off, cmd.pos.y + off}, cmd.height, cmd.text, Color{0, 0, 0, cmd.color.a * 0.6f});
            drawTextNow(0, cmd.pos, cmd.height, cmd.text, cmd.color);
        }
        endOverlay();
    }
    text_.clear();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    stats_.textures = static_cast<int>(textures_.size());
    lastStats_ = stats_;
    // Terrenos que ya no se dibujan (escena cerrada, fin de Jugar): se liberan tras unos segundos.
    if (++frameCounter_ % 120 == 0)
        for (size_t i = 0; i < terrains_.size();) {
            TerrainGpu& t = terrains_[i];
            if (frameCounter_ - t.lastUsed > 300) {
                if (t.mesh.vbo) glDeleteBuffers(1, &t.mesh.vbo);
                if (t.mesh.ibo) glDeleteBuffers(1, &t.mesh.ibo);
                if (t.mesh.vao) glDeleteVertexArrays(1, &t.mesh.vao);
                terrains_.erase(terrains_.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
}

} // namespace titan
