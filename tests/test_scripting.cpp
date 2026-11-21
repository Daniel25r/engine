#include <map>

#include "Test.h"
#include "titan/scripting/ScriptSystem.h"

using namespace titan;

namespace {

class FakeHost final : public ScriptHost {
public:
    Input input;
    std::string requestedScene;
    double scriptTime() const override { return 1.5; }
    float scriptDeltaTime() const override { return 0.016f; }
    float scriptFixedDeltaTime() const override { return 1.0f / 60.0f; }
    uint64_t scriptFrame() const override { return 7; }
    const Input& scriptInput() const override { return input; }
    Vec2 scriptScreenSize() const override { return {800, 600}; }
    bool scriptScreenToWorld(Vec2 p, Vec3& w) const override { w = {p.x / 100, p.y / 100, 0}; return true; }
    bool scriptLoadScene(const std::string& n) override { requestedScene = n; return true; }
    void scriptDrawText(float, float, float, const std::string& t, Color) override { lastText = t; }
    std::string lastText;
};

struct Fixture {
    FakeHost host;
    ComponentRegistry registry = ComponentRegistry::createDefault();
    std::map<uint64_t, std::pair<std::string, std::string>> files; // id -> (nombre, código)
    EventBus bus;
    Scene scene{"Prueba", &bus};
    ScriptSystem scripts{host, registry, [this](AssetRef id, std::string& name, std::string& src) {
                             auto it = files.find(id.value);
                             if (it == files.end()) return false;
                             name = it->second.first;
                             src = it->second.second;
                             return true;
                         }};

    AssetRef addScript(const std::string& name, const std::string& code) {
        const AssetRef id = UUID::generate();
        files[id.value] = {name, code};
        return id;
    }
    Entity entityWith(const std::string& name, AssetRef script) {
        Entity e = scene.createEntity(name);
        scene.world().add<Script>(e, {script, true});
        return e;
    }
};

} // namespace

TEST("scripts: ciclo de vida onCreate, onStart, onUpdate, onFixedUpdate, onLateUpdate, onDestroy") {
    Fixture f;
    const AssetRef s = f.addScript("vida.lua", R"(
        local C = {}
        log = {}
        function C:onCreate() table.insert(log, "create") end
        function C:onStart() table.insert(log, "start") end
        function C:onFixedUpdate(dt) table.insert(log, "fixed") end
        function C:onUpdate(dt) table.insert(log, "update") end
        function C:onLateUpdate(dt) table.insert(log, "late") end
        function C:onDestroy() table.insert(log, "destroy") end
        return C)");
    Entity e = f.entityWith("Obj", s);
    f.scripts.attach(f.scene, f.bus);
    f.scripts.fixedUpdate(0.016f);
    f.scripts.update(0.016f);
    f.scripts.lateUpdate(0.016f);
    f.scene.destroyEntity(e);
    f.scene.flushDestroyed();
    CHECK(f.scripts.runString(R"(assert(table.concat(log, ",") == "create,start,fixed,update,late,destroy", table.concat(log, ",")))"));
    CHECK(f.scripts.errors().empty());
    CHECK_EQ(f.scripts.instanceCount(), 0u);
}

TEST("scripts: mueven entidades reales de la escena") {
    Fixture f;
    const AssetRef s = f.addScript("mover.lua", R"(
        local M = {}
        function M:onUpdate(dt) self.entity:translate(10 * dt, 0, 0) self.entity:rotate(0, 0, 90) end
        return M)");
    Entity e = f.entityWith("Nave", s);
    f.scripts.attach(f.scene, f.bus);
    f.scripts.update(0.5f);
    CHECK_VEC3(f.scene.transform(e).position(), 5, 0, 0);
    CHECK_NEAR(f.scene.transform(e).eulerDegrees().z, 90, 1e-3);
}

TEST("scripts: API de entidades, componentes y jerarquía") {
    Fixture f;
    const AssetRef s = f.addScript("api.lua", R"(
        local A = {}
        function A:onStart()
            local hijo = Entity.create("Hijo", self.entity)
            assert(hijo:parent() == self.entity)
            assert(#self.entity:children() == 1)
            hijo:addComponent("SpriteRenderer")
            hijo:setColor(1, 0, 0)
            local r, g, b, a = hijo:getColor()
            assert(r == 1 and g == 0 and a == 1)
            assert(hijo:hasComponent("SpriteRenderer"))
            hijo:setPosition(1, 2, 3)
            self.entity:setPosition(10, 0, 0)
            local x, y, z = hijo:getWorldPosition()
            assert(x == 11 and y == 2 and z == 3, x)
            assert(Entity.find("Hijo") == hijo)
            assert(Entity.get(hijo:id()) == hijo)
            assert(Time.time() == 1.5 and Time.frame() == 7)
            assert(Screen.width() == 800)
            Scene.load("Nivel2")
            self.listo = true
        end
        return A)");
    Entity e = f.entityWith("Padre", s);
    f.scripts.attach(f.scene, f.bus);
    CHECK(f.scripts.errors().empty());
    for (auto& err : f.scripts.errors()) std::printf("      %s\n", err.message.c_str());
    CHECK(!f.scene.findByName("Hijo").isNull());
    CHECK(f.scene.world().has<SpriteRenderer>(f.scene.findByName("Hijo")));
    CHECK_EQ(f.host.requestedScene, std::string("Nivel2"));
    CHECK(f.scripts.runString("assert(Entity.find('Padre'):getScript().listo == true)"));
    (void)e;
}

TEST("scripts: los errores indican archivo, línea y función, y no detienen a los demás") {
    Fixture f;
    const AssetRef bad = f.addScript("scripts/roto.lua", "local R = {}\nfunction R:onUpdate(dt)\n  local x = nil\n  return x.campo\nend\nreturn R");
    const AssetRef good = f.addScript("bien.lua", "local B = {}\nfunction B:onUpdate(dt) self.entity:translate(1,0,0) end\nreturn B");
    f.entityWith("Malo", bad);
    Entity ok = f.entityWith("Bueno", good);
    f.scripts.attach(f.scene, f.bus);
    f.scripts.update(0.016f);
    f.scripts.update(0.016f);
    CHECK_EQ(f.scripts.errors().size(), 1u); // la instancia rota se desactiva tras el primer error
    const ScriptError& err = f.scripts.errors().front();
    CHECK_EQ(err.script, std::string("scripts/roto.lua"));
    CHECK_EQ(err.function, std::string("onUpdate"));
    CHECK_EQ(err.entity, std::string("Malo"));
    CHECK(err.message.find("scripts/roto.lua:4:") != std::string::npos);
    CHECK(!err.traceback.empty());
    CHECK_VEC3(f.scene.transform(ok).position(), 2, 0, 0);
}

TEST("scripts: errores de sintaxis y scripts que no devuelven tabla") {
    Fixture f;
    f.entityWith("A", f.addScript("sintaxis.lua", "local = 3"));
    f.entityWith("B", f.addScript("sin_return.lua", "local X = {}"));
    f.entityWith("C", UUID::generate()); // el archivo no existe
    f.scripts.attach(f.scene, f.bus);
    CHECK_EQ(f.scripts.errors().size(), 3u);
    bool syntaxReported = false, returnReported = false, missingReported = false;
    for (const ScriptError& e : f.scripts.errors()) {
        syntaxReported |= e.script == "sintaxis.lua" && e.message.find("sintaxis.lua:1:") != std::string::npos;
        returnReported |= e.script == "sin_return.lua" && e.message.find("return") != std::string::npos;
        missingReported |= e.message.find("no se encuentra") != std::string::npos;
    }
    CHECK(syntaxReported);
    CHECK(returnReported);
    CHECK(missingReported);
    CHECK_EQ(f.scripts.instanceCount(), 0u);
}

TEST("scripts: un bucle infinito se corta por tiempo y el sistema sigue funcionando") {
    Fixture f;
    f.scripts.setTimeBudget(0.05);
    const AssetRef s = f.addScript("bucle.lua", "local B = {}\nfunction B:onUpdate(dt) while true do end end\nreturn B");
    f.entityWith("Colgado", s);
    f.scripts.attach(f.scene, f.bus);
    f.scripts.update(0.016f);
    CHECK_EQ(f.scripts.errors().size(), 1u);
    CHECK(f.scripts.errors()[0].message.find("tiempo") != std::string::npos);
    CHECK(f.scripts.runString("x = 1 + 1"));
}

TEST("scripts: límite de memoria") {
    Fixture f;
    f.scripts.setMemoryLimit(8u * 1024u * 1024u);
    CHECK(!f.scripts.runString("local t = {} for i = 1, 1e8 do t[i] = ('x'):rep(100) .. i end"));
    CHECK(f.scripts.errors().back().message.find("memory") != std::string::npos);
    CHECK(f.scripts.runString("collectgarbage() y = 2"));
}

TEST("scripts: Screen.text con y sin color") {
    Fixture f;
    CHECK(f.scripts.runString("Screen.text(10, 20, 24, 'Puntos: ' .. 5)"));
    CHECK_EQ(f.host.lastText, std::string("Puntos: 5"));
    CHECK(f.scripts.runString("Screen.text(10, 20, 24, 'Gris', 0.8, 0.8, 0.8)"));
    CHECK_EQ(f.host.lastText, std::string("Gris"));
}

TEST("scripts: sin acceso al sistema (io, os, load, require)") {
    Fixture f;
    CHECK(f.scripts.runString("assert(io == nil and os == nil and load == nil and require == nil and dofile == nil)"));
    CHECK(f.scripts.runString("assert(string.dump == nil and debug == nil and package == nil)"));
}

TEST("scripts: usar una entidad destruida da un error claro") {
    Fixture f;
    const AssetRef s = f.addScript("guarda.lua", R"(
        local G = {}
        function G:onStart() otro = Entity.find("Otro") end
        function G:onUpdate(dt) otro:getPosition() end
        return G)");
    f.entityWith("Guardián", s);
    Entity other = f.scene.createEntity("Otro");
    f.scripts.attach(f.scene, f.bus);
    f.scene.destroyEntity(other);
    f.scene.flushDestroyed();
    f.scripts.update(0.016f);
    CHECK_EQ(f.scripts.errors().size(), 1u);
    CHECK(f.scripts.errors()[0].message.find("ya no existe") != std::string::npos);
}

TEST("scripts: setActive llama onDisable/onEnable y detiene onUpdate") {
    Fixture f;
    const AssetRef s = f.addScript("activo.lua", R"(
        local A = {}
        cuentas = {update = 0, enable = 0, disable = 0}
        function A:onUpdate() cuentas.update = cuentas.update + 1 end
        function A:onEnable() cuentas.enable = cuentas.enable + 1 end
        function A:onDisable() cuentas.disable = cuentas.disable + 1 end
        return A)");
    f.entityWith("X", s);
    f.scripts.attach(f.scene, f.bus);
    f.scripts.update(0.1f);
    CHECK(f.scripts.runString("Entity.find('X'):setActive(false)"));
    f.scripts.update(0.1f);
    CHECK(f.scripts.runString("Entity.find('X'):setActive(true)"));
    f.scripts.update(0.1f);
    CHECK(f.scripts.runString("assert(cuentas.update == 2 and cuentas.enable == 1 and cuentas.disable == 1)"));
}
