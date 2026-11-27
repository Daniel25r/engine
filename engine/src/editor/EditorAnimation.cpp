// Editor de máquinas de estados de animación: parámetros, estados (clip o mezcla 1D, eventos) y transiciones
// con condiciones. Formularios en vez de un lienzo de nodos: se manejan bien con el dedo en un teléfono.
#include <algorithm>
#include <cmath>

#include "EditorImpl.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace titan {

namespace {
const char* kParamTypes = "Decimal\0Entero\0Sí/no\0Disparo\0";
const char* kOps = ">\0<\0=\0!=\0verdadero\0falso\0";
} // namespace

void Editor::Impl::drawMachineEditor() {
    Entity e = scene.findById(machineEditorFor);
    Animator* an = e.isNull() ? nullptr : scene.world().get<Animator>(e);
    if (!an) {
        showMachineEditor = false;
        return;
    }
    AnimStateMachine& m = an->machine;
    const MeshRenderer* mr = scene.world().get<MeshRenderer>(e);
    const int model = mr ? editorModel(mr->model) : -1;
    std::vector<std::string> clips;
    for (int i = 0; i < renderer.modelAnimationCount(model); ++i) clips.push_back(renderer.modelAnimationName(model, i));

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.08f, vp->Pos.y + vp->Size.y * 0.08f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.84f, vp->Size.y * 0.84f), ImGuiCond_Appearing);
    const std::string title = "Máquina de estados · " + scene.info(e).name + "###maquina";
    if (!ImGui::Begin(title.c_str(), &showMachineEditor, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    noteFloatingWindow();
    auto clipCombo = [&](const char* id, std::string& clip) {
        if (ImGui::BeginCombo(id, clip.empty() ? "(ninguno)" : clip.c_str())) {
            for (const std::string& c : clips)
                if (ImGui::Selectable(c.c_str(), c == clip)) {
                    pushUndo();
                    clip = c;
                }
            ImGui::EndCombo();
        }
    };
    auto paramCombo = [&](const char* id, std::string& name) {
        if (ImGui::BeginCombo(id, name.empty() ? "(parámetro)" : name.c_str())) {
            for (const AnimParam& p : m.params)
                if (ImGui::Selectable(p.name.c_str(), p.name == name)) {
                    pushUndo();
                    name = p.name;
                }
            ImGui::EndCombo();
        }
    };
    auto stateCombo = [&](const char* id, std::string& name, bool allowAny) {
        if (ImGui::BeginCombo(id, name == "*" ? "(cualquiera)" : name.empty() ? "(estado)" : name.c_str())) {
            if (allowAny && ImGui::Selectable("(cualquiera)", name == "*")) {
                pushUndo();
                name = "*";
            }
            for (const AnimState& s : m.states)
                if (ImGui::Selectable(s.name.c_str(), s.name == name)) {
                    pushUndo();
                    name = s.name;
                }
            ImGui::EndCombo();
        }
    };
    if (clips.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "El objeto no tiene un modelo con animaciones: los clips se escriben a mano.");
    ImGui::TextDisabled("Estado actual: %s · Los scripts cambian los parámetros con e:setAnimParam / e:setAnimTrigger",
                        m.current.empty() ? "-" : m.current.c_str());

    // ---- Parámetros
    if (ImGui::CollapsingHeader("Parámetros", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < m.params.size(); ++i) {
            AnimParam& p = m.params[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
            ImGui::InputText("##pn", &p.name);
            track();
            ImGui::SameLine();
            int type = static_cast<int>(p.type);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
            if (ImGui::Combo("##pt", &type, kParamTypes)) {
                pushUndo();
                p.type = static_cast<AnimParam::Type>(type);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            if (p.type == AnimParam::Type::Bool || p.type == AnimParam::Type::Trigger) {
                bool v = p.value != 0;
                if (ImGui::Checkbox("##pv", &v)) p.value = v ? 1.0f : 0.0f;
            } else {
                ImGui::DragFloat("##pv", &p.value, 0.05f);
                track();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Quitar")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) {
            pushUndo();
            m.params.erase(m.params.begin() + remove);
        }
        if (ImGui::Button("+ Parámetro")) {
            pushUndo();
            m.params.push_back({"param" + std::to_string(m.params.size() + 1), AnimParam::Type::Float, 0});
        }
    }

    // ---- Estados
    if (ImGui::CollapsingHeader("Estados (el primero es el de entrada)", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1, up = -1;
        for (size_t i = 0; i < m.states.size(); ++i) {
            AnimState& s = m.states[i];
            ImGui::PushID(static_cast<int>(1000 + i));
            const bool open = ImGui::TreeNodeEx("##st", ImGuiTreeNodeFlags_DefaultOpen, "%s%s", s.name.c_str(),
                                                s.name == m.current ? "  (actual)" : "");
            ImGui::SameLine();
            if (i > 0 && ImGui::SmallButton("Subir")) up = static_cast<int>(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Quitar")) remove = static_cast<int>(i);
            if (open) {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                const std::string before = s.name;
                ImGui::InputText("Nombre", &s.name);
                track();
                if (ImGui::IsItemDeactivatedAfterEdit() && before != s.name) // renombrar también en las transiciones
                    for (AnimTransition& t : m.transitions) {
                        if (t.from == before) t.from = s.name;
                        if (t.to == before) t.to = s.name;
                    }
                bool blend = !s.blend.empty();
                if (ImGui::Checkbox("Mezcla 1D (blend tree)", &blend)) {
                    pushUndo();
                    if (blend) s.blend = {{0, s.clip}, {1, ""}};
                    else s.blend.clear();
                }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                if (!blend) {
                    clipCombo("Clip", s.clip);
                } else {
                    paramCombo("Parámetro", s.blendParam);
                    int removePoint = -1;
                    for (size_t k = 0; k < s.blend.size(); ++k) {
                        ImGui::PushID(static_cast<int>(k));
                        ImGui::SetNextItemWidth(80 * scale);
                        ImGui::DragFloat("##th", &s.blend[k].threshold, 0.05f);
                        track();
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                        clipCombo("##bc", s.blend[k].clip);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) removePoint = static_cast<int>(k);
                        ImGui::PopID();
                    }
                    if (removePoint >= 0) {
                        pushUndo();
                        s.blend.erase(s.blend.begin() + removePoint);
                    }
                    if (ImGui::SmallButton("+ Punto")) {
                        pushUndo();
                        s.blend.push_back({s.blend.empty() ? 0 : s.blend.back().threshold + 1, ""});
                    }
                }
                ImGui::SetNextItemWidth(120 * scale);
                ImGui::DragFloat("Velocidad", &s.speed, 0.01f, -4, 4, "%.2fx");
                track();
                ImGui::SameLine();
                bool loop = s.loop;
                if (ImGui::Checkbox("Repetir", &loop)) {
                    pushUndo();
                    s.loop = loop;
                }
                int removeEvent = -1;
                for (size_t k = 0; k < s.events.size(); ++k) {
                    ImGui::PushID(static_cast<int>(5000 + k));
                    ImGui::SetNextItemWidth(100 * scale);
                    ImGui::SliderFloat("##et", &s.events[k].time, 0, 1, "%.2f");
                    track();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputText("##en", &s.events[k].name);
                    track();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) removeEvent = static_cast<int>(k);
                    ImGui::PopID();
                }
                if (removeEvent >= 0) {
                    pushUndo();
                    s.events.erase(s.events.begin() + removeEvent);
                }
                if (ImGui::SmallButton("+ Evento (onAnimationEvent)")) {
                    pushUndo();
                    s.events.push_back({0.5f, "evento"});
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (up > 0) {
            pushUndo();
            std::swap(m.states[up], m.states[up - 1]);
        }
        if (remove >= 0) {
            pushUndo();
            m.states.erase(m.states.begin() + remove);
        }
        if (ImGui::Button("+ Estado")) {
            pushUndo();
            AnimState s;
            s.name = "Estado" + std::to_string(m.states.size() + 1);
            if (!clips.empty()) s.clip = clips[std::min(m.states.size(), clips.size() - 1)];
            m.states.push_back(s);
        }
    }

    // ---- Transiciones
    if (ImGui::CollapsingHeader("Transiciones", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < m.transitions.size(); ++i) {
            AnimTransition& t = m.transitions[i];
            ImGui::PushID(static_cast<int>(2000 + i));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
            stateCombo("##from", t.from, true);
            ImGui::SameLine();
            ImGui::TextUnformatted("->");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            stateCombo("##to", t.to, false);
            ImGui::SameLine();
            if (ImGui::SmallButton("Quitar")) remove = static_cast<int>(i);
            ImGui::Indent();
            ImGui::SetNextItemWidth(100 * scale);
            ImGui::DragFloat("Mezcla", &t.duration, 0.01f, 0, 3, "%.2f s");
            track();
            ImGui::SameLine();
            bool exit = t.hasExitTime;
            if (ImGui::Checkbox("Esperar al clip", &exit)) {
                pushUndo();
                t.hasExitTime = exit;
            }
            if (t.hasExitTime) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100 * scale);
                ImGui::SliderFloat("##exit", &t.exitTime, 0, 1, "%.2f");
                track();
            }
            int removeCond = -1;
            for (size_t k = 0; k < t.conditions.size(); ++k) {
                AnimCondition& c = t.conditions[k];
                ImGui::PushID(static_cast<int>(k));
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
                paramCombo("##cp", c.param);
                ImGui::SameLine();
                int op = static_cast<int>(c.op);
                ImGui::SetNextItemWidth(110 * scale);
                if (ImGui::Combo("##op", &op, kOps)) {
                    pushUndo();
                    c.op = static_cast<AnimCondition::Op>(op);
                }
                if (c.op != AnimCondition::Op::IsTrue && c.op != AnimCondition::Op::IsFalse) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90 * scale);
                    ImGui::DragFloat("##cv", &c.value, 0.05f);
                    track();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeCond = static_cast<int>(k);
                ImGui::PopID();
            }
            if (removeCond >= 0) {
                pushUndo();
                t.conditions.erase(t.conditions.begin() + removeCond);
            }
            if (ImGui::SmallButton("+ Condición")) {
                pushUndo();
                t.conditions.push_back({m.params.empty() ? "" : m.params.front().name, AnimCondition::Op::Greater, 0});
            }
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0) {
            pushUndo();
            m.transitions.erase(m.transitions.begin() + remove);
        }
        if (ImGui::Button("+ Transición") && !m.states.empty()) {
            pushUndo();
            m.transitions.push_back({m.states.front().name, m.states.back().name, {}, 0.2f, false, 1});
        }
    }
    ImGui::End();
}

} // namespace titan
