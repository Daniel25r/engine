#include "titan/core/Bounds.h"

#include <algorithm>
#include <cmath>

namespace titan {

void AABB::expand(const Vec3& p) {
    min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
    max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
}

AABB AABB::inflated(float fraction) const {
    if (!valid()) return *this;
    const Vec3 grow = (max - min) * fraction;
    return {min - grow, max + grow};
}

AABB transformAABB(const AABB& local, const Mat4& w) {
    if (!local.valid()) return local;
    // Centro transformado + semiejes proyectados con el valor absoluto de la parte 3x3.
    const Vec3 c = w.transformPoint(local.center());
    const Vec3 e = local.extents();
    const float* m = w.m; // columnas: m[0..2] eje X, m[4..6] eje Y, m[8..10] eje Z
    const Vec3 r{std::fabs(m[0]) * e.x + std::fabs(m[4]) * e.y + std::fabs(m[8]) * e.z,
                 std::fabs(m[1]) * e.x + std::fabs(m[5]) * e.y + std::fabs(m[9]) * e.z,
                 std::fabs(m[2]) * e.x + std::fabs(m[6]) * e.y + std::fabs(m[10]) * e.z};
    return {c - r, c + r};
}

Frustum Frustum::fromMatrix(const Mat4& vp) {
    // Gribb y Hartmann: cada plano es la fila 4 más o menos otra fila. Matriz guardada por columnas.
    auto row = [&](int i, float out[4]) {
        out[0] = vp.m[i];
        out[1] = vp.m[4 + i];
        out[2] = vp.m[8 + i];
        out[3] = vp.m[12 + i];
    };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0);
    row(1, r1);
    row(2, r2);
    row(3, r3);
    Frustum f;
    for (int k = 0; k < 4; ++k) {
        f.planes[0][k] = r3[k] + r0[k]; // izquierda
        f.planes[1][k] = r3[k] - r0[k]; // derecha
        f.planes[2][k] = r3[k] + r1[k]; // abajo
        f.planes[3][k] = r3[k] - r1[k]; // arriba
        f.planes[4][k] = r3[k] + r2[k]; // cerca
        f.planes[5][k] = r3[k] - r2[k]; // lejos
    }
    for (auto& p : f.planes) {
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len > 1e-12f)
            for (float& v : p) v /= len;
    }
    return f;
}

bool Frustum::intersects(const AABB& b) const {
    if (!b.valid()) return false;
    for (const auto& p : planes) {
        // Vértice de la caja más adentro según la normal del plano: si está fuera, toda la caja lo está.
        const float x = p[0] >= 0 ? b.max.x : b.min.x;
        const float y = p[1] >= 0 ? b.max.y : b.min.y;
        const float z = p[2] >= 0 ? b.max.z : b.min.z;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
    }
    return true;
}

bool Frustum::containsPoint(const Vec3& v) const {
    for (const auto& p : planes)
        if (p[0] * v.x + p[1] * v.y + p[2] * v.z + p[3] < 0) return false;
    return true;
}

} // namespace titan
