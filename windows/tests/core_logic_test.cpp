#include "core_logic.h"

#include <cmath>
#include <cstdio>
#include <string>

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

int main() {
    testVersions();
    testChecksums();
    testSceneCycle();
    testRecordingLayout();
    testSignaturePolicy();
    if (gFailures) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("core_logic tests passed\n");
    return 0;
}
