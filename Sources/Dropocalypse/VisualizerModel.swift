import AppKit
import DropocalypseCore
import SwiftUI
import UniformTypeIdentifiers

/// App-level state shared by the window, the settings panel and the renderer.
final class VisualizerModel: NSObject, ObservableObject {
    /// First, so it sees the settings as they were before this launch touches them.
    let whatsNew = WhatsNew()
    let settings = AppSettings()
    let capture = AudioCapture()
    let updates = UpdateChecker()

    // Top toolbar auto-hide.
    @Published private(set) var toolbarVisible = true
    @Published var openPopover: String?          // a toolbar drop-down is open → stay visible
    @Published var pointerOverToolbar = false
    @Published private(set) var isFullScreen = false
    /// Height of the camera notch (MacBook Pro) when the visuals are full screen on that display:
    /// the toolbar sits below it so nothing hides behind the notch.
    @Published private(set) var topSafeInset: CGFloat = 0
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

    // MilkDrop presets (Milkdrop.swift): bundled ones, then the user's folder.
    @Published private(set) var milkCatalog = MilkCatalog()
    /// Changes whenever the list is rebuilt, so the renderer loads the current preset again.
    @Published private(set) var milkGeneration = 0
    @Published private(set) var milkFolder: URL?
    @Published private(set) var milkScanning = false
    private var milkFolderAccess: URL?
    /// Nil when presets can be played; otherwise why not.
    let milkEngineError: String? = MilkdropHost.engineAvailable ? nil
        : "The MilkDrop engine (projectM) isn't included in this build."
    @Published var errorMessage: String?
    /// Logos and text items, left to right (top to bottom in portrait). Up to OverlayLayout.maxItems.
    @Published private(set) var items: [OverlayItem] = []
    /// The next image chosen in the file panel replaces this item instead of being added.
    private var pendingReplace: UUID?
    private var imageCache: [UUID: NSImage] = [:]

    var hasCustomLogo: Bool { !items.isEmpty }
    var canAddItem: Bool { items.count < OverlayLayout.maxItems }

    /// The toolbar's Logo menu label.
    var logoMenuTitle: String {
        switch items.count {
        case 0: return "Add Logo"
        case 1: return items[0].isText ? "Text: \(items[0].name)" : items[0].name
        default: return "Logos (\(items.count))"
        }
    }

    weak var renderer: Renderer? {
        didSet { pushItems() }
    }

    private var keyMonitor: Any?

    override init() {
        super.init()
        items = Self.loadSavedItems()
        refreshMilkdrop()
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
            try recorder.start(url: url, quality: quality, portrait: settings.pictureOrientation != 0, device: device)
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
        noteActivity(reveal: y < Self.revealZone + topSafeInset)
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
        topSafeInset = Self.notchHeight(of: screen)
        NotificationCenter.default.addObserver(self, selector: #selector(screensChanged),
                                               name: NSApplication.didChangeScreenParametersNotification, object: nil)
    }

    func exitFullScreen() {
        guard isFullScreen, let window = targetWindow else { return }
        NotificationCenter.default.removeObserver(self, name: NSApplication.didChangeScreenParametersNotification, object: nil)
        isFullScreen = false
        topSafeInset = 0
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
        topSafeInset = Self.notchHeight(of: screen)
    }

    static func notchHeight(of screen: NSScreen) -> CGFloat {
        if #available(macOS 12.0, *) { return screen.safeAreaInsets.top }
        return 0
    }

    func startAudio() {
        capture.start(deviceUID: settings.inputDeviceUID.isEmpty ? nil : settings.inputDeviceUID)
    }

    func reportError(_ message: String) {
        DispatchQueue.main.async { self.errorMessage = message }
    }

    // MARK: Scenes

    func nextScene(_ delta: Int = 1) {
        let n = VisualScene.all.count
        settings.sceneIndex = ((settings.sceneIndex + delta) % n + n) % n
    }

    // MARK: MilkDrop presets

    /// Looks through the bundled presets and the user's folder (in the background) and rebuilds
    /// the MilkDrop part of the style list.
    func refreshMilkdrop() {
        guard milkEngineError == nil else { return }
        milkFolderAccess?.stopAccessingSecurityScopedResource()
        milkFolderAccess = nil
        var folder: URL?
        if !settings.milkFolderBookmark.isEmpty {
            var stale = false
            if let url = try? URL(resolvingBookmarkData: settings.milkFolderBookmark, options: [.withSecurityScope],
                                  relativeTo: nil, bookmarkDataIsStale: &stale) {
                // Kept open: presets are read whenever one is shown.
                if url.startAccessingSecurityScopedResource() { milkFolderAccess = url }
                folder = url
            }
        }
        milkFolder = folder
        milkScanning = true
        DispatchQueue.global(qos: .userInitiated).async {
            let catalog = MilkCatalog.scan(bundled: MilkCatalog.bundledFolder, user: folder)
            DispatchQueue.main.async {
                self.milkCatalog = catalog
                VisualScene.setMilkdrop(names: catalog.presets.map(\.name))
                self.settings.userPresetStart = catalog.userCount > 0
                    ? VisualScene.builtInCount + catalog.bundledCount : nil
                self.milkGeneration += 1
                self.milkScanning = false
            }
        }
    }

    func chooseMilkFolder() {
        let panel = NSOpenPanel()
        panel.title = "Choose the folder with your MilkDrop presets (.milk files)"
        panel.message = "Subfolders are searched too. A \"Textures\" folder next to the presets is used as well."
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = milkFolder
        panel.prompt = "Use Folder"
        if panel.runModal() == .OK, let url = panel.url,
           let data = try? url.bookmarkData(options: [.withSecurityScope], includingResourceValuesForKeys: nil,
                                             relativeTo: nil) {
            settings.milkFolderBookmark = data
            refreshMilkdrop()
        }
    }

    func removeMilkFolder() {
        settings.milkFolderBookmark = Data()
        if settings.sceneIndex >= VisualScene.builtInCount + milkCatalog.bundledCount { settings.sceneIndex = 0 }
        refreshMilkdrop()
    }

    func openMilkFolder() {
        if let milkFolder { NSWorkspace.shared.open(milkFolder) }
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
    static var itemsURL: URL { supportDirectory.appendingPathComponent("items.json") }

    /// Saved items. The first time 1.3 runs, a logo saved by an earlier version becomes item 1.
    private static func loadSavedItems() -> [OverlayItem] {
        let fm = FileManager.default
        if let data = try? Data(contentsOf: itemsURL),
           let saved = try? JSONDecoder().decode([OverlayItem].self, from: data) {
            return Array(saved.filter { item in
                item.isText ? !OverlayLayout.cleanText(item.text).isEmpty
                            : fm.fileExists(atPath: supportDirectory.appendingPathComponent(item.file).path)
            }.prefix(OverlayLayout.maxItems))
        }
        if fm.fileExists(atPath: savedLogoURL.path) {
            let name = UserDefaults.standard.string(forKey: "logoName") ?? "Your logo"
            return [OverlayItem(image: "logo.png", name: name)]
        }
        return []
    }

    private func saveItems() {
        if let data = try? JSONEncoder().encode(items) {
            try? data.write(to: Self.itemsURL, options: .atomic)
        }
    }

    private func image(for item: OverlayItem) -> NSImage? {
        if let cached = imageCache[item.id] { return cached }
        let image = NSImage(contentsOf: Self.supportDirectory.appendingPathComponent(item.file))
        imageCache[item.id] = image
        return image
    }

    /// Sends the current items to the renderer and saves them.
    private func pushItems() {
        let sources: [Renderer.ItemSource] = items.compactMap { (item: OverlayItem) -> Renderer.ItemSource? in
            if item.isText {
                return Renderer.ItemSource.text(OverlayLayout.cleanText(item.text), font: item.font, color: item.color)
            }
            guard let picture = self.image(for: item) else { return nil }
            return Renderer.ItemSource.image(key: item.file, picture)
        }
        renderer?.setItems(sources)
        saveItems()
    }

    /// Adds a logo (or, after Replace…, swaps one). L, ⌘O and the Logo menu all come here.
    func chooseLogo() {
        guard canAddItem || pendingReplace != nil else {
            errorMessage = "That's the most: \(OverlayLayout.maxItems) logos or text items at once. Remove one to add another."
            return
        }
        let panel = NSOpenPanel()
        panel.title = pendingReplace == nil ? "Add a logo" : "Choose a logo to replace this one"
        panel.message = "PNG with a transparent background works best."
        panel.allowedContentTypes = [.png, .jpeg, .tiff, .heic, .gif, .bmp, .pdf, .svg, .image]
        panel.allowsMultipleSelection = pendingReplace == nil
        panel.canChooseDirectories = false
        if panel.runModal() == .OK {
            for url in panel.urls { loadLogo(from: url) }
        }
        pendingReplace = nil
    }

    func replaceItem(_ id: UUID) {
        pendingReplace = id
        chooseLogo()
    }

    func loadLogo(from url: URL) {
        guard let image = NSImage(contentsOf: url) else {
            errorMessage = "Couldn't open \(url.lastPathComponent) as an image."
            return
        }
        setLogo(image, name: url.lastPathComponent)
    }

    /// Adds an image as a new item (or replaces the one picked with Replace…). When all slots
    /// are full, a new image replaces the last one.
    func setLogo(_ image: NSImage, name: String?) {
        // Save a PNG copy so the logo is still there next launch.
        let file = "logo-\(UUID().uuidString).png"
        guard let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]),
              (try? png.write(to: Self.supportDirectory.appendingPathComponent(file))) != nil else {
            errorMessage = "Couldn't save a copy of that image."
            return
        }
        let item = OverlayItem(image: file, name: name ?? "Logo")
        imageCache[item.id] = image
        if let target = pendingReplace ?? (canAddItem ? nil : items.last?.id),
           let index = items.firstIndex(where: { $0.id == target }) {
            deleteFile(of: items[index])
            items[index] = item
            pendingReplace = nil
        } else {
            items.append(item)
        }
        settings.showLogo = true
        errorMessage = nil
        pushItems()
    }

    func addTextItem() {
        guard canAddItem else { return }
        items.append(OverlayItem(text: "YOUR TEXT", font: 1, color: 0))
        settings.showLogo = true
        errorMessage = nil
        pushItems()
        // The words are typed in the Logo panel.
        if !controlsDetached { openLogoPanel() }
    }

    func setText(_ id: UUID, _ text: String) {
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        items[i].text = String(text.prefix(120))
        let clean = OverlayLayout.cleanText(text)
        if !clean.isEmpty {   // keep showing the last text until something is typed
            items[i].name = clean
            pushItems()
        }
    }

    func setFont(_ id: UUID, _ font: Int) {
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        items[i].font = font
        pushItems()
    }

    func setColor(_ id: UUID, _ color: Int) {
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        items[i].color = color
        pushItems()
    }

    func moveItem(_ id: UUID, by delta: Int) {
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        let j = i + delta
        guard items.indices.contains(j) else { return }
        items.swapAt(i, j)
        pushItems()
    }

    func removeItem(_ id: UUID) {
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        deleteFile(of: items[i])
        imageCache[id] = nil
        items.remove(at: i)
        errorMessage = nil
        pushItems()
    }

    /// Opens the Logo panel, where text items are typed and items are reordered.
    func openLogoPanel() {
        noteActivity(reveal: true)
        openPopover = "logo"
    }

    /// Removes every logo and text item.
    func clearLogo() {
        for item in items { deleteFile(of: item) }
        items.removeAll()
        imageCache.removeAll()
        UserDefaults.standard.removeObject(forKey: "logoName")
        pushItems()
    }

    private func deleteFile(of item: OverlayItem) {
        guard !item.isText, !item.file.isEmpty, !item.file.contains("/") else { return }
        try? FileManager.default.removeItem(at: Self.supportDirectory.appendingPathComponent(item.file))
    }

    /// Handles files or images dragged onto the window.
    func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard !providers.isEmpty else { return false }
        var handled = false
        for provider in providers.prefix(OverlayLayout.maxItems) where handleDrop(provider) { handled = true }
        return handled
    }

    private func handleDrop(_ provider: NSItemProvider) -> Bool {
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
            // Typing in a text field (a text item's words) is not a shortcut.
            if event.window?.firstResponder is NSTextView { return event }
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
