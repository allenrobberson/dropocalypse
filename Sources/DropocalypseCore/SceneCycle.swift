import Foundation

/// Which style auto-cycle shows next. Names in `skipped` are the ones unchecked in the style list.
/// If every style is skipped, auto-cycle still advances by one so the picture keeps changing.
public enum SceneCycle {
    /// Styles at `cycleable` and above (MilkDrop presets from the user's own folder, when they
    /// aren't included) are never chosen; nil = every style can be.
    public static func nextIndex(after index: Int, names: [String], skipped: Set<String>,
                                 cycleable: Int? = nil) -> Int {
        let n = names.count
        guard n > 0 else { return 0 }
        let limit = min(n, max(1, cycleable ?? n))
        let index = ((index % n) + n) % n
        for k in 1...n {
            let candidate = (index + k) % n
            if candidate < limit, !skipped.contains(names[candidate]) { return candidate }
        }
        let next = (index + 1) % n
        return next < limit ? next : 0
    }
}
