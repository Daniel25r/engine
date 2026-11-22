#pragma once
// Utilidades internas compartidas por los archivos que implementan la API de Lua
// (ScriptSystem.cpp y ScriptApi*.cpp). No es una cabecera pública.
#include "lauxlib.h"
#include "lua.h"
#include "titan/scripting/ScriptSystem.h"

namespace titan::script_detail {

inline constexpr const char* kEntityMeta = "Titan.Entity";

inline ScriptSystem& sys(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return *static_cast<ScriptSystem*>(ud);
}

inline Scene& scene(lua_State* L) {
    Scene* s = sys(L).scene();
    if (!s) luaL_error(L, "no hay ninguna escena cargada");
    return *s;
}

struct LuaEntity {
    uint32_t index;
    uint32_t generation;
};

inline void pushEntity(lua_State* L, Entity e) {
    if (e.isNull()) {
        lua_pushnil(L);
        return;
    }
    auto* u = static_cast<LuaEntity*>(lua_newuserdatauv(L, sizeof(LuaEntity), 0));
    *u = {e.index, e.generation};
    luaL_setmetatable(L, kEntityMeta);
}

inline Entity toEntity(lua_State* L, int idx) {
    auto* u = static_cast<LuaEntity*>(luaL_checkudata(L, idx, kEntityMeta));
    return {u->index, u->generation};
}

inline Entity live(lua_State* L, int idx) {
    const Entity e = toEntity(L, idx);
    if (!scene(L).isValid(e)) luaL_error(L, "la entidad ya no existe (fue destruida)");
    return e;
}

inline int push3(lua_State* L, const Vec3& v) {
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    lua_pushnumber(L, v.z);
    return 3;
}

inline Vec3 check3(lua_State* L, int first, const Vec3& def) {
    return {static_cast<float>(luaL_optnumber(L, first, def.x)), static_cast<float>(luaL_optnumber(L, first + 1, def.y)),
            static_cast<float>(luaL_optnumber(L, first + 2, def.z))};
}

template <class T>
T& component(lua_State* L, Entity e, const char* name) {
    T* c = scene(L).world().get<T>(e);
    if (!c) luaL_error(L, "'%s' no tiene %s", scene(L).info(e).name.c_str(), name);
    return *c;
}

inline void setGlobalTable(lua_State* L, const char* name, const luaL_Reg* fns) {
    lua_newtable(L);
    luaL_setfuncs(L, fns, 0);
    lua_setglobal(L, name);
}

// ScriptApiWorld.cpp: calidad, partículas, entrada por acciones, física avanzada, animación, guardado.
// Añade métodos a la tabla de métodos de Entity (en la cima de la pila) y crea sus tablas globales.
void registerWorldEntityMethods(lua_State* L);
void registerWorldGlobals(lua_State* L);

} // namespace titan::script_detail
