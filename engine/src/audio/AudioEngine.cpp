#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio/miniaudio.h"

#include "titan/audio/AudioEngine.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>
#include <unordered_map>

#include "titan/core/Log.h"

namespace titan {

namespace {
struct Clip {
    float* pcm = nullptr;      // intercalado, 2 canales
    ma_uint64 frames = 0;
    ~Clip() { if (pcm) ma_free(pcm, nullptr); }
};
// ---- Bus del mezclador: un grupo de miniaudio (suma sus voces y sub-buses) seguido de un nodo propio que aplica la
// cadena de efectos, el volumen y mide el nivel. Grupo → efectos → grupo del bus de destino (o altavoces).
struct Bus;
struct FxNode {
    ma_node_base base; // debe ir primero (miniaudio trata el puntero como ma_node_base*)
    Bus* bus = nullptr;
};
struct Bus {
    AudioBusSettings settings;
    ma_sound_group group{};
    FxNode node{};
    bool groupReady = false, nodeReady = false;
    std::mutex mutex; // protege effects/enabled frente al hilo de audio
    std::vector<std::unique_ptr<AudioEffect>> effects;
    std::vector<char> enabled;
    std::atomic<float> gain{1.0f};
    std::atomic<bool> bypass{false};
    std::atomic<float> peakL{0}, peakR{0};
    void updateGain() { gain = settings.mute ? 0.0f : std::pow(10.0f, settings.volumeDb / 20.0f); }
};

void atomicMax(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

void fxProcess(ma_node* pNode, const float** ppIn, ma_uint32* pFrameCountIn, float** ppOut, ma_uint32* pFrameCountOut) {
    Bus& b = *reinterpret_cast<FxNode*>(pNode)->bus;
    const ma_uint32 n = *pFrameCountOut;
    float* out = ppOut[0];
    const float* in = ppIn ? ppIn[0] : nullptr;
    const ma_uint32 avail = (in && pFrameCountIn) ? std::min(*pFrameCountIn, n) : 0;
    if (avail > 0 && out != in) std::memmove(out, in, size_t(avail) * 2 * sizeof(float));
    if (avail < n) std::memset(out + size_t(avail) * 2, 0, size_t(n - avail) * 2 * sizeof(float));
    if (!b.bypass.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(b.mutex);
        for (size_t i = 0; i < b.effects.size(); ++i)
            if (b.enabled[i]) b.effects[i]->process(out, out, int(n));
    }
    const float g = b.gain.load(std::memory_order_relaxed);
    float pl = 0, pr = 0;
    for (ma_uint32 i = 0; i < n; ++i) {
        out[i * 2] *= g;
        out[i * 2 + 1] *= g;
        pl = std::max(pl, std::fabs(out[i * 2]));
        pr = std::max(pr, std::fabs(out[i * 2 + 1]));
    }
    atomicMax(b.peakL, pl);
    atomicMax(b.peakR, pr);
    if (pFrameCountIn) *pFrameCountIn = avail;
}

ma_node_vtable gFxVtable = {fxProcess, nullptr, 1, 1,
                            MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT};

struct Voice {
    uint32_t id = 0;
    ma_audio_buffer_ref buffer;
    ma_sound sound;
    Entity owner;
    bool spatial = false;
};
} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    bool initialized = false;
    FileSource* files = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Clip>> clips;
    std::list<Voice> voices; // lista: las direcciones de ma_sound no deben moverse
    uint32_t nextId = 1;
    std::list<Bus> buses;    // lista: los nodos de miniaudio no deben moverse; el primero es Master

    Bus* bus(const std::string& name) {
        for (Bus& b : buses)
            if (b.settings.name == name) return &b;
        return nullptr;
    }
    void destroyBuses() {
        // Primero los grupos (se desconectan sus entradas), luego los nodos de efectos.
        for (Bus& b : buses)
            if (b.groupReady) ma_sound_group_uninit(&b.group);
        for (Bus& b : buses)
            if (b.nodeReady) ma_node_uninit(&b.node, nullptr);
        buses.clear();
    }
    void configureEffect(Bus& b, const AudioBusEffectSettings& fx) {
        auto e = AudioEffect::create(fx.type, float(ma_engine_get_sample_rate(&engine)));
        for (const auto& [k, v] : fx.params)
            if (!e->setParam(k, v))
                TITAN_WARN("audio", "Bus '%s': el efecto %s no tiene el parámetro '%s'", b.settings.name.c_str(),
                           audioEffectName(fx.type), k.c_str());
        b.effects.push_back(std::move(e));
        b.enabled.push_back(fx.enabled ? 1 : 0);
    }
    bool buildBuses(AudioBusLayout layout) {
        destroyBuses();
        layout.normalize();
        ma_uint32 channels = 2;
        for (const AudioBusSettings& s : layout.buses) {
            buses.emplace_back();
            Bus& b = buses.back();
            b.settings = s;
            b.settings.effects.clear(); // la verdad está en b.effects; busLayout() la reconstruye
            b.node.bus = &b;
            if (ma_sound_group_init(&engine, 0, nullptr, &b.group) != MA_SUCCESS) {
                TITAN_ERROR("audio", "No se pudo crear el bus '%s'", s.name.c_str());
                destroyBuses();
                return false;
            }
            b.groupReady = true;
            ma_node_config cfg = ma_node_config_init();
            cfg.vtable = &gFxVtable;
            cfg.pInputChannels = &channels;
            cfg.pOutputChannels = &channels;
            if (ma_node_init(ma_engine_get_node_graph(&engine), &cfg, nullptr, &b.node) != MA_SUCCESS) {
                TITAN_ERROR("audio", "No se pudo crear la cadena de efectos del bus '%s'", s.name.c_str());
                destroyBuses();
                return false;
            }
            b.nodeReady = true;
            ma_node_attach_output_bus(&b.group, 0, &b.node, 0);
            for (const AudioBusEffectSettings& fx : s.effects) configureEffect(b, fx);
            b.bypass = s.bypassEffects;
            b.updateGain();
        }
        for (Bus& b : buses) {
            Bus* dest = &b == &buses.front() ? nullptr : bus(b.settings.send);
            ma_node_attach_output_bus(&b.node, 0, dest ? static_cast<ma_node*>(&dest->group)
                                                       : static_cast<ma_node*>(ma_engine_get_endpoint(&engine)), 0);
        }
        return true;
    }

    Clip* load(const std::string& path) {
        auto it = clips.find(path);
        if (it != clips.end()) return it->second.get();
        std::vector<uint8_t> bytes;
        auto clip = std::make_unique<Clip>();
        if (!files->read(path, bytes)) {
            TITAN_ERROR("audio", "No se encontró el sonido '%s'", path.c_str());
        } else {
            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, ma_engine_get_sample_rate(&engine));
            void* pcm = nullptr;
            if (ma_decode_memory(bytes.data(), bytes.size(), &cfg, &clip->frames, &pcm) != MA_SUCCESS) {
                TITAN_ERROR("audio", "No se pudo decodificar '%s' (formatos: WAV, MP3, FLAC)", path.c_str());
                clip->frames = 0;
            } else {
                clip->pcm = static_cast<float*>(pcm);
                TITAN_INFO("audio", "Sonido '%s' cargado (%.2f s)", path.c_str(),
                           double(clip->frames) / ma_engine_get_sample_rate(&engine));
            }
        }
        Clip* raw = clip.get();
        clips[path] = std::move(clip);
        return raw;
    }
    Voice* find(uint32_t id) {
        for (auto& v : voices)
            if (v.id == id) return &v;
        return nullptr;
    }
    void release(std::list<Voice>::iterator it) {
        ma_sound_uninit(&it->sound);
        ma_audio_buffer_ref_uninit(&it->buffer);
        voices.erase(it);
    }
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(FileSource& files, bool withDevice) {
    shutdown();
    impl_->files = &files;
    ma_engine_config cfg = ma_engine_config_init();
    if (!withDevice) {
        cfg.noDevice = MA_TRUE;
        cfg.channels = 2;
        cfg.sampleRate = 48000;
    }
    const ma_result r = ma_engine_init(&cfg, &impl_->engine);
    if (r != MA_SUCCESS) {
        TITAN_WARN("audio", "No hay dispositivo de audio disponible (%s): el juego funcionará sin sonido",
                   ma_result_description(r));
        return false;
    }
    impl_->initialized = true;
    impl_->buildBuses(AudioBusLayout{}); // solo Master, sin efectos: suena igual que antes de los buses
    if (withDevice) {
        TITAN_INFO("audio", "Audio listo: %s", deviceInfo().c_str());
        if (!hasRealOutput())
            TITAN_WARN("audio", "La salida es el dispositivo NULO de miniaudio: no hay tarjeta de sonido o el sistema no dejó "
                                "abrir ninguna. El juego funciona, pero NO se oirá nada.");
    }
    return true;
}

void AudioEngine::shutdown() {
    if (!impl_->initialized) return;
    stopAll();
    impl_->destroyBuses();
    ma_engine_uninit(&impl_->engine);
    impl_->clips.clear();
    impl_->initialized = false;
}

bool AudioEngine::ready() const { return impl_->initialized; }

uint32_t AudioEngine::play(const std::string& path, const PlayParams& p) {
    if (!impl_->initialized) return 0;
    Clip* clip = impl_->load(path);
    if (!clip || !clip->pcm || clip->frames == 0) return 0;
    if (impl_->voices.size() >= 64) { // límite de voces: se sustituye la más antigua
        impl_->release(impl_->voices.begin());
    }
    impl_->voices.emplace_back();
    Voice& v = impl_->voices.back();
    v.id = impl_->nextId++;
    v.owner = p.owner;
    v.spatial = p.spatial;
    ma_audio_buffer_ref_init(ma_format_f32, 2, clip->pcm, clip->frames, &v.buffer);
    // El clip ya está a la frecuencia del motor; se dice explícitamente (miniaudio 0.11 lo deja a 0 y entonces
    // supone la del motor, que es lo mismo, pero no queremos depender de ese detalle).
    v.buffer.sampleRate = ma_engine_get_sample_rate(&impl_->engine);
    Bus* bus = p.bus.empty() ? nullptr : impl_->bus(p.bus);
    if (!bus) {
        if (!p.bus.empty()) TITAN_WARN("audio", "El bus '%s' no existe: el sonido va a Master", p.bus.c_str());
        bus = impl_->buses.empty() ? nullptr : &impl_->buses.front();
    }
    if (ma_sound_init_from_data_source(&impl_->engine, &v.buffer, p.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION,
                                       bus ? &bus->group : nullptr, &v.sound) != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&v.buffer);
        impl_->voices.pop_back();
        return 0;
    }
    ma_sound_set_volume(&v.sound, p.volume);
    ma_sound_set_pitch(&v.sound, std::max(0.01f, p.pitch));
    ma_sound_set_looping(&v.sound, p.loop);
    if (p.spatial) {
        ma_sound_set_position(&v.sound, p.position.x, p.position.y, p.position.z);
        ma_sound_set_min_distance(&v.sound, p.minDistance);
        ma_sound_set_max_distance(&v.sound, p.maxDistance);
        ma_sound_set_attenuation_model(&v.sound, ma_attenuation_model_inverse);
    }
    ma_sound_start(&v.sound);
    return v.id;
}

void AudioEngine::stop(uint32_t id) {
    for (auto it = impl_->voices.begin(); it != impl_->voices.end(); ++it)
        if (it->id == id) {
            impl_->release(it);
            return;
        }
}

void AudioEngine::stopOwner(Entity owner) {
    for (auto it = impl_->voices.begin(); it != impl_->voices.end();)
        if (it->owner == owner) {
            auto next = std::next(it);
            impl_->release(it);
            it = next;
        } else {
            ++it;
        }
}

void AudioEngine::stopAll() {
    while (!impl_->voices.empty()) impl_->release(impl_->voices.begin());
}

bool AudioEngine::isPlaying(uint32_t id) const {
    Voice* v = impl_->find(id);
    return v && !ma_sound_at_end(&v->sound);
}

void AudioEngine::setVoicePosition(uint32_t id, const Vec3& p) {
    if (Voice* v = impl_->find(id)) ma_sound_set_position(&v->sound, p.x, p.y, p.z);
}

void AudioEngine::setListener(const Vec3& p, const Vec3& f, const Vec3& u) {
    if (!impl_->initialized) return;
    ma_engine_listener_set_position(&impl_->engine, 0, p.x, p.y, p.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, f.x, f.y, f.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, u.x, u.y, u.z);
}

void AudioEngine::setMasterVolume(float v) {
    if (impl_->initialized) ma_engine_set_volume(&impl_->engine, std::max(0.0f, v));
}

void AudioEngine::setPaused(bool paused) {
    if (!impl_->initialized) return;
    for (auto& v : impl_->voices) {
        if (paused) ma_sound_stop(&v.sound);
        else if (!ma_sound_at_end(&v.sound)) ma_sound_start(&v.sound);
    }
}

void AudioEngine::updateImpl(const std::function<bool(Entity, Vec3&)>& positionOf) {
    for (auto it = impl_->voices.begin(); it != impl_->voices.end();) {
        auto next = std::next(it);
        Vec3 p;
        if (ma_sound_at_end(&it->sound)) {
            impl_->release(it); // terminó (y no es un bucle)
        } else if (!it->owner.isNull()) {
            if (!positionOf(it->owner, p)) impl_->release(it); // su entidad ya no existe
            else if (it->spatial) ma_sound_set_position(&it->sound, p.x, p.y, p.z);
        }
        it = next;
    }
}

size_t AudioEngine::activeVoices() const { return impl_->voices.size(); }

void AudioEngine::render(int frames, std::vector<float>& out) {
    out.assign(static_cast<size_t>(frames) * 2, 0.0f);
    if (impl_->initialized) ma_engine_read_pcm_frames(&impl_->engine, out.data(), static_cast<ma_uint64>(frames), nullptr);
}

bool AudioEngine::hasRealOutput() const {
    if (!impl_->initialized) return false;
    ma_device* d = ma_engine_get_device(&impl_->engine);
    return d && d->pContext->backend != ma_backend_null;
}

int AudioEngine::sampleRate() const {
    return impl_->initialized ? static_cast<int>(ma_engine_get_sample_rate(&impl_->engine)) : 0;
}

std::string AudioEngine::deviceInfo() const {
    if (!impl_->initialized) return "sin audio";
    ma_engine* e = &impl_->engine;
    ma_device* d = ma_engine_get_device(e);
    char buf[512];
    if (!d) {
        std::snprintf(buf, sizeof(buf), "sin dispositivo (mezcla en memoria), %u Hz, %u canales", ma_engine_get_sample_rate(e),
                      ma_engine_get_channels(e));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%s, salida '%s', %u Hz, %u canales, volumen %.2f", ma_get_backend_name(d->pContext->backend),
                  d->playback.name, d->sampleRate, d->playback.channels, ma_engine_get_volume(e));
    return buf;
}

// ---------------------------------------------------------------- mezclador
void AudioEngine::setBusLayout(const AudioBusLayout& layout) {
    if (!impl_->initialized) return;
    stopAll();
    if (!impl_->buildBuses(layout)) impl_->buildBuses(AudioBusLayout{});
}

AudioBusLayout AudioEngine::busLayout() const {
    AudioBusLayout l;
    for (Bus& b : impl_->buses) {
        AudioBusSettings s = b.settings;
        std::lock_guard<std::mutex> lock(b.mutex);
        for (size_t i = 0; i < b.effects.size(); ++i) {
            AudioBusEffectSettings fx;
            fx.type = b.effects[i]->type();
            fx.enabled = b.enabled[i] != 0;
            const auto& info = b.effects[i]->params();
            for (size_t k = 0; k < info.size(); ++k)
                if (b.effects[i]->values()[k] != info[k].defaultValue) fx.params.emplace_back(info[k].name, b.effects[i]->values()[k]);
            s.effects.push_back(fx);
        }
        l.buses.push_back(s);
    }
    if (l.buses.empty()) l.normalize();
    return l;
}

bool AudioEngine::hasBus(const std::string& name) const { return impl_->bus(name) != nullptr; }

bool AudioEngine::setBusVolumeDb(const std::string& name, float db) {
    Bus* b = impl_->bus(name);
    if (!b) return false;
    b->settings.volumeDb = std::clamp(db, -80.0f, 24.0f);
    b->updateGain();
    return true;
}

bool AudioEngine::setBusMute(const std::string& name, bool mute) {
    Bus* b = impl_->bus(name);
    if (!b) return false;
    b->settings.mute = mute;
    b->updateGain();
    return true;
}

bool AudioEngine::setBusBypass(const std::string& name, bool bypass) {
    Bus* b = impl_->bus(name);
    if (!b) return false;
    b->settings.bypassEffects = bypass;
    b->bypass = bypass;
    return true;
}

int AudioEngine::addBusEffect(const std::string& name, AudioEffectType type) {
    Bus* b = impl_->bus(name);
    if (!b) return -1;
    AudioBusEffectSettings fx;
    fx.type = type;
    auto e = AudioEffect::create(type, float(sampleRate()));
    std::lock_guard<std::mutex> lock(b->mutex);
    b->effects.push_back(std::move(e));
    b->enabled.push_back(1);
    return int(b->effects.size()) - 1;
}

bool AudioEngine::removeBusEffect(const std::string& name, int index) {
    Bus* b = impl_->bus(name);
    if (!b || index < 0) return false;
    std::lock_guard<std::mutex> lock(b->mutex);
    if (size_t(index) >= b->effects.size()) return false;
    b->effects.erase(b->effects.begin() + index);
    b->enabled.erase(b->enabled.begin() + index);
    return true;
}

bool AudioEngine::setBusEffectEnabled(const std::string& name, int index, bool enabled) {
    Bus* b = impl_->bus(name);
    if (!b || index < 0) return false;
    std::lock_guard<std::mutex> lock(b->mutex);
    if (size_t(index) >= b->effects.size()) return false;
    b->enabled[size_t(index)] = enabled ? 1 : 0;
    return true;
}

bool AudioEngine::setBusEffectParam(const std::string& name, int index, const std::string& param, float value) {
    Bus* b = impl_->bus(name);
    if (!b || index < 0) return false;
    std::lock_guard<std::mutex> lock(b->mutex);
    return size_t(index) < b->effects.size() && b->effects[size_t(index)]->setParam(param, value);
}

bool AudioEngine::getBusEffectParam(const std::string& name, int index, const std::string& param, float& value) const {
    Bus* b = impl_->bus(name);
    if (!b || index < 0) return false;
    std::lock_guard<std::mutex> lock(b->mutex);
    return size_t(index) < b->effects.size() && b->effects[size_t(index)]->getParam(param, value);
}

bool AudioEngine::readBusLevel(const std::string& name, AudioBusLevel& out) {
    Bus* b = impl_->bus(name);
    if (!b) return false;
    out.peakLeft = b->peakL.exchange(0);
    out.peakRight = b->peakR.exchange(0);
    return true;
}

void AudioEngine::resetBusEffects() {
    for (Bus& b : impl_->buses) {
        std::lock_guard<std::mutex> lock(b.mutex);
        for (auto& e : b.effects) e->reset();
    }
}

float AudioEngine::renderForTest(int frames) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    ma_engine_read_pcm_frames(&impl_->engine, buf.data(), frames, nullptr);
    float peak = 0;
    for (float s : buf) peak = std::max(peak, std::fabs(s));
    return peak;
}

} // namespace titan
