#include "titan/net/NetSession.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>

#include "titan/core/Log.h"
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // <windows.h> (vía ENet) no debe definir min/max
#endif
#include "enet/enet.h"

namespace titan {

namespace {
std::once_flag gInit;
bool gInitOk = false;
void initEnet() {
    std::call_once(gInit, [] {
        gInitOk = enet_initialize() == 0;
        if (gInitOk) std::atexit(enet_deinitialize);
    });
}
} // namespace

struct NetSession::Impl {
    ENetHost* host = nullptr;
    bool server = false;
    ENetPeer* serverPeer = nullptr;     // cliente: conexión con el servidor
    bool connected = false;
    std::map<int, ENetPeer*> peers;     // servidor: jugadores por id
    int nextId = 1;
};

NetSession::NetSession() : impl_(std::make_unique<Impl>()) {}
NetSession::~NetSession() { close(); }

bool NetSession::host(uint16_t port, int maxPlayers, std::string* error) {
    close();
    initEnet();
    if (!gInitOk) {
        if (error) *error = "no se pudo iniciar la red";
        return false;
    }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    impl_->host = enet_host_create(&addr, size_t(std::max(1, maxPlayers)), 2, 0, 0);
    if (!impl_->host) {
        if (error) *error = "no se pudo abrir el puerto " + std::to_string(port) + " (¿ocupado?)";
        return false;
    }
    impl_->server = true;
    TITAN_INFO("net", "Servidor abierto en el puerto %u (hasta %d jugadores)", unsigned(port), maxPlayers);
    return true;
}

bool NetSession::connect(const std::string& address, uint16_t port, std::string* error) {
    close();
    initEnet();
    if (!gInitOk) {
        if (error) *error = "no se pudo iniciar la red";
        return false;
    }
    impl_->host = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress addr;
    if (!impl_->host || enet_address_set_host(&addr, address.c_str()) != 0) {
        if (error) *error = "dirección no válida: " + address;
        close();
        return false;
    }
    addr.port = port;
    impl_->serverPeer = enet_host_connect(impl_->host, &addr, 2, 0);
    if (!impl_->serverPeer) {
        if (error) *error = "no se pudo conectar";
        close();
        return false;
    }
    impl_->server = false;
    return true;
}

void NetSession::close() {
    if (!impl_->host) return;
    if (impl_->server)
        for (auto& [id, p] : impl_->peers) enet_peer_disconnect_now(p, 0);
    else if (impl_->serverPeer)
        enet_peer_disconnect_now(impl_->serverPeer, 0);
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
    *impl_ = Impl{};
}

std::vector<NetEvent> NetSession::poll() {
    std::vector<NetEvent> out;
    if (!impl_->host) return out;
    ENetEvent ev;
    while (enet_host_service(impl_->host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            int id = 0;
            if (impl_->server) {
                id = impl_->nextId++;
                impl_->peers[id] = ev.peer;
                ev.peer->data = reinterpret_cast<void*>(intptr_t(id));
            } else {
                impl_->connected = true;
            }
            out.push_back({NetEvent::Type::Connected, id, {}});
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
            const int id = impl_->server ? int(intptr_t(ev.peer->data)) : 0;
            if (impl_->server) impl_->peers.erase(id);
            else impl_->connected = false, impl_->serverPeer = nullptr;
            out.push_back({NetEvent::Type::Disconnected, id, {}});
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            const int id = impl_->server ? int(intptr_t(ev.peer->data)) : 0;
            out.push_back({NetEvent::Type::Message, id,
                           std::string(reinterpret_cast<const char*>(ev.packet->data), ev.packet->dataLength)});
            enet_packet_destroy(ev.packet);
            break;
        }
        default: break;
        }
    }
    return out;
}

bool NetSession::send(const std::string& data, int peer, bool reliable) {
    if (!impl_->host) return false;
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    const enet_uint8 channel = reliable ? 0 : 1;
    if (!impl_->server) {
        if (!impl_->connected || !impl_->serverPeer) {
            enet_packet_destroy(pkt);
            return false;
        }
        return enet_peer_send(impl_->serverPeer, channel, pkt) == 0;
    }
    if (peer < 0) {
        enet_host_broadcast(impl_->host, channel, pkt);
        return true;
    }
    auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end()) {
        enet_packet_destroy(pkt);
        return false;
    }
    return enet_peer_send(it->second, channel, pkt) == 0;
}

bool NetSession::isServer() const { return impl_->host && impl_->server; }
bool NetSession::open() const { return impl_->host != nullptr; }
bool NetSession::active() const { return impl_->host && (impl_->server || impl_->connected); }
int NetSession::peerCount() const { return impl_->server ? int(impl_->peers.size()) : (impl_->connected ? 1 : 0); }
std::vector<int> NetSession::peers() const {
    std::vector<int> ids;
    if (impl_->server)
        for (auto& [id, p] : impl_->peers) ids.push_back(id);
    else if (impl_->connected)
        ids.push_back(0);
    return ids;
}
uint64_t NetSession::bytesSent() const { return impl_->host ? impl_->host->totalSentData : 0; }
uint64_t NetSession::bytesReceived() const { return impl_->host ? impl_->host->totalReceivedData : 0; }

} // namespace titan
