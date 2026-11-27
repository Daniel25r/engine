#include "titan/audio/SoundCheck.h"

#include <cmath>
#include <cstring>

namespace titan {

namespace {
std::vector<uint8_t> wavFromSamples(const std::vector<float>& s, int rate) {
    const uint32_t n = static_cast<uint32_t>(s.size());
    std::vector<uint8_t> w(44 + n * 2);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&w[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&w[at], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4);
    put32(4, 36 + n * 2);
    std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, static_cast<uint32_t>(rate));
    put32(28, static_cast<uint32_t>(rate) * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(&w[36], "data", 4);
    put32(40, n * 2);
    for (uint32_t i = 0; i < n; ++i) {
        const float v = std::fmax(-1.0f, std::fmin(1.0f, s[i]));
        put16(44 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(v * 32767.0f)));
    }
    return w;
}
float envelope(float t, float len) { // ataque y caída de 10 ms
    return std::fmin(1.0f, std::fmin(t / 0.01f, (len - t) / 0.01f));
}
} // namespace

std::vector<uint8_t> makeToneWav(float f, float seconds, int rate, float amp) {
    std::vector<float> s(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < s.size(); ++i) {
        const float t = float(i) / rate;
        s[i] = amp * std::sin(6.2831853f * f * t) * envelope(t, seconds);
    }
    return wavFromSamples(s, rate);
}

std::vector<uint8_t> makeMelodyWav(int rate) {
    const float notes[] = {261.63f, 293.66f, 329.63f, 349.23f, 392.0f, 440.0f, 493.88f, 523.25f};
    const float len = 0.3f;
    std::vector<float> s;
    for (float f : notes)
        for (int i = 0; i < int(len * rate); ++i) {
            const float t = float(i) / rate;
            const float decay = std::exp(-3.0f * t);
            s.push_back(0.45f * decay * envelope(t, len) *
                        (std::sin(6.2831853f * f * t) + 0.3f * std::sin(6.2831853f * 2 * f * t)));
        }
    return wavFromSamples(s, rate);
}

std::vector<uint8_t> makeClickWav(int rate) {
    std::vector<float> s(static_cast<size_t>(0.12f * rate));
    uint32_t r = 12345;
    for (size_t i = 0; i < s.size(); ++i) {
        r = r * 1664525u + 1013904223u;
        const float noise = ((r >> 9) & 0xFFFF) / 32768.0f - 1.0f;
        const float t = float(i) / rate;
        s[i] = 0.7f * std::exp(-30.0f * t) * (0.6f * noise + 0.4f * std::sin(6.2831853f * 180 * t));
    }
    return wavFromSamples(s, rate);
}

void addSoundCheckFiles(MemoryFileSource& files) {
    files.add("diag/tono.wav", makeToneWav(440, 1.0f));
    files.add("diag/melodia.wav", makeMelodyWav());
    files.add("diag/paso.wav", makeClickWav());
}

void SoundCheck::start(AudioEngine& audio) {
    step_ = 0;
    lastStep_ = -1;
    steps_ = 0;
    audio.setListener({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
}

bool SoundCheck::update(AudioEngine& audio, float t) {
    if (step_ < 0) return false;
    const int step = t < 1.2f ? 0 : t < 3.8f ? 1 : t < 7.0f ? 2 : 3;
    if (step != lastStep_) {
        lastStep_ = step;
        if (step == 0) audio.play("diag/tono.wav", {});
        if (step == 1) audio.play("diag/melodia.wav", {});
    }
    if (step == 2) { // 6 pasos de x = -6 a x = +6, a 2 m delante del oyente
        const uint32_t wanted = static_cast<uint32_t>((t - 3.8f) / 0.5f) + 1;
        while (steps_ < wanted && steps_ < 6) {
            PlayParams p;
            p.spatial = true;
            p.position = {-6.0f + steps_ * 2.4f, 0, -2};
            p.minDistance = 1;
            p.maxDistance = 30;
            audio.play("diag/paso.wav", p);
            ++steps_;
        }
    }
    step_ = step;
    return step < 3;
}

const char* SoundCheck::stepName() const {
    switch (step_) {
    case 0: return "Tono de 440 Hz (un «la»)";
    case 1: return "Melodía de 8 notas";
    case 2: return "Pasos de izquierda a derecha (mejor con auriculares)";
    default: return "Terminado";
    }
}

} // namespace titan
