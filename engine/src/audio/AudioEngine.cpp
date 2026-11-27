#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio/miniaudio.h"

#include "titan/audio/AudioEngine.h"

#include <cmath>
#include <list>
#include <unordered_map>

#include "titan/core/Log.h"

namespace titan {

namespace {
struct Clip {
    float* pcm = nullptr;      // intercalado, 2 canales
    ma_uint64 frames = 0;
    ~Clip() { if (pcm) ma_free(pcm, nullptr); }
};
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
    if (ma_sound_init_from_data_source(&impl_->engine, &v.buffer, p.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                       &v.sound) != MA_SUCCESS) {
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

float AudioEngine::renderForTest(int frames) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    ma_engine_read_pcm_frames(&impl_->engine, buf.data(), frames, nullptr);
    float peak = 0;
    for (float s : buf) peak = std::max(peak, std::fabs(s));
    return peak;
}

} // namespace titan
