// API de Lua de la red multijugador (Net.*). Los mensajes son tablas de Lua (o textos) que viajan como JSON.
//   Net.host(puerto[, jugadores])  -> true o false, error       Net.connect(dirección, puerto) -> true o false, error
//   Net.send(datos[, jugador][, fiable])                        Net.receive() -> lista de {type, peer, data}
//   Net.close()  Net.isServer()  Net.connected()  Net.peers()  Net.stats()
#include "ScriptInternal.h"
#include "titan/net/NetSession.h"

namespace titan::script_detail {

namespace {
NetSession& net(lua_State* L) {
    NetSession* n = sys(L).host().scriptNet();
    if (!n) luaL_error(L, "Net: la red no está disponible aquí");
    return *n;
}

int n_host(lua_State* L) {
    std::string err;
    const bool ok = net(L).host(uint16_t(luaL_checkinteger(L, 1)), int(luaL_optinteger(L, 2, 16)), &err);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    lua_pushstring(L, err.c_str());
    return 2;
}

int n_connect(lua_State* L) {
    std::string err;
    const bool ok = net(L).connect(luaL_checkstring(L, 1), uint16_t(luaL_checkinteger(L, 2)), &err);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    lua_pushstring(L, err.c_str());
    return 2;
}

int n_close(lua_State* L) {
    net(L).close();
    return 0;
}

// Net.send(datos[, jugador][, fiable]): jugador nil = todos (o el servidor, en un cliente).
int n_send(lua_State* L) {
    std::string payload;
    if (lua_type(L, 1) == LUA_TSTRING) payload = Json(lua_tostring(L, 1)).dump();
    else payload = luaToJson(L, 1).dump();
    if (payload.size() > 64 * 1024) return luaL_error(L, "Net.send: mensaje de más de 64 KB");
    const int peer = lua_isnoneornil(L, 2) ? -1 : int(luaL_checkinteger(L, 2));
    const bool reliable = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3) != 0;
    lua_pushboolean(L, net(L).send(payload, peer, reliable));
    return 1;
}

int n_receive(lua_State* L) {
    const std::vector<NetEvent> events = net(L).poll();
    lua_createtable(L, int(events.size()), 0);
    int i = 1;
    for (const NetEvent& ev : events) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, ev.type == NetEvent::Type::Connected ? "connect" : ev.type == NetEvent::Type::Disconnected ? "disconnect" : "message");
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev.peer);
        lua_setfield(L, -2, "peer");
        if (ev.type == NetEvent::Type::Message) {
            const Json j = Json::parse(ev.data, nullptr, false);
            if (j.is_discarded()) lua_pushnil(L); // mensaje que no es de Titán: se ignora su contenido
            else jsonToLua(L, j);
            lua_setfield(L, -2, "data");
        }
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int n_isServer(lua_State* L) { lua_pushboolean(L, net(L).isServer()); return 1; }
int n_connected(lua_State* L) { lua_pushboolean(L, net(L).active()); return 1; }

int n_peers(lua_State* L) {
    const std::vector<int> ids = net(L).peers();
    lua_createtable(L, int(ids.size()), 0);
    for (size_t k = 0; k < ids.size(); ++k) {
        lua_pushinteger(L, ids[k]);
        lua_rawseti(L, -2, int(k + 1));
    }
    return 1;
}

int n_stats(lua_State* L) {
    NetSession& n = net(L);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, lua_Integer(n.bytesSent()));
    lua_setfield(L, -2, "sent");
    lua_pushinteger(L, lua_Integer(n.bytesReceived()));
    lua_setfield(L, -2, "received");
    lua_pushinteger(L, n.peerCount());
    lua_setfield(L, -2, "players");
    return 1;
}

const luaL_Reg kNetGlobals[] = {{"host", n_host}, {"connect", n_connect}, {"close", n_close}, {"send", n_send},
                                {"receive", n_receive}, {"isServer", n_isServer}, {"connected", n_connected},
                                {"peers", n_peers}, {"stats", n_stats}, {nullptr, nullptr}};
} // namespace

void registerNetGlobals(lua_State* L) { setGlobalTable(L, "Net", kNetGlobals); }

} // namespace titan::script_detail
