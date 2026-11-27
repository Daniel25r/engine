// Importación de FBX (Mixamo, Prisma 3D, Maya, 3ds Max, Blender...) con ufbx (MIT, third_party/ufbx).
// Se convierte a MeshBuild -> GLB: mallas trianguladas con normales y UV, una parte por material, texturas de color,
// esqueleto con su pose de reposo y matrices de enlace, pesos (4 por vértice) y animaciones muestreadas a 30 fps.
// Ejes y unidades se normalizan a Y arriba y metros.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "titan/core/ModelImport.h"
#include "ufbx.h"

namespace titan {

namespace {
namespace fs = std::filesystem;

Vec3 v3(ufbx_vec3 v) { return {float(v.x), float(v.y), float(v.z)}; }
Quat q4(ufbx_quat q) { return {float(q.x), float(q.y), float(q.z), float(q.w)}; }

void toColumnMajor(const ufbx_matrix& m, float out[16]) {
    // ufbx_matrix: 3 columnas de ejes + traslación (cols[4]).
    const ufbx_vec3* c = m.cols;
    const float r[16] = {float(c[0].x), float(c[0].y), float(c[0].z), 0, float(c[1].x), float(c[1].y), float(c[1].z), 0,
                         float(c[2].x), float(c[2].y), float(c[2].z), 0, float(c[3].x), float(c[3].y), float(c[3].z), 1};
    std::memcpy(out, r, sizeof(r));
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::error_code ec;
    if (p.empty() || !fs::is_regular_file(p, ec)) return false;
    std::ifstream f(p, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

// Textura de color: incrustada en el FBX o junto al archivo. Solo PNG y JPEG (las demás se avisan).
void loadTexture(const ufbx_texture* t, const fs::path& dir, MeshBuildMaterial& m, ImportReport* report) {
    std::vector<uint8_t> data;
    if (t->content.size > 0) {
        const auto* p = static_cast<const uint8_t*>(t->content.data);
        data.assign(p, p + t->content.size);
    } else {
        const std::string name(t->filename.data, t->filename.length), rel(t->relative_filename.data, t->relative_filename.length);
        for (const fs::path& cand : {fs::path(name), dir / rel, dir / fs::path(name).filename(), dir / "textures" / fs::path(name).filename()})
            if (readFile(cand, data)) break;
        if (data.empty()) {
            if (report) report->warnings.push_back("no se encontró la textura '" + name + "'");
            return;
        }
    }
    if (data.size() > 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') m.imageMime = "image/png";
    else if (data.size() > 3 && data[0] == 0xFF && data[1] == 0xD8) m.imageMime = "image/jpeg";
    else {
        if (report) report->warnings.push_back("textura en un formato no admitido (solo PNG y JPEG): " + std::string(t->name.data, t->name.length));
        return;
    }
    m.image = std::move(data);
}
} // namespace

bool loadFbx(const std::string& path, MeshBuild& out, ImportReport* report, std::string* error) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        char buf[512];
        ufbx_format_error(buf, sizeof(buf), &err);
        if (error) *error = std::string("FBX no válido: ") + buf;
        return false;
    }
    struct Guard { ufbx_scene* s; ~Guard() { ufbx_free_scene(s); } } guard{scene};
    const fs::path dir = fs::path(path).parent_path();
    out = MeshBuild{};

    // 1. Huesos: los de las pieles y sus antepasados (sin la raíz de la escena), padres antes que hijos.
    std::set<const ufbx_node*> boneSet;
    std::map<const ufbx_node*, const ufbx_skin_cluster*> clusterOf;
    for (size_t i = 0; i < scene->skin_clusters.count; ++i) {
        const ufbx_skin_cluster* c = scene->skin_clusters.data[i];
        if (!c->bone_node) continue;
        clusterOf.emplace(c->bone_node, c);
        for (const ufbx_node* n = c->bone_node; n && !n->is_root; n = n->parent) boneSet.insert(n);
    }
    // Objetos animados sin piel (puertas, hélices, accesorios): si la malla o un antepasado tiene animación de
    // posición, rotación o escala, su cadena de nodos entra en el esqueleto como huesos rígidos.
    std::set<const ufbx_node*> animated;
    for (size_t l = 0; l < scene->anim_layers.count; ++l) {
        const ufbx_anim_layer* layer = scene->anim_layers.data[l];
        for (size_t i = 0; i < layer->anim_props.count; ++i) {
            const ufbx_anim_prop& ap = layer->anim_props.data[i];
            if (const ufbx_node* n = ufbx_as_node(ap.element))
                if (std::strncmp(ap.prop_name.data, "Lcl ", 4) == 0) animated.insert(n);
        }
    }
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        if (!node->mesh || node->mesh->skin_deformers.count) continue;
        bool moves = false;
        for (const ufbx_node* a = node; a && !a->is_root; a = a->parent) moves = moves || animated.count(a) > 0;
        if (moves)
            for (const ufbx_node* a = node; a && !a->is_root; a = a->parent) boneSet.insert(a);
    }
    // FBX solo de animación (Mixamo "Without Skin"): no hay pieles; el esqueleto son los nodos hueso o animados.
    if (scene->meshes.count == 0)
        for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
            const ufbx_node* n = scene->nodes.data[ni];
            if (n->is_root || !(n->bone || animated.count(n))) continue;
            for (const ufbx_node* a = n; a && !a->is_root; a = a->parent) boneSet.insert(a);
        }
    std::map<const ufbx_node*, int> boneIndex;
    std::vector<const ufbx_node*> boneNodes;
    std::vector<std::pair<const ufbx_node*, int>> stack{{scene->root_node, -1}};
    while (!stack.empty()) {
        auto [n, parent] = stack.back();
        stack.pop_back();
        int me = parent;
        if (boneSet.count(n)) {
            MeshBuildBone b;
            b.name = std::string(n->name.data, n->name.length);
            b.parent = parent;
            b.restLocal = v3(n->local_transform.translation);
            b.restRotation = q4(n->local_transform.rotation);
            b.restScale = v3(n->local_transform.scale);
            ufbx_matrix bind = n->node_to_world;
            if (auto it = clusterOf.find(n); it != clusterOf.end()) bind = it->second->bind_to_world;
            const ufbx_matrix inv = ufbx_matrix_invert(&bind);
            toColumnMajor(inv, b.inverseBind);
            b.hasInverseBind = true;
            b.restWorld = v3(bind.cols[3]);
            me = int(out.bones.size());
            boneIndex[n] = me;
            boneNodes.push_back(n);
            out.bones.push_back(b);
        }
        for (size_t i = n->children.count; i-- > 0;) stack.push_back({n->children.data[i], me});
    }

    // 2. Mallas: cada instancia en el mundo (los vértices de las pieles en su pose de enlace).
    std::map<const ufbx_material*, int> partOf;
    std::vector<std::unordered_map<std::string, uint32_t>> welds; // por parte: vértice -> índice
    std::vector<uint32_t> tri;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || !node->visible) continue;
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        const ufbx_matrix toWorld = node->geometry_to_world;
        const ufbx_matrix normalMat = ufbx_get_compatible_matrix_for_normals(node);
        // Malla sin piel en un esqueleto: sigue al hueso antepasado más cercano (p. ej. una espada en la mano).
        int rigidBone = out.bones.empty() ? -1 : 0;
        for (const ufbx_node* a = node; a; a = a->parent)
            if (auto it = boneIndex.find(a); it != boneIndex.end()) { rigidBone = it->second; break; }
        tri.resize(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            const uint32_t matIdx = mesh->face_material.count ? mesh->face_material.data[fi] : 0;
            const ufbx_material* mat = matIdx < mesh->materials.count ? mesh->materials.data[matIdx] : nullptr;
            auto it = partOf.find(mat);
            if (it == partOf.end()) {
                MeshBuildMaterial m;
                if (mat) {
                    m.name = std::string(mat->name.data, mat->name.length);
                    const ufbx_vec4 c = mat->pbr.base_color.value_vec4;
                    const float f = mat->pbr.base_factor.has_value ? float(mat->pbr.base_factor.value_real) : 1.0f;
                    m.color = {float(c.x) * f, float(c.y) * f, float(c.z) * f, 1};
                    if (mat->pbr.roughness.has_value) m.roughness = float(mat->pbr.roughness.value_real);
                    if (mat->pbr.metalness.has_value) m.metallic = float(mat->pbr.metalness.value_real);
                    if (const ufbx_texture* t = mat->pbr.base_color.texture ? mat->pbr.base_color.texture : mat->fbx.diffuse_color.texture) {
                        loadTexture(t, dir, m, report);
                        if (!m.image.empty()) m.color = {1, 1, 1, 1}; // el color lo da la textura
                    }
                }
                out.materials.push_back(m);
                out.parts.push_back({});
                out.parts.back().material = int(out.materials.size() - 1);
                it = partOf.emplace(mat, int(out.parts.size() - 1)).first;
                welds.emplace_back();
            }
            MeshBuildPart& part = out.parts[size_t(it->second)];
            const uint32_t nTri = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t k = 0; k < nTri * 3; ++k) {
                const uint32_t corner = tri[k];
                MeshBuildVertex v;
                v.position = v3(ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, corner)));
                if (mesh->vertex_normal.exists) {
                    const Vec3 nn = v3(ufbx_transform_direction(&normalMat, ufbx_get_vertex_vec3(&mesh->vertex_normal, corner)));
                    const float len = nn.length();
                    v.normal = len > 1e-8f ? nn * (1.0f / len) : Vec3{0, 1, 0};
                }
                if (mesh->vertex_uv.exists) {
                    const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
                    v.uv = {float(uv.x), 1.0f - float(uv.y)}; // glTF: V hacia abajo
                }
                if (!out.bones.empty()) {
                    int count = 0;
                    if (skin) {
                        const uint32_t vi = mesh->vertex_indices.data[corner];
                        const ufbx_skin_vertex sv = skin->vertices.data[vi];
                        float total = 0;
                        for (uint32_t w = 0; w < sv.num_weights && count < 4; ++w) {
                            const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                            const ufbx_skin_cluster* c = skin->clusters.data[sw.cluster_index];
                            auto bi = c->bone_node ? boneIndex.find(c->bone_node) : boneIndex.end();
                            if (bi == boneIndex.end() || sw.weight <= 0) continue;
                            v.joints[count] = uint16_t(bi->second);
                            v.weights[count] = float(sw.weight);
                            total += float(sw.weight);
                            ++count;
                        }
                        for (int w = 0; w < count; ++w) v.weights[w] /= std::max(total, 1e-6f);
                    }
                    if (count == 0) { // sin pesos: rígido con su hueso
                        v.joints[0] = uint16_t(std::max(rigidBone, 0));
                        v.weights[0] = 1;
                    }
                }
                // Vértices idénticos (misma posición, normal, UV y pesos) se comparten: malla conectada (la necesitan
                // la simplificación de los niveles de detalle y la caché de vértices de la GPU).
                std::string key(reinterpret_cast<const char*>(&v), sizeof(v));
                auto [it2, inserted] = welds[size_t(it->second)].emplace(std::move(key), uint32_t(part.vertices.size()));
                if (inserted) part.vertices.push_back(v);
                part.indices.push_back(it2->second);
            }
        }
    }
    out.parts.erase(std::remove_if(out.parts.begin(), out.parts.end(), [](const MeshBuildPart& p) { return p.indices.empty(); }),
                    out.parts.end());
    const bool animationOnly = out.parts.empty();
    if (animationOnly && (out.bones.empty() || !scene->anim_stacks.count)) {
        if (error) *error = "el FBX no tiene mallas ni animaciones";
        return false;
    }

    // 3. Animaciones: cada pila se muestrea a 30 fps (traslación, rotación y escala locales de cada hueso).
    const std::string stem = fs::path(path).stem().string();
    for (size_t si = 0; si < scene->anim_stacks.count && !out.bones.empty(); ++si) {
        const ufbx_anim_stack* st = scene->anim_stacks.data[si];
        const double t0 = st->time_begin, dur = std::max(0.0, st->time_end - st->time_begin);
        if (dur <= 0) continue;
        const int frames = std::max(2, int(std::ceil(dur * 30.0)) + 1);
        MeshBuildClip clip;
        clip.name = std::string(st->name.data, st->name.length);
        if (clip.name.empty() || clip.name == "mixamo.com" || clip.name == "Take 001" || scene->anim_stacks.count == 1)
            clip.name = scene->anim_stacks.count == 1 ? stem : stem + "_" + std::to_string(si + 1);
        for (size_t b = 0; b < boneNodes.size(); ++b) {
            MeshBuildChannel ct, cr, cs;
            ct.bone = cr.bone = cs.bone = int(b);
            ct.path = 0;
            cr.path = 1;
            cs.path = 2;
            bool movesT = false, movesR = false, movesS = false;
            const MeshBuildBone& rest = out.bones[b];
            Quat prev = rest.restRotation;
            for (int f = 0; f < frames; ++f) {
                const double t = std::min(dur, f / 30.0);
                const ufbx_transform tr = ufbx_evaluate_transform(st->anim, boneNodes[b], t0 + t);
                const Vec3 p = v3(tr.translation), s = v3(tr.scale);
                Quat q = q4(tr.rotation);
                if (q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0) q = {-q.x, -q.y, -q.z, -q.w}; // camino corto
                prev = q;
                for (MeshBuildChannel* c : {&ct, &cr, &cs}) c->times.push_back(float(t));
                ct.values.insert(ct.values.end(), {p.x, p.y, p.z});
                cr.values.insert(cr.values.end(), {q.x, q.y, q.z, q.w});
                cs.values.insert(cs.values.end(), {s.x, s.y, s.z});
                movesT = movesT || (p - rest.restLocal).length() > 1e-4f;
                movesR = movesR || std::fabs(q.x * rest.restRotation.x + q.y * rest.restRotation.y + q.z * rest.restRotation.z +
                                             q.w * rest.restRotation.w) < 0.99999f;
                movesS = movesS || (s - rest.restScale).length() > 1e-4f;
            }
            if (movesT) clip.channels.push_back(std::move(ct)); // solo lo que cambia: archivos más pequeños
            if (movesR) clip.channels.push_back(std::move(cr));
            if (movesS) clip.channels.push_back(std::move(cs));
        }
        if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
    }
    if (animationOnly && out.clips.empty()) {
        if (error) *error = "el FBX no tiene mallas y sus animaciones no mueven ningún hueso";
        return false;
    }
    if (animationOnly && report)
        report->warnings.push_back("solo animación: añádela a un personaje con el mismo esqueleto en su Animator (\"Animaciones de\")");
    return true;
}

} // namespace titan
