// Mezclador: buses, enrutado y efectos portados de Godot. Cada prueba mezcla señal real y mide lo que sale.
#include <cmath>
#include <vector>

#include "Test.h"
#include "titan/audio/AudioEngine.h"
#include "titan/audio/SoundCheck.h"

using namespace titan;

namespace {

constexpr int kRate = 48000; // el motor sin dispositivo mezcla a 48 kHz

float rms(const std::vector<float>& st, size_t fromFrame, size_t toFrame, int channel = 0) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = fromFrame; i < toFrame && i * 2 + 1 < st.size(); ++i, ++n) sum += double(st[i * 2 + channel]) * st[i * 2 + channel];
    return n ? float(std::sqrt(sum / double(n))) : 0.0f;
}
float peak(const std::vector<float>& st, size_t fromFrame, size_t toFrame) {
    float p = 0;
    for (size_t i = fromFrame * 2; i < toFrame * 2 && i < st.size(); ++i) p = std::max(p, std::fabs(st[i]));
    return p;
}
// Amplitud de una frecuencia concreta (correlación con seno y coseno: un "bin" de Fourier).
float toneAmplitude(const std::vector<float>& st, float hz, size_t from, size_t to) {
    double s = 0, c = 0;
    for (size_t i = from; i < to; ++i) {
        const double ph = 6.283185307179586 * hz * double(i) / kRate;
        s += st[i * 2] * std::sin(ph);
        c += st[i * 2] * std::cos(ph);
    }
    return float(2 * std::sqrt(s * s + c * c) / double(to - from));
}

struct Mixer {
    MemoryFileSource files;
    AudioEngine audio;
    Mixer() {
        files.add("t100.wav", makeToneWav(100, 1.5f, kRate, 0.5f));
        files.add("t440.wav", makeToneWav(440, 1.5f, kRate, 0.5f));
        files.add("t4000.wav", makeToneWav(4000, 1.5f, kRate, 0.5f));
        files.add("clic.wav", makeClickWav(kRate));
        audio.init(files, false);
    }
    std::vector<float> play(const char* file, const std::string& bus, float seconds, float volume = 1.0f) {
        PlayParams p;
        p.bus = bus;
        p.volume = volume;
        audio.play(file, p);
        std::vector<float> out;
        audio.render(int(seconds * kRate), out);
        return out;
    }
};

} // namespace

TEST("mezclador: sin disposición solo existe Master y suena igual que antes") {
    Mixer m;
    CHECK(m.audio.hasBus("Master"));
    CHECK(!m.audio.hasBus("Música"));
    const auto out = m.play("t440.wav", "", 0.5f);
    CHECK_NEAR(rms(out, 4800, 19200), 0.5f / std::sqrt(2.0f), 0.02f); // tono de amplitud 0,5
    CHECK_EQ(m.audio.busLayout().buses.size(), 1u);
}

TEST("mezclador: volumen en dB, silencio y bus inexistente") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    CHECK(m.audio.hasBus("Música") && m.audio.hasBus("Efectos"));
    const float ref = rms(m.play("t440.wav", "Música", 0.5f), 4800, 19200);
    m.audio.stopAll();
    CHECK(m.audio.setBusVolumeDb("Música", -6.0206f));
    const float half = rms(m.play("t440.wav", "Música", 0.5f), 4800, 19200);
    CHECK_NEAR(half / ref, 0.5f, 0.01f);
    m.audio.stopAll();
    CHECK(m.audio.setBusMute("Música", true));
    // El grafo de miniaudio puede tener ya mezclado un trozo anterior al cambio (latencia normal de un búfer):
    // se mide a partir de 1024 fotogramas (21 ms).
    CHECK(peak(m.play("t440.wav", "Música", 0.3f), 1024, 14400) < 1e-6f);
    m.audio.stopAll();
    // Un bus que no existe no pierde el sonido: va a Master.
    CHECK(rms(m.play("t440.wav", "NoExiste", 0.3f), 4800, 14400) > 0.3f);
    CHECK(!m.audio.setBusVolumeDb("NoExiste", 0));
}

TEST("mezclador: enrutado en cadena (sub-bus → bus → Master)") {
    Mixer m;
    AudioBusLayout l = AudioBusLayout::defaultLayout();
    AudioBusSettings voces;
    voces.name = "Voces";
    voces.send = "Efectos";
    l.buses.push_back(voces);
    m.audio.setBusLayout(l);
    CHECK(rms(m.play("t440.wav", "Voces", 0.3f), 4800, 14400) > 0.3f);
    m.audio.stopAll();
    m.audio.setBusMute("Efectos", true); // silenciar el bus de destino silencia lo que le llega
    CHECK(peak(m.play("t440.wav", "Voces", 0.3f), 1024, 14400) < 1e-6f);
    m.audio.stopAll();
    m.audio.setBusMute("Efectos", false);
    m.audio.setBusVolumeDb("Master", -6.0206f); // y Master afecta a todo
    CHECK_NEAR(rms(m.play("t440.wav", "Voces", 0.3f), 4800, 14400), 0.5f / std::sqrt(2.0f) / 2, 0.01f);
}

TEST("mezclador: paso bajo y paso alto (biquad de Godot)") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    const int lp = m.audio.addBusEffect("Efectos", AudioEffectType::LowPass);
    CHECK_EQ(lp, 0);
    CHECK(m.audio.setBusEffectParam("Efectos", lp, "cutoff_hz", 500));
    CHECK(m.audio.setBusEffectParam("Efectos", lp, "db", 3)); // 24 dB por octava
    const float low = toneAmplitude(m.play("t100.wav", "Efectos", 0.6f), 100, 9600, 28800);
    m.audio.stopAll();
    const float high = toneAmplitude(m.play("t4000.wav", "Efectos", 0.6f), 4000, 9600, 28800);
    m.audio.stopAll();
    // 100 Hz pasa. Como en Godot, cada etapa usa Q = resonancia (0,5 por defecto, no 0,707 Butterworth): con 4
    // etapas la banda de paso cae ~1,4 dB a 1/5 del corte. Es el comportamiento del original, no un fallo del port.
    CHECK(low > 0.5f * 0.8f && low < 0.5f * 1.01f);
    CHECK(high < 0.5f * 0.001f);     // 4 kHz, 3 octavas por encima: más de 60 dB menos
    // Paso alto: al revés.
    CHECK(m.audio.removeBusEffect("Efectos", 0));
    const int hp = m.audio.addBusEffect("Efectos", AudioEffectType::HighPass);
    m.audio.setBusEffectParam("Efectos", hp, "cutoff_hz", 1000);
    m.audio.setBusEffectParam("Efectos", hp, "db", 1);
    CHECK(toneAmplitude(m.play("t100.wav", "Efectos", 0.6f), 100, 9600, 28800) < 0.5f * 0.02f);
    m.audio.stopAll();
    const float passHigh = toneAmplitude(m.play("t4000.wav", "Efectos", 0.6f), 4000, 9600, 28800);
    CHECK(passHigh > 0.5f * 0.85f && passHigh < 0.5f * 1.01f);
    // Desactivado o en "bypass", no cambia nada.
    m.audio.stopAll();
    m.audio.setBusEffectEnabled("Efectos", hp, false);
    CHECK_NEAR(toneAmplitude(m.play("t100.wav", "Efectos", 0.6f), 100, 9600, 28800), 0.5f, 0.02f);
}

TEST("mezclador: reverberación deja cola y el eco repite el golpe a su tiempo") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    // Golpe seco: después de 0,2 s no queda nada.
    const auto dry = m.play("clic.wav", "Efectos", 1.0f);
    CHECK(rms(dry, 14400, 33600) < 1e-5f);
    m.audio.stopAll();
    const int rv = m.audio.addBusEffect("Efectos", AudioEffectType::Reverb);
    m.audio.setBusEffectParam("Efectos", rv, "wet", 1);
    m.audio.setBusEffectParam("Efectos", rv, "room_size", 0.9f);
    const auto wet = m.play("clic.wav", "Efectos", 1.0f);
    CHECK(rms(wet, 14400, 33600) > 0.002f);           // cola de reverberación entre 0,3 y 0,7 s
    CHECK(rms(wet, 14400, 33600, 1) > 0.002f);        // también a la derecha
    CHECK(rms(wet, 14400, 24000) > rms(wet, 33600, 43200)); // y se apaga con el tiempo
    m.audio.stopAll();
    m.audio.resetBusEffects();
    CHECK(m.audio.removeBusEffect("Efectos", rv));

    // Eco: toque 1 a 250 ms (-6 dB) y toque 2 a 500 ms (-12 dB). La panorámica solo atenúa el lado contrario
    // (Godot limita 1 ± pan a 0..1), así que el canal más fuerte conserva el nivel del toque.
    const int dl = m.audio.addBusEffect("Efectos", AudioEffectType::Delay);
    CHECK_EQ(dl, 0);
    const auto echo = m.play("clic.wav", "Efectos", 0.8f);
    const float p0 = peak(echo, 0, 4800);
    const float p1 = peak(echo, 12000 - 100, 12000 + 4800);
    const float p2 = peak(echo, 24000 - 100, 24000 + 4800);
    CHECK(peak(echo, 6000, 11800) < p0 * 0.01f); // nada entre el golpe y el primer eco
    CHECK_NEAR(p1 / p0, 0.5012f, 0.01f);
    CHECK_NEAR(p2 / p0, 0.2512f, 0.01f);
    // Izquierda del toque 1 = derecha × (1 - 0,2).
    float l1 = 0, r1 = 0;
    for (size_t i = 11900; i < 16800; ++i) {
        l1 = std::max(l1, std::fabs(echo[i * 2]));
        r1 = std::max(r1, std::fabs(echo[i * 2 + 1]));
    }
    CHECK_NEAR(l1 / r1, 0.8f, 0.01f);
}

TEST("mezclador: limitador, compresor y distorsión") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    // Limitador: +12 dB a un tono de 0,5 lo llevaría a 2,0; el techo es -0,3 dB (0,966).
    const int li = m.audio.addBusEffect("Efectos", AudioEffectType::Limiter);
    m.audio.setBusEffectParam("Efectos", li, "pre_gain_db", 12);
    const auto lim = m.play("t440.wav", "Efectos", 0.5f);
    CHECK(peak(lim, 2400, 24000) <= 0.9661f);
    CHECK(peak(lim, 2400, 24000) > 0.9f); // limita, no aplasta
    m.audio.stopAll();
    m.audio.removeBusEffect("Efectos", li);

    // Compresor: umbral -20 dB, proporción 4:1 → baja el nivel de un tono fuerte.
    const float before = rms(m.play("t440.wav", "Efectos", 0.5f), 9600, 24000);
    m.audio.stopAll();
    const int co = m.audio.addBusEffect("Efectos", AudioEffectType::Compressor);
    m.audio.setBusEffectParam("Efectos", co, "threshold_db", -20);
    m.audio.setBusEffectParam("Efectos", co, "ratio", 4);
    const float after = rms(m.play("t440.wav", "Efectos", 0.5f), 9600, 24000);
    CHECK(after < before * 0.6f);
    m.audio.stopAll();
    m.audio.removeBusEffect("Efectos", co);

    // Distorsión: un seno puro no tiene armónicos; recortado aparece el 3.º (1320 Hz).
    const float clean = toneAmplitude(m.play("t440.wav", "Efectos", 0.5f), 1320, 4800, 19200);
    m.audio.stopAll();
    const int di = m.audio.addBusEffect("Efectos", AudioEffectType::Distortion);
    m.audio.setBusEffectParam("Efectos", di, "drive", 0.8f);
    m.audio.setBusEffectParam("Efectos", di, "pre_gain_db", 12);
    const float dirty = toneAmplitude(m.play("t440.wav", "Efectos", 0.5f), 1320, 4800, 19200);
    CHECK(clean < 0.002f);
    CHECK(dirty > 0.05f);
}

TEST("mezclador: medidor de nivel por bus") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    AudioBusLevel lv;
    m.play("t440.wav", "Música", 0.2f);
    CHECK(m.audio.readBusLevel("Música", lv));
    CHECK_NEAR(lv.peakLeft, 0.5f, 0.02f);
    CHECK(m.audio.readBusLevel("Efectos", lv));
    CHECK(lv.peakLeft == 0.0f);
    CHECK(!m.audio.readBusLevel("NoExiste", lv));
}

TEST("mezclador: disposición en JSON, normalización y parámetros") {
    AudioBusLayout l = AudioBusLayout::defaultLayout();
    l.buses[1].volumeDb = -3;
    AudioBusEffectSettings fx;
    fx.type = AudioEffectType::Reverb;
    fx.params = {{"wet", 0.25f}};
    l.buses[2].effects.push_back(fx);
    AudioBusLayout back;
    CHECK(AudioBusLayout::fromJson(l.toJson(), back));
    CHECK_EQ(back.buses.size(), 3u);
    CHECK_EQ(back.buses[1].name, std::string("Música"));
    CHECK_NEAR(back.buses[1].volumeDb, -3.0f, 1e-6f);
    CHECK_EQ(back.buses[2].effects.size(), 1u);
    CHECK(back.buses[2].effects[0].type == AudioEffectType::Reverb);

    // Ciclos, destinos inexistentes, nombres repetidos y Master ausente se corrigen.
    AudioBusLayout bad;
    AudioBusSettings a, b, c;
    a.name = "A"; a.send = "B";
    b.name = "B"; b.send = "A";
    c.name = "A"; c.send = "Fantasma";
    bad.buses = {a, b, c};
    bad.normalize();
    CHECK_EQ(bad.buses[0].name, std::string("Master"));
    CHECK_EQ(bad.buses.size(), 4u);
    CHECK_EQ(bad.buses[3].name, std::string("A 2"));
    CHECK_EQ(bad.buses[3].send, std::string("Master"));
    // A → B → A era un ciclo: al menos uno de los dos acaba en Master y ya no hay ciclo.
    CHECK(bad.buses[1].send == "Master" || bad.buses[2].send == "Master");
    CHECK(!AudioBusLayout::fromJson("{no es json", back));

    // El motor aplica la disposición y la devuelve con los cambios hechos en ejecución.
    MemoryFileSource files;
    AudioEngine audio;
    audio.init(files, false);
    audio.setBusLayout(l);
    float wet = 0;
    CHECK(audio.getBusEffectParam("Efectos", 0, "wet", wet));
    CHECK_NEAR(wet, 0.25f, 1e-6f);
    CHECK(!audio.setBusEffectParam("Efectos", 0, "no_existe", 1));
    audio.setBusEffectParam("Efectos", 0, "room_size", 0.3f);
    const AudioBusLayout now = audio.busLayout();
    CHECK_EQ(now.buses.size(), 3u);
    CHECK_NEAR(now.buses[1].volumeDb, -3.0f, 1e-6f);
    bool found = false;
    for (const auto& [k, v] : now.buses[2].effects[0].params) found |= k == "room_size" && std::fabs(v - 0.3f) < 1e-6f;
    CHECK(found);
}

// ---- Lua: el mismo mezclador, manejado desde un script.
#include "titan/scripting/ScriptSystem.h"

namespace {
class AudioHost : public ScriptHost {
public:
    AudioEngine* audio = nullptr;
    Input input;
    double scriptTime() const override { return 0; }
    float scriptDeltaTime() const override { return 0.016f; }
    float scriptFixedDeltaTime() const override { return 1.0f / 60.0f; }
    uint64_t scriptFrame() const override { return 0; }
    const Input& scriptInput() const override { return input; }
    Vec2 scriptScreenSize() const override { return {800, 600}; }
    bool scriptScreenToWorld(Vec2, Vec3&) const override { return false; }
    bool scriptLoadScene(const std::string&) override { return true; }
    void scriptDrawText(float, float, float, const std::string&, Color) override {}
    AudioEngine* scriptAudio() override { return audio; }
    bool scriptPlaySound(const std::string& path, Entity, bool, const Vec3&, float volume, const std::string& bus) override {
        PlayParams p;
        p.volume = volume;
        p.bus = bus;
        return audio->play(path, p) != 0;
    }
};
} // namespace

TEST("mezclador: API de Lua (buses, efectos, parámetros, nivel y errores claros)") {
    Mixer m;
    m.audio.setBusLayout(AudioBusLayout::defaultLayout());
    AudioHost host;
    host.audio = &m.audio;
    ComponentRegistry registry = ComponentRegistry::createDefault();
    ScriptSystem scripts(host, registry, [](AssetRef, std::string&, std::string&) { return false; });
    CHECK(scripts.runString(R"(
        local b = Audio.buses()
        assert(#b == 3 and b[1] == "Master" and b[2] == "Música" and b[3] == "Efectos", table.concat(b, ","))
        Audio.setBusVolume("Música", -6)
        assert(math.abs(Audio.getBusVolume("Música") + 6) < 1e-6)
        local i = Audio.addEffect("Efectos", "lowpass", { cutoff_hz = 800, db = 1 })
        assert(i == 1)
        assert(math.abs(Audio.getEffectParam("Efectos", 1, "cutoff_hz") - 800) < 1e-3)
        Audio.setEffectParam("Efectos", 1, "cutoff_hz", 1200)
        assert(math.abs(Audio.getEffectParam("Efectos", 1, "cutoff_hz") - 1200) < 1e-3)
        assert(Audio.addEffect("Efectos", "reverb", { wet = 0.2 }) == 2)
        assert(Audio.setEffectEnabled("Efectos", 2, false))
        assert(Audio.removeEffect("Efectos", 2))
        assert(not Audio.removeEffect("Efectos", 5))
        assert(Audio.play("t440.wav", 1, "Música"))
    )"));
    std::vector<float> out;
    m.audio.render(4800, out);
    // Lo que pidió el script ha llegado al motor: -6 dB en Música.
    CHECK_NEAR(peak(out, 2400, 4800), 0.5f * 0.5012f, 0.01f);
    CHECK(scripts.runString(R"(
        local l, r = Audio.busLevel("Música")
        assert(l > -12.5 and l < -11.5, "nivel " .. l) -- tono de 0,5 (-6 dB) en un bus a -6 dB
        local s = Audio.busLevel("Efectos")
        assert(s == -80)
    )"));
    // Errores con mensaje útil, no fallos silenciosos.
    CHECK(!scripts.runString(R"(Audio.setBusVolume("NoExiste", 0))"));
    CHECK(!scripts.runString(R"(Audio.addEffect("Efectos", "chorus"))"));
    CHECK(!scripts.runString(R"(Audio.addEffect("Efectos", "reverb", { tamano = 1 }))"));
    CHECK(!scripts.runString(R"(Audio.setEffectParam("Efectos", 1, "no_existe", 1))"));
}
