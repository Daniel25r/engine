// Red multijugador: un servidor y un cliente de verdad (UDP por localhost) en el mismo proceso.
#include <chrono>
#include <thread>

#include "Test.h"
#include "titan/net/NetSession.h"

using namespace titan;

namespace {
// Procesa los dos lados hasta que se cumpla la condición (o 3 s).
template <class F>
bool pump(NetSession& a, NetSession& b, std::vector<NetEvent>& evA, std::vector<NetEvent>& evB, F done) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        for (auto& e : a.poll()) evA.push_back(e);
        for (auto& e : b.poll()) evB.push_back(e);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
bool has(const std::vector<NetEvent>& v, NetEvent::Type t, const std::string& data = {}) {
    for (const NetEvent& e : v)
        if (e.type == t && (data.empty() || e.data == data)) return true;
    return false;
}
} // namespace

TEST("red: servidor y cliente se conectan, intercambian mensajes y se despiden") {
    NetSession server, client;
    std::string err;
    const uint16_t port = 47231;
    CHECK(server.host(port, 4, &err));
    CHECK(server.isServer());
    CHECK(client.connect("127.0.0.1", port, &err));
    std::vector<NetEvent> sv, cl;
    CHECK(pump(server, client, sv, cl, [&] { return has(sv, NetEvent::Type::Connected) && has(cl, NetEvent::Type::Connected); }));
    CHECK_EQ(server.peerCount(), 1);
    CHECK(client.active() && !client.isServer());
    const int playerId = server.peers().empty() ? -1 : server.peers()[0];
    CHECK(playerId >= 1);
    // Cliente -> servidor (fiable) y servidor -> ese jugador; también a todos.
    CHECK(client.send("{\"hola\":1}"));
    CHECK(server.send("{\"bienvenido\":true}", playerId));
    CHECK(server.send("\"para todos\"", -1, false));
    CHECK(pump(server, client, sv, cl, [&] {
        return has(sv, NetEvent::Type::Message, "{\"hola\":1}") && has(cl, NetEvent::Type::Message, "{\"bienvenido\":true}") &&
               has(cl, NetEvent::Type::Message, "\"para todos\"");
    }));
    for (const NetEvent& e : sv)
        if (e.type == NetEvent::Type::Message) CHECK_EQ(e.peer, playerId); // el servidor sabe quién lo envió
    CHECK(server.bytesReceived() > 0 && client.bytesSent() > 0);
    // Un mensaje a un jugador que no existe falla sin romper nada.
    CHECK(!server.send("x", 999));
    // Al cerrar el cliente, el servidor recibe la desconexión.
    client.close();
    CHECK(pump(server, client, sv, cl, [&] { return has(sv, NetEvent::Type::Disconnected); }));
    CHECK_EQ(server.peerCount(), 0);
}

TEST("red: puerto ocupado y dirección no válida dan un error claro") {
    NetSession a, b, c;
    std::string err;
    CHECK(a.host(47232, 2, &err));
    CHECK(!b.host(47232, 2, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!c.connect("esto no es una direccion", 47232, &err));
    CHECK(!err.empty());
    CHECK(!c.send("x")); // sin conexión
}
