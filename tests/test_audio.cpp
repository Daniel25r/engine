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
