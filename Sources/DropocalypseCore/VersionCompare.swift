import Foundation

/// Dotted numeric versions: 0.10.0 is newer than 0.9.2. A "-" suffix (1.2.0-dev) is ignored.
/// Equal versions are not newer.
public enum VersionCompare {
    public static func isNewer(_ a: String, than b: String) -> Bool {
        func parts(_ s: String) -> [Int] {
            s.split(separator: "-").first.map { $0.split(separator: ".").map { Int($0) ?? 0 } } ?? []
        }
        let x = parts(a), y = parts(b)
        for i in 0..<max(x.count, y.count) {
            let p = i < x.count ? x[i] : 0, q = i < y.count ? y[i] : 0
            if p != q { return p > q }
        }
        return false
    }
}
