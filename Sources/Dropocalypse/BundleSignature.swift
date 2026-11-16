import DropocalypseCore
import Foundation

/// Reads a bundle's Developer ID signature and asks Gatekeeper (`spctl`) whether it would
/// accept the app. The install policy in DropocalypseCore decides what to do with the result.
enum BundleSignature {
    static func observe(app: URL) -> CodeSignatureObservation {
        let verify = command("/usr/bin/codesign", ["--verify", "--deep", "--strict", "--verbose=2", app.path])
        let display = command("/usr/bin/codesign", ["-dv", "--verbose=4", app.path])
        var teamID: String?
        var developerID = false
        for line in display.output.split(whereSeparator: \.isNewline) {
            let text = String(line)
            if text.hasPrefix("TeamIdentifier=") {
                let value = String(text.dropFirst("TeamIdentifier=".count))
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                if !value.isEmpty && value != "not set" { teamID = value }
            }
            if text.hasPrefix("Authority=Developer ID Application:") { developerID = true }
        }
        let gatekeeper = command("/usr/sbin/spctl", ["--assess", "--type", "execute", "--verbose", app.path])
        return CodeSignatureObservation(
            signatureValid: verify.status == 0,
            developerID: developerID,
            teamID: teamID,
            gatekeeperAccepted: gatekeeper.status == 0)
    }

    private static func command(_ tool: String, _ args: [String]) -> (status: Int32, output: String) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = args
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            return (-1, "")
        }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return (process.terminationStatus, String(data: data, encoding: .utf8) ?? "")
    }
}
