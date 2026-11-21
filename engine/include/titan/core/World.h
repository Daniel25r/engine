#pragma once
// Almacén de entidades y componentes (ECS) basado en "sparse sets".
// - Entity = índice + generación. Al destruir una entidad su generación aumenta,
//   así cualquier copia antigua del Entity deja de ser válida (protección ante referencias obsoletas).
// - Cada tipo de componente vive en un arreglo denso propio: recorrerlo es recorrer memoria contigua.
// World no sabe nada de escenas, jerarquías ni archivos: eso es trabajo de Scene.
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace titan {

struct Entity {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool isNull() const { return index == UINT32_MAX; }
    bool operator==(const Entity& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Entity& o) const { return !(*this == o); }
};

namespace detail {

inline uint32_t nextComponentId() {
    static uint32_t counter = 0;
    return counter++;
}

template <class T>
uint32_t componentId() {
    static const uint32_t id = nextComponentId();
    return id;
}

struct PoolBase {
    virtual ~PoolBase() = default;
    virtual void remove(uint32_t index) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
};

template <class T>
struct Pool final : PoolBase {
    std::vector<uint32_t> sparse; // índice de entidad -> posición densa + 1 (0 = no tiene)
    std::vector<uint32_t> owners; // posición densa -> índice de entidad
    std::vector<T> data;

    bool contains(uint32_t i) const { return i < sparse.size() && sparse[i] != 0; }
    T* get(uint32_t i) { return contains(i) ? &data[sparse[i] - 1] : nullptr; }
    const T* get(uint32_t i) const { return contains(i) ? &data[sparse[i] - 1] : nullptr; }

    T& set(uint32_t i, T value) {
        if (i >= sparse.size()) sparse.resize(i + 1, 0);
        if (sparse[i]) {
            data[sparse[i] - 1] = std::move(value);
            return data[sparse[i] - 1];
        }
        owners.push_back(i);
        data.push_back(std::move(value));
        sparse[i] = static_cast<uint32_t>(data.size());
        return data.back();
    }

    void remove(uint32_t i) override {
        if (!contains(i)) return;
        const uint32_t pos = sparse[i] - 1;
        const uint32_t last = static_cast<uint32_t>(data.size() - 1);
        if (pos != last) {
            data[pos] = std::move(data[last]);
            owners[pos] = owners[last];
            sparse[owners[pos]] = pos + 1;
        }
        data.pop_back();
        owners.pop_back();
        sparse[i] = 0;
    }

    void clear() override {
        sparse.clear();
        owners.clear();
        data.clear();
    }

    size_t size() const override { return data.size(); }
};

} // namespace detail

class World {
public:
    Entity create() {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<uint32_t>(generations_.size());
            generations_.push_back(0);
        }
        ++alive_;
        return {index, generations_[index]};
    }

    bool alive(Entity e) const { return e.index < generations_.size() && generations_[e.index] == e.generation; }

    void destroy(Entity e) {
        if (!alive(e)) return;
        for (auto& p : pools_)
            if (p) p->remove(e.index);
        ++generations_[e.index];
        free_.push_back(e.index);
        --alive_;
    }

    void clear() {
        for (auto& p : pools_)
            if (p) p->clear();
        free_.clear();
        for (uint32_t i = 0; i < generations_.size(); ++i) {
            ++generations_[i];
            free_.push_back(i);
        }
        alive_ = 0;
    }

    template <class T>
    T& add(Entity e, T value = T{}) {
        return pool<T>().set(e.index, std::move(value));
    }
    template <class T>
    T* get(Entity e) {
        return alive(e) ? pool<T>().get(e.index) : nullptr;
    }
    template <class T>
    const T* get(Entity e) const {
        const auto* p = findPool<T>();
        return (alive(e) && p) ? p->get(e.index) : nullptr;
    }
    template <class T>
    bool has(Entity e) const {
        const auto* p = findPool<T>();
        return alive(e) && p && p->contains(e.index);
    }
    template <class T>
    void remove(Entity e) {
        if (alive(e)) pool<T>().remove(e.index);
    }

    // Recorre las entidades que tienen A y Rest...: fn(Entity, A&, Rest&...).
    // Va hacia atrás para que destruir la entidad visitada sea seguro.
    // No añadas componentes de tipo A durante el recorrido.
    template <class A, class... Rest, class F>
    void each(F&& fn) {
        auto& pa = pool<A>();
        for (size_t i = pa.data.size(); i-- > 0;) {
            if (i >= pa.data.size()) continue;
            const uint32_t idx = pa.owners[i];
            if constexpr (sizeof...(Rest) == 0) {
                fn(Entity{idx, generations_[idx]}, pa.data[i]);
            } else {
                if (!(pool<Rest>().contains(idx) && ...)) continue;
                fn(Entity{idx, generations_[idx]}, pa.data[i], *pool<Rest>().get(idx)...);
            }
        }
    }

    size_t count() const { return alive_; }
    // Número total de componentes de todos los tipos (para el perfilador).
    size_t componentCount() const {
        size_t n = 0;
        for (auto& p : pools_)
            if (p) n += p->size();
        return n;
    }

private:
    template <class T>
    detail::Pool<T>& pool() {
        const uint32_t id = detail::componentId<T>();
        if (id >= pools_.size()) pools_.resize(id + 1);
        if (!pools_[id]) pools_[id] = std::make_unique<detail::Pool<T>>();
        return *static_cast<detail::Pool<T>*>(pools_[id].get());
    }
    template <class T>
    const detail::Pool<T>* findPool() const {
        const uint32_t id = detail::componentId<T>();
        return id < pools_.size() ? static_cast<const detail::Pool<T>*>(pools_[id].get()) : nullptr;
    }

    std::vector<uint32_t> generations_;
    std::vector<uint32_t> free_;
    std::vector<std::unique_ptr<detail::PoolBase>> pools_;
    size_t alive_ = 0;
};

} // namespace titan
