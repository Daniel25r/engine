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
        float yaw = cam3Yaw, pitch = cam3Pitch;
        if (viewAngle == ViewAngle::Top) { yaw = 0; pitch = 1.5607f; }
        else if (viewAngle == ViewAngle::Front) { yaw = 0; pitch = 0; }
        else if (viewAngle == ViewAngle::Right) { yaw = 1.5708f; pitch = 0; }
        const float cp = std::cos(pitch);
        const Vec3 eye = cam3Target + Vec3{std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp} * cam3Dist;
        editorView = lookAtWorld(eye, cam3Target).inverted();
        if (viewAngle == ViewAngle::Perspective) {
            editorProj = Mat4::perspective(55 * kDegToRad, aspect, 0.05f, 2000.0f);
        } else {
            const float h = cam3Dist * 0.5f;
            editorProj = Mat4::ortho(-h * aspect, h * aspect, -h, h, -1000.0f, 2000.0f);
        }
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
    // Vista previa de animaciones en el editor (como el Animator de Unity con "Preview").
    const float dt = ImGui::GetIO().DeltaTime;
    scene.world().each<Animator>([&](Entity, Animator& a) {
        if (a.playing) a.time += dt * a.speed;
    });
    updateEditorCamera(float(vp.width) / float(vp.height));
    SceneView view;
    view.view = editorView;
    view.projection = editorProj;
    view.perspective = threeD();
    renderer.setUnlitOverride(viewUnlit);
    // Fondo: el de la cámara del juego, para ver la escena como se verá al jugar.
    SceneView gameView;
    view.clearColor = SceneRenderer::cameraView(scene, 1.0f, gameView) ? gameView.clearColor : Color::hex(0x1B2230);
    sceneRenderer.render(scene, renderer, vp, view, [this](AssetRef r) { return editorTexture(r); },
                         [this](AssetRef r) { return editorModel(r); });
    drawGizmos();
    renderer.endFrame();
}

void Editor::Impl::drawGizmos() {
    // Rejilla.
    if (!showGrid) {
    } else if (threeD()) {
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

    // Colliders en verde (como en los editores profesionales); los triggers, en cian.
    scene.world().each<Collider, Transform>([&](Entity e, Collider& c, Transform& t) {
        if (!scene.isActiveInHierarchy(e)) return;
        const Mat4& m = t.worldMatrix();
        const Color col = c.isTrigger ? Color{0.3f, 0.9f, 1, 0.8f} : Color{0.35f, 1, 0.45f, 0.8f};
        if (c.shape == Collider::Shape::Box) {
            Vec3 p[8];
            for (int i = 0; i < 8; ++i)
                p[i] = m.transformPoint(c.center + Vec3{(i & 1) ? c.size.x * 0.5f : -c.size.x * 0.5f,
                                                        (i & 2) ? c.size.y * 0.5f : -c.size.y * 0.5f,
                                                        (i & 4) ? c.size.z * 0.5f : -c.size.z * 0.5f});
            const int ed[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto& d : ed) renderer.drawLine(p[d[0]], p[d[1]], col);
        } else {
            // Esfera: tres aros. Cápsula: aros en los extremos y cuatro líneas laterales.
            const float r = c.radius, half = c.shape == Collider::Shape::Capsule ? std::max(0.0f, c.height * 0.5f - r) : 0.0f;
            for (int ring = 0; ring < 3; ++ring)
                for (int i = 0; i < 32; ++i) {
                    auto pt = [&](int k, float yoff) {
                        const float a = 6.2831853f * k / 32;
                        const Vec3 v = ring == 0 ? Vec3{std::cos(a) * r, yoff, std::sin(a) * r}
                                     : ring == 1 ? Vec3{std::cos(a) * r, std::sin(a) * r + (std::sin(a) > 0 ? half : -half), 0}
                                                 : Vec3{0, std::sin(a) * r + (std::sin(a) > 0 ? half : -half), std::cos(a) * r};
                        return m.transformPoint(c.center + v);
                    };
                    renderer.drawLine(pt(i, half), pt(i + 1, half), col);
                    if (ring == 0 && half > 0) renderer.drawLine(pt(i, -half), pt(i + 1, -half), col);
                }
        }
    });

    // Selección.
    Entity sel = selectedEntity();
    if (sel.isNull()) return;
    const Mat4& m = scene.transform(sel).worldMatrix();
    const Color hi{0.98f, 0.62f, 0.1f, 1};
    Vec3 bmin, bmax;
    if (localBounds(sel, bmin, bmax)) {
        Vec3 c[8];
        for (int i = 0; i < 8; ++i) c[i] = m.transformPoint({(i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y, (i & 4) ? bmax.z : bmin.z});
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& e : edges) renderer.drawLine(c[e[0]], c[e[1]], hi);
    } else if (const SpriteRenderer* sp = scene.world().get<SpriteRenderer>(sel)) {
        const float l = -sp->pivot.x * sp->size.x, r = (1 - sp->pivot.x) * sp->size.x;
        const float b = -sp->pivot.y * sp->size.y, tp = (1 - sp->pivot.y) * sp->size.y;
        const Vec3 c[4] = {m.transformPoint({l, b, 0}), m.transformPoint({r, b, 0}), m.transformPoint({r, tp, 0}),
                           m.transformPoint({l, tp, 0})};
        for (int i = 0; i < 4; ++i) renderer.drawLine(c[i], c[(i + 1) % 4], hi);
    }
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
        Vec3 bmin, bmax;
        if (localBounds(e, bmin, bmax)) {
            // Rayo contra la caja del objeto: se lleva a la caja unidad.
            const Vec3 size{std::max(bmax.x - bmin.x, 1e-4f), std::max(bmax.y - bmin.y, 1e-4f), std::max(bmax.z - bmin.z, 1e-4f)};
            const Vec3 c = (bmin + bmax) * 0.5f;
            const Vec3 o2{(lo.x - c.x) / size.x, (lo.y - c.y) / size.y, (lo.z - c.z) / size.z};
            const Vec3 d2{ld.x / size.x, ld.y / size.y, ld.z / size.z};
            if (rayUnitBox(o2, d2, t)) consider(e, t);
        }
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
    if (touches.size() == 2) {
        pinchStart = pdist(touches[0].pos, touches[1].pos);
        if (gizmoDragging) {
            gizmoDragging = false;
            pendingEdit.clear();
        }
    }
    if (touches.size() != 1) return;

    // 1) Las asas del gizmo tienen prioridad.
    if (showGizmos && tool != Tool::Select && !selectedEntity().isNull()) {
        const int axis = gizmoHitTest(p);
        if (axis >= 0) {
            Entity e = selectedEntity();
            gizmoAxis = axis;
            gizmoDragging = true;
            gizmoGrab = p;
            Vec3 s;
            scene.computeWorldMatrix(e).decompose(gizmoStartWorld, gizmoStartWorldRot, s);
            gizmoStartScale = scene.transform(e).scale();
            beginEdit();
            return;
        }
    }
    // 2) Con la herramienta Mover, arrastrar el propio objeto lo desplaza sobre el suelo (3D) o el plano (2D).
    if (tool != Tool::Translate) return;
    Entity hit = pick(p);
    if (hit.isNull() || scene.info(hit).id != selected) return; // primero se selecciona con un toque
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
    if (gizmoDragging && touches.size() == 1) {
        gizmoDrag(p);
        return;
    }
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
    if (gizmoDragging && touches.empty()) {
        gizmoDragging = false;
        gizmoAxis = -1;
        if (gestureMoved) commitEdit();
        else pendingEdit.clear();
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

// ============================================================ Gizmo de transformación

bool Editor::Impl::worldToScreen(const Vec3& p, ImVec2& out) const {
    const Mat4 vp = editorProj * editorView;
    const float* m = vp.m;
    const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 1e-5f) return false;
    out = ImVec2(vpMin.x + (x / w * 0.5f + 0.5f) * (vpMax.x - vpMin.x), vpMin.y + (0.5f - y / w * 0.5f) * (vpMax.y - vpMin.y));
    return true;
}

float Editor::Impl::gizmoLength() const {
    Entity e = const_cast<Impl*>(this)->selectedEntity();
    if (e.isNull()) return 1;
    const Vec3 o = const_cast<Impl*>(this)->scene.computeWorldMatrix(e).translation();
    if (!threeD()) return cam2Size * 0.28f;
    if (viewAngle != ViewAngle::Perspective) return cam3Dist * 0.14f;
    const Vec3 cam = editorView.inverted().translation();
    return std::max(0.05f, (cam - o).length() * 0.16f);
}

namespace {
const Vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const ImU32 kGizmoColors[3] = {IM_COL32(235, 55, 55, 255), IM_COL32(90, 205, 50, 255), IM_COL32(45, 115, 245, 255)};
const ImU32 kGizmoActive = IM_COL32(255, 215, 40, 255);

float distToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

// Puntos de un aro alrededor de un eje (para el gizmo de rotación).
void ringPoints(const Vec3& o, const Vec3& axis, float r, Vec3* out, int n) {
    const Vec3 u = std::fabs(axis.y) > 0.9f ? Vec3{1, 0, 0} : cross(axis, {0, 1, 0}).normalized();
    const Vec3 v = cross(axis, u);
    for (int i = 0; i < n; ++i) {
        const float a = 6.2831853f * i / n;
        out[i] = o + (u * std::cos(a) + v * std::sin(a)) * r;
    }
}
} // namespace

int Editor::Impl::gizmoHitTest(ImVec2 p) {
    Entity e = selectedEntity();
    if (e.isNull()) return -1;
    const Vec3 o = scene.computeWorldMatrix(e).translation();
    const float L = gizmoLength(), tol = 14 * scale;
    ImVec2 so;
    if (!worldToScreen(o, so)) return -1;
    const int axes = threeD() ? 3 : 2;
    if (tool == Tool::Rotate) {
        int best = -1;
        float bestD = tol;
        for (int a = threeD() ? 0 : 2; a < 3; ++a) {
            Vec3 pts[48];
            ringPoints(o, kAxes[a], L, pts, 48);
            for (int i = 0; i < 48; ++i) {
                ImVec2 s0, s1;
                if (!worldToScreen(pts[i], s0) || !worldToScreen(pts[(i + 1) % 48], s1)) continue;
                const float d = distToSegment(p, s0, s1);
                if (d < bestD) { bestD = d; best = a; }
            }
        }
        return best;
    }
    if (std::fabs(p.x - so.x) < tol && std::fabs(p.y - so.y) < tol) return 3; // centro
    int best = -1;
    float bestD = tol;
    for (int a = 0; a < axes; ++a) {
        ImVec2 s1;
        if (!worldToScreen(o + kAxes[a] * L, s1)) continue;
        const float d = distToSegment(p, so, s1);
        if (d < bestD) { bestD = d; best = a; }
    }
    return best;
}

void Editor::Impl::gizmoDrag(ImVec2 p) {
    Entity e = selectedEntity();
    if (e.isNull()) return;
    Transform& t = scene.transform(e);
    const Entity parent = scene.parent(e);
    const Mat4 parentWorld = parent.isNull() ? Mat4{} : scene.computeWorldMatrix(parent);
    const float L = gizmoLength();
    const ImVec2 delta(p.x - gizmoGrab.x, p.y - gizmoGrab.y);
    ImVec2 s0, s1;
    if (!worldToScreen(gizmoStartWorld, s0)) return;

    auto alongAxis = [&](int a) -> float { // desplazamiento en unidades de mundo a lo largo del eje a
        if (!worldToScreen(gizmoStartWorld + kAxes[a] * L, s1)) return 0;
        const float dx = s1.x - s0.x, dy = s1.y - s0.y, len2 = dx * dx + dy * dy;
        if (len2 < 25) return 0; // el eje apunta a la cámara: no se puede arrastrar con precisión
        return (delta.x * dx + delta.y * dy) / len2 * L;
    };

    if (tool == Tool::Translate) {
        Vec3 world = gizmoStartWorld;
        if (gizmoAxis == 3) {
            // Centro: libre sobre el suelo (3D) o el plano (2D).
            Vec3 o, d, h0, h1;
            const Vec3 n = threeD() ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            if (screenRay(gizmoGrab, o, d) && rayPlane(o, d, n, gizmoStartWorld, h0) && screenRay(p, o, d) &&
                rayPlane(o, d, n, gizmoStartWorld, h1))
                world = gizmoStartWorld + (h1 - h0);
        } else {
            world += kAxes[gizmoAxis] * alongAxis(gizmoAxis);
        }
        if (snap) {
            float* c[3] = {&world.x, &world.y, &world.z};
            for (int a = 0; a < 3; ++a)
                if (gizmoAxis == a || (gizmoAxis == 3 && a != (threeD() ? 1 : 2))) *c[a] = std::round(*c[a] / snapMove) * snapMove;
        }
        t.setPosition(parent.isNull() ? world : parentWorld.inverted().transformPoint(world));
    } else if (tool == Tool::Rotate && gizmoAxis >= 0 && gizmoAxis < 3) {
        // Ángulo barrido alrededor del centro del gizmo en pantalla.
        const ImVec2 v0(gizmoGrab.x - s0.x, gizmoGrab.y - s0.y), v1(p.x - s0.x, p.y - s0.y);
        float ang = std::atan2(v0.x * v1.y - v0.y * v1.x, v0.x * v1.x + v0.y * v1.y);
        const Vec3 toCam = (editorView.inverted().translation() - gizmoStartWorld).normalized();
        if (dot(kAxes[gizmoAxis], toCam) > 0 || !threeD()) ang = -ang; // pantalla con Y hacia abajo
        if (snap) ang = std::round(ang / (snapRotate * kDegToRad)) * snapRotate * kDegToRad;
        const Quat world = (Quat::fromAxisAngle(kAxes[gizmoAxis], ang) * gizmoStartWorldRot).normalized();
        Quat parentRot;
        Vec3 pt, ps;
        parentWorld.decompose(pt, parentRot, ps);
        t.setRotation(parent.isNull() ? world : parentRot.conjugate() * world);
    } else if (tool == Tool::Scale) {
        Vec3 s = gizmoStartScale;
        if (gizmoAxis == 3) {
            const float f = std::max(0.01f, 1.0f + (delta.x - delta.y) / (120.0f * scale));
            s = s * f;
        } else {
            const float f = std::max(0.01f, 1.0f + alongAxis(gizmoAxis) / L);
            float* c[3] = {&s.x, &s.y, &s.z};
            *c[gizmoAxis] *= f;
            if (snap) *c[gizmoAxis] = std::max(snapScale, std::round(*c[gizmoAxis] / snapScale) * snapScale);
        }
        t.setScale(s);
    }
}

void Editor::Impl::drawGizmo(ImDrawList* dl) {
    Entity e = selectedEntity();
    if (e.isNull() || tool == Tool::Select) return;
    const Vec3 o = scene.computeWorldMatrix(e).translation();
    ImVec2 so;
    if (!worldToScreen(o, so)) return;
    const float L = gizmoLength(), th = 3.0f * scale;
    dl->PushClipRect(vpMin, vpMax, true);
    if (tool == Tool::Rotate) {
        for (int a = threeD() ? 0 : 2; a < 3; ++a) {
            Vec3 pts[48];
            ringPoints(o, kAxes[a], L, pts, 48);
            ImVec2 sp[49];
            int n = 0;
            for (int i = 0; i <= 48; ++i)
                if (worldToScreen(pts[i % 48], sp[n])) ++n;
            dl->AddPolyline(sp, n, gizmoDragging && gizmoAxis == a ? kGizmoActive : kGizmoColors[a], 0, th);
        }
    } else {
        const int axes = threeD() ? 3 : 2;
        for (int a = 0; a < axes; ++a) {
            ImVec2 s1;
            if (!worldToScreen(o + kAxes[a] * L, s1)) continue;
            const ImU32 col = gizmoDragging && gizmoAxis == a ? kGizmoActive : kGizmoColors[a];
            dl->AddLine(so, s1, col, th);
            const float dx = s1.x - so.x, dy = s1.y - so.y, len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            const ImVec2 dir(dx / len, dy / len), nrm(-dir.y, dir.x);
            const float hs = 9 * scale;
            if (tool == Tool::Translate) {
                dl->AddTriangleFilled(ImVec2(s1.x + dir.x * hs * 1.6f, s1.y + dir.y * hs * 1.6f),
                                      ImVec2(s1.x + nrm.x * hs * 0.7f, s1.y + nrm.y * hs * 0.7f),
                                      ImVec2(s1.x - nrm.x * hs * 0.7f, s1.y - nrm.y * hs * 0.7f), col);
            } else {
                dl->AddRectFilled(ImVec2(s1.x - hs * 0.7f, s1.y - hs * 0.7f), ImVec2(s1.x + hs * 0.7f, s1.y + hs * 0.7f), col);
            }
        }
        const float c = 7 * scale;
        const ImU32 cc = gizmoDragging && gizmoAxis == 3 ? kGizmoActive : IM_COL32(235, 235, 235, 230);
        if (tool == Tool::Translate) dl->AddCircleFilled(so, c * 0.8f, cc);
        else dl->AddRectFilled(ImVec2(so.x - c, so.y - c), ImVec2(so.x + c, so.y + c), cc);
    }
    dl->PopClipRect();
}

void Editor::Impl::drawAxisIndicator(ImDrawList* dl) {
    // Ejes del mundo vistos desde la cámara, en la esquina inferior izquierda.
    const ImVec2 c(vpMin.x + 34 * scale, vpMax.y - 34 * scale);
    const float r = 22 * scale;
    static const char* names[3] = {"X", "Y", "Z"};
    for (int a = 0; a < (threeD() ? 3 : 2); ++a) {
        const Vec3 d = editorView.transformDirection(kAxes[a]);
        const ImVec2 tip(c.x + d.x * r, c.y - d.y * r);
        dl->AddLine(c, tip, kGizmoColors[a], 2.5f * scale);
        dl->AddText(ImVec2(tip.x - 4 * scale, tip.y - 8 * scale), kGizmoColors[a], names[a]);
    }
}

} // namespace titan
