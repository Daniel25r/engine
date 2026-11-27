// Modo de modelado y escultura del editor (como el Edit Mode y el Sculpt Mode de Blender, primera versión).
// Se edita la malla de un objeto cuyo modelo es models/<Nombre>.glb con su fuente models/<Nombre>.tmesh. Cada cambio
// se ve al momento (el GLB se reconstruye en memoria) y al soltar el ratón se guarda en disco.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "EditorImpl.h"
#include "imgui.h"
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
    if (medit.mode == 2) return; // al esculpir, la superficie limpia (como en Blender): solo el círculo del pincel
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
    header(medit.mode == 2 ? "ESCULPIR" : "MODELAR", scale);
    Entity e = scene.findById(medit.entity);
    EditMesh& m = medit.mesh;
    ImGui::TextUnformatted(e.isNull() ? "?" : scene.info(e).name.c_str());
    ImGui::TextDisabled("%zu vértices · %zu caras · %s", m.verts.size(), m.faces.size(), medit.tmeshRel.c_str());
    ImGui::Spacing();
    const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    const char* modes[] = {"Vértices (1)", "Caras (3)", "Esculpir"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, medit.mode == i ? ImVec4(0.0f, 0.44f, 0.88f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(modes[i], ImVec2(bw, 0))) medit.mode = i;
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    auto op = [&](const char* label, const char* id, float w = -1) {
        if (ImGui::Button(label, ImVec2(w, 0))) meshEditOp(id);
    };
    if (medit.mode < 2) {
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
