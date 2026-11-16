// Dropocalypse for Windows - records the visuals (without the toolbar) and the music to an MP4.
//
// Video: each frame, the rendered picture is scaled (letterboxed) into a fixed-size texture on
// the GPU, read back through a small ring of staging textures and handed to a Media Foundation
// sink writer (H.264). Audio: the capture thread pushes stereo float samples, which become
// 16-bit PCM at 48 kHz for the AAC encoder. Writing happens on a worker thread.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IMFSinkWriter;

class Recorder {
public:
    struct Quality {
        const char* label;
        int width, height, fps;
        unsigned bitrate;
    };
    static const Quality kQualities[3]; // 720p, 1080p, 4K

    Recorder();
    ~Recorder();

    bool init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error);

    /// Starts writing `path`. `portrait` uses 9:16 (720x1280, 1080x1920, 2160x3840).
    /// Returns false (and sets error) if the file can't be created.
    bool start(const std::wstring& path, int qualityIndex, bool portrait, std::wstring& error);
    /// Stops; the file is finished on the worker thread and `onDone(path, ok)` is called there.
    void stop(std::function<void(const std::wstring&, bool)> onDone);

    bool isRecording() const { return recording_.load(); }
    /// Waits until the last file has been finished (call before exiting).
    void waitIdle() { if (worker_.joinable()) worker_.join(); }
    double elapsedSeconds() const;

    /// Call right after the visuals are drawn into `source` (before any UI), once per frame.
    /// `uvX/uvY/uvW/uvH` is the content rectangle in texture space (top-left origin), so a
    /// pillarboxed portrait frame is recorded without the black bars.
    void captureFrame(ID3D11Texture2D* source, float uvX = 0, float uvY = 0, float uvW = 1, float uvH = 1);

    /// Called from the audio thread with interleaved stereo float samples.
    void pushAudio(const float* stereo, uint32_t frames, uint32_t sampleRate);

private:
    struct Job {
        bool isVideo;
        int64_t time;            // 100 ns units
        int64_t duration;
        std::vector<uint8_t> data;
    };

    bool createGpuResources(int w, int h);
    void worker();
    void enqueue(Job&& j);
    void writeAudioFrames(const int16_t* pcm, uint32_t frames);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cropCB_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetRTV_;
    static constexpr int kStaging = 3;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_[kStaging];
    int64_t stagingTime_[kStaging] = {};
    bool stagingFull_[kStaging] = {};
    int stagingNext_ = 0;

    int width_ = 0, height_ = 0, fps_ = 60;
    std::atomic<bool> recording_{false};
    int64_t startQpc_ = 0, qpcFreq_ = 1;
    int64_t lastVideoTime_ = -1;

    IMFSinkWriter* writer_ = nullptr;
    unsigned long videoStream_ = 0, audioStream_ = 0;
    std::wstring path_;

    // Audio (resampled to 48 kHz stereo 16-bit).
    std::mutex audioMutex_;
    double resamplePos_ = 0;
    float lastL_ = 0, lastR_ = 0;
    uint32_t inRate_ = 0;
    int64_t audioFramesWritten_ = 0;
    std::vector<int16_t> audioPending_;

    std::thread worker_;
    std::mutex qMutex_;
    std::condition_variable qCv_;
    std::deque<Job> queue_;
    bool stopWorker_ = false;
    std::function<void(const std::wstring&, bool)> onDone_;
};
