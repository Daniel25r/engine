#include "titan/runtime/ResourceCache.h"

#include "titan/core/FileSource.h"
#include "titan/core/Log.h"
#include "titan/core/Serialization.h"

namespace titan {

TextureHandle ResourceCache::texture(AssetRef ref) {
    if (ref.isNull() || !renderer_ || !assets_) return kWhiteTexture;
    auto it = textures_.find(ref.value);
    if (it != textures_.end()) return it->second;
    TextureHandle h = kWhiteTexture;
    if (const AssetRecord* r = assets_->find(ref)) {
        TextureOptions opt;
        opt.nearest = r->importSettings.value("filter", "linear") == "nearest";
        opt.mipmaps = r->importSettings.value("mipmaps", true);
        h = renderer_->loadTexture("assets/" + r->path, opt);
    } else {
        TITAN_ERROR("render", "Textura %s no encontrada", ref.toString().c_str());
    }
    textures_[ref.value] = h;
    return h;
}

int ResourceCache::model(AssetRef ref) {
    if (ref.isNull() || !renderer_ || !assets_) return -1;
    auto it = models_.find(ref.value);
    if (it != models_.end()) return it->second;
    const AssetRecord* r = assets_->find(ref);
    const int m = r ? renderer_->loadModel("assets/" + r->path) : -1;
    models_[ref.value] = m;
    return m;
}

int ResourceCache::font(AssetRef ref) {
    if (ref.isNull() || !renderer_ || !assets_) return -1;
    auto it = fonts_.find(ref.value);
    if (it != fonts_.end()) return it->second;
    const AssetRecord* r = assets_->find(ref);
    const int f = r ? renderer_->loadFont("assets/" + r->path) : -1;
    fonts_[ref.value] = f;
    return f;
}

const MeshRenderer* ResourceCache::material(AssetRef ref) {
    if (ref.isNull() || !assets_ || !files_) return nullptr;
    auto it = materials_.find(ref.value);
    if (it != materials_.end()) return it->second.get();
    std::unique_ptr<MeshRenderer> m;
    const AssetRecord* r = assets_->find(ref);
    std::string text;
    if (r && files_->readText("assets/" + r->path, text)) {
        m = std::make_unique<MeshRenderer>();
        const LoadReport rep = parseMaterial(text, *m);
        for (const auto& e : rep.errors) TITAN_ERROR("render", "Material %s: %s", r->path.c_str(), e.c_str());
        for (const auto& w : rep.warnings) TITAN_WARN("render", "Material %s: %s", r->path.c_str(), w.c_str());
        if (!rep.ok()) m.reset();
    } else {
        TITAN_ERROR("render", "Material %s no encontrado", ref.toString().c_str());
    }
    const MeshRenderer* out = m.get();
    materials_[ref.value] = std::move(m);
    return out;
}

void ResourceCache::clear() {
    materials_.clear();
    textures_.clear();
    models_.clear();
    fonts_.clear();
}

} // namespace titan
