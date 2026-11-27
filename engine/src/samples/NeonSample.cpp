// Proyecto de ejemplo "Ciudad de neón": escaparate del render de Titán. Una calle de noche hecha solo con primitivas,
// materiales por nodos y luces del motor (sin recursos de terceros): HDR con bloom, suelo mojado con reflejos (SSR),
// ventanas generadas con nodos, coche con barniz, farolas con luces puntuales, SSAO, sombras en cascada y TAA.
// Una cámara en Lua recorre la calle despacio.
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "titan/core/FileIO.h"
#include "titan/core/MaterialGraph.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"
#include "titan/samples/Samples.h"

using namespace titan;

namespace {

const char* kCameraLua = R"(-- Cámara del escaparate: recorre la calle despacio, con un leve balanceo.
local Camara = { props = { velocidad = 1.6, largo = 34 } }

function Camara:onStart()
    self.t = 0
end

function Camara:onUpdate(dt)
    self.t = self.t + dt
    local z = 14 - (self.t * self.velocidad) % self.largo
    local x = math.sin(self.t * 0.25) * 1.2
    self.entity:setPosition(x, 1.7 + math.sin(self.t * 0.5) * 0.15, z)
    self.entity:setRotation(-4, math.sin(self.t * 0.2) * 10, 0)
end

return Camara
)";

Entity box(Scene& s, const char* name, Vec3 pos, Vec3 scale, Color color, float rough = 0.6f, float metal = 0.0f,
           MeshPrimitive mesh = MeshPrimitive::Cube) {
    Entity e = s.createEntity(name);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale(scale);
    MeshRenderer mr;
    mr.mesh = mesh;
    mr.color = color;
    mr.roughness = rough;
    mr.metallic = metal;
    s.world().add<MeshRenderer>(e, mr);
    return e;
}

// Fachada con ventanas: rejilla = limitar(sin(u·F)) × limitar(sin(v·F)) (rectángulos separados por marcos) y ruido
// para que solo algunas estén encendidas. Todo con nodos: se puede abrir y modificar en el editor de materiales.
std::shared_ptr<MaterialGraph> facade(Color facadeColor, Color windowLight, float windows, float seedShift) {
    auto g = std::make_shared<MaterialGraph>();
    const int out = g->add(MatNode::Output, {900, 140});
    const int base = g->add(MatNode::Color, {660, 20});
    g->find(base)->value = facadeColor;
    const int rough = g->add(MatNode::Scalar, {660, 100});
    g->find(rough)->value.r = 0.7f;
    const int uv = g->add(MatNode::TexCoord, {20, 220});
    g->find(uv)->value = {windows, windows * 1.6f, 0, 0};
    const int s1 = g->add(MatNode::Sine, {200, 160});
    g->find(s1)->value.r = 6.2831853f;
    const int cl = g->add(MatNode::Clamp, {380, 160});
    const int pu = g->add(MatNode::Component, {380, 80});
    const int pv = g->add(MatNode::Component, {380, 240});
    g->find(pv)->value.r = 1;
    const int grid = g->add(MatNode::Multiply, {560, 160});
    const int sharp = g->add(MatNode::Power, {560, 260}); // ventanas con bordes definidos
    g->find(sharp)->value.r = 0.25f;
    const int noise = g->add(MatNode::Noise, {200, 360});
    g->find(noise)->value.r = 0.9f + seedShift;
    const int onoff = g->add(MatNode::Power, {380, 360}); // pocas encendidas
    g->find(onoff)->value.r = 3.0f;
    const int lit = g->add(MatNode::Multiply, {660, 300});
    const int light = g->add(MatNode::Color, {660, 400});
    g->find(light)->value = windowLight;
    const int glow = g->add(MatNode::Multiply, {800, 300});
    g->connect(base, out, 0);
    g->connect(rough, out, 2);
    g->connect(uv, s1, 0);
    g->connect(s1, cl, 0);
    g->connect(cl, pu, 0);
    g->connect(cl, pv, 0);
    g->connect(pu, grid, 0);
    g->connect(pv, grid, 1);
    g->connect(grid, sharp, 0);
    g->connect(uv, noise, 0);
    g->connect(noise, onoff, 0);
    g->connect(sharp, lit, 0);
    g->connect(onoff, lit, 1);
    g->connect(lit, glow, 0);
    g->connect(light, glow, 1);
    g->connect(glow, out, 3);
    return g;
}

bool buildCity(Project& p, UUID scriptId, UUID& sceneId) {
    Scene s("calle");
    Entity env = s.createEntity("Noche");
    Environment en;
    en.skyZenith = Color::hex(0x05040E);
    en.skyHorizon = Color::hex(0x1C1030);
    en.ground = Color::hex(0x050508);
    en.fogColor = Color::hex(0x1C1030);
    en.fogDensity = 0.035f;
    en.ambientIntensity = 0.45f;
    en.autoExposure = true; // el ojo se adapta a la noche
    en.shadows = true;
    en.shadowDistance = 40;
    en.bloom = true;
    en.bloomIntensity = 0.9f;
    en.bloomThreshold = 0.9f;
    en.vignette = 0.25f;
    en.saturation = 1.1f;
    en.ssao = en.ssr = en.taa = en.hdr = true;
    s.world().add<Environment>(env, en);
    Entity moon = s.createEntity("Luna");
    s.transform(moon).setEulerDegrees({-35, -40, 0});
    Light ml;
    ml.color = Color::hex(0x8CA0FF);
    ml.intensity = 0.25f;
    s.world().add<Light>(moon, ml);

    // Calle mojada (muy lisa: refleja los neones) y aceras.
    box(s, "Asfalto mojado", {0, 0, -2}, {9, 1, 60}, Color::hex(0x0C0D10), 0.07f, 0.0f, MeshPrimitive::Plane);
    box(s, "Acera izquierda", {-6, 0.08f, -2}, {3, 0.16f, 60}, Color::hex(0x2A2A30), 0.5f);
    box(s, "Acera derecha", {6, 0.08f, -2}, {3, 0.16f, 60}, Color::hex(0x2A2A30), 0.5f);
    for (int i = 0; i < 12; ++i) // marcas viales
        box(s, "Marca vial", {0, 0.01f, 22.0f - i * 5.0f}, {0.15f, 0.01f, 2.0f}, Color::hex(0xD8D2B0), 0.5f);

    // Edificios a los dos lados con ventanas por nodos.
    const Color facades[] = {Color::hex(0x1A1C24), Color::hex(0x221A20), Color::hex(0x16202A), Color::hex(0x1E1E1E)};
    const Color lights[] = {Color::hex(0xFFC27A), Color::hex(0x9FD8FF), Color::hex(0xFFE3B0), Color::hex(0xFF9AD5)};
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < 7; ++i) {
            const float h = 8.0f + float((i * 7 + (side > 0 ? 3 : 0)) % 5) * 3.0f;
            const float z = 18.0f - i * 7.5f;
            Entity b = box(s, "Edificio", {side * 11.5f, h * 0.5f, z}, {8, h, 6.8f}, facades[i % 4]);
            s.world().get<MeshRenderer>(b)->graph = facade(facades[i % 4], lights[(i + (side > 0 ? 1 : 0)) % 4], 6.0f + float(i % 3), float(i) * 0.37f);
        }

    // Letreros de neón.
    const Color neon[] = {Color::hex(0xFF2E9A), Color::hex(0x23E6FF), Color::hex(0xFFE03A), Color::hex(0x9B5CFF), Color::hex(0x3DFF8C)};
    for (int i = 0; i < 10; ++i) {
        const int side = i % 2 ? 1 : -1;
        const float z = 15.0f - i * 4.2f;
        const Color c = neon[i % 5];
        Entity n = box(s, "Neón", {side * 7.45f, 3.2f + float(i % 3) * 1.1f, z}, {0.12f, 0.12f, 2.2f}, c, 0.4f);
        s.world().get<MeshRenderer>(n)->emission = c;
        s.world().get<MeshRenderer>(n)->castShadows = false;
        Entity v = box(s, "Neón", {side * 7.45f, 3.2f + float(i % 3) * 1.1f - 0.6f, z + 1.0f}, {0.12f, 1.3f, 0.12f}, c, 0.4f);
        s.world().get<MeshRenderer>(v)->emission = c;
        s.world().get<MeshRenderer>(v)->castShadows = false;
    }

    // Farolas con luz puntual (como mucho 4 luces locales en el perfil alto).
    for (int i = 0; i < 4; ++i) {
        const int side = i % 2 ? 1 : -1;
        const float z = 12.0f - i * 9.0f;
        box(s, "Farola", {side * 4.9f, 2.2f, z}, {0.12f, 4.4f, 0.12f}, Color::hex(0x202226), 0.4f, 0.9f, MeshPrimitive::Cylinder);
        Entity lamp = box(s, "Bombilla", {side * 4.6f, 4.35f, z}, {0.5f, 0.12f, 0.25f}, Color::hex(0xFFE2B0), 0.4f);
        s.world().get<MeshRenderer>(lamp)->emission = Color::hex(0xFFD08A);
        s.world().get<MeshRenderer>(lamp)->castShadows = false;
        Entity pl = s.createEntity("Luz de farola");
        s.transform(pl).setPosition({side * 4.4f, 4.0f, z});
        Light l;
        l.type = Light::Type::Point;
        l.color = Color::hex(0xFFC98A);
        l.intensity = 2.2f;
        l.range = 9.0f;
        s.world().add<Light>(pl, l);
    }

    // Coche con barniz (carrocería, cabina, ruedas y faros).
    Entity car = s.createEntity("Coche");
    s.transform(car).setPosition({3.0f, 0, 2}); // en el carril derecho: la cámara pasa a su lado
    auto part = [&](const char* name, Vec3 pos, Vec3 scale, Color c, MeshPrimitive mesh = MeshPrimitive::Cube) {
        Entity e = box(s, name, pos, scale, c, 0.45f, 0.0f, mesh);
        s.setParent(e, car, false);
        return s.world().get<MeshRenderer>(e);
    };
    MeshRenderer* body = part("Carrocería", {0, 0.55f, 0}, {1.8f, 0.55f, 4.2f}, Color::hex(0xB0101A));
    body->clearcoat = 1.0f;
    body->clearcoatRoughness = 0.04f;
    MeshRenderer* cab = part("Cabina", {0, 1.05f, -0.2f}, {1.6f, 0.5f, 2.0f}, Color::hex(0x101216));
    cab->roughness = 0.05f;
    cab->metallic = 0.2f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            Entity w = box(s, "Rueda", {sx * 0.92f, 0.36f, sz * 1.35f}, {0.72f, 0.28f, 0.72f}, Color::hex(0x0A0A0A), 0.8f, 0.0f,
                           MeshPrimitive::Cylinder);
            s.transform(w).setEulerDegrees({0, 0, 90});
            s.setParent(w, car, false);
        }
    for (int sx = -1; sx <= 1; sx += 2) {
        MeshRenderer* hl = part("Faro", {sx * 0.6f, 0.62f, -2.11f}, {0.38f, 0.14f, 0.05f}, Color::hex(0xFFFFFF));
        hl->emission = Color::hex(0xEAF4FF);
        hl->castShadows = false;
        MeshRenderer* tl = part("Piloto", {sx * 0.65f, 0.65f, 2.11f}, {0.3f, 0.1f, 0.05f}, Color::hex(0xFF2020));
        tl->emission = Color::hex(0xFF1010);
        tl->castShadows = false;
    }

    // Cámara que recorre la calle.
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition({0, 1.7f, 14});
    s.transform(cam).setEulerDegrees({-4, 0, 0});
    Camera cc;
    cc.projection = Camera::Projection::Perspective;
    cc.fieldOfView = 55;
    cc.clearColor = Color::hex(0x05040E);
    s.world().add<Camera>(cam, cc);
    Script sc{scriptId, true};
    s.world().add<Script>(cam, sc);

    const std::string path = "scenes/calle.scene";
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    if (p.saveScene(s, reg, path).isNull()) return false;
    sceneId = p.assets().findByPath(path)->id;
    return true;
}

} // namespace

bool titan::createNeonSample(const std::string& dir, const std::string& name, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::string err;
    auto p = Project::create(dir, name.empty() ? "Ciudad de neón" : name, ProjectDimension::ThreeD, &err);
    if (!p) return fail(err);
    std::filesystem::create_directories(p->assetsDir() + "/scripts");
    if (!fileio::writeAtomic(p->assetsDir() + "/scripts/camara.lua", kCameraLua, false)) return fail("no se pudo escribir camara.lua");
    const UUID script = p->assets().import("scripts/camara.lua");
    UUID scene;
    if (!buildCity(*p, script, scene)) return fail("no se pudo guardar la escena");
    p->setStartScene(scene);
    if (!p->save()) return fail("no se pudo guardar el proyecto");
    return true;
}
