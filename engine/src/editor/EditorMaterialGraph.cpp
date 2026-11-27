// Editor de materiales por nodos: lienzo con nodos, conexiones arrastrando de una salida a una entrada, parámetros
// dentro de cada nodo y el estado de la compilación. El resultado se ve al momento en el visor (el grafo se ejecuta
// en la GPU sin recompilar shaders).
#include <algorithm>
#include <cmath>

#include "EditorImpl.h"
#include "imgui.h"
#include "titan/core/MaterialGraph.h"

namespace titan {

namespace {
ImVec2 add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
float dist2(ImVec2 a, ImVec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
ImU32 nodeColor(MatNode t) {
    switch (t) {
    case MatNode::Output: return IM_COL32(150, 60, 60, 255);
    case MatNode::Color: case MatNode::Scalar: return IM_COL32(60, 110, 60, 255);
    case MatNode::Texture: return IM_COL32(150, 100, 40, 255);
    case MatNode::TexCoord: case MatNode::Time: case MatNode::WorldPos: case MatNode::Normal: case MatNode::Fresnel:
        return IM_COL32(60, 90, 150, 255);
    default: return IM_COL32(80, 80, 95, 255);
    }
}
// Parámetro que se edita dentro del nodo (texto y cuántos números).
const char* paramLabel(MatNode t, int& count) {
    count = 1;
    switch (t) {
    case MatNode::Scalar: return "Valor";
    case MatNode::TexCoord: count = 2; return "Repetir";
    case MatNode::Fresnel: case MatNode::Power: return "Exponente";
    case MatNode::Sine: return "Frecuencia";
    case MatNode::Noise: case MatNode::Checker: return "Escala";
    case MatNode::Panner: count = 2; return "Velocidad";
    default: count = 0; return nullptr;
    }
}
} // namespace

void Editor::Impl::openMaterialGraph(Entity e) {
    MeshRenderer* mr = scene.world().get<MeshRenderer>(e);
    if (!mr) return;
    if (!mr->graph) {
        pushUndo();
        mr->graph = std::make_shared<MaterialGraph>(MaterialGraph::makeDefault());
        // El color del grafo empieza siendo el del material, para que nada cambie al crearlo.
        for (MatGraphNode& n : mr->graph->nodes)
            if (n.type == MatNode::Color) n.value = mr->color;
        mr->graph->touch();
    }
    matGraphEntity = scene.info(e).id;
    showMatGraph = true;
}

void Editor::Impl::drawMaterialGraph() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.2f, vp->Pos.y + vp->Size.y * 0.2f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.6f, vp->Size.y * 0.6f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Material por nodos###matgraph", &showMatGraph, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    noteFloatingWindow();
    const Entity e = scene.findById(matGraphEntity);
    MeshRenderer* mr = scene.isValid(e) ? scene.world().get<MeshRenderer>(e) : nullptr;
    if (!mr || !mr->graph) {
        ImGui::TextDisabled("Selecciona un objeto y pulsa \"Material por nodos\" en Detalles › Material.");
        ImGui::End();
        return;
    }
    MaterialGraph& g = *mr->graph;
    const float s = ImGui::GetFontSize() / 16.0f;

    // Barra: añadir nodo, quitar el grafo y estado de la compilación.
    if (ImGui::Button("+ Nodo")) ImGui::OpenPopup("##addnode");
    if (ImGui::BeginPopup("##addnode")) {
        for (int t = 1; t < int(MatNode::Count); ++t)
            if (ImGui::Selectable(matNodeInfo(MatNode(t)).name)) {
                pushUndo();
                g.add(MatNode(t), Vec2{-matGraphPan.x / s + 80, -matGraphPan.y / s + 60});
            }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Quitar el material por nodos")) {
        pushUndo();
        mr->graph.reset();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    const MatGraphProgram& prog = g.program();
    if (prog.ok()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1), "Compilado: %d operaciones (máx. %d)", prog.count, kMatGraphMaxInstructions);
    else ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "No se aplica: %s", prog.error.c_str());
    ImGui::TextDisabled("Arrastra de una salida (derecha) a una entrada (izquierda). Clic en una entrada conectada la suelta. "
                        "Botón derecho o central: mover el lienzo. Supr: borrar el nodo elegido.");

    ImGui::BeginChild("##lienzo", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c0 = ImGui::GetCursorScreenPos(), csize = ImGui::GetContentRegionAvail();
    const ImVec2 origin = add(c0, matGraphPan);
    dl->AddRectFilled(c0, add(c0, csize), IM_COL32(28, 30, 36, 255));
    const float gridStep = 32 * s;
    for (float x = std::fmod(matGraphPan.x, gridStep); x < csize.x; x += gridStep)
        dl->AddLine(ImVec2(c0.x + x, c0.y), ImVec2(c0.x + x, c0.y + csize.y), IM_COL32(45, 48, 56, 255));
    for (float y = std::fmod(matGraphPan.y, gridStep); y < csize.y; y += gridStep)
        dl->AddLine(ImVec2(c0.x, c0.y + y), ImVec2(c0.x + csize.x, c0.y + y), IM_COL32(45, 48, 56, 255));
    const bool canvasHovered = ImGui::IsWindowHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (canvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        matGraphPan.x += ImGui::GetIO().MouseDelta.x;
        matGraphPan.y += ImGui::GetIO().MouseDelta.y;
    }

    const float nodeW = 180 * s, headerH = 24 * s, rowH = 24 * s, pinR = 6 * s;
    auto nodePos = [&](const MatGraphNode& n) { return ImVec2(origin.x + n.pos.x * s, origin.y + n.pos.y * s); };
    auto nodeHeight = [&](const MatGraphNode& n) {
        int pc = 0;
        paramLabel(n.type, pc);
        const int rows = int(n.inputs.size()) + (pc ? 1 : 0) + (n.type == MatNode::Color || n.type == MatNode::Texture ? 1 : 0);
        return headerH + std::max(1, rows) * rowH + 6 * s;
    };
    auto outPin = [&](const MatGraphNode& n) { const ImVec2 p = nodePos(n); return ImVec2(p.x + nodeW, p.y + headerH + rowH * 0.5f); };
    auto inPin = [&](const MatGraphNode& n, int i) { const ImVec2 p = nodePos(n); return ImVec2(p.x, p.y + headerH + rowH * (i + 0.5f)); };

    // Conexiones.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(0);
    for (const MatGraphNode& n : g.nodes)
        for (size_t i = 0; i < n.inputs.size(); ++i)
            if (const MatGraphNode* src = g.find(n.inputs[i])) {
                const ImVec2 a = outPin(*src), b = inPin(n, int(i));
                const float dx = std::max(40 * s, std::fabs(b.x - a.x) * 0.5f);
                dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, IM_COL32(200, 200, 120, 255), 2.5f * s);
            }
    if (matGraphLinkFrom) {
        if (const MatGraphNode* src = g.find(matGraphLinkFrom)) {
            const ImVec2 a = outPin(*src);
            dl->AddBezierCubic(a, ImVec2(a.x + 60 * s, a.y), ImVec2(mouse.x - 60 * s, mouse.y), mouse, IM_COL32(255, 255, 160, 255), 2.5f * s);
        }
    }

    // Nodos.
    dl->ChannelsSetCurrent(1);
    int removeId = 0;
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    for (MatGraphNode& n : g.nodes) {
        ImGui::PushID(n.id);
        const ImVec2 p = nodePos(n), q(p.x + nodeW, p.y + nodeHeight(n));
        const bool sel = matGraphSelected == n.id;
        dl->AddRectFilled(p, q, IM_COL32(40, 42, 50, 240), 6 * s);
        dl->AddRectFilled(p, ImVec2(q.x, p.y + headerH), nodeColor(n.type), 6 * s, ImDrawFlags_RoundCornersTop);
        dl->AddRect(p, q, sel ? IM_COL32(255, 200, 80, 255) : IM_COL32(20, 20, 24, 255), 6 * s, 0, sel ? 2.5f * s : 1.0f);
        dl->AddText(ImVec2(p.x + 8 * s, p.y + 4 * s), IM_COL32(240, 240, 240, 255), matNodeInfo(n.type).name);
        // Cabecera: elegir y arrastrar.
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##cab", ImVec2(nodeW - 20 * s, headerH));
        if (ImGui::IsItemActivated()) {
            matGraphSelected = n.id;
            pushUndo();
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            n.pos.x += ImGui::GetIO().MouseDelta.x / s;
            n.pos.y += ImGui::GetIO().MouseDelta.y / s;
        }
        // Salida.
        if (matNodeInfo(n.type).hasOutput) {
            const ImVec2 o = outPin(n);
            dl->AddCircleFilled(o, pinR, IM_COL32(230, 230, 140, 255));
            if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dist2(mouse, o) < (pinR * 2) * (pinR * 2))
                matGraphLinkFrom = n.id;
        }
        // Entradas.
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            const ImVec2 ip = inPin(n, int(i));
            const bool linked = n.inputs[i] != 0;
            dl->AddCircleFilled(ip, pinR, linked ? IM_COL32(230, 230, 140, 255) : IM_COL32(110, 110, 120, 255));
            dl->AddText(ImVec2(ip.x + 12 * s, ip.y - 8 * s), IM_COL32(210, 210, 210, 255), matNodeInfo(n.type).inputs[i]);
            const bool over = dist2(mouse, ip) < (pinR * 2.2f) * (pinR * 2.2f);
            if (over && released && matGraphLinkFrom) {
                pushUndo();
                if (!g.connect(matGraphLinkFrom, n.id, int(i))) setStatus("Esa conexión haría un ciclo");
                matGraphLinkFrom = 0;
            } else if (over && linked && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                pushUndo();
                matGraphLinkFrom = n.inputs[i]; // se suelta y se puede llevar a otra entrada
                g.disconnect(n.id, int(i));
            }
        }
        // Parámetros dentro del nodo.
        float y = p.y + headerH + rowH * float(n.inputs.size());
        const float fieldW = nodeW - 16 * s;
        int pc = 0;
        if (const char* label = paramLabel(n.type, pc)) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 8 * s, y + 2 * s));
            ImGui::SetNextItemWidth(fieldW);
            float* v = &n.value.r;
            const bool ch = pc == 2 ? ImGui::DragFloat2((std::string("##") + label).c_str(), v, 0.01f, -100, 100, "%.2f")
                                    : ImGui::DragFloat((std::string("##") + label).c_str(), v, 0.01f, -100, 100, (std::string(label) + " %.2f").c_str());
            if (ImGui::IsItemActivated()) pushUndo();
            if (ch) g.touch();
            y += rowH;
        }
        if (n.type == MatNode::Color) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 8 * s, y + 2 * s));
            ImGui::SetNextItemWidth(fieldW);
            if (ImGui::ColorEdit4("##c", &n.value.r, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) g.touch();
            if (ImGui::IsItemActivated()) pushUndo();
        }
        if (n.type == MatNode::Texture) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + 8 * s, y + 2 * s));
            ImGui::SetNextItemWidth(fieldW);
            AssetRef ref = n.texture;
            if (assetCombo("##tex", AssetType::Texture, ref) && ref != n.texture) {
                pushUndo();
                n.texture = ref;
                g.touch();
            }
        }
        if (sel && n.type != MatNode::Output && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) removeId = n.id;
        ImGui::PopID();
    }
    dl->ChannelsMerge();
    if (released) matGraphLinkFrom = 0; // soltado fuera de una entrada
    if (removeId) {
        pushUndo();
        g.remove(removeId);
        matGraphSelected = 0;
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace titan
