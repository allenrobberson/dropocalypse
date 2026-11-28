// Dropocalypse for Linux - records the visuals (without the toolbar) and the music to an MP4.
//
// Video: each frame, the rendered picture is scaled (letterboxed) into a fixed-size framebuffer on
// the GPU and read back through a small ring of pixel buffers, so the render loop never waits.
// Frames go out on a constant frame-rate timeline (repeated if the screen draws slower).
// Audio: the capture thread pushes stereo float samples, which become 16-bit PCM at 48 kHz;
// gaps (silence, a device change) are filled so sound stays in step with the picture.
// Encoding is done by ffmpeg (H.264 + AAC), which nearly every Linux distribution packages:
// raw frames go to its standard input and the audio through a named pipe.
#pragma once

#include "gl.h"

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

    /// True if ffmpeg can be found (recording needs it).
    static bool available();

    /// Starts writing `path`. `portrait` uses 9:16 (720x1280, 1080x1920, 2160x3840).
    /// Returns false (and sets error) if it can't start.
    bool start(const std::wstring& path, int qualityIndex, bool portrait, std::wstring& error);
    /// Stops; the file is finished on a worker thread and `onDone(path, ok)` is called there.
    void stop(std::function<void(const std::wstring&, bool)> onDone);

    bool isRecording() const { return recording_.load(); }
    /// Waits until the last file has been finished (call before exiting).
    void waitIdle();
    double elapsedSeconds() const;

    /// Call right after the visuals are drawn into the window's framebuffer (before any UI).
    /// `x, y, w, h` is the content rectangle in window pixels (top-left origin), so a pillarboxed
    /// portrait frame is recorded without the black bars.
    void captureFrame(int srcWidth, int srcHeight, float x, float y, float w, float h);

    /// Called from the audio thread with interleaved stereo float samples.
    void pushAudio(const float* stereo, uint32_t frames, uint32_t sampleRate);

private:
    void releaseGpu();
    void videoWorker();
    void audioWorker();
    void finish();

    // GPU side (render thread only).
    GLuint fbo_ = 0, rb_ = 0;
    static constexpr int kPbos = 3;
    GLuint pbo_[kPbos] = {};
    int pboSlots_[kPbos] = {};   // how many timeline slots each pending frame fills (0 = empty)
    int pboNext_ = 0;

    int width_ = 0, height_ = 0, fps_ = 60;
    std::atomic<bool> recording_{false};
    std::chrono::steady_clock::time_point start_;
    std::atomic<double> stopSeconds_{0};
    /// Recording time so far, frozen at the moment Stop was pressed.
    double clockSeconds() const { return recording_ ? elapsedSeconds() : stopSeconds_.load(); }
    int64_t slotsWritten_ = 0;   // video frames handed to the encoder so far

    // ffmpeg.
    pid_t pid_ = -1;
    int videoFd_ = -1;
    std::string fifoPath_, tmpDir_;
    std::wstring path_;
    std::atomic<bool> failed_{false};
    std::atomic<int> workersDone_{0};   // video and audio writers that have finished

    // Video queue (render thread -> video writer).
    std::mutex vMutex_;
    std::condition_variable vCv_;
    std::deque<std::vector<uint8_t>> vQueue_;
    bool vStop_ = false;

    // Audio (resampled to 48 kHz stereo 16-bit).
    std::mutex aMutex_;
    std::condition_variable aCv_;
    std::vector<int16_t> aPending_;
    bool aStop_ = false;
    double resamplePos_ = 0;
    float lastL_ = 0, lastR_ = 0;
    int64_t audioFramesQueued_ = 0;

    std::thread videoThread_, audioThread_, finisher_;
    std::function<void(const std::wstring&, bool)> onDone_;
};
