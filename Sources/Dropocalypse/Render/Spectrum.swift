import Accelerate

/// Spectrum + waveform for drawing. Runs on the render thread from the audio ring buffer.
/// (Logo motion does not wait on this: it uses the per-sample bass envelope.)
final class Spectrum {
    static let fftSize = 1024
    static let bandCount = 48
    static let waveCount = 512

    private let log2n = vDSP_Length(10)
    private let setup: FFTSetup
    private var window = [Float](repeating: 0, count: fftSize)
    private var samples = [Float](repeating: 0, count: fftSize)
    private var windowed = [Float](repeating: 0, count: fftSize)
    private var overallPeak: Float = 1e-3
    private var real = [Float](repeating: 0, count: fftSize / 2)
    private var imag = [Float](repeating: 0, count: fftSize / 2)
    private var magnitudes = [Float](repeating: 0, count: fftSize / 2)
    private var bandPeaks = [Float](repeating: 1e-4, count: bandCount)
    private var bandEdges: [Int] = []
    private var wavePeak: Float = 0.05

    private(set) var bands = [Float](repeating: 0, count: bandCount)   // 0…1, smoothed
    private(set) var wave = [Float](repeating: 0, count: waveCount)    // −1…1, auto-scaled

    init(sampleRate: Double = 48_000) {
        setup = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2))!
        vDSP_hann_window(&window, vDSP_Length(Self.fftSize), Int32(vDSP_HANN_NORM))
        configure(sampleRate: sampleRate)
    }

    deinit { vDSP_destroy_fftsetup(setup) }

    func configure(sampleRate: Double) {
        let sr = sampleRate > 0 ? sampleRate : 48_000
        let binHz = sr / Double(Self.fftSize)
        let lo = 35.0, hi = min(16_000.0, sr / 2 - binHz)
        bandEdges = (0...Self.bandCount).map { i in
            let f = lo * pow(hi / lo, Double(i) / Double(Self.bandCount))
            return max(1, min(Self.fftSize / 2 - 1, Int(f / binHz)))
        }
    }

    func update(from analysis: AudioAnalysis) {
        samples.withUnsafeMutableBufferPointer { analysis.copyRecentSamples(into: $0.baseAddress!, count: Self.fftSize) }

        // Waveform: newest 512 samples, auto-scaled.
        var peak: Float = 0
        let offset = Self.fftSize - Self.waveCount
        for i in 0..<Self.waveCount { peak = max(peak, abs(samples[offset + i])) }
        wavePeak = max(peak, wavePeak * 0.995, 0.01)
        let inv = 1 / wavePeak
        for i in 0..<Self.waveCount { wave[i] = samples[offset + i] * inv }

        // FFT magnitudes.
        vDSP_vmul(samples, 1, window, 1, &windowed, 1, vDSP_Length(Self.fftSize))
        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBytes { raw in
                    vDSP_ctoz(raw.bindMemory(to: DSPComplex.self).baseAddress!, 2, &split, 1, vDSP_Length(Self.fftSize / 2))
                }
                vDSP_fft_zrip(setup, &split, 1, log2n, FFTDirection(FFT_FORWARD))
                vDSP_zvabs(&split, 1, &magnitudes, 1, vDSP_Length(Self.fftSize / 2))
            }
        }

        // Log-spaced bands with per-band auto gain, instant attack, smooth release.
        var frameMax: Float = 0
        for b in 0..<Self.bandCount {
            let a = bandEdges[b], e = max(bandEdges[b + 1], a + 1)
            for k in a..<e { frameMax = max(frameMax, magnitudes[k]) }
        }
        overallPeak = max(frameMax, overallPeak * 0.998, 1e-3)
        for b in 0..<Self.bandCount {
            let a = bandEdges[b], e = max(bandEdges[b + 1], a + 1)
            var sum: Float = 0
            for k in a..<e { sum = max(sum, magnitudes[k]) }
            bandPeaks[b] = max(sum, bandPeaks[b] * 0.997, overallPeak * 0.2)
            let v = min(1, sum / bandPeaks[b])
            bands[b] = v > bands[b] ? v : bands[b] * 0.86 + v * 0.14
        }
    }
}
