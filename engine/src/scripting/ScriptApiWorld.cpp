// API de Lua de los sistemas añadidos en 0.13: calidad gráfica (y, en las secciones siguientes, más sistemas).
#include "ScriptInternal.h"
#include "titan/core/AnimationGraph.h"
#include "titan/core/Particles.h"
#include "titan/core/TerrainTools.h"

namespace titan::script_detail {

namespace {

// ---------------------------------------------------------------- Quality
int q_set(lua_State* L) { lua_pushboolean(L, sys(L).host().scriptSetQuality(luaL_checkstring(L, 1))); return 1; }
int q_get(lua_State* L) { lua_pushstring(L, sys(L).host().scriptQuality().c_str()); return 1; }
int q_recommended(lua_State* L) {
    lua_pushstring(L, qualityName(recommendedQuality(sys(L).host().scriptDeviceCaps())));
    return 1;
}
int q_device(lua_State* L) {
    const DeviceCaps c = sys(L).host().scriptDeviceCaps();
    lua_createtable(L, 0, 6);
    lua_pushstring(L, c.renderer.c_str()); lua_setfield(L, -2, "gpu");
    lua_pushstring(L, c.vendor.c_str()); lua_setfield(L, -2, "vendor");
    lua_pushstring(L, c.version.c_str()); lua_setfield(L, -2, "version");
    lua_pushinteger(L, c.maxTextureSize); lua_setfield(L, -2, "maxTextureSize");
    lua_pushinteger(L, c.memoryMB); lua_setfield(L, -2, "memoryMB");
    lua_pushboolean(L, c.astc); lua_setfield(L, -2, "astc");
    return 1;
}
// ---------------------------------------------------------------- Input: acciones, teclado y mando
InputActions* actions(lua_State* L) { return sys(L).host().scriptActions(); }
int in_action(lua_State* L) { InputActions* a = actions(L); lua_pushboolean(L, a && a->held(luaL_checkstring(L, 1))); return 1; }
int in_pressed(lua_State* L) { InputActions* a = actions(L); lua_pushboolean(L, a && a->pressed(luaL_checkstring(L, 1))); return 1; }
int in_released(lua_State* L) { InputActions* a = actions(L); lua_pushboolean(L, a && a->released(luaL_checkstring(L, 1))); return 1; }
int in_axis(lua_State* L) {
    InputActions* a = actions(L);
    const Vec2 v = a ? a->axis(luaL_checkstring(L, 1)) : Vec2{};
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}
int in_define(lua_State* L) {
    InputActions* a = actions(L);
    const char* name = luaL_checkstring(L, 1);
    const std::string type = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<std::string> bindings;
    const lua_Integer n = luaL_len(L, 3);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_geti(L, 3, i);
        bindings.emplace_back(luaL_checkstring(L, -1));
        lua_pop(L, 1);
    }
    const bool axis = type == "eje" || type == "axis";
    if (!axis && type != "boton" && type != "botón" && type != "button")
        return luaL_error(L, "Input.define: el tipo debe ser \"boton\" o \"eje\"");
    std::string err;
    if (!a || !a->define(name, axis ? InputActions::Type::Axis2D : InputActions::Type::Button, bindings, &err))
        return luaL_error(L, "Input.define('%s'): %s", name, a ? err.c_str() : "no disponible");
    return 0;
}
int in_key(lua_State* L) { lua_pushboolean(L, sys(L).host().scriptInput().keyDown(keyFromName(luaL_checkstring(L, 1)))); return 1; }
int in_keyPressed(lua_State* L) {
    lua_pushboolean(L, sys(L).host().scriptInput().keyPressed(keyFromName(luaL_checkstring(L, 1))));
    return 1;
}
// Input.cameraAxis("Mover") → x, z en el mundo: el eje de la acción girado según hacia dónde mira la cámara.
int in_cameraAxis(lua_State* L) {
    InputActions* a = actions(L);
    const Vec2 v = a ? a->axis(luaL_checkstring(L, 1)) : Vec2{};
    Vec3 f{0, 0, -1}, r{1, 0, 0};
    sys(L).host().scriptCameraBasis(f, r);
    const Vec3 w = r * v.x + f * v.y;
    lua_pushnumber(L, w.x);
    lua_pushnumber(L, w.z);
    return 2;
}
int in_gamepad(lua_State* L) { lua_pushboolean(L, sys(L).host().scriptInput().gamepadConnected()); return 1; }
const luaL_Reg kInputExtra[] = {{"action", in_action}, {"pressed", in_pressed}, {"released", in_released},
                                {"axis", in_axis}, {"define", in_define}, {"key", in_key},
                                {"keyPressed", in_keyPressed}, {"gamepad", in_gamepad},
                                {"cameraAxis", in_cameraAxis}, {nullptr, nullptr}};

// ---------------------------------------------------------------- Save: partida guardada
constexpr size_t kMaxSaveBytes = 1024 * 1024; // 1 MB: suficiente para progreso, inventario y ajustes

Json toJsonValue(lua_State* L, int idx, int depth) {
    if (depth > 16) luaL_error(L, "Save: tabla demasiado anidada (máx. 16 niveles)");
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL: return nullptr;
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) return static_cast<long long>(lua_tointeger(L, idx));
        return lua_tonumber(L, idx);
    case LUA_TSTRING: return std::string(lua_tostring(L, idx));
    case LUA_TTABLE: {
        const lua_Integer n = luaL_len(L, idx);
        bool array = n > 0;
        if (array) { // una lista: claves 1..n y nada más
            lua_Integer count = 0;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                ++count;
                lua_pop(L, 1);
            }
            array = count == n;
        }
        if (array) {
            Json a = Json::array();
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_geti(L, idx, i);
                a.push_back(toJsonValue(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return a;
        }
        Json o = Json::object();
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            if (lua_type(L, -2) == LUA_TSTRING) o[lua_tostring(L, -2)] = toJsonValue(L, -1, depth + 1);
            else if (lua_type(L, -2) == LUA_TNUMBER) {
                lua_pushvalue(L, -2);
                o[lua_tostring(L, -1)] = toJsonValue(L, -2, depth + 1);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        return o;
    }
    default: luaL_error(L, "Save: solo se guardan números, textos, booleanos y tablas"); return nullptr;
    }
}

void pushJson(lua_State* L, const Json& j) {
    if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer()) lua_pushinteger(L, static_cast<lua_Integer>(j.get<long long>()));
    else if (j.is_number()) lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        for (size_t i = 0; i < j.size(); ++i) {
            pushJson(L, j[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    } else if (j.is_object()) {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (auto it = j.begin(); it != j.end(); ++it) {
            pushJson(L, it.value());
            lua_setfield(L, -2, it.key().c_str());
        }
    } else lua_pushnil(L);
}

int sv_set(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    Json v = toJsonValue(L, 2, 0);
    Json& data = sys(L).saveData();
    Json previous = data.contains(key) ? data[key] : Json();
    data[key] = std::move(v);
    if (data.dump().size() > kMaxSaveBytes) {
        if (previous.is_null()) data.erase(key);
        else data[key] = previous;
        return luaL_error(L, "Save: la partida superaría 1 MB");
    }
    sys(L).markSaveDirty();
    return 0;
}
int sv_get(lua_State* L) {
    const Json& data = sys(L).saveData();
    const char* key = luaL_checkstring(L, 1);
    if (!data.contains(key) || data[key].is_null()) {
        lua_settop(L, 2); // el valor por defecto (o nil)
        return 1;
    }
    pushJson(L, data[key]);
    return 1;
}
int sv_has(lua_State* L) { lua_pushboolean(L, sys(L).saveData().contains(luaL_checkstring(L, 1))); return 1; }
int sv_delete(lua_State* L) {
    sys(L).saveData().erase(luaL_checkstring(L, 1));
    sys(L).markSaveDirty();
    return 0;
}
int sv_clear(lua_State* L) {
    sys(L).saveData() = Json::object();
    sys(L).markSaveDirty();
    return 0;
}
int sv_write(lua_State* L) {
    std::string err;
    const bool ok = sys(L).writeSave(&err);
    lua_pushboolean(L, ok);
    if (!ok) lua_pushstring(L, err.c_str());
    return ok ? 1 : 2;
}
const luaL_Reg kSaveGlobals[] = {{"set", sv_set}, {"get", sv_get}, {"has", sv_has}, {"delete", sv_delete},
                                 {"clear", sv_clear}, {"write", sv_write}, {nullptr, nullptr}};

// ---------------------------------------------------------------- Debug.stats
int d_stats(lua_State* L) {
    const ScriptHost::FrameStats s = sys(L).host().scriptFrameStats();
    lua_createtable(L, 0, 11);
    auto num = [&](const char* k, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); };
    num("fps", s.fps);
    num("frameMs", s.frameMs);
    num("scriptMs", s.scriptMs);
    num("renderMs", s.renderMs);
    num("drawCalls", s.drawCalls);
    num("triangles", s.triangles);
    num("meshes", s.meshes);
    num("meshesVisible", s.meshesVisible);
    num("particles", s.particles);
    num("bodies", s.bodies);
    num("entities", s.entities);
    return 1;
}

const luaL_Reg kQualityGlobals[] = {{"set", q_set}, {"get", q_get}, {"recommended", q_recommended},
                                    {"device", q_device}, {nullptr, nullptr}};

// ---------------------------------------------------------------- Partículas
ParticleSystem& particles(lua_State* L, Entity e) { return component<ParticleSystem>(L, e, "ParticleSystem"); }
int e_playParticles(lua_State* L) { playParticles(particles(L, live(L, 1))); return 0; }
int e_stopParticles(lua_State* L) {
    ParticleSystem& ps = particles(L, live(L, 1));
    stopParticles(ps);
    if (lua_toboolean(L, 2)) clearParticles(ps); // stopParticles(true): quitar también las que quedan
    return 0;
}
int e_emitParticles(lua_State* L) {
    const Entity e = live(L, 1);
    emitParticles(particles(L, e), scene(L).computeWorldMatrix(e), static_cast<int>(luaL_checkinteger(L, 2)));
    return 0;
}
int e_particleCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(aliveParticles(particles(L, live(L, 1)))));
    return 1;
}

// ---------------------------------------------------------------- Física: personaje, consultas y capas
PhysicsApi& phys(lua_State* L) {
    PhysicsApi* p = sys(L).host().scriptPhysics();
    if (!p) luaL_error(L, "la física no está disponible");
    return *p;
}
CharacterController& character(lua_State* L, Entity e) { return component<CharacterController>(L, e, "CharacterController"); }
int e_move(lua_State* L) { // e:move(x, z): velocidad horizontal en m/s (se mantiene hasta el próximo move)
    CharacterController& c = character(L, live(L, 1));
    const float x = float(luaL_checknumber(L, 2));
    const float z = float(lua_gettop(L) >= 4 ? luaL_checknumber(L, 4) : luaL_optnumber(L, 3, 0)); // move(x,z) o move(x,y,z)
    c.desiredVelocity = {x, 0, z};
    return 0;
}
int e_jump(lua_State* L) { character(L, live(L, 1)).jumpSpeed = float(luaL_optnumber(L, 2, 5)); return 0; }
int e_isGrounded(lua_State* L) { lua_pushboolean(L, character(L, live(L, 1)).grounded); return 1; }
int e_endJump(lua_State* L) { character(L, live(L, 1)).cutJump = true; return 0; } // salto variable: soltar el botón
int e_isTouchingWall(lua_State* L) {
    const CharacterController& c = character(L, live(L, 1));
    lua_pushboolean(L, c.touchingWall);
    lua_pushnumber(L, c.wallNormal.x);
    lua_pushnumber(L, c.wallNormal.z);
    return 3;
}
int e_setVerticalVelocity(lua_State* L) { // golpe al suelo (negativa), rebote en un enemigo (positiva)
    CharacterController& c = character(L, live(L, 1));
    c.setVertical = float(luaL_checknumber(L, 2));
    c.hasSetVertical = true;
    return 0;
}
int e_pushCharacter(lua_State* L) { // empujón horizontal inmediato (salto de pared, retroceso al recibir daño)
    CharacterController& c = character(L, live(L, 1));
    c.impulse = c.impulse + Vec3{float(luaL_checknumber(L, 2)), 0, float(luaL_checknumber(L, 3))};
    return 0;
}
int e_jumpsUsed(lua_State* L) { lua_pushinteger(L, character(L, live(L, 1)).jumpsUsed); return 1; }

uint32_t maskArg(lua_State* L, int idx) { return static_cast<uint32_t>(luaL_optinteger(L, idx, 0xFFFF)); }
void pushEntityList(lua_State* L, const std::vector<Entity>& list) {
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int i = 1;
    for (Entity e : list) {
        if (!scene(L).isValid(e)) continue;
        pushEntity(L, e);
        lua_rawseti(L, -2, i++);
    }
}
int p_sphereCast(lua_State* L) { // ox,oy,oz, radio, dx,dy,dz [, distancia, ignorar, máscara]
    const Vec3 o = check3(L, 1, {});
    const float r = float(luaL_checknumber(L, 4));
    const Vec3 d{float(luaL_checknumber(L, 5)), float(luaL_checknumber(L, 6)), float(luaL_checknumber(L, 7))};
    const Entity ignore = lua_isnoneornil(L, 9) ? Entity{} : toEntity(L, 9);
    RaycastHit hit;
    if (!phys(L).sphereCast(o, r, d, float(luaL_optnumber(L, 8, 1000)), hit, maskArg(L, 10), ignore) || !scene(L).isValid(hit.entity)) {
        lua_pushnil(L);
        return 1;
    }
    pushEntity(L, hit.entity);
    push3(L, hit.point);
    lua_pushnumber(L, hit.distance);
    push3(L, hit.normal);
    return 8;
}
int p_overlapSphere(lua_State* L) {
    pushEntityList(L, phys(L).overlapSphere(check3(L, 1, {}), float(luaL_checknumber(L, 4)), maskArg(L, 5)));
    return 1;
}
int p_overlapBox(lua_State* L) { // x,y,z, mitad x,y,z [, máscara]
    const Vec3 c = check3(L, 1, {}), h = check3(L, 4, {0.5f, 0.5f, 0.5f});
    pushEntityList(L, phys(L).overlapBox(c, h, Quat{}, maskArg(L, 7)));
    return 1;
}
int p_setLayerCollision(lua_State* L) {
    phys(L).setLayerCollision(int(luaL_checkinteger(L, 1)), int(luaL_checkinteger(L, 2)), lua_toboolean(L, 3));
    return 0;
}
int p_layersCollide(lua_State* L) {
    lua_pushboolean(L, phys(L).layersCollide(int(luaL_checkinteger(L, 1)), int(luaL_checkinteger(L, 2))));
    return 1;
}
const luaL_Reg kPhysicsExtra[] = {{"sphereCast", p_sphereCast}, {"overlapSphere", p_overlapSphere},
                                  {"overlapBox", p_overlapBox}, {"setLayerCollision", p_setLayerCollision},
                                  {"layersCollide", p_layersCollide}, {nullptr, nullptr}};

// ---------------------------------------------------------------- Máquina de estados de animación
Animator& animator(lua_State* L, Entity e) { return component<Animator>(L, e, "Animator"); }
int e_setAnimParam(lua_State* L) {
    Animator& a = animator(L, live(L, 1));
    const char* name = luaL_checkstring(L, 2);
    const float v = lua_isboolean(L, 3) ? (lua_toboolean(L, 3) ? 1.0f : 0.0f) : float(luaL_checknumber(L, 3));
    if (!setAnimParam(a, name, v)) return luaL_error(L, "el Animator no tiene el parámetro '%s'", name);
    return 0;
}
int e_setAnimTrigger(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);
    if (!setAnimTrigger(animator(L, live(L, 1)), name)) return luaL_error(L, "el Animator no tiene el parámetro '%s'", name);
    return 0;
}
int e_getAnimParam(lua_State* L) { lua_pushnumber(L, animParam(animator(L, live(L, 1)), luaL_checkstring(L, 2))); return 1; }
int e_getAnimState(lua_State* L) { lua_pushstring(L, animator(L, live(L, 1)).machine.current.c_str()); return 1; }

// ---------------------------------------------------------------- Terreno
int e_terrainHeight(lua_State* L) { // altura del terreno (en el mundo) en x, z del mundo
    const Entity e = live(L, 1);
    const Terrain& t = component<Terrain>(L, e, "Terrain");
    const Mat4 world = scene(L).computeWorldMatrix(e);
    const Vec3 local = world.inverted().transformPoint({float(luaL_checknumber(L, 2)), 0, float(luaL_checknumber(L, 3))});
    const Vec3 w = world.transformPoint({local.x, terrainHeightAt(t, local.x, local.z), local.z});
    lua_pushnumber(L, w.y);
    return 1;
}

// ---------------------------------------------------------------- Cámara de seguimiento
int e_shakeCamera(lua_State* L) {
    CameraFollow& c = component<CameraFollow>(L, live(L, 1), "CameraFollow");
    c.shake = float(luaL_optnumber(L, 2, 0.2));
    c.shakeTime = float(luaL_optnumber(L, 3, 0.3));
    return 0;
}
int e_setCameraTarget(lua_State* L) {
    CameraFollow& c = component<CameraFollow>(L, live(L, 1), "CameraFollow");
    c.target = lua_isnoneornil(L, 2) ? UUID{} : scene(L).info(live(L, 2)).id;
    return 0;
}
int e_setCameraDistance(lua_State* L) {
    component<CameraFollow>(L, live(L, 1), "CameraFollow").distance = float(luaL_checknumber(L, 2));
    return 0;
}
int e_setCameraAngles(lua_State* L) { // yaw, pitch en grados
    CameraFollow& c = component<CameraFollow>(L, live(L, 1), "CameraFollow");
    c.yaw = float(luaL_checknumber(L, 2));
    c.pitch = float(luaL_optnumber(L, 3, c.pitch));
    return 0;
}

// ---------------------------------------------------------------- Navegación e IA
NavAgent& agent(lua_State* L, Entity e) { return component<NavAgent>(L, e, "NavAgent"); }
int e_setDestination(lua_State* L) { // e:setDestination(x, y, z) o e:setDestination(otraEntidad) para seguirla
    NavAgent& a = agent(L, live(L, 1));
    if (lua_isuserdata(L, 2)) {
        a.follow = scene(L).info(live(L, 2)).id;
    } else {
        a.follow = {};
        a.destination = check3(L, 2, {});
    }
    a.hasDestination = true;
    a.reached = false;
    a.path.clear();
    a.repathTimer = 0;
    return 0;
}
int e_stopNavigation(lua_State* L) {
    NavAgent& a = agent(L, live(L, 1));
    a.hasDestination = false;
    a.follow = {};
    a.path.clear();
    return 0;
}
int e_hasReachedDestination(lua_State* L) { lua_pushboolean(L, agent(L, live(L, 1)).reached); return 1; }
int e_hasPath(lua_State* L) {
    const NavAgent& a = agent(L, live(L, 1));
    lua_pushboolean(L, a.hasDestination && !a.pathFailed && !a.path.empty());
    return 1;
}
int e_remainingDistance(lua_State* L) {
    const Entity e = live(L, 1);
    const NavAgent& a = agent(L, e);
    float d = 0;
    Vec3 p = scene(L).computeWorldMatrix(e).translation();
    for (size_t i = a.next; i < a.path.size(); ++i) {
        d += (a.path[i] - p).length();
        p = a.path[i];
    }
    lua_pushnumber(L, d);
    return 1;
}
int nav_bake(lua_State* L) { // Navigation.bake() automático, o (cx,cy,cz, ancho, fondo [, celda])
    NavSettings s;
    const bool manual = lua_gettop(L) >= 5;
    if (manual) {
        s.center = check3(L, 1, {});
        s.size = {float(luaL_checknumber(L, 4)), float(luaL_checknumber(L, 5))};
        s.cellSize = float(luaL_optnumber(L, 6, 0.5));
        s.topY = s.center.y + 50;
        s.bottomY = s.center.y - 50;
    }
    lua_pushinteger(L, sys(L).host().scriptBakeNav(manual ? &s : nullptr));
    return 1;
}
int nav_findPath(lua_State* L) {
    NavGrid* g = sys(L).host().scriptNavGrid();
    std::vector<Vec3> path;
    if (!g || !g->findPath(check3(L, 1, {}), check3(L, 4, {}), path)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, static_cast<int>(path.size()), 0);
    for (size_t i = 0; i < path.size(); ++i) {
        lua_createtable(L, 0, 3);
        lua_pushnumber(L, path[i].x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, path[i].y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, path[i].z); lua_setfield(L, -2, "z");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}
int nav_isWalkable(lua_State* L) {
    NavGrid* g = sys(L).host().scriptNavGrid();
    lua_pushboolean(L, g && g->isWalkable(check3(L, 1, {})));
    return 1;
}
const luaL_Reg kNavigationGlobals[] = {{"bake", nav_bake}, {"findPath", nav_findPath}, {"isWalkable", nav_isWalkable},
                                       {nullptr, nullptr}};

// AI.canSee(observador, objetivo [, distancia=20, campoVisión=120]): está cerca, dentro del ángulo de visión y no hay
// nada en medio (un rayo desde los "ojos" del observador hasta el objetivo).
int ai_canSee(lua_State* L) {
    const Entity me = live(L, 1), target = live(L, 2);
    const float maxDist = float(luaL_optnumber(L, 3, 20)), fov = float(luaL_optnumber(L, 4, 120));
    const Mat4 mw = scene(L).computeWorldMatrix(me);
    const Vec3 eye = mw.translation() + Vec3{0, 1.5f, 0};
    const Vec3 aim = scene(L).computeWorldMatrix(target).translation() + Vec3{0, 1.0f, 0};
    const Vec3 to = aim - eye;
    const float dist = to.length();
    bool sees = dist <= maxDist && dist > 1e-4f;
    if (sees && fov < 360) {
        const Vec3 fwd = mw.transformDirection({0, 0, -1}).normalized();
        const Vec3 flatTo = Vec3{to.x, 0, to.z}.normalized(), flatFwd = Vec3{fwd.x, 0, fwd.z}.normalized();
        sees = dot(flatTo, flatFwd) >= std::cos(fov * 0.5f * kDegToRad);
    }
    if (sees) {
        if (PhysicsApi* p = sys(L).host().scriptPhysics()) {
            RaycastHit hit;
            if (p->raycast(eye, to, dist, hit, me)) sees = hit.entity == target || scene(L).isDescendantOf(hit.entity, target);
        }
    }
    lua_pushboolean(L, sees);
    return 1;
}
int ai_distance(lua_State* L) {
    const Vec3 a = scene(L).computeWorldMatrix(live(L, 1)).translation(), b = scene(L).computeWorldMatrix(live(L, 2)).translation();
    lua_pushnumber(L, (a - b).length());
    return 1;
}
const luaL_Reg kAIGlobals[] = {{"canSee", ai_canSee}, {"distance", ai_distance}, {nullptr, nullptr}};

const luaL_Reg kWorldEntityMethods[] = {
    {"setDestination", e_setDestination}, {"stopNavigation", e_stopNavigation},
    {"hasReachedDestination", e_hasReachedDestination}, {"hasPath", e_hasPath}, {"remainingDistance", e_remainingDistance},
    {"shakeCamera", e_shakeCamera}, {"setCameraTarget", e_setCameraTarget}, {"setCameraDistance", e_setCameraDistance},
    {"setCameraAngles", e_setCameraAngles},
    {"terrainHeight", e_terrainHeight},
    {"setAnimParam", e_setAnimParam}, {"setAnimTrigger", e_setAnimTrigger}, {"getAnimParam", e_getAnimParam},
    {"getAnimState", e_getAnimState},
    {"move", e_move}, {"jump", e_jump}, {"isGrounded", e_isGrounded}, {"endJump", e_endJump},
    {"isTouchingWall", e_isTouchingWall}, {"setVerticalVelocity", e_setVerticalVelocity},
    {"pushCharacter", e_pushCharacter}, {"jumpsUsed", e_jumpsUsed},
    {"playParticles", e_playParticles}, {"stopParticles", e_stopParticles}, {"emitParticles", e_emitParticles},
    {"particleCount", e_particleCount}, {nullptr, nullptr}};

} // namespace

void registerWorldEntityMethods(lua_State* L) { luaL_setfuncs(L, kWorldEntityMethods, 0); }

void registerWorldGlobals(lua_State* L) {
    setGlobalTable(L, "Quality", kQualityGlobals);
    setGlobalTable(L, "Save", kSaveGlobals);
    setGlobalTable(L, "Navigation", kNavigationGlobals);
    setGlobalTable(L, "AI", kAIGlobals);
    lua_getglobal(L, "Input"); // se añaden a la tabla Input que ya existe
    luaL_setfuncs(L, kInputExtra, 0);
    lua_pop(L, 1);
    lua_getglobal(L, "Debug");
    lua_pushcfunction(L, d_stats);
    lua_setfield(L, -2, "stats");
    lua_pop(L, 1);
    lua_getglobal(L, "Physics");
    luaL_setfuncs(L, kPhysicsExtra, 0);
    lua_pop(L, 1);
}

} // namespace titan::script_detail
