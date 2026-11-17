// Dropocalypse for Windows - window, low-latency swap chain, auto-hiding toolbar, settings.
//
// Portable: a single .exe, no installer, no admin rights (the manifest requests "asInvoker").
// Settings and the saved logo live in %APPDATA%\Dropocalypse.

#include "audio.h"
#include "core_logic.h"
#include "recorder.h"
#include "renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <ctime>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------------------------
// Settings (UTF-8 "key=value" file in %APPDATA%\Dropocalypse\settings.ini)
// ---------------------------------------------------------------------------------------------

struct AppSettings {
    RenderSettings r;
    std::string audioSource = "loop:"; // default: what the PC is playing
    float sensitivity = 1.0f;
    float beatThreshold = 1.35f;
    std::wstring logoFile;
    std::wstring logoName;
    bool checkForUpdates = true;      // ask GitHub for a newer version once a day
    long long lastUpdateCheck = 0;    // unix time of the last successful check
    std::string latestVersion;        // newest version GitHub reported
    std::string latestUrl;            // its release page
    long long updateSnoozeUntil = 0;  // "Ignore for 24 hours": hide the toolbar button until then
    bool privacyNoticeSeen = false;   // first-run notice about the update check has been answered
    bool controlsDetached = false;    // toolbar lives in its own window (for a second screen)
    int recordQuality = 1;            // 0 = 720p, 1 = 1080p, 2 = 4K
    std::wstring recordFolder;        // empty = the Videos folder
};

static std::wstring appDataDir() {
    PWSTR raw = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        dir = std::wstring(raw) + L"\\Dropocalypse";
        CoTaskMemFree(raw);
    } else {
        dir = L".\\Dropocalypse";
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::string serialize(const AppSettings& s) {
    char buf[4096];
    const RenderSettings& r = s.r;
    std::snprintf(buf, sizeof(buf),
                  "sceneIndex=%d\nautoCycle=%d\ncycleSeconds=%.3f\nshowLogo=%d\nlogoSize=%.4f\npulse=%.4f\n"
                  "squash=%.4f\nbounce=%.4f\nslide=%.4f\nwobble=%.4f\nglow=%.4f\nlogoTint=%.4f\nlogoTrails=%d\n"
                  "beatFlash=%.4f\nbrightness=%.4f\nsensitivity=%.4f\nbeatThreshold=%.4f\n",
                  r.sceneIndex, r.autoCycle ? 1 : 0, r.cycleSeconds, r.showLogo ? 1 : 0, r.logoSize, r.pulse,
                  r.squash, r.bounce, r.slide, r.wobble, r.glow, r.logoTint, r.logoTrails ? 1 : 0, r.beatFlash,
                  r.brightness, s.sensitivity, s.beatThreshold);
    std::string out = buf;
    out += "cycleSkip=" + std::to_string(r.cycleSkip) + "\n";
    out += "audioSource=" + s.audioSource + "\n";
    out += std::string("checkForUpdates=") + (s.checkForUpdates ? "1" : "0") + "\n";
    out += "lastUpdateCheck=" + std::to_string(s.lastUpdateCheck) + "\n";
    out += "latestVersion=" + s.latestVersion + "\n";
    out += "latestUrl=" + s.latestUrl + "\n";
    out += "updateSnoozeUntil=" + std::to_string(s.updateSnoozeUntil) + "\n";
    out += std::string("privacyNoticeSeen=") + (s.privacyNoticeSeen ? "1" : "0") + "\n";
    out += std::string("controlsDetached=") + (s.controlsDetached ? "1" : "0") + "\n";
    out += "recordQuality=" + std::to_string(s.recordQuality) + "\n";
    out += std::string("portrait=") + (s.r.portrait ? "1" : "0") + "\n";
    out += "recordFolder=" + narrow(s.recordFolder) + "\n";
    out += "logoFile=" + narrow(s.logoFile) + "\n";
    out += "logoName=" + narrow(s.logoName) + "\n";
    return out;
}

static void loadSettings(const std::wstring& path, AppSettings& s) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return;
    std::map<std::string, std::string> kv;
    char line[2048];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        auto eq = l.find('=');
        if (eq != std::string::npos) kv[l.substr(0, eq)] = l.substr(eq + 1);
    }
    std::fclose(f);

    auto num = [&](const char* k, float& v, float lo, float hi) {
        auto it = kv.find(k);
        if (it != kv.end()) v = std::min(hi, std::max(lo, std::strtof(it->second.c_str(), nullptr)));
    };
    auto flag = [&](const char* k, bool& v) {
        auto it = kv.find(k);
        if (it != kv.end()) v = it->second == "1";
    };
    RenderSettings& r = s.r;
    if (kv.count("sceneIndex")) r.sceneIndex = std::max(0, std::atoi(kv["sceneIndex"].c_str()));
    flag("autoCycle", r.autoCycle);
    if (kv.count("cycleSkip")) r.cycleSkip = (uint32_t)std::strtoul(kv["cycleSkip"].c_str(), nullptr, 10);
    num("cycleSeconds", r.cycleSeconds, 5, 360);
    flag("showLogo", r.showLogo);
    num("logoSize", r.logoSize, 0.08f, 0.8f);
    num("pulse", r.pulse, 0, 1.5f);
    num("squash", r.squash, 0, 1.5f);
    num("bounce", r.bounce, 0, 1.5f);
    num("slide", r.slide, 0, 1.5f);
    num("wobble", r.wobble, 0, 1.5f);
    num("glow", r.glow, 0, 1.5f);
    num("logoTint", r.logoTint, 0, 1);
    flag("logoTrails", r.logoTrails);
    num("beatFlash", r.beatFlash, 0, 1);
    num("brightness", r.brightness, 0.4f, 1.6f);
    num("sensitivity", s.sensitivity, 0.2f, 4);
    num("beatThreshold", s.beatThreshold, 1.1f, 2.2f);
    if (kv.count("audioSource")) s.audioSource = kv["audioSource"];
    flag("checkForUpdates", s.checkForUpdates);
    if (kv.count("lastUpdateCheck")) s.lastUpdateCheck = std::atoll(kv["lastUpdateCheck"].c_str());
    if (kv.count("latestVersion")) s.latestVersion = kv["latestVersion"];
    if (kv.count("latestUrl")) s.latestUrl = kv["latestUrl"];
    if (kv.count("updateSnoozeUntil")) s.updateSnoozeUntil = std::atoll(kv["updateSnoozeUntil"].c_str());
    flag("privacyNoticeSeen", s.privacyNoticeSeen);
    flag("controlsDetached", s.controlsDetached);
    if (kv.count("recordQuality")) s.recordQuality = std::clamp(std::atoi(kv["recordQuality"].c_str()), 0, 2);
    if (kv.count("portrait")) s.r.portrait = kv["portrait"] == "1";
    if (kv.count("recordFolder")) s.recordFolder = widen(kv["recordFolder"]);
    if (kv.count("logoFile")) s.logoFile = widen(kv["logoFile"]);
    if (kv.count("logoName")) s.logoName = widen(kv["logoName"]);
}

static void writeFile(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static void resetMotion(RenderSettings& r) {
    RenderSettings d;
    r.logoSize = d.logoSize; r.pulse = d.pulse; r.squash = d.squash; r.bounce = d.bounce; r.slide = d.slide;
    r.wobble = d.wobble; r.glow = d.glow; r.logoTrails = d.logoTrails; r.logoTint = d.logoTint;
    r.beatFlash = d.beatFlash; r.brightness = d.brightness;
}

// ---------------------------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------------------------

struct App {
    HWND hwnd = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain2> swapChain;
    ComPtr<ID3D11RenderTargetView> backbufferRTV;
    HANDLE frameWaitable = nullptr;
    int width = 0, height = 0;
    bool resizePending = false;
    bool occluded = false;

    Renderer renderer;
    AudioCapture audio;
    Recorder recorder;
    std::mutex recordMutex;
    std::wstring recordNotice;      // "Saved ..." / errors, shown for a few seconds
    double recordNoticeUntil = 0;
    std::wstring lastRecording;
    bool recordNoticePending = false;
    std::wstring pendingNotice;
    std::vector<AudioSource> sources;
    AppSettings settings;
    std::wstring dataDir, settingsPath;
    std::string lastSaved;
    std::wstring errorMessage;

    // Toolbar auto-hide.
    float dpi = 1.0f;
    bool fontsDirty = true;
    bool toolbarVisible = true;
    float toolbarAnim = 1.0f;
    double lastActivity = 0, lastMouseMove = 0;
    bool anyPopupOpen = false;
    bool pointerOverToolbar = false;

    // Detached controls window: its own Win32 window, swap chain and Dear ImGui context, so the
    // toolbar can sit on the main screen while the visuals run full screen on another display.
    float mainDpi = 1.0f;
    ImGuiContext* mainCtx = nullptr;
    HWND ctrlHwnd = nullptr;
    ImGuiContext* ctrlCtx = nullptr;
    ComPtr<IDXGISwapChain1> ctrlSwap;
    ComPtr<ID3D11RenderTargetView> ctrlRTV;
    int ctrlW = 0, ctrlH = 0;
    bool ctrlResizePending = false;
    float ctrlDpi = 1.0f;
    bool ctrlFontsDirty = true;
    int wantDetach = -1;          // deferred: 1 = open the controls window, 0 = close it

    // Full screen.
    bool fullscreen = false;
    WINDOWPLACEMENT prevPlacement{sizeof(WINDOWPLACEMENT)};
};

static App g;

// ---------------------------------------------------------------------------------------------
// Update check: one HTTPS request to GitHub for the latest release (at most once a day). If it's
// newer than this copy, the toolbar offers Update now. Nothing is downloaded until the user asks,
// and nothing about the user is sent.

static const char* kReleasesPage = "https://github.com/allenrobberson/dropocalypse/releases/latest";

struct UpdateState {
    std::mutex m;
    std::atomic<bool> checking{false};
    bool resultReady = false;
    bool userInitiated = false;
    bool ok = false;
    std::string version, url, error;
    std::string message;   // shown in the Updates menu after "Check now"
};
static UpdateState gUpdate;

static bool isDevVersion(const char* v) {
    return std::string(v).find("dev") != std::string::npos || std::string(v) == "0.0.0";
}

/// HTTPS GET of a full URL (redirects followed). `progress(done, total)` is optional.
static bool httpsGetUrl(const std::string& url, std::string& out, std::string& error, size_t maxBytes,
                        const std::function<void(uint64_t, uint64_t)>& progress = nullptr,
                        const wchar_t* extraHeaders = nullptr) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "Bad download address.";
        return false;
    }
    std::wstring fullPath = std::wstring(path) + extra;
    HINTERNET session = WinHttpOpen(L"Dropocalypse-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = "Couldn't start the network request."; return false; }
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 30000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    bool ok = request &&
              WinHttpSendRequest(request, extraHeaders ? extraHeaders : WINHTTP_NO_ADDITIONAL_HEADERS,
                                 extraHeaders ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (!ok) error = "Couldn't reach GitHub. Check your internet connection.";
    uint64_t total = 0;
    if (ok) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) { ok = false; error = "GitHub didn't answer (" + std::to_string(status) + "). Try again later."; }
        wchar_t len[32] = {};
        DWORD lenSize = sizeof(len);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lenSize,
                                WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(len, nullptr, 10);
    }
    while (ok) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), avail, &read)) { ok = false; error = "Download interrupted."; break; }
        out.append(buf.data(), read);
        if (progress) progress(out.size(), total);
        if (out.size() > maxBytes) { ok = false; error = "Download was larger than expected."; break; }
    }
    if (ok && total > 0 && out.size() != total) { ok = false; error = "Download was cut off. Try again."; }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

static bool httpsGet(const wchar_t* host, const wchar_t* path, std::string& out, std::string& error) {
    return httpsGetUrl("https://" + narrow(host) + narrow(path), out, error, 2'000'000, nullptr,
                       L"Accept: application/vnd.github+json\r\n");
}

/// Pulls a string value out of GitHub's JSON without a full parser: "key": "value".
static std::string jsonString(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return {};
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = json.find('"', p);
    if (p == std::string::npos) return {};
    size_t e = json.find('"', p + 1);
    return e == std::string::npos ? std::string() : json.substr(p + 1, e - p - 1);
}

#ifdef STORE_EDITION
static constexpr bool kStoreEdition = true;    // Microsoft Store build: the Store installs updates
#else
static constexpr bool kStoreEdition = false;
#endif

static void startUpdateCheck(bool userInitiated) {
    if (kStoreEdition) return;
    if (gUpdate.checking.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(gUpdate.m);
        gUpdate.userInitiated = userInitiated;
        if (userInitiated) gUpdate.message = "Checking...";
    }
    std::thread([] {
        std::string body, error;
        bool ok = httpsGet(L"api.github.com", L"/repos/allenrobberson/dropocalypse/releases/latest", body, error);
        std::string tag = ok ? jsonString(body, "tag_name") : std::string();
        if (ok && tag.empty()) { ok = false; error = "Unexpected answer from GitHub."; }
        std::lock_guard<std::mutex> lock(gUpdate.m);
        gUpdate.ok = ok;
        gUpdate.version = (!tag.empty() && tag[0] == 'v') ? tag.substr(1) : tag;
        gUpdate.url = ok ? jsonString(body, "html_url") : std::string();
        if (gUpdate.url.empty()) gUpdate.url = kReleasesPage;
        gUpdate.error = error;
        gUpdate.resultReady = true;
        gUpdate.checking = false;
    }).detach();
}

/// Called once per frame on the UI thread: stores a finished check's result.
static void pollUpdateCheck() {
    std::lock_guard<std::mutex> lock(gUpdate.m);
    if (!gUpdate.resultReady) return;
    gUpdate.resultReady = false;
    AppSettings& s = g.settings;
    if (gUpdate.ok) {
        s.lastUpdateCheck = (long long)std::time(nullptr);
        s.latestVersion = gUpdate.version;
        s.latestUrl = gUpdate.url;
    }
    if (gUpdate.userInitiated) {
        if (!gUpdate.ok) gUpdate.message = gUpdate.error;
        else if (versionIsNewer(gUpdate.version, APP_VERSION_STR)) gUpdate.message = "Version " + gUpdate.version + " is available!";
        else gUpdate.message = "You're up to date.";
    }
}

static bool updateAvailable() {
    if (kStoreEdition) return false;
    return !g.settings.latestVersion.empty() && versionIsNewer(g.settings.latestVersion, APP_VERSION_STR);
}

static void openDownloadPage() {
    std::string url = g.settings.latestUrl.rfind("https://github.com/", 0) == 0 ? g.settings.latestUrl : kReleasesPage;
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------------------------
// Update now: download the new portable zip, check it against the release's SHA256SUMS.txt,
// and, once this copy itself is Authenticode-signed, require the new exe to be signed by the
// same publisher. A helper process then waits until this process has exited (and terminates
// it if it is still alive) before replacing files and starting the new version. Replacing
// files or launching the new exe from here left the old process running beside the new one.
// Any failure before the helper starts leaves the current version untouched. Unsigned builds
// keep updating on the checksum alone.

enum class InstallPhase { Idle, Downloading, Verifying, Installing, ReadyToRelaunch, Failed };
struct InstallState {
    std::atomic<InstallPhase> phase{InstallPhase::Idle};
    std::atomic<float> progress{0};
    std::mutex m;
    std::string error;
};
static InstallState gInstall;
struct PendingUpdate {
    std::wstring source; // unpacked Dropocalypse\ folder
    std::wstring dest;   // folder this exe is running from
    std::wstring work;   // temp dir the helper deletes after a successful swap
};
static PendingUpdate gPendingUpdate;

static bool installBusy() {
    InstallPhase p = gInstall.phase;
    return p == InstallPhase::Downloading || p == InstallPhase::Verifying || p == InstallPhase::Installing ||
           p == InstallPhase::ReadyToRelaunch;
}

static std::wstring exePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return buf;
}
static std::wstring dirOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

/// Trusted Authenticode signature and the signing certificate's simple display name (the publisher).
struct AuthenticodeResult {
    bool hasSignature = false;
    bool trusted = false;
    std::string publisher;
};

static AuthenticodeResult queryAuthenticode(const std::wstring& path) {
    AuthenticodeResult result;
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE; // a CRL outage must not block an update
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &fileInfo;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &action, &trust);
    if (status == ERROR_SUCCESS) {
        result.hasSignature = true;
        result.trusted = true;
    } else if (status != TRUST_E_NOSIGNATURE) {
        result.hasSignature = true;
    }

    if (trust.hWVTStateData) {
        if (CRYPT_PROVIDER_DATA* prov = WTHelperProvDataFromStateData(trust.hWVTStateData)) {
            if (prov->csSigners > 0) {
                result.hasSignature = true;
                if (CRYPT_PROVIDER_SGNR* sgnr = WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0)) {
                    if (sgnr->csCertChain > 0 && sgnr->pasCertChain && sgnr->pasCertChain[0].pCert) {
                        PCCERT_CONTEXT cert = sgnr->pasCertChain[0].pCert;
                        const DWORD n = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                        if (n > 1) {
                            std::wstring name(n, L'\0');
                            CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name.data(), n);
                            if (!name.empty() && name.back() == L'\0') name.pop_back();
                            result.publisher = narrow(name);
                        }
                    }
                }
            }
        }
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &action, &trust);
    }
    return result;
}

static std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
            if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
                char b[3];
                for (unsigned char c : digest) { std::snprintf(b, sizeof(b), "%02x", c); hex += b; }
            }
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

/// Every "browser_download_url" value in GitHub's release JSON.
static std::vector<std::string> assetUrls(const std::string& json) {
    std::vector<std::string> urls;
    const std::string key = "\"browser_download_url\"";
    size_t p = 0;
    while ((p = json.find(key, p)) != std::string::npos) {
        size_t q = json.find('"', json.find(':', p + key.size()));
        size_t e = q == std::string::npos ? q : json.find('"', q + 1);
        if (e == std::string::npos) break;
        urls.push_back(json.substr(q + 1, e - q - 1));
        p = e;
    }
    return urls;
}

static bool runHidden(const std::wstring& cmdline, DWORD& exitCode) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static void failInstall(const std::string& why) {
    std::lock_guard<std::mutex> lock(gInstall.m);
    gInstall.error = why;
    gInstall.phase = InstallPhase::Failed;
}

static void installWorker() {
    // 1. Latest release and its files.
    std::string json, err;
    if (!httpsGet(L"api.github.com", L"/repos/allenrobberson/dropocalypse/releases/latest", json, err))
        return failInstall(err);
    std::string tag = jsonString(json, "tag_name");
    std::string version = (!tag.empty() && tag[0] == 'v') ? tag.substr(1) : tag;
    if (!versionIsNewer(version, APP_VERSION_STR)) return failInstall("You already have the latest version.");
    std::string zipUrl, sumsUrl;
    for (const auto& u : assetUrls(json)) {
        if (u.size() > 21 && u.compare(u.size() - 21, 21, "-Windows-portable.zip") == 0) zipUrl = u;
        if (u.size() > 15 && u.compare(u.size() - 15, 15, "/SHA256SUMS.txt") == 0) sumsUrl = u;
    }
    if (zipUrl.empty()) return failInstall("This release has no Windows download. Use the download page.");
    if (sumsUrl.empty()) return failInstall("This release has no checksums, so it can't be installed automatically. Use the download page.");
    std::string zipName = zipUrl.substr(zipUrl.find_last_of('/') + 1);

    // 2. Checksums, then the zip.
    std::string sums;
    if (!httpsGetUrl(sumsUrl, sums, err, 100000)) return failInstall(err);
    std::string expected = checksumFor(zipName, sums);
    if (expected.size() != 64) return failInstall("Couldn't read the release checksums.");
    std::string zip;
    gInstall.progress = 0;
    if (!httpsGetUrl(zipUrl, zip, err, 200'000'000, [](uint64_t done, uint64_t total) {
            if (total > 0) gInstall.progress = (float)done / (float)total;
        }))
        return failInstall(err);

    // 3. Verify.
    gInstall.phase = InstallPhase::Verifying;
    std::string actual = sha256Hex(zip);
    std::string want = expected;
    std::transform(want.begin(), want.end(), want.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
    if (actual.empty() || actual != want) return failInstall("The download didn't match its checksum, so it wasn't installed. Try again.");

    // 4. Unpack into a temp folder (tar.exe ships with Windows 10 and 11; PowerShell as a fallback).
    gInstall.phase = InstallPhase::Installing;
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring work = std::wstring(tmp) + L"DropocalypseUpdate-" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(work.c_str(), nullptr);
    std::wstring zipPath = work + L"\\update.zip";
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, zipPath.c_str(), L"wb") != 0 || !f) return failInstall("Couldn't save the download.");
        bool wrote = std::fwrite(zip.data(), 1, zip.size(), f) == zip.size();
        std::fclose(f);
        if (!wrote) return failInstall("Couldn't save the download.");
    }
    std::wstring outDir = work + L"\\files";
    CreateDirectoryW(outDir.c_str(), nullptr);
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    DWORD code = 1;
    bool unpacked = runHidden(L"\"" + std::wstring(sysDir) + L"\\tar.exe\" -xf \"" + zipPath + L"\" -C \"" + outDir + L"\"", code) && code == 0;
    if (!unpacked) {
        code = 1;
        unpacked = runHidden(L"powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath '" + zipPath +
                                 L"' -DestinationPath '" + outDir + L"' -Force\"", code) && code == 0;
    }
    std::wstring newDir = outDir + L"\\Dropocalypse";
    std::wstring newExe = newDir + L"\\Dropocalypse.exe";
    if (!unpacked || GetFileAttributesW(newExe.c_str()) == INVALID_FILE_ATTRIBUTES)
        return failInstall("Unpacking the update failed. Use the download page.");

    // Signature: enforced only when the running exe is already trusted-signed, so unsigned
    // builds (today, until SignPath signing lands) keep updating on the checksum alone.
    gInstall.phase = InstallPhase::Verifying;
    const AuthenticodeResult running = queryAuthenticode(exePath());
    const AuthenticodeResult update = queryAuthenticode(newExe);
    UpdateSignatureFacts signature;
    signature.runningIsSigned = running.trusted;
    signature.runningPublisher = running.publisher;
    signature.updateIsSigned = update.hasSignature;
    signature.updateTrusted = update.trusted;
    signature.updatePublisher = update.publisher;
    if (const char* why = updateSignatureRefusal(signature)) return failInstall(why);

    // Leave the new files in the temp folder. The helper copies them only after this
    // process is gone, so the exe is not locked and a second window is not opened first.
    if (planWindowsUpdateHandoff().replaceFilesWhileRunning)
        return failInstall("Couldn't install the update. Use the download page.");
    {
        std::lock_guard<std::mutex> lock(gInstall.m);
        gPendingUpdate.source = newDir;
        gPendingUpdate.dest = dirOf(exePath());
        gPendingUpdate.work = work;
    }
    gInstall.phase = InstallPhase::ReadyToRelaunch;
}

static void startInstall() {
    if (kStoreEdition) return; // the Store replaces the package; this exe must not
    if (installBusy()) return;
    if (isDevVersion(APP_VERSION_STR)) return failInstall("This is a development build. Use the download page.");
    gInstall.phase = InstallPhase::Downloading;
    gInstall.progress = 0;
    std::thread(installWorker).detach();
}

static bool writeExact(const std::wstring& path, const char* bytes) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const size_t n = std::strlen(bytes);
    const bool ok = n == 0 || std::fwrite(bytes, 1, n, f) == n;
    std::fclose(f);
    return ok;
}

/// Helper is a separate process so it can outlive us, wait on our PID, and kill us if
/// teardown (WASAPI join, the detached install thread, D3D/ImGui) never returns.
static bool spawnUpdateHelper() {
    const WindowsUpdateHandoff handoff = planWindowsUpdateHandoff();
    if (handoff.replaceFilesWhileRunning || handoff.launchReplacementWhileOldAlive) return false;
    PendingUpdate pending;
    {
        std::lock_guard<std::mutex> lock(gInstall.m);
        pending = gPendingUpdate;
    }
    if (pending.source.empty() || pending.dest.empty() || pending.work.empty()) return false;
    if (pending.source.find(L'"') != std::wstring::npos || pending.dest.find(L'"') != std::wstring::npos)
        return false;
    const std::wstring scriptPath = pending.work + L"\\apply-update.ps1";
    if (!writeExact(scriptPath, windowsUpdateHelperScript())) return false;

    wchar_t winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    const std::wstring ps = std::wstring(winDir) + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
                       scriptPath + L"\" -ProcessId " + std::to_wstring(GetCurrentProcessId()) + L" -WaitMs " +
                       std::to_wstring(handoff.waitForExitMs) + L" -Source \"" + pending.source + L"\" -Dest \"" +
                       pending.dest + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    // The helper is not a thread of this process, so ExitProcess does not stop it.
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        pending.work.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

/// Once the new files are unpacked: save settings, hand off to the helper, and leave.
/// ExitProcess (not returning from wWinMain) so a detached install thread cannot deadlock
/// CRT shutdown and keep this process alive next to the new one.
static bool relaunchIfUpdated() {
    if (gInstall.phase != InstallPhase::ReadyToRelaunch) return false;
    static bool started = false;
    if (started) return true;
    writeFile(g.settingsPath, serialize(g.settings));
    g.lastSaved = serialize(g.settings);
    if (!spawnUpdateHelper()) {
        failInstall("The update was downloaded, but it couldn't be applied. Nothing was changed. Use the download page.");
        return false;
    }
    started = true;
    if (g.hwnd) ShowWindow(g.hwnd, SW_HIDE);
    if (g.ctrlHwnd) ShowWindow(g.ctrlHwnd, SW_HIDE);
    return true;
}

/// After an older update the previous .exe may still be "<name>.exe.old". Delete it once it is unlocked.
static void cleanupOldExe() {
    std::thread([] {
        std::wstring old = exePath() + L".old";
        for (int i = 0; i < 40; ++i) {
            if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES || DeleteFileW(old.c_str())) return;
            Sleep(250);
        }
    }).detach();
}

/// First launch: say plainly what goes over the network and let the user turn it off before
/// anything is sent. (Audio never leaves the computer.)
static void drawPrivacyNotice() {
    if (g.settings.privacyNoticeSeen || kStoreEdition) return;   // the Store edition never contacts GitHub
    if (!ImGui::IsPopupOpen("Welcome to Dropocalypse")) ImGui::OpenPopup("Welcome to Dropocalypse");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * g.dpi, 0));
    if (ImGui::BeginPopupModal("Welcome to Dropocalypse", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextWrapped("Your audio is analyzed live on this computer and never sent anywhere. It's only saved if you press Record.");
        ImGui::Spacing();
        ImGui::TextWrapped("To tell you about new versions, Dropocalypse asks GitHub once a day for the latest version number, "
                           "and downloads an update only when you click Update now. Nothing about you is sent. "
                           "You can change this any time in the Updates menu.");
        ImGui::Spacing();
        if (ImGui::SmallButton("Read the privacy policy"))
            ShellExecuteW(nullptr, L"open", L"https://github.com/allenrobberson/dropocalypse/blob/main/PRIVACY.md",
                          nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::Separator();
        if (ImGui::Button("OK, check for updates")) {
            g.settings.privacyNoticeSeen = true;
            g.settings.checkForUpdates = true;
            ImGui::CloseCurrentPopup();
            if (!isDevVersion(APP_VERSION_STR)) startUpdateCheck(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't check for updates")) {
            g.settings.privacyNoticeSeen = true;
            g.settings.checkForUpdates = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

/// The popup shown from the toolbar's Update button.
static void drawUpdatePopupBody() {
    InstallPhase phase = gInstall.phase;
    if (phase == InstallPhase::Downloading) {
        ImGui::ProgressBar(gInstall.progress, ImVec2(-1, 0), "Downloading...");
        return;
    }
    if (phase == InstallPhase::Verifying) { ImGui::TextUnformatted("Checking the download..."); return; }
    if (phase == InstallPhase::Installing || phase == InstallPhase::ReadyToRelaunch) {
        ImGui::TextUnformatted("Installing and restarting...");
        return;
    }
    if (phase == InstallPhase::Failed) {
        std::lock_guard<std::mutex> lock(gInstall.m);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1));
        ImGui::TextWrapped("%s", gInstall.error.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("You have version %s.", APP_VERSION_STR);
        ImGui::TextWrapped("Update now downloads it, installs it and reopens Dropocalypse. Your logo and settings are kept.");
    }
    ImGui::Spacing();
    if (ImGui::Button(phase == InstallPhase::Failed ? "Try again" : "Update now")) startInstall();
    ImGui::SameLine();
    if (ImGui::Button("Download page")) { openDownloadPage(); ImGui::CloseCurrentPopup(); }
    if (phase != InstallPhase::Failed) {
        ImGui::SameLine();
        if (ImGui::Button("Ignore for 24 hours")) {
            g.settings.updateSnoozeUntil = (long long)std::time(nullptr) + 24 * 3600;
            ImGui::CloseCurrentPopup();
        }
    }
}

static double nowSeconds() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

static constexpr double kHideDelay = 2.5;
static float toolbarHeight() { return 40.0f * g.dpi; }

static void noteActivity(bool reveal) {
    g.lastActivity = nowSeconds();
    if (reveal) g.toolbarVisible = true;
}

// ---------------------------------------------------------------------------------------------
// Logo handling
// ---------------------------------------------------------------------------------------------

static std::wstring fileNameOf(const std::wstring& path) {
    auto p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

static std::wstring extensionOf(const std::wstring& path) {
    auto name = fileNameOf(path);
    auto p = name.find_last_of(L'.');
    return p == std::wstring::npos ? L"" : name.substr(p);
}

static void loadLogoFrom(const std::wstring& path) {
    LogoImage img;
    std::wstring err;
    if (!loadLogoImage(path, img, err)) {
        g.errorMessage = err;
        return;
    }
    // Keep a private copy so the logo survives the original being moved or deleted.
    std::wstring dest = g.dataDir + L"\\logo" + extensionOf(path);
    if (_wcsicmp(dest.c_str(), path.c_str()) != 0) {
        if (!g.settings.logoFile.empty() && _wcsicmp(g.settings.logoFile.c_str(), dest.c_str()) != 0)
            DeleteFileW(g.settings.logoFile.c_str());
        CopyFileW(path.c_str(), dest.c_str(), FALSE);
    }
    g.renderer.setLogo(img);
    g.settings.logoFile = dest;
    g.settings.logoName = fileNameOf(path);
    g.settings.r.showLogo = true;
    g.errorMessage.clear();
}

static void clearLogo() {
    if (!g.settings.logoFile.empty()) DeleteFileW(g.settings.logoFile.c_str());
    g.settings.logoFile.clear();
    g.settings.logoName.clear();
    g.renderer.setLogo(placeholderLogo());
}

static void chooseLogo() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    COMDLG_FILTERSPEC filters[] = {
        {L"Images", L"*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.tif;*.tiff;*.webp;*.heic;*.ico"},
        {L"All files", L"*.*"},
    };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Choose your logo (PNG with a transparent background works best)");
    if (FAILED(dlg->Show(g.hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        loadLogoFrom(path);
        CoTaskMemFree(path);
    }
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

static double nowSeconds();

static std::wstring defaultRecordFolder() {
    PWSTR raw = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &raw))) dir = raw;
    if (raw) CoTaskMemFree(raw);
    return dir.empty() ? g.dataDir : dir;
}

static std::wstring recordFolder() {
    return g.settings.recordFolder.empty() ? defaultRecordFolder() : g.settings.recordFolder;
}

static void showRecordNotice(const std::wstring& msg) {
    g.recordNotice = msg;
    g.recordNoticeUntil = nowSeconds() + 7.0;
}

static void startRecording() {
    if (g.recorder.isRecording()) return;
    std::wstring dir = recordFolder();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t name[64];
    wcsftime(name, 64, L"Dropocalypse %Y-%m-%d at %H.%M.%S.mp4", &tm);
    std::wstring path = dir + L"\\" + name;
    std::wstring err;
    if (!g.recorder.start(path, g.settings.recordQuality, g.settings.r.portrait, err)) {
        showRecordNotice(L"Couldn't start recording: " + err);
        return;
    }
    g.audio.setRecordSink([](const float* stereo, uint32_t frames, uint32_t rate) {
        g.recorder.pushAudio(stereo, frames, rate);
    });
}

static void stopRecording() {
    if (!g.recorder.isRecording()) return;
    g.audio.setRecordSink(nullptr);
    showRecordNotice(L"Finishing the video...");
    g.recorder.stop([](const std::wstring& path, bool ok) {
        std::lock_guard<std::mutex> lock(g.recordMutex);
        if (ok) g.lastRecording = path;
        size_t slash = path.find_last_of(L"\\/");
        std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
        g.pendingNotice = ok ? L"Saved " + file : L"Recording failed while saving " + file;
        g.recordNoticePending = true;
    });
}

static void toggleRecording() {
    if (g.recorder.isRecording()) stopRecording();
    else startRecording();
}

static void showInExplorer(const std::wstring& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

static void openRecordFolder() {
    std::wstring dir = recordFolder();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void chooseRecordFolder() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Choose where to save recordings");
    ComPtr<IShellItem> start;
    if (SUCCEEDED(SHCreateItemFromParsingName(recordFolder().c_str(), nullptr, IID_PPV_ARGS(&start))))
        dlg->SetFolder(start.Get());
    if (FAILED(dlg->Show(g.ctrlHwnd ? g.ctrlHwnd : g.hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        g.settings.recordFolder = path;
        CoTaskMemFree(path);
    }
}

static std::string recordTimeLabel() {
    int sec = (int)g.recorder.elapsedSeconds();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
    return buf;
}

/// Recording settings, shown in the toolbar's drop-down and in the controls window.
static void recordSettingsBody() {
    ImGui::TextUnformatted("Video size");
    ImGui::BeginDisabled(g.recorder.isRecording());
    for (int i = 0; i < 3; ++i) {
        const auto& q = Recorder::kQualities[i];
        const RecordingPixels px = recordingPixels(i, g.settings.r.portrait);
        char label[64];
        std::snprintf(label, sizeof(label), "%s  (%d x %d, %d fps)", q.label, px.width, px.height, q.fps);
        if (ImGui::RadioButton(label, g.settings.recordQuality == i)) g.settings.recordQuality = i;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextUnformatted("Save recordings to");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", narrow(recordFolder()).c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Change folder...")) chooseRecordFolder();
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) openRecordFolder();
    if (!g.settings.recordFolder.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Use Videos")) g.settings.recordFolder.clear();
    }
    std::wstring last;
    {
        std::lock_guard<std::mutex> lock(g.recordMutex);
        last = g.lastRecording;
    }
    if (!last.empty() && ImGui::Button("Show last recording")) showInExplorer(last);
    ImGui::TextDisabled("Records the visuals and the music, without the toolbar. Landscape follows the window. Portrait records the centered 9:16 frame (set under Look).");
}

/// Red record button, or a stop button with the running time while recording.
static void recordButton(const ImVec2& size = ImVec2(0, 0)) {
    bool rec = g.recorder.isRecording();
    // Grey like the other buttons until recording, then red.
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Button, rec ? ImVec4(0.85f, 0.12f, 0.18f, 1) : st.Colors[ImGuiCol_Button]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rec ? ImVec4(0.95f, 0.2f, 0.25f, 1) : st.Colors[ImGuiCol_ButtonHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rec ? ImVec4(1.0f, 0.3f, 0.3f, 1) : st.Colors[ImGuiCol_ButtonActive]);
    std::string label = rec ? "Stop  " + recordTimeLabel() + "###recbtn" : "Record###recbtn";
    if (ImGui::Button(label.c_str(), size)) toggleRecording();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(rec ? "Stop recording and save the video (R)" : "Record the visuals and music to a video (R)");
}

// ---------------------------------------------------------------------------------------------
// Window / swap chain
// ---------------------------------------------------------------------------------------------

static void createBackbufferRTV() {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(g.swapChain->GetBuffer(0, IID_PPV_ARGS(&back))))
        g.device->CreateRenderTargetView(back.Get(), nullptr, &g.backbufferRTV);
}

static bool createDevice(std::wstring& error) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4, D3D11_SDK_VERSION,
                                   &g.device, nullptr, &g.ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 4, D3D11_SDK_VERSION,
                               &g.device, nullptr, &g.ctx);
    }
    if (FAILED(hr)) {
        error = L"Couldn't start Direct3D 11. Please update your graphics driver.";
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    g.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));
    if (!factory) {
        error = L"Couldn't create the display swap chain (DXGI 1.2 is required).";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT; // recording reads it
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> sc1;
    hr = factory->CreateSwapChainForHwnd(g.device.Get(), g.hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) {
        // Older systems: fall back to FLIP_SEQUENTIAL / stretch scaling.
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Scaling = DXGI_SCALING_STRETCH;
        hr = factory->CreateSwapChainForHwnd(g.device.Get(), g.hwnd, &sd, nullptr, nullptr, &sc1);
    }
    if (FAILED(hr) || FAILED(sc1.As(&g.swapChain))) {
        error = L"Couldn't create the display swap chain.";
        return false;
    }
    factory->MakeWindowAssociation(g.hwnd, DXGI_MWA_NO_ALT_ENTER);
    // One queued frame: the picture on screen is never more than a frame behind the audio.
    g.swapChain->SetMaximumFrameLatency(1);
    g.frameWaitable = g.swapChain->GetFrameLatencyWaitableObject();
    createBackbufferRTV();
    return true;
}

static void handleResize() {
    if (!g.swapChain || g.width <= 0 || g.height <= 0) return;
    g.backbufferRTV.Reset();
    g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g.ctx->Flush();
    g.swapChain->ResizeBuffers(0, g.width, g.height, DXGI_FORMAT_UNKNOWN,
                               DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    createBackbufferRTV();
}

static void toggleFullscreen() {
    HWND h = g.hwnd;
    if (!g.fullscreen) {
        g.prevPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(h, &g.prevPlacement);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        g.fullscreen = true;
    } else {
        SetWindowLongPtrW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(h, &g.prevPlacement);
        SetWindowPos(h, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g.fullscreen = false;
    }
}

// ---- Displays: "Show visuals on" picks the screen the visuals go full screen on.
struct DisplayInfo {
    HMONITOR handle;
    RECT rect;
    bool primary;
    std::string name;
};

static std::vector<DisplayInfo> listDisplays() {
    std::vector<DisplayInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        auto* list = reinterpret_cast<std::vector<DisplayInfo>*>(p);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(m, &mi)) return TRUE;
        DisplayInfo d{m, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0, {}};
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Display %d  (%ld x %ld)%s", (int)list->size() + 1,
                      mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                      d.primary ? "  - main" : "");
        d.name = buf;
        list->push_back(d);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    return out;
}

/// Moves the visuals window to a display and makes it full screen there.
static void showVisualsOn(const DisplayInfo& d) {
    HWND h = g.hwnd;
    if (g.fullscreen) {
        SetWindowPos(h, HWND_TOP, d.rect.left, d.rect.top, d.rect.right - d.rect.left, d.rect.bottom - d.rect.top,
                     SWP_NOOWNERZORDER);
        return;
    }
    RECT wr;
    GetWindowRect(h, &wr);
    int w = std::min<int>(wr.right - wr.left, d.rect.right - d.rect.left);
    int hh = std::min<int>(wr.bottom - wr.top, d.rect.bottom - d.rect.top);
    SetWindowPos(h, nullptr, d.rect.left + (d.rect.right - d.rect.left - w) / 2,
                 d.rect.top + (d.rect.bottom - d.rect.top - hh) / 2, w, hh, SWP_NOZORDER | SWP_NOOWNERZORDER);
    toggleFullscreen();
}

static HMONITOR visualsMonitor() { return MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST); }

static void nextScene(int delta) {
    int n = (int)builtInScenes().size();
    g.settings.r.sceneIndex = ((g.settings.r.sceneIndex + delta) % n + n) % n;
}

static void startAudio() {
    g.audio.start(g.settings.audioSource);
}

// ---------------------------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------------------------

static void rebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0x2190, 0x21FF, 0x2300, 0x23FF, 0};
    wchar_t winDir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::string font = narrow(std::wstring(winDir) + L"\\Fonts\\segoeui.ttf");
    ImFont* f = nullptr;
    if (GetFileAttributesA(font.c_str()) != INVALID_FILE_ATTRIBUTES)
        f = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f * g.dpi, nullptr, ranges);
    if (!f) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * g.dpi;
        io.Fonts->AddFontDefault(&cfg);
    }

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 0;
    style.PopupRounding = 8;
    style.FrameRounding = 6;
    style.GrabRounding = 6;
    style.WindowBorderSize = 0;
    style.PopupBorderSize = 1;
    style.ItemSpacing = ImVec2(8, 8);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    style.Colors[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.06f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.16f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.26f);
    style.ScaleAllSizes(g.dpi);
    ImGui::GetStyle() = style;

    ImGui_ImplDX11_InvalidateDeviceObjects();
}

static bool slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f") {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string id = std::string("##") + label;
    return ImGui::SliderFloat(id.c_str(), v, lo, hi, fmt);
}

static void beginPanel(const char* title) {
    ImGui::PushTextWrapPos(280.0f * g.dpi);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    ImGui::PushItemWidth(260.0f * g.dpi);
}

static void endPanel() {
    ImGui::PopItemWidth();
    ImGui::PopTextWrapPos();
}

static std::string ellipsize(const std::string& s, float maxWidth) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxWidth) return s;
    std::string t = s;
    while (!t.empty() && ImGui::CalcTextSize((t + "...").c_str()).x > maxWidth) t.pop_back();
    return t.empty() ? std::string() : t + "...";
}

// ---- Panel contents, shared by the toolbar's drop-downs and the detached controls window.
static void motionPanelBody(RenderSettings& r) {
    slider("Pulse", &r.pulse, 0, 1.5f);
    slider("Squash & stretch", &r.squash, 0, 1.5f);
    slider("Bounce", &r.bounce, 0, 1.5f);
    slider("Slide", &r.slide, 0, 1.5f);
    slider("Wobble", &r.wobble, 0, 1.5f);
    if (ImGui::Button("Reset to defaults")) resetMotion(r);
}

static void logoPanelBody(RenderSettings& r) {
    slider("Size", &r.logoSize, 0.08f, 0.8f);
    slider("Glow", &r.glow, 0, 1.5f);
    slider("Color tint", &r.logoTint, 0, 1);
    ImGui::Checkbox("Logo trails (feeds into the visuals)", &r.logoTrails);
    ImGui::Checkbox("Show logo", &r.showLogo);
}

static void audioPanelBody() {
    std::string curName = "System audio (what this PC is playing)";
    for (const auto& s : g.sources)
        if (s.key() == g.settings.audioSource) curName = narrow(s.name);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##source", curName.c_str())) {
        for (const auto& s : g.sources) {
            std::string k = s.key();
            if (ImGui::Selectable(narrow(s.name).c_str(), k == g.settings.audioSource)) {
                g.settings.audioSource = k;
                startAudio();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Refresh devices")) g.sources = AudioCapture::listSources();
    ImGui::SameLine();
    if (ImGui::Button("Restart audio")) startAudio();
    slider("Sensitivity", &g.settings.sensitivity, 0.2f, 4.0f);
    slider("Beat threshold", &g.settings.beatThreshold, 1.1f, 2.2f, "%.2fx");
    ImGui::TextDisabled("%s", narrow(g.audio.status()).c_str());
}

static void lookPanelBody(RenderSettings& r) {
    slider("Beat flash", &r.beatFlash, 0, 1);
    slider("Brightness", &r.brightness, 0.4f, 1.6f);
    ImGui::TextUnformatted("Frame");
    ImGui::BeginDisabled(g.recorder.isRecording());
    if (ImGui::RadioButton("Landscape", !r.portrait)) r.portrait = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Portrait (9:16)", r.portrait)) r.portrait = true;
    ImGui::EndDisabled();
    ImGui::TextWrapped(r.portrait
                           ? "Portrait centers a vertical frame. Recordings are 720x1280, 1080x1920, or 2160x3840."
                           : "Landscape fills the window.");
    slider("Auto-cycle every", &r.cycleSeconds, 5, 360, "%.0f s");
}

/// The Updates menu: version, check now, automatic checks, and installing a new version.
static void updatesPanelBody() {
    if (kStoreEdition) {
        ImGui::TextUnformatted("Updates come automatically from the Microsoft Store.");
        ImGui::TextDisabled("Version %s", APP_VERSION_STR);
        return;
    }
    if (updateAvailable() && !g.settings.latestVersion.empty()) {
        ImGui::TextUnformatted(("Dropocalypse " + g.settings.latestVersion + " is available").c_str());
        drawUpdatePopupBody();
        ImGui::Separator();
    } else {
        ImGui::TextUnformatted("You have the latest version.");
    }
    if (ImGui::Checkbox("Check for updates automatically", &g.settings.checkForUpdates) && g.settings.checkForUpdates)
        startUpdateCheck(false);
    if (ImGui::Button(gUpdate.checking ? "Checking..." : "Check now") && !gUpdate.checking) startUpdateCheck(true);
    ImGui::SameLine();
    ImGui::TextDisabled("Version %s", APP_VERSION_STR);
    {
        std::lock_guard<std::mutex> lock(gUpdate.m);
        if (!gUpdate.message.empty()) ImGui::TextUnformatted(gUpdate.message.c_str());
    }
}

static void styleListBody(RenderSettings& r, int cur) {
    const auto& scenes = builtInScenes();
    int n = (int)scenes.size();
    // Click a name to show it; checkboxes pick which styles auto-cycle rotates through.
    ImGui::TextDisabled("Checked = included in auto-cycle");
    if (ImGui::SmallButton("All")) r.cycleSkip = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) r.cycleSkip = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    ImGui::Separator();
    int checked = 0;
    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        bool on = r.inCycle(i);
        if (ImGui::Checkbox("##cycle", &on)) r.setInCycle(i, on);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include %s in auto-cycle", scenes[i].name);
        ImGui::SameLine();
        char label[64];
        std::snprintf(label, sizeof(label), "%d. %s", i + 1, scenes[i].name);
        if (ImGui::Selectable(label, i == cur)) r.sceneIndex = i;
        ImGui::PopID();
        if (on) ++checked;
    }
    ImGui::Separator();
    if (checked == 0) ImGui::TextDisabled("Nothing checked: auto-cycle uses every style");
    else ImGui::TextDisabled("Auto-cycle: %d of %d styles", checked, n);
}

static void keysPanelBody() {
    const char* rows[][2] = {{"F / F11", "Full screen"},    {"T / S", "Show / hide toolbar"},
                             {"Esc", "Hide toolbar / exit full screen"}, {"L", "Choose logo"},
                             {"G", "Show / hide logo"},     {"Left / Right", "Previous / next style"},
                             {"A", "Auto-cycle on / off"},  {"D", "Detach / attach the controls"},
                             {"R", "Start / stop recording"}};
    for (auto& row : rows) {
        ImGui::TextUnformatted(row[0]);
        ImGui::SameLine(110 * g.dpi);
        ImGui::TextDisabled("%s", row[1]);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Move the mouse to the top edge to bring the toolbar back.");
}

/// The toolbar. `docked` = drawn over the visuals (auto-hides); otherwise it's the top row of the
/// separate controls window and always visible.
static void drawToolbar(float dt, bool docked = true) {
    ImGuiIO& io = ImGui::GetIO();
    RenderSettings& r = g.settings.r;
    if (docked) {
        float target = g.toolbarVisible ? 1.0f : 0.0f;
        g.toolbarAnim += (target - g.toolbarAnim) * std::min(1.0f, dt * 14.0f);
        if (g.toolbarAnim < 0.01f && !g.toolbarVisible) return;
    }
    const float anim = docked ? g.toolbarAnim : 1.0f;

    const float h = toolbarHeight();
    ImGui::SetNextWindowPos(ImVec2(0, -h * (1.0f - anim)));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h));
    ImGui::SetNextWindowBgAlpha(docked ? 0.78f : 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * g.dpi, 0));
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar();

    const float y = (h - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorPosY(y);

    // ---- Logo menu
    std::string logoLabel = g.settings.logoName.empty() ? "Add Logo" : "Logo: " + narrow(g.settings.logoName);
    logoLabel = ellipsize(logoLabel, 180 * g.dpi) + "##logomenu";
    if (ImGui::Button(logoLabel.c_str())) ImGui::OpenPopup("logo_menu");
    if (ImGui::BeginPopup("logo_menu")) {
        if (ImGui::MenuItem("Choose Logo...", "L")) chooseLogo();
        if (ImGui::MenuItem("Remove Logo", nullptr, false, !g.settings.logoFile.empty())) clearLogo();
        ImGui::Separator();
        ImGui::MenuItem("Show Logo", "G", &r.showLogo);
        ImGui::EndPopup();
    }

    // ---- Style controls
    ImGui::SameLine(0, 16 * g.dpi);
    if (ImGui::Button(" < ")) nextScene(-1);
    ImGui::SameLine();
    const auto& scenes = builtInScenes();
    int n = (int)scenes.size();
    int cur = ((r.sceneIndex % n) + n) % n;
    ImGui::SetNextItemWidth(170 * g.dpi);
    if (ImGui::BeginCombo("##style", scenes[cur].name, ImGuiComboFlags_HeightLargest)) {
        styleListBody(r, cur);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(" > ")) nextScene(1);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-cycle", &r.autoCycle);

    // ---- Record
    ImGui::SameLine(0, 16 * g.dpi);
    recordButton();
    ImGui::SameLine(0, 2 * g.dpi);
    if (ImGui::Button("v##recopts")) ImGui::OpenPopup("recopts");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recording settings: video size and where to save");
    if (ImGui::BeginPopup("recopts")) {
        beginPanel("Recording");
        recordSettingsBody();
        endPanel();
        ImGui::EndPopup();
    }

    // ---- Panels
    auto panelButton = [&](const char* label, const char* id) {
        ImGui::SameLine(0, 12 * g.dpi);
        if (ImGui::Button(label)) ImGui::OpenPopup(id);
    };

    panelButton("Motion", "motion");
    if (ImGui::BeginPopup("motion")) {
        beginPanel("Logo motion - driven by the bass");
        motionPanelBody(r);
        endPanel();
        ImGui::EndPopup();
    }

    panelButton("Logo", "logo");
    if (ImGui::BeginPopup("logo")) {
        beginPanel("Logo");
        logoPanelBody(r);
        endPanel();
        ImGui::EndPopup();
    }

    panelButton(g.audio.isRunning() ? "Audio" : "Audio (off)", "audio");
    if (ImGui::BeginPopup("audio")) {
        beginPanel("Audio input");
        audioPanelBody();
        endPanel();
        ImGui::EndPopup();
    }

    panelButton("Look", "look");
    if (ImGui::BeginPopup("look")) {
        beginPanel("Look");
        lookPanelBody(r);
        endPanel();
        ImGui::EndPopup();
    }

    // ---- Updates menu: always here; turns pink with the version when an update is out.
    {
        ImGui::SameLine(0, 12 * g.dpi);
        bool highlight = updateAvailable() &&
                         (installBusy() || (long long)std::time(nullptr) >= g.settings.updateSnoozeUntil);
        std::string label = installBusy() ? "Updating... " + std::to_string((int)(gInstall.progress * 100)) + "%"
                            : highlight   ? "Update " + g.settings.latestVersion
                                          : std::string("Updates");
        label += "###updatebtn";
        if (highlight) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.29f, 0.67f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.42f, 0.76f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.58f, 1));
        }
        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("update");
        if (highlight) ImGui::PopStyleColor(3);
        if (ImGui::BeginPopup("update")) {
            beginPanel("Updates");
            updatesPanelBody();
            endPanel();
            ImGui::EndPopup();
        }
    }

    // ---- Right side: status, screen picker, detach, shortcuts, full screen
    const char* fsLabel = g.fullscreen ? "Exit full screen" : "Full screen";
    const char* detachLabel = docked ? "Detach" : "Attach";
    ImGuiStyle& st = ImGui::GetStyle();
    float rightW = ImGui::CalcTextSize("Screen").x + ImGui::CalcTextSize(detachLabel).x +
                   ImGui::CalcTextSize("Keys").x + ImGui::CalcTextSize(fsLabel).x + st.FramePadding.x * 8 +
                   st.ItemSpacing.x * 3 + 10 * g.dpi;
    ImGui::SameLine(0, 16 * g.dpi);
    float statusW = ImGui::GetContentRegionAvail().x - rightW - 12 * g.dpi;
    if (statusW > 60 * g.dpi) {
        std::string status = ellipsize(narrow(g.audio.status()), statusW);
        ImGui::SetCursorPosY(y + st.FramePadding.y);
        ImGui::TextDisabled("%s", status.c_str());
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(io.DisplaySize.x - rightW);
    ImGui::SetCursorPosY(y);
    if (ImGui::Button("Screen")) ImGui::OpenPopup("screen");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the visuals full screen on another display");
    if (ImGui::BeginPopup("screen")) {
        ImGui::TextDisabled("Show the visuals full screen on:");
        HMONITOR cur = visualsMonitor();
        for (const auto& d : listDisplays()) {
            bool here = d.handle == cur && g.fullscreen;
            if (ImGui::MenuItem(d.name.c_str(), nullptr, here)) showVisualsOn(d);
        }
        if (g.fullscreen) {
            ImGui::Separator();
            if (ImGui::MenuItem("Back to a window")) toggleFullscreen();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(detachLabel)) g.wantDetach = docked ? 1 : 0;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(docked ? "Move these controls into their own window (for a second screen)"
                                 : "Put the controls back on top of the visuals");
    ImGui::SameLine();
    if (ImGui::Button("Keys")) ImGui::OpenPopup("keys");
    if (ImGui::BeginPopup("keys")) {
        beginPanel("Keyboard shortcuts");
        keysPanelBody();
        endPanel();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(fsLabel)) toggleFullscreen();

    ImGui::End();
}

// ---- Detached controls window: everything stacked top to bottom, sections shown in place.
static void drawControlsPanel() {
    ImGuiIO& io = ImGui::GetIO();
    RenderSettings& r = g.settings.r;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * g.dpi, 12 * g.dpi));
    ImGui::Begin("##controls", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PushItemWidth(-FLT_MIN);
    const float full = -FLT_MIN;

    // Updates: its own section; highlighted and open when a new version is out.
    {
        bool highlight = updateAvailable() &&
                         (installBusy() || (long long)std::time(nullptr) >= g.settings.updateSnoozeUntil);
        if (highlight) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.92f, 0.29f, 0.67f, 0.85f));
        std::string title = highlight ? "Updates - " + g.settings.latestVersion + " available###updates"
                                      : std::string("Updates###updates");
        if (highlight) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader(title.c_str())) updatesPanelBody();
        if (highlight) ImGui::PopStyleColor();
    }

    // Visuals: screen + full screen
    ImGui::SeparatorText("Visuals");
    {
        HMONITOR cur = visualsMonitor();
        std::string curName = "Choose a screen...";
        auto displays = listDisplays();
        for (const auto& d : displays) if (d.handle == cur) curName = d.name;
        ImGui::TextDisabled("Show the visuals full screen on:");
        ImGui::SetNextItemWidth(full);
        if (ImGui::BeginCombo("##screen", curName.c_str())) {
            for (const auto& d : displays)
                if (ImGui::Selectable(d.name.c_str(), d.handle == cur && g.fullscreen)) showVisualsOn(d);
            ImGui::EndCombo();
        }
        if (ImGui::Button(g.fullscreen ? "Exit full screen" : "Full screen", ImVec2(full, 0))) toggleFullscreen();
    }

    // Record
    ImGui::SeparatorText("Record");
    recordButton(ImVec2(full, 0));
    if (nowSeconds() < g.recordNoticeUntil) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(narrow(g.recordNotice).c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::CollapsingHeader("Recording settings")) recordSettingsBody();

    // Logo
    ImGui::SeparatorText("Logo");
    {
        std::string name = g.settings.logoName.empty() ? "No logo chosen" : narrow(g.settings.logoName);
        ImGui::TextUnformatted(ellipsize(name, ImGui::GetContentRegionAvail().x).c_str());
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Choose logo...", ImVec2(half, 0))) chooseLogo();
        ImGui::SameLine();
        ImGui::BeginDisabled(g.settings.logoFile.empty());
        if (ImGui::Button("Remove", ImVec2(half, 0))) clearLogo();
        ImGui::EndDisabled();
    }

    // Style
    ImGui::SeparatorText("Style");
    {
        const auto& scenes = builtInScenes();
        int n = (int)scenes.size();
        int cur = ((r.sceneIndex % n) + n) % n;
        float bw = ImGui::GetFrameHeight() * 1.4f;
        if (ImGui::Button(" < ", ImVec2(bw, 0))) nextScene(-1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##style2", scenes[cur].name, ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < n; ++i)
                if (ImGui::Selectable(scenes[i].name, i == cur)) r.sceneIndex = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(" > ", ImVec2(bw, 0))) nextScene(1);
        ImGui::Checkbox("Auto-cycle", &r.autoCycle);
        if (ImGui::CollapsingHeader("Styles in auto-cycle")) styleListBody(r, cur);
    }

    // Sections
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) motionPanelBody(r);
    if (ImGui::CollapsingHeader("Logo look")) logoPanelBody(r);
    if (ImGui::CollapsingHeader(g.audio.isRunning() ? "Audio" : "Audio (off)", ImGuiTreeNodeFlags_DefaultOpen))
        audioPanelBody();
    if (ImGui::CollapsingHeader("Look")) lookPanelBody(r);
    if (ImGui::CollapsingHeader("Keyboard shortcuts")) keysPanelBody();

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Attach controls to the visuals", ImVec2(full, 0))) g.wantDetach = 0;
    ImGui::PopItemWidth();
    ImGui::End();
}

static void drawNotices() {
    std::wstring msg;
    {
        std::lock_guard<std::mutex> lock(g.recordMutex);
        if (g.recordNoticePending) {
            g.recordNoticePending = false;
            showRecordNotice(g.pendingNotice);
        }
    }
    // With the controls detached, recording messages show there instead of on the audience's screen.
    if (nowSeconds() < g.recordNoticeUntil && !g.settings.controlsDetached) msg = g.recordNotice;
    else if (!g.errorMessage.empty()) msg = g.errorMessage;
    else if (!g.audio.isRunning()) msg = g.audio.status();
    if (msg.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24 * g.dpi), ImGuiCond_Always,
                            ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##notice", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImGui::PushTextWrapPos(520 * g.dpi);
    ImGui::TextUnformatted(narrow(msg).c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------------------------

static bool cursorShouldHide() {
    return !g.toolbarVisible && !g.anyPopupOpen && nowSeconds() - g.lastMouseMove > kHideDelay;
}

/// Keyboard shortcuts shared by the visuals window and the controls window.
static bool handleKey(WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (key) {
    case VK_RIGHT: nextScene(1); return true;
    case VK_LEFT: nextScene(-1); return true;
    case VK_F11: toggleFullscreen(); return true;
    case VK_ESCAPE:
        if (g.toolbarVisible && !g.anyPopupOpen && !g.settings.controlsDetached) g.toolbarVisible = false;
        else if (g.fullscreen) toggleFullscreen();
        return true;
    case 'F': if (!ctrl) { toggleFullscreen(); return true; } break;
    case 'T':
    case 'S':
    case 'H':
        if (!ctrl && !g.settings.controlsDetached) { g.toolbarVisible = !g.toolbarVisible; g.lastActivity = nowSeconds(); return true; }
        break;
    case 'D': if (!ctrl) { g.wantDetach = g.settings.controlsDetached ? 0 : 1; return true; } break;
    case 'R': if (!ctrl) { toggleRecording(); return true; } break;
    case 'L': if (!ctrl) { chooseLogo(); return true; } break;
    case 'O': if (ctrl) { chooseLogo(); return true; } break;
    case 'G': if (!ctrl) { g.settings.r.showLogo = !g.settings.r.showLogo; return true; } break;
    case 'A': if (!ctrl) { g.settings.r.autoCycle = !g.settings.r.autoCycle; return true; } break;
    }
    return false;
}

static LRESULT MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && cursorShouldHide()) {
        SetCursor(nullptr);
        return TRUE;
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return TRUE;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g.width = LOWORD(lParam);
            g.height = HIWORD(lParam);
            g.resizePending = true;
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = (LONG)(640 * g.dpi);
        mmi->ptMinTrackSize.y = (LONG)(400 * g.dpi);
        return 0;
    }

    case WM_DPICHANGED: {
        g.mainDpi = HIWORD(wParam) / 96.0f;
        g.fontsDirty = true;
        if (g.fullscreen) {   // moved to a screen with another scale: stay exactly full screen
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowPos(hWnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        auto rc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        g.lastMouseMove = nowSeconds();
        float y = (float)(short)HIWORD(lParam);
        noteActivity(y < 90.0f * g.dpi);
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MOUSEWHEEL:
        g.lastMouseMove = nowSeconds();
        noteActivity(true);
        break;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, (UINT)(sizeof(path) / sizeof(path[0])))) loadLogoFrom(path);
        DragFinish(drop);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
        if (handleKey(wParam)) return 0;
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // no beep / menu on Alt
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/// Every message is handled with the visuals window's ImGui context current (a modal dialog opened
/// from the controls window can deliver messages here mid-frame).
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (g.mainCtx) ImGui::SetCurrentContext(g.mainCtx);
    LRESULT res = MainWndProc(hWnd, msg, wParam, lParam);
    ImGui::SetCurrentContext(prev);
    return res;
}

static LRESULT ControlsWndProcImpl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return TRUE;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g.ctrlW = LOWORD(lParam);
            g.ctrlH = HIWORD(lParam);
            g.ctrlResizePending = true;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = (LONG)(300 * g.ctrlDpi);
        mmi->ptMinTrackSize.y = (LONG)(360 * g.ctrlDpi);
        return 0;
    }
    case WM_DPICHANGED: {
        g.ctrlDpi = HIWORD(wParam) / 96.0f;
        g.ctrlFontsDirty = true;
        auto rc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, (UINT)(sizeof(path) / sizeof(path[0])))) loadLogoFrom(path);
        DragFinish(drop);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
        if (handleKey(wParam)) return 0;
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        g.wantDetach = 0;   // closing the controls window puts the toolbar back on the visuals
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ControlsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g.ctrlCtx);
    LRESULT res = ControlsWndProcImpl(hWnd, msg, wParam, lParam);
    ImGui::SetCurrentContext(prev);
    return res;
}

static void createControlsRTV() {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(g.ctrlSwap->GetBuffer(0, IID_PPV_ARGS(&back))))
        g.device->CreateRenderTargetView(back.Get(), nullptr, &g.ctrlRTV);
}

static void openControlsWindow() {
    if (g.ctrlHwnd) { SetForegroundWindow(g.ctrlHwnd); return; }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ControlsWndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"DropocalypseControls";
        RegisterClassExW(&wc);
        registered = true;
    }
    // A tall panel on the main (primary) screen, near its right edge.
    POINT origin{0, 0};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    g.ctrlDpi = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    int w = (int)(360 * g.ctrlDpi);
    int h = std::min<int>((int)(820 * g.ctrlDpi), mi.rcWork.bottom - mi.rcWork.top - (int)(40 * g.ctrlDpi));
    int x = mi.rcWork.right - w - (int)(24 * g.ctrlDpi);
    int y = mi.rcWork.top + (int)(24 * g.ctrlDpi);
    g.ctrlHwnd = CreateWindowExW(0, L"DropocalypseControls", L"Dropocalypse Controls",
                                 WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!g.ctrlHwnd) return;
    DragAcceptFiles(g.ctrlHwnd, TRUE);

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    g.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = factory ? factory->CreateSwapChainForHwnd(g.device.Get(), g.ctrlHwnd, &sd, nullptr, nullptr, &g.ctrlSwap)
                         : E_FAIL;
    if (FAILED(hr) && factory) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(g.device.Get(), g.ctrlHwnd, &sd, nullptr, nullptr, &g.ctrlSwap);
    }
    if (FAILED(hr)) { DestroyWindow(g.ctrlHwnd); g.ctrlHwnd = nullptr; return; }
    factory->MakeWindowAssociation(g.ctrlHwnd, DXGI_MWA_NO_ALT_ENTER);
    createControlsRTV();

    ImGuiContext* prev = ImGui::GetCurrentContext();
    g.ctrlCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g.ctrlCtx);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(g.ctrlHwnd);
    ImGui_ImplDX11_Init(g.device.Get(), g.ctx.Get());
    ImGui::SetCurrentContext(prev);
    g.ctrlFontsDirty = true;

    RECT rc;
    GetClientRect(g.ctrlHwnd, &rc);
    g.ctrlW = rc.right - rc.left;
    g.ctrlH = rc.bottom - rc.top;
    g.ctrlResizePending = true;
    ShowWindow(g.ctrlHwnd, SW_SHOW);
    g.settings.controlsDetached = true;
    g.toolbarVisible = false;
}

static void closeControlsWindow() {
    g.settings.controlsDetached = false;
    if (!g.ctrlHwnd) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g.ctrlCtx);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g.ctrlCtx);
    ImGui::SetCurrentContext(prev == g.ctrlCtx ? g.mainCtx : prev);
    g.ctrlCtx = nullptr;
    g.ctrlRTV.Reset();
    g.ctrlSwap.Reset();
    HWND h = g.ctrlHwnd;
    g.ctrlHwnd = nullptr;
    DestroyWindow(h);
    noteActivity(true);
}

/// Draws one frame of the controls window (if it's open).
static void renderControlsWindow() {
    if (!g.ctrlHwnd || !g.ctrlSwap || IsIconic(g.ctrlHwnd)) return;
    ImGui::SetCurrentContext(g.ctrlCtx);
    g.dpi = g.ctrlDpi;
    if (g.ctrlResizePending && g.ctrlW > 0 && g.ctrlH > 0) {
        g.ctrlResizePending = false;
        g.ctrlRTV.Reset();
        g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
        g.ctrlSwap->ResizeBuffers(0, g.ctrlW, g.ctrlH, DXGI_FORMAT_UNKNOWN, 0);
        createControlsRTV();
    }
    if (g.ctrlFontsDirty) { rebuildFonts(); g.ctrlFontsDirty = false; }
    if (g.ctrlRTV) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawControlsPanel();
        ImGui::Render();
        ID3D11RenderTargetView* rtv = g.ctrlRTV.Get();
        const float bg[4] = {0.06f, 0.06f, 0.08f, 1.0f};
        g.ctx->ClearRenderTargetView(rtv, bg);
        g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g.ctrlSwap->Present(0, 0);
    }
    ImGui::SetCurrentContext(g.mainCtx);
    g.dpi = g.mainDpi;
}

// ---------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------

static constexpr wchar_t kWindowClass[] = L"DropocalypseWindow";
// Held until process exit. The kernel releases it when we die, including TerminateProcess.
static HANDLE gInstanceMutex = nullptr;

static bool commandLineHasUpdatedFlag(PWSTR cmdLine) {
    if (cmdLine && wcsstr(cmdLine, L"--updated")) return true;
    const wchar_t* full = GetCommandLineW();
    return full && wcsstr(full, L"--updated");
}

static void focusExistingDropocalypse() {
    HWND hwnd = FindWindowW(kWindowClass, nullptr);
    if (!hwnd) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    HWND fore = GetForegroundWindow();
    DWORD foreThread = fore ? GetWindowThreadProcessId(fore, nullptr) : 0;
    DWORD thisThread = GetCurrentThreadId();
    const bool attached = foreThread && foreThread != thisThread && AttachThreadInput(thisThread, foreThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached) AttachThreadInput(thisThread, foreThread, FALSE);
}

/// False: this process should exit (an existing window was focused, or the relaunch gave up).
static bool becomeTheOnlyInstance(bool updaterRelaunch) {
    const WindowsUpdateHandoff handoff = planWindowsUpdateHandoff();
    const int sliceMs = 100;
    const int attempts = updaterRelaunch ? std::max(1, handoff.relaunchMutexWaitMs / sliceMs) : 1;
    for (int i = 0; i < attempts; ++i) {
        SetLastError(0);
        HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DropocalypseSingleInstance");
        if (!mutex) return true; // don't refuse to run if the mutex cannot be created
        const bool held = GetLastError() == ERROR_ALREADY_EXISTS;
        const InstanceLaunch plan = planInstanceLaunch(held, updaterRelaunch);
        if (plan == InstanceLaunch::BecomePrimary) {
            gInstanceMutex = mutex;
            return true;
        }
        CloseHandle(mutex);
        if (plan == InstanceLaunch::FocusExisting) {
            focusExistingDropocalypse();
            return false;
        }
        Sleep(sliceMs);
    }
    focusExistingDropocalypse();
    return false;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int nCmdShow) {
    if (!becomeTheOnlyInstance(commandLineHasUpdatedFlag(cmdLine))) return 0;
    ImGui_ImplWin32_EnableDpiAwareness();
    timeBeginPeriod(1); // 1 ms timer resolution for the audio thread's waits
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    cleanupOldExe();
    g.dataDir = appDataDir();
    g.settingsPath = g.dataDir + L"\\settings.ini";
    loadSettings(g.settingsPath, g.settings);
    g.lastSaved = serialize(g.settings);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    POINT origin{0, 0};
    g.dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    int w = (int)(1280 * g.dpi), h = (int)(720 * g.dpi);
    g.hwnd = CreateWindowExW(0, kWindowClass, L"Dropocalypse", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = g.mainDpi = ImGui_ImplWin32_GetDpiScaleForHwnd(g.hwnd);
    DragAcceptFiles(g.hwnd, TRUE);

    std::wstring error;
    if (!createDevice(error)) {
        MessageBoxW(nullptr, error.c_str(), L"Dropocalypse", MB_ICONERROR);
        return 1;
    }
    if (!g.renderer.init(g.device.Get(), g.ctx.Get(), error)) {
        MessageBoxW(nullptr, error.c_str(), L"Dropocalypse", MB_ICONERROR);
        return 1;
    }
    {
        std::wstring recErr;
        g.recorder.init(g.device.Get(), g.ctx.Get(), recErr);
    }

    IMGUI_CHECKVERSION();
    g.mainCtx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // portable: no imgui.ini next to the exe
    ImGui_ImplWin32_Init(g.hwnd);
    ImGui_ImplDX11_Init(g.device.Get(), g.ctx.Get());

    ShowWindow(g.hwnd, nCmdShow);
    UpdateWindow(g.hwnd);
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    g.width = rc.right - rc.left;
    g.height = rc.bottom - rc.top;
    handleResize();

    if (!g.settings.logoFile.empty()) {
        LogoImage img;
        std::wstring err;
        if (loadLogoImage(g.settings.logoFile, img, err)) g.renderer.setLogo(img);
        else { g.settings.logoFile.clear(); g.settings.logoName.clear(); }
    }

    g.sources = AudioCapture::listSources();
    startAudio();
    if (g.settings.checkForUpdates && g.settings.privacyNoticeSeen && !isDevVersion(APP_VERSION_STR) &&
        (long long)std::time(nullptr) - g.settings.lastUpdateCheck > 20 * 3600)
        startUpdateCheck(false);
    noteActivity(true);
    if (g.settings.controlsDetached) openControlsWindow();

    double lastFrame = nowSeconds();
    double lastSaveCheck = lastFrame;
    bool running = true;

    while (running) {
        pollUpdateCheck();
        if (relaunchIfUpdated()) ExitProcess(0);
        // Wait until DXGI is ready for a new frame, *then* read input and audio, so what we draw is
        // as fresh as possible when it reaches the screen.
        if (g.frameWaitable && !g.occluded) WaitForSingleObjectEx(g.frameWaitable, 100, TRUE);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        if (g.occluded) {
            Sleep(50);
            if (g.swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) continue;
            g.occluded = false;
        }
        if (g.resizePending) {
            g.resizePending = false;
            handleResize();
        }
        if (g.wantDetach >= 0) {
            int want = g.wantDetach;
            g.wantDetach = -1;
            if (want == 1) openControlsWindow();
            else closeControlsWindow();
        }
        ImGui::SetCurrentContext(g.mainCtx);
        g.dpi = g.mainDpi;
        if (g.fontsDirty) { rebuildFonts(); g.fontsDirty = false; }
        if (!g.backbufferRTV) { Sleep(10); continue; }

        double now = nowSeconds();
        float dt = (float)(now - lastFrame);
        lastFrame = now;

        // Feed settings to the audio thread.
        g.audio.analysis.sensitivity.store(g.settings.sensitivity);
        g.audio.analysis.beatThreshold.store(g.settings.beatThreshold);

        // Visualizer.
        g.renderer.render(g.backbufferRTV.Get(), g.width, g.height, g.audio.analysis, g.audio.sampleRate.load(),
                          g.settings.r);
        if (g.recorder.isRecording()) {   // grab the visuals before the toolbar is drawn on top
            ComPtr<ID3D11Texture2D> back;
            if (SUCCEEDED(g.swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
                const ContentRect frame = fitContentRect((float)g.width, (float)g.height, g.settings.r.portrait);
                const float uw = std::max(1.0f, (float)g.width);
                const float uh = std::max(1.0f, (float)g.height);
                g.recorder.captureFrame(back.Get(), frame.x / uw, frame.y / uh, frame.width / uw, frame.height / uh);
            }
        }

        // Toolbar auto-hide.
        if (cursorShouldHide()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        g.anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (g.settings.controlsDetached) g.toolbarVisible = false;
        g.pointerOverToolbar = g.toolbarVisible && io.MousePos.y >= 0 && io.MousePos.y < toolbarHeight();
        if (g.anyPopupOpen || g.pointerOverToolbar) g.lastActivity = now;
        if (g.toolbarVisible && now - g.lastActivity > kHideDelay) g.toolbarVisible = false;

        if (!g.settings.controlsDetached) drawToolbar(dt);
        drawPrivacyNotice();
        drawNotices();

        ImGui::Render();
        ID3D11RenderTargetView* rtv = g.backbufferRTV.Get();
        g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (cursorShouldHide()) {
            POINT pt;
            GetCursorPos(&pt);
            if (WindowFromPoint(pt) == g.hwnd) SetCursor(nullptr);
        }

        HRESULT hr = g.swapChain->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) g.occluded = true;

        renderControlsWindow();

        // Save settings when something changed (checked twice a second).
        if (now - lastSaveCheck > 0.5) {
            lastSaveCheck = now;
            std::string s = serialize(g.settings);
            if (s != g.lastSaved) {
                writeFile(g.settingsPath, s);
                g.lastSaved = s;
            }
        }
    }

    if (g.recorder.isRecording()) stopRecording();
    g.recorder.waitIdle();
    bool keepDetached = g.settings.controlsDetached;
    closeControlsWindow();
    g.settings.controlsDetached = keepDetached;   // reopen detached next time
    writeFile(g.settingsPath, serialize(g.settings));
    g.audio.stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    timeEndPeriod(1);
    CoUninitialize();
    return 0;
}
