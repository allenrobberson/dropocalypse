import AppKit
import DropocalypseCore
import SwiftUI

/// The "What's New" window: opens once, the first time the app starts after an update, and any
/// time from the Updates menu. The notes come from docs/whats-new.md (see WhatsNewNotes.swift).
final class WhatsNew: ObservableObject {
    typealias Entry = (version: String, items: [String])

    @Published var isPresented = false
    @Published private(set) var shown: [Entry] = []

    let currentVersion: String
    static let changelog = URL(string: "https://github.com/allenrobberson/dropocalypse/wiki/Changelog")!

    private let defaults = UserDefaults.standard
    private static let lastSeenKey = "whatsNewLastSeen"

    /// Runs while the model is created, before any setting is written this launch, so "were there
    /// settings already?" tells an update (show the notes) from a first install (don't).
    init() {
        currentVersion = (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String) ?? "0.0.0-dev"
        let lastSeen = defaults.string(forKey: Self.lastSeenKey)
        let existingInstall = ["sceneIndex", "autoCycle", "cycleSkip", "checkForUpdates", "sensitivity",
                               "updateLastCheck", "updateLatestVersion", "pictureOrientation", "recordQuality"]
            .contains { defaults.object(forKey: $0) != nil }
        let isDev = currentVersion.contains("dev") || currentVersion == "0.0.0"

        if WhatsNewLogic.shouldShow(lastSeen: lastSeen, current: currentVersion, existingInstall: existingInstall) {
            shown = Self.pick(WhatsNewLogic.entries(versions: WhatsNewNotes.entries.map { $0.version },
                                                    lastSeen: lastSeen, current: currentVersion))
            // Give the window a moment to appear first.
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) { [weak self] in self?.isPresented = true }
        } else if lastSeen == nil && !isDev {
            // First install: start from this version, so the next update shows its notes.
            defaults.set(currentVersion, forKey: Self.lastSeenKey)
        }
    }

    /// From the menus: the latest few versions up to this one.
    func show() {
        shown = Self.pick(WhatsNewLogic.entries(versions: WhatsNewNotes.entries.map { $0.version },
                                                lastSeen: "0", current: currentVersion, maxEntries: 3))
        isPresented = true
    }

    /// "Got it" (or closing the sheet): don't show these notes again.
    func markSeen() {
        let isDev = currentVersion.contains("dev") || currentVersion == "0.0.0"
        if !isDev { defaults.set(currentVersion, forKey: Self.lastSeenKey) }
    }

    private static func pick(_ indices: [Int]) -> [Entry] { indices.map { WhatsNewNotes.entries[$0] } }
}

struct WhatsNewView: View {
    @ObservedObject var whatsNew: WhatsNew

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(spacing: 14) {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .frame(width: 52, height: 52)
                VStack(alignment: .leading, spacing: 2) {
                    Text("What's new in Dropocalypse").font(.title2.bold())
                    Text("Version \(whatsNew.currentVersion)").foregroundStyle(.secondary)
                }
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    if whatsNew.shown.isEmpty {
                        Text("Dropocalypse has been updated to version \(whatsNew.currentVersion). The full list of changes is in the changelog.")
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    ForEach(Array(whatsNew.shown.enumerated()), id: \.offset) { _, entry in
                        VStack(alignment: .leading, spacing: 10) {
                            if whatsNew.shown.count > 1 {
                                Text("Version \(entry.version)").font(.headline).foregroundStyle(.pink)
                            }
                            ForEach(Array(entry.items.enumerated()), id: \.offset) { _, item in
                                HStack(alignment: .firstTextBaseline, spacing: 8) {
                                    Text("•").foregroundStyle(.pink)
                                    Text((try? AttributedString(markdown: item)) ?? AttributedString(item))
                                        .fixedSize(horizontal: false, vertical: true)
                                }
                            }
                        }
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxHeight: 380)

            HStack {
                Button("Full changelog") { NSWorkspace.shared.open(WhatsNew.changelog) }
                Spacer()
                Button("Got it") {
                    whatsNew.markSeen()
                    whatsNew.isPresented = false
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(24)
        .frame(width: 500)
        .fixedSize(horizontal: false, vertical: true)
    }
}
