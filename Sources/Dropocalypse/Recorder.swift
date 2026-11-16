import AVFoundation
import CoreVideo
import Metal
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

    func start(url: URL, quality: Quality, device: MTLDevice) throws {
        let (w, h) = quality.size
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
/// about recording. Interleaves to stereo in a reusable buffer.
final class RecordingAudioTap: @unchecked Sendable {
    static let shared = RecordingAudioTap()
    private let lock = NSLock()
    private var recorder: Recorder?
    private var scratch = [Float](repeating: 0, count: 16384)
    private var pending = [Float]()        // batched to ~20 ms chunks to keep the writer light
    private var pendingRate: Double = 0

    func attach(_ recorder: Recorder?) {
        lock.lock()
        self.recorder = recorder
        pending.removeAll(keepingCapacity: true)
        lock.unlock()
    }

    func feed(_ list: UnsafeMutableAudioBufferListPointer, frameCount: Int, sampleRate: Double) {
        lock.lock()
        defer { lock.unlock() }
        guard let recorder, frameCount > 0 else { return }
        if scratch.count < frameCount * 2 { scratch = [Float](repeating: 0, count: frameCount * 2) }
        // Collect up to two channels from deinterleaved or interleaved buffers.
        var channels: [(UnsafeMutablePointer<Float>, Int, Int)] = []   // (data, stride, offset)
        for buffer in list {
            guard let data = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
            let n = max(1, Int(buffer.mNumberChannels))
            for c in 0..<n where channels.count < 2 { channels.append((data, n, c)) }
        }
        guard let left = channels.first else { return }
        let right = channels.count > 1 ? channels[1] : left
        if pendingRate != sampleRate { pending.removeAll(keepingCapacity: true); pendingRate = sampleRate }
        pending.reserveCapacity(4096 * 2)
        for i in 0..<frameCount {
            pending.append(left.0[i * left.1 + left.2])
            pending.append(right.0[i * right.1 + right.2])
        }
        let chunk = max(256, Int(sampleRate * 0.02))
        if pending.count / 2 >= chunk {
            pending.withUnsafeBufferPointer { buf in
                recorder.appendAudio(buf.baseAddress!, frames: buf.count / 2, sampleRate: sampleRate)
            }
            pending.removeAll(keepingCapacity: true)
        }
    }
}
