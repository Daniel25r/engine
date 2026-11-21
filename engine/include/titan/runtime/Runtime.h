#pragma once
// Runtime: ejecuta un paquete de juego (project.titan + asset_index.json + assets/).
// Es lo que corre dentro del APK exportado y en el reproductor de escritorio, sin editor.
//
// Orden de cada fotograma (ver docs/ARCHITECTURE.md):
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
#include "titan/render/Renderer.h"
#include "titan/scripting/ScriptSystem.h"

namespace titan {

struct FrameProfile {
    double frameMs = 0, scriptMs = 0, transformMs = 0, renderMs = 0;
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
    void frame(int width, int height);
    void pause();
    void resume();
    void shutdown();

    bool running() const { return started_; }
    bool quitRequested() const { return quitRequested_; }
    Input& input() { return input_; }
    Renderer& renderer() { return renderer_; }
    EventBus& events() { return events_; }
    Scene* scene() { return scene_.get(); }
    ScriptSystem& scripts() { return *scripts_; }
    const FrameProfile& profile() const { return profile_; }

    bool showStats = true;

    // ScriptHost
    double scriptTime() const override { return time_; }
    float scriptDeltaTime() const override { return delta_; }
    float scriptFixedDeltaTime() const override { return kFixedStep; }
    uint64_t scriptFrame() const override { return frameIndex_; }
    const Input& scriptInput() const override { return input_; }
    Vec2 scriptScreenSize() const override { return screen_; }
    bool scriptScreenToWorld(Vec2 pixels, Vec3& world) const override;
    bool scriptLoadScene(const std::string& nameOrPath) override;

private:
    bool loadScene(UUID id);
    void applyPendingSceneLoad();
    void render();
    Entity findCamera() const;
    TextureHandle textureFor(AssetRef ref);

    using Clock = std::chrono::steady_clock;

    FileSource* files_ = nullptr;
    EventBus events_;
    ComponentRegistry registry_;
    AssetDatabase assets_;
    Renderer renderer_;
    Input input_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<ScriptSystem> scripts_;
    std::unordered_map<uint64_t, TextureHandle> textures_;
    std::string projectName_;
    UUID pendingScene_;

    Clock::time_point last_;
    float accumulator_ = 0;
    float delta_ = 0;
    double time_ = 0;
    uint64_t frameIndex_ = 0;
    Vec2 screen_{1, 1};
    Mat4 viewProj_;
    bool started_ = false;
    bool quitRequested_ = false;
    bool warnedNoCamera_ = false;

    FrameProfile profile_;
    float fps_ = 0, fpsTimer_ = 0;
    int fpsFrames_ = 0;

    struct DrawItem {
        int order;
        TextureHandle texture;
        const Mat4* world;
        const SpriteRenderer* sprite;
    };
    std::vector<DrawItem> drawList_;
};

} // namespace titan
