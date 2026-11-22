#include "Test.h"
#include "titan/core/Particles.h"

using namespace titan;

TEST("partículas: ritmo de emisión, vida y máximo") {
    ParticleSystem ps;
    ps.rate = 10;
    ps.lifetimeMin = ps.lifetimeMax = 100;
    const Mat4 world;
    for (int i = 0; i < 60; ++i) updateParticleSystem(ps, world, 1.0f / 60); // 1 segundo
    CHECK(aliveParticles(ps) >= 9 && aliveParticles(ps) <= 11);
    ps.maxParticles = 15;
    for (int i = 0; i < 120; ++i) updateParticleSystem(ps, world, 1.0f / 60);
    CHECK_EQ(aliveParticles(ps), 15u);
    ParticleSystem shortLived;
    shortLived.rate = 100;
    shortLived.lifetimeMin = shortLived.lifetimeMax = 0.1f;
    for (int i = 0; i < 60; ++i) updateParticleSystem(shortLived, world, 1.0f / 60);
    CHECK(aliveParticles(shortLived) <= 12); // solo las de los últimos 0,1 s
}

TEST("partículas: ráfaga, sin repetir, parar y emisión puntual") {
    ParticleSystem ps;
    ps.rate = 0;
    ps.burst = 50;
    ps.looping = false;
    ps.duration = 0.5f;
    ps.lifetimeMin = ps.lifetimeMax = 10;
    const Mat4 world;
    updateParticleSystem(ps, world, 0.01f);
    CHECK_EQ(aliveParticles(ps), 50u);
    for (int i = 0; i < 60; ++i) updateParticleSystem(ps, world, 1.0f / 60);
    CHECK_EQ(aliveParticles(ps), 50u); // sin repetir: no hay segunda ráfaga
    CHECK(!ps.state->playing);
    emitParticles(ps, world, 7);
    CHECK_EQ(aliveParticles(ps), 57u);
    clearParticles(ps);
    CHECK_EQ(aliveParticles(ps), 0u);
    ParticleSystem off;
    off.playOnStart = false;
    updateParticleSystem(off, world, 1);
    CHECK_EQ(aliveParticles(off), 0u);
    playParticles(off);
    updateParticleSystem(off, world, 0.5f);
    CHECK(aliveParticles(off) > 0);
}

TEST("partículas: gravedad, dirección, espacio del mundo y emisión por distancia") {
    ParticleSystem ps;
    ps.rate = 0;
    ps.burst = 20;
    ps.spread = 0; // todas hacia +Y local
    ps.speedMin = ps.speedMax = 5;
    ps.lifetimeMin = ps.lifetimeMax = 10;
    ps.acceleration = {0, -10, 0};
    const Mat4 world = Mat4::translate({100, 0, 0});
    updateParticleSystem(ps, world, 0.0f);
    for (const Particle& p : ps.state->particles) {
        CHECK_NEAR(p.position.x, 100, 1e-3);
        CHECK_NEAR(p.velocity.y, 5, 1e-3);
    }
    for (int i = 0; i < 100; ++i) updateParticleSystem(ps, world, 0.01f); // 1 s: v = 5 - 10 = -5
    CHECK_NEAR(ps.state->particles[0].velocity.y, -5, 1e-2);
    CHECK(ps.state->bounds.valid() && ps.state->bounds.min.x > 99);
    ParticleSystem trail;
    trail.rate = 0;
    trail.ratePerMeter = 4;
    trail.lifetimeMin = trail.lifetimeMax = 100;
    for (int i = 0; i <= 10; ++i) updateParticleSystem(trail, Mat4::translate({i * 1.0f, 0, 0}), 0.1f); // 10 m
    CHECK(aliveParticles(trail) >= 39 && aliveParticles(trail) <= 41);
}
