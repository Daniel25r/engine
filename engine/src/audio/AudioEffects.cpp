// Efectos de audio portados de Godot Engine (MIT). Ver la cabecera para la atribución completa.
//   Reverb      ← servers/audio/effects/reverb_filter.cpp + audio_effect_reverb.cpp (Freeverb)
//   Delay       ← servers/audio/effects/audio_effect_delay.cpp
//   Filtros     ← servers/audio/audio_filter_sw.cpp + audio_effect_filter.cpp (biquad RBJ, 1..4 etapas)
//   Compressor  ← servers/audio/effects/audio_effect_compressor.cpp
//   Limiter     ← servers/audio/effects/audio_effect_hard_limiter.cpp
//   Distortion  ← servers/audio/effects/audio_effect_distortion.cpp
#include "titan/audio/AudioEffects.h"

#include <algorithm>
#include <cmath>

namespace titan {
namespace {

constexpr float kTau = 6.28318530717958647692f;

inline float undenormalize(float f) { return std::fabs(f) < 1e-15f ? 0.0f : f; }
inline float dbToLinear(float db) { return std::exp(db * 0.11512925464970228420089957273422f); }
inline float linearToDb(float l) { return std::log(l) * 8.6858896380650365530225783783321f; }

// ---------------------------------------------------------------- Reverb (Freeverb, un canal)
class ReverbChannel {
public:
    static constexpr int kInputMax = 1024;
    void configure(float mixRate, float spreadBase) {
        mixRate_ = mixRate;
        static const float combTunings[8] = {0.025306122448979593f, 0.026938775510204082f, 0.028956916099773241f,
                                             0.03074829931972789f,  0.032244897959183672f, 0.03380952380952381f,
                                             0.035306122448979592f, 0.036666666666666667f};
        static const float allpassTunings[4] = {0.0051020408163265302f, 0.007732426303854875f, 0.01f,
                                                0.012607709750566893f};
        for (int i = 0; i < 8; ++i) {
            Comb& c = comb_[i];
            c.extraSpread = int(std::rint(spreadBase * mixRate));
            c.buffer.assign(std::max(5, int(std::rint(combTunings[i] * mixRate)) + c.extraSpread), 0.0f);
            c.pos = 0;
            c.dampH = 0;
        }
        for (int i = 0; i < 4; ++i) {
            AllPass& a = allpass_[i];
            a.extraSpread = int(std::rint(spreadBase * mixRate));
            a.buffer.assign(std::max(5, int(std::rint(allpassTunings[i] * mixRate)) + a.extraSpread), 0.0f);
            a.pos = 0;
        }
        echo_.assign(size_t(0.5f * mixRate + 1), 0.0f); // MAX_ECHO_MS = 500
        echoPos_ = 0;
        hpf1_ = hpf2_ = 0;
        input_.assign(kInputMax, 0.0f);
    }
    void setRoom(float roomSize, float damp) {
        for (Comb& c : comb_) {
            c.feedback = std::clamp(0.7f + roomSize * 0.28f, 0.7f, 0.98f);
            float auxdmp = damp / 2.0f + 0.5f; // solo media gama (0,5..1)
            auxdmp *= auxdmp;
            c.damp = std::exp(-kTau * auxdmp * 10000 / mixRate_);
        }
    }
    void process(const float* src, int stride, float* dst, int frames, float predelayMs, float predelayFb, float hpf,
                 float extraSpread, float wet, float dry) {
        int predelayFrames = int(std::rint(predelayMs / 1000.0f * mixRate_));
        const int echoSize = int(echo_.size());
        predelayFrames = std::clamp(predelayFrames, 10, echoSize - 1);
        for (int i = 0; i < frames; ++i) {
            if (echoPos_ >= echoSize) echoPos_ = 0;
            int readPos = echoPos_ - predelayFrames;
            while (readPos < 0) readPos += echoSize;
            const float in = undenormalize(echo_[size_t(readPos)] * predelayFb + src[i * stride]);
            echo_[size_t(echoPos_)] = in;
            input_[size_t(i)] = in;
            dst[i] = 0;
            ++echoPos_;
        }
        if (hpf > 0) {
            const float hpaux = std::exp(-kTau * hpf * 6000 / mixRate_);
            const float a1 = (1.0f + hpaux) / 2.0f, a2 = -(1.0f + hpaux) / 2.0f, b1 = hpaux;
            for (int i = 0; i < frames; ++i) {
                const float in = input_[size_t(i)];
                input_[size_t(i)] = in * a1 + hpf1_ * a2 + hpf2_ * b1;
                hpf2_ = input_[size_t(i)];
                hpf1_ = in;
            }
        }
        for (Comb& c : comb_) {
            const int limit = int(c.buffer.size()) - int(std::rint(float(c.extraSpread) * (1.0f - extraSpread)));
            for (int j = 0; j < frames; ++j) {
                if (c.pos >= limit) c.pos = 0;
                float out = undenormalize(c.buffer[size_t(c.pos)] * c.feedback);
                out = out * (1.0f - c.damp) + c.dampH * c.damp; // paso bajo
                c.dampH = out;
                c.buffer[size_t(c.pos)] = input_[size_t(j)] + out;
                dst[j] += out;
                ++c.pos;
            }
        }
        constexpr float kAllpassFeedback = 0.7f;
        for (AllPass& a : allpass_) {
            const int limit = int(a.buffer.size()) - int(std::rint(float(a.extraSpread) * (1.0f - extraSpread)));
            for (int j = 0; j < frames; ++j) {
                if (a.pos >= limit) a.pos = 0;
                const float aux = a.buffer[size_t(a.pos)];
                a.buffer[size_t(a.pos)] = undenormalize(kAllpassFeedback * aux + dst[j]);
                dst[j] = aux - kAllpassFeedback * a.buffer[size_t(a.pos)];
                ++a.pos;
            }
        }
        constexpr float kWetScale = 0.6f;
        for (int i = 0; i < frames; ++i) dst[i] = dst[i] * wet * kWetScale + src[i * stride] * dry;
    }
private:
    struct Comb {
        std::vector<float> buffer;
        float feedback = 0, damp = 0, dampH = 0;
        int pos = 0, extraSpread = 0;
    };
    struct AllPass {
        std::vector<float> buffer;
        int pos = 0, extraSpread = 0;
    };
    Comb comb_[8];
    AllPass allpass_[4];
    std::vector<float> input_, echo_;
    int echoPos_ = 0;
    float hpf1_ = 0, hpf2_ = 0, mixRate_ = 44100;
};

const std::vector<AudioEffectParamInfo> kReverbParams = {
    {"room_size", 0, 1, 0.8f, "Tamaño de la sala"},
    {"damping", 0, 1, 0.5f, "Amortiguación"},
    {"spread", 0, 1, 1.0f, "Amplitud estéreo"},
    {"hipass", 0, 1, 0.0f, "Paso alto"},
    {"dry", 0, 1, 1.0f, "Señal original"},
    {"wet", 0, 1, 0.5f, "Señal con efecto"},
    {"predelay_ms", 20, 500, 150, "Pre-retardo (ms)"},
    {"predelay_feedback", 0, 0.98f, 0.4f, "Realimentación del pre-retardo"},
};

class Reverb final : public AudioEffect {
public:
    explicit Reverb(float rate) : AudioEffect(AudioEffectType::Reverb, &kReverbParams, rate) {
        ch_[0].configure(rate, 0);
        ch_[1].configure(rate, 0.000521f); // un poco distinto a la derecha: efecto estéreo
        changed();
    }
    void process(const float* in, float* out, int frames) override {
        float l[ReverbChannel::kInputMax], r[ReverbChannel::kInputMax];
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, ReverbChannel::kInputMax);
            const float* src = in + done * 2;
            ch_[0].process(src, 2, l, n, v(6), v(7), v(3), v(2), v(5), v(4));
            ch_[1].process(src + 1, 2, r, n, v(6), v(7), v(3), v(2), v(5), v(4));
            float* dst = out + done * 2;
            for (int i = 0; i < n; ++i) {
                dst[i * 2] = l[i];
                dst[i * 2 + 1] = r[i];
            }
            done += n;
        }
    }
    void reset() override {
        ch_[0].configure(sampleRate_, 0);
        ch_[1].configure(sampleRate_, 0.000521f);
        changed();
    }

protected:
    void changed() override {
        for (ReverbChannel& c : ch_) c.setRoom(v(0), v(1));
    }

private:
    ReverbChannel ch_[2];
};

// ---------------------------------------------------------------- Delay (eco de dos toques + realimentación)
const std::vector<AudioEffectParamInfo> kDelayParams = {
    {"dry", 0, 1, 1.0f, "Señal original"},
    {"tap1_active", 0, 1, 1, "Toque 1 activo"},
    {"tap1_delay_ms", 0, 1500, 250, "Toque 1: retardo (ms)"},
    {"tap1_level_db", -60, 0, -6, "Toque 1: nivel (dB)"},
    {"tap1_pan", -1, 1, 0.2f, "Toque 1: panorámica"},
    {"tap2_active", 0, 1, 1, "Toque 2 activo"},
    {"tap2_delay_ms", 0, 1500, 500, "Toque 2: retardo (ms)"},
    {"tap2_level_db", -60, 0, -12, "Toque 2: nivel (dB)"},
    {"tap2_pan", -1, 1, -0.4f, "Toque 2: panorámica"},
    {"feedback_active", 0, 1, 0, "Realimentación activa"},
    {"feedback_delay_ms", 0, 1500, 340, "Realimentación: retardo (ms)"},
    {"feedback_level_db", -60, 0, -6, "Realimentación: nivel (dB)"},
    {"feedback_lowpass", 1, 16000, 16000, "Realimentación: paso bajo (Hz)"},
};

class Delay final : public AudioEffect {
public:
    explicit Delay(float rate) : AudioEffect(AudioEffectType::Delay, &kDelayParams, rate) { reset(); }
    void reset() override {
        size_t size = 1;
        const size_t need = size_t((1500.0f + 100) / 1000.0f * sampleRate_); // retardo máximo + margen
        while (size < need) size <<= 1;
        ring_.assign(size * 2, 0.0f);
        mask_ = size - 1;
        feedback_.assign(need * 2, 0.0f);
        ringPos_ = 0;
        fbPos_ = 0;
        h_[0] = h_[1] = 0;
    }
    void process(const float* in, float* out, int frames) override {
        const float rate = sampleRate_;
        const float tap1 = v(1) > 0.5f ? dbToLinear(v(3)) : 0.0f, tap2 = v(5) > 0.5f ? dbToLinear(v(7)) : 0.0f;
        const size_t d1 = size_t(v(2) / 1000.0f * rate), d2 = size_t(v(6) / 1000.0f * rate);
        const float fbLevel = v(9) > 0.5f ? dbToLinear(v(11)) : 0.0f;
        const size_t fbFrames = std::clamp<size_t>(size_t(v(10) / 1000.0f * rate), 1, feedback_.size() / 2);
        const float t1l = tap1 * std::clamp(1.0f - v(4), 0.0f, 1.0f), t1r = tap1 * std::clamp(1.0f + v(4), 0.0f, 1.0f);
        const float t2l = tap2 * std::clamp(1.0f - v(8), 0.0f, 1.0f), t2r = tap2 * std::clamp(1.0f + v(8), 0.0f, 1.0f);
        const float lpc = std::exp(-kTau * v(12) / rate), lpic = 1.0f - lpc;
        for (int i = 0; i < frames; ++i) {
            const float sl = in[i * 2], sr = in[i * 2 + 1];
            ring_[(ringPos_ & mask_) * 2] = sl;
            ring_[(ringPos_ & mask_) * 2 + 1] = sr;
            const size_t a = ((ringPos_ - d1) & mask_) * 2, b = ((ringPos_ - d2) & mask_) * 2;
            float ol = sl * v(0) + ring_[a] * t1l + ring_[b] * t2l + feedback_[fbPos_ * 2];
            float orr = sr * v(0) + ring_[a + 1] * t1r + ring_[b + 1] * t2r + feedback_[fbPos_ * 2 + 1];
            h_[0] = undenormalize(ol * fbLevel * lpic + h_[0] * lpc);
            h_[1] = undenormalize(orr * fbLevel * lpic + h_[1] * lpc);
            feedback_[fbPos_ * 2] = h_[0];
            feedback_[fbPos_ * 2 + 1] = h_[1];
            out[i * 2] = ol;
            out[i * 2 + 1] = orr;
            ++ringPos_;
            if (++fbPos_ >= fbFrames) fbPos_ = 0;
        }
    }

private:
    std::vector<float> ring_, feedback_;
    size_t mask_ = 0, ringPos_ = 0, fbPos_ = 0;
    float h_[2] = {0, 0};
};

// ---------------------------------------------------------------- Filtros biquad (RBJ), 6/12/18/24 dB por octava
const std::vector<AudioEffectParamInfo> kFilterParams = {
    {"cutoff_hz", 1, 20500, 2000, "Frecuencia de corte (Hz)"},
    {"resonance", 0, 1, 0.5f, "Resonancia"},
    {"db", 0, 3, 0, "Pendiente (0=6, 1=12, 2=18, 3=24 dB/oct)"},
};

class Filter final : public AudioEffect {
public:
    Filter(AudioEffectType t, float rate) : AudioEffect(t, &kFilterParams, rate) { changed(); }
    void reset() override {
        for (auto& ch : hist_)
            for (auto& h : ch) h = {};
    }
    void process(const float* in, float* out, int frames) override {
        const int stages = std::clamp(int(v(2)) + 1, 1, 4);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i) {
                float s = in[i * 2 + c];
                for (int st = 0; st < stages; ++st) {
                    History& h = hist_[c][st];
                    const float pre = s;
                    s = s * b0_ + h.hb1 * b1_ + h.hb2 * b2_ + h.ha1 * a1_ + h.ha2 * a2_;
                    h.ha2 = h.ha1;
                    h.hb2 = h.hb1;
                    h.hb1 = pre;
                    h.ha1 = undenormalize(s);
                }
                out[i * 2 + c] = s;
            }
    }

protected:
    void changed() override { // AudioFilterSW::prepare_coefficients
        const int stages = std::clamp(int(v(2)) + 1, 1, 4);
        const double srLimit = sampleRate_ / 2 + 512;
        const double cutoff = std::clamp<double>(v(0), 1, srLimit);
        const double omega = kTau * cutoff / sampleRate_, sinv = std::sin(omega), cosv = std::cos(omega);
        double q = v(1) <= 0 ? 0.0001 : v(1);
        if (type() == AudioEffectType::BandPass) q *= 2.0;
        if (stages > 1) q = q > 1.0 ? std::pow(q, 1.0 / stages) : q;
        const double alpha = sinv / (2 * q), a0 = 1.0 + alpha;
        double b0, b1, b2, a1 = -2.0 * cosv, a2 = 1.0 - alpha;
        if (type() == AudioEffectType::HighPass) {
            b0 = (1.0 + cosv) / 2.0;
            b1 = -(1.0 + cosv);
            b2 = (1.0 + cosv) / 2.0;
        } else if (type() == AudioEffectType::BandPass) {
            b0 = alpha * std::sqrt(q + 1);
            b1 = 0;
            b2 = -alpha * std::sqrt(q + 1);
        } else {
            b0 = (1.0 - cosv) / 2.0;
            b1 = 1.0 - cosv;
            b2 = (1.0 - cosv) / 2.0;
        }
        b0_ = float(b0 / a0);
        b1_ = float(b1 / a0);
        b2_ = float(b2 / a0);
        a1_ = float(a1 / -a0);
        a2_ = float(a2 / -a0);
    }

private:
    struct History {
        float ha1 = 0, ha2 = 0, hb1 = 0, hb2 = 0;
    };
    History hist_[2][4];
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
};

// ---------------------------------------------------------------- Compresor
const std::vector<AudioEffectParamInfo> kCompressorParams = {
    {"threshold_db", -60, 0, 0, "Umbral (dB)"},
    {"ratio", 1, 48, 4, "Proporción"},
    {"gain_db", -20, 20, 0, "Ganancia de compensación (dB)"},
    {"attack_us", 20, 2000, 20, "Ataque (µs)"},
    {"release_ms", 20, 2000, 250, "Liberación (ms)"},
    {"mix", 0, 1, 1, "Mezcla"},
};

class Compressor final : public AudioEffect {
public:
    explicit Compressor(float rate) : AudioEffect(AudioEffectType::Compressor, &kCompressorParams, rate) {}
    void reset() override { rundb_ = averatio_ = runratio_ = 0; }
    void process(const float* in, float* out, int frames) override {
        const float rate = sampleRate_;
        const float threshold = dbToLinear(v(0));
        const float ratatcoef = std::exp(-1 / (0.00001f * rate)), ratrelcoef = std::exp(-1 / (0.5f * rate));
        const float atcoef = std::exp(-1 / (v(3) / 1000000.0f * rate)), relcoef = std::exp(-1 / (v(4) / 1000.0f * rate));
        const float makeup = dbToLinear(v(2)), mix = v(5), ratio = v(1);
        for (int i = 0; i < frames; ++i) {
            const float l = in[i * 2], r = in[i * 2 + 1];
            const float peak = std::max(std::fabs(l), std::fabs(r));
            float overdb = peak > 0 ? 2.08136898f * linearToDb(peak / threshold) : 0.0f;
            if (overdb < 0) overdb = 0;
            if (overdb - rundb_ > 5) averatio_ = 4;
            if (overdb > rundb_) {
                rundb_ = overdb + atcoef * (rundb_ - overdb);
                runratio_ = averatio_ + ratatcoef * (runratio_ - averatio_);
            } else {
                rundb_ = overdb + relcoef * (rundb_ - overdb);
                runratio_ = averatio_ + ratrelcoef * (runratio_ - averatio_);
            }
            averatio_ = runratio_;
            const float grv = dbToLinear(-rundb_ * (ratio - 1) / ratio);
            out[i * 2] = l * grv * makeup * mix + l * (1 - mix);
            out[i * 2 + 1] = r * grv * makeup * mix + r * (1 - mix);
        }
    }

private:
    float rundb_ = 0, averatio_ = 0, runratio_ = 0;
};

// ---------------------------------------------------------------- Limitador duro (con 2 ms de anticipación)
const std::vector<AudioEffectParamInfo> kLimiterParams = {
    {"pre_gain_db", -24, 24, 0, "Ganancia previa (dB)"},
    {"ceiling_db", -24, 0, -0.3f, "Techo (dB)"},
    {"release", 0.01f, 3, 0.1f, "Liberación (s)"},
};

class Limiter final : public AudioEffect {
public:
    explicit Limiter(float rate) : AudioEffect(AudioEffectType::Limiter, &kLimiterParams, rate) { reset(); }
    void reset() override {
        constexpr float attack = 0.002f, sustain = 0.02f;
        bufL_.assign(size_t(std::ceil(sampleRate_ * attack)) + 1, 0.0f);
        bufR_.assign(bufL_.size(), 0.0f);
        samplesToStore_ = int(std::ceil(sampleRate_ * (attack + sustain) + 1));
        bucketSize_ = std::max(1, int(sampleRate_ * attack));
        buckets_.clear();
        for (int i = 0; i < samplesToStore_; i += bucketSize_) buckets_.push_back(1.0f);
        cursor_ = bucketCursor_ = 0;
        releaseFactor_ = attackFactor_ = 0;
        gain_ = gainTarget_ = 1;
    }
    void process(const float* in, float* out, int frames) override {
        constexpr float attack = 0.002f;
        const float ceiling = dbToLinear(v(1)), release = v(2), pre = dbToLinear(v(0)), rate = sampleRate_;
        for (int i = 0; i < frames; ++i) {
            const float l = in[i * 2] * pre, r = in[i * 2 + 1] * pre;
            const float largest = std::max(std::fabs(l), std::fabs(r));
            releaseFactor_ = std::min(std::max(0.0f, releaseFactor_ - 1.0f / rate), release);
            if (releaseFactor_ > 0) gain_ = gainTarget_ + (1.0f - gainTarget_) * (1.0f - releaseFactor_ / release);
            if (largest * gain_ > ceiling) {
                gainTarget_ = ceiling / largest;
                releaseFactor_ = release;
                attackFactor_ = attack;
            }
            attackFactor_ = std::max(0.0f, attackFactor_ - 1.0f / rate);
            if (attackFactor_ > 0) gain_ = gainTarget_ + (gain_ - gainTarget_) * (1.0f - attackFactor_ / attack);
            const int bucket = bucketCursor_ / bucketSize_;
            if (bucketCursor_ % bucketSize_ == 0) buckets_[size_t(bucket)] = 1.0f;
            buckets_[size_t(bucket)] = std::min(buckets_[size_t(bucket)], gain_);
            bucketCursor_ = (bucketCursor_ + 1) % samplesToStore_;
            for (float g : buckets_) gain_ = std::min(gain_, g);
            const float dl = bufL_[size_t(cursor_)], dr = bufR_[size_t(cursor_)];
            bufL_[size_t(cursor_)] = l;
            bufR_[size_t(cursor_)] = r;
            cursor_ = (cursor_ + 1) % int(bufL_.size());
            out[i * 2] = dl * gain_;
            out[i * 2 + 1] = dr * gain_;
        }
    }

private:
    std::vector<float> bufL_, bufR_, buckets_;
    int cursor_ = 0, samplesToStore_ = 0, bucketSize_ = 1, bucketCursor_ = 0;
    float releaseFactor_ = 0, attackFactor_ = 0, gain_ = 1, gainTarget_ = 1;
};

// ---------------------------------------------------------------- Distorsión
const std::vector<AudioEffectParamInfo> kDistortionParams = {
    {"mode", 0, 4, 0, "Modo (0 recorte, 1 atan, 2 lo-fi, 3 saturación, 4 forma de onda)"},
    {"pre_gain_db", -60, 60, 0, "Ganancia previa (dB)"},
    {"keep_hf_hz", 1, 20000, 16000, "Conservar agudos desde (Hz)"},
    {"drive", 0, 1, 0, "Intensidad"},
    {"post_gain_db", -80, 24, 0, "Ganancia final (dB)"},
};

class Distortion final : public AudioEffect {
public:
    explicit Distortion(float rate) : AudioEffect(AudioEffectType::Distortion, &kDistortionParams, rate) {}
    void reset() override { h_[0] = h_[1] = 0; }
    void process(const float* in, float* out, int frames) override {
        const float lpc = std::exp(-kTau * v(2) / sampleRate_), lpic = 1.0f - lpc;
        const float drive = v(3), pre = dbToLinear(v(1)), post = dbToLinear(v(4));
        const float atanMult = std::pow(10.0f, drive * drive * 3.0f) - 1.0f + 0.001f;
        const float atanDiv = 1.0f / (std::atan(atanMult) * (1.0f + drive * 8));
        const float lofiMult = std::pow(2.0f, 2.0f + (1.0f - drive) * 14); // de 16 a 2 bits
        const int mode = std::clamp(int(v(0)), 0, 4);
        for (int i = 0; i < frames * 2; ++i) {
            const float o = undenormalize(in[i] * lpic + lpc * h_[i & 1]);
            h_[i & 1] = o;
            float a = o * pre;
            const float high = in[i] - o; // los agudos pasan sin distorsionar
            switch (mode) {
            case 0: a = std::clamp(std::pow(std::fabs(a), 1.0001f - drive) * (a < 0 ? -1.0f : 1.0f), -1.0f, 1.0f); break;
            case 1: a = std::atan(a * atanMult) * atanDiv; break;
            case 2: a = std::floor(a * lofiMult + 0.5f) / lofiMult; break;
            case 3: {
                const double x = a * 0.686306, z = 1 + std::exp(std::sqrt(std::fabs(x)) * -0.75);
                a = float((std::exp(x) - std::exp(-x * z)) / (std::exp(x) + std::exp(-x)));
            } break;
            default: {
                const float k = 2 * drive / (1.00001f - drive);
                a = (1.0f + k) * a / (1.0f + k * std::fabs(a));
            }
            }
            out[i] = a * post + high;
        }
    }

private:
    float h_[2] = {0, 0};
};

} // namespace

// ---------------------------------------------------------------- base
AudioEffect::AudioEffect(AudioEffectType t, const std::vector<AudioEffectParamInfo>* info, float sampleRate)
    : sampleRate_(sampleRate), type_(t), info_(info) {
    for (const AudioEffectParamInfo& p : *info) values_.push_back(p.defaultValue);
}

bool AudioEffect::setParam(const std::string& name, float value) {
    for (size_t i = 0; i < info_->size(); ++i)
        if (name == (*info_)[i].name) {
            values_[i] = std::clamp(value, (*info_)[i].min, (*info_)[i].max);
            changed();
            return true;
        }
    return false;
}

bool AudioEffect::getParam(const std::string& name, float& value) const {
    for (size_t i = 0; i < info_->size(); ++i)
        if (name == (*info_)[i].name) {
            value = values_[i];
            return true;
        }
    return false;
}

std::unique_ptr<AudioEffect> AudioEffect::create(AudioEffectType type, float sampleRate) {
    switch (type) {
    case AudioEffectType::Reverb: return std::make_unique<Reverb>(sampleRate);
    case AudioEffectType::Delay: return std::make_unique<Delay>(sampleRate);
    case AudioEffectType::LowPass:
    case AudioEffectType::HighPass:
    case AudioEffectType::BandPass: return std::make_unique<Filter>(type, sampleRate);
    case AudioEffectType::Compressor: return std::make_unique<Compressor>(sampleRate);
    case AudioEffectType::Limiter: return std::make_unique<Limiter>(sampleRate);
    case AudioEffectType::Distortion: return std::make_unique<Distortion>(sampleRate);
    }
    return nullptr;
}

namespace {
const char* const kNames[] = {"reverb", "delay", "lowpass", "highpass", "bandpass", "compressor", "limiter", "distortion"};
}

const char* audioEffectName(AudioEffectType t) { return kNames[static_cast<int>(t)]; }

bool audioEffectFromName(const std::string& name, AudioEffectType& out) {
    for (int i = 0; i < 8; ++i)
        if (name == kNames[i]) {
            out = static_cast<AudioEffectType>(i);
            return true;
        }
    return false;
}

} // namespace titan
