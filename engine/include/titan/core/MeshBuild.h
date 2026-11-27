#pragma once
// Malla lista para guardar como GLB: lo que producen los importadores (OBJ, STL, PLY) y la malla editable.
// GlbWriter la escribe en glTF binario, que el motor ya sabe cargar, animar y dibujar.
#include <cstdint>
#include <string>
#include <vector>

#include "titan/core/Math.h"

namespace titan {

struct MeshBuildMaterial {
    std::string name = "Material";
    Color color{0.8f, 0.8f, 0.8f, 1};   // color base (lineal en glTF; se escribe tal cual)
    float metallic = 0, roughness = 0.6f;
    std::vector<uint8_t> image;         // textura de color (PNG o JPEG) incrustada; vacía = sin textura
    std::string imageMime;              // "image/png" o "image/jpeg"
};

struct MeshBuildVertex {
    Vec3 position, normal;
    Vec2 uv;
};

struct MeshBuildPart {
    int material = 0;
    std::vector<MeshBuildVertex> vertices;
    std::vector<uint32_t> indices; // triángulos
};

struct MeshBuild {
    std::vector<MeshBuildMaterial> materials;
    std::vector<MeshBuildPart> parts;
    size_t triangleCount() const;
    // Normales por cara (plano) a partir de las posiciones, para formatos que no traen normales.
    void computeFlatNormals();
};

// glTF binario (.glb) con una malla por parte del mismo nodo. false si no hay geometría.
bool writeGlb(const MeshBuild& mesh, std::vector<uint8_t>& out, std::string* error = nullptr);

} // namespace titan
