#include "titan/core/Geometry.h"

#include <cmath>

namespace titan {

namespace {

void addQuad(MeshData& m, Vec3 center, Vec3 right, Vec3 up, Vec3 normal) {
    const uint16_t b = static_cast<uint16_t>(m.vertices.size());
    m.vertices.push_back({center - right - up, normal, {0, 1}});
    m.vertices.push_back({center + right - up, normal, {1, 1}});
    m.vertices.push_back({center + right + up, normal, {1, 0}});
    m.vertices.push_back({center - right + up, normal, {0, 0}});
    m.indices.insert(m.indices.end(), {b, uint16_t(b + 1), uint16_t(b + 2), b, uint16_t(b + 2), uint16_t(b + 3)});
}

MeshData cube() {
    MeshData m;
    const float h = 0.5f;
    addQuad(m, {0, 0, h}, {h, 0, 0}, {0, h, 0}, {0, 0, 1});
    addQuad(m, {0, 0, -h}, {-h, 0, 0}, {0, h, 0}, {0, 0, -1});
    addQuad(m, {h, 0, 0}, {0, 0, -h}, {0, h, 0}, {1, 0, 0});
    addQuad(m, {-h, 0, 0}, {0, 0, h}, {0, h, 0}, {-1, 0, 0});
    addQuad(m, {0, h, 0}, {h, 0, 0}, {0, 0, -h}, {0, 1, 0});
    addQuad(m, {0, -h, 0}, {h, 0, 0}, {0, 0, h}, {0, -1, 0});
    return m;
}

MeshData plane() {
    MeshData m;
    addQuad(m, {0, 0, 0}, {0.5f, 0, 0}, {0, 0, -0.5f}, {0, 1, 0});
    return m;
}

// Esfera (cylinderHalf = 0) o cápsula (los hemisferios se separan ±cylinderHalf en Y).
MeshData sphereLike(float radius, float cylinderHalf) {
    MeshData m;
    const int rings = 16, segments = 32;
    // Con cápsula se duplica el anillo del ecuador: uno para cada hemisferio.
    for (int pass = 0; pass < (cylinderHalf > 0 ? 2 : 1); ++pass) {
        const int first = cylinderHalf > 0 ? (pass == 0 ? 0 : rings / 2) : 0;
        const int last = cylinderHalf > 0 ? (pass == 0 ? rings / 2 : rings) : rings;
        const float offset = cylinderHalf > 0 ? (pass == 0 ? cylinderHalf : -cylinderHalf) : 0.0f;
        const uint16_t base = static_cast<uint16_t>(m.vertices.size());
        for (int r = first; r <= last; ++r) {
            const float phi = kPi * r / rings; // 0 = polo norte
            for (int s = 0; s <= segments; ++s) {
                const float theta = 2.0f * kPi * s / segments;
                const Vec3 n{std::sin(phi) * std::sin(theta), std::cos(phi), std::sin(phi) * std::cos(theta)};
                m.vertices.push_back({n * radius + Vec3{0, offset, 0}, n, {float(s) / segments, float(r) / rings}});
            }
        }
        const int count = last - first;
        for (int r = 0; r < count; ++r)
            for (int s = 0; s < segments; ++s) {
                const uint16_t a = uint16_t(base + r * (segments + 1) + s), b = uint16_t(a + segments + 1);
                m.indices.insert(m.indices.end(), {a, b, uint16_t(a + 1), uint16_t(a + 1), b, uint16_t(b + 1)});
            }
    }
    if (cylinderHalf > 0) {
        // Une el ecuador superior con el inferior (tramo cilíndrico).
        const uint16_t top = uint16_t((rings / 2) * (segments + 1));
        const uint16_t bottom = uint16_t((rings / 2 + 1) * (segments + 1));
        for (int s = 0; s < segments; ++s) {
            const uint16_t a = uint16_t(top + s), b = uint16_t(bottom + s);
            m.indices.insert(m.indices.end(), {a, b, uint16_t(a + 1), uint16_t(a + 1), b, uint16_t(b + 1)});
        }
    }
    return m;
}

// Cilindro (topRadius = radio) o cono (topRadius = 0), altura 1.
MeshData tube(float topRadius) {
    MeshData m;
    const int segments = 32;
    const float r = 0.5f, h = 0.5f;
    const float slope = (r - topRadius) / (2 * h); // inclina la normal del lateral en el cono
    for (int s = 0; s <= segments; ++s) {
        const float t = 2.0f * kPi * s / segments;
        const Vec3 dir{std::sin(t), 0, std::cos(t)};
        const Vec3 n = Vec3{dir.x, slope, dir.z}.normalized();
        m.vertices.push_back({dir * topRadius + Vec3{0, h, 0}, n, {float(s) / segments, 0}});
        m.vertices.push_back({dir * r + Vec3{0, -h, 0}, n, {float(s) / segments, 1}});
    }
    for (int s = 0; s < segments; ++s) {
        const uint16_t a = uint16_t(s * 2), b = uint16_t(a + 1), c = uint16_t(a + 2), d = uint16_t(a + 3);
        m.indices.insert(m.indices.end(), {a, b, c, c, b, d});
    }
    auto cap = [&](float y, float radius, float ny) {
        const uint16_t center = static_cast<uint16_t>(m.vertices.size());
        m.vertices.push_back({{0, y, 0}, {0, ny, 0}, {0.5f, 0.5f}});
        for (int s = 0; s <= segments; ++s) {
            const float t = 2.0f * kPi * s / segments;
            m.vertices.push_back({{std::sin(t) * radius, y, std::cos(t) * radius}, {0, ny, 0},
                                  {0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::cos(t)}});
        }
        for (int s = 0; s < segments; ++s) {
            const uint16_t a = uint16_t(center + 1 + s), b = uint16_t(a + 1);
            if (ny > 0) m.indices.insert(m.indices.end(), {center, a, b});
            else m.indices.insert(m.indices.end(), {center, b, a});
        }
    };
    if (topRadius > 0) cap(h, topRadius, 1);
    cap(-h, r, -1);
    return m;
}

} // namespace

MeshData buildPrimitive(MeshPrimitive type) {
    switch (type) {
    case MeshPrimitive::Cube: return cube();
    case MeshPrimitive::Sphere: return sphereLike(0.5f, 0);
    case MeshPrimitive::Plane: return plane();
    case MeshPrimitive::Cylinder: return tube(0.5f);
    case MeshPrimitive::Cone: return tube(0);
    case MeshPrimitive::Capsule: return sphereLike(0.25f, 0.25f);
    }
    return cube();
}

} // namespace titan
