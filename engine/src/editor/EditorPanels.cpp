// Paneles y distribución del editor. Cada control modifica directamente los datos de la escena
// (titan::Scene) y registra la operación para poder deshacerla.
//
//  ┌ Menú ─────────────────────────────────────────────────────────────┐
//  │ Barra: guardar · Q W E R · imán · ▶ ❚❚ ■                          │
//  ├ Documentos (Escena | script.lua) ───────────────┬ Outliner ───────┤
//  │ Vista de escena (con su barra flotante)         │                 │
//  │                                                 ├ Detalles ───────┤
//  ├ Contenido | Consola ────────────────────────────┤                 │
//  └ Barra de estado ────────────────────────────────┴─────────────────┘
// En teléfonos: vista arriba y una hoja inferior con pestañas.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

#include "EditorImpl.h"
#include "titan/core/Particles.h"
#include "titan/core/Voxel.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "lauxlib.h"
#include "lua.h"
#include "titan/core/FileIO.h"
#include "titan/core/Templates.h"
#include "titan/core/Version.h"

namespace fs = std::filesystem;

namespace titan {

namespace {

const ImVec4 kBlue(0.0f, 0.44f, 0.88f, 1);
const ImVec4 kPlayGreen(0.30f, 0.75f, 0.35f, 1);
const ImVec4 kStopRed(0.86f, 0.30f, 0.30f, 1);
const ImVec4 kDim(0.55f, 0.55f, 0.55f, 1);
const ImU32 kAxisColor[3] = {IM_COL32(220, 60, 60, 255), IM_COL32(110, 190, 40, 255), IM_COL32(50, 110, 230, 255)};

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

bool beginPanel(const char* id, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extra = 0) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    return ImGui::Begin(id, nullptr, kPanelFlags | extra);
}

// Cabecera de panel al estilo de los editores profesionales: título en una franja oscura.
void panelHeader(const char* title, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 8 * scale;
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(ImVec2(p.x - 4 * scale, p.y), ImVec2(p.x + w + 4 * scale, p.y + h), IM_COL32(26, 26, 26, 255));
    dl->AddRectFilled(ImVec2(p.x - 4 * scale, p.y + h - 2 * scale), ImVec2(p.x + 60 * scale, p.y + h), IM_COL32(0, 112, 224, 255));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 2 * scale, p.y + 4 * scale));
    ImGui::TextUnformatted(title);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4 * scale));
}

enum class Icon { Save, Select, Move, Rotate, Scale, Magnet, Play, Pause, Stop, Undo, Redo, Plus, Folder };

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float r, ImU32 col) {
    const float t = std::max(1.5f, r * 0.16f);
    switch (icon) {
    case Icon::Play: dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r * 0.75f), ImVec2(c.x - r * 0.6f, c.y + r * 0.75f), ImVec2(c.x + r * 0.75f, c.y), col); break;
    case Icon::Pause:
        dl->AddRectFilled(ImVec2(c.x - r * 0.6f, c.y - r * 0.7f), ImVec2(c.x - r * 0.15f, c.y + r * 0.7f), col);
        dl->AddRectFilled(ImVec2(c.x + r * 0.15f, c.y - r * 0.7f), ImVec2(c.x + r * 0.6f, c.y + r * 0.7f), col);
        break;
    case Icon::Stop: dl->AddRectFilled(ImVec2(c.x - r * 0.6f, c.y - r * 0.6f), ImVec2(c.x + r * 0.6f, c.y + r * 0.6f), col, r * 0.1f); break;
    case Icon::Save:
        dl->AddRect(ImVec2(c.x - r * 0.7f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), col, r * 0.12f, 0, t);
        dl->AddRectFilled(ImVec2(c.x - r * 0.4f, c.y - r * 0.7f), ImVec2(c.x + r * 0.3f, c.y - r * 0.2f), col);
        dl->AddRectFilled(ImVec2(c.x - r * 0.45f, c.y + r * 0.15f), ImVec2(c.x + r * 0.45f, c.y + r * 0.55f), col);
        break;
    case Icon::Select:
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.8f), ImVec2(c.x - r * 0.55f, c.y + r * 0.5f), ImVec2(c.x + r * 0.55f, c.y + r * 0.05f), col);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r * 0.35f, c.y + r * 0.8f), col, t * 1.3f);
        break;
    case Icon::Move:
        dl->AddLine(ImVec2(c.x - r * 0.8f, c.y), ImVec2(c.x + r * 0.8f, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.8f), ImVec2(c.x, c.y + r * 0.8f), col, t);
        for (int i = 0; i < 4; ++i) {
            const float a = i * 1.5708f, ca = std::cos(a), sa = std::sin(a);
            const ImVec2 tip(c.x + ca * r * 0.95f, c.y + sa * r * 0.95f);
            dl->AddTriangleFilled(tip, ImVec2(c.x + ca * r * 0.55f - sa * r * 0.28f, c.y + sa * r * 0.55f + ca * r * 0.28f),
                                  ImVec2(c.x + ca * r * 0.55f + sa * r * 0.28f, c.y + sa * r * 0.55f - ca * r * 0.28f), col);
        }
        break;
    case Icon::Rotate:
        dl->PathArcTo(c, r * 0.65f, 0.3f, 5.6f, 20);
        dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.95f, c.y + r * 0.05f), ImVec2(c.x + r * 0.35f, c.y + r * 0.05f), ImVec2(c.x + r * 0.65f, c.y + r * 0.5f), col);
        break;
    case Icon::Scale:
        dl->AddRect(ImVec2(c.x - r * 0.75f, c.y - r * 0.1f), ImVec2(c.x + r * 0.1f, c.y + r * 0.75f), col, 0, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r * 0.75f, c.y - r * 0.75f), col, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.2f, c.y - r * 0.75f), ImVec2(c.x + r * 0.75f, c.y - r * 0.2f), col);
        break;
    case Icon::Magnet:
        dl->PathArcTo(ImVec2(c.x, c.y + r * 0.05f), r * 0.55f, 0.0f, 3.1416f, 14);
        dl->PathStroke(col, 0, t * 1.6f);
        dl->AddLine(ImVec2(c.x - r * 0.55f, c.y + r * 0.05f), ImVec2(c.x - r * 0.55f, c.y - r * 0.7f), col, t * 1.6f);
        dl->AddLine(ImVec2(c.x + r * 0.55f, c.y + r * 0.05f), ImVec2(c.x + r * 0.55f, c.y - r * 0.7f), col, t * 1.6f);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        const float s = icon == Icon::Undo ? 1.0f : -1.0f;
        dl->PathArcTo(c, r * 0.55f, icon == Icon::Undo ? 3.6f : -0.46f, icon == Icon::Undo ? 6.9f : 2.75f, 16);
        dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x - s * r * 0.85f, c.y - r * 0.2f), ImVec2(c.x - s * r * 0.25f, c.y - r * 0.55f), ImVec2(c.x - s * r * 0.3f, c.y + r * 0.1f), col);
        break;
    }
    case Icon::Plus:
        dl->AddLine(ImVec2(c.x - r * 0.6f, c.y), ImVec2(c.x + r * 0.6f, c.y), col, t * 1.4f);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.6f), ImVec2(c.x, c.y + r * 0.6f), col, t * 1.4f);
        break;
    case Icon::Folder:
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.45f), ImVec2(c.x + r * 0.8f, c.y + r * 0.6f), col, r * 0.1f);
        dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.65f), ImVec2(c.x - r * 0.1f, c.y - r * 0.35f), col, r * 0.1f);
        break;
    }
}

bool iconButton(const char* id, Icon icon, bool active, const char* tooltip, ImU32 color = IM_COL32(205, 205, 205, 255)) {
    const float sz = ImGui::GetFrameHeight();
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.88f, 0.55f));
    const bool pressed = ImGui::Button(id, ImVec2(sz * 1.15f, sz));
    if (active) ImGui::PopStyleColor();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), sz * 0.36f, color);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void vsep() {
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + h * 0.15f), ImVec2(p.x, p.y + h * 0.85f), IM_COL32(70, 70, 70, 255));
    ImGui::Dummy(ImVec2(1, h));
    ImGui::SameLine();
}

std::string fileName(const std::string& path) {
    const size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

std::string folderOf(const std::string& path) {
    const size_t s = path.find_last_of('/');
    return s == std::string::npos ? "" : path.substr(0, s);
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

} // namespace

// ============================================================ Distribución general

void Editor::Impl::drawUI() {
    vpVisible = false;
    floatingRects.clear();
    if (!project || showBrowser) {
        drawBrowser();
        return;
    }
    const float W = float(fbWidth), H = float(fbHeight);
    const ImGuiStyle& st = ImGui::GetStyle();
    drawMenuBar();
    if (showMachineEditor) drawMachineEditor();
    if (soundCheckRunning || showDiagnostics) drawDiagnostics();
    if (showMixer) drawMixer();
    if (showMatGraph) drawMaterialGraph();
    const float menuH = ImGui::GetFrameHeight();
    const float rowH = ImGui::GetFrameHeight() + st.WindowPadding.y * 2;
    drawToolbar(ImVec2(0, menuH), ImVec2(W, rowH));
    const float statusH = ImGui::GetFrameHeight() + 4 * scale;
    const float top = menuH + rowH;
    const bool wide = W > H * 1.15f && W > 760 * scale;

    if (wide) {
        const float right = std::max(300 * scale, W * 0.26f);
        const float mainW = W - right;
        const float avail = H - top - statusH;
        // Pantallas bajas (teléfono en horizontal): el panel inferior empieza plegado y la columna se reparte mejor.
        const bool shortScreen = avail < 520 * scale;
        static bool layoutDecided = false;
        if (!layoutDecided) {
            showBottom = !shortScreen;
            layoutDecided = true;
        }
        const float split = shortScreen ? 0.45f : 0.40f;
        if (beginPanel("##outliner", ImVec2(mainW, top), ImVec2(right, avail * split))) drawOutliner();
        ImGui::End();
        if (beginPanel("##detalles", ImVec2(mainW, top + avail * split), ImVec2(right, avail * (1 - split)))) drawDetails();
        ImGui::End();
        const float bottomH = showBottom ? avail * (shortScreen ? 0.45f : 0.30f) : 0.0f;
        if (beginPanel("##documentos", ImVec2(0, top), ImVec2(mainW, rowH), ImGuiWindowFlags_NoScrollbar)) drawDocumentTabs();
        ImGui::End();
        const ImVec2 docPos(0, top + rowH), docSize(mainW, avail - rowH - bottomH);
        if (codeTabActive) {
            if (beginPanel("##codigo", docPos, docSize)) drawCode();
            ImGui::End();
        } else {
            vpVisible = true;
            vpMin = docPos;
            vpMax = ImVec2(docPos.x + docSize.x, docPos.y + docSize.y);
        }
        if (showBottom) {
            if (beginPanel("##inferior", ImVec2(0, top + avail - bottomH), ImVec2(mainW, bottomH))) {
                if (ImGui::BeginTabBar("##inf")) {
                    if (ImGui::BeginTabItem("Contenido", nullptr, bottomIsConsole ? 0 : ImGuiTabItemFlags_None)) {
                        bottomIsConsole = false;
                        drawContent();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Registro de salida")) {
                        bottomIsConsole = true;
                        drawConsole();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
        drawStatusBar(ImVec2(0, H - statusH), ImVec2(W, statusH));
    } else {
        // Teléfono: documentos, vista y hoja inferior con pestañas.
        if (beginPanel("##documentos", ImVec2(0, top), ImVec2(W, rowH), ImGuiWindowFlags_NoScrollbar)) drawDocumentTabs();
        ImGui::End();
        const float y0 = top + rowH, avail = H - y0;
        if (codeTabActive) {
            if (beginPanel("##codigo", ImVec2(0, y0), ImVec2(W, avail))) drawCode();
            ImGui::End();
        } else {
            const float vh = avail * 0.46f;
            vpVisible = true;
            vpMin = ImVec2(0, y0);
            vpMax = ImVec2(W, y0 + vh);
            if (beginPanel("##hoja", ImVec2(0, y0 + vh), ImVec2(W, avail - vh))) {
                struct Tab { const char* label; Panel p; };
                static const Tab tabs[] = {{"Outliner", Panel::Outliner}, {"Detalles", Panel::Details},
                                           {"Contenido", Panel::Content}, {"Registro", Panel::Console}};
                if (ImGui::BeginTabBar("##hojatabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    for (const Tab& t : tabs) {
                        static Panel lastPanel = Panel::Outliner;
                        const bool force = panel != lastPanel && panel == t.p;
                        if (ImGui::BeginTabItem(t.label, nullptr, force ? ImGuiTabItemFlags_SetSelected : 0)) {
                            if (!(panel != lastPanel && panel != t.p)) panel = t.p;
                            lastPanel = panel;
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                }
                ImGui::BeginChild("##hojacontenido");
                switch (panel) {
                case Panel::Outliner: drawOutliner(); break;
                case Panel::Details: drawDetails(); break;
                case Panel::Content: drawContent(); break;
                case Panel::Console: drawConsole(); break;
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }
    }

    if (vpVisible) {
        updateEditorCamera((vpMax.x - vpMin.x) / std::max(1.0f, vpMax.y - vpMin.y));
        drawViewportBar();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (!runtime) {
            drawUIOutlines(dl);
            if (showGizmos && !medit.active) drawGizmo(dl);
            meshEditOverlay(dl);
            drawAxisIndicator(dl);
        } else {
            dl->AddRect(vpMin, vpMax, IM_COL32(77, 190, 90, 255), 0, 0, 2 * scale);
        }
    }
}

// ============================================================ Menú y barras

void Editor::Impl::drawCreateMenu() {
    static const char* basic[] = {"Vacío", "Cámara", "Cámara en tercera persona", "Sprite"};
    static const char* shapes[] = {"Cubo", "Esfera", "Plano", "Cilindro", "Cono", "Cápsula"};
    static const char* lights[] = {"Luz direccional", "Luz puntual", "Foco"};
    for (const char* k : basic)
        if (ImGui::MenuItem(k)) createEntityOfKind(k);
    if (ImGui::BeginMenu("Formas")) {
        for (const char* k : shapes)
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Luces")) {
        for (const char* k : lights)
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Física")) {
        if (ImGui::MenuItem("Cubo con física")) createEntityOfKind("Cubo con física");
        if (ImGui::MenuItem("Esfera con física")) createEntityOfKind("Esfera con física");
        if (ImGui::MenuItem("Zona trigger")) createEntityOfKind("Zona trigger");
        if (ImGui::MenuItem("Personaje")) createEntityOfKind("Personaje");
        if (ImGui::MenuItem("Enemigo que persigue")) createEntityOfKind("Enemigo");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Efectos")) {
        for (const char* k : {"Fuego", "Humo", "Chispas", "Explosión", "Magia", "Lluvia", "Nieve"})
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Interfaz")) {
        for (const char* k : {"Texto", "Imagen", "Panel", "Botón", "Barra de vida", "Joystick"})
            if (ImGui::MenuItem(k)) createEntityOfKind(k);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Modelo editable")) {
        for (const char* k : {"Cubo", "Esfera", "Cilindro", "Cono", "Plano", "Cabeza (esfera densa)"})
            if (ImGui::MenuItem(k)) createEditableModel(k);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Terreno")) createEntityOfKind("Terreno");
    if (ImGui::MenuItem("Mundo de bloques")) createEntityOfKind("Mundo de bloques");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Mundo infinito de bloques con biomas, cuevas y árboles; se genera al pulsar Jugar alrededor de la cámara");
    if (ImGui::MenuItem("Cielo y niebla")) createEntityOfKind("Cielo y niebla");
}

void Editor::Impl::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Guardar todo", "Ctrl+S")) save();
        if (ImGui::MenuItem("Proyectos...")) {
            save();
            showBrowser = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exportar paquete del juego")) exportPackage();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editar")) {
        if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, !undoStack.empty() && !runtime)) undo();
        if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, !redoStack.empty() && !runtime)) redo();
        ImGui::Separator();
        const bool sel = !selectedEntity().isNull();
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, sel)) duplicateSelected();
        if (ImGui::MenuItem("Eliminar", "Supr", false, sel)) deleteSelected();
        if (ImGui::MenuItem("Enfocar selección", "F", false, sel && !runtime)) focusSelected();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Crear")) {
        drawCreateMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ventana")) {
        ImGui::MenuItem("Panel inferior", nullptr, &showBottom);
        ImGui::MenuItem("Rejilla", nullptr, &showGrid);
        ImGui::MenuItem("Gizmos", nullptr, &showGizmos);
        ImGui::MenuItem("Interfaz del juego", nullptr, &showUI);
        ImGui::MenuItem("Mezclador de audio", nullptr, &showMixer);
        if (ImGui::BeginMenu("Tamaño de la interfaz")) {
            for (float z : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
                const std::string label = std::to_string(int(z * 100)) + " %";
                if (ImGui::MenuItem(label.c_str(), z == 1.25f ? "Ctrl+0" : nullptr, std::fabs(uiZoom - z) < 0.01f)) setUiZoom(z);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Ctrl + y Ctrl - para ajustar");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Calidad gráfica")) {
            const QualityLevel rec = recommendedQuality(renderer.caps());
            for (QualityLevel l : {QualityLevel::VeryLow, QualityLevel::Low, QualityLevel::Medium, QualityLevel::High,
                                   QualityLevel::VeryHigh}) {
                std::string label = qualityName(l);
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                if (l == rec) label += "  (recomendada)";
                if (ImGui::MenuItem(label.c_str(), nullptr, renderer.quality().level == l)) {
                    editorQuality = QualitySettings::preset(l);
                    renderer.setQuality(editorQuality);
                }
            }
            ImGui::Separator();
            const DeviceCaps& c = renderer.caps();
            ImGui::TextDisabled("%s", c.renderer.c_str());
            ImGui::TextDisabled("%s · textura máx. %d · %d MB", c.version.c_str(), c.maxTextureSize, c.memoryMB);
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Estadísticas de render", nullptr, &showStats);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        ImGui::TextDisabled("%s %s", kEngineName, kEngineVersion);
        ImGui::TextDisabled("Q seleccionar · W mover · E rotar · R escalar");
        ImGui::TextDisabled("Ctrl+P jugar/detener · Ctrl+S guardar");
        ImGui::Separator();
        if (ImGui::MenuItem("Prueba de sonido", nullptr, false, !runtime)) startSoundCheck();
        if (ImGui::MenuItem("Diagnóstico del dispositivo")) showDiagnostics = true;
        ImGui::EndMenu();
    }
    // Nombre del proyecto a la derecha.
    const std::string title = project->name() + (dirty ? " *" : "");
    const float w = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - 12 * scale);
    ImGui::TextDisabled("%s", title.c_str());
    ImGui::EndMainMenuBar();
}

void Editor::Impl::drawToolbar(const ImVec2& pos, const ImVec2& size) {
    if (!beginPanel("##barra", pos, size, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return;
    }
    auto playControls = [&]() {
        if (!runtime) {
            if (iconButton("##jugar", Icon::Play, false, "Jugar (Ctrl+P)", ImGui::GetColorU32(kPlayGreen))) play();
        } else if (iconButton("##reanudar", Icon::Play, !playPaused, "Reanudar", ImGui::GetColorU32(kPlayGreen))) {
            playPaused = false;
            runtime->paused = false;
            runtime->audio().setPaused(false);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!runtime);
        if (iconButton("##pausa", Icon::Pause, playPaused, "Pausa")) {
            playPaused = !playPaused;
            runtime->paused = playPaused;
            runtime->audio().setPaused(playPaused);
        }
        ImGui::SameLine();
        if (iconButton("##detener", Icon::Stop, false, "Detener", ImGui::GetColorU32(runtime ? kStopRed : kDim))) stop();
        ImGui::EndDisabled();
    };
    // En teléfonos el botón de jugar va primero para que siempre esté a la vista.
    const bool narrow = size.x < 700 * scale;
    if (narrow) {
        playControls();
        vsep();
    }
    if (iconButton("##guardar", Icon::Save, false, "Guardar todo (Ctrl+S)")) save();
    ImGui::SameLine();
    ImGui::BeginDisabled(undoStack.empty() || runtime);
    if (iconButton("##deshacer", Icon::Undo, false, "Deshacer (Ctrl+Z)")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redoStack.empty() || runtime);
    if (iconButton("##rehacer", Icon::Redo, false, "Rehacer (Ctrl+Y)")) redo();
    ImGui::EndDisabled();
    vsep();
    if (iconButton("##sel", Icon::Select, tool == Tool::Select, "Seleccionar (Q)")) tool = Tool::Select;
    ImGui::SameLine();
    if (iconButton("##mover", Icon::Move, tool == Tool::Translate, "Mover (W)")) tool = Tool::Translate;
    ImGui::SameLine();
    if (iconButton("##rotar", Icon::Rotate, tool == Tool::Rotate, "Rotar (E)")) tool = Tool::Rotate;
    ImGui::SameLine();
    if (iconButton("##escalar", Icon::Scale, tool == Tool::Scale, "Escalar (R)")) tool = Tool::Scale;
    vsep();
    if (iconButton("##iman", Icon::Magnet, snap, "Ajustar a la rejilla")) snap = !snap;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 2.2f);
    float* snapValue = tool == Tool::Rotate ? &snapRotate : tool == Tool::Scale ? &snapScale : &snapMove;
    ImGui::DragFloat("##snapv", snapValue, 0.05f, 0.01f, 90.0f, tool == Tool::Rotate ? "%.0f°" : "%.2f");
    if (ImGui::IsItemActive()) noScrollWidgetActive = true;
    vsep();
    if (iconButton("##crear", Icon::Plus, false, "Añadir a la escena")) ImGui::OpenPopup("crear");
    if (ImGui::BeginPopup("crear")) {
        drawCreateMenu();
        ImGui::EndPopup();
    }

    if (!narrow) {
        // Controles de juego, centrados.
        ImGui::SameLine();
        const float sz = ImGui::GetFrameHeight() * 1.15f;
        const float groupW = sz * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
        const float after = ImGui::GetCursorPosX() + sz * 0.5f; // nunca encima de las herramientas
        ImGui::SetCursorPosX(std::max(after, (size.x - groupW) * 0.5f));
        playControls();
    }
    if (statusTime > 0 && size.x > 900 * scale) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("  %s", status.c_str());
    }
    ImGui::End();
}

void Editor::Impl::drawDocumentTabs() {
    const AssetRecord* r = project->assets().find(sceneId);
    const std::string sceneLabel = (r ? fileName(r->path) : std::string("escena")) + (dirty ? " *" : "");
    auto tab = [&](const std::string& label, bool active) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        const bool pressed = ImGui::Button(label.c_str());
        if (active) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, b.y - 2 * scale), b, IM_COL32(0, 112, 224, 255));
            ImGui::PopStyleColor();
        }
        return pressed;
    };
    if (tab(sceneLabel + "##docescena", !codeTabActive)) codeTabActive = false;
    if (!codePath.empty()) {
        ImGui::SameLine();
        if (tab(fileName(codePath) + (codeDirty ? " *" : "") + "##doccodigo", codeTabActive)) codeTabActive = true;
        ImGui::SameLine(0, 0);
        if (ImGui::SmallButton("x##cerrarcodigo")) {
            saveCode();
            codePath.clear();
            code.clear();
            codeTabActive = false;
        }
    }
}

void Editor::Impl::drawViewportBar() {
    // Jugando, la barra va a la derecha para no tapar la interfaz del juego (arriba a la izquierda).
    const float barW = vpBarMax.x > vpBarMin.x ? vpBarMax.x - vpBarMin.x : 0.0f;
    const float barX = runtime && barW > 0 ? vpMax.x - barW - 6 * scale : vpMin.x + 6 * scale;
    ImGui::SetNextWindowPos(ImVec2(barX, vpMin.y + 6 * scale));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##barravista", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
    if (ImGui::Button("=##vistamenu")) ImGui::OpenPopup("vistamenu");
    if (ImGui::BeginPopup("vistamenu")) {
        ImGui::MenuItem("Rejilla", nullptr, &showGrid);
        ImGui::MenuItem("Gizmos", nullptr, &showGizmos);
        ImGui::MenuItem("Estadísticas de render", nullptr, &showStats);
        if (runtime) ImGui::MenuItem("Estadísticas del juego", nullptr, &runtime->showStats);
        ImGui::EndPopup();
    }
    if (threeD() && !runtime) {
        ImGui::SameLine();
        static const char* angles[] = {"Perspectiva", "Superior", "Frontal", "Derecha"};
        if (ImGui::Button((std::string(angles[int(viewAngle)]) + "##angulo").c_str())) ImGui::OpenPopup("angulo");
        if (ImGui::BeginPopup("angulo")) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::MenuItem(angles[i], nullptr, int(viewAngle) == i)) viewAngle = ViewAngle(i);
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(viewUnlit ? "Sin iluminación##luz" : "Iluminado##luz")) ImGui::OpenPopup("luz");
    if (ImGui::BeginPopup("luz")) {
        if (ImGui::MenuItem("Iluminado", nullptr, !viewUnlit)) viewUnlit = false;
        if (ImGui::MenuItem("Sin iluminación", nullptr, viewUnlit)) viewUnlit = true;
        ImGui::EndPopup();
    }
    if (runtime) {
        ImGui::SameLine();
        ImGui::TextColored(kPlayGreen, playPaused ? "  EN PAUSA" : "  JUGANDO");
    }
    ImGui::PopStyleColor();
    vpBarMin = ImGui::GetWindowPos();
    vpBarMax = ImVec2(vpBarMin.x + ImGui::GetWindowSize().x, vpBarMin.y + ImGui::GetWindowSize().y);
    ImGui::End();

    if (showStats && !runtime) {
        const RenderStats& rs = renderer.stats();
        const CullStats& cs = sceneRenderer.cullStats();
        char text[256];
        std::snprintf(text, sizeof(text),
                      "%.0f FPS · %d draw calls · %d/%d mallas visibles · %d triángulos · %zu entidades",
                      ImGui::GetIO().Framerate, rs.drawCalls, cs.meshesVisible, cs.meshes, rs.triangles, scene.entityCount());
        const ImVec2 ts = ImGui::CalcTextSize(text);
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(vpMax.x - ts.x - 10 * scale, vpMin.y + 10 * scale),
                                                IM_COL32(230, 230, 230, 220), text);
    }
}

void Editor::Impl::drawStatusBar(const ImVec2& pos, const ImVec2& size) {
    if (!beginPanel("##estado", pos, size, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    if (ImGui::SmallButton("Contenido")) {
        showBottom = !(showBottom && !bottomIsConsole);
        bottomIsConsole = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Registro de salida")) {
        showBottom = !(showBottom && bottomIsConsole);
        bottomIsConsole = true;
    }
    const char* saved = dirty || codeDirty ? "Cambios sin guardar" : "Todo guardado";
    const float savedX = size.x - ImGui::CalcTextSize(saved).x - 14 * scale;
    ImGui::SameLine();
    if (!logs.empty()) {
        // Último mensaje del registro, recortado para no pisar el estado de guardado.
        const LogMessage& m = logs.back();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(p, ImVec2(pos.x + savedX - 10 * scale, p.y + size.y), true);
        ImGui::PushStyleColor(ImGuiCol_Text, m.level == LogLevel::Error ? ImVec4(0.95f, 0.45f, 0.45f, 1)
                                             : m.level == LogLevel::Warning ? ImVec4(0.95f, 0.78f, 0.35f, 1)
                                                                            : kDim);
        ImGui::TextUnformatted(("  " + m.text).c_str());
        ImGui::PopStyleColor();
        ImGui::PopClipRect();
    }
    ImGui::SameLine(savedX);
    ImGui::TextDisabled("%s", saved);
    ImGui::End();
}

// ============================================================ Navegador de proyectos


// ============================================================ Outliner

const char* Editor::Impl::typeLabel(Entity e) {
    World& w = scene.world();
    if (w.has<CameraFollow>(e)) return "Cámara 3ª persona";
    if (w.has<Camera>(e)) return "Cámara";
    if (w.has<ParticleSystem>(e)) return "Partículas";
    if (w.has<Terrain>(e)) return "Terreno";
    if (w.has<VoxelTerrain>(e)) return "Mundo de bloques";
    if (w.has<NavAgent>(e)) return "Agente IA";
    if (w.has<CharacterController>(e)) return "Personaje";
    if (w.has<UIJoystick>(e)) return "Joystick";
    if (w.has<UIButton>(e)) return "Botón";
    if (w.has<UIText>(e)) return "Texto UI";
    if (w.has<UIImage>(e)) return "Imagen UI";
    if (w.has<UIRect>(e)) return "UI";
    if (const Light* l = w.get<Light>(e))
        return l->type == Light::Type::Directional ? "Luz direccional" : l->type == Light::Type::Spot ? "Foco" : "Luz puntual";
    if (w.has<Environment>(e)) return "Cielo y niebla";
    if (const MeshRenderer* m = w.get<MeshRenderer>(e)) return m->model.isNull() ? "Malla" : "Modelo";
    if (w.has<SpriteRenderer>(e)) return "Sprite";
    if (w.has<Script>(e)) return "Script";
    return scene.children(e).empty() ? "Vacío" : "Carpeta";
}

void Editor::Impl::drawOutliner() {
    panelHeader("OUTLINER", scale);
    if (iconButton("##outcrear", Icon::Plus, false, "Añadir")) ImGui::OpenPopup("crearout");
    if (ImGui::BeginPopup("crearout")) {
        drawCreateMenu();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##buscar", "Buscar...", &outlinerFilter);

    const float footer = ImGui::GetTextLineHeightWithSpacing() + 4 * scale;
    if (ImGui::BeginTable("##tablaout", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                               ImGuiTableFlags_Resizable,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##vis", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 0.9f);
        ImGui::TableSetupColumn("Nombre", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        if (outlinerFilter.empty()) {
            for (Entity r : scene.roots()) drawOutlinerNode(r);
        } else {
            std::string f = outlinerFilter;
            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            scene.traverse([&](Entity e, int) {
                std::string n = scene.info(e).name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(f) == std::string::npos) return;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(static_cast<int>(scene.info(e).id.value));
                if (ImGui::Selectable(scene.info(e).name.c_str(), scene.info(e).id == selected, ImGuiSelectableFlags_SpanAllColumns))
                    selected = scene.info(e).id;
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", typeLabel(e));
                ImGui::PopID();
            });
        }
        ImGui::EndTable();
    }
    // Soltar en el vacío del panel: sacar a la raíz.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##raiz"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("TITAN_ENTITY")) {
            Entity child = scene.findById(UUID{*static_cast<const uint64_t*>(pl->Data)});
            if (!child.isNull() && !scene.parent(child).isNull()) {
                pushUndo();
                scene.setParent(child, {}, true);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::TextDisabled("%zu entidades%s", scene.entityCount(), selectedEntity().isNull() ? "" : " (1 seleccionada)");
}

void Editor::Impl::drawOutlinerNode(Entity e) {
    EntityInfo& info = scene.info(e);
    const auto kids = scene.children(e);
    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(info.id.value ^ (info.id.value >> 32)));

    // Visibilidad (activa/inactiva), como el "ojo" de los outliners profesionales.
    ImGui::TableSetColumnIndex(0);
    {
        const float s = ImGui::GetFrameHeight() * 0.8f;
        if (ImGui::InvisibleButton("##ojo", ImVec2(s, s))) {
            pushUndo();
            info.active = !info.active;
        }
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = info.active ? IM_COL32(200, 200, 200, 255) : IM_COL32(90, 90, 90, 255);
        dl->AddEllipse(c, ImVec2(s * 0.42f, s * 0.24f), col, 0, 20, 1.5f);
        if (info.active) dl->AddCircleFilled(c, s * 0.12f, col);
        else dl->AddLine(ImVec2(a.x + s * 0.1f, b.y - s * 0.1f), ImVec2(b.x - s * 0.1f, a.y + s * 0.1f), col, 1.5f);
    }

    ImGui::TableSetColumnIndex(1);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
    if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (info.id == selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!info.active) ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    const bool open = ImGui::TreeNodeEx("##nodo", flags, "%s", info.name.c_str());
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
    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("%s", typeLabel(e));
    ImGui::PopID();
    if (open && !kids.empty()) {
        for (Entity c : kids) drawOutlinerNode(c);
        ImGui::TreePop();
    }
}

// ============================================================ Detalles

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

namespace {
// Fila "Etiqueta  [X][Y][Z]  ↺" con una franja de color por eje.
bool vec3Row(const char* label, float* v, float speed, const float* resetTo, float scale, Editor::Impl& ed) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    const float reset = ImGui::GetFrameHeight();
    const float w = (ImGui::GetContentRegionAvail().x - reset - ImGui::GetStyle().ItemSpacing.x * 3) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(w);
        changed |= ImGui::DragFloat("##v", &v[i], speed, 0, 0, w < 80 * scale ? "%.2f" : "%.3f");
        ed.track();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + 3 * scale, b.y), kAxisColor[i], 2 * scale,
                                                  ImDrawFlags_RoundCornersLeft);
        ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::Button("##reset", ImVec2(reset, reset))) {
        ed.pushUndo();
        for (int i = 0; i < 3; ++i) v[i] = resetTo[i];
        changed = true;
    }
    {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        drawIcon(ImGui::GetWindowDrawList(), Icon::Undo, c, reset * 0.3f, IM_COL32(170, 170, 170, 255));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Restablecer");
    }
    ImGui::PopID();
    return changed;
}

bool beginProps(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.32f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.68f);
    return true;
}

} // namespace

// Etiqueta de una fila de propiedades (tabla de dos columnas del panel Detalles). También la usan
// los otros archivos del editor (EditorImpl.h).
void propLabel(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
}

void Editor::Impl::drawDetails() {
    if (medit.active && !runtime) {
        drawMeshEditPanel();
        return;
    }
    panelHeader("DETALLES", scale);
    Entity e = selectedEntity();
    if (e.isNull()) {
        ImGui::TextDisabled("Selecciona algo en la vista o en el Outliner.");
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
    const float addW = ImGui::CalcTextSize("+ Añadir").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##nombre", &info.name);
    track();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
    if (ImGui::Button("+ Añadir")) ImGui::OpenPopup("añadir");
    ImGui::PopStyleColor();
    if (ImGui::BeginPopup("añadir")) {
        for (const auto& type : registry.types()) {
            if (type.name == "Transform" || type.has(scene, e)) continue;
            if (ImGui::MenuItem(type.name.c_str())) {
                pushUndo();
                LoadReport ignored;
                const Json empty = Json::object();
                FieldReader reader(empty, type.name, ignored);
                type.load(scene, e, reader);
                if (type.name == "Collider") fitCollider(e);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("%s  ·  id %s", typeLabel(e), info.id.toString().c_str());

    if (!scene.world().has<UIRect>(e) && ImGui::CollapsingHeader("Transformación", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform& t = scene.transform(e);
        Vec3 p = t.position(), s = t.scale();
        if (inspectorEulerFor != info.id || !ImGui::IsAnyItemActive()) {
            inspectorEuler = t.eulerDegrees(); // se recalcula salvo mientras se arrastra (evita saltos)
            inspectorEulerFor = info.id;
        }
        static const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
        if (beginProps("##tr")) {
            if (vec3Row("Ubicación", &p.x, 0.05f, zero, scale, *this)) t.setPosition(p);
            if (vec3Row("Rotación", &inspectorEuler.x, 0.5f, zero, scale, *this)) t.setEulerDegrees(inspectorEuler);
            if (vec3Row("Escala", &s.x, 0.02f, one, scale, *this)) t.setScale(s);
            ImGui::EndTable();
        }
    }

    for (const auto& type : registry.types()) {
        if (type.name == "Transform" || !type.has(scene, e)) continue;
        bool keep = true;
        if (ImGui::CollapsingHeader(type.name.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(type.name.c_str());
            if (beginProps("##props")) {
                drawComponentFields(type.name, e);
                ImGui::EndTable();
            }
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
    if (ImGui::Button("Guardar como prefab", ImVec2(-1, 0))) savePrefab(e);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Guarda este objeto, sus hijos y componentes en prefabs/ para reutilizarlo");
    ImGui::PopID();
}

void Editor::Impl::drawComponentFields(const std::string& name, Entity e) {
    World& w = scene.world();
    auto check = [&](const char* label, bool& value) {
        propLabel(label);
        bool v = value;
        if (ImGui::Checkbox((std::string("##") + label).c_str(), &v)) {
            pushUndo();
            value = v;
        }
    };
    auto dragF = [&](const char* label, float& v, float speed, float lo, float hi, const char* fmt = "%.3f") {
        propLabel(label);
        ImGui::DragFloat((std::string("##") + label).c_str(), &v, speed, lo, hi, fmt);
        track();
    };
    auto color = [&](const char* label, Color& c, bool alpha = true) {
        propLabel(label);
        if (alpha) ImGui::ColorEdit4((std::string("##") + label).c_str(), &c.r);
        else ImGui::ColorEdit3((std::string("##") + label).c_str(), &c.r);
        track();
    };
    auto combo = [&](const char* label, int& v, const char* items) {
        propLabel(label);
        int t = v;
        if (ImGui::Combo((std::string("##") + label).c_str(), &t, items)) {
            pushUndo();
            v = t;
        }
    };

    if (name == "Camera") {
        Camera& c = *w.get<Camera>(e);
        int proj = static_cast<int>(c.projection);
        combo("Proyección", proj, "Ortográfica (2D)\0Perspectiva (3D)\0");
        c.projection = static_cast<Camera::Projection>(proj);
        if (c.projection == Camera::Projection::Orthographic) dragF("Tamaño", c.orthographicSize, 0.05f, 0.1f, 500.0f);
        else dragF("Campo de visión", c.fieldOfView, 0.25f, 10.0f, 150.0f, "%.1f°");
        dragF("Plano cercano", c.nearClip, 0.01f, 0.001f, c.farClip);
        dragF("Plano lejano", c.farClip, 1.0f, c.nearClip + 0.01f, 100000.0f, "%.1f");
        propLabel("Prioridad");
        ImGui::DragInt("##prio", &c.priority, 0.1f);
        track();
        color("Fondo", c.clearColor);
    } else if (name == "SpriteRenderer") {
        SpriteRenderer& s = *w.get<SpriteRenderer>(e);
        propLabel("Textura");
        assetCombo("##tex", AssetType::Texture, s.texture);
        color("Color", s.color);
        propLabel("Tamaño");
        ImGui::DragFloat2("##size", &s.size.x, 0.02f);
        track();
        propLabel("Pivote");
        ImGui::DragFloat2("##pivot", &s.pivot.x, 0.01f, 0, 1);
        track();
        propLabel("Orden");
        ImGui::DragInt("##orden", &s.sortingOrder, 0.1f);
        track();
        check("Visible", s.visible);
    } else if (name == "MeshRenderer") {
        MeshRenderer& m = *w.get<MeshRenderer>(e);
        int mesh = static_cast<int>(m.mesh);
        propLabel("Modelo");
        assetCombo("##modelo", AssetType::Model, m.model);
        if (m.model.isNull()) {
            combo("Malla", mesh, "Cubo\0Esfera\0Plano\0Cilindro\0Cono\0Cápsula\0");
            m.mesh = static_cast<MeshPrimitive>(mesh);
            propLabel("");
            ImGui::TextDisabled("Para modelarla: Crear › Modelo editable");
        } else {
            propLabel("");
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.5f, 0.1f, 1));
            if (ImGui::Button("Modelar / Esculpir (Tab)", ImVec2(-1, 0))) enterMeshEdit(e);
            ImGui::PopStyleColor();
            propLabel("Triángulos");
            ImGui::TextDisabled("%d", renderer.modelTriangles(editorModel(m.model)));
        }
        propLabel("Material");
        assetCombo("##material", AssetType::Material, m.material);
        // Con material compartido se editan los valores del archivo .mat (afecta a todos los que lo usan);
        // sin él, los del propio objeto.
        const MeshRenderer* shared = m.material.isNull() ? nullptr : resources.material(m.material);
        MeshRenderer sharedEdit = shared ? *shared : MeshRenderer{};
        MeshRenderer& mat = shared ? sharedEdit : m;
        if (shared) {
            propLabel("");
            ImGui::TextDisabled("Editando el material compartido");
        }
        auto map = [&](const char* label, AssetRef& ref) {
            propLabel(label);
            assetCombo((std::string("##") + label).c_str(), AssetType::Texture, ref);
        };
        propLabel("Nodos");
        if (shared) {
            ImGui::TextDisabled("Quita el material compartido para usar nodos en este objeto");
        } else if (ImGui::Button(m.graph ? "Editar material por nodos" : "Material por nodos", ImVec2(-1, 0))) {
            openMaterialGraph(e);
        }
        if (m.graph && !shared) {
            const MatGraphProgram& gp = m.graph->program();
            propLabel("");
            if (gp.ok()) ImGui::TextDisabled("Los nodos mandan sobre lo que conectes en la Salida");
            else ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Nodos sin aplicar: %s", gp.error.c_str());
        }
        map("Textura", mat.texture);
        color("Color base", mat.color);
        dragF("Metálico", mat.metallic, 0.01f, 0, 1, "%.2f");
        dragF("Rugosidad", mat.roughness, 0.01f, 0, 1, "%.2f");
        map("Mapa normal", mat.normalMap);
        if (!mat.normalMap.isNull()) dragF("Relieve", mat.normalStrength, 0.01f, 0, 4, "%.2f");
        dragF("Barniz", mat.clearcoat, 0.01f, 0, 1, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Capa transparente brillante encima del material: pintura de coche, madera barnizada");
        if (mat.clearcoat > 0) dragF("Rugosidad barniz", mat.clearcoatRoughness, 0.005f, 0, 1, "%.2f");
        map("Metal/rugosidad", mat.metallicRoughnessMap);
        map("Oclusión", mat.occlusionMap);
        color("Emisión", mat.emission, false);
        map("Mapa emisión", mat.emissionMap);
        propLabel("Repetición");
        ImGui::DragFloat2("##tiling", &mat.tiling.x, 0.01f, -100, 100, "%.2f");
        track();
        propLabel("Desplazamiento");
        ImGui::DragFloat2("##offset", &mat.offset.x, 0.005f, -100, 100, "%.3f");
        track();
        check("Recorte alfa", mat.alphaClip);
        if (mat.alphaClip) dragF("Umbral alfa", mat.alphaCutoff, 0.005f, 0, 1, "%.2f");
        check("Doble cara", mat.doubleSided);
        check("Rejilla", mat.prototypeGrid);
        check("Sin luz", mat.unlit);
        if (shared && serializeMaterial(sharedEdit) != serializeMaterial(*shared)) {
            const AssetRecord* r = project->assets().find(m.material);
            std::string err;
            if (r && fileio::writeAtomic(project->assetsDir() + "/" + r->path, serializeMaterial(sharedEdit), true, &err))
                resources.invalidateMaterial(m.material);
            else
                TITAN_ERROR("editor", "No se pudo guardar el material: %s", err.c_str());
        }
        check("Sombras", m.castShadows);
        check("Visible", m.visible);
        propLabel("");
        if (ImGui::Button(shared ? "Quitar material" : "Guardar como material")) {
            if (shared) {
                pushUndo();
                m.material = {};
            } else {
                saveMaterial(e);
            }
        }
    } else if (name == "Light") {
        Light& l = *w.get<Light>(e);
        int type = static_cast<int>(l.type);
        combo("Tipo", type, "Direccional (sol)\0Puntual\0Foco\0");
        l.type = static_cast<Light::Type>(type);
        color("Color", l.color, false);
        dragF("Intensidad", l.intensity, 0.02f, 0, 20, "%.2f");
        if (l.type != Light::Type::Directional) dragF("Alcance", l.range, 0.1f, 0.1f, 500, "%.1f");
        if (l.type == Light::Type::Spot) {
            dragF("Apertura", l.spotAngle, 0.25f, 1, 179, "%.0f°");
            dragF("Borde suave", l.spotSoftness, 0.005f, 0, 1, "%.2f");
        }
    } else if (name == "Environment") {
        Environment& v = *w.get<Environment>(e);
        check("Cielo", v.sky);
        color("Cénit", v.skyZenith, false);
        color("Horizonte", v.skyHorizon, false);
        color("Suelo", v.ground, false);
        dragF("Luz ambiental", v.ambientIntensity, 0.01f, 0, 4, "%.2f");
        color("Niebla", v.fogColor, false);
        dragF("Niebla dens.", v.fogDensity, 0.0005f, 0, 0.5f, "%.4f");
        dragF("Exposición", v.exposure, 0.01f, 0.05f, 8, "%.2f");
        check("Sombras", v.shadows);
        dragF("Dist. sombras", v.shadowDistance, 0.5f, 5, 500, "%.0f");
        check("Bloom", v.bloom);
        if (v.bloom) {
            dragF("Intens. bloom", v.bloomIntensity, 0.01f, 0, 4, "%.2f");
            dragF("Umbral bloom", v.bloomThreshold, 0.005f, 0, 1, "%.2f");
        }
        check("Oclusión (SSAO)", v.ssao);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Sombras de contacto en rincones y bajo los objetos (perfiles de calidad alto y muy alto)");
        if (v.ssao) {
            dragF("Intens. SSAO", v.ssaoIntensity, 0.01f, 0, 4, "%.2f");
            dragF("Radio SSAO", v.ssaoRadius, 0.01f, 0.05f, 5, "%.2f m");
        }
        check("Reflejos (SSR)", v.ssr);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("La escena se refleja en el agua, los suelos pulidos, el metal y el barniz (perfiles alto y muy alto)");
        if (v.ssr) dragF("Intens. reflejos", v.ssrIntensity, 0.01f, 0, 2, "%.2f");
        check("Antialiasing temporal (TAA)", v.taa);
        check("HDR (brillos y neón)", v.hdr);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("La emisión, el sol y los brillos fuertes pasan de 1 y hacen bloom de verdad");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Bordes limpios y sin parpadeo (como UE5). Sustituye al FXAA. Perfiles alto y muy alto");
        dragF("Viñeta", v.vignette, 0.005f, 0, 1, "%.2f");
        dragF("Contraste", v.contrast, 0.005f, 0.5f, 1.5f, "%.2f");
        dragF("Saturación", v.saturation, 0.005f, 0, 2, "%.2f");
        dragF("Temperatura", v.temperature, 0.01f, -1, 1, "%.2f");
        if (!renderer.quality().bloom && v.bloom) {
            propLabel("");
            ImGui::TextDisabled("El perfil de calidad actual no permite bloom");
        }
    } else if (name == "Animator") {
        Animator& a = *w.get<Animator>(e);
        const MeshRenderer* mr = w.get<MeshRenderer>(e);
        const int model = mr ? editorModel(mr->model) : -1;
        const int count = renderer.modelAnimationCount(model);
        propLabel("Clip");
        if (count == 0) {
            ImGui::TextDisabled(model < 0 ? "Necesita un modelo con animaciones" : "El modelo no tiene animaciones");
        } else {
            const std::string preview = a.clip.empty() ? renderer.modelAnimationName(model, 0) + " (primero)" : a.clip;
            if (ImGui::BeginCombo("##clip", preview.c_str())) {
                for (int i = 0; i < count; ++i) {
                    const std::string n = renderer.modelAnimationName(model, i);
                    char label[160];
                    std::snprintf(label, sizeof(label), "%s  (%.1f s)", n.c_str(), renderer.modelAnimationDuration(model, i));
                    if (ImGui::Selectable(label, n == a.clip)) {
                        pushUndo();
                        switchAnimation(a, n); // se ve la transición en la vista previa
                    }
                }
                ImGui::EndCombo();
            }
        }
        check("Reproducir", a.playing);
        check("Repetir", a.loop);
        dragF("Velocidad", a.speed, 0.01f, -4, 4, "%.2fx");
        dragF("Transición", a.fadeDuration, 0.01f, 0, 2, "%.2f s");
        propLabel("Tiempo");
        const int idx = renderer.modelAnimationIndex(model, a.clip);
        const float dur = renderer.modelAnimationDuration(model, idx);
        float t = dur > 0 ? std::fmod(a.time, dur) : a.time;
        if (ImGui::SliderFloat("##tiempo", &t, 0, std::max(dur, 0.01f), "%.2f s")) a.time = t; // fregar la animación
        if (ImGui::IsItemActive()) noScrollWidgetActive = true;
        propLabel("Estados");
        const std::string label = a.machine.empty() ? std::string("Crear máquina de estados")
                                                    : "Editar (" + std::to_string(a.machine.states.size()) + " estados)";
        if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
            machineEditorFor = scene.info(e).id;
            showMachineEditor = true;
        }
        if (!a.machine.empty()) {
            propLabel("Estado actual");
            ImGui::TextDisabled("%s", a.machine.current.empty() ? "-" : a.machine.current.c_str());
        }
    } else if (name == "AudioSource") {
        AudioSource& a = *w.get<AudioSource>(e);
        propLabel("Sonido");
        assetCombo("##clipaudio", AssetType::Audio, a.clip);
        dragF("Volumen", a.volume, 0.01f, 0, 4, "%.2f");
        dragF("Tono", a.pitch, 0.01f, 0.1f, 4, "%.2f");
        check("Repetir", a.loop);
        check("Al empezar", a.playOnStart);
        propLabel("Bus");
        if (mixerProject != project->root()) loadMixer();
        if (ImGui::BeginCombo("##busaudio", a.bus.empty() ? "Master" : a.bus.c_str())) {
            for (const AudioBusSettings& b : mixerLayout.buses)
                if (ImGui::Selectable(b.name.c_str(), (a.bus.empty() ? "Master" : a.bus) == b.name)) {
                    a.bus = b.name == "Master" ? std::string() : b.name;
                    dirty = true;
                }
            if (ImGui::Selectable("Abrir el mezclador...")) showMixer = true;
            ImGui::EndCombo();
        }
        if (!a.bus.empty() && mixerLayout.find(a.bus) < 0)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "El bus '%s' no existe: sonará en Master", a.bus.c_str());
        check("Sonido 3D", a.spatial);
        if (a.spatial) {
            dragF("Distancia mín.", a.minDistance, 0.05f, 0.01f, 1000, "%.1f m");
            dragF("Distancia máx.", a.maxDistance, 0.1f, 0.1f, 10000, "%.1f m");
        }
    } else if (name == "ParticleSystem") {
        ParticleSystem& p = *w.get<ParticleSystem>(e);
        propLabel("");
        if (ImGui::Button("Reiniciar")) {
            clearParticles(p);
            playParticles(p);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu vivas", aliveParticles(p));
        check("Al empezar", p.playOnStart);
        check("Repetir", p.looping);
        dragF("Duración", p.duration, 0.05f, 0.05f, 600, "%.2f s");
        dragF("Por segundo", p.rate, 0.5f, 0, 5000, "%.0f");
        dragF("Por metro", p.ratePerMeter, 0.1f, 0, 1000, "%.1f");
        propLabel("Ráfaga");
        ImGui::DragInt("##burst", &p.burst, 1, 0, 5000);
        track();
        propLabel("Máximo");
        ImGui::DragInt("##maxp", &p.maxParticles, 5, 1, 10000);
        track();
        propLabel("Vida");
        ImGui::DragFloatRange2("##vida", &p.lifetimeMin, &p.lifetimeMax, 0.02f, 0.01f, 60, "%.2f s");
        track();
        propLabel("Velocidad");
        ImGui::DragFloatRange2("##vel", &p.speedMin, &p.speedMax, 0.05f, 0, 200, "%.1f");
        track();
        dragF("Apertura", p.spread, 0.5f, 0, 180, "%.0f°");
        combo("Forma", p.shape, "Punto\0Esfera\0Caja\0");
        if (p.shape != 0) {
            propLabel(p.shape == 1 ? "Radio" : "Tamaño");
            if (p.shape == 1) ImGui::DragFloat("##radio", &p.shapeSize.x, 0.01f, 0, 500);
            else ImGui::DragFloat3("##caja", &p.shapeSize.x, 0.02f, 0, 500);
            track();
        }
        propLabel("Aceleración");
        ImGui::DragFloat3("##acc", &p.acceleration.x, 0.05f, -100, 100, "%.2f");
        track();
        dragF("Frenado", p.drag, 0.01f, 0, 20, "%.2f");
        propLabel("Tamaño");
        ImGui::DragFloatRange2("##tam", &p.sizeStart, &p.sizeEnd, 0.005f, 0, 50, "inicio %.2f", "fin %.2f");
        track();
        color("Color inicial", p.colorStart);
        color("Color final", p.colorEnd);
        dragF("Giro", p.rotationSpeed, 1, 0, 2000, "%.0f°/s");
        propLabel("Textura");
        assetCombo("##ptex", AssetType::Texture, p.texture);
        if (!p.texture.isNull()) {
            propLabel("Cuadros");
            int cr[2] = {p.sheetColumns, p.sheetRows};
            if (ImGui::DragInt2("##sheet", cr, 0.1f, 1, 64)) {
                p.sheetColumns = std::max(1, cr[0]);
                p.sheetRows = std::max(1, cr[1]);
            }
            track();
        }
        check("Aditiva (brillo)", p.additive);
        check("En el mundo", p.worldSpace);
    } else if (name == "UIRect") {
        UIRect& r = *w.get<UIRect>(e);
        auto drag2 = [&](const char* label, Vec2& v, float speed, float lo, float hi, const char* fmt) {
            propLabel(label);
            ImGui::DragFloat2((std::string("##") + label).c_str(), &v.x, speed, lo, hi, fmt);
            track();
        };
        // Anclaje rápido: 9 posiciones de la pantalla (o del padre) y "estirar".
        propLabel("Anclaje");
        const float cell = ImGui::GetFrameHeight();
        ImGui::BeginGroup();
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                const Vec2 a{i * 0.5f, j * 0.5f};
                const bool on = r.anchorMin == a && r.anchorMax == a;
                ImGui::PushID(j * 3 + i);
                ImGui::PushStyleColor(ImGuiCol_Button, on ? kBlue : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                if (ImGui::Button(on ? "o" : "", ImVec2(cell, cell * 0.8f))) {
                    pushUndo();
                    r.anchorMin = r.anchorMax = r.pivot = a;
                    const float m = 24; // margen al borde
                    r.position = {(0.5f - a.x) * 2 * m, (0.5f - a.y) * 2 * m};
                    if (r.size.x <= 0 || r.size.y <= 0) r.size = {240, 80};
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 2) ImGui::SameLine(0, 2 * scale);
            }
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ImGui::Button("Estirar", ImVec2(0, cell * 2.4f + 4 * scale))) {
            pushUndo();
            r.anchorMin = {0, 0};
            r.anchorMax = {1, 1};
            r.pivot = {0.5f, 0.5f};
            r.position = {};
            r.size = {};
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Ocupa todo el padre (o la pantalla)");
        drag2("Posición", r.position, 1, -10000, 10000, "%.0f");
        const bool stretchX = r.anchorMin.x != r.anchorMax.x, stretchY = r.anchorMin.y != r.anchorMax.y;
        drag2(stretchX || stretchY ? "Tamaño (+/-)" : "Tamaño", r.size, 1, -10000, 10000, "%.0f");
        drag2("Pivote", r.pivot, 0.01f, 0, 1, "%.2f");
        drag2("Anclaje mín.", r.anchorMin, 0.01f, 0, 1, "%.2f");
        drag2("Anclaje máx.", r.anchorMax, 0.01f, 0, 1, "%.2f");
    } else if (name == "UIImage") {
        UIImage& img = *w.get<UIImage>(e);
        propLabel("Imagen");
        assetCombo("##uitex", AssetType::Texture, img.texture);
        color("Color", img.color);
        if (img.texture.isNull()) dragF("Esquinas", img.cornerRadius, 0.5f, 0, 1000, "%.0f px");
        propLabel("Relleno");
        ImGui::SliderFloat("##fill", &img.fill, 0, 1, "%.2f");
        track();
    } else if (name == "UIText") {
        UIText& t = *w.get<UIText>(e);
        propLabel("Texto");
        ImGui::InputTextMultiline("##uitext", &t.text, ImVec2(-1, ImGui::GetTextLineHeight() * 3.2f));
        track();
        propLabel("Fuente");
        assetCombo("##uifont", AssetType::Font, t.font);
        if (t.font.isNull()) check("Negrita", t.bold);
        dragF("Tamaño", t.size, 0.25f, 4, 400, "%.0f px");
        color("Color", t.color);
        combo("Alineación", t.align, "Izquierda\0Centro\0Derecha\0");
        combo("Vertical", t.verticalAlign, "Arriba\0Centro\0Abajo\0");
        check("Sombra", t.shadow);
    } else if (name == "UIButton") {
        UIButton& b = *w.get<UIButton>(e);
        check("Interactivo", b.interactable);
        color("Tinte al pulsar", b.pressedTint);
        propLabel("");
        ImGui::TextDisabled("Al pulsarlo llama a onClick() de su script");
    } else if (name == "UIJoystick") {
        UIJoystick& j = *w.get<UIJoystick>(e);
        color("Base", j.baseColor);
        color("Mango", j.handleColor);
        dragF("Tamaño mango", j.handleSize, 0.005f, 0.1f, 1, "%.2f");
        dragF("Zona muerta", j.deadZone, 0.005f, 0, 0.9f, "%.2f");
        propLabel("");
        ImGui::TextDisabled("En un script: local x, y = e:getJoystick()");
    } else if (name == "Rigidbody") {
        Rigidbody& b = *w.get<Rigidbody>(e);
        int type = static_cast<int>(b.type);
        combo("Tipo", type, "Estático\0Dinámico\0Cinemático\0");
        b.type = static_cast<Rigidbody::Type>(type);
        dragF("Masa", b.mass, 0.05f, 0.001f, 100000, "%.2f kg");
        dragF("Fricción", b.friction, 0.01f, 0, 2, "%.2f");
        dragF("Rebote", b.restitution, 0.01f, 0, 1, "%.2f");
        dragF("Amort. lineal", b.linearDamping, 0.01f, 0, 10, "%.2f");
        dragF("Amort. angular", b.angularDamping, 0.01f, 0, 10, "%.2f");
        dragF("Escala gravedad", b.gravityScale, 0.01f, -10, 10, "%.2f");
        check("Bloquear rotación", b.lockRotation);
    } else if (name == "Collider") {
        Collider& c = *w.get<Collider>(e);
        int shape = static_cast<int>(c.shape);
        combo("Forma", shape, "Caja\0Esfera\0Cápsula\0Malla (escenario)\0Envolvente convexa\0");
        c.shape = static_cast<Collider::Shape>(shape);
        if (c.shape == Collider::Shape::Box) {
            propLabel("Tamaño");
            ImGui::DragFloat3("##csize", &c.size.x, 0.02f, 0.001f, 10000);
            track();
        } else if (c.shape == Collider::Shape::Sphere || c.shape == Collider::Shape::Capsule) {
            dragF("Radio", c.radius, 0.01f, 0.001f, 1000, "%.2f");
            if (c.shape == Collider::Shape::Capsule) dragF("Altura", c.height, 0.01f, 0.001f, 1000, "%.2f");
        } else {
            propLabel("");
            ImGui::TextDisabled(c.shape == Collider::Shape::Mesh ? "Usa los triángulos de su modelo (solo estático)"
                                                                 : "Envolvente del modelo (sirve para dinámicos)");
        }
        propLabel("Centro");
        ImGui::DragFloat3("##ccenter", &c.center.x, 0.02f);
        track();
        check("Es trigger", c.isTrigger);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        if (ImGui::SmallButton("Ajustar al objeto")) {
            pushUndo();
            fitCollider(e);
        }
        propLabel("Capa");
        ImGui::SliderInt("##collayer", &c.layer, 0, 15);
        track();
    } else if (name == "NavAgent") {
        NavAgent& a = *w.get<NavAgent>(e);
        dragF("Velocidad", a.speed, 0.05f, 0.1f, 50, "%.1f m/s");
        dragF("Distancia de parada", a.stoppingDistance, 0.01f, 0.05f, 20, "%.2f m");
        dragF("Recalcular cada", a.repathInterval, 0.01f, 0.05f, 10, "%.2f s");
        propLabel("");
        ImGui::TextDisabled("e:setDestination(x, y, z) o e:setDestination(jugador); AI.canSee(e, jugador)");
    } else if (name == "VoxelTerrain") {
        VoxelTerrain& v = *w.get<VoxelTerrain>(e);
        propLabel("Semilla");
        ImGui::InputInt("##vseed", &v.seed);
        track();
        propLabel("Distancia de vista");
        ImGui::SliderInt("##vview", &v.viewDistance, 1, 24, "%d chunks");
        track();
        propLabel("Nombre del mundo");
        ImGui::InputText("##vname", &v.worldName);
        track();
        propLabel("Nivel del mar");
        ImGui::SliderInt("##vsea", &v.seaLevel, 2, kChunkHeight - 10);
        track();
        dragF("Escala del relieve", v.terrainScale, 0.01f, 0.2f, 3, "%.2f");
        check("Cuevas", v.caves);
        check("Árboles", v.trees);
        propLabel("");
        ImGui::TextDisabled("Se genera al jugar. Los cambios se guardan en .titan/mundos/<nombre>.\n"
                            "La semilla y los ajustes de un mundo ya guardado mandan sobre estos.\n"
                            "Lua: Voxel.get/set/raycast/surface/biome/save");
    } else if (name == "BlobShadow") {
        BlobShadow& b = *w.get<BlobShadow>(e);
        dragF("Radio", b.radius, 0.01f, 0.05f, 20, "%.2f m");
        dragF("Opacidad", b.opacity, 0.01f, 0, 1, "%.2f");
        dragF("Altura máx.", b.maxHeight, 0.1f, 0.5f, 200, "%.1f m");
        propLabel("");
        ImGui::TextDisabled("Se ve al jugar (usa la física para encontrar el suelo)");
    } else if (name == "CameraFollow") {
        CameraFollow& c = *w.get<CameraFollow>(e);
        propLabel("Sigue a");
        const Entity tgt = c.target.isNull() ? Entity{} : scene.findById(c.target);
        if (ImGui::BeginCombo("##camtarget", tgt.isNull() ? "(nadie)" : scene.info(tgt).name.c_str())) {
            scene.traverse([&](Entity o, int) {
                if (o == e) return;
                ImGui::PushID(static_cast<int>(o.index));
                if (ImGui::Selectable(scene.info(o).name.c_str(), o == tgt)) {
                    pushUndo();
                    c.target = scene.info(o).id;
                }
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }
        dragF("Distancia", c.distance, 0.05f, 0.5f, 100, "%.1f m");
        propLabel("Zoom mín./máx.");
        ImGui::DragFloatRange2("##camzoom", &c.minDistance, &c.maxDistance, 0.05f, 0.3f, 200, "%.1f");
        track();
        dragF("Altura mira", c.height, 0.02f, -10, 10, "%.2f m");
        dragF("Giro", c.yaw, 0.5f, -360, 360, "%.0f°");
        dragF("Inclinación", c.pitch, 0.5f, -89, 89, "%.0f°");
        propLabel("Inclinación lím.");
        ImGui::DragFloatRange2("##campitch", &c.minPitch, &c.maxPitch, 0.5f, -89, 89, "%.0f°");
        track();
        check("Girar con dedo/mando", c.rotateWithInput);
        if (c.rotateWithInput) {
            dragF("Sensib. dedo", c.touchSensitivity, 0.005f, 0.01f, 2, "%.2f");
            dragF("Vel. mando", c.padSpeed, 1, 10, 720, "%.0f°/s");
        }
        dragF("Suavizado", c.smoothing, 0.1f, 0, 50, "%.1f");
        check("Evitar paredes", c.avoidWalls);
        propLabel("");
        ImGui::TextDisabled("Mover respecto a la cámara: Input.cameraAxis(\"Mover\")");
    } else if (name == "Terrain") {
        drawTerrainPanel(e, *w.get<Terrain>(e));
    } else if (name == "CharacterController") {
        CharacterController& c = *w.get<CharacterController>(e);
        dragF("Altura", c.height, 0.01f, 0.2f, 20, "%.2f m");
        dragF("Radio", c.radius, 0.01f, 0.05f, 5, "%.2f m");
        dragF("Pendiente máx.", c.maxSlope, 0.5f, 1, 89, "%.0f°");
        dragF("Escalón", c.stepHeight, 0.01f, 0, 2, "%.2f m");
        dragF("Escala gravedad", c.gravityScale, 0.01f, -5, 5, "%.2f");
        dragF("Aceleración", c.acceleration, 0.5f, 0, 500, "%.0f m/s²");
        dragF("Frenado", c.deceleration, 0.5f, 0, 500, "%.0f m/s²");
        dragF("Control aéreo", c.airControl, 0.01f, 0, 1, "%.2f");
        propLabel("Saltos");
        ImGui::SliderInt("##ccjumps", &c.maxJumps, 1, 5);
        track();
        dragF("Corte de salto", c.jumpCut, 0.01f, 0, 1, "%.2f");
        check("Girar al moverse", c.faceMovement);
        if (c.faceMovement) dragF("Vel. de giro", c.turnSpeed, 5, 30, 3600, "%.0f°/s");
        propLabel("Capa");
        ImGui::SliderInt("##cclayer", &c.layer, 0, 15);
        track();
        propLabel("");
        ImGui::TextDisabled("En un script: e:move(x, z), e:jump(5), e:isGrounded()");
    } else if (name == "Joint") {
        Joint& j = *w.get<Joint>(e);
        int type = static_cast<int>(j.type);
        combo("Tipo", type, "Fija\0Bisagra\0Distancia / cuerda\0");
        j.type = static_cast<Joint::Type>(type);
        propLabel("Conectada a");
        const Entity other = j.connected.isNull() ? Entity{} : scene.findById(j.connected);
        const std::string current = other.isNull() ? std::string("(el mundo)") : scene.info(other).name;
        if (ImGui::BeginCombo("##jointcon", current.c_str())) {
            if (ImGui::Selectable("(el mundo)", other.isNull())) {
                pushUndo();
                j.connected = {};
            }
            scene.world().each<Rigidbody>([&](Entity o, Rigidbody&) {
                if (o == e) return;
                ImGui::PushID(static_cast<int>(o.index));
                if (ImGui::Selectable(scene.info(o).name.c_str(), o == other)) {
                    pushUndo();
                    j.connected = scene.info(o).id;
                }
                ImGui::PopID();
            });
            ImGui::EndCombo();
        }
        propLabel("Punto (local)");
        ImGui::DragFloat3("##janchor", &j.anchor.x, 0.01f);
        track();
        if (j.type == Joint::Type::Hinge) {
            propLabel("Eje (local)");
            ImGui::DragFloat3("##jaxis", &j.axis.x, 0.01f, -1, 1);
            track();
            check("Limitar giro", j.limitAngle);
            if (j.limitAngle) {
                propLabel("Ángulos");
                ImGui::DragFloatRange2("##jang", &j.minAngle, &j.maxAngle, 0.5f, -180, 180, "%.0f°");
                track();
            }
        } else if (j.type == Joint::Type::Distance) {
            propLabel("Distancia");
            ImGui::DragFloatRange2("##jdist", &j.minDistance, &j.maxDistance, 0.01f, 0, 1000, "mín %.2f", "máx %.2f");
            track();
            dragF("Muelle (Hz)", j.springFrequency, 0.05f, 0, 30, "%.2f");
            if (j.springFrequency > 0) dragF("Amortiguación", j.springDamping, 0.01f, 0, 1, "%.2f");
        }
    } else if (name == "Script") {
        Script& s = *w.get<Script>(e);
        propLabel("Archivo");
        assetCombo("##script", AssetType::Script, s.script);
        check("Activado", s.enabled);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        if (const AssetRecord* r = project->assets().find(s.script)) {
            drawScriptProps(s);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
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

void Editor::Impl::drawScriptProps(Script& s) {
    const auto& props = scriptProps(s.script);
    if (props.empty()) return;
    Json over = Json::parse(s.props, nullptr, false);
    if (!over.is_object()) over = Json::object();
    bool changed = false;
    for (const ScriptProp& p : props) {
        propLabel(p.name.c_str());
        ImGui::PushID(p.name.c_str());
        const bool overridden = over.contains(p.name);
        if (overridden) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.72f, 1, 1)); // valor cambiado en esta entidad
        if (p.type == 0) {
            float v = static_cast<float>(overridden && over[p.name].is_number() ? over[p.name].get<double>() : p.number);
            if (ImGui::DragFloat("##p", &v, 0.05f)) { over[p.name] = v; changed = true; }
            track();
        } else if (p.type == 1) {
            bool v = overridden && over[p.name].is_boolean() ? over[p.name].get<bool>() : p.boolean;
            if (ImGui::Checkbox("##p", &v)) { pushUndo(); over[p.name] = v; changed = true; }
        } else {
            std::string v = overridden && over[p.name].is_string() ? over[p.name].get<std::string>() : p.text;
            if (ImGui::InputText("##p", &v)) { over[p.name] = v; changed = true; }
            track();
        }
        if (overridden) ImGui::PopStyleColor();
        if (overridden && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            pushUndo();
            over.erase(p.name); // clic derecho: volver al valor del script
            changed = true;
        }
        ImGui::PopID();
    }
    if (changed) {
        s.props = over.dump();
        dirty = true;
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
    if (codePath.empty()) {
        codeTabActive = false;
        return;
    }
    if (ImGui::Button(codeDirty ? "Guardar *" : "Guardar")) saveCode();
    ImGui::SameLine();
    if (ImGui::Button("Comprobar")) checkCode();
    ImGui::SameLine();
    ImGui::TextDisabled("Línea %d", codeCursorLine);
    if (!codeCheck.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, codeErrorLine ? ImVec4(0.95f, 0.45f, 0.45f, 1) : ImVec4(0.45f, 0.85f, 0.55f, 1));
        ImGui::TextUnformatted(codeCheck.c_str());
        ImGui::PopStyleColor();
    }

    // Atajos para teclados táctiles: insertan en la posición del cursor.
    static const char* snippets[] = {"self.entity:", "function", "end", "local", "if", "then", "else", "for", "do",
                                     "return", "(", ")", "{", "}", "=", "==", ".", ":", ",", "\"", "--", "dt"};
    ImGui::BeginChild("##atajos", ImVec2(0, ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4 * scale), 0,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < sizeof(snippets) / sizeof(*snippets); ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::SmallButton(snippets[i])) {
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

    if (monoFont) ImGui::PushFont(monoFont);
    if (codeRequestFocus) {
        ImGui::SetKeyboardFocusHere();
        codeRequestFocus = false;
    }
    const std::string before = code;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 1));
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
    ImGui::PopStyleColor();
    if (ImGui::IsItemActive()) noScrollWidgetActive = true;
    if (monoFont) ImGui::PopFont();
    if (code != before) {
        codeDirty = true;
        codeCheck.clear();
    }
}

// ============================================================ Navegador de contenido

void Editor::Impl::drawContent() {
    if (iconButton("##contanadir", Icon::Plus, false, "Añadir")) ImGui::OpenPopup("anadircont");
    if (ImGui::BeginPopup("anadircont")) {
        if (ImGui::MenuItem("Nueva escena")) {
            save();
            Scene s("nueva");
            Entity cam = s.createEntity("Cámara");
            Camera c;
            if (project->dimension() == ProjectDimension::ThreeD) {
                c.projection = Camera::Projection::Perspective;
                s.transform(cam).setPosition({0, 3, 9});
                s.transform(cam).setEulerDegrees({-15, 0, 0});
                s.world().add<Environment>(s.createEntity("Cielo y niebla"));
                Entity sun = s.createEntity("Sol");
                s.transform(sun).setEulerDegrees({-50, 35, 0});
                s.world().add<Light>(sun);
            } else {
                s.transform(cam).setPosition({0, 0, 10});
            }
            s.world().add<Camera>(cam, c);
            std::string rel = "scenes/escena.scene";
            for (int i = 2; fs::exists(project->assetsDir() + "/" + rel); ++i) rel = "scenes/escena" + std::to_string(i) + ".scene";
            const UUID id = project->saveScene(s, registry, rel);
            if (!id.isNull()) loadScene(id);
        }
        if (ImGui::BeginMenu("Nuevo script")) {
            ImGui::SetNextItemWidth(200 * scale);
            ImGui::InputTextWithHint("##nombrescript", "Nombre", &newScriptName);
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
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Importar")) {
        if (!platform.requestImportFile()) ImGui::OpenPopup("importar ruta");
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
    // Ruta actual.
    ImGui::SameLine();
    ImGui::TextDisabled("  Contenido%s%s", contentFolder.empty() ? "" : " / ", contentFolder.c_str());
    const AssetRecord* sel = project->assets().find(contentSelected);
    Entity target = selectedEntity();
    if (sel) {
        ImGui::SameLine();
        if (sel->type == AssetType::Scene && sel->id != sceneId && ImGui::SmallButton("Abrir escena")) {
            save();
            loadScene(sel->id);
        }
        if (sel->type == AssetType::Scene && sel->id != project->startScene()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Hacer inicial")) {
                project->setStartScene(sel->id);
                project->save();
            }
        }
        if (sel->type == AssetType::Script) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Editar")) openScriptFile(sel->path);
        }
        if (sel->type == AssetType::Prefab) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
            if (ImGui::SmallButton("Añadir a la escena##prefab")) instantiatePrefabAsset(*sel);
            ImGui::PopStyleColor();
        }
        if (sel->type == AssetType::Model) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
            if (ImGui::SmallButton("Añadir a la escena")) addModelToScene(*sel);
            ImGui::PopStyleColor();
        }
        if ((sel->type == AssetType::Texture || sel->type == AssetType::Script) && !target.isNull()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(("Asignar a " + scene.info(target).name).c_str())) {
                pushUndo();
                if (sel->type == AssetType::Script) scene.world().add<Script>(target, {sel->id, true});
                else if (auto* m = scene.world().get<MeshRenderer>(target)) m->texture = sel->id;
                else {
                    SpriteRenderer sp = scene.world().has<SpriteRenderer>(target) ? *scene.world().get<SpriteRenderer>(target) : SpriteRenderer{};
                    sp.texture = sel->id;
                    scene.world().add<SpriteRenderer>(target, sp);
                }
            }
        }
    }

    // Árbol de carpetas a la izquierda, mosaicos a la derecha.
    std::set<std::string> folders;
    for (const AssetRecord* r : project->assets().all())
        for (std::string f = folderOf(r->path); !f.empty(); f = folderOf(f)) folders.insert(f);
    const float treeW = std::min(200 * scale, ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::BeginChild("##carpetas", ImVec2(treeW, 0), ImGuiChildFlags_Borders);
    if (ImGui::Selectable("Contenido", contentFolder.empty())) contentFolder.clear();
    for (const std::string& f : folders) {
        const int depth = static_cast<int>(std::count(f.begin(), f.end(), '/')) + 1;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 12 * scale);
        if (ImGui::Selectable((fileName(f) + "##" + f).c_str(), contentFolder == f)) contentFolder = f;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##mosaicos", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const float tile = 84 * scale, pad = ImGui::GetStyle().ItemSpacing.x;
    const int cols = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + pad) / (tile + pad)));
    int col = 0;
    auto tileStart = [&]() {
        if (col > 0) ImGui::SameLine();
        if (++col >= cols) col = 0;
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Subcarpetas directas.
    for (const std::string& f : folders) {
        if (folderOf(f) != contentFolder) continue;
        tileStart();
        ImGui::PushID(f.c_str());
        if (ImGui::Button("##carpeta", ImVec2(tile, tile))) contentFolder = f;
        const ImVec2 a = ImGui::GetItemRectMin();
        drawIcon(dl, Icon::Folder, ImVec2(a.x + tile * 0.5f, a.y + tile * 0.4f), tile * 0.3f, IM_COL32(200, 160, 80, 255));
        const std::string n = fileName(f);
        dl->AddText(ImVec2(a.x + 4 * scale, a.y + tile - ImGui::GetFontSize() - 4 * scale), IM_COL32(220, 220, 220, 255), n.c_str());
        ImGui::PopID();
    }
    // Recursos de la carpeta.
    for (const AssetRecord* r : project->assets().all()) {
        if (folderOf(r->path) != contentFolder) continue;
        tileStart();
        ImGui::PushID(r->path.c_str());
        const bool selectedTile = r->id == contentSelected;
        if (selectedTile) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.88f, 0.6f));
        if (ImGui::Button("##tile", ImVec2(tile, tile))) {
            if (contentSelected == r->id && r->type == AssetType::Script) openScriptFile(r->path); // segundo toque abre
            contentSelected = r->id;
        }
        if (selectedTile) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (r->type == AssetType::Scene && r->id != sceneId) {
                save();
                loadScene(r->id);
            } else if (r->type == AssetType::Script) {
                openScriptFile(r->path);
            } else if (r->type == AssetType::Model) {
                addModelToScene(*r);
            } else if (r->type == AssetType::Prefab) {
                instantiatePrefabAsset(*r);
            }
        }
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 thumbA(a.x + 6 * scale, a.y + 6 * scale), thumbB(a.x + tile - 6 * scale, a.y + tile - ImGui::GetFontSize() - 10 * scale);
        static const ImU32 typeColors[] = {IM_COL32(120, 120, 120, 255), IM_COL32(230, 140, 40, 255), IM_COL32(200, 70, 70, 255),
                                           IM_COL32(70, 160, 230, 255),  IM_COL32(170, 90, 220, 255), IM_COL32(60, 190, 120, 255),
                                           IM_COL32(200, 200, 80, 255),  IM_COL32(150, 150, 150, 255),
                                           IM_COL32(80, 200, 230, 255)};
        const ImU32 tc = typeColors[static_cast<int>(r->type)];
        if (r->type == AssetType::Texture) {
            const TextureHandle h = editorTexture(r->id);
            dl->AddImage(static_cast<ImTextureID>(static_cast<uintptr_t>(renderer.glTexture(h))), thumbA, thumbB);
        } else {
            dl->AddRectFilled(thumbA, thumbB, IM_COL32(40, 40, 40, 255), 4 * scale);
            const char* kind = assetTypeName(r->type);
            const ImVec2 ks = ImGui::CalcTextSize(kind);
            dl->AddText(ImVec2((thumbA.x + thumbB.x - ks.x) * 0.5f, (thumbA.y + thumbB.y - ks.y) * 0.5f), tc, kind);
        }
        dl->AddRectFilled(ImVec2(thumbA.x, thumbB.y - 3 * scale), thumbB, tc);
        std::string n = fileName(r->path);
        while (n.size() > 3 && ImGui::CalcTextSize(n.c_str()).x > tile - 8 * scale) n = n.substr(0, n.size() - 4) + "..";
        dl->AddText(ImVec2(a.x + 4 * scale, a.y + tile - ImGui::GetFontSize() - 4 * scale), IM_COL32(220, 220, 220, 255), n.c_str());
        if (r->id == project->startScene()) dl->AddCircleFilled(ImVec2(thumbB.x - 6 * scale, thumbA.y + 6 * scale), 4 * scale, IM_COL32(240, 200, 60, 255));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ============================================================ Registro de salida

void Editor::Impl::drawConsole() {
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
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (monoFont) ImGui::PushFont(monoFont);
    for (const LogMessage& m : logs) {
        if ((m.level == LogLevel::Info && !showInfo) || (m.level == LogLevel::Warning && !showWarnings) ||
            (m.level == LogLevel::Error && !showErrors))
            continue;
        const ImVec4 col = m.level == LogLevel::Error ? ImVec4(0.95f, 0.45f, 0.45f, 1)
                         : m.level == LogLevel::Warning ? ImVec4(0.95f, 0.78f, 0.35f, 1)
                                                        : ImVec4(0.78f, 0.78f, 0.78f, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrapped("%7.2f  %-8s %s", m.seconds, m.category.c_str(), m.text.c_str());
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
