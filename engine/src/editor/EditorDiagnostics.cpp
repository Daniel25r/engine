// Prueba de sonido y diagnóstico del dispositivo: lo que hay que mirar cuando algo no funciona en un teléfono.
#include <algorithm>

#include "EditorImpl.h"
#include "imgui.h"
#include "titan/core/Version.h"

namespace titan {

void Editor::Impl::startSoundCheck() {
    static bool filesReady = false;
    if (!filesReady) {
        addSoundCheckFiles(soundCheckFiles);
        filesReady = true;
    }
    soundCheckAudio = std::make_unique<AudioEngine>();
    if (!soundCheckAudio->init(soundCheckFiles, true)) {
        soundCheckDevice = "No se pudo abrir la salida de audio del dispositivo (ver Registro)";
        soundCheckAudio.reset();
        showDiagnostics = true;
        return;
    }
    soundCheckDevice = soundCheckAudio->deviceInfo();
    soundCheckRealOutput = soundCheckAudio->hasRealOutput();
    TITAN_INFO("audio", "Prueba de sonido en: %s", soundCheckDevice.c_str());
    soundCheck.start(*soundCheckAudio);
    soundCheckTime = 0;
    soundCheckRunning = true;
}

void Editor::Impl::updateSoundCheck(float dt) {
    if (!soundCheckRunning || !soundCheckAudio) return;
    soundCheckTime += dt;
    soundCheckAudio->update([](Entity, Vec3&) { return false; });
    if (!soundCheck.update(*soundCheckAudio, soundCheckTime) && soundCheckTime > 7.5f) {
        soundCheckRunning = false;
        soundCheckAudio.reset(); // libera el dispositivo de audio
        showDiagnostics = true;
    }
}

void Editor::Impl::drawDiagnostics() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.2f, vp->Pos.y + vp->Size.y * 0.15f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.6f, vp->Size.y * 0.7f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin("Diagnóstico###diag", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    noteFloatingWindow();
    if (!open) {
        showDiagnostics = false;
        if (soundCheckRunning) {
            soundCheckRunning = false;
            soundCheckAudio.reset();
        }
    }
    ImGui::TextUnformatted("Sonido");
    ImGui::Separator();
    if (soundCheckRunning) {
        ImGui::Text("Sonando: %s", soundCheck.stepName());
        ImGui::ProgressBar(std::min(1.0f, soundCheckTime / 7.0f), ImVec2(-1, 0));
    } else if (ImGui::Button("Reproducir prueba de sonido (7 s)", ImVec2(-1, 0))) {
        startSoundCheck();
    }
    ImGui::TextWrapped("Salida: %s", soundCheckDevice.empty() ? "(aún no se ha probado)" : soundCheckDevice.c_str());
    if (!soundCheckDevice.empty() && !soundCheckRealOutput)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "No hay salida de audio real: este aparato no abrió ningún altavoz. "
                                                    "Así no puede sonar nada (no es un fallo de tu juego).");
    ImGui::TextWrapped("Deberías oír: un tono continuo, una melodía de 8 notas que sube y 6 pasos que van de la "
                       "izquierda a la derecha. Si no oyes nada: sube el volumen multimedia (no el del timbre), quita "
                       "el modo silencio y prueba con auriculares. Anota en docs/VALIDACION_ANDROID.md lo que oyes.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Dispositivo");
    ImGui::Separator();
    const DeviceCaps& c = renderer.caps();
    ImGui::TextWrapped("GPU: %s (%s)", c.renderer.c_str(), c.vendor.c_str());
    ImGui::TextWrapped("OpenGL: %s · textura máx. %d · ASTC %s", c.version.c_str(), c.maxTextureSize, c.astc ? "sí" : "no");
    ImGui::Text("Memoria: %d MB · calidad actual: %s (recomendada: %s)", c.memoryMB, qualityName(renderer.quality().level),
                qualityName(recommendedQuality(c)));
    ImGui::Text("Motor: %s %s", kEngineName, kEngineVersion);
    if (!platformLogPath.empty()) ImGui::TextWrapped("Registro guardado en: %s", platformLogPath.c_str());
    if (ImGui::Button("Copiar informe (dispositivo + últimos 200 mensajes)", ImVec2(-1, 0))) {
        std::string report = std::string(kEngineName) + " " + kEngineVersion + "\nGPU: " + c.renderer + " (" + c.vendor +
                             ")\nOpenGL: " + c.version + "\nMemoria: " + std::to_string(c.memoryMB) +
                             " MB\nAudio: " + (soundCheckDevice.empty() ? "sin probar" : soundCheckDevice) + "\n\n";
        const auto recent = Log::recent();
        static const char* levels[] = {"info ", "AVISO", "ERROR"};
        for (size_t i = recent.size() > 200 ? recent.size() - 200 : 0; i < recent.size(); ++i) {
            const LogMessage& m = recent[i];
            char line[64];
            std::snprintf(line, sizeof(line), "[%8.2f] %s [", m.seconds, levels[static_cast<int>(m.level)]);
            report += line + m.category + "] " + m.text + "\n";
        }
        platform.setClipboard(report);
        setStatus("Informe copiado: pégalo en un mensaje");
    }
    ImGui::End();
}

} // namespace titan
