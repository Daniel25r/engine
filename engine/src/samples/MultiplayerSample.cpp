// Proyecto de ejemplo "Plaza multijugador": varios jugadores en la misma escena por red (Net.* sobre ENet).
// H crea la partida (servidor) y C se une (dirección del script, 127.0.0.1 por defecto). Cada uno mueve su cápsula con
// WASD; el servidor reenvía las posiciones de todos. Sirve de plantilla para juegos en línea.
#include <filesystem>
#include <string>

#include "titan/core/FileIO.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"
#include "titan/samples/Samples.h"

using namespace titan;

namespace {
const char* kNetLua = R"(-- Multijugador: H = crear partida (servidor), C = unirse. WASD mueve tu cápsula y ves a los demás.
-- El servidor es la autoridad: recibe la posición de cada cliente y la reenvía a todos (20 veces por segundo).
local Red = { props = { puerto = 7777, direccion = "127.0.0.1", velocidad = 5 } }

function Red:onStart()
    self.yo = Entity.find("Tú")
    self.estado = Entity.find("Estado")
    self.libres = {}   -- cápsulas para los demás jugadores (ocultas hasta que haya alguien)
    for i = 1, 7 do
        local e = Entity.find("Otro " .. i)
        e:setActive(false)
        table.insert(self.libres, e)
    end
    self.otros = {}    -- id del jugador -> cápsula
    self.pos = {}      -- servidor: id -> {x, z}
    self.reloj = 0
    self:mostrar("H: crear partida   C: unirse a " .. self.direccion)
end

function Red:mostrar(t) self.estado:setText(t) end

function Red:otro(id)
    if self.otros[id] == nil and #self.libres > 0 then
        self.otros[id] = table.remove(self.libres)
        self.otros[id]:setActive(true)
    end
    return self.otros[id]
end

function Red:quitar(id)
    local e = self.otros[id]
    if e then e:setActive(false); table.insert(self.libres, e); self.otros[id] = nil end
    self.pos[id] = nil
end

function Red:onUpdate(dt)
    if not Net.connected() and not Net.isServer() then
        if Input.keyPressed("H") then
            local ok, err = Net.host(self.puerto, 8)
            self:mostrar(ok and ("Servidor en el puerto " .. self.puerto .. ". Esperando jugadores...") or err)
        elseif Input.keyPressed("C") then
            local ok, err = Net.connect(self.direccion, self.puerto)
            self:mostrar(ok and ("Conectando a " .. self.direccion .. "...") or err)
        end
    end
    -- Mover la cápsula propia.
    local mx, mz = Input.cameraAxis("Mover")
    local x, y, z = self.yo:getPosition()
    x, z = x + mx * self.velocidad * dt, z + mz * self.velocidad * dt
    self.yo:setPosition(x, y, z)

    for _, ev in ipairs(Net.receive()) do
        if ev.type == "connect" then
            self:mostrar(Net.isServer() and ("Jugadores conectados: " .. #Net.peers()) or "Conectado al servidor")
        elseif ev.type == "disconnect" then
            if Net.isServer() then self:quitar(ev.peer) else self:mostrar("El servidor cerró la partida") end
            if Net.isServer() then self:mostrar("Jugadores conectados: " .. #Net.peers()) end
        elseif ev.type == "message" and type(ev.data) == "table" then
            local d = ev.data
            if Net.isServer() and d.t == "pos" then
                self.pos[ev.peer] = { d.x, d.z }
                local e = self:otro(ev.peer)
                if e then e:setPosition(d.x, 0.9, d.z) end
            elseif not Net.isServer() and d.t == "todos" then
                local vistos = {}
                for _, p in ipairs(d.lista) do
                    if p.id ~= d.tu then
                        vistos[p.id] = true
                        local e = self:otro(p.id)
                        if e then e:setPosition(p.x, 0.9, p.z) end
                    end
                end
                for id in pairs(self.otros) do if not vistos[id] then self:quitar(id) end end
            end
        end
    end

    -- 20 veces por segundo: el cliente manda su posición; el servidor manda la de todos a cada uno.
    self.reloj = self.reloj + dt
    if self.reloj >= 0.05 then
        self.reloj = 0
        if Net.isServer() then
            local lista = { { id = 0, x = x, z = z } }
            for id, p in pairs(self.pos) do table.insert(lista, { id = id, x = p[1], z = p[2] }) end
            for _, id in ipairs(Net.peers()) do Net.send({ t = "todos", tu = id, lista = lista }, id, false) end
        elseif Net.connected() then
            Net.send({ t = "pos", x = x, z = z }, nil, false)
        end
    end
end

return Red
)";

Entity capsule(Scene& s, const char* name, Vec3 pos, Color c) {
    Entity e = s.createEntity(name);
    s.transform(e).setPosition(pos);
    s.transform(e).setScale({0.8f, 1.8f, 0.8f});
    MeshRenderer mr;
    mr.mesh = MeshPrimitive::Capsule;
    mr.color = c;
    s.world().add<MeshRenderer>(e, mr);
    return e;
}
} // namespace

bool titan::createMultiplayerSample(const std::string& dir, const std::string& name, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::string err;
    auto p = Project::create(dir, name.empty() ? "Plaza multijugador" : name, ProjectDimension::ThreeD, &err);
    if (!p) return fail(err);
    std::filesystem::create_directories(p->assetsDir() + "/scripts");
    if (!fileio::writeAtomic(p->assetsDir() + "/scripts/red.lua", kNetLua, false)) return fail("no se pudo escribir red.lua");
    const UUID script = p->assets().import("scripts/red.lua");

    Scene s("plaza");
    Entity env = s.createEntity("Cielo");
    Environment en;
    en.ssao = en.taa = true;
    s.world().add<Environment>(env, en);
    Entity sun = s.createEntity("Sol");
    s.transform(sun).setEulerDegrees({-50, 30, 0});
    s.world().add<Light>(sun);
    Entity floor = s.createEntity("Plaza");
    s.transform(floor).setScale({30, 1, 30});
    MeshRenderer fm;
    fm.mesh = MeshPrimitive::Plane;
    fm.color = Color::hex(0x8A9098);
    fm.prototypeGrid = true;
    s.world().add<MeshRenderer>(floor, fm);
    Entity me = capsule(s, "Tú", {0, 0.9f, 0}, Color::hex(0x3B7DD8));
    Script sc{script, true};
    s.world().add<Script>(me, sc);
    const Color others[] = {Color::hex(0xD64545), Color::hex(0x45B86A), Color::hex(0xE8B23A), Color::hex(0x9B5CFF),
                            Color::hex(0xFF7AB8), Color::hex(0x35C9D8), Color::hex(0xF07A2A)};
    for (int i = 0; i < 7; ++i) capsule(s, ("Otro " + std::to_string(i + 1)).c_str(), {float(i) * 1.5f - 4.5f, 0.9f, -4}, others[i]);
    Entity cam = s.createEntity("Cámara");
    s.transform(cam).setPosition({0, 9, 12});
    s.transform(cam).setEulerDegrees({-35, 0, 0});
    Camera cc;
    cc.projection = Camera::Projection::Perspective;
    s.world().add<Camera>(cam, cc);
    Entity txt = s.createEntity("Estado");
    UIRect r;
    r.anchorMin = r.anchorMax = {0.5f, 0};
    r.pivot = {0.5f, 0};
    r.position = {0, 20};
    r.size = {1000, 50};
    s.world().add<UIRect>(txt, r);
    UIText t;
    t.size = 30;
    s.world().add<UIText>(txt, t);

    const ComponentRegistry reg = ComponentRegistry::createDefault();
    if (p->saveScene(s, reg, "scenes/plaza.scene").isNull()) return fail("no se pudo guardar la escena");
    p->setStartScene(p->assets().findByPath("scenes/plaza.scene")->id);
    if (!p->save()) return fail("no se pudo guardar el proyecto");
    return true;
}
