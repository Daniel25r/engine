#include "titan/core/Particles.h"

#include <algorithm>
#include <cmath>

namespace titan {

namespace {

ParticleState& stateOf(ParticleSystem& ps) {
    if (!ps.state) ps.state = std::make_shared<ParticleState>();
    return *ps.state;
}

float rand01(uint32_t& s) { // xorshift32: rápido y determinista (las pruebas pueden repetir resultados)
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s & 0xFFFFFF) / 16777216.0f;
}
float range(uint32_t& s, float a, float b) { return a + (b - a) * rand01(s); }

void spawn(ParticleSystem& ps, ParticleState& st, const Mat4& world, int count) {
    count = std::min(count, std::max(0, ps.maxParticles - static_cast<int>(st.particles.size())));
    const float spread = std::clamp(ps.spread, 0.0f, 180.0f) * kDegToRad;
    for (int i = 0; i < count; ++i) {
        Particle p;
        // Dirección dentro de un cono alrededor de +Y (distribución uniforme en el casquete esférico).
        const float cosMax = std::cos(spread);
        const float z = range(st.rng, cosMax, 1.0f), phi = range(st.rng, 0, 6.2831853f);
        const float r = std::sqrt(std::max(0.0f, 1 - z * z));
        Vec3 dir{r * std::cos(phi), z, r * std::sin(phi)};
        Vec3 pos;
        if (ps.shape == 1) {
            const Vec3 d{range(st.rng, -1, 1), range(st.rng, -1, 1), range(st.rng, -1, 1)};
            pos = d.normalized() * (ps.shapeSize.x * std::cbrt(rand01(st.rng)));
        } else if (ps.shape == 2) {
            pos = {range(st.rng, -0.5f, 0.5f) * ps.shapeSize.x, range(st.rng, -0.5f, 0.5f) * ps.shapeSize.y,
                   range(st.rng, -0.5f, 0.5f) * ps.shapeSize.z};
        }
        const float speed = range(st.rng, ps.speedMin, ps.speedMax);
        if (ps.worldSpace) {
            p.position = world.transformPoint(pos);
            p.velocity = world.transformDirection(dir).normalized() * speed;
        } else {
            p.position = pos;
            p.velocity = dir * speed;
        }
        p.life = std::max(0.01f, range(st.rng, ps.lifetimeMin, ps.lifetimeMax));
        p.rotation = range(st.rng, 0, 360);
        p.rotationSpeed = range(st.rng, -ps.rotationSpeed, ps.rotationSpeed);
        st.particles.push_back(p);
    }
}

} // namespace

void playParticles(ParticleSystem& ps) {
    ParticleState& st = stateOf(ps);
    st.playing = true;
    st.started = true;
    st.time = 0;
    st.emitCarry = 0;
    st.burstPending = true;
}

void stopParticles(ParticleSystem& ps) {
    ParticleState& st = stateOf(ps);
    st.playing = false;
    st.started = true;
}

void clearParticles(ParticleSystem& ps) {
    if (ps.state) ps.state->particles.clear();
}

size_t aliveParticles(const ParticleSystem& ps) { return ps.state ? ps.state->particles.size() : 0; }

void emitParticles(ParticleSystem& ps, const Mat4& world, int count) { spawn(ps, stateOf(ps), world, std::max(0, count)); }

void updateParticleSystem(ParticleSystem& ps, const Mat4& world, float dt) {
    ParticleState& st = stateOf(ps);
    if (!st.started) {
        st.started = true;
        if (ps.playOnStart) playParticles(ps);
    }
    dt = std::max(0.0f, dt);

    // 1. Mover y envejecer.
    const float dragK = std::max(0.0f, 1.0f - ps.drag * dt);
    for (Particle& p : st.particles) {
        p.age += dt;
        p.velocity = (p.velocity + ps.acceleration * dt) * dragK;
        p.position = p.position + p.velocity * dt;
        p.rotation += p.rotationSpeed * dt;
    }
    st.particles.erase(std::remove_if(st.particles.begin(), st.particles.end(),
                                      [](const Particle& p) { return p.age >= p.life; }),
                       st.particles.end());

    // 2. Emitir.
    const Vec3 here = world.translation();
    if (st.playing) {
        if (st.burstPending && ps.burst > 0) spawn(ps, st, world, ps.burst);
        st.burstPending = false;
        st.emitCarry += std::max(0.0f, ps.rate) * dt;
        if (ps.ratePerMeter > 0 && st.hasLastPosition) st.emitCarry += (here - st.lastPosition).length() * ps.ratePerMeter;
        const int n = static_cast<int>(st.emitCarry);
        st.emitCarry -= static_cast<float>(n);
        spawn(ps, st, world, n);
        st.time += dt;
        if (st.time >= std::max(0.01f, ps.duration)) {
            if (ps.looping) {
                st.time = std::fmod(st.time, std::max(0.01f, ps.duration));
                st.burstPending = true;
            } else {
                st.playing = false;
            }
        }
    }
    st.lastPosition = here;
    st.hasLastPosition = true;

    // 3. Caja en el mundo (para no dibujar emisores fuera de cámara).
    st.bounds = AABB{};
    const float pad = std::max(ps.sizeStart, ps.sizeEnd);
    for (const Particle& p : st.particles) {
        const Vec3 w = ps.worldSpace ? p.position : world.transformPoint(p.position);
        st.bounds.expand(w - Vec3{pad, pad, pad});
        st.bounds.expand(w + Vec3{pad, pad, pad});
    }
}

void updateParticles(Scene& scene, float dt) {
    scene.world().each<ParticleSystem>([&](Entity e, ParticleSystem& ps) {
        if (!scene.isActiveInHierarchy(e)) return;
        updateParticleSystem(ps, scene.transform(e).worldMatrix(), dt);
    });
}

} // namespace titan
