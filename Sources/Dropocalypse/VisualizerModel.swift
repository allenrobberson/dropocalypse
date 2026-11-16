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
    /// The toolbar lives in its own window (so it can stay on the main screen while the visuals
    /// play full screen on another display).
    @Published private(set) var controlsDetached = false

    // Recording
    private(set) var recorder: Recorder?
    @Published private(set) var isRecording = false
    @Published private(set) var recordingElapsed: TimeInterval = 0
    @Published var recordMessage: String?
    @Published private(set) var lastRecordingURL: URL?
    private var recordTimer: Timer?
    private var recordFolderAccess: URL?
    private var controlsWindow: NSWindow?
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
        if UserDefaults.standard.bool(forKey: "controlsDetached") {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in self?.detachControls() }
        }
    }

    // MARK: Recording

    /// The real ~/Movies (inside the App Store sandbox, too).
    static var moviesFolder: URL {
        if let pw = getpwuid(getuid()), let home = pw.pointee.pw_dir {
            return URL(fileURLWithPath: String(cString: home)).appendingPathComponent("Movies", isDirectory: true)
        }
        return FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask).first!
    }

    /// The folder recordings are saved to (a folder the user picked, or Movies).
    var recordFolder: URL {
        if !settings.recordFolderBookmark.isEmpty {
            var stale = false
            if let url = try? URL(resolvingBookmarkData: settings.recordFolderBookmark, options: [.withSecurityScope],
                                  relativeTo: nil, bookmarkDataIsStale: &stale) {
                return url
            }
        }
        return Self.moviesFolder
    }

    var recordFolderName: String {
        let url = recordFolder
        return url.path.replacingOccurrences(of: NSHomeDirectory(), with: "~")
    }

    func chooseRecordFolder() {
        let panel = NSOpenPanel()
        panel.title = "Choose where to save recordings"
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.directoryURL = recordFolder
        panel.prompt = "Choose"
        if panel.runModal() == .OK, let url = panel.url,
           let data = try? url.bookmarkData(options: [.withSecurityScope], includingResourceValuesForKeys: nil,
                                             relativeTo: nil) {
            settings.recordFolderBookmark = data
            objectWillChange.send()
        }
    }

    func useMoviesFolder() {
        settings.recordFolderBookmark = Data()
        objectWillChange.send()
    }

    func toggleRecording() {
        isRecording ? stopRecording() : startRecording()
    }

    func startRecording() {
        guard !isRecording else { return }
        guard let device = renderer?.device else { return }
        let folder = recordFolder
        let scoped = folder.startAccessingSecurityScopedResource()
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd 'at' HH.mm.ss"
        let url = folder.appendingPathComponent("Dropocalypse \(f.string(from: Date())).mp4")
        let quality = Recorder.Quality(rawValue: settings.recordQuality) ?? .p1080
        let recorder = Recorder()
        do {
            try recorder.start(url: url, quality: quality, device: device)
        } catch {
            if scoped { folder.stopAccessingSecurityScopedResource() }
            recordMessage = "Couldn't start recording: \(error.localizedDescription)"
            return
        }
        recordFolderAccess = scoped ? folder : nil
        self.recorder = recorder
        RecordingAudioTap.shared.attach(recorder)
        isRecording = true
        recordMessage = nil
        recordingElapsed = 0
        recordTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self, let r = self.recorder else { return }
            self.recordingElapsed = r.elapsed
        }
    }

    func stopRecording() {
        guard isRecording, let recorder else { return }
        RecordingAudioTap.shared.attach(nil)
        recordTimer?.invalidate()
        recordTimer = nil
        isRecording = false
        recordMessage = "Finishing the video…"
        recorder.stop { [weak self] url, error in
            guard let self else { return }
            self.recorder = nil
            self.recordFolderAccess?.stopAccessingSecurityScopedResource()
            self.recordFolderAccess = nil
            if let url {
                self.lastRecordingURL = url
                self.recordMessage = "Saved \(url.lastPathComponent)"
            } else {
                self.recordMessage = "Recording failed: \(error?.localizedDescription ?? "unknown error")"
            }
            let message = self.recordMessage
            DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
                if self?.recordMessage == message { self?.recordMessage = nil }
            }
        }
    }

    func showLastRecording() {
        if let url = lastRecordingURL { NSWorkspace.shared.activateFileViewerSelecting([url]) }
    }

    func openRecordFolder() {
        let folder = recordFolder
        let scoped = folder.startAccessingSecurityScopedResource()
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        NSWorkspace.shared.open(folder)
        if scoped { folder.stopAccessingSecurityScopedResource() }
    }

    static func timeLabel(_ t: TimeInterval) -> String {
        let s = Int(t)
        return String(format: "%d:%02d", s / 60, s % 60)
    }

    // MARK: Detached controls window

    func toggleControlsDetached() {
        controlsDetached ? attachControls() : detachControls()
    }

    func detachControls() {
        if let w = controlsWindow { w.makeKeyAndOrderFront(nil); return }
        let view = TopToolbar(model: self, settings: settings, capture: capture, updates: updates, vertical: true)
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 370, height: 800),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "Dropocalypse Controls"
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: view)
        window.contentMinSize = NSSize(width: 350, height: 360)
        window.appearance = NSAppearance(named: .darkAqua)
        window.delegate = self
        // Start on the main screen (the one with the menu bar), near its right edge.
        if !window.setFrameUsingName("DropocalypseControls"), let screen = NSScreen.screens.first {
            let vf = screen.visibleFrame
            let h = min(800, vf.height - 40)
            window.setFrame(NSRect(x: vf.maxX - 370 - 24, y: vf.maxY - h - 24, width: 370, height: h), display: false)
        }
        window.setFrameAutosaveName("DropocalypseControls")
        controlsWindow = window
        openPopover = nil
        withAnimation(.easeOut(duration: 0.18)) {
            controlsDetached = true
            toolbarVisible = false
        }
        UserDefaults.standard.set(true, forKey: "controlsDetached")
        window.makeKeyAndOrderFront(nil)
    }

    func attachControls() {
        guard controlsDetached else { return }
        openPopover = nil
        controlsDetached = false
        UserDefaults.standard.set(false, forKey: "controlsDetached")
        let w = controlsWindow
        controlsWindow = nil
        w?.delegate = nil
        w?.close()
        noteActivity(reveal: true)
    }

    /// Screens the visuals can be shown on, main screen first.
    var screens: [NSScreen] { NSScreen.screens }

    var visualsScreen: NSScreen? { targetWindow?.screen }

    func screenName(_ screen: NSScreen) -> String {
        let size = "\(Int(screen.frame.width)) × \(Int(screen.frame.height))"
        let main = screen == NSScreen.screens.first ? " · main" : ""
        return "\(screen.localizedName) (\(size))\(main)"
    }

    /// Moves the visuals to a screen and makes them full screen there.
    func showVisuals(on screen: NSScreen) {
        guard let window = targetWindow else { return }
        if isFullScreen { exitFullScreen() }
        let vf = screen.visibleFrame
        let size = NSSize(width: min(window.frame.width, vf.width), height: min(window.frame.height, vf.height))
        window.setFrame(NSRect(x: vf.midX - size.width / 2, y: vf.midY - size.height / 2,
                               width: size.width, height: size.height), display: true)
        enterFullScreen()
    }

    // MARK: Toolbar auto-hide

    /// Call on any user activity. `nearTop` reveals the toolbar; other activity only keeps it alive.
    func noteActivity(reveal: Bool) {
        lastActivity = Date()
        if reveal && !toolbarVisible && !controlsDetached {
            withAnimation(.easeOut(duration: 0.18)) { toolbarVisible = true }
        }
    }

    func pointerMoved(toY y: CGFloat) {
        noteActivity(reveal: y < Self.revealZone)
    }

    func toggleToolbar() {
        guard !controlsDetached else { controlsWindow?.makeKeyAndOrderFront(nil); return }
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
        window ?? NSApp.windows.first { $0.isVisible && $0.canBecomeMain && $0 !== controlsWindow }
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

        // Hide the menu bar and Dock only when the visuals cover the screen they live on; on a
        // second display they stay available for the controls window.
        let isMainScreen = screen == NSScreen.screens.first
        NSApp.presentationOptions = isMainScreen ? [.hideDock, .hideMenuBar] : []
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
            case "d": self.toggleControlsDetached(); return nil
            case "r": self.toggleRecording(); return nil
            default: return event
            }
        }
    }
}

extension VisualizerModel: NSWindowDelegate {
    /// Closing the controls window puts the toolbar back on the visuals.
    func windowWillClose(_ notification: Notification) {
        guard (notification.object as? NSWindow) === controlsWindow else { return }
        DispatchQueue.main.async { [weak self] in self?.attachControls() }
    }
}

/// Lets an AppKit button call a Swift closure.
final class ButtonTarget: NSObject {
    var action: (() -> Void)?
    @objc func fire() { action?() }
}
