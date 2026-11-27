#include <cmath>
#include <cstring>
#include <map>

#include "Test.h"
#include "titan/audio/AudioEngine.h"

using namespace titan;

namespace {
// WAV de 16 bits mono con un tono de 440 Hz, generado en memoria.
std::vector<uint8_t> sineWav(float seconds) {
    const int rate = 22050, n = static_cast<int>(rate * seconds);
    std::vector<uint8_t> w(44 + n * 2);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&w[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&w[at], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4); put32(4, 36 + n * 2); std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&w[36], "data", 4); put32(40, n * 2);
    for (int i = 0; i < n; ++i) put16(44 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(std::sin(i * 6.2831853 * 440 / rate) * 12000)));
    return w;
}
class MemFiles final : public FileSource {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    bool read(const std::string& p, std::vector<uint8_t>& out) override {
        auto it = files.find(p);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
};
} // namespace

TEST("audio: decodifica un WAV, lo mezcla y termina solo") {
    MemFiles files;
    files.files["assets/tono.wav"] = sineWav(0.2f);
    AudioEngine audio;
    CHECK(audio.init(files, false));
    CHECK(audio.renderForTest(1024) < 1e-6f); // silencio
    const uint32_t v = audio.play("assets/tono.wav", {});
    CHECK(v != 0);
    CHECK(audio.renderForTest(2048) > 0.2f); // suena
    for (int i = 0; i < 10; ++i) audio.renderForTest(2400); // 0,5 s
    audio.update([](Entity, Vec3&) { return true; });
    CHECK_EQ(audio.activeVoices(), 0u); // la voz terminó y se liberó
    CHECK(audio.play("assets/no_existe.wav", {}) == 0);
}

TEST("audio: bucle, volumen y voces que siguen a su entidad") {
    MemFiles files;
    files.files["assets/tono.wav"] = sineWav(0.1f);
    AudioEngine audio;
    CHECK(audio.init(files, false));
    PlayParams p;
    p.loop = true;
    p.volume = 0.25f;
    p.owner = Entity{3, 0};
    audio.play("assets/tono.wav", p);
    for (int i = 0; i < 10; ++i) audio.renderForTest(4800); // 1 s: un bucle sigue sonando
    const float peak = audio.renderForTest(2048);
    CHECK(peak > 0.03f && peak < 0.15f); // atenuado por el volumen
    audio.update([](Entity, Vec3&) { return false; }); // su entidad desapareció
    CHECK_EQ(audio.activeVoices(), 0u);
}

// ---- Pruebas de la señal mezclada: no basta con que "no haya errores", se mide lo que sale.
#include <fstream>
#include <iterator>

#include "titan/audio/SoundCheck.h"

namespace {
struct Analysis {
    float rmsL = 0, rmsR = 0;
    float frequency = 0; // por cruces por cero del canal izquierdo
};
Analysis analyze(const std::vector<float>& st, int rate, size_t from = 0, size_t to = SIZE_MAX) {
    Analysis a;
    const size_t frames = std::min(st.size() / 2, to);
    size_t crossings = 0, n = 0;
    for (size_t i = from; i < frames; ++i) {
        const float l = st[i * 2], r = st[i * 2 + 1];
        a.rmsL += l * l;
        a.rmsR += r * r;
        if (i > from && (st[(i - 1) * 2] < 0) != (l < 0)) ++crossings;
        ++n;
    }
    if (n) {
        a.rmsL = std::sqrt(a.rmsL / n);
        a.rmsR = std::sqrt(a.rmsR / n);
        // Frecuencia por autocorrelación (robusta frente al ruido de compresión del MP3); cruces como respaldo.
        // Se toma el primer pico que llega al 95 % del máximo (un periodo, no dos).
        const size_t span = std::min<size_t>(n, size_t(rate) / 5);
        std::vector<double> ac;
        const int minLag = rate / 2000, maxLag = rate / 60;
        for (int lag = minLag; lag < maxLag && from + span + lag < frames; ++lag) {
            double v = 0;
            for (size_t i = from; i < from + span; ++i) v += double(st[i * 2]) * st[(i + lag) * 2];
            ac.push_back(v);
        }
        double maxV = 0;
        for (double v : ac) maxV = std::max(maxV, v);
        int best = 0;
        for (size_t k = 1; k + 1 < ac.size(); ++k)
            if (ac[k] >= 0.95 * maxV && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1]) { best = minLag + int(k); break; }
        a.frequency = best > 0 ? float(rate) / best : crossings / 2.0f / (float(n) / rate);
    }
    return a;
}
std::vector<uint8_t> fileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST("audio: lo que sale de la mezcla es el tono correcto (WAV, MP3 y FLAC)") {
    MemoryFileSource files;
    files.add("tono.wav", makeToneWav(440, 1.0f, 22050)); // otra frecuencia de muestreo: se convierte
    files.add("tono.mp3", fileBytes(TITAN_TEST_DATA_DIR "/tono440.mp3"));
    files.add("tono.flac", fileBytes(TITAN_TEST_DATA_DIR "/tono440.flac"));
    for (const char* f : {"tono.wav", "tono.mp3", "tono.flac"}) {
        AudioEngine audio;
        CHECK(audio.init(files, false));
        CHECK(audio.play(f, {}) != 0);
        std::vector<float> out;
        audio.render(audio.sampleRate() * 3 / 4, out);
        const Analysis a = analyze(out, audio.sampleRate(), audio.sampleRate() / 5); // tras el retardo inicial del MP3
        CHECK(a.rmsL > 0.05f && a.rmsR > 0.05f);          // suena, y por los dos lados
        CHECK(std::fabs(a.frequency - 440) < 8);          // a la altura correcta (no más rápido ni más lento)
        if (std::fabs(a.frequency - 440) >= 8) std::printf("      %s: %.1f Hz\n", f, a.frequency);
    }
}

TEST("audio: volumen, tono (pitch), sonido 3D a izquierda/derecha y atenuación por distancia") {
    MemoryFileSource files;
    addSoundCheckFiles(files);
    auto rms = [&](PlayParams p, Vec3 listener = {}) {
        AudioEngine audio;
        audio.init(files, false);
        audio.setListener(listener, {0, 0, -1}, {0, 1, 0});
        audio.play("diag/tono.wav", p);
        std::vector<float> out;
        audio.render(audio.sampleRate() / 4, out);
        return analyze(out, audio.sampleRate(), 1000);
    };
    const Analysis full = rms({});
    PlayParams half;
    half.volume = 0.5f;
    CHECK_NEAR(rms(half).rmsL / full.rmsL, 0.5, 0.05);
    PlayParams up;
    up.pitch = 2.0f;
    CHECK(std::fabs(rms(up).frequency - 880) < 15); // una octava más aguda
    PlayParams left;
    left.spatial = true;
    left.position = {-5, 0, 0}; // a la izquierda del oyente que mira a -Z
    const Analysis l = rms(left);
    CHECK(l.rmsL > l.rmsR * 1.5f);
    PlayParams right = left;
    right.position = {5, 0, 0};
    const Analysis r = rms(right);
    CHECK(r.rmsR > r.rmsL * 1.5f);
    PlayParams nearP = left, farP = left;
    nearP.position = {0, 0, -2};
    farP.position = {0, 0, -20};
    CHECK(rms(nearP).rmsL > rms(farP).rmsL * 2); // lejos suena más bajo
}

TEST("audio: la prueba de sonido completa produce tono, melodía y pasos que cruzan") {
    MemoryFileSource files;
    addSoundCheckFiles(files);
    AudioEngine audio;
    audio.init(files, false);
    SoundCheck check;
    check.start(audio);
    const int rate = audio.sampleRate();
    std::vector<float> all, chunk;
    const int block = rate / 50;
    float t = 0;
    while (check.update(audio, t)) {
        audio.render(block, chunk);
        all.insert(all.end(), chunk.begin(), chunk.end());
        t += float(block) / rate;
    }
    CHECK(t > 6.5f && t < 7.5f);
    const Analysis tone = analyze(all, rate, size_t(0.2 * rate), size_t(0.9 * rate));
    CHECK(std::fabs(tone.frequency - 440) < 8);
    const Analysis melody = analyze(all, rate, size_t(1.3 * rate), size_t(3.6 * rate));
    CHECK(melody.rmsL > 0.02f);
    const Analysis firstSteps = analyze(all, rate, size_t(3.8 * rate), size_t(4.9 * rate));
    const Analysis lastSteps = analyze(all, rate, size_t(5.9 * rate), size_t(7.0 * rate));
    CHECK(firstSteps.rmsL > firstSteps.rmsR); // empiezan a la izquierda
    CHECK(lastSteps.rmsR > lastSteps.rmsL);   // y terminan a la derecha
}
