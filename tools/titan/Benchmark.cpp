// "titan benchmark <carpeta>": proyecto de pruebas de rendimiento que se ejecuta solo, en el editor o exportado.
// Recorre escenas de 100, 500, 1000 y 1600 objetos, luces, partículas, física y materiales; mide durante 8 s cada
// una (tras 2 s de calentamiento) y guarda los resultados con Save y en el registro ("BENCHMARK ...").
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "titan/core/FileIO.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"

using namespace titan;

namespace {

const char* kMeasureScript = R"(-- Mide el rendimiento de la escena y pasa a la siguiente.
local M = { props = { siguiente = "", espera = 2, medir = 8 } }

local function nombre() return (Scene.name():match("([^/]+)%.scene$") or Scene.name()) end

function M:onStart()
    self.t, self.frames, self.time, self.peor = 0, 0, 0, 0
end

function M:onUpdate(dt)
    self.t = self.t + dt
    local s = Debug.stats()
    if self.t > self.espera then
        self.frames = self.frames + 1
        self.time = self.time + dt
        if dt * 1000 > self.peor then self.peor = dt * 1000 end
        self.dc, self.tri, self.part, self.cuerpos = s.drawCalls, s.triangles, s.particles, s.bodies
    end
    Screen.text(20, 20, 26, string.format("Benchmark: %s  %.0f FPS  %.1f ms CPU  %d draw calls  %d triángulos",
        nombre(), s.fps, s.frameMs, s.drawCalls, s.triangles))
    Screen.text(20, 54, 20, string.format("Midiendo... %.0f / %.0f s", math.max(0, self.t - self.espera), self.medir), 0.8, 0.8, 0.8)
    if self.t > self.espera + self.medir then
        local ms = self.time / math.max(1, self.frames) * 1000
        local r = Save.get("benchmark", {})
        r[nombre()] = { ms = ms, fps = 1000 / ms, peor = self.peor, drawCalls = self.dc, triangulos = self.tri,
                        particulas = self.part, cuerpos = self.cuerpos, calidad = Quality.get() }
        Save.set("benchmark", r)
        Save.write()
        Debug.log(string.format("BENCHMARK %s: %.2f ms por fotograma (%.0f FPS), peor %.1f ms, %d draw calls, %d triángulos, calidad %s",
            nombre(), ms, 1000 / ms, self.peor, self.dc, self.tri, Quality.get()))
        Scene.load(self.siguiente)
    end
end

return M
)";

const char* kResultsScript = R"(-- Tabla con los resultados guardados por medir.lua.
local R = {}
local orden = {"objetos_100", "objetos_500", "objetos_1000", "objetos_1600", "luces", "particulas", "fisica", "materiales"}

function R:onUpdate(dt)
    local r = Save.get("benchmark", {})
    local q = Quality.device()
    Screen.text(20, 20, 30, "Resultados del benchmark")
    Screen.text(20, 58, 18, (q.gpu or "?") .. " · " .. (q.memoryMB or 0) .. " MB · calidad " .. Quality.get(), 0.8, 0.8, 0.8)
    local y = 92
    for _, n in ipairs(orden) do
        local v = r[n]
        if v then
            Screen.text(20, y, 22, string.format("%-14s %6.1f FPS  %6.2f ms  peor %5.1f ms  %5d draw calls  %7d triáng.",
                n, v.fps, v.ms, v.peor, v.drawCalls or 0, v.triangulos or 0))
        else
            Screen.text(20, y, 22, n .. ": sin medir", 0.6, 0.6, 0.6)
        end
        y = y + 30
    end
    Screen.text(20, y + 10, 18, "Copia esta pantalla o el informe (Ayuda > Diagnóstico) y compártelo.", 0.8, 0.8, 0.8)
end

return R
)";

struct Ctx {
    Project* project;
    ComponentRegistry reg = ComponentRegistry::createDefault();
    UUID measure, results;
};

Entity baseScene(Scene& s, const Ctx& c, const std::string& next, float camDist = 22) {
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition({0, camDist * 0.4f, camDist});
    s.transform(cam).setEulerDegrees({-18, 0, 0});
    Camera cc;
    cc.projection = Camera::Projection::Perspective;
    s.world().add<Camera>(cam, cc);
    Entity env = s.createEntity("Cielo y niebla");
    s.world().add<Environment>(env);
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setEulerDegrees({-50, 30, 0});
    s.world().add<Light>(sun);
    Entity floor = s.createEntity("Suelo");
    s.transform(floor).setPosition({0, -0.5f, 0});
    s.transform(floor).setScale({200, 1, 200});
    MeshRenderer fm;
    fm.color = Color::hex(0x8C9099);
    fm.prototypeGrid = true;
    s.world().add<MeshRenderer>(floor, fm);
    s.world().add<Collider>(floor);
    Entity m = s.createEntity("Medidor");
    Script sc{c.measure, true};
    sc.props = std::string("{\"siguiente\": \"") + next + "\"}";
    s.world().add<Script>(m, sc);
    return cam;
}

void grid(Scene& s, int count, float spacing, MeshPrimitive mesh) {
    const int side = static_cast<int>(std::ceil(std::sqrt(float(count))));
    for (int i = 0; i < count; ++i) {
        Entity e = s.createEntity("Objeto");
        s.transform(e).setPosition({(i % side - side * 0.5f) * spacing, 0.5f, (i / side - side * 0.5f) * spacing});
        MeshRenderer mr;
        mr.mesh = mesh;
        mr.color = Color::hex(0xD0D4DC);
        s.world().add<MeshRenderer>(e, mr);
    }
}

bool save(Ctx& c, Scene& s, const std::string& name) {
    return !c.project->saveScene(s, c.reg, "scenes/" + name + ".scene").isNull();
}

} // namespace

int cmdBenchmark(const std::string& dir) {
    std::string err;
    auto p = Project::create(dir, "Benchmark Titán", ProjectDimension::ThreeD, &err);
    if (!p) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }
    Ctx c;
    c.project = p.get();
    std::filesystem::create_directories(p->assetsDir() + "/scripts");
    fileio::writeAtomic(p->assetsDir() + "/scripts/medir.lua", kMeasureScript, false);
    fileio::writeAtomic(p->assetsDir() + "/scripts/resultados.lua", kResultsScript, false);
    c.measure = p->assets().import("scripts/medir.lua");
    c.results = p->assets().import("scripts/resultados.lua");

    const int counts[] = {100, 500, 1000, 1600};
    const char* next[] = {"objetos_500", "objetos_1000", "objetos_1600", "luces"};
    UUID first;
    for (int k = 0; k < 4; ++k) {
        Scene s("objetos_" + std::to_string(counts[k]));
        baseScene(s, c, next[k]);
        grid(s, counts[k], 3, MeshPrimitive::Cube);
        const std::string n = "objetos_" + std::to_string(counts[k]);
        if (!save(c, s, n)) return 1;
        if (k == 0) first = p->assets().findByPath("scenes/" + n + ".scene")->id;
    }
    { // 400 objetos y 8 luces de colores (puntuales y focos)
        Scene s("luces");
        baseScene(s, c, "particulas");
        grid(s, 400, 2.5f, MeshPrimitive::Sphere);
        for (int i = 0; i < 8; ++i) {
            Entity l = s.createEntity("Luz");
            const float a = i * 0.785f;
            s.transform(l).setPosition({std::cos(a) * 12, 3, std::sin(a) * 12});
            s.transform(l).setEulerDegrees({-90, 0, 0});
            Light light;
            light.type = i % 2 ? Light::Type::Spot : Light::Type::Point;
            light.color = Color{0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a), 1, 1};
            light.intensity = 3;
            light.range = 14;
            s.world().add<Light>(l, light);
        }
        if (!save(c, s, "luces")) return 1;
    }
    { // 10 emisores de unas 600 partículas
        Scene s("particulas");
        baseScene(s, c, "fisica");
        for (int i = 0; i < 10; ++i) {
            Entity e = s.createEntity("Emisor");
            s.transform(e).setPosition({(i % 5 - 2) * 5.0f, 0.5f, (i / 5) * 6.0f - 3});
            ParticleSystem ps;
            ps.rate = 300;
            ps.maxParticles = 600;
            ps.lifetimeMin = 1.5f;
            ps.lifetimeMax = 2.0f;
            ps.speedMin = 1;
            ps.speedMax = 4;
            ps.spread = 30;
            ps.additive = i % 2 == 0;
            ps.colorStart = {1, 0.6f, 0.2f, 0.8f};
            ps.colorEnd = {0.3f, 0.3f, 0.3f, 0};
            s.world().add<ParticleSystem>(e, ps);
        }
        if (!save(c, s, "particulas")) return 1;
    }
    { // 400 cajas dinámicas que caen y se apilan
        Scene s("fisica");
        baseScene(s, c, "materiales");
        for (int i = 0; i < 400; ++i) {
            Entity e = s.createEntity("Caja");
            s.transform(e).setPosition({(i % 10 - 5) * 1.1f, 2 + (i / 100) * 1.2f, ((i / 10) % 10 - 5) * 1.1f});
            MeshRenderer mr;
            mr.color = Color::hex(0xE0A040);
            s.world().add<MeshRenderer>(e, mr);
            s.world().add<Collider>(e);
            s.world().add<Rigidbody>(e);
        }
        if (!save(c, s, "fisica")) return 1;
    }
    { // 225 esferas con metal, rugosidad y emisión distintos
        Scene s("materiales");
        Entity cam = baseScene(s, c, "resultados", 18);
        (void)cam;
        for (int i = 0; i < 225; ++i) {
            Entity e = s.createEntity("Esfera");
            s.transform(e).setPosition({(i % 15 - 7) * 1.3f, 0.5f, (i / 15 - 7) * 1.3f});
            MeshRenderer mr;
            mr.mesh = MeshPrimitive::Sphere;
            mr.color = Color{0.9f, 0.5f + 0.3f * (i % 3), 0.3f, 1};
            mr.metallic = (i % 15) / 14.0f;
            mr.roughness = (i / 15) / 14.0f;
            if (i % 17 == 0) mr.emission = {0.8f, 0.4f, 0.1f, 1};
            s.world().add<MeshRenderer>(e, mr);
        }
        if (!save(c, s, "materiales")) return 1;
    }
    { // Resultados
        Scene s("resultados");
        Entity cam = s.createEntity("Cámara");
        Camera cc;
        cc.clearColor = Color::hex(0x141B2B);
        s.world().add<Camera>(cam, cc);
        s.transform(cam).setPosition({0, 0, 10});
        Entity r = s.createEntity("Resultados");
        s.world().add<Script>(r, {c.results, true});
        if (!save(c, s, "resultados")) return 1;
    }
    p->setStartScene(first);
    if (!p->save()) return 1;
    std::printf("Proyecto de benchmark creado en %s (9 escenas). Ábrelo en el editor y pulsa Jugar, o expórtalo.\n",
                dir.c_str());
    return 0;
}
