// Malla editable: primitivas y operaciones de modelado (extruir, inset, subdivisión Catmull-Clark, borrar, fusionar,
// simetría). Algoritmos escritos para Titán a partir de su descripción pública (no hay código de Blender).
#include "titan/core/EditMesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "titan/core/Serialization.h"

namespace titan {

namespace {


// Primitivas convexas centradas: cada cara mira hacia fuera (su normal apunta en el mismo sentido que su centro).
void orientOutward(EditMesh& m) {
    for (size_t f = 0; f < m.faces.size(); ++f)
        if (dot(m.faceNormal(int(f)), m.faceCenter(int(f))) < 0) {
            std::reverse(m.faces[f].v.begin(), m.faces[f].v.end());
            std::reverse(m.faces[f].uv.begin(), m.faces[f].uv.end());
        }
}

uint64_t edgeKey(int a, int b) {
    const uint32_t lo = uint32_t(std::min(a, b)), hi = uint32_t(std::max(a, b));
    return (uint64_t(lo) << 32) | hi;
}
uint64_t directedKey(int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); }

} // namespace

// ---------------------------------------------------------------- primitivas
EditMesh EditMesh::cube(float size) {
    EditMesh m;
    const float h = size * 0.5f;
    for (int i = 0; i < 8; ++i) m.verts.push_back({(i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h});
    const int quads[6][4] = {{1, 3, 7, 5}, {0, 2, 6, 4}, {2, 3, 7, 6}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 1, 3, 2}};
    for (const auto& q : quads) m.faces.push_back({{q[0], q[1], q[2], q[3]}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0});
    orientOutward(m);
    return m;
}

EditMesh EditMesh::plane(float size, int divisions) {
    EditMesh m;
    const int n = std::max(1, divisions);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) m.verts.push_back({(float(i) / n - 0.5f) * size, 0, (float(j) / n - 0.5f) * size});
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const int a = j * (n + 1) + i;
            // Antihorario visto desde arriba (+Y): a, a+(n+1), a+(n+1)+1, a+1
            m.faces.push_back({{a, a + n + 1, a + n + 2, a + 1},
                               {{float(i) / n, float(j) / n}, {float(i) / n, float(j + 1) / n},
                                {float(i + 1) / n, float(j + 1) / n}, {float(i + 1) / n, float(j) / n}},
                               0});
        }
    for (size_t f = 0; f < m.faces.size(); ++f)
        if (m.faceNormal(int(f)).y < 0) {
            std::reverse(m.faces[f].v.begin(), m.faces[f].v.end());
            std::reverse(m.faces[f].uv.begin(), m.faces[f].uv.end());
        }
    return m;
}

EditMesh EditMesh::cylinder(float radius, float height, int segments) {
    EditMesh m;
    const int s = std::max(3, segments);
    for (int ring = 0; ring < 2; ++ring)
        for (int i = 0; i < s; ++i) {
            const float a = 2 * kPi * i / s;
            m.verts.push_back({std::cos(a) * radius, (ring ? 0.5f : -0.5f) * height, std::sin(a) * radius});
        }
    for (int i = 0; i < s; ++i) {
        const int n = (i + 1) % s;
        m.faces.push_back({{i, n, s + n, s + i}, {{float(i) / s, 0}, {float(i + 1) / s, 0}, {float(i + 1) / s, 1}, {float(i) / s, 1}}, 0});
    }
    EditFace bottom, top;
    for (int i = 0; i < s; ++i) {
        bottom.v.push_back(i);
        top.v.push_back(s + i);
    }
    m.faces.push_back(bottom);
    m.faces.push_back(top);
    orientOutward(m);
    return m;
}

EditMesh EditMesh::cone(float radius, float height, int segments) {
    EditMesh m;
    const int s = std::max(3, segments);
    for (int i = 0; i < s; ++i) {
        const float a = 2 * kPi * i / s;
        m.verts.push_back({std::cos(a) * radius, -0.5f * height, std::sin(a) * radius});
    }
    m.verts.push_back({0, 0.5f * height, 0});
    for (int i = 0; i < s; ++i) m.faces.push_back({{i, (i + 1) % s, s}, {}, 0});
    EditFace base;
    for (int i = 0; i < s; ++i) base.v.push_back(i);
    m.faces.push_back(base);
    orientOutward(m);
    return m;
}

EditMesh EditMesh::uvSphere(float radius, int segments, int rings) {
    EditMesh m;
    const int s = std::max(3, segments), r = std::max(2, rings);
    m.verts.push_back({0, radius, 0}); // polo norte
    for (int j = 1; j < r; ++j) {
        const float phi = kPi * j / r;
        for (int i = 0; i < s; ++i) {
            const float th = 2 * kPi * i / s;
            m.verts.push_back({std::sin(phi) * std::cos(th) * radius, std::cos(phi) * radius, std::sin(phi) * std::sin(th) * radius});
        }
    }
    m.verts.push_back({0, -radius, 0}); // polo sur
    const int south = int(m.verts.size()) - 1;
    auto ringV = [&](int j, int i) { return 1 + (j - 1) * s + ((i % s) + s) % s; };
    for (int i = 0; i < s; ++i) m.faces.push_back({{0, ringV(1, i), ringV(1, i + 1)}, {}, 0});
    for (int j = 1; j < r - 1; ++j)
        for (int i = 0; i < s; ++i) m.faces.push_back({{ringV(j, i), ringV(j + 1, i), ringV(j + 1, i + 1), ringV(j, i + 1)}, {}, 0});
    for (int i = 0; i < s; ++i) m.faces.push_back({{ringV(r - 1, i), south, ringV(r - 1, i + 1)}, {}, 0});
    orientOutward(m);
    m.smooth = true;
    return m;
}

// ---------------------------------------------------------------- consultas
Vec3 EditMesh::faceCenter(int f) const {
    Vec3 c;
    for (int v : faces[size_t(f)].v) c += verts[size_t(v)];
    return c / float(std::max<size_t>(1, faces[size_t(f)].v.size()));
}

Vec3 EditMesh::faceNormal(int f) const {
    const std::vector<int>& v = faces[size_t(f)].v;
    Vec3 n;
    for (size_t i = 0; i < v.size(); ++i) { // Newell
        const Vec3& a = verts[size_t(v[i])];
        const Vec3& b = verts[size_t(v[(i + 1) % v.size()])];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n.normalized();
}

std::vector<int> EditMesh::faceVertices(const std::set<int>& fs) const {
    std::set<int> out;
    for (int f : fs)
        if (f >= 0 && size_t(f) < faces.size()) out.insert(faces[size_t(f)].v.begin(), faces[size_t(f)].v.end());
    return {out.begin(), out.end()};
}

bool EditMesh::valid(std::string* why) const {
    auto fail = [&](const std::string& s) {
        if (why) *why = s;
        return false;
    };
    for (size_t f = 0; f < faces.size(); ++f) {
        const EditFace& face = faces[f];
        if (face.v.size() < 3) return fail("cara " + std::to_string(f) + " con menos de 3 vértices");
        if (!face.uv.empty() && face.uv.size() != face.v.size()) return fail("cara " + std::to_string(f) + ": UV y vértices no coinciden");
        std::set<int> seen;
        for (int v : face.v) {
            if (v < 0 || size_t(v) >= verts.size()) return fail("cara " + std::to_string(f) + " usa un vértice que no existe");
            if (!seen.insert(v).second) return fail("cara " + std::to_string(f) + " repite un vértice");
        }
    }
    return true;
}

size_t EditMesh::boundaryEdgeCount() const {
    std::set<uint64_t> directed;
    for (const EditFace& f : faces)
        for (size_t i = 0; i < f.v.size(); ++i) directed.insert(directedKey(f.v[i], f.v[(i + 1) % f.v.size()]));
    size_t n = 0;
    for (uint64_t k : directed)
        if (!directed.count(directedKey(int(k & 0xFFFFFFFFu), int(k >> 32)))) ++n;
    return n;
}

// ---------------------------------------------------------------- transformar
void EditMesh::translate(const std::vector<int>& vs, Vec3 d) {
    for (int v : vs) verts[size_t(v)] += d;
}
void EditMesh::scale(const std::vector<int>& vs, Vec3 p, Vec3 k) {
    for (int v : vs) {
        const Vec3 o = verts[size_t(v)] - p;
        verts[size_t(v)] = p + Vec3{o.x * k.x, o.y * k.y, o.z * k.z};
    }
}
void EditMesh::rotate(const std::vector<int>& vs, Vec3 p, const Quat& q) {
    for (int v : vs) verts[size_t(v)] = p + q.rotate(verts[size_t(v)] - p);
}

// ---------------------------------------------------------------- modelado
std::vector<int> EditMesh::extrude(const std::set<int>& sel, float distance) {
    std::set<int> fs;
    for (int f : sel)
        if (f >= 0 && size_t(f) < faces.size()) fs.insert(f);
    if (fs.empty()) return {};
    Vec3 n;
    for (int f : fs) n += faceNormal(f);
    n = n.length() > 1e-6f ? n.normalized() : faceNormal(*fs.begin());
    // Aristas de la región: las de borde (sin pareja inversa dentro de la selección) tendrán pared lateral.
    std::set<uint64_t> inside;
    for (int f : fs)
        for (size_t i = 0; i < faces[size_t(f)].v.size(); ++i)
            inside.insert(directedKey(faces[size_t(f)].v[i], faces[size_t(f)].v[(i + 1) % faces[size_t(f)].v.size()]));
    std::map<int, int> copy;
    for (int v : faceVertices(fs)) {
        copy[v] = int(verts.size());
        verts.push_back(verts[size_t(v)] + n * distance);
    }
    std::vector<EditFace> walls;
    for (int f : fs) {
        const EditFace& face = faces[size_t(f)];
        for (size_t i = 0; i < face.v.size(); ++i) {
            const int a = face.v[i], b = face.v[(i + 1) % face.v.size()];
            if (!inside.count(directedKey(b, a))) walls.push_back({{a, b, copy[b], copy[a]}, {}, face.material});
        }
    }
    for (int f : fs)
        for (int& v : faces[size_t(f)].v) v = copy[v];
    for (EditFace& w : walls) faces.push_back(w);
    return {fs.begin(), fs.end()};
}

std::vector<int> EditMesh::inset(const std::set<int>& sel, float amount) {
    amount = std::clamp(amount, 0.0f, 0.99f);
    std::vector<int> result;
    for (int f : sel) {
        if (f < 0 || size_t(f) >= faces.size()) continue;
        const Vec3 c = faceCenter(f);
        EditFace face = faces[size_t(f)];
        Vec2 uvc;
        for (const Vec2& u : face.uv) uvc += u;
        if (!face.uv.empty()) uvc = uvc / float(face.uv.size());
        std::vector<int> inner;
        for (int v : face.v) {
            inner.push_back(int(verts.size()));
            verts.push_back(verts[size_t(v)] + (c - verts[size_t(v)]) * amount);
        }
        const size_t n = face.v.size();
        for (size_t i = 0; i < n; ++i) {
            const size_t j = (i + 1) % n;
            EditFace ring{{face.v[i], face.v[j], inner[j], inner[i]}, {}, face.material};
            if (!face.uv.empty())
                ring.uv = {face.uv[i], face.uv[j], face.uv[j] + (uvc - face.uv[j]) * amount, face.uv[i] + (uvc - face.uv[i]) * amount};
            faces.push_back(ring);
        }
        faces[size_t(f)].v = inner;
        for (Vec2& u : faces[size_t(f)].uv) u = u + (uvc - u) * amount;
        result.push_back(f);
    }
    return result;
}

void EditMesh::subdivide(bool cc) {
    const size_t nv = verts.size();
    // Aristas únicas y sus caras.
    std::unordered_map<uint64_t, int> edgeIndex;
    std::vector<std::pair<int, int>> edges;
    std::vector<std::vector<int>> edgeFaces;
    for (size_t f = 0; f < faces.size(); ++f)
        for (size_t i = 0; i < faces[f].v.size(); ++i) {
            const int a = faces[f].v[i], b = faces[f].v[(i + 1) % faces[f].v.size()];
            auto [it, added] = edgeIndex.emplace(edgeKey(a, b), int(edges.size()));
            if (added) {
                edges.push_back({a, b});
                edgeFaces.emplace_back();
            }
            edgeFaces[size_t(it->second)].push_back(int(f));
        }
    std::vector<Vec3> facePoint(faces.size());
    for (size_t f = 0; f < faces.size(); ++f) facePoint[f] = faceCenter(int(f));
    std::vector<Vec3> edgePoint(edges.size());
    for (size_t e = 0; e < edges.size(); ++e) {
        const Vec3 mid = (verts[size_t(edges[e].first)] + verts[size_t(edges[e].second)]) * 0.5f;
        if (cc && edgeFaces[e].size() == 2)
            edgePoint[e] = (verts[size_t(edges[e].first)] + verts[size_t(edges[e].second)] + facePoint[size_t(edgeFaces[e][0])] +
                            facePoint[size_t(edgeFaces[e][1])]) * 0.25f;
        else
            edgePoint[e] = mid;
    }
    std::vector<Vec3> newVerts(verts);
    if (cc) { // vértices originales: regla de Catmull-Clark (interior) o de borde
        std::vector<Vec3> sumF(nv), sumR(nv), boundarySum(nv);
        std::vector<int> nFaces(nv, 0), nEdges(nv, 0), nBoundary(nv, 0);
        for (size_t f = 0; f < faces.size(); ++f)
            for (int v : faces[f].v) {
                sumF[size_t(v)] += facePoint[f];
                ++nFaces[size_t(v)];
            }
        for (size_t e = 0; e < edges.size(); ++e) {
            const auto [a, b] = edges[e];
            const Vec3 mid = (verts[size_t(a)] + verts[size_t(b)]) * 0.5f;
            for (int v : {a, b}) {
                sumR[size_t(v)] += mid;
                ++nEdges[size_t(v)];
            }
            if (edgeFaces[e].size() == 1) {
                boundarySum[size_t(a)] += verts[size_t(b)];
                boundarySum[size_t(b)] += verts[size_t(a)];
                ++nBoundary[size_t(a)];
                ++nBoundary[size_t(b)];
            }
        }
        for (size_t v = 0; v < nv; ++v) {
            if (nBoundary[v] == 2) {
                newVerts[v] = verts[v] * 0.75f + boundarySum[v] * 0.125f;
            } else if (nBoundary[v] == 0 && nFaces[v] >= 3) {
                const float n = float(nFaces[v]);
                newVerts[v] = (sumF[v] / n + (sumR[v] / float(nEdges[v])) * 2.0f + verts[v] * (n - 3)) / n;
            }
        }
    }
    const int edgeBase = int(nv), faceBase = int(nv + edges.size());
    newVerts.insert(newVerts.end(), edgePoint.begin(), edgePoint.end());
    newVerts.insert(newVerts.end(), facePoint.begin(), facePoint.end());
    std::vector<EditFace> out;
    for (size_t f = 0; f < faces.size(); ++f) {
        const EditFace& face = faces[f];
        const size_t n = face.v.size();
        Vec2 uvc;
        for (const Vec2& u : face.uv) uvc += u;
        if (!face.uv.empty()) uvc = uvc / float(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t prev = (i + n - 1) % n, next = (i + 1) % n;
            const int ePrev = edgeBase + edgeIndex[edgeKey(face.v[prev], face.v[i])];
            const int eNext = edgeBase + edgeIndex[edgeKey(face.v[i], face.v[next])];
            EditFace q{{face.v[i], eNext, faceBase + int(f), ePrev}, {}, face.material};
            if (!face.uv.empty())
                q.uv = {face.uv[i], (face.uv[i] + face.uv[next]) * 0.5f, uvc, (face.uv[prev] + face.uv[i]) * 0.5f};
            out.push_back(q);
        }
    }
    verts = std::move(newVerts);
    faces = std::move(out);
    if (cc) smooth = true;
}

void EditMesh::removeUnusedVertices() {
    std::vector<int> remap(verts.size(), -1);
    std::vector<Vec3> kept;
    for (EditFace& f : faces)
        for (int& v : f.v) {
            if (remap[size_t(v)] < 0) {
                remap[size_t(v)] = int(kept.size());
                kept.push_back(verts[size_t(v)]);
            }
            v = remap[size_t(v)];
        }
    verts = std::move(kept);
}

void EditMesh::deleteFaces(const std::set<int>& sel) {
    std::vector<EditFace> kept;
    for (size_t f = 0; f < faces.size(); ++f)
        if (!sel.count(int(f))) kept.push_back(faces[f]);
    faces = std::move(kept);
    removeUnusedVertices();
}

void EditMesh::deleteVertices(const std::set<int>& vs) {
    std::vector<EditFace> kept;
    for (const EditFace& f : faces)
        if (std::none_of(f.v.begin(), f.v.end(), [&](int v) { return vs.count(v) > 0; })) kept.push_back(f);
    faces = std::move(kept);
    removeUnusedVertices();
}

void EditMesh::flipNormals(const std::set<int>& sel) {
    for (size_t f = 0; f < faces.size(); ++f)
        if (sel.empty() || sel.count(int(f))) {
            std::reverse(faces[f].v.begin(), faces[f].v.end());
            std::reverse(faces[f].uv.begin(), faces[f].uv.end());
        }
}

int EditMesh::mergeByDistance(float distance) {
    const float cell = std::max(distance, 1e-6f), d2 = distance * distance;
    std::unordered_map<uint64_t, std::vector<int>> grid;
    auto key = [&](long x, long y, long z) { return (uint64_t(x & 0x1FFFFF) << 42) | (uint64_t(y & 0x1FFFFF) << 21) | uint64_t(z & 0x1FFFFF); };
    std::vector<int> target(verts.size());
    int merged = 0;
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vec3& p = verts[i];
        const long cx = long(std::floor(p.x / cell)), cy = long(std::floor(p.y / cell)), cz = long(std::floor(p.z / cell));
        int found = -1;
        for (long dx = -1; dx <= 1 && found < 0; ++dx)
            for (long dy = -1; dy <= 1 && found < 0; ++dy)
                for (long dz = -1; dz <= 1 && found < 0; ++dz) {
                    auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (int j : it->second) {
                        const Vec3 o = verts[size_t(j)] - p;
                        if (dot(o, o) <= d2) {
                            found = j;
                            break;
                        }
                    }
                }
        if (found >= 0) {
            target[i] = found;
            ++merged;
        } else {
            target[i] = int(i);
            grid[key(cx, cy, cz)].push_back(int(i));
        }
    }
    if (merged == 0) return 0;
    std::vector<EditFace> kept;
    for (EditFace f : faces) {
        EditFace g;
        g.material = f.material;
        for (size_t i = 0; i < f.v.size(); ++i) {
            const int v = target[size_t(f.v[i])];
            if (!g.v.empty() && g.v.back() == v) continue;
            g.v.push_back(v);
            if (!f.uv.empty()) g.uv.push_back(f.uv[i]);
        }
        while (g.v.size() > 1 && g.v.front() == g.v.back()) {
            g.v.pop_back();
            if (!g.uv.empty()) g.uv.pop_back();
        }
        if (g.v.size() >= 3 && std::set<int>(g.v.begin(), g.v.end()).size() == g.v.size()) kept.push_back(g);
    }
    faces = std::move(kept);
    removeUnusedVertices();
    return merged;
}

void EditMesh::mirror(int axis) {
    axis = std::clamp(axis, 0, 2);
    const int n0 = int(verts.size());
    const size_t f0 = faces.size();
    for (int i = 0; i < n0; ++i) {
        Vec3 p = verts[size_t(i)];
        (axis == 0 ? p.x : axis == 1 ? p.y : p.z) *= -1;
        verts.push_back(p);
    }
    for (size_t f = 0; f < f0; ++f) {
        EditFace m = faces[f];
        for (int& v : m.v) v += n0;
        std::reverse(m.v.begin(), m.v.end()); // el reflejo invierte el sentido: se corrige para que mire hacia fuera
        std::reverse(m.uv.begin(), m.uv.end());
        faces.push_back(m);
    }
    mergeByDistance(1e-4f); // la costura sobre el plano de simetría queda soldada
}

// ---------------------------------------------------------------- a triángulos
MeshBuild EditMesh::build() const {
    MeshBuild out;
    out.materials = materials.empty() ? std::vector<MeshBuildMaterial>{MeshBuildMaterial{}} : materials;
    std::vector<Vec3> vertexNormal;
    if (smooth) {
        vertexNormal.assign(verts.size(), Vec3{});
        for (size_t f = 0; f < faces.size(); ++f) {
            const Vec3 n = faceNormal(int(f));
            for (int v : faces[f].v) vertexNormal[size_t(v)] += n;
        }
        for (Vec3& n : vertexNormal) n = n.length() > 1e-8f ? n.normalized() : Vec3{0, 1, 0};
    }
    // Esqueleto: pesos por vértice de la malla (automáticos si faltan o la malla cambió) y clips.
    Rig rig = this->rig;
    const bool skinned = !rig.empty();
    if (skinned) {
        if (rig.joints.size() != verts.size()) rig.autoWeights(verts);
        for (const RigBone& b : rig.bones)
            out.bones.push_back({b.name, b.parent, b.parent >= 0 ? b.head - rig.bones[size_t(b.parent)].head : b.head, b.head});
        for (const RigClip& clip : rig.clips) {
            MeshBuildClip mc;
            mc.name = clip.name;
            for (const RigTrack& t : clip.tracks) {
                if (t.bone < 0 || size_t(t.bone) >= rig.bones.size() || t.keys.empty()) continue;
                MeshBuildChannel pos, rot;
                pos.bone = rot.bone = t.bone;
                pos.path = 0;
                rot.path = 1;
                for (const RigKey& k : t.keys) {
                    pos.times.push_back(k.time);
                    rot.times.push_back(k.time);
                    const Vec3 p = out.bones[size_t(t.bone)].restLocal + k.translation;
                    pos.values.insert(pos.values.end(), {p.x, p.y, p.z});
                    const Quat q = k.rotation.normalized();
                    rot.values.insert(rot.values.end(), {q.x, q.y, q.z, q.w});
                }
                mc.channels.push_back(pos);
                mc.channels.push_back(rot);
            }
            out.clips.push_back(mc);
        }
    }
    std::map<int, size_t> partOf;
    for (size_t f = 0; f < faces.size(); ++f) {
        const EditFace& face = faces[f];
        const int mat = face.material >= 0 && size_t(face.material) < out.materials.size() ? face.material : 0;
        auto it = partOf.find(mat);
        if (it == partOf.end()) {
            it = partOf.emplace(mat, out.parts.size()).first;
            out.parts.push_back({});
            out.parts.back().material = mat;
        }
        MeshBuildPart& part = out.parts[it->second];
        const Vec3 fn = faceNormal(int(f));
        const Vec3 an{std::fabs(fn.x), std::fabs(fn.y), std::fabs(fn.z)};
        const uint32_t base = uint32_t(part.vertices.size());
        for (size_t i = 0; i < face.v.size(); ++i) {
            const Vec3& p = verts[size_t(face.v[i])];
            MeshBuildVertex mv;
            mv.position = p;
            mv.normal = smooth ? vertexNormal[size_t(face.v[i])] : fn;
            if (skinned)
                for (int k = 0; k < 4; ++k) {
                    mv.joints[k] = uint16_t(rig.joints[size_t(face.v[i])][size_t(k)]);
                    mv.weights[k] = rig.weights[size_t(face.v[i])][size_t(k)];
                }
            if (!face.uv.empty()) mv.uv = face.uv[i];
            else if (an.x >= an.y && an.x >= an.z) mv.uv = {p.z, p.y}; // proyección por caja
            else if (an.y >= an.z) mv.uv = {p.x, p.z};
            else mv.uv = {p.x, p.y};
            part.vertices.push_back(mv);
        }
        for (size_t i = 1; i + 1 < face.v.size(); ++i) part.indices.insert(part.indices.end(), {base, base + uint32_t(i), base + uint32_t(i + 1)});
    }
    return out;
}

// ---------------------------------------------------------------- .tmesh
std::string EditMesh::toJson() const {
    Json j;
    j["format"] = "titan-mesh";
    j["version"] = 1;
    j["smooth"] = smooth;
    Json v = Json::array();
    for (const Vec3& p : verts) v.insert(v.end(), {p.x, p.y, p.z});
    j["verts"] = v;
    Json fs = Json::array();
    for (const EditFace& f : faces) {
        Json jf{{"v", f.v}};
        if (f.material) jf["m"] = f.material;
        if (!f.uv.empty()) {
            Json uv = Json::array();
            for (const Vec2& u : f.uv) uv.insert(uv.end(), {u.x, u.y});
            jf["uv"] = uv;
        }
        fs.push_back(jf);
    }
    j["faces"] = fs;
    Json ms = Json::array();
    for (const MeshBuildMaterial& m : materials)
        ms.push_back(Json{{"name", m.name}, {"color", {m.color.r, m.color.g, m.color.b, m.color.a}}, {"metallic", m.metallic},
                          {"roughness", m.roughness}});
    j["materials"] = ms;
    if (!rig.empty()) {
        Json bones = Json::array();
        for (const RigBone& b : rig.bones)
            bones.push_back(Json{{"name", b.name}, {"parent", b.parent}, {"head", {b.head.x, b.head.y, b.head.z}}, {"tail", {b.tail.x, b.tail.y, b.tail.z}}});
        Json clips = Json::array();
        for (const RigClip& c : rig.clips) {
            Json tracks = Json::array();
            for (const RigTrack& t : c.tracks) {
                Json keys = Json::array();
                for (const RigKey& k : t.keys)
                    keys.push_back(Json{{"t", k.time}, {"r", {k.rotation.x, k.rotation.y, k.rotation.z, k.rotation.w}},
                                        {"p", {k.translation.x, k.translation.y, k.translation.z}}});
                tracks.push_back(Json{{"bone", t.bone}, {"keys", keys}});
            }
            clips.push_back(Json{{"name", c.name}, {"duration", c.duration}, {"loop", c.loop}, {"tracks", tracks}});
        }
        Json jr{{"bones", bones}, {"clips", clips}};
        if (rig.joints.size() == verts.size()) { // pesos guardados (si se pintan a mano no deben perderse)
            Json jj = Json::array(), ww = Json::array();
            for (size_t i = 0; i < verts.size(); ++i)
                for (int k = 0; k < 4; ++k) {
                    jj.push_back(rig.joints[i][size_t(k)]);
                    ww.push_back(rig.weights[i][size_t(k)]);
                }
            jr["joints"] = jj;
            jr["weights"] = ww;
        }
        j["rig"] = jr;
    }
    return j.dump();
}

bool EditMesh::fromJson(const std::string& text, EditMesh& out, std::string* error) {
    const Json j = Json::parse(text, nullptr, false);
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (!j.is_object() || j.value("format", std::string()) != "titan-mesh") return fail("no es una malla de Titán (.tmesh)");
    if (j.value("version", 0) > 1) return fail("versión de .tmesh más nueva que este motor");
    EditMesh m;
    m.smooth = j.value("smooth", false);
    const Json& v = j.value("verts", Json::array());
    if (!v.is_array() || v.size() % 3) return fail("lista de vértices incorrecta");
    for (size_t i = 0; i + 2 < v.size(); i += 3) m.verts.push_back({v[i].get<float>(), v[i + 1].get<float>(), v[i + 2].get<float>()});
    for (const Json& jf : j.value("faces", Json::array())) {
        EditFace f;
        f.v = jf.value("v", std::vector<int>{});
        f.material = jf.value("m", 0);
        const Json& uv = jf.value("uv", Json::array());
        for (size_t i = 0; i + 1 < uv.size(); i += 2) f.uv.push_back({uv[i].get<float>(), uv[i + 1].get<float>()});
        m.faces.push_back(f);
    }
    m.materials.clear();
    for (const Json& jm : j.value("materials", Json::array())) {
        MeshBuildMaterial mat;
        mat.name = jm.value("name", std::string("Material"));
        const std::vector<float> c = jm.value("color", std::vector<float>{0.8f, 0.8f, 0.8f, 1});
        if (c.size() == 4) mat.color = {c[0], c[1], c[2], c[3]};
        mat.metallic = jm.value("metallic", 0.0f);
        mat.roughness = jm.value("roughness", 0.6f);
        m.materials.push_back(mat);
    }
    if (m.materials.empty()) m.materials.push_back({});
    if (j.contains("rig") && j["rig"].is_object()) {
        const Json& jr = j["rig"];
        auto v3 = [](const Json& a) { return a.is_array() && a.size() == 3 ? Vec3{a[0].get<float>(), a[1].get<float>(), a[2].get<float>()} : Vec3{}; };
        for (const Json& b : jr.value("bones", Json::array()))
            m.rig.bones.push_back({b.value("name", std::string("Hueso")), b.value("parent", -1), v3(b.value("head", Json())), v3(b.value("tail", Json()))});
        for (const RigBone& b : m.rig.bones)
            if (b.parent >= int(m.rig.bones.size())) return fail("hueso con un padre inexistente");
        for (const Json& jc : jr.value("clips", Json::array())) {
            RigClip c;
            c.name = jc.value("name", std::string("Animación"));
            c.duration = jc.value("duration", 1.0f);
            c.loop = jc.value("loop", true);
            for (const Json& jt : jc.value("tracks", Json::array())) {
                RigTrack t;
                t.bone = jt.value("bone", -1);
                for (const Json& jk : jt.value("keys", Json::array())) {
                    RigKey k;
                    k.time = jk.value("t", 0.0f);
                    const std::vector<float> r = jk.value("r", std::vector<float>{0, 0, 0, 1});
                    if (r.size() == 4) k.rotation = {r[0], r[1], r[2], r[3]};
                    k.translation = v3(jk.value("p", Json()));
                    t.keys.push_back(k);
                }
                c.tracks.push_back(t);
            }
            m.rig.clips.push_back(c);
        }
        const std::vector<int> jj = jr.value("joints", std::vector<int>{});
        const std::vector<float> ww = jr.value("weights", std::vector<float>{});
        if (jj.size() == m.verts.size() * 4 && ww.size() == jj.size()) {
            m.rig.joints.resize(m.verts.size());
            m.rig.weights.resize(m.verts.size());
            for (size_t i = 0; i < m.verts.size(); ++i)
                for (size_t k = 0; k < 4; ++k) {
                    m.rig.joints[i][k] = std::clamp(jj[i * 4 + k], 0, std::max(0, int(m.rig.bones.size()) - 1));
                    m.rig.weights[i][k] = ww[i * 4 + k];
                }
        }
    }
    std::string why;
    if (!m.valid(&why)) {
        if (error) *error = "malla no válida: " + why;
        return false;
    }
    out = std::move(m);
    return true;
}

} // namespace titan
