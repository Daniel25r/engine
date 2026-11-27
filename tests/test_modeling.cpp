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

// ---- Escultura, rayo contra la malla y exportación a OBJ
#include "titan/core/MeshSculpt.h"

TEST("escultura: rayo contra la malla, dibujar, inflar, suavizar, aplanar, pellizcar y agarrar") {
    EditMesh m = EditMesh::plane(2, 16); // rejilla densa en y = 0
    MeshRayHit hit;
    CHECK(raycastEditMesh(m, {0.1f, 5, 0.1f}, {0, -1, 0}, hit));
    CHECK_NEAR(hit.distance, 5.0f, 1e-4f);
    CHECK_NEAR(hit.normal.y, 1.0f, 1e-5f);
    CHECK(!raycastEditMesh(m, {5, 5, 5}, {0, -1, 0}, hit)); // fuera de la rejilla

    SculptStroke st;
    st.begin(m, {0, 0, 0});
    const int moved = sculptMesh(m, st, MeshBrush::Draw, {0, 0, 0}, 0.5f, 0.2f, false);
    CHECK(moved > 0);
    float centerY = -1, edgeY = -1;
    for (const Vec3& v : m.verts) {
        if (std::fabs(v.x) < 1e-4f && std::fabs(v.z) < 1e-4f) centerY = v.y;
        if (std::fabs(v.x - 1) < 1e-4f && std::fabs(v.z - 1) < 1e-4f) edgeY = v.y;
    }
    CHECK_NEAR(centerY, 0.2f, 1e-4f); // el centro sube la fuerza completa
    CHECK_NEAR(edgeY, 0.0f, 1e-6f);   // fuera del radio no se mueve
    // Invertido: hunde.
    st.begin(m, {0, 0, 0});
    sculptMesh(m, st, MeshBrush::Draw, {0, 0, 0}, 0.5f, 0.4f, true);
    for (const Vec3& v : m.verts)
        if (v.length() < 0.3f && std::fabs(v.x) < 1e-4f && std::fabs(v.z) < 1e-4f) CHECK(v.y < 0);
    // Suavizar repetidamente devuelve la zona casi al plano.
    for (int i = 0; i < 60; ++i) {
        st.begin(m, {0, 0, 0});
        sculptMesh(m, st, MeshBrush::Smooth, {0, 0, 0}, 0.8f, 1.0f, false);
    }
    float maxAbs = 0;
    for (const Vec3& v : m.verts) maxAbs = std::max(maxAbs, std::fabs(v.y));
    CHECK(maxAbs < 0.05f);
    // Aplanar lleva al plano medio.
    m.verts[100].y = 0.3f;
    st.begin(m, m.verts[100]);
    for (int i = 0; i < 5; ++i) sculptMesh(m, st, MeshBrush::Flatten, m.verts[100], 0.6f, 1.0f, false);
    CHECK(std::fabs(m.verts[100].y) < 0.15f);
    // Agarrar mueve la zona con el ratón, sin acumular entre pasos.
    EditMesh g = EditMesh::plane(2, 8);
    st.begin(g, {0, 0, 0});
    sculptMesh(g, st, MeshBrush::Grab, {0, 0, 0}, 0.6f, 1, false, {0, 0.5f, 0});
    sculptMesh(g, st, MeshBrush::Grab, {0, 0, 0}, 0.6f, 1, false, {0, 0.5f, 0});
    for (const Vec3& v : g.verts)
        if (std::fabs(v.x) < 1e-4f && std::fabs(v.z) < 1e-4f) CHECK_NEAR(v.y, 0.5f, 1e-4f);
    // Inflar una esfera la hace más grande; pellizcar acerca al centro del pincel.
    EditMesh s = EditMesh::uvSphere(0.5f, 16, 8);
    st.begin(s, {0, 0, 0});
    sculptMesh(s, st, MeshBrush::Inflate, {0, 0, 0}, 10, 0.1f, false);
    for (const Vec3& v : s.verts) CHECK_NEAR(v.length(), 0.5f + 0.1f * std::pow(1 - std::pow(0.5f / 10, 2.0f), 2.0f), 2e-3f);
    EditMesh p = EditMesh::plane(2, 4);
    const Vec3 before = p.verts[6];
    st.begin(p, {0, 0, 0});
    sculptMesh(p, st, MeshBrush::Pinch, {0, 0, 0}, 1.5f, 0.3f, false);
    CHECK(p.verts[6].length() < before.length());
}

TEST("exportar: OBJ con MTL que se vuelve a importar igual") {
    const std::string dir = tempDir("exportar");
    EditMesh m = EditMesh::cube(1);
    m.materials[0].name = "Piedra";
    m.materials[0].color = {0.4f, 0.4f, 0.45f, 1};
    std::string err;
    CHECK(writeObj(m.build(), dir + "/cubo.obj", "cubo.mtl", &err));
    MeshBuild back;
    ImportReport rep;
    CHECK(loadObj(dir + "/cubo.obj", back, &rep, &err));
    CHECK_EQ(back.triangleCount(), 12u);
    CHECK_EQ(back.materials.size(), 1u);
    CHECK_EQ(back.materials[0].name, std::string("Piedra"));
    CHECK_NEAR(back.materials[0].color.b, 0.45f, 1e-5f);
}

// ---- Rigging y animación
#include "titan/core/Rig.h"

TEST("rig: esqueleto humano ajustado a la caja, pesos automáticos y pose de reposo") {
    const Rig r = Rig::humanoid({-0.9f, 0, -0.2f}, {0.9f, 1.8f, 0.2f});
    CHECK_EQ(r.bones.size(), 19u);
    for (size_t i = 0; i < r.bones.size(); ++i) CHECK(r.bones[i].parent < int(i)); // los padres van antes
    CHECK_NEAR(r.bones[size_t(r.find("Cabeza"))].tail.y, 1.8f, 1e-5f);
    CHECK(r.bones[size_t(r.find("Mano.I"))].tail.x > 0.8f); // los brazos llegan a los lados (pose T)
    CHECK(r.find("Pie.D") >= 0 && r.find("Antebrazo.I") >= 0);

    Rig w = r;
    std::vector<Vec3> verts = {{0.85f, 1.44f, 0}, {0, 1.7f, 0}, {0.16f, 0.2f, 0}, {0, 1.0f, 0.1f}};
    w.autoWeights(verts);
    for (size_t v = 0; v < verts.size(); ++v) {
        float sum = 0;
        for (float x : w.weights[v]) sum += x;
        CHECK_NEAR(sum, 1.0f, 1e-5f);
    }
    CHECK_EQ(w.joints[0][0], w.find("Mano.I"));   // el hueso dominante es el más cercano
    CHECK_EQ(w.joints[1][0], w.find("Cabeza"));
    CHECK_EQ(w.joints[2][0], w.find("Pierna.I"));
    CHECK(w.weights[0][0] > 0.6f);
    // En reposo la malla no se mueve.
    const std::vector<Vec3> rest = w.deform(verts, w.skinMatrices(-1, 0));
    for (size_t i = 0; i < verts.size(); ++i) CHECK((rest[i] - verts[i]).length() < 1e-5f);
}

TEST("rig: claves, interpolación y deformación al girar un hueso") {
    Rig r;
    r.bones.push_back({"Brazo", -1, {0, 0, 0}, {1, 0, 0}});
    r.bones.push_back({"Antebrazo", 0, {1, 0, 0}, {2, 0, 0}});
    RigClip clip;
    clip.name = "Doblar";
    RigKey a, b;
    a.time = 0;
    b.time = 1;
    b.rotation = Quat::fromAxisAngle({0, 0, 1}, kPi / 2); // codo 90° hacia arriba
    clip.setKey(1, a);
    clip.setKey(1, b);
    clip.setKey(1, b); // la misma clave dos veces la sustituye
    CHECK_EQ(clip.tracks[0].keys.size(), 2u);
    CHECK_EQ(clip.keyTimes().size(), 2u);
    Quat q;
    Vec3 t;
    CHECK(clip.sample(1, 0.5f, q, t));
    CHECK_NEAR(q.toEuler().z, kPi / 4, 1e-3f); // a mitad, la mitad del giro (interpolación esférica; radianes)
    CHECK(!clip.sample(0, 0.5f, q, t));      // el brazo no tiene claves
    r.clips.push_back(clip);
    // Un punto en la punta del antebrazo, todo suyo: con el codo a 90°, pasa de (2,0,0) a (1,1,0).
    r.joints = {{1, 0, 0, 0}};
    r.weights = {{1, 0, 0, 0}};
    const std::vector<Vec3> bent = r.deform({{2, 0, 0}}, r.skinMatrices(0, 1));
    CHECK((bent[0] - Vec3{1, 1, 0}).length() < 1e-4f);
    CHECK(clip.removeKey(1, 1.0f));
    CHECK(!clip.removeKey(1, 0.5f));
}

TEST("rig: el GLB lleva skin, pesos y animación válidos, y el .tmesh los conserva") {
    EditMesh m = EditMesh::cube(1);
    m.subdivide(false);
    m.rig = Rig::humanoid({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f});
    RigClip wave;
    wave.name = "Saludar";
    RigKey k0, k1;
    k1.time = 0.5f;
    k1.rotation = Quat::fromAxisAngle({0, 0, 1}, 1.0f);
    wave.setKey(m.rig.find("Brazo.I"), k0);
    wave.setKey(m.rig.find("Brazo.I"), k1);
    m.rig.clips.push_back(wave);
    const MeshBuild mb = m.build(); // los pesos faltan: se calculan solos
    CHECK_EQ(mb.bones.size(), 19u);
    CHECK_EQ(mb.clips.size(), 1u);
    std::vector<uint8_t> glb;
    std::string err;
    CHECK(writeGlb(mb, glb, &err));
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    CHECK(cgltf_parse(&opt, glb.data(), glb.size(), &data) == cgltf_result_success);
    CHECK(cgltf_load_buffers(&opt, data, nullptr) == cgltf_result_success);
    CHECK(cgltf_validate(data) == cgltf_result_success);
    CHECK_EQ(data->skins_count, size_t(1));
    CHECK_EQ(data->skins[0].joints_count, size_t(19));
    CHECK_EQ(data->animations_count, size_t(1));
    CHECK_EQ(std::string(data->animations[0].name), std::string("Saludar"));
    CHECK_EQ(data->animations[0].channels_count, size_t(2)); // traslación y rotación del brazo
    bool hasJoints = false;
    for (size_t a = 0; a < data->meshes[0].primitives[0].attributes_count; ++a)
        hasJoints |= data->meshes[0].primitives[0].attributes[a].type == cgltf_attribute_type_joints;
    CHECK(hasJoints);
    CHECK(data->nodes[0].skin == &data->skins[0]);
    cgltf_free(data);

    EditMesh back;
    CHECK(EditMesh::fromJson(m.toJson(), back, &err));
    CHECK_EQ(back.rig.bones.size(), 19u);
    CHECK_EQ(back.rig.clips.size(), 1u);
    CHECK_EQ(back.rig.clips[0].tracks[0].keys.size(), 2u);
    CHECK_NEAR(back.rig.clips[0].tracks[0].keys[1].rotation.z, k1.rotation.z, 1e-6f);
}
