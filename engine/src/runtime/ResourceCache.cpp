#include "titan/runtime/ResourceCache.h"

#include "titan/core/Log.h"

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

void ResourceCache::clear() {
    textures_.clear();
    models_.clear();
    fonts_.clear();
}

} // namespace titan
