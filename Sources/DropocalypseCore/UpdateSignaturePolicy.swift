import Foundation

/// What the Mac updater observed about one app bundle. The observation is filled in by
/// `codesign` and `spctl`; this type is only the facts the policy decision needs.
public struct CodeSignatureObservation: Equatable {
    public var signatureValid: Bool
    public var developerID: Bool
    public var teamID: String?
    /// `spctl --assess --type execute` accepted the bundle (Gatekeeper, which on macOS 13+
    /// means a notarized Developer ID app).
    public var gatekeeperAccepted: Bool

    public init(signatureValid: Bool, developerID: Bool, teamID: String?, gatekeeperAccepted: Bool) {
        self.signatureValid = signatureValid
        self.developerID = developerID
        self.teamID = teamID
        self.gatekeeperAccepted = gatekeeperAccepted
    }
}

public struct UpdateSignatureFacts: Equatable {
    public var candidate: CodeSignatureObservation
    /// Team ID of the copy that is running. Nil or blank when this copy is unsigned or ad-hoc.
    public var runningTeamID: String?

    public init(candidate: CodeSignatureObservation, runningTeamID: String?) {
        self.candidate = candidate
        self.runningTeamID = runningTeamID
    }
}

public enum UpdateSignatureDecision: Equatable {
    case allow
    case refuse(String)
}

/// Install an update only when it is validly signed with a Developer ID from the same team as
/// the running app, and Gatekeeper accepts it. Anything else is refused.
public enum UpdateSignaturePolicy {
    public static func decide(_ facts: UpdateSignatureFacts) -> UpdateSignatureDecision {
        let running = normalize(facts.runningTeamID)
        guard !running.isEmpty else {
            return .refuse("This copy of Dropocalypse isn't signed with a Developer ID, so an update can't be checked against it. Use the download page.")
        }
        let candidate = facts.candidate
        guard candidate.signatureValid else {
            return .refuse("The downloaded app's code signature is not valid, so it wasn't installed.")
        }
        guard candidate.developerID else {
            return .refuse("The downloaded app isn't signed with a Developer ID, so it wasn't installed.")
        }
        let team = normalize(candidate.teamID)
        guard !team.isEmpty, team.caseInsensitiveCompare(running) == .orderedSame else {
            return .refuse("The downloaded app is signed by a different developer, so it wasn't installed.")
        }
        guard candidate.gatekeeperAccepted else {
            return .refuse("macOS Gatekeeper did not accept the downloaded app, so it wasn't installed. It may not be notarized yet. Try again later, or use the download page.")
        }
        return .allow
    }

    private static func normalize(_ teamID: String?) -> String {
        teamID?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    }
}
