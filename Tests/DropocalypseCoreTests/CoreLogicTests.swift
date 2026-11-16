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
