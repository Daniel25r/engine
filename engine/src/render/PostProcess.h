#pragma once
// Posprocesado (interno del módulo render): la escena se dibuja en un render target a la resolución elegida
// (escala de render) y después se compone en pantalla con bloom, FXAA, corrección de color y viñeta.
// Funciona sobre la imagen ya mapeada a LDR (RGBA8): es compatible con cualquier GPU OpenGL ES 3.0,
// a cambio de que el bloom se calcule sobre colores ya limitados a 1 (umbral sobre la imagen final).
#include <cstdint>

namespace titan {

struct PostSettings {
    bool fxaa = false;
    // Oclusión ambiental en pantalla (SSAO): oscurece rincones, contactos y huecos a partir de la profundidad.
    bool ssao = false;
    float ssaoIntensity = 1.0f;
    float ssaoRadius = 0.6f;   // metros
    float proj[4] = {1, 1, -1, -0.2f};
    // Reflejos en pantalla (SSR): lo que se ve en el agua, el suelo pulido, el metal y el barniz.
    bool ssr = false;
    float ssrIntensity = 1.0f;
    // Antialiasing temporal: matrices sin desplazamiento (actual invertida y anterior) y desplazamiento en UV.
    bool taa = false, taaReset = false;
    bool hdr = false; // la escena viene en lineal sin tonemapear: ACES al componer
    bool autoExposure = false;
    float adaptationSpeed = 1.5f, dt = 1.0f / 60.0f;
    float invViewProj[16] = {}, prevViewProj[16] = {};
    float jitter[2] = {0, 0}; // P00, P11, P22, P23 de la proyección en perspectiva
    bool bloom = false;
    float bloomIntensity = 0.5f;
    float bloomThreshold = 0.8f;
    float vignette = 0;
    float contrast = 1;
    float saturation = 1;
    float temperature = 0; // -1 frío .. 1 cálido
};

class PostProcessor {
public:
    bool init();
    void shutdown();
    void onContextLost(); // olvida los objetos (el contexto ya no existe)
    // Crea (si hace falta) y activa el render target de la escena. Devuelve false si no se puede.
    // hdr: la escena en RGBA16F (valores por encima de 1: brillos, emisión, sol); se tonemapea al componer.
    bool begin(int width, int height, bool hdr = false);
    bool isHdr() const { return hdr_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Compone en el framebuffer "target", dentro del rectángulo (x, y, w, h) en píxeles de OpenGL.
    void resolve(uint32_t target, int x, int y, int w, int h, const PostSettings& s);
    int lastPasses() const { return passes_; }
    bool hasSpecTarget() const { return sceneSpec_ != 0; }

private:
    static constexpr int kBloomLevels = 5;
    bool createTargets(int width, int height);
    void destroyTargets();
    void drawFullscreen();

    uint32_t vao_ = 0;
    uint32_t composite_ = 0, down_ = 0, up_ = 0, ssao_ = 0, ssaoBlur_ = 0;
    uint32_t aoFbo_[2] = {}, aoTex_[2] = {}; // media resolución: oclusión y oclusión suavizada
    int aoW_ = 0, aoH_ = 0;
    int depthBits_ = 0;
    bool hdr_ = false, hdrFailed_ = false;
    // Exposición automática: luminancia logarítmica en 64×64 y luminancia adaptada en 1×1 (se alternan).
    uint32_t lum_ = 0, adapt_ = 0, lumFbo_ = 0, lumTex_ = 0, adaptFbo_[2] = {}, adaptTex_[2] = {};
    int adaptIndex_ = 0;
    bool adaptValid_ = false;

    uint32_t sceneFbo_ = 0, sceneColor_ = 0, sceneDepth_ = 0;
    uint32_t sceneSpec_ = 0;          // reflectancia y lisura por píxel (la escriben los opacos)
    uint32_t ssr_ = 0, ssrFbo_ = 0, ssrTex_ = 0; // reflejos a media resolución
    uint32_t ssrBlur_ = 0, ssrFbo2_ = 0, ssrTex2_ = 0; // y suavizados
    uint32_t taa_ = 0, taaFbo_[2] = {}, taaTex_[2] = {}; // historia del TAA (se alternan)
    int taaIndex_ = 0;
    bool historyValid_ = false;
    uint32_t bloomFbo_[kBloomLevels] = {}, bloomTex_[kBloomLevels] = {};
    int bloomW_[kBloomLevels] = {}, bloomH_[kBloomLevels] = {};
    int bloomLevels_ = 0;
    int width_ = 0, height_ = 0;
    int passes_ = 0;
};

} // namespace titan
