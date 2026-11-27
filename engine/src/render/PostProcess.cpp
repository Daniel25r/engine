#include "PostProcess.h"

#include <GLES3/gl3.h>

#include <algorithm>

#include "titan/core/Log.h"

namespace titan {

namespace {

const char* kFullscreenVS = R"(#version 300 es
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Reducción del bloom ("dual filtering" de Kawase): 5 muestras; la primera pasada se queda solo con lo brillante.
const char* kDownFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int uPrefilter;
uniform float uThreshold;
out vec4 outColor;
void main() {
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV - uTexel).rgb + texture(uSrc, vUV + uTexel).rgb;
    s += texture(uSrc, vUV + vec2(uTexel.x, -uTexel.y)).rgb + texture(uSrc, vUV - vec2(uTexel.x, -uTexel.y)).rgb;
    s *= 0.125;
    if (uPrefilter == 1) {
        float b = max(s.r, max(s.g, s.b));
        float knee = clamp(b - uThreshold + 0.1, 0.0, 0.2);   // transición suave alrededor del umbral
        float soft = knee * knee / 0.8;
        s *= max(soft, b - uThreshold) / max(b, 1e-4);
    }
    outColor = vec4(s, 1.0);
}
)";

// Ampliación del bloom: 8 muestras en anillo; se suma (mezcla aditiva) al nivel mayor.
const char* kUpFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 outColor;
void main() {
    vec2 t = uTexel;
    vec3 s = texture(uSrc, vUV + vec2(-2.0 * t.x, 0.0)).rgb + texture(uSrc, vUV + vec2(2.0 * t.x, 0.0)).rgb;
    s += texture(uSrc, vUV + vec2(0.0, -2.0 * t.y)).rgb + texture(uSrc, vUV + vec2(0.0, 2.0 * t.y)).rgb;
    s += (texture(uSrc, vUV + vec2(-t.x, t.y)).rgb + texture(uSrc, vUV + vec2(t.x, t.y)).rgb +
          texture(uSrc, vUV + vec2(t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb) * 2.0;
    outColor = vec4(s / 12.0, 1.0);
}
)";

// Composición final: FXAA (algoritmo de Lottes, versión de 5 muestras) + bloom + color + viñeta.
const char* kCompositeFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2 uTexel;
uniform int uFxaa;
uniform int uBloomOn;
uniform float uBloomIntensity;
uniform float uVignette;
uniform float uContrast;
uniform float uSaturation;
uniform float uTemperature;
uniform sampler2D uAO;
uniform int uAOOn;
out vec4 outColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 fxaa(vec2 uv) {
    vec3 nw = texture(uScene, uv + vec2(-1.0, -1.0) * uTexel).rgb;
    vec3 ne = texture(uScene, uv + vec2(1.0, -1.0) * uTexel).rgb;
    vec3 sw = texture(uScene, uv + vec2(-1.0, 1.0) * uTexel).rgb;
    vec3 se = texture(uScene, uv + vec2(1.0, 1.0) * uTexel).rgb;
    vec3 m = texture(uScene, uv).rgb;
    float lNW = luma(nw), lNE = luma(ne), lSW = luma(sw), lSE = luma(se), lM = luma(m);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) return m; // sin borde: no se toca
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uTexel;
    vec3 a = 0.5 * (texture(uScene, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uScene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uScene, uv - dir * 0.5).rgb + texture(uScene, uv + dir * 0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}
void main() {
    vec3 c = uFxaa == 1 ? fxaa(vUV) : texture(uScene, vUV).rgb;
    if (uAOOn == 1) c *= texture(uAO, vUV).r; // antes del bloom: la oclusión no apaga los brillos
    if (uBloomOn == 1) c += texture(uBloom, vUV).rgb * uBloomIntensity;
    c = (c - 0.5) * uContrast + 0.5;
    c = mix(vec3(luma(c)), c, uSaturation);
    c *= vec3(1.0 + 0.1 * uTemperature, 1.0, 1.0 - 0.1 * uTemperature);
    float d = length(vUV - 0.5) * 1.4142;
    c *= 1.0 - uVignette * smoothstep(0.45, 1.0, d);
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// SSAO estilo "Alchemy"/SAO (McGuire et al.): posiciones en el espacio de la cámara reconstruidas desde la
// profundidad, normal por diferencias con el vecino más cercano en profundidad (bordes limpios), 12 muestras en
// espiral giradas por píxel (ruido de gradiente entrelazado) y caída por distancia. A media resolución.
const char* kSsaoFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform highp sampler2D uDepth;
uniform vec4 uProj;   // P00, P11, P22, P23
uniform vec2 uTexel;  // de la profundidad (resolución completa)
uniform float uRadius;
uniform float uIntensity;
out vec4 outColor;
vec3 viewPos(vec2 uv) {
    // Al centro del píxel de profundidad: x, y y la profundidad tienen que ser del mismo punto (si no, en un suelo
    // visto de lado medio píxel de diferencia inventa una pendiente falsa y salen bandas).
    uv = (floor(uv / uTexel) + 0.5) * uTexel;
    float zn = texture(uDepth, uv).r * 2.0 - 1.0;
    float z = -uProj.w / (zn + uProj.z);
    return vec3((uv * 2.0 - 1.0) * (-z) / uProj.xy, z);
}
void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { outColor = vec4(1.0); return; } // cielo
    vec3 P = viewPos(vUV);
    vec3 l = viewPos(vUV - vec2(uTexel.x, 0.0)), r = viewPos(vUV + vec2(uTexel.x, 0.0));
    vec3 b = viewPos(vUV - vec2(0.0, uTexel.y)), t = viewPos(vUV + vec2(0.0, uTexel.y));
    vec3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;
    vec3 dy = abs(t.z - P.z) < abs(P.z - b.z) ? t - P : P - b;
    vec3 N = normalize(cross(dx, dy));
    float screenR = uRadius * uProj.x / max(-P.z, 0.1) * 0.5; // radio en UV
    if (screenR < uTexel.x * 1.5) { outColor = vec4(1.0); return; } // demasiado lejos: nada que ocluir
    screenR = min(screenR, 0.25);
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float angle0 = noise * 6.2831853;
    float r2 = uRadius * uRadius, sum = 0.0;
    const int N_SAMPLES = 12;
    for (int i = 0; i < N_SAMPLES; ++i) {
        float a = (float(i) + 0.5) / float(N_SAMPLES);
        float ang = angle0 + a * 6.2831853 * 3.0; // tres vueltas de espiral
        vec2 uv = vUV + vec2(cos(ang), sin(ang)) * a * screenR;
        vec3 v = viewPos(uv) - P;
        float vv = dot(v, v);
        float cosA = dot(v, N) * inversesqrt(vv + 1e-6);
        float falloff = max(r2 - vv, 0.0) / r2;
        // Sesgo de ángulo pequeño (como HBAO): una muestra solo ocluye si sube sobre el plano de la superficie.
        sum += falloff * max(cosA - 0.1, 0.0);
    }
    float ao = max(0.0, 1.0 - 2.0 * uIntensity * sum / float(N_SAMPLES));
    outColor = vec4(ao, P.z, 0.0, 1.0);
}
)";

// Suavizado de la oclusión respetando bordes: 4×4 muestras pesadas por la diferencia de profundidad.
const char* kSsaoBlurFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uAOIn;
uniform vec2 uTexel; // de la textura de oclusión
out vec4 outColor;
void main() {
    vec2 c = texture(uAOIn, vUV).rg;
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            vec2 s = texture(uAOIn, vUV + (vec2(x, y) + 0.5) * uTexel).rg;
            float w = 1.0 / (1e-3 + abs(s.g - c.g) * 4.0 / max(-c.g, 0.1));
            w = min(w, 50.0);
            sum += s.r * w;
            wsum += w;
        }
    outColor = vec4(sum / max(wsum, 1e-4), c.g, 0.0, 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        TITAN_ERROR("render", "Posprocesado: error compilando shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, kFullscreenVS), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLuint makeColorTexture(int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

} // namespace

bool PostProcessor::init() {
    composite_ = link(kCompositeFS);
    down_ = link(kDownFS);
    up_ = link(kUpFS);
    ssao_ = link(kSsaoFS);
    ssaoBlur_ = link(kSsaoBlurFS);
    if (!ssao_ || !ssaoBlur_) TITAN_WARN("render", "SSAO no disponible en esta GPU (no compiló); se dibuja sin él");
    glGenVertexArrays(1, &vao_);
    return composite_ && down_ && up_;
}

void PostProcessor::destroyTargets() {
    if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
    if (sceneColor_) glDeleteTextures(1, &sceneColor_);
    if (sceneDepth_) glDeleteTextures(1, &sceneDepth_);
    sceneFbo_ = sceneColor_ = sceneDepth_ = 0;
    for (int i = 0; i < 2; ++i) {
        if (aoFbo_[i]) glDeleteFramebuffers(1, &aoFbo_[i]);
        if (aoTex_[i]) glDeleteTextures(1, &aoTex_[i]);
        aoFbo_[i] = aoTex_[i] = 0;
    }
    aoW_ = aoH_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) {
        if (bloomFbo_[i]) glDeleteFramebuffers(1, &bloomFbo_[i]);
        if (bloomTex_[i]) glDeleteTextures(1, &bloomTex_[i]);
        bloomFbo_[i] = bloomTex_[i] = 0;
    }
    width_ = height_ = bloomLevels_ = 0;
}

void PostProcessor::shutdown() {
    destroyTargets();
    for (uint32_t* p : {&composite_, &down_, &up_, &ssao_, &ssaoBlur_})
        if (*p) glDeleteProgram(*p), *p = 0;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void PostProcessor::onContextLost() {
    composite_ = down_ = up_ = ssao_ = ssaoBlur_ = vao_ = sceneFbo_ = sceneColor_ = sceneDepth_ = 0;
    for (int i = 0; i < 2; ++i) aoFbo_[i] = aoTex_[i] = 0;
    aoW_ = aoH_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) bloomFbo_[i] = bloomTex_[i] = 0;
    width_ = height_ = bloomLevels_ = 0;
}

bool PostProcessor::createTargets(int w, int h) {
    destroyTargets();
    sceneColor_ = makeColorTexture(w, h);
    // Profundidad como textura (no renderbuffer): el SSAO la lee.
    glGenTextures(1, &sceneDepth_);
    glBindTexture(GL_TEXTURE_2D, sceneDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        TITAN_ERROR("render", "Posprocesado: no se pudo crear el render target de %dx%d", w, h);
        destroyTargets();
        return false;
    }
    // Pirámide del bloom: la mitad, un cuarto... hasta 5 niveles o 8 píxeles.
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    for (int i = 0; i < kBloomLevels && bw >= 8 && bh >= 8; ++i) {
        bloomTex_[i] = makeColorTexture(bw, bh);
        glGenFramebuffers(1, &bloomFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
        bloomW_[i] = bw;
        bloomH_[i] = bh;
        bloomLevels_ = i + 1;
        bw /= 2;
        bh /= 2;
    }
    GLint depthBits = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits);
    depthBits_ = depthBits;
    // Oclusión ambiental a media resolución (R = oclusión, G = profundidad para el suavizado que respeta bordes).
    aoW_ = std::max(1, w / 2);
    aoH_ = std::max(1, h / 2);
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &aoTex_[i]);
        glBindTexture(GL_TEXTURE_2D, aoTex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, aoW_, aoH_, 0, GL_RG, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &aoFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoTex_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { // RG16F no renderizable aquí
            glDeleteFramebuffers(1, &aoFbo_[i]);
            glDeleteTextures(1, &aoTex_[i]);
            aoFbo_[i] = aoTex_[i] = 0;
        }
    }
    if (!aoFbo_[0] || !aoFbo_[1]) TITAN_WARN("render", "SSAO desactivado: la GPU no puede dibujar en texturas RG16F");
    width_ = w;
    height_ = h;
    TITAN_INFO("render", "Render target de la escena: %dx%d (%d niveles de bloom, profundidad de %d bits)", w, h, bloomLevels_, depthBits_);
    return true;
}

bool PostProcessor::begin(int w, int h) {
    if (!composite_ && !init()) return false;
    if ((w != width_ || h != height_ || !sceneFbo_) && !createTargets(w, h)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, w, h);
    passes_ = 0;
    return true;
}

void PostProcessor::drawFullscreen() {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++passes_;
}

void PostProcessor::resolve(uint32_t target, int x, int y, int w, int h, const PostSettings& s) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    const bool bloom = s.bloom && s.bloomIntensity > 0 && bloomLevels_ > 0;
    if (bloom) {
        glUseProgram(down_);
        glUniform1i(glGetUniformLocation(down_, "uSrc"), 0);
        glUniform1f(glGetUniformLocation(down_, "uThreshold"), s.bloomThreshold);
        const GLint texel = glGetUniformLocation(down_, "uTexel"), pre = glGetUniformLocation(down_, "uPrefilter");
        for (int i = 0; i < bloomLevels_; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
            glViewport(0, 0, bloomW_[i], bloomH_[i]);
            const int sw = i == 0 ? width_ : bloomW_[i - 1], sh = i == 0 ? height_ : bloomH_[i - 1];
            glBindTexture(GL_TEXTURE_2D, i == 0 ? sceneColor_ : bloomTex_[i - 1]);
            glUniform2f(texel, 0.5f / sw, 0.5f / sh);
            glUniform1i(pre, i == 0 ? 1 : 0);
            drawFullscreen();
        }
        glUseProgram(up_);
        glUniform1i(glGetUniformLocation(up_, "uSrc"), 0);
        const GLint utexel = glGetUniformLocation(up_, "uTexel");
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = bloomLevels_ - 1; i > 0; --i) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
            glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
            glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
            glUniform2f(utexel, 0.5f / bloomW_[i], 0.5f / bloomH_[i]);
            drawFullscreen();
        }
        glDisable(GL_BLEND);
    }

    // Oclusión ambiental: cálculo y suavizado a media resolución.
    const bool ao = s.ssao && ssao_ && ssaoBlur_ && aoFbo_[0] && aoFbo_[1] && s.ssaoIntensity > 0;
    if (ao) {
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[0]);
        glViewport(0, 0, aoW_, aoH_);
        glUseProgram(ssao_);
        glUniform1i(glGetUniformLocation(ssao_, "uDepth"), 0);
        glUniform4f(glGetUniformLocation(ssao_, "uProj"), s.proj[0], s.proj[1], s.proj[2], s.proj[3]);
        glUniform2f(glGetUniformLocation(ssao_, "uTexel"), 1.0f / width_, 1.0f / height_);
        glUniform1f(glGetUniformLocation(ssao_, "uRadius"), std::max(0.05f, s.ssaoRadius));
        glUniform1f(glGetUniformLocation(ssao_, "uIntensity"), s.ssaoIntensity);
        glBindTexture(GL_TEXTURE_2D, sceneDepth_);
        drawFullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[1]);
        glUseProgram(ssaoBlur_);
        glUniform1i(glGetUniformLocation(ssaoBlur_, "uAOIn"), 0);
        glUniform2f(glGetUniformLocation(ssaoBlur_, "uTexel"), 1.0f / aoW_, 1.0f / aoH_);
        glBindTexture(GL_TEXTURE_2D, aoTex_[0]);
        drawFullscreen();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glUseProgram(composite_);
    glUniform1i(glGetUniformLocation(composite_, "uScene"), 0);
    glUniform1i(glGetUniformLocation(composite_, "uBloom"), 1);
    glUniform2f(glGetUniformLocation(composite_, "uTexel"), 1.0f / width_, 1.0f / height_);
    glUniform1i(glGetUniformLocation(composite_, "uFxaa"), s.fxaa ? 1 : 0);
    glUniform1i(glGetUniformLocation(composite_, "uBloomOn"), bloom ? 1 : 0);
    // Los niveles se suman al ampliar: se divide por su número para que la intensidad no dependa de la resolución.
    glUniform1f(glGetUniformLocation(composite_, "uBloomIntensity"), s.bloomIntensity / std::max(1, bloomLevels_));
    glUniform1f(glGetUniformLocation(composite_, "uVignette"), s.vignette);
    glUniform1f(glGetUniformLocation(composite_, "uContrast"), s.contrast);
    glUniform1f(glGetUniformLocation(composite_, "uSaturation"), s.saturation);
    glUniform1f(glGetUniformLocation(composite_, "uTemperature"), s.temperature);
    glUniform1i(glGetUniformLocation(composite_, "uAO"), 2);
    glUniform1i(glGetUniformLocation(composite_, "uAOOn"), ao ? 1 : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ao ? aoTex_[1] : sceneColor_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom ? bloomTex_[0] : sceneColor_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColor_);
    drawFullscreen();
    glBindVertexArray(0);
}

} // namespace titan
