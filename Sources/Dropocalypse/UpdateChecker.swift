import AppKit
import Foundation

/// Asks GitHub for the latest release (at most once a day) and, if it's newer than this copy,
/// shows an "Update available" button in the toolbar that opens the download page.
/// Nothing is downloaded or installed automatically, and no information about the user is sent:
/// it's one plain request for the public latest-release info.
final class UpdateChecker: ObservableObject {
    struct Release: Equatable {
        let version: String
        let url: URL
    }

    @Published private(set) var available: Release?
    @Published private(set) var checking = false

    static let releasesPage = URL(string: "https://github.com/allenrobberson/dropocalypse/releases/latest")!
    private static let api = URL(string: "https://api.github.com/repos/allenrobberson/dropocalypse/releases/latest")!

    let currentVersion: String = {
        (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String) ?? "0.0.0-dev"
    }()

    /// Development builds (`swift run`, CI test builds) don't nag.
    var isDevBuild: Bool { currentVersion.contains("dev") || currentVersion == "0.0.0" }

    private let defaults = UserDefaults.standard
    private let lastCheckKey = "updateLastCheck"
    private let latestKey = "updateLatestVersion"
    private let latestURLKey = "updateLatestURL"

    init() {
        // Show a previously found update right away, without waiting for the network.
        if let v = defaults.string(forKey: latestKey), let u = defaults.string(forKey: latestURLKey),
           let url = URL(string: u), Self.isNewer(v, than: currentVersion) {
            available = Release(version: v, url: url)
        }
    }

    /// Called at launch: checks if enabled and the last check was more than 20 hours ago.
    func checkAutomatically(enabled: Bool) {
        guard enabled, !isDevBuild else { return }
        let last = defaults.double(forKey: lastCheckKey)
        guard Date().timeIntervalSince1970 - last > 20 * 3600 else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in self?.check(userInitiated: false) }
    }

    /// "Check for Updates…": always checks, and reports the result in an alert.
    func check(userInitiated: Bool) {
        guard !checking else { return }
        checking = true
        var request = URLRequest(url: Self.api, timeoutInterval: 15)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("Dropocalypse/\(currentVersion)", forHTTPHeaderField: "User-Agent")
        URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            var found: Release?
            var failure: String?
            if let data, (response as? HTTPURLResponse)?.statusCode == 200,
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let tag = json["tag_name"] as? String {
                let version = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
                let url = (json["html_url"] as? String).flatMap(URL.init(string:)) ?? Self.releasesPage
                found = Release(version: version, url: url)
            } else {
                failure = error?.localizedDescription ?? "GitHub didn't answer. Try again later."
            }
            DispatchQueue.main.async {
                guard let self else { return }
                self.checking = false
                if let found {
                    self.defaults.set(Date().timeIntervalSince1970, forKey: self.lastCheckKey)
                    self.defaults.set(found.version, forKey: self.latestKey)
                    self.defaults.set(found.url.absoluteString, forKey: self.latestURLKey)
                    self.available = Self.isNewer(found.version, than: self.currentVersion) ? found : nil
                }
                if userInitiated { self.report(found: found, failure: failure) }
            }
        }.resume()
    }

    func openDownloadPage() {
        NSWorkspace.shared.open(available?.url ?? Self.releasesPage)
    }

    private func report(found: Release?, failure: String?) {
        let alert = NSAlert()
        if let available {
            alert.messageText = "Dropocalypse \(available.version) is available"
            alert.informativeText = "You have version \(currentVersion). Download the new version from GitHub and replace this one; your logo and settings are kept."
            alert.addButton(withTitle: "Download")
            alert.addButton(withTitle: "Later")
            if alert.runModal() == .alertFirstButtonReturn { openDownloadPage() }
        } else if found != nil {
            alert.messageText = "You're up to date"
            alert.informativeText = "Dropocalypse \(currentVersion) is the latest version."
            alert.runModal()
        } else {
            alert.messageText = "Couldn't check for updates"
            alert.informativeText = failure ?? "Check your internet connection and try again."
            alert.addButton(withTitle: "OK")
            alert.addButton(withTitle: "Open Download Page")
            if alert.runModal() == .alertSecondButtonReturn { NSWorkspace.shared.open(Self.releasesPage) }
        }
    }

    /// Compares dotted versions numerically: 0.10.0 > 0.9.2. Anything after "-" is ignored.
    static func isNewer(_ a: String, than b: String) -> Bool {
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
