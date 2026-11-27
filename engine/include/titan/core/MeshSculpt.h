#pragma once
// Escultura sobre mallas editables (como el Sculpt Mode de Blender, versión inicial): pinceles que mueven los
// vértices dentro de un radio con caída suave. Para esculpir detalle hace falta densidad: subdividir antes.
#include <vector>

#include "titan/core/EditMesh.h"

namespace titan {

enum class MeshBrush { Draw, Inflate, Smooth, Flatten, Pinch, Grab };
const char* meshBrushName(MeshBrush b);

struct MeshRayHit {
    int face = -1;
    float distance = 0;
    Vec3 point, normal;
};
// Rayo contra los triángulos de la malla (espacio local de la malla). Devuelve la cara más cercana.
bool raycastEditMesh(const EditMesh& m, Vec3 origin, Vec3 dir, MeshRayHit& hit);

// Datos de un trazo: vecinos y normales se calculan una vez al empezarlo (rápido en cada paso).
struct SculptStroke {
    std::vector<std::vector<int>> neighbors;
    std::vector<Vec3> normals;      // normales suaves por vértice al empezar
    std::vector<Vec3> startPositions; // para Agarrar
    Vec3 startCenter;
    void begin(const EditMesh& m, Vec3 center);
};

// Un paso del pincel en center (espacio local). strength: unidades por paso (Dibujar, Inflar, Pellizcar) o
// fracción 0..1 (Suavizar, Aplanar). invert: hundir en lugar de levantar. grabDelta: desplazamiento de Agarrar
// desde el inicio del trazo. Devuelve cuántos vértices se movieron.
int sculptMesh(EditMesh& m, SculptStroke& stroke, MeshBrush brush, Vec3 center, float radius, float strength, bool invert,
               Vec3 grabDelta = {});

// Exportar a OBJ (texto) con normales y UV, y su MTL con los colores de los materiales.
bool writeObj(const MeshBuild& mesh, const std::string& objPath, const std::string& mtlFileName, std::string* error = nullptr);

} // namespace titan
