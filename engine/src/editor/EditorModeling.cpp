// Modo de modelado y escultura del editor (como el Edit Mode y el Sculpt Mode de Blender, primera versión).
// Se edita la malla de un objeto cuyo modelo es models/<Nombre>.glb con su fuente models/<Nombre>.tmesh. Cada cambio
// se ve al momento (el GLB se reconstruye en memoria) y al soltar el ratón se guarda en disco.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "EditorImpl.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "titan/core/FileIO.h"

namespace fs = std::filesystem;

namespace titan {

namespace {

const ImU32 kVert = IM_COL32(20, 20, 24, 255);
const ImU32 kVertSel = IM_COL32(255, 160, 30, 255);
const Color kEdge{0.08f, 0.08f, 0.1f, 1};
const Color kEdgeSel{1.0f, 0.62f, 0.12f, 1};

// Cabecera de panel con el mismo aspecto que las demás (franja oscura y subrayado azul).
void header(const char* title, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 8 * scale, w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(ImVec2(p.x - 4 * scale, p.y), ImVec2(p.x + w + 4 * scale, p.y + h), IM_COL32(26, 26, 26, 255));
    dl->AddRectFilled(ImVec2(p.x - 4 * scale, p.y + h - 2 * scale), ImVec2(p.x + 60 * scale, p.y + h), IM_COL32(255, 160, 30, 255));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 2 * scale, p.y + 4 * scale));
    ImGui::TextUnformatted(title);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4 * scale));
}

std::string uniqueModelName(const Project& p, const std::string& base) {
    std::string name = base;
    for (int i = 2; fs::exists(p.assetsDir() + "/models/" + name + ".tmesh") || fs::exists(p.assetsDir() + "/models/" + name + ".glb"); ++i)
        name = base + " " + std::to_string(i);
    return name;
}

} // namespace

// ---------------------------------------------------------------- entrar, salir, guardar
void Editor::Impl::meshEditRebuild() {
    std::vector<uint8_t> glb;
    std::string err;
    if (medit.mesh.faces.empty() || !writeGlb(medit.mesh.build(), glb, &err)) return;
    renderer.setModelData("assets/" + medit.glbRel, std::move(glb));
}

void Editor::Impl::meshEditSave() {
    if (!project || !medit.active) return;
    std::vector<uint8_t> glb;
    std::string err;
    if (!medit.mesh.faces.empty() && writeGlb(medit.mesh.build(), glb, &err)) {
        fs::create_directories(project->assetsDir() + "/models");
        fileio::writeAtomic(project->assetsDir() + "/" + medit.tmeshRel, medit.mesh.toJson(), true, &err);
        fileio::writeAtomic(project->assetsDir() + "/" + medit.glbRel, std::string(glb.begin(), glb.end()), true, &err);
        if (!project->assets().findByPath(medit.glbRel)) project->assets().import(medit.glbRel);
    }
}

void Editor::Impl::meshEditSnapshot() {
    medit.undo.push_back(medit.mesh);
    if (medit.undo.size() > 64) medit.undo.erase(medit.undo.begin());
}

void Editor::Impl::meshEditChanged() {
    meshEditRebuild();
    meshEditSave();
    dirty = true;
}

bool Editor::Impl::enterMeshEdit(Entity e) {
    if (!project || e.isNull()) return false;
    MeshRenderer* mr = scene.world().get<MeshRenderer>(e);
    const AssetRecord* r = mr && !mr->model.isNull() ? project->assets().find(mr->model) : nullptr;
    if (!r) {
        setStatus("Para modelar, el objeto necesita un modelo: Crear › Modelo editable");
        return false;
    }
    medit = MeshEditState{};
    const fs::path glbPath(r->path);
    const std::string tmesh = (glbPath.parent_path() / (glbPath.stem().string() + ".tmesh")).generic_string();
    std::string text, err;
    if (fileio::readText(project->assetsDir() + "/" + tmesh, text) && EditMesh::fromJson(text, medit.mesh, &err)) {
        medit.tmeshRel = tmesh;
        medit.glbRel = r->path;
    } else {
        // Modelo importado (OBJ, GLB...): se convierte en malla editable propia en models/, sin tocar el original.
        std::vector<Vec3> verts;
        std::vector<uint32_t> idx;
        if (!renderer.modelTriangles(editorModel(mr->model), verts, idx) || idx.empty()) {
            setStatus("No se pudo leer la malla de este modelo");
            return false;
        }
        EditMesh m;
        m.verts = verts;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) m.faces.push_back({{int(idx[i]), int(idx[i + 1]), int(idx[i + 2])}, {}, 0});
        m.mergeByDistance(1e-5f);
        m.smooth = true;
        medit.mesh = std::move(m);
        const std::string name = uniqueModelName(*project, glbPath.stem().string());
        medit.tmeshRel = "models/" + name + ".tmesh";
        medit.glbRel = "models/" + name + ".glb";
        meshEditSave();
        const AssetRecord* nr = project->assets().findByPath(medit.glbRel);
        if (!nr) return false;
        pushUndo();
        mr->model = nr->id;
        TITAN_INFO("editor", "Malla editable creada en %s (el modelo original no se modifica; texturas y huesos no se "
                             "conservan todavía)", medit.tmeshRel.c_str());
    }
    medit.active = true;
    medit.entity = scene.info(e).id;
    meshEditRebuild();
    select(e);
    focusSelected(); // la cámara encuadra el modelo
    setStatus("Modelado: clic selecciona, arrastrar mueve, E extruir, I inset, X borrar, Tab terminar");
    return true;
}

void Editor::Impl::exitMeshEdit() {
    if (!medit.active) return;
    meshEditSave();
    medit.active = false;
    setStatus("Modelado terminado y guardado");
}

void Editor::Impl::createEditableModel(const char* shape) {
    if (!project) return;
    EditMesh m;
    const std::string s = shape;
    if (s == "Cubo") m = EditMesh::cube(1);
    else if (s == "Esfera") m = EditMesh::uvSphere(0.5f, 24, 12);
    else if (s == "Cilindro") m = EditMesh::cylinder(0.5f, 1, 24);
    else if (s == "Cono") m = EditMesh::cone(0.5f, 1, 24);
    else if (s == "Plano") m = EditMesh::plane(2, 4);
    else if (s == "Cabeza (esfera densa)") {
        m = EditMesh::cube(1);
        m.subdivide(true);
        m.subdivide(true);
        m.subdivide(true);
        m.subdivide(true);
        m.subdivide(true); // 6144 caras redondeadas: base para esculpir
    } else return;
    const std::string name = uniqueModelName(*project, s == "Cabeza (esfera densa)" ? "Escultura" : s);
    medit = MeshEditState{};
    medit.mesh = std::move(m);
    medit.tmeshRel = "models/" + name + ".tmesh";
    medit.glbRel = "models/" + name + ".glb";
    medit.active = true; // para que meshEditSave escriba
    meshEditSave();
    medit.active = false;
    const AssetRecord* r = project->assets().findByPath(medit.glbRel);
    if (!r) {
        setStatus("No se pudo crear el modelo");
        return;
    }
    pushUndo();
    Entity e = scene.createEntity(name);
    MeshRenderer mr;
    mr.model = r->id;
    scene.world().add<MeshRenderer>(e, mr);
    Vec3 p = spawnPoint();
    if (threeD()) p.y = std::max(p.y, 0.5f);
    scene.transform(e).setPosition(p);
    select(e);
    enterMeshEdit(e);
    if (s == "Cabeza (esfera densa)") medit.mode = 2;
}

// ---------------------------------------------------------------- selección y arrastre
namespace {
struct LocalRay {
    Vec3 o, d;
};
} // namespace

bool Editor::Impl::meshEditPointerDown(ImVec2 p) {
    if (!medit.active || runtime) return false;
    Entity e = scene.findById(medit.entity);
    if (e.isNull()) {
        medit.active = false;
        return false;
    }
    Vec3 o, d;
    if (!screenRay(p, o, d)) return false;
    const Mat4 world = scene.computeWorldMatrix(e), inv = world.inverted();
    const Vec3 lo = inv.transformPoint(o), ld = inv.transformDirection(d).normalized();
    const bool add = ImGui::GetIO().KeyShift;
    EditMesh& m = medit.mesh;

    if (medit.mode >= 3) { // esqueleto y animar: clic sobre un hueso lo selecciona
        const int bone = meshEditPickBone(p);
        if (bone >= 0) medit.selBone = bone;
        return bone >= 0;
    }
    if (medit.mode == 2) { // esculpir
        MeshRayHit hit;
        if (!raycastEditMesh(m, lo, ld, hit)) return false;
        meshEditSnapshot();
        medit.sculpting = true;
        medit.brushAt = p;
        medit.stroke.begin(m, hit.point);
        medit.grabNormal = inv.transformDirection(d * -1.0f).normalized();
        medit.grabStart = hit.point;
        return true;
    }

    // Vértice más cercano en pantalla (modo vértices) o cara bajo el ratón (modo caras).
    int picked = -1;
    if (medit.mode == 0) {
        float best = 14 * scale;
        for (size_t i = 0; i < m.verts.size(); ++i) {
            ImVec2 s;
            if (!worldToScreen(world.transformPoint(m.verts[i]), s)) continue;
            const float dd = std::hypot(s.x - p.x, s.y - p.y);
            if (dd < best) {
                best = dd;
                picked = int(i);
            }
        }
    } else {
        MeshRayHit hit;
        if (raycastEditMesh(m, lo, ld, hit)) picked = hit.face;
    }
    std::set<int>& sel = medit.mode == 0 ? medit.selVerts : medit.selFaces;
    if (picked < 0) {
        if (!add) sel.clear();
        return false; // arrastrar en el vacío sigue moviendo la cámara
    }
    if (add) {
        if (!sel.erase(picked)) sel.insert(picked);
    } else if (!sel.count(picked)) {
        sel = {picked};
    }
    // Arrastrar mueve la selección en el plano de la pantalla que pasa por ella.
    medit.dragVerts = medit.mode == 0 ? std::vector<int>(medit.selVerts.begin(), medit.selVerts.end()) : m.faceVertices(medit.selFaces);
    if (medit.dragVerts.empty()) return true;
    Vec3 c;
    for (int v : medit.dragVerts) c += world.transformPoint(m.verts[size_t(v)]);
    c = c / float(medit.dragVerts.size());
    medit.dragPlaneN = (d * -1.0f).normalized();
    medit.dragPlanePoint = c;
    const float denom = dot(d, medit.dragPlaneN);
    if (std::fabs(denom) < 1e-6f) return true;
    medit.dragHitStart = o + d * (dot(c - o, medit.dragPlaneN) / denom);
    medit.dragOrig.clear();
    for (int v : medit.dragVerts) medit.dragOrig.push_back(m.verts[size_t(v)]);
    medit.dragging = true;
    medit.dragMoved = false;
    return true;
}

void Editor::Impl::meshEditPointerMove(ImVec2 p) {
    Entity e = scene.findById(medit.entity);
    if (e.isNull()) return;
    const Mat4 world = scene.computeWorldMatrix(e), inv = world.inverted();
    Vec3 o, d;
    if (!screenRay(p, o, d)) return;
    if (medit.sculpting) {
        medit.brushAt = p;
        return; // el pincel se aplica en meshEditTick, a ritmo constante
    }
    if (!medit.dragging) return;
    const float denom = dot(d, medit.dragPlaneN);
    if (std::fabs(denom) < 1e-6f) return;
    const Vec3 hit = o + d * (dot(medit.dragPlanePoint - o, medit.dragPlaneN) / denom);
    Vec3 delta = inv.transformDirection(hit - medit.dragHitStart);
    if (!medit.dragMoved) {
        meshEditSnapshot();
        medit.dragMoved = true;
    }
    for (size_t i = 0; i < medit.dragVerts.size(); ++i) medit.mesh.verts[size_t(medit.dragVerts[i])] = medit.dragOrig[i] + delta;
    meshEditRebuild();
}

void Editor::Impl::meshEditPointerUp() {
    if (medit.sculpting) {
        medit.sculpting = false;
        meshEditChanged();
        return;
    }
    if (medit.dragging) {
        medit.dragging = false;
        if (medit.dragMoved) meshEditChanged();
    }
}

void Editor::Impl::meshEditTick(float dt) {
    meshEditRigTick(dt);
    if (!medit.active || !medit.sculpting) return;
    Entity e = scene.findById(medit.entity);
    if (e.isNull()) return;
    const Mat4 world = scene.computeWorldMatrix(e), inv = world.inverted();
    Vec3 o, d;
    if (!screenRay(medit.brushAt, o, d)) return;
    const Vec3 lo = inv.transformPoint(o), ld = inv.transformDirection(d).normalized();
    const bool invert = ImGui::GetIO().KeyCtrl;
    const float step = std::min(dt, 0.05f) * 30; // independiente de los FPS
    if (medit.brush == MeshBrush::Grab) {
        const float denom = dot(ld, medit.grabNormal);
        if (std::fabs(denom) < 1e-6f) return;
        const Vec3 hit = lo + ld * (dot(medit.grabStart - lo, medit.grabNormal) / denom);
        sculptMesh(medit.mesh, medit.stroke, MeshBrush::Grab, hit, medit.radius, 1, false, hit - medit.grabStart);
    } else {
        MeshRayHit hit;
        if (!raycastEditMesh(medit.mesh, lo, ld, hit)) return;
        const bool fraction = medit.brush == MeshBrush::Smooth || medit.brush == MeshBrush::Flatten;
        // Fuerza 0..1 (como en Blender). Con 0,5 y el ratón quieto: ~0,1 radios por fotograma a 60 FPS.
        const float k = fraction ? std::min(1.0f, medit.strength * step) : medit.strength * medit.radius * step * 0.4f;
        // Los vecinos y las normales se recalculan en cada paso (la superficie cambia bajo el pincel).
        medit.stroke.begin(medit.mesh, hit.point);
        sculptMesh(medit.mesh, medit.stroke, medit.brush, hit.point, medit.radius, k, invert);
    }
    meshEditRebuild();
}

// ---------------------------------------------------------------- operaciones
void Editor::Impl::meshEditOp(const char* op) {
    if (!medit.active) return;
    EditMesh& m = medit.mesh;
    const std::string o = op;
    if (o == "seleccionar_todo") {
        if (medit.mode == 0) {
            if (medit.selVerts.size() == m.verts.size()) medit.selVerts.clear();
            else for (size_t i = 0; i < m.verts.size(); ++i) medit.selVerts.insert(int(i));
        } else {
            if (medit.selFaces.size() == m.faces.size()) medit.selFaces.clear();
            else for (size_t i = 0; i < m.faces.size(); ++i) medit.selFaces.insert(int(i));
        }
        return;
    }
    if (o == "deshacer") {
        if (medit.undo.empty()) return;
        m = medit.undo.back();
        medit.undo.pop_back();
        medit.selVerts.clear();
        medit.selFaces.clear();
        meshEditChanged();
        return;
    }
    // Las operaciones de caras necesitan caras; si se trabaja con vértices, se usan las caras que tienen todos sus
    // vértices seleccionados.
    std::set<int> faces = medit.selFaces;
    if (medit.mode == 0)
        for (size_t f = 0; f < m.faces.size(); ++f)
            if (std::all_of(m.faces[f].v.begin(), m.faces[f].v.end(), [&](int v) { return medit.selVerts.count(v) > 0; }))
                faces.insert(int(f));
    meshEditSnapshot();
    if (o == "extruir") {
        if (faces.empty()) { setStatus("Extruir: selecciona caras"); medit.undo.pop_back(); return; }
        const std::vector<int> top = m.extrude(faces, medit.extrudeDistance);
        medit.selFaces = std::set<int>(top.begin(), top.end());
        medit.mode = 1;
    } else if (o == "inset") {
        if (faces.empty()) { setStatus("Inset: selecciona caras"); medit.undo.pop_back(); return; }
        m.inset(faces, medit.insetAmount);
    } else if (o == "borrar") {
        if (medit.mode == 0) m.deleteVertices(medit.selVerts);
        else m.deleteFaces(medit.selFaces);
        medit.selVerts.clear();
        medit.selFaces.clear();
    } else if (o == "subdividir") {
        m.subdivide(false);
        medit.selVerts.clear();
        medit.selFaces.clear();
    } else if (o == "suavizar") {
        m.subdivide(true);
        medit.selVerts.clear();
        medit.selFaces.clear();
    } else if (o == "voltear") {
        m.flipNormals(faces);
    } else if (o == "fusionar") {
        const int n = m.mergeByDistance(medit.mergeDistance);
        setStatus(std::to_string(n) + " vértices fusionados");
        medit.selVerts.clear();
        medit.selFaces.clear();
    } else if (o == "simetria_x" || o == "simetria_y" || o == "simetria_z") {
        m.mirror(o.back() - 'x');
        medit.selVerts.clear();
        medit.selFaces.clear();
    }
    meshEditChanged();
}

void Editor::Impl::meshEditExport(bool obj) {
    if (!project || !medit.active) return;
    const std::string dir = project->root() + "/exportados";
    fs::create_directories(dir);
    const std::string name = fs::path(medit.glbRel).stem().string();
    std::string err;
    bool ok = false;
    std::string path;
    if (obj) {
        path = dir + "/" + name + ".obj";
        ok = writeObj(medit.mesh.build(), path, name + ".mtl", &err);
    } else {
        std::vector<uint8_t> glb;
        path = dir + "/" + name + ".glb";
        ok = writeGlb(medit.mesh.build(), glb, &err) && fileio::writeAtomic(path, std::string(glb.begin(), glb.end()), false, &err);
    }
    if (ok) {
        TITAN_INFO("editor", "Exportado %s", path.c_str());
        setStatus("Exportado: " + path);
    } else {
        setStatus("No se pudo exportar: " + err);
    }
}

void Editor::Impl::meshEditKeys() {
    if (!medit.active || runtime) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) exitMeshEdit();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) meshEditOp("deshacer");
    else if (io.KeyCtrl) return;
    else if (ImGui::IsKeyPressed(ImGuiKey_1, false)) medit.mode = 0;
    else if (ImGui::IsKeyPressed(ImGuiKey_3, false)) medit.mode = 1;
    else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) meshEditOp("extruir");
    else if (ImGui::IsKeyPressed(ImGuiKey_I, false)) meshEditOp("inset");
    else if (ImGui::IsKeyPressed(ImGuiKey_X, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) meshEditOp("borrar");
    else if (ImGui::IsKeyPressed(ImGuiKey_A, false)) meshEditOp("seleccionar_todo");
}

// ---------------------------------------------------------------- dibujo
void Editor::Impl::meshEditLines() {
    if (!medit.active || runtime) return;
    Entity e = scene.findById(medit.entity);
    if (e.isNull()) return;
    const Mat4 w = scene.computeWorldMatrix(e);
    const EditMesh& m = medit.mesh;
    if (medit.mode >= 2) return; // al esculpir y animar, la superficie limpia: solo el pincel o los huesos
    size_t lines = 0;
    for (size_t f = 0; f < m.faces.size() && lines < 7000; ++f) {
        const bool sel = medit.mode == 1 && medit.selFaces.count(int(f));
        const std::vector<int>& v = m.faces[f].v;
        for (size_t i = 0; i < v.size(); ++i, ++lines) {
            const int a = v[i], b = v[(i + 1) % v.size()];
            const bool edgeSel = sel || (medit.mode == 0 && medit.selVerts.count(a) && medit.selVerts.count(b));
            renderer.drawLine(w.transformPoint(m.verts[size_t(a)]), w.transformPoint(m.verts[size_t(b)]), edgeSel ? kEdgeSel : kEdge);
        }
    }
}

void Editor::Impl::meshEditOverlay(ImDrawList* dl) {
    if (!medit.active || runtime) return;
    Entity e = scene.findById(medit.entity);
    if (e.isNull()) return;
    const Mat4 w = scene.computeWorldMatrix(e);
    const EditMesh& m = medit.mesh;
    dl->PushClipRect(vpMin, vpMax, true);
    meshEditBones(dl, w);
    if (medit.mode == 0 && m.verts.size() < 20000) {
        // Solo los vértices de caras que miran a la cámara (los de detrás no se pueden seleccionar a ciegas).
        const Vec3 eye = editorView.inverted().translation();
        std::vector<char> front(m.verts.size(), 0);
        for (size_t f = 0; f < m.faces.size(); ++f) {
            const Vec3 c = w.transformPoint(m.faceCenter(int(f)));
            const Vec3 n = w.transformDirection(m.faceNormal(int(f)));
            if (dot(n, eye - c) > 0)
                for (int v : m.faces[f].v) front[size_t(v)] = 1;
        }
        const float r = 3.0f * scale;
        for (size_t i = 0; i < m.verts.size(); ++i) {
            const bool sel = medit.selVerts.count(int(i)) > 0;
            if (!front[i] && !sel) continue;
            ImVec2 s;
            if (!worldToScreen(w.transformPoint(m.verts[i]), s)) continue;
            dl->AddRectFilled(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), sel ? kVertSel : kVert);
        }
    }
    if (medit.mode == 2) { // círculo del pincel sobre la superficie
        const ImVec2 at = medit.sculpting ? medit.brushAt : ImGui::GetIO().MousePos;
        Vec3 o, d;
        MeshRayHit hit;
        const Mat4 inv = w.inverted();
        if (screenRay(at, o, d) && raycastEditMesh(m, inv.transformPoint(o), inv.transformDirection(d).normalized(), hit)) {
            ImVec2 c, edge;
            const Vec3 pw = w.transformPoint(hit.point);
            const Vec3 side = w.transformDirection(cross(hit.normal, std::fabs(hit.normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}).normalized() * medit.radius);
            if (worldToScreen(pw, c) && worldToScreen(pw + side, edge)) {
                const float rr = std::hypot(edge.x - c.x, edge.y - c.y);
                dl->AddCircle(c, rr, IM_COL32(255, 255, 255, 200), 48, 1.5f * scale);
                dl->AddCircle(c, rr * 0.5f, IM_COL32(255, 255, 255, 90), 32, 1.0f * scale);
            }
        }
    }
    dl->PopClipRect();
}

void Editor::Impl::drawMeshEditPanel() {
    static const char* titles[] = {"MODELAR", "MODELAR", "ESCULPIR", "ESQUELETO", "ANIMAR"};
    header(titles[std::clamp(medit.mode, 0, 4)], scale);
    Entity e = scene.findById(medit.entity);
    EditMesh& m = medit.mesh;
    ImGui::TextUnformatted(e.isNull() ? "?" : scene.info(e).name.c_str());
    ImGui::TextDisabled("%zu vértices · %zu caras · %s", m.verts.size(), m.faces.size(), medit.tmeshRel.c_str());
    ImGui::Spacing();
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    const char* modes[] = {"Vértices (1)", "Caras (3)", "Esculpir", "Esqueleto", "Animar"};
    const float bw2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    for (int i = 0; i < 5; ++i) { // dos filas: modelar/esculpir y esqueleto/animar
        if (i != 0 && i != 3) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, medit.mode == i ? ImVec4(0.0f, 0.44f, 0.88f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(modes[i], ImVec2(i < 3 ? bw : bw2, 0))) medit.mode = i;
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    auto op = [&](const char* label, const char* id, float w = -1) {
        if (ImGui::Button(label, ImVec2(w, 0))) meshEditOp(id);
    };
    if (medit.mode == 3) {
        drawRigPanel();
    } else if (medit.mode == 4) {
        drawAnimPanel();
    } else if (medit.mode < 2) {
        ImGui::TextDisabled("Seleccionados: %zu", medit.mode == 0 ? medit.selVerts.size() : medit.selFaces.size());
        ImGui::SeparatorText("Caras");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##ext", &medit.extrudeDistance, -2, 2, "Distancia de extrusión %.2f");
        op("Extruir (E)", "extruir");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##ins", &medit.insetAmount, 0.02f, 0.9f, "Inset %.2f");
        op("Inset (I)", "inset");
        op("Voltear normales", "voltear");
        op("Borrar selección (X)", "borrar");
        op("Seleccionar todo / nada (A)", "seleccionar_todo");
        ImGui::SeparatorText("Malla entera");
        const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        op("Subdividir", "subdividir", hw);
        ImGui::SameLine();
        op("Suavizar (Catmull-Clark)", "suavizar", hw);
        const float tw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
        op("Simetría X", "simetria_x", tw);
        ImGui::SameLine();
        op("Simetría Y", "simetria_y", tw);
        ImGui::SameLine();
        op("Simetría Z", "simetria_z", tw);
        ImGui::SetNextItemWidth(hw);
        ImGui::DragFloat("##merge", &medit.mergeDistance, 0.0005f, 0.0001f, 1, "%.4f m");
        ImGui::SameLine();
        op("Fusionar vértices", "fusionar", hw);
    } else {
        ImGui::SeparatorText("Pincel");
        const MeshBrush brushes[] = {MeshBrush::Draw, MeshBrush::Inflate, MeshBrush::Smooth, MeshBrush::Flatten, MeshBrush::Pinch, MeshBrush::Grab};
        for (int i = 0; i < 6; ++i) {
            if (i % 3) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, medit.brush == brushes[i] ? ImVec4(0.0f, 0.44f, 0.88f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button(meshBrushName(brushes[i]), ImVec2(bw, 0))) medit.brush = brushes[i];
            ImGui::PopStyleColor();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##rad", &medit.radius, 0.02f, 2, "Radio %.2f m");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##str", &medit.strength, 0.01f, 1, "Fuerza %.2f");
        ImGui::TextDisabled("Arrastra sobre el modelo. Ctrl: hundir en vez de levantar.");
        if (m.faces.size() < 4000) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Pocos polígonos: suaviza (subdivide) para esculpir detalle");
        op("Suavizar malla (más polígonos)", "suavizar");
    }
    ImGui::SeparatorText("Material");
    if (!m.materials.empty()) {
        MeshBuildMaterial& mat = m.materials[0];
        float c[3] = {mat.color.r, mat.color.g, mat.color.b};
        if (ImGui::ColorEdit3("Color", c)) {
            mat.color = {c[0], c[1], c[2], 1};
            meshEditRebuild();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) meshEditSave();
        ImGui::SliderFloat("Metal", &mat.metallic, 0, 1);
        if (ImGui::IsItemDeactivatedAfterEdit()) meshEditChanged();
        ImGui::SliderFloat("Rugosidad", &mat.roughness, 0.02f, 1);
        if (ImGui::IsItemDeactivatedAfterEdit()) meshEditChanged();
    }
    if (ImGui::Checkbox("Sombreado suave", &m.smooth)) meshEditChanged();
    ImGui::SeparatorText("Exportar");
    const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("GLB", ImVec2(hw, 0))) meshEditExport(false);
    ImGui::SameLine();
    if (ImGui::Button("OBJ", ImVec2(hw, 0))) meshEditExport(true);
    ImGui::TextDisabled("Se guarda en la carpeta exportados/ del proyecto");
    ImGui::Spacing();
    op("Deshacer (Ctrl+Z)", "deshacer");
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.88f, 1));
    if (ImGui::Button("Terminar (Tab)", ImVec2(-1, 0))) exitMeshEdit();
    ImGui::PopStyleColor();
}

} // namespace titan

// ---------------------------------------------------------------- esqueleto y animación (modos 3 y 4)
namespace titan {

namespace {
// Pose actual: matrices de cada hueso (clip y tiempo del editor en Animar; reposo en Esqueleto).
std::vector<Mat4> currentSkin(const Editor::Impl::MeshEditState& s) {
    return s.mesh.rig.skinMatrices(s.mode == 4 ? s.clip : -1, s.time);
}
} // namespace

// Vista previa de la pose: la malla deformada en la CPU, sin esqueleto (solo para ver; el GLB guardado lleva la piel).
void Editor::Impl::meshEditPosePreview() {
    EditMesh posed = medit.mesh;
    Rig& rig = posed.rig;
    if (rig.joints.size() != posed.verts.size()) rig.autoWeights(posed.verts);
    posed.verts = rig.deform(posed.verts, currentSkin(medit));
    posed.rig = Rig{};
    std::vector<uint8_t> glb;
    std::string err;
    if (!posed.faces.empty() && writeGlb(posed.build(), glb, &err)) renderer.setModelData("assets/" + medit.glbRel, std::move(glb));
}

void Editor::Impl::drawRigPanel() {
    Rig& rig = medit.mesh.rig;
    if (rig.empty()) {
        ImGui::TextWrapped("El modelo no tiene esqueleto. El automático crea 19 huesos (cadera, columna, cabeza, brazos, "
                           "piernas) ajustados al tamaño del modelo, de pie y mirando a +Z, y calcula los pesos.");
        if (ImGui::Button("Esqueleto humano automático", ImVec2(-1, 0))) {
            meshEditSnapshot();
            Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
            for (const Vec3& v : medit.mesh.verts) {
                mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
                mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
            }
            rig = Rig::humanoid(mn, mx);
            rig.autoWeights(medit.mesh.verts);
            meshEditChanged();
        }
        if (ImGui::Button("Un solo hueso (raíz)", ImVec2(-1, 0))) {
            meshEditSnapshot();
            rig.bones.push_back({"Raíz", -1, {0, 0, 0}, {0, 0.5f, 0}});
            rig.autoWeights(medit.mesh.verts);
            meshEditChanged();
        }
        return;
    }
    ImGui::TextDisabled("%zu huesos. Clic en un hueso de la vista para seleccionarlo.", rig.bones.size());
    ImGui::BeginChild("##huesos", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        int depth = 0;
        for (int p = rig.bones[i].parent; p >= 0; p = rig.bones[size_t(p)].parent) ++depth;
        ImGui::Indent(float(depth) * 8 * scale);
        if (ImGui::Selectable((rig.bones[i].name + "##b" + std::to_string(i)).c_str(), medit.selBone == int(i))) medit.selBone = int(i);
        ImGui::Unindent(float(depth) * 8 * scale);
    }
    ImGui::EndChild();
    if (medit.selBone >= 0 && size_t(medit.selBone) < rig.bones.size()) {
        RigBone& b = rig.bones[size_t(medit.selBone)];
        ImGui::InputText("Nombre", &b.name);
        if (ImGui::IsItemDeactivatedAfterEdit()) meshEditSave();
        bool moved = ImGui::DragFloat3("Cabeza", &b.head.x, 0.005f);
        const bool done1 = ImGui::IsItemDeactivatedAfterEdit();
        moved |= ImGui::DragFloat3("Cola", &b.tail.x, 0.005f);
        if (moved) rig.joints.clear(); // los pesos se recalculan al construir
        if (done1 || ImGui::IsItemDeactivatedAfterEdit()) meshEditChanged();
        const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Añadir hijo", ImVec2(hw, 0))) {
            meshEditSnapshot();
            const Vec3 dir = b.tail - b.head;
            rig.bones.push_back({b.name + ".hijo", medit.selBone, b.tail, b.tail + dir * 0.6f});
            medit.selBone = int(rig.bones.size() - 1);
            rig.joints.clear();
            meshEditChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("Borrar hueso", ImVec2(hw, 0)) && rig.bones.size() > 1) {
            // Se borra con sus descendientes; los índices de padres y pistas se reajustan.
            meshEditSnapshot();
            std::vector<char> gone(rig.bones.size(), 0);
            gone[size_t(medit.selBone)] = 1;
            for (size_t i = 0; i < rig.bones.size(); ++i)
                if (rig.bones[i].parent >= 0 && gone[size_t(rig.bones[i].parent)]) gone[i] = 1;
            std::vector<int> remap(rig.bones.size(), -1);
            std::vector<RigBone> kept;
            for (size_t i = 0; i < rig.bones.size(); ++i)
                if (!gone[i]) {
                    remap[i] = int(kept.size());
                    kept.push_back(rig.bones[i]);
                }
            for (RigBone& k : kept) k.parent = k.parent >= 0 ? remap[size_t(k.parent)] : -1;
            for (RigClip& c : rig.clips) {
                std::vector<RigTrack> tracks;
                for (RigTrack t : c.tracks)
                    if (t.bone >= 0 && remap[size_t(t.bone)] >= 0) {
                        t.bone = remap[size_t(t.bone)];
                        tracks.push_back(t);
                    }
                c.tracks = tracks;
            }
            rig.bones = kept;
            rig.joints.clear();
            medit.selBone = -1;
            meshEditChanged();
        }
    }
    if (ImGui::Button("Recalcular pesos automáticos", ImVec2(-1, 0))) {
        meshEditSnapshot();
        rig.autoWeights(medit.mesh.verts);
        meshEditChanged();
    }
    if (ImGui::Button("Quitar esqueleto", ImVec2(-1, 0))) {
        meshEditSnapshot();
        rig = Rig{};
        medit.selBone = -1;
        meshEditChanged();
    }
}

void Editor::Impl::drawAnimPanel() {
    Rig& rig = medit.mesh.rig;
    if (rig.empty()) {
        ImGui::TextWrapped("Para animar, primero crea el esqueleto (botón Esqueleto).");
        return;
    }
    if (ImGui::Button("+ Nuevo clip", ImVec2(-1, 0))) {
        meshEditSnapshot();
        RigClip c;
        c.name = "Animación " + std::to_string(rig.clips.size() + 1);
        rig.clips.push_back(c);
        medit.clip = int(rig.clips.size() - 1);
        medit.time = 0;
        meshEditSave();
    }
    if (rig.clips.empty()) {
        ImGui::TextDisabled("Crea un clip para empezar (Idle, Caminar, Saludar...).");
        return;
    }
    medit.clip = std::clamp(medit.clip, 0, int(rig.clips.size()) - 1);
    if (ImGui::BeginCombo("Clip", rig.clips[size_t(medit.clip)].name.c_str())) {
        for (size_t i = 0; i < rig.clips.size(); ++i)
            if (ImGui::Selectable(rig.clips[i].name.c_str(), medit.clip == int(i))) {
                medit.clip = int(i);
                medit.time = 0;
            }
        ImGui::EndCombo();
    }
    RigClip& clip = rig.clips[size_t(medit.clip)];
    ImGui::InputText("Nombre del clip", &clip.name);
    if (ImGui::IsItemDeactivatedAfterEdit()) meshEditSave();
    ImGui::DragFloat("Duración (s)", &clip.duration, 0.05f, 0.1f, 60);
    if (ImGui::IsItemDeactivatedAfterEdit()) meshEditSave();
    if (ImGui::Checkbox("Repetir", &clip.loop)) meshEditSave();

    // Línea de tiempo con las claves marcadas.
    ImGui::SeparatorText("Línea de tiempo");
    if (ImGui::Button(medit.playing ? "Pausa" : "Reproducir")) medit.playing = !medit.playing;
    ImGui::SameLine();
    if (ImGui::Button("|<")) medit.time = 0;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##t", &medit.time, 0, clip.duration, "%.2f s");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        for (float k : clip.keyTimes()) {
            const float x = a.x + (b.x - a.x) * std::clamp(k / std::max(0.01f, clip.duration), 0.0f, 1.0f);
            dl->AddTriangleFilled(ImVec2(x, b.y - 2), ImVec2(x - 4 * scale, b.y + 5 * scale), ImVec2(x + 4 * scale, b.y + 5 * scale),
                                  IM_COL32(255, 190, 60, 255));
        }
        ImGui::Dummy(ImVec2(0, 6 * scale));
    }
    // Hueso seleccionado: girarlo crea o cambia la clave en el tiempo actual (clave automática).
    ImGui::SeparatorText("Pose");
    if (medit.selBone < 0 || size_t(medit.selBone) >= rig.bones.size()) {
        ImGui::TextDisabled("Selecciona un hueso (clic en la vista o en la lista de Esqueleto).");
    } else {
        ImGui::TextUnformatted(rig.bones[size_t(medit.selBone)].name.c_str());
        Quat q;
        Vec3 tr;
        clip.sample(medit.selBone, medit.time, q, tr);
        Vec3 e = q.toEuler() * (180.0f / kPi);
        bool changed = ImGui::DragFloat3("Giro (grados)", &e.x, 0.5f, -180, 180);
        const bool done = ImGui::IsItemDeactivatedAfterEdit();
        bool moved = false, movedDone = false;
        if (rig.bones[size_t(medit.selBone)].parent < 0) { // la raíz también se desplaza (saltos, caminar)
            moved = ImGui::DragFloat3("Desplazamiento", &tr.x, 0.005f);
            movedDone = ImGui::IsItemDeactivatedAfterEdit();
        }
        if (changed || moved) {
            RigKey k;
            k.time = medit.time;
            k.rotation = Quat::fromEuler(e * (kPi / 180.0f));
            k.translation = tr;
            clip.setKey(medit.selBone, k);
            medit.posePreview = true;
        }
        if (done || movedDone) meshEditSave();
        const float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Insertar clave", ImVec2(hw, 0))) {
            RigKey k;
            k.time = medit.time;
            k.rotation = q;
            k.translation = tr;
            clip.setKey(medit.selBone, k);
            meshEditSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Borrar clave", ImVec2(hw, 0)) && clip.removeKey(medit.selBone, medit.time)) {
            medit.posePreview = true;
            meshEditSave();
        }
    }
    ImGui::TextDisabled("En el juego: Animator › clip \"%s\"", clip.name.c_str());
}

void Editor::Impl::meshEditRigTick(float dt) {
    if (!medit.active) return;
    // Al salir de Animar se vuelve a la malla con piel; al entrar o cambiar la pose, se previsualiza.
    if (medit.lastMode != medit.mode) {
        if (medit.lastMode == 4) meshEditRebuild();
        if (medit.mode == 4) medit.posePreview = true;
        medit.lastMode = medit.mode;
    }
    if (medit.mode != 4 || medit.mesh.rig.empty() || medit.mesh.rig.clips.empty()) return;
    static float lastTime = -1;
    if (medit.playing) {
        const RigClip& c = medit.mesh.rig.clips[size_t(std::clamp(medit.clip, 0, int(medit.mesh.rig.clips.size()) - 1))];
        medit.time += dt;
        if (medit.time > c.duration) medit.time = c.loop ? std::fmod(medit.time, std::max(0.01f, c.duration)) : c.duration;
    }
    if (medit.posePreview || medit.time != lastTime) {
        meshEditPosePreview();
        medit.posePreview = false;
        lastTime = medit.time;
    }
}

void Editor::Impl::meshEditBones(ImDrawList* dl, const Mat4& w) {
    const Rig& rig = medit.mesh.rig;
    if (rig.empty() || (medit.mode != 3 && medit.mode != 4)) return;
    const std::vector<Mat4> skin = currentSkin(medit);
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const Vec3 h = w.transformPoint(skin[i].transformPoint(rig.bones[i].head));
        const Vec3 t = w.transformPoint(skin[i].transformPoint(rig.bones[i].tail));
        ImVec2 a, b;
        if (!worldToScreen(h, a) || !worldToScreen(t, b)) continue;
        const bool sel = medit.selBone == int(i);
        const ImU32 col = sel ? IM_COL32(255, 170, 40, 255) : IM_COL32(120, 200, 255, 230);
        dl->AddLine(a, b, col, (sel ? 4.0f : 2.5f) * scale);
        dl->AddCircleFilled(a, 4 * scale, col);
    }
}

int Editor::Impl::meshEditPickBone(ImVec2 p) {
    Entity e = scene.findById(medit.entity);
    const Rig& rig = medit.mesh.rig;
    if (e.isNull() || rig.empty()) return -1;
    const Mat4 w = scene.computeWorldMatrix(e);
    const std::vector<Mat4> skin = currentSkin(medit);
    int best = -1;
    float bestD = 12 * scale;
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        ImVec2 a, b;
        if (!worldToScreen(w.transformPoint(skin[i].transformPoint(rig.bones[i].head)), a) ||
            !worldToScreen(w.transformPoint(skin[i].transformPoint(rig.bones[i].tail)), b))
            continue;
        const float vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy;
        const float t = l2 > 0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / l2, 0.0f, 1.0f) : 0.0f;
        const float d = std::hypot(p.x - (a.x + vx * t), p.y - (a.y + vy * t));
        if (d < bestD) {
            bestD = d;
            best = int(i);
        }
    }
    return best;
}

} // namespace titan
