// Dropocalypse for Windows - Direct3D 11 renderer.
#pragma once

#include "audio.h"
#include "core_logic.h"
#include "scenes.h"
#include "logo_layers.h"
#include "milkdrop_host.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

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
    /// Covers the built-in styles and the bundled MilkDrop presets (the first 64 styles).
    uint64_t cycleSkip = 0;
    /// MilkDrop presets from the user's own folder take part in auto-cycle only when this is on.
    bool cycleUserPresets = false;
    /// Set by the app: styles from here on are the user's presets (-1 = none).
    int userPresetStart = -1;
    bool inCycle(int i) const {
        if (userPresetStart >= 0 && i >= userPresetStart) return cycleUserPresets;
        return !cycleBitSkipped(cycleSkip, i);
    }
    void setInCycle(int i, bool on) {
        if (i < 0 || i >= 64) return;
        if (on) cycleSkip &= ~(1ull << i); else cycleSkip |= (1ull << i);
    }
    int cycleable() const { return (userPresetStart >= 0 && !cycleUserPresets) ? userPresetStart : -1; }
    bool showLogo = true;
    float logoSize = 0.32f;
    float pulse = 0.7f, squash = 0.6f, bounce = 0.5f, slide = 0.4f, wobble = 0.35f;
    float glow = 0.6f, logoTint = 0.0f;
    bool logoTrails = false;
    /// Logo layers (logo_layers.h): how hard each move is, 0 (none) to 100 (super wild).
    LayerAmounts layers;
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
/// Draws a text item (font and color indexes from core_logic.h) with a dark outline, ready to
/// use like a logo image. Returns an empty image if no font could be loaded.
LogoImage renderTextLogo(const std::string& utf8Text, int font, int color);

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

/// One per logo or text item. `variant` (core_logic.h) gives each item its own small delay,
/// direction and drift so several items react to the same beat without moving like clones.
class LogoMotion {
public:
    LogoPose update(float dt, const AudioSnapshot& audio, const RenderSettings& s,
                    const MotionVariant& variant = MotionVariant());

private:
    struct Spring {
        float x = 0, v = 0, k, c;
        Spring(float k_, float c_) : k(k_), c(c_) {}
        void step(float dt) { float a = -k * x - c * v; v += a * dt; x += v * dt; }
    };
    void kick(float strength, const RenderSettings& s);
    Spring squash_{320, 15}, slide_{38, 6.5f}, wobble_{130, 9};
    float bounceY_ = 0, bounceV_ = 0, side_ = 1, time_ = 0;
    int lastBeat_ = 0;
    bool started_ = false, pending_ = false;
    float pendingStrength_ = 0, pendingAt_ = 0;
};

// ---------------------------------------------------------------------------------------------

class StyleFX; // renderer.cpp

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error);
    /// The logos and text items to show, left to right (top to bottom in portrait), up to
    /// kMaxOverlayItems. An empty list shows the "YOUR LOGO" placeholder.
    void setItems(const std::vector<LogoImage>& images);

    /// Renders one frame into `target`. May advance settings.sceneIndex when auto-cycling.
    void render(ID3D11RenderTargetView* target, int width, int height, AudioAnalysis& analysis,
                double sampleRate, RenderSettings& settings);

    const char* sceneName() const {
        const auto& scenes = allScenes();
        int i = activeScene_ < 0 ? 0 : activeScene_;
        return i < (int)scenes.size() ? scenes[i].name : scenes[0].name;
    }

    /// MilkDrop presets: the host that plays them, the list they come from, and the user's folder.
    void setMilkdrop(MilkdropHost* host, const MilkCatalog* catalog, const std::filesystem::path& userDir) {
        milk_ = host;
        catalog_ = catalog;
        milkUserDir_ = userDir;
        milkLoaded_ = -1;   // the list may have changed: load the current preset again
    }

private:
    template <typename T> using Com = Microsoft::WRL::ComPtr<T>;

    struct Target {
        Com<ID3D11Texture2D> tex;
        Com<ID3D11RenderTargetView> rtv;
        Com<ID3D11ShaderResourceView> srv;
    };

    bool compile(const char* entry, const char* profile, Com<ID3DBlob>& out, std::wstring& error);
    void ensureFeedback(int width, int height);
    VisualScene blendedScene() const;
    void updateScene(double now, float dt, RenderSettings& s);
    void drawGpuSignal(const VisualScene& scene, const AudioSnapshot& audio, float t, float aspect);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;

    Com<ID3D11VertexShader> vsFullscreen_, vsGeom_, vsSignal_, vsLogo_;
    Com<ID3D11PixelShader> psWarp_, psGeom_, psPresent_, psLogo_;
    Com<ID3D11InputLayout> geomLayout_;
    Com<ID3D11Buffer> warpCB_, presentCB_, logoCB_, signalCB_, sampleBuf_, vertexBuffer_;
    Com<ID3D11ShaderResourceView> sampleSRV_;
    Com<ID3D11BlendState> blendNone_, blendAdd_, blendPremul_;
    Com<ID3D11SamplerState> sampZero_, sampClamp_, sampMip_;
    Com<ID3D11RasterizerState> raster_;

    Target feedback_[2];
    int feedbackIndex_ = 0, fbWidth_ = 0, fbHeight_ = 0;

    Com<ID3D11Texture2D> logoTex_[kMaxOverlayItems];
    Com<ID3D11ShaderResourceView> logoSRV_[kMaxOverlayItems];
    float logoAspect_[kMaxOverlayItems] = {1, 1, 1, 1};
    int itemCount_ = 0;

    /// Each item split into its separate shapes, one texture per shape.
    struct ItemLayers {
        std::vector<Com<ID3D11ShaderResourceView>> srv;
        std::vector<float> u0, v0, u1, v1, nx, ny;
        LayerMotion motion;
    };
    ItemLayers layers_[kMaxOverlayItems];
    float glitchPulse_ = 0;
    int lastGlitchBeat_ = 0;

    Spectrum spectrum_;
    LogoMotion motion_[kMaxOverlayItems];
    std::unique_ptr<StyleFX> fx_;
    LARGE_INTEGER qpcFreq_{}, qpcStart_{}, qpcLast_{};
    float flash_ = 0;
    int lastFlashBeat_ = 0;

    MilkdropHost* milk_ = nullptr;
    const MilkCatalog* catalog_ = nullptr;
    std::filesystem::path milkUserDir_;
    int milkLoaded_ = -1;

    VisualScene current_, previous_;
    float sceneBlend_ = 1;
    int activeScene_ = -1;
    double sceneStart_ = 0;

    static constexpr int MaxVertices = 96000;
    static constexpr int AdditiveCapacity = 52000; // the rest is for the alpha-blended "over" batch
};
