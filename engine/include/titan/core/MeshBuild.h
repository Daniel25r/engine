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
    uint16_t joints[4] = {0, 0, 0, 0}; // solo con esqueleto (MeshBuild::bones)
    float weights[4] = {0, 0, 0, 0};
};

// Esqueleto y animación (opcionales): se escriben como skin y animaciones glTF.
struct MeshBuildBone {
    std::string name;
    int parent = -1;
    Vec3 restLocal; // posición de reposo respecto al padre (o a la malla si es raíz)
    Vec3 restWorld; // cabeza del hueso en el espacio de la malla (para la matriz inversa de reposo)
    // Pose de reposo completa (huesos importados de FBX/Mixamo, que tienen rotación y escala). Los del rig de
    // Titán no la usan: rotación identidad y matriz inversa = trasladar(-restWorld).
    Quat restRotation;
    Vec3 restScale{1, 1, 1};
    bool hasInverseBind = false;
    float inverseBind[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct MeshBuildChannel {
    int bone = 0;
    int path = 0; // 0 traslación (vec3), 1 rotación (quat x, y, z, w), 2 escala (vec3)
    std::vector<float> times, values;
};
struct MeshBuildClip {
    std::string name;
    std::vector<MeshBuildChannel> channels;
};

struct MeshBuildPart {
    int material = 0;
    std::vector<MeshBuildVertex> vertices;
    std::vector<uint32_t> indices; // triángulos
};

struct MeshBuild {
    std::vector<MeshBuildMaterial> materials;
    std::vector<MeshBuildPart> parts;
    std::vector<MeshBuildBone> bones; // vacío = malla sin esqueleto
    std::vector<MeshBuildClip> clips;
    size_t triangleCount() const;
    // Normales por cara (plano) a partir de las posiciones, para formatos que no traen normales.
    void computeFlatNormals();
};

// glTF binario (.glb) con una malla por parte del mismo nodo. false si no hay geometría.
bool writeGlb(const MeshBuild& mesh, std::vector<uint8_t>& out, std::string* error = nullptr);

} // namespace titan
