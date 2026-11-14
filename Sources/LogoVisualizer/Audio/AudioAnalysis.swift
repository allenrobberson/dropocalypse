import Foundation
import Accelerate

/// Shared state between the real-time audio thread (writer) and the render thread (reader).
///
/// Everything here is plain memory with single-writer semantics so the audio thread never
/// takes a lock or allocates. Word-sized loads/stores are atomic on Apple hardware, and the
/// visualizer tolerates reading a value that is one audio block old.
final class AudioAnalysis: @unchecked Sendable {
    static let ringSize = 4096 // must be a power of two
    private static let ringMask = ringSize - 1

    // Raw mono samples for waveform + spectrum drawing.
    let ring: UnsafeMutablePointer<Float>
    private let writeIndex: UnsafeMutablePointer<Int>

    // Published features.
    private let features: UnsafeMutablePointer<Float>
    private let beatCounter: UnsafeMutablePointer<Int>

    // Settings written by the UI, read by the audio thread.
    private let settings: UnsafeMutablePointer<Float>

    private enum F: Int, CaseIterable { case bass, bassNorm, mid, midNorm, treble, trebleNorm, level, beatStrength }
    private enum S: Int, CaseIterable { case sensitivity, beatThreshold }

    init() {
        ring = .allocate(capacity: Self.ringSize)
        ring.initialize(repeating: 0, count: Self.ringSize)
        writeIndex = .allocate(capacity: 1)
        writeIndex.initialize(to: 0)
        features = .allocate(capacity: F.allCases.count)
        features.initialize(repeating: 0, count: F.allCases.count)
        beatCounter = .allocate(capacity: 1)
        beatCounter.initialize(to: 0)
        settings = .allocate(capacity: S.allCases.count)
        settings.initialize(repeating: 1, count: S.allCases.count)
        settings[S.beatThreshold.rawValue] = 1.35
    }

    deinit {
        ring.deallocate(); writeIndex.deallocate(); features.deallocate()
        beatCounter.deallocate(); settings.deallocate()
    }

    // MARK: Settings (UI thread)

    var sensitivity: Float {
        get { settings[S.sensitivity.rawValue] }
        set { settings[S.sensitivity.rawValue] = max(0.05, newValue) }
    }

    /// How far above its recent average the bass must jump to count as a beat (1.1 – 2.0).
    var beatThreshold: Float {
        get { settings[S.beatThreshold.rawValue] }
        set { settings[S.beatThreshold.rawValue] = newValue }
    }

    // MARK: Reader API (render thread)

    struct Snapshot {
        var bass: Float = 0        // normalized 0…~1.5, instant attack
        var mid: Float = 0
        var treble: Float = 0
        var level: Float = 0       // raw RMS-ish level, 0…1
        var beatCount: Int = 0     // increments on every detected beat
        var beatStrength: Float = 0
    }

    func snapshot() -> Snapshot {
        Snapshot(bass: features[F.bassNorm.rawValue],
                 mid: features[F.midNorm.rawValue],
                 treble: features[F.trebleNorm.rawValue],
                 level: features[F.level.rawValue],
                 beatCount: beatCounter.pointee,
                 beatStrength: features[F.beatStrength.rawValue])
    }

    /// Copies the most recent `count` samples (oldest first) into `out`.
    func copyRecentSamples(into out: UnsafeMutablePointer<Float>, count: Int) {
        let end = writeIndex.pointee
        var idx = (end - count) & Self.ringMask
        for i in 0..<count {
            out[i] = ring[idx]
            idx = (idx + 1) & Self.ringMask
        }
    }

    // MARK: Writer API (audio thread)

    @inline(__always) func push(sample: Float) {
        let i = writeIndex.pointee
        ring[i] = sample
        writeIndex.pointee = (i + 1) & Self.ringMask
    }

    @inline(__always) func publish(bass: Float, bassNorm: Float, mid: Float, midNorm: Float,
                                   treble: Float, trebleNorm: Float, level: Float) {
        features[F.bass.rawValue] = bass
        features[F.bassNorm.rawValue] = bassNorm
        features[F.mid.rawValue] = mid
        features[F.midNorm.rawValue] = midNorm
        features[F.treble.rawValue] = treble
        features[F.trebleNorm.rawValue] = trebleNorm
        features[F.level.rawValue] = level
    }

    @inline(__always) func publishBeat(strength: Float) {
        features[F.beatStrength.rawValue] = strength
        beatCounter.pointee &+= 1
    }
}

// MARK: - DSP building blocks

/// RBJ cookbook biquad (transposed direct form II).
struct Biquad {
    private var b0: Float = 1, b1: Float = 0, b2: Float = 0, a1: Float = 0, a2: Float = 0
    private var z1: Float = 0, z2: Float = 0

    static func lowPass(cutoff: Double, sampleRate: Double, q: Double = 0.7071) -> Biquad {
        var f = Biquad()
        let w0 = 2 * Double.pi * cutoff / sampleRate
        let alpha = sin(w0) / (2 * q)
        let cosw = cos(w0)
        let a0 = 1 + alpha
        f.b0 = Float(((1 - cosw) / 2) / a0)
        f.b1 = Float((1 - cosw) / a0)
        f.b2 = f.b0
        f.a1 = Float((-2 * cosw) / a0)
        f.a2 = Float((1 - alpha) / a0)
        return f
    }

    @inline(__always) mutating func process(_ x: Float) -> Float {
        let y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        return y
    }
}

/// Peak envelope follower with near-instant attack and a smooth release.
struct EnvelopeFollower {
    private var env: Float = 0
    private let attack: Float
    private let release: Float

    init(attackMs: Double, releaseMs: Double, sampleRate: Double) {
        attack = Float(1 - exp(-1 / (attackMs * 0.001 * sampleRate)))
        release = Float(1 - exp(-1 / (releaseMs * 0.001 * sampleRate)))
    }

    @inline(__always) mutating func process(_ x: Float) -> Float {
        let r = abs(x)
        env += (r - env) * (r > env ? attack : release)
        return env
    }

    var value: Float { env }
}

/// Slow automatic gain control so quiet rooms and loud line-ins both drive the full range.
struct AutoGain {
    private var peak: Float = 0.02
    private let decay: Float

    init(halfLifeSeconds: Double, sampleRate: Double) {
        decay = Float(pow(0.5, 1 / (halfLifeSeconds * sampleRate)))
    }

    @inline(__always) mutating func normalize(_ env: Float, floor: Float) -> Float {
        peak = max(env, peak * decay)
        return env / max(peak, floor)
    }
}

/// Per-sample feature extractor that runs entirely on the audio thread. No allocations.
///
/// Bass is isolated with a 4th-order low-pass at ~140 Hz and tracked with a sample-accurate
/// envelope follower, so a kick drum shows up in the published value within a few
/// milliseconds instead of waiting for an FFT window to fill.
struct FeatureExtractor {
    private var bassLP1: Biquad, bassLP2: Biquad
    private var midLP1: Biquad, midLP2: Biquad
    private var bassEnv: EnvelopeFollower
    private var midEnv: EnvelopeFollower
    private var trebleEnv: EnvelopeFollower
    private var levelEnv: EnvelopeFollower
    private var bassAGC: AutoGain, midAGC: AutoGain, trebleAGC: AutoGain

    // Beat detection state (evaluated once per audio block).
    private var bassAverage: Float = 0
    private let averageCoef: Float
    private var lastBass: Float = 0
    private var samplesSinceBeat: Int = 0
    private let refractorySamples: Int
    private let sampleRate: Double

    init(sampleRate: Double) {
        self.sampleRate = sampleRate
        bassLP1 = .lowPass(cutoff: 140, sampleRate: sampleRate)
        bassLP2 = .lowPass(cutoff: 140, sampleRate: sampleRate)
        midLP1 = .lowPass(cutoff: 2500, sampleRate: sampleRate)
        midLP2 = .lowPass(cutoff: 2500, sampleRate: sampleRate)
        bassEnv = EnvelopeFollower(attackMs: 1, releaseMs: 110, sampleRate: sampleRate)
        midEnv = EnvelopeFollower(attackMs: 1, releaseMs: 90, sampleRate: sampleRate)
        trebleEnv = EnvelopeFollower(attackMs: 0.5, releaseMs: 70, sampleRate: sampleRate)
        levelEnv = EnvelopeFollower(attackMs: 2, releaseMs: 200, sampleRate: sampleRate)
        bassAGC = AutoGain(halfLifeSeconds: 3, sampleRate: sampleRate)
        midAGC = AutoGain(halfLifeSeconds: 3, sampleRate: sampleRate)
        trebleAGC = AutoGain(halfLifeSeconds: 3, sampleRate: sampleRate)
        averageCoef = Float(1 - exp(-1 / (0.6 * sampleRate))) // ~0.6 s running average
        refractorySamples = Int(0.14 * sampleRate)             // max ~7 beats/sec
        samplesSinceBeat = refractorySamples
    }

    /// Processes one block of mono samples and publishes features.
    mutating func process(_ samples: UnsafePointer<Float>, count: Int, into out: AudioAnalysis) {
        let sens = out.sensitivity
        let floor = 0.004 / sens // noise floor: below this, things stay calm
        var b: Float = 0, m: Float = 0, t: Float = 0, l: Float = 0
        var bn: Float = 0, mn: Float = 0, tn: Float = 0

        for i in 0..<count {
            let x = samples[i]
            out.push(sample: x)

            let low = bassLP2.process(bassLP1.process(x))
            let belowMid = midLP2.process(midLP1.process(x))
            let midBand = belowMid - low
            let highBand = x - belowMid

            b = bassEnv.process(low)
            m = midEnv.process(midBand)
            t = trebleEnv.process(highBand)
            l = levelEnv.process(x)

            bn = bassAGC.normalize(b, floor: floor)
            mn = midAGC.normalize(m, floor: floor * 0.6)
            tn = trebleAGC.normalize(t, floor: floor * 0.4)

            // Running average for beat detection, per sample so it is block-size independent.
            bassAverage += (b - bassAverage) * averageCoef
        }

        out.publish(bass: b, bassNorm: bn, mid: m, midNorm: mn, treble: t, trebleNorm: tn,
                    level: min(1, l * 2))

        // Onset detection: bass jumps well above its recent average and is still rising.
        samplesSinceBeat += count
        let threshold = out.beatThreshold
        if samplesSinceBeat >= refractorySamples,
           b > floor,
           b > bassAverage * threshold,
           b > lastBass {
            let strength = min(1, max(0.25, (b / max(bassAverage, 1e-6) - 1) / 1.5))
            out.publishBeat(strength: strength)
            samplesSinceBeat = 0
        }
        lastBass = b
    }
}
