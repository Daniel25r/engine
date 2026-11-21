// Paneles del editor. Cada control modifica directamente los datos de la escena (titan::Scene)
// y registra la operación para poder deshacerla.
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "EditorImpl.h"
#include "imgui_stdlib.h"
#include "lauxlib.h"
#include "lua.h"
#include "titan/core/FileIO.h"
#include "titan/core/Templates.h"
#include "titan/core/Version.h"

namespace fs = std::filesystem;

namespace titan {

namespace {

const ImVec4 kAccent(0.949f, 0.71f, 0.29f, 1);
const ImVec4 kPlay(0.30f, 0.70f, 0.45f, 1);
const ImVec4 kStop(0.85f, 0.36f, 0.38f, 1);
const ImVec4 kDim(0.56f, 0.61f, 0.70f, 1);

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

bool beginPanel(const char* id, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extra = 0) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    return ImGui::Begin(id, nullptr, kPanelFlags | extra);
}

void sectionTitle(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

bool coloredButton(const char* label, ImVec4 color, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 1.1f, color.y * 1.1f, color.z * 1.1f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.9f, color.y * 0.9f, color.z * 0.9f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.07f, 0.09f, 1));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool toggleButton(const char* label, bool on) {
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f));
    const bool r = ImGui::Button(label);
    if (on) ImGui::PopStyleColor();
    return r;
}

std::string fileName(const std::string& path) {
    const size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

std::string identifierFrom(const std::string& name) {
    std::string out;
    bool upper = true;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            out += upper ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
            upper = false;
        } else {
            upper = true;
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "Script" + out;
    return out;
}

const char* kTemplateNames[] = {"2D vacío", "3D vacío", "2D Plataformas (jugable)", "3D Exploración (jugable)"};
const char* kTemplateHelp[] = {
    "Una cámara 2D. Para empezar desde cero.",
    "Cámara en perspectiva, luz, suelo y un cubo.",
    "Un personaje que corre, salta y recoge monedas. Todo está programado en Lua y puedes cambiarlo.",
    "Un personaje que camina donde tocas, con cámara que le sigue, luces y gemas que recoger.",
};

} // namespace

// ============================================================ Distribución general

void Editor::Impl::drawUI() {
    vpVisible = false;
    if (!project || showBrowser) {
        drawBrowser();
        return;
    }
    const float W = float(fbWidth), H = float(fbHeight);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float barH = ImGui::GetFrameHeight() + st.WindowPadding.y * 2;
    drawToolbar(ImVec2(0, 0), ImVec2(W, barH));
    const bool wide = W > H * 1.2f && W > 760 * scale;
    const float tabsH = ImGui::GetFrameHeight() + st.WindowPadding.y * 2;

    if (wide) {
        const float left = std::max(230 * scale, W * 0.22f), right = std::max(270 * scale, W * 0.27f);
        const float colH = H - barH;
        if (beginPanel("##jerarquia", ImVec2(0, barH), ImVec2(left, colH * 0.58f))) drawHierarchy();
        ImGui::End();
        if (beginPanel("##recursos", ImVec2(0, barH + colH * 0.58f), ImVec2(left, colH * 0.42f))) drawAssets();
        ImGui::End();
        if (beginPanel("##inspector", ImVec2(W - right, barH), ImVec2(right, colH))) drawInspector();
        ImGui::End();
        const float cx = left, cw = W - left - right;
        if (beginPanel("##pestanas", ImVec2(cx, barH), ImVec2(cw, tabsH), ImGuiWindowFlags_NoScrollbar)) drawPanelTabs(true);
        ImGui::End();
        const ImVec2 cpos(cx, barH + tabsH), csize(cw, H - barH - tabsH);
        if (panel == Panel::Code || panel == Panel::Console) {
            if (beginPanel("##central", cpos, csize)) drawPanel(panel);
            ImGui::End();
        } else {
            vpVisible = true;
            vpMin = cpos;
            vpMax = ImVec2(cpos.x + csize.x, cpos.y + csize.y);
        }
    } else {
        if (beginPanel("##pestanas", ImVec2(0, barH), ImVec2(W, tabsH), ImGuiWindowFlags_NoScrollbar |
                                                                          ImGuiWindowFlags_HorizontalScrollbar))
            drawPanelTabs(false);
        ImGui::End();
        const float y0 = barH + tabsH, avail = H - y0;
        if (panel == Panel::Scene) {
            vpVisible = true;
            vpMin = ImVec2(0, y0);
            vpMax = ImVec2(W, H);
        } else if (panel == Panel::Hierarchy || panel == Panel::Inspector) {
            const float vh = avail * 0.42f;
            vpVisible = true;
            vpMin = ImVec2(0, y0);
            vpMax = ImVec2(W, y0 + vh);
            if (beginPanel("##panel", ImVec2(0, y0 + vh), ImVec2(W, avail - vh))) drawPanel(panel);
            ImGui::End();
        } else {
            if (beginPanel("##panel", ImVec2(0, y0), ImVec2(W, avail))) drawPanel(panel);
            ImGui::End();
        }
    }

    // Indicaciones sobre la vista de escena.
    if (vpVisible) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const char* hint = runtime ? "Jugando: toca la vista para interactuar con el juego"
                         : tool == Tool::Move ? "Mover: arrastra un objeto · 1 dedo en vacío gira/desplaza · 2 dedos zoom"
                                              : "Cámara: 1 dedo gira/desplaza · 2 dedos desplazan y hacen zoom";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const ImVec2 p(vpMin.x + 8 * scale, vpMax.y - ts.y - 8 * scale);
        dl->AddRectFilled(ImVec2(p.x - 4 * scale, p.y - 2 * scale), ImVec2(p.x + ts.x + 4 * scale, p.y + ts.y + 2 * scale),
                          IM_COL32(16, 20, 28, 170), 4 * scale);
        dl->AddText(p, IM_COL32(200, 208, 222, 255), hint);
        if (runtime) dl->AddRect(vpMin, vpMax, IM_COL32(77, 178, 115, 255), 0, 0, 3 * scale);
    }
}

void Editor::Impl::drawPanelTabs(bool wide) {
    struct Tab { const char* label; Panel p; };
    static const Tab all[] = {{"Escena", Panel::Scene}, {"Jerarquía", Panel::Hierarchy}, {"Inspector", Panel::Inspector},
                              {"Código", Panel::Code},  {"Recursos", Panel::Assets},     {"Consola", Panel::Console}};
    bool first = true;
    for (const Tab& t : all) {
        if (wide && (t.p == Panel::Hierarchy || t.p == Panel::Inspector || t.p == Panel::Assets)) continue;
        if (!first) ImGui::SameLine();
        first = false;
        const bool on = panel == t.p || (wide && t.p == Panel::Scene && panel != Panel::Code && panel != Panel::Console);
        std::string label = t.label;
        if (t.p == Panel::Code && codeDirty) label += " *";
        if (t.p == Panel::Console && runtime && !runtime->scripts().errors().empty()) label += " (!)";
        if (toggleButton((label + "##tab").c_str(), on)) panel = t.p;
    }
}

void Editor::Impl::drawPanel(Panel p) {
    switch (p) {
    case Panel::Hierarchy: drawHierarchy(); break;
    case Panel::Inspector: drawInspector(); break;
    case Panel::Code: drawCode(); break;
    case Panel::Assets: drawAssets(); break;
    case Panel::Console: drawConsole(); break;
    default: break;
    }
}

// ============================================================ Barra superior

void Editor::Impl::drawToolbar(const ImVec2& pos, const ImVec2& size) {
    if (!beginPanel("##barra", pos, size, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Menú")) ImGui::OpenPopup("menu");
    if (ImGui::BeginPopup("menu")) {
        if (ImGui::MenuItem("Guardar", "Ctrl+S")) save();
        if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, !undoStack.empty() && !runtime)) undo();
        if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, !redoStack.empty() && !runtime)) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Exportar paquete del juego")) exportPackage();
        ImGui::Separator();
        if (ImGui::MenuItem("Proyectos...")) {
            save();
            showBrowser = true;
        }
        ImGui::EndPopup();
    }
    const bool wide = size.x > 900 * scale;
    if (wide) {
        ImGui::SameLine();
        if (ImGui::Button(dirty ? "Guardar *" : "Guardar")) save();
        ImGui::SameLine();
        ImGui::BeginDisabled(undoStack.empty() || runtime);
        if (ImGui::Button("Deshacer")) undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redoStack.empty() || runtime);
        if (ImGui::Button("Rehacer")) redo();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (toggleButton("Mover", tool == Tool::Move)) tool = Tool::Move;
    ImGui::SameLine();
    if (toggleButton("Cámara", tool == Tool::Camera)) tool = Tool::Camera;
    ImGui::SameLine();
    if (toggleButton("Imán", snap)) snap = !snap;

    // Nombre del proyecto y estado.
    ImGui::SameLine();
    std::string title = project->name() + (dirty ? " *" : "");
    if (statusTime > 0 && wide) title += "   ·   " + status;
    const float playW = ImGui::CalcTextSize("Detener").x + ImGui::GetStyle().FramePadding.x * 2 + 8 * scale;
    const float room = ImGui::GetContentRegionAvail().x - playW;
    if (room > 40 * scale) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusTime > 0 ? kAccent : kDim);
        ImGui::SetNextItemWidth(room);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(statusTime > 0 && !wide ? status.c_str() : title.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(size.x - playW);
    if (runtime) {
        if (coloredButton("Detener", kStop)) stop();
    } else if (coloredButton("Jugar", kPlay)) {
        play();
    }
    ImGui::End();
}

// ============================================================ Navegador de proyectos

void Editor::Impl::drawBrowser() {
    const float W = float(fbWidth), H = float(fbHeight);
    if (!beginPanel("##proyectos", ImVec2(0, 0), ImVec2(W, H))) {
        ImGui::End();
        return;
    }
    const float pad = std::max(0.0f, (W - 720 * scale) * 0.5f);
    ImGui::Indent(pad);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::Text("%s", kEngineName);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("motor de videojuegos %s", kEngineVersion);
    if (project && ImGui::Button(("Volver a " + project->name()).c_str())) showBrowser = false;
    ImGui::Separator();

    sectionTitle("NUEVO PROYECTO");
    ImGui::SetNextItemWidth(std::min(W - pad * 2 - 20 * scale, 420 * scale));
    ImGui::InputText("Nombre", &newProjectName);
    for (int i = 0; i < 4; ++i) {
        ImGui::RadioButton(kTemplateNames[i], &newProjectTemplate, i);
        ImGui::Indent(30 * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::PushTextWrapPos(W - pad - 10 * scale);
        ImGui::TextUnformatted(kTemplateHelp[i]);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Unindent(30 * scale);
    }
    std::string safeName;
    for (char c : newProjectName) safeName += (c == '/' || c == '\\' || c == ':' || c == '.') ? '_' : c;
    const std::string dir = projectsDir() + "/" + safeName;
    const bool exists = !safeName.empty() && fs::exists(dir);
    ImGui::BeginDisabled(safeName.empty() || exists);
    if (coloredButton("Crear proyecto", kAccent)) {
        static const ProjectTemplate t[] = {ProjectTemplate::Empty2D, ProjectTemplate::Empty3D,
                                            ProjectTemplate::Platformer2D, ProjectTemplate::Explorer3D};
        std::string err;
        if (createProjectFromTemplate(dir, newProjectName, t[newProjectTemplate], &err)) openProject(dir);
        else setStatus("No se pudo crear: " + err);
    }
    ImGui::EndDisabled();
    if (exists) {
        ImGui::SameLine();
        ImGui::TextDisabled("Ya hay un proyecto con ese nombre");
    }

    ImGui::Spacing();
    ImGui::Separator();
    sectionTitle("TUS PROYECTOS");
    std::vector<std::pair<std::string, std::string>> found; // carpeta, descripción
    std::error_code ec;
    for (auto& d : fs::directory_iterator(projectsDir(), ec)) {
        if (!d.is_directory() || !fs::exists(d.path() / "project.titan")) continue;
        std::string text, name;
        ProjectDimension dim = ProjectDimension::TwoD;
        UUID start;
        if (!fileio::readText((d.path() / "project.titan").string(), text) ||
            !Project::parseProjectFile(text, name, dim, start, nullptr)) {
            found.push_back({d.path().generic_string(), d.path().filename().string() + "  (no se puede leer)"});
            continue;
        }
        found.push_back({d.path().generic_string(), name + (dim == ProjectDimension::ThreeD ? "   3D" : "   2D")});
    }
    std::sort(found.begin(), found.end());
    if (found.empty()) ImGui::TextDisabled("Aún no hay proyectos. Crea el primero arriba.");
    for (auto& [path, label] : found) {
        ImGui::PushID(path.c_str());
        if (ImGui::Button("Abrir")) openProject(path);
        ImGui::SameLine();
        if (pendingDelete == path) {
            if (coloredButton("Confirmar borrado", kStop)) {
                if (project && project->root() == path) closeProject();
                fs::remove_all(path, ec);
                pendingDelete.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) pendingDelete.clear();
        } else if (ImGui::Button("Borrar")) {
            pendingDelete = path;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Carpeta: %s", projectsDir().c_str());
    ImGui::Unindent(pad);
    ImGui::End();
}

// ============================================================ Jerarquía

void Editor::Impl::drawHierarchy() {
    sectionTitle("JERARQUÍA");
    ImGui::SameLine();
    if (ImGui::Button("+ Crear")) ImGui::OpenPopup("crear");
    if (ImGui::BeginPopup("crear")) {
        static const char* kinds2D[] = {"Vacío", "Sprite", "Cámara"};
        static const char* kinds3D[] = {"Cubo", "Esfera", "Plano", "Cilindro", "Cono", "Cápsula", "Luz direccional", "Luz puntual"};
        for (const char* k : kinds2D)
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::Separator();
        for (const char* k : kinds3D)
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::EndPopup();
    }
    Entity sel = selectedEntity();
    if (!sel.isNull()) {
        ImGui::SameLine();
        if (ImGui::Button("Duplicar")) duplicateSelected();
        ImGui::SameLine();
        if (ImGui::Button("Eliminar")) deleteSelected();
    }
    ImGui::Separator();
    for (Entity r : scene.roots()) drawHierarchyNode(r);

    // Zona para sacar una entidad a la raíz.
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::Selectable("(suelta aquí para sacarla a la raíz)", false, ImGuiSelectableFlags_Disabled);
    ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TITAN_ENTITY")) {
            Entity child = scene.findById(UUID{*static_cast<const uint64_t*>(pl->Data)});
            if (!child.isNull() && !scene.parent(child).isNull()) {
                pushUndo();
                scene.setParent(child, {}, true);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (scene.entityCount() == 0) ImGui::TextDisabled("La escena está vacía. Pulsa \"+ Crear\".");
}

void Editor::Impl::drawHierarchyNode(Entity e) {
    const EntityInfo& info = scene.info(e);
    const auto kids = scene.children(e);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_FramePadding;
    if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (info.id == selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!info.active) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(info.id.value)), flags, "%s",
                                        info.name.c_str());
    if (!info.active) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected = info.id;
        inspectorEulerFor = {};
    }
    if (ImGui::BeginDragDropSource()) {
        const uint64_t id = info.id.value;
        ImGui::SetDragDropPayload("TITAN_ENTITY", &id, sizeof(id));
        ImGui::Text("Mover %s", info.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TITAN_ENTITY")) {
            Entity child = scene.findById(UUID{*static_cast<const uint64_t*>(pl->Data)});
            if (!child.isNull() && child != e && !scene.isDescendantOf(e, child)) {
                pushUndo();
                scene.setParent(child, e, true);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // Etiqueta breve con lo que es.
    const char* kind = nullptr;
    if (scene.world().has<Camera>(e)) kind = "cámara";
    else if (scene.world().has<Light>(e)) kind = "luz";
    else if (scene.world().has<MeshRenderer>(e)) kind = "malla";
    else if (scene.world().has<SpriteRenderer>(e)) kind = "sprite";
    if (kind || scene.world().has<Script>(e)) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", kind ? kind : "", scene.world().has<Script>(e) ? " +lua" : "");
    }
    if (open) {
        for (Entity c : kids) drawHierarchyNode(c);
        ImGui::TreePop();
    }
}

// ============================================================ Inspector

bool Editor::Impl::assetCombo(const char* label, AssetType type, AssetRef& ref) {
    const AssetRecord* cur = project->assets().find(ref);
    const std::string preview = ref.isNull() ? "(ninguno)" : cur ? fileName(cur->path) : "(no encontrado)";
    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("(ninguno)", ref.isNull())) {
            pushUndo();
            ref = {};
            changed = true;
        }
        for (const AssetRecord* r : project->assets().all()) {
            if (r->type != type) continue;
            if (ImGui::Selectable(r->path.c_str(), r->id == ref)) {
                pushUndo();
                ref = r->id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void Editor::Impl::drawInspector() {
    sectionTitle("INSPECTOR");
    Entity e = selectedEntity();
    if (e.isNull()) {
        ImGui::TextDisabled("Selecciona una entidad en la escena o en la jerarquía.");
        return;
    }
    EntityInfo& info = scene.info(e);
    ImGui::PushID(static_cast<int>(info.id.value));
    bool active = info.active;
    if (ImGui::Checkbox("##activa", &active)) {
        pushUndo();
        info.active = active;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##nombre", &info.name);
    track();
    ImGui::TextDisabled("id %s", info.id.toString().c_str());

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform& t = scene.transform(e);
        Vec3 p = t.position(), s = t.scale();
        if (inspectorEulerFor != info.id || !ImGui::IsAnyItemActive()) {
            // Se recalcula desde el cuaternión salvo mientras se arrastra (evita saltos de Euler).
            inspectorEuler = t.eulerDegrees();
            inspectorEulerFor = info.id;
        }
        if (ImGui::DragFloat3("Posición", &p.x, 0.05f)) t.setPosition(p);
        track();
        if (ImGui::DragFloat3("Rotación", &inspectorEuler.x, 0.5f)) t.setEulerDegrees(inspectorEuler);
        track();
        if (ImGui::DragFloat3("Escala", &s.x, 0.02f)) t.setScale(s);
        track();
        if (ImGui::SmallButton("Restablecer")) {
            pushUndo();
            t.reset();
        }
    }

    for (const auto& type : registry.types()) {
        if (type.name == "Transform" || !type.has(scene, e)) continue;
        bool keep = true;
        if (ImGui::CollapsingHeader(type.name.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(type.name.c_str());
            drawComponentFields(type.name, e);
            ImGui::PopID();
        }
        if (!keep && type.remove) {
            pushUndo();
            type.remove(scene, e);
            break;
        }
    }
    if (auto* kept = scene.world().get<PreservedComponents>(e))
        for (auto it = kept->data.begin(); it != kept->data.end(); ++it)
            ImGui::TextDisabled("%s (tipo desconocido, se conserva)", it.key().c_str());

    ImGui::Spacing();
    if (ImGui::Button("Añadir componente", ImVec2(-1, 0))) ImGui::OpenPopup("añadir");
    if (ImGui::BeginPopup("añadir")) {
        for (const auto& type : registry.types()) {
            if (type.name == "Transform" || type.has(scene, e)) continue;
            if (ImGui::MenuItem(type.name.c_str())) {
                pushUndo();
                LoadReport ignored;
                const Json empty = Json::object();
                FieldReader reader(empty, type.name, ignored);
                type.load(scene, e, reader);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void Editor::Impl::drawComponentFields(const std::string& name, Entity e) {
    World& w = scene.world();
    if (name == "Camera") {
        Camera& c = *w.get<Camera>(e);
        int proj = static_cast<int>(c.projection);
        if (ImGui::Combo("Proyección", &proj, "Ortográfica (2D)\0Perspectiva (3D)\0")) {
            pushUndo();
            c.projection = static_cast<Camera::Projection>(proj);
        }
        if (c.projection == Camera::Projection::Orthographic) {
            ImGui::DragFloat("Tamaño", &c.orthographicSize, 0.05f, 0.1f, 500.0f);
            track();
        } else {
            ImGui::DragFloat("Campo de visión", &c.fieldOfView, 0.25f, 10.0f, 150.0f);
            track();
        }
        ImGui::DragFloat("Plano cercano", &c.nearClip, 0.01f, 0.001f, c.farClip);
        track();
        ImGui::DragFloat("Plano lejano", &c.farClip, 1.0f, c.nearClip + 0.01f, 100000.0f);
        track();
        ImGui::DragInt("Prioridad", &c.priority, 0.1f);
        track();
        ImGui::ColorEdit4("Fondo", &c.clearColor.r);
        track();
    } else if (name == "SpriteRenderer") {
        SpriteRenderer& s = *w.get<SpriteRenderer>(e);
        assetCombo("Textura", AssetType::Texture, s.texture);
        ImGui::ColorEdit4("Color", &s.color.r);
        track();
        ImGui::DragFloat2("Tamaño", &s.size.x, 0.02f);
        track();
        ImGui::DragFloat2("Pivote", &s.pivot.x, 0.01f, 0, 1);
        track();
        ImGui::DragInt("Orden", &s.sortingOrder, 0.1f);
        track();
        bool vis = s.visible;
        if (ImGui::Checkbox("Visible", &vis)) {
            pushUndo();
            s.visible = vis;
        }
    } else if (name == "MeshRenderer") {
        MeshRenderer& m = *w.get<MeshRenderer>(e);
        int mesh = static_cast<int>(m.mesh);
        if (ImGui::Combo("Malla", &mesh, "Cubo\0Esfera\0Plano\0Cilindro\0Cono\0Cápsula\0")) {
            pushUndo();
            m.mesh = static_cast<MeshPrimitive>(mesh);
        }
        assetCombo("Textura", AssetType::Texture, m.texture);
        ImGui::ColorEdit4("Color", &m.color.r);
        track();
        ImGui::DragFloat("Brillo", &m.shininess, 0.5f, 1.0f, 256.0f);
        track();
        bool unlit = m.unlit, vis = m.visible;
        if (ImGui::Checkbox("Sin iluminación", &unlit)) {
            pushUndo();
            m.unlit = unlit;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Visible", &vis)) {
            pushUndo();
            m.visible = vis;
        }
    } else if (name == "Light") {
        Light& l = *w.get<Light>(e);
        int type = static_cast<int>(l.type);
        if (ImGui::Combo("Tipo", &type, "Direccional\0Puntual\0")) {
            pushUndo();
            l.type = static_cast<Light::Type>(type);
        }
        ImGui::ColorEdit3("Color", &l.color.r);
        track();
        ImGui::DragFloat("Intensidad", &l.intensity, 0.02f, 0, 20);
        track();
        if (l.type == Light::Type::Point) {
            ImGui::DragFloat("Alcance", &l.range, 0.1f, 0.1f, 500);
            track();
        } else {
            ImGui::TextDisabled("Ilumina en la dirección en la que apunta (gírala).");
        }
    } else if (name == "Script") {
        Script& s = *w.get<Script>(e);
        assetCombo("Archivo", AssetType::Script, s.script);
        bool en = s.enabled;
        if (ImGui::Checkbox("Activado", &en)) {
            pushUndo();
            s.enabled = en;
        }
        if (const AssetRecord* r = project->assets().find(s.script)) {
            ImGui::SameLine();
            if (ImGui::Button("Editar código")) openScriptFile(r->path);
        } else if (ImGui::Button("Crear script nuevo")) {
            const std::string cls = identifierFrom(scene.info(e).name);
            std::string rel = "scripts/" + cls + ".lua";
            for (int i = 2; fs::exists(project->assetsDir() + "/" + rel); ++i) rel = "scripts/" + cls + std::to_string(i) + ".lua";
            fileio::writeAtomic(project->assetsDir() + "/" + rel, newScriptSource(cls), false);
            pushUndo();
            s.script = project->assets().import(rel);
            openScriptFile(rel);
        }
    }
}

// ============================================================ Código

void Editor::Impl::checkCode() {
    lua_State* L = luaL_newstate();
    const std::string chunk = "@" + codePath;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") == LUA_OK) {
        codeCheck = "Sin errores de sintaxis";
        codeErrorLine = 0;
    } else {
        codeCheck = lua_tostring(L, -1);
        const size_t colon = codeCheck.find(':', codePath.size());
        codeErrorLine = colon != std::string::npos ? std::atoi(codeCheck.c_str() + colon + 1) : 0;
    }
    lua_close(L);
}

void Editor::Impl::drawCode() {
    // Selector de archivo y acciones.
    const std::string preview = codePath.empty() ? "(elige un script)" : codePath;
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.45f, 320 * scale));
    if (ImGui::BeginCombo("##script", preview.c_str())) {
        for (const AssetRecord* r : project->assets().all())
            if (r->type == AssetType::Script && ImGui::Selectable(r->path.c_str(), r->path == codePath)) openScriptFile(r->path);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Nuevo")) ImGui::OpenPopup("nuevo script");
    if (ImGui::BeginPopup("nuevo script")) {
        ImGui::SetNextItemWidth(220 * scale);
        ImGui::InputText("Nombre", &newScriptName);
        if (ImGui::Button("Crear") && !newScriptName.empty()) {
            const std::string cls = identifierFrom(newScriptName);
            const std::string rel = "scripts/" + cls + ".lua";
            if (!fs::exists(project->assetsDir() + "/" + rel)) {
                fileio::writeAtomic(project->assetsDir() + "/" + rel, newScriptSource(cls), false);
                project->assets().import(rel);
            }
            openScriptFile(rel);
            newScriptName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (codePath.empty()) {
        ImGui::TextDisabled("Abre un script o crea uno nuevo. Para que se ejecute, añádelo a una entidad");
        ImGui::TextDisabled("con el componente Script (Inspector > Añadir componente > Script).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(codeDirty ? "Guardar *" : "Guardar")) saveCode();
    ImGui::SameLine();
    if (ImGui::Button("Comprobar")) checkCode();

    // Fila de atajos para teclados táctiles: inserta en la posición del cursor.
    static const char* snippets[] = {"self.entity:", "function", "end", "local", "if", "then", "else", "for", "do",
                                     "return", "(", ")", "{", "}", "=", "==", ".", ":", ",", "\"", "--", "dt"};
    ImGui::BeginChild("##atajos", ImVec2(0, ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4 * scale), 0,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < sizeof(snippets) / sizeof(*snippets); ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(snippets[i])) {
            std::string ins = snippets[i];
            if (ins == "function") ins = "function Nombre:onUpdate(dt)\n    \nend\n";
            else if (ins == "if") ins = "if  then\n    \nend";
            else if (ins == "for") ins = "for i = 1, 10 do\n    \nend";
            const int at = std::clamp(codeCursor, 0, static_cast<int>(code.size()));
            code.insert(static_cast<size_t>(at), ins);
            codeRequestCursor = at + static_cast<int>(ins.size());
            codeRequestFocus = true;
            codeDirty = true;
        }
    }
    ImGui::EndChild();

    // Estado: línea actual y resultado de la comprobación.
    if (!codeCheck.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, codeErrorLine ? ImVec4(0.95f, 0.45f, 0.45f, 1) : ImVec4(0.45f, 0.85f, 0.55f, 1));
        ImGui::TextWrapped("%s", codeCheck.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("Línea %d", codeCursorLine);

    if (monoFont) ImGui::PushFont(monoFont);
    if (codeRequestFocus) {
        ImGui::SetKeyboardFocusHere();
        codeRequestFocus = false;
    }
    const std::string before = code;
    ImGui::InputTextMultiline(
        "##codigo", &code, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
        [](ImGuiInputTextCallbackData* d) {
            auto* self = static_cast<Editor::Impl*>(d->UserData);
            if (self->codeRequestCursor >= 0) {
                d->CursorPos = d->SelectionStart = d->SelectionEnd = std::min(self->codeRequestCursor, d->BufTextLen);
                self->codeRequestCursor = -1;
            }
            self->codeCursor = d->CursorPos;
            self->codeCursorLine = 1 + static_cast<int>(std::count(d->Buf, d->Buf + d->CursorPos, '\n'));
            return 0;
        },
        this);
    if (ImGui::IsItemActive()) noScrollWidgetActive = true;
    if (monoFont) ImGui::PopFont();
    if (code != before) {
        codeDirty = true;
        codeCheck.clear();
    }
}

// ============================================================ Recursos

void Editor::Impl::drawAssets() {
    sectionTitle("RECURSOS");
    ImGui::SameLine();
    if (ImGui::Button("Importar")) {
        if (!platform.requestImportFile()) ImGui::OpenPopup("importar ruta");
    }
    ImGui::SameLine();
    if (ImGui::Button("Nueva escena")) {
        save();
        Scene s("nueva");
        Entity cam = s.createEntity("Cámara");
        Camera c;
        if (project->dimension() == ProjectDimension::ThreeD) {
            c.projection = Camera::Projection::Perspective;
            s.transform(cam).setPosition({0, 3, 8});
            s.transform(cam).setEulerDegrees({-15, 0, 0});
        } else {
            s.transform(cam).setPosition({0, 0, 10});
        }
        s.world().add<Camera>(cam, c);
        std::string rel = "scenes/escena.scene";
        for (int i = 2; fs::exists(project->assetsDir() + "/" + rel); ++i) rel = "scenes/escena" + std::to_string(i) + ".scene";
        const UUID id = project->saveScene(s, registry, rel);
        if (!id.isNull()) loadScene(id);
    }
    if (ImGui::BeginPopup("importar ruta")) {
        ImGui::TextUnformatted("Ruta del archivo a importar:");
        ImGui::SetNextItemWidth(360 * scale);
        ImGui::InputText("##ruta", &importPath);
        if (ImGui::Button("Importar") && !importPath.empty()) {
            importFile(importPath, "");
            importPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    Entity sel = selectedEntity();
    struct Group { AssetType type; const char* title; };
    static const Group groups[] = {{AssetType::Scene, "Escenas"},   {AssetType::Script, "Scripts"},
                                   {AssetType::Texture, "Texturas"}, {AssetType::Audio, "Audio (aún sin reproductor)"},
                                   {AssetType::Model, "Modelos (importador pendiente)"}, {AssetType::Font, "Fuentes"},
                                   {AssetType::Data, "Datos"},         {AssetType::Unknown, "Otros"}};
    for (const Group& g : groups) {
        std::vector<const AssetRecord*> items;
        for (const AssetRecord* r : project->assets().all())
            if (r->type == g.type) items.push_back(r);
        if (items.empty()) continue;
        if (!ImGui::TreeNodeEx(g.title, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (const AssetRecord* r : items) {
            ImGui::PushID(r->path.c_str());
            const bool isStart = r->id == project->startScene(), isOpen = r->id == sceneId;
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s%s", fileName(r->path).c_str(), isStart ? "  (inicial)" : "", isOpen ? "  [abierta]" : "");
            if (g.type == AssetType::Scene) {
                if (!isOpen) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Abrir")) {
                        save();
                        loadScene(r->id);
                    }
                }
                if (!isStart) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Hacer inicial")) {
                        project->setStartScene(r->id);
                        project->save();
                    }
                }
            } else if (g.type == AssetType::Script) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Editar")) openScriptFile(r->path);
                if (!sel.isNull()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Asignar")) {
                        pushUndo();
                        scene.world().add<Script>(sel, {r->id, true});
                    }
                }
            } else if (g.type == AssetType::Texture && !sel.isNull()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Asignar")) {
                    pushUndo();
                    if (auto* m = scene.world().get<MeshRenderer>(sel)) m->texture = r->id;
                    else {
                        SpriteRenderer sp = scene.world().has<SpriteRenderer>(sel) ? *scene.world().get<SpriteRenderer>(sel) : SpriteRenderer{};
                        sp.texture = r->id;
                        scene.world().add<SpriteRenderer>(sel, sp);
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (!sel.isNull()) ImGui::TextDisabled("\"Asignar\" aplica el recurso a: %s", scene.info(sel).name.c_str());
}

// ============================================================ Consola

void Editor::Impl::drawConsole() {
    sectionTitle("CONSOLA");
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Avisos", &showWarnings);
    ImGui::SameLine();
    ImGui::Checkbox("Errores", &showErrors);
    ImGui::SameLine();
    if (ImGui::Button("Limpiar")) logs.clear();
    if (runtime) {
        for (const ScriptError& err : runtime->scripts().errors()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
            ImGui::TextWrapped("%s · %s() en '%s': %s", err.script.c_str(), err.function.c_str(), err.entity.c_str(),
                               err.message.c_str());
            ImGui::PopStyleColor();
            ImGui::PushID(&err);
            if (ImGui::SmallButton("Abrir el script")) openScriptFile(err.script);
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##log");
    if (monoFont) ImGui::PushFont(monoFont);
    for (const LogMessage& m : logs) {
        if ((m.level == LogLevel::Info && !showInfo) || (m.level == LogLevel::Warning && !showWarnings) ||
            (m.level == LogLevel::Error && !showErrors))
            continue;
        const ImVec4 col = m.level == LogLevel::Error ? ImVec4(0.95f, 0.45f, 0.45f, 1)
                         : m.level == LogLevel::Warning ? ImVec4(0.95f, 0.78f, 0.35f, 1)
                                                        : ImVec4(0.80f, 0.84f, 0.90f, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrapped("%7.2f [%s] %s", m.seconds, m.category.c_str(), m.text.c_str());
        ImGui::PopStyleColor();
    }
    if (scrollConsole) {
        ImGui::SetScrollHereY(1.0f);
        scrollConsole = false;
    }
    if (monoFont) ImGui::PopFont();
    ImGui::EndChild();
}

} // namespace titan
