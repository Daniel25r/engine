// Modelado e importación: cada operación se comprueba con propiedades geométricas (malla cerrada, Euler, caras
// hacia fuera, posiciones) y el GLB escrito se valida con cgltf, el mismo lector que usa el motor.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "Test.h"
#include "cgltf.h"
#include "titan/core/EditMesh.h"
#include "titan/core/ModelImport.h"

using namespace titan;
namespace fs = std::filesystem;

namespace {

size_t edgeCount(const EditMesh& m) {
    std::set<std::pair<int, int>> e;
    for (const EditFace& f : m.faces)
        for (size_t i = 0; i < f.v.size(); ++i) {
            const int a = f.v[i], b = f.v[(i + 1) % f.v.size()];
            e.insert({std::min(a, b), std::max(a, b)});
        }
    return e.size();
}
int euler(const EditMesh& m) { return int(m.verts.size()) - int(edgeCount(m)) + int(m.faces.size()); }
bool allOutward(const EditMesh& m) {
    for (size_t f = 0; f < m.faces.size(); ++f)
        if (dot(m.faceNormal(int(f)), m.faceCenter(int(f))) <= 0) return false;
    return true;
}
std::string tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / ("titan_modelado_" + std::string(name));
    fs::remove_all(d);
    fs::create_directories(d);
    return d.string();
}
void writeText(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

// El GLB lo valida cgltf (estructura, accesores, límites del búfer) y se cuentan los triángulos que contiene.
size_t glbTriangles(const std::vector<uint8_t>& glb, bool& valid) {
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    valid = cgltf_parse(&opt, glb.data(), glb.size(), &data) == cgltf_result_success &&
            cgltf_load_buffers(&opt, data, nullptr) == cgltf_result_success && cgltf_validate(data) == cgltf_result_success;
    size_t tris = 0;
    if (data)
        for (size_t i = 0; i < data->meshes_count; ++i)
            for (size_t p = 0; p < data->meshes[i].primitives_count; ++p) tris += data->meshes[i].primitives[p].indices->count / 3;
    cgltf_free(data);
    return tris;
}

} // namespace

TEST("modelado: primitivas cerradas, válidas y con las caras hacia fuera") {
    for (const EditMesh& m : {EditMesh::cube(), EditMesh::cylinder(0.5f, 1, 12), EditMesh::cone(0.5f, 1, 10), EditMesh::uvSphere(0.5f, 12, 6)}) {
        std::string why;
        CHECK(m.valid(&why));
        CHECK_EQ(m.boundaryEdgeCount(), 0u); // cerradas: toda arista tiene dos caras
        CHECK_EQ(euler(m), 2);               // esfera topológica: V - A + C = 2
        CHECK(allOutward(m));
    }
    const EditMesh p = EditMesh::plane(2, 3);
    CHECK_EQ(p.faces.size(), 9u);
    CHECK_EQ(p.verts.size(), 16u);
    CHECK(p.faceNormal(0).y > 0.99f);
}

TEST("modelado: extruir la cara de arriba de un cubo crea las paredes y sigue cerrado") {
    EditMesh m = EditMesh::cube(1);
    int top = -1;
    for (size_t f = 0; f < m.faces.size(); ++f)
        if (m.faceNormal(int(f)).y > 0.9f) top = int(f);
    CHECK(top >= 0);
    m.extrude({top}, 1.0f);
    CHECK_EQ(m.verts.size(), 12u);
    CHECK_EQ(m.faces.size(), 10u);
    CHECK_EQ(m.boundaryEdgeCount(), 0u);
    CHECK_EQ(euler(m), 2);
    CHECK_NEAR(m.faceCenter(top).y, 1.5f, 1e-5f);
    CHECK(allOutward(m));
    // Región de dos caras vecinas: una sola pared por el borde exterior, ninguna entre ellas.
    EditMesh g = EditMesh::plane(2, 2);
    g.extrude({0, 1}, 0.5f);
    CHECK_EQ(g.faces.size(), 4u + 6u); // 2 caras × 2 aristas exteriores + 2 extremos = 6 paredes
}

TEST("modelado: inset, subdivisión lineal y Catmull-Clark") {
    EditMesh m = EditMesh::cube(1);
    m.inset({0}, 0.3f);
    CHECK_EQ(m.faces.size(), 10u);
    CHECK_EQ(m.boundaryEdgeCount(), 0u);
    CHECK(allOutward(m));

    EditMesh p = EditMesh::plane(1, 1);
    p.subdivide(false);
    CHECK_EQ(p.faces.size(), 4u);
    CHECK_EQ(p.verts.size(), 9u);
    for (const Vec3& v : p.verts) CHECK_NEAR(v.y, 0.0f, 1e-6f);

    EditMesh c = EditMesh::cube(1);
    c.subdivide(true);
    c.subdivide(true);
    CHECK_EQ(c.faces.size(), 96u); // 6 × 4 × 4
    CHECK_EQ(c.boundaryEdgeCount(), 0u);
    CHECK_EQ(euler(c), 2);
    CHECK(allOutward(c));
    // Catmull-Clark redondea: todos los puntos quedan dentro del cubo y lejos de las esquinas.
    float minR = 1e9f, maxR = 0;
    for (const Vec3& v : c.verts) {
        minR = std::min(minR, v.length());
        maxR = std::max(maxR, v.length());
    }
    CHECK(maxR < 0.866f * 0.8f); // la esquina del cubo estaba a 0,866
    CHECK(minR > 0.25f);
    CHECK(c.smooth);
}

TEST("modelado: borrar, fusionar por distancia, voltear y simetría") {
    EditMesh m = EditMesh::cube(1);
    m.deleteFaces({0});
    CHECK_EQ(m.faces.size(), 5u);
    CHECK_EQ(m.boundaryEdgeCount(), 4u); // queda un agujero de 4 aristas
    EditMesh d = EditMesh::cube(1);
    d.deleteVertices({0});
    CHECK_EQ(d.faces.size(), 3u);
    CHECK_EQ(d.verts.size(), 7u);

    // Dos cubos que comparten una cara en el mismo sitio: se sueldan los 4 vértices duplicados.
    EditMesh a = EditMesh::cube(1), b = EditMesh::cube(1);
    for (Vec3& v : b.verts) v.x += 1;
    const int base = int(a.verts.size());
    a.verts.insert(a.verts.end(), b.verts.begin(), b.verts.end());
    for (EditFace f : b.faces) {
        for (int& v : f.v) v += base;
        a.faces.push_back(f);
    }
    CHECK_EQ(a.mergeByDistance(1e-4f), 4);
    CHECK_EQ(a.verts.size(), 12u);

    EditMesh f = EditMesh::cube(1);
    f.flipNormals({});
    CHECK(dot(f.faceNormal(0), f.faceCenter(0)) < 0);

    // Rejilla 2×2 de -0,5 a 0,5 en X: se borra la columna izquierda (x < 0) y se refleja la derecha.
    EditMesh half = EditMesh::plane(1, 2);
    half.deleteFaces({0, 2});
    CHECK_EQ(half.verts.size(), 6u); // 3 en la costura (x = 0) y 3 en x = 0,5
    half.mirror(0);
    CHECK_EQ(half.faces.size(), 4u);
    CHECK_EQ(half.verts.size(), 9u); // 6 + 6 - 3 soldados en la costura
    CHECK_EQ(half.boundaryEdgeCount(), 8u); // el contorno de la rejilla completa, sin raja en el centro
    for (size_t i = 0; i < half.faces.size(); ++i) CHECK(half.faceNormal(int(i)).y > 0.99f);
}

TEST("modelado: .tmesh ida y vuelta, triángulos y GLB válido para el motor") {
    EditMesh m = EditMesh::cube(2);
    m.extrude({2}, 0.5f);
    m.materials[0].color = {0.2f, 0.5f, 0.9f, 1};
    EditMesh back;
    std::string err;
    CHECK(EditMesh::fromJson(m.toJson(), back, &err));
    CHECK_EQ(back.verts.size(), m.verts.size());
    CHECK_EQ(back.faces.size(), m.faces.size());
    CHECK_NEAR(back.materials[0].color.b, 0.9f, 1e-6f);
    CHECK(!EditMesh::fromJson("{\"format\":\"otra\"}", back, &err));

    const MeshBuild mb = m.build();
    CHECK_EQ(mb.triangleCount(), m.faces.size() * 2); // todas son cuadriláteros
    std::vector<uint8_t> glb;
    CHECK(writeGlb(mb, glb, &err));
    bool valid = false;
    CHECK_EQ(glbTriangles(glb, valid), mb.triangleCount());
    CHECK(valid);
    MeshBuild empty;
    CHECK(!writeGlb(empty, glb, &err));
}

TEST("importar: OBJ con MTL, cuadriláteros, índices negativos y sin normales") {
    const std::string dir = tempDir("obj");
    writeText(dir + "/m.mtl", "newmtl Rojo\nKd 0.9 0.1 0.1\nNs 250\nnewmtl Azul\nKd 0.1 0.1 0.9\nmap_Kd falta.tga\n");
    writeText(dir + "/m.obj",
              "# cubo de Prisma 3D (simplificado)\nmtllib m.mtl\n"
              "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nv -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
              "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
              "usemtl Rojo\nf 1/1 2/2 3/3 4/4\nf -4/1 -1/2 -2/3 -3/4\n"
              "usemtl Azul\nf 4 3 7 8\nf 5 6 2 1\nf 2 6 7 3\nf 5 1 4 8\n");
    MeshBuild mb;
    ImportReport rep;
    std::string err;
    CHECK(loadObj(dir + "/m.obj", mb, &rep, &err));
    CHECK_EQ(mb.triangleCount(), 12u);
    CHECK_EQ(mb.materials.size(), 2u);
    CHECK_EQ(mb.parts.size(), 2u);
    CHECK_NEAR(mb.materials[0].color.r, 0.9f, 1e-5f);
    CHECK(mb.materials[0].roughness < 0.2f); // Ns alto = brillante
    CHECK(!rep.warnings.empty());            // la textura TGA no se puede traer: se avisa
    CHECK_NEAR(std::fabs(mb.parts[0].vertices[0].normal.length()), 1.0f, 1e-4f); // normales calculadas
    // Conversión completa a GLB y validación.
    CHECK(convertModelToGlb(dir + "/m.obj", dir + "/m.glb", &rep, &err));
    std::ifstream f(dir + "/m.glb", std::ios::binary);
    std::vector<uint8_t> glb((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    bool valid = false;
    CHECK_EQ(glbTriangles(glb, valid), 12u);
    CHECK(valid);
    writeText(dir + "/roto.obj", "v 0 0 0\nf 1 2 3\n");
    CHECK(!loadObj(dir + "/roto.obj", mb, &rep, &err)); // índice fuera de rango: error, no fallo del programa
}

TEST("importar: STL ASCII y binario (Z arriba → Y arriba) y PLY ASCII y binario") {
    const std::string dir = tempDir("stl");
    writeText(dir + "/a.stl", "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 2\nendloop\nendfacet\nendsolid t\n");
    MeshBuild mb;
    std::string err;
    CHECK(loadStl(dir + "/a.stl", mb, nullptr, &err));
    CHECK_EQ(mb.triangleCount(), 1u);
    CHECK_NEAR(mb.parts[0].vertices[2].position.y, 2.0f, 1e-6f); // la Z del STL es la Y del motor
    CHECK_NEAR(mb.parts[0].vertices[2].position.z, -1.0f, 1e-6f);
    { // binario: 2 triángulos
        std::string bin(80, ' ');
        uint32_t n = 2;
        bin.append(reinterpret_cast<char*>(&n), 4);
        for (int t = 0; t < 2; ++t) {
            float data[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, float(t)};
            bin.append(reinterpret_cast<char*>(data), 48);
            bin.append(2, '\0');
        }
        writeText(dir + "/b.stl", bin);
        CHECK(loadStl(dir + "/b.stl", mb, nullptr, &err));
        CHECK_EQ(mb.triangleCount(), 2u);
    }
    writeText(dir + "/a.ply", "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
                              "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n4 0 1 2 3\n");
    CHECK(loadPly(dir + "/a.ply", mb, nullptr, &err));
    CHECK_EQ(mb.triangleCount(), 2u);
    CHECK_NEAR(mb.parts[0].vertices[0].normal.z, 1.0f, 1e-5f);
    { // binario little endian con colores (se avisa de que no se importan)
        std::string h = "ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                        "property uchar red\nproperty uchar green\nproperty uchar blue\nelement face 1\n"
                        "property list uchar uint vertex_indices\nend_header\n";
        const float p[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 0, -1}};
        for (auto& v : p) {
            h.append(reinterpret_cast<const char*>(v), 12);
            h.append("\xff\x00\x00", 3);
        }
        h.push_back('\3');
        const uint32_t idx[3] = {0, 1, 2};
        h.append(reinterpret_cast<const char*>(idx), 12);
        writeText(dir + "/b.ply", h);
        ImportReport rep;
        CHECK(loadPly(dir + "/b.ply", mb, &rep, &err));
        CHECK_EQ(mb.triangleCount(), 1u);
        CHECK(!rep.warnings.empty());
    }
    CHECK(isConvertibleModel("x.BLEND") && isConvertibleModel("y.obj") && !isConvertibleModel("z.glb"));
}
