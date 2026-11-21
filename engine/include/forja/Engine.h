#pragma once
#include <chrono>
#include <memory>
#include <vector>

#include "forja/Assets.h"
#include "forja/Components.h"
#include "forja/Input.h"
#include "forja/Renderer.h"
#include "forja/World.h"

namespace forja {

class Engine;

// Lo que implementa cada juego.
class Game {
public:
    virtual ~Game() = default;
    virtual void onStart(Engine&) {}
    // Lógica a paso fijo (60 veces por segundo, pase lo que pase con los FPS).
    virtual void onUpdate(Engine&, float dt) {}
    virtual void onStop(Engine&) {}
};

// Cada juego define esta función: el motor la llama al arrancar.
std::unique_ptr<Game> createGame();

class Engine {
public:
    static constexpr float kFixedStep = 1.0f / 60.0f;

    World& world() { return world_; }
    Renderer& renderer() { return renderer_; }
    Input& input() { return input_; }
    Camera2D& camera() { return camera_; }
    Assets& assets() { return *assets_; }

    float time() const { return time_; }
    float fps() const { return fps_; }

    Color clearColor = Color::hex(0x101318);
    bool showStats = true;

    void quit() { quitRequested_ = true; }
    bool quitRequested() const { return quitRequested_; }

    // --- Interfaz para la capa de plataforma (Android / escritorio) ---
    bool startup(Assets& assets, std::unique_ptr<Game> game);
    void frame(int width, int height);
    void pause();
    void resume();
    void shutdown();
    bool running() const { return game_ != nullptr; }

private:
    void renderWorld();

    using Clock = std::chrono::steady_clock;

    World world_;
    Renderer renderer_;
    Input input_;
    Camera2D camera_;
    Assets* assets_ = nullptr;
    std::unique_ptr<Game> game_;

    Clock::time_point last_;
    float accumulator_ = 0.0f;
    float time_ = 0.0f;
    float fps_ = 0.0f, fpsTimer_ = 0.0f;
    int fpsFrames_ = 0;
    bool quitRequested_ = false;

    struct DrawItem {
        int layer;
        TextureId texture;
        const Transform* transform;
        const Sprite* sprite;
    };
    std::vector<DrawItem> drawList_;
};

} // namespace forja
