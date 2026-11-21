#include "titan/runtime/Runtime.h"
#include "titan/runtime/UIRenderer.h"

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

bool Runtime::initSystems(FileSource& files) {
    files_ = &files;
    if (!renderer_.init(files)) return false;
    audio_.init(files, true); // sin tarjeta de sonido el juego sigue funcionando en silencio
    scripts_ = std::make_unique<ScriptSystem>(*this, registry_, [this](AssetRef id, std::string& name, std::string& src) {
        const AssetRecord* r = assets_.find(id);
        if (!r || r->type != AssetType::Script) return false;
        name = r->path;
        return files_->readText("assets/" + r->path, src);
    });
    started_ = true;
    last_ = Clock::now();
    return true;
}

bool Runtime::start(FileSource& files) {
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
    if (!initSystems(files)) return false;
    if (!loadScene(startScene)) {
        TITAN_ERROR("runtime", "No se pudo cargar la escena inicial");
        shutdown();
        return false;
    }
    TITAN_INFO("runtime", "%s %s ejecutando '%s'", kEngineName, kEngineVersion, projectName_.c_str());
    return true;
}

bool Runtime::startEmbedded(FileSource& projectRoot, const AssetDatabase& assets, const std::string& sceneText,
                            const std::string& sceneName) {
    assets_ = assets;
    if (!initSystems(projectRoot)) return false;
    if (!activateScene(sceneText, sceneName)) {
        shutdown();
        return false;
    }
    return true;
}

bool Runtime::loadScene(UUID id) {
    const AssetRecord* r = assets_.find(id);
    std::string text;
    if (!r || r->type != AssetType::Scene || !files_->readText("assets/" + r->path, text)) {
        TITAN_ERROR("runtime", "No existe la escena %s", id.toString().c_str());
        return false;
    }
    return activateScene(text, r->path);
}

bool Runtime::activateScene(const std::string& text, const std::string& name) {
    auto next = std::make_unique<Scene>(name, &events_);
    const LoadReport report = deserializeScene(text, *next, registry_);
    for (auto& w : report.warnings) TITAN_WARN("runtime", "%s: %s", name.c_str(), w.c_str());
    for (auto& e : report.errors) TITAN_ERROR("runtime", "%s: %s", name.c_str(), e.c_str());
    if (!report.ok()) return false;

    if (scene_) {
        scripts_->detach();
        events_.emit(SceneUnloadedEvent{scene_.get()});
        scene_.reset();
    }
    scene_ = std::move(next);
    warnedNoCamera_ = false;
    TransformSystem::update(*scene_);
    physics_.build(*scene_);
    physicsDestroySub_ = events_.subscribe<EntityDestroyedEvent>([this](const EntityDestroyedEvent& ev) {
        if (ev.scene != scene_.get()) return;
        physics_.removeEntity(ev.entity);
        audio_.stopOwner(ev.entity);
    });
    audio_.stopAll();
    scene_->world().each<AudioSource>([&](Entity e, AudioSource& a) {
        if (a.playOnStart && scene_->isActiveInHierarchy(e)) scriptPlayEntitySound(e);
    });
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

bool Runtime::scriptPlaySound(const std::string& path, Entity owner, bool spatial, const Vec3& at, float volume) {
    std::string file = path;
    if (!assets_.findByPath(path)) {
        // Se admite también el nombre sin carpeta ni extensión: "disparo".
        for (const AssetRecord* r : assets_.all()) {
            if (r->type != AssetType::Audio) continue;
            std::string base = r->path.substr(r->path.find_last_of('/') + 1);
            if (base == path || base.substr(0, base.rfind('.')) == path) { file = r->path; break; }
        }
    }
    PlayParams p;
    p.volume = volume;
    p.owner = owner;
    p.spatial = spatial;
    p.position = owner.isNull() ? at : scene_->computeWorldMatrix(owner).translation();
    return audio_.play("assets/" + file, p) != 0;
}

void Runtime::scriptPlayEntitySound(Entity e) {
    const AudioSource* a = scene_ ? scene_->world().get<AudioSource>(e) : nullptr;
    const AssetRecord* r = a ? assets_.find(a->clip) : nullptr;
    if (!r) return;
    PlayParams p;
    p.volume = a->volume;
    p.pitch = a->pitch;
    p.loop = a->loop;
    p.spatial = a->spatial;
    p.minDistance = a->minDistance;
    p.maxDistance = a->maxDistance;
    p.owner = e;
    p.position = scene_->computeWorldMatrix(e).translation();
    audio_.play("assets/" + r->path, p);
}

Entity Runtime::scriptInstantiate(const std::string& name, const Vec3& position) {
    for (const AssetRecord* r : assets_.all()) {
        if (r->type != AssetType::Prefab) continue;
        const size_t slash = r->path.find_last_of('/');
        std::string base = r->path.substr(slash == std::string::npos ? 0 : slash + 1);
        base = base.substr(0, base.rfind('.'));
        if (r->path != name && base != name) continue;
        std::string text;
        if (!files_->readText("assets/" + r->path, text)) break;
        const Entity e = instantiatePrefab(text, *scene_, {}, registry_);
        if (e.isNull()) break;
        scene_->transform(e).setPosition(position);
        TransformSystem::update(*scene_);
        physics_.addSubtree(*scene_, e);
        scripts_->attachSubtree(e);
        return e;
    }
    TITAN_ERROR("runtime", "Scene.instantiate: no existe el prefab '%s'", name.c_str());
    return {};
}

void Runtime::applyPendingSceneLoad() {
    if (pendingScene_.isNull()) return;
    const UUID id = pendingScene_;
    pendingScene_ = {};
    loadScene(id);
}

void Runtime::frame(const Viewport& viewport) {
    if (!started_ || !scene_) return;
    const auto frameStart = Clock::now();
    screen_ = {static_cast<float>(std::max(viewport.width, 1)), static_cast<float>(std::max(viewport.height, 1))};

    float dt = std::chrono::duration<float>(frameStart - last_).count();
    last_ = frameStart;
    dt = std::min(dt, 0.25f);
    if (paused) dt = 0;
    const bool simulate = !paused;
    delta_ = dt;

    // 0. Interfaz: botones y joysticks leen los toques antes que los scripts.
    ui_.layout(*scene_, screen_);
    clicked_.clear();
    if (simulate) {
        ui_.update(*scene_, input_, &clicked_);
        for (Entity e : clicked_) scripts_->dispatchEvent(e, "onClick");
        scene_->flushDestroyed();
    }

    // 1. Paso fijo con límite: tras un parón no se intenta "recuperar" todo el tiempo perdido.
    auto t = Clock::now();
    accumulator_ += dt;
    int steps = 0;
    while (simulate && accumulator_ >= kFixedStep && steps < kMaxFixedStepsPerFrame) {
        scripts_->fixedUpdate(kFixedStep);
        physics_.step(*scene_, kFixedStep);
        for (const ContactEvent& ev : physics_.takeEvents()) {
            scripts_->dispatchContact(ev.a, ev.b, ev.begin, ev.trigger);
            scripts_->dispatchContact(ev.b, ev.a, ev.begin, ev.trigger);
        }
        scene_->flushDestroyed();
        accumulator_ -= kFixedStep;
        ++steps;
    }
    if (steps == kMaxFixedStepsPerFrame) accumulator_ = 0;

    // 2-3. Lógica variable.
    time_ += dt;
    if (!paused) {
        scripts_->update(dt);
        scripts_->lateUpdate(dt);
    }
    scene_->flushDestroyed();
    profile_.scriptMs = msSince(t);

    // Animación: avanza el tiempo de cada Animator que esté reproduciendo.
    if (!paused)
        scene_->world().each<Animator>([&](Entity, Animator& a) { advanceAnimator(a, dt); });

    // Audio: el oyente es la cámara activa; las voces 3D siguen a su entidad.
    {
        Entity cam;
        SceneView v;
        if (SceneRenderer::cameraView(*scene_, 1, v, &cam)) {
            const Mat4 w = scene_->computeWorldMatrix(cam);
            audio_.setListener(w.translation(), w.transformDirection({0, 0, -1}).normalized(), {0, 1, 0});
        }
        audio_.update([this](Entity e, Vec3& out) {
            if (!scene_->isValid(e)) return false;
            out = scene_->computeWorldMatrix(e).translation();
            return true;
        });
    }

    // 4. Transformaciones.
    t = Clock::now();
    profile_.transformsUpdated = TransformSystem::update(*scene_);
    profile_.transformMs = msSince(t);

    // 5. Render.
    t = Clock::now();
    render(viewport);
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
        TITAN_ERROR("render", "Textura %s no encontrada", ref.toString().c_str());
    }
    textures_[ref.value] = h; // también se cachean los fallos para no repetir el error
    return h;
}

int Runtime::fontFor(AssetRef ref) {
    auto it = fonts_.find(ref.value);
    if (it != fonts_.end()) return it->second;
    const AssetRecord* r = assets_.find(ref);
    const int f = r ? renderer_.loadFont("assets/" + r->path) : -1;
    fonts_[ref.value] = f;
    return f;
}

int Runtime::modelFor(AssetRef ref) {
    auto it = models_.find(ref.value);
    if (it != models_.end()) return it->second;
    const AssetRecord* r = assets_.find(ref);
    const int m = r ? renderer_.loadModel("assets/" + r->path) : -1;
    models_[ref.value] = m;
    return m;
}

void Runtime::render(const Viewport& viewport) {
    const float aspect = screen_.x / screen_.y;
    SceneView view;
    if (!SceneRenderer::cameraView(*scene_, aspect, view)) {
        if (!warnedNoCamera_) TITAN_WARN("render", "La escena no tiene ninguna cámara activa; se usa una por defecto");
        warnedNoCamera_ = true;
        Camera def;
        view.view = Mat4::translate({0, 0, 10}).inverted();
        view.projection = def.projectionMatrix(aspect);
    }
    viewProj_ = view.projection * view.view;
    perspective_ = view.perspective;
    sceneRenderer_.render(*scene_, renderer_, viewport, view, [this](AssetRef r) { return textureFor(r); },
                          [this](AssetRef r) { return modelFor(r); });

    ui_.layout(*scene_, screen_); // los scripts pueden haber cambiado textos, tamaños o posiciones
    drawUI(renderer_, *scene_, ui_, [this](AssetRef r) { return textureFor(r); }, [this](AssetRef r) { return fontFor(r); });

    for (const auto& t : scriptTexts_) renderer_.drawText({t.x, t.y}, t.size, t.text, t.color);
    scriptTexts_.clear();
    if (showStats) {
        const RenderStats& rs = renderer_.stats();
        char text[512];
        std::snprintf(text, sizeof(text),
                      "%.0f FPS  %.2f ms (scripts %.2f, transform %.2f, render %.2f)\n"
                      "%zu entidades  %d draw calls  %d mallas  %d sprites  %d triangulos\n"
                      "%zu scripts  Lua %.0f KB  %zu errores  %zu cuerpos",
                      fps_, profile_.frameMs, profile_.scriptMs, profile_.transformMs, profile_.renderMs,
                      scene_->entityCount(), rs.drawCalls, rs.meshes, rs.sprites, rs.triangles,
                      scripts_->instanceCount(), scripts_->memoryUsed() / 1024.0, scripts_->errors().size(), physics_.bodyCount());
        // Arriba a la derecha: a la izquierda y abajo suele ir la interfaz del propio juego.
        const float h = std::max(12.0f, std::min(screen_.x, screen_.y) * 0.022f);
        const float width = renderer_.measureText(0, h, text).x;
        renderer_.drawText({std::max(h * 0.5f, screen_.x - width - h * 0.6f), h * 0.6f}, h, text, Color{1, 1, 1, 0.75f});
    }
    renderer_.endFrame();
}

void Runtime::scriptDrawText(float x, float y, float size, const std::string& text, Color color) {
    if (scriptTexts_.size() < 256) scriptTexts_.push_back({x, y, size, text, color});
}

bool Runtime::scriptScreenToWorld(Vec2 px, Vec3& world) const {
    const float nx = px.x / screen_.x * 2.0f - 1.0f, ny = 1.0f - px.y / screen_.y * 2.0f;
    Mat4 inv;
    if (!viewProj_.inverse(inv)) return false;
    auto unproject = [&](float z) {
        const float* m = inv.m;
        const float w = m[3] * nx + m[7] * ny + m[11] * z + m[15];
        return Vec3{(m[0] * nx + m[4] * ny + m[8] * z + m[12]) / w, (m[1] * nx + m[5] * ny + m[9] * z + m[13]) / w,
                    (m[2] * nx + m[6] * ny + m[10] * z + m[14]) / w};
    };
    const Vec3 a = unproject(-1.0f), b = unproject(1.0f);
    // Plano de juego: Z = 0 con cámara ortográfica (2D), suelo Y = 0 con cámara en perspectiva (3D).
    const float da = perspective_ ? a.y : a.z, db = perspective_ ? b.y : b.z;
    if (std::fabs(db - da) < 1e-8f) {
        world = a;
        return true;
    }
    const float k = -da / (db - da);
    if (k < 0) return false; // el rayo no llega al plano (mira hacia arriba)
    world = a + (b - a) * k;
    return true;
}

void Runtime::pause() {
    input_.cancelAll();
    audio_.setPaused(true);
    events_.emit(AppPausedEvent{});
}

void Runtime::resume() {
    last_ = Clock::now();
    accumulator_ = 0;
    audio_.setPaused(false);
    events_.emit(AppResumedEvent{});
}

void Runtime::shutdown() {
    if (!started_) return;
    started_ = false;
    if (scripts_) scripts_->detach();
    physicsDestroySub_.reset();
    physics_.clear();
    audio_.shutdown();
    if (scene_) {
        events_.emit(SceneUnloadedEvent{scene_.get()});
        scene_.reset();
    }
    scripts_.reset();
    textures_.clear();
    models_.clear();
    fonts_.clear();
    renderer_.shutdown();
    TITAN_INFO("runtime", "Runtime detenido");
}

} // namespace titan
