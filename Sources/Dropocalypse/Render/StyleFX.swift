import Foundation
import simd

/// Inputs shared by every style for one frame.
struct FXFrame {
    let scene: VisualScene
    let audio: AudioAnalysis.Snapshot
    let bands: [Float]          // 48 bands, 0…1, low → high
    let wave: [Float]           // 512 samples, −1…1
    let t: Float                // seconds since launch
    let dt: Float               // seconds since last frame
    let aspect: Float
    let logoCenter: SIMD2<Float>
    let logoRadius: Float       // half-size of the logo, 0 when hidden
    let color: (Float, Float) -> GeometryBuilder.RGBA // (palette position, alpha boost)

    var bass: Float { min(audio.bass, 1.4) }
}

/// Small, fast, deterministic random numbers (xorshift32).
struct FXRandom {
    private var state: UInt32
    init(seed: UInt32) { state = seed == 0 ? 0x9E37_79B9 : seed }
    mutating func next() -> Float {
        state ^= state << 13
        state ^= state >> 17
        state ^= state << 5
        return Float(state & 0x00FF_FFFF) / Float(0x0100_0000)
    }
    mutating func range(_ lo: Float, _ hi: Float) -> Float { lo + (hi - lo) * next() }
}

/// Persistent state and drawing for the "classic favorites" styles 11–20: particles, stars,
/// lightning bolts, ridgeline history and rain columns survive from frame to frame.
final class StyleFX {
    private var rng = FXRandom(seed: 0x1234_5678)
    private var lastBeat = 0
    private var beatFired = false
    private var beatStrength: Float = 0

    struct Particle {
        var p: SIMD2<Float>
        var v: SIMD2<Float>
        var life: Float
        var maxLife: Float
        var size: Float
        var hue: Float
    }

    private var sparks: [Particle] = []
    private var swarm: [Particle] = []
    private var embers: [Particle] = []

    private struct Star { var x: Float; var y: Float; var z: Float; var hue: Float }
    private var stars: [Star] = []
    private var warpBoost: Float = 0

    private struct Bolt { var points: [SIMD2<Float>]; var life: Float; var maxLife: Float; var width: Float; var hue: Float }
    private var bolts: [Bolt] = []

    private static let ridgeRows = 30
    private static let ridgePoints = 96
    private var ridges: [[Float]] = []
    private var ridgeTimer: Float = 0

    private struct RainColumn { var y: Float; var speed: Float; var length: Int; var seed: UInt32 }
    private var rain: [RainColumn] = []

    private var gridPhase: Float = 0
    private var lavaTime: Float = 0

    /// Call once per frame before drawing so beats are never missed.
    func begin(_ f: FXFrame) {
        beatFired = f.audio.beatCount != lastBeat && lastBeat != 0
        lastBeat = f.audio.beatCount
        beatStrength = f.audio.beatStrength
    }

    // MARK: - 11. Bass Ring

    func bassRing(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let bass = f.bass
        let c = f.logoCenter
        let r0 = max(0.22, f.logoRadius * 1.02 + 0.03) * (1 + 0.12 * bass) * f.scene.waveScale
        let n = f.bands.count
        let barWidth = min(0.022, 2 * .pi * r0 / Float(n * 2) * 0.6) * f.scene.thickness

        for side: Float in [-1, 1] {
            for i in 0..<n {
                let frac = (Float(i) + 0.5) / Float(n)
                let a = .pi / 2 - side * frac * .pi
                let dir = SIMD2<Float>(cos(a), sin(a))
                let v = pow(f.bands[i], 1.3)
                let len = 0.012 + v * 0.36
                g.line(c + dir * r0, c + dir * (r0 + len), width: barWidth, f.color(frac * 0.6, 1))
            }
        }
        // Thin inner ring.
        var ring: [SIMD2<Float>] = []
        let segs = 96
        for i in 0..<segs {
            let a = Float(i) / Float(segs) * 2 * .pi
            ring.append(c + SIMD2(cos(a), sin(a)) * (r0 - 0.012))
        }
        let ringColor = f.color(0.1, 0.7)
        g.polyline(ring, closed: true, width: 0.004, color: { _ in ringColor })

        // Sparks fly off the ring, a burst on every beat.
        var spawn = Int(2 + f.audio.treble * 6)
        if beatFired { spawn += Int(20 + beatStrength * 30) }
        for _ in 0..<min(spawn, 80) where sparks.count < 700 {
            let a = rng.range(0, 2 * .pi)
            let dir = SIMD2<Float>(cos(a), sin(a))
            let speed = rng.range(0.12, 0.45) + bass * 0.5
            let life = rng.range(1.0, 2.2)
            sparks.append(Particle(p: c + dir * r0, v: dir * speed, life: life, maxLife: life,
                                   size: rng.range(0.002, 0.006), hue: rng.range(0, 0.6)))
        }
        updateAndDraw(&sparks, f, &g) { p, dt in p.v *= (1 - 0.4 * dt) }
    }

    // MARK: - 12. Neon Equalizer

    func neonEqualizer(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let n = f.bands.count
        let total = 1.84 * f.aspect
        let slot = total / Float(n)
        let left = -total / 2
        let base: Float = -0.42
        let s = f.scene.waveScale

        let line = f.color(0.5, 0.5)
        g.rect(x0: left, y0: base - 0.002, x1: left + total, y1: base + 0.002, line)

        for i in 0..<n {
            let prev = f.bands[max(0, i - 1)], next = f.bands[min(n - 1, i + 1)]
            let v = (prev + 2 * f.bands[i] + next) / 4
            let h = 0.012 + pow(v, 1.15) * 0.62 * s
            let x0 = left + Float(i) * slot + slot * 0.18
            let x1 = x0 + slot * 0.64
            let col = f.color(Float(i) / Float(n), 1)
            g.rect(x0: x0, y0: base + 0.01, x1: x1, y1: base + 0.01 + h, col)
            // Reflection that fades downward.
            let top: GeometryBuilder.RGBA = (col.0, col.1, col.2, 0.3)
            let bottom: GeometryBuilder.RGBA = (col.0, col.1, col.2, 0)
            let ry = base - 0.01, rh = h * 0.45
            g.quad(SIMD2(x0, ry), top, SIMD2(x1, ry), top, SIMD2(x1, ry - rh), bottom, SIMD2(x0, ry - rh), bottom)
        }
    }

    // MARK: - 13. Pulsar Ridges (drawn in the "over" batch so lower ridges hide the ones behind)

    func pulsarRidges(_ f: FXFrame, _ over: inout GeometryBuilder) {
        let rows = Self.ridgeRows, pts = Self.ridgePoints
        if ridges.isEmpty { ridges = Array(repeating: Array(repeating: 0, count: pts), count: rows) }

        ridgeTimer += f.dt
        if ridgeTimer >= 1.0 / 20.0 {
            ridgeTimer = 0
            var row = [Float](repeating: 0, count: pts)
            let n = f.bands.count
            for j in 0..<pts {
                let x01 = Float(j) / Float(pts - 1)
                let d = abs(x01 - 0.5) * 2                       // 0 center … 1 edge
                let env = exp(-pow((x01 - 0.5) / 0.22, 2)) * 0.85 + 0.15
                let idx = min(n - 1, Int(d * Float(n) * 0.7))    // bass in the middle
                row[j] = (pow(f.bands[idx], 1.4) + rng.range(0, 0.06)) * env
            }
            ridges.removeFirst()
            ridges.append(row)
        }

        let spacing: Float = 1.3 / Float(rows)
        let halfWidth: Float = f.aspect * 1.02            // edge to edge (slightly past, so no visible ends)
        let ink = f.color(0, 1)
        let lineColor: GeometryBuilder.RGBA = (ink.0, ink.1, ink.2, 0.95)
        let fill: GeometryBuilder.RGBA = (0, 0, 0, 1)
        let s = f.scene.waveScale * (1 + f.bass * 0.15)

        // Oldest (top, farthest) first so newer ridges overlap them.
        for k in 0..<rows {
            let row = ridges[k]
            let baseY = 0.68 - Float(k) * spacing
            var prev = SIMD2<Float>(-halfWidth, baseY + row[0] * 0.32 * s)
            for j in 1..<pts {
                let x = -halfWidth + Float(j) / Float(pts - 1) * 2 * halfWidth
                let p = SIMD2<Float>(x, baseY + row[j] * 0.32 * s)
                over.quad(prev, fill, p, fill, SIMD2(p.x, baseY - 0.04), fill, SIMD2(prev.x, baseY - 0.04), fill)
                over.line(prev, p, width: 0.0045 * f.scene.thickness, lineColor)
                prev = p
            }
        }
    }

    // MARK: - 14. Outrun Grid

    func outrunGrid(_ f: FXFrame, _ g: inout GeometryBuilder, _ over: inout GeometryBuilder) {
        let horizon: Float = -0.02
        let bass = f.bass
        let w = f.aspect * 1.05

        // Sky glow, sun, then the floor that hides everything below the horizon.
        let glow: GeometryBuilder.RGBA = (0.9, 0.15, 0.6, 0.35)
        let clear: GeometryBuilder.RGBA = (0.9, 0.15, 0.6, 0)
        over.quad(SIMD2(-w, horizon), glow, SIMD2(w, horizon), glow, SIMD2(w, horizon + 0.55), clear,
                  SIMD2(-w, horizon + 0.55), clear)

        drawSun(radius: 0.34 * (1 + 0.08 * bass), cy: horizon + 0.3, horizon: horizon, into: &over)
        over.rect(x0: -w, y0: -1.05, x1: w, y1: horizon, (0.02, 0, 0.05, 1))

        // Scrolling perspective grid.
        gridPhase += (0.45 + bass * 2.2 + (beatFired ? 0.6 : 0)) * f.dt
        gridPhase -= floor(gridPhase)
        let gridHue = f.scene.baseHue
        for i in 0..<26 {
            let z = 1 + Float(i) + (1 - gridPhase)
            let y = horizon - 0.95 / z
            let a = min(1, 1.8 / z)
            let c = hsv(gridHue, 0.85, 1)
            g.line(SIMD2(-w, y), SIMD2(w, y), width: 0.0025 + 0.004 / z, (c.0, c.1, c.2, a))
        }
        for k in -16...16 {
            let xw = Float(k) * 0.5
            let near = SIMD2<Float>(xw * 0.95, horizon - 0.95)
            let far = SIMD2<Float>(xw * 0.95 / 30, horizon - 0.95 / 30)
            let c = hsv(gridHue, 0.85, 1)
            g.line(far, near, width: 0.003, (c.0, c.1, c.2, 0.75))
        }
        // Horizon line and a spectrum "skyline" sitting on it.
        let hc = hsv(gridHue + 0.05, 0.5, 1)
        g.rect(x0: -w, y0: horizon - 0.003, x1: w, y1: horizon + 0.003, (hc.0, hc.1, hc.2, 0.9))
        // Edge to edge: bass meets in the middle, highs reach the screen edges. Colors run from
        // the sun's gold through the sky's pink and magenta to the grid's purple, with a
        // reflection on the floor.
        let n = f.bands.count
        let slot = w / Float(n)
        for i in 0..<n {
            let frac = Float(i) / Float(n - 1)
            let prev = f.bands[max(0, i - 1)], next = f.bands[min(n - 1, i + 1)]
            let v = (prev + 2 * f.bands[i] + next) / 4
            let h = 0.012 + pow(v, 1.1) * 0.2 * f.scene.waveScale
            let hue = 0.12 - frac * 0.37
            let c = hsv(hue, 0.9, 1), t = hsv(hue + 0.03, 0.5, 1)
            let foot: GeometryBuilder.RGBA = (c.0, c.1, c.2, 0.85)
            let tip: GeometryBuilder.RGBA = (t.0, t.1, t.2, 1)
            let refl: GeometryBuilder.RGBA = (c.0, c.1, c.2, 0.28)
            let fade: GeometryBuilder.RGBA = (c.0, c.1, c.2, 0)
            for side: Float in [-1, 1] {
                let xa = side * (Float(i) * slot + slot * 0.18)
                let xb = side * (Float(i) * slot + slot * 0.82)
                let x0 = min(xa, xb), x1 = max(xa, xb)
                g.quad(SIMD2(x0, horizon + 0.003), foot, SIMD2(x1, horizon + 0.003), foot,
                       SIMD2(x1, horizon + h), tip, SIMD2(x0, horizon + h), tip)
                g.quad(SIMD2(x0, horizon - 0.003), refl, SIMD2(x1, horizon - 0.003), refl,
                       SIMD2(x1, horizon - h * 0.45), fade, SIMD2(x0, horizon - h * 0.45), fade)
            }
        }
    }

    /// Smooth striped synthwave sun. Each solid band is cut into thin slices whose left and right
    /// edges follow the circle exactly (no stair steps), colored with a smooth top-to-bottom
    /// gradient, and edged with a soft feather so it stays smooth on any screen.
    private func drawSun(radius r: Float, cy: Float, horizon: Float, into over: inout GeometryBuilder) {
        let top = cy + r, bottom = max(cy - r, horizon)
        func halfWidth(_ y: Float) -> Float { sqrt(max(0, r * r - (y - cy) * (y - cy))) }
        func color(_ y: Float, _ a: Float) -> GeometryBuilder.RGBA {
            let frac = (top - y) / (2 * r)
            let c = hsv(0.13 - frac * 0.21, 0.85, 1)
            return (c.0, c.1, c.2, a)
        }
        // Solid bands: unbroken on top, then gaps that widen toward the horizon.
        var bands: [(Float, Float)] = []
        let stripeStart = top - 0.45 * 2 * r
        bands.append((top, stripeStart))
        let period = 2 * r / 13
        var y = stripeStart
        while y > bottom {
            let t = (stripeStart - y) / max(0.001, stripeStart - (cy - r))
            let gap = period * (0.16 + 0.42 * t)
            let solidBottom = max(bottom, y - (period - gap))
            bands.append((y - gap * 0.5, solidBottom))
            y = solidBottom - gap * 0.5
        }
        let feather: Float = 0.006
        for (y0, y1) in bands where y0 > y1 {
            let pieces = max(1, Int(ceil((y0 - y1) / 0.006)))
            for k in 0..<pieces {
                let ya = y0 - (y0 - y1) * Float(k) / Float(pieces)
                let yb = y0 - (y0 - y1) * Float(k + 1) / Float(pieces)
                let wa = halfWidth(ya), wb = halfWidth(yb)
                let ca = color(ya, 1), cb = color(yb, 1)
                over.quad(SIMD2(-wb, yb), cb, SIMD2(wb, yb), cb, SIMD2(wa, ya), ca, SIMD2(-wa, ya), ca)
                // Soft anti-aliased rim on both sides.
                let ea = color(ya, 0), eb = color(yb, 0)
                over.quad(SIMD2(wb, yb), cb, SIMD2(wb + feather, yb), eb, SIMD2(wa + feather, ya), ea, SIMD2(wa, ya), ca)
                over.quad(SIMD2(-wb - feather, yb), eb, SIMD2(-wb, yb), cb, SIMD2(-wa, ya), ca, SIMD2(-wa - feather, ya), ea)
            }
            // Feather the flat top and bottom of each band too.
            let wt = halfWidth(y0), wbm = halfWidth(y1)
            let ct = color(y0, 1), cbm = color(y1, 1)
            over.quad(SIMD2(-wt, y0), ct, SIMD2(wt, y0), ct, SIMD2(wt, y0 + feather * 0.6), color(y0, 0),
                      SIMD2(-wt, y0 + feather * 0.6), color(y0, 0))
            over.quad(SIMD2(-wbm, y1 - feather * 0.6), color(y1, 0), SIMD2(wbm, y1 - feather * 0.6), color(y1, 0),
                      SIMD2(wbm, y1), cbm, SIMD2(-wbm, y1), cbm)
        }
    }

    // MARK: - 21. Mirror Equalizer

    /// Bars grow up and down from a center line, bass in the middle and highs out to the edges,
    /// in a sunset rainbow (pink → orange → gold).
    func mirrorEqualizer(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let n = f.bands.count
        let half = 0.94 * f.aspect
        let slot = half / Float(n)
        let mid: Float = -0.02
        let s = f.scene.waveScale
        let pulse = 1 + 0.12 * f.bass
        for i in 0..<n {
            let prev = f.bands[max(0, i - 1)], next = f.bands[min(n - 1, i + 1)]
            let v = (prev + 2 * f.bands[i] + next) / 4
            let h = (0.008 + pow(v, 1.2) * 0.42 * s) * pulse
            let frac = Float(i) / Float(n - 1)
            let c = hsv(0.93 + frac * 0.24 + 0.03 * sin(f.t * 0.4), 0.9, 1)
            let core: GeometryBuilder.RGBA = (c.0, c.1, c.2, 1)
            let tip: GeometryBuilder.RGBA = (min(1, c.0 + 0.35), min(1, c.1 + 0.35), min(1, c.2 + 0.35), 1)
            for side: Float in [-1, 1] {
                let xa = side * (Float(i) * slot + slot * 0.16)
                let xb = side * (Float(i) * slot + slot * 0.84)
                let x0 = min(xa, xb), x1 = max(xa, xb)
                g.quad(SIMD2(x0, mid), core, SIMD2(x1, mid), core, SIMD2(x1, mid + h), tip, SIMD2(x0, mid + h), tip)
                g.quad(SIMD2(x0, mid - h), tip, SIMD2(x1, mid - h), tip, SIMD2(x1, mid), core, SIMD2(x0, mid), core)
            }
        }
        let line = hsv(0.95, 0.4, 1)
        g.rect(x0: -half, y0: mid - 0.0015, x1: half, y1: mid + 0.0015, (line.0, line.1, line.2, 0.8))
    }

    // MARK: - 22. LED Meter

    private var ledPeaks: [Float] = []
    private var ledHold: [Float] = []

    /// A classic hardware spectrum analyzer: segmented LEDs from green through yellow to red,
    /// with peak-hold caps that hang for a moment and then drop.
    func ledMeter(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let cols = 24, segs = 18
        let n = f.bands.count
        if ledPeaks.count != cols { ledPeaks = Array(repeating: 0, count: cols); ledHold = ledPeaks }
        let total = 1.7 * f.aspect
        let slot = total / Float(cols)
        let left = -total / 2
        let base: Float = -0.62, height: Float = 1.18
        let segH = height / Float(segs)
        let s = f.scene.waveScale
        for c in 0..<cols {
            let a = c * n / cols, b = max(a + 1, (c + 1) * n / cols)
            var v: Float = 0
            for k in a..<b { v = max(v, f.bands[k]) }
            let level = min(1, pow(v, 1.1) * 1.05 * s)
            let lit = Int((level * Float(segs)).rounded())
            if Float(lit) >= ledPeaks[c] { ledPeaks[c] = Float(lit); ledHold[c] = 0.45 }
            else {
                ledHold[c] -= f.dt
                if ledHold[c] <= 0 { ledPeaks[c] = max(0, ledPeaks[c] - f.dt * 14) }
            }
            let x0 = left + Float(c) * slot + slot * 0.12, x1 = x0 + slot * 0.76
            for k in 0..<segs {
                let y0 = base + Float(k) * segH + segH * 0.12, y1 = y0 + segH * 0.76
                let frac = Float(k) / Float(segs - 1)
                let hue: Float = frac < 0.6 ? 0.33 - frac / 0.6 * 0.12 : 0.21 - (frac - 0.6) / 0.4 * 0.21
                let col = hsv(hue, 0.95, 1)
                let on = k < lit
                g.rect(x0: x0, y0: y0, x1: x1, y1: y1, (col.0, col.1, col.2, on ? 1 : 0.07))
            }
            let pk = Int(ledPeaks[c])
            if pk > 0 && pk <= segs {
                let y0 = base + Float(pk - 1) * segH + segH * 0.12
                g.rect(x0: x0, y0: y0, x1: x1, y1: y0 + segH * 0.76, (1, 1, 1, 0.95))
            }
        }
    }

    // MARK: - 23. Rainbow Peaks

    private var capY: [Float] = []
    private var capV: [Float] = []

    /// Thin full-spectrum bars that cycle through the rainbow, with glowing caps the bars knock
    /// into the air and that fall back with gravity, and a light trail tracing the bar tops.
    func rainbowPeaks(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let n = f.bands.count
        if capY.count != n { capY = Array(repeating: 0, count: n); capV = capY }
        let total = 1.86 * f.aspect
        let slot = total / Float(n)
        let left = -total / 2
        let base: Float = -0.5
        let s = f.scene.waveScale
        var tops: [SIMD2<Float>] = []
        tops.reserveCapacity(n)
        for i in 0..<n {
            let prev = f.bands[max(0, i - 1)], next = f.bands[min(n - 1, i + 1)]
            let v = (prev + 2 * f.bands[i] + next) / 4
            let h = 0.01 + pow(v, 1.1) * 0.75 * s
            let x = left + (Float(i) + 0.5) * slot
            let hue = Float(i) / Float(n) + f.t * 0.08
            let c = hsv(hue, 0.9, 1)
            let foot: GeometryBuilder.RGBA = (c.0, c.1, c.2, 0.35)
            let head: GeometryBuilder.RGBA = (c.0, c.1, c.2, 1)
            let bw = slot * 0.22
            g.quad(SIMD2(x - bw, base), foot, SIMD2(x + bw, base), foot, SIMD2(x + bw, base + h), head, SIMD2(x - bw, base + h), head)
            // Cap physics: pushed up by the bar, pulled down by gravity.
            capV[i] -= 2.4 * f.dt
            capY[i] += capV[i] * f.dt
            if capY[i] < h + 0.012 {
                capY[i] = h + 0.012
                capV[i] = max(capV[i], (beatFired ? 0.9 : 0.25) * v)
            }
            let cy = base + capY[i]
            g.softDot(SIMD2(x, cy), radius: slot * 0.55, (c.0, c.1, c.2, 0.35), segments: 10)
            g.rect(x0: x - slot * 0.32, y0: cy - 0.004, x1: x + slot * 0.32, y1: cy + 0.004, (1, 1, 1, 0.95))
            tops.append(SIMD2(x, base + h))
        }
        let trail = tops
        g.polyline(trail, closed: false, width: 0.004) { i in
            let c = hsv(Float(i) / Float(n) + f.t * 0.08 + 0.5, 0.6, 1)
            return (c.0, c.1, c.2, 0.55)
        }
        g.rect(x0: left, y0: base - 0.002, x1: left + total, y1: base + 0.002, (1, 1, 1, 0.35))
    }

    // MARK: - 15. Hyperspace

    func hyperspace(_ f: FXFrame, _ g: inout GeometryBuilder) {
        if stars.isEmpty {
            for _ in 0..<600 {
                stars.append(Star(x: rng.range(-1, 1), y: rng.range(-1, 1), z: rng.range(0.05, 1),
                                  hue: rng.range(-0.08, 0.08)))
            }
        }
        if beatFired { warpBoost = max(warpBoost, 0.6 + beatStrength * 0.8) }
        warpBoost *= pow(0.05, f.dt)
        let speed = (0.18 + f.bass * 1.3 + warpBoost) * f.scene.waveScale
        let proj: Float = 0.35

        for i in stars.indices {
            var s = stars[i]
            s.z -= speed * f.dt
            var p = SIMD2<Float>(s.x / s.z, s.y / s.z) * proj
            if s.z <= 0.03 || abs(p.x) > f.aspect * 1.2 || abs(p.y) > 1.2 {
                s = Star(x: rng.range(-1, 1), y: rng.range(-1, 1), z: rng.range(0.85, 1), hue: s.hue)
                p = SIMD2<Float>(s.x / s.z, s.y / s.z) * proj
            }
            let tailZ = min(1, s.z + speed * 0.07)
            let tail = SIMD2<Float>(s.x / tailZ, s.y / tailZ) * proj
            let near = 1 - s.z
            let c = hsv(f.scene.baseHue + s.hue, f.scene.saturation * (0.4 + near * 0.6), 1)
            g.line(tail, p, width: (0.0015 + 0.005 * near) * f.scene.thickness, (c.0, c.1, c.2, min(1, near * 1.4)))
            stars[i] = s
        }
    }

    // MARK: - 16. Particle Storm

    func particleStorm(_ f: FXFrame, _ g: inout GeometryBuilder) {
        if swarm.isEmpty {
            for _ in 0..<900 {
                let a = rng.range(0, 2 * .pi), r = rng.range(0.1, 0.8)
                swarm.append(Particle(p: SIMD2(cos(a), sin(a)) * r, v: .zero, life: 1, maxLife: 1,
                                      size: rng.range(0.002, 0.005), hue: rng.range(0, 1)))
            }
        }
        let t = f.t, dt = f.dt, bass = f.bass
        let burst = beatFired ? (0.5 + beatStrength * 1.1) : 0
        for i in swarm.indices {
            var q = swarm[i]
            let a = sin(q.p.x * 2.1 + t * 0.35) * cos(q.p.y * 2.3 - t * 0.28) * 3 + t * 0.15
            let flow = SIMD2<Float>(cos(a), sin(a)) * (0.1 + bass * 0.45)
            let r = max(0.001, (q.p.x * q.p.x + q.p.y * q.p.y).squareRoot())
            let radial = q.p / r
            let tangent = SIMD2<Float>(-radial.y, radial.x)
            let target = flow + tangent * 0.22 + radial * ((0.55 - r) * 0.6)
            q.v += (target - q.v) * min(1, dt * 3)
            if burst > 0 { q.v += radial * burst }
            q.p += q.v * dt
            if abs(q.p.x) > f.aspect + 0.1 { q.p.x = -q.p.x * 0.98 }
            if abs(q.p.y) > 1.1 { q.p.y = -q.p.y * 0.98 }
            swarm[i] = q
            let hue = atan2(q.p.y, q.p.x) / (2 * .pi) * 0.4 + q.hue * 0.15
            g.spark(q.p, radius: (q.size + f.audio.treble * 0.003) * f.scene.thickness, f.color(hue, 0.85))
        }
    }

    // MARK: - 17. Lightning Storm

    func lightning(_ f: FXFrame, _ g: inout GeometryBuilder) {
        var spawn = 0
        if beatFired { spawn += 1 + Int(beatStrength * 2.5) }
        if f.audio.treble > 0.7 && rng.next() < f.dt * 3 { spawn += 1 }
        for _ in 0..<spawn where bolts.count < 24 { makeBolt(f) }

        // Soft core glow that breathes with the bass.
        let core = f.color(0.1, 0.18 + f.bass * 0.2)
        g.softDot(f.logoCenter, radius: 0.15 + f.bass * 0.12, core, segments: 24)

        for i in bolts.indices.reversed() {
            bolts[i].life -= f.dt
            if bolts[i].life <= 0 { bolts.remove(at: i); continue }
        }
        for b in bolts {
            let k = b.life / b.maxLife
            let c = hsv(b.hue, 0.6, 1)
            for j in 1..<b.points.count {
                g.line(b.points[j - 1], b.points[j], width: 0.02 * b.width, (c.0, c.1, c.2, 0.2 * k))
                g.line(b.points[j - 1], b.points[j], width: 0.0045 * b.width, (0.9, 0.95, 1, k))
            }
        }
    }

    private func makeBolt(_ f: FXFrame) {
        let a = rng.range(0, 2 * .pi)
        let start = f.logoCenter + SIMD2(cos(a), sin(a)) * rng.range(0.03, 0.12)
        let a2 = a + rng.range(-0.35, 0.35)
        let end = SIMD2<Float>(cos(a2), sin(a2)) * rng.range(0.9, 1.4) * SIMD2(f.aspect, 1)
        let main = jagged(start, end, levels: 6, roughness: 0.18)
        let life = rng.range(0.18, 0.35)
        let hue = f.scene.baseHue + rng.range(-0.06, 0.06)
        bolts.append(Bolt(points: main, life: life, maxLife: life, width: 1, hue: hue))
        for _ in 0..<2 {
            let idx = Int(rng.range(0.3, 0.7) * Float(main.count - 1))
            let from = main[idx]
            let dir = normalize(end - start)
            let side = SIMD2<Float>(-dir.y, dir.x) * (rng.next() < 0.5 ? -1 : 1)
            let to = from + (dir * 0.3 + side * 0.25) * rng.range(0.5, 1.0)
            bolts.append(Bolt(points: jagged(from, to, levels: 4, roughness: 0.2), life: life * 0.8,
                              maxLife: life * 0.8, width: 0.6, hue: hue))
        }
    }

    private func jagged(_ a: SIMD2<Float>, _ b: SIMD2<Float>, levels: Int, roughness: Float) -> [SIMD2<Float>] {
        var pts = [a, b]
        var amount = length(b - a) * roughness
        for _ in 0..<levels {
            var next: [SIMD2<Float>] = [pts[0]]
            for i in 1..<pts.count {
                let p0 = pts[i - 1], p1 = pts[i]
                let d = p1 - p0
                let n = normalize(SIMD2(-d.y, d.x) + 1e-6)
                next.append((p0 + p1) / 2 + n * rng.range(-amount, amount))
                next.append(p1)
            }
            pts = next
            amount *= 0.5
        }
        return pts
    }

    // MARK: - 18. Digital Rain

    func digitalRain(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let cell: Float = 0.05
        let columns = Int(2 * f.aspect / cell) + 1
        if rain.count != columns {
            rain = (0..<columns).map { _ in
                RainColumn(y: rng.range(-1, 1.8), speed: rng.range(0.35, 0.9), length: Int(rng.range(8, 22)),
                           seed: UInt32(rng.next() * 1_000_000))
            }
        }
        let tick = Int(f.t * 7)
        let tempo = (0.55 + f.bass * 1.6 + (beatFired ? 0.5 : 0)) * f.scene.waveScale
        let hue = f.scene.baseHue

        for c in 0..<columns {
            var col = rain[c]
            col.y -= col.speed * tempo * f.dt
            if col.y + Float(col.length) * cell < -1.05 {
                col = RainColumn(y: rng.range(1.05, 1.6), speed: rng.range(0.35, 0.9), length: Int(rng.range(8, 22)),
                                 seed: UInt32(rng.next() * 1_000_000))
            }
            rain[c] = col
            let x = -f.aspect + (Float(c) + 0.5) * cell
            let headRow = Int(floor(col.y / cell))
            for k in 0..<col.length {
                let row = headRow + k
                let cy = (Float(row) + 0.5) * cell
                if cy < -1.05 || cy > 1.05 { continue }
                var h = col.seed &* 2_654_435_761 &+ UInt32(truncatingIfNeeded: row &* 40503)
                if (h & 7) == 0 { h = h &+ UInt32(truncatingIfNeeded: tick &* 9973) } // a few glyphs flicker
                let fade = pow(1 - Float(k) / Float(col.length), 1.3)
                let color: GeometryBuilder.RGBA
                if k == 0 {
                    color = (0.85, 1, 0.9, 1)
                } else {
                    let cc = hsv(hue, f.scene.saturation, 1)
                    color = (cc.0, cc.1, cc.2, 0.9 * fade)
                }
                glyph(&g, center: SIMD2(x, cy), size: cell, hash: h, color)
            }
        }
    }

    private func glyph(_ g: inout GeometryBuilder, center c: SIMD2<Float>, size: Float, hash: UInt32,
                       _ col: GeometryBuilder.RGBA) {
        let w = size * 0.32, h = size * 0.4
        let p: [SIMD2<Float>] = [SIMD2(-w, h), SIMD2(w, h), SIMD2(-w, 0), SIMD2(w, 0), SIMD2(-w, -h), SIMD2(w, -h)]
        let segs: [(Int, Int)] = [(0, 1), (2, 3), (4, 5), (0, 2), (2, 4), (1, 3), (3, 5), (0, 3), (2, 5), (1, 2)]
        var bits = hash
        var drawn = 0
        for (i, s) in segs.enumerated() where drawn < 4 {
            if (bits >> UInt32(i)) & 1 == 1 {
                g.line(c + p[s.0], c + p[s.1], width: size * 0.07, col)
                drawn += 1
            }
        }
        if drawn == 0 { bits = 0; g.line(c + p[0], c + p[5], width: size * 0.07, col) }
    }

    // MARK: - 19. Lava Lamp

    func lavaLamp(_ f: FXFrame, _ g: inout GeometryBuilder) {
        lavaTime += f.dt * (0.5 + f.bass * 1.2)
        let t = lavaTime
        for i in 0..<7 {
            let fi = Float(i)
            let x = sin(t * (0.21 + fi * 0.037) + fi * 1.7) * f.aspect * 0.55
            let y = sin(t * (0.17 + fi * 0.029) + fi * 2.3) * 0.7
            let r = (0.16 + 0.06 * sin(t * 0.8 + fi)) * (1 + 0.35 * f.bass) * f.scene.waveScale
            g.softDot(SIMD2(x, y), radius: r, f.color(fi * 0.07, 0.35), segments: 28)
            g.softDot(SIMD2(x, y), radius: r * 0.5, f.color(fi * 0.07 + 0.05, 0.25), segments: 20)
        }
    }

    // MARK: - 20. Inferno

    func inferno(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let emitters = 24
        let width = f.aspect * 1.9
        for e in 0..<emitters {
            let band = f.bands[min(f.bands.count - 1, e * 2)]
            let rate = (0.4 + band * 3 + f.bass * 2) * f.dt * 30
            var count = Int(rate)
            if rng.next() < rate - Float(count) { count += 1 }
            for _ in 0..<count where embers.count < 1100 {
                let x = -width / 2 + (Float(e) + rng.next()) / Float(emitters) * width
                let life = rng.range(0.9, 1.6)
                embers.append(Particle(p: SIMD2(x, -1.02),
                                       v: SIMD2(rng.range(-0.05, 0.05), 0.35 + band * 0.9 + rng.range(0, 0.3) + f.bass * 0.4),
                                       life: life, maxLife: life,
                                       size: rng.range(0.018, 0.04) * (0.7 + band) * f.scene.waveScale,
                                       hue: rng.range(0, 1)))
            }
        }
        let t = f.t
        for i in embers.indices.reversed() {
            embers[i].life -= f.dt
            if embers[i].life <= 0 { embers.swapAt(i, embers.count - 1); embers.removeLast(); continue }
            var q = embers[i]
            q.v.y *= (1 - 0.3 * f.dt)
            q.v.x += sin(t * 3 + q.p.y * 6 + q.hue * 10) * 0.35 * f.dt
            q.p += q.v * f.dt
            embers[i] = q
            let k = q.life / q.maxLife
            let c = hsv(0.13 * k, 0.95, 1)
            g.softDot(q.p, radius: q.size * pow(k, 0.5), (c.0, c.1, c.2, 0.5 * k), segments: 8)
        }
    }

    // MARK: - Shared particle update

    private func updateAndDraw(_ list: inout [Particle], _ f: FXFrame, _ g: inout GeometryBuilder,
                               extra: (inout Particle, Float) -> Void) {
        for i in list.indices.reversed() {
            list[i].life -= f.dt
            if list[i].life <= 0 { list.swapAt(i, list.count - 1); list.removeLast(); continue }
            var q = list[i]
            extra(&q, f.dt)
            q.p += q.v * f.dt
            list[i] = q
            let k = q.life / q.maxLife
            g.spark(q.p, radius: q.size * (0.4 + 0.6 * k), f.color(q.hue, k))
        }
    }
}
