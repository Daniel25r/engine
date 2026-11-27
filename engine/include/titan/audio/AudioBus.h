#pragma once
// Disposición de buses del mezclador (como el "Audio Bus Layout" de Godot): cada bus tiene volumen, silencio,
// una cadena de efectos y un bus de destino. Master siempre existe y va a los altavoces. Se guarda en el proyecto
// como "audio_buses.json"; los proyectos que no lo tienen usan solo Master (suenan igual que antes de 0.14).
#include <string>
#include <utility>
#include <vector>

#include "titan/audio/AudioEffects.h"

namespace titan {

struct AudioBusEffectSettings {
    AudioEffectType type = AudioEffectType::Reverb;
    bool enabled = true;
    std::vector<std::pair<std::string, float>> params; // solo los que cambian respecto al valor por defecto
};

struct AudioBusSettings {
    std::string name = "Master";
    std::string send = "Master"; // bus al que va (Master: a los altavoces)
    float volumeDb = 0;
    bool mute = false;
    bool bypassEffects = false;
    std::vector<AudioBusEffectSettings> effects;
};

struct AudioBusLayout {
    std::vector<AudioBusSettings> buses; // el primero es siempre Master (si falta, se añade)

    // Master + "Música" + "Efectos" (un punto de partida habitual).
    static AudioBusLayout defaultLayout();
    // Corrige nombres repetidos o vacíos, destinos que no existen y ciclos (van a Master), y pone Master el primero.
    void normalize();
    int find(const std::string& name) const;

    std::string toJson() const;
    // false si el texto no es una disposición válida (error describe por qué); la disposición queda normalizada.
    static bool fromJson(const std::string& text, AudioBusLayout& out, std::string* error = nullptr);
};

} // namespace titan
