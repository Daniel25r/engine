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

    // ---- Recoger mallas.
    const Mat4 camWorld = view.view.inverted();
    const Vec3 camPos = camWorld.translation();
    opaque_.clear();
    transparent_.clear();
    scene.world().each<MeshRenderer, Transform>([&](Entity e, MeshRenderer& m, Transform& t) {
        if (!m.visible || !scene.isActiveInHierarchy(e)) return;
        const Vec3 d = t.worldPosition() - camPos;
        const int model = (!m.model.isNull() && models) ? models(m.model) : -1;
        MeshItem item{&m, &t.worldMatrix(), textures(m.texture), dot(d, d), model};
        (m.color.a < 0.999f ? transparent_ : opaque_).push_back(item);
    });

    // ---- Sombras del sol: un mapa ortográfico centrado delante de la cámara, alineado a sus texels
    // para que las sombras no "tiemblen" al mover la cámara.
    L.shadows = L.env.shadows && haveSun && !opaque_.empty() && view.perspective;
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
        renderer.beginShadowPass(L.lightViewProjection);
        for (const MeshItem& m : opaque_)
            if (m.mesh->castShadows && !m.mesh->unlit) {
                if (m.model >= 0) renderer.drawModelShadow(m.model, *m.world);
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
        if (m.model >= 0) renderer.drawModel(m.model, *m.world, *m.mesh, m.texture);
        else renderer.drawMesh(m.mesh->mesh, *m.world, *m.mesh, m.texture);
    };
    for (const MeshItem& m : opaque_) draw(m);
    for (const MeshItem& m : transparent_) draw(m);

    sprites_.clear();
    scene.world().each<SpriteRenderer, Transform>([&](Entity e, SpriteRenderer& sp, Transform& t) {
        if (!sp.visible || !scene.isActiveInHierarchy(e)) return;
        sprites_.push_back({sp.sortingOrder, textures(sp.texture), &t.worldMatrix(), &sp});
    });
    std::stable_sort(sprites_.begin(), sprites_.end(), [](const SpriteItem& a, const SpriteItem& b) {
        return a.order != b.order ? a.order < b.order : a.texture < b.texture;
    });
    for (const SpriteItem& s : sprites_)
        renderer.drawSprite(s.texture, *s.world, s.sprite->size, s.sprite->pivot, s.sprite->uv, s.sprite->color);
}

} // namespace titan
