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
struct DropocalypseApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = VisualizerModel()

    var body: some Scene {
        WindowGroup("Dropocalypse") {
            ContentView(model: model)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 720)
        .commands {
            CommandGroup(after: .appInfo) {
                if UpdateChecker.isEnabled {
                    Button("Check for Updates…") { model.updates.check(userInitiated: true) }
                }
            }
            CommandGroup(replacing: .newItem) {
                Button("Add Logo…") { model.chooseLogo() }
                    .keyboardShortcut("o")
                Button("Add Text") { model.addTextItem() }
                Button("Remove All Logos") { model.clearLogo() }
            }
            CommandMenu("Visualizer") {
                Button("Next Style") { model.nextScene(1) }
                    .keyboardShortcut(.rightArrow, modifiers: [.command])
                Button("Previous Style") { model.nextScene(-1) }
                    .keyboardShortcut(.leftArrow, modifiers: [.command])
                Divider()
                Button("Full Screen") { model.toggleFullScreen() }
                    .keyboardShortcut("f", modifiers: [.command, .control])
                Button("Show/Hide Toolbar") { model.toggleToolbar() }
                    .keyboardShortcut(",", modifiers: [.command])
                Button(model.controlsDetached ? "Attach Controls" : "Detach Controls") { model.toggleControlsDetached() }
                    .keyboardShortcut("d", modifiers: [.command, .shift])
                Divider()
                Button(model.isRecording ? "Stop Recording" : "Start Recording") { model.toggleRecording() }
                    .keyboardShortcut("r", modifiers: [.command, .shift])
                Divider()
                Button("Restart Audio") { model.startAudio() }
            }
        }
    }
}
