// Pure update and auto-cycle decisions shared by the app and the unit tests.
// No Windows headers: the test binary compiles this on its own.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

/// Dotted versions compared numerically (0.10.0 > 0.9.2). Anything after "-" is ignored.
/// Equal versions are not newer. This is the same rule the Mac app uses.
inline std::vector<int> versionComponents(const std::string& s) {
    std::vector<int> v;
    std::string core = s.substr(0, s.find('-'));
    size_t i = 0;
    while (i <= core.size()) {
        size_t j = core.find('.', i);
        if (j == std::string::npos) j = core.size();
        v.push_back(std::atoi(core.substr(i, j - i).c_str()));
        i = j + 1;
    }
    return v;
}

inline bool versionIsNewer(const std::string& a, const std::string& b) {
    const std::vector<int> x = versionComponents(a);
    const std::vector<int> y = versionComponents(b);
    const size_t n = x.size() > y.size() ? x.size() : y.size();
    for (size_t i = 0; i < n; ++i) {
        const int p = i < x.size() ? x[i] : 0;
        const int q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}

/// What's New window: open it once after an update. `lastSeen` is the version whose notes were
/// last shown ("" when the settings predate the window). An empty `lastSeen` on an existing
/// install means "just updated from an older version"; on a brand-new install it means "first
/// run", which gets the welcome notice instead. Development builds never show it.
inline bool shouldShowWhatsNew(const std::string& lastSeen, const std::string& current, bool existingInstall) {
    if (current.find("dev") != std::string::npos || current == "0.0.0") return false;
    if (lastSeen.empty()) return existingInstall;
    return versionIsNewer(current, lastSeen);
}

/// Which notes to show (indices into `versions`, newest first as listed): every version after
/// `lastSeen` up to `current`, at most `maxEntries`. With no `lastSeen`, only `current`'s notes.
inline std::vector<int> whatsNewEntries(const std::vector<std::string>& versions, const std::string& lastSeen,
                                        const std::string& current, int maxEntries = 4) {
    std::vector<int> out;
    for (int i = 0; i < (int)versions.size() && (int)out.size() < maxEntries; ++i) {
        const std::string& v = versions[i];
        if (versionIsNewer(v, current)) continue;                       // notes for a later release
        bool wanted = lastSeen.empty() ? !versionIsNewer(current, v)    // == current
                                       : versionIsNewer(v, lastSeen);
        if (wanted) out.push_back(i);
    }
    return out;
}

/// The updater only talks to GitHub: the release API, the release pages and GitHub's download
/// servers (asset downloads redirect there). Checked before connecting and after redirects.
inline bool isTrustedUpdateHost(std::string host) {
    for (auto& c : host) c = (char)std::tolower((unsigned char)c);
    static const char* const allowed[] = {"api.github.com", "github.com", "objects.githubusercontent.com",
                                          "release-assets.githubusercontent.com"};
    for (const char* a : allowed)
        if (host == a) return true;
    return false;
}

/// The host of an "https://host[:port]/..." address, or "" when it isn't https.
inline std::string httpsHostOf(const std::string& url) {
    if (url.compare(0, 8, "https://") != 0) return "";
    size_t end = url.find_first_of(":/?#", 8);
    std::string host = url.substr(8, end == std::string::npos ? std::string::npos : end - 8);
    if (host.find('@') != std::string::npos) return "";   // no user:password@host tricks
    return host;
}

/// "<hash>  <file>" or "<hash> *<file>" from a sha256sum listing. Empty if `file` is absent.
inline std::string checksumFor(const std::string& file, const std::string& listing) {
    size_t i = 0;
    while (i < listing.size()) {
        size_t end = listing.find_first_of("\r\n", i);
        if (end == std::string::npos) end = listing.size();
        const std::string line = listing.substr(i, end - i);
        i = end;
        if (i < listing.size() && listing[i] == '\r') ++i;
        if (i < listing.size() && listing[i] == '\n') ++i;

        std::vector<std::string> parts;
        std::string cur;
        for (char c : line) {
            if (c == ' ') {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) parts.push_back(cur);
        if (parts.size() < 2) continue;
        std::string name = parts.back();
        if (!name.empty() && name[0] == '*') name.erase(name.begin());
        if (name == file) return parts.front();
    }
    return {};
}

/// Bit i set in `cycleSkip` means style i is left out of auto-cycle.
inline bool cycleBitSkipped(std::uint64_t cycleSkip, int index) {
    return index >= 0 && index < 64 && ((cycleSkip >> index) & 1u) != 0;
}

/// Next style after `index`. If every style is skipped, still advances by one
/// (auto-cycle keeps moving when nothing is checked). Styles at `cycleable` and above (MilkDrop
/// presets from the user's own folder, when they aren't included) are never chosen; -1 = all.
inline int nextCycleIndex(int index, int count, std::uint64_t cycleSkip, int cycleable = -1) {
    if (count <= 0) return 0;
    const int n = count;
    const int limit = (cycleable < 0 || cycleable > n) ? n : (cycleable == 0 ? n : cycleable);
    int requested = index % n;
    if (requested < 0) requested += n;
    int next = (requested + 1) % n;
    if (next >= limit) next = 0;
    for (int k = 1; k <= n; ++k) {
        const int c = (requested + k) % n;
        if (c < limit && !cycleBitSkipped(cycleSkip, c)) {
            next = c;
            break;
        }
    }
    return next;
}

/// Signature facts for one update. The app fills these in from WinVerifyTrust;
/// the decision itself does not call Windows.
struct UpdateSignatureFacts {
    bool runningIsSigned = false;
    std::string runningPublisher;
    bool updateIsSigned = false;
    bool updateTrusted = false;
    std::string updatePublisher;
};

/// Recording pixels for a quality index. 0 = 720p, 1 = 1080p, 2 = 4K; anything else clamps.
/// Landscape is 16:9. Portrait is the same frame turned 9:16 (720x1280, 1080x1920, 2160x3840).
/// This matches FrameLayout.recordingSize in Sources/DropocalypseCore/FrameLayout.swift.
struct RecordingPixels {
    int width = 0;
    int height = 0;
};

inline RecordingPixels recordingPixels(int quality, bool portrait) {
    const int q = quality < 0 ? 0 : (quality > 2 ? 2 : quality);
    const int landscape[3][2] = {{1280, 720}, {1920, 1080}, {3840, 2160}};
    RecordingPixels s;
    s.width = portrait ? landscape[q][1] : landscape[q][0];
    s.height = portrait ? landscape[q][0] : landscape[q][1];
    return s;
}

/// On-screen rectangle for the visuals. Origin is the top-left, in view pixels.
/// Landscape fills the view. Portrait is the largest 9:16 frame centered in it.
/// Matches FrameLayout.contentRect on the Mac.
struct ContentRect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

inline ContentRect fitContentRect(float viewWidth, float viewHeight, bool portrait) {
    const float vw = viewWidth > 0 ? viewWidth : 0;
    const float vh = viewHeight > 0 ? viewHeight : 0;
    ContentRect r;
    if (!portrait || vw < 1.0f || vh < 1.0f) {
        r.width = vw;
        r.height = vh;
        return r;
    }
    const float target = 9.0f / 16.0f;
    if (vw / vh > target) {
        r.height = vh;
        r.width = vh * target;
        r.x = (vw - r.width) * 0.5f;
        return r;
    }
    r.width = vw;
    r.height = vw / target;
    r.y = (vh - r.height) * 0.5f;
    return r;
}

// ---------------------------------------------------------------------------------------------
// WASAPI open plan. No Windows headers: values match audiosessiontypes.h, and audio.cpp
// static_asserts them against the SDK. The capture thread refuses any plan that
// wasapiPlanBlocksOtherPlayback() flags.
// ---------------------------------------------------------------------------------------------

/// AUDCLNT_SHAREMODE_SHARED / AUDCLNT_SHAREMODE_EXCLUSIVE.
inline constexpr unsigned kWasapiShareModeShared = 0u;
inline constexpr unsigned kWasapiShareModeExclusive = 1u;

/// AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK.
inline constexpr unsigned kWasapiStreamFlagsLoopback = 0x00020000u;
inline constexpr unsigned kWasapiStreamFlagsEventCallback = 0x00040000u;

enum class WasapiEndpointKind {
    RenderLoopback, // capture the mix already playing on an output device
    CaptureDevice,  // microphone or line-in
};

enum WasapiStreamFlag : unsigned {
    WasapiFlagEventCallback = 1u << 0,
    WasapiFlagLoopback = 1u << 1,
};

/// How to open one capture stream. Dropocalypse never renders, never takes exclusive mode,
/// and never asks a playback endpoint to leave its default engine period.
struct WasapiStreamPlan {
    bool sharedMode = true;
    /// InitializeSharedAudioStream at a period below the engine default. Legal only on a
    /// capture endpoint. On a render endpoint the period is device-wide: the engine leaves
    /// its ~10 ms default for every app (Microsoft, "Low Latency Audio").
    bool pullSharedEnginePeriod = false;
    /// Activate an IAudioClient as a render stream, including a silent "helper". Never.
    bool openRenderStream = false;
    unsigned flags = 0;
    /// IAudioClient::Initialize hnsBufferDuration, in 100 ns units. 0 = the engine's
    /// default aligned period. A hand-picked duration can be misaligned to that period.
    long long bufferDurationHns = 0;
};

inline WasapiStreamPlan planWasapiCapture(WasapiEndpointKind kind, bool eventDriven) {
    WasapiStreamPlan p;
    p.sharedMode = true;
    p.openRenderStream = false;
    p.bufferDurationHns = 0;
    // Shortening the period is confined to the microphone / line-in endpoint. Loopback
    // capture of a render endpoint must listen at whatever period the engine already uses.
    p.pullSharedEnginePeriod = (kind == WasapiEndpointKind::CaptureDevice);
    if (eventDriven) p.flags |= WasapiFlagEventCallback;
    if (kind == WasapiEndpointKind::RenderLoopback) p.flags |= WasapiFlagLoopback;
    return p;
}

inline unsigned wasapiShareMode(const WasapiStreamPlan& p) {
    return p.sharedMode ? kWasapiShareModeShared : kWasapiShareModeExclusive;
}

inline unsigned wasapiStreamFlags(const WasapiStreamPlan& p) {
    unsigned flags = 0;
    if (p.flags & WasapiFlagEventCallback) flags |= kWasapiStreamFlagsEventCallback;
    if (p.flags & WasapiFlagLoopback) flags |= kWasapiStreamFlagsLoopback;
    return flags;
}

/// True when opening this plan would take the playback device away from other apps
/// (exclusive mode, a render stream on the output, or a sub-default period on that output).
inline bool wasapiPlanBlocksOtherPlayback(const WasapiStreamPlan& p) {
    if (!p.sharedMode || wasapiShareMode(p) != kWasapiShareModeShared) return true;
    if (p.openRenderStream) return true;
    if (p.pullSharedEnginePeriod && (p.flags & WasapiFlagLoopback)) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------
// Windows in-app update handoff. The running process must not copy files over itself or
// start the replacement: a helper waits for this PID, terminates it if it is still alive,
// and only then replaces files and starts the new exe. A second ordinary launch focuses
// the existing window; the helper's relaunch passes --updated so it can wait out a mutex
// the old process has not released yet.
// ---------------------------------------------------------------------------------------------

struct WindowsUpdateHandoff {
    int waitForExitMs = 8000;
    bool terminateIfStillAlive = true;
    /// The running process must not rename or copy the exe while it is still alive.
    bool replaceFilesWhileRunning = false;
    /// The running process must not CreateProcess the new exe. The helper does that
    /// after this process is gone, or two windows stay up.
    bool launchReplacementWhileOldAlive = false;
    /// How long an --updated relaunch retries the single-instance mutex before giving up.
    int relaunchMutexWaitMs = 8000;
};

inline WindowsUpdateHandoff planWindowsUpdateHandoff() {
    WindowsUpdateHandoff h;
    h.waitForExitMs = 8000;
    h.terminateIfStillAlive = true;
    h.replaceFilesWhileRunning = false;
    h.launchReplacementWhileOldAlive = false;
    h.relaunchMutexWaitMs = 8000;
    return h;
}

enum class InstanceLaunch {
    BecomePrimary,           // this process owns the single-instance mutex
    FocusExisting,           // another instance is running; focus it and exit
    RetryThenBecomePrimary,  // updater relaunch: the old process may still be exiting
};

inline InstanceLaunch planInstanceLaunch(bool anotherInstanceOwnsMutex, bool updaterRelaunch) {
    if (!anotherInstanceOwnsMutex) return InstanceLaunch::BecomePrimary;
    if (updaterRelaunch) return InstanceLaunch::RetryThenBecomePrimary;
    return InstanceLaunch::FocusExisting;
}

/// PowerShell applied by the helper process. Order is the contract: wait for our PID,
/// terminate if that times out, refuse to continue while that PID is alive, then copy,
/// then start Dropocalypse.exe --updated.
inline const char* windowsUpdateHelperScript() {
    return R"ps(param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][int]$WaitMs,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Dest
)
$ErrorActionPreference = 'Stop'
try {
    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -ne $proc) {
        if (-not $proc.WaitForExit($WaitMs)) {
            Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
            $null = $proc.WaitForExit(5000)
        }
    }
    $still = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -ne $still) {
        throw "Dropocalypse didn't close, so the update wasn't installed."
    }
    Get-ChildItem -LiteralPath $Source -File | ForEach-Object {
        $name = $_.Name
        $target = Join-Path $Dest $name
        try {
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        } catch {
            if ($name -eq 'Dropocalypse.exe') { throw }
        }
    }
    $exe = Join-Path $Dest 'Dropocalypse.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Couldn't copy the new Dropocalypse.exe."
    }
    Start-Process -FilePath $exe -ArgumentList '--updated' -WorkingDirectory $Dest
    try {
        $work = Split-Path -Parent (Split-Path -Parent $Source)
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    } catch {}
} catch {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Dropocalypse') | Out-Null
    exit 1
}
)ps";
}

// ---------------------------------------------------------------------------------------------
// Frame pacing and the GPU signal draw. Simple wave styles are expanded in the vertex
// shader; these counts are the triangle lists that shader indexes with SV_VertexID.
// ---------------------------------------------------------------------------------------------

enum class GpuSignalMode : int { None = 0, Polyline = 1, Bars = 2, Radial = 3, Lissajous = 4, Dots = 5 };

/// Triangle-list length for one GPU signal draw. `count` is points, bars, or dots.
inline int gpuSignalVertices(GpuSignalMode mode, int count, bool closed) {
    if (count <= 0 || mode == GpuSignalMode::None) return 0;
    switch (mode) {
    case GpuSignalMode::Polyline:
    case GpuSignalMode::Lissajous: {
        const int segs = closed ? count : count - 1;
        return segs > 0 ? segs * 6 : 0;
    }
    case GpuSignalMode::Bars:
    case GpuSignalMode::Radial:
        return count * 6;
    case GpuSignalMode::Dots:
        return count * 24;
    default:
        return 0;
    }
}

/// Dear ImGui's full frame is skipped while the toolbar is hidden and nothing else needs it.
inline bool uiFrameNeeded(bool toolbarVisible, bool controlsDetached, bool notice, bool privacyPending) {
    return toolbarVisible || controlsDetached || notice || privacyPending;
}

/// Milliseconds to sleep instead of rendering. 0 means draw this refresh.
inline int hiddenFrameSleepMs(bool iconic, bool occluded) {
    return (iconic || occluded) ? 100 : 0;
}

/// nullptr means the update may be installed.
/// An unsigned running copy does not require a signature, so today's unsigned builds still update.
/// Once the running exe has a trusted Authenticode signature, the download must be trusted and
/// signed by the same publisher.
inline const char* updateSignatureRefusal(const UpdateSignatureFacts& f) {
    if (!f.runningIsSigned) return nullptr;
    if (!f.updateIsSigned || !f.updateTrusted)
        return "The downloaded app isn't signed with a trusted certificate, so it wasn't installed.";
    if (f.runningPublisher.empty() || f.updatePublisher.empty())
        return "Couldn't confirm the update is signed by the same publisher, so it wasn't installed.";
    if (f.updatePublisher != f.runningPublisher)
        return "The downloaded app is signed by a different publisher, so it wasn't installed.";
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Logos and text items: up to four on screen at once. The Mac copy of this math lives in
// Sources/DropocalypseCore/OverlayLayout.swift. Keep them in step.
// ---------------------------------------------------------------------------------------------

inline constexpr int kMaxOverlayItems = 4;

/// Where item `index` of `count` sits, in the renderer's units: x runs from -aspect to aspect and
/// y from -1 (bottom) to 1 (top). Landscape (aspect >= 1) spaces the items evenly across the
/// width; portrait stacks them top to bottom. maxHalfW/maxHalfH bound the item's half size so
/// neighbors don't overlap at rest (the beat can still push them a little past it).
struct ItemSlot {
    float cx = 0, cy = 0, maxHalfW = 1, maxHalfH = 1;
};

inline ItemSlot itemSlot(int index, int count, float aspect) {
    if (count < 1) count = 1;
    if (count > kMaxOverlayItems) count = kMaxOverlayItems;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    if (!(aspect > 0.05f)) aspect = 1.0f;
    ItemSlot s;
    if (count == 1) {
        s.maxHalfW = aspect * 0.95f;
        s.maxHalfH = 0.95f;
        return s;
    }
    const float spanFrac = 0.86f;   // leave a margin at the edges of the frame
    const float gapFrac = 0.86f;    // and between neighbors
    if (aspect >= 1.0f) {
        const float span = 2.0f * aspect * spanFrac;
        const float cell = span / (float)count;
        s.cx = -span * 0.5f + cell * ((float)index + 0.5f);
        s.cy = 0.0f;
        s.maxHalfW = cell * 0.5f * gapFrac;
        s.maxHalfH = 0.9f;
    } else {
        const float span = 2.0f * spanFrac;
        const float cell = span / (float)count;
        s.cx = 0.0f;
        s.cy = span * 0.5f - cell * ((float)index + 0.5f);
        s.maxHalfW = aspect * 0.92f;
        s.maxHalfH = cell * 0.5f * gapFrac;
    }
    return s;
}

/// Half size of an item at rest: the longer side is `size` (the Logo panel's Size slider),
/// then the item shrinks if needed to stay inside its slot. Aspect is the image's width/height.
struct ItemExtent {
    float halfW = 0, halfH = 0;
};

inline ItemExtent itemExtent(float imageAspect, float size, const ItemSlot& slot) {
    if (!(imageAspect > 0.0f)) imageAspect = 1.0f;
    ItemExtent e;
    e.halfH = imageAspect >= 1.0f ? size / imageAspect : size;
    e.halfW = e.halfH * imageAspect;
    float fit = 1.0f;
    if (e.halfW > slot.maxHalfW) fit = slot.maxHalfW / e.halfW;
    if (e.halfH * fit > slot.maxHalfH) fit = slot.maxHalfH / e.halfH;
    e.halfW *= fit;
    e.halfH *= fit;
    return e;
}

/// Each item reacts to the same beat, with small differences so they don't move like clones:
/// a short delay before the kick lands, which way it slides and tips first, a phase offset for
/// the slow drift, and a slightly different strength. Item 0 is exactly the single-logo motion.
struct MotionVariant {
    float delay = 0;     // seconds
    float side = 1;      // first slide / wobble direction
    float phase = 0;     // radians added to the slow drift
    float amount = 1;    // multiplies the beat impulses
};

inline MotionVariant motionVariant(int index) {
    static const MotionVariant v[kMaxOverlayItems] = {
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.045f, -1.0f, 1.7f, 0.92f},
        {0.020f, 1.0f, 3.1f, 1.06f},
        {0.065f, -1.0f, 4.6f, 0.96f},
    };
    if (index < 0) index = 0;
    return v[index % kMaxOverlayItems];
}

/// Text item colors (0xRRGGBB) and their names, in the order the color buttons show them.
inline constexpr int kTextColorCount = 6;
inline std::uint32_t textColorRGB(int index) {
    static const std::uint32_t c[kTextColorCount] = {0xFFFFFF, 0xFF3EA5, 0x2EE6FF, 0xFFC83D, 0x9BFF3A, 0xA65CFF};
    if (index < 0 || index >= kTextColorCount) index = 0;
    return c[index];
}
inline const char* textColorName(int index) {
    static const char* n[kTextColorCount] = {"White", "Pink", "Cyan", "Gold", "Lime", "Purple"};
    if (index < 0 || index >= kTextColorCount) index = 0;
    return n[index];
}

/// Text item fonts. Each platform maps these to fonts it has (Mac, Windows and Linux differ).
inline constexpr int kTextFontCount = 4;
inline const char* textFontName(int index) {
    static const char* n[kTextFontCount] = {"Bold", "Heavy", "Serif", "Mono"};
    if (index < 0 || index >= kTextFontCount) index = 0;
    return n[index];
}

/// Turns white-on-black text coverage (one byte per pixel, rows top to bottom) into premultiplied
/// RGBA in `rgb`, with a dark outline `outline` pixels wide so the text stays readable over bright
/// visuals. The output is the same size as the input.
inline std::vector<std::uint8_t> textCoverageToRGBA(const std::vector<std::uint8_t>& cov, int w, int h,
                                                    std::uint32_t rgb, int outline) {
    std::vector<std::uint8_t> out;
    if (w <= 0 || h <= 0 || cov.size() < (size_t)w * (size_t)h) return out;
    out.assign((size_t)w * h * 4, 0);
    // Dilate the coverage (separable max filter) for the outline.
    std::vector<std::uint8_t> tmp((size_t)w * h, 0), dil((size_t)w * h, 0);
    if (outline < 0) outline = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t m = 0;
            const int x0 = x - outline < 0 ? 0 : x - outline, x1 = x + outline >= w ? w - 1 : x + outline;
            for (int xx = x0; xx <= x1; ++xx) {
                const std::uint8_t v = cov[(size_t)y * w + xx];
                if (v > m) m = v;
            }
            tmp[(size_t)y * w + x] = m;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t m = 0;
            const int y0 = y - outline < 0 ? 0 : y - outline, y1 = y + outline >= h ? h - 1 : y + outline;
            for (int yy = y0; yy <= y1; ++yy) {
                const std::uint8_t v = tmp[(size_t)yy * w + x];
                if (v > m) m = v;
            }
            dil[(size_t)y * w + x] = m;
        }
    const unsigned r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const unsigned fill = cov[i];
        const unsigned edge = dil[i] * 3 / 4;                         // the outline is 75% black
        const unsigned a = fill + edge * (255 - fill) / 255;          // fill over outline
        out[i * 4 + 0] = (std::uint8_t)((r * fill + 127) / 255);
        out[i * 4 + 1] = (std::uint8_t)((g * fill + 127) / 255);
        out[i * 4 + 2] = (std::uint8_t)((b * fill + 127) / 255);
        out[i * 4 + 3] = (std::uint8_t)(a > 255 ? 255 : a);
    }
    return out;
}

/// What a saved item is. Images point at a private copy in the app's data folder.
struct OverlayItemInfo {
    bool isText = false;
    std::string file;   // UTF-8 path of the saved image (images only)
    std::string name;   // what the menu shows: the original file name, or the text
    std::string text;   // text items only
    int font = 0;
    int color = 0;
};

/// Text is stored on one settings line, so line breaks become spaces and it is trimmed and
/// capped at 60 characters (counted in UTF-8 code points).
inline std::string cleanItemText(const std::string& s) {
    std::string t;
    for (char c : s) t += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    size_t a = t.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    size_t b = t.find_last_not_of(' ');
    t = t.substr(a, b - a + 1);
    int points = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        if (((unsigned char)t[i] & 0xC0) != 0x80) {
            if (points == 60) { t.resize(i); break; }
            ++points;
        }
    }
    return t;
}

/// "item0.kind=text" style lines for the settings file.
inline std::string serializeItems(const std::vector<OverlayItemInfo>& items) {
    std::string out = "items=" + std::to_string(items.size() > (size_t)kMaxOverlayItems ? kMaxOverlayItems
                                                                                       : (int)items.size()) + "\n";
    for (size_t i = 0; i < items.size() && i < (size_t)kMaxOverlayItems; ++i) {
        const OverlayItemInfo& it = items[i];
        const std::string p = "item" + std::to_string(i) + ".";
        out += p + "kind=" + (it.isText ? "text" : "image") + "\n";
        if (it.isText) {
            out += p + "text=" + cleanItemText(it.text) + "\n";
            out += p + "font=" + std::to_string(it.font) + "\n";
            out += p + "color=" + std::to_string(it.color) + "\n";
        } else {
            out += p + "file=" + it.file + "\n";
            out += p + "name=" + it.name + "\n";
        }
    }
    return out;
}

/// Reads items back from key/value pairs. `get(key, value)` returns false when the key is absent.
/// Settings saved before items existed only have logoFile/logoName: that logo becomes item 0.
template <typename Get>
inline std::vector<OverlayItemInfo> parseItems(Get get) {
    std::vector<OverlayItemInfo> items;
    std::string v;
    if (get("items", v)) {
        int n = std::atoi(v.c_str());
        if (n < 0) n = 0;
        if (n > kMaxOverlayItems) n = kMaxOverlayItems;
        for (int i = 0; i < n; ++i) {
            const std::string p = "item" + std::to_string(i) + ".";
            OverlayItemInfo it;
            std::string kind;
            get(p + "kind", kind);
            it.isText = kind == "text";
            if (it.isText) {
                get(p + "text", it.text);
                it.text = cleanItemText(it.text);
                std::string f, c;
                if (get(p + "font", f)) it.font = std::atoi(f.c_str());
                if (get(p + "color", c)) it.color = std::atoi(c.c_str());
                if (it.font < 0 || it.font >= kTextFontCount) it.font = 0;
                if (it.color < 0 || it.color >= kTextColorCount) it.color = 0;
                if (it.text.empty()) continue;
                it.name = it.text;
            } else {
                get(p + "file", it.file);
                get(p + "name", it.name);
                if (it.file.empty()) continue;
            }
            items.push_back(it);
        }
        return items;
    }
    std::string file;
    if (get("logoFile", file) && !file.empty()) {
        OverlayItemInfo it;
        it.file = file;
        get("logoName", it.name);
        items.push_back(it);
    }
    return items;
}
