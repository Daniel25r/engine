#pragma once
// Caché de recursos de GPU por id de recurso (AssetRef → textura, modelo o fuente del Renderer).
// La comparten el editor y el modo Jugar: al pulsar Jugar no se vuelve a leer ni a subir nada que el editor ya
// tenga cargado, y cada textura está una sola vez en la memoria de la GPU.
#include <cstdint>
#include <memory>
#include <unordered_map>

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
    // Las rutas de los recursos son relativas a "assets/" en el FileSource del Renderer.
    TextureHandle texture(AssetRef ref); // kWhiteTexture si no existe o no se puede leer
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
};

} // namespace titan
