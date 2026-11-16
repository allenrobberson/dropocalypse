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
