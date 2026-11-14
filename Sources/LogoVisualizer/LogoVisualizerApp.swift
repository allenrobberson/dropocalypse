import SwiftUI
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Needed when launched as a bare executable (e.g. `swift run`); harmless inside the .app.
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        DispatchQueue.main.async {
            for window in NSApp.windows {
                window.isMovableByWindowBackground = true
                window.backgroundColor = .black
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct LogoVisualizerApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = VisualizerModel()

    var body: some Scene {
        WindowGroup("Logo Visualizer") {
            ContentView(model: model)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 720)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Choose Logo…") { model.chooseLogo() }
                    .keyboardShortcut("o")
                Button("Remove Logo") { model.clearLogo() }
            }
            CommandMenu("Visualizer") {
                Button("Next Style") { model.nextScene(1) }
                    .keyboardShortcut(.rightArrow, modifiers: [.command])
                Button("Previous Style") { model.nextScene(-1) }
                    .keyboardShortcut(.leftArrow, modifiers: [.command])
                Divider()
                Button("Show/Hide Toolbar") { model.toggleToolbar() }
                    .keyboardShortcut(",", modifiers: [.command])
                Button("Restart Audio") { model.startAudio() }
            }
        }
    }
}
