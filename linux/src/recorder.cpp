// Dropocalypse for Linux - MP4 recording through ffmpeg. See recorder.h.

#include "recorder.h"
#include "audio.h"
#include "core_logic.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern char** environ;

const Recorder::Quality Recorder::kQualities[3] = {
    {"720p", 1280, 720, 60, 8000000},
    {"1080p", 1920, 1080, 60, 16000000},
    {"4K", 3840, 2160, 30, 40000000},
};

static constexpr uint32_t kAudioRate = 48000;

static std::string findFfmpeg() {
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find(':', i);
        if (j == std::string::npos) j = p.size();
        std::string dir = p.substr(i, j - i);
        if (!dir.empty()) {
            std::string exe = dir + "/ffmpeg";
            if (access(exe.c_str(), X_OK) == 0) return exe;
        }
        i = j + 1;
    }
    return {};
}

Recorder::Recorder() = default;

Recorder::~Recorder() {
    if (recording_) stop(nullptr);
    waitIdle();
}

bool Recorder::available() { return !findFfmpeg().empty(); }

double Recorder::elapsedSeconds() const {
    if (!recording_) return 0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

void Recorder::waitIdle() {
    if (finisher_.joinable()) finisher_.join();
}

bool Recorder::start(const std::wstring& path, int qualityIndex, bool portrait, std::wstring& error) {
    if (recording_) return false;
    waitIdle();
    std::string ffmpeg = findFfmpeg();
    if (ffmpeg.empty()) {
        error = L"Recording needs ffmpeg. Install it with your package manager (for example: sudo apt install ffmpeg), "
                L"then try again.";
        return false;
    }
    const Quality& q = kQualities[std::clamp(qualityIndex, 0, 2)];
    const RecordingPixels px = recordingPixels(qualityIndex, portrait);
    width_ = px.width;
    height_ = px.height;
    fps_ = q.fps;
    path_ = path;

    // GPU: a framebuffer at the video size and a ring of pixel buffers for asynchronous readback.
    releaseGpu();
    glGenRenderbuffers(1, &rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width_, height_);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_);
    bool fboOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenBuffers(kPbos, pbo_);
    for (int i = 0; i < kPbos; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)width_ * height_ * 4, nullptr, GL_STREAM_READ);
        pboSlots_[i] = 0;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboNext_ = 0;
    if (!fboOk) {
        releaseGpu();
        error = L"The graphics driver couldn't make a " + widen(q.label) + L" video buffer. Try a smaller size.";
        return false;
    }

    // Audio goes through a named pipe in a private temp folder.
    char tmpl[] = "/tmp/dropocalypse-rec-XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        releaseGpu();
        error = L"Couldn't create a temporary folder.";
        return false;
    }
    tmpDir_ = dir;
    fifoPath_ = tmpDir_ + "/audio.pcm";
    if (mkfifo(fifoPath_.c_str(), 0600) != 0) {
        rmdir(tmpDir_.c_str());
        releaseGpu();
        error = L"Couldn't set up the audio pipe for recording.";
        return false;
    }

    int pipeFd[2];
    if (pipe(pipeFd) != 0) {
        unlink(fifoPath_.c_str());
        rmdir(tmpDir_.c_str());
        releaseGpu();
        error = L"Couldn't start ffmpeg.";
        return false;
    }
    fcntl(pipeFd[1], F_SETFD, FD_CLOEXEC);

    std::string size = std::to_string(width_) + "x" + std::to_string(height_);
    std::string rate = std::to_string(fps_);
    std::string bitrate = std::to_string(q.bitrate);
    std::string bufsize = std::to_string(q.bitrate * 2);
    std::string gop = std::to_string(fps_ * 2);
    std::string out = narrow(path);
    std::vector<std::string> args = {
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba", "-s", size, "-framerate", rate, "-i", "pipe:0",
        "-f", "s16le", "-ar", std::to_string(kAudioRate), "-ac", "2", "-i", fifoPath_,
        "-vf", "vflip,format=yuv420p",
        "-c:v", "libx264", "-preset", "veryfast", "-profile:v", "high", "-b:v", bitrate, "-maxrate", bitrate,
        "-bufsize", bufsize, "-g", gop,
        "-c:a", "aac", "-b:a", "192k",
        "-movflags", "+faststart", out};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipeFd[0], 0);
    posix_spawn_file_actions_addclose(&fa, pipeFd[0]);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    int rc = posix_spawn(&pid, ffmpeg.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipeFd[0]);
    if (rc != 0) {
        close(pipeFd[1]);
        unlink(fifoPath_.c_str());
        rmdir(tmpDir_.c_str());
        releaseGpu();
        error = L"Couldn't start ffmpeg.";
        return false;
    }
    pid_ = pid;
    videoFd_ = pipeFd[1];
    signal(SIGPIPE, SIG_IGN);   // a crashed ffmpeg must not take Dropocalypse down with it

    failed_ = false;
    {
        std::lock_guard<std::mutex> l(vMutex_);
        vQueue_.clear();
        vStop_ = false;
    }
    {
        std::lock_guard<std::mutex> l(aMutex_);
        aPending_.clear();
        aStop_ = false;
        resamplePos_ = 0;
        lastL_ = lastR_ = 0;
        audioFramesQueued_ = 0;
    }
    slotsWritten_ = 0;
    start_ = std::chrono::steady_clock::now();
    videoThread_ = std::thread(&Recorder::videoWorker, this);
    audioThread_ = std::thread(&Recorder::audioWorker, this);
    recording_ = true;
    return true;
}

void Recorder::releaseGpu() {
    if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (rb_) { glDeleteRenderbuffers(1, &rb_); rb_ = 0; }
    if (pbo_[0]) { glDeleteBuffers(kPbos, pbo_); std::fill(std::begin(pbo_), std::end(pbo_), 0u); }
}

void Recorder::captureFrame(int winW, int winH, float cx, float cy, float cw, float ch) {
    if (!recording_ || winW <= 0 || winH <= 0) return;
    // Source rectangle in framebuffer pixels (OpenGL rows start at the bottom).
    int sx0 = (int)std::lround(cx), sw = std::max(1, (int)std::lround(cw));
    int sh = std::max(1, (int)std::lround(ch));
    int sy0 = winH - (int)std::lround(cy) - sh;
    const int srcW = sw, srcH = sh;
    if (failed_) return;

    // How many timeline slots are due: one per 1/fps of wall-clock time.
    double t = elapsedSeconds();
    int64_t dueSlots = (int64_t)std::floor(t * fps_) + 1;
    int64_t pendingSlots = 0;
    for (int s : pboSlots_) pendingSlots += s;
    int64_t slots = dueSlots - (slotsWritten_ + pendingSlots);
    if (slots <= 0) return;                 // the screen draws faster than the video's frame rate
    slots = std::min<int64_t>(slots, fps_); // after a long stall, don't flood the encoder

    // Letterboxed copy of the window into the video framebuffer (rows bottom-up, flipped by ffmpeg).
    float scale = std::min((float)width_ / srcW, (float)height_ / srcH);
    int dw = (int)std::lround(srcW * scale), dh = (int)std::lround(srcH * scale);
    int dx = (width_ - dw) / 2, dy = (height_ - dh) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBlitFramebuffer(sx0, sy0, sx0 + srcW, sy0 + srcH, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Start reading this frame into the next pixel buffer...
    int slot = pboNext_;
    pboNext_ = (pboNext_ + 1) % kPbos;
    // ...but first collect the frame that buffer still holds (read a few frames ago).
    if (pboSlots_[slot] > 0) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
        const uint8_t* px = (const uint8_t*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)width_ * height_ * 4,
                                                              GL_MAP_READ_BIT);
        if (px) {
            std::vector<uint8_t> frame(px, px + (size_t)width_ * height_ * 4);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            std::lock_guard<std::mutex> l(vMutex_);
            for (int k = 0; k < pboSlots_[slot]; ++k) {
                if (k + 1 < pboSlots_[slot]) vQueue_.push_back(frame);
                else vQueue_.push_back(std::move(frame));
            }
            // Don't let a slow disk eat all the memory: drop the oldest frames past ~1 s.
            while ((int)vQueue_.size() > fps_ * 2) vQueue_.pop_front();
        }
        slotsWritten_ += pboSlots_[slot];
        pboSlots_[slot] = 0;
        vCv_.notify_one();
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    pboSlots_[slot] = (int)slots;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Recorder::pushAudio(const float* stereo, uint32_t frames, uint32_t rate) {
    if (!recording_ || rate == 0) return;
    std::lock_guard<std::mutex> l(aMutex_);
    // Linear resample to 48 kHz.
    double step = (double)rate / kAudioRate;
    for (uint32_t i = 0; i < frames; ++i) {
        float L = stereo[i * 2], R = stereo[i * 2 + 1];
        while (resamplePos_ < 1.0) {
            float k = (float)resamplePos_;
            float l = lastL_ + (L - lastL_) * k, r = lastR_ + (R - lastR_) * k;
            aPending_.push_back((int16_t)std::lround(std::clamp(l, -1.0f, 1.0f) * 32767.0f));
            aPending_.push_back((int16_t)std::lround(std::clamp(r, -1.0f, 1.0f) * 32767.0f));
            ++audioFramesQueued_;
            resamplePos_ += step;
        }
        resamplePos_ -= 1.0;
        lastL_ = L;
        lastR_ = R;
    }
    aCv_.notify_one();
}

static bool writeAll(int fd, const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    while (size > 0) {
        ssize_t n = write(fd, p, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

void Recorder::videoWorker() {
    for (;;) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> l(vMutex_);
            vCv_.wait(l, [&] { return vStop_ || !vQueue_.empty(); });
            if (vQueue_.empty()) break;   // stopping and drained
            frame = std::move(vQueue_.front());
            vQueue_.pop_front();
        }
        if (!failed_ && !writeAll(videoFd_, frame.data(), frame.size())) failed_ = true;
    }
    close(videoFd_);
    videoFd_ = -1;
}

void Recorder::audioWorker() {
    // Opening a FIFO for writing waits until ffmpeg opens it for reading.
    int fd = open(fifoPath_.c_str(), O_WRONLY);
    if (fd < 0) { failed_ = true; return; }
    int64_t framesWritten = 0;
    for (;;) {
        std::vector<int16_t> chunk;
        bool stopping;
        {
            std::unique_lock<std::mutex> l(aMutex_);
            aCv_.wait_for(l, std::chrono::milliseconds(50), [&] { return aStop_ || !aPending_.empty(); });
            chunk.swap(aPending_);
            stopping = aStop_;
        }
        // Keep the sound in step with the clock: if no audio arrived for a while, write silence.
        int64_t due = (int64_t)(clockSeconds() * kAudioRate);
        int64_t have = framesWritten + (int64_t)chunk.size() / 2;
        if (due - have > kAudioRate / 10) chunk.insert(chunk.begin(), (size_t)(due - have) * 2, (int16_t)0);
        if (!chunk.empty()) {
            if (!writeAll(fd, chunk.data(), chunk.size() * sizeof(int16_t))) { failed_ = true; break; }
            framesWritten += (int64_t)chunk.size() / 2;
        }
        if (stopping) break;
    }
    close(fd);
}

void Recorder::stop(std::function<void(const std::wstring&, bool)> onDone) {
    if (!recording_) return;
    stopSeconds_ = elapsedSeconds();
    recording_ = false;
    onDone_ = std::move(onDone);

    // Hand over the frames still in the pixel buffers.
    for (int k = 0; k < kPbos; ++k) {
        int slot = (pboNext_ + k) % kPbos;
        if (pboSlots_[slot] <= 0) continue;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
        const uint8_t* px = (const uint8_t*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)width_ * height_ * 4,
                                                              GL_MAP_READ_BIT);
        if (px) {
            std::vector<uint8_t> frame(px, px + (size_t)width_ * height_ * 4);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            std::lock_guard<std::mutex> l(vMutex_);
            for (int i = 0; i < pboSlots_[slot]; ++i) vQueue_.push_back(frame);
        }
        pboSlots_[slot] = 0;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    releaseGpu();

    {
        std::lock_guard<std::mutex> l(vMutex_);
        vStop_ = true;
    }
    vCv_.notify_all();
    {
        std::lock_guard<std::mutex> l(aMutex_);
        aStop_ = true;
    }
    aCv_.notify_all();
    finisher_ = std::thread(&Recorder::finish, this);
}

void Recorder::finish() {
    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    int status = 0;
    bool ok = false;
    if (pid_ > 0) {
        // ffmpeg finishes the file once both inputs end; give it time, then stop it.
        for (int i = 0; i < 600; ++i) {
            pid_t r = waitpid(pid_, &status, WNOHANG);
            if (r == pid_) { ok = WIFEXITED(status) && WEXITSTATUS(status) == 0; break; }
            if (r < 0) break;
            if (i == 599) { kill(pid_, SIGTERM); waitpid(pid_, &status, 0); }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        pid_ = -1;
    }
    unlink(fifoPath_.c_str());
    rmdir(tmpDir_.c_str());
    ok = ok && !failed_;
    if (onDone_) onDone_(path_, ok);
}
