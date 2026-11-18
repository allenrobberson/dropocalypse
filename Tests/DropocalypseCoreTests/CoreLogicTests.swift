import DropocalypseCore
import XCTest

final class CoreLogicTests: XCTestCase {
    func testVersionComparison() {
        XCTAssertTrue(VersionCompare.isNewer("0.10.0", than: "0.9.2"))
        XCTAssertTrue(VersionCompare.isNewer("1.2.2", than: "1.2.1"))
        XCTAssertTrue(VersionCompare.isNewer("2.0.0", than: "1.9.9"))
        XCTAssertTrue(VersionCompare.isNewer("1.2.1", than: "1.2"))
        XCTAssertFalse(VersionCompare.isNewer("1.2.1", than: "1.2.1"))
        XCTAssertFalse(VersionCompare.isNewer("1.2.0", than: "1.2.1"))
        XCTAssertFalse(VersionCompare.isNewer("1.2", than: "1.2.0"))
        XCTAssertFalse(VersionCompare.isNewer("1.2.0-dev", than: "1.2.0"))
        XCTAssertFalse(VersionCompare.isNewer("1.2.0", than: "1.2.0-beta"))
        XCTAssertTrue(VersionCompare.isNewer("1.3.0-dev", than: "1.2.9"))
    }

    func testChecksumParsing() {
        let listing = """
        aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  Dropocalypse-1.2.6-macOS.zip
        bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb *Dropocalypse-1.2.6-Windows-portable.zip
        cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc  Dropocalypse-User-Guide.pdf
        not a checksum line
        """
        XCTAssertEqual(
            Checksums.sha256(for: "Dropocalypse-1.2.6-macOS.zip", in: listing),
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        XCTAssertEqual(
            Checksums.sha256(for: "Dropocalypse-1.2.6-Windows-portable.zip", in: listing),
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
        XCTAssertEqual(
            Checksums.sha256(for: "Dropocalypse-User-Guide.pdf", in: listing),
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")
        XCTAssertNil(Checksums.sha256(for: "missing.zip", in: listing))
        XCTAssertNil(Checksums.sha256(for: "macOS.zip", in: listing))
        XCTAssertNil(Checksums.sha256(for: "Dropocalypse-1.2.6-macOS.zip", in: ""))
        XCTAssertEqual(Checksums.sha256(for: "only.txt", in: "deadbeef  only.txt\n"), "deadbeef")
    }

    func testSceneCycleSkipsUncheckedStyles() {
        let names = ["Classic Tunnel", "Spectrum Skyline", "Starburst", "Vortex"]
        XCTAssertEqual(SceneCycle.nextIndex(after: 0, names: names, skipped: []), 1)
        XCTAssertEqual(SceneCycle.nextIndex(after: 3, names: names, skipped: []), 0)
        XCTAssertEqual(SceneCycle.nextIndex(after: 0, names: names, skipped: ["Spectrum Skyline"]), 2)
        XCTAssertEqual(SceneCycle.nextIndex(after: 3, names: names, skipped: ["Classic Tunnel"]), 1)
        XCTAssertEqual(SceneCycle.nextIndex(after: 0, names: names, skipped: Set(names)), 1)
        XCTAssertEqual(SceneCycle.nextIndex(after: 2, names: names, skipped: Set(names)), 3)
        XCTAssertEqual(SceneCycle.nextIndex(after: 0, names: ["Only"], skipped: []), 0)
        XCTAssertEqual(SceneCycle.nextIndex(after: 5, names: [], skipped: []), 0)
    }

    func testRecordingSizeFollowsOrientation() {
        XCTAssertEqual(FrameLayout.recordingSize(quality: 0, portrait: false), FrameLayout.Size(width: 1280, height: 720))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 1, portrait: false), FrameLayout.Size(width: 1920, height: 1080))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 2, portrait: false), FrameLayout.Size(width: 3840, height: 2160))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 0, portrait: true), FrameLayout.Size(width: 720, height: 1280))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 1, portrait: true), FrameLayout.Size(width: 1080, height: 1920))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 2, portrait: true), FrameLayout.Size(width: 2160, height: 3840))
        XCTAssertEqual(FrameLayout.recordingSize(quality: -4, portrait: false), FrameLayout.Size(width: 1280, height: 720))
        XCTAssertEqual(FrameLayout.recordingSize(quality: 9, portrait: true), FrameLayout.Size(width: 2160, height: 3840))
        for quality in 0...2 {
            let portrait = FrameLayout.recordingSize(quality: quality, portrait: true)
            XCTAssertEqual(portrait.width * 16, portrait.height * 9)
            let landscape = FrameLayout.recordingSize(quality: quality, portrait: false)
            XCTAssertEqual(landscape.width, portrait.height)
            XCTAssertEqual(landscape.height, portrait.width)
        }
    }

    func testPortraitFrameIsCenteredNineBySixteen() {
        let full = FrameLayout.contentRect(viewWidth: 1600, viewHeight: 900, portrait: false)
        XCTAssertEqual(full, FrameLayout.Rect(x: 0, y: 0, width: 1600, height: 900))

        let wide = FrameLayout.contentRect(viewWidth: 1920, viewHeight: 1080, portrait: true)
        XCTAssertEqual(wide.y, 0, accuracy: 0.001)
        XCTAssertEqual(wide.height, 1080, accuracy: 0.001)
        XCTAssertEqual(wide.width, 1080.0 * 9.0 / 16.0, accuracy: 0.001)
        XCTAssertEqual(wide.x, (1920 - wide.width) / 2, accuracy: 0.001)
        XCTAssertEqual(wide.aspect, 9.0 / 16.0, accuracy: 0.0001)

        let exact = FrameLayout.contentRect(viewWidth: 1080, viewHeight: 1920, portrait: true)
        XCTAssertEqual(exact.x, 0, accuracy: 0.001)
        XCTAssertEqual(exact.y, 0, accuracy: 0.001)
        XCTAssertEqual(exact.width, 1080, accuracy: 0.001)
        XCTAssertEqual(exact.height, 1920, accuracy: 0.001)

        let tall = FrameLayout.contentRect(viewWidth: 900, viewHeight: 2000, portrait: true)
        XCTAssertEqual(tall.x, 0, accuracy: 0.001)
        XCTAssertEqual(tall.width, 900, accuracy: 0.001)
        XCTAssertEqual(tall.height, 900 / (9.0 / 16.0), accuracy: 0.001)
        XCTAssertEqual(tall.y, (2000 - tall.height) / 2, accuracy: 0.001)

        let empty = FrameLayout.contentRect(viewWidth: 0, viewHeight: 100, portrait: true)
        XCTAssertEqual(empty, FrameLayout.Rect(x: 0, y: 0, width: 0, height: 100))
    }

    func testOverlayLayoutRowAndColumn() {
        let wide: Float = 16.0 / 9.0
        let one = OverlayLayout.slot(index: 0, count: 1, aspect: wide)
        XCTAssertEqual(one.cx, 0); XCTAssertEqual(one.cy, 0)
        for n in 2...4 {
            for i in 0..<(n - 1) {
                let s = OverlayLayout.slot(index: i, count: n, aspect: wide)
                let t = OverlayLayout.slot(index: i + 1, count: n, aspect: wide)
                XCTAssertLessThan(s.cx, t.cx)
                XCTAssertEqual(s.cy, 0)
                XCTAssertLessThanOrEqual(s.cx + s.maxHalfW, t.cx - t.maxHalfW + 1e-4)
            }
            let last = OverlayLayout.slot(index: n - 1, count: n, aspect: wide)
            XCTAssertLessThanOrEqual(last.cx + last.maxHalfW, wide)
        }
        let tall: Float = 9.0 / 16.0
        for n in 2...4 {
            for i in 0..<(n - 1) {
                let s = OverlayLayout.slot(index: i, count: n, aspect: tall)
                let t = OverlayLayout.slot(index: i + 1, count: n, aspect: tall)
                XCTAssertEqual(s.cx, 0)
                XCTAssertGreaterThan(s.cy, t.cy)
                XCTAssertGreaterThanOrEqual(s.cy - s.maxHalfH, t.cy + t.maxHalfH - 1e-4)
            }
        }
        XCTAssertEqual(OverlayLayout.slot(index: 9, count: 9, aspect: wide),
                       OverlayLayout.slot(index: 3, count: 4, aspect: wide))
    }

    func testOverlayExtentKeepsShapeInsideSlot() {
        let one = OverlayLayout.slot(index: 0, count: 1, aspect: 16.0 / 9.0)
        let e1 = OverlayLayout.extent(imageAspect: 2, size: 0.32, slot: one)
        XCTAssertEqual(e1.halfW, 0.32, accuracy: 1e-5)
        XCTAssertEqual(e1.halfH, 0.16, accuracy: 1e-5)
        let s4 = OverlayLayout.slot(index: 0, count: 4, aspect: 16.0 / 9.0)
        let e4 = OverlayLayout.extent(imageAspect: 4, size: 0.8, slot: s4)
        XCTAssertLessThanOrEqual(e4.halfW, s4.maxHalfW + 1e-5)
        XCTAssertEqual(e4.halfW / e4.halfH, 4, accuracy: 1e-4)
    }

    func testOverlayMotionVariantsAndText() {
        XCTAssertEqual(OverlayLayout.motionVariant(0),
                       OverlayLayout.MotionVariant(delay: 0, side: 1, phase: 0, amount: 1))
        for i in 1..<OverlayLayout.maxItems {
            let v = OverlayLayout.motionVariant(i)
            XCTAssertLessThanOrEqual(v.delay, 0.08)
            XCTAssertGreaterThan(v.amount, 0.85)
            XCTAssertLessThan(v.amount, 1.15)
        }
        XCTAssertNotEqual(OverlayLayout.motionVariant(1).side, OverlayLayout.motionVariant(0).side)
        XCTAssertEqual(OverlayLayout.cleanText("  DJ\nNight\t "), "DJ Night")
        XCTAssertEqual(OverlayLayout.cleanText("   "), "")
        XCTAssertEqual(OverlayLayout.cleanText(String(repeating: "x", count: 80)).count, 60)
        XCTAssertEqual(OverlayLayout.textColor(99), 0xFFFFFF)
        XCTAssertEqual(OverlayLayout.textFontNames.count, 4)
        XCTAssertEqual(OverlayLayout.textColors.count, OverlayLayout.textColorNames.count)
    }

    func testSignaturePolicy() {
        let signed = observation(teamID: "TEAMID1234")
        XCTAssertEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: signed, runningTeamID: "TEAMID1234")),
            .allow)
        XCTAssertEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: signed, runningTeamID: "teamid1234")),
            .allow)

        XCTAssertNotEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: signed, runningTeamID: nil)),
            .allow)
        XCTAssertNotEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: signed, runningTeamID: "  ")),
            .allow)

        let invalid = observation(signatureValid: false)
        XCTAssertNotEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: invalid, runningTeamID: "TEAMID1234")),
            .allow)

        let adHoc = observation(developerID: false, teamID: nil)
        XCTAssertNotEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: adHoc, runningTeamID: "TEAMID1234")),
            .allow)

        let otherTeam = observation(teamID: "OTHERTEAM1")
        XCTAssertNotEqual(
            UpdateSignaturePolicy.decide(UpdateSignatureFacts(candidate: otherTeam, runningTeamID: "TEAMID1234")),
            .allow)

        let unnotarized = observation(gatekeeperAccepted: false)
        if case .refuse(let message) = UpdateSignaturePolicy.decide(
            UpdateSignatureFacts(candidate: unnotarized, runningTeamID: "TEAMID1234")) {
            XCTAssertTrue(message.contains("Gatekeeper"))
        } else {
            XCTFail("expected Gatekeeper refusal")
        }
    }

    private func observation(
        signatureValid: Bool = true,
        developerID: Bool = true,
        teamID: String? = "TEAMID1234",
        gatekeeperAccepted: Bool = true
    ) -> CodeSignatureObservation {
        CodeSignatureObservation(
            signatureValid: signatureValid,
            developerID: developerID,
            teamID: teamID,
            gatekeeperAccepted: gatekeeperAccepted)
    }
}
