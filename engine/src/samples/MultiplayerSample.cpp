// Proyecto de ejemplo "Plaza multijugador": varios jugadores en la misma escena por red (Net.* sobre ENet).
// H crea la partida (servidor) y C se une (dirección del script, 127.0.0.1 por defecto). Cada uno mueve su cápsula con
// WASD; la replicación automática (NetSync) envía y aplica las posiciones. Plantilla para juegos en línea.
#include <filesystem>
#include <string>

#include "titan/core/FileIO.h"
#include "titan/core/Project.h"
#include "titan/core/Serialization.h"
#include "titan/samples/Samples.h"

using namespace titan;

namespace {
const char* kNetLua = R"LUA(-- Multijugador con replicación automática: cada cápsula tiene NetSync con un dueño (0 = servidor,
-- 1..7 = jugadores). Cada equipo mueve la suya (e:isMine()) y el motor envía y aplica las posiciones solo.
-- H = crear partida (servidor), C = unirse. WASD mueve tu cápsula.
local Red = { props = { puerto = 7777, direccion = "127.0.0.1", velocidad = 5 } }

function Red:onStart()
    self.estado = Entity.find("Estado")
    self.capsulas = {}
    for id = 0, 7 do
        local e = Entity.find("Jugador " .. id)
        e:setActive(id == 0) -- la del servidor se ve; las demás cuando entran
        self.capsulas[id] = e
    end
    self:mostrar("H: crear partida   C: unirse a " .. self.direccion)
end

function Red:mostrar(t) self.estado:setText(t) end

-- El servidor dice a todos quién está jugando (al entrar o salir alguien).
function Red:anunciar()
    local ids = { 0 }
    for _, id in ipairs(Net.peers()) do table.insert(ids, id) end
    Net.send({ t = "jugadores", ids = ids })
    self:verSolo(ids)
    self:mostrar("Jugadores: " .. #ids .. " (tú eres el servidor)")
end

function Red:verSolo(ids)
    local en = {}
    for _, id in ipairs(ids) do en[id] = true end
    for id, e in pairs(self.capsulas) do e:setActive(en[id] == true) end
end

function Red:onUpdate(dt)
    if not Net.connected() and not Net.isServer() then
        if Input.keyPressed("H") then
            local ok, err = Net.host(self.puerto, 7)
            self:mostrar(ok and ("Servidor en el puerto " .. self.puerto) or err)
        elseif Input.keyPressed("C") then
            local ok, err = Net.connect(self.direccion, self.puerto)
            self:mostrar(ok and ("Conectando a " .. self.direccion .. "...") or err)
        end
    end
    for _, ev in ipairs(Net.receive()) do
        if Net.isServer() and (ev.type == "connect" or ev.type == "disconnect") then self:anunciar() end
        if not Net.isServer() and ev.type == "message" and type(ev.data) == "table" and ev.data.t == "jugadores" then
            self:verSolo(ev.data.ids)
            self:mostrar("Jugadores: " .. #ev.data.ids .. " (tú eres el " .. Net.myId() .. ")")
        end
        if not Net.isServer() and ev.type == "disconnect" then self:mostrar("El servidor cerró la partida") end
    end
    -- Mover solo la cápsula propia; las demás las mueve la red.
    local mx, mz = Input.cameraAxis("Mover")
    for _, e in pairs(self.capsulas) do
        if e:isActive() and e:isMine() then
            local x, y, z = e:getPosition()
            e:setPosition(x + mx * self.velocidad * dt, y, z + mz * self.velocidad * dt)
        end
    end
end

return Red
)LUA";

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
    // Una cápsula por jugador posible, con NetSync: la mueve su dueño y la red la replica en los demás equipos.
    const Color colors[] = {Color::hex(0x3B7DD8), Color::hex(0xD64545), Color::hex(0x45B86A), Color::hex(0xE8B23A),
                            Color::hex(0x9B5CFF), Color::hex(0xFF7AB8), Color::hex(0x35C9D8), Color::hex(0xF07A2A)};
    for (int i = 0; i < 8; ++i) {
        Entity c = capsule(s, ("Jugador " + std::to_string(i)).c_str(), {float(i) * 1.5f - 5.25f, 0.9f, 0}, colors[i]);
        NetSync ns;
        ns.owner = i;
        s.world().add<NetSync>(c, ns);
    }
    Entity logic = s.createEntity("Red");
    Script sc{script, true};
    s.world().add<Script>(logic, sc);
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
