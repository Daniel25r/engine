// Juego de ejemplo: un enjambre de partículas que rebotan en los bordes y son
// atraídas por tus dedos. Cada toque nuevo suelta 40 partículas más.
// Sirve para comprobar el motor: ECS, render por lotes, multitáctil y paso fijo.
#include <algorithm>
#include <cmath>
#include <random>

#include "forja/Engine.h"

using namespace forja;

namespace {

// Componente propio del juego: cualquier struct vale.
struct Body {
    Vec2 velocity;
    float radius = 10.0f;
};

const Color kPalette[] = {Color::hex(0xF2B54A), Color::hex(0x5FD0C8), Color::hex(0xE8676B), Color::hex(0x8FA8FF),
                          Color::hex(0xF4F1E8)};

constexpr size_t kMaxParticles = 20000;

class SwarmGame final : public Game {
public:
    void onStart(Engine& engine) override {
        engine.clearColor = Color::hex(0x10141C);
        engine.camera().viewSize = 720.0f;
        dot_ = engine.renderer().loadTexture("textures/orb.png");
        const Vec2 half = engine.camera().halfExtents();
        for (int i = 0; i < 400; ++i) spawn(engine, {rand(-half.x, half.x), rand(-half.y, half.y)});
    }

    void onUpdate(Engine& engine, float dt) override {
        Camera2D& cam = engine.camera();
        World& world = engine.world();

        for (const TouchEvent& ev : engine.input().events())
            if (ev.phase == TouchPhase::Began)
                for (int i = 0; i < 40; ++i) spawn(engine, cam.screenToWorld(ev.position));

        // Los dedos que siguen tocando atraen a todas las partículas.
        attractors_.clear();
        for (const Pointer& p : engine.input().pointers()) attractors_.push_back(cam.screenToWorld(p.position));

        const Vec2 half = cam.halfExtents();
        const float damping = 1.0f - 0.35f * dt;
        world.each<Body, Transform>([&](Entity, Body& b, Transform& t) {
            for (const Vec2& a : attractors_) {
                const Vec2 d = a - t.position;
                const float dist = std::max(d.length(), 40.0f);
                b.velocity += d / dist * (2400.0f * dt);
            }
            b.velocity *= damping;
            t.position += b.velocity * dt;

            // Rebote con los bordes de la pantalla.
            if (t.position.x < -half.x + b.radius) { t.position.x = -half.x + b.radius; b.velocity.x = std::abs(b.velocity.x) * 0.9f; }
            if (t.position.x >  half.x - b.radius) { t.position.x =  half.x - b.radius; b.velocity.x = -std::abs(b.velocity.x) * 0.9f; }
            if (t.position.y < -half.y + b.radius) { t.position.y = -half.y + b.radius; b.velocity.y = std::abs(b.velocity.y) * 0.9f; }
            if (t.position.y >  half.y - b.radius) { t.position.y =  half.y - b.radius; b.velocity.y = -std::abs(b.velocity.y) * 0.9f; }
        });
    }

private:
    float rand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }

    void spawn(Engine& engine, Vec2 at) {
        World& world = engine.world();
        if (world.count() >= kMaxParticles) return;
        const float r = rand(5.0f, 14.0f);
        const float angle = rand(0.0f, 2.0f * kPi), speed = rand(60.0f, 380.0f);

        Entity e = world.create();
        world.add<Transform>(e, {at});
        Sprite s;
        s.texture = dot_;
        s.size = {r * 2.0f, r * 2.0f};
        s.color = kPalette[std::uniform_int_distribution<int>(0, 4)(rng_)];
        world.add<Sprite>(e, s);
        world.add<Body>(e, {{std::cos(angle) * speed, std::sin(angle) * speed}, r});
    }

    TextureId dot_ = kWhiteTexture;
    std::vector<Vec2> attractors_;
    std::mt19937 rng_{1234};
};

} // namespace

std::unique_ptr<Game> forja::createGame() { return std::make_unique<SwarmGame>(); }
