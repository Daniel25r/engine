#pragma once
// Malla editable (modelado): vértices compartidos y caras poligonales (triángulos, cuadriláteros o n-gonos), como el
// modo edición de Blender. Las operaciones trabajan sobre índices de caras o de vértices; build() la convierte en
// triángulos con normales y UV para guardarla como GLB y usarla en el juego. Se guarda como ".tmesh" (JSON).
//
// Convención: caras en sentido antihorario vistas desde fuera (normal hacia fuera), igual que glTF.
#include <set>
#include <string>
#include <vector>

#include "titan/core/MeshBuild.h"
#include "titan/core/Rig.h"

namespace titan {

struct EditFace {
    std::vector<int> v;   // índices de vértices, en orden
    std::vector<Vec2> uv; // UV por esquina (misma longitud que v) o vacío: proyección automática al construir
    int material = 0;
};

class EditMesh {
public:
    std::vector<Vec3> verts;
    std::vector<EditFace> faces;
    std::vector<MeshBuildMaterial> materials{MeshBuildMaterial{}};
    bool smooth = false; // sombreado suave (normales promediadas) o plano
    Rig rig;             // esqueleto, pesos y animaciones (opcional). Si la malla cambia de vértices, los pesos se
                         // recalculan solos al construir.

    // ---- Primitivas (centradas en el origen)
    static EditMesh cube(float size = 1);
    static EditMesh plane(float size = 1, int divisions = 1);
    static EditMesh cylinder(float radius = 0.5f, float height = 1, int segments = 16);
    static EditMesh cone(float radius = 0.5f, float height = 1, int segments = 16);
    static EditMesh uvSphere(float radius = 0.5f, int segments = 16, int rings = 8);

    // ---- Consultas
    Vec3 faceCenter(int f) const;
    Vec3 faceNormal(int f) const;          // Newell: válida también para n-gonos no planos
    std::vector<int> faceVertices(const std::set<int>& faces) const;
    bool valid(std::string* why = nullptr) const; // índices dentro de rango, caras de 3+ vértices sin repetir
    // Aristas de borde (las que solo usa una cara): una malla cerrada no tiene ninguna.
    size_t boundaryEdgeCount() const;

    // ---- Transformar una selección de vértices
    void translate(const std::vector<int>& vs, Vec3 delta);
    void scale(const std::vector<int>& vs, Vec3 pivot, Vec3 factor);
    void rotate(const std::vector<int>& vs, Vec3 pivot, const Quat& q);

    // ---- Operaciones de modelado. Devuelven las caras nuevas o resultantes cuando tiene sentido.
    // Extruir una región de caras a lo largo de su normal media (como la E de Blender): crea las paredes laterales.
    std::vector<int> extrude(const std::set<int>& faces, float distance);
    // Inset por cara: un anillo de cuadriláteros y la cara interior reducida (amount 0..1 hacia el centro).
    std::vector<int> inset(const std::set<int>& faces, float amount);
    // Subdivisión: cada cara se parte en cuadriláteros (centro + puntos medios). smooth = Catmull-Clark.
    void subdivide(bool smoothSubdivision);
    void deleteFaces(const std::set<int>& faces);   // y los vértices que queden sueltos
    void deleteVertices(const std::set<int>& vs);   // y las caras que los usaban
    void flipNormals(const std::set<int>& faces);   // vacío = todas
    int mergeByDistance(float distance);            // devuelve cuántos vértices se fusionaron
    // Simetría: duplica la malla reflejada en el plano axis = 0 (0 X, 1 Y, 2 Z) y fusiona la costura.
    void mirror(int axis);
    void removeUnusedVertices();

    // Triángulos con normales (planas o suaves) y UV (las de la cara o proyección por caja) para guardar como GLB.
    MeshBuild build() const;

    std::string toJson() const;
    static bool fromJson(const std::string& text, EditMesh& out, std::string* error = nullptr);
};

} // namespace titan
