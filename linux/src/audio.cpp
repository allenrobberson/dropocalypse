// Dropocalypse for Linux - PulseAudio capture + feature extraction.
//
//  * "What this computer is playing" records the monitor source of an output (sink); this works
//    on PulseAudio and on PipeWire through pipewire-pulse, which every current desktop ships.
//  * Microphones / line-ins are ordinary sources.
//  * Small fragments (~5 ms) keep latency low; bass is tracked per sample with an IIR filter and
//    an envelope follower, so nothing waits on an FFT.

#include "audio.h"

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <cwchar>

static constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------
// UTF-8 <-> wide strings (the UI code is shared with the Windows version, which uses wstring)
// ---------------------------------------------------------------------------------------------

std::wstring widen(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { ++i; out.push_back(L'?'); continue; }
        if (i + extra >= s.size()) { out.push_back(L'?'); break; }
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back((wchar_t)cp);
        i += extra + 1;
    }
    return out;
}

std::string narrow(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        uint32_t cp = (uint32_t)wc;
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string AudioSource::key() const { return (loopback ? "loop:" : "mic:") + narrow(id); }

// ---------------------------------------------------------------------------------------------
// AudioAnalysis
// ---------------------------------------------------------------------------------------------

AudioAnalysis::AudioAnalysis() {
    for (auto& v : ring_) v.store(0.0f, std::memory_order_relaxed);
}

AudioSnapshot AudioAnalysis::snapshot() const {
    AudioSnapshot s;
    s.beatCount = beatCount_.load(std::memory_order_acquire);
    s.bass = bass_.load(std::memory_order_relaxed);
    s.mid = mid_.load(std::memory_order_relaxed);
    s.treble = treble_.load(std::memory_order_relaxed);
    s.level = level_.load(std::memory_order_relaxed);
    s.beatStrength = beatStrength_.load(std::memory_order_relaxed);
    return s;
}

void AudioAnalysis::copyRecent(float* out, int count) const {
    int end = writeIndex_.load(std::memory_order_acquire);
    int idx = (end - count) & RingMask;
    for (int i = 0; i < count; ++i) {
        out[i] = ring_[idx].load(std::memory_order_relaxed);
        idx = (idx + 1) & RingMask;
    }
}

// ---------------------------------------------------------------------------------------------
// DSP
// ---------------------------------------------------------------------------------------------

Biquad Biquad::lowPass(double cutoff, double sampleRate, double q) {
    Biquad f;
    double w0 = 2 * kPi * cutoff / sampleRate;
    double alpha = std::sin(w0) / (2 * q);
    double cosw = std::cos(w0);
    double a0 = 1 + alpha;
    f.b0 = (float)(((1 - cosw) / 2) / a0);
    f.b1 = (float)((1 - cosw) / a0);
    f.b2 = f.b0;
    f.a1 = (float)((-2 * cosw) / a0);
    f.a2 = (float)((1 - alpha) / a0);
    return f;
}

EnvelopeFollower::EnvelopeFollower(double attackMs, double releaseMs, double sr) {
    attack = (float)(1 - std::exp(-1 / (attackMs * 0.001 * sr)));
    release = (float)(1 - std::exp(-1 / (releaseMs * 0.001 * sr)));
}

AutoGain::AutoGain(double halfLifeSeconds, double sr) {
    decay = (float)std::pow(0.5, 1 / (halfLifeSeconds * sr));
}

FeatureExtractor::FeatureExtractor(double sr)
    : bassLP1_(Biquad::lowPass(140, sr)), bassLP2_(Biquad::lowPass(140, sr)),
      midLP1_(Biquad::lowPass(2500, sr)), midLP2_(Biquad::lowPass(2500, sr)),
      bassEnv_(1, 110, sr), midEnv_(1, 90, sr), trebleEnv_(0.5, 70, sr), levelEnv_(2, 200, sr),
      bassAGC_(3, sr), midAGC_(3, sr), trebleAGC_(3, sr) {
    averageCoef_ = (float)(1 - std::exp(-1 / (0.6 * sr)));
    refractorySamples_ = (int)(0.14 * sr);
    samplesSinceBeat_ = refractorySamples_;
}

void FeatureExtractor::process(const float* samples, int count, AudioAnalysis& out) {
    if (count <= 0) return;
    float sens = std::max(0.05f, out.sensitivity.load(std::memory_order_relaxed));
    float floorLevel = 0.004f / sens;
    float b = 0, m = 0, t = 0, l = 0, bn = 0, mn = 0, tn = 0;

    for (int i = 0; i < count; ++i) {
        float x = samples[i];
        out.push(x);
        float low = bassLP2_.process(bassLP1_.process(x));
        float belowMid = midLP2_.process(midLP1_.process(x));
        b = bassEnv_.process(low);
        m = midEnv_.process(belowMid - low);
        t = trebleEnv_.process(x - belowMid);
        l = levelEnv_.process(x);
        bn = bassAGC_.normalize(b, floorLevel);
        mn = midAGC_.normalize(m, floorLevel * 0.6f);
        tn = trebleAGC_.normalize(t, floorLevel * 0.4f);
        bassAverage_ += (b - bassAverage_) * averageCoef_;
    }
    out.publish(bn, mn, tn, std::min(1.0f, l * 2));

    samplesSinceBeat_ += count;
    float threshold = out.beatThreshold.load(std::memory_order_relaxed);
    if (samplesSinceBeat_ >= refractorySamples_ && b > floorLevel &&
        b > bassAverage_ * threshold && b > lastBass_) {
        float strength = std::min(1.0f, std::max(0.25f, (b / std::max(bassAverage_, 1e-6f) - 1) / 1.5f));
        out.publishBeat(strength);
        samplesSinceBeat_ = 0;
    }
    lastBass_ = b;
}


// ---------------------------------------------------------------------------------------------
// Device list (PulseAudio introspection, run synchronously on a private main loop)
// ---------------------------------------------------------------------------------------------

namespace {

struct ListState {
    std::vector<AudioSource>* out;
    int pending = 0;
    bool failed = false;
};

void onSource(pa_context*, const pa_source_info* i, int eol, void* user) {
    auto* st = static_cast<ListState*>(user);
    if (eol != 0 || !i) { st->pending--; return; }
    AudioSource s;
    s.id = widen(i->name ? i->name : "");
    std::string desc = i->description ? i->description : (i->name ? i->name : "Audio device");
    if (i->monitor_of_sink != PA_INVALID_INDEX) {
        // "Monitor of Built-in Audio" -> "Built-in Audio" (what it plays).
        const char* prefix = "Monitor of ";
        if (desc.rfind(prefix, 0) == 0) desc = desc.substr(std::strlen(prefix));
        s.loopback = true;
    }
    s.name = widen(desc);
    st->out->push_back(s);
}

void onState(pa_context* c, void* user) {
    auto* st = static_cast<ListState*>(user);
    pa_context_state_t s = pa_context_get_state(c);
    if (s == PA_CONTEXT_READY) {
        st->pending = 1;
        pa_operation* op = pa_context_get_source_info_list(c, onSource, st);
        if (op) pa_operation_unref(op);
        else st->pending = 0;
    } else if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED) {
        st->failed = true;
    }
}

/// The name of PulseAudio's current default output (loopback) or input. "" if it doesn't answer
/// within a second.
std::string defaultDeviceName(bool loopback) {
    struct Query {
        bool loopback;
        std::string name;
        bool done = false, failed = false;
    } q{loopback};
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) return "";
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "Dropocalypse");
    if (ctx) {
        pa_context_set_state_callback(ctx, [](pa_context* c, void* user) {
            auto* q = static_cast<Query*>(user);
            const pa_context_state_t st = pa_context_get_state(c);
            if (st == PA_CONTEXT_READY) {
                pa_operation* op = pa_context_get_server_info(c, [](pa_context*, const pa_server_info* info, void* u) {
                    auto* q = static_cast<Query*>(u);
                    if (info) {
                        const char* n = q->loopback ? info->default_sink_name : info->default_source_name;
                        if (n) q->name = n;
                    }
                    q->done = true;
                }, q);
                if (op) pa_operation_unref(op); else q->failed = true;
            } else if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
                q->failed = true;
            }
        }, &q);
        if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!q.done && !q.failed && std::chrono::steady_clock::now() < deadline)
                pa_mainloop_iterate(ml, 0, nullptr), std::this_thread::sleep_for(std::chrono::milliseconds(2));
            pa_context_disconnect(ctx);
        }
        pa_context_unref(ctx);
    }
    pa_mainloop_free(ml);
    return q.done ? q.name : "";
}

} // namespace

std::vector<AudioSource> AudioCapture::listSources() {
    std::vector<AudioSource> devices;
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) return {};
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "Dropocalypse");
    ListState st{&devices};
    st.pending = -1;
    if (ctx) {
        pa_context_set_state_callback(ctx, onState, &st);
        if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!st.failed && st.pending != 0 && std::chrono::steady_clock::now() < deadline)
                pa_mainloop_iterate(ml, 0, nullptr), std::this_thread::sleep_for(std::chrono::milliseconds(2));
            pa_context_disconnect(ctx);
        }
        pa_context_unref(ctx);
    }
    pa_mainloop_free(ml);

    // Defaults first, then outputs (what they play), then microphones / inputs.
    std::vector<AudioSource> out;
    out.push_back({L"", L"What this computer is playing", true});
    out.push_back({L"", L"Default microphone", false});
    for (const auto& d : devices) if (d.loopback) out.push_back({d.id, L"Playing on: " + d.name, true});
    for (const auto& d : devices) if (!d.loopback) out.push_back({d.id, d.name, false});
    return out;
}

// ---------------------------------------------------------------------------------------------
// Capture thread
// ---------------------------------------------------------------------------------------------

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() { stop(); }

std::wstring AudioCapture::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void AudioCapture::setStatus(const std::wstring& s) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = s;
}

void AudioCapture::start(const std::string& key) {
    stop();
    bool loopback = key.rfind("mic:", 0) != 0; // anything not "mic:" is loopback
    std::string idPart;
    auto colon = key.find(':');
    if (colon != std::string::npos) idPart = key.substr(colon + 1);
    stopRequested_ = false;
    thread_ = std::thread(&AudioCapture::run, this, widen(idPart), loopback);
}

void AudioCapture::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

void AudioCapture::run(std::wstring id, bool loopback) {
    const uint32_t rate = 48000;
    std::string device = narrow(id);
    if (device.empty()) device = loopback ? "@DEFAULT_MONITOR@" : "@DEFAULT_SOURCE@";

    while (!stopRequested_) {
        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_FLOAT32LE;
        spec.rate = rate;
        spec.channels = 2;
        pa_buffer_attr attr{};
        attr.maxlength = (uint32_t)-1;
        attr.fragsize = (uint32_t)pa_usec_to_bytes(5000, &spec); // ~5 ms per read
        attr.tlength = attr.prebuf = attr.minreq = (uint32_t)-1;

        int err = 0;
        pa_simple* s = pa_simple_new(nullptr, "Dropocalypse", PA_STREAM_RECORD, device.c_str(),
                                     loopback ? "Visualizer (system audio)" : "Visualizer (microphone)", &spec, nullptr,
                                     &attr, &err);
        if (!s) {
            setStatus(loopback ? L"Couldn't listen to the audio output. Is PulseAudio or PipeWire running? Retrying…"
                               : L"Couldn't open the microphone. Retrying…");
            for (int i = 0; i < 20 && !stopRequested_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        sampleRate = (int)rate;
        FeatureExtractor extractor(rate);
        const uint32_t frames = 240; // 5 ms
        std::vector<float> stereo(frames * 2), mono(frames);
        running_ = true;
        {
            std::string name = narrow(id);
            if (name.empty()) name = loopback ? "what this computer is playing" : "the default microphone";
            setStatus(L"Listening to " + widen(name) + L" · 48000 Hz · 5 ms blocks");
        }

        // Following the default device: check every two seconds (on a helper thread, so reading
        // never waits) whether the user switched outputs, for example to HDMI or headphones.
        std::atomic<bool> defaultChanged{false}, watchStop{false};
        std::thread watcher;
        if (id.empty()) {
            watcher = std::thread([&] {
                const std::string first = defaultDeviceName(loopback);
                while (!watchStop && !stopRequested_) {
                    for (int i = 0; i < 20 && !watchStop && !stopRequested_; ++i)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (watchStop || stopRequested_) break;
                    const std::string now = defaultDeviceName(loopback);
                    if (!first.empty() && !now.empty() && now != first) { defaultChanged = true; break; }
                }
            });
        }

        bool failed = false;
        while (!stopRequested_) {
            if (defaultChanged) { failed = true; break; }
            if (pa_simple_read(s, stereo.data(), stereo.size() * sizeof(float), &err) < 0) { failed = true; break; }
            for (uint32_t i = 0; i < frames; ++i) mono[i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
            extractor.process(mono.data(), (int)frames, analysis);
            if (recordActive_.load(std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> lock(recordMutex_);
                if (recordSink_) recordSink_(stereo.data(), frames, rate);
            }
        }
        watchStop = true;
        if (watcher.joinable()) watcher.join();
        pa_simple_free(s);
        running_ = false;
        if (failed && !stopRequested_) {
            setStatus(L"Audio device changed, reconnecting…");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
}
