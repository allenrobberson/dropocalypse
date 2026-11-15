import SwiftUI

/// User-adjustable settings, persisted with UserDefaults.
final class AppSettings: ObservableObject {
    // Audio
    @AppStorage("inputDeviceUID") var inputDeviceUID: String = ""
    @AppStorage("sensitivity") var sensitivity: Double = 1.0
    @AppStorage("beatThreshold") var beatThreshold: Double = 1.35

    // Scenes
    @AppStorage("sceneIndex") var sceneIndex: Int = 0
    @AppStorage("autoCycle") var autoCycle: Bool = true
    @AppStorage("cycleSeconds") var cycleSeconds: Double = 30
    @AppStorage("changeSceneOnDrop") var changeSceneOnDrop: Bool = false
    /// Names of styles left out of auto-cycle (unchecked in the style list). Stored as names, so
    /// styles added in later versions are included automatically.
    @AppStorage("cycleSkip") var cycleSkip: String = ""
    /// Check GitHub for a newer version once a day and show "Update available" in the toolbar.
    @AppStorage("checkForUpdates") var checkForUpdates: Bool = true

    private var skipped: Set<String> {
        Set(cycleSkip.split(separator: "\n").map(String.init))
    }

    func isInCycle(_ name: String) -> Bool { !skipped.contains(name) }

    func setInCycle(_ name: String, _ on: Bool) {
        var s = skipped
        if on { s.remove(name) } else { s.insert(name) }
        cycleSkip = s.sorted().joined(separator: "\n")
    }

    func setAllInCycle(_ on: Bool) {
        cycleSkip = on ? "" : VisualScene.builtIn.map(\.name).joined(separator: "\n")
    }

    /// The next checked style after `index`. If nothing is checked, cycles through all of them.
    func nextCycleIndex(after index: Int) -> Int {
        let scenes = VisualScene.builtIn
        let n = scenes.count
        let skip = skipped
        for k in 1...n {
            let candidate = (index + k) % n
            if !skip.contains(scenes[candidate].name) { return candidate }
        }
        return (index + 1) % n
    }

    // Logo
    @AppStorage("showLogo") var showLogo: Bool = true
    @AppStorage("logoSize") var logoSize: Double = 0.32
    @AppStorage("pulse") var pulse: Double = 0.7
    @AppStorage("squash") var squash: Double = 0.6
    @AppStorage("bounce") var bounce: Double = 0.5
    @AppStorage("slide") var slide: Double = 0.4
    @AppStorage("wobble") var wobble: Double = 0.35
    @AppStorage("glow") var glow: Double = 0.6
    @AppStorage("logoTrails") var logoTrails: Bool = false
    @AppStorage("logoTint") var logoTint: Double = 0.0

    // Look
    @AppStorage("beatFlash") var beatFlash: Double = 0.4
    @AppStorage("brightness") var brightness: Double = 1.0
    @AppStorage("showStatus") var showStatus: Bool = true

    func resetMotion() {
        logoSize = 0.32; pulse = 0.7; squash = 0.6; bounce = 0.5; slide = 0.4
        wobble = 0.35; glow = 0.6; logoTrails = false; logoTint = 0
        beatFlash = 0.4; brightness = 1.0
    }
}
