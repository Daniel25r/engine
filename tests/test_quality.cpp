#include <cmath>

#include "Test.h"
#include "titan/core/Quality.h"
#include "titan/core/Scene.h"
#include "titan/core/Serialization.h"

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
    CHECK(!vl.ssao && !lo.ssao && !me.ssao && hi.ssao && vh.ssao); // la oclusión en pantalla solo en alto y muy alto
    CHECK(!vl.ssr && !lo.ssr && !me.ssr && hi.ssr && vh.ssr);       // y los reflejos en pantalla
}

TEST("calidad: el SSAO de la escena se guarda y los proyectos antiguos siguen sin él") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene a("a");
    Environment en;
    en.ssao = true;
    en.ssaoIntensity = 1.7f;
    en.ssaoRadius = 1.25f;
    a.world().add<Environment>(a.createEntity("Entorno"), en);
    const std::string text = serializeScene(a, reg);
    Scene b("b");
    CHECK(deserializeScene(text, b, reg).ok());
    const Environment* e = b.world().get<Environment>(b.findByName("Entorno"));
    CHECK(e && e->ssao);
    CHECK(e && std::abs(e->ssaoIntensity - 1.7f) < 1e-5f && std::abs(e->ssaoRadius - 1.25f) < 1e-5f);
    // Una escena escrita antes de existir el SSAO (sin los campos) carga con él desactivado: se ve igual que antes.
    std::string old = text;
    for (const char* key : {"\"ssao\"", "\"ssaoIntensity\"", "\"ssaoRadius\""}) {
        const size_t k = old.find(key);
        CHECK(k != std::string::npos);
        const size_t end = old.find_first_of(",}", k);
        old.erase(k, end - k + (old[end] == ',' ? 1 : 0));
    }
    Scene c("c");
    CHECK(deserializeScene(old, c, reg).ok());
    const Environment* oe = c.world().get<Environment>(c.findByName("Entorno"));
    CHECK(oe && !oe->ssao);
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

TEST("calidad: barniz y reflejos en pantalla se guardan; los proyectos antiguos no cambian") {
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene a("a");
    Entity coche = a.createEntity("Coche");
    MeshRenderer mr;
    mr.clearcoat = 0.8f;
    mr.clearcoatRoughness = 0.1f;
    a.world().add<MeshRenderer>(coche, mr);
    Environment en;
    en.ssr = true;
    en.ssrIntensity = 0.7f;
    a.world().add<Environment>(a.createEntity("Entorno"), en);
    const std::string text = serializeScene(a, reg);
    Scene b("b");
    CHECK(deserializeScene(text, b, reg).ok());
    const MeshRenderer* m = b.world().get<MeshRenderer>(b.findByName("Coche"));
    CHECK(m && std::abs(m->clearcoat - 0.8f) < 1e-5f && std::abs(m->clearcoatRoughness - 0.1f) < 1e-5f);
    const Environment* e = b.world().get<Environment>(b.findByName("Entorno"));
    CHECK(e && e->ssr && std::abs(e->ssrIntensity - 0.7f) < 1e-5f);
    // Sin los campos (escena de una versión anterior): sin barniz y sin reflejos en pantalla.
    MeshRenderer def;
    Environment edef;
    CHECK(def.clearcoat == 0.0f && !edef.ssr);
}
