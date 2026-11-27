#include "titan/core/Quality.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace titan {

QualitySettings QualitySettings::preset(QualityLevel level) {
    QualitySettings q;
    q.level = level;
    switch (level) {
    case QualityLevel::VeryLow:
        q.lodBias = 2.0f; q.renderScale = 0.6f; q.fxaa = false; q.bloom = false; q.ssao = false; q.ssr = false; q.taa = false; q.shadows = false; q.shadowMapSize = 512; q.maxPointLights = 1;
        break;
    case QualityLevel::Low:
        q.lodBias = 1.5f; q.renderScale = 0.75f; q.fxaa = false; q.bloom = false; q.ssao = false; q.ssr = false; q.taa = false; q.shadows = true; q.shadowMapSize = 1024; q.maxPointLights = 2;
        break;
    case QualityLevel::Medium:
        q.renderScale = 0.9f; q.fxaa = true; q.bloom = false; q.ssao = false; q.ssr = false; q.taa = false; q.shadows = true; q.shadowMapSize = 1024; q.maxPointLights = 4;
        break;
    case QualityLevel::High:
    case QualityLevel::Custom:
        q.renderScale = 1.0f; q.fxaa = true; q.bloom = true; q.shadows = true; q.shadowMapSize = 2048; q.maxPointLights = 4;
        break;
    case QualityLevel::VeryHigh:
        q.renderScale = 1.0f; q.fxaa = true; q.bloom = true; q.shadows = true; q.shadowMapSize = 4096; q.maxPointLights = 8;
        break;
    }
    return q;
}

const char* qualityName(QualityLevel l) {
    switch (l) {
    case QualityLevel::VeryLow: return "muy bajo";
    case QualityLevel::Low: return "bajo";
    case QualityLevel::Medium: return "medio";
    case QualityLevel::High: return "alto";
    case QualityLevel::VeryHigh: return "muy alto";
    case QualityLevel::Custom: return "personalizado";
    }
    return "alto";
}

bool parseQuality(const std::string& name, QualityLevel& out) {
    std::string n;
    for (char c : name)
        if (c != ' ' && c != '_' && c != '-') n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::pair<const char*, QualityLevel> names[] = {
        {"muybajo", QualityLevel::VeryLow}, {"bajo", QualityLevel::Low}, {"medio", QualityLevel::Medium},
        {"alto", QualityLevel::High}, {"muyalto", QualityLevel::VeryHigh}, {"personalizado", QualityLevel::Custom},
        {"verylow", QualityLevel::VeryLow}, {"low", QualityLevel::Low}, {"medium", QualityLevel::Medium},
        {"high", QualityLevel::High}, {"veryhigh", QualityLevel::VeryHigh}, {"custom", QualityLevel::Custom}};
    for (auto& [k, v] : names)
        if (n == k) { out = v; return true; }
    return false;
}

QualityLevel recommendedQuality(const DeviceCaps& c) {
    // PC: la versión de OpenGL ES no dice nada (ANGLE informa 3.0 con cualquier tarjeta). Una GPU de PC, incluida
    // una integrada Intel UHD, mueve el perfil alto; con render por software, medio.
    if (c.desktop) return c.softwareRenderer ? QualityLevel::Medium : QualityLevel::High;
    // OpenGL ES 3.2 suele ir con GPU recientes; 3.1, gama media; 3.0 (o límites de textura de 4096), gama baja.
    QualityLevel l = QualityLevel::Low;
    if (c.glesMajor > 3 || (c.glesMajor == 3 && c.glesMinor >= 2)) l = c.maxTextureSize >= 8192 ? QualityLevel::High : QualityLevel::Medium;
    else if (c.glesMajor == 3 && c.glesMinor == 1) l = QualityLevel::Medium;
    if (c.maxTextureSize < 4096) l = QualityLevel::VeryLow;
    // Poca memoria: como mucho medio (2-3 GB) o bajo (menos de 2 GB).
    if (c.memoryMB > 0 && c.memoryMB < 2048) l = std::min(l, QualityLevel::Low);
    else if (c.memoryMB > 0 && c.memoryMB < 3072) l = std::min(l, QualityLevel::Medium);
    return l;
}

int systemMemoryMB() {
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[256];
    long kb = 0;
    while (std::fgets(line, sizeof(line), f))
        if (std::sscanf(line, "MemTotal: %ld kB", &kb) == 1) break;
    std::fclose(f);
    return static_cast<int>(kb / 1024);
}

} // namespace titan
