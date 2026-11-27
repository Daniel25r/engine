#include "Test.h"
#include "titan/core/Quality.h"

using namespace titan;

TEST("calidad: cada perfil cambia opciones reales y en orden creciente") {
    const QualitySettings vl = QualitySettings::preset(QualityLevel::VeryLow), lo = QualitySettings::preset(QualityLevel::Low);
    const QualitySettings me = QualitySettings::preset(QualityLevel::Medium), hi = QualitySettings::preset(QualityLevel::High);
    const QualitySettings vh = QualitySettings::preset(QualityLevel::VeryHigh);
    CHECK(!vl.shadows && lo.shadows);
    CHECK(vl.renderScale < lo.renderScale && lo.renderScale < me.renderScale && me.renderScale <= hi.renderScale);
    CHECK(lo.shadowMapSize < hi.shadowMapSize && hi.shadowMapSize < vh.shadowMapSize);
    CHECK(!lo.fxaa && me.fxaa && !me.bloom && hi.bloom);
    CHECK(vl.maxPointLights < hi.maxPointLights && hi.maxPointLights < vh.maxPointLights);
}

TEST("calidad: nombres y recomendación según capacidades medibles") {
    QualityLevel l;
    CHECK(parseQuality("Muy Bajo", l) && l == QualityLevel::VeryLow);
    CHECK(parseQuality("muy_alto", l) && l == QualityLevel::VeryHigh);
    CHECK(parseQuality("medium", l) && l == QualityLevel::Medium);
    CHECK(!parseQuality("ultra", l));
    CHECK_EQ(std::string(qualityName(QualityLevel::High)), std::string("alto"));
    DeviceCaps c;
    c.glesMajor = 3; c.glesMinor = 2; c.maxTextureSize = 16384; c.memoryMB = 8000;
    CHECK(recommendedQuality(c) == QualityLevel::High);
    c.memoryMB = 2500; // poca memoria: como mucho medio
    CHECK(recommendedQuality(c) == QualityLevel::Medium);
    c.memoryMB = 1500;
    CHECK(recommendedQuality(c) == QualityLevel::Low);
    c = {}; c.glesMajor = 3; c.glesMinor = 0; c.maxTextureSize = 4096;
    CHECK(recommendedQuality(c) == QualityLevel::Low);
    c.maxTextureSize = 2048;
    CHECK(recommendedQuality(c) == QualityLevel::VeryLow);
    c = {}; c.glesMajor = 3; c.glesMinor = 1; c.maxTextureSize = 8192;
    CHECK(recommendedQuality(c) == QualityLevel::Medium);
    // PC con ANGLE (Windows): informa OpenGL ES 3.0 aunque sea una GPU de escritorio → alto, no bajo.
    c = {}; c.glesMajor = 3; c.glesMinor = 0; c.maxTextureSize = 16384; c.desktop = true;
    c.renderer = "ANGLE (Intel, Intel(R) UHD Graphics Direct3D11 vs_5_0 ps_5_0)";
    CHECK(recommendedQuality(c) == QualityLevel::High);
    c.softwareRenderer = true; // llvmpipe, WARP...
    CHECK(recommendedQuality(c) == QualityLevel::Medium);
}
