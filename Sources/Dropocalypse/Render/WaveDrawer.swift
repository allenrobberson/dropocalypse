import Metal
import Foundation

/// Writes colored triangles straight into a shared Metal buffer. Coordinates are in
/// "height units": y runs −1…1 and x runs −aspect…aspect, so circles stay round.
struct GeometryBuilder {
    private let ptr: UnsafeMutablePointer<GeomVertex>
    private let capacity: Int
    let aspect: Float
    private(set) var count = 0

    init(buffer: MTLBuffer, capacity: Int, aspect: Float) {
        ptr = buffer.contents().bindMemory(to: GeomVertex.self, capacity: capacity)
        self.capacity = capacity
        self.aspect = aspect
    }

    typealias RGBA = (Float, Float, Float, Float)

    @inline(__always) private mutating func v(_ x: Float, _ y: Float, _ c: RGBA) {
        ptr[count] = GeomVertex(x: x / aspect, y: y, r: c.0, g: c.1, b: c.2, a: c.3)
        count += 1
    }

    mutating func triangle(_ a: SIMD2<Float>, _ b: SIMD2<Float>, _ c: SIMD2<Float>, _ col: RGBA) {
        guard count + 3 <= capacity else { return }
        v(a.x, a.y, col); v(b.x, b.y, col); v(c.x, c.y, col)
    }

    mutating func quad(_ a: SIMD2<Float>, _ b: SIMD2<Float>, _ c: SIMD2<Float>, _ d: SIMD2<Float>, _ col: RGBA) {
        triangle(a, b, c, col)
        triangle(a, c, d, col)
    }

    mutating func line(_ a: SIMD2<Float>, _ b: SIMD2<Float>, width: Float, _ col: RGBA) {
        let d = b - a
        let len = max((d.x * d.x + d.y * d.y).squareRoot(), 1e-6)
        let n = SIMD2<Float>(-d.y / len, d.x / len) * (width * 0.5)
        quad(a + n, b + n, b - n, a - n, col)
    }

    mutating func rect(x0: Float, y0: Float, x1: Float, y1: Float, _ col: RGBA) {
        quad(SIMD2(x0, y0), SIMD2(x1, y0), SIMD2(x1, y1), SIMD2(x0, y1), col)
    }

    mutating func dot(_ center: SIMD2<Float>, radius: Float, _ col: RGBA) {
        let segments = 8
        var prev = center + SIMD2(radius, 0)
        for i in 1...segments {
            let a = Float(i) / Float(segments) * 2 * .pi
            let p = center + SIMD2(cos(a), sin(a)) * radius
            triangle(center, prev, p, col)
            prev = p
        }
    }

    mutating func polyline(_ points: [SIMD2<Float>], closed: Bool, width: Float, color: (Int) -> RGBA) {
        guard points.count > 1 else { return }
        for i in 0..<(points.count - 1) {
            line(points[i], points[i + 1], width: width, color(i))
        }
        if closed, let first = points.first, let last = points.last {
            line(last, first, width: width, color(points.count - 1))
        }
    }
}

/// Draws the foreground shape for each scene's wave style.
enum WaveDrawer {
    static func draw(scene: VisualScene, audio: AudioAnalysis.Snapshot, spectrum: Spectrum,
                     time t: Float, into g: inout GeometryBuilder) {
        let bass = min(audio.bass, 1.4)
        let width = 0.0065 * scene.thickness * (1 + bass * 0.6)
        let alpha = 0.55 + 0.45 * min(1, max(bass, audio.mid))
        let hue0 = scene.baseHue + t * scene.hueSpeed
        func color(_ frac: Float, _ boost: Float = 1) -> GeometryBuilder.RGBA {
            let c = hsv(hue0 + frac * 0.35, scene.saturation, 1)
            return (c.0, c.1, c.2, min(1, alpha * boost))
        }

        let wave = spectrum.wave
        let bands = spectrum.bands
        let s = scene.waveScale
        let aspect = g.aspect

        switch scene.wave {
        case .circle:
            ring(wave: wave, stride: 2, radius: scene.centerRadius * s * (1 + 0.25 * bass), amplitude: 0.12 * s,
                 rotation: t * 0.2, width: width, into: &g) { color($0) }

        case .twinRings:
            ring(wave: wave, stride: 2, radius: scene.centerRadius * s * (1 + 0.3 * bass), amplitude: 0.07 * s,
                 rotation: t * 0.5, width: width, into: &g) { color($0) }
            ring(wave: wave, stride: 2, offset: 1, radius: max(0.42 * s, scene.centerRadius * s + 0.22) * (1 + 0.15 * bass), amplitude: 0.1 * s,
                 rotation: -t * 0.4, width: width, into: &g) { color($0 + 0.5) }

        case .line:
            let n = 256
            var pts = [SIMD2<Float>](); pts.reserveCapacity(n)
            for i in 0..<n {
                let x = -0.94 * aspect + Float(i) / Float(n - 1) * 1.88 * aspect
                pts.append(SIMD2(x, wave[i * 2] * 0.3 * s))
            }
            g.polyline(pts, closed: false, width: width) { color(Float($0) / Float(n)) }

        case .verticalLine:
            let n = 256
            for side: Float in [-1, 1] {
                var pts = [SIMD2<Float>](); pts.reserveCapacity(n)
                for i in 0..<n {
                    let y = -0.94 + Float(i) / Float(n - 1) * 1.88
                    pts.append(SIMD2(side * 0.35 * aspect * 0.5 + wave[i * 2] * 0.22 * s, y))
                }
                g.polyline(pts, closed: false, width: width) { color(Float($0) / Float(n) + (side > 0 ? 0.5 : 0)) }
            }

        case .bars:
            let count = bands.count
            let total = 1.9 * aspect
            let slot = total / Float(count)
            for i in 0..<count {
                let x0 = -total / 2 + Float(i) * slot + slot * 0.15
                let h = 0.02 + bands[i] * 1.15 * s
                g.rect(x0: x0, y0: -0.98, x1: x0 + slot * 0.7, y1: -0.98 + h,
                       color(Float(i) / Float(count), 0.9))
            }

        case .radialBars:
            let count = bands.count
            let inner = scene.centerRadius > 0 ? scene.centerRadius + 0.06 * bass : 0
            for side in 0..<2 {
                for i in 0..<count {
                    let frac = Float(i) / Float(count)
                    let a = (side == 0 ? frac : -frac) * .pi + .pi / 2 + t * 0.1
                    let dir = SIMD2<Float>(cos(a), sin(a))
                    let len = 0.03 + bands[i] * 0.65 * s
                    g.line(dir * inner, dir * (inner + len), width: width * 1.2, color(frac))
                }
            }

        case .spiralDots:
            let count = bands.count
            for arm in 0..<3 {
                for i in 0..<count {
                    let frac = Float(i) / Float(count)
                    let a = Float(i) * 0.33 + Float(arm) * 2 * .pi / 3 + t * 1.2
                    let r = scene.centerRadius + frac * 0.85 * s
                    let radius = 0.006 + bands[i] * 0.032 * scene.thickness
                    g.dot(SIMD2(cos(a), sin(a)) * r, radius: radius, color(frac + Float(arm) * 0.2))
                }
            }

        case .lissajous:
            let delay = 23
            let n = wave.count - delay
            var pts = [SIMD2<Float>](); pts.reserveCapacity(n / 2)
            var i = 0
            while i < n {
                pts.append(SIMD2(wave[i] * 0.5 * s, wave[i + delay] * 0.5 * s))
                i += 2
            }
            g.polyline(pts, closed: false, width: width * 0.8) { color(Float($0) / Float(pts.count)) }
        }
    }

    private static func ring(wave: [Float], stride: Int, offset: Int = 0, radius: Float, amplitude: Float,
                             rotation: Float, width: Float, into g: inout GeometryBuilder,
                             color: (Float) -> GeometryBuilder.RGBA) {
        let n = (wave.count - offset) / stride
        guard n > 16 else { return }
        let blend = 24
        var pts = [SIMD2<Float>](); pts.reserveCapacity(n)
        let first = wave[offset]
        for i in 0..<n {
            var w = wave[offset + i * stride]
            if i >= n - blend { // fade into the first sample so the loop closes smoothly
                let k = Float(i - (n - blend)) / Float(blend)
                w += (first - w) * k
            }
            let a = Float(i) / Float(n) * 2 * .pi + rotation
            let r = max(0, radius + w * amplitude) // a zero radius emits straight from the center
            pts.append(SIMD2(cos(a), sin(a)) * r)
        }
        g.polyline(pts, closed: true, width: width) { color(Float($0) / Float(n)) }
    }
}
