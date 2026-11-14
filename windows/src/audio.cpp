// Dropocalypse for Windows - WASAPI capture + feature extraction.
//
// Latency notes:
//  * Microphones / line-ins use IAudioClient3 with the smallest shared-mode period the driver
//    allows (often 128-256 frames, ~3-5 ms), falling back to the default period.
//  * "System audio" uses WASAPI loopback on the output device - no driver or admin rights.
//  * The capture thread is registered with MMCSS ("Pro Audio") so it isn't starved.
//  * Bass is tracked per sample with an IIR filter + envelope follower; nothing waits on an FFT.

#include "audio.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>

using Microsoft::WRL::ComPtr;

static constexpr double kPi = 3.14159265358979323846;

// PKEY_Device_FriendlyName, defined locally so no extra GUID library is needed.
static const PROPERTYKEY kFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// ---------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string AudioSource::key() const {
    return (loopback ? "loop:" : "mic:") + narrow(id);
}

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
// Device list
// ---------------------------------------------------------------------------------------------

static std::wstring friendlyName(IMMDevice* dev) {
    ComPtr<IPropertyStore> props;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return L"Audio device";
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring name = L"Audio device";
    if (SUCCEEDED(props->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) name = pv.pwszVal;
    PropVariantClear(&pv);
    return name;
}

std::vector<AudioSource> AudioCapture::listSources() {
    std::vector<AudioSource> out;
    out.push_back({L"", L"System audio (what this PC is playing)", true});
    out.push_back({L"", L"Default microphone / line-in", false});

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;

    auto add = [&](EDataFlow flow, bool loopback) {
        ComPtr<IMMDeviceCollection> coll;
        if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) return;
        UINT n = 0;
        coll->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            ComPtr<IMMDevice> dev;
            if (FAILED(coll->Item(i, &dev))) continue;
            LPWSTR idRaw = nullptr;
            if (FAILED(dev->GetId(&idRaw))) continue;
            AudioSource s;
            s.id = idRaw;
            CoTaskMemFree(idRaw);
            s.loopback = loopback;
            s.name = loopback ? L"Playing on: " + friendlyName(dev.Get()) : friendlyName(dev.Get());
            out.push_back(s);
        }
    };
    add(eCapture, false);
    add(eRender, true);
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

namespace {

enum class SampleKind { Float32, Int16, Int24, Int32, Unknown };

SampleKind sampleKind(const WAVEFORMATEX* wf) {
    WORD tag = wf->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        auto ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        tag = (WORD)ext->SubFormat.Data1; // KSDATAFORMAT_SUBTYPE_* share the format tag in Data1
    }
    if (tag == 3 /* IEEE float */ && wf->wBitsPerSample == 32) return SampleKind::Float32;
    if (tag == 1 /* PCM */) {
        switch (wf->wBitsPerSample) {
        case 16: return SampleKind::Int16;
        case 24: return SampleKind::Int24;
        case 32: return SampleKind::Int32;
        }
    }
    return SampleKind::Unknown;
}

void toMono(const BYTE* data, UINT32 frames, const WAVEFORMATEX* wf, SampleKind kind, float* out) {
    const int ch = std::max<int>(1, wf->nChannels);
    const float inv = 1.0f / ch;
    for (UINT32 f = 0; f < frames; ++f) {
        float sum = 0;
        for (int c = 0; c < ch; ++c) {
            size_t i = (size_t)f * ch + c;
            switch (kind) {
            case SampleKind::Float32: sum += reinterpret_cast<const float*>(data)[i]; break;
            case SampleKind::Int16: sum += reinterpret_cast<const int16_t*>(data)[i] / 32768.0f; break;
            case SampleKind::Int32: sum += reinterpret_cast<const int32_t*>(data)[i] / 2147483648.0f; break;
            case SampleKind::Int24: {
                const BYTE* p = data + i * 3;
                int32_t v = (int32_t)((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8;
                sum += v / 8388608.0f;
                break;
            }
            default: break;
            }
        }
        out[f] = sum * inv;
    }
}

std::wstring defaultDeviceId(IMMDeviceEnumerator* en, EDataFlow flow) {
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(flow, eConsole, &dev))) return {};
    LPWSTR id = nullptr;
    if (FAILED(dev->GetId(&id))) return {};
    std::wstring s = id;
    CoTaskMemFree(id);
    return s;
}

} // namespace

void AudioCapture::run(std::wstring id, bool loopback) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    const bool followDefault = id.empty();
    std::vector<float> mono(48000, 0.0f);

    while (!stopRequested_) {
        // ---- open the device -----------------------------------------------------------
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> cap;
        WAVEFORMATEX* wf = nullptr;
        std::wstring openedId;
        UINT32 periodFrames = 0;
        bool useEvent = true;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
        if (SUCCEEDED(hr)) {
            hr = followDefault ? en->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &dev)
                               : en->GetDevice(id.c_str(), &dev);
        }
        if (SUCCEEDED(hr)) {
            LPWSTR raw = nullptr;
            if (SUCCEEDED(dev->GetId(&raw))) { openedId = raw; CoTaskMemFree(raw); }
            hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf());
        }
        if (SUCCEEDED(hr)) hr = client->GetMixFormat(&wf);

        if (SUCCEEDED(hr)) {
            HRESULT init = E_FAIL;
            if (!loopback) {
                // Smallest shared-mode period the driver supports (Windows 10+).
                ComPtr<IAudioClient3> c3;
                if (SUCCEEDED(client.As(&c3))) {
                    UINT32 def = 0, fund = 0, minP = 0, maxP = 0;
                    if (SUCCEEDED(c3->GetSharedModeEnginePeriod(wf, &def, &fund, &minP, &maxP)) && minP > 0) {
                        init = c3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minP, wf, nullptr);
                        if (SUCCEEDED(init)) periodFrames = minP;
                    }
                }
            }
            if (FAILED(init)) {
                DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
                init = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, wf, nullptr);
                if (FAILED(init) && loopback) {
                    // Older Windows builds don't support event-driven loopback: poll instead.
                    client.Reset();
                    if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.GetAddressOf()))) {
                        init = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                  20 * 10000, 0, wf, nullptr);
                        useEvent = false;
                    }
                }
                if (SUCCEEDED(init)) {
                    REFERENCE_TIME defPeriod = 0, minPeriod = 0;
                    if (SUCCEEDED(client->GetDevicePeriod(&defPeriod, &minPeriod)))
                        periodFrames = (UINT32)(defPeriod * wf->nSamplesPerSec / 10000000);
                }
            }
            hr = init;
        }
        if (SUCCEEDED(hr) && useEvent) hr = client->SetEventHandle(ev);
        if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&cap));

        SampleKind kind = wf ? sampleKind(wf) : SampleKind::Unknown;
        if (SUCCEEDED(hr) && kind == SampleKind::Unknown) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = client->Start();

        if (FAILED(hr)) {
            running_ = false;
            if (hr == E_ACCESSDENIED)
                setStatus(L"Microphone access is off. Turn on Settings → Privacy & security → Microphone → "
                          L"“Let desktop apps access your microphone”.");
            else
                setStatus(loopback ? L"Couldn't open the audio output for capture. Retrying…"
                                   : L"Couldn't open the audio input. Check that one is plugged in. Retrying…");
            if (wf) CoTaskMemFree(wf);
            for (int i = 0; i < 20 && !stopRequested_; ++i) Sleep(100);
            continue;
        }

        const double sr = wf->nSamplesPerSec;
        sampleRate = (int)sr;
        FeatureExtractor extractor(sr);
        std::wstring name = dev ? friendlyName(dev.Get()) : L"";
        {
            wchar_t buf[512];
            if (periodFrames > 0)
                swprintf_s(buf, L"%s: %s · %d Hz · %.1f ms buffer", loopback ? L"System audio" : L"Listening",
                           name.c_str(), (int)sr, periodFrames * 1000.0 / sr);
            else
                swprintf_s(buf, L"%s: %s · %d Hz", loopback ? L"System audio" : L"Listening", name.c_str(), (int)sr);
            setStatus(buf);
        }
        running_ = true;

        // ---- capture loop ---------------------------------------------------------------
        auto lastData = std::chrono::steady_clock::now();
        auto lastDefaultCheck = lastData;
        bool restart = false;

        while (!stopRequested_ && !restart) {
            if (useEvent) WaitForSingleObject(ev, loopback ? 4 : 20); else Sleep(2);

            UINT32 packet = 0;
            bool gotData = false;
            while (SUCCEEDED(hr = cap->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;
                UINT32 done = 0;
                while (done < frames) {
                    UINT32 n = std::min<UINT32>(frames - done, (UINT32)mono.size());
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                        std::fill(mono.begin(), mono.begin() + n, 0.0f);
                    else
                        toMono(data + (size_t)done * wf->nBlockAlign, n, wf, kind, mono.data());
                    extractor.process(mono.data(), (int)n, analysis);
                    done += n;
                }
                cap->ReleaseBuffer(frames);
                gotData = true;
            }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) { restart = true; break; }

            auto now = std::chrono::steady_clock::now();
            if (gotData) {
                lastData = now;
            } else {
                // Loopback delivers nothing while the PC is silent; feed silence so things settle.
                double gap = std::chrono::duration<double>(now - lastData).count();
                if (gap > 0.02) {
                    int n = std::min((int)(gap * sr), (int)mono.size());
                    std::fill(mono.begin(), mono.begin() + n, 0.0f);
                    extractor.process(mono.data(), n, analysis);
                    lastData = now;
                }
            }

            // Follow the system default device when the user switches outputs/inputs.
            if (followDefault && now - lastDefaultCheck > std::chrono::seconds(2)) {
                lastDefaultCheck = now;
                std::wstring cur = defaultDeviceId(en.Get(), loopback ? eRender : eCapture);
                if (!cur.empty() && cur != openedId) restart = true;
            }
        }

        client->Stop();
        if (wf) CoTaskMemFree(wf);
        running_ = false;
        if (restart && !stopRequested_) {
            setStatus(L"Audio device changed, reconnecting…");
            Sleep(200);
        }
    }

    if (ev) CloseHandle(ev);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (SUCCEEDED(hrCo)) CoUninitialize();
}
