#include "titan/runtime/Runtime.h"

#include <algorithm>
#include <cstdio>

#include "titan/core/Log.h"
#include "titan/core/Version.h"

namespace titan {

namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

Runtime::Runtime() : registry_(ComponentRegistry::createDefault()) {}

Runtime::~Runtime() { shutdown(); }

bool Runtime::start(FileSource& files) {
    files_ = &files;
    std::string text, error;
    if (!files.readText("project.titan", text)) {
        TITAN_ERROR("runtime", "El paquete no contiene project.titan");
        return false;
    }
    ProjectDimension dim;
    UUID startScene;
    if (!Project::parseProjectFile(text, projectName_, dim, startScene, &error)) {
        TITAN_ERROR("runtime", "%s", error.c_str());
        return false;
    }
    if (!files.readText("asset_index.json", text) || !assets_.loadIndex(text, &error)) {
        TITAN_ERROR("runtime", "Índice de recursos no válido: %s", error.c_str());
        return false;
    }
    if (!renderer_.init(files)) return false;

    scripts_ = std::make_unique<ScriptSystem>(*this, registry_, [this](AssetRef id, std::string& name, std::string& src) {
        const AssetRecord* r = assets_.find(id);
        if (!r || r->type != AssetType::Script) return false;
        name = r->path;
        return files_->readText("assets/" + r->path, src);
    });

    started_ = true;
    if (!loadScene(startScene)) {
        TITAN_ERROR("runtime", "No se pudo cargar la escena inicial");
        shutdown();
        return false;
    }
    last_ = Clock::now();
    TITAN_INFO("runtime", "%s %s ejecutando '%s'", kEngineName, kEngineVersion, projectName_.c_str());
    return true;
}

bool Runtime::loadScene(UUID id) {
    const AssetRecord* r = assets_.find(id);
    std::string text;
    if (!r || r->type != AssetType::Scene || !files_->readText("assets/" + r->path, text)) {
        TITAN_ERROR("runtime", "No existe la escena %s en el paquete", id.toString().c_str());
        return false;
    }
    auto next = std::make_unique<Scene>(r->path, &events_);
    const LoadReport report = deserializeScene(text, *next, registry_);
    for (auto& w : report.warnings) TITAN_WARN("runtime", "%s: %s", r->path.c_str(), w.c_str());
    for (auto& e : report.errors) TITAN_ERROR("runtime", "%s: %s", r->path.c_str(), e.c_str());
    if (!report.ok()) return false;

    if (scene_) {
        scripts_->detach();
        events_.emit(SceneUnloadedEvent{scene_.get()});
        scene_.reset();
    }
    scene_ = std::move(next);
    warnedNoCamera_ = false;
    TransformSystem::update(*scene_);
    events_.emit(SceneLoadedEvent{scene_.get()});
    TITAN_INFO("runtime", "Escena '%s' cargada (%zu entidades)", scene_->name().c_str(), scene_->entityCount());
    scripts_->attach(*scene_, events_);
    scene_->flushDestroyed();
    return true;
}

bool Runtime::scriptLoadScene(const std::string& nameOrPath) {
    for (const AssetRecord* r : assets_.all()) {
        if (r->type != AssetType::Scene) continue;
        const size_t slash = r->path.find_last_of('/');
        std::string base = r->path.substr(slash == std::string::npos ? 0 : slash + 1);
        base = base.substr(0, base.rfind('.'));
        if (r->path == nameOrPath || base == nameOrPath) {
            pendingScene_ = r->id; // se aplica al final del fotograma, nunca a mitad de un script
            return true;
        }
    }
    TITAN_ERROR("runtime", "Scene.load: no existe la escena '%s'", nameOrPath.c_str());
    return false;
}

void Runtime::applyPendingSceneLoad() {
    if (pendingScene_.isNull()) return;
    const UUID id = pendingScene_;
    pendingScene_ = {};
    loadScene(id);
}

void Runtime::frame(int width, int height) {
    if (!started_ || !scene_) return;
    const auto frameStart = Clock::now();
    screen_ = {static_cast<float>(std::max(width, 1)), static_cast<float>(std::max(height, 1))};

    float dt = std::chrono::duration<float>(frameStart - last_).count();
    last_ = frameStart;
    dt = std::min(dt, 0.25f);
    delta_ = dt;

    // 1. Paso fijo con límite: tras un parón no se intenta "recuperar" todo el tiempo perdido.
    auto t = Clock::now();
    accumulator_ += dt;
    int steps = 0;
    while (accumulator_ >= kFixedStep && steps < kMaxFixedStepsPerFrame) {
        scripts_->fixedUpdate(kFixedStep);
        scene_->flushDestroyed();
        accumulator_ -= kFixedStep;
        ++steps;
    }
    if (steps == kMaxFixedStepsPerFrame) accumulator_ = 0;

    // 2-3. Lógica variable.
    time_ += dt;
    scripts_->update(dt);
    scripts_->lateUpdate(dt);
    scene_->flushDestroyed();
    profile_.scriptMs = msSince(t);

    // 4. Transformaciones.
    t = Clock::now();
    profile_.transformsUpdated = TransformSystem::update(*scene_);
    profile_.transformMs = msSince(t);

    // 5. Render.
    t = Clock::now();
    render();
    profile_.renderMs = msSince(t);

    input_.endFrame();
    ++frameIndex_;
    profile_.frameMs = msSince(frameStart);
    fpsFrames_++;
    fpsTimer_ += dt;
    if (fpsTimer_ >= 0.5f) {
        fps_ = fpsFrames_ / fpsTimer_;
        fpsFrames_ = 0;
        fpsTimer_ = 0;
    }

    // 6. Cambio de escena.
    applyPendingSceneLoad();
}

Entity Runtime::findCamera() const {
    Entity best;
    int bestPriority = 0;
    scene_->world().each<Camera>([&](Entity e, Camera& c) {
        if (!scene_->isActiveInHierarchy(e)) return;
        if (best.isNull() || c.priority > bestPriority || (c.priority == bestPriority && e.index < best.index)) {
            best = e;
            bestPriority = c.priority;
        }
    });
    return best;
}

TextureHandle Runtime::textureFor(AssetRef ref) {
    if (ref.isNull()) return kWhiteTexture;
    auto it = textures_.find(ref.value);
    if (it != textures_.end()) return it->second;
    TextureHandle h = kWhiteTexture;
    if (const AssetRecord* r = assets_.find(ref)) {
        TextureOptions opt;
        opt.nearest = r->importSettings.value("filter", "linear") == "nearest";
        opt.mipmaps = r->importSettings.value("mipmaps", true);
        h = renderer_.loadTexture("assets/" + r->path, opt);
    } else {
        TITAN_ERROR("render", "Textura %s no encontrada en el paquete", ref.toString().c_str());
    }
    textures_[ref.value] = h; // también se cachean los fallos para no repetir el error
    return h;
}

void Runtime::render() {
    const float aspect = screen_.x / screen_.y;
    const Entity camEntity = findCamera();
    Camera cam;
    Mat4 camWorld = Mat4::translate({0, 0, 10});
    if (!camEntity.isNull()) {
        cam = *scene_->world().get<Camera>(camEntity);
        camWorld = scene_->transform(camEntity).worldMatrix();
    } else if (!warnedNoCamera_) {
        TITAN_WARN("render", "La escena no tiene ninguna cámara activa; se usa una por defecto");
        warnedNoCamera_ = true;
    }
    viewProj_ = cam.projectionMatrix(aspect) * camWorld.inverted();

    renderer_.beginFrame(static_cast<int>(screen_.x), static_cast<int>(screen_.y), cam.clearColor);
    renderer_.setViewProjection(viewProj_);

    drawList_.clear();
    scene_->world().each<SpriteRenderer, Transform>([&](Entity e, SpriteRenderer& sp, Transform& tr) {
        if (!sp.visible || !scene_->isActiveInHierarchy(e)) return;
        drawList_.push_back({sp.sortingOrder, textureFor(sp.texture), &tr.worldMatrix(), &sp});
    });
    // Orden de dibujo: sortingOrder; dentro del mismo orden, por textura para agrupar lotes.
    std::stable_sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.order != b.order ? a.order < b.order : a.texture < b.texture;
    });
    for (const DrawItem& d : drawList_)
        renderer_.drawSprite(d.texture, *d.world, d.sprite->size, d.sprite->pivot, d.sprite->uv, d.sprite->color);

    if (showStats) {
        const RenderStats& rs = renderer_.stats();
        char text[512];
        std::snprintf(text, sizeof(text),
                      "%.0f FPS  %.2f ms (scripts %.2f, transform %.2f, render %.2f)\n"
                      "%zu entidades  %zu componentes  %d draw calls  %d sprites\n"
                      "%zu scripts  Lua %.0f KB  %zu errores",
                      fps_, profile_.frameMs, profile_.scriptMs, profile_.transformMs, profile_.renderMs,
                      scene_->entityCount(), scene_->world().componentCount(), rs.drawCalls, rs.quads,
                      scripts_->instanceCount(), scripts_->memoryUsed() / 1024.0, scripts_->errors().size());
        const float h = std::max(14.0f, std::min(screen_.x, screen_.y) * 0.024f);
        renderer_.drawText({h * 0.8f, h * 0.8f}, h, text, Color{1, 1, 1, 0.85f});
    }
    renderer_.endFrame();
}

bool Runtime::scriptScreenToWorld(Vec2 px, Vec3& world) const {
    const Vec3 ndc{px.x / screen_.x * 2.0f - 1.0f, 1.0f - px.y / screen_.y * 2.0f, 0.0f};
    Mat4 inv;
    if (!viewProj_.inverse(inv)) return false;
    // Punto sobre el plano Z = 0 (el plano de juego en 2D): se interseca el rayo near-far.
    auto unproject = [&](float z) {
        const float x = ndc.x, y = ndc.y;
        const float* m = inv.m;
        const float w = m[3] * x + m[7] * y + m[11] * z + m[15];
        return Vec3{(m[0] * x + m[4] * y + m[8] * z + m[12]) / w, (m[1] * x + m[5] * y + m[9] * z + m[13]) / w,
                    (m[2] * x + m[6] * y + m[10] * z + m[14]) / w};
    };
    const Vec3 a = unproject(-1.0f), b = unproject(1.0f);
    const float dz = b.z - a.z;
    if (std::fabs(dz) < 1e-8f) {
        world = a;
        return true;
    }
    const float k = -a.z / dz;
    world = a + (b - a) * k;
    return true;
}

void Runtime::pause() {
    input_.cancelAll();
    events_.emit(AppPausedEvent{});
}

void Runtime::resume() {
    last_ = Clock::now();
    accumulator_ = 0;
    events_.emit(AppResumedEvent{});
}

void Runtime::shutdown() {
    if (!started_) return;
    started_ = false;
    if (scripts_) scripts_->detach();
    if (scene_) {
        events_.emit(SceneUnloadedEvent{scene_.get()});
        scene_.reset();
    }
    scripts_.reset();
    textures_.clear();
    renderer_.shutdown();
    TITAN_INFO("runtime", "Runtime detenido");
}

} // namespace titan
