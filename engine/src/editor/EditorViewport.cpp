// Vista de escena del editor: cámara de edición, gestos táctiles, selección, mover objetos y gizmos.
// Dibuja con el mismo SceneRenderer que el juego; en modo Play muestra el Runtime.
#include <algorithm>
#include <cmath>

#include "EditorImpl.h"

namespace titan {

namespace {

Mat4 lookAtWorld(const Vec3& eye, const Vec3& target) {
    const Vec3 f = (target - eye).normalized();
    Vec3 r = cross(f, {0, 1, 0});
    if (r.length() < 1e-4f) r = {1, 0, 0};
    r = r.normalized();
    const Vec3 u = cross(r, f);
    Mat4 m;
    m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z;
    m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
    m.m[8] = -f.x; m.m[9] = -f.y; m.m[10] = -f.z;
    m.m[12] = eye.x; m.m[13] = eye.y; m.m[14] = eye.z;
    return m;
}

// Intersección de un rayo con el cubo unidad [-0.5, 0.5]^3 (en espacio local).
bool rayUnitBox(const Vec3& o, const Vec3& d, float& tHit) {
    float tmin = -1e30f, tmax = 1e30f;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-9f) {
            if (oo[i] < -0.5f || oo[i] > 0.5f) return false;
            continue;
        }
        float t1 = (-0.5f - oo[i]) / dd[i], t2 = (0.5f - oo[i]) / dd[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmax < 0) return false;
    tHit = tmin >= 0 ? tmin : tmax;
    return true;
}

bool rayPlane(const Vec3& o, const Vec3& d, const Vec3& n, const Vec3& p, Vec3& hit) {
    const float den = dot(d, n);
    if (std::fabs(den) < 1e-6f) return false;
    const float t = dot(p - o, n) / den;
    if (t < 0) return false;
    hit = o + d * t;
    return true;
}

float pdist(const ImVec2& a, const ImVec2& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

} // namespace

bool Editor::Impl::threeD() const { return project && project->dimension() == ProjectDimension::ThreeD; }

void Editor::Impl::updateEditorCamera(float aspect) {
    if (threeD()) {
        const float cp = std::cos(cam3Pitch);
        const Vec3 eye = cam3Target + Vec3{std::sin(cam3Yaw) * cp, std::sin(cam3Pitch), std::cos(cam3Yaw) * cp} * cam3Dist;
        editorView = lookAtWorld(eye, cam3Target).inverted();
        editorProj = Mat4::perspective(55 * kDegToRad, aspect, 0.05f, 2000.0f);
    } else {
        editorView = Mat4::translate({cam2Center.x, cam2Center.y, 50}).inverted();
        editorProj = Mat4::ortho(-cam2Size * aspect, cam2Size * aspect, -cam2Size, cam2Size, 0.1f, 1000.0f);
    }
}

void Editor::Impl::renderViewport() {
    const int x = static_cast<int>(vpMin.x), w = static_cast<int>(vpMax.x - vpMin.x);
    const int h = static_cast<int>(vpMax.y - vpMin.y);
    const Viewport vp{x, fbHeight - static_cast<int>(vpMax.y), std::max(w, 1), std::max(h, 1)};
    if (runtime) {
        runtime->frame(vp);
        return;
    }
    TransformSystem::update(scene);
    updateEditorCamera(float(vp.width) / float(vp.height));
    SceneView view;
    view.view = editorView;
    view.projection = editorProj;
    view.perspective = threeD();
    // Fondo: el de la cámara del juego, para ver la escena como se verá al jugar.
    SceneView gameView;
    view.clearColor = SceneRenderer::cameraView(scene, 1.0f, gameView) ? gameView.clearColor : Color::hex(0x1B2230);
    sceneRenderer.render(scene, renderer, vp, view, [this](AssetRef r) { return editorTexture(r); });
    drawGizmos();
    renderer.endFrame();
}

void Editor::Impl::drawGizmos() {
    // Rejilla.
    if (threeD()) {
        const float ext = 20;
        for (int i = -20; i <= 20; ++i) {
            const Color c = i == 0 ? Color{1, 1, 1, 0.25f} : Color{1, 1, 1, i % 5 == 0 ? 0.12f : 0.06f};
            renderer.drawLine({float(i), 0.001f, -ext}, {float(i), 0.001f, ext}, i == 0 ? Color{0.35f, 0.55f, 1, 0.8f} : c);
            renderer.drawLine({-ext, 0.001f, float(i)}, {ext, 0.001f, float(i)}, i == 0 ? Color{1, 0.35f, 0.35f, 0.8f} : c);
        }
    } else {
        const float step = cam2Size > 30 ? 10.0f : cam2Size > 12 ? 5.0f : 1.0f;
        const float ext = cam2Size * 3;
        const float x0 = std::floor((cam2Center.x - ext) / step) * step, y0 = std::floor((cam2Center.y - ext) / step) * step;
        for (float v = x0; v <= cam2Center.x + ext; v += step)
            renderer.drawLine({v, cam2Center.y - ext, -1}, {v, cam2Center.y + ext, -1},
                              std::fabs(v) < 1e-4f ? Color{0.35f, 1, 0.45f, 0.6f} : Color{1, 1, 1, 0.07f});
        for (float v = y0; v <= cam2Center.y + ext; v += step)
            renderer.drawLine({cam2Center.x - ext, v, -1}, {cam2Center.x + ext, v, -1},
                              std::fabs(v) < 1e-4f ? Color{1, 0.35f, 0.35f, 0.6f} : Color{1, 1, 1, 0.07f});
    }

    // Cámaras y luces (no tienen forma visible).
    const float iconSize = threeD() ? 0.4f : cam2Size * 0.05f;
    scene.world().each<Camera, Transform>([&](Entity, Camera& c, Transform& t) {
        const Mat4& m = t.worldMatrix();
        const Vec3 o = m.translation();
        const float s = iconSize * 1.5f;
        const Vec3 corners[4] = {m.transformPoint({-s, -s * 0.6f, -s * 2}), m.transformPoint({s, -s * 0.6f, -s * 2}),
                                 m.transformPoint({s, s * 0.6f, -s * 2}), m.transformPoint({-s, s * 0.6f, -s * 2})};
        const Color col{0.95f, 0.85f, 0.4f, 0.9f};
        for (int i = 0; i < 4; ++i) {
            renderer.drawLine(o, corners[i], col);
            renderer.drawLine(corners[i], corners[(i + 1) % 4], col);
        }
        if (!threeD() && c.projection == Camera::Projection::Orthographic) {
            // En 2D se dibuja el encuadre que verá el juego (con relación 16:9 de referencia).
            const float hh = c.orthographicSize, hw = hh * 16.0f / 9.0f;
            const Vec3 a{o.x - hw, o.y - hh, 0}, b{o.x + hw, o.y - hh, 0}, d{o.x + hw, o.y + hh, 0}, e{o.x - hw, o.y + hh, 0};
            const Color fc{0.95f, 0.85f, 0.4f, 0.5f};
            renderer.drawLine(a, b, fc);
            renderer.drawLine(b, d, fc);
            renderer.drawLine(d, e, fc);
            renderer.drawLine(e, a, fc);
        }
    });
    scene.world().each<Light, Transform>([&](Entity, Light& l, Transform& t) {
        const Vec3 o = t.worldPosition();
        const Color col{1, 0.9f, 0.5f, 0.9f};
        const float s = iconSize;
        renderer.drawLine(o - Vec3{s, 0, 0}, o + Vec3{s, 0, 0}, col);
        renderer.drawLine(o - Vec3{0, s, 0}, o + Vec3{0, s, 0}, col);
        renderer.drawLine(o - Vec3{0, 0, s}, o + Vec3{0, 0, s}, col);
        if (l.type == Light::Type::Directional) renderer.drawLine(o, o + t.forward() * (s * 4), col);
    });

    // Selección.
    Entity sel = selectedEntity();
    if (sel.isNull()) return;
    const Mat4& m = scene.transform(sel).worldMatrix();
    const Color hi{0.95f, 0.71f, 0.29f, 1};
    if (scene.world().has<MeshRenderer>(sel)) {
        Vec3 c[8];
        for (int i = 0; i < 8; ++i) c[i] = m.transformPoint({(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f});
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& e : edges) renderer.drawLine(c[e[0]], c[e[1]], hi);
    } else if (const SpriteRenderer* sp = scene.world().get<SpriteRenderer>(sel)) {
        const float l = -sp->pivot.x * sp->size.x, r = (1 - sp->pivot.x) * sp->size.x;
        const float b = -sp->pivot.y * sp->size.y, tp = (1 - sp->pivot.y) * sp->size.y;
        const Vec3 c[4] = {m.transformPoint({l, b, 0}), m.transformPoint({r, b, 0}), m.transformPoint({r, tp, 0}),
                           m.transformPoint({l, tp, 0})};
        for (int i = 0; i < 4; ++i) renderer.drawLine(c[i], c[(i + 1) % 4], hi);
    }
    // Ejes locales del objeto seleccionado.
    const Vec3 o = m.translation();
    const float a = threeD() ? 1.0f : cam2Size * 0.15f;
    renderer.drawLine(o, o + scene.transform(sel).right() * a, {1, 0.3f, 0.3f, 1});
    renderer.drawLine(o, o + scene.transform(sel).up() * a, {0.3f, 1, 0.4f, 1});
    if (threeD()) renderer.drawLine(o, o + scene.transform(sel).forward() * a, {0.35f, 0.55f, 1, 1});
}

bool Editor::Impl::screenRay(ImVec2 p, Vec3& origin, Vec3& dir) const {
    const float w = vpMax.x - vpMin.x, h = vpMax.y - vpMin.y;
    if (w <= 0 || h <= 0) return false;
    const float nx = (p.x - vpMin.x) / w * 2 - 1, ny = 1 - (p.y - vpMin.y) / h * 2;
    Mat4 inv;
    if (!(editorProj * editorView).inverse(inv)) return false;
    auto un = [&](float z) {
        const float* m = inv.m;
        const float ww = m[3] * nx + m[7] * ny + m[11] * z + m[15];
        return Vec3{(m[0] * nx + m[4] * ny + m[8] * z + m[12]) / ww, (m[1] * nx + m[5] * ny + m[9] * z + m[13]) / ww,
                    (m[2] * nx + m[6] * ny + m[10] * z + m[14]) / ww};
    };
    origin = un(-1);
    dir = (un(1) - origin).normalized();
    return true;
}

Entity Editor::Impl::pick(ImVec2 p) {
    Vec3 o, d;
    if (!screenRay(p, o, d)) return {};
    Entity best;
    float bestT = 1e30f;
    auto consider = [&](Entity e, float t) {
        if (t < bestT) {
            bestT = t;
            best = e;
        }
    };
    scene.traverse([&](Entity e, int) {
        if (!scene.isActiveInHierarchy(e)) return;
        const Mat4& world = scene.transform(e).worldMatrix();
        const Mat4 inv = world.inverted();
        const Vec3 lo = inv.transformPoint(o), ld = inv.transformDirection(d);
        float t;
        if (scene.world().has<MeshRenderer>(e) && rayUnitBox(lo, ld, t)) consider(e, t);
        if (const SpriteRenderer* sp = scene.world().get<SpriteRenderer>(e)) {
            if (std::fabs(ld.z) > 1e-9f) {
                const float tt = -lo.z / ld.z;
                const Vec3 q = lo + ld * tt;
                if (tt >= 0 && q.x >= -sp->pivot.x * sp->size.x && q.x <= (1 - sp->pivot.x) * sp->size.x &&
                    q.y >= -sp->pivot.y * sp->size.y && q.y <= (1 - sp->pivot.y) * sp->size.y)
                    consider(e, tt - sp->sortingOrder * 1e-3f); // en 2D gana el que se dibuja encima
            }
        }
        if (scene.world().has<Camera>(e) || scene.world().has<Light>(e)) {
            const Vec3 c = world.translation();
            const float along = dot(c - o, d);
            const Vec3 closest = o + d * along;
            const float radius = threeD() ? 0.5f : cam2Size * 0.06f;
            if (along > 0 && (c - closest).length() < radius) consider(e, along);
        }
    });
    return best;
}

Vec3 snapValue(Vec3 v, bool snap, bool threeDim) {
    if (!snap) return v;
    auto r = [](float f) { return std::round(f * 2.0f) / 2.0f; };
    return threeDim ? Vec3{r(v.x), v.y, r(v.z)} : Vec3{r(v.x), r(v.y), v.z};
}

void Editor::Impl::viewportPointerDown(int id, ImVec2 p) {
    if (runtime) {
        runtime->input().touchBegan(id, {p.x - vpMin.x, p.y - vpMin.y});
        touches.push_back({id, p, p, p});
        return;
    }
    touches.push_back({id, p, p, p});
    gestureMoved = false;
    movingEntity = false;
    if (touches.size() == 2) pinchStart = pdist(touches[0].pos, touches[1].pos);
    if (touches.size() != 1 || tool != Tool::Move) return;
    Entity hit = pick(p);
    if (hit.isNull()) return;
    select(hit);
    Vec3 o, d;
    if (!screenRay(p, o, d)) return;
    const Vec3 world = scene.transform(hit).worldPosition();
    movePlaneNormal = threeD() ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    if (rayPlane(o, d, movePlaneNormal, world, moveStartHit)) {
        moveStartWorld = world;
        movingEntity = true;
        beginEdit();
    }
}

void Editor::Impl::viewportPointerMove(int id, ImVec2 p) {
    Touch* t = nullptr;
    for (auto& x : touches)
        if (x.id == id) t = &x;
    if (!t) return;
    const ImVec2 last = t->pos;
    t->pos = p;
    if (runtime) {
        runtime->input().touchMoved(id, {p.x - vpMin.x, p.y - vpMin.y});
        return;
    }
    if (pdist(t->start, p) > 8 * scale) gestureMoved = true;
    if (!gestureMoved) return;
    const float vh = std::max(1.0f, vpMax.y - vpMin.y);

    if (touches.size() >= 2) {
        // Dos dedos: desplazar con el punto medio y hacer zoom con la separación.
        movingEntity = false;
        pendingEdit.clear();
        const Touch& a = touches[0];
        const Touch& b = touches[1];
        const float dist = pdist(a.pos, b.pos);
        if (pinchStart > 1 && dist > 1) {
            const float k = pinchStart / dist;
            if (threeD()) cam3Dist = std::clamp(cam3Dist * k, 1.0f, 500.0f);
            else cam2Size = std::clamp(cam2Size * k, 0.5f, 500.0f);
            pinchStart = dist;
        }
        const ImVec2 dm((p.x - last.x) * 0.5f, (p.y - last.y) * 0.5f);
        if (threeD()) {
            const float k = cam3Dist / vh * 1.1f;
            const Vec3 right{std::cos(cam3Yaw), 0, -std::sin(cam3Yaw)};
            const Vec3 fwd{-std::sin(cam3Yaw), 0, -std::cos(cam3Yaw)};
            cam3Target += right * (-dm.x * k) + fwd * (dm.y * k);
        } else {
            const float k = cam2Size * 2 / vh;
            cam2Center += Vec2{-dm.x * k, dm.y * k};
        }
        return;
    }

    if (movingEntity) {
        Entity e = selectedEntity();
        Vec3 o, d, hit;
        if (e.isNull() || !screenRay(p, o, d) || !rayPlane(o, d, movePlaneNormal, moveStartWorld, hit)) return;
        const Vec3 world = snapValue(moveStartWorld + (hit - moveStartHit), snap, threeD());
        const Entity parent = scene.parent(e);
        const Vec3 local = parent.isNull() ? world : scene.computeWorldMatrix(parent).inverted().transformPoint(world);
        scene.transform(e).setPosition(local);
        return;
    }

    // Un dedo en vacío (o herramienta Cámara): girar en 3D, desplazar en 2D.
    const float dx = p.x - last.x, dy = p.y - last.y;
    if (threeD()) {
        cam3Yaw -= dx * 0.008f;
        cam3Pitch = std::clamp(cam3Pitch + dy * 0.008f, -1.45f, 1.45f);
    } else {
        const float k = cam2Size * 2 / vh;
        cam2Center += Vec2{-dx * k, dy * k};
    }
}

void Editor::Impl::viewportPointerUp(int id, ImVec2 p) {
    auto it = std::find_if(touches.begin(), touches.end(), [id](const Touch& t) { return t.id == id; });
    if (it == touches.end()) return;
    touches.erase(it);
    if (runtime) {
        runtime->input().touchEnded(id, {p.x - vpMin.x, p.y - vpMin.y});
        return;
    }
    if (movingEntity && touches.empty()) {
        movingEntity = false;
        if (gestureMoved) commitEdit();
        else pendingEdit.clear();
    }
    if (!gestureMoved && touches.empty()) select(pick(p)); // toque: seleccionar (o deseleccionar en vacío)
    if (touches.size() == 1) touches[0].start = touches[0].pos;
}

void Editor::Impl::viewportScroll(float amount) {
    const float k = std::pow(0.88f, amount);
    if (threeD()) cam3Dist = std::clamp(cam3Dist * k, 1.0f, 500.0f);
    else cam2Size = std::clamp(cam2Size * k, 0.5f, 500.0f);
}

} // namespace titan
