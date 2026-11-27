// Mezclador de audio del editor: una tira por bus (volumen, silencio, efectos, destino y medidor de nivel).
// Guarda en assets/audio_buses.json, que el juego carga al empezar (también exportado). Durante Jugar los cambios
// se aplican al mezclador real al momento y los medidores muestran lo que está sonando.
#include <algorithm>
#include <cmath>
#include <map>

#include "EditorImpl.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "titan/core/FileIO.h"

namespace titan {

namespace {

const char* effectLabel(AudioEffectType t) {
    switch (t) {
    case AudioEffectType::Reverb: return "Reverberación";
    case AudioEffectType::Delay: return "Eco";
    case AudioEffectType::LowPass: return "Paso bajo";
    case AudioEffectType::HighPass: return "Paso alto";
    case AudioEffectType::BandPass: return "Paso banda";
    case AudioEffectType::Compressor: return "Compresor";
    case AudioEffectType::Limiter: return "Limitador";
    case AudioEffectType::Distortion: return "Distorsión";
    }
    return "?";
}

// Parámetros de cada tipo de efecto (nombre, rango, valor por defecto), leídos de una instancia de verdad.
const std::vector<AudioEffectParamInfo>& paramsOf(AudioEffectType t) {
    static std::map<AudioEffectType, std::unique_ptr<AudioEffect>> cache;
    auto& e = cache[t];
    if (!e) e = AudioEffect::create(t, 48000);
    return e->params();
}

float paramValue(const AudioBusEffectSettings& fx, const AudioEffectParamInfo& info) {
    for (const auto& [k, v] : fx.params)
        if (k == info.name) return v;
    return info.defaultValue;
}

void setParamValue(AudioBusEffectSettings& fx, const AudioEffectParamInfo& info, float value) {
    for (auto& [k, v] : fx.params)
        if (k == info.name) {
            v = value;
            return;
        }
    fx.params.emplace_back(info.name, value);
}

std::string layoutPath(const Project& p) { return p.assetsDir() + "/audio_buses.json"; }

} // namespace

void Editor::Impl::loadMixer() {
    mixerLayout = AudioBusLayout{};
    mixerLayout.normalize();
    mixerProject = project ? project->root() : std::string();
    if (!project) return;
    std::string text, error;
    if (fileio::readText(layoutPath(*project), text) && !AudioBusLayout::fromJson(text, mixerLayout, &error)) {
        TITAN_ERROR("editor", "audio_buses.json: %s (se muestra solo Master; al guardar se reemplaza)", error.c_str());
        mixerLayout = AudioBusLayout{};
        mixerLayout.normalize();
    }
}

void Editor::Impl::saveMixer() {
    if (!project) return;
    mixerLayout.normalize();
    std::string err;
    if (!fileio::writeAtomic(layoutPath(*project), mixerLayout.toJson(), true, &err)) {
        TITAN_ERROR("editor", "No se pudo guardar el mezclador: %s", err.c_str());
        return;
    }
    if (!project->assets().findByPath("audio_buses.json")) project->assets().import("audio_buses.json");
    setStatus("Mezclador guardado");
}

// Durante Jugar: lleva la disposición del editor al mezclador que suena. Los cambios de estructura (buses,
// destinos, efectos añadidos o quitados) rehacen los buses y paran los sonidos; los de valores no cortan nada.
void Editor::Impl::applyMixerLive(bool structural) {
    if (!runtime) return;
    AudioEngine& a = runtime->audio();
    if (!a.ready()) return;
    if (structural) {
        a.setBusLayout(mixerLayout);
        return;
    }
    for (const AudioBusSettings& b : mixerLayout.buses) {
        a.setBusVolumeDb(b.name, b.volumeDb);
        a.setBusMute(b.name, b.mute);
        a.setBusBypass(b.name, b.bypassEffects);
        for (size_t i = 0; i < b.effects.size(); ++i) {
            a.setBusEffectEnabled(b.name, int(i), b.effects[i].enabled);
            for (const AudioEffectParamInfo& info : paramsOf(b.effects[i].type))
                a.setBusEffectParam(b.name, int(i), info.name, paramValue(b.effects[i], info));
        }
    }
}

void Editor::Impl::drawMixer() {
    if (!project) return;
    if (mixerProject != project->root()) loadMixer();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.1f, vp->Pos.y + vp->Size.y * 0.12f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.8f, vp->Size.y * 0.76f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Mezclador de audio###mixer", &showMixer, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    bool changed = false, structural = false, commit = false;
    if (ImGui::Button("+ Bus")) {
        AudioBusSettings b;
        b.name = "Bus";
        mixerLayout.buses.push_back(b);
        mixerLayout.normalize();
        structural = commit = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restaurar (Master, Música, Efectos)")) {
        mixerLayout = AudioBusLayout::defaultLayout();
        structural = commit = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(runtime ? "Jugando: los cambios suenan al momento" : "Se guarda en assets/audio_buses.json");

    // Niveles del juego en marcha (pico con caída de 24 dB/s, como un medidor de mesa de mezclas).
    mixerMeters.resize(mixerLayout.buses.size(), {-80.0f, -80.0f});
    const float dt = ImGui::GetIO().DeltaTime;
    for (size_t i = 0; i < mixerLayout.buses.size(); ++i) {
        AudioBusLevel lv;
        const bool live = runtime && runtime->audio().readBusLevel(mixerLayout.buses[i].name, lv);
        const float in[2] = {lv.peakLeft, lv.peakRight};
        for (int c = 0; c < 2; ++c) {
            const float db = live && in[c] > 1e-4f ? 20.0f * std::log10(in[c]) : -80.0f;
            mixerMeters[i][size_t(c)] = std::max(db, mixerMeters[i][size_t(c)] - 24.0f * dt);
        }
    }

    const float stripW = ImGui::GetFontSize() * 15;
    ImGui::BeginChild("tiras", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    int removeBus = -1;
    for (size_t bi = 0; bi < mixerLayout.buses.size(); ++bi) {
        AudioBusSettings& b = mixerLayout.buses[bi];
        ImGui::PushID(int(bi));
        if (bi > 0) ImGui::SameLine();
        ImGui::BeginChild("tira", ImVec2(stripW, 0), ImGuiChildFlags_Borders);
        // Nombre (Master no se renombra) y destino.
        ImGui::SetNextItemWidth(-1);
        if (bi == 0) {
            ImGui::TextUnformatted("Master");
        } else {
            std::string name = b.name;
            if (ImGui::InputText("##nombre", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty()) {
                // Las fuentes que usaban el nombre viejo irían a Master: se avisa en el registro.
                for (AudioBusSettings& o : mixerLayout.buses)
                    if (o.send == b.name) o.send = name;
                TITAN_INFO("editor", "Bus '%s' renombrado a '%s' (revisa los AudioSource y scripts que lo usan)",
                           b.name.c_str(), name.c_str());
                b.name = name;
                mixerLayout.normalize();
                structural = commit = true;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##destino", ("Envía a " + b.send).c_str())) {
                for (const AudioBusSettings& o : mixerLayout.buses) {
                    if (o.name == b.name) continue;
                    if (ImGui::Selectable(o.name.c_str(), o.name == b.send)) {
                        b.send = o.name;
                        mixerLayout.normalize(); // si crea un ciclo, vuelve a Master
                        structural = commit = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        // Medidor y volumen.
        const float h = ImGui::GetFontSize() * 9;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float mw = ImGui::GetFontSize() * 0.6f;
        for (int c = 0; c < 2; ++c) {
            const float db = mixerMeters[bi][size_t(c)];
            const float f = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f); // -60..+6 dB
            const ImVec2 a(p.x + c * (mw + 2), p.y), z(a.x + mw, p.y + h);
            dl->AddRectFilled(a, z, IM_COL32(30, 32, 38, 255));
            const ImU32 col = db > -0.5f ? IM_COL32(230, 70, 60, 255) : db > -12 ? IM_COL32(230, 200, 60, 255)
                                                                              : IM_COL32(80, 200, 110, 255);
            dl->AddRectFilled(ImVec2(a.x, z.y - h * f), z, col);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + mw * 2 + 8, p.y));
        if (ImGui::VSliderFloat("##vol", ImVec2(ImGui::GetFontSize() * 2.4f, h), &b.volumeDb, -60, 6, "%.1f\ndB")) changed = true;
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { // doble toque: 0 dB
            b.volumeDb = 0;
            changed = commit = true;
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Checkbox("Silencio", &b.mute)) changed = commit = true;
        if (ImGui::Checkbox("Sin efectos", &b.bypassEffects)) changed = commit = true;
        if (bi > 0 && ImGui::Button("Eliminar bus")) removeBus = int(bi);
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));

        // Cadena de efectos (en orden de procesado).
        ImGui::SeparatorText("Efectos");
        int removeFx = -1;
        for (size_t fi = 0; fi < b.effects.size(); ++fi) {
            AudioBusEffectSettings& fx = b.effects[fi];
            ImGui::PushID(int(fi));
            if (ImGui::Checkbox("##on", &fx.enabled)) changed = commit = true;
            ImGui::SameLine();
            const bool open = ImGui::TreeNodeEx("fx", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", effectLabel(fx.type));
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight()));
            if (ImGui::SmallButton("x")) removeFx = int(fi);
            if (open) {
                for (const AudioEffectParamInfo& info : paramsOf(fx.type)) {
                    float v = paramValue(fx, info);
                    ImGui::TextDisabled("%s", info.label);
                    ImGui::SetNextItemWidth(-1);
                    const std::string id = std::string("##") + info.name;
                    const bool isSwitch = std::string(info.name).find("_active") != std::string::npos;
                    const bool isStep = std::string(info.name) == "db" || std::string(info.name) == "mode";
                    bool edited = false;
                    if (isSwitch) {
                        bool on = v > 0.5f;
                        if (ImGui::Checkbox(id.c_str(), &on)) {
                            v = on ? 1.0f : 0.0f;
                            edited = true;
                            commit = true;
                        }
                    } else if (isStep) {
                        int iv = int(v);
                        edited = ImGui::SliderInt(id.c_str(), &iv, int(info.min), int(info.max));
                        v = float(iv);
                    } else {
                        edited = ImGui::SliderFloat(id.c_str(), &v, info.min, info.max, "%.3g");
                    }
                    commit |= ImGui::IsItemDeactivatedAfterEdit();
                    if (edited) {
                        setParamValue(fx, info, v);
                        changed = true;
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeFx >= 0) {
            b.effects.erase(b.effects.begin() + removeFx);
            structural = commit = true;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##addfx", "+ Efecto")) {
            for (int t = 0; t < 8; ++t) {
                const AudioEffectType type = static_cast<AudioEffectType>(t);
                if (ImGui::Selectable(effectLabel(type))) {
                    AudioBusEffectSettings fx;
                    fx.type = type;
                    b.effects.push_back(fx);
                    structural = commit = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    if (removeBus > 0) {
        mixerLayout.buses.erase(mixerLayout.buses.begin() + removeBus);
        mixerLayout.normalize(); // lo que iba a ese bus pasa a Master
        mixerMeters.clear();
        structural = commit = true;
    }
    ImGui::EndChild();
    ImGui::End();

    if (changed || structural) applyMixerLive(structural);
    if (commit) saveMixer(); // al soltar el control, no en cada fotograma del arrastre
}

} // namespace titan
