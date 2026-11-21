#include "titan/scripting/ScriptSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

// Lua se compila como C++ (ver CMakeLists.txt): sus errores son excepciones y los
// destructores de C++ se ejecutan correctamente al propagarse un error.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "titan/core/Log.h"

namespace titan {

namespace {

const char* kEntityMeta = "Titan.Entity";

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

ScriptSystem& sys(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return *static_cast<ScriptSystem*>(ud);
}

Scene& scene(lua_State* L) {
    Scene* s = sys(L).scene();
    if (!s) luaL_error(L, "no hay ninguna escena cargada");
    return *s;
}

void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return static_cast<ScriptSystem*>(ud)->allocate(ptr, osize, nsize);
}

void timeHook(lua_State* L, lua_Debug*) {
    if (sys(L).deadlinePassed())
        luaL_error(L, "el script superó el tiempo máximo por llamada (¿un bucle infinito?)");
}

int traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) msg = luaL_tolstring(L, 1, nullptr);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

// ---------------------------------------------------------------- Entity

struct LuaEntity {
    uint32_t index;
    uint32_t generation;
};

void pushEntity(lua_State* L, Entity e) {
    if (e.isNull()) {
        lua_pushnil(L);
        return;
    }
    auto* u = static_cast<LuaEntity*>(lua_newuserdatauv(L, sizeof(LuaEntity), 0));
    *u = {e.index, e.generation};
    luaL_setmetatable(L, kEntityMeta);
}

Entity toEntity(lua_State* L, int idx) {
    auto* u = static_cast<LuaEntity*>(luaL_checkudata(L, idx, kEntityMeta));
    return {u->index, u->generation};
}

Entity live(lua_State* L, int idx) {
    const Entity e = toEntity(L, idx);
    if (!scene(L).isValid(e)) luaL_error(L, "la entidad ya no existe (fue destruida)");
    return e;
}

int push3(lua_State* L, const Vec3& v) {
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    lua_pushnumber(L, v.z);
    return 3;
}

Vec3 check3(lua_State* L, int first, const Vec3& def) {
    return {static_cast<float>(luaL_optnumber(L, first, def.x)), static_cast<float>(luaL_optnumber(L, first + 1, def.y)),
            static_cast<float>(luaL_optnumber(L, first + 2, def.z))};
}

SpriteRenderer& sprite(lua_State* L, Entity e) {
    SpriteRenderer* sp = scene(L).world().get<SpriteRenderer>(e);
    if (!sp) luaL_error(L, "'%s' no tiene SpriteRenderer", scene(L).info(e).name.c_str());
    return *sp;
}

int e_name(lua_State* L) { lua_pushstring(L, scene(L).info(live(L, 1)).name.c_str()); return 1; }
int e_setName(lua_State* L) { scene(L).info(live(L, 1)).name = luaL_checkstring(L, 2); return 0; }
int e_id(lua_State* L) { lua_pushstring(L, scene(L).info(live(L, 1)).id.toString().c_str()); return 1; }
int e_isValid(lua_State* L) {
    Scene* s = sys(L).scene();
    lua_pushboolean(L, s && s->isValid(toEntity(L, 1)));
    return 1;
}
int e_isActive(lua_State* L) { lua_pushboolean(L, scene(L).isActiveInHierarchy(live(L, 1))); return 1; }
int e_setActive(lua_State* L) {
    sys(L).setEntityActive(live(L, 1), lua_toboolean(L, 2));
    return 0;
}
int e_destroy(lua_State* L) { scene(L).destroyEntity(live(L, 1)); return 0; }
int e_parent(lua_State* L) { pushEntity(L, scene(L).parent(live(L, 1))); return 1; }
int e_setParent(lua_State* L) {
    const Entity e = live(L, 1);
    const Entity p = lua_isnoneornil(L, 2) ? Entity{} : live(L, 2);
    const bool keep = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3);
    lua_pushboolean(L, scene(L).setParent(e, p, keep));
    return 1;
}
int e_children(lua_State* L) {
    const auto kids = scene(L).children(live(L, 1));
    lua_createtable(L, static_cast<int>(kids.size()), 0);
    for (size_t i = 0; i < kids.size(); ++i) {
        pushEntity(L, kids[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}
int e_getPosition(lua_State* L) { return push3(L, scene(L).transform(live(L, 1)).position()); }
int e_setPosition(lua_State* L) {
    Transform& t = scene(L).transform(live(L, 1));
    t.setPosition(check3(L, 2, t.position()));
    return 0;
}
int e_translate(lua_State* L) {
    scene(L).transform(live(L, 1)).translate(check3(L, 2, {}));
    return 0;
}
int e_getWorldPosition(lua_State* L) { return push3(L, scene(L).computeWorldMatrix(live(L, 1)).translation()); }
int e_getRotation(lua_State* L) { return push3(L, scene(L).transform(live(L, 1)).eulerDegrees()); }
int e_setRotation(lua_State* L) {
    Transform& t = scene(L).transform(live(L, 1));
    t.setEulerDegrees(check3(L, 2, t.eulerDegrees()));
    return 0;
}
int e_rotate(lua_State* L) {
    Transform& t = scene(L).transform(live(L, 1));
    t.setRotation(t.rotation() * Quat::fromEuler(check3(L, 2, {}) * kDegToRad));
    return 0;
}
int e_getScale(lua_State* L) { return push3(L, scene(L).transform(live(L, 1)).scale()); }
int e_setScale(lua_State* L) {
    Transform& t = scene(L).transform(live(L, 1));
    if (lua_gettop(L) == 2) {
        const float s = static_cast<float>(luaL_checknumber(L, 2));
        t.setScale({s, s, s});
    } else {
        t.setScale(check3(L, 2, t.scale()));
    }
    return 0;
}
int e_forward(lua_State* L) { return push3(L, scene(L).computeWorldMatrix(live(L, 1)).transformDirection({0, 0, -1}).normalized()); }
int e_right(lua_State* L) { return push3(L, scene(L).computeWorldMatrix(live(L, 1)).transformDirection({1, 0, 0}).normalized()); }
int e_up(lua_State* L) { return push3(L, scene(L).computeWorldMatrix(live(L, 1)).transformDirection({0, 1, 0}).normalized()); }
// Color del componente visible de la entidad: SpriteRenderer, UIImage, UIText o MeshRenderer.
Color& colorOf(lua_State* L, Entity e) {
    World& w = scene(L).world();
    if (auto* c = w.get<SpriteRenderer>(e)) return c->color;
    if (auto* c = w.get<UIImage>(e)) return c->color;
    if (auto* c = w.get<UIText>(e)) return c->color;
    if (auto* c = w.get<MeshRenderer>(e)) return c->color;
    luaL_error(L, "'%s' no tiene nada con color (SpriteRenderer, UIImage, UIText o MeshRenderer)", scene(L).info(e).name.c_str());
    static Color dummy;
    return dummy;
}
int e_getColor(lua_State* L) {
    const Color c = colorOf(L, live(L, 1));
    lua_pushnumber(L, c.r); lua_pushnumber(L, c.g); lua_pushnumber(L, c.b); lua_pushnumber(L, c.a);
    return 4;
}
int e_setColor(lua_State* L) {
    Color& c = colorOf(L, live(L, 1));
    c = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
         static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_optnumber(L, 5, c.a))};
    return 0;
}

// ---- Interfaz de juego ----
template <class T>
T& uiComp(lua_State* L, Entity e, const char* name) {
    T* c = scene(L).world().get<T>(e);
    if (!c) luaL_error(L, "'%s' no tiene %s", scene(L).info(e).name.c_str(), name);
    return *c;
}
int e_setText(lua_State* L) {
    UIText& t = uiComp<UIText>(L, live(L, 1), "UIText");
    t.text = luaL_tolstring(L, 2, nullptr); // acepta números: e:setText(puntos)
    return 0;
}
int e_getText(lua_State* L) { lua_pushstring(L, uiComp<UIText>(L, live(L, 1), "UIText").text.c_str()); return 1; }
int e_setFill(lua_State* L) {
    UIImage& i = uiComp<UIImage>(L, live(L, 1), "UIImage");
    i.fill = std::clamp(static_cast<float>(luaL_checknumber(L, 2)), 0.0f, 1.0f);
    return 0;
}
int e_getFill(lua_State* L) { lua_pushnumber(L, uiComp<UIImage>(L, live(L, 1), "UIImage").fill); return 1; }
int e_isPressed(lua_State* L) { lua_pushboolean(L, uiComp<UIButton>(L, live(L, 1), "UIButton").pressed); return 1; }
int e_wasClicked(lua_State* L) { lua_pushboolean(L, uiComp<UIButton>(L, live(L, 1), "UIButton").clicked); return 1; }
int e_setInteractable(lua_State* L) {
    uiComp<UIButton>(L, live(L, 1), "UIButton").interactable = lua_toboolean(L, 2);
    return 0;
}
int e_getJoystick(lua_State* L) {
    const UIJoystick& j = uiComp<UIJoystick>(L, live(L, 1), "UIJoystick");
    lua_pushnumber(L, j.value.x);
    lua_pushnumber(L, j.value.y);
    return 2;
}
int e_getUIPosition(lua_State* L) {
    const UIRect& r = uiComp<UIRect>(L, live(L, 1), "UIRect");
    lua_pushnumber(L, r.position.x);
    lua_pushnumber(L, r.position.y);
    return 2;
}
int e_setUIPosition(lua_State* L) {
    UIRect& r = uiComp<UIRect>(L, live(L, 1), "UIRect");
    r.position = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};
    return 0;
}
int e_setUISize(lua_State* L) {
    UIRect& r = uiComp<UIRect>(L, live(L, 1), "UIRect");
    r.size = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};
    return 0;
}
int e_setSize(lua_State* L) {
    SpriteRenderer& sp = sprite(L, live(L, 1));
    sp.size = {static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};
    return 0;
}
int e_hasComponent(lua_State* L) {
    const Entity e = live(L, 1);
    const ComponentType* type = sys(L).registry().find(luaL_checkstring(L, 2));
    lua_pushboolean(L, type && type->has(scene(L), e));
    return 1;
}
int e_addComponent(lua_State* L) {
    const Entity e = live(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const ComponentType* type = sys(L).registry().find(name);
    if (!type) return luaL_error(L, "no existe el componente '%s'", name);
    if (!type->has(scene(L), e)) {
        LoadReport ignored;
        const Json empty = Json::object();
        FieldReader reader(empty, name, ignored);
        type->load(scene(L), e, reader);
    }
    return 0;
}
int e_removeComponent(lua_State* L) {
    const Entity e = live(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const ComponentType* type = sys(L).registry().find(name);
    if (!type) return luaL_error(L, "no existe el componente '%s'", name);
    if (!type->remove) return luaL_error(L, "el componente '%s' no se puede quitar", name);
    type->remove(scene(L), e);
    return 0;
}
int e_getScript(lua_State* L) {
    const int ref = sys(L).instanceRef(live(L, 1));
    if (ref < 0) lua_pushnil(L);
    else lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}
PhysicsApi& physics(lua_State* L) {
    PhysicsApi* p = sys(L).host().scriptPhysics();
    if (!p) luaL_error(L, "la física no está disponible");
    return *p;
}
Entity body(lua_State* L) {
    const Entity e = live(L, 1);
    if (!physics(L).hasBody(e)) luaL_error(L, "'%s' no tiene cuerpo físico (añade Collider y Rigidbody)", scene(L).info(e).name.c_str());
    return e;
}
int e_addForce(lua_State* L) { const Entity e = body(L); physics(L).addForce(e, check3(L, 2, {}), false); return 0; }
int e_addImpulse(lua_State* L) { const Entity e = body(L); physics(L).addForce(e, check3(L, 2, {}), true); return 0; }
int e_addTorque(lua_State* L) { const Entity e = body(L); physics(L).addTorque(e, check3(L, 2, {})); return 0; }
int e_setVelocity(lua_State* L) { const Entity e = body(L); physics(L).setVelocity(e, check3(L, 2, {})); return 0; }
int e_getVelocity(lua_State* L) { const Entity e = body(L); return push3(L, physics(L).velocity(e)); }
int e_teleport(lua_State* L) { const Entity e = body(L); physics(L).teleport(e, check3(L, 2, {})); return 0; }

Animator& animator(lua_State* L, Entity e) {
    World& w = scene(L).world();
    if (!w.has<Animator>(e)) w.add<Animator>(e);
    return *w.get<Animator>(e);
}
int e_playAnimation(lua_State* L) {
    const Entity e = live(L, 1);
    Animator& a = animator(L, e);
    const std::string clip = luaL_optstring(L, 2, "");
    if (!lua_isnoneornil(L, 3)) a.loop = lua_toboolean(L, 3);
    if (!a.playing && clip == a.clip) a.time = 0;
    switchAnimation(a, clip); // volver a pedir la misma animación no la reinicia; otra distinta entra con transición
    a.playing = true;
    return 0;
}
int e_stopAnimation(lua_State* L) { animator(L, live(L, 1)).playing = false; return 0; }
int e_getAnimation(lua_State* L) {
    const Animator* a = scene(L).world().get<Animator>(live(L, 1));
    if (!a) return 0;
    lua_pushstring(L, a->clip.c_str());
    lua_pushnumber(L, a->time);
    lua_pushboolean(L, a->playing);
    return 3;
}
int e_setAnimationSpeed(lua_State* L) { animator(L, live(L, 1)).speed = static_cast<float>(luaL_checknumber(L, 2)); return 0; }

int e_playSound(lua_State* L) {
    const Entity e = live(L, 1);
    if (lua_isnoneornil(L, 2)) sys(L).host().scriptPlayEntitySound(e); // su AudioSource
    else sys(L).host().scriptPlaySound(luaL_checkstring(L, 2), e, true, {}, float(luaL_optnumber(L, 3, 1)));
    return 0;
}
int e_stopSound(lua_State* L) { sys(L).host().scriptStopSound(live(L, 1)); return 0; }

int e_eq(lua_State* L) {
    const Entity a = toEntity(L, 1), b = toEntity(L, 2);
    lua_pushboolean(L, a == b);
    return 1;
}
int e_tostring(lua_State* L) {
    const Entity e = toEntity(L, 1);
    Scene* s = sys(L).scene();
    if (s && s->isValid(e)) lua_pushfstring(L, "Entity(%s)", s->info(e).name.c_str());
    else lua_pushstring(L, "Entity(destruida)");
    return 1;
}

const luaL_Reg kEntityMethods[] = {
    {"name", e_name}, {"setName", e_setName}, {"id", e_id}, {"isValid", e_isValid},
    {"isActive", e_isActive}, {"setActive", e_setActive}, {"destroy", e_destroy},
    {"parent", e_parent}, {"setParent", e_setParent}, {"children", e_children},
    {"getPosition", e_getPosition}, {"setPosition", e_setPosition}, {"translate", e_translate},
    {"getWorldPosition", e_getWorldPosition}, {"getRotation", e_getRotation}, {"setRotation", e_setRotation},
    {"rotate", e_rotate}, {"getScale", e_getScale}, {"setScale", e_setScale},
    {"forward", e_forward}, {"right", e_right}, {"up", e_up},
    {"getColor", e_getColor}, {"setColor", e_setColor}, {"setSize", e_setSize},
    {"hasComponent", e_hasComponent}, {"addComponent", e_addComponent}, {"removeComponent", e_removeComponent},
    {"getScript", e_getScript}, {"addForce", e_addForce}, {"addImpulse", e_addImpulse}, {"addTorque", e_addTorque},
    {"setVelocity", e_setVelocity}, {"getVelocity", e_getVelocity}, {"teleport", e_teleport},
    {"playAnimation", e_playAnimation}, {"stopAnimation", e_stopAnimation}, {"getAnimation", e_getAnimation},
    {"setAnimationSpeed", e_setAnimationSpeed}, {"playSound", e_playSound}, {"stopSound", e_stopSound},
    {"setText", e_setText}, {"getText", e_getText}, {"setFill", e_setFill}, {"getFill", e_getFill},
    {"isPressed", e_isPressed}, {"wasClicked", e_wasClicked}, {"setInteractable", e_setInteractable},
    {"getJoystick", e_getJoystick}, {"getUIPosition", e_getUIPosition}, {"setUIPosition", e_setUIPosition},
    {"setUISize", e_setUISize}, {nullptr, nullptr}};

// ---------------------------------------------------------------- Tablas globales

int g_find(lua_State* L) { pushEntity(L, scene(L).findByName(luaL_checkstring(L, 1))); return 1; }
int g_get(lua_State* L) {
    UUID id;
    if (!UUID::parse(luaL_checkstring(L, 1), id)) return luaL_error(L, "id de entidad no válido");
    pushEntity(L, scene(L).findById(id));
    return 1;
}
int g_create(lua_State* L) {
    const Entity parent = lua_isnoneornil(L, 2) ? Entity{} : live(L, 2);
    pushEntity(L, scene(L).createEntity(luaL_optstring(L, 1, "Entidad"), parent));
    return 1;
}
const luaL_Reg kEntityGlobals[] = {{"find", g_find}, {"get", g_get}, {"create", g_create}, {nullptr, nullptr}};

int s_load(lua_State* L) { lua_pushboolean(L, sys(L).host().scriptLoadScene(luaL_checkstring(L, 1))); return 1; }
int s_name(lua_State* L) { lua_pushstring(L, scene(L).name().c_str()); return 1; }
int s_instantiate(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const Vec3 p = check3(L, 2, {});
    pushEntity(L, sys(L).host().scriptInstantiate(name, p));
    return 1;
}
const luaL_Reg kSceneGlobals[] = {{"load", s_load}, {"name", s_name}, {"instantiate", s_instantiate}, {nullptr, nullptr}};

int t_delta(lua_State* L) { lua_pushnumber(L, sys(L).host().scriptDeltaTime()); return 1; }
int t_fixed(lua_State* L) { lua_pushnumber(L, sys(L).host().scriptFixedDeltaTime()); return 1; }
int t_time(lua_State* L) { lua_pushnumber(L, sys(L).host().scriptTime()); return 1; }
int t_frame(lua_State* L) { lua_pushinteger(L, static_cast<lua_Integer>(sys(L).host().scriptFrame())); return 1; }
const luaL_Reg kTimeGlobals[] = {{"delta", t_delta}, {"fixedDelta", t_fixed}, {"time", t_time}, {"frame", t_frame}, {nullptr, nullptr}};

// Los dedos que está usando la interfaz (botones, joystick) no cuentan como toques del juego.
std::vector<const Pointer*> gamePointers(lua_State* L) {
    std::vector<const Pointer*> out;
    for (const Pointer& p : sys(L).host().scriptInput().pointers())
        if (!p.ui) out.push_back(&p);
    return out;
}
const Pointer& pointerArg(lua_State* L) {
    const auto ptrs = gamePointers(L);
    const lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 1 || i > static_cast<lua_Integer>(ptrs.size())) luaL_error(L, "no hay toque número %d", static_cast<int>(i));
    return *ptrs[static_cast<size_t>(i - 1)];
}
int i_touchCount(lua_State* L) { lua_pushinteger(L, static_cast<lua_Integer>(gamePointers(L).size())); return 1; }
int i_touch(lua_State* L) {
    const Pointer& p = pointerArg(L);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, p.id); lua_setfield(L, -2, "id");
    lua_pushnumber(L, p.position.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, p.position.y); lua_setfield(L, -2, "y");
    lua_pushstring(L, p.justBegan ? "began" : "held"); lua_setfield(L, -2, "phase");
    return 1;
}
int i_touchWorld(lua_State* L) {
    const Pointer& p = pointerArg(L);
    Vec3 w;
    if (!sys(L).host().scriptScreenToWorld(p.position, w)) return 0;
    return push3(L, w);
}
int i_tapped(lua_State* L) {
    bool tapped = false;
    for (const auto& ev : sys(L).host().scriptInput().events()) tapped = tapped || (ev.phase == TouchPhase::Began && !ev.ui);
    lua_pushboolean(L, tapped);
    return 1;
}
const luaL_Reg kInputGlobals[] = {{"touchCount", i_touchCount}, {"touch", i_touch}, {"touchWorld", i_touchWorld},
                                  {"tapped", i_tapped}, {nullptr, nullptr}};

int sc_width(lua_State* L) { lua_pushnumber(L, sys(L).host().scriptScreenSize().x); return 1; }
int sc_height(lua_State* L) { lua_pushnumber(L, sys(L).host().scriptScreenSize().y); return 1; }
int sc_text(lua_State* L) {
    const float x = static_cast<float>(luaL_checknumber(L, 1)), y = static_cast<float>(luaL_checknumber(L, 2));
    const float size = static_cast<float>(luaL_checknumber(L, 3));
    // El color se lee antes que el texto: luaL_tolstring apila un valor y movería los índices.
    const Color c{static_cast<float>(luaL_optnumber(L, 5, 1)), static_cast<float>(luaL_optnumber(L, 6, 1)),
                  static_cast<float>(luaL_optnumber(L, 7, 1)), static_cast<float>(luaL_optnumber(L, 8, 1))};
    const char* text = luaL_tolstring(L, 4, nullptr);
    sys(L).host().scriptDrawText(x, y, size, text, c);
    return 0;
}
const luaL_Reg kScreenGlobals[] = {{"width", sc_width}, {"height", sc_height}, {"text", sc_text}, {nullptr, nullptr}};

std::string joinArgs(lua_State* L) {
    std::string out;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += ' ';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}
int d_log(lua_State* L) { TITAN_INFO("script", "%s", joinArgs(L).c_str()); return 0; }
int d_warn(lua_State* L) { TITAN_WARN("script", "%s", joinArgs(L).c_str()); return 0; }
int d_error(lua_State* L) { TITAN_ERROR("script", "%s", joinArgs(L).c_str()); return 0; }
const luaL_Reg kDebugGlobals[] = {{"log", d_log}, {"warn", d_warn}, {"error", d_error}, {nullptr, nullptr}};

int p_raycast(lua_State* L) {
    const Vec3 o{float(luaL_checknumber(L, 1)), float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3))};
    const Vec3 d{float(luaL_checknumber(L, 4)), float(luaL_checknumber(L, 5)), float(luaL_checknumber(L, 6))};
    RaycastHit hit;
    const Entity ignore = lua_isnoneornil(L, 8) ? Entity{} : toEntity(L, 8);
    if (!physics(L).raycast(o, d, float(luaL_optnumber(L, 7, 1000)), hit, ignore) || !scene(L).isValid(hit.entity)) {
        lua_pushnil(L);
        return 1;
    }
    pushEntity(L, hit.entity);
    push3(L, hit.point);
    lua_pushnumber(L, hit.distance);
    return 5;
}
int p_setGravity(lua_State* L) { physics(L).setGravity(check3(L, 1, {})); return 0; }
int p_getGravity(lua_State* L) { return push3(L, physics(L).gravity()); }
const luaL_Reg kPhysicsGlobals[] = {{"raycast", p_raycast}, {"setGravity", p_setGravity}, {"getGravity", p_getGravity}, {nullptr, nullptr}};

int a_play(lua_State* L) {
    lua_pushboolean(L, sys(L).host().scriptPlaySound(luaL_checkstring(L, 1), {}, false, {}, float(luaL_optnumber(L, 2, 1))));
    return 1;
}
int a_playAt(lua_State* L) {
    const Vec3 p = check3(L, 2, {});
    lua_pushboolean(L, sys(L).host().scriptPlaySound(luaL_checkstring(L, 1), {}, true, p, float(luaL_optnumber(L, 5, 1))));
    return 1;
}
int a_setVolume(lua_State* L) { sys(L).host().scriptSetVolume(float(luaL_checknumber(L, 1))); return 0; }
const luaL_Reg kAudioGlobals[] = {{"play", a_play}, {"playAt", a_playAt}, {"setVolume", a_setVolume}, {nullptr, nullptr}};

void setGlobalTable(lua_State* L, const char* name, const luaL_Reg* fns) {
    lua_newtable(L);
    luaL_setfuncs(L, fns, 0);
    lua_setglobal(L, name);
}

} // namespace

// ---------------------------------------------------------------- ScriptSystem

ScriptSystem::ScriptSystem(ScriptHost& host, const ComponentRegistry& registry, SourceLoader loader)
    : host_(host), registry_(registry), loader_(std::move(loader)) {
    L_ = lua_newstate(luaAlloc, this);
    // Solo bibliotecas sin acceso al sistema: nada de io, os, package ni debug.
    const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},          {LUA_TABLIBNAME, luaopen_table},
                             {LUA_STRLIBNAME, luaopen_string},   {LUA_MATHLIBNAME, luaopen_math},
                             {LUA_UTF8LIBNAME, luaopen_utf8},    {LUA_COLIBNAME, luaopen_coroutine}};
    for (const auto& lib : libs) {
        luaL_requiref(L_, lib.name, lib.func, 1);
        lua_pop(L_, 1);
    }
    for (const char* name : {"dofile", "loadfile", "load"}) {
        lua_pushnil(L_);
        lua_setglobal(L_, name);
    }
    lua_getglobal(L_, "string");
    lua_pushnil(L_);
    lua_setfield(L_, -2, "dump");
    lua_pop(L_, 1);
    lua_sethook(L_, timeHook, LUA_MASKCOUNT, 10000);
    registerApi();
}

ScriptSystem::~ScriptSystem() {
    detach();
    lua_close(L_);
}

void ScriptSystem::registerApi() {
    luaL_newmetatable(L_, kEntityMeta);
    lua_newtable(L_);
    luaL_setfuncs(L_, kEntityMethods, 0);
    lua_setfield(L_, -2, "__index");
    lua_pushcfunction(L_, e_eq);
    lua_setfield(L_, -2, "__eq");
    lua_pushcfunction(L_, e_tostring);
    lua_setfield(L_, -2, "__tostring");
    lua_pop(L_, 1);

    setGlobalTable(L_, "Entity", kEntityGlobals);
    setGlobalTable(L_, "Scene", kSceneGlobals);
    setGlobalTable(L_, "Time", kTimeGlobals);
    setGlobalTable(L_, "Input", kInputGlobals);
    setGlobalTable(L_, "Screen", kScreenGlobals);
    setGlobalTable(L_, "Debug", kDebugGlobals);
    setGlobalTable(L_, "Physics", kPhysicsGlobals);
    setGlobalTable(L_, "Audio", kAudioGlobals);
    lua_pushcfunction(L_, d_log);
    lua_setglobal(L_, "print");
}

void* ScriptSystem::allocate(void* ptr, size_t osize, size_t nsize) {
    if (!ptr) osize = 0; // con ptr nulo, osize indica el tipo de objeto, no un tamaño
    if (nsize == 0) {
        std::free(ptr);
        memoryUsed_ -= osize;
        return nullptr;
    }
    if (nsize > osize && memoryUsed_ + (nsize - osize) > memoryLimit_) return nullptr; // Lua lanza "not enough memory"
    void* p = std::realloc(ptr, nsize);
    if (p) memoryUsed_ = memoryUsed_ - osize + nsize;
    return p;
}

void ScriptSystem::armDeadline() { deadline_ = now() + timeBudget_; }
bool ScriptSystem::deadlinePassed() const { return now() > deadline_; }

void ScriptSystem::reportError(const std::string& script, const std::string& entity, const std::string& function,
                               const std::string& full) {
    ScriptError err{script, entity, function, full, {}};
    const size_t tb = full.find("\nstack traceback:");
    if (tb != std::string::npos) {
        err.message = full.substr(0, tb);
        err.traceback = full.substr(tb + 1);
    }
    TITAN_ERROR("script", "%s (%s, en %s): %s", script.c_str(), entity.c_str(), function.c_str(), err.message.c_str());
    if (errors_.size() < 200) errors_.push_back(std::move(err));
}

int ScriptSystem::loadClass(AssetRef id, std::string& chunkName) {
    auto cached = classRefs_.find(id.value);
    if (cached != classRefs_.end()) {
        chunkName = classNames_[id.value];
        return cached->second;
    }
    std::string source;
    classRefs_[id.value] = LUA_NOREF; // si falla, no se reintenta en cada entidad
    if (!loader_ || !loader_(id, chunkName, source)) {
        chunkName = id.toString();
        classNames_[id.value] = chunkName;
        reportError(chunkName, "-", "cargar", "no se encuentra el script " + id.toString());
        return LUA_NOREF;
    }
    classNames_[id.value] = chunkName;

    lua_pushcfunction(L_, traceback);
    const int handler = lua_gettop(L_);
    if (luaL_loadbufferx(L_, source.data(), source.size(), ("@" + chunkName).c_str(), "t") != LUA_OK) {
        reportError(chunkName, "-", "compilar", lua_tostring(L_, -1));
        lua_settop(L_, handler - 1);
        return LUA_NOREF;
    }
    armDeadline();
    if (lua_pcall(L_, 0, 1, handler) != LUA_OK) {
        reportError(chunkName, "-", "cargar", lua_tostring(L_, -1));
        lua_settop(L_, handler - 1);
        return LUA_NOREF;
    }
    if (!lua_istable(L_, -1)) {
        reportError(chunkName, "-", "cargar", chunkName + ": el script debe terminar con 'return' y una tabla");
        lua_settop(L_, handler - 1);
        return LUA_NOREF;
    }
    const int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    lua_settop(L_, handler - 1);
    classRefs_[id.value] = ref;
    return ref;
}

void ScriptSystem::createInstance(Entity e, const Script& component) {
    if (component.script.isNull()) return;
    std::string chunkName;
    const int cls = loadClass(component.script, chunkName);
    if (cls == LUA_NOREF) return;
    lua_newtable(L_);                       // self
    pushEntity(L_, e);
    lua_setfield(L_, -2, "entity");
    // Variables del Inspector: primero los valores por defecto de Clase.props, luego los de esta entidad.
    lua_rawgeti(L_, LUA_REGISTRYINDEX, cls);
    lua_getfield(L_, -1, "props");
    if (lua_istable(L_, -1)) {
        lua_pushnil(L_);
        while (lua_next(L_, -2)) {
            if (lua_type(L_, -2) == LUA_TSTRING) {
                lua_pushvalue(L_, -2);
                lua_insert(L_, -2);
                lua_settable(L_, -6); // self[clave] = valor
            } else {
                lua_pop(L_, 1);
            }
        }
    }
    lua_pop(L_, 2);
    const Json over = Json::parse(component.props, nullptr, false);
    if (over.is_object())
        for (auto it = over.begin(); it != over.end(); ++it) {
            if (it->is_number()) lua_pushnumber(L_, it->get<double>());
            else if (it->is_boolean()) lua_pushboolean(L_, it->get<bool>());
            else if (it->is_string()) lua_pushstring(L_, it->get<std::string>().c_str());
            else continue;
            lua_setfield(L_, -2, it.key().c_str());
        }
    lua_newtable(L_);                       // metatabla: los métodos se buscan en la clase
    lua_rawgeti(L_, LUA_REGISTRYINDEX, cls);
    lua_setfield(L_, -2, "__index");
    lua_setmetatable(L_, -2);
    Instance inst;
    inst.entity = e;
    inst.ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    inst.script = chunkName;
    instances_.push_back(std::move(inst));
}

bool ScriptSystem::call(Instance& inst, const char* method, int nargs, const float* arg, const Entity* entityArg) {
    lua_pushcfunction(L_, traceback);
    const int handler = lua_gettop(L_);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, inst.ref);
    lua_getfield(L_, -1, method);
    if (!lua_isfunction(L_, -1)) {
        lua_settop(L_, handler - 1);
        return true;
    }
    lua_insert(L_, -2); // función, self
    if (entityArg) pushEntity(L_, *entityArg);
    else if (nargs) lua_pushnumber(L_, *arg);
    armDeadline();
    const int status = lua_pcall(L_, 1 + nargs, 0, handler);
    if (status != LUA_OK) {
        const std::string entity = scene_ && scene_->isValid(inst.entity) ? scene_->info(inst.entity).name : "?";
        reportError(inst.script, entity, method, lua_tostring(L_, -1));
        // Una instancia que falla deja de ejecutarse para no repetir el mismo error cada fotograma.
        inst.failed = true;
    }
    lua_settop(L_, handler - 1);
    return status == LUA_OK;
}

void ScriptSystem::attach(Scene& scene, EventBus& events) {
    detach();
    scene_ = &scene;
    destroyedSub_ = events.subscribe<EntityDestroyedEvent>([this](const EntityDestroyedEvent& ev) {
        if (ev.scene != scene_) return;
        for (auto& inst : instances_)
            if (inst.alive && inst.entity == ev.entity) {
                if (!inst.failed) call(inst, "onDestroy", 0, nullptr);
                destroyInstance(inst);
            }
    });
    scene.world().each<Script>([&](Entity e, Script& s) { createInstance(e, s); });
    // Todos los onCreate antes de cualquier onStart: en onStart las demás instancias ya existen.
    for (size_t i = 0; i < instances_.size(); ++i)
        if (instances_[i].alive) call(instances_[i], "onCreate", 0, nullptr);
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance& inst = instances_[i];
        const Script* sc = scene.world().get<Script>(inst.entity);
        if (inst.alive && !inst.failed && sc && sc->enabled && scene.isActiveInHierarchy(inst.entity)) {
            inst.started = true;
            call(inst, "onStart", 0, nullptr);
        }
    }
    TITAN_INFO("script", "%zu instancias de script en '%s'", instanceCount(), scene.name().c_str());
}

void ScriptSystem::destroyInstance(Instance& inst) {
    if (inst.ref >= 0) luaL_unref(L_, LUA_REGISTRYINDEX, inst.ref);
    inst.ref = -1;
    inst.alive = false;
}

void ScriptSystem::detach() {
    if (!scene_) return;
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance& inst = instances_[i];
        if (!inst.alive) continue;
        if (!inst.failed) call(inst, "onDestroy", 0, nullptr);
        destroyInstance(inst);
    }
    instances_.clear();
    destroyedSub_.reset();
    scene_ = nullptr;
    lua_gc(L_, LUA_GCCOLLECT);
}

void ScriptSystem::callAll(const char* method, float dt) {
    if (!scene_) return;
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance& inst = instances_[i];
        if (!inst.alive || inst.failed || !scene_->isValid(inst.entity)) continue;
        const Script* sc = scene_->world().get<Script>(inst.entity);
        if (!sc || !sc->enabled || !scene_->isActiveInHierarchy(inst.entity)) continue;
        if (!inst.started) {
            inst.started = true;
            if (!call(inst, "onStart", 0, nullptr)) continue;
        }
        call(instances_[i], method, 1, &dt);
    }
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [](const Instance& x) { return !x.alive; }),
                     instances_.end());
}

void ScriptSystem::fixedUpdate(float dt) { callAll("onFixedUpdate", dt); }

void ScriptSystem::attachSubtree(Entity root) {
    if (!scene_ || !scene_->isValid(root)) return;
    const size_t first = instances_.size();
    std::vector<Entity> all;
    scene_->traverse([&](Entity e, int) {
        if (e == root || scene_->isDescendantOf(e, root)) all.push_back(e);
    });
    for (Entity e : all)
        if (const Script* s = scene_->world().get<Script>(e)) createInstance(e, *s);
    for (size_t i = first; i < instances_.size(); ++i) call(instances_[i], "onCreate", 0, nullptr);
    // onStart se llama antes de su primer update (callAll), igual que en las entidades activadas más tarde.
}

void ScriptSystem::dispatchContact(Entity self, Entity other, bool begin, bool trigger) {
    if (!scene_ || !scene_->isValid(self)) return;
    const char* method = trigger ? (begin ? "onTriggerEnter" : "onTriggerExit") : (begin ? "onCollisionEnter" : "onCollisionExit");
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance& inst = instances_[i];
        if (!inst.alive || inst.failed || inst.entity != self) continue;
        call(inst, method, 1, nullptr, &other);
    }
}
void ScriptSystem::dispatchEvent(Entity self, const char* method) {
    if (!scene_ || !scene_->isValid(self)) return;
    for (size_t i = 0; i < instances_.size(); ++i) {
        Instance& inst = instances_[i];
        if (!inst.alive || inst.failed || inst.entity != self) continue;
        call(inst, method, 0, nullptr);
    }
}
void ScriptSystem::update(float dt) { callAll("onUpdate", dt); }
void ScriptSystem::lateUpdate(float dt) { callAll("onLateUpdate", dt); }

void ScriptSystem::setEntityActive(Entity e, bool active) {
    if (!scene_ || !scene_->isValid(e) || scene_->info(e).active == active) return;
    scene_->info(e).active = active;
    for (size_t i = 0; i < instances_.size(); ++i)
        if (instances_[i].alive && !instances_[i].failed && instances_[i].entity == e)
            call(instances_[i], active ? "onEnable" : "onDisable", 0, nullptr);
}

int ScriptSystem::instanceRef(Entity e) const {
    for (const auto& inst : instances_)
        if (inst.alive && inst.entity == e) return inst.ref;
    return -1;
}

size_t ScriptSystem::instanceCount() const {
    size_t n = 0;
    for (const auto& inst : instances_) n += inst.alive ? 1 : 0;
    return n;
}

bool ScriptSystem::runString(const std::string& code, const std::string& chunkName) {
    lua_pushcfunction(L_, traceback);
    const int handler = lua_gettop(L_);
    bool ok = luaL_loadbufferx(L_, code.data(), code.size(), ("=" + chunkName).c_str(), "t") == LUA_OK;
    if (ok) {
        armDeadline();
        ok = lua_pcall(L_, 0, 0, handler) == LUA_OK;
    }
    if (!ok) reportError(chunkName, "-", "ejecutar", lua_tostring(L_, -1));
    lua_settop(L_, handler - 1);
    return ok;
}

} // namespace titan
