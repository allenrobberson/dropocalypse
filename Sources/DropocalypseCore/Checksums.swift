import Foundation

/// SHA-256 listings in the `sha256sum` format published with each release: "<hash>  <file>"
/// or "<hash> *<file>" (binary mode).
public enum Checksums {
    public static func sha256(for file: String, in listing: String) -> String? {
        for line in listing.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: " ", omittingEmptySubsequences: true)
            guard parts.count >= 2 else { continue }
            var name = String(parts[parts.count - 1])
            if name.hasPrefix("*") { name.removeFirst() }
            if name == file { return String(parts[0]) }
        }
        return nil
    }
}
