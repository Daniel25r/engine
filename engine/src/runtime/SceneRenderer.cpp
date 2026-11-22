#include "titan/runtime/SceneRenderer.h"

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

void SceneRenderer::render(Scene& scene, Renderer& renderer, const Viewport& viewport, const SceneView& view,
                           const TextureResolver& textures, const ModelResolver& models) {
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
        } else if (l.type == Light::Type::Point && L.points.size() < Renderer::kMaxPointLights) {
            L.points.push_back({t.worldPosition(), l.range, c});
        }
    });
    if (anyLight && !haveSun) L.sunColor = {0, 0, 0, 1};

    // ---- Recoger mallas con su caja en el mundo.
    const Mat4 camWorld = view.view.inverted();
    const Vec3 camPos = camWorld.translation();
    stats_ = {};
    candidates_.clear();
    opaque_.clear();
    transparent_.clear();
    casters_.clear();
    poseCount_ = 0;
    const AABB unitBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}; // todas las primitivas caben en ella
    bool anyCaster = false;
    scene.world().each<MeshRenderer, Transform>([&](Entity e, MeshRenderer& m, Transform& t) {
        if (!m.visible || !scene.isActiveInHierarchy(e)) return;
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
        candidates_.push_back({{&m, &t.worldMatrix(), textures(m.texture), dot(d, d), model, -1},
                               transformAABB(local, t.worldMatrix()), skinned, e});
        anyCaster = anyCaster || (m.castShadows && !m.unlit && m.color.a >= 0.999f);
    });
    stats_.meshes = static_cast<int>(candidates_.size());

    // ---- Sombras del sol: un mapa ortográfico centrado delante de la cámara, alineado a sus texels
    // para que las sombras no "tiemblen" al mover la cámara.
    L.shadows = L.env.shadows && haveSun && anyCaster && view.perspective;
    if (L.shadows) {
        const float radius = std::max(5.0f, L.env.shadowDistance * 0.5f);
        const Vec3 forward = camWorld.transformDirection({0, 0, -1}).normalized();
        Vec3 focus = camPos + forward * radius * 0.8f;
        const Vec3 dir = L.sunDirection.normalized();
        Mat4 lightView = lookAt(focus - dir * radius * 3.0f, focus);
        const float texel = 2.0f * radius / Renderer::kShadowMapSize;
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
        const bool opaque = m.color.a >= 0.999f;
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
            const int from = an && an->fromWeight > 0 ? renderer.modelAnimationIndex(model, an->fromClip) : -1;
            if (renderer.computeBlendedPose(model, clip, an ? an->time : 0.0f, from, an ? an->fromTime : 0.0f,
                                            an ? an->fromWeight : 0.0f, an ? an->loop : true, poses_[poseCount_])) {
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

    if (L.shadows) {
        renderer.beginShadowPass(L.lightViewProjection);
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
        if (m.model >= 0) renderer.drawModel(m.model, *m.world, *m.mesh, m.texture, m.pose >= 0 ? &poses_[m.pose] : nullptr);
        else renderer.drawMesh(m.mesh->mesh, *m.world, *m.mesh, m.texture);
    };
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
}

} // namespace titan
