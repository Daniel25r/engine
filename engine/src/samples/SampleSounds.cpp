// Sonidos de la demo de plataformas, sintetizados aquí (sin archivos de terceros ni licencias): efectos cortos y
// una música en bucle. Todo en WAV PCM 16 bits mono a 44,1 kHz.
#include "SampleSounds.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace titan::samples {
namespace {

constexpr int kRate = 44100;
constexpr float kPi = 3.14159265f;

std::vector<uint8_t> wav(const std::vector<float>& samples) {
    std::vector<uint8_t> out(44 + samples.size() * 2);
    auto u32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
    auto u16 = [&](size_t at, uint16_t v) { std::memcpy(&out[at], &v, 2); };
    std::memcpy(&out[0], "RIFF", 4);
    u32(4, uint32_t(36 + samples.size() * 2));
    std::memcpy(&out[8], "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 1);
    u32(24, kRate);
    u32(28, kRate * 2);
    u16(32, 2);
    u16(34, 16);
    std::memcpy(&out[36], "data", 4);
    u32(40, uint32_t(samples.size() * 2));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float v = std::fmax(-1.0f, std::fmin(1.0f, samples[i]));
        const int16_t s = int16_t(std::lround(v * 32767));
        std::memcpy(&out[44 + i * 2], &s, 2);
    }
    return out;
}

// Ruido blanco determinista (siempre el mismo sonido).
struct Noise {
    uint32_t s = 0x1234567u;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s) / 2147483648.0f - 1.0f;
    }
};

// Onda cuadrada suavizada (suma de 3 armónicos): sonido "de consola" sin aliasing fuerte.
float softSquare(float phase) {
    return (std::sin(phase) + std::sin(3 * phase) / 3 + std::sin(5 * phase) / 5) * 0.8f;
}

// Nota con frecuencia que va de f0 a f1, envolvente de ataque corto y caída exponencial.
void note(std::vector<float>& buf, float start, float dur, float f0, float f1, float amp, bool square = true,
          float decay = 6) {
    const size_t a = size_t(start * kRate), n = size_t(dur * kRate);
    if (buf.size() < a + n) buf.resize(a + n, 0);
    float phase = 0;
    for (size_t i = 0; i < n; ++i) {
        const float t = float(i) / kRate, k = float(i) / n;
        const float f = f0 + (f1 - f0) * k;
        phase += 2 * kPi * f / kRate;
        const float env = std::fmin(1.0f, t / 0.005f) * std::exp(-decay * t) * (1 - k * k);
        buf[a + i] += (square ? softSquare(phase) : std::sin(phase)) * amp * env;
    }
}

void noise(std::vector<float>& buf, float start, float dur, float amp, float lowpass) {
    const size_t a = size_t(start * kRate), n = size_t(dur * kRate);
    if (buf.size() < a + n) buf.resize(a + n, 0);
    Noise r;
    float y = 0;
    for (size_t i = 0; i < n; ++i) {
        const float k = float(i) / n;
        y += (r.next() - y) * lowpass; // paso bajo de un polo: más grave cuanto menor
        buf[a + i] += y * amp * (1 - k) * (1 - k);
    }
}

float midi(int n) { return 440.0f * std::pow(2.0f, (n - 69) / 12.0f); }

std::vector<float> music() {
    // 8 compases a 132 ppm en Do mayor: melodía, bajo y un charles suave. Dura ~14,5 s y enlaza consigo misma.
    const float beat = 60.0f / 132;
    std::vector<float> buf(size_t(32 * beat * kRate), 0);
    const int melody[] = {76, 79, 81, 79, 76, 74, 72, 74, 76, 76, 79, 76, 74, 72, 74, -1,
                          77, 81, 84, 81, 79, 77, 76, 74, 72, 74, 76, 79, 77, 74, 72, -1};
    const int bass[] = {48, 48, 45, 45, 41, 41, 43, 43};
    for (int i = 0; i < 32; ++i)
        if (melody[i] > 0) note(buf, i * beat, beat * 0.95f, midi(melody[i]), midi(melody[i]), 0.16f, true, 3);
    for (int bar = 0; bar < 8; ++bar)
        for (int b = 0; b < 4; ++b) note(buf, (bar * 4 + b) * beat, beat * 0.9f, midi(bass[bar]), midi(bass[bar]), 0.2f, false, 4);
    for (int i = 0; i < 64; ++i) noise(buf, i * beat * 0.5f, 0.05f, i % 2 ? 0.05f : 0.08f, 0.9f);
    buf.resize(size_t(32 * beat * kRate)); // exacto para que el bucle no salte
    return buf;
}

} // namespace

std::vector<std::pair<std::string, std::vector<uint8_t>>> platformerSounds() {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    auto add = [&](const char* name, const std::vector<float>& s) { out.emplace_back(name, wav(s)); };
    std::vector<float> b;

    b.clear(); // moneda: dos notas agudas
    note(b, 0, 0.07f, midi(83), midi(83), 0.35f);
    note(b, 0.07f, 0.25f, midi(88), midi(88), 0.35f, true, 8);
    add("moneda", b);

    b.clear(); // salto: barrido hacia arriba
    note(b, 0, 0.2f, 280, 720, 0.35f, true, 5);
    add("salto", b);

    b.clear(); // golpe: ruido grave con tono que baja
    noise(b, 0, 0.25f, 0.6f, 0.25f);
    note(b, 0, 0.25f, 220, 90, 0.4f, true, 8);
    add("golpe", b);

    b.clear(); // pisotón: "boing" que baja
    note(b, 0, 0.22f, 520, 130, 0.4f, true, 6);
    noise(b, 0, 0.08f, 0.4f, 0.4f);
    add("pisoton", b);

    b.clear(); // paso: golpecito corto
    noise(b, 0, 0.06f, 0.5f, 0.3f);
    add("paso", b);

    b.clear(); // caída: barrido largo hacia abajo
    note(b, 0, 0.7f, 900, 140, 0.3f, true, 2);
    add("caida", b);

    b.clear(); // punto de control
    note(b, 0, 0.12f, midi(72), midi(72), 0.3f);
    note(b, 0.12f, 0.3f, midi(79), midi(79), 0.3f, true, 6);
    add("control", b);

    b.clear(); // victoria: arpegio
    const int arp[] = {72, 76, 79, 84, 79, 84};
    for (int i = 0; i < 6; ++i) note(b, i * 0.11f, i == 5 ? 0.8f : 0.2f, midi(arp[i]), midi(arp[i]), 0.3f, true, i == 5 ? 2.5f : 6);
    add("victoria", b);

    b.clear(); // derrota: tres notas que bajan
    const int down[] = {67, 63, 60};
    for (int i = 0; i < 3; ++i) note(b, i * 0.22f, i == 2 ? 0.7f : 0.24f, midi(down[i]), midi(down[i]), 0.3f, true, 3);
    add("derrota", b);

    add("musica", music());
    return out;
}

} // namespace titan::samples
