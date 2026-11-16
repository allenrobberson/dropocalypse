import AVFoundation
import CoreAudio
import AudioToolbox

struct AudioInputDevice: Identifiable, Hashable {
    let id: AudioDeviceID
    let uid: String
    let name: String
}

/// Captures audio from a microphone or line-in — or, on macOS 14.2+, straight from what the Mac
/// is playing (all apps, or one app such as Spotify) through a Core Audio process tap — with the smallest practical hardware buffer
/// and feeds it, on the real-time thread, straight into the feature extractor.
///
/// Uses `AVAudioSinkNode` rather than `installTap`: taps deliver audio in 100–400 ms chunks,
/// while a sink node receives each hardware I/O buffer (~5 ms at 256 frames / 48 kHz).
final class AudioCapture: ObservableObject {
    let analysis = AudioAnalysis()

    @Published private(set) var devices: [AudioInputDevice] = []
    /// Open apps you can listen to directly (macOS 14.2+).
    @Published private(set) var audioApps: [AudioApp] = []
    private var watchWork: DispatchWorkItem?
    private var pendingOutputChange = false
    /// True while the selected source is Mac audio rather than a mic or line-in.
    @Published private(set) var isTapSource = false
    @Published private(set) var statusText: String = "Starting…"
    @Published private(set) var isRunning = false
    @Published private(set) var permissionDenied = false
    @Published private(set) var ioBufferFrames: UInt32 = 0
    @Published private(set) var sampleRate: Double = 0

    /// Preferred hardware buffer size. 256 frames ≈ 5.3 ms at 48 kHz.
    var preferredBufferFrames: UInt32 = 128     // ~2.7 ms at 48 kHz; the HAL clamps to what the device allows

    private var engine: AVAudioEngine?
    private var configObserver: NSObjectProtocol?
    private var engineSignature = ""            // device + format the engine was built for
    private var configWork: DispatchWorkItem?
    private var lastRebuild = Date.distantPast
    private var extractorBox: ExtractorBox?
    private var selectedUID: String?
    private var tap: AnyObject?            // ProcessTap (macOS 14.2+)
    private var tapWatcher: AnyObject?     // TapWatcher (macOS 14.2+)
    private var tapProcesses: Set<AudioObjectID> = []
    private var restartWork: DispatchWorkItem?

    /// Picker tags for the Mac-audio sources.
    static let allMacAudioTag = "tap:all"
    static func appTag(_ bundleID: String) -> String { "tap:app:\(bundleID)" }

    /// Process taps need macOS 14.2 or later.
    static var macAudioSupported: Bool {
        if #available(macOS 14.2, *) { return true }
        return false
    }

    init() {
        refreshDevices()
    }

    // MARK: Devices

    func refreshDevices() {
        devices = Self.inputDevices()
        if #available(macOS 14.2, *) { audioApps = ProcessTap.audioApps() }
    }

    // MARK: Start / stop

    /// Starts capture from the device with the given UID, or the system default input if nil.
    func start(deviceUID: String?) {
        selectedUID = deviceUID
        if let uid = deviceUID, uid.hasPrefix("tap:") {
            startTap(uid)
            return
        }
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            startEngine()
        case .notDetermined:
            statusText = "Waiting for microphone permission…"
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                DispatchQueue.main.async {
                    if granted { self.startEngine() } else { self.markDenied() }
                }
            }
        default:
            markDenied()
        }
    }

    func stop() {
        restartWork?.cancel()
        restartWork = nil
        configWork?.cancel()
        configWork = nil
        if #available(macOS 14.2, *) { (tap as? ProcessTap)?.stop() }
        tap = nil
        tapWatcher = nil
        tapProcesses = []
        isTapSource = false
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        configObserver = nil
        engine?.stop()
        engine = nil
        extractorBox = nil
        isRunning = false
    }

    private func markDenied() {
        permissionDenied = true
        isRunning = false
        statusText = "Microphone access is off. Enable it in System Settings → Privacy & Security → Microphone."
    }

    private func startEngine() {
        stop()
        permissionDenied = false
        refreshDevices()

        let engine = AVAudioEngine()
        let input = engine.inputNode

        guard let unit = input.audioUnit else {
            statusText = "Couldn't open the audio input."
            return
        }

        // Pick the device.
        if let uid = selectedUID, let device = devices.first(where: { $0.uid == uid }) {
            var deviceID = device.id
            let status = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                              kAudioUnitScope_Global, 0, &deviceID,
                                              UInt32(MemoryLayout<AudioDeviceID>.size))
            if status != noErr {
                statusText = "Couldn't switch to \(device.name) (error \(status)); using default input."
            }
        }

        // Ask for a small hardware buffer. The HAL picks the smallest value any client asks for
        // within the device's allowed range, so this only ever makes things faster.
        var frames = preferredBufferFrames
        _ = AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize,
                                 kAudioUnitScope_Global, 0, &frames, UInt32(MemoryLayout<UInt32>.size))
        if let deviceID = Self.currentDevice(of: unit) {
            Self.setBufferFrameSize(frames, on: deviceID)
        }

        let format = input.outputFormat(forBus: 0)
        guard format.sampleRate > 0, format.channelCount > 0 else {
            statusText = "No audio input found. Plug in a mic or choose an input in Settings."
            return
        }

        let box = ExtractorBox(sampleRate: format.sampleRate, maxFrames: 8192)
        let analysis = self.analysis
        let sink = AVAudioSinkNode { _, frameCount, bufferList -> OSStatus in
            box.process(bufferList: bufferList, frameCount: Int(frameCount), into: analysis)
            return noErr
        }

        engine.attach(sink)
        engine.connect(input, to: sink, format: format)

        do {
            engine.prepare()
            try engine.start()
        } catch {
            statusText = "Audio failed to start: \(error.localizedDescription)"
            return
        }

        self.engine = engine
        self.extractorBox = box
        isRunning = true
        sampleRate = format.sampleRate

        var actual: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        if AudioUnitGetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &actual, &size) == noErr {
            ioBufferFrames = actual
        }

        let deviceName = Self.currentDevice(of: unit).flatMap { id in devices.first { $0.id == id }?.name } ?? "Default input"
        let ms = ioBufferFrames > 0 ? String(format: " · %.1f ms buffer", Double(ioBufferFrames) / format.sampleRate * 1000) : ""
        statusText = "Listening: \(deviceName) · \(Int(format.sampleRate)) Hz\(ms)"

        engineSignature = Self.signature(unit: unit, format: format)

        // Core Audio posts configuration changes for many reasons (other apps changing the shared
        // buffer size, power states, routing), sometimes several times a second for a while after
        // launch. Rebuilding the engine each time froze the visuals (it runs on the main thread),
        // so: coalesce the notices, just restart the engine if the device and format are the same,
        // and only rebuild (at most every 2 s) when they really changed.
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in
            guard let self else { return }
            self.configWork?.cancel()
            let work = DispatchWorkItem { [weak self] in self?.handleConfigChange() }
            self.configWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
        }
    }

    private static func signature(unit: AudioUnit, format: AVAudioFormat) -> String {
        "\(currentDevice(of: unit) ?? 0)|\(format.sampleRate)|\(format.channelCount)"
    }

    private func handleConfigChange() {
        guard let engine, let unit = engine.inputNode.audioUnit else { return }
        let format = engine.inputNode.outputFormat(forBus: 0)
        let same = format.sampleRate > 0 && Self.signature(unit: unit, format: format) == engineSignature
        if same {
            // Same device and format: the engine only needs to be running again.
            if !engine.isRunning {
                DispatchQueue.global(qos: .userInitiated).async { try? engine.start() }
            }
            return
        }
        let wait = 2 - Date().timeIntervalSince(lastRebuild)
        if wait > 0 {
            configWork?.cancel()
            let work = DispatchWorkItem { [weak self] in self?.handleConfigChange() }
            configWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + wait, execute: work)
            return
        }
        lastRebuild = Date()
        start(deviceUID: selectedUID)
    }

    // MARK: Mac audio (process tap)

    private func startTap(_ uid: String) {
        stop()
        permissionDenied = false
        isTapSource = true
        guard #available(macOS 14.2, *) else {
            statusText = "Listening to Mac audio needs macOS 14.2 or later. Use a mic or line-in instead."
            return
        }
        refreshDevices()

        var processes: [AudioObjectID]? = nil
        var label = "All Mac audio"
        if uid != Self.allMacAudioTag {
            let bundleID = String(uid.dropFirst("tap:app:".count))
            label = ProcessTap.appName(for: bundleID) ?? audioApps.first { $0.id == bundleID }?.name ?? bundleID
            let objects = ProcessTap.processObjects(forApp: bundleID)
            tapProcesses = Set(objects)
            if objects.isEmpty {
                statusText = ProcessTap.appName(for: bundleID) == nil
                    ? "\(label) isn't open. Open it and press play."
                    : "Waiting for \(label) to play something…"
                watchTap()
                return
            }
            processes = objects
        }

        do {
            let tap = try ProcessTap(processes: processes)
            let box = ExtractorBox(sampleRate: tap.sampleRate, maxFrames: 8192)
            let analysis = self.analysis
            try tap.start(preferredBufferFrames: preferredBufferFrames) { list, frames in
                box.process(bufferList: list, frameCount: frames, into: analysis)
            }
            self.tap = tap
            extractorBox = box
            isRunning = true
            sampleRate = tap.sampleRate
            ioBufferFrames = tap.bufferFrames
            let ms = tap.bufferFrames > 0 ? String(format: " · %.1f ms buffer", Double(tap.bufferFrames) / tap.sampleRate * 1000) : ""
            statusText = "Listening: \(label) · \(Int(tap.sampleRate)) Hz\(ms)"
        } catch {
            statusText = "Couldn't listen to Mac audio: \(error)"
        }
        watchTap()
    }

    /// Restart the tap when the chosen app starts/stops audio or the output device changes.
    @available(macOS 14.2, *)
    private func watchTap() {
        // Core Audio can report process changes many times a second (apps opening and closing
        // audio streams). Coalesce them and do the scanning off the main thread, so the main
        // thread, which also draws the visuals, never stalls.
        tapWatcher = TapWatcher { [weak self] change in
            guard let self else { return }
            if change == .output { self.pendingOutputChange = true }
            self.watchWork?.cancel()
            let work = DispatchWorkItem { [weak self] in self?.handleTapChange() }
            self.watchWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: work)
        }
    }

    @available(macOS 14.2, *)
    private func handleTapChange() {
        guard let uid = selectedUID, uid.hasPrefix("tap:") else { return }
        let outputChanged = pendingOutputChange
        pendingOutputChange = false
        let current = tapProcesses
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let apps = ProcessTap.audioApps()
            var processesChanged = false
            if uid != Self.allMacAudioTag {
                let bundleID = String(uid.dropFirst("tap:app:".count))
                processesChanged = Set(ProcessTap.processObjects(forApp: bundleID)) != current
            }
            DispatchQueue.main.async {
                guard let self, self.selectedUID == uid else { return }
                if apps != self.audioApps { self.audioApps = apps }
                if outputChanged || processesChanged { self.scheduleRestart() }
            }
        }
    }

    private func scheduleRestart() {
        restartWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.start(deviceUID: self.selectedUID)
        }
        restartWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: work)
    }

    // MARK: Core Audio helpers

    private static func currentDevice(of unit: AudioUnit) -> AudioDeviceID? {
        var id = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        let status = AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                          kAudioUnitScope_Global, 0, &id, &size)
        return status == noErr && id != 0 ? id : nil
    }

    static func setBufferFrameSize(_ frames: UInt32, on device: AudioDeviceID) {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyBufferFrameSize,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        // Setting it, even to the same value, makes Core Audio notify every client of a change.
        var current: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        if AudioObjectGetPropertyData(device, &addr, 0, nil, &size, &current) == noErr, current == frames { return }
        var value = frames
        _ = AudioObjectSetPropertyData(device, &addr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &value)
    }

    static func inputDevices() -> [AudioInputDevice] {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        let system = AudioObjectID(kAudioObjectSystemObject)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr else { return [] }
        var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return [] }

        return ids.compactMap { id -> AudioInputDevice? in
            guard inputChannelCount(id) > 0 else { return nil }
            let name = stringProperty(id, kAudioObjectPropertyName) ?? "Audio Input \(id)"
            let uid = stringProperty(id, kAudioDevicePropertyDeviceUID) ?? "\(id)"
            if uid.hasPrefix("io.github.allenrobberson.dropocalypse.tap") { return nil }   // our own tap device
            return AudioInputDevice(id: id, uid: uid, name: name)
        }
    }

    private static func inputChannelCount(_ id: AudioDeviceID) -> Int {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyStreamConfiguration,
                                              mScope: kAudioDevicePropertyScopeInput,
                                              mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size),
                                                   alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(id, &addr, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    static func stringProperty(_ id: AudioObjectID, _ selector: AudioObjectPropertySelector) -> String? {
        var addr = AudioObjectPropertyAddress(mSelector: selector,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var size = UInt32(MemoryLayout<CFString?>.size)
        var result: Unmanaged<CFString>?
        let status = withUnsafeMutablePointer(to: &result) { ptr in
            AudioObjectGetPropertyData(id, &addr, 0, nil, &size, ptr)
        }
        guard status == noErr, let value = result?.takeRetainedValue() else { return nil }
        return value as String
    }
}

/// Owns the feature extractor and a scratch buffer for mixing to mono, so the real-time
/// callback never allocates.
final class ExtractorBox: @unchecked Sendable {
    private var extractor: FeatureExtractor
    private let mono: UnsafeMutablePointer<Float>
    private let maxFrames: Int

    private let sampleRate: Double

    init(sampleRate: Double, maxFrames: Int) {
        self.sampleRate = sampleRate
        extractor = FeatureExtractor(sampleRate: sampleRate)
        self.maxFrames = maxFrames
        mono = .allocate(capacity: maxFrames)
        mono.initialize(repeating: 0, count: maxFrames)
    }

    deinit { mono.deallocate() }

    func process(bufferList: UnsafePointer<AudioBufferList>, frameCount: Int, into analysis: AudioAnalysis) {
        let list = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: bufferList))
        RecordingAudioTap.shared.feed(list, frameCount: frameCount, sampleRate: sampleRate)
        var offset = 0
        while offset < frameCount {
            let n = min(maxFrames, frameCount - offset)
            mixToMono(list, from: offset, count: n)
            extractor.process(mono, count: n, into: analysis)
            offset += n
        }
    }

    private func mixToMono(_ list: UnsafeMutableAudioBufferListPointer, from offset: Int, count: Int) {
        // Standard AVAudioEngine format is deinterleaved Float32: one buffer per channel.
        var channels = 0
        for i in 0..<count { mono[i] = 0 }
        for buffer in list {
            guard let data = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
            let stride = max(1, Int(buffer.mNumberChannels))
            for c in 0..<stride {
                for i in 0..<count { mono[i] += data[(offset + i) * stride + c] }
                channels += 1
            }
        }
        if channels > 1 {
            let scale = 1 / Float(channels)
            for i in 0..<count { mono[i] *= scale }
        }
    }
}
