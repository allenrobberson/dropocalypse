import AppKit
import CoreAudio
import AudioToolbox

/// An app that can play audio, for the "Mac audio" sources in the input picker.
struct AudioApp: Identifiable, Hashable {
    /// Bundle ID of the app (helper processes are folded into their parent app).
    let id: String
    let name: String
    let isPlaying: Bool
}

struct TapError: Error, CustomStringConvertible {
    let description: String
    init(_ what: String, _ status: OSStatus = noErr) {
        description = status == noErr ? what : "\(what) (error \(status))"
    }
}

/// Captures what the Mac is playing — every app, or just the ones you pick — with a
/// Core Audio process tap (macOS 14.2+). No loopback driver, no extra software.
///
/// How it works: a `CATapDescription` describes which processes to listen to, the HAL turns it
/// into a tap object, and a private aggregate device wraps that tap so we can read it with an
/// ordinary IOProc. The IOProc runs directly on the HAL's real-time thread, just like the
/// microphone path, so latency stays at one hardware buffer.
@available(macOS 14.2, *)
final class ProcessTap {
    private(set) var sampleRate: Double = 48_000
    private(set) var bufferFrames: UInt32 = 0
    let processes: [AudioObjectID]?

    private var tapID = AudioObjectID(kAudioObjectUnknown)
    private var aggregateID = AudioObjectID(kAudioObjectUnknown)
    private var procID: AudioDeviceIOProcID?

    static let aggregateUIDPrefix = "io.github.allenrobberson.dropocalypse.tap"

    /// `processes == nil` taps everything the Mac plays (except Dropocalypse itself).
    init(processes: [AudioObjectID]?) throws {
        self.processes = processes
        let description: CATapDescription
        if let processes {
            description = CATapDescription(stereoMixdownOfProcesses: processes)
        } else {
            description = CATapDescription(stereoGlobalTapButExcludeProcesses: Self.ownProcessObject().map { [$0] } ?? [])
        }
        description.uuid = UUID()
        description.name = "Dropocalypse"
        description.isPrivate = true
        description.muteBehavior = .unmuted          // you still hear everything normally

        var tap = AudioObjectID(kAudioObjectUnknown)
        var status = AudioHardwareCreateProcessTap(description, &tap)
        guard status == noErr, tap != kAudioObjectUnknown else {
            throw TapError("Couldn't create the audio tap", status)
        }
        tapID = tap

        guard let outputUID = Self.defaultOutputUID() else {
            destroy()
            throw TapError("No audio output device found")
        }

        let aggregate: [String: Any] = [
            kAudioAggregateDeviceNameKey: "Dropocalypse Tap",
            kAudioAggregateDeviceUIDKey: "\(Self.aggregateUIDPrefix).\(UUID().uuidString)",
            kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: outputUID]],
            kAudioAggregateDeviceTapListKey: [[
                kAudioSubTapDriftCompensationKey: true,
                kAudioSubTapUIDKey: description.uuid.uuidString,
            ]],
        ]
        var agg = AudioObjectID(kAudioObjectUnknown)
        status = AudioHardwareCreateAggregateDevice(aggregate as CFDictionary, &agg)
        guard status == noErr, agg != kAudioObjectUnknown else {
            destroy()
            throw TapError("Couldn't create the capture device", status)
        }
        aggregateID = agg

        if let format = Self.tapFormat(tap) {
            guard format.mFormatID == kAudioFormatLinearPCM,
                  format.mFormatFlags & kAudioFormatFlagIsFloat != 0,
                  format.mBitsPerChannel == 32 else {
                destroy()
                throw TapError("Unexpected audio format from the tap")
            }
            if format.mSampleRate > 0 { sampleRate = format.mSampleRate }
        }
    }

    deinit { destroy() }

    /// Starts delivering audio. `handler` runs on the real-time audio thread: it must not
    /// allocate, lock or block.
    func start(preferredBufferFrames: UInt32,
               handler: @escaping (UnsafePointer<AudioBufferList>, Int) -> Void) throws {
        AudioCapture.setBufferFrameSize(preferredBufferFrames, on: aggregateID)
        bufferFrames = Self.getUInt32(aggregateID, kAudioDevicePropertyBufferFrameSize) ?? 0

        var proc: AudioDeviceIOProcID?
        // nil queue = call the block directly on the HAL's I/O thread (lowest latency).
        var status = AudioDeviceCreateIOProcIDWithBlock(&proc, aggregateID, nil) { _, input, _, _, _ in
            let list = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: input))
            guard list.count > 0 else { return }
            let first = list[0]
            let channels = max(1, Int(first.mNumberChannels))
            let frames = Int(first.mDataByteSize) / (MemoryLayout<Float>.size * channels)
            if frames > 0 { handler(input, frames) }
        }
        guard status == noErr, let proc else { throw TapError("Couldn't attach to the tap", status) }
        procID = proc
        status = AudioDeviceStart(aggregateID, proc)
        guard status == noErr else { throw TapError("Couldn't start capturing Mac audio", status) }
    }

    func stop() { destroy() }

    private func destroy() {
        if aggregateID != kAudioObjectUnknown {
            if let procID {
                AudioDeviceStop(aggregateID, procID)
                AudioDeviceDestroyIOProcID(aggregateID, procID)
            }
            AudioHardwareDestroyAggregateDevice(aggregateID)
        }
        if tapID != kAudioObjectUnknown { AudioHardwareDestroyProcessTap(tapID) }
        procID = nil
        aggregateID = AudioObjectID(kAudioObjectUnknown)
        tapID = AudioObjectID(kAudioObjectUnknown)
    }

    // MARK: Finding apps

    /// Every regular app that's open on the Mac, the ones playing sound right now first.
    /// Apps that haven't made a sound yet are listed too: pick one and capture starts the
    /// moment it plays.
    static func audioApps() -> [AudioApp] {
        var playing: [String: Bool] = [:]
        for obj in processObjects() {
            guard let key = appKey(for: obj) else { continue }
            let running = (getUInt32(obj, kAudioProcessPropertyIsRunningOutput) ?? 0) != 0
            playing[key] = (playing[key] ?? false) || running
        }
        let me = Bundle.main.bundleIdentifier
        var seen = Set<String>()
        var apps: [AudioApp] = []
        for app in NSWorkspace.shared.runningApplications where app.activationPolicy == .regular {
            guard let key = app.bundleIdentifier, key != me, app.processIdentifier != getpid(),
                  !seen.contains(key) else { continue }
            seen.insert(key)
            apps.append(AudioApp(id: key, name: app.localizedName ?? key, isPlaying: playing[key] ?? false))
        }
        return apps.sorted { ($0.isPlaying ? 0 : 1, $0.name.lowercased()) < ($1.isPlaying ? 0 : 1, $1.name.lowercased()) }
    }

    /// Core Audio process objects belonging to the app (including its helper processes).
    static func processObjects(forApp key: String) -> [AudioObjectID] {
        processObjects().filter { appKey(for: $0) == key }
    }

    /// Friendly name of a running app, or nil if it isn't a regular app (daemons, our own process).
    static func appName(for key: String) -> String? {
        let app = NSWorkspace.shared.runningApplications.first {
            $0.bundleIdentifier == key && $0.activationPolicy == .regular
        }
        return app?.localizedName
    }

    /// Folds helper processes into their parent app: "com.google.Chrome.helper.renderer" → "com.google.Chrome".
    private static func appKey(for process: AudioObjectID) -> String? {
        guard let pid = getInt32(process, kAudioProcessPropertyPID), pid != getpid() else { return nil }
        guard var bundle = AudioCapture.stringProperty(process, kAudioProcessPropertyBundleID), !bundle.isEmpty else {
            return NSRunningApplication(processIdentifier: pid)?.bundleIdentifier
        }
        if let r = bundle.range(of: ".helper", options: .caseInsensitive) { bundle = String(bundle[..<r.lowerBound]) }
        return bundle
    }

    private static func processObjects() -> [AudioObjectID] {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyProcessObjectList,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        let system = AudioObjectID(kAudioObjectSystemObject)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &addr, 0, nil, &size) == noErr, size > 0 else { return [] }
        var ids = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
        guard AudioObjectGetPropertyData(system, &addr, 0, nil, &size, &ids) == noErr else { return [] }
        return ids
    }

    private static func ownProcessObject() -> AudioObjectID? {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyTranslatePIDToProcessObject,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var pid = getpid()
        var obj = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        let status = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr,
                                                UInt32(MemoryLayout<pid_t>.size), &pid, &size, &obj)
        return status == noErr && obj != kAudioObjectUnknown ? obj : nil
    }

    // MARK: Property helpers

    private static func defaultOutputUID() -> String? {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultSystemOutputDevice,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var device = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &device) == noErr,
              device != 0 else { return nil }
        return AudioCapture.stringProperty(device, kAudioDevicePropertyDeviceUID)
    }

    private static func tapFormat(_ tap: AudioObjectID) -> AudioStreamBasicDescription? {
        var addr = AudioObjectPropertyAddress(mSelector: kAudioTapPropertyFormat,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var format = AudioStreamBasicDescription()
        var size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        return AudioObjectGetPropertyData(tap, &addr, 0, nil, &size, &format) == noErr ? format : nil
    }

    static func getUInt32(_ obj: AudioObjectID, _ selector: AudioObjectPropertySelector) -> UInt32? {
        var addr = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: UInt32 = 0
        var size = UInt32(MemoryLayout<UInt32>.size)
        return AudioObjectGetPropertyData(obj, &addr, 0, nil, &size, &value) == noErr ? value : nil
    }

    private static func getInt32(_ obj: AudioObjectID, _ selector: AudioObjectPropertySelector) -> Int32? {
        var addr = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value: Int32 = 0
        var size = UInt32(MemoryLayout<Int32>.size)
        return AudioObjectGetPropertyData(obj, &addr, 0, nil, &size, &value) == noErr ? value : nil
    }
}

/// Watches for apps starting/stopping audio and for the default output changing.
@available(macOS 14.2, *)
final class TapWatcher {
    enum Change { case processes, output }
    private var listeners: [(AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []

    init(onChange: @escaping (Change) -> Void) {
        let watched: [(AudioObjectPropertySelector, Change)] = [
            (kAudioHardwarePropertyProcessObjectList, .processes),
            (kAudioHardwarePropertyDefaultSystemOutputDevice, .output),
            (kAudioHardwarePropertyDefaultOutputDevice, .output),
        ]
        for (selector, change) in watched {
            var addr = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                                                  mElement: kAudioObjectPropertyElementMain)
            let block: AudioObjectPropertyListenerBlock = { _, _ in onChange(change) }
            if AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &addr, .main, block) == noErr {
                listeners.append((addr, block))
            }
        }
    }

    deinit {
        for (addr, block) in listeners {
            var a = addr
            AudioObjectRemovePropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &a, .main, block)
        }
    }
}
