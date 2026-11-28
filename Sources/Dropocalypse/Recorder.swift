import AVFoundation
import CoreVideo
import DropocalypseCore
import Metal
import os
import QuartzCore

/// Records the visuals (without the toolbar) and the music to an MP4 (H.264 + AAC).
///
/// Video: the renderer draws the finished frame, letterboxed, into a pixel buffer from the
/// writer's pool (a Metal texture backed by the same memory), and appends it when the GPU is done.
/// Audio: the capture callback hands over stereo float samples, which are appended as LPCM and
/// encoded to AAC by AVAssetWriter. Silence fills gaps so audio and video stay in sync.
final class Recorder: @unchecked Sendable {
    enum Quality: Int, CaseIterable, Identifiable {
        case p720 = 0, p1080 = 1, p2160 = 2
        var id: Int { rawValue }
        var label: String { ["720p", "1080p", "4K"][rawValue] }
        var size: (width: Int, height: Int) { [(1280, 720), (1920, 1080), (3840, 2160)][rawValue] }
        var fps: Int { self == .p2160 ? 30 : 60 }
        var bitrate: Int { [8_000_000, 16_000_000, 40_000_000][rawValue] }
        var detail: String { "\(label)  (\(size.width) × \(size.height), \(fps) fps)" }
    }

    private let lock = NSLock()
    private let writeQueue = DispatchQueue(label: "Dropocalypse.recorder")
    private var writer: AVAssetWriter?
    private var videoInput: AVAssetWriterInput?
    private var audioInput: AVAssetWriterInput?
    private var adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private var textureCache: CVMetalTextureCache?
    private(set) var url: URL?
    private var active = false
    private var startTime: CFTimeInterval = 0
    private var nextSlot: Double = 0   // time of the next video frame on a fixed fps grid
    private var fps = 60
    private(set) var width = 1920
    private(set) var height = 1080

    // Audio bookkeeping (on the audio thread, under `lock`).
    private var audioRate: Double = 0
    private var audioFramesWritten: Int64 = 0
    private var audioFormat: CMAudioFormatDescription?

    var isRecording: Bool { lock.lock(); defer { lock.unlock() }; return active }
    var elapsed: TimeInterval { isRecording ? CACurrentMediaTime() - startTime : 0 }

    func start(url: URL, quality: Quality, portrait: Bool, device: MTLDevice) throws {
        let pixels = FrameLayout.recordingSize(quality: quality.rawValue, portrait: portrait)
        let w = pixels.width
        let h = pixels.height
        width = w
        height = h
        fps = quality.fps
        try? FileManager.default.removeItem(at: url)
        let writer = try AVAssetWriter(outputURL: url, fileType: .mp4)

        let video = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: w,
            AVVideoHeightKey: h,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: quality.bitrate,
                AVVideoExpectedSourceFrameRateKey: quality.fps,
                AVVideoMaxKeyFrameIntervalKey: quality.fps * 2,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            ],
        ])
        video.expectsMediaDataInRealTime = true
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: video, sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: w,
            kCVPixelBufferHeightKey as String: h,
            kCVPixelBufferMetalCompatibilityKey as String: true,
            kCVPixelBufferIOSurfacePropertiesKey as String: [:],
        ])
        let audio = AVAssetWriterInput(mediaType: .audio, outputSettings: [
            AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVNumberOfChannelsKey: 2,
            AVSampleRateKey: 48_000,
            AVEncoderBitRateKey: 192_000,
        ])
        audio.expectsMediaDataInRealTime = true
        guard writer.canAdd(video), writer.canAdd(audio) else {
            throw NSError(domain: "Dropocalypse", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "This Mac can't encode video at this size."])
        }
        writer.add(video)
        writer.add(audio)
        guard writer.startWriting() else {
            throw writer.error ?? NSError(domain: "Dropocalypse", code: 2,
                                          userInfo: [NSLocalizedDescriptionKey: "Couldn't create the video file."])
        }
        writer.startSession(atSourceTime: .zero)

        var cache: CVMetalTextureCache?
        CVMetalTextureCacheCreate(nil, nil, device, nil, &cache)

        lock.lock()
        self.writer = writer
        videoInput = video
        audioInput = audio
        self.adaptor = adaptor
        textureCache = cache
        self.url = url
        audioRate = 0
        audioFramesWritten = 0
        audioFormat = nil
        nextSlot = 0
        startTime = CACurrentMediaTime()
        active = true
        lock.unlock()
    }

    /// Stops and finishes the file; `completion` runs on the main queue.
    func stop(completion: @escaping (URL?, Error?) -> Void) {
        lock.lock()
        guard active, let writer else { lock.unlock(); return }
        active = false
        let video = videoInput, audio = audioInput, url = self.url
        lock.unlock()
        writeQueue.async {
            video?.markAsFinished()
            audio?.markAsFinished()
            writer.finishWriting {
                let ok = writer.status == .completed
                DispatchQueue.main.async { completion(ok ? url : nil, ok ? nil : writer.error) }
            }
        }
    }

    // MARK: Video (render thread)

    struct FrameTarget {
        let pixelBuffer: CVPixelBuffer
        let texture: MTLTexture
        let cvTexture: CVMetalTexture
        let time: CFTimeInterval
    }

    /// A pixel buffer to draw this frame into, or nil if no frame is due (keeps the chosen fps).
    func nextFrameTarget(now: CFTimeInterval) -> FrameTarget? {
        lock.lock()
        defer { lock.unlock() }
        guard active, let adaptor, let pool = adaptor.pixelBufferPool, let cache = textureCache else { return nil }
        // Constant frame rate: frames sit exactly on a 1/fps grid whatever the screen's refresh.
        let t = now - startTime
        let step = 1.0 / Double(fps)
        if t < nextSlot - step * 0.5 { return nil }
        if t - nextSlot > step * 2 { nextSlot = (t / step).rounded(.down) * step }   // fell behind: catch up
        let slot = nextSlot
        var pb: CVPixelBuffer?
        guard CVPixelBufferPoolCreatePixelBuffer(nil, pool, &pb) == kCVReturnSuccess, let pixelBuffer = pb else { return nil }
        var cvTex: CVMetalTexture?
        CVMetalTextureCacheCreateTextureFromImage(nil, cache, pixelBuffer, nil, .bgra8Unorm, width, height, 0, &cvTex)
        guard let cvTexture = cvTex, let texture = CVMetalTextureGetTexture(cvTexture) else { return nil }
        nextSlot = slot + step
        return FrameTarget(pixelBuffer: pixelBuffer, texture: texture, cvTexture: cvTexture, time: slot)
    }

    /// Call when the GPU has finished drawing into the frame target.
    func appendFrame(_ frame: FrameTarget) {
        writeQueue.async { [self] in
            lock.lock()
            let input = videoInput, adaptor = self.adaptor, ok = active
            lock.unlock()
            guard ok, let input, let adaptor, input.isReadyForMoreMediaData else { return }
            adaptor.append(frame.pixelBuffer, withPresentationTime: CMTime(seconds: frame.time, preferredTimescale: 600))
            _ = frame.cvTexture // keep the Metal view of the buffer alive until here
        }
    }

    // MARK: Audio (audio thread)

    /// Interleaved stereo float samples at `sampleRate`.
    func appendAudio(_ samples: UnsafePointer<Float>, frames: Int, sampleRate: Double) {
        guard frames > 0, sampleRate > 0 else { return }
        lock.lock()
        guard active, let input = audioInput else { lock.unlock(); return }
        if audioRate != sampleRate || audioFormat == nil {
            audioRate = sampleRate
            var asbd = AudioStreamBasicDescription(
                mSampleRate: sampleRate, mFormatID: kAudioFormatLinearPCM,
                mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
                mBytesPerPacket: 8, mFramesPerPacket: 1, mBytesPerFrame: 8, mChannelsPerFrame: 2,
                mBitsPerChannel: 32, mReserved: 0)
            var fmt: CMAudioFormatDescription?
            CMAudioFormatDescriptionCreate(allocator: nil, asbd: &asbd, layoutSize: 0, layout: nil,
                                           magicCookieSize: 0, magicCookie: nil, extensions: nil,
                                           formatDescriptionOut: &fmt)
            audioFormat = fmt
            // Start this rate's timeline where the wall clock is.
            audioFramesWritten = Int64((CACurrentMediaTime() - startTime) * sampleRate)
        }
        // Keep audio continuous: if the source went quiet (no callbacks), jump to the wall clock.
        let wallFrames = Int64((CACurrentMediaTime() - startTime) * sampleRate)
        if wallFrames - audioFramesWritten > Int64(sampleRate * 0.25) {
            audioFramesWritten = wallFrames - Int64(frames)
        }
        let pts = CMTime(value: audioFramesWritten, timescale: CMTimeScale(sampleRate))
        audioFramesWritten += Int64(frames)
        let format = audioFormat
        lock.unlock()
        guard let format else { return }

        let bytes = frames * 8
        var block: CMBlockBuffer?
        guard CMBlockBufferCreateWithMemoryBlock(allocator: nil, memoryBlock: nil, blockLength: bytes,
                                                 blockAllocator: nil, customBlockSource: nil, offsetToData: 0,
                                                 dataLength: bytes, flags: 0, blockBufferOut: &block) == noErr,
              let block,
              CMBlockBufferReplaceDataBytes(with: samples, blockBuffer: block, offsetIntoDestination: 0,
                                            dataLength: bytes) == noErr else { return }
        var sample: CMSampleBuffer?
        guard CMAudioSampleBufferCreateReadyWithPacketDescriptions(
            allocator: nil, dataBuffer: block, formatDescription: format, sampleCount: frames,
            presentationTimeStamp: pts, packetDescriptions: nil, sampleBufferOut: &sample) == noErr,
              let sample else { return }
        writeQueue.async {
            if input.isReadyForMoreMediaData { input.append(sample) }
        }
    }
}

/// The audio callbacks feed the recorder through this, so AudioCapture doesn't need to know
/// about recording. The callbacks run on Core Audio's real-time thread, which must never wait,
/// allocate or call into CoreMedia: `feed` only copies samples into a fixed ring buffer (and
/// skips a buffer rather than wait if the lock is busy). A timer on its own queue drains the ring
/// every 20 ms and does the CoreMedia work there.
final class RecordingAudioTap: @unchecked Sendable {
    static let shared = RecordingAudioTap()

    private static let capacity = 1 << 20          // floats, stereo interleaved: about 5 s at 96 kHz
    private let lockPtr: UnsafeMutablePointer<os_unfair_lock>
    private let ring = UnsafeMutablePointer<Float>.allocate(capacity: RecordingAudioTap.capacity)
    private let drained = UnsafeMutablePointer<Float>.allocate(capacity: RecordingAudioTap.capacity)
    // Guarded by the lock. The indexes only grow; positions in the ring are index & (capacity - 1).
    private var writeIndex = 0
    private var readIndex = 0
    private var ringRate: Double = 0
    private var recorder: Recorder?

    private let drainQueue = DispatchQueue(label: "dropocalypse.recording-audio", qos: .userInitiated)
    private var timer: DispatchSourceTimer?    // drainQueue only

    private init() {
        lockPtr = .allocate(capacity: 1)
        lockPtr.initialize(to: os_unfair_lock())
    }

    func attach(_ recorder: Recorder?) {
        if recorder == nil {
            drainQueue.sync { drain() }   // the last few milliseconds go into the file too
        }
        os_unfair_lock_lock(lockPtr)
        self.recorder = recorder
        readIndex = writeIndex
        os_unfair_lock_unlock(lockPtr)
        drainQueue.async { [self] in
            timer?.cancel()
            timer = nil
            guard recorder != nil else { return }
            let t = DispatchSource.makeTimerSource(queue: drainQueue)
            t.schedule(deadline: .now() + .milliseconds(20), repeating: .milliseconds(20), leeway: .milliseconds(5))
            t.setEventHandler { [weak self] in self?.drain() }
            t.resume()
            timer = t
        }
    }

    /// Real-time audio thread: copies up to two channels into the ring. No allocation, no waiting.
    func feed(_ list: UnsafeMutableAudioBufferListPointer, frameCount: Int, sampleRate: Double) {
        guard frameCount > 0, os_unfair_lock_trylock(lockPtr) else { return }
        defer { os_unfair_lock_unlock(lockPtr) }
        guard recorder != nil else { return }
        // Find the left and right channels in deinterleaved or interleaved buffers.
        var left: (UnsafeMutablePointer<Float>, Int, Int)?
        var right: (UnsafeMutablePointer<Float>, Int, Int)?
        for buffer in list {
            guard let data = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
            let n = max(1, Int(buffer.mNumberChannels))
            for c in 0..<n {
                if left == nil { left = (data, n, c) } else if right == nil { right = (data, n, c) }
            }
            if right != nil { break }
        }
        guard let l = left else { return }
        let r = right ?? l
        if ringRate != sampleRate {   // a new input rate: start fresh rather than mix two rates
            ringRate = sampleRate
            readIndex = writeIndex
        }
        let mask = Self.capacity - 1
        let room = (Self.capacity - (writeIndex - readIndex)) / 2
        let frames = min(frameCount, room)
        var w = writeIndex
        for i in 0..<frames {
            ring[w & mask] = l.0[i * l.1 + l.2]
            ring[(w + 1) & mask] = r.0[i * r.1 + r.2]
            w += 2
        }
        writeIndex = w
    }

    /// drainQueue: hands what the ring holds to the recorder.
    private func drain() {
        os_unfair_lock_lock(lockPtr)
        let recorder = self.recorder, rate = ringRate
        let count = writeIndex - readIndex
        let mask = Self.capacity - 1
        let start = readIndex & mask
        let first = min(count, Self.capacity - start)
        drained.update(from: ring + start, count: first)
        if count > first { (drained + first).update(from: ring, count: count - first) }
        readIndex = writeIndex
        os_unfair_lock_unlock(lockPtr)
        guard let recorder, count >= 2, rate > 0 else { return }
        recorder.appendAudio(drained, frames: count / 2, sampleRate: rate)
    }
}
