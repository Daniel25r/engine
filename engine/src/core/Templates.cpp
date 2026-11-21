#include "titan/core/Templates.h"

#include <cmath>

#include "titan/core/FileIO.h"

namespace titan {

namespace {

const char* kPlayer2D = R"(-- Jugador de plataformas: toca a la izquierda o a la derecha de la pantalla
-- para moverte y en la mitad de arriba para saltar. Recoge todas las monedas.
local Jugador = {}

function Jugador:onStart()
    self.velocidad = 4
    self.vy = 0
    self.gravedad = -18
    self.salto = 9
    self.sueloY = 0.5          -- altura del centro del jugador cuando pisa el suelo
    self.monedas = 0
    self.total = #Entity.find("Monedas"):children()
end

function Jugador:onUpdate(dt)
    local x, y = self.entity:getPosition()
    local dir = 0
    for i = 1, Input.touchCount() do
        local t = Input.touch(i)
        if t.y < Screen.height() * 0.5 then
            if t.phase == "began" and y <= self.sueloY + 0.01 then self.vy = self.salto end
        elseif t.x < Screen.width() * 0.5 then
            dir = -1
        else
            dir = 1
        end
    end

    x = math.max(-7.5, math.min(7.5, x + dir * self.velocidad * dt))
    self.vy = self.vy + self.gravedad * dt
    y = y + self.vy * dt
    if y < self.sueloY then
        y = self.sueloY
        self.vy = 0
    end
    self.entity:setPosition(x, y, 0)

    -- Recoger monedas por distancia (los colisionadores llegarán con la física).
    for _, moneda in ipairs(Entity.find("Monedas"):children()) do
        local mx, my = moneda:getWorldPosition()
        if math.abs(mx - x) < 0.7 and math.abs(my - y) < 0.7 then
            moneda:destroy()
            self.monedas = self.monedas + 1
        end
    end
end

function Jugador:onLateUpdate(dt)
    local texto = "Monedas: " .. self.monedas .. " / " .. self.total
    if self.monedas == self.total then texto = "Has recogido todas las monedas!" end
    Screen.text(20, 20, 32, texto)
    Screen.text(20, Screen.height() - 50, 22, "Toca izquierda/derecha para moverte y arriba para saltar", 0.8, 0.8, 0.8)
end

return Jugador
)";

const char* kCoin = R"(-- Moneda: gira sobre sí misma.
local Moneda = {}

function Moneda:onUpdate(dt)
    self.entity:rotate(0, 0, 180 * dt)
end

return Moneda
)";

const char* kPlayer3D = R"(-- Jugador 3D: toca el suelo y el jugador camina hasta allí. Recoge todas las gemas.
local Jugador = {}

function Jugador:onStart()
    self.velocidad = 5
    self.destino = nil
    self.gemas = 0
    self.total = #Entity.find("Gemas"):children()
end

function Jugador:onUpdate(dt)
    if Input.touchCount() > 0 then
        local x, y, z = Input.touchWorld(1)   -- punto del suelo (y = 0) bajo el dedo
        if x then self.destino = {x, z} end
    end

    local px, py, pz = self.entity:getPosition()
    if self.destino then
        local dx, dz = self.destino[1] - px, self.destino[2] - pz
        local distancia = math.sqrt(dx * dx + dz * dz)
        if distancia > 0.05 then
            local paso = math.min(distancia, self.velocidad * dt)
            px = px + dx / distancia * paso
            pz = pz + dz / distancia * paso
            self.entity:setRotation(0, math.deg(math.atan(dx, dz)), 0)
        end
    end
    self.entity:setPosition(px, py, pz)

    for _, gema in ipairs(Entity.find("Gemas"):children()) do
        local gx, gy, gz = gema:getWorldPosition()
        if (gx - px) ^ 2 + (gz - pz) ^ 2 < 0.8 then
            gema:destroy()
            self.gemas = self.gemas + 1
        end
    end
end

function Jugador:onLateUpdate(dt)
    local texto = "Gemas: " .. self.gemas .. " / " .. self.total
    if self.gemas == self.total then texto = "Todas las gemas! Bien hecho" end
    Screen.text(20, 20, 32, texto)
    Screen.text(20, Screen.height() - 50, 22, "Toca el suelo para moverte", 0.8, 0.8, 0.8)
end

return Jugador
)";

const char* kGem = R"(-- Gema: gira y flota.
local Gema = {}

function Gema:onStart()
    local x, y, z = self.entity:getPosition()
    self.baseY = y
    self.fase = x + z
end

function Gema:onUpdate(dt)
    self.entity:rotate(0, 90 * dt, 0)
    local x, _, z = self.entity:getPosition()
    self.entity:setPosition(x, self.baseY + math.sin(Time.time() * 3 + self.fase) * 0.15, z)
end

return Gema
)";

const char* kFollowCamera = R"(-- Cámara que sigue al jugador manteniendo la distancia inicial.
local Camara = {}

function Camara:onStart()
    self.objetivo = Entity.find("Jugador")
    local x, y, z = self.entity:getPosition()
    local tx, ty, tz = self.objetivo:getPosition()
    self.offset = {x - tx, y - ty, z - tz}
end

function Camara:onLateUpdate(dt)
    if not self.objetivo:isValid() then return end
    local tx, ty, tz = self.objetivo:getPosition()
    local x, y, z = self.entity:getPosition()
    local k = math.min(1, 5 * dt)
    self.entity:setPosition(x + (tx + self.offset[1] - x) * k,
                            y + (ty + self.offset[2] - y) * k,
                            z + (tz + self.offset[3] - z) * k)
end

return Camara
)";

const char* kFollowCamera2D = R"(-- Cámara 2D que sigue al jugador en horizontal, sin salirse del nivel.
local Camara = {}

function Camara:onStart()
    self.objetivo = Entity.find("Jugador")
    self.limite = 4.5
end

function Camara:onLateUpdate(dt)
    if not self.objetivo:isValid() then return end
    local tx = self.objetivo:getPosition()
    local x, y, z = self.entity:getPosition()
    local destino = math.max(-self.limite, math.min(self.limite, tx))
    self.entity:setPosition(x + (destino - x) * math.min(1, 6 * dt), y, z)
end

return Camara
)";

UUID writeScript(Project& p, const std::string& rel, const char* code) {
    fileio::writeAtomic(p.assetsDir() + "/" + rel, code, false);
    return p.assets().import(rel);
}

Entity addSprite(Scene& s, const std::string& name, Vec3 pos, Vec2 size, Color color, Entity parent = {}, int order = 0) {
    Entity e = s.createEntity(name, parent);
    s.transform(e).setPosition(pos);
    SpriteRenderer sp;
    sp.size = size;
    sp.color = color;
    sp.sortingOrder = order;
    s.world().add<SpriteRenderer>(e, sp);
    return e;
}

Entity addMesh(Scene& s, const std::string& name, MeshPrimitive m, Vec3 pos, Vec3 scale, Color color, Entity parent = {}) {
    Entity e = s.createEntity(name, parent);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale(scale);
    MeshRenderer mr;
    mr.mesh = m;
    mr.color = color;
    s.world().add<MeshRenderer>(e, mr);
    return e;
}

Entity addCamera(Scene& s, bool perspective, Vec3 pos, Vec3 euler, Color clear) {
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition(pos);
    s.transform(cam).setEulerDegrees(euler);
    Camera c;
    c.projection = perspective ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
    c.clearColor = clear;
    s.world().add<Camera>(cam, c);
    return cam;
}

void addSun(Scene& s) {
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setPosition({0, 6, 0});
    s.transform(sun).setEulerDegrees({-55, 35, 0});
    Light l;
    l.color = Color::hex(0xFFF4E0);
    s.world().add<Light>(sun, l);
}

} // namespace

std::string newScriptSource(const std::string& className) {
    return "-- Script " + className + ". Añádelo a una entidad con el componente Script.\n"
           "local " + className + " = {}\n\n"
           "function " + className + ":onStart()\n"
           "    -- se ejecuta una vez, al empezar\n"
           "end\n\n"
           "function " + className + ":onUpdate(dt)\n"
           "    -- se ejecuta en cada fotograma; dt = segundos desde el anterior\n"
           "    -- ejemplo: self.entity:rotate(0, 90 * dt, 0)\n"
           "end\n\n"
           "return " + className + "\n";
}

std::unique_ptr<Project> createProjectFromTemplate(const std::string& dir, const std::string& name, ProjectTemplate t,
                                                   std::string* error) {
    const bool threeD = t == ProjectTemplate::Empty3D || t == ProjectTemplate::Explorer3D;
    auto p = Project::create(dir, name, threeD ? ProjectDimension::ThreeD : ProjectDimension::TwoD, error);
    if (!p) return nullptr;
    const ComponentRegistry reg = ComponentRegistry::createDefault();
    Scene s("principal");

    switch (t) {
    case ProjectTemplate::Empty2D:
        addCamera(s, false, {0, 0, 10}, {}, Color::hex(0x1B2230));
        break;
    case ProjectTemplate::Empty3D:
        addCamera(s, true, {0, 4, 8}, {-25, 0, 0}, Color::hex(0x4A6A8F));
        addSun(s);
        addMesh(s, "Suelo", MeshPrimitive::Plane, {0, 0, 0}, {20, 1, 20}, Color::hex(0x4C7A4F));
        addMesh(s, "Cubo", MeshPrimitive::Cube, {0, 0.5f, 0}, {1, 1, 1}, Color::hex(0xD0D4DC));
        break;
    case ProjectTemplate::Platformer2D: {
        const UUID player = writeScript(*p, "scripts/jugador.lua", kPlayer2D);
        const UUID coin = writeScript(*p, "scripts/moneda.lua", kCoin);
        const UUID follow = writeScript(*p, "scripts/camara.lua", kFollowCamera2D);
        Entity cam = addCamera(s, false, {-4.5f, 2, 10}, {}, Color::hex(0x7FB8E6));
        s.world().add<Script>(cam, {follow, true});
        addSprite(s, "Sol", {6, 5.2f, 0}, {1.4f, 1.4f}, Color::hex(0xFFE28A));
        addSprite(s, "Suelo", {0, -0.5f, 0}, {17, 1}, Color::hex(0x4C8A4F));
        addSprite(s, "Tierra", {0, -2, 0}, {17, 2}, Color::hex(0x6B4B33));
        Entity pl = addSprite(s, "Jugador", {-6, 0.5f, 0}, {1, 1}, Color::hex(0xF2B54A), {}, 2);
        s.world().add<Script>(pl, {player, true});
        Entity eyes = addSprite(s, "Ojos", {0.15f, 0.15f, 0}, {0.45f, 0.15f}, Color::hex(0x1B2230), pl, 3);
        (void)eyes;
        Entity coins = s.createEntity("Monedas");
        const float xs[] = {-3.5f, -1.5f, 0.5f, 2.5f, 4.5f, 6.5f};
        const float ys[] = {1.0f, 2.2f, 1.0f, 2.4f, 1.0f, 2.0f};
        for (int i = 0; i < 6; ++i) {
            Entity c = addSprite(s, "Moneda", {xs[i], ys[i], 0}, {0.5f, 0.5f}, Color::hex(0xFFD23F), coins, 1);
            s.world().add<Script>(c, {coin, true});
        }
        break;
    }
    case ProjectTemplate::Explorer3D: {
        const UUID player = writeScript(*p, "scripts/jugador.lua", kPlayer3D);
        const UUID gem = writeScript(*p, "scripts/gema.lua", kGem);
        const UUID follow = writeScript(*p, "scripts/camara.lua", kFollowCamera);
        Entity cam = addCamera(s, true, {0, 7, 8}, {-40, 0, 0}, Color::hex(0x8FB6DA));
        s.world().add<Script>(cam, {follow, true});
        addSun(s);
        addMesh(s, "Suelo", MeshPrimitive::Plane, {0, 0, 0}, {24, 1, 24}, Color::hex(0x5B8C51));
        Entity pl = addMesh(s, "Jugador", MeshPrimitive::Capsule, {0, 0.5f, 0}, {1, 1, 1}, Color::hex(0x3D7EDB));
        s.world().add<Script>(pl, {player, true});
        addMesh(s, "Visor", MeshPrimitive::Cube, {0, 0.18f, 0.22f}, {0.32f, 0.12f, 0.1f}, Color::hex(0x1B2230), pl);
        Entity trees = s.createEntity("Árboles");
        for (int i = 0; i < 10; ++i) {
            const float a = i * 0.628f, r = 8.0f + (i % 3);
            const Vec3 base{std::cos(a) * r, 0, std::sin(a) * r};
            addMesh(s, "Tronco", MeshPrimitive::Cylinder, base + Vec3{0, 0.5f, 0}, {0.3f, 1, 0.3f}, Color::hex(0x7A5230), trees);
            addMesh(s, "Copa", MeshPrimitive::Cone, base + Vec3{0, 1.8f, 0}, {1.4f, 1.8f, 1.4f}, Color::hex(0x2E7D46), trees);
        }
        addMesh(s, "Roca", MeshPrimitive::Sphere, {-3, 0.3f, -2}, {1.2f, 0.7f, 1}, Color::hex(0x8A8F99));
        addMesh(s, "Caja", MeshPrimitive::Cube, {3.5f, 0.5f, -1}, {1, 1, 1}, Color::hex(0xB98B52));
        Entity gems = s.createEntity("Gemas");
        const Vec3 spots[] = {{2, 0.6f, 2}, {-4, 0.6f, 3}, {5, 0.6f, -4}, {-2, 0.6f, -5}, {0, 0.6f, 5}};
        for (const Vec3& g : spots) {
            Entity e = addMesh(s, "Gema", MeshPrimitive::Sphere, g, {0.45f, 0.45f, 0.45f}, Color::hex(0xF7C948), gems);
            s.world().get<MeshRenderer>(e)->shininess = 64;
            s.world().add<Script>(e, {gem, true});
        }
        Entity lamp = s.createEntity("Farol");
        s.transform(lamp).setPosition({-3, 1.5f, 3});
        Light pl2;
        pl2.type = Light::Type::Point;
        pl2.color = Color::hex(0xFFB066);
        pl2.intensity = 1.5f;
        pl2.range = 6;
        s.world().add<Light>(lamp, pl2);
        break;
    }
    }

    const UUID scene = p->saveScene(s, reg, "scenes/principal.scene", error);
    if (scene.isNull()) return nullptr;
    p->setStartScene(scene);
    if (!p->save(error)) return nullptr;
    return p;
}

} // namespace titan
