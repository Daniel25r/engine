// Escultura de mallas, rayo contra la malla y exportación a OBJ.
#include "titan/core/MeshSculpt.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace titan {

const char* meshBrushName(MeshBrush b) {
    switch (b) {
    case MeshBrush::Draw: return "Dibujar";
    case MeshBrush::Inflate: return "Inflar";
    case MeshBrush::Smooth: return "Suavizar";
    case MeshBrush::Flatten: return "Aplanar";
    case MeshBrush::Pinch: return "Pellizcar";
    case MeshBrush::Grab: return "Agarrar";
    }
    return "?";
}

bool raycastEditMesh(const EditMesh& m, Vec3 o, Vec3 d, MeshRayHit& hit) {
    hit = {};
    float best = 1e30f;
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const std::vector<int>& v = m.faces[f].v;
        for (size_t i = 1; i + 1 < v.size(); ++i) { // abanico de triángulos (Möller–Trumbore)
            const Vec3 a = m.verts[size_t(v[0])], b = m.verts[size_t(v[i])], c = m.verts[size_t(v[i + 1])];
            const Vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
            const float det = dot(e1, p);
            if (std::fabs(det) < 1e-12f) continue;
            const float inv = 1.0f / det;
            const Vec3 s = o - a;
            const float u = dot(s, p) * inv;
            if (u < 0 || u > 1) continue;
            const Vec3 q = cross(s, e1);
            const float w = dot(d, q) * inv;
            if (w < 0 || u + w > 1) continue;
            const float t = dot(e2, q) * inv;
            if (t > 1e-6f && t < best) {
                best = t;
                hit.face = int(f);
            }
        }
    }
    if (hit.face < 0) return false;
    hit.distance = best;
    hit.point = o + d * best;
    hit.normal = m.faceNormal(hit.face);
    return true;
}

void SculptStroke::begin(const EditMesh& m, Vec3 center) {
    std::vector<std::set<int>> nb(m.verts.size());
    normals.assign(m.verts.size(), Vec3{});
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const std::vector<int>& v = m.faces[f].v;
        const Vec3 n = m.faceNormal(int(f));
        for (size_t i = 0; i < v.size(); ++i) {
            const int a = v[i], b = v[(i + 1) % v.size()];
            nb[size_t(a)].insert(b);
            nb[size_t(b)].insert(a);
            normals[size_t(a)] += n;
        }
    }
    neighbors.assign(m.verts.size(), {});
    for (size_t i = 0; i < nb.size(); ++i) neighbors[i].assign(nb[i].begin(), nb[i].end());
    for (Vec3& n : normals) n = n.length() > 1e-8f ? n.normalized() : Vec3{0, 1, 0};
    startPositions = m.verts;
    startCenter = center;
}

int sculptMesh(EditMesh& m, SculptStroke& st, MeshBrush brush, Vec3 center, float radius, float strength, bool invert,
               Vec3 grabDelta) {
    if (radius <= 0 || st.normals.size() != m.verts.size()) return 0;
    const Vec3 c = brush == MeshBrush::Grab ? st.startCenter : center;
    const float sign = invert ? -1.0f : 1.0f;
    // Vértices afectados y su peso (caída suave: 1 en el centro, 0 en el borde).
    std::vector<std::pair<int, float>> hit;
    Vec3 avgN, avgP;
    float wsum = 0;
    const std::vector<Vec3>& base = brush == MeshBrush::Grab ? st.startPositions : m.verts;
    for (size_t i = 0; i < base.size(); ++i) {
        const float dist = (base[i] - c).length();
        if (dist >= radius) continue;
        const float x = dist / radius, w = (1 - x * x) * (1 - x * x);
        hit.push_back({int(i), w});
        avgN += st.normals[i] * w;
        avgP += base[i] * w;
        wsum += w;
    }
    if (hit.empty()) return 0;
    avgN = avgN.length() > 1e-8f ? avgN.normalized() : Vec3{0, 1, 0};
    avgP = avgP / wsum;
    std::vector<Vec3> next = m.verts;
    for (const auto& [i, w] : hit) {
        const Vec3 p = m.verts[size_t(i)];
        switch (brush) {
        case MeshBrush::Draw: next[size_t(i)] = p + avgN * (strength * w * sign); break;
        case MeshBrush::Inflate: next[size_t(i)] = p + st.normals[size_t(i)] * (strength * w * sign); break;
        case MeshBrush::Pinch: next[size_t(i)] = p + (c - p) * std::min(1.0f, strength * w * sign); break;
        case MeshBrush::Grab: next[size_t(i)] = st.startPositions[size_t(i)] + grabDelta * w; break;
        case MeshBrush::Smooth: {
            const std::vector<int>& nb = st.neighbors[size_t(i)];
            if (nb.empty()) break;
            Vec3 avg;
            for (int j : nb) avg += m.verts[size_t(j)];
            avg = avg / float(nb.size());
            next[size_t(i)] = p + (avg - p) * std::clamp(strength * w, 0.0f, 1.0f);
            break;
        }
        case MeshBrush::Flatten: { // hacia el plano medio de la zona del pincel
            const float h = dot(p - avgP, avgN);
            next[size_t(i)] = p - avgN * (h * std::clamp(strength * w, 0.0f, 1.0f));
            break;
        }
        }
    }
    m.verts = std::move(next);
    return int(hit.size());
}

bool writeObj(const MeshBuild& mesh, const std::string& objPath, const std::string& mtlFileName, std::string* error) {
    std::ofstream o(objPath);
    if (!o) {
        if (error) *error = "no se pudo escribir " + objPath;
        return false;
    }
    o << "# Exportado desde Titán Engine\n";
    if (!mtlFileName.empty()) o << "mtllib " << mtlFileName << "\n";
    size_t base = 1;
    for (const MeshBuildPart& p : mesh.parts) {
        const MeshBuildMaterial& mat = mesh.materials[size_t(std::clamp(p.material, 0, int(mesh.materials.size()) - 1))];
        o << "o " << mat.name << "\n";
        for (const MeshBuildVertex& v : p.vertices) o << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
        for (const MeshBuildVertex& v : p.vertices) o << "vt " << v.uv.x << ' ' << 1.0f - v.uv.y << '\n';
        for (const MeshBuildVertex& v : p.vertices) o << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
        if (!mtlFileName.empty()) o << "usemtl " << mat.name << "\n";
        for (size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            o << 'f';
            for (int k = 0; k < 3; ++k) {
                const size_t idx = base + p.indices[i + size_t(k)];
                o << ' ' << idx << '/' << idx << '/' << idx;
            }
            o << '\n';
        }
        base += p.vertices.size();
    }
    if (!mtlFileName.empty()) {
        std::ofstream mt((std::filesystem::path(objPath).parent_path() / mtlFileName).string());
        for (const MeshBuildMaterial& m : mesh.materials)
            mt << "newmtl " << m.name << "\nKd " << m.color.r << ' ' << m.color.g << ' ' << m.color.b << "\nd " << m.color.a
               << "\nPr " << m.roughness << "\nPm " << m.metallic << "\n\n";
    }
    return bool(o);
}

} // namespace titan
