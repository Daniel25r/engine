#pragma once
// Generación de geometría de las primitivas (sin GPU, por eso vive en el Core y se puede probar).
// Todas caben en un cubo de 1x1x1 centrado en el origen. Triángulos en sentido antihorario vistos desde fuera.
#include <cstdint>
#include <vector>

#include "titan/core/Components.h"

namespace titan {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;
};

MeshData buildPrimitive(MeshPrimitive type);

} // namespace titan
