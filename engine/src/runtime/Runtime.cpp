#include "titan/runtime/Runtime.h"
#include "titan/runtime/UIRenderer.h"
#include "titan/core/AnimationGraph.h"
#include "titan/core/Geometry.h"
#include "titan/core/Particles.h"

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
    actions_.addDefaults();
    // Colliders de malla: triángulos del modelo glTF (o de la primitiva) del MeshRenderer de la entidad.
    physics_.setMeshProvider([this](Entity e, std::vector<Vec3>& verts, std::vector<uint32_t>& idx) {
        const MeshRenderer* mr = scene_ ? scene_->world().get<MeshRenderer>(e) : nullptr;
        if (!mr) return false;
        if (!mr->model.isNull()) return renderer_->modelTriangles(cache_->model(mr->model), verts, idx);
        const MeshData d = buildPrimitive(mr->mesh);
        verts.clear();
        for (const MeshVertex& v : d.vertices) verts.push_back(v.position);
        idx.assign(d.indices.begin(), d.indices.end());
        return true;
    });
    if (!usesSharedRenderer()) {
        if (!renderer_->init(files)) return false;
        // Juego exportado: perfil según lo que se puede medir de este dispositivo (el juego puede cambiarlo).
        const DeviceCaps& c = renderer_->caps();
        TITAN_INFO("render", "GPU: %s (%s), %s, textura máx. %d, memoria %d MB", c.renderer.c_str(), c.vendor.c_str(),
                   c.version.c_str(), c.maxTextureSize, c.memoryMB);
        renderer_->setQuality(QualitySettings::preset(recommendedQuality(c)));
    }
    if (cache_ == &ownCache_) cache_->bind(renderer_, &assets_, &files); // la del editor ya apunta a su proyecto
    cache_->setJobs(parallel ? &jobs_ : nullptr); // texturas leídas y decodificadas en otros hilos
    audio_.init(files, true); // sin tarjeta de sonido el juego sigue funcionando en silencio
    loadBusLayout(files);
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

void Runtime::preloadTextures() {
    World& w = scene_->world();
    auto req = [this](AssetRef r) { cache_->requestTexture(r); };
    w.each<MeshRenderer>([&](Entity, MeshRenderer& m) {
        const MeshRenderer* mat = cache_->material(m.material);
        for (const MeshRenderer* src : {static_cast<const MeshRenderer*>(&m), mat}) {
            if (!src) continue;
            for (AssetRef r : {src->texture, src->normalMap, src->metallicRoughnessMap, src->emissionMap, src->occlusionMap}) req(r);
        }
    });
    w.each<SpriteRenderer>([&](Entity, SpriteRenderer& s) { req(s.texture); });
    w.each<UIImage>([&](Entity, UIImage& i) { req(i.texture); });
    w.each<ParticleSystem>([&](Entity, ParticleSystem& p) { req(p.texture); });
    w.each<Terrain>([&](Entity, Terrain& t) {
        for (AssetRef r : t.layerTextures) req(r);
    });
}

// Mezclador del proyecto: assets/audio_buses.json (opcional; sin él solo existe Master).
void Runtime::loadBusLayout(FileSource& files) {
    std::string text, error;
    if (!files.readText("assets/audio_buses.json", text)) return;
    AudioBusLayout layout;
    if (!AudioBusLayout::fromJson(text, layout, &error)) {
        TITAN_ERROR("audio", "audio_buses.json: %s; se usa solo Master", error.c_str());
        return;
    }
    audio_.setBusLayout(layout);
    TITAN_INFO("audio", "Mezclador: %zu buses", layout.buses.size());
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
                            const std::string& sceneName, Renderer* sharedRenderer, ResourceCache* sharedCache) {
    assets_ = assets;
    if (sharedRenderer) renderer_ = sharedRenderer;
    if (sharedRenderer && sharedCache) cache_ = sharedCache;
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
    preloadTextures(); // todas las texturas de la escena se piden ya, en paralelo (no al dibujarse por primera vez)
    audio_.stopAll();
    audio_.resetBusEffects(); // sin colas de reverberación o eco de la escena anterior
    scene_->world().each<AudioSource>([&](Entity e, AudioSource& a) {
        if (a.playOnStart && scene_->isActiveInHierarchy(e)) scriptPlayEntitySound(e);
    });
    navGrid_ = {};
    bool anyAgent = false;
    scene_->world().each<NavAgent>([&](Entity, NavAgent&) { anyAgent = true; });
    if (anyAgent) scriptBakeNav(nullptr); // hay enemigos o NPC que navegan: se hornea la rejilla al cargar
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

bool Runtime::scriptPlaySound(const std::string& path, Entity owner, bool spatial, const Vec3& at, float volume,
                              const std::string& bus) {
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
    p.bus = bus;
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
    p.bus = a->bus;
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
        actions_.update(input_, scene_.get()); // después de la UI: los botones y joysticks ya tienen su estado
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
    profile_.physicsMs = msSince(t);
    t = Clock::now();

    // 2-3. Lógica variable.
    time_ += dt;
    if (!paused) {
        scripts_->update(dt);
        scripts_->lateUpdate(dt);
        if (navGrid_.baked()) updateNavAgents(*scene_, navGrid_, dt); // los agentes deciden su velocidad
    }
    scene_->flushDestroyed();
    profile_.scriptMs = msSince(t);

    // Animación: avanza el tiempo de cada Animator que esté reproduciendo.
    if (!paused) {
        // Animación: máquinas de estados (con la duración real de cada clip) y eventos para los scripts.
        std::vector<std::pair<Entity, std::string>> animEvents;
        World& w = scene_->world();
        w.each<Animator>([&](Entity e, Animator& a) {
            const MeshRenderer* mr = w.get<MeshRenderer>(e);
            const int model = mr && !mr->model.isNull() ? cache_->model(mr->model) : -1;
            updateAnimator(a, dt, [&](const std::string& c) {
                return renderer_->modelAnimationDuration(model, renderer_->modelAnimationIndex(model, c));
            });
            for (const std::string& ev : a.machine.firedEvents) animEvents.emplace_back(e, ev);
        });
        for (const auto& [e, ev] : animEvents) scripts_->dispatchEvent(e, "onAnimationEvent", ev.c_str());
        TransformSystem::update(*scene_); // los emisores necesitan su posición actual
        const auto tp = Clock::now();
        updateParticles(*scene_, dt, parallel ? &jobs_ : nullptr);
        profile_.particlesMs = msSince(tp);
        cameras_.update(*scene_, input_, &actions_, &physics_, dt); // cámaras que siguen al jugador
        // Sombras de gota: un rayo hacia abajo desde cada una (sin chocar con su propia entidad).
        scene_->world().each<BlobShadow>([&](Entity e, BlobShadow& b) {
            const Vec3 from = scene_->computeWorldMatrix(e).translation() + Vec3{0, 0.1f, 0};
            RaycastHit hit;
            b.visible = scene_->isActiveInHierarchy(e) && physics_.raycast(from, {0, -1, 0}, b.maxHeight, hit, e);
            if (b.visible) {
                b.point = hit.point;
                b.normal = hit.normal.length() > 0.1f ? hit.normal.normalized() : Vec3{0, 1, 0};
                b.height = hit.distance;
            }
        });
    }

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
    cache_->pump(4.0); // texturas ya decodificadas en otros hilos: a la GPU, como mucho ~4 ms por fotograma
    sceneRenderer_.render(*scene_, *renderer_, viewport, view, [this](AssetRef r) { return cache_->texture(r); },
                          [this](AssetRef r) { return cache_->model(r); },
                          [this](AssetRef r) { return cache_->material(r); });

    ui_.layout(*scene_, screen_); // los scripts pueden haber cambiado textos, tamaños o posiciones
    drawUI(*renderer_, *scene_, ui_, [this](AssetRef r) { return cache_->texture(r); },
           [this](AssetRef r) { return cache_->font(r); });

    for (const auto& t : scriptTexts_) renderer_->drawText({t.x, t.y}, t.size, t.text, t.color);
    scriptTexts_.clear();
    if (showStats) {
        const RenderStats& rs = renderer_->stats();
        const CullStats& cs = sceneRenderer_.cullStats();
        char text[640];
        std::snprintf(text, sizeof(text),
                      "%.0f FPS  %.2f ms (scripts %.2f, fisica %.2f, particulas %.2f, transform %.2f, render %.2f)  texturas en carga %zu\n"
                      "%zu entidades  %d draw calls  %d mallas  %d sprites  %d triangulos\n"
                      "%zu scripts  Lua %.0f KB  %zu errores  %zu cuerpos\n"
                      "visibles: %d/%d mallas  %d/%d sombras  %d/%d sprites  %d esqueletos",
                      fps_, profile_.frameMs, profile_.scriptMs, profile_.physicsMs, profile_.particlesMs,
                      profile_.transformMs, profile_.renderMs, cache_->pendingTextures(),
                      scene_->entityCount(), rs.drawCalls, rs.meshes, rs.sprites, rs.triangles,
                      scripts_->instanceCount(), scripts_->memoryUsed() / 1024.0, scripts_->errors().size(), physics_.bodyCount(),
                      cs.meshesVisible, cs.meshes, cs.shadowCastersVisible, cs.shadowCasters, cs.spritesVisible, cs.sprites,
                      cs.posesComputed);
        // Arriba a la derecha: a la izquierda y abajo suele ir la interfaz del propio juego.
        const float h = std::max(12.0f, std::min(screen_.x, screen_.y) * 0.022f);
        const float width = renderer_->measureText(0, h, text).x;
        renderer_->drawText({std::max(h * 0.5f, screen_.x - width - h * 0.6f), h * 0.6f}, h, text, Color{1, 1, 1, 0.75f});
    }
    renderer_->endFrame();
}

int Runtime::scriptBakeNav(const NavSettings* settings) {
    if (!scene_) return 0;
    const auto t0 = Clock::now();
    const NavSettings s = settings ? *settings : navSettingsForScene(*scene_);
    // Lo que se mueve no es suelo ni obstáculo fijo: personajes, agentes y cuerpos dinámicos.
    std::vector<Entity> ignore;
    World& w = scene_->world();
    w.each<CharacterController>([&](Entity e, CharacterController&) { ignore.push_back(e); });
    w.each<NavAgent>([&](Entity e, NavAgent&) {
        if (std::find(ignore.begin(), ignore.end(), e) == ignore.end()) ignore.push_back(e);
    });
    w.each<Rigidbody>([&](Entity e, Rigidbody& rb) {
        if (rb.type == Rigidbody::Type::Dynamic) ignore.push_back(e);
    });
    navGrid_.bake(physics_, s, ignore);
    const int cells = navGrid_.walkableCells();
    TITAN_INFO("navigation", "Rejilla de navegación: %dx%d celdas de %.2f m, %d caminables (%.0f ms)", navGrid_.width(),
               navGrid_.depth(), s.cellSize, cells, msSince(t0));
    return cells;
}

bool Runtime::scriptCameraBasis(Vec3& forward, Vec3& right) const {
    if (!scene_) return false;
    Entity cam;
    SceneView v;
    if (!SceneRenderer::cameraView(*scene_, 1, v, &cam)) return false;
    const Mat4 w = scene_->computeWorldMatrix(cam);
    Vec3 f = w.transformDirection({0, 0, -1});
    f.y = 0;
    if (f.length() < 1e-4f) f = w.transformDirection({0, 1, 0}) * -1.0f; // cámara mirando recto abajo
    f.y = 0;
    forward = f.normalized();
    right = Vec3{-forward.z, 0, forward.x};
    return true;
}

ScriptHost::FrameStats Runtime::scriptFrameStats() const {
    FrameStats f;
    const RenderStats& rs = renderer_->stats();
    const CullStats& cs = sceneRenderer_.cullStats();
    f.fps = fps_;
    f.frameMs = static_cast<float>(profile_.frameMs);
    f.scriptMs = static_cast<float>(profile_.scriptMs);
    f.renderMs = static_cast<float>(profile_.renderMs);
    f.drawCalls = rs.drawCalls;
    f.triangles = rs.triangles;
    f.meshes = cs.meshes;
    f.meshesVisible = cs.meshesVisible;
    f.particles = cs.particlesDrawn;
    f.bodies = static_cast<int>(physics_.bodyCount());
    f.entities = scene_ ? static_cast<int>(scene_->entityCount()) : 0;
    return f;
}

bool Runtime::scriptSetQuality(const std::string& name) {
    QualityLevel l;
    if (!parseQuality(name, l)) {
        TITAN_ERROR("runtime", "Quality.set: perfil desconocido '%s' (muy bajo, bajo, medio, alto, muy alto)", name.c_str());
        return false;
    }
    renderer_->setQuality(QualitySettings::preset(l));
    return true;
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
    // Android puede cerrar la app en segundo plano: la partida pendiente se escribe ya.
    if (scripts_ && scripts_->saveDirty()) scripts_->writeSave();
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
    if (scripts_ && scripts_->saveDirty()) scripts_->writeSave();
    if (scripts_) scripts_->detach();
    physicsDestroySub_.reset();
    physics_.clear();
    audio_.shutdown();
    if (scene_) {
        events_.emit(SceneUnloadedEvent{scene_.get()});
        scene_.reset();
    }
    scripts_.reset();
    cache_->setJobs(nullptr); // espera las lecturas en curso: el JobSystem y el FileSource pueden desaparecer
    cache_->clear();
    if (!usesSharedRenderer()) renderer_->shutdown(); // el del editor lo cierra el editor
    renderer_ = &ownRenderer_;
    cache_ = &ownCache_;
    TITAN_INFO("runtime", "Runtime detenido");
}

} // namespace titan
