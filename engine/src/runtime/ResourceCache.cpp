#include "titan/runtime/ResourceCache.h"

#include <chrono>

#include "titan/core/FileSource.h"
#include "titan/core/Log.h"
#include "titan/core/Serialization.h"

namespace titan {

bool ResourceCache::texturePath(AssetRef ref, std::string& path, TextureOptions& opt) const {
    const AssetRecord* r = assets_ ? assets_->find(ref) : nullptr;
    if (!r) return false;
    opt.nearest = r->importSettings.value("filter", "linear") == "nearest";
    opt.mipmaps = r->importSettings.value("mipmaps", true);
    path = "assets/" + r->path;
    return true;
}

void ResourceCache::setJobs(JobSystem* jobs) {
    if (jobs_ && jobs_ != jobs) waitPending();
    jobs_ = jobs;
}

void ResourceCache::requestTexture(AssetRef ref) {
    if (ref.isNull() || !renderer_ || !assets_ || textures_.count(ref.value)) return;
    for (const Pending& p : pending_)
        if (p.ref == ref.value) return;
    Pending p;
    p.ref = ref.value;
    if (!texturePath(ref, p.path, p.options)) {
        TITAN_ERROR("render", "Textura %s no encontrada", ref.toString().c_str());
        textures_[ref.value] = kWhiteTexture;
        return;
    }
    if (TextureHandle h = renderer_->findTexture(p.path, p.options)) { // ya en la GPU (otra referencia, el editor)
        textures_[ref.value] = h;
        return;
    }
    if (!jobs_ || !files_) { // sin hilos: en el acto
        textures_[ref.value] = renderer_->loadTexture(p.path, p.options);
        return;
    }
    p.result = std::make_shared<Decoded>();
    // El trabajo solo lee el archivo y decodifica (nada de OpenGL ni del ECS). Copia lo que necesita.
    jobs_->submit([files = files_, path = p.path, out = p.result] {
        decodeImage(*files, path, out->image);
        out->done.store(true, std::memory_order_release);
    }, &inFlight_, JobPriority::Low);
    pending_.push_back(std::move(p));
}

int ResourceCache::pump(double budgetMs) {
    if (pending_.empty()) return 0;
    const auto start = std::chrono::steady_clock::now();
    int uploaded = 0;
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        if (!p.result->done.load(std::memory_order_acquire)) {
            ++i;
            continue;
        }
        if (uploaded > 0 &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > budgetMs)
            break; // el resto, en el siguiente fotograma
        TextureHandle h = kWhiteTexture;
        if (p.result->image.rgba.empty())
            TITAN_ERROR("render", "Textura '%s': %s", p.path.c_str(), p.result->image.error.c_str());
        else
            h = renderer_->addDecodedTexture(p.path, p.options, p.result->image);
        textures_[p.ref] = h;
        pending_.erase(pending_.begin() + static_cast<long>(i));
        ++uploaded;
    }
    return uploaded;
}

void ResourceCache::waitPending() {
    if (jobs_) jobs_->wait(inFlight_);
}

TextureHandle ResourceCache::texture(AssetRef ref) {
    if (ref.isNull() || !renderer_ || !assets_) return kWhiteTexture;
    auto it = textures_.find(ref.value);
    if (it != textures_.end()) return it->second;
    requestTexture(ref); // síncrona sin JobSystem; si no, queda pedida
    it = textures_.find(ref.value);
    return it != textures_.end() ? it->second : kWhiteTexture;
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
    waitPending();
    pending_.clear();
    materials_.clear();
    textures_.clear();
    models_.clear();
    fonts_.clear();
}

} // namespace titan
