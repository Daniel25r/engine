#pragma once
// Runtime: ejecuta un paquete de juego (project.titan + asset_index.json + assets/).
// Es lo que corre dentro del APK exportado y en el reproductor de escritorio, sin editor.
//
// Orden de cada fotograma (ver docs/TITAN_ENGINE_ARCHITECTURE.md):
//   1. Paso fijo (0..N veces a 60 Hz): scripts onFixedUpdate  [física: pendiente]
//   2. scripts onUpdate, onLateUpdate
//   3. destrucciones diferidas
//   4. TransformSystem (matrices de mundo)
//   5. Render: cámara activa + sprites
//   6. cambio de escena pendiente, si un script lo pidió
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "titan/core/Events.h"
#include "titan/core/FileSource.h"
#include "titan/core/Input.h"
#include "titan/core/Project.h"
#include "titan/core/Scene.h"
#include "titan/core/Serialization.h"
#include "titan/audio/AudioEngine.h"
#include "titan/physics/PhysicsWorld.h"
#include "titan/render/Renderer.h"
#include "titan/core/CameraSystem.h"
#include "titan/runtime/ResourceCache.h"
#include "titan/runtime/SceneRenderer.h"
#include "titan/runtime/VoxelSystem.h"
#include "titan/scripting/ScriptSystem.h"
#include "titan/core/JobSystem.h"
#include "titan/ui/UISystem.h"

namespace titan {

struct FrameProfile {
    double frameMs = 0, scriptMs = 0, transformMs = 0, renderMs = 0;
    double physicsMs = 0;   // paso fijo: física + onFixedUpdate + contactos
    double particlesMs = 0; // simulación de partículas (en paralelo con el Job System)
    size_t transformsUpdated = 0;
};

class Runtime final : public ScriptHost {
public:
    static constexpr float kFixedStep = 1.0f / 60.0f;
    static constexpr int kMaxFixedStepsPerFrame = 5;

    Runtime();
    ~Runtime() override;

    // Carga project.titan, el índice de recursos y la escena inicial. Necesita un contexto GL activo.
    bool start(FileSource& files);
    // Modo Play del editor: ejecuta una escena en memoria (con sus cambios sin guardar) usando los
    // recursos del proyecto abierto. Los cambios que haga el juego no tocan la escena del editor.
    // Con sharedRenderer y sharedCache usa el Renderer y la caché del editor: no recompila shaders ni vuelve
    // a cargar texturas, modelos ni fuentes, y no las duplica en la GPU. El Runtime no los inicia ni los cierra.
    bool startEmbedded(FileSource& projectRoot, const AssetDatabase& assets, const std::string& sceneText,
                       const std::string& sceneName, Renderer* sharedRenderer = nullptr,
                       ResourceCache* sharedCache = nullptr);
    void frame(int width, int height) { frame(Viewport{0, 0, width, height}); }
    void frame(const Viewport& viewport);
    void pause();
    void resume();
    void shutdown();

    bool running() const { return started_; }
    bool quitRequested() const { return quitRequested_; }
    Input& input() { return input_; }
    Renderer& renderer() { return *renderer_; }
    bool usesSharedRenderer() const { return renderer_ != &ownRenderer_; }
    EventBus& events() { return events_; }
    Scene* scene() { return scene_.get(); }
    ScriptSystem& scripts() { return *scripts_; }
    const FrameProfile& profile() const { return profile_; }
    JobSystem& jobs() { return jobs_; }
    size_t pendingTextures() const { return cache_ ? cache_->pendingTextures() : 0; }
    bool parallel = true; // false: todo en el hilo principal (para comparar y depurar)

    bool showStats = true;
    // En pausa no se ejecuta la lógica, pero se sigue dibujando.
    bool paused = false;

    // ScriptHost
    double scriptTime() const override { return time_; }
    float scriptDeltaTime() const override { return delta_; }
    float scriptFixedDeltaTime() const override { return kFixedStep; }
    uint64_t scriptFrame() const override { return frameIndex_; }
    const Input& scriptInput() const override { return input_; }
    Vec2 scriptScreenSize() const override { return screen_; }
    bool scriptScreenToWorld(Vec2 pixels, Vec3& world) const override;
    bool scriptLoadScene(const std::string& nameOrPath) override;
    void scriptDrawText(float x, float y, float size, const std::string& text, Color color) override;
    PhysicsApi* scriptPhysics() override { return &physics_; }
    Entity scriptInstantiate(const std::string& prefab, const Vec3& position) override;
    bool scriptPlaySound(const std::string& path, Entity owner, bool spatial, const Vec3& at, float volume,
                         const std::string& bus) override;
    AudioEngine* scriptAudio() override { return &audio_; }
    // Mezclador del proyecto (assets/audio_buses.json); el editor lo vuelve a leer al pulsar Jugar.
    void loadBusLayout(FileSource& files);
    void preloadTextures();
    void scriptPlayEntitySound(Entity e) override;
    void scriptStopSound(Entity e) override { audio_.stopOwner(e); }
    void scriptSetVolume(float v) override { audio_.setMasterVolume(v); }
    bool scriptSetQuality(const std::string& name) override;
    std::string scriptQuality() const override { return qualityName(renderer_->quality().level); }
    DeviceCaps scriptDeviceCaps() const override { return renderer_->caps(); }
    InputActions* scriptActions() override { return &actions_; }
    std::string scriptSavePath() const override { return savePath_; }
    FrameStats scriptFrameStats() const override;
    bool scriptCameraBasis(Vec3& forward, Vec3& right) const override;
    NavGrid* scriptNavGrid() override { return &navGrid_; }
    int scriptBakeNav(const NavSettings* settings) override; // nulo = zona automática según la escena
    VoxelWorld* scriptVoxelWorld() override { return voxel_.world(); }
    void scriptVoxelChanged(int x, int y, int z) override { voxel_.blockChanged(x, y, z); }
    int scriptVoxelSave() override;
    VoxelSystem& voxel() { return voxel_; }
    // Dónde se guarda la partida (Save.* en Lua). Lo fija la plataforma antes de start().
    void setSavePath(const std::string& path) { savePath_ = path; }
    InputActions& actions() { return actions_; }
    AudioEngine& audio() { return audio_; }
    PhysicsWorld& physics() { return physics_; }
    const UISystem& ui() const { return ui_; }
    SceneRenderer& sceneRenderer() { return sceneRenderer_; }

private:
    bool initSystems(FileSource& files);
    bool loadScene(UUID id);
    bool activateScene(const std::string& text, const std::string& name);
    void applyPendingSceneLoad();
    void render(const Viewport& viewport);
    void startVoxel(); // si la escena tiene un VoxelTerrain
    void captureProbes(); // si la escena tiene una ReflectionProbe
    bool cameraPosition(Vec3& out);


    using Clock = std::chrono::steady_clock;

    FileSource* files_ = nullptr;
    EventBus events_;
    ComponentRegistry registry_;
    AssetDatabase assets_;
    Renderer ownRenderer_;              // el del juego exportado
    Renderer* renderer_ = &ownRenderer_; // o el del editor, en el modo Jugar
    ResourceCache ownCache_;
    ResourceCache* cache_ = &ownCache_;
    Input input_;
    PhysicsWorld physics_;
    AudioEngine audio_;
    Subscription physicsDestroySub_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<ScriptSystem> scripts_;
    UISystem ui_;
    InputActions actions_;
    CameraSystem cameras_;
    NavGrid navGrid_;
    VoxelSystem voxel_;
    std::string savePath_;
    std::vector<Entity> clicked_;
    std::string projectName_;
    UUID pendingScene_;

    Clock::time_point last_;
    float accumulator_ = 0;
    float delta_ = 0;
    double time_ = 0;
    uint64_t frameIndex_ = 0;
    Vec2 screen_{1, 1};
    Mat4 viewProj_;
    bool perspective_ = false;
    SceneRenderer sceneRenderer_;
    struct ScriptText { float x, y, size; std::string text; Color color; };
    std::vector<ScriptText> scriptTexts_;
    bool started_ = false;
    bool quitRequested_ = false;
    bool warnedNoCamera_ = false;

    FrameProfile profile_;
    JobSystem jobs_; // hilos de trabajo del motor (núcleos - 1)
    float fps_ = 0, fpsTimer_ = 0;
    int fpsFrames_ = 0;
};

} // namespace titan
