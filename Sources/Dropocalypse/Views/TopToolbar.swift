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
    @State private var milkFilter = ""
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
                    }
                    .buttonStyle(RaisedKeyStyle(height: 34, fill: true))
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
                    HStack(spacing: 6) {
                        arrowKey("chevron.left", help: "Previous style (←)") { model.nextScene(-1) }
                        styleListButton(width: nil)
                        arrowKey("chevron.right", help: "Next style (→)") { model.nextScene(1) }
                    }
                    Toggle("Auto-cycle (A)", isOn: $settings.autoCycle)
                    Well {
                        slider("Change style every", $settings.cycleSeconds, 5...360) { "\(Int($0)) s" }
                    }
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
                }
                .buttonStyle(RaisedKeyStyle(height: 34, fill: true))
            }
            .padding(16)
            .buttonStyle(RaisedKeyStyle(height: 28))
            .toggleStyle(Switch3DStyle())
            .disclosureGroupStyle(Raised3DDisclosureStyle())
            .environment(\.inDetachedControls, true)
        }
        .frame(minWidth: 300, maxWidth: .infinity, maxHeight: .infinity)
        .background(Palette3D.bar)
        .environment(\.colorScheme, .dark)
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased()).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            content()
        }
    }

    // MARK: Over the visuals (horizontal)

    /// Left: the style with its arrows, auto-cycle, the menus and record. Right: Detach, Screen,
    /// Updates and Keys. The menus show their full names when they fit, and single letters (with
    /// the name on hover) when they don't. Detach always shows its full name.
    private var horizontalBody: some View {
        ViewThatFits(in: .horizontal) {
            toolbarRow(letters: false)
            toolbarRow(letters: true)
        }
        .padding(.leading, model.isFullScreen ? 12 : 80) // leave room for the window buttons
        .padding(.trailing, 10)
        .frame(height: 50)
        .frame(maxWidth: .infinity)
        .background(Palette3D.bar.opacity(0.9))
        .background(.ultraThinMaterial)
        .overlay(alignment: .bottom) { Rectangle().fill(Palette3D.base).frame(height: 3) }
        .environment(\.colorScheme, .dark)
        .onHover { model.pointerOverToolbar = $0 }
    }

    private func toolbarRow(letters: Bool) -> some View {
        HStack(spacing: 6) {
            arrowKey("chevron.left", help: "Previous style (←)") { model.nextScene(-1) }
            styleListButton(width: letters ? 128 : 180)
            arrowKey("chevron.right", help: "Next style (→)") { model.nextScene(1) }
            autoCycleKey(letters: letters)
            separator
            menuKey("Logo", letter: "L", name: "Logos and text", id: "logo", letters: letters) {
                logoLookControls(withItems: true)
            }
            menuKey("Motion", letter: "M", name: "Motion", id: "motion", letters: letters) { motionControls }
            menuKey(capture.isRunning ? "Audio" : "Audio (off)", letter: "A",
                    name: capture.isRunning ? "Audio" : "Audio (off)", id: "audio", letters: letters,
                    look: capture.isRunning ? .normal : .alert) { audioControls }
            menuKey("Look", letter: "V", name: "Look", id: "look", letters: letters) { lookControls }
            separator
            recordButton(compact: true, letters: letters)
            menuKey("", letter: "", name: "Recording settings", id: "record", letters: letters,
                    icon: "chevron.down", width: 26) { recordSettings }

            Spacer(minLength: 12)
                .overlay(alignment: .leading) {   // the status never forces the letters on
                    Text(capture.isRunning ? capture.statusText : "Audio off")
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .truncationMode(.tail)
                        .padding(.leading, 4)
                }

            Button("Detach") { model.detachControls() }
                .buttonStyle(RaisedKeyStyle(height: 36))
                .help("Move the controls into their own window, for a second screen (D)")
            menuKey("Screen", letter: "S", name: "Screen", id: "screen", letters: letters) { screenMenu }
            if UpdateChecker.isEnabled {
                let highlight = updates.showBadge || updates.isBusy
                menuKey(highlight ? "Update \(updates.available?.version ?? "")" : "Updates", letter: "U",
                        name: highlight ? "Dropocalypse \(updates.available?.version ?? "") is available" : "Updates",
                        id: "update", letters: letters, look: highlight ? .on : .normal) { updatesPanel }
            }
            menuKey("Keys", letter: "?", name: "Keyboard shortcuts", id: "keys", letters: letters) { shortcuts }
        }
    }

    // MARK: Pieces

    private var separator: some View {
        Rectangle().fill(.white.opacity(0.15)).frame(width: 1, height: 24).padding(.horizontal, 3)
    }

    private var isPortrait: Bool { settings.pictureOrientation != 0 }

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

    /// Auto-cycle key: green and lit while on. Right-click (or Control-click) for its settings.
    private func autoCycleKey(letters: Bool) -> some View {
        let isOpen = popoverBinding("autocycle")
        return Button { settings.autoCycle.toggle() } label: {
            HStack(spacing: 6) {
                Image(systemName: "arrow.triangle.2.circlepath").font(.system(size: 14, weight: .semibold))
                if !letters { Text("Auto") }
            }
        }
        .buttonStyle(RaisedKeyStyle(look: settings.autoCycle ? .lit : .normal, height: 36, minWidth: 36))
        .help(settings.autoCycle ? "Auto-cycle is on (A). Right-click for settings."
                                 : "Auto-cycle is off (A). Right-click for settings.")
        .contextMenu {
            Button(settings.autoCycle ? "Turn auto-cycle off" : "Turn auto-cycle on") { settings.autoCycle.toggle() }
            Button("Auto-cycle settings…") { isOpen.wrappedValue = true }
        }
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            MenuPanel(title: "Auto-cycle", subtitle: "Click the style name to choose which styles take part.") {
                Toggle("Auto-cycle (A)", isOn: $settings.autoCycle)
                Well {
                    slider("Change style every", $settings.cycleSeconds, 5...360) { "\(Int($0)) s" }
                }
            }
            .environment(\.colorScheme, .dark)
        }
    }

    /// The Screen menu: which display shows the visuals, full screen, and detaching the controls.
    private var screenMenu: some View {
        MenuPanel(title: "Screen", width: 280) {
            if model.screens.count > 1 {
                Text("Show the visuals full screen on:").font(.caption).foregroundStyle(.secondary)
                ForEach(model.screens, id: \.self) { screen in
                    Button(model.screenName(screen)) {
                        model.showVisuals(on: screen)
                        model.openPopover = nil
                    }
                    .buttonStyle(RaisedKeyStyle(height: 30, fill: true))
                }
            }
            Button(model.isFullScreen ? "Exit full screen (F)" : "Full screen (F)") {
                model.openPopover = nil
                model.toggleFullScreen()
            }
            .buttonStyle(RaisedKeyStyle(height: 30, fill: true))
            Button("Detach the controls (D)") {
                model.openPopover = nil
                model.detachControls()
            }
            .buttonStyle(RaisedKeyStyle(height: 30, fill: true))
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
    private func styleListButton(width: CGFloat?) -> some View {
        let isOpen = popoverBinding("styles")
        return Button {
            isOpen.wrappedValue.toggle()
        } label: {
            Text(model.currentSceneName)
                .truncationMode(.tail)
                .frame(width: width.map { $0 - 20 })
                .frame(maxWidth: width == nil ? .infinity : nil)
        }
        .buttonStyle(RaisedKeyStyle(look: isOpen.wrappedValue ? .on : .name, height: 36, fill: width == nil))
        .help("Pick a style, and check which ones auto-cycle rotates through")
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            styleList.environment(\.colorScheme, .dark).environment(\.inDetachedControls, false)
        }
    }

    private var styleList: some View {
        let scenes = VisualScene.all
        let current = ((settings.sceneIndex % scenes.count) + scenes.count) % scenes.count
        let bundledEnd = VisualScene.builtInCount + model.milkCatalog.bundledCount
        let fixed = min(scenes.count, bundledEnd)
        let checked = scenes.prefix(fixed).filter { settings.isInCycle($0.name) }.count
        return MenuPanel(title: "Styles", subtitle: "Click a name to show it. Checked styles are the ones auto-cycle rotates through.",
                         width: 340) {
            HStack {
                Toggle("Auto-cycle", isOn: $settings.autoCycle)
                Spacer()
                Button("All") { settings.setAllInCycle(true) }
                Button("None") { settings.setAllInCycle(false) }
            }
            .font(.caption)
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(0..<min(scenes.count, VisualScene.builtInCount), id: \.self) { index in
                        styleRow(scenes[index], index: index, current: current, checkbox: true)
                    }
                    if bundledEnd > VisualScene.builtInCount {
                        styleSectionHeader("MilkDrop")
                        ForEach(VisualScene.builtInCount..<min(scenes.count, bundledEnd), id: \.self) { index in
                            styleRow(scenes[index], index: index, current: current, checkbox: true)
                        }
                    }
                    milkFolderSection(scenes: scenes, current: current, start: bundledEnd)
                }
                .padding(6)
            }
            .frame(height: 460)
            .background(RoundedRectangle(cornerRadius: 9).fill(Palette3D.well))
            .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(Color.black, lineWidth: 1))
            Text(checked == 0 ? "Nothing checked, so auto-cycle uses every style."
                              : "Auto-cycle: \(checked) of \(fixed) styles"
                                + (settings.userPresetStart != nil && settings.cycleUserPresets ? ", plus your presets" : ""))
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private func styleSectionHeader(_ title: String) -> some View {
        Text(title).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            .padding(.top, 10).padding(.bottom, 2).padding(.horizontal, 6)
    }

    private func styleRow(_ scene: VisualScene, index: Int, current: Int, checkbox: Bool) -> some View {
        HStack(spacing: 8) {
            if checkbox {
                Toggle("", isOn: Binding(
                    get: { settings.isInCycle(scene.name) },
                    set: { settings.setInCycle(scene.name, $0) }))
                    .toggleStyle(.checkbox)
                    .tint(Palette3D.pink)
                    .labelsHidden()
                    .help("Include \(scene.name) in auto-cycle")
            }
            Button {
                settings.sceneIndex = index
            } label: {
                HStack {
                    if checkbox {
                        Text("\(index + 1).").monospacedDigit().foregroundStyle(.secondary)
                            .frame(width: 24, alignment: .trailing)
                    }
                    Text(scene.name).fontWeight(index == current ? .bold : .regular).lineLimit(1)
                    Spacer()
                    if index == current {
                        Image(systemName: "play.fill").font(.caption2).foregroundStyle(Palette3D.pink)
                    }
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
        .padding(.vertical, 3)
        .padding(.horizontal, 6)
        .background(index == current ? Palette3D.pink.opacity(0.22) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 6))
    }

    /// The user's own MilkDrop presets: the folder, whether they join auto-cycle, and a searchable list.
    @ViewBuilder
    private func milkFolderSection(scenes: [VisualScene], current: Int, start: Int) -> some View {
        styleSectionHeader("Your MilkDrop presets")
        if let error = model.milkEngineError {
            Text(error).font(.caption).foregroundStyle(.secondary).padding(.horizontal, 6)
        } else {
            VStack(alignment: .leading, spacing: 6) {
                if let folder = model.milkFolder {
                    Text(folder.path.replacingOccurrences(of: NSHomeDirectory(), with: "~"))
                        .font(.caption).foregroundStyle(.secondary).lineLimit(2).truncationMode(.middle)
                } else {
                    Text("Pick a folder of .milk files (subfolders too), for example a MilkDrop or NestDrop \"Presets\" folder. A \"Textures\" folder next to it is used as well.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                }
                HStack(spacing: 6) {
                    Button(model.milkFolder == nil ? "Choose Folder…" : "Change…") { model.chooseMilkFolder() }
                    if model.milkFolder != nil {
                        Button("Open") { model.openMilkFolder() }
                        Button("Rescan") { model.refreshMilkdrop() }
                        Button("Remove") { model.removeMilkFolder() }
                    }
                }
                .buttonStyle(RaisedKeyStyle(look: .normal, height: 26))
                .font(.caption)
                if model.milkFolder != nil {
                    let count = model.milkCatalog.userCount
                    if model.milkScanning {
                        Text("Looking for presets…").font(.caption).foregroundStyle(.secondary)
                    } else if count == 0 {
                        Text("No .milk files found in this folder.").font(.caption).foregroundStyle(.secondary)
                    } else {
                        Toggle("Include these \(count) presets in auto-cycle\(model.milkCatalog.userTruncated ? " (first \(count))" : "")",
                               isOn: $settings.cycleUserPresets)
                            .toggleStyle(.checkbox).font(.caption)
                        TextField("Search presets", text: $milkFilter)
                            .textFieldStyle(.roundedBorder).font(.caption)
                    }
                }
            }
            .padding(.horizontal, 6)
            if model.milkFolder != nil, !model.milkScanning, start < scenes.count {
                let needle = milkFilter.trimmingCharacters(in: .whitespaces).lowercased()
                let shown = (start..<scenes.count).filter { needle.isEmpty || scenes[$0].name.lowercased().contains(needle) }
                ForEach(shown, id: \.self) { index in
                    styleRow(scenes[index], index: index, current: current, checkbox: false)
                        .help(index - VisualScene.builtInCount < model.milkCatalog.presets.count
                              ? model.milkCatalog.presets[index - VisualScene.builtInCount].url.path : "")
                }
                if shown.isEmpty {
                    Text("No presets match.").font(.caption).foregroundStyle(.secondary).padding(.horizontal, 6)
                }
            }
        }
    }

    // MARK: Updates

    private var updatesPanel: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                RoundedRectangle(cornerRadius: 2).fill(Palette3D.pink).frame(width: 4, height: 17)
                Text("Updates").font(.system(size: 15, weight: .semibold)).foregroundStyle(.white)
            }
            .padding([.top, .horizontal], 14)
            if let update = updates.available, updates.showBadge || updates.isBusy {
                updatePopover(update)
                Divider().padding(.horizontal, 14)
            } else {
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
        .background(vertical ? Color.clear : Palette3D.panel)
        .buttonStyle(RaisedKeyStyle(height: 28))
        .toggleStyle(Switch3DStyle())
    }

    // MARK: Recording

    private func recordButton(compact: Bool, letters: Bool = false) -> some View {
        Button {
            model.toggleRecording()
        } label: {
            HStack(spacing: 6) {
                Image(systemName: model.isRecording ? "stop.fill" : "circle.fill")
                    .font(.system(size: model.isRecording ? 10 : 11))
                    .foregroundStyle(model.isRecording ? Color.white : Palette3D.red)
                if model.isRecording {
                    Text(letters ? VisualizerModel.timeLabel(model.recordingElapsed)
                                 : "Stop  \(VisualizerModel.timeLabel(model.recordingElapsed))")
                        .monospacedDigit()
                } else if !letters {
                    Text("Record")
                }
            }
        }
        .buttonStyle(RaisedKeyStyle(look: model.isRecording ? .recording : .normal, height: compact ? 36 : 34,
                                    minWidth: 36, fill: !compact))
        .help(model.isRecording ? "Stop recording and save the video (R)"
                                : "Record the visuals and music to a video (R)")
    }

    private var recordSettings: some View {
        PopoverForm(title: "Recording") {
            Text("Video size").font(.caption)
            Segmented3D(selection: $settings.recordQuality,
                        options: Recorder.Quality.allCases.map { (label: $0.label, value: $0.rawValue) })
                .disabled(model.isRecording)
            if let q = Recorder.Quality(rawValue: settings.recordQuality) {
                Text(recordSizeLabel(q)).font(.caption2).foregroundStyle(.secondary)
            }
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
        PopoverForm(title: "Motion", subtitle: "Your logo's moves, driven by the bass") {
            Well {
                slider("Pulse", $settings.pulse, 0...1.5)
                slider("Squash & stretch", $settings.squash, 0...1.5)
                slider("Bounce", $settings.bounce, 0...1.5)
                slider("Slide", $settings.slide, 0...1.5)
                slider("Wobble", $settings.wobble, 0...1.5)
            }
            Button("Reset to defaults") { settings.resetMotion() }
        }
    }

    private func logoLookControls(withItems: Bool) -> some View {
        PopoverForm(title: withItems ? "Logos and text" : "Logo", width: withItems ? 360 : 300) {
            if withItems {
                itemsEditor
                Divider()
            }
            Well {
                slider("Size", $settings.logoSize, 0.08...0.8)
                slider("Glow", $settings.glow, 0...1.5)
                slider("Color tint", $settings.logoTint, 0...1)
            }
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
        PopoverForm(title: "Audio", subtitle: "What Dropocalypse listens to") {
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
            Well {
                slider("Sensitivity", $settings.sensitivity, 0.2...4)
                slider("Beat threshold", $settings.beatThreshold, 1.1...2.2) { String(format: "%.2f×", $0) }
            }
            Text(capture.statusText)
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .onAppear { capture.refreshDevices() }
    }

    private var lookControls: some View {
        PopoverForm(title: "Look") {
            Well {
                slider("Beat flash", $settings.beatFlash, 0...1)
                slider("Brightness", $settings.brightness, 0.4...1.6)
            }
            Text("Frame").font(.caption)
            Segmented3D(selection: $settings.pictureOrientation,
                        options: [(label: "Landscape", value: 0), (label: "Portrait (9:16)", value: 1)])
                .disabled(model.isRecording)
            Text(settings.pictureOrientation == 0
                 ? "Landscape fills the window."
                 : "Portrait is a 9:16 frame, centered on screen. Recordings are 720×1280, 1080×1920, or 2160×3840.")
                .font(.caption2).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text("Frame rate").font(.caption)
            Segmented3D(selection: $settings.frameCap,
                        options: [(label: "Display", value: 0), (label: "60", value: 60), (label: "30", value: 30)])
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

    /// Open/close binding for one of the toolbar's drop-downs.
    private func popoverBinding(_ id: String) -> Binding<Bool> {
        Binding<Bool>(
            get: { model.openPopover == id },
            set: { open in
                if open { model.openPopover = id } else if model.openPopover == id { model.openPopover = nil }
            })
    }

    /// A square raised key with a bold arrow.
    private func arrowKey(_ icon: String, help: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon).font(.system(size: 15, weight: .bold))
        }
        .buttonStyle(RaisedKeyStyle(height: 36, minWidth: 36))
        .help(help)
    }

    /// A toolbar key that opens a drop-down: the full word, or one letter on small screens.
    private func menuKey<Content: View>(_ word: String, letter: String, name: String, id: String, letters: Bool,
                                        look: KeyLook = .normal, icon: String? = nil, width: CGFloat? = nil,
                                        @ViewBuilder content: @escaping () -> Content) -> some View {
        let isOpen = popoverBinding(id)
        return Button {
            isOpen.wrappedValue.toggle()
        } label: {
            if let icon {
                Image(systemName: icon).font(.system(size: 11, weight: .bold))
            } else {
                Text(letters ? letter : word)
                    .font(.system(size: letters ? 15 : 13, weight: letters ? .semibold : .medium))
            }
        }
        .buttonStyle(RaisedKeyStyle(look: isOpen.wrappedValue ? .on : look, height: 36,
                                    minWidth: width ?? 36, caret: icon == nil))
        .frame(width: width)
        .help(name)
        .popover(isPresented: isOpen, arrowEdge: .bottom) {
            content().environment(\.colorScheme, .dark)
        }
    }

    private func slider(_ title: String, _ value: Binding<Double>, _ range: ClosedRange<Double>,
                        format: @escaping (Double) -> String = { String(format: "%.2f", $0) }) -> some View {
        Fader(title, value: value, range: range, format: format)
    }
}

/// Every drop-down's contents use the 3D menu panel.
private typealias PopoverForm<Content: View> = MenuPanel<Content>
