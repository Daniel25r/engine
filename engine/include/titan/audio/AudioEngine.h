#pragma once
// Audio de Titán sobre miniaudio (dominio público / MIT-0): mezclador, sonidos 2D y 3D, bucles, volumen y tono.
// Los clips se decodifican una vez a memoria (float, 2 canales) desde el FileSource del juego (APK o carpeta).
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "titan/core/FileSource.h"
#include "titan/core/Math.h"
#include "titan/core/World.h"

namespace titan {

struct PlayParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool spatial = false;
    Vec3 position;
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    Entity owner; // la voz sigue a esta entidad (o se para si se destruye)
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // withDevice = false: mezcla sin tarjeta de sonido (pruebas). Devuelve false si no hay audio disponible.
    bool init(FileSource& files, bool withDevice = true);
    void shutdown();
    bool ready() const;

    // Devuelve un identificador de voz (0 si falla). path es relativo al FileSource ("assets/sonidos/x.wav").
    uint32_t play(const std::string& path, const PlayParams& params);
    void stop(uint32_t voice);
    void stopOwner(Entity owner);
    void stopAll();
    bool isPlaying(uint32_t voice) const;
    void setVoicePosition(uint32_t voice, const Vec3& p);
    void setListener(const Vec3& position, const Vec3& forward, const Vec3& up);
    void setMasterVolume(float v);
    void setPaused(bool paused);
    // Limpia las voces terminadas; mueve las que siguen a entidades (lo llama el runtime cada fotograma).
    template <class PosFn>
    void update(PosFn&& positionOf) {
        updateImpl([&](Entity e, Vec3& out) { return positionOf(e, out); });
    }
    size_t activeVoices() const;
    // Solo pruebas sin dispositivo: mezcla N fotogramas y devuelve la amplitud máxima.
    float renderForTest(int frames);
    // Sin dispositivo: mezcla N fotogramas estéreo intercalados (L, R, L, R...) — para analizar o guardar en WAV.
    void render(int frames, std::vector<float>& interleaved);
    int sampleRate() const;
    // Diagnóstico: sistema de audio (AAudio, OpenSL ES, ALSA...), dispositivo, frecuencia y canales.
    std::string deviceInfo() const;
    // false si no hay salida real: sin dispositivo, o miniaudio usa su "dispositivo nulo" (no suena nada).
    bool hasRealOutput() const;

    struct Impl;

private:
    void updateImpl(const std::function<bool(Entity, Vec3&)>& positionOf);
    std::unique_ptr<Impl> impl_;
};

} // namespace titan
