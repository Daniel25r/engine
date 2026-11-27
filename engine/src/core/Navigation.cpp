#include "titan/core/Navigation.h"

#include "titan/core/Bounds.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace titan {

void NavGrid::setGrid(const NavSettings& s, int w, int d, std::vector<uint8_t> walkable, std::vector<float> heights) {
    s_ = s;
    w_ = w;
    d_ = d;
    walkable_ = std::move(walkable);
    height_ = std::move(heights);
}

int NavGrid::walkableCells() const {
    int n = 0;
    for (uint8_t v : walkable_) n += v ? 1 : 0;
    return n;
}

void NavGrid::bake(const PhysicsApi& physics, const NavSettings& s, const std::vector<Entity>& ignore) {
    s_ = s;
    w_ = std::max(1, static_cast<int>(std::ceil(s.size.x / s.cellSize)));
    d_ = std::max(1, static_cast<int>(std::ceil(s.size.y / s.cellSize)));
    walkable_.assign(size_t(w_) * d_, 0);
    height_.assign(size_t(w_) * d_, 0);
    const float cosSlope = std::cos(s.maxSlope * kDegToRad);
    const Entity skip = ignore.empty() ? Entity{} : ignore.front();
    for (int z = 0; z < d_; ++z)
        for (int x = 0; x < w_; ++x) {
            const Vec3 c = cellCenter(x, z);
            RaycastHit hit;
            if (!physics.raycastMasked({c.x, s.topY, c.z}, {0, -1, 0}, s.topY - s.bottomY, hit, s.layerMask, skip)) continue;
            if (hit.normal.y < cosSlope) continue; // demasiado inclinado
            height_[size_t(z) * w_ + x] = hit.point.y;
            // ¿Cabe un personaje de pie? Esfera a la altura de la cabeza y otra a media altura.
            bool clear = true;
            for (float hh : {s.agentHeight * 0.5f, s.agentHeight - s.agentRadius}) {
                for (Entity e : physics.overlapSphere(hit.point + Vec3{0, std::max(hh, s.agentRadius + 0.05f), 0}, s.agentRadius,
                                                      s.layerMask))
                    if (std::find(ignore.begin(), ignore.end(), e) == ignore.end()) clear = false;
            }
            walkable_[size_t(z) * w_ + x] = clear ? 1 : 0;
        }
}

Vec3 NavGrid::cellCenter(int x, int z) const {
    const float x0 = s_.center.x - s_.size.x * 0.5f, z0 = s_.center.z - s_.size.y * 0.5f;
    const float h = height_.empty() ? s_.center.y : height_[size_t(std::clamp(z, 0, d_ - 1)) * w_ + std::clamp(x, 0, w_ - 1)];
    return {x0 + (x + 0.5f) * s_.cellSize, h, z0 + (z + 0.5f) * s_.cellSize};
}

bool NavGrid::cellOf(const Vec3& p, int& x, int& z) const {
    const float x0 = s_.center.x - s_.size.x * 0.5f, z0 = s_.center.z - s_.size.y * 0.5f;
    x = static_cast<int>(std::floor((p.x - x0) / s_.cellSize));
    z = static_cast<int>(std::floor((p.z - z0) / s_.cellSize));
    return x >= 0 && z >= 0 && x < w_ && z < d_;
}

bool NavGrid::isWalkable(const Vec3& p) const {
    int x, z;
    return cellOf(p, x, z) && walkable_[size_t(z) * w_ + x];
}

bool NavGrid::nearestWalkable(const Vec3& p, Vec3& out, int r) const {
    int cx, cz;
    cellOf(p, cx, cz);
    float best = 1e30f;
    bool found = false;
    for (int z = cz - r; z <= cz + r; ++z)
        for (int x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || z < 0 || x >= w_ || z >= d_ || !walkable_[size_t(z) * w_ + x]) continue;
            const float d = float((x - cx) * (x - cx) + (z - cz) * (z - cz));
            if (d < best) {
                best = d;
                out = cellCenter(x, z);
                found = true;
            }
        }
    return found;
}

bool NavGrid::lineWalkable(int x0, int z0, int x1, int z1) const {
    // Recorre las celdas de la línea (Bresenham) y comprueba que todas se pueden pisar sin escalones grandes.
    int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0), sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1, err = dx - dz;
    int x = x0, z = z0;
    float prevH = height_[size_t(z) * w_ + x];
    while (true) {
        if (!walkable_[size_t(z) * w_ + x]) return false;
        const float h = height_[size_t(z) * w_ + x];
        if (std::fabs(h - prevH) > s_.maxStep) return false;
        prevH = h;
        if (x == x1 && z == z1) return true;
        const int e2 = 2 * err;
        if (e2 > -dz) { err -= dz; x += sx; }
        if (e2 < dx) { err += dx; z += sz; }
    }
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& path) const {
    path.clear();
    if (!baked()) return false;
    Vec3 a = from, b = to;
    int sx, sz, gx, gz;
    if (!cellOf(a, sx, sz) || !walkable_[size_t(sz) * w_ + sx]) {
        if (!nearestWalkable(from, a)) return false;
        cellOf(a, sx, sz);
    }
    if (!cellOf(b, gx, gz) || !walkable_[size_t(gz) * w_ + gx]) {
        if (!nearestWalkable(to, b)) return false;
        cellOf(b, gx, gz);
    }
    // A* en 8 direcciones (sin cortar esquinas) con distancia octil como heurística.
    const size_t n = size_t(w_) * d_;
    std::vector<float> g(n, 1e30f);
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    auto idx = [&](int x, int z) { return z * w_ + x; };
    auto heur = [&](int x, int z) {
        const float dx = float(std::abs(x - gx)), dz = float(std::abs(z - gz));
        return (dx + dz) + (1.41421356f - 2) * std::min(dx, dz);
    };
    const int start = idx(sx, sz), goal = idx(gx, gz);
    g[size_t(start)] = 0;
    open.push({heur(sx, sz), start});
    int expanded = 0;
    while (!open.empty()) {
        const int cur = open.top().second;
        open.pop();
        if (closed[size_t(cur)]) continue;
        closed[size_t(cur)] = 1;
        if (cur == goal) break;
        if (++expanded > 200000) return false;
        const int cx = cur % w_, cz = cur / w_;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                const int nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= w_ || nz >= d_) continue;
                const int ni = idx(nx, nz);
                if (!walkable_[size_t(ni)] || closed[size_t(ni)]) continue;
                if (dx && dz && (!walkable_[size_t(idx(cx + dx, cz))] || !walkable_[size_t(idx(cx, cz + dz))])) continue;
                if (std::fabs(height_[size_t(ni)] - height_[size_t(cur)]) > s_.maxStep) continue;
                const float cost = g[size_t(cur)] + ((dx && dz) ? 1.41421356f : 1.0f);
                if (cost < g[size_t(ni)]) {
                    g[size_t(ni)] = cost;
                    parent[size_t(ni)] = cur;
                    open.push({cost + heur(nx, nz), ni});
                }
            }
    }
    if (!closed[size_t(goal)]) return false;
    std::vector<int> cells;
    for (int c = goal; c != -1; c = parent[size_t(c)]) cells.push_back(c);
    std::reverse(cells.begin(), cells.end());
    // Suavizado: desde cada punto, saltar al más lejano que se ve en línea recta caminable.
    std::vector<int> smooth{cells.front()};
    size_t i = 0;
    while (i + 1 < cells.size()) {
        size_t j = cells.size() - 1;
        while (j > i + 1 && !lineWalkable(cells[i] % w_, cells[i] / w_, cells[j] % w_, cells[j] / w_)) --j;
        smooth.push_back(cells[j]);
        i = j;
    }
    for (size_t k = 0; k < smooth.size(); ++k) path.push_back(cellCenter(smooth[k] % w_, smooth[k] / w_));
    if (!path.empty()) path.back() = Vec3{b.x, path.back().y, b.z};
    return true;
}

void updateNavAgents(Scene& scene, const NavGrid& grid, float dt) {
    World& w = scene.world();
    w.each<NavAgent>([&](Entity e, NavAgent& a) {
        CharacterController* cc = w.get<CharacterController>(e);
        auto stop = [&] {
            if (cc) cc->desiredVelocity = {};
        };
        if (!a.hasDestination || !scene.isActiveInHierarchy(e)) {
            stop();
            return;
        }
        const Vec3 pos = scene.computeWorldMatrix(e).translation();
        if (!a.follow.isNull()) {
            const Entity t = scene.findById(a.follow);
            if (t.isNull()) {
                a.hasDestination = false;
                stop();
                return;
            }
            a.destination = scene.computeWorldMatrix(t).translation();
        }
        const Vec3 toGoal{a.destination.x - pos.x, 0, a.destination.z - pos.z};
        if (toGoal.length() <= a.stoppingDistance) {
            a.reached = true;
            stop();
            return;
        }
        a.reached = false;
        a.repathTimer -= dt;
        if (a.path.empty() || a.repathTimer <= 0) {
            a.repathTimer = std::max(0.05f, a.repathInterval);
            a.pathFailed = !grid.findPath(pos, a.destination, a.path);
            a.next = a.path.size() > 1 ? 1 : 0;
        }
        if (a.pathFailed || a.path.empty()) {
            stop();
            return;
        }
        while (a.next < a.path.size()) {
            const Vec3 d{a.path[a.next].x - pos.x, 0, a.path[a.next].z - pos.z};
            if (d.length() > 0.35f || a.next + 1 >= a.path.size()) break;
            ++a.next;
        }
        const Vec3 target = a.path[std::min(a.next, a.path.size() - 1)];
        Vec3 dir{target.x - pos.x, 0, target.z - pos.z};
        const float len = dir.length();
        if (len < 1e-4f) return;
        dir = dir * (1.0f / len);
        if (cc) {
            cc->desiredVelocity = dir * a.speed;
        } else { // sin física: desliza y mira hacia donde va
            Transform& t = scene.transform(e);
            t.translate(dir * std::min(a.speed * dt, len));
            const Vec3 eul = t.eulerDegrees();
            t.setEulerDegrees({eul.x, std::atan2(-dir.x, -dir.z) * kRadToDeg, eul.z});
        }
    });
}

NavSettings navSettingsForScene(Scene& scene, float cellSize) {
    AABB box;
    World& w = scene.world();
    auto add = [&](Entity e, const AABB& local) { box.expand(transformAABB(local, scene.computeWorldMatrix(e)).min);
                                                  box.expand(transformAABB(local, scene.computeWorldMatrix(e)).max); };
    w.each<Collider>([&](Entity e, Collider& c) {
        if (c.isTrigger) return;
        add(e, {c.center - c.size * 0.5f, c.center + c.size * 0.5f});
    });
    w.each<Terrain>([&](Entity e, Terrain& t) {
        add(e, {{-t.size * 0.5f, 0, -t.size * 0.5f}, {t.size * 0.5f, t.maxHeight, t.size * 0.5f}});
    });
    NavSettings s;
    s.cellSize = cellSize;
    if (box.valid()) {
        s.center = box.center();
        s.size = {std::min(240.0f, box.max.x - box.min.x + 2), std::min(240.0f, box.max.z - box.min.z + 2)};
        s.topY = box.max.y + 5;
        s.bottomY = box.min.y - 5;
    }
    return s;
}

} // namespace titan
