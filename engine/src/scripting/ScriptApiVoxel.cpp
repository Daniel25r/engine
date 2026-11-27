// API de Lua del mundo de bloques (Voxel.*). Funciona si la escena tiene un componente VoxelTerrain; si no, las
// consultas devuelven aire/nil y los cambios devuelven false (sin errores: un script puede comprobar Voxel.available()).
#include "ScriptInternal.h"
#include <cmath>

#include "titan/core/Voxel.h"

namespace titan::script_detail {

namespace {
VoxelWorld* world(lua_State* L) { return sys(L).host().scriptVoxelWorld(); }
int toInt(lua_State* L, int i) { return int(std::floor(luaL_checknumber(L, i))); }

// Un bloque se puede indicar por número o por nombre ("Piedra").
BlockId blockArg(lua_State* L, VoxelWorld& w, int i) {
    if (lua_type(L, i) == LUA_TSTRING) {
        const char* name = lua_tostring(L, i);
        if (std::string(name) == "Aire") return kAir;
        const BlockId b = w.blocks.find(name);
        if (b == kAir) luaL_error(L, "Voxel: no existe el bloque '%s'", name);
        return b;
    }
    const lua_Integer b = luaL_checkinteger(L, i);
    if (b < 0 || size_t(b) >= w.blocks.size()) luaL_error(L, "Voxel: bloque %d fuera de rango", int(b));
    return BlockId(b);
}

int v_available(lua_State* L) { lua_pushboolean(L, world(L) != nullptr); return 1; }

int v_get(lua_State* L) {
    VoxelWorld* w = world(L);
    lua_pushinteger(L, w ? w->get(toInt(L, 1), toInt(L, 2), toInt(L, 3)) : 0);
    return 1;
}

int v_set(lua_State* L) {
    VoxelWorld* w = world(L);
    if (!w) { lua_pushboolean(L, 0); return 1; }
    const int x = toInt(L, 1), y = toInt(L, 2), z = toInt(L, 3);
    const BlockId b = blockArg(L, *w, 4);
    const bool ok = w->get(x, y, z) != b && w->set(x, y, z, b);
    if (ok) sys(L).host().scriptVoxelChanged(x, y, z); // la malla y la colisión se rehacen al momento
    lua_pushboolean(L, ok);
    return 1;
}

int v_block(lua_State* L) {
    VoxelWorld* w = world(L);
    const BlockId b = w ? w->blocks.find(luaL_checkstring(L, 1)) : kAir;
    lua_pushinteger(L, b);
    return 1;
}

int v_blockName(lua_State* L) {
    VoxelWorld* w = world(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (!w || id < 0 || size_t(id) >= w->blocks.size()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, id == 0 ? "Aire" : w->blocks.get(BlockId(id)).name.c_str());
    return 1;
}

int v_blockCount(lua_State* L) {
    VoxelWorld* w = world(L);
    lua_pushinteger(L, w ? lua_Integer(w->blocks.size()) : 0);
    return 1;
}

// Voxel.raycast(ox, oy, oz, dx, dy, dz, distancia) -> {x, y, z, nx, ny, nz, block, distance} o nil
int v_raycast(lua_State* L) {
    VoxelWorld* w = world(L);
    const Vec3 o = check3(L, 1, {});
    Vec3 d = check3(L, 4, {0, -1, 0});
    const float maxD = float(luaL_optnumber(L, 7, 8.0));
    VoxelWorld::RayHit h;
    if (!w || d.length() < 1e-6f || !w->raycast(o, d.normalized(), maxD, h)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 8);
    const std::pair<const char*, double> f[] = {{"x", h.x}, {"y", h.y}, {"z", h.z}, {"nx", h.nx}, {"ny", h.ny},
                                                {"nz", h.nz}, {"block", h.block}, {"distance", h.distance}};
    for (const auto& [k, v] : f) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, k);
    }
    return 1;
}

int v_surface(lua_State* L) {
    VoxelWorld* w = world(L);
    lua_pushinteger(L, w ? w->surfaceHeight(toInt(L, 1), toInt(L, 2)) : -1);
    return 1;
}

int v_biome(lua_State* L) {
    VoxelWorld* w = world(L);
    if (!w) { lua_pushnil(L); return 1; }
    lua_pushstring(L, biomeName(biomeAt(w->gen, toInt(L, 1), toInt(L, 2))));
    return 1;
}

int v_save(lua_State* L) { lua_pushinteger(L, sys(L).host().scriptVoxelSave()); return 1; }

int v_loaded(lua_State* L) {
    VoxelWorld* w = world(L);
    lua_pushinteger(L, w ? lua_Integer(w->loadedChunks()) : 0);
    return 1;
}

int v_seed(lua_State* L) {
    VoxelWorld* w = world(L);
    lua_pushinteger(L, w ? lua_Integer(w->gen.seed) : 0);
    return 1;
}

const luaL_Reg kVoxelGlobals[] = {{"available", v_available}, {"get", v_get}, {"set", v_set}, {"block", v_block},
                                  {"blockName", v_blockName}, {"blockCount", v_blockCount}, {"raycast", v_raycast},
                                  {"surface", v_surface}, {"biome", v_biome}, {"save", v_save}, {"loaded", v_loaded},
                                  {"seed", v_seed}, {nullptr, nullptr}};
} // namespace

void registerVoxelGlobals(lua_State* L) { setGlobalTable(L, "Voxel", kVoxelGlobals); }

} // namespace titan::script_detail
