import Foundation

/// Which style auto-cycle shows next. Names in `skipped` are the ones unchecked in the style list.
/// If every style is skipped, auto-cycle still advances by one so the picture keeps changing.
public enum SceneCycle {
    public static func nextIndex(after index: Int, names: [String], skipped: Set<String>) -> Int {
        let n = names.count
        guard n > 0 else { return 0 }
        for k in 1...n {
            let candidate = (index + k) % n
            if !skipped.contains(names[candidate]) { return candidate }
        }
        return (index + 1) % n
    }
}
