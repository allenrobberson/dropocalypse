// Dropocalypse for Windows - window, low-latency swap chain, auto-hiding toolbar, settings.
//
// Portable: a single .exe, no installer, no admin rights (the manifest requests "asInvoker").
// Settings and the saved logo live in %APPDATA%\Dropocalypse.

#include "audio.h"
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
    num("cycleSeconds", r.cycleSeconds, 5, 120);
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

    // Full screen.
    bool fullscreen = false;
    WINDOWPLACEMENT prevPlacement{sizeof(WINDOWPLACEMENT)};
};

static App g;

// ---------------------------------------------------------------------------------------------
// Update check: one HTTPS request to GitHub for the latest release (at most once a day). If it's
// newer than this copy, the toolbar shows "Update X.Y.Z", which opens the download page. Nothing
// is downloaded or installed automatically, and nothing about the user is sent.

static const char* kReleasesPage = "https://github.com/allenrobberson/dropocalypse/releases/latest";

struct UpdateState {
    std::mutex m;
    std::atomic<bool> checking{false};
    bool resultReady = false;
    bool userInitiated = false;
    bool ok = false;
    std::string version, url, error;
    std::string message;   // shown in the Look panel after "Check now"
};
static UpdateState gUpdate;

static bool isDevVersion(const char* v) {
    return std::string(v).find("dev") != std::string::npos || std::string(v) == "0.0.0";
}

/// Dotted versions compared numerically (0.10.0 > 0.9.2); anything after "-" is ignored.
static bool versionIsNewer(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
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
    };
    auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
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

static void startUpdateCheck(bool userInitiated) {
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
    return !g.settings.latestVersion.empty() && versionIsNewer(g.settings.latestVersion, APP_VERSION_STR);
}

static void openDownloadPage() {
    std::string url = g.settings.latestUrl.rfind("https://github.com/", 0) == 0 ? g.settings.latestUrl : kReleasesPage;
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------------------------
// Update now: download the new portable zip, check it against the release's SHA256SUMS.txt,
// unpack it, swap the files in this folder (a running .exe can be renamed, so no admin rights or
// helper program are needed), start the new version and quit. Any failure leaves the current
// version untouched and falls back to the download page.

enum class InstallPhase { Idle, Downloading, Verifying, Installing, ReadyToRelaunch, Failed };
struct InstallState {
    std::atomic<InstallPhase> phase{InstallPhase::Idle};
    std::atomic<float> progress{0};
    std::mutex m;
    std::string error;
};
static InstallState gInstall;

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
    std::string expected;
    {
        size_t p = sums.find(zipName);
        if (p != std::string::npos) {
            size_t lineStart = sums.rfind('\n', p);
            lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            expected = sums.substr(lineStart, 64);
        }
    }
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
    if (!unpacked || GetFileAttributesW((newDir + L"\\Dropocalypse.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
        return failInstall("Unpacking the update failed. Use the download page.");

    // 5. Swap files in this folder: rename the running .exe out of the way, copy the new files in.
    std::wstring cur = exePath();
    std::wstring appDir = dirOf(cur);
    std::wstring old = cur + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
        return failInstall("Dropocalypse can't replace itself in this folder. Move the Dropocalypse folder somewhere "
                           "you can edit (like Documents or Desktop), or use the download page.");
    if (!CopyFileW((newDir + L"\\Dropocalypse.exe").c_str(), cur.c_str(), FALSE)) {
        MoveFileExW(old.c_str(), cur.c_str(), MOVEFILE_REPLACE_EXISTING);
        return failInstall("Couldn't copy the new version into this folder. Nothing was changed.");
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((newDir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            if (_wcsicmp(name.c_str(), L"Dropocalypse.exe") == 0) continue;
            CopyFileW((newDir + L"\\" + name).c_str(), (appDir + L"\\" + name).c_str(), FALSE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    gInstall.phase = InstallPhase::ReadyToRelaunch;
}

static void startInstall() {
    if (installBusy()) return;
    if (isDevVersion(APP_VERSION_STR)) return failInstall("This is a development build. Use the download page.");
    gInstall.phase = InstallPhase::Downloading;
    gInstall.progress = 0;
    std::thread(installWorker).detach();
}

/// Called each frame: once the new files are in place, save settings, start the new version, quit.
static bool relaunchIfUpdated() {
    if (gInstall.phase != InstallPhase::ReadyToRelaunch) return false;
    writeFile(g.settingsPath, serialize(g.settings));
    g.lastSaved = serialize(g.settings);
    std::wstring cur = exePath();
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + cur + L"\" --updated";
    std::wstring dir = dirOf(cur);
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;   // caller quits
    }
    failInstall("The update was installed, but Dropocalypse couldn't restart. Please open it again.");
    return false;
}

/// After an update, the previous .exe is left as "<name>.exe.old" until it has quit; tidy it up.
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
    if (g.settings.privacyNoticeSeen) return;
    if (!ImGui::IsPopupOpen("Welcome to Dropocalypse")) ImGui::OpenPopup("Welcome to Dropocalypse");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460 * g.dpi, 0));
    if (ImGui::BeginPopupModal("Welcome to Dropocalypse", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextWrapped("Your audio is analyzed live on this computer and is never recorded or sent anywhere.");
        ImGui::Spacing();
        ImGui::TextWrapped("To tell you about new versions, Dropocalypse asks GitHub once a day for the latest version number, "
                           "and downloads an update only when you click Update now. Nothing about you is sent. "
                           "You can change this any time under Look > Check for updates automatically.");
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
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
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
    g.fontsDirty = false;
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

static void drawToolbar(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    RenderSettings& r = g.settings.r;
    float target = g.toolbarVisible ? 1.0f : 0.0f;
    g.toolbarAnim += (target - g.toolbarAnim) * std::min(1.0f, dt * 14.0f);
    if (g.toolbarAnim < 0.01f && !g.toolbarVisible) return;

    const float h = toolbarHeight();
    ImGui::SetNextWindowPos(ImVec2(0, -h * (1.0f - g.toolbarAnim)));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h));
    ImGui::SetNextWindowBgAlpha(0.78f);
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
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(" > ")) nextScene(1);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-cycle", &r.autoCycle);

    // ---- Panels
    auto panelButton = [&](const char* label, const char* id) {
        ImGui::SameLine(0, 12 * g.dpi);
        if (ImGui::Button(label)) ImGui::OpenPopup(id);
    };

    panelButton("Motion", "motion");
    if (ImGui::BeginPopup("motion")) {
        beginPanel("Logo motion - driven by the bass");
        slider("Pulse", &r.pulse, 0, 1.5f);
        slider("Squash & stretch", &r.squash, 0, 1.5f);
        slider("Bounce", &r.bounce, 0, 1.5f);
        slider("Slide", &r.slide, 0, 1.5f);
        slider("Wobble", &r.wobble, 0, 1.5f);
        if (ImGui::Button("Reset to defaults")) resetMotion(r);
        endPanel();
        ImGui::EndPopup();
    }

    panelButton("Logo", "logo");
    if (ImGui::BeginPopup("logo")) {
        beginPanel("Logo");
        slider("Size", &r.logoSize, 0.08f, 0.8f);
        slider("Glow", &r.glow, 0, 1.5f);
        slider("Color tint", &r.logoTint, 0, 1);
        ImGui::Checkbox("Logo trails (feeds into the visuals)", &r.logoTrails);
        ImGui::Checkbox("Show logo", &r.showLogo);
        endPanel();
        ImGui::EndPopup();
    }

    panelButton(g.audio.isRunning() ? "Audio" : "Audio (off)", "audio");
    if (ImGui::BeginPopup("audio")) {
        beginPanel("Audio input");
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
        endPanel();
        ImGui::EndPopup();
    }

    panelButton("Look", "look");
    if (ImGui::BeginPopup("look")) {
        beginPanel("Look");
        slider("Beat flash", &r.beatFlash, 0, 1);
        slider("Brightness", &r.brightness, 0.4f, 1.6f);
        slider("Auto-cycle every", &r.cycleSeconds, 5, 120, "%.0f s");
        ImGui::Separator();
        if (ImGui::Checkbox("Check for updates automatically", &g.settings.checkForUpdates) && g.settings.checkForUpdates)
            startUpdateCheck(false);
        if (ImGui::Button(gUpdate.checking ? "Checking..." : "Check now") && !gUpdate.checking) startUpdateCheck(true);
        ImGui::SameLine();
        ImGui::TextDisabled("Version %s", APP_VERSION_STR);
        {
            std::lock_guard<std::mutex> lock(gUpdate.m);
            if (!gUpdate.message.empty()) ImGui::TextUnformatted(gUpdate.message.c_str());
        }
        if (updateAvailable()) {
            if (ImGui::Button("Update now")) startInstall();
            ImGui::SameLine();
            if (ImGui::Button("Open download page")) openDownloadPage();
            if (gInstall.phase == InstallPhase::Failed) {
                std::lock_guard<std::mutex> lock(gInstall.m);
                ImGui::TextWrapped("%s", gInstall.error.c_str());
            } else if (installBusy()) {
                ImGui::ProgressBar(gInstall.progress, ImVec2(-1, 0), "Updating...");
            }
        }
        endPanel();
        ImGui::EndPopup();
    }

    // ---- Update available: a bright button with Download / Ignore for 24 hours
    if (updateAvailable() && (installBusy() || (long long)std::time(nullptr) >= g.settings.updateSnoozeUntil)) {
        ImGui::SameLine(0, 12 * g.dpi);
        std::string label = installBusy() ? "Updating... " + std::to_string((int)(gInstall.progress * 100)) + "%"
                                          : "Update " + g.settings.latestVersion + " available";
        label += "###updatebtn";
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.29f, 0.67f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.42f, 0.76f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.58f, 1));
        if (ImGui::Button(label.c_str())) ImGui::OpenPopup("update");
        ImGui::PopStyleColor(3);
        if (ImGui::BeginPopup("update")) {
            beginPanel(("Dropocalypse " + g.settings.latestVersion + " is available").c_str());
            drawUpdatePopupBody();
            endPanel();
            ImGui::EndPopup();
        }
    }

    // ---- Right side: status, shortcuts, full screen
    const char* fsLabel = g.fullscreen ? "Exit full screen" : "Full screen";
    ImGuiStyle& st = ImGui::GetStyle();
    float rightW = ImGui::CalcTextSize("Keys").x + ImGui::CalcTextSize(fsLabel).x + st.FramePadding.x * 4 +
                   st.ItemSpacing.x + 10 * g.dpi;
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
    if (ImGui::Button("Keys")) ImGui::OpenPopup("keys");
    if (ImGui::BeginPopup("keys")) {
        beginPanel("Keyboard shortcuts");
        const char* rows[][2] = {{"F / F11", "Full screen"},    {"T / S", "Show / hide toolbar"},
                                 {"Esc", "Hide toolbar / exit full screen"}, {"L", "Choose logo"},
                                 {"G", "Show / hide logo"},     {"Left / Right", "Previous / next style"},
                                 {"A", "Auto-cycle on / off"}};
        for (auto& row : rows) {
            ImGui::TextUnformatted(row[0]);
            ImGui::SameLine(110 * g.dpi);
            ImGui::TextDisabled("%s", row[1]);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Move the mouse to the top edge to bring the toolbar back.");
        endPanel();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(fsLabel)) toggleFullscreen();

    ImGui::End();
}

static void drawNotices() {
    std::wstring msg;
    if (!g.errorMessage.empty()) msg = g.errorMessage;
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

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        g.dpi = HIWORD(wParam) / 96.0f;
        g.fontsDirty = true;
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
    case WM_SYSKEYDOWN: {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) break;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        switch (wParam) {
        case VK_RIGHT: nextScene(1); return 0;
        case VK_LEFT: nextScene(-1); return 0;
        case VK_F11: toggleFullscreen(); return 0;
        case VK_ESCAPE:
            if (g.toolbarVisible && !g.anyPopupOpen) g.toolbarVisible = false;
            else if (g.fullscreen) toggleFullscreen();
            return 0;
        case 'F': if (!ctrl) { toggleFullscreen(); return 0; } break;
        case 'T':
        case 'S':
        case 'H': if (!ctrl) { g.toolbarVisible = !g.toolbarVisible; g.lastActivity = nowSeconds(); return 0; } break;
        case 'L': if (!ctrl) { chooseLogo(); return 0; } break;
        case 'O': if (ctrl) { chooseLogo(); return 0; } break;
        case 'G': if (!ctrl) { g.settings.r.showLogo = !g.settings.r.showLogo; return 0; } break;
        case 'A': if (!ctrl) { g.settings.r.autoCycle = !g.settings.r.autoCycle; return 0; } break;
        }
        break;
    }

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // no beep / menu on Alt
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
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
    wc.lpszClassName = L"DropocalypseWindow";
    RegisterClassExW(&wc);

    POINT origin{0, 0};
    g.dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    int w = (int)(1280 * g.dpi), h = (int)(720 * g.dpi);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Dropocalypse", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    if (!g.hwnd) return 1;
    g.dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(g.hwnd);
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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

    double lastFrame = nowSeconds();
    double lastSaveCheck = lastFrame;
    bool running = true;

    while (running) {
        pollUpdateCheck();
        if (relaunchIfUpdated()) running = false;
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
        if (g.fontsDirty) rebuildFonts();
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

        // Toolbar auto-hide.
        if (cursorShouldHide()) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        g.anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        g.pointerOverToolbar = g.toolbarVisible && io.MousePos.y >= 0 && io.MousePos.y < toolbarHeight();
        if (g.anyPopupOpen || g.pointerOverToolbar) g.lastActivity = now;
        if (g.toolbarVisible && now - g.lastActivity > kHideDelay) g.toolbarVisible = false;

        drawToolbar(dt);
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

    writeFile(g.settingsPath, serialize(g.settings));
    g.audio.stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    timeEndPeriod(1);
    CoUninitialize();
    return 0;
}
