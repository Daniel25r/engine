#pragma once
// Prueba de sonido: sonidos de referencia generados por el motor (sin archivos) para comprobar de oído que el
// audio funciona en un dispositivo: un tono de 440 Hz, una melodía corta y pasos que cruzan de izquierda a derecha.
#include <cstdint>
#include <vector>

#include "titan/audio/AudioEngine.h"
#include "titan/core/FileSource.h"

namespace titan {

// WAV PCM 16 bits mono con un tono (envolvente suave para que no haga "clic").
std::vector<uint8_t> makeToneWav(float frequency, float seconds, int sampleRate = 44100, float amplitude = 0.5f);
// Melodía de 8 notas (do mayor), ~2,4 s.
std::vector<uint8_t> makeMelodyWav(int sampleRate = 44100);
// Golpe corto (para el sonido espacial).
std::vector<uint8_t> makeClickWav(int sampleRate = 44100);

// Añade "diag/tono.wav", "diag/melodia.wav" y "diag/paso.wav" a la fuente de archivos.
void addSoundCheckFiles(MemoryFileSource& files);

// Secuencia de la prueba: 0 = tono (1 s), 1 = melodía (2,4 s), 2 = pasos de izquierda a derecha (3 s).
// Llamar cada fotograma con el tiempo transcurrido desde el inicio; devuelve false cuando ha terminado.
class SoundCheck {
public:
    void start(AudioEngine& audio);
    bool update(AudioEngine& audio, float elapsed);
    const char* stepName() const;

private:
    int step_ = -1;
    int lastStep_ = 0;
    uint32_t steps_ = 0;
};

} // namespace titan
