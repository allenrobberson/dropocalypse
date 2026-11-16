import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// App-level state shared by the window, the settings panel and the renderer.
final class VisualizerModel: NSObject, ObservableObject {
    let settings = AppSettings()
    let capture = AudioCapture()
    let updates = UpdateChecker()

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

    override init() {
        super.init()
        hasCustomLogo = FileManager.default.fileExists(atPath: Self.savedLogoURL.path)
        logoName = UserDefaults.standard.string(forKey: "logoName")
        installKeyMonitor()
        startHideTimer()
        updates.presentPanel = { [weak self] in
            guard let self else { return }
            self.noteActivity(reveal: true)
            self.openPopover = "update"
        }
        updates.checkAutomatically(enabled: settings.checkForUpdates)
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

    // MARK: Full screen
    //
    // macOS's built-in full screen keeps a black band under the camera notch on newer MacBooks and
    // can leave the menu-bar strip unused. Instead we turn the window itself into a frameless window
    // the exact size of its screen and hide the menu bar and Dock, so the visuals cover every pixel.

    private weak var window: NSWindow?
    private var savedFrame: NSRect?
    private var savedStyle: NSWindow.StyleMask = []
    private var savedMinSize = NSSize(width: 640, height: 400)
    private var savedMaxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
    private var savedMovable = true
    private let zoomTarget = ButtonTarget()

    /// Called once the Metal view is inside its window.
    func attachWindow(_ window: NSWindow?) {
        guard let window, self.window !== window else { return }
        self.window = window
        // Route every full-screen path (green button, F key, toolbar, menu) through our own mode.
        window.collectionBehavior.remove(.fullScreenPrimary)
        window.collectionBehavior.insert(.fullScreenNone)
        hookZoomButton()
    }

    private func hookZoomButton() {
        zoomTarget.action = { [weak self] in self?.toggleFullScreen() }
        if let zoom = window?.standardWindowButton(.zoomButton) {
            zoom.target = zoomTarget
            zoom.action = #selector(ButtonTarget.fire)
        }
    }

    private var targetWindow: NSWindow? {
        window ?? NSApp.mainWindow ?? NSApp.windows.first { $0.isVisible && $0.canBecomeMain }
    }

    func toggleFullScreen() {
        isFullScreen ? exitFullScreen() : enterFullScreen()
    }

    func enterFullScreen() {
        guard !isFullScreen, let window = targetWindow else { return }
        if window.styleMask.contains(.fullScreen) { window.toggleFullScreen(nil) } // leave any system full screen first
        guard let screen = window.screen ?? NSScreen.main else { return }
        savedFrame = window.frame
        savedStyle = window.styleMask
        savedMinSize = window.contentMinSize
        savedMaxSize = window.contentMaxSize
        savedMovable = window.isMovable

        NSApp.presentationOptions = [.hideDock, .hideMenuBar]   // fully hidden, no slide-down
        // .resizable (without .titled) keeps the window able to take keyboard focus.
        window.styleMask = [.resizable, .fullSizeContentView]
        window.isMovable = false
        window.contentMinSize = screen.frame.size
        window.contentMaxSize = screen.frame.size
        window.setFrame(screen.frame, display: true)
        window.makeKeyAndOrderFront(nil)
        isFullScreen = true
        NotificationCenter.default.addObserver(self, selector: #selector(screensChanged),
                                               name: NSApplication.didChangeScreenParametersNotification, object: nil)
    }

    func exitFullScreen() {
        guard isFullScreen, let window = targetWindow else { return }
        NotificationCenter.default.removeObserver(self, name: NSApplication.didChangeScreenParametersNotification, object: nil)
        isFullScreen = false
        window.styleMask = savedStyle
        window.contentMinSize = savedMinSize
        window.contentMaxSize = savedMaxSize
        window.isMovable = savedMovable
        if let frame = savedFrame { window.setFrame(frame, display: true) }
        NSApp.presentationOptions = []
        window.makeKeyAndOrderFront(nil)
        hookZoomButton()   // the title-bar buttons are rebuilt when the frame comes back
    }

    /// Resolution changed or a display was plugged in or out: re-fit to the screen we're on.
    @objc private func screensChanged() {
        guard isFullScreen, let window = targetWindow, let screen = window.screen ?? NSScreen.main else { return }
        window.contentMinSize = screen.frame.size
        window.contentMaxSize = screen.frame.size
        window.setFrame(screen.frame, display: true)
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
                if self.isFullScreen && self.openPopover == nil {
                    self.exitFullScreen()
                    return nil
                }
                return event
            default: break
            }
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "f": self.toggleFullScreen(); return nil
            case "s", "h", "t": self.toggleToolbar(); return nil
            case "l": self.chooseLogo(); return nil
            case "a": self.settings.autoCycle.toggle(); return nil
            case "g": self.settings.showLogo.toggle(); return nil
            default: return event
            }
        }
    }
}

/// Lets an AppKit button call a Swift closure.
final class ButtonTarget: NSObject {
    var action: (() -> Void)?
    @objc func fire() { action?() }
}
