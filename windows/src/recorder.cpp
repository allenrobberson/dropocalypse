#include "recorder.h"
#include "core_logic.h"

#include <windows.h>
#include <codecapi.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

const Recorder::Quality Recorder::kQualities[3] = {
    {"720p", 1280, 720, 60, 8000000},
    {"1080p", 1920, 1080, 60, 16000000},
    {"4K", 3840, 2160, 30, 40000000},
};

namespace {

const char* kShader = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer CropCB : register(b0) {
    float cropX;
    float cropY;
    float cropW;
    float cropH;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_main(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ps_main(VSOut i) : SV_Target {
    float2 uv = float2(cropX, cropY) + i.uv * float2(cropW, cropH);
    return float4(tex.Sample(smp, uv).rgb, 1);
}
)";

const int kAudioRate = 48000;

int64_t qpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

template <class T> void safeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

} // namespace

Recorder::Recorder() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFreq_ = f.QuadPart;
}

Recorder::~Recorder() {
    if (recording_) stop(nullptr);
    if (worker_.joinable()) worker_.join();
}

bool Recorder::init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error) {
    device_ = device;
    ctx_ = ctx;
    ComPtr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), "rec", nullptr, nullptr, "vs_main", "vs_4_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kShader, std::strlen(kShader), "rec", nullptr, nullptr, "ps_main", "ps_4_0", 0, 0, &psb, &err))) {
        error = L"Couldn't prepare the recorder.";
        return false;
    }
    device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_);
    device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd, &sampler_);
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbd, nullptr, &cropCB_);
    return vs_ && ps_ && sampler_ && cropCB_;
}

bool Recorder::createGpuResources(int w, int h) {
    target_.Reset();
    targetRTV_.Reset();
    for (auto& s : staging_) s.Reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&d, nullptr, &target_))) return false;
    if (FAILED(device_->CreateRenderTargetView(target_.Get(), nullptr, &targetRTV_))) return false;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto& s : staging_)
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &s))) return false;
    for (int i = 0; i < kStaging; ++i) stagingFull_[i] = false;
    stagingNext_ = 0;
    return true;
}

bool Recorder::start(const std::wstring& path, int qualityIndex, bool portrait, std::wstring& error) {
    if (recording_) return true;
    if (worker_.joinable()) worker_.join(); // a previous file is still being finished
    const Quality& q = kQualities[std::clamp(qualityIndex, 0, 2)];
    const RecordingPixels px = recordingPixels(qualityIndex, portrait);
    width_ = px.width;
    height_ = px.height;
    fps_ = q.fps;
    if (!createGpuResources(width_, height_)) {
        error = L"Not enough graphics memory to record at this size. Try a smaller size.";
        return false;
    }

    static bool mfStarted = false;
    if (!mfStarted) {
        if (FAILED(MFStartup(MF_VERSION))) {
            error = L"Windows Media Foundation isn't available, so recording can't start.";
            return false;
        }
        mfStarted = true;
    }

    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attr.Get(), &writer_);
    if (FAILED(hr)) {
        error = L"Couldn't create the video file. Check that the recordings folder exists and you can save there.";
        return false;
    }

    auto fail = [&](const wchar_t* why) {
        error = why;
        safeRelease(writer_);
        DeleteFileW(path.c_str());
        return false;
    };

    // Video: RGB32 frames in, H.264 out.
    ComPtr<IMFMediaType> vOut, vIn;
    MFCreateMediaType(&vOut);
    vOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vOut->SetUINT32(MF_MT_AVG_BITRATE, q.bitrate);
    vOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    vOut->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    MFSetAttributeSize(vOut.Get(), MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(vOut.Get(), MF_MT_FRAME_RATE, fps_, 1);
    MFSetAttributeRatio(vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer_->AddStream(vOut.Get(), &videoStream_))) return fail(L"This PC can't encode H.264 video at this size. Try 1080p.");
    MFCreateMediaType(&vIn);
    vIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    vIn->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(width_ * 4)); // positive = top-down rows
    MFSetAttributeSize(vIn.Get(), MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(vIn.Get(), MF_MT_FRAME_RATE, fps_, 1);
    MFSetAttributeRatio(vIn.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer_->SetInputMediaType(videoStream_, vIn.Get(), nullptr)))
        return fail(L"Couldn't set up the video encoder. Try a smaller size.");

    // Audio: 48 kHz stereo 16-bit PCM in, AAC out.
    ComPtr<IMFMediaType> aOut, aIn;
    MFCreateMediaType(&aOut);
    aOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
    aOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000); // 192 kbps
    if (FAILED(writer_->AddStream(aOut.Get(), &audioStream_))) return fail(L"Couldn't set up the AAC audio encoder.");
    MFCreateMediaType(&aIn);
    aIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    aIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
    aIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    aIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
    if (FAILED(writer_->SetInputMediaType(audioStream_, aIn.Get(), nullptr)))
        return fail(L"Couldn't set up the audio encoder.");

    if (FAILED(writer_->BeginWriting())) return fail(L"Couldn't start writing the video file.");

    path_ = path;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        resamplePos_ = 0;
        inRate_ = 0;
        audioFramesWritten_ = 0;
        audioPending_.clear();
    }
    lastVideoTime_ = -1;
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        queue_.clear();
        stopWorker_ = false;
    }
    startQpc_ = qpcNow();
    recording_ = true;
    worker_ = std::thread(&Recorder::worker, this);
    return true;
}

double Recorder::elapsedSeconds() const {
    if (!recording_) return 0;
    return (double)(qpcNow() - startQpc_) / (double)qpcFreq_;
}

void Recorder::enqueue(Job&& j) {
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        // Don't let a slow disk eat all memory: drop video frames if far behind.
        if (j.isVideo) {
            int videoQueued = 0;
            for (auto& q : queue_) videoQueued += q.isVideo ? 1 : 0;
            if (videoQueued > fps_ * 2) return;
        }
        queue_.push_back(std::move(j));
    }
    qCv_.notify_one();
}

void Recorder::captureFrame(ID3D11Texture2D* source, float uvX, float uvY, float uvW, float uvH) {
    if (!recording_ || !source) return;
    const int64_t now100ns = (qpcNow() - startQpc_) * 10000000 / qpcFreq_;
    const int64_t frameLen = 10000000 / fps_;

    // Keep the audio track continuous while the PC is silent (loopback delivers nothing then).
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        int64_t wallFrames = now100ns * kAudioRate / 10000000;
        int64_t have = audioFramesWritten_ + (int64_t)audioPending_.size() / 2;
        if (wallFrames - have > kAudioRate / 5) {
            // At most one second per video frame: a long stall (a sleeping laptop) catches up in
            // steps instead of building minutes of silence on this thread at once.
            const int64_t gap = std::min<int64_t>(wallFrames - have - kAudioRate / 20, kAudioRate);
            std::vector<int16_t> silence((size_t)gap * 2, 0);
            writeAudioFrames(silence.data(), (uint32_t)(silence.size() / 2));
        }
    }

    // Read back the oldest staged frame (from a few frames ago, so the GPU is done with it).
    int slot = stagingNext_;
    if (stagingFull_[slot]) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx_->Map(staging_[slot].Get(), 0, D3D11_MAP_READ, 0, &m))) {
            Job j;
            j.isVideo = true;
            j.time = stagingTime_[slot];
            j.duration = frameLen;
            const size_t row = (size_t)width_ * 4;
            j.data.resize(row * height_);
            for (int y = 0; y < height_; ++y)
                std::memcpy(j.data.data() + row * y, (const uint8_t*)m.pData + (size_t)m.RowPitch * y, row);
            ctx_->Unmap(staging_[slot].Get(), 0);
            enqueue(std::move(j));
        }
        stagingFull_[slot] = false;
    }

    // Constant frame rate: frames sit exactly on a 1/fps grid whatever the screen's refresh.
    if (lastVideoTime_ < 0) lastVideoTime_ = 0;              // next slot
    if (now100ns < lastVideoTime_ - frameLen / 2) return;
    if (now100ns - lastVideoTime_ > frameLen * 2) lastVideoTime_ = (now100ns / frameLen) * frameLen;
    const int64_t slotTime = lastVideoTime_;
    lastVideoTime_ += frameLen;

    D3D11_TEXTURE2D_DESC sd{};
    source->GetDesc(&sd);
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(source, nullptr, &srv))) return;

    // Save the pipeline state we touch, so the renderer and UI are unaffected.
    ComPtr<ID3D11RenderTargetView> oldRTV;
    ComPtr<ID3D11DepthStencilView> oldDSV;
    ctx_->OMGetRenderTargets(1, &oldRTV, &oldDSV);
    UINT nvp = 1;
    D3D11_VIEWPORT oldVP{};
    ctx_->RSGetViewports(&nvp, &oldVP);
    ComPtr<ID3D11VertexShader> oldVS;
    ComPtr<ID3D11PixelShader> oldPS;
    ctx_->VSGetShader(&oldVS, nullptr, nullptr);
    ctx_->PSGetShader(&oldPS, nullptr, nullptr);
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ctx_->PSGetShaderResources(0, 1, &oldSRV);
    ComPtr<ID3D11SamplerState> oldSmp;
    ctx_->PSGetSamplers(0, 1, &oldSmp);
    D3D11_PRIMITIVE_TOPOLOGY oldTopo;
    ctx_->IAGetPrimitiveTopology(&oldTopo);
    ComPtr<ID3D11InputLayout> oldLayout;
    ctx_->IAGetInputLayout(&oldLayout);
    ComPtr<ID3D11BlendState> oldBlend;
    float oldFactor[4];
    UINT oldMask;
    ctx_->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
    ComPtr<ID3D11RasterizerState> oldRS;
    ctx_->RSGetState(&oldRS);
    ComPtr<ID3D11DepthStencilState> oldDS;
    UINT oldRef;
    ctx_->OMGetDepthStencilState(&oldDS, &oldRef);
    ComPtr<ID3D11Buffer> oldCB;
    ctx_->PSGetConstantBuffers(0, 1, &oldCB);

    // Letterbox the content rectangle (not the pillarbox bars) into the recording size.
    const float black[4] = {0, 0, 0, 1};
    ctx_->ClearRenderTargetView(targetRTV_.Get(), black);
    const float srcW = std::max(uvW, 0.001f) * (float)sd.Width;
    const float srcH = std::max(uvH, 0.001f) * (float)sd.Height;
    float sa = srcW / srcH;
    float ta = (float)width_ / (float)height_;
    struct Crop { float x, y, w, h; } crop{uvX, uvY, std::max(uvW, 0.001f), std::max(uvH, 0.001f)};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx_->Map(cropCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &crop, sizeof(crop));
        ctx_->Unmap(cropCB_.Get(), 0);
    }
    D3D11_VIEWPORT vp{};
    if (sa > ta) { vp.Width = (float)width_; vp.Height = width_ / sa; }
    else { vp.Height = (float)height_; vp.Width = height_ * sa; }
    vp.TopLeftX = (width_ - vp.Width) * 0.5f;
    vp.TopLeftY = (height_ - vp.Height) * 0.5f;
    vp.MaxDepth = 1;
    ID3D11RenderTargetView* rtv = targetRTV_.Get();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* s = srv.Get();
    ctx_->PSSetShaderResources(0, 1, &s);
    ID3D11SamplerState* smp = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11Buffer* cropBuf = cropCB_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &cropBuf);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->RSSetState(nullptr);
    ctx_->OMSetDepthStencilState(nullptr, 0);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSRV);
    ctx_->CopyResource(staging_[slot].Get(), target_.Get());
    stagingTime_[slot] = slotTime;
    stagingFull_[slot] = true;
    stagingNext_ = (slot + 1) % kStaging;

    // Restore.
    ID3D11RenderTargetView* ortv = oldRTV.Get();
    ctx_->OMSetRenderTargets(1, &ortv, oldDSV.Get());
    ctx_->RSSetViewports(1, &oldVP);
    ctx_->VSSetShader(oldVS.Get(), nullptr, 0);
    ctx_->PSSetShader(oldPS.Get(), nullptr, 0);
    ID3D11ShaderResourceView* osrv = oldSRV.Get();
    ctx_->PSSetShaderResources(0, 1, &osrv);
    ID3D11SamplerState* osmp = oldSmp.Get();
    ctx_->PSSetSamplers(0, 1, &osmp);
    ctx_->IASetPrimitiveTopology(oldTopo);
    ctx_->IASetInputLayout(oldLayout.Get());
    ID3D11Buffer* ocb = oldCB.Get();
    ctx_->PSSetConstantBuffers(0, 1, &ocb);
    ctx_->OMSetBlendState(oldBlend.Get(), oldFactor, oldMask);
    ctx_->RSSetState(oldRS.Get());
    ctx_->OMSetDepthStencilState(oldDS.Get(), oldRef);
}

// Called with audioMutex_ held.
void Recorder::writeAudioFrames(const int16_t* pcm, uint32_t frames) {
    audioPending_.insert(audioPending_.end(), pcm, pcm + (size_t)frames * 2);
    const size_t chunk = 1024 * 2;
    while (audioPending_.size() >= chunk) {
        Job j;
        j.isVideo = false;
        j.time = audioFramesWritten_ * 10000000 / kAudioRate;
        j.duration = (int64_t)1024 * 10000000 / kAudioRate;
        j.data.resize(chunk * sizeof(int16_t));
        std::memcpy(j.data.data(), audioPending_.data(), j.data.size());
        audioPending_.erase(audioPending_.begin(), audioPending_.begin() + chunk);
        audioFramesWritten_ += 1024;
        enqueue(std::move(j));
    }
}

void Recorder::pushAudio(const float* stereo, uint32_t frames, uint32_t sampleRate) {
    if (!recording_ || frames == 0 || sampleRate == 0) return;
    std::lock_guard<std::mutex> lock(audioMutex_);
    if (!recording_) return;
    if (sampleRate != inRate_) {
        inRate_ = sampleRate;
        resamplePos_ = 0;
    }
    // Linear resampling to 48 kHz (a no-op copy at 48 kHz).
    const double step = (double)sampleRate / kAudioRate;
    std::vector<int16_t> out;
    out.reserve((size_t)(frames / step + 4) * 2);
    auto toI16 = [](float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };
    double pos = resamplePos_;
    while (pos < frames) {
        int i = (int)std::floor(pos);
        float f = (float)(pos - i);
        float l0 = i <= 0 ? (i == 0 ? stereo[0] : lastL_) : stereo[(i) * 2];
        float r0 = i <= 0 ? (i == 0 ? stereo[1] : lastR_) : stereo[(i) * 2 + 1];
        float l1 = (i + 1 < (int)frames) ? stereo[(i + 1) * 2] : l0;
        float r1 = (i + 1 < (int)frames) ? stereo[(i + 1) * 2 + 1] : r0;
        out.push_back(toI16(l0 + (l1 - l0) * f));
        out.push_back(toI16(r0 + (r1 - r0) * f));
        pos += step;
    }
    resamplePos_ = pos - frames;
    lastL_ = stereo[(frames - 1) * 2];
    lastR_ = stereo[(frames - 1) * 2 + 1];
    writeAudioFrames(out.data(), (uint32_t)(out.size() / 2));
}

void Recorder::stop(std::function<void(const std::wstring&, bool)> onDone) {
    if (!recording_) return;
    recording_ = false;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        if (!audioPending_.empty()) {
            std::vector<int16_t> pad(1024 * 2, 0); // flush the last partial chunk
            writeAudioFrames(pad.data(), 1024);
        }
    }
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        onDone_ = std::move(onDone);
        stopWorker_ = true;
    }
    qCv_.notify_one();
}

void Recorder::worker() {
    bool ok = true;
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lock(qMutex_);
            qCv_.wait(lock, [&] { return !queue_.empty() || stopWorker_; });
            if (queue_.empty() && stopWorker_) break;
            j = std::move(queue_.front());
            queue_.pop_front();
        }
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(MFCreateMemoryBuffer((DWORD)j.data.size(), &buf))) continue;
        BYTE* p = nullptr;
        if (SUCCEEDED(buf->Lock(&p, nullptr, nullptr))) {
            std::memcpy(p, j.data.data(), j.data.size());
            buf->Unlock();
        }
        buf->SetCurrentLength((DWORD)j.data.size());
        ComPtr<IMFSample> sample;
        MFCreateSample(&sample);
        sample->AddBuffer(buf.Get());
        sample->SetSampleTime(j.time);
        sample->SetSampleDuration(j.duration);
        if (FAILED(writer_->WriteSample(j.isVideo ? videoStream_ : audioStream_, sample.Get()))) ok = false;
    }
    if (writer_) {
        if (FAILED(writer_->Finalize())) ok = false;
        safeRelease(writer_);
    }
    std::function<void(const std::wstring&, bool)> done;
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        done = std::move(onDone_);
    }
    if (done) done(path_, ok);
}
