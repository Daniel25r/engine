// Importación de modelos glTF 2.0 (.glb, o .gltf con los datos incrustados) con cgltf.
// Se leen todas las mallas de triángulos con su transformación de nodo ya aplicada, sus normales, UV,
// materiales PBR (color, metálico, rugosidad, emisión, doble cara) y las texturas de color incrustadas.
// Pendiente: animaciones y esqueletos (los modelos con piel se muestran en su pose de reposo).
#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#include "stb_image.h"
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

bool Renderer::buildModel(Model& m) {
    m.parts.clear();
    m.triangles = 0;
    std::vector<uint8_t> bytes;
    if (!files_ || !files_->read(m.path, bytes)) {
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
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
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

            ModelPart part;
            glGenVertexArrays(1, &part.mesh.vao);
            glBindVertexArray(part.mesh.vao);
            glGenBuffers(1, &part.mesh.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, part.mesh.vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(MeshVertex), verts.data(), GL_STATIC_DRAW);
            glGenBuffers(1, &part.mesh.ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.mesh.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, position));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, uv));
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
                    part.texture = textureOf(pbr.base_color_texture.texture);
                }
                part.emission = {toSrgb(mat->emissive_factor[0]), toSrgb(mat->emissive_factor[1]), toSrgb(mat->emissive_factor[2]), 1};
                part.doubleSided = mat->double_sided;
                if (mat->alpha_mode != cgltf_alpha_mode_blend) part.color.a = 1; // MASK aún se trata como opaco
            }
            m.triangles += part.mesh.indexCount / 3;
            m.parts.push_back(part);
        }
    }
    cgltf_free(data);
    if (m.parts.empty()) {
        TITAN_ERROR("render", "'%s' no contiene mallas de triángulos", m.path.c_str());
        return false;
    }
    TITAN_INFO("render", "Modelo '%s' cargado: %zu partes, %d triángulos", m.path.c_str(), m.parts.size(), m.triangles);
    return true;
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

void Renderer::drawModel(int model, const Mat4& world, const MeshRenderer& mat, TextureHandle overrideTex) {
    if (model < 0 || model >= static_cast<int>(models_.size())) return;
    for (const ModelPart& p : models_[model].parts) {
        const Color c{p.color.r * mat.color.r, p.color.g * mat.color.g, p.color.b * mat.color.b, p.color.a * mat.color.a};
        const Color e{std::max(p.emission.r, mat.emission.r), std::max(p.emission.g, mat.emission.g),
                      std::max(p.emission.b, mat.emission.b), 1};
        drawSurface(p.mesh.vao, p.mesh.indexCount, true, world, c, p.metallic, p.roughness, e, mat.prototypeGrid, mat.unlit,
                    overrideTex != kWhiteTexture ? overrideTex : p.texture, p.doubleSided);
    }
}

void Renderer::drawModelShadow(int model, const Mat4& world) {
    if (model < 0 || model >= static_cast<int>(models_.size())) return;
    glUniformMatrix4fv(uShadowModel_, 1, GL_FALSE, world.m);
    for (const ModelPart& p : models_[model].parts) {
        glBindVertexArray(p.mesh.vao);
        glDrawElements(GL_TRIANGLES, p.mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        stats_.drawCalls++;
    }
}

} // namespace titan
