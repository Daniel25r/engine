#pragma once
// Bus de eventos tipado y síncrono.
// - subscribe<E>(fn) devuelve una Subscription: al destruirla se da de baja sola,
//   así ningún sistema recibe eventos después de morir.
// - Es seguro suscribirse o darse de baja mientras se emite un evento.
// - Si el bus muere antes que la suscripción, la suscripción simplemente no hace nada.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "titan/core/World.h"

namespace titan {

class Scene;

// ---- Eventos del motor ----
struct EntityCreatedEvent { Scene* scene; Entity entity; };
struct EntityDestroyedEvent { Scene* scene; Entity entity; }; // se emite ANTES de borrar sus datos
struct SceneLoadedEvent { Scene* scene; };
struct SceneUnloadedEvent { Scene* scene; };
struct AppPausedEvent {};
struct AppResumedEvent {};

namespace detail {
struct BusState {
    struct Handler {
        uint64_t id;
        std::function<void(const void*)> fn;
        bool alive;
    };
    std::unordered_map<uint32_t, std::vector<Handler>> handlers;
    uint64_t nextId = 1;
    int dispatchDepth = 0;
    bool needsCompact = false;

    void unsubscribe(uint32_t type, uint64_t id) {
        auto it = handlers.find(type);
        if (it == handlers.end()) return;
        for (auto& h : it->second)
            if (h.id == id) {
                h.alive = false;
                needsCompact = true;
            }
        compact();
    }
    void compact() {
        if (dispatchDepth > 0 || !needsCompact) return;
        for (auto& [type, list] : handlers)
            list.erase(std::remove_if(list.begin(), list.end(), [](const Handler& h) { return !h.alive; }), list.end());
        needsCompact = false;
    }
};
} // namespace detail

class Subscription {
public:
    Subscription() = default;
    Subscription(std::weak_ptr<detail::BusState> bus, uint32_t type, uint64_t id) : bus_(std::move(bus)), type_(type), id_(id) {}
    Subscription(Subscription&& o) noexcept { *this = std::move(o); }
    Subscription& operator=(Subscription&& o) noexcept {
        if (this != &o) {
            reset();
            bus_ = std::move(o.bus_);
            type_ = o.type_;
            id_ = o.id_;
            o.id_ = 0;
        }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { reset(); }

    void reset() {
        if (id_ == 0) return;
        if (auto bus = bus_.lock()) bus->unsubscribe(type_, id_);
        id_ = 0;
    }

private:
    std::weak_ptr<detail::BusState> bus_;
    uint32_t type_ = 0;
    uint64_t id_ = 0;
};

class EventBus {
public:
    EventBus() : state_(std::make_shared<detail::BusState>()) {}

    template <class E, class F>
    [[nodiscard]] Subscription subscribe(F&& fn) {
        const uint32_t type = detail::componentId<E>();
        const uint64_t id = state_->nextId++;
        state_->handlers[type].push_back(
            {id, [f = std::forward<F>(fn)](const void* e) { f(*static_cast<const E*>(e)); }, true});
        return Subscription(state_, type, id);
    }

    template <class E>
    void emit(const E& event) {
        auto it = state_->handlers.find(detail::componentId<E>());
        if (it == state_->handlers.end()) return;
        auto keepAlive = state_;
        keepAlive->dispatchDepth++;
        // Los suscriptores añadidos durante la emisión no reciben este evento.
        const size_t count = it->second.size();
        for (size_t i = 0; i < count; ++i) {
            auto& list = keepAlive->handlers[detail::componentId<E>()];
            if (i < list.size() && list[i].alive) {
                auto fn = list[i].fn; // copia: la lista puede crecer dentro de fn
                fn(&event);
            }
        }
        keepAlive->dispatchDepth--;
        keepAlive->compact();
    }

private:
    std::shared_ptr<detail::BusState> state_;
};

} // namespace titan
