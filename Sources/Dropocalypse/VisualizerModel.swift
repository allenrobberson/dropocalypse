import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// App-level state shared by the window, the settings panel and the renderer.
final class VisualizerModel: ObservableObject {
    let settings = AppSettings()
    let capture = AudioCapture()

    // Top toolbar auto-hide.
    @Published private(set) var toolbarVisible = true
    @Published var openPopover: String?          // a toolbar drop-down is open → stay visible
    @Published var pointerOverToolbar = false
    @Published private(set) var isFullScreen = false
    private var lastActivity = Date()
    private var hideTimer: Timer?
    static let hideDelay: TimeInterval = 2.5
    static let revealZone: CGFloat = 90          // points from the top edge that reveal the bar
    @Published var currentSceneName = VisualScene.builtIn[0].name
    @Published var errorMessage: String?
    @Published private(set) var hasCustomLogo = false
    @Published private(set) var logoName: String?

    weak var renderer: Renderer? {
        didSet { renderer?.setLogo(savedLogo()) }
    }

    private var keyMonitor: Any?

    init() {
        hasCustomLogo = FileManager.default.fileExists(atPath: Self.savedLogoURL.path)
        logoName = UserDefaults.standard.string(forKey: "logoName")
        installKeyMonitor()
        startHideTimer()
        observeFullScreen()
    }

    // MARK: Toolbar auto-hide

    /// Call on any user activity. `nearTop` reveals the toolbar; other activity only keeps it alive.
    func noteActivity(reveal: Bool) {
        lastActivity = Date()
        if reveal && !toolbarVisible {
            withAnimation(.easeOut(duration: 0.18)) { toolbarVisible = true }
        }
    }

    func pointerMoved(toY y: CGFloat) {
        noteActivity(reveal: y < Self.revealZone)
    }

    func toggleToolbar() {
        lastActivity = Date()
        withAnimation(.easeOut(duration: 0.18)) { toolbarVisible.toggle() }
    }

    private func startHideTimer() {
        hideTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self else { return }
            let pinned = self.openPopover != nil || self.pointerOverToolbar
            if pinned { self.lastActivity = Date(); return }
            if self.toolbarVisible, Date().timeIntervalSince(self.lastActivity) > Self.hideDelay {
                withAnimation(.easeIn(duration: 0.3)) { self.toolbarVisible = false }
                NSCursor.setHiddenUntilMouseMoves(true)
            }
        }
    }

    private func observeFullScreen() {
        let nc = NotificationCenter.default
        nc.addObserver(forName: NSWindow.didEnterFullScreenNotification, object: nil, queue: .main) { [weak self] _ in
            self?.isFullScreen = true
            // Keep the system menu bar from sliding over our toolbar at the top edge.
            NSApp.presentationOptions = [.fullScreen, .hideMenuBar, .hideDock]
        }
        nc.addObserver(forName: NSWindow.didExitFullScreenNotification, object: nil, queue: .main) { [weak self] _ in
            self?.isFullScreen = false
            NSApp.presentationOptions = []
        }
    }

    func startAudio() {
        capture.start(deviceUID: settings.inputDeviceUID.isEmpty ? nil : settings.inputDeviceUID)
    }

    func reportError(_ message: String) {
        DispatchQueue.main.async { self.errorMessage = message }
    }

    // MARK: Scenes

    func nextScene(_ delta: Int = 1) {
        let n = VisualScene.builtIn.count
        settings.sceneIndex = ((settings.sceneIndex + delta) % n + n) % n
    }

    // MARK: Logo

    static var supportDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let dir = base.appendingPathComponent("Dropocalypse", isDirectory: true)
        let fm = FileManager.default
        if !fm.fileExists(atPath: dir.path) {
            // Carry over a logo saved by the app's earlier name ("Logo Visualizer").
            let old = base.appendingPathComponent("LogoVisualizer", isDirectory: true)
            if fm.fileExists(atPath: old.path) { try? fm.copyItem(at: old, to: dir) }
        }
        try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static var savedLogoURL: URL { supportDirectory.appendingPathComponent("logo.png") }

    private func savedLogo() -> NSImage? {
        guard hasCustomLogo else { return nil }
        return NSImage(contentsOf: Self.savedLogoURL)
    }

    func chooseLogo() {
        let panel = NSOpenPanel()
        panel.title = "Choose your logo"
        panel.message = "PNG with a transparent background works best."
        panel.allowedContentTypes = [.png, .jpeg, .tiff, .heic, .gif, .bmp, .pdf, .svg, .image]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            loadLogo(from: url)
        }
    }

    func loadLogo(from url: URL) {
        guard let image = NSImage(contentsOf: url) else {
            errorMessage = "Couldn't open \(url.lastPathComponent) as an image."
            return
        }
        setLogo(image, name: url.lastPathComponent)
    }

    func setLogo(_ image: NSImage, name: String?) {
        // Save a PNG copy so the logo is still there next launch.
        if let tiff = image.tiffRepresentation,
           let rep = NSBitmapImageRep(data: tiff),
           let png = rep.representation(using: .png, properties: [:]) {
            try? png.write(to: Self.savedLogoURL)
        }
        hasCustomLogo = true
        logoName = name
        UserDefaults.standard.set(name, forKey: "logoName")
        renderer?.setLogo(image)
        settings.showLogo = true
        errorMessage = nil
    }

    func clearLogo() {
        try? FileManager.default.removeItem(at: Self.savedLogoURL)
        hasCustomLogo = false
        logoName = nil
        UserDefaults.standard.removeObject(forKey: "logoName")
        renderer?.setLogo(nil)
    }

    /// Handles files or images dragged onto the window.
    func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }
        if provider.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                var url: URL?
                if let data = item as? Data { url = URL(dataRepresentation: data, relativeTo: nil) }
                else if let u = item as? URL { url = u }
                guard let url else { return }
                DispatchQueue.main.async { self.loadLogo(from: url) }
            }
            return true
        }
        if provider.hasItemConformingToTypeIdentifier(UTType.image.identifier) {
            provider.loadDataRepresentation(forTypeIdentifier: UTType.image.identifier) { data, _ in
                guard let data, let image = NSImage(data: data) else { return }
                DispatchQueue.main.async { self.setLogo(image, name: "Dropped image") }
            }
            return true
        }
        return false
    }

    // MARK: Keyboard

    private func installKeyMonitor() {
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .leftMouseDown, .rightMouseDown, .scrollWheel]) { [weak self] event in
            guard let self else { return event }
            if event.type != .keyDown {
                // Clicks and scrolls count as activity and bring the toolbar back.
                self.noteActivity(reveal: true)
                return event
            }
            // Leave shortcuts that use ⌘/⌃/⌥ to the menus.
            if !event.modifierFlags.intersection([.command, .control, .option]).isEmpty { return event }
            switch event.keyCode {
            case 124: self.nextScene(1); return nil   // →
            case 123: self.nextScene(-1); return nil  // ←
            case 53:                                   // esc
                if self.toolbarVisible && self.openPopover == nil {
                    self.lastActivity = .distantPast
                    return nil
                }
                return event
            default: break
            }
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "f": NSApp.keyWindow?.toggleFullScreen(nil); return nil
            case "s", "h", "t": self.toggleToolbar(); return nil
            case "l": self.chooseLogo(); return nil
            case "a": self.settings.autoCycle.toggle(); return nil
            case "g": self.settings.showLogo.toggle(); return nil
            default: return event
            }
        }
    }
}
