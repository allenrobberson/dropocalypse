// Dropocalypse for Linux - OpenGL 3.3 renderer (port of the Direct3D 11 one).
#pragma once

#include "audio.h"
#include "core_logic.h"
#include "scenes.h"

#include "gl.h"

#include <chrono>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct RenderSettings {
    int sceneIndex = 0;
    bool autoCycle = true;
    float cycleSeconds = 30;
    /// Bit i set = style i is left out of auto-cycle (unchecked in the style list).
    /// Stored as "skipped" so styles added in later versions are included automatically.
    uint32_t cycleSkip = 0;
    bool inCycle(int i) const { return !cycleBitSkipped(cycleSkip, i); }
    void setInCycle(int i, bool on) { if (on) cycleSkip &= ~(1u << i); else cycleSkip |= (1u << i); }
    bool showLogo = true;
    float logoSize = 0.32f;
    float pulse = 0.7f, squash = 0.6f, bounce = 0.5f, slide = 0.4f, wobble = 0.35f;
    float glow = 0.6f, logoTint = 0.0f;
    bool logoTrails = false;
    float beatFlash = 0.4f, brightness = 1.0f;
    /// False fills the window. True draws a centered 9:16 frame (and recordings match it).
    bool portrait = false;
};

/// Premultiplied RGBA8 image, rows top to bottom.
struct LogoImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

bool loadLogoImage(const std::wstring& path, LogoImage& out, std::wstring& error);
LogoImage placeholderLogo();

// ---------------------------------------------------------------------------------------------

class Spectrum {
public:
    static constexpr int FFTSize = 1024;
    static constexpr int BandCount = 48;
    static constexpr int WaveCount = 512;

    Spectrum();
    void update(const AudioAnalysis& analysis, double sampleRate);
    float bands[BandCount] = {};
    float wave[WaveCount] = {};

private:
    float samples_[FFTSize] = {};
    float window_[FFTSize] = {};
    float re_[FFTSize] = {}, im_[FFTSize] = {};
    float mags_[FFTSize / 2] = {};
    float bandPeaks_[BandCount] = {};
    int bandEdges_[BandCount + 1] = {};
    float wavePeak_ = 0.05f, overallPeak_ = 1e-3f;
    double configuredRate_ = 0;
};

struct LogoPose {
    float x = 0, y = 0, scaleX = 1, scaleY = 1, rotation = 0;
};

class LogoMotion {
public:
    LogoPose update(float dt, const AudioSnapshot& audio, const RenderSettings& s);

private:
    struct Spring {
        float x = 0, v = 0, k, c;
        Spring(float k_, float c_) : k(k_), c(c_) {}
        void step(float dt) { float a = -k * x - c * v; v += a * dt; x += v * dt; }
    };
    Spring squash_{320, 15}, slide_{38, 6.5f}, wobble_{130, 9};
    float bounceY_ = 0, bounceV_ = 0, side_ = 1, time_ = 0;
    int lastBeat_ = 0;
};

// ---------------------------------------------------------------------------------------------

class StyleFX; // renderer.cpp
struct GeomVertex; // renderer.cpp

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(std::wstring& error);
    void setLogo(const LogoImage& image);

    /// Renders one frame into framebuffer `target` (0 = the window). May advance settings.sceneIndex
    /// when auto-cycling.
    void render(GLuint target, int width, int height, AudioAnalysis& analysis, double sampleRate,
                RenderSettings& settings);

    const char* sceneName() const { return builtInScenes()[activeScene_ < 0 ? 0 : activeScene_].name; }

private:
    struct Target {
        GLuint tex = 0, fbo = 0;
    };

    void ensureFeedback(int width, int height);
    VisualScene blendedScene() const;
    void updateScene(double now, float dt, RenderSettings& s);
    void drawGpuSignal(const VisualScene& scene, const AudioSnapshot& audio, float t, float aspect);

    GLuint progWarp_ = 0, progGeom_ = 0, progPresent_ = 0, progLogo_ = 0, progSignal_ = 0;
    GLuint emptyVao_ = 0, geomVao_ = 0, vertexBuffer_ = 0;
    GLuint uboWarp_ = 0, uboPresent_ = 0, uboLogo_ = 0, uboSignal_ = 0;
    GLuint sampleTbo_ = 0, sampleTex_ = 0;   // wave samples + band levels for the signal shader

    Target feedback_[2];
    int feedbackIndex_ = 0, fbWidth_ = 0, fbHeight_ = 0;

    GLuint logoTex_ = 0;
    float logoAspect_ = 1;

    Spectrum spectrum_;
    LogoMotion motion_;
    std::unique_ptr<StyleFX> fx_;
    std::chrono::steady_clock::time_point start_, last_;
    float flash_ = 0;
    int lastFlashBeat_ = 0;

    VisualScene current_, previous_;
    float sceneBlend_ = 1;
    int activeScene_ = -1;
    double sceneStart_ = 0;

    static constexpr int MaxVertices = 96000;
    static constexpr int AdditiveCapacity = 52000; // the rest is for the alpha-blended "over" batch
    std::vector<GeomVertex> vertices_;
};
