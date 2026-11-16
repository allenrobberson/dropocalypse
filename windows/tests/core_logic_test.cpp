#include "core_logic.h"

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
    testSignaturePolicy();
    if (gFailures) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("core_logic tests passed\n");
    return 0;
}
