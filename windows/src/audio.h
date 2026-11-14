// Dropocalypse for Windows - audio capture and real-time bass analysis.
#pragma once

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Shared state between the audio thread (single writer) and the render thread (reader).
// ---------------------------------------------------------------------------------------------

struct AudioSnapshot {
    float bass = 0;        // normalized 0..~1.5, instant attack
    float mid = 0;
    float treble = 0;
    float level = 0;       // 0..1
    int beatCount = 0;     // increments on every detected beat
    float beatStrength = 0;
};

class AudioAnalysis {
public:
    static constexpr int RingSize = 4096; // power of two
    static constexpr int RingMask = RingSize - 1;

    AudioAnalysis();

    // Settings written by the UI, read by the audio thread.
    std::atomic<float> sensitivity{1.0f};
    std::atomic<float> beatThreshold{1.35f};

    // Reader API.
    AudioSnapshot snapshot() const;
    void copyRecent(float* out, int count) const;

    // Writer API (audio thread).
    void push(float s) {
        int i = writeIndex_.load(std::memory_order_relaxed);
        ring_[i].store(s, std::memory_order_relaxed);
        writeIndex_.store((i + 1) & RingMask, std::memory_order_release);
    }
    void publish(float bass, float mid, float treble, float level) {
        bass_.store(bass, std::memory_order_relaxed);
        mid_.store(mid, std::memory_order_relaxed);
        treble_.store(treble, std::memory_order_relaxed);
        level_.store(level, std::memory_order_relaxed);
    }
    void publishBeat(float strength) {
        beatStrength_.store(strength, std::memory_order_relaxed);
        beatCount_.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<float> ring_[RingSize];
    std::atomic<int> writeIndex_{0};
    std::atomic<float> bass_{0}, mid_{0}, treble_{0}, level_{0}, beatStrength_{0};
    std::atomic<int> beatCount_{0};
};

// ---------------------------------------------------------------------------------------------
// DSP building blocks (identical in behavior to the macOS version).
// ---------------------------------------------------------------------------------------------

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad lowPass(double cutoff, double sampleRate, double q = 0.7071);
    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct EnvelopeFollower {
    float env = 0, attack = 1, release = 1;
    EnvelopeFollower() = default;
    EnvelopeFollower(double attackMs, double releaseMs, double sampleRate);
    float process(float x) {
        float r = std::fabs(x);
        env += (r - env) * (r > env ? attack : release);
        return env;
    }
};

struct AutoGain {
    float peak = 0.02f, decay = 0.9999f;
    AutoGain() = default;
    AutoGain(double halfLifeSeconds, double sampleRate);
    float normalize(float env, float floorLevel) {
        peak = std::fmax(env, peak * decay);
        return env / std::fmax(peak, floorLevel);
    }
};

/// Per-sample feature extractor; runs on the audio thread and never allocates.
class FeatureExtractor {
public:
    explicit FeatureExtractor(double sampleRate);
    void process(const float* samples, int count, AudioAnalysis& out);

private:
    Biquad bassLP1_, bassLP2_, midLP1_, midLP2_;
    EnvelopeFollower bassEnv_, midEnv_, trebleEnv_, levelEnv_;
    AutoGain bassAGC_, midAGC_, trebleAGC_;
    float bassAverage_ = 0, averageCoef_ = 0, lastBass_ = 0;
    int samplesSinceBeat_ = 0, refractorySamples_ = 0;
};

// ---------------------------------------------------------------------------------------------
// WASAPI capture.
// ---------------------------------------------------------------------------------------------

struct AudioSource {
    std::wstring id;      // empty = system default
    std::wstring name;
    bool loopback = false; // true = capture what an output device is playing
    std::string key() const; // "loop:<id>" or "mic:<id>" - stored in settings
};

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    AudioAnalysis analysis;

    static std::vector<AudioSource> listSources();

    /// key: "loop:" (default output, i.e. what the PC is playing), "mic:" (default mic),
    /// or "loop:<device id>" / "mic:<device id>".
    void start(const std::string& key);
    void stop();

    bool isRunning() const { return running_.load(); }
    std::atomic<int> sampleRate{48000};
    std::wstring status() const;

private:
    void run(std::wstring id, bool loopback);
    void setStatus(const std::wstring& s);

    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex statusMutex_;
    std::wstring status_ = L"Starting audio…";
};

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
