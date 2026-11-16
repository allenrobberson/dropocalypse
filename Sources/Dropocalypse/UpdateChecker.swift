import AppKit
import CryptoKit
import Foundation

/// Asks GitHub for the latest release (at most once a day) and, if it's newer than this copy,
/// shows an "Update available" button in the toolbar. From there the user can choose
/// **Update now**: the new version is downloaded, checked against the SHA-256 checksums published
/// with the release, installed over this copy, and reopened. Nothing happens without that click,
/// and no information about the user is sent.
final class UpdateChecker: ObservableObject {
    struct Release: Equatable {
        let version: String
        let url: URL
    }

    @Published private(set) var available: Release?
    @Published private(set) var checking = false
    /// "Ignore for 24 hours": the toolbar button stays hidden until this time.
    @Published private(set) var snoozedUntil: Date?

    enum InstallPhase: Equatable {
        case idle
        case downloading(Double)     // 0...1
        case verifying
        case installing
        case relaunching
        case failed(String)
    }
    @Published private(set) var phase: InstallPhase = .idle

    /// Shows the toolbar's update panel (set by the model), used by the menu's "Update now".
    var presentPanel: (() -> Void)?

    /// The Mac App Store edition is updated by the App Store, so it never checks GitHub
    /// (built with `-Xswiftc -DAPP_STORE`).
    #if APP_STORE
    static let isEnabled = false
    #else
    static let isEnabled = true
    #endif

    /// Whether the toolbar should show the "Update" button right now.
    var showBadge: Bool {
        guard Self.isEnabled, available != nil else { return false }
        if let until = snoozedUntil, Date() < until { return false }
        return true
    }

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
    private let snoozeKey = "updateSnoozeUntil"

    init() {
        let until = defaults.double(forKey: snoozeKey)
        if until > Date().timeIntervalSince1970 { snoozedUntil = Date(timeIntervalSince1970: until) }
        // Show a previously found update right away, without waiting for the network.
        if let v = defaults.string(forKey: latestKey), let u = defaults.string(forKey: latestURLKey),
           let url = URL(string: u), Self.isNewer(v, than: currentVersion) {
            available = Release(version: v, url: url)
        }
    }

    /// Called at launch: checks if enabled and the last check was more than 20 hours ago.
    func checkAutomatically(enabled: Bool) {
        guard Self.isEnabled, enabled, !isDevBuild else { return }
        let last = defaults.double(forKey: lastCheckKey)
        guard Date().timeIntervalSince1970 - last > 20 * 3600 else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in self?.check(userInitiated: false) }
    }

    /// "Check for Updates…": always checks, and reports the result in an alert.
    func check(userInitiated: Bool) {
        guard Self.isEnabled, !checking else { return }
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

    /// Hide the toolbar button for a day. A manual "Check now" still reports the update.
    func ignoreForADay() {
        let until = Date().addingTimeInterval(24 * 3600)
        snoozedUntil = until
        defaults.set(until.timeIntervalSince1970, forKey: snoozeKey)
        // Make the button come back on its own when the day is up.
        DispatchQueue.main.asyncAfter(deadline: .now() + 24 * 3600 + 1) { [weak self] in
            guard let self, let s = self.snoozedUntil, Date() >= s else { return }
            self.snoozedUntil = nil
        }
    }

    // MARK: - Update now: download, verify, install, relaunch

    var isBusy: Bool {
        switch phase {
        case .downloading, .verifying, .installing, .relaunching: return true
        default: return false
        }
    }

    /// Why "Update now" can't work for this copy, or nil if it can.
    var installBlocker: String? {
        if isDevBuild { return "This is a development build. Use the download page instead." }
        let app = Bundle.main.bundleURL
        if app.pathExtension != "app" { return "Use the download page to update this copy." }
        if app.path.contains("/AppTranslocation/") || app.path.hasPrefix("/Volumes/") {
            return "Move Dropocalypse into your Applications folder first, open it from there, then update."
        }
        let fm = FileManager.default
        if !fm.isWritableFile(atPath: app.deletingLastPathComponent().path) || !fm.isWritableFile(atPath: app.path) {
            return "Dropocalypse can't replace itself in this folder. Use the download page, or move the app to your Applications folder."
        }
        return nil
    }

    func installUpdate() {
        guard Self.isEnabled else { return }
        guard !isBusy else { return }
        if let blocker = installBlocker {
            phase = .failed(blocker)
            return
        }
        phase = .downloading(0)
        Task { [weak self] in
            guard let self else { return }
            do {
                try await self.performInstall()
            } catch {
                let message = error.localizedDescription
                await MainActor.run { self.phase = .failed(message) }
            }
        }
    }

    func resetInstallState() {
        if case .failed = phase { phase = .idle }
    }

    private struct UpdateError: LocalizedError {
        let errorDescription: String?
        init(_ message: String) { errorDescription = message }
    }

    private func performInstall() async throws {
        // 1. Latest release info, with its asset URLs.
        var request = URLRequest(url: Self.api, timeoutInterval: 20)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("Dropocalypse/\(currentVersion)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await URLSession.shared.data(for: request)
        guard (response as? HTTPURLResponse)?.statusCode == 200,
              let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let tag = json["tag_name"] as? String,
              let assets = json["assets"] as? [[String: Any]] else {
            throw UpdateError("Couldn't read the latest release from GitHub. Try again later.")
        }
        let version = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
        guard Self.isNewer(version, than: currentVersion) else {
            throw UpdateError("You already have the latest version.")
        }
        func asset(_ match: (String) -> Bool) -> (name: String, url: URL)? {
            for a in assets {
                if let name = a["name"] as? String, match(name),
                   let s = a["browser_download_url"] as? String, let url = URL(string: s) { return (name, url) }
            }
            return nil
        }
        guard let zip = asset({ $0.hasSuffix("-macOS.zip") }) else {
            throw UpdateError("This release doesn't include a Mac zip. Use the download page.")
        }
        guard let sums = asset({ $0 == "SHA256SUMS.txt" }) else {
            throw UpdateError("This release has no checksums, so it can't be installed automatically. Use the download page.")
        }

        // 2. Checksums, then the app itself (with progress).
        let (sumsData, sumsResponse) = try await URLSession.shared.data(from: sums.url)
        guard (sumsResponse as? HTTPURLResponse)?.statusCode == 200,
              let sumsText = String(data: sumsData, encoding: .utf8),
              let expected = Self.checksum(for: zip.name, in: sumsText) else {
            throw UpdateError("Couldn't read the release checksums.")
        }
        let work = FileManager.default.temporaryDirectory
            .appendingPathComponent("DropocalypseUpdate-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        let zipFile = work.appendingPathComponent(zip.name)
        try await download(zip.url, to: zipFile)

        // 3. Verify.
        await MainActor.run { self.phase = .verifying }
        let digest = SHA256.hash(data: try Data(contentsOf: zipFile, options: .mappedIfSafe))
        let actual = digest.map { String(format: "%02x", $0) }.joined()
        guard actual == expected.lowercased() else {
            throw UpdateError("The download didn't match its checksum, so it wasn't installed. Try again.")
        }

        // 4. Unpack and check it's really Dropocalypse at the expected version.
        await MainActor.run { self.phase = .installing }
        let unpacked = work.appendingPathComponent("unpacked", isDirectory: true)
        try run("/usr/bin/ditto", ["-x", "-k", zipFile.path, unpacked.path])
        guard let newApp = try FileManager.default.contentsOfDirectory(at: unpacked, includingPropertiesForKeys: nil)
                .first(where: { $0.pathExtension == "app" }),
              let bundle = Bundle(url: newApp),
              bundle.bundleIdentifier == Bundle.main.bundleIdentifier,
              (bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String) == version else {
            throw UpdateError("The downloaded app didn't look right, so it wasn't installed.")
        }
        try? run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", newApp.path])

        // 5. A tiny helper swaps the apps once we've quit, then reopens the new one.
        //    If anything goes wrong it puts the old app back and reopens that.
        let script = work.appendingPathComponent("install.sh")
        try """
        #!/bin/sh
        PID="$1"; OLD="$2"; NEW="$3"
        while kill -0 "$PID" 2>/dev/null; do sleep 0.2; done
        BACKUP="$OLD.update-backup"
        rm -rf "$BACKUP"
        if mv "$OLD" "$BACKUP" && ditto "$NEW" "$OLD"; then
          rm -rf "$BACKUP"
        else
          rm -rf "$OLD"; mv "$BACKUP" "$OLD"
        fi
        xattr -dr com.apple.quarantine "$OLD" 2>/dev/null
        open "$OLD"
        rm -rf "$(dirname "$0")"
        """.write(to: script, atomically: true, encoding: .utf8)

        let helper = Process()
        helper.executableURL = URL(fileURLWithPath: "/bin/sh")
        helper.arguments = [script.path, String(getpid()), Bundle.main.bundleURL.path, newApp.path]
        try helper.run()

        await MainActor.run {
            self.phase = .relaunching
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { NSApp.terminate(nil) }
        }
    }

    private func run(_ tool: String, _ args: [String]) throws {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: tool)
        p.arguments = args
        try p.run()
        p.waitUntilExit()
        guard p.terminationStatus == 0 else { throw UpdateError("Unpacking the update failed.") }
    }

    private func download(_ url: URL, to destination: URL) async throws {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            let delegate = DownloadDelegate(destination: destination, progress: { [weak self] p in
                DispatchQueue.main.async { self?.phase = .downloading(p) }
            }, completion: { error in
                if let error { cont.resume(throwing: error) } else { cont.resume() }
            })
            let session = URLSession(configuration: .default, delegate: delegate, delegateQueue: nil)
            var request = URLRequest(url: url, timeoutInterval: 60)
            request.setValue("Dropocalypse/\(currentVersion)", forHTTPHeaderField: "User-Agent")
            session.downloadTask(with: request).resume()
            session.finishTasksAndInvalidate()
        }
    }

    /// Finds "<hash>  <file name>" in a sha256sum listing.
    static func checksum(for file: String, in listing: String) -> String? {
        for line in listing.split(whereSeparator: \.isNewline) {
            let parts = line.split(separator: " ", omittingEmptySubsequences: true)
            if parts.count >= 2, parts.last.map({ $0.hasPrefix("*") ? String($0.dropFirst()) : String($0) }) == file {
                return String(parts[0])
            }
        }
        return nil
    }

    func openDownloadPage() {
        NSWorkspace.shared.open(available?.url ?? Self.releasesPage)
    }

    private func report(found: Release?, failure: String?) {
        let alert = NSAlert()
        if let available {
            alert.messageText = "Dropocalypse \(available.version) is available"
            alert.informativeText = "You have version \(currentVersion). Update Now downloads it, installs it and reopens Dropocalypse. Your logo and settings are kept."
            alert.addButton(withTitle: "Update Now")
            alert.addButton(withTitle: "Later")
            alert.addButton(withTitle: "Download Page")
            switch alert.runModal() {
            case .alertFirstButtonReturn:
                presentPanel?()
                installUpdate()
            case .alertThirdButtonReturn:
                openDownloadPage()
            default: break
            }
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

/// Streams a download to disk and reports progress.
private final class DownloadDelegate: NSObject, URLSessionDownloadDelegate {
    let destination: URL
    let progress: (Double) -> Void
    let completion: (Error?) -> Void
    private var finished = false

    init(destination: URL, progress: @escaping (Double) -> Void, completion: @escaping (Error?) -> Void) {
        self.destination = destination
        self.progress = progress
        self.completion = completion
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        if totalBytesExpectedToWrite > 0 { progress(Double(totalBytesWritten) / Double(totalBytesExpectedToWrite)) }
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        let status = (downloadTask.response as? HTTPURLResponse)?.statusCode ?? 0
        do {
            guard status == 200 else { throw URLError(.badServerResponse) }
            try? FileManager.default.removeItem(at: destination)
            try FileManager.default.moveItem(at: location, to: destination)
            finish(nil)
        } catch {
            finish(error)
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        finish(error ?? (finished ? nil : URLError(.unknown)))
    }

    private func finish(_ error: Error?) {
        guard !finished else { return }
        finished = true
        completion(error)
    }
}
