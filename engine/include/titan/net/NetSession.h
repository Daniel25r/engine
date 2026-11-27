#pragma once
// Red para juegos multijugador (ENet, MIT): un jugador hace de servidor (host) y los demás se conectan.
// Mensajes fiables (llegan y en orden: chat, disparos, puntos) o rápidos (pueden perderse: posiciones cada fotograma).
// Sin hilos propios: poll() se llama una vez por fotograma y devuelve lo que llegó.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace titan {

struct NetEvent {
    enum class Type { Connected, Disconnected, Message } type = Type::Message;
    int peer = 0;            // id del jugador (en el cliente, 0 = el servidor)
    std::string data;        // contenido del mensaje
};

class NetSession {
public:
    NetSession();
    ~NetSession();
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Servidor en el puerto dado (todas las interfaces). false y error si el puerto está ocupado.
    bool host(uint16_t port, int maxPlayers = 16, std::string* error = nullptr);
    // Cliente: empieza a conectar (el evento Connected llega con poll()).
    bool connect(const std::string& address, uint16_t port, std::string* error = nullptr);
    void close();

    // Procesa la red y devuelve los eventos llegados desde la última llamada.
    std::vector<NetEvent> poll();
    // peer < 0: a todos (en el cliente: al servidor). reliable = llega seguro y en orden.
    bool send(const std::string& data, int peer = -1, bool reliable = true);

    bool isServer() const;
    bool active() const;      // servidor abierto o cliente conectado
    bool open() const;        // servidor abierto o cliente conectado o conectando
    int peerCount() const;    // jugadores conectados (en el cliente: 1 si está conectado al servidor)
    std::vector<int> peers() const;
    uint64_t bytesSent() const;
    uint64_t bytesReceived() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace titan
