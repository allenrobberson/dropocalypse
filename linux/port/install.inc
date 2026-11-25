// ---------------------------------------------------------------------------------------------
// Update now (AppImage): download the new AppImage, check it against the release's
// SHA256SUMS.txt, put it in place of this one (renaming over a running AppImage is safe), start it
// and quit. Any failure leaves the current version untouched and falls back to the download page.

enum class InstallPhase { Idle, Downloading, Verifying, Installing, ReadyToRelaunch, Failed };
struct InstallState {
    std::atomic<InstallPhase> phase{InstallPhase::Idle};
    std::atomic<float> progress{0};
    std::mutex m;
    std::string error;
    std::string newPath;
};
static InstallState gInstall;

static bool installBusy() {
    InstallPhase p = gInstall.phase;
    return p == InstallPhase::Downloading || p == InstallPhase::Verifying || p == InstallPhase::Installing ||
           p == InstallPhase::ReadyToRelaunch;
}

/// The .AppImage file this copy runs from (set by the AppImage runtime), or empty.
static std::string appImagePath() {
    const char* p = std::getenv("APPIMAGE");
    return p && *p ? p : std::string();
}

// Compact SHA-256 (FIPS 180-4), used to check downloads.
static std::string sha256Hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string msg = data;
    uint64_t bits = (uint64_t)data.size() * 8;
    msg.push_back((char)0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((char)((bits >> (i * 8)) & 0xff));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const unsigned char* b = (const unsigned char*)msg.data() + off + i * 4;
            w[i] = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], gg = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & gg);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = gg; gg = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += gg; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
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

static void failInstall(const std::string& why) {
    std::lock_guard<std::mutex> lock(gInstall.m);
    gInstall.error = why;
    gInstall.phase = InstallPhase::Failed;
}

static void installWorker() {
    std::string target = appImagePath();
    // 1. Latest release and its files.
    std::string json, err;
    if (!httpsGet(L"api.github.com", L"/repos/allenrobberson/dropocalypse/releases/latest", json, err))
        return failInstall(err);
    std::string tag = jsonString(json, "tag_name");
    std::string version = (!tag.empty() && tag[0] == 'v') ? tag.substr(1) : tag;
    if (!versionIsNewer(version, APP_VERSION_STR)) return failInstall("You already have the latest version.");
    std::string fileUrl, sumsUrl;
    // "-x86_64.AppImage" since 1.6.3; older releases named it "-Linux-x86_64.AppImage".
    const std::string suffix = "-x86_64.AppImage";
    for (const auto& u : assetUrls(json)) {
        const bool appImage = u.size() > suffix.size() && u.compare(u.size() - suffix.size(), suffix.size(), suffix) == 0;
        if (appImage && (fileUrl.empty() || u.find("-Linux-x86_64.AppImage") == std::string::npos)) fileUrl = u;
        if (u.size() > 15 && u.compare(u.size() - 15, 15, "/SHA256SUMS.txt") == 0) sumsUrl = u;
    }
    if (fileUrl.empty()) return failInstall("This release has no Linux AppImage. Use the download page.");
    if (sumsUrl.empty()) return failInstall("This release has no checksums, so it can't be installed automatically. Use the download page.");
    std::string fileName = fileUrl.substr(fileUrl.find_last_of('/') + 1);

    // 2. Checksums, then the AppImage.
    std::string sums;
    if (!httpsGetUrl(sumsUrl, sums, err, 100000)) return failInstall(err);
    std::string expected = checksumFor(fileName, sums);
    if (expected.size() != 64) return failInstall("Couldn't read the release checksums.");
    std::string file;
    gInstall.progress = 0;
    if (!httpsGetUrl(fileUrl, file, err, 400'000'000, [](uint64_t done, uint64_t total) {
            if (total > 0) gInstall.progress = (float)done / (float)total;
        }))
        return failInstall(err);

    // 3. Verify.
    gInstall.phase = InstallPhase::Verifying;
    std::string want = expected;
    std::transform(want.begin(), want.end(), want.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
    if (sha256Hex(file) != want) return failInstall("The download didn't match its checksum, so it wasn't installed. Try again.");

    // 4. Put it next to the current AppImage, then rename it into place.
    gInstall.phase = InstallPhase::Installing;
    std::string tmp = target + ".new";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return failInstall("Dropocalypse can't replace itself in this folder. Move the AppImage somewhere you can "
                               "edit (like your home folder), or use the download page.");
    bool wrote = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    wrote = std::fclose(f) == 0 && wrote;
    if (!wrote) { std::remove(tmp.c_str()); return failInstall("Couldn't save the download."); }
    chmod(tmp.c_str(), 0755);
    if (std::rename(tmp.c_str(), target.c_str()) != 0) {
        std::remove(tmp.c_str());
        return failInstall("Couldn't put the new version in place. Nothing was changed.");
    }
    {
        std::lock_guard<std::mutex> lock(gInstall.m);
        gInstall.newPath = target;
    }
    gInstall.phase = InstallPhase::ReadyToRelaunch;
}

static void startInstall() {
    if (kStoreEdition) return;
    if (installBusy()) return;
    if (isDevVersion(APP_VERSION_STR)) return failInstall("This is a development build. Use the download page.");
    if (appImagePath().empty())
        return failInstall("Automatic updates work for the AppImage. For this copy, use the download page.");
    gInstall.phase = InstallPhase::Downloading;
    gInstall.progress = 0;
    std::thread(installWorker).detach();
}

/// Called each frame: once the new AppImage is in place, save settings, start it, quit.
static bool relaunchIfUpdated() {
    if (gInstall.phase != InstallPhase::ReadyToRelaunch) return false;
    writeFile(g.settingsPath, serialize(g.settings));
    g.lastSaved = serialize(g.settings);
    std::string path;
    {
        std::lock_guard<std::mutex> lock(gInstall.m);
        path = gInstall.newPath;
    }
    if (launchDetached({path, "--updated"})) return true;   // caller quits
    failInstall("The update was installed, but Dropocalypse couldn't restart. Please open it again.");
    return false;
}

