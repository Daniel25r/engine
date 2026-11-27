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
        if (any(isnan(s)) || any(isinf(s))) s = vec3(0.0);
        s = min(s, vec3(64.0)); // limita brillos extremos (luciérnagas) antes de difuminar
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
uniform sampler2D uSSR;
uniform sampler2D uSpec;
uniform int uSSROn;
uniform int uHdr;
vec3 acesTone(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
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
    if (uSSROn == 1) {
        // El sombreador ya puso el reflejo del cielo: donde el rayo encontró escena, se sustituye por ella.
        vec4 r = texture(uSSR, vUV);
        vec3 tint = texture(uSpec, vUV).rgb;
        c = c * (1.0 - luma(tint) * r.a) + r.rgb * tint * r.a;
    }
    if (uBloomOn == 1) c += texture(uBloom, vUV).rgb * uBloomIntensity;
    if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0); // red de seguridad: nunca un píxel inválido en pantalla
    if (uHdr == 1) c = pow(acesTone(max(c, 0.0)), vec3(1.0 / 2.2)); // HDR: el tonemapping se hace aquí
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
    vec3 cn = cross(dx, dy);
    if (dot(cn, cn) < 1e-20) { outColor = vec4(1.0, P.z, 0.0, 1.0); return; } // geometría muy fina: sin normal fiable
    vec3 N = normalize(cn);
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

// Reflejos en pantalla: rayo en el espacio de la cámara desde cada píxel liso, avance creciente con ruido y
// refinado binario al cruzar la profundidad. Se desvanece en los bordes de la pantalla, con la distancia y cuando
// el rayo vuelve hacia la cámara (ahí la pantalla no tiene la información).
const char* kSsrFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform highp sampler2D uDepth;
uniform sampler2D uScene;
uniform sampler2D uSpec;
uniform vec4 uProj;
uniform vec2 uTexel;
uniform float uIntensity;
out vec4 outColor;
vec3 viewPos(vec2 uv) {
    uv = (floor(uv / uTexel) + 0.5) * uTexel;
    float zn = texture(uDepth, uv).r * 2.0 - 1.0;
    float z = -uProj.w / (zn + uProj.z);
    return vec3((uv * 2.0 - 1.0) * (-z) / uProj.xy, z);
}
vec2 project(vec3 p) { return p.xy * uProj.xy / (-p.z) * 0.5 + 0.5; }
void main() {
    vec4 sp = texture(uSpec, vUV);
    float w = smoothstep(0.35, 0.85, sp.a) * max(sp.r, max(sp.g, sp.b));
    float d = texture(uDepth, vUV).r;
    if (w < 0.005 || d >= 1.0) { outColor = vec4(0.0); return; }
    vec3 P = viewPos(vUV);
    vec3 l = viewPos(vUV - vec2(uTexel.x, 0.0)), r = viewPos(vUV + vec2(uTexel.x, 0.0));
    vec3 b = viewPos(vUV - vec2(0.0, uTexel.y)), t = viewPos(vUV + vec2(0.0, uTexel.y));
    vec3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;
    vec3 dy = abs(t.z - P.z) < abs(P.z - b.z) ? t - P : P - b;
    vec3 cn = cross(dx, dy);
    if (dot(cn, cn) < 1e-20) { outColor = vec4(0.0); return; } // geometría muy fina: sin normal fiable
    vec3 N = normalize(cn);
    vec3 R = reflect(normalize(P), N);
    float towardCam = clamp(1.0 - R.z * 2.0, 0.0, 1.0);
    if (towardCam <= 0.0) { outColor = vec4(0.0); return; }
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float stepLen = max(0.03, -P.z * 0.008);
    vec3 ray = P + N * (0.01 * -P.z) + R * stepLen * noise;
    vec3 prev = ray;
    bool hit = false;
    vec2 uv = vUV;
    float travelled = 0.0;
    const int STEPS = 48;
    int i = 0;
    for (; i < STEPS; ++i) {
        prev = ray;
        ray += R * stepLen;
        travelled += stepLen;
        stepLen *= 1.08;
        if (ray.z > -0.05) break; // detrás de la cámara
        uv = project(ray);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float sceneZ = viewPos(uv).z;
        float diff = sceneZ - ray.z; // > 0: el rayo pasó por detrás de la superficie
        if (diff > 0.0 && diff < max(stepLen * 1.5, 0.12)) {
            vec3 a = prev, c = ray; // refinado binario
            for (int k = 0; k < 6; ++k) {
                vec3 m = (a + c) * 0.5;
                uv = project(m);
                if (viewPos(uv).z - m.z > 0.0) c = m; else a = m;
            }
            uv = project(c);
            // Comprobación: el punto refinado tiene que estar sobre la superficie. Si no, el rayo pasó por detrás de
            // un objeto (no lo tocó) y se sigue avanzando: así no salen "rayas" en los reflejos.
            if (abs(viewPos(uv).z - c.z) < 0.05 + 0.01 * -c.z) {
                hit = true;
                break;
            }
        }
    }
    if (!hit) { outColor = vec4(0.0); return; }
    vec2 edge = min(uv, 1.0 - uv);
    float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - float(i) / float(STEPS)) * towardCam;
    if (texture(uDepth, uv).r >= 1.0) fade = 0.0; // el cielo ya está en el reflejo del sombreador
    vec4 res = vec4(texture(uScene, uv).rgb, fade * uIntensity * smoothstep(0.35, 0.85, sp.a));
    // Casos degenerados (rayo rasante, precisión): sin reflejo en vez de un valor inválido que se extendería.
    if (any(isnan(res)) || any(isinf(res))) res = vec4(0.0);
    outColor = vec4(res.rgb, clamp(res.a, 0.0, 1.0));
}
)";

// Suavizado del reflejo: 4×4 muestras pesadas por su confianza (alfa); quita el ruido del avance del rayo.
const char* kSsrBlurFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 outColor;
void main() {
    vec3 col = vec3(0.0);
    float wsum = 0.0, asum = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            vec4 s = texture(uSrc, vUV + (vec2(x, y) + 0.5) * uTexel);
            if (any(isnan(s)) || any(isinf(s))) continue;
            col += s.rgb * s.a;
            wsum += s.a;
            asum += s.a;
        }
    outColor = vec4(wsum > 1e-4 ? col / wsum : vec3(0.0), asum / 16.0);
}
)";

// Antialiasing temporal: cada píxel mezcla el fotograma actual (desplazado una fracción de píxel) con la historia
// reproyectada con la profundidad y la cámara anterior. La historia se limita al rango de colores de los 9 vecinos
// actuales (en YCoCg, por varianza): así lo que se mueve no deja estela.
const char* kTaaFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uCur;
uniform sampler2D uHist;
uniform highp sampler2D uDepth;
uniform mat4 uInvVP;
uniform mat4 uPrevVP;
uniform vec2 uTexel;
uniform vec2 uJitter;
uniform int uReset;
out vec4 outColor;
vec3 toYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 toRgb(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
void main() {
    vec3 cur = texture(uCur, vUV).rgb;
    if (any(isnan(cur)) || any(isinf(cur))) cur = vec3(0.0); // un valor inválido no debe quedarse en la historia
    if (uReset == 1) { outColor = vec4(cur, 1.0); return; }
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    float d = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 o = vec2(float(x), float(y)) * uTexel;
            vec3 c = toYCoCg(texture(uCur, vUV + o).rgb);
            m1 += c;
            m2 += c * c;
            d = min(d, texture(uDepth, vUV + o).r); // lo más cercano: los bordes se reproyectan con el objeto
        }
    vec3 mean = m1 / 9.0, sd = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    vec4 w = uInvVP * vec4((vUV - uJitter) * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    w /= w.w;
    vec4 pc = uPrevVP * w;
    vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
    if (pc.w <= 0.0 || puv.x < 0.0 || puv.y < 0.0 || puv.x > 1.0 || puv.y > 1.0) { outColor = vec4(cur, 1.0); return; }
    vec3 hist = clamp(toYCoCg(texture(uHist, puv).rgb), mean - sd * 1.25, mean + sd * 1.25);
    if (any(isnan(hist))) hist = toYCoCg(cur);
    // Más peso al presente cuando la imagen se desplaza mucho (menos desenfoque al mover la cámara).
    float moved = length((vUV - puv) / uTexel);
    float a = mix(0.1, 0.25, clamp(moved / 16.0, 0.0, 1.0));
    outColor = vec4(toRgb(mix(hist, toYCoCg(cur), a)), 1.0);
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

GLuint makeColorTexture(int w, int h, bool hdr = false) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    if (hdr) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
    ssr_ = link(kSsrFS);
    ssrBlur_ = link(kSsrBlurFS);
    taa_ = link(kTaaFS);
    if (!taa_) TITAN_WARN("render", "TAA no disponible en esta GPU (no compiló); se usa FXAA");
    if (!ssr_) TITAN_WARN("render", "SSR no disponible en esta GPU (no compiló); se dibuja sin él");
    if (!ssao_ || !ssaoBlur_) TITAN_WARN("render", "SSAO no disponible en esta GPU (no compiló); se dibuja sin él");
    glGenVertexArrays(1, &vao_);
    return composite_ && down_ && up_;
}

void PostProcessor::destroyTargets() {
    if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
    if (sceneColor_) glDeleteTextures(1, &sceneColor_);
    if (sceneDepth_) glDeleteTextures(1, &sceneDepth_);
    if (sceneSpec_) glDeleteTextures(1, &sceneSpec_);
    if (ssrFbo_) glDeleteFramebuffers(1, &ssrFbo_);
    if (ssrTex_) glDeleteTextures(1, &ssrTex_);
    if (ssrFbo2_) glDeleteFramebuffers(1, &ssrFbo2_);
    if (ssrTex2_) glDeleteTextures(1, &ssrTex2_);
    sceneFbo_ = sceneColor_ = sceneDepth_ = sceneSpec_ = ssrFbo_ = ssrTex_ = ssrFbo2_ = ssrTex2_ = 0;
    for (int i = 0; i < 2; ++i) {
        if (taaFbo_[i]) glDeleteFramebuffers(1, &taaFbo_[i]);
        if (taaTex_[i]) glDeleteTextures(1, &taaTex_[i]);
        taaFbo_[i] = taaTex_[i] = 0;
    }
    historyValid_ = false;
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
    for (uint32_t* p : {&composite_, &down_, &up_, &ssao_, &ssaoBlur_, &ssr_, &ssrBlur_, &taa_})
        if (*p) glDeleteProgram(*p), *p = 0;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void PostProcessor::onContextLost() {
    composite_ = down_ = up_ = ssao_ = ssaoBlur_ = ssr_ = vao_ = sceneFbo_ = sceneColor_ = sceneDepth_ = 0;
    sceneSpec_ = ssrFbo_ = ssrTex_ = ssrBlur_ = ssrFbo2_ = ssrTex2_ = taa_ = 0;
    for (int i = 0; i < 2; ++i) taaFbo_[i] = taaTex_[i] = 0;
    historyValid_ = false;
    for (int i = 0; i < 2; ++i) aoFbo_[i] = aoTex_[i] = 0;
    aoW_ = aoH_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) bloomFbo_[i] = bloomTex_[i] = 0;
    width_ = height_ = bloomLevels_ = 0;
}

bool PostProcessor::createTargets(int w, int h) {
    destroyTargets();
    sceneColor_ = makeColorTexture(w, h, hdr_);
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
    sceneSpec_ = makeColorTexture(w, h);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, sceneSpec_, 0);
    {
        static const GLenum colorOnly[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, colorOnly); // por defecto solo color: los opacos activan el segundo destino al dibujarse
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        TITAN_ERROR("render", "Posprocesado: no se pudo crear el render target de %dx%d", w, h);
        destroyTargets();
        return false;
    }
    // Pirámide del bloom: la mitad, un cuarto... hasta 5 niveles o 8 píxeles.
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    for (int i = 0; i < kBloomLevels && bw >= 8 && bh >= 8; ++i) {
        bloomTex_[i] = makeColorTexture(bw, bh, hdr_);
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
    ssrTex_ = makeColorTexture(aoW_, aoH_, hdr_);
    glGenFramebuffers(1, &ssrFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssrTex_, 0);
    for (int i = 0; i < 2; ++i) { // historia del TAA, a resolución completa
        taaTex_[i] = makeColorTexture(w, h, hdr_);
        glGenFramebuffers(1, &taaFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, taaFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, taaTex_[i], 0);
    }
    historyValid_ = false;
    ssrTex2_ = makeColorTexture(aoW_, aoH_, hdr_);
    glGenFramebuffers(1, &ssrFbo2_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo2_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssrTex2_, 0);
    if (!aoFbo_[0] || !aoFbo_[1]) TITAN_WARN("render", "SSAO desactivado: la GPU no puede dibujar en texturas RG16F");
    width_ = w;
    height_ = h;
    TITAN_INFO("render", "Render target de la escena: %dx%d (%d niveles de bloom, profundidad de %d bits%s)", w, h, bloomLevels_,
               depthBits_, hdr_ ? ", HDR" : "");
    return true;
}

bool PostProcessor::begin(int w, int h, bool hdr) {
    if (!composite_ && !init()) return false;
    hdr = hdr && !hdrFailed_;
    if (w != width_ || h != height_ || !sceneFbo_ || hdr != hdr_) {
        hdr_ = hdr;
        if (!createTargets(w, h)) {
            if (!hdr_) return false;
            // La GPU no dibuja en RGBA16F: se sigue sin HDR (y no se vuelve a intentar).
            TITAN_WARN("render", "HDR no disponible en esta GPU; se dibuja sin HDR");
            hdrFailed_ = true;
            hdr_ = false;
            if (!createTargets(w, h)) return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, w, h);
    if (sceneSpec_) { // reflectancia a cero: lo que no sea un opaco (cielo, sprites) no refleja nada
        static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}, colorOnly[1] = {GL_COLOR_ATTACHMENT0};
        const GLfloat zero[4] = {0, 0, 0, 0};
        glDrawBuffers(2, both);
        glDisable(GL_SCISSOR_TEST);
        glClearBufferfv(GL_COLOR, 1, zero);
        glDrawBuffers(1, colorOnly);
    }
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
    GLuint sceneSrc = sceneColor_; // imagen de la escena (o la suavizada por el TAA)
    if (s.taa && taa_ && taaFbo_[0] && taaFbo_[1]) {
        const int next = taaIndex_ ^ 1;
        glBindFramebuffer(GL_FRAMEBUFFER, taaFbo_[next]);
        glViewport(0, 0, width_, height_);
        glUseProgram(taa_);
        glUniform1i(glGetUniformLocation(taa_, "uCur"), 0);
        glUniform1i(glGetUniformLocation(taa_, "uHist"), 1);
        glUniform1i(glGetUniformLocation(taa_, "uDepth"), 2);
        glUniformMatrix4fv(glGetUniformLocation(taa_, "uInvVP"), 1, GL_FALSE, s.invViewProj);
        glUniformMatrix4fv(glGetUniformLocation(taa_, "uPrevVP"), 1, GL_FALSE, s.prevViewProj);
        glUniform2f(glGetUniformLocation(taa_, "uTexel"), 1.0f / width_, 1.0f / height_);
        glUniform2f(glGetUniformLocation(taa_, "uJitter"), s.jitter[0], s.jitter[1]);
        glUniform1i(glGetUniformLocation(taa_, "uReset"), (s.taaReset || !historyValid_) ? 1 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, taaTex_[taaIndex_]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, sceneDepth_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneColor_);
        drawFullscreen();
        taaIndex_ = next;
        historyValid_ = true;
        sceneSrc = taaTex_[next];
    } else {
        historyValid_ = false;
    }
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
            glBindTexture(GL_TEXTURE_2D, i == 0 ? sceneSrc : bloomTex_[i - 1]);
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

    const bool ssr = s.ssr && ssr_ && ssrBlur_ && ssrFbo_ && ssrFbo2_ && sceneSpec_ && s.ssrIntensity > 0;
    if (ssr) {
        glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo_);
        glViewport(0, 0, aoW_, aoH_);
        glUseProgram(ssr_);
        glUniform1i(glGetUniformLocation(ssr_, "uDepth"), 0);
        glUniform1i(glGetUniformLocation(ssr_, "uScene"), 1);
        glUniform1i(glGetUniformLocation(ssr_, "uSpec"), 2);
        glUniform4f(glGetUniformLocation(ssr_, "uProj"), s.proj[0], s.proj[1], s.proj[2], s.proj[3]);
        glUniform2f(glGetUniformLocation(ssr_, "uTexel"), 1.0f / width_, 1.0f / height_);
        glUniform1f(glGetUniformLocation(ssr_, "uIntensity"), s.ssrIntensity);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sceneColor_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, sceneSpec_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneDepth_);
        drawFullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, ssrFbo2_);
        glUseProgram(ssrBlur_);
        glUniform1i(glGetUniformLocation(ssrBlur_, "uSrc"), 0);
        glUniform2f(glGetUniformLocation(ssrBlur_, "uTexel"), 1.0f / aoW_, 1.0f / aoH_);
        glBindTexture(GL_TEXTURE_2D, ssrTex_);
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
    glUniform1i(glGetUniformLocation(composite_, "uSSR"), 3);
    glUniform1i(glGetUniformLocation(composite_, "uSpec"), 4);
    glUniform1i(glGetUniformLocation(composite_, "uSSROn"), ssr ? 1 : 0);
    glUniform1i(glGetUniformLocation(composite_, "uHdr"), (s.hdr && hdr_) ? 1 : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ssr ? ssrTex2_ : sceneColor_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, ssr ? sceneSpec_ : sceneColor_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ao ? aoTex_[1] : sceneColor_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom ? bloomTex_[0] : sceneSrc);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneSrc);
    drawFullscreen();
    glBindVertexArray(0);
}

} // namespace titan
