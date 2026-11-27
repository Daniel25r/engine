#pragma once
// Perfiles de calidad gráfica y capacidades del dispositivo. Solo datos (sin GPU): el Renderer los aplica.
// Cada opción de un perfil cambia algo real del render (ver docs/RENDERING.md).
#include <string>

namespace titan {

enum class QualityLevel { VeryLow, Low, Medium, High, VeryHigh, Custom };

struct QualitySettings {
    QualityLevel level = QualityLevel::High;
    float renderScale = 1.0f;   // resolución de la escena respecto a la pantalla (la UI siempre va a resolución completa)
    bool postProcess = true;    // render target + composición (necesario para escala, FXAA, bloom y color)
    bool fxaa = true;
    bool bloom = true;          // permitido; cada escena decide si lo usa (Environment.bloom)
    bool ssao = true;           // permitido; cada escena decide si lo usa (Environment.ssao)
    bool ssr = true;            // reflejos en pantalla, permitidos (Environment.ssr)
    bool taa = true;            // antialiasing temporal, permitido (Environment.taa)
    bool shadows = true;
    int shadowMapSize = 2048;
    int maxPointLights = 4;     // luces locales (puntuales y focos) por fotograma
    float lodBias = 1.0f;       // >1: se pasa antes a los modelos simplificados (menos triángulos de lejos)

    static QualitySettings preset(QualityLevel level);
};

const char* qualityName(QualityLevel level);          // "muy bajo", "bajo", "medio", "alto", "muy alto", "personalizado"
bool parseQuality(const std::string& name, QualityLevel& out); // acepta los nombres anteriores y "muy_bajo", "muybajo"...

struct DeviceCaps {
    std::string vendor, renderer, version;
    int glesMajor = 3, glesMinor = 0;
    int maxTextureSize = 2048;
    bool astc = false;            // GL_KHR_texture_compression_astc_ldr
    bool halfFloatTargets = false; // GL_EXT_color_buffer_half_float (o float)
    int memoryMB = 0;             // memoria total del sistema; 0 = desconocida
    // PC (Windows, Linux, macOS): GPU de escritorio aunque OpenGL ES diga "3.0" (ANGLE en Windows siempre lo dice).
    bool desktop = false;
    bool softwareRenderer = false; // llvmpipe, SwiftShader, softpipe, WARP: la "GPU" es la CPU
};

// Perfil recomendado según capacidades medibles (versión de OpenGL ES, límites, memoria), nunca por el nombre del modelo.
// Es un punto de partida: el juego o el usuario pueden cambiarlo.
QualityLevel recommendedQuality(const DeviceCaps& caps);

// Memoria total del sistema en MB leyendo /proc/meminfo (Linux y Android). 0 si no se puede leer.
int systemMemoryMB();

} // namespace titan
