#pragma once
// Volúmenes de contorno y frustum de cámara para decidir qué se dibuja (culling).
// Sin GPU: vive en el Core para poder probarlo y usarlo en render, física, LOD y streaming.
#include "titan/core/Math.h"

namespace titan {

// Caja alineada con los ejes. Vacía (min > max) hasta que se le añade un punto.
struct AABB {
    Vec3 min{1e30f, 1e30f, 1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    AABB() = default;
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    void expand(const Vec3& p);
    // Crece por cada lado una fracción de su tamaño (p. ej. personajes animados que salen de su pose de reposo).
    AABB inflated(float fraction) const;
};

// Caja que contiene a "local" tras aplicarle la matriz (exacta para la caja, método de Arvo).
AABB transformAABB(const AABB& local, const Mat4& world);

// Seis planos (izquierda, derecha, abajo, arriba, cerca, lejos) con la normal hacia dentro.
struct Frustum {
    float planes[6][4] = {};

    // A partir de proyección * vista (convención de OpenGL: z de recorte entre -w y w).
    static Frustum fromMatrix(const Mat4& viewProjection);
    // false solo si la caja está seguro fuera (puede dar true para alguna caja que roza una esquina).
    bool intersects(const AABB& box) const;
    bool containsPoint(const Vec3& p) const;
};

} // namespace titan
