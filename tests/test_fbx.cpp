// Importación de FBX: un archivo real de Maya (malla con esqueleto de 3 huesos y una animación).
#include <cmath>

#include "Test.h"
#include "cgltf.h"
#include "titan/core/ModelImport.h"

using namespace titan;

TEST("fbx: malla con piel de Maya: esqueleto, pesos, normales y GLB válido") {
    MeshBuild m;
    std::string err;
    ImportReport rep;
    CHECK(loadFbx(std::string(TITAN_TEST_DATA_DIR) + "/skin_cubo.fbx", m, &rep, &err));
    CHECK(err.empty());
    CHECK(!m.parts.empty());
    CHECK(m.triangleCount() >= 12); // al menos un cubo
    CHECK(m.bones.size() >= 3);
    // (la animación de este archivo está en el nodo de la propia malla con piel, no en los huesos: no se trae)
    // Cada vértice: pesos que suman 1 y huesos válidos.
    bool weightsOk = true, normalsOk = true;
    for (const MeshBuildPart& p : m.parts)
        for (const MeshBuildVertex& v : p.vertices) {
            const float sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
            weightsOk = weightsOk && std::fabs(sum - 1.0f) < 1e-3f;
            for (int k = 0; k < 4; ++k) weightsOk = weightsOk && v.joints[k] < m.bones.size();
            normalsOk = normalsOk && std::fabs(v.normal.length() - 1.0f) < 1e-3f;
        }
    CHECK(weightsOk);
    CHECK(normalsOk);
    // Vértices compartidos entre triángulos (malla conectada): muchos menos vértices que esquinas.
    size_t verts = 0, corners = 0;
    for (const MeshBuildPart& p : m.parts) {
        verts += p.vertices.size();
        corners += p.indices.size();
    }
    CHECK(verts * 2 < corners);
    // Los padres van antes que los hijos.
    for (size_t b = 0; b < m.bones.size(); ++b) CHECK(m.bones[b].parent < int(b));
    // GLB: lo lee cgltf con piel y animación.
    std::vector<uint8_t> glb;
    CHECK(writeGlb(m, glb, &err));
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    CHECK(cgltf_parse(&opt, glb.data(), glb.size(), &data) == cgltf_result_success);
    if (data) {
        CHECK_EQ(data->skins_count, size_t(1));
        CHECK(cgltf_validate(data) == cgltf_result_success);
        cgltf_free(data);
    }
}

TEST("fbx: objeto animado sin esqueleto (3ds Max): la animación se conserva como hueso rígido") {
    MeshBuild m;
    std::string err;
    CHECK(loadFbx(std::string(TITAN_TEST_DATA_DIR) + "/caja_animada.fbx", m, nullptr, &err));
    CHECK_EQ(m.bones.size(), size_t(1));
    CHECK_EQ(m.clips.size(), size_t(1));
    CHECK_EQ(m.clips[0].name, std::string("caja_animada")); // "Take 001" se cambia por el nombre del archivo
    CHECK(!m.clips[0].channels.empty());
    // 2 segundos a 30 fps: 61 muestras por canal, y los valores cambian con el tiempo.
    const MeshBuildChannel& ch = m.clips[0].channels[0];
    CHECK_EQ(ch.times.size(), size_t(61));
    CHECK_NEAR(ch.times.back(), 2.0f, 1e-4f);
    const int w = ch.path == 1 ? 4 : 3;
    bool changes = false;
    for (int k = 0; k < w; ++k) changes = changes || std::fabs(ch.values[size_t(k)] - ch.values[ch.values.size() - size_t(w) + size_t(k)]) > 1e-3f;
    CHECK(changes);
    std::vector<uint8_t> glb;
    CHECK(writeGlb(m, glb, &err));
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    CHECK(cgltf_parse(&opt, glb.data(), glb.size(), &data) == cgltf_result_success);
    if (data) {
        CHECK_EQ(data->animations_count, size_t(1));
        CHECK(cgltf_validate(data) == cgltf_result_success);
        cgltf_free(data);
    }
}

TEST("fbx: archivos dañados o sin malla dan un error claro") {
    MeshBuild m;
    std::string err;
    CHECK(!loadFbx(std::string(TITAN_TEST_DATA_DIR) + "/no_existe.fbx", m, nullptr, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!loadFbx(std::string(TITAN_TEST_DATA_DIR) + "/tono440.mp3", m, nullptr, &err)); // no es un FBX
    CHECK(!err.empty());
}

TEST("animaciones: biblioteca sin malla (estilo Mixamo \"Without Skin\") es un GLB válido") {
    MeshBuild lib;
    MeshBuildBone hips;
    hips.name = "mixamorig:Hips";
    hips.restLocal = {0, 1, 0};
    lib.bones.push_back(hips);
    MeshBuildClip clip;
    clip.name = "Correr";
    MeshBuildChannel ch;
    ch.bone = 0;
    ch.path = 1;
    ch.times = {0, 0.5f};
    ch.values = {0, 0, 0, 1, 0, 0.3826834f, 0, 0.9238795f};
    clip.channels.push_back(ch);
    lib.clips.push_back(clip);
    std::vector<uint8_t> glb;
    std::string err;
    CHECK(writeGlb(lib, glb, &err));
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    CHECK(cgltf_parse(&opt, glb.data(), glb.size(), &data) == cgltf_result_success);
    if (data) {
        CHECK_EQ(data->meshes_count, size_t(0));
        CHECK_EQ(data->animations_count, size_t(1));
        CHECK(data->nodes_count >= 2 && std::string(data->nodes[1].name) == "mixamorig:Hips");
        CHECK(cgltf_validate(data) == cgltf_result_success);
        cgltf_free(data);
    }
    MeshBuild empty; // sin malla ni animaciones: no se escribe
    CHECK(!writeGlb(empty, glb, &err));
}
