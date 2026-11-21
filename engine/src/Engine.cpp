#include "forja/Engine.h"

#include <algorithm>
#include <cstdio>

#include "forja/Log.h"

namespace forja {

bool Engine::startup(Assets& assets, std::unique_ptr<Game> game) {
    assets_ = &assets;
    if (!renderer_.init(assets)) {
        FORJA_ERROR("No se pudo iniciar el renderizador");
        return false;
    }
    game_ = std::move(game);
    last_ = Clock::now();
    game_->onStart(*this);
    FORJA_LOG("Motor iniciado");
    return true;
}

void Engine::pause() { input_.cancelAll(); }

void Engine::resume() {
    // Sin esto, el primer fotograma tras volver a la app tendría un dt enorme.
    last_ = Clock::now();
    accumulator_ = 0.0f;
}

void Engine::shutdown() {
    if (!game_) return;
    game_->onStop(*this);
    game_.reset();
    world_.clear();
    renderer_.shutdown();
    FORJA_LOG("Motor detenido");
}

void Engine::frame(int width, int height) {
    if (!game_) return;
    camera_.viewport = {static_cast<float>(std::max(width, 1)), static_cast<float>(std::max(height, 1))};

    const auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - last_).count();
    last_ = now;
    dt = std::min(dt, 0.25f); // evita la "espiral de la muerte" tras una pausa larga

    // Lógica a paso fijo: el juego se comporta igual a 30, 60 o 120 FPS.
    accumulator_ += dt;
    while (accumulator_ >= kFixedStep) {
        game_->onUpdate(*this, kFixedStep);
        time_ += kFixedStep;
        accumulator_ -= kFixedStep;
        if (!game_) return;
        input_.clearEvents();
    }

    fpsFrames_++;
    fpsTimer_ += dt;
    if (fpsTimer_ >= 0.5f) {
        fps_ = fpsFrames_ / fpsTimer_;
        fpsFrames_ = 0;
        fpsTimer_ = 0.0f;
    }

    renderer_.beginFrame(width, height, clearColor);
    renderer_.setProjection(camera_.matrix());
    renderWorld();
    if (showStats) {
        const RenderStats& s = renderer_.stats();
        char line[160];
        std::snprintf(line, sizeof(line), "%.0f FPS  %zu entidades  %d draw calls  %d sprites", fps_, world_.count(),
                      s.drawCalls, s.quads);
        const float h = std::max(16.0f, std::min(width, height) * 0.028f);
        renderer_.drawText({h * 0.8f, h * 0.8f}, h, line, Color{1.0f, 1.0f, 1.0f, 0.85f});
    }
    renderer_.endFrame();
}

void Engine::renderWorld() {
    // Solo se dibuja lo que está dentro de la cámara (con margen).
    const Vec2 half = camera_.halfExtents();
    const float minX = camera_.position.x - half.x, maxX = camera_.position.x + half.x;
    const float minY = camera_.position.y - half.y, maxY = camera_.position.y + half.y;

    drawList_.clear();
    world_.each<Sprite, Transform>([&](Entity, Sprite& s, Transform& t) {
        const float r = std::max(std::abs(s.size.x * t.scale.x), std::abs(s.size.y * t.scale.y));
        if (t.position.x + r < minX || t.position.x - r > maxX || t.position.y + r < minY ||
            t.position.y - r > maxY)
            return;
        drawList_.push_back({s.layer, s.texture, &t, &s});
    });
    // Ordenar por capa y luego por textura minimiza los cambios de estado (menos draw calls).
    std::stable_sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.texture < b.texture;
    });
    for (const DrawItem& d : drawList_) {
        const Sprite& s = *d.sprite;
        const Transform& t = *d.transform;
        renderer_.drawQuad(s.texture, t.position, {s.size.x * t.scale.x, s.size.y * t.scale.y}, t.rotation, s.pivot,
                           s.uv, s.color);
    }
}

} // namespace forja
