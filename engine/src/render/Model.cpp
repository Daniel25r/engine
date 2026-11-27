// Importación de modelos glTF 2.0 (.glb, o .gltf con los datos incrustados) con cgltf.
// Se leen todas las mallas de triángulos con su transformación de nodo ya aplicada, sus normales, UV,
// materiales PBR (color, metálico, rugosidad, emisión, doble cara) y las texturas de color incrustadas.
// También se leen los nodos, el primer esqueleto (skin) y los clips de animación para el skinning en la GPU.
#include <map>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#include "stb_image.h"
#include "meshoptimizer.h"
#include "titan/core/FileSource.h"
#include "titan/core/Geometry.h"
#include "titan/core/Log.h"
#include "titan/render/Renderer.h"

namespace titan {

namespace {
float toSrgb(float linear) { return std::pow(std::max(linear, 0.0f), 1.0f / 2.2f); }
} // namespace

int Renderer::loadModel(const std::string& path) {
    for (size_t i = 0; i < models_.size(); ++i)
        if (models_[i].path == path) return models_[i].ok ? static_cast<int>(i) : -1;
    Model m;
    m.path = path;
    m.ok = buildModel(m);
    models_.push_back(std::move(m));
    return models_.back().ok ? static_cast<int>(models_.size() - 1) : -1;
}

void Renderer::releaseModelGpu(Model& m) {
    for (ModelPart& p : m.parts) {
        if (p.mesh.vbo) glDeleteBuffers(1, &p.mesh.vbo);
        if (p.mesh.ibo) glDeleteBuffers(1, &p.mesh.ibo);
        if (p.mesh.vao) glDeleteVertexArrays(1, &p.mesh.vao);
        if (p.skinVbo) glDeleteBuffers(1, &p.skinVbo);
    }
    m.parts.clear();
}

int Renderer::setModelData(const std::string& path, std::vector<uint8_t> glb) {
    size_t i = 0;
    while (i < models_.size() && models_[i].path != path) ++i;
    if (i == models_.size()) {
        models_.emplace_back();
        models_.back().path = path;
    }
    Model& m = models_[i];
    releaseModelGpu(m);
    m.memory = std::move(glb);
    m.ok = buildModel(m);
    return m.ok ? static_cast<int>(i) : -1;
}

void Renderer::unloadModel(const std::string& path) {
    for (Model& m : models_)
        if (m.path == path) {
            releaseModelGpu(m);
            m.memory.clear();
            m.memory.shrink_to_fit();
            m.collisionVerts.clear();
            m.collisionIndices.clear();
            m.ok = false;
        }
}

bool Renderer::buildModel(Model& m) {
    m.collisionVerts.clear();
    m.collisionIndices.clear();
    m.parts.clear();
    m.triangles = 0;
    std::vector<uint8_t> bytes;
    if (!m.memory.empty()) bytes = m.memory;
    else if (!files_ || !files_->read(m.path, bytes)) {
        TITAN_ERROR("render", "No se encontró el modelo '%s'", m.path.c_str());
        return false;
    }
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&opt, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
        TITAN_ERROR("render", "'%s' no es un glTF/GLB válido", m.path.c_str());
        return false;
    }
    if (cgltf_load_buffers(&opt, data, nullptr) != cgltf_result_success) {
        TITAN_ERROR("render", "'%s' usa archivos externos (.bin o imágenes): expórtalo como GLB", m.path.c_str());
        cgltf_free(data);
        return false;
    }

    // ---- Nodos (pose de reposo), esqueletos y clips de animación.
    m.nodes.assign(data->nodes_count, {});
    m.skins.clear();
    m.clips.clear();
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        AnimNode& an = m.nodes[i];
        an.name = n.name ? n.name : "";
        an.parent = n.parent ? static_cast<int>(n.parent - data->nodes) : -1;
        if (n.has_matrix) {
            Mat4 mm;
            std::memcpy(mm.m, n.matrix, sizeof(mm.m));
            mm.decompose(an.t, an.r, an.s);
        } else {
            if (n.has_translation) an.t = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) an.r = Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
            if (n.has_scale) an.s = {n.scale[0], n.scale[1], n.scale[2]};
        }
    }
    for (size_t si = 0; si < data->skins_count; ++si) {
        const cgltf_skin& sk = data->skins[si];
        Skin skin;
        for (size_t j = 0; j < sk.joints_count; ++j) {
            skin.joints.push_back(static_cast<int>(sk.joints[j] - data->nodes));
            Mat4 ib;
            if (sk.inverse_bind_matrices) cgltf_accessor_read_float(sk.inverse_bind_matrices, j, ib.m, 16);
            skin.inverseBind.push_back(ib);
        }
        m.skins.push_back(std::move(skin));
    }
    for (size_t ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& a = data->animations[ai];
        AnimClip clip;
        clip.name = a.name ? a.name : "Clip " + std::to_string(ai + 1);
        for (size_t ci = 0; ci < a.channels_count; ++ci) {
            const cgltf_animation_channel& ch = a.channels[ci];
            if (!ch.target_node || !ch.sampler) continue;
            AnimChannel c;
            c.node = static_cast<int>(ch.target_node - data->nodes);
            if (ch.target_path == cgltf_animation_path_type_translation) c.path = 0;
            else if (ch.target_path == cgltf_animation_path_type_rotation) c.path = 1;
            else if (ch.target_path == cgltf_animation_path_type_scale) c.path = 2;
            else continue; // pesos de morph targets: pendiente
            const int comps = c.path == 1 ? 4 : 3;
            const cgltf_accessor* in = ch.sampler->input;
            const cgltf_accessor* out = ch.sampler->output;
            const bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            c.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
            c.times.resize(in->count);
            for (size_t k = 0; k < in->count; ++k) cgltf_accessor_read_float(in, k, &c.times[k], 1);
            c.values.resize(in->count * comps);
            for (size_t k = 0; k < in->count; ++k) // en "cubic spline" cada clave trae tangentes: se usa el valor central
                cgltf_accessor_read_float(out, cubic ? k * 3 + 1 : k, &c.values[k * comps], comps);
            if (!c.times.empty()) clip.duration = std::max(clip.duration, c.times.back());
            clip.channels.push_back(std::move(c));
        }
        m.clips.push_back(std::move(clip));
    }
    const cgltf_skin* mainSkin = data->skins_count ? &data->skins[0] : nullptr;
    if (mainSkin && mainSkin->joints_count > static_cast<size_t>(kMaxJoints))
        TITAN_WARN("render", "%s: el esqueleto tiene %zu huesos (máximo %d); se mostrará sin animar", m.path.c_str(),
                   mainSkin->joints_count, kMaxJoints);

    // Texturas incrustadas (una por imagen, compartidas entre materiales).
    std::vector<int> imageTex(data->images_count, -1);
    auto textureOf = [&](const cgltf_texture* t) -> TextureHandle {
        if (!t || !t->image) return kWhiteTexture;
        const size_t idx = static_cast<size_t>(t->image - data->images);
        if (imageTex[idx] >= 0) return static_cast<TextureHandle>(imageTex[idx]);
        imageTex[idx] = kWhiteTexture;
        const cgltf_buffer_view* bv = t->image->buffer_view;
        if (!bv || !bv->buffer->data) return kWhiteTexture;
        const auto* src = static_cast<const stbi_uc*>(bv->buffer->data) + bv->offset;
        int w = 0, h = 0, ch = 0;
        stbi_uc* px = stbi_load_from_memory(src, static_cast<int>(bv->size), &w, &h, &ch, 4);
        if (!px) {
            TITAN_WARN("render", "%s: no se pudo decodificar una textura (%s)", m.path.c_str(), stbi_failure_reason());
            return kWhiteTexture;
        }
        Texture tex;
        glGenTextures(1, &tex.gl);
        glBindTexture(GL_TEXTURE_2D, tex.gl);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        stbi_image_free(px);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        tex.width = w;
        tex.height = h;
        textures_.push_back(tex);
        imageTex[idx] = static_cast<int>(textures_.size() - 1);
        return static_cast<TextureHandle>(imageTex[idx]);
    };

    bool firstPoint = true;
    for (size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        Mat4 world;
        if (!node->skin) cgltf_node_transform_world(node, world.m); // con piel, la pose la daría el esqueleto
        const Mat4 inv = world.inverted();
        for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) jnt = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) wgt = at.data;
            }
            if (!pos || pos->count == 0) continue;
            std::vector<MeshVertex> verts(pos->count);
            for (size_t k = 0; k < pos->count; ++k) {
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
                cgltf_accessor_read_float(pos, k, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, k, n, 3);
                if (uv) cgltf_accessor_read_float(uv, k, t, 2);
                verts[k].position = world.transformPoint({p[0], p[1], p[2]});
                // Normales con la inversa traspuesta (correcto con escalas no uniformes).
                const Vec3 nn{inv.m[0] * n[0] + inv.m[1] * n[1] + inv.m[2] * n[2],
                              inv.m[4] * n[0] + inv.m[5] * n[1] + inv.m[6] * n[2],
                              inv.m[8] * n[0] + inv.m[9] * n[1] + inv.m[10] * n[2]};
                verts[k].normal = nn.normalized();
                verts[k].uv = {t[0], t[1]};
                const Vec3& q = verts[k].position;
                if (firstPoint) {
                    m.min = m.max = q;
                    firstPoint = false;
                }
                m.min = {std::min(m.min.x, q.x), std::min(m.min.y, q.y), std::min(m.min.z, q.z)};
                m.max = {std::max(m.max.x, q.x), std::max(m.max.y, q.y), std::max(m.max.z, q.z)};
            }
            std::vector<uint32_t> idx;
            if (prim.indices) {
                idx.resize(prim.indices->count);
                for (size_t k = 0; k < idx.size(); ++k) idx[k] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
            } else {
                idx.resize(pos->count);
                for (size_t k = 0; k < idx.size(); ++k) idx[k] = static_cast<uint32_t>(k);
            }
            if (!nrm) {
                // Sin normales en el archivo: se calculan a partir de las caras.
                for (auto& v : verts) v.normal = {};
                for (size_t k = 0; k + 2 < idx.size(); k += 3) {
                    MeshVertex &a = verts[idx[k]], &b = verts[idx[k + 1]], &c = verts[idx[k + 2]];
                    const Vec3 f = cross(b.position - a.position, c.position - a.position);
                    a.normal += f; b.normal += f; c.normal += f;
                }
                for (auto& v : verts) v.normal = v.normal.normalized();
            }

            // Copia en la CPU de los triángulos (colisión de malla). Los personajes con esqueleto también la tienen
            // en su pose de reposo, aunque para ellos se recomienda una cápsula.
            const uint32_t base = static_cast<uint32_t>(m.collisionVerts.size());
            for (const MeshVertex& v : verts) m.collisionVerts.push_back(v.position);
            for (uint32_t i : idx) m.collisionIndices.push_back(base + i);

            ModelPart part;
            part.skinned = node->skin && node->skin == mainSkin && jnt && wgt &&
                           mainSkin->joints_count <= static_cast<size_t>(kMaxJoints);
            glGenVertexArrays(1, &part.mesh.vao);
            glBindVertexArray(part.mesh.vao);
            glGenBuffers(1, &part.mesh.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, part.mesh.vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(MeshVertex), verts.data(), GL_STATIC_DRAW);
            // Niveles de detalle (meshoptimizer): 50 % y 25 % de los triángulos, con los bordes fijos para que no se
            // abran grietas entre partes. Solo en partes con bastantes triángulos y si la simplificación ahorra.
            std::vector<uint32_t> allIdx = idx;
            part.lodCount[0] = int(idx.size());
            if (idx.size() >= 256 * 3) {
                std::vector<uint32_t> prev = idx, lod(idx.size());
                for (int level = 1; level < 3; ++level) {
                    const size_t target = (idx.size() / 3 >> level) * 3;
                    float err = 0;
                    const size_t n = meshopt_simplify(lod.data(), idx.data(), idx.size(), &verts[0].position.x, verts.size(),
                                                      sizeof(MeshVertex), target, 0.05f, meshopt_SimplifyLockBorder, &err);
                    if (n == 0 || n > prev.size() * 9 / 10) break; // no ahorra lo suficiente
                    part.lodFirst[level] = int(allIdx.size());
                    part.lodCount[level] = int(n);
                    allIdx.insert(allIdx.end(), lod.begin(), lod.begin() + long(n));
                    prev.assign(lod.begin(), lod.begin() + long(n));
                    part.lods = level + 1;
                }
            }
            glGenBuffers(1, &part.mesh.ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.mesh.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIdx.size() * sizeof(uint32_t), allIdx.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, position));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, uv));
            if (part.skinned) {
                // 8 floats por vértice: índices de 4 huesos y sus 4 pesos.
                std::vector<float> sw(pos->count * 8, 0.0f);
                for (size_t k = 0; k < pos->count; ++k) {
                    cgltf_uint j[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jnt, k, j, 4);
                    float w[4] = {1, 0, 0, 0};
                    cgltf_accessor_read_float(wgt, k, w, 4);
                    const float sum = w[0] + w[1] + w[2] + w[3];
                    for (int q = 0; q < 4; ++q) {
                        sw[k * 8 + q] = static_cast<float>(j[q]);
                        sw[k * 8 + 4 + q] = sum > 0 ? w[q] / sum : (q == 0 ? 1.0f : 0.0f);
                    }
                }
                glGenBuffers(1, &part.skinVbo);
                glBindBuffer(GL_ARRAY_BUFFER, part.skinVbo);
                glBufferData(GL_ARRAY_BUFFER, sw.size() * sizeof(float), sw.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(4);
                glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
            }
            glBindVertexArray(0);
            part.mesh.indexCount = static_cast<int>(idx.size());

            if (const cgltf_material* mat = prim.material) {
                if (mat->has_pbr_metallic_roughness) {
                    const auto& pbr = mat->pbr_metallic_roughness;
                    // glTF guarda los factores en espacio lineal; el sombreador espera sRGB.
                    part.color = {toSrgb(pbr.base_color_factor[0]), toSrgb(pbr.base_color_factor[1]),
                                  toSrgb(pbr.base_color_factor[2]), pbr.base_color_factor[3]};
                    part.metallic = pbr.metallic_factor;
                    part.roughness = pbr.roughness_factor;
                    part.maps.base = textureOf(pbr.base_color_texture.texture);
                    part.maps.metallicRoughness = textureOf(pbr.metallic_roughness_texture.texture);
                }
                part.maps.normal = textureOf(mat->normal_texture.texture);
                part.maps.emission = textureOf(mat->emissive_texture.texture);
                part.maps.occlusion = textureOf(mat->occlusion_texture.texture);
                part.emission = {toSrgb(mat->emissive_factor[0]), toSrgb(mat->emissive_factor[1]), toSrgb(mat->emissive_factor[2]), 1};
                part.doubleSided = mat->double_sided;
                if (mat->alpha_mode == cgltf_alpha_mode_mask) part.alphaCutoff = mat->alpha_cutoff;
                if (mat->alpha_mode != cgltf_alpha_mode_blend) part.color.a = 1;
            }
            m.triangles += part.mesh.indexCount / 3;
            m.parts.push_back(part);
        }
    }
    cgltf_free(data);
    if (m.parts.empty()) {
        if (!m.clips.empty()) { // biblioteca de animaciones (esqueleto sin malla): sirve para otros modelos
            TITAN_INFO("render", "'%s': %zu animaciones sin malla (para usar en otros modelos)", m.path.c_str(), m.clips.size());
            return true;
        }
        TITAN_ERROR("render", "'%s' no contiene mallas de triángulos", m.path.c_str());
        return false;
    }
    if (m.path.rfind("voxel/", 0) != 0) /* los chunks del mundo de bloques se cargan a cientos: sin mensaje */
        TITAN_INFO("render", "Modelo '%s' cargado: %zu partes, %d triángulos", m.path.c_str(), m.parts.size(), m.triangles);
    return true;
}

bool Renderer::modelTriangles(int model, std::vector<Vec3>& verts, std::vector<uint32_t>& indices) const {
    if (model < 0 || model >= static_cast<int>(models_.size()) || !models_[model].ok) return false;
    verts = models_[model].collisionVerts;
    indices = models_[model].collisionIndices;
    return !indices.empty();
}

bool Renderer::modelBounds(int model, Vec3& min, Vec3& max) const {
    if (model < 0 || model >= static_cast<int>(models_.size()) || !models_[model].ok) return false;
    min = models_[model].min;
    max = models_[model].max;
    return true;
}

int Renderer::modelTriangles(int model) const {
    return model < 0 || model >= static_cast<int>(models_.size()) ? 0 : models_[model].triangles;
}

int Renderer::addAnimationsFrom(int target, int source) {
    if (target < 0 || source < 0 || target == source || target >= int(models_.size()) || source >= int(models_.size())) return 0;
    if (!mergedAnimations_.insert({target, source}).second) return 0;
    Model& dst = models_[size_t(target)];
    const Model& src = models_[size_t(source)];
    std::map<std::string, int> byName;
    for (size_t i = 0; i < dst.nodes.size(); ++i)
        if (!dst.nodes[i].name.empty()) byName.emplace(dst.nodes[i].name, int(i));
    int added = 0;
    for (const AnimClip& clip : src.clips) {
        if (modelAnimationIndex(target, clip.name) >= 0 && !clip.name.empty()) continue; // ya tiene uno con ese nombre
        AnimClip copy;
        copy.name = clip.name;
        copy.duration = clip.duration;
        for (const AnimChannel& ch : clip.channels) {
            if (ch.node < 0 || ch.node >= int(src.nodes.size())) continue;
            auto it = byName.find(src.nodes[size_t(ch.node)].name);
            if (it == byName.end()) continue; // hueso que este modelo no tiene
            AnimChannel c = ch;
            c.node = it->second;
            copy.channels.push_back(std::move(c));
        }
        if (copy.channels.empty()) continue;
        dst.clips.push_back(std::move(copy));
        ++added;
    }
    if (added) TITAN_INFO("render", "%d animaciones de '%s' añadidas a '%s'", added, src.path.c_str(), dst.path.c_str());
    else TITAN_WARN("render", "'%s' no comparte huesos con '%s': no se añadió ninguna animación", src.path.c_str(), dst.path.c_str());
    return added;
}

// Nivel de detalle según lo que ocupa el modelo en pantalla: radio de su caja / distancia × escala de la proyección.
int Renderer::chooseLod(const Model& m, const ModelPart& p, const Mat4& world) const {
    if (p.lods <= 1 || !lodEnabled_) return 0;
    const Vec3 c = world.transformPoint((m.min + m.max) * 0.5f);
    const float scale = std::max({world.transformDirection({1, 0, 0}).length(), world.transformDirection({0, 1, 0}).length(),
                                  world.transformDirection({0, 0, 1}).length()});
    const float radius = (m.max - m.min).length() * 0.5f * scale;
    const float dist = std::max((c - cameraPos_).length(), 1e-3f);
    const float focal = projection_.m[15] == 0.0f ? std::fabs(projection_.m[5]) : 1.0f;
    const float coverage = radius / dist * focal / std::max(quality_.lodBias, 0.1f); // ~fracción de media pantalla
    const int lod = coverage < 0.08f ? 2 : coverage < 0.2f ? 1 : 0;
    return std::min(lod, p.lods - 1);
}

void Renderer::drawModel(int model, const Mat4& world, const MeshRenderer& mat, const MaterialTextures& o,
                         const std::vector<Mat4>* joints) {
    if (model < 0 || model >= static_cast<int>(models_.size())) return;
    auto pick = [](TextureHandle over, TextureHandle own) { return over != kWhiteTexture ? over : own; };
    for (const ModelPart& p : models_[model].parts) {
        currentJoints_ = p.skinned ? joints : nullptr;
        Surface s;
        s.color = {p.color.r * mat.color.r, p.color.g * mat.color.g, p.color.b * mat.color.b, p.color.a * mat.color.a};
        s.metallic = p.metallic;
        s.roughness = p.roughness;
        s.emission = {std::max(p.emission.r, mat.emission.r), std::max(p.emission.g, mat.emission.g),
                      std::max(p.emission.b, mat.emission.b), 1};
        s.grid = mat.prototypeGrid;
        s.unlit = mat.unlit;
        s.doubleSided = p.doubleSided || mat.doubleSided;
        s.maps = {pick(o.base, p.maps.base), pick(o.normal, p.maps.normal), pick(o.metallicRoughness, p.maps.metallicRoughness),
                  pick(o.emission, p.maps.emission), pick(o.occlusion, p.maps.occlusion)};
        s.tiling = mat.tiling;
        s.offset = mat.offset;
        s.alphaCutoff = mat.alphaClip ? mat.alphaCutoff : p.alphaCutoff;
        s.normalStrength = mat.normalStrength;
        s.clearcoat = mat.clearcoat;
        s.clearcoatRoughness = mat.clearcoatRoughness;
        s.graph = mat.graph && mat.graph->program().ok() ? &mat.graph->program() : nullptr;
        s.maps.graph0 = o.graph0;
        s.maps.graph1 = o.graph1;
        const int lod = chooseLod(models_[model], p, world);
        s.firstIndex = p.lodFirst[lod];
        drawSurface(p.mesh.vao, p.lodCount[lod] > 0 ? p.lodCount[lod] : p.mesh.indexCount, true, world, s);
    }
    currentJoints_ = nullptr;
}

void Renderer::drawModelShadow(int model, const Mat4& world, const std::vector<Mat4>* joints) {
    if (model < 0 || model >= static_cast<int>(models_.size())) return;
    glUniformMatrix4fv(uShadowModel_, 1, GL_FALSE, world.m);
    for (const ModelPart& p : models_[model].parts) {
        const bool skin = p.skinned && joints && !joints->empty();
        glUniform1i(uShadowSkinned_, skin ? 1 : 0);
        glUniform1i(uShadowJoints_, 2);
        if (skin) uploadJoints(*joints);
        glBindVertexArray(p.mesh.vao);
        const int lod = chooseLod(models_[model], p, world);
        glDrawElements(GL_TRIANGLES, p.lodCount[lod] > 0 ? p.lodCount[lod] : p.mesh.indexCount, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(size_t(p.lodFirst[lod]) * 4));
        stats_.drawCalls++;
    }
}

// ---------------------------------------------------------------- Animación

int Renderer::modelAnimationCount(int model) const {
    return model < 0 || model >= static_cast<int>(models_.size()) ? 0 : static_cast<int>(models_[model].clips.size());
}

std::string Renderer::modelAnimationName(int model, int anim) const {
    if (anim < 0 || anim >= modelAnimationCount(model)) return {};
    return models_[model].clips[anim].name;
}

float Renderer::modelAnimationDuration(int model, int anim) const {
    if (anim < 0 || anim >= modelAnimationCount(model)) return 0;
    return models_[model].clips[anim].duration;
}

int Renderer::modelAnimationIndex(int model, const std::string& name) const {
    const int n = modelAnimationCount(model);
    if (n == 0) return -1;
    if (name.empty()) return 0;
    for (int i = 0; i < n; ++i)
        if (models_[model].clips[i].name == name) return i;
    return -1;
}

bool Renderer::modelHasSkin(int model) const {
    if (model < 0 || model >= static_cast<int>(models_.size())) return false;
    for (const ModelPart& p : models_[model].parts)
        if (p.skinned) return true;
    return false;
}

namespace {
// Valor del canal en el instante t (interpolación lineal; esférica normalizada para rotaciones).
void sample(const std::vector<float>& times, const std::vector<float>& values, int comps, bool step, float t, float* out) {
    const size_t n = times.size();
    if (n == 0) return;
    if (t <= times.front() || n == 1) { std::copy_n(values.begin(), comps, out); return; }
    if (t >= times.back()) { std::copy_n(values.begin() + (n - 1) * comps, comps, out); return; }
    const size_t k = static_cast<size_t>(std::upper_bound(times.begin(), times.end(), t) - times.begin()) - 1;
    const float* a = &values[k * comps];
    const float* b = &values[(k + 1) * comps];
    if (step) { std::copy_n(a, comps, out); return; }
    const float f = (t - times[k]) / std::max(times[k + 1] - times[k], 1e-6f);
    float sign = 1;
    if (comps == 4 && a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0) sign = -1; // camino corto
    for (int c = 0; c < comps; ++c) out[c] = a[c] + (b[c] * sign - a[c]) * f;
}
} // namespace

bool Renderer::computePose(int model, int anim, float time, bool loop, std::vector<Mat4>& joints) const {
    return computeBlendedPose(model, anim, time, -1, 0, 0, loop, joints);
}

void Renderer::sampleClip(const Model& m, int anim, float time, bool loop, std::vector<AnimNode>& local) const {
    local = m.nodes;
    if (anim >= 0 && anim < static_cast<int>(m.clips.size())) {
        const AnimClip& clip = m.clips[anim];
        float t = time;
        if (clip.duration > 0) t = loop ? std::fmod(std::max(time, 0.0f), clip.duration) : std::min(time, clip.duration);
        for (const AnimChannel& c : clip.channels) {
            if (c.node < 0 || c.node >= static_cast<int>(local.size())) continue;
            float v[4] = {0, 0, 0, 1};
            sample(c.times, c.values, c.path == 1 ? 4 : 3, c.step, t, v);
            AnimNode& n = local[c.node];
            if (c.path == 0) n.t = {v[0], v[1], v[2]};
            else if (c.path == 1) n.r = Quat{v[0], v[1], v[2], v[3]}.normalized();
            else n.s = {v[0], v[1], v[2]};
        }
    }
}

bool Renderer::computeBlendedPose(int model, int anim, float time, int fromAnim, float fromTime, float fromWeight,
                                  bool loop, std::vector<Mat4>& joints) const {
    joints.clear();
    if (model < 0 || model >= static_cast<int>(models_.size())) return false;
    const Model& m = models_[model];
    if (m.skins.empty() || !modelHasSkin(model)) return false;
    std::vector<AnimNode> local;
    sampleClip(m, anim, time, loop, local);
    if (fromWeight > 0.001f && fromAnim >= 0) {
        // Transición: mezcla con la pose del clip anterior (lineal en T/S, esférica normalizada en R).
        std::vector<AnimNode> from;
        sampleClip(m, fromAnim, fromTime, loop, from);
        const float w = std::min(fromWeight, 1.0f);
        for (size_t i = 0; i < local.size(); ++i) {
            AnimNode& a = local[i];
            const AnimNode& b = from[i];
            a.t = a.t + (b.t - a.t) * w;
            a.s = a.s + (b.s - a.s) * w;
            const float sign = (a.r.x * b.r.x + a.r.y * b.r.y + a.r.z * b.r.z + a.r.w * b.r.w) < 0 ? -1.0f : 1.0f;
            a.r = Quat{a.r.x + (b.r.x * sign - a.r.x) * w, a.r.y + (b.r.y * sign - a.r.y) * w,
                       a.r.z + (b.r.z * sign - a.r.z) * w, a.r.w + (b.r.w * sign - a.r.w) * w}.normalized();
        }
    }
    // Matrices globales recorriendo la jerarquía de nodos (con memoria: cada nodo una vez).
    std::vector<Mat4> global(local.size());
    std::vector<char> done(local.size(), 0);
    std::function<const Mat4&(int)> world = [&](int i) -> const Mat4& {
        if (!done[i]) {
            const Mat4 l = Mat4::trs(local[i].t, local[i].r, local[i].s);
            global[i] = local[i].parent >= 0 ? world(local[i].parent) * l : l;
            done[i] = 1;
        }
        return global[i];
    };
    const Skin& skin = m.skins[0];
    joints.resize(skin.joints.size());
    for (size_t j = 0; j < skin.joints.size(); ++j) joints[j] = world(skin.joints[j]) * skin.inverseBind[j];
    return true;
}

} // namespace titan
