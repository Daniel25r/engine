#pragma once
// Conecta el mundo de bloques (core/Voxel) con el juego: streaming de chunks alrededor de un punto (generación o
// lectura del disco y mallas en los hilos del JobSystem), dibujo (modelos del Renderer), colisión (mallas
// estáticas de Jolt) y guardado de los cambios al descargar chunks y al parar.
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "titan/core/JobSystem.h"
#include "titan/core/Voxel.h"
#include "titan/physics/PhysicsWorld.h"
#include "titan/render/Renderer.h"
#include "titan/runtime/SceneRenderer.h"

namespace titan {

struct VoxelStats {
    int loaded = 0, meshed = 0, pendingLoads = 0, pendingMeshes = 0, triangles = 0;
    double lastMeshMs = 0;
};

class VoxelSystem {
public:
    ~VoxelSystem();
    // folder: carpeta del mundo (se crea si hace falta); jobs puede ser nulo (todo en el hilo principal).
    void start(const VoxelTerrain& settings, const std::string& folder, JobSystem* jobs, Renderer* renderer, PhysicsWorld* physics);
    void stop(); // guarda lo modificado y libera todo
    bool active() const { return active_; }
    // Una vez por fotograma (hilo principal): pide y descarga chunks según la distancia a center y aplica lo
    // terminado en otros hilos (como mucho unos pocos por fotograma para no dar tirones).
    void update(Vec3 center);
    // Carga de golpe los chunks cercanos (al empezar, para que el jugador no caiga al vacío).
    void preload(Vec3 center, int radius);
    VoxelWorld* world() { return active_ ? &world_ : nullptr; }
    // Tras cambiar un bloque: rehace al momento la malla de su chunk (y del vecino si está en el borde).
    void blockChanged(int x, int y, int z);
    const VoxelStats& stats() const { return stats_; }
    std::vector<SceneRenderer::ExtraModel>& drawables() { return drawables_; }

private:
    struct ChunkGpu {
        int model = -1;
        uint32_t body = 0;
        bool dirty = true;
    };
    struct MeshResult {
        int cx, cz;
        std::vector<uint8_t> glb;
        std::vector<Vec3> colVerts;
        std::vector<uint32_t> colIdx;
        int triangles = 0;
    };
    static int64_t key(int cx, int cz) { return (int64_t(cx) << 32) ^ int64_t(uint32_t(cz)); }
    void applyMesh(MeshResult& r);
    void meshNow(int cx, int cz);
    void releaseChunk(int cx, int cz);
    void rebuildDrawables();

    bool active_ = false;
    VoxelWorld world_;
    int viewDistance_ = 6;
    JobSystem* jobs_ = nullptr;
    Renderer* renderer_ = nullptr;
    PhysicsWorld* physics_ = nullptr;
    std::unordered_map<int64_t, ChunkGpu> gpu_;
    std::unordered_set<int64_t> loading_, meshing_;
    std::mutex mutex_;
    std::deque<std::unique_ptr<VoxelChunk>> loaded_; // terminados en otros hilos
    std::deque<MeshResult> meshed_;
    JobCounter inFlight_;
    std::vector<SceneRenderer::ExtraModel> drawables_;
    bool drawablesDirty_ = true;
    VoxelStats stats_;
};

} // namespace titan
