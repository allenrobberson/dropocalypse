import Foundation

/// When to open the What's New window and which notes go in it. Same rules as
/// shouldShowWhatsNew / whatsNewEntries in windows/src/core_logic.h.
public enum WhatsNewLogic {
    /// `lastSeen` is the version whose notes were last shown (nil when the settings predate the
    /// window). No `lastSeen` on an existing install means "just updated from an older version";
    /// on a brand-new install it means "first run". Development builds never show it.
    public static func shouldShow(lastSeen: String?, current: String, existingInstall: Bool) -> Bool {
        if current.contains("dev") || current == "0.0.0" { return false }
        guard let lastSeen, !lastSeen.isEmpty else { return existingInstall }
        return VersionCompare.isNewer(current, than: lastSeen)
    }

    /// Indices into `versions` (newest first): every version after `lastSeen` up to `current`,
    /// at most `maxEntries`. With no `lastSeen`, only `current`'s notes.
    public static func entries(versions: [String], lastSeen: String?, current: String, maxEntries: Int = 4) -> [Int] {
        var out: [Int] = []
        for (i, v) in versions.enumerated() where out.count < maxEntries {
            if VersionCompare.isNewer(v, than: current) { continue }
            let wanted: Bool
            if let lastSeen, !lastSeen.isEmpty {
                wanted = VersionCompare.isNewer(v, than: lastSeen)
            } else {
                wanted = !VersionCompare.isNewer(current, than: v)
            }
            if wanted { out.append(i) }
        }
        return out
    }
}
