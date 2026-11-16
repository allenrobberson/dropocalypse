import SwiftUI

/// Slim auto-hiding toolbar pinned to the top edge. Every control lives here; sliders open
/// as drop-down popovers so the visuals stay unobstructed.
struct TopToolbar: View {
    @ObservedObject var model: VisualizerModel
    @ObservedObject var settings: AppSettings
    @ObservedObject var capture: AudioCapture
    @ObservedObject var updates: UpdateChecker

    var body: some View {
        HStack(spacing: 4) {
            logoMenu
            separator
            styleControls
            separator
            popoverButton("Motion", icon: "waveform.path.ecg", id: "motion") { motionControls }
            popoverButton("Logo", icon: "sparkles", id: "logo") { logoLookControls }
            popoverButton("Audio", icon: capture.isRunning ? "mic.fill" : "mic.slash", id: "audio") { audioControls }
            popoverButton("Look", icon: "sun.max", id: "look") { lookControls }

            Spacer(minLength: 12)

            if updates.showBadge || updates.isBusy, let update = updates.available {
                Button {
                    model.openPopover = model.openPopover == "update" ? nil : "update"
                } label: {
                    Label("Update \(update.version)", systemImage: "arrow.down.circle.fill")
                        .font(.system(size: 11, weight: .semibold))
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(Color(red: 0.92, green: 0.29, blue: 0.67), in: Capsule())
                        .foregroundStyle(.white)
                }
                .buttonStyle(.plain)
                .help("Dropocalypse \(update.version) is available (you have \(updates.currentVersion))")
                .popover(isPresented: Binding(
                    get: { model.openPopover == "update" },
                    set: { if !$0 && model.openPopover == "update" { model.openPopover = nil } }),
                         arrowEdge: .bottom) {
                    updatePopover(update).environment(\.colorScheme, .dark)
                }
            }

            Text(capture.isRunning ? capture.statusText : "Audio off")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)

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

    private var logoMenu: some View {
        Menu {
            Button("Choose Logo…  (L)") { model.chooseLogo() }
            Button("Remove Logo") { model.clearLogo() }.disabled(!model.hasCustomLogo)
            Divider()
            Toggle("Show Logo  (G)", isOn: $settings.showLogo)
        } label: {
            Label(model.hasCustomLogo ? (model.logoName ?? "Logo") : "Add Logo", systemImage: "photo")
                .labelStyle(.titleAndIcon)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Choose the logo (or drag an image onto the window)")
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
                Text("You have version \(updates.currentVersion). **Update now** downloads the new version, installs it and reopens Dropocalypse. Your logo and settings are kept.")
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
        .frame(width: 360)
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

    private var logoLookControls: some View {
        PopoverForm(title: "Logo") {
            slider("Size", $settings.logoSize, 0.08...0.8)
            slider("Glow", $settings.glow, 0...1.5)
            slider("Color tint", $settings.logoTint, 0...1)
            Toggle("Logo trails (feeds into the visuals)", isOn: $settings.logoTrails)
            Toggle("Show logo", isOn: $settings.showLogo)
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
            slider("Auto-cycle every", $settings.cycleSeconds, 5...120) { "\(Int($0)) s" }
            Divider()
            Toggle("Check for updates automatically", isOn: $settings.checkForUpdates)
                .onChange(of: settings.checkForUpdates) { on in
                    if on { updates.check(userInitiated: false) }
                }
            HStack {
                Button(updates.checking ? "Checking…" : "Check now") { updates.check(userInitiated: true) }
                    .disabled(updates.checking)
                Spacer()
                Text("Version \(updates.currentVersion)").foregroundStyle(.secondary)
            }
            .font(.caption)
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
        ["L", "Choose logo"], ["G", "Show / hide logo"],
        ["Left / Right", "Previous / next style"], ["A", "Auto-cycle on / off"],
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
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            content()
        }
        .padding(14)
        .frame(width: 280)
    }
}
