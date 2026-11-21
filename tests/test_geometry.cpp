#include "Test.h"
#include "titan/core/Geometry.h"

using namespace titan;

// Para cada triángulo: el sentido antihorario debe dar una normal que apunte hacia fuera,
// y las normales de los vértices deben coincidir con ella. Si no, el back-face culling
// borraría caras visibles.
TEST("geometría: primitivas cerradas, antihorarias y con normales hacia fuera") {
    const MeshPrimitive all[] = {MeshPrimitive::Cube, MeshPrimitive::Sphere, MeshPrimitive::Plane,
                                 MeshPrimitive::Cylinder, MeshPrimitive::Cone, MeshPrimitive::Capsule};
    for (MeshPrimitive p : all) {
        const MeshData m = buildPrimitive(p);
        CHECK(!m.indices.empty());
        CHECK(m.indices.size() % 3 == 0);
        int bad = 0, degenerate = 0;
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            const MeshVertex& a = m.vertices[m.indices[i]];
            const MeshVertex& b = m.vertices[m.indices[i + 1]];
            const MeshVertex& c = m.vertices[m.indices[i + 2]];
            const Vec3 face = cross(b.position - a.position, c.position - a.position);
            if (face.length() < 1e-7f) { ++degenerate; continue; } // polos de la esfera
            const Vec3 avg = a.normal + b.normal + c.normal;
            if (dot(face, avg) <= 0) ++bad;
        }
        CHECK_EQ(bad, 0);
        for (const auto& v : m.vertices) {
            CHECK(std::fabs(v.position.x) <= 0.5001f && std::fabs(v.position.y) <= 0.5001f && std::fabs(v.position.z) <= 0.5001f);
            CHECK_NEAR(v.normal.length(), 1.0, 1e-3);
        }
        CHECK(degenerate < static_cast<int>(m.indices.size() / 3));
    }
}
