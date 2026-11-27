#include "titan/runtime/VoxelSystem.h"

#include <chrono>
#include <cmath>

#include "titan/core/Log.h"

namespace titan {

namespace {
std::string chunkModelPath(int cx, int cz) { return "voxel/" + std::to_string(cx) + "_" + std::to_string(cz) + ".glb"; }
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Malla (GLB para dibujar) y triángulos de colisión de un chunk: se puede llamar desde cualquier hilo.
void buildMesh(const PaddedChunk& p, const BlockRegistry& reg, std::vector<uint8_t>& glb, std::vector<Vec3>& cv,
               std::vector<uint32_t>& ci, int& triangles) {
    const MeshBuild draw = meshChunk(p, reg);
    triangles = int(draw.triangleCount());
    if (triangles > 0) writeGlb(draw, glb);
    const MeshBuild solid = meshChunk(p, reg, true);
    for (const MeshBuildPart& part : solid.parts) {
        const uint32_t base = uint32_t(cv.size());
        for (const MeshBuildVertex& v : part.vertices) cv.push_back(v.position);
        for (uint32_t i : part.indices) ci.push_back(base + i);
    }
}
} // namespace

VoxelSystem::~VoxelSystem() { stop(); }

void VoxelSystem::start(const VoxelTerrain& s, const std::string& folder, JobSystem* jobs, Renderer* renderer, PhysicsWorld* physics) {
    stop();
    world_ = VoxelWorld{};
    world_.folder = folder;
    world_.gen.seed = uint32_t(s.seed);
    world_.gen.seaLevel = std::clamp(s.seaLevel, 2, kChunkHeight - 10);
    world_.gen.terrainScale = s.terrainScale;
    world_.gen.caves = s.caves;
    world_.gen.trees = s.trees;
    std::string err;
    if (!folder.empty()) {
        if (world_.loadSettings(&err)) TITAN_INFO("voxel", "Mundo '%s' cargado (semilla %u)", folder.c_str(), world_.gen.seed);
        else if (!err.empty()) TITAN_ERROR("voxel", "%s: %s", folder.c_str(), err.c_str());
        else world_.saveSettings(); // mundo nuevo: se guarda la semilla para que vuelva a ser el mismo
    }
    viewDistance_ = std::clamp(s.viewDistance, 1, 24);
    jobs_ = jobs;
    renderer_ = renderer;
    physics_ = physics;
    stats_ = {};
    active_ = true;
}

void VoxelSystem::stop() {
    if (!active_) return;
    if (jobs_) jobs_->wait(inFlight_); // nada sigue escribiendo en las colas
    const int saved = world_.saveModified();
    if (saved) TITAN_INFO("voxel", "%d chunks modificados guardados", saved);
    for (const auto& [cx, cz] : world_.chunkCoords()) releaseChunk(cx, cz);
    gpu_.clear();
    loading_.clear();
    meshing_.clear();
    loaded_.clear();
    meshed_.clear();
    drawables_.clear();
    active_ = false;
}

void VoxelSystem::releaseChunk(int cx, int cz) {
    auto it = gpu_.find(key(cx, cz));
    if (it == gpu_.end()) return;
    if (renderer_ && it->second.model >= 0) renderer_->unloadModel(chunkModelPath(cx, cz));
    if (physics_ && it->second.body) physics_->removeStaticMesh(it->second.body);
    gpu_.erase(it);
    drawablesDirty_ = true;
}

void VoxelSystem::applyMesh(MeshResult& r) {
    auto it = gpu_.find(key(r.cx, r.cz));
    if (it == gpu_.end() || !world_.chunk(r.cx, r.cz)) return; // se descargó mientras tanto
    ChunkGpu& g = it->second;
    if (renderer_) {
        if (r.glb.empty()) {
            renderer_->unloadModel(chunkModelPath(r.cx, r.cz));
            g.model = -1;
        } else {
            g.model = renderer_->setModelData(chunkModelPath(r.cx, r.cz), std::move(r.glb));
        }
    }
    if (physics_) {
        if (g.body) physics_->removeStaticMesh(g.body);
        g.body = r.colIdx.empty() ? 0 : physics_->addStaticMesh(r.colVerts, r.colIdx);
    }
    drawablesDirty_ = true;
}

void VoxelSystem::meshNow(int cx, int cz) {
    if (!world_.chunk(cx, cz)) return;
    MeshResult r{cx, cz, {}, {}, {}, 0};
    buildMesh(world_.padded(cx, cz), world_.blocks, r.glb, r.colVerts, r.colIdx, r.triangles);
    gpu_[key(cx, cz)].dirty = false;
    applyMesh(r);
}

void VoxelSystem::blockChanged(int x, int y, int z) {
    (void)y;
    const int cx = floorDiv(x, kChunkSize), cz = floorDiv(z, kChunkSize);
    meshNow(cx, cz);
    const int lx = x - cx * kChunkSize, lz = z - cz * kChunkSize;
    if (lx == 0) meshNow(cx - 1, cz);
    if (lx == kChunkSize - 1) meshNow(cx + 1, cz);
    if (lz == 0) meshNow(cx, cz - 1);
    if (lz == kChunkSize - 1) meshNow(cx, cz + 1);
}

void VoxelSystem::preload(Vec3 center, int radius) {
    if (!active_) return;
    const int ccx = floorDiv(int(std::floor(center.x)), kChunkSize), ccz = floorDiv(int(std::floor(center.z)), kChunkSize);
    for (int dz = -radius - 1; dz <= radius + 1; ++dz)
        for (int dx = -radius - 1; dx <= radius + 1; ++dx) {
            world_.ensureChunk(ccx + dx, ccz + dz);
            gpu_[key(ccx + dx, ccz + dz)];
        }
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) meshNow(ccx + dx, ccz + dz);
}

void VoxelSystem::update(Vec3 center) {
    if (!active_) return;
    const auto t0 = std::chrono::steady_clock::now();
    const int ccx = floorDiv(int(std::floor(center.x)), kChunkSize), ccz = floorDiv(int(std::floor(center.z)), kChunkSize);
    const int R = viewDistance_;

    // 1. Lo terminado en otros hilos.
    std::deque<std::unique_ptr<VoxelChunk>> loaded;
    std::deque<MeshResult> meshed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded.swap(loaded_);
        meshed.swap(meshed_);
    }
    for (auto& c : loaded) {
        const int cx = c->cx, cz = c->cz;
        loading_.erase(key(cx, cz));
        if (std::abs(cx - ccx) > R + 2 || std::abs(cz - ccz) > R + 2) continue; // ya no hace falta
        world_.insert(std::move(c));
        gpu_[key(cx, cz)].dirty = true;
        for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) // los vecinos ya no tienen un hueco al lado
            if (auto it = gpu_.find(key(cx + dx, cz + dz)); it != gpu_.end()) it->second.dirty = true;
    }
    int applied = 0;
    while (!meshed.empty()) {
        MeshResult& r = meshed.front();
        meshing_.erase(key(r.cx, r.cz));
        if (applied < 4) { // subir a la GPU y crear la colisión cuesta: unos pocos por fotograma
            applyMesh(r);
            ++applied;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            meshed_.push_back(std::move(r));
            meshing_.insert(key(meshed_.back().cx, meshed_.back().cz));
        }
        meshed.pop_front();
    }

    // 2. Pedir los chunks que faltan, del centro hacia fuera.
    std::vector<std::pair<int, int>> wanted;
    for (int dz = -R - 1; dz <= R + 1; ++dz)
        for (int dx = -R - 1; dx <= R + 1; ++dx)
            if (dx * dx + dz * dz <= (R + 1) * (R + 1)) wanted.push_back({dx, dz});
    std::sort(wanted.begin(), wanted.end(), [](auto a, auto b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
    int requested = 0;
    for (auto [dx, dz] : wanted) {
        const int cx = ccx + dx, cz = ccz + dz;
        const int64_t k = key(cx, cz);
        if (world_.chunk(cx, cz) || loading_.count(k)) continue;
        if (requested >= 6 || loading_.size() >= 12) break;
        loading_.insert(k);
        ++requested;
        auto job = [this, cx, cz] {
            auto c = std::make_unique<VoxelChunk>();
            c->cx = cx;
            c->cz = cz;
            if (!world_.loadChunk(cx, cz, *c)) generateChunk(world_.gen, world_.blocks, *c);
            std::lock_guard<std::mutex> lock(mutex_);
            loaded_.push_back(std::move(c));
        };
        if (jobs_) jobs_->submit(job, &inFlight_, JobPriority::Normal);
        else job();
    }

    // 3. Mallas de los chunks visibles cuyo contenido cambió (con sus 4 vecinos ya cargados: bordes correctos).
    int meshRequests = 0;
    for (auto& [k, g] : gpu_) {
        if (!g.dirty || meshing_.count(k) || meshRequests >= 4) continue;
        const int cx = int(k >> 32), cz = int(int32_t(uint32_t(k & 0xFFFFFFFF)));
        if (std::abs(cx - ccx) > R || std::abs(cz - ccz) > R) continue;
        if (!world_.chunk(cx + 1, cz) || !world_.chunk(cx - 1, cz) || !world_.chunk(cx, cz + 1) || !world_.chunk(cx, cz - 1)) continue;
        g.dirty = false;
        meshing_.insert(k);
        ++meshRequests;
        auto padded = std::make_shared<PaddedChunk>(world_.padded(cx, cz)); // copia: el trabajo no toca el mundo
        auto job = [this, padded] {
            MeshResult r{padded->cx, padded->cz, {}, {}, {}, 0};
            buildMesh(*padded, world_.blocks, r.glb, r.colVerts, r.colIdx, r.triangles);
            std::lock_guard<std::mutex> lock(mutex_);
            meshed_.push_back(std::move(r));
        };
        if (jobs_) jobs_->submit(job, &inFlight_, JobPriority::Normal);
        else job();
    }

    // 4. Descargar lo lejano (guardando lo que el jugador cambió).
    for (const auto& [cx, cz] : world_.chunkCoords())
        if (std::abs(cx - ccx) > R + 3 || std::abs(cz - ccz) > R + 3) {
            std::unique_ptr<VoxelChunk> c = world_.remove(cx, cz);
            if (c && c->modified) world_.saveChunk(*c);
            releaseChunk(cx, cz);
        }
    rebuildDrawables();
    stats_.loaded = int(world_.loadedChunks());
    stats_.pendingLoads = int(loading_.size());
    stats_.pendingMeshes = int(meshing_.size());
    stats_.lastMeshMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void VoxelSystem::rebuildDrawables() {
    if (!drawablesDirty_) return;
    drawablesDirty_ = false;
    drawables_.clear();
    stats_.meshed = 0;
    stats_.triangles = 0;
    for (const auto& [k, g] : gpu_)
        if (g.model >= 0) {
            SceneRenderer::ExtraModel x;
            x.model = g.model;
            drawables_.push_back(x);
            ++stats_.meshed;
            if (renderer_) stats_.triangles += renderer_->modelTriangles(g.model);
        }
}

} // namespace titan
