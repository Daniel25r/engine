#include "titan/runtime/SceneRenderer.h"

#include <algorithm>

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

void SceneRenderer::render(Scene& scene, Renderer& renderer, const Viewport& viewport, const SceneView& view,
                           const TextureResolver& textures) {
    renderer.beginFrame(viewport, view.clearColor);
    renderer.setCamera(view.view, view.projection);

    // Luces: la primera direccional activa y hasta 4 puntuales. Sin ninguna luz en la escena se usa
    // una iluminación por defecto para que un objeto recién creado no salga negro.
    LightSetup lights;
    bool anyLight = false, haveDirectional = false;
    scene.world().each<Light, Transform>([&](Entity e, Light& l, Transform& t) {
        if (!scene.isActiveInHierarchy(e)) return;
        anyLight = true;
        const Color c{l.color.r * l.intensity, l.color.g * l.intensity, l.color.b * l.intensity, 1};
        if (l.type == Light::Type::Directional && !haveDirectional) {
            haveDirectional = true;
            lights.directionalDir = t.forward();
            lights.directionalColor = c;
        } else if (l.type == Light::Type::Point && lights.points.size() < Renderer::kMaxPointLights) {
            lights.points.push_back({t.worldPosition(), l.range, c});
        }
    });
    if (anyLight && !haveDirectional) lights.directionalColor = {0, 0, 0, 1};
    renderer.setLights(lights);

    const Vec3 camPos = view.view.inverted().translation();
    opaque_.clear();
    transparent_.clear();
    scene.world().each<MeshRenderer, Transform>([&](Entity e, MeshRenderer& m, Transform& t) {
        if (!m.visible || !scene.isActiveInHierarchy(e)) return;
        const Vec3 d = t.worldPosition() - camPos;
        MeshItem item{&m, &t.worldMatrix(), textures(m.texture), dot(d, d)};
        (m.color.a < 0.999f ? transparent_ : opaque_).push_back(item);
    });
    // Opacos de delante a atrás (menos píxeles sombreados de más); transparentes de atrás a delante.
    std::sort(opaque_.begin(), opaque_.end(), [](const MeshItem& a, const MeshItem& b) { return a.depth < b.depth; });
    std::sort(transparent_.begin(), transparent_.end(), [](const MeshItem& a, const MeshItem& b) { return a.depth > b.depth; });
    for (const MeshItem& m : opaque_)
        renderer.drawMesh(m.mesh->mesh, *m.world, m.mesh->color, m.texture, m.mesh->shininess, m.mesh->unlit);
    for (const MeshItem& m : transparent_)
        renderer.drawMesh(m.mesh->mesh, *m.world, m.mesh->color, m.texture, m.mesh->shininess, m.mesh->unlit);

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
