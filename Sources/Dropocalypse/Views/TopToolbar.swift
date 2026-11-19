import DropocalypseCore
import SwiftUI

/// Slim auto-hiding toolbar pinned to the top edge. Every control lives here; sliders open
/// as drop-down popovers so the visuals stay unobstructed.
struct TopToolbar: View {
    @ObservedObject var model: VisualizerModel
    @ObservedObject var settings: AppSettings
    @ObservedObject var capture: AudioCapture
    @ObservedObject var updates: UpdateChecker
    /// true = the detached controls window: everything stacked top to bottom.
    var vertical = false

    @State var showMotion = true
    @State var showLogoLook = false
    @State var showAudio = true
    @State var showLook = false
    @State var showKeys = false
    @State var showUpdates = false

    var body: some View {
        if vertical { verticalBody } else { horizontalBody }
    }

    // MARK: Detached window (vertical)

    private var verticalBody: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {

                section("Visuals") {
                    Menu {
                        ForEach(model.screens, id: \.self) { screen in
                            Button(model.screenName(screen)) { model.showVisuals(on: screen) }
                        }
                    } label: {
                        Label(model.visualsScreen.map { model.screenName($0) } ?? "Choose a screen",
                              systemImage: "display")
                    }
                    .help("Show the visuals full screen on this display")
                    Button {
                        model.toggleFullScreen()
                    } label: {
                        Label(model.isFullScreen ? "Exit full screen" : "Full screen",
                              systemImage: model.isFullScreen ? "arrow.down.right.and.arrow.up.left"
                                                              : "arrow.up.left.and.arrow.down.right")
                            .frame(maxWidth: .infinity)
                    }
                    .controlSize(.large)
                    Text(capture.isRunning ? capture.statusText : "Audio off")
                        .font(.caption).foregroundStyle(.secondary).lineLimit(2)
                }

                section("Record") {
                    recordButton(compact: false)
                    if let message = model.recordMessage {
                        Text(message).font(.caption).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    DisclosureGroup("Recording settings") { recordSettings }
                }

                section("Logos and text") {
                    itemsEditor
                    Toggle("Show logos", isOn: $settings.showLogo)
                }

                section("Style") {
                    HStack(spacing: 4) {
                        Button { model.nextScene(-1) } label: { Image(systemName: "chevron.left") }
                        styleListButton.frame(maxWidth: .infinity)
                        Button { model.nextScene(1) } label: { Image(systemName: "chevron.right") }
                    }
                    Toggle("Auto-cycle", isOn: $settings.autoCycle)
                }

                DisclosureGroup("Motion", isExpanded: $showMotion) { motionControls }
                DisclosureGroup("Logo look", isExpanded: $showLogoLook) { logoLookControls(withItems: false) }
                DisclosureGroup(capture.isRunning ? "Audio" : "Audio (off)", isExpanded: $showAudio) { audioControls }
                DisclosureGroup("Look", isExpanded: $showLook) { lookControls }
                if UpdateChecker.isEnabled {
                    DisclosureGroup(isExpanded: Binding(get: { showUpdates || updates.showBadge || updates.isBusy },
                                                        set: { showUpdates = $0 })) {
                        updatesPanel
                    } label: {
                        Text(updates.showBadge || updates.isBusy
                             ? "Updates — \(updates.available?.version ?? "") available" : "Updates")
                            .foregroundStyle(updates.showBadge || updates.isBusy
                                             ? Color(red: 0.92, green: 0.29, blue: 0.67) : .primary)
                    }
                }
                DisclosureGroup("Keyboard shortcuts", isExpanded: $showKeys) { shortcuts }

                Divider()
                Button {
                    model.attachControls()
                } label: {
                    Label("Attach controls to the visuals", systemImage: "rectangle.on.rectangle")
                        .frame(maxWidth: .infinity)
                }
            }
            .padding(16)
        }
        .frame(minWidth: 300, maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(white: 0.08))
        .environment(\.colorScheme, .dark)
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased()).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            content()
        }
    }

    // MARK: Over the visuals (horizontal)

    private var horizontalBody: some View {
        HStack(spacing: 4) {
            logoMenu
            separator
            styleControls
            separator
            recordButton(compact: true)
            popoverButton(nil, icon: "chevron.down", id: "record") { recordSettings }
            separator
            popoverButton("Motion", icon: "waveform.path.ecg", id: "motion") { motionControls }
            popoverButton("Logo", icon: "sparkles", id: "logo") { logoLookControls(withItems: true) }
            popoverButton("Audio", icon: capture.isRunning ? "mic.fill" : "mic.slash", id: "audio") { audioControls }
            popoverButton("Look", icon: "sun.max", id: "look") { lookControls }

            Spacer(minLength: 12)

            if UpdateChecker.isEnabled {
                updatesMenuButton
            }

            Text(capture.isRunning ? capture.statusText : "Audio off")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)

            if model.screens.count > 1 {
                Menu {
                    Text("Show the visuals full screen on:")
                    ForEach(model.screens, id: \.self) { screen in
                        Button(model.screenName(screen)) { model.showVisuals(on: screen) }
                    }
                } label: {
                    Image(systemName: "display.2")
                }
                .menuStyle(.borderlessButton)
                .fixedSize()
                .help("Show the visuals on another display")
            }
            toolbarButton(nil, icon: "rectangle.portrait.and.arrow.right",
                          help: "Detach the controls into their own window (D)") {
                model.detachControls()
            }
            popoverButton(nil, icon: "keyboard", id: "keys") { shortcuts }
            toolbarButton(nil, icon: model.isFullScreen ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right",
                          help: "Full screen (F)") {
                model.toggleFullScreen()
            }
        }
        .padding(.leading, model.isFullScreen ? 12 : 80) // leave room for the window buttons
        .padding(.trailing, 10)
        .frame(height: 38)
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial)
        .overlay(alignment: .bottom) { Rectangle().fill(.white.opacity(0.08)).frame(height: 1) }
        .environment(\.colorScheme, .dark)
        .onHover { model.pointerOverToolbar = $0 }
    }

    // MARK: Pieces

    private var separator: some View {
        Rectangle().fill(.white.opacity(0.15)).frame(width: 1, height: 18).padding(.horizontal, 4)
    }

    private var isPortrait: Bool { settings.pictureOrientation != 0 }

    private var logoMenu: some View {
        Menu {
            ForEach(Array(model.items.enumerated()), id: \.element.id) { index, item in
                Menu("\(index + 1). " + (item.isText ? "“\(item.name)”" : item.name)) {
                    if !item.isText {
                        Button("Replace…") { model.replaceItem(item.id) }
                    }
                    Button(isPortrait ? "Move Up" : "Move Left") { model.moveItem(item.id, by: -1) }
                        .disabled(index == 0)
                    Button(isPortrait ? "Move Down" : "Move Right") { model.moveItem(item.id, by: 1) }
                        .disabled(index == model.items.count - 1)
                    Button("Remove") { model.removeItem(item.id) }
                }
            }
            if !model.items.isEmpty { Divider() }
            Button("Add Logo…  (L)") { model.chooseLogo() }.disabled(!model.canAddItem)
            Button("Add Text") { model.addTextItem() }.disabled(!model.canAddItem)
            Button("Edit Text and Order…") { model.openLogoPanel() }.disabled(model.items.isEmpty)
            Button("Remove All") { model.clearLogo() }.disabled(model.items.isEmpty)
            Divider()
            Toggle("Show Logos  (G)", isOn: $settings.showLogo)
        } label: {
            Label(model.logoMenuTitle, systemImage: model.items.count > 1 ? "photo.on.rectangle" : "photo")
                .labelStyle(.titleAndIcon)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Up to 4 logos or text items (or drag images onto the window)")
    }

    /// The list of logos and text items: each text item's words, font and color, plus
    /// replace, reorder and remove. In the Logo panel and the detached controls window.
    private var itemsEditor: some View {
        VStack(alignment: .leading, spacing: 8) {
            if model.items.isEmpty {
                Text("No logo yet: add an image or some text.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ForEach(Array(model.items.enumerated()), id: \.element.id) { index, item in
                itemRow(index: index, item: item)
            }
            HStack {
                Button("Add Logo…") { model.chooseLogo() }
                Button("Add Text") { model.addTextItem() }
            }
            .font(.caption)
            .disabled(!model.canAddItem)
            Text(model.canAddItem ? "Up to 4. They sit side by side, or top to bottom in Portrait."
                                  : "That's the most: 4 at once. Remove one to add another.")
                .font(.caption2).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func itemRow(index: Int, item: OverlayItem) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Text("\(index + 1).").font(.caption).monospacedDigit().foregroundStyle(.secondary)
                if item.isText {
                    TextField("Type your text", text: Binding(get: { item.text },
                                                              set: { model.setText(item.id, $0) }))
                        .textFieldStyle(.roundedBorder)
                        .font(.caption)
                } else {
                    Text(item.name)
                        .font(.caption).lineLimit(1).truncationMode(.middle)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .help(item.name)
                    Button("Replace…") { model.replaceItem(item.id) }.font(.caption)
                }
                Button { model.moveItem(item.id, by: -1) } label: {
                    Image(systemName: isPortrait ? "chevron.up" : "chevron.left")
                }
                .disabled(index == 0)
                .help(isPortrait ? "Move up" : "Move left")
                Button { model.moveItem(item.id, by: 1) } label: {
                    Image(systemName: isPortrait ? "chevron.down" : "chevron.right")
                }
                .disabled(index == model.items.count - 1)
                .help(isPortrait ? "Move down" : "Move right")
                Button { model.removeItem(item.id) } label: { Image(systemName: "xmark.circle.fill") }
                    .help("Remove")
            }
            .buttonStyle(.borderless)
            if item.isText {
                HStack(spacing: 6) {
                    Picker("Font", selection: Binding(get: { item.font }, set: { model.setFont(item.id, $0) })) {
                        ForEach(OverlayLayout.textFontNames.indices, id: \.self) { f in
                            Text(OverlayLayout.textFontNames[f]).tag(f)
                        }
                    }
                    .labelsHidden()
                    .controlSize(.small)
                    .frame(width: 90)
                    ForEach(OverlayLayout.textColors.indices, id: \.self) { c in
                        colorSwatch(c, selected: item.color == c) { model.setColor(item.id, c) }
                    }
                }
                .padding(.leading, 18)
            }
        }
    }

    private func colorSwatch(_ index: Int, selected: Bool, action: @escaping () -> Void) -> some View {
        let rgb = OverlayLayout.textColors[index]
        let color = Color(red: Double((rgb >> 16) & 0xFF) / 255, green: Double((rgb >> 8) & 0xFF) / 255,
                          blue: Double(rgb & 0xFF) / 255)
        return Circle()
            .fill(color)
            .frame(width: 16, height: 16)
            .overlay(Circle().stroke(Color.white, lineWidth: selected ? 2 : 0))
            .contentShape(Circle())
            .onTapGesture(perform: action)
            .help(OverlayLayout.textColorNames[index])
    }

    private var styleControls: some View {
        HStack(spacing: 2) {
            toolbarButton(nil, icon: "chevron.left", help: "Previous style (←)") { model.nextScene(-1) }
            styleListButton
            toolbarButton(nil, icon: "chevron.right", help: "Next style (→)") { model.nextScene(1) }
            Toggle(isOn: $settings.autoCycle) {
                Image(systemName: "arrow.triangle.2.circlepath")
            }
            .toggleStyle(.button)
            .help("Auto-cycle styles (A)")
        }
    }

    private func updatePopover(_ update: UpdateChecker.Release) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Dropocalypse \(update.version) is available").font(.headline)
            switch updates.phase {
            case .downloading(let p):
                ProgressView(value: p) { Text("Downloading… \(Int(p * 100))%").font(.caption) }
            case .verifying:
                ProgressView { Text("Checking the download…").font(.caption) }
            case .installing:
                ProgressView { Text("Installing…").font(.caption) }
            case .relaunching:
                ProgressView { Text("Restarting Dropocalypse…").font(.caption) }
            case .failed(let message):
                Text(message).font(.caption).foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Button("Download page") { updates.openDownloadPage(); model.openPopover = nil }
                    Spacer()
                    Button("Try again") { updates.resetInstallState(); updates.installUpdate() }
                        .disabled(updates.installBlocker != nil)
                }
            case .idle:
                Text("You have version \(updates.currentVersion). **Update now** downloads the new version, checks its signature, installs it and reopens Dropocalypse. Your logo and settings are kept.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Button("Ignore for 24 hours") {
                        updates.ignoreForADay()
                        model.openPopover = nil
                    }
                    Spacer()
                    Button("Download page") {
                        updates.openDownloadPage()
                        model.openPopover = nil
                    }
                    Button("Update now") { updates.installUpdate() }
                        .keyboardShortcut(.defaultAction)
                }
            }
        }
        .padding(14)
        .frame(width: vertical ? nil : 360)
    }

    /// The style name opens a list: click a name to show it now, and use the checkboxes to choose
    /// which styles auto-cycle rotates through.
    private var styleListButton: some View {
        let isOpen = Binding<Bool>(
            get: { model.openPopover == "styles" },
            set: { open in
                if open { model.openPopover = "styles" } else if model.openPopover == "styles" { model.openPopover = nil }
            })
        return Button {
            isOpen.wrappedValue.toggle()
        } label: {
            HStack(spacing: 4) {
                Text(model.currentSceneName)
                Image(systemName: "chevron.down").font(.system(size: 9, weight: .semibold))
            }
            .frame(minWidth: 110)
        }
        .buttonStyle(.borderless)
        .help("Pick a style, and check which ones auto-cycle rotates through")
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            styleList.environment(\.colorScheme, .dark)
        }
    }

    private var styleList: some View {
        let scenes = VisualScene.builtIn
        let current = ((settings.sceneIndex % scenes.count) + scenes.count) % scenes.count
        let checked = scenes.filter { settings.isInCycle($0.name) }.count
        return VStack(alignment: .leading, spacing: 8) {
            Text("Styles").font(.headline)
            Text("Click a name to show it. Checked styles are the ones auto-cycle rotates through.")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Toggle("Auto-cycle", isOn: $settings.autoCycle)
                Spacer()
                Button("All") { settings.setAllInCycle(true) }
                Button("None") { settings.setAllInCycle(false) }
            }
            .font(.caption)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(scenes.enumerated()), id: \.offset) { index, scene in
                        HStack(spacing: 8) {
                            Toggle("", isOn: Binding(
                                get: { settings.isInCycle(scene.name) },
                                set: { settings.setInCycle(scene.name, $0) }))
                                .toggleStyle(.checkbox)
                                .labelsHidden()
                                .help("Include \(scene.name) in auto-cycle")
                            Button {
                                settings.sceneIndex = index
                            } label: {
                                HStack {
                                    Text("\(index + 1).").monospacedDigit().foregroundStyle(.secondary)
                                        .frame(width: 24, alignment: .trailing)
                                    Text(scene.name).fontWeight(index == current ? .bold : .regular)
                                    Spacer()
                                    if index == current { Image(systemName: "play.fill").font(.caption2) }
                                }
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                        .padding(.vertical, 3)
                        .padding(.horizontal, 6)
                        .background(index == current ? Color.white.opacity(0.1) : Color.clear,
                                    in: RoundedRectangle(cornerRadius: 6))
                    }
                }
            }
            .frame(height: 420)
            Text(checked == 0 ? "Nothing checked, so auto-cycle uses every style."
                              : "Auto-cycle: \(checked) of \(scenes.count) styles")
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(14)
        .frame(width: 300)
    }

    // MARK: Updates

    /// Always in the toolbar; pink with the new version number when an update is out.
    private var updatesMenuButton: some View {
        let isOpen = Binding<Bool>(
            get: { model.openPopover == "update" },
            set: { open in
                if open { model.openPopover = "update" } else if model.openPopover == "update" { model.openPopover = nil }
            })
        let highlight = updates.showBadge || updates.isBusy
        return Button {
            isOpen.wrappedValue.toggle()
        } label: {
            Label(highlight ? "Update \(updates.available?.version ?? "")" : "Updates",
                  systemImage: highlight ? "arrow.down.circle.fill" : "arrow.down.circle")
                .font(.system(size: 11, weight: highlight ? .semibold : .regular))
                .padding(.horizontal, highlight ? 8 : 4)
                .padding(.vertical, 3)
                .background(highlight ? Color(red: 0.92, green: 0.29, blue: 0.67) : Color.clear, in: Capsule())
                .foregroundStyle(highlight ? Color.white : Color.primary)
        }
        .buttonStyle(.plain)
        .help(highlight ? "Dropocalypse \(updates.available?.version ?? "") is available (you have \(updates.currentVersion))"
                        : "Check for updates")
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            updatesPanel.environment(\.colorScheme, .dark)
        }
    }

    private var updatesPanel: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let update = updates.available, updates.showBadge || updates.isBusy {
                updatePopover(update)
                Divider().padding(.horizontal, 14)
            } else {
                Text("Updates").font(.headline).padding([.top, .horizontal], 14)
                Text("You have the latest version (\(updates.currentVersion)).")
                    .font(.caption).foregroundStyle(.secondary).padding(.horizontal, 14)
            }
            VStack(alignment: .leading, spacing: 8) {
                Toggle("Check for updates automatically", isOn: $settings.checkForUpdates)
                    .onChange(of: settings.checkForUpdates) { on in
                        if on { updates.check(userInitiated: false) }
                    }
                HStack {
                    Button(updates.checking ? "Checking…" : "Check now") { updates.check(userInitiated: true) }
                        .disabled(updates.checking)
                    Button("What's new") { model.openPopover = nil; model.whatsNew.show() }
                    Spacer()
                    Text("Version \(updates.currentVersion)").foregroundStyle(.secondary)
                }
            }
            .font(.caption)
            .padding([.horizontal, .bottom], 14)
        }
        .frame(width: vertical ? nil : 360)
    }

    // MARK: Recording

    private func recordButton(compact: Bool) -> some View {
        Button {
            model.toggleRecording()
        } label: {
            HStack(spacing: 5) {
                Image(systemName: model.isRecording ? "stop.circle.fill" : "record.circle")
                Text(model.isRecording ? "Stop  \(VisualizerModel.timeLabel(model.recordingElapsed))" : "Record")
                    .monospacedDigit()
            }
            .font(.system(size: compact ? 11 : 13, weight: .semibold))
            .padding(.horizontal, compact ? 8 : 12)
            .padding(.vertical, compact ? 3 : 7)
            .frame(maxWidth: compact ? nil : .infinity)
            .background(model.isRecording ? Color.red : Color.white.opacity(0.12),
                        in: RoundedRectangle(cornerRadius: compact ? 11 : 8))
            .foregroundStyle(.white)
        }
        .buttonStyle(.plain)
        .help(model.isRecording ? "Stop recording and save the video (R)"
                                : "Record the visuals and music to a video (R)")
    }

    private var recordSettings: some View {
        PopoverForm(title: "Recording") {
            Picker("Video size", selection: $settings.recordQuality) {
                ForEach(Recorder.Quality.allCases) { q in
                    Text(recordSizeLabel(q)).tag(q.rawValue)
                }
            }
            .pickerStyle(.radioGroup)
            .disabled(model.isRecording)
            VStack(alignment: .leading, spacing: 4) {
                Text("Save recordings to").font(.caption)
                Text(model.recordFolderName)
                    .font(.caption).foregroundStyle(.secondary)
                    .lineLimit(2).truncationMode(.middle)
                HStack {
                    Button("Change…") { model.chooseRecordFolder() }
                    Button("Open") { model.openRecordFolder() }
                    if !settings.recordFolderBookmark.isEmpty {
                        Button("Use Movies") { model.useMoviesFolder() }
                    }
                }
                .font(.caption)
            }
            if model.lastRecordingURL != nil {
                Button("Show last recording") { model.showLastRecording() }.font(.caption)
            }
            Text("Records the visuals and the music you're listening to, without the toolbar. Landscape follows the window (letterboxed into 16:9 when it isn't). Portrait records the centered 9:16 frame. Set that under Look.")
                .font(.caption2).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func recordSizeLabel(_ quality: Recorder.Quality) -> String {
        let pixels = FrameLayout.recordingSize(quality: quality.rawValue, portrait: settings.pictureOrientation != 0)
        return "\(quality.label)  (\(pixels.width) × \(pixels.height), \(quality.fps) fps)"
    }

    // MARK: Popover contents

    private var motionControls: some View {
        PopoverForm(title: "Logo motion — driven by the bass") {
            slider("Pulse", $settings.pulse, 0...1.5)
            slider("Squash & stretch", $settings.squash, 0...1.5)
            slider("Bounce", $settings.bounce, 0...1.5)
            slider("Slide", $settings.slide, 0...1.5)
            slider("Wobble", $settings.wobble, 0...1.5)
            Button("Reset to defaults") { settings.resetMotion() }.font(.caption)
        }
    }

    private func logoLookControls(withItems: Bool) -> some View {
        PopoverForm(title: withItems ? "Logos and text" : "Logo", width: withItems ? 340 : 280) {
            if withItems {
                itemsEditor
                Divider()
            }
            slider("Size", $settings.logoSize, 0.08...0.8)
            slider("Glow", $settings.glow, 0...1.5)
            slider("Color tint", $settings.logoTint, 0...1)
            Toggle("Logo trails (feeds into the visuals)", isOn: $settings.logoTrails)
            Toggle("Show logos", isOn: $settings.showLogo)
        }
    }

    /// A saved app that isn't open right now still needs a row, or the picker shows blank.
    private var missingAppTag: (tag: String, name: String)? {
        let tag = settings.inputDeviceUID
        guard tag.hasPrefix("tap:app:") else { return nil }
        let id = String(tag.dropFirst("tap:app:".count))
        guard !capture.audioApps.contains(where: { $0.id == id }) else { return nil }
        return (tag, "\(id.components(separatedBy: ".").last ?? id) (not open)")
    }

    private var audioControls: some View {
        PopoverForm(title: "Audio source") {
            Picker("Listen to", selection: $settings.inputDeviceUID) {
                if AudioCapture.macAudioSupported {
                    Section("Mac audio: no mic needed") {
                        Text("All Mac audio").tag(AudioCapture.allMacAudioTag)
                        ForEach(capture.audioApps) { app in
                            Text(app.isPlaying ? "\(app.name)  ♪ playing" : app.name)
                                .tag(AudioCapture.appTag(app.id))
                        }
                        if let missing = missingAppTag {
                            Text(missing.name).tag(missing.tag)
                        }
                    }
                }
                Section("Microphone or line-in") {
                    Text("System default input").tag("")
                    ForEach(capture.devices) { d in Text(d.name).tag(d.uid) }
                }
            }
            HStack {
                Button("Refresh list") { capture.refreshDevices() }
                Button("Restart audio") { model.startAudio() }
            }
            .font(.caption)
            if capture.isTapSource {
                Text("Pick Spotify, Music, a browser or any open app, and Dropocalypse hears exactly what it plays, at full quality. The first time, macOS asks to allow system audio recording. If nothing moves, turn on Dropocalypse in System Settings → Privacy & Security → Screen & System Audio Recording.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else if !AudioCapture.macAudioSupported {
                Text("Update to macOS 14.2 or later to listen to Spotify and other apps directly.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            slider("Sensitivity", $settings.sensitivity, 0.2...4)
            slider("Beat threshold", $settings.beatThreshold, 1.1...2.2) { String(format: "%.2f×", $0) }
            Text(capture.statusText)
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .onAppear { capture.refreshDevices() }
    }

    private var lookControls: some View {
        PopoverForm(title: "Look") {
            slider("Beat flash", $settings.beatFlash, 0...1)
            slider("Brightness", $settings.brightness, 0.4...1.6)
            Picker("Frame", selection: $settings.pictureOrientation) {
                Text("Landscape").tag(0)
                Text("Portrait").tag(1)
            }
            .pickerStyle(.segmented)
            .disabled(model.isRecording)
            Text(settings.pictureOrientation == 0
                 ? "Landscape fills the window."
                 : "Portrait is a 9:16 frame, centered on screen. Recordings are 720×1280, 1080×1920, or 2160×3840.")
                .font(.caption2).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            slider("Auto-cycle every", $settings.cycleSeconds, 5...360) { "\(Int($0)) s" }
            Picker("Frame rate", selection: $settings.frameCap) {
                Text("Display").tag(0)
                Text("60").tag(60)
                Text("30").tag(30)
            }
            .pickerStyle(.segmented)
            Text(settings.frameCap == 0
                 ? "Display matches the monitor."
                 : "A lower cap uses less CPU. The picture stays the same shape.")
                .font(.caption2).foregroundStyle(.secondary)
            if !UpdateChecker.isEnabled {
                Divider()
                Text("Version \(updates.currentVersion) · Updates come from the App Store.")
                    .font(.caption).foregroundStyle(.secondary)
                Button("What's new in this version") { model.openPopover = nil; model.whatsNew.show() }
                    .font(.caption)
            }
        }
    }

    private var shortcuts: some View {
        PopoverForm(title: "Keyboard shortcuts") {
            ForEach(Self.shortcutRows, id: \.self) { row in
                HStack {
                    Text(row[0]).bold().frame(width: 84, alignment: .leading)
                    Text(row[1]).foregroundStyle(.secondary)
                }
                .font(.caption)
            }
            Text("Move the mouse to the top edge to bring the toolbar back.")
                .font(.caption2).foregroundStyle(.secondary)
        }
    }

    private static let shortcutRows: [[String]] = [
        ["F", "Full screen"], ["T / S", "Show / hide toolbar"], ["Esc", "Hide toolbar"],
        ["L", "Add a logo"], ["G", "Show / hide logo"],
        ["Left / Right", "Previous / next style"], ["A", "Auto-cycle on / off"],
        ["D", "Detach / attach the controls"], ["R", "Start / stop recording"],
    ]

    // MARK: Helpers

    private func toolbarButton(_ title: String?, icon: String, help: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            if let title { Label(title, systemImage: icon) } else { Image(systemName: icon) }
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 4)
        .help(help)
    }

    private func popoverButton<Content: View>(_ title: String?, icon: String, id: String,
                                              @ViewBuilder content: @escaping () -> Content) -> some View {
        let isOpen = Binding<Bool>(
            get: { model.openPopover == id },
            set: { open in
                if open { model.openPopover = id } else if model.openPopover == id { model.openPopover = nil }
            })
        return Button {
            isOpen.wrappedValue.toggle()
        } label: {
            if let title { Label(title, systemImage: icon) } else { Image(systemName: icon) }
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 4)
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            content().environment(\.colorScheme, .dark)
        }
    }

    private func slider(_ title: String, _ value: Binding<Double>, _ range: ClosedRange<Double>,
                        format: @escaping (Double) -> String = { String(format: "%.2f", $0) }) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title)
                Spacer()
                Text(format(value.wrappedValue)).monospacedDigit().foregroundStyle(.secondary)
            }
            .font(.caption)
            Slider(value: value, in: range).controlSize(.small)
        }
    }
}

private struct PopoverForm<Content: View>: View {
    let title: String
    var width: CGFloat = 280
    @ViewBuilder let content: () -> Content

    init(title: String, width: CGFloat = 280, @ViewBuilder content: @escaping () -> Content) {
        self.title = title
        self.width = width
        self.content = content
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            content()
        }
        .padding(14)
        .frame(width: width)
    }
}
