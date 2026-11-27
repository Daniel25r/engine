#pragma once
// Caché de recursos de GPU por id de recurso (AssetRef → textura, modelo o fuente del Renderer).
// La comparten el editor y el modo Jugar: al pulsar Jugar no se vuelve a leer ni a subir nada que el editor ya
// tenga cargado, y cada textura está una sola vez en la memoria de la GPU.
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "titan/core/JobSystem.h"

#include "titan/core/Project.h"
#include "titan/render/Renderer.h"

namespace titan {

class ResourceCache {
public:
    // assets puede cambiar (otro proyecto abierto): entonces hay que llamar a clear().
    void bind(Renderer* renderer, const AssetDatabase* assets, FileSource* files = nullptr) {
        renderer_ = renderer;
        assets_ = assets;
        if (files) files_ = files;
    }
    // Carga asíncrona: con un JobSystem, las texturas se leen y decodifican en otros hilos y aquí solo se suben a
    // la GPU (pump). Mientras tanto texture() devuelve la blanca. Sin JobSystem (nullptr), todo es síncrono.
    void setJobs(JobSystem* jobs);
    // Pide una textura sin esperarla (precarga al empezar una escena). No hace nada si ya está o ya se pidió.
    void requestTexture(AssetRef ref);
    // Sube a la GPU las texturas ya decodificadas, hasta gastar budgetMs (al menos una). Devuelve cuántas subió.
    // Llamarla en el hilo del render, una vez por fotograma.
    int pump(double budgetMs);
    size_t pendingTextures() const { return pending_.size(); }
    // Espera a que terminen las lecturas en curso (antes de destruir el JobSystem o el FileSource).
    void waitPending();

    // Las rutas de los recursos son relativas a "assets/" en el FileSource del Renderer.
    TextureHandle texture(AssetRef ref); // kWhiteTexture si no existe, no se puede leer o aún se está cargando
    int model(AssetRef ref);             // -1 si no existe
    int font(AssetRef ref);              // -1 si no existe (se usará la fuente del motor)
    // Material (.mat) leído y validado; nulo si no existe o no es válido. Sus campos de material
    // sustituyen a los del MeshRenderer que lo usa.
    const MeshRenderer* material(AssetRef ref);
    // Vuelve a leer un material la próxima vez (el editor lo llama al guardarlo).
    void invalidateMaterial(AssetRef ref) { materials_.erase(ref.value); }
    // Olvida las asociaciones (los objetos siguen en el Renderer, que los reutiliza por ruta).
    void clear();
    size_t size() const { return textures_.size() + models_.size() + fonts_.size(); }

private:
    Renderer* renderer_ = nullptr;
    const AssetDatabase* assets_ = nullptr;
    // También se guardan los fallos, para no repetir el error en cada fotograma.
    std::unordered_map<uint64_t, TextureHandle> textures_;
    std::unordered_map<uint64_t, int> models_;
    std::unordered_map<uint64_t, int> fonts_;
    std::unordered_map<uint64_t, std::unique_ptr<MeshRenderer>> materials_; // nulo = no existe o no es válido
    FileSource* files_ = nullptr;
    // Carga asíncrona
    struct Decoded {
        std::atomic<bool> done{false};
        DecodedImage image;
    };
    struct Pending {
        uint64_t ref;
        std::string path;
        TextureOptions options;
        std::shared_ptr<Decoded> result; // compartido con el trabajo: sigue vivo aunque se limpie la caché
    };
    JobSystem* jobs_ = nullptr;
    JobCounter inFlight_;
    std::vector<Pending> pending_;
    bool texturePath(AssetRef ref, std::string& path, TextureOptions& opt) const;
};

} // namespace titan
