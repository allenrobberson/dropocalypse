#include "core_logic.h"
#include "logo_layers.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int gFailures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++gFailures;                                                                                     \
        }                                                                                                    \
    } while (0)

static void testVersions() {
    CHECK(versionIsNewer("0.10.0", "0.9.2"));
    CHECK(versionIsNewer("1.2.2", "1.2.1"));
    CHECK(versionIsNewer("2.0.0", "1.9.9"));
    CHECK(versionIsNewer("1.2.1", "1.2"));
    CHECK(!versionIsNewer("1.2.1", "1.2.1"));
    CHECK(!versionIsNewer("1.2.0", "1.2.1"));
    CHECK(!versionIsNewer("1.2", "1.2.0"));
    CHECK(!versionIsNewer("1.2.0-dev", "1.2.0"));
    CHECK(!versionIsNewer("1.2.0", "1.2.0-beta"));
    CHECK(versionIsNewer("1.3.0-dev", "1.2.9"));
}

static void testChecksums() {
    const std::string listing =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  Dropocalypse-1.2.6-macOS.zip\n"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb *Dropocalypse-1.2.6-Windows-portable.zip\r\n"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc  Dropocalypse-User-Guide.pdf\n"
        "not a checksum line\n";
    CHECK(checksumFor("Dropocalypse-1.2.6-macOS.zip", listing) ==
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(checksumFor("Dropocalypse-1.2.6-Windows-portable.zip", listing) ==
          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    CHECK(checksumFor("Dropocalypse-User-Guide.pdf", listing) ==
          "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    CHECK(checksumFor("missing.zip", listing).empty());
    CHECK(checksumFor("macOS.zip", listing).empty());
    CHECK(checksumFor("Dropocalypse-1.2.6-macOS.zip", "").empty());
    CHECK(checksumFor("only.txt", "deadbeef  only.txt\n") == "deadbeef");
}

static void testSceneCycle() {
    CHECK(nextCycleIndex(0, 4, 0) == 1);
    CHECK(nextCycleIndex(3, 4, 0) == 0);
    CHECK(nextCycleIndex(0, 4, 1u << 1) == 2);
    CHECK(nextCycleIndex(3, 4, 1u << 0) == 1);
    CHECK(nextCycleIndex(0, 4, 0xFu) == 1);
    CHECK(nextCycleIndex(2, 4, 0xFu) == 3);
    CHECK(nextCycleIndex(-1, 4, 0) == 0);
    CHECK(nextCycleIndex(0, 1, 0) == 0);
    CHECK(nextCycleIndex(5, 0, 0) == 0);
    CHECK(!cycleBitSkipped(0, 3));
    // Styles past 32 (bundled MilkDrop presets) use the upper bits.
    CHECK(cycleBitSkipped(1ull << 40, 40));
    CHECK(!cycleBitSkipped(1ull << 40, 70));
    CHECK(nextCycleIndex(39, 45, 1ull << 40) == 41);
    // Presets from the user's folder (index 35 and up here) stay out of auto-cycle.
    CHECK(nextCycleIndex(34, 40, 0, 35) == 0);
    CHECK(nextCycleIndex(36, 40, 0, 35) == 0);
    CHECK(nextCycleIndex(10, 40, 0, 35) == 11);
    CHECK(cycleBitSkipped(1u << 3, 3));
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void testRecordingLayout() {
    const RecordingPixels p720 = recordingPixels(0, false);
    const RecordingPixels p1080 = recordingPixels(1, false);
    const RecordingPixels p4k = recordingPixels(2, false);
    CHECK(p720.width == 1280 && p720.height == 720);
    CHECK(p1080.width == 1920 && p1080.height == 1080);
    CHECK(p4k.width == 3840 && p4k.height == 2160);

    const RecordingPixels v720 = recordingPixels(0, true);
    const RecordingPixels v1080 = recordingPixels(1, true);
    const RecordingPixels v4k = recordingPixels(2, true);
    CHECK(v720.width == 720 && v720.height == 1280);
    CHECK(v1080.width == 1080 && v1080.height == 1920);
    CHECK(v4k.width == 2160 && v4k.height == 3840);
    CHECK(recordingPixels(-3, false).width == 1280);
    CHECK(recordingPixels(8, true).width == 2160 && recordingPixels(8, true).height == 3840);
    CHECK(v720.width * 16 == v720.height * 9);
    CHECK(v1080.width * 16 == v1080.height * 9);
    CHECK(v4k.width * 16 == v4k.height * 9);

    const ContentRect full = fitContentRect(1600, 900, false);
    CHECK(full.x == 0 && full.y == 0 && full.width == 1600 && full.height == 900);

    const ContentRect wide = fitContentRect(1920, 1080, true);
    CHECK(wide.y == 0 && near(wide.height, 1080));
    CHECK(near(wide.width, 1080.0f * 9.0f / 16.0f));
    CHECK(near(wide.x, (1920.0f - wide.width) * 0.5f));

    const ContentRect exact = fitContentRect(1080, 1920, true);
    CHECK(near(exact.x, 0) && near(exact.y, 0));
    CHECK(near(exact.width, 1080) && near(exact.height, 1920));

    const ContentRect tall = fitContentRect(900, 2000, true);
    CHECK(tall.x == 0 && near(tall.width, 900));
    CHECK(near(tall.height, 900.0f / (9.0f / 16.0f)));
    CHECK(near(tall.y, (2000.0f - tall.height) * 0.5f));

    const ContentRect empty = fitContentRect(0, 100, true);
    CHECK(empty.width == 0 && empty.height == 100);
}

static void testWasapiCapturePlan() {
    const WasapiStreamPlan loop = planWasapiCapture(WasapiEndpointKind::RenderLoopback, true);
    CHECK(loop.sharedMode);
    CHECK(!loop.openRenderStream);
    CHECK(!loop.pullSharedEnginePeriod);
    CHECK(loop.bufferDurationHns == 0);
    CHECK((loop.flags & WasapiFlagLoopback) != 0);
    CHECK((loop.flags & WasapiFlagEventCallback) != 0);
    CHECK(wasapiShareMode(loop) == kWasapiShareModeShared);
    CHECK(wasapiStreamFlags(loop) == (kWasapiStreamFlagsLoopback | kWasapiStreamFlagsEventCallback));
    CHECK(!wasapiPlanBlocksOtherPlayback(loop));

    // Event-driven loopback is unavailable on some older builds. The fallback still
    // listens in shared mode at the engine's default period, and still does not render.
    const WasapiStreamPlan poll = planWasapiCapture(WasapiEndpointKind::RenderLoopback, false);
    CHECK(!poll.openRenderStream);
    CHECK(!poll.pullSharedEnginePeriod);
    CHECK(poll.bufferDurationHns == 0);
    CHECK((poll.flags & WasapiFlagLoopback) != 0);
    CHECK((poll.flags & WasapiFlagEventCallback) == 0);
    CHECK(wasapiStreamFlags(poll) == kWasapiStreamFlagsLoopback);
    CHECK(wasapiShareMode(poll) == kWasapiShareModeShared);
    CHECK(!wasapiPlanBlocksOtherPlayback(poll));

    // A microphone may ask its own capture endpoint for a shorter period. It still
    // must not open a render stream or set the loopback flag.
    const WasapiStreamPlan mic = planWasapiCapture(WasapiEndpointKind::CaptureDevice, true);
    CHECK(mic.sharedMode);
    CHECK(mic.pullSharedEnginePeriod);
    CHECK(!mic.openRenderStream);
    CHECK((mic.flags & WasapiFlagLoopback) == 0);
    CHECK(wasapiStreamFlags(mic) == kWasapiStreamFlagsEventCallback);
    CHECK(!wasapiPlanBlocksOtherPlayback(mic));
    const WasapiStreamPlan micPoll = planWasapiCapture(WasapiEndpointKind::CaptureDevice, false);
    CHECK(micPoll.pullSharedEnginePeriod);
    CHECK((micPoll.flags & WasapiFlagLoopback) == 0);
    CHECK(!wasapiPlanBlocksOtherPlayback(micPoll));

    // The three ways this app used to (or could) stop other programs from playing.
    WasapiStreamPlan blocked = loop;
    blocked.openRenderStream = true;
    CHECK(wasapiPlanBlocksOtherPlayback(blocked));
    blocked = loop;
    blocked.pullSharedEnginePeriod = true;
    CHECK(wasapiPlanBlocksOtherPlayback(blocked));
    blocked = loop;
    blocked.sharedMode = false;
    CHECK(wasapiShareMode(blocked) == kWasapiShareModeExclusive);
    CHECK(wasapiPlanBlocksOtherPlayback(blocked));

    CHECK(kWasapiShareModeShared == 0u);
    CHECK(kWasapiShareModeExclusive == 1u);
    CHECK(kWasapiStreamFlagsLoopback == 0x00020000u);
    CHECK(kWasapiStreamFlagsEventCallback == 0x00040000u);
}

static void testWindowsUpdateHandoff() {
    const WindowsUpdateHandoff handoff = planWindowsUpdateHandoff();
    CHECK(handoff.waitForExitMs >= 1000);
    CHECK(handoff.terminateIfStillAlive);
    CHECK(!handoff.replaceFilesWhileRunning);
    CHECK(!handoff.launchReplacementWhileOldAlive);
    CHECK(handoff.relaunchMutexWaitMs >= handoff.waitForExitMs);

    CHECK(planInstanceLaunch(false, false) == InstanceLaunch::BecomePrimary);
    CHECK(planInstanceLaunch(false, true) == InstanceLaunch::BecomePrimary);
    CHECK(planInstanceLaunch(true, false) == InstanceLaunch::FocusExisting);
    CHECK(planInstanceLaunch(true, true) == InstanceLaunch::RetryThenBecomePrimary);

    const char* script = windowsUpdateHelperScript();
    const std::string text = script ? script : "";
    const auto waitPos = text.find("WaitForExit");
    const auto killPos = text.find("Stop-Process");
    const auto copyPos = text.find("Copy-Item");
    const auto startPos = text.find("Start-Process");
    const auto stillPos = text.find("$null -ne $still");
    CHECK(waitPos != std::string::npos);
    CHECK(killPos != std::string::npos);
    CHECK(copyPos != std::string::npos);
    CHECK(startPos != std::string::npos);
    CHECK(stillPos != std::string::npos);
    CHECK(waitPos < killPos);
    CHECK(killPos < stillPos);
    CHECK(stillPos < copyPos);
    CHECK(copyPos < startPos);
    CHECK(text.find("--updated") != std::string::npos);
    CHECK(text.find("throw") != std::string::npos);
}

static void testGpuSignalAndPacing() {
    CHECK(gpuSignalVertices(GpuSignalMode::Polyline, 256, true) == 256 * 6);
    CHECK(gpuSignalVertices(GpuSignalMode::Polyline, 256, false) == 255 * 6);
    CHECK(gpuSignalVertices(GpuSignalMode::Bars, 48, false) == 48 * 6);
    CHECK(gpuSignalVertices(GpuSignalMode::Radial, 48, false) == 48 * 6);
    CHECK(gpuSignalVertices(GpuSignalMode::Lissajous, 244, false) == 243 * 6);
    CHECK(gpuSignalVertices(GpuSignalMode::Dots, 144, false) == 144 * 24);
    CHECK(gpuSignalVertices(GpuSignalMode::Polyline, 1, false) == 0);
    CHECK(gpuSignalVertices(GpuSignalMode::None, 256, true) == 0);

    CHECK(!uiFrameNeeded(false, false, false, false));
    CHECK(uiFrameNeeded(true, false, false, false));
    CHECK(uiFrameNeeded(false, true, false, false));
    CHECK(uiFrameNeeded(false, false, true, false));
    CHECK(uiFrameNeeded(false, false, false, true));
    CHECK(hiddenFrameSleepMs(false, false) == 0);
    CHECK(hiddenFrameSleepMs(true, false) == 100);
    CHECK(hiddenFrameSleepMs(false, true) == 100);
}

static void testUpdateHosts() {
    CHECK(isTrustedUpdateHost(httpsHostOf("https://github.com/allenrobberson/dropocalypse/releases/download/v1/a.zip")));
    CHECK(isTrustedUpdateHost(httpsHostOf("https://release-assets.githubusercontent.com/x?sig=1")));
    CHECK(isTrustedUpdateHost(httpsHostOf("https://objects.githubusercontent.com/x")));
    CHECK(isTrustedUpdateHost(httpsHostOf("https://API.GitHub.com/repos")));
    CHECK(!isTrustedUpdateHost(httpsHostOf("https://evil.example/a.zip")));
    CHECK(!isTrustedUpdateHost(httpsHostOf("https://github.com.evil.example/a.zip")));
    CHECK(!isTrustedUpdateHost(httpsHostOf("https://github.com@evil.example/a.zip")));
    CHECK(!isTrustedUpdateHost(httpsHostOf("http://github.com/a.zip")));
    CHECK(httpsHostOf("https://github.com:443/x") == "github.com");
}

static void testSignaturePolicy() {
    UpdateSignatureFacts ok;
    ok.runningIsSigned = true;
    ok.runningPublisher = "SignPath Foundation";
    ok.updateIsSigned = true;
    ok.updateTrusted = true;
    ok.updatePublisher = "SignPath Foundation";
    CHECK(updateSignatureRefusal(ok) == nullptr);

    UpdateSignatureFacts unsignedRunning;
    CHECK(updateSignatureRefusal(unsignedRunning) == nullptr);
    unsignedRunning.updateIsSigned = false;
    CHECK(updateSignatureRefusal(unsignedRunning) == nullptr);

    UpdateSignatureFacts unsignedUpdate = ok;
    unsignedUpdate.updateIsSigned = false;
    unsignedUpdate.updateTrusted = false;
    CHECK(updateSignatureRefusal(unsignedUpdate) != nullptr);

    UpdateSignatureFacts untrusted = ok;
    untrusted.updateTrusted = false;
    CHECK(updateSignatureRefusal(untrusted) != nullptr);

    UpdateSignatureFacts other = ok;
    other.updatePublisher = "Someone Else";
    CHECK(updateSignatureRefusal(other) != nullptr);

    UpdateSignatureFacts blank = ok;
    blank.updatePublisher.clear();
    CHECK(updateSignatureRefusal(blank) != nullptr);

    UpdateSignatureFacts blankRunning = ok;
    blankRunning.runningPublisher.clear();
    CHECK(updateSignatureRefusal(blankRunning) != nullptr);
}


static void testOverlayLayout() {
    // One item: centered, the whole frame to itself.
    ItemSlot one = itemSlot(0, 1, 16.0f / 9.0f);
    CHECK(near(one.cx, 0) && near(one.cy, 0));
    // Landscape: a row, left to right, evenly spaced and symmetric.
    const float a = 16.0f / 9.0f;
    ItemSlot l0 = itemSlot(0, 2, a), l1 = itemSlot(1, 2, a);
    CHECK(l0.cx < 0 && l1.cx > 0 && near(l0.cx, -l1.cx) && near(l0.cy, 0) && near(l1.cy, 0));
    for (int n = 2; n <= 4; ++n) {
        for (int i = 0; i + 1 < n; ++i) {
            ItemSlot s = itemSlot(i, n, a), t = itemSlot(i + 1, n, a);
            CHECK(t.cx > s.cx);
            CHECK(s.cx + s.maxHalfW <= t.cx - t.maxHalfW + 1e-4f);   // no overlap at rest
        }
        ItemSlot first = itemSlot(0, n, a), last = itemSlot(n - 1, n, a);
        CHECK(first.cx - first.maxHalfW >= -a);                      // inside the frame
        CHECK(last.cx + last.maxHalfW <= a);
    }
    // Portrait: a column, top to bottom.
    const float p = 9.0f / 16.0f;
    for (int n = 2; n <= 4; ++n) {
        for (int i = 0; i + 1 < n; ++i) {
            ItemSlot s = itemSlot(i, n, p), t = itemSlot(i + 1, n, p);
            CHECK(near(s.cx, 0) && s.cy > t.cy);
            CHECK(s.cy - s.maxHalfH >= t.cy + t.maxHalfH - 1e-4f);
        }
        CHECK(itemSlot(0, n, p).cy + itemSlot(0, n, p).maxHalfH <= 1.0f);
    }
    // Out-of-range input is clamped, not trusted.
    ItemSlot c = itemSlot(9, 9, a);
    CHECK(near(c.cx, itemSlot(3, 4, a).cx));
    // Extents: Size controls the longer side, then the slot caps it.
    ItemExtent e1 = itemExtent(2.0f, 0.32f, one);
    CHECK(near(e1.halfW, 0.32f) && near(e1.halfH, 0.16f));
    ItemExtent e4 = itemExtent(4.0f, 0.8f, itemSlot(0, 4, a));
    CHECK(e4.halfW <= itemSlot(0, 4, a).maxHalfW + 1e-5f);
    CHECK(near(e4.halfW / e4.halfH, 4.0f));                          // keeps its shape
    ItemExtent tall = itemExtent(0.5f, 0.8f, itemSlot(0, 3, p));
    CHECK(tall.halfH <= itemSlot(0, 3, p).maxHalfH + 1e-5f && near(tall.halfW / tall.halfH, 0.5f));
}

static void testOverlayMotionAndText() {
    MotionVariant v0 = motionVariant(0);
    CHECK(near(v0.delay, 0) && near(v0.side, 1) && near(v0.phase, 0) && near(v0.amount, 1));
    for (int i = 1; i < kMaxOverlayItems; ++i) {
        MotionVariant v = motionVariant(i);
        CHECK(v.delay >= 0 && v.delay <= 0.08f);      // small offsets: still on the beat
        CHECK(v.amount > 0.85f && v.amount < 1.15f);
    }
    CHECK(motionVariant(1).side != motionVariant(0).side);
    CHECK(textColorRGB(0) == 0xFFFFFF && textColorRGB(99) == 0xFFFFFF);
    CHECK(std::string(textFontName(3)) == "Mono");

    // Coverage -> premultiplied color with a dark outline around it.
    const int w = 9, h = 5;
    std::vector<std::uint8_t> cov(w * h, 0);
    cov[2 * w + 4] = 255;
    std::vector<std::uint8_t> px = textCoverageToRGBA(cov, w, h, 0xFF3EA5, 2);
    CHECK(px.size() == (size_t)w * h * 4);
    const std::uint8_t* c = &px[(2 * w + 4) * 4];
    CHECK(c[0] == 0xFF && c[1] == 0x3E && c[2] == 0xA5 && c[3] == 255);
    const std::uint8_t* o = &px[(2 * w + 6) * 4];                  // 2 px away: outline only
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] > 150);
    const std::uint8_t* far = &px[(2 * w + 8) * 4];                // 4 px away: transparent
    CHECK(far[3] == 0);

    CHECK(cleanItemText("  DJ\nNight\t ") == "DJ Night");
    CHECK(cleanItemText("   ").empty());
    CHECK(cleanItemText(std::string(80, 'x')).size() == 60);
    CHECK(cleanItemText(std::string(70, 'x') + "\xC3\xA9").size() == 60);   // counts code points
}

static void testOverlaySettings() {
    std::vector<OverlayItemInfo> items(2);
    items[0].file = "C:\\Users\\a\\AppData\\Roaming\\Dropocalypse\\logo-1.png";
    items[0].name = "band.png";
    items[1].isText = true;
    items[1].text = "Friday = Bass\nNight";
    items[1].font = 2;
    items[1].color = 3;
    const std::string text = serializeItems(items);
    std::map<std::string, std::string> kv;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        std::string line = text.substr(i, j - i);
        size_t eq = line.find('=');
        kv[line.substr(0, eq)] = line.substr(eq + 1);
        i = j + 1;
    }
    auto get = [&](const std::string& k, std::string& v) {
        auto it = kv.find(k);
        if (it == kv.end()) return false;
        v = it->second;
        return true;
    };
    std::vector<OverlayItemInfo> back = parseItems(get);
    CHECK(back.size() == 2);
    CHECK(!back[0].isText && back[0].file == items[0].file && back[0].name == "band.png");
    CHECK(back[1].isText && back[1].text == "Friday = Bass Night" && back[1].font == 2 && back[1].color == 3);
    CHECK(back[1].name == back[1].text);

    // Older settings: a single logo becomes item 0. No logo means no items.
    std::map<std::string, std::string> old = {{"logoFile", "/home/a/.config/dropocalypse/logo.png"},
                                              {"logoName", "mine.png"}};
    auto getOld = [&](const std::string& k, std::string& v) {
        auto it = old.find(k);
        if (it == old.end()) return false;
        v = it->second;
        return true;
    };
    std::vector<OverlayItemInfo> migrated = parseItems(getOld);
    CHECK(migrated.size() == 1 && !migrated[0].isText && migrated[0].name == "mine.png");
    std::map<std::string, std::string> none;
    auto getNone = [&](const std::string& k, std::string& v) {
        auto it = none.find(k);
        if (it == none.end()) return false;
        v = it->second;
        return true;
    };
    CHECK(parseItems(getNone).empty());
    // An explicit empty list wins over an old logoFile key.
    old["items"] = "0";
    CHECK(parseItems(getOld).empty());
    // Bad values are clamped or dropped.
    std::map<std::string, std::string> bad = {{"items", "9"}, {"item0.kind", "text"}, {"item0.text", "Hi"},
                                              {"item0.font", "42"}, {"item0.color", "-3"},
                                              {"item1.kind", "image"}, {"item1.file", ""},
                                              {"item2.kind", "text"}, {"item2.text", "  "}};
    auto getBad = [&](const std::string& k, std::string& v) {
        auto it = bad.find(k);
        if (it == bad.end()) return false;
        v = it->second;
        return true;
    };
    std::vector<OverlayItemInfo> cleaned = parseItems(getBad);
    CHECK(cleaned.size() == 1 && cleaned[0].font == 0 && cleaned[0].color == 0);
}

static void testWhatsNew() {
    CHECK(!shouldShowWhatsNew("", "1.4.0", false));          // brand-new install: welcome notice instead
    CHECK(shouldShowWhatsNew("", "1.4.0", true));            // updated from a version without the window
    CHECK(shouldShowWhatsNew("1.3.2", "1.4.0", true));
    CHECK(!shouldShowWhatsNew("1.4.0", "1.4.0", true));      // already seen
    CHECK(!shouldShowWhatsNew("1.5.0", "1.4.0", true));      // went back to an older version
    CHECK(!shouldShowWhatsNew("1.3.2", "0.0.0-dev", true));  // development build
    const std::vector<std::string> v{"1.5.0", "1.4.1", "1.4.0", "1.3.2", "1.3.1", "1.3.0", "1.2.12"};
    CHECK((whatsNewEntries(v, "", "1.4.1") == std::vector<int>{1}));
    CHECK((whatsNewEntries(v, "1.3.2", "1.4.1") == std::vector<int>{1, 2}));
    CHECK((whatsNewEntries(v, "1.2.12", "1.4.1", 3) == std::vector<int>{1, 2, 3}));
    CHECK(whatsNewEntries(v, "", "1.4.2").empty());           // no notes written for this version
    CHECK(whatsNewEntries(v, "1.4.1", "1.4.1").empty());
}

int main() {
    testWhatsNew();
    testVersions();
    testChecksums();
    testSceneCycle();
    testRecordingLayout();
    testWasapiCapturePlan();
    testWindowsUpdateHandoff();
    testGpuSignalAndPacing();
    testSignaturePolicy();
    testUpdateHosts();
    testOverlayLayout();
    testOverlayMotionAndText();
    testOverlaySettings();
    if (gFailures) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    {   // Logo layers: three shapes (plus a speck and soft edges) become three layers, left to right.
        const int w = 60, h = 20;
        std::vector<std::uint8_t> img((size_t)w * h * 4, 0);
        auto put = [&](int x, int y, int a) { img[((size_t)y * w + x) * 4 + 3] = (std::uint8_t)a; };
        for (int y = 5; y < 15; ++y) {
            for (int x = 40; x < 50; ++x) put(x, y, 255);   // right
            for (int x = 3; x < 12; ++x) put(x, y, 255);    // left
            for (int x = 22; x < 30; ++x) put(x, y, 255);   // middle
            put(12, y, 40);                                  // soft edge of the left shape
        }
        put(56, 2, 255);                                     // a speck: joins the nearest layer
        LayerSplit split = splitLogoLayers(img.data(), w, h);
        CHECK(split.layers.size() == 3);
        CHECK(split.layers[0].x0 == 3 && split.layers[0].x1 == 13);   // includes the soft edge
        CHECK(split.layers[1].x0 == 22);
        CHECK(split.layers[2].x1 == 57);                              // the speck went right
        int lw = 0, lh = 0;
        auto one = extractLogoLayer(img.data(), split, 1, lw, lh);
        CHECK(lw == 10 && lh == 12 && one.size() == (size_t)lw * lh * 4);
        CHECK(one[((size_t)1 * lw + 1) * 4 + 3] == 255);
        CHECK(one[3] == 0);
        // A single shape stays one layer; an empty image gives one full-size layer.
        std::vector<std::uint8_t> solid((size_t)8 * 8 * 4, 255);
        CHECK(splitLogoLayers(solid.data(), 8, 8).layers.size() == 1);
        std::vector<std::uint8_t> clear((size_t)8 * 8 * 4, 0);
        CHECK(splitLogoLayers(clear.data(), 8, 8).layers.size() == 1);
        // Many pieces: only the largest kLogoLayers survive.
        std::vector<std::uint8_t> dots((size_t)200 * 10 * 4, 0);
        for (int d = 0; d < 40; ++d)
            for (int y = 2; y < 6; ++y)
                for (int x = d * 5; x < d * 5 + 3; ++x) dots[((size_t)y * 200 + x) * 4 + 3] = 255;
        CHECK((int)splitLogoLayers(dots.data(), 200, 10).layers.size() == kMaxLogoLayers);
    }
    {   // Layer motion: a beat kicks the layers, they settle back, and 0 means no movement.
        std::vector<float> nx = {-1, 0, 1}, ny = {0, 0, 0};
        auto peakFor = [&](LayerAmounts amt, bool settle) {
            LayerMotion lm;
            lm.update(0.016f, amt, nx, ny, 0, 0, 0, 1, 0);
            float peak = 0;
            for (int f = 0; f < 40; ++f) {
                lm.update(0.016f, amt, nx, ny, f < 20 ? 1.0f : 0.0f, 0.5f, 0.3f, 2, 1.0f);
                for (size_t i = 0; i < 3; ++i) {
                    LayerPose p = lm.pose(i);
                    peak = std::max(peak, std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.scale - 1));
                }
            }
            if (settle) {
                for (int f = 0; f < 400; ++f) lm.update(0.016f, amt, nx, ny, 0, 0, 0, 2, 0);
                for (size_t i = 0; i < 3; ++i) {
                    LayerPose p = lm.pose(i);
                    CHECK(std::fabs(p.x) < 0.01f && std::fabs(p.y) < 0.01f && std::fabs(p.scale - 1) < 0.01f);
                }
            }
            return peak;
        };
        for (int kind = 0; kind < 4; ++kind) {
            LayerAmounts low, high;
            float* lp[4] = {&low.wave, &low.explode, &low.bands, &low.scatter};
            float* hp[4] = {&high.wave, &high.explode, &high.bands, &high.scatter};
            *lp[kind] = 20;
            *hp[kind] = 100;
            const float pl = peakFor(low, true), ph = peakFor(high, true);
            CHECK(pl > 0.01f);
            CHECK(ph > pl * 2);      // 100 is much wilder than 20
        }
        CHECK(peakFor(LayerAmounts(), false) == 0); // all sliders at 0: no movement
        LayerAmounts all{100, 100, 100, 100};
        CHECK(peakFor(all, true) > 0.5f);         // everything at once still settles
        CHECK(layerStrength(0) == 0 && layerStrength(100) > 3 && layerStrength(50) < 1.5f);
        LayerQuad q = layerQuad(0, 0, 1, 1, 0, 0, 0, 0.5f, 1, LayerPose());
        CHECK(std::fabs(q.cx + 0.5f) < 1e-5f && std::fabs(q.cy) < 1e-5f && std::fabs(q.hx - 0.5f) < 1e-5f);
    }
    {   // Kinetic Text rows cover the screen edge to edge and the beat lights one row.
        KineticRow rows[kKineticRows];
        for (float t : {0.0f, 3.7f, 120.25f}) {
            int n = kineticRows(rows, t, 16.0f / 9.0f, 3.0f, 9, 1.0f, 0.5f);
            CHECK(n == kKineticRows);
            for (int r = 0; r < n; ++r) {
                const KineticRow& row = rows[r];
                CHECK(row.copies > 1 && row.copies <= 40);
                CHECK(row.x0 - row.hx <= -16.0f / 9.0f);
                CHECK(row.x0 + (row.copies - 1) * row.spacing + row.hx >= 16.0f / 9.0f);
                CHECK(row.y > -1 && row.y < 1);
            }
            CHECK(rows[9 % kKineticRows].opacity > rows[(9 + 1) % kKineticRows].opacity);
        }
        kineticRows(rows, 0, 1, 40.0f, 0, 0, 0); // a very wide item still fits
        CHECK(rows[0].hx <= 0.9f + 1e-5f);
    }

    std::printf("core_logic tests passed\n");
    return 0;
}
