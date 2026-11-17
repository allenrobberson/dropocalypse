// Pure update and auto-cycle decisions shared by the app and the unit tests.
// No Windows headers: the test binary compiles this on its own.
#pragma once

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
inline bool cycleBitSkipped(std::uint32_t cycleSkip, int index) {
    return (cycleSkip & (1u << index)) != 0;
}

/// Next style after `index`. If every style is skipped, still advances by one
/// (auto-cycle keeps moving when nothing is checked).
inline int nextCycleIndex(int index, int count, std::uint32_t cycleSkip) {
    if (count <= 0) return 0;
    const int n = count;
    int requested = index % n;
    if (requested < 0) requested += n;
    int next = (requested + 1) % n;
    for (int k = 1; k <= n; ++k) {
        const int c = (requested + k) % n;
        if (!cycleBitSkipped(cycleSkip, c)) {
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
