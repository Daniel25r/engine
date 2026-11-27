#include "titan/runtime/SceneRenderer.h"

#include "titan/core/Particles.h"
#include "titan/core/TerrainTools.h"

#include <algorithm>
#include <cmath>

namespace titan {

bool SceneRenderer::cameraView(Scene& scene, float aspect, SceneView& out, Entity* cameraEntity) {
    Entity best;
    int bestPriority = 0;
    scene.world().each<Camera>([&](Entity e, Camera& c) {
        if (!scene.isActiveInHierarchy(e)) return;
        if (best.isNull() || c.priority > bestPriority || (c.priority == bestPriority && e.index < best.index)) {
            best = e;
            bestPriority = c.priority;
        }
    });
    if (cameraEntity) *cameraEntity = best;
    if (best.isNull()) return false;
    const Camera& cam = *scene.world().get<Camera>(best);
    out.view = scene.transform(best).worldMatrix().inverted();
    out.projection = cam.projectionMatrix(aspect);
    out.clearColor = cam.clearColor;
    out.perspective = cam.projection == Camera::Projection::Perspective;
    return true;
}

namespace {
Mat4 lookAt(const Vec3& eye, const Vec3& target) {
    const Vec3 f = (target - eye).normalized();
    Vec3 r = cross(f, std::fabs(f.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}).normalized();
    const Vec3 u = cross(r, f);
    Mat4 m;
    m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z;
    m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
    m.m[8] = -f.x; m.m[9] = -f.y; m.m[10] = -f.z;
    m.m[12] = eye.x; m.m[13] = eye.y; m.m[14] = eye.z;
    return m.inverted();
}
} // namespace

namespace {
// Dos primitivas se pueden dibujar juntas si son la misma malla y su material es idéntico y sin texturas.
bool instanceable(const MeshRenderer& m, const MaterialTextures& t) {
    return m.model.isNull() && t.base == kWhiteTexture && t.normal == kWhiteTexture && t.metallicRoughness == kWhiteTexture &&
           t.emission == kWhiteTexture && t.occlusion == kWhiteTexture && !m.alphaClip;
}
bool sameMaterial(const MeshRenderer& a, const MeshRenderer& b) {
    return a.mesh == b.mesh && a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b &&
           a.color.a == b.color.a && a.metallic == b.metallic && a.roughness == b.roughness && a.emission.r == b.emission.r &&
           a.emission.g == b.emission.g && a.emission.b == b.emission.b && a.prototypeGrid == b.prototypeGrid &&
           a.unlit == b.unlit && a.doubleSided == b.doubleSided;
}
} // namespace

void SceneRenderer::groupInstances(std::vector<MeshItem>& items, std::vector<InstanceBatch>& out) {
    out.clear();
    if (!instancing) return;
    // 1. Grupos de objetos iguales.
    std::vector<int> groupOf(items.size(), -1);
    std::vector<std::vector<size_t>> members;
    for (size_t i = 0; i < items.size(); ++i) {
        const MeshItem& it = items[i];
        if (it.model >= 0 || !instanceable(*it.mesh, it.maps)) continue;
        int g = -1;
        for (size_t k = 0; k < members.size() && g < 0; ++k)
            if (sameMaterial(*items[members[k][0]].mesh, *it.mesh)) g = static_cast<int>(k);
        if (g < 0) {
            members.emplace_back();
            g = static_cast<int>(members.size() - 1);
        }
        members[size_t(g)].push_back(i);
        groupOf[i] = g;
    }
    // 2. Los grupos de 2 o más se dibujan juntos; los sueltos, como siempre.
    std::vector<MeshItem> rest;
    rest.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
        if (groupOf[i] < 0 || members[size_t(groupOf[i])].size() < 2) rest.push_back(items[i]);
    for (const auto& m : members) {
        if (m.size() < 2) continue;
        InstanceBatch b{items[m[0]].mesh, {}};
        b.worlds.reserve(m.size());
        for (size_t i : m) b.worlds.push_back(*items[i].world);
        out.push_back(std::move(b));
    }
    items.swap(rest);
}

void SceneRenderer::render(Scene& scene, Renderer& renderer, const Viewport& viewport, const SceneView& view,
                           const TextureResolver& textures, const ModelResolver& models,
                           const MaterialResolver& materials) {
    // ---- Ambiente: el primer Environment activo, o valores por defecto.
    SceneLighting L;
    bool hasEnv = false;
    scene.world().each<Environment>([&](Entity e, Environment& env) {
        if (!hasEnv && scene.isActiveInHierarchy(e)) {
            L.env = env;
            hasEnv = true;
        }
    });
    if (!hasEnv && !view.perspective) {
        // 2D sin Environment: sin cielo, sin niebla y con luz ambiente neutra.
        L.env.sky = false;
        L.env.fogDensity = 0;
        L.env.shadows = false;
    }
    L.drawSky = L.env.sky && view.perspective;

    bool anyLight = false, haveSun = false;
    scene.world().each<Light, Transform>([&](Entity e, Light& l, Transform& t) {
        if (!scene.isActiveInHierarchy(e)) return;
        anyLight = true;
        const Color c{l.color.r * l.intensity, l.color.g * l.intensity, l.color.b * l.intensity, 1};
        if (l.type == Light::Type::Directional && !haveSun) {
            haveSun = true;
            L.sunDirection = t.forward();
            L.sunColor = c;
        } else if (l.type != Light::Type::Directional &&
                   L.points.size() < static_cast<size_t>(std::min(Renderer::kMaxPointLights, renderer.quality().maxPointLights))) {
            PointLightData p{t.worldPosition(), l.range, c};
            if (l.type == Light::Type::Spot) {
                const float half = std::clamp(l.spotAngle, 1.0f, 179.0f) * 0.5f * kDegToRad;
                p.spot = true;
                p.direction = t.forward();
                p.cosOuter = std::cos(half);
                p.cosInner = std::cos(half * (1.0f - std::clamp(l.spotSoftness, 0.0f, 1.0f)));
            }
            L.points.push_back(p);
        }
    });
    if (anyLight && !haveSun) L.sunColor = {0, 0, 0, 1};

    // ---- Recoger mallas con su caja en el mundo.
    const Mat4 camWorld = view.view.inverted();
    const Vec3 camPos = camWorld.translation();
    stats_ = {};
    candidates_.clear();
    effective_.clear();
    opaque_.clear();
    transparent_.clear();
    casters_.clear();
    poseCount_ = 0;
    const AABB unitBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}; // todas las primitivas caben en ella
    bool anyCaster = false;
    scene.world().each<MeshRenderer, Transform>([&](Entity e, MeshRenderer& own, Transform& t) {
        if (!own.visible || !scene.isActiveInHierarchy(e)) return;
        const MeshRenderer* shared = (!own.material.isNull() && materials) ? materials(own.material) : nullptr;
        const MeshRenderer* src = &own;
        if (shared) { // material compartido: sus campos de material, y lo propio del objeto (malla, sombras...)
            MeshRenderer& eff = effective_.emplace_back(*shared);
            eff.mesh = own.mesh;
            eff.model = own.model;
            eff.material = own.material;
            eff.castShadows = own.castShadows;
            eff.visible = own.visible;
            src = &eff;
        }
        const MeshRenderer& m = *src;
        const Vec3 d = t.worldPosition() - camPos;
        const int model = (!m.model.isNull() && models) ? models(m.model) : -1;
        AABB local = unitBox;
        bool skinned = false;
        if (model >= 0) {
            renderer.modelBounds(model, local.min, local.max);
            // Un personaje animado sale de su pose de reposo (brazos, piernas): margen del 50 % por lado.
            skinned = renderer.modelHasSkin(model);
            if (skinned) local = local.inflated(0.5f);
        }
        const MaterialTextures maps{textures(m.texture), textures(m.normalMap), textures(m.metallicRoughnessMap),
                                    textures(m.emissionMap), textures(m.occlusionMap)};
        candidates_.push_back({{&m, &t.worldMatrix(), maps, dot(d, d), model, -1},
                               transformAABB(local, t.worldMatrix()), skinned, e});
        anyCaster = anyCaster || (m.castShadows && !m.unlit && (m.color.a >= 0.999f || m.alphaClip));
    });
    stats_.meshes = static_cast<int>(candidates_.size());

    // ---- Terrenos: malla en la GPU (rehecha solo si se editó) y caja para el culling.
    terrains_.clear();
    scene.world().each<Terrain, Transform>([&](Entity e, Terrain& t, Transform& tr) {
        if (!scene.isActiveInHierarchy(e)) return;
        const uint64_t key = (reinterpret_cast<uintptr_t>(&scene) * 1000003ull) ^ ((uint64_t(e.generation) << 32) | e.index);
        Terrain* tp = &t;
        const int handle = renderer.terrainMesh(key, t.revision, [tp] { return buildTerrainMesh(*tp); });
        TextureHandle layers[4];
        for (int i = 0; i < 4; ++i) layers[i] = textures(t.layerTextures[i]);
        terrains_.push_back({handle, &tr.worldMatrix(), tp, transformAABB(terrainBounds(t), tr.worldMatrix()),
                             {layers[0], layers[1], layers[2], layers[3]}});
        anyCaster = true;
    });

    // ---- Sombras del sol: un mapa ortográfico centrado delante de la cámara, alineado a sus texels
    // para que las sombras no "tiemblen" al mover la cámara.
    L.shadows = L.env.shadows && renderer.quality().shadows && haveSun && anyCaster && view.perspective;
    if (L.shadows) {
        const float radius = std::max(5.0f, L.env.shadowDistance * 0.5f);
        const Vec3 forward = camWorld.transformDirection({0, 0, -1}).normalized();
        Vec3 focus = camPos + forward * radius * 0.8f;
        const Vec3 dir = L.sunDirection.normalized();
        Mat4 lightView = lookAt(focus - dir * radius * 3.0f, focus);
        const float texel = 2.0f * radius / renderer.shadowMapSize();
        Vec3 ls = lightView.transformPoint(focus);
        const Vec3 snapped{std::floor(ls.x / texel) * texel, std::floor(ls.y / texel) * texel, ls.z};
        lightView = Mat4::translate(snapped - ls) * lightView;
        L.lightViewProjection = Mat4::ortho(-radius, radius, -radius, radius, 0.1f, radius * 6.0f) * lightView;
    }

    // ---- Visibilidad: frustum de la cámara para la pasada principal; caja de la luz para las sombras
    // (lo que queda fuera de ella tampoco aparecería en el mapa de sombras).
    const Frustum camFrustum = Frustum::fromMatrix(view.projection * view.view);
    const Frustum lightFrustum = Frustum::fromMatrix(L.lightViewProjection);
    for (Candidate& c : candidates_) {
        const MeshRenderer& m = *c.item.mesh;
        const bool opaque = m.color.a >= 0.999f || m.alphaClip;
        const bool caster = L.shadows && opaque && m.castShadows && !m.unlit;
        const bool inView = !culling || camFrustum.intersects(c.bounds);
        const bool inShadow = caster && (!culling || lightFrustum.intersects(c.bounds));
        if (caster) stats_.shadowCasters++;
        if (!inView && !inShadow) continue;
        if (c.skinned) {
            // Pose del esqueleto: la del Animator si lo hay; si no, la de reposo. Solo de lo que se dibuja.
            const Animator* an = scene.world().get<Animator>(c.entity);
            const int model = c.item.model;
            if (poseCount_ >= poses_.size()) poses_.emplace_back();
            const int clip = an ? renderer.modelAnimationIndex(model, an->clip) : -1;
            // Segundo clip: la transición si la hay; si no, la mezcla 1D de la máquina de estados.
            int from = -1;
            float fromTime = 0, fromWeight = 0;
            if (an && an->fromWeight > 0) {
                from = renderer.modelAnimationIndex(model, an->fromClip);
                fromTime = an->fromTime;
                fromWeight = an->fromWeight;
            } else if (an && an->blendWeight > 0) {
                from = renderer.modelAnimationIndex(model, an->blendClip);
                fromTime = an->blendTime;
                fromWeight = an->blendWeight;
            }
            if (renderer.computeBlendedPose(model, clip, an ? an->time : 0.0f, from, fromTime, fromWeight,
                                            an ? an->loop : true, poses_[poseCount_])) {
                c.item.pose = static_cast<int>(poseCount_++);
                stats_.posesComputed++;
            }
        }
        if (inView) {
            stats_.meshesVisible++;
            (opaque ? opaque_ : transparent_).push_back(c.item);
        }
        if (inShadow) {
            stats_.shadowCastersVisible++;
            casters_.push_back(c.item);
        }
    }

    groupInstances(casters_, shadowBatches_);
    groupInstances(opaque_, batches_);
    if (L.shadows) {
        renderer.beginShadowPass(L.lightViewProjection);
        for (const InstanceBatch& b : shadowBatches_) renderer.drawShadowCastersInstanced(b.material->mesh, b.worlds);
        for (const TerrainItem& t : terrains_)
            if (!culling || lightFrustum.intersects(t.bounds)) renderer.drawTerrainShadow(t.handle, *t.world);
        for (const MeshItem& m : casters_) {
            if (m.model >= 0) renderer.drawModelShadow(m.model, *m.world, m.pose >= 0 ? &poses_[m.pose] : nullptr);
            else renderer.drawShadowCaster(m.mesh->mesh, *m.world);
        }
        renderer.endShadowPass();
    }

    // ---- Pasada principal.
    renderer.beginFrame(viewport, L.drawSky ? L.env.skyHorizon : view.clearColor);
    renderer.setCamera(view.view, view.projection);
    renderer.setLighting(L);
    renderer.drawSky();
    // Opacos de delante a atrás (menos píxeles sombreados de más); transparentes de atrás a delante.
    std::sort(opaque_.begin(), opaque_.end(), [](const MeshItem& a, const MeshItem& b) { return a.depth < b.depth; });
    std::sort(transparent_.begin(), transparent_.end(), [](const MeshItem& a, const MeshItem& b) { return a.depth > b.depth; });
    auto draw = [&](const MeshItem& m) {
        if (m.model >= 0) renderer.drawModel(m.model, *m.world, *m.mesh, m.maps, m.pose >= 0 ? &poses_[m.pose] : nullptr);
        else renderer.drawMesh(m.mesh->mesh, *m.world, *m.mesh, m.maps);
    };
    for (const TerrainItem& t : terrains_) {
        stats_.meshes++;
        if (culling && !camFrustum.intersects(t.bounds)) continue;
        stats_.meshesVisible++;
        renderer.drawTerrain(t.handle, *t.world, *t.terrain, t.layers);
    }
    for (const InstanceBatch& b : batches_) {
        renderer.drawMeshInstanced(b.material->mesh, b.worlds, *b.material);
        stats_.instanced += static_cast<int>(b.worlds.size());
        stats_.instanceBatches++;
    }
    for (const MeshItem& m : opaque_) draw(m);
    for (const MeshItem& m : transparent_) draw(m);

    sprites_.clear();
    scene.world().each<SpriteRenderer, Transform>([&](Entity e, SpriteRenderer& sp, Transform& t) {
        if (!sp.visible || !scene.isActiveInHierarchy(e)) return;
        stats_.sprites++;
        if (culling) {
            const AABB quad{{-sp.pivot.x * sp.size.x, -sp.pivot.y * sp.size.y, 0},
                            {(1 - sp.pivot.x) * sp.size.x, (1 - sp.pivot.y) * sp.size.y, 0}};
            if (!camFrustum.intersects(transformAABB(quad, t.worldMatrix()))) return;
        }
        stats_.spritesVisible++;
        sprites_.push_back({sp.sortingOrder, textures(sp.texture), &t.worldMatrix(), &sp});
    });
    std::stable_sort(sprites_.begin(), sprites_.end(), [](const SpriteItem& a, const SpriteItem& b) {
        return a.order != b.order ? a.order < b.order : a.texture < b.texture;
    });
    for (const SpriteItem& s : sprites_)
        renderer.drawSprite(s.texture, *s.world, s.sprite->size, s.sprite->pivot, s.sprite->uv, s.sprite->color);

    // ---- Sombras de gota: un disco suave sobre el suelo, más pequeño y claro cuanto más alto está el objeto.
    scene.world().each<BlobShadow>([&](Entity, BlobShadow& b) {
        if (!b.visible) return;
        const float k = std::clamp(1.0f - b.height / std::max(b.maxHeight, 0.01f), 0.0f, 1.0f);
        const float size = b.radius * 2 * (0.5f + 0.5f * k);
        const Vec3 n = b.normal;
        const Vec3 t = (std::fabs(n.y) < 0.99f ? cross(n, {0, 1, 0}) : cross(n, {1, 0, 0})).normalized();
        const Vec3 bt = cross(n, t);
        Mat4 m;
        m.m[0] = t.x; m.m[1] = t.y; m.m[2] = t.z;
        m.m[4] = bt.x; m.m[5] = bt.y; m.m[6] = bt.z;
        m.m[8] = n.x; m.m[9] = n.y; m.m[10] = n.z;
        const Vec3 p = b.point + n * 0.02f; // un poco por encima para no parpadear con el suelo
        m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z;
        renderer.drawSprite(renderer.softParticleTexture(), m, {size, size}, {0.5f, 0.5f}, {0, 0, 1, 1},
                            Color{0, 0, 0, b.opacity * (0.4f + 0.6f * k)});
    });

    // ---- Partículas: cuadrados hacia la cámara; las de mezcla alfa, de lejos a cerca.
    scene.world().each<ParticleSystem, Transform>([&](Entity e, ParticleSystem& ps, Transform& t) {
        if (!ps.state || ps.state->particles.empty() || !scene.isActiveInHierarchy(e)) return;
        stats_.particleSystems++;
        if (culling && !camFrustum.intersects(ps.state->bounds)) return;
        stats_.particleSystemsVisible++;
        const TextureHandle tex = ps.texture.isNull() ? renderer.softParticleTexture() : textures(ps.texture);
        const Mat4& world = t.worldMatrix();
        const auto& parts = ps.state->particles;
        particleOrder_.clear();
        for (int k = 0; k < static_cast<int>(parts.size()); ++k) {
            const Vec3 w = ps.worldSpace ? parts[k].position : world.transformPoint(parts[k].position);
            const Vec3 d = w - camPos;
            particleOrder_.push_back({dot(d, d), k});
        }
        if (!ps.additive)
            std::sort(particleOrder_.begin(), particleOrder_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        const int cols = std::max(1, ps.sheetColumns), rows = std::max(1, ps.sheetRows), frames = cols * rows;
        for (const auto& [depth, k] : particleOrder_) {
            const Particle& p = parts[k];
            const float f = std::clamp(p.age / p.life, 0.0f, 1.0f);
            const float size = ps.sizeStart + (ps.sizeEnd - ps.sizeStart) * f;
            const Color c{ps.colorStart.r + (ps.colorEnd.r - ps.colorStart.r) * f,
                          ps.colorStart.g + (ps.colorEnd.g - ps.colorStart.g) * f,
                          ps.colorStart.b + (ps.colorEnd.b - ps.colorStart.b) * f,
                          ps.colorStart.a + (ps.colorEnd.a - ps.colorStart.a) * f};
            const int frame = std::min(frames - 1, static_cast<int>(f * frames));
            const Rect uv{static_cast<float>(frame % cols) / cols, static_cast<float>(frame / cols) / rows, 1.0f / cols, 1.0f / rows};
            const Vec3 w = ps.worldSpace ? p.position : world.transformPoint(p.position);
            renderer.drawBillboard(tex, w, size, p.rotation, uv, c, ps.additive);
            stats_.particlesDrawn++;
        }
    });
}

} // namespace titan
