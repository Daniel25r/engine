#pragma once
// Simulación de partículas en la CPU (sin GPU: se prueba sin ventana). El render la lee en SceneRenderer.
#include <cstdint>
#include <vector>

#include "titan/core/Bounds.h"
#include "titan/core/Components.h"
#include "titan/core/Scene.h"

namespace titan {

struct Particle {
    Vec3 position;     // en el mundo (worldSpace) o en el espacio local del emisor
    Vec3 velocity;
    float age = 0, life = 1;
    float rotation = 0, rotationSpeed = 0; // grados
};

struct ParticleState {
    std::vector<Particle> particles;
    float time = 0;          // dentro del ciclo actual
    float emitCarry = 0;     // fracción de partícula pendiente de emitir
    float distanceCarry = 0;
    bool playing = false;
    bool started = false;    // ya se aplicó playOnStart
    bool burstPending = true;
    bool hasLastPosition = false;
    Vec3 lastPosition;
    uint32_t rng = 0x9E3779B9u;
    AABB bounds;             // caja de las partículas vivas en el mundo (para el culling)
};

// Avanza un emisor: nace, mueve y retira partículas. "world" es la matriz de mundo de su entidad.
void updateParticleSystem(ParticleSystem& ps, const Mat4& world, float dt);
// Todos los emisores activos de la escena.
void updateParticles(Scene& scene, float dt);

void playParticles(ParticleSystem& ps);   // empieza un ciclo nuevo
void stopParticles(ParticleSystem& ps);   // deja de emitir (las vivas terminan su vida)
void emitParticles(ParticleSystem& ps, const Mat4& world, int count); // emisión puntual (impactos, eventos)
void clearParticles(ParticleSystem& ps);  // quita todas al instante
size_t aliveParticles(const ParticleSystem& ps);

} // namespace titan
