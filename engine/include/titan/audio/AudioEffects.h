#pragma once
// Efectos de audio para los buses del mezclador: reverberación, eco, filtros, compresor, limitador y distorsión.
//
// El procesado de señal está portado de Godot Engine (servers/audio/effects y audio_filter_sw), licencia MIT:
//   Copyright (c) 2014-present Godot Engine contributors. Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.
// Se ha adaptado a Titán (sin AudioServer ni Ref<>; parámetros por nombre; búferes estéreo intercalados).
// Texto completo de la licencia en docs/TITAN_ENGINE_DEPENDENCIES.md.
#include <memory>
#include <string>
#include <vector>

namespace titan {

enum class AudioEffectType { Reverb, Delay, LowPass, HighPass, BandPass, Compressor, Limiter, Distortion };

// Nombre en los archivos y en Lua: "reverb", "delay", "lowpass", "highpass", "bandpass", "compressor", "limiter",
// "distortion". Devuelve false si el nombre no existe.
const char* audioEffectName(AudioEffectType t);
bool audioEffectFromName(const std::string& name, AudioEffectType& out);

struct AudioEffectParamInfo {
    const char* name;
    float min, max, defaultValue;
    const char* label; // para el editor
};

class AudioEffect {
public:
    virtual ~AudioEffect() = default;
    AudioEffectType type() const { return type_; }
    // in y out pueden ser el mismo búfer. frames estéreo intercalados (L, R, L, R...).
    virtual void process(const float* in, float* out, int frames) = 0;
    // Vacía colas de reverberación y eco (al cargar otra escena).
    virtual void reset() = 0;
    bool setParam(const std::string& name, float value); // false si el parámetro no existe
    bool getParam(const std::string& name, float& value) const;
    const std::vector<AudioEffectParamInfo>& params() const { return *info_; }
    const std::vector<float>& values() const { return values_; }

    static std::unique_ptr<AudioEffect> create(AudioEffectType type, float sampleRate);

protected:
    AudioEffect(AudioEffectType t, const std::vector<AudioEffectParamInfo>* info, float sampleRate);
    virtual void changed() {} // un parámetro cambió (recalcular coeficientes)
    float v(int i) const { return values_[static_cast<size_t>(i)]; }
    float sampleRate_;

private:
    AudioEffectType type_;
    const std::vector<AudioEffectParamInfo>* info_;
    std::vector<float> values_;
};

} // namespace titan
