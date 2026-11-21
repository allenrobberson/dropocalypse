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
    /// Index of the newest row in `ridges` (a ring buffer, so a new row never shifts or allocates).
    private var ridgeHead = StyleFX.ridgeRows - 1
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
        let segs = 96
        let ringColor = f.color(0.1, 0.7)
        let first = c + SIMD2<Float>(1, 0) * (r0 - 0.012)
        var prev = first
        for i in 1..<segs {
            let a = Float(i) / Float(segs) * 2 * .pi
            let p = c + SIMD2(cos(a), sin(a)) * (r0 - 0.012)
            g.line(prev, p, width: 0.004, ringColor)
            prev = p
        }
        g.line(prev, first, width: 0.004, ringColor) // close the loop

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
        if ridges.isEmpty {
            ridges = Array(repeating: Array(repeating: 0, count: pts), count: rows)
            ridgeHead = rows - 1
        }

        ridgeTimer += f.dt
        if ridgeTimer >= 1.0 / 20.0 {
            ridgeTimer = 0
            ridgeHead = (ridgeHead + 1) % rows // overwrite the oldest row in place
            let n = f.bands.count
            for j in 0..<pts {
                let x01 = Float(j) / Float(pts - 1)
                let d = abs(x01 - 0.5) * 2                       // 0 center … 1 edge
                let env = exp(-pow((x01 - 0.5) / 0.22, 2)) * 0.85 + 0.15
                let idx = min(n - 1, Int(d * Float(n) * 0.7))    // bass in the middle
                ridges[ridgeHead][j] = (pow(f.bands[idx], 1.4) + rng.range(0, 0.06)) * env
            }
        }

        let spacing: Float = 1.3 / Float(rows)
        let halfWidth: Float = f.aspect * 1.02            // edge to edge (slightly past, so no visible ends)
        let ink = f.color(0, 1)
        let lineColor: GeometryBuilder.RGBA = (ink.0, ink.1, ink.2, 0.95)
        let fill: GeometryBuilder.RGBA = (0, 0, 0, 1)
        let s = f.scene.waveScale * (1 + f.bass * 0.15)

        // Oldest (top, farthest) first so newer ridges overlap them.
        for k in 0..<rows {
            let row = ridges[(ridgeHead + 1 + k) % rows]
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
        let c = hsv(gridHue, 0.85, 1)
        for i in 0..<26 {
            let z = 1 + Float(i) + (1 - gridPhase)
            let y = horizon - 0.95 / z
            let a = min(1, 1.8 / z)
            g.line(SIMD2(-w, y), SIMD2(w, y), width: 0.0025 + 0.004 / z, (c.0, c.1, c.2, a))
        }
        for k in -16...16 {
            let xw = Float(k) * 0.5
            let near = SIMD2<Float>(xw * 0.95, horizon - 0.95)
            let far = SIMD2<Float>(xw * 0.95 / 30, horizon - 0.95 / 30)
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
    private static let ledSegments = 18
    /// Green → yellow → red, one color per LED row (bottom to top).
    private static let ledRowColors: [(Float, Float, Float)] = (0..<ledSegments).map { k in
        let frac = Float(k) / Float(ledSegments - 1)
        let hue: Float = frac < 0.6 ? 0.33 - frac / 0.6 * 0.12 : 0.21 - (frac - 0.6) / 0.4 * 0.21
        return hsv(hue, 0.95, 1)
    }
    private var ledHold: [Float] = []

    /// A classic hardware spectrum analyzer: segmented LEDs from green through yellow to red,
    /// with peak-hold caps that hang for a moment and then drop.
    func ledMeter(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let cols = 24, segs = Self.ledSegments
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
                let col = Self.ledRowColors[k]
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

    // MARK: - 24. Tesseract

    private var tessAngles = SIMD4<Float>(0.3, 0.8, 0.2, 0.5)   // rotation in the XW, YW, ZW and XY planes
    private var tessKick: Float = 0

    /// A 4D hypercube: 16 corners and 32 edges rotating through the fourth dimension, projected
    /// down to the screen, so the inner cube keeps turning itself inside out through the outer one.
    /// It spins harder on every beat and swells with the bass.
    func tesseract(_ f: FXFrame, _ g: inout GeometryBuilder) {
        if beatFired { tessKick = min(3, tessKick + 0.9 + beatStrength * 0.6) }
        tessKick *= exp(-f.dt * 2.5)
        let spin = (0.22 + 0.55 * f.bass + tessKick) * f.dt
        tessAngles += SIMD4<Float>(0.9, 0.65, 0.4, 0.25) * spin

        func rotate(_ p: inout SIMD4<Float>, _ i: Int, _ j: Int, _ a: Float) {
            let c = cos(a), s = sin(a)
            let pi = p[i], pj = p[j]
            p[i] = pi * c - pj * s
            p[j] = pi * s + pj * c
        }

        let scale = 1.55 * (1 + 0.14 * f.bass) * f.scene.waveScale
        var pts = [SIMD2<Float>](repeating: .zero, count: 16)
        var w = [Float](repeating: 0, count: 16)
        for i in 0..<16 {
            var p = SIMD4<Float>(i & 1 == 0 ? -1 : 1, i & 2 == 0 ? -1 : 1, i & 4 == 0 ? -1 : 1, i & 8 == 0 ? -1 : 1)
            rotate(&p, 0, 3, tessAngles.x)
            rotate(&p, 1, 3, tessAngles.y)
            rotate(&p, 2, 3, tessAngles.z)
            rotate(&p, 0, 1, tessAngles.w)
            let k4 = 1 / (2.6 - p.w)                       // 4D -> 3D perspective
            let q = SIMD3<Float>(p.x, p.y, p.z) * k4
            let k3 = 1 / (2.8 - q.z)                       // 3D -> 2D perspective
            pts[i] = SIMD2(q.x, q.y) * k3 * scale
            w[i] = p.w
        }

        let width = 0.0045 * f.scene.thickness * (1 + 0.6 * f.bass)
        for i in 0..<16 {
            for bit in 0..<4 {
                let j = i ^ (1 << bit)
                guard j > i else { continue }
                let near = ((w[i] + w[j]) / 2 + 2) / 4         // 0 = deep in 4D, 1 = closest
                let col = f.color(near * 0.85 + Float(bit) * 0.04, 1)
                let lw = width * (0.6 + 0.8 * near)
                g.line(pts[i], pts[j], width: lw * 4, (col.0, col.1, col.2, 0.16))   // glow
                g.line(pts[i], pts[j], width: lw, (col.0, col.1, col.2, min(1, 0.55 + 0.45 * near)))
            }
        }
        for i in 0..<16 {
            let near = (w[i] + 2) / 4
            let col = f.color(near * 0.85, 1)
            g.softDot(pts[i], radius: 0.014 * (1 + f.bass) * (0.6 + 0.8 * near), (col.0, col.1, col.2, 0.5), segments: 12)
            g.dot(pts[i], radius: 0.005 * (0.6 + 0.8 * near), (1, 1, 1, 0.9))
        }
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
        let glyphRGB = hsv(f.scene.baseHue, f.scene.saturation, 1)

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
                    color = (glyphRGB.0, glyphRGB.1, glyphRGB.2, 0.9 * fade)
                }
                glyph(&g, center: SIMD2(x, cy), size: cell, hash: h, color)
            }
        }
    }

    /// Glyph corners at unit size (scaled by the cell size when drawn), and the strokes between them.
    private static let glyphPoints: [SIMD2<Float>] = [
        SIMD2(-0.32, 0.4), SIMD2(0.32, 0.4),
        SIMD2(-0.32, 0.0), SIMD2(0.32, 0.0),
        SIMD2(-0.32, -0.4), SIMD2(0.32, -0.4),
    ]
    private static let glyphSegs: [(Int, Int)] = [
        (0, 1), (2, 3), (4, 5), (0, 2), (2, 4), (1, 3), (3, 5), (0, 3), (2, 5), (1, 2),
    ]

    private func glyph(_ g: inout GeometryBuilder, center c: SIMD2<Float>, size: Float, hash: UInt32,
                       _ col: GeometryBuilder.RGBA) {
        let p = Self.glyphPoints, segs = Self.glyphSegs // shared constants: no allocation per glyph
        var drawn = 0
        for i in 0..<segs.count where drawn < 4 {
            if (hash >> UInt32(i)) & 1 == 1 {
                let s = segs[i]
                g.line(c + p[s.0] * size, c + p[s.1] * size, width: size * 0.07, col)
                drawn += 1
            }
        }
        if drawn == 0 { g.line(c + p[0] * size, c + p[5] * size, width: size * 0.07, col) }
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

    // MARK: - 25. Full Kaleidoscope
    // Thin symmetric rings and spokes on top of the shader mandala. Drawn already
    // mirrored so the current frame stays symmetric; bass pulses them, bands set the spokes.

    private static let kaleidoHues: [Float] = [0.50, 0.92, 0.62, 0.11, 0.30] // cyan, magenta, blue, gold, acid green

    func fullKaleidoscope(_ f: FXFrame, _ g: inout GeometryBuilder) {
        let bass = f.bass
        let pulse = 1 + bass * 0.22
        let reach = min(0.92, f.aspect * 0.92)
        let hues = Self.kaleidoHues
        func neon(_ i: Int, _ alpha: Float) -> GeometryBuilder.RGBA {
            let c = hsv(hues[i % hues.count] + f.audio.mid * 0.04, 1, 1)
            return (c.0, c.1, c.2, alpha)
        }
        let ringCount = 3
        let segments = 72
        for i in 0..<ringCount {
            let radius = min(reach, (0.16 + Float(i) * 0.22) * pulse)
            let col = neon(i, 0.28 + bass * 0.22)
            let width = 0.0035 + bass * 0.003
            let spin = f.t * 0.04 * Float(i + 1)
            let first = SIMD2(cos(spin), sin(spin)) * radius
            var prev = first
            for k in 1..<segments {
                let a = Float(k) / Float(segments) * 2 * .pi + spin
                let p = SIMD2(cos(a), sin(a)) * radius
                g.line(prev, p, width: width, col)
                prev = p
            }
            g.line(prev, first, width: width, col) // close the ring
        }
        let arms = 8
        let bands = f.bands
        let n = bands.count
        if n > 0 {
            for arm in 0..<arms {
                let a = Float(arm) / Float(arms) * 2 * .pi
                let dir = SIMD2<Float>(cos(a), sin(a))
                let band = bands[(arm * n / arms) % n]
                let inner = min(reach * 0.18, 0.08 * pulse)
                let outer = min(reach, inner + 0.1 + band * (0.55 + f.audio.treble * 0.4))
                g.line(dir * inner, dir * outer, width: 0.005 + band * 0.008, neon(arm, 0.4 + band * 0.45))
                g.softDot(dir * outer, radius: 0.012 + band * 0.02, neon(arm + 1, 0.35 + band * 0.4), segments: 8)
            }
        }
    }

    // MARK: - 26. Main Stage

    // The view from an arena floor: haze, a stage of nested LED frames behind the logo, a nave of
    // truss columns whose moving-head fans meet overhead like a cathedral's vaulted ceiling, a laser
    // fan from the stage and a crowd with phones up. The look changes every 8 beats, like a
    // lighting desk running cues. Mirrors mainStage() in windows/src/renderer.cpp.
    typealias StageRGB = SIMD3<Float>
    private struct StageLook {
        var haze, beam, fixture, laserA, laserB, led: StageRGB
        var laserSustain, beamLevel: Float
    }
    private static func stageLook(_ i: Int) -> StageLook {
        switch i % 4 {
        case 0: return StageLook(haze: [1.0, 0.52, 0.22], beam: [1.0, 0.82, 0.58], fixture: [1.0, 0.92, 0.78],
                                 laserA: [1.0, 0.95, 0.85], laserB: [1.0, 0.75, 0.35], led: [1.0, 0.62, 0.3],
                                 laserSustain: 0.15, beamLevel: 1.0)   // amber
        case 1: return StageLook(haze: [0.08, 0.75, 0.55], beam: [0.6, 1.0, 0.82], fixture: [0.35, 1.0, 0.6],
                                 laserA: [0.25, 1.0, 0.45], laserB: [0.75, 1.0, 0.85], led: [0.55, 1.0, 0.75],
                                 laserSustain: 0.25, beamLevel: 0.9)   // teal
        case 2: return StageLook(haze: [0.42, 0.32, 0.92], beam: [0.85, 0.8, 1.0], fixture: [0.7, 0.75, 1.0],
                                 laserA: [0.78, 0.55, 1.0], laserB: [0.95, 0.92, 1.0], led: [0.8, 0.7, 1.0],
                                 laserSustain: 0.75, beamLevel: 0.55)  // lasers
        default: return StageLook(haze: [0.08, 0.16, 0.95], beam: [0.45, 0.6, 1.0], fixture: [0.3, 0.45, 1.0],
                                  laserA: [0.35, 0.85, 1.0], laserB: [0.6, 0.65, 1.0], led: [0.35, 0.5, 1.0],
                                  laserSustain: 0.3, beamLevel: 0.6)   // deep blue
        }
    }
    private struct CrowdPerson { var x: Float; var row: Int; var head: Float; var phase: Float; var arm: Float; var phone: Bool; var hOff: Float }
    private var stageBeats = 0, stageCurLook = 0, stagePrevLook = 0
    private var stageLookMix: Float = 1, stageBlinder: Float = 0, stageLaser: Float = 0, stageWhite: Float = 0, stageWhiteCool: Float = 0
    private var stageSinceBeat: Float = 10, stageHit: Float = 0
    private var stageEnergy: Float = 0, stageSweep: Float = 0, stageChase: Float = 0, stageBounce: Float = 0
    private var stageLaserFlip = false
    private var stageCrowd: [CrowdPerson] = []

    func mainStage(_ f: FXFrame, _ g: inout GeometryBuilder, _ over: inout GeometryBuilder) {
        func rgba(_ c: StageRGB, _ a: Float) -> GeometryBuilder.RGBA { (c.x, c.y, c.z, a) }
        func mix3(_ a: StageRGB, _ b: StageRGB, _ k: Float) -> StageRGB { a + (b - a) * k }
        let dt = min(f.dt, 0.1)
        let bass = f.bass
        let mid = min(f.audio.mid, 1.4), treb = min(f.audio.treble, 1.4)
        let a = f.aspect

        // Cues and envelopes.
        if beatFired {
            stageBeats += 1
            stageBlinder = max(stageBlinder, 0.45 + beatStrength * 0.75)
            stageLaser = max(stageLaser, 0.55 + beatStrength * 0.7)
            stageLaserFlip.toggle()
            if stageBeats % 8 == 0 { stagePrevLook = stageCurLook; stageCurLook = (stageCurLook + 1) % 4; stageLookMix = 0 }
            stageSinceBeat = 0
            stageHit = 0.5 + 0.5 * beatStrength
            if beatStrength > 0.85 && stageWhiteCool <= 0 { stageWhite = 1; stageWhiteCool = 6 }
        }
        stageSinceBeat += dt
        stageBlinder = max(0, stageBlinder - dt * 3.2)
        stageLaser = max(0, stageLaser - dt * 2.2)
        stageWhite = max(0, stageWhite - dt * 1.6)
        stageWhiteCool -= dt
        stageLookMix = min(1, stageLookMix + dt * 2.5)
        stageEnergy += (min(1, bass * 0.6 + mid * 0.4) - stageEnergy) * min(1, dt * 1.5)
        stageSweep += dt * (0.35 + mid * 0.9)
        stageChase += dt * (1.2 + bass * 3.0)

        let A = Self.stageLook(stagePrevLook), B = Self.stageLook(stageCurLook)
        let k = stageLookMix * stageLookMix * (3 - 2 * stageLookMix)
        let white = StageRGB(1, 1, 1)
        let haze = mix3(mix3(A.haze, B.haze, k), white, stageWhite * 0.8)
        let beamC = mix3(A.beam, B.beam, k), fixC = mix3(A.fixture, B.fixture, k)
        let lasA = mix3(A.laserA, B.laserA, k), lasB = mix3(A.laserB, B.laserB, k), ledC = mix3(A.led, B.led, k)
        let laserSustain = A.laserSustain + (B.laserSustain - A.laserSustain) * k
        let beamLevel = A.beamLevel + (B.beamLevel - A.beamLevel) * k

        let vp = SIMD2<Float>(0, 0.04)     // vanishing point, behind the logo
        let floorY: Float = -0.3           // stage deck in front
        let crowdTop: Float = -0.48        // nothing additive reaches below this
        let W = min(a * 0.88, 1.45)
        func depthY(_ y: Float, _ s: Float) -> Float { vp.y + (y - vp.y) * s }
        let sides: [Float] = [-1, 1]

        // 1. Haze, drawn with normal blending so the crowd can sit in front of it.
        let hz = 0.16 + 0.2 * stageEnergy + 0.18 * stageBlinder + 0.45 * stageWhite
        over.softDot(vp, radius: 1.05 * max(0.75, min(a, 1.3)), rgba(haze, min(0.85, hz)), segments: 28)
        over.softDot(SIMD2(0, floorY + 0.05), radius: 0.75 * max(0.6, min(a, 1.2)),
                     rgba(mix3(haze, white, 0.35), min(0.7, hz * 0.9)), segments: 24)
        for side in sides {
            over.softDot(SIMD2(side * W * 0.75, vp.y + 0.45), radius: 0.55, rgba(haze, hz * 0.45), segments: 20)
        }
        // Light spilling over the crowd from the stage, so their heads read as silhouettes.
        do {
            let wash = min(0.8, 0.25 + 0.3 * stageEnergy + 0.35 * stageBlinder + 0.5 * stageWhite)
            let lit = rgba(mix3(haze, white, 0.25), wash), none = rgba(haze, 0)
            let xl = -a - 0.1, xr = a + 0.1
            over.quad(SIMD2(xl, crowdTop + 0.04), none, SIMD2(xr, crowdTop + 0.04), none,
                      SIMD2(xr, -0.6), lit, SIMD2(xl, -0.6), lit)
            over.quad(SIMD2(xl, -0.6), lit, SIMD2(xr, -0.6), lit, SIMD2(xr, -0.75), none, SIMD2(xl, -0.75), none)
        }

        // 2. Stage: nested house-shaped LED frames running back to the vanishing point.
        let frames = 5
        let chase = fmod(stageChase, Float(frames + 2))
        for i in stride(from: frames - 1, through: 0, by: -1) {
            let s = pow(0.7, Float(i))
            let w = W * 0.78 * s
            let yb = depthY(floorY, s), yt = vp.y + 0.62 * s, ya = vp.y + 0.98 * s
            let d = chase - Float(i)
            let pulse = exp(-d * d * 1.5)
            let lv = (0.3 + 0.5 * pulse + 0.35 * stageBlinder + 0.5 * stageWhite) * (0.55 + 0.45 * s)
            let pts: [SIMD2<Float>] = [SIMD2(-w, yb), SIMD2(-w, yt), SIMD2(0, ya), SIMD2(w, yt), SIMD2(w, yb)]
            for e in 0..<4 {
                g.line(pts[e], pts[e + 1], width: 0.035 * s, rgba(ledC, 0.09 * lv))
                g.line(pts[e], pts[e + 1], width: 0.005 * s + 0.0015, rgba(mix3(ledC, white, 0.5), min(1, 0.5 * lv)))
            }
            // A row of small fixtures along each frame's top edge.
            for j in 1...3 {
                let u = Float(j) / 4
                for side in sides {
                    let p = SIMD2<Float>(side * w * (1 - u), yt + (ya - yt) * u)
                    g.softDot(p, radius: 0.035 * s, rgba(fixC, 0.25 * lv + 0.4 * stageBlinder), segments: 10)
                }
            }
        }

        // 3. The nave: pairs of truss columns running back to the stage. Each column is topped with
        // a cluster of moving heads that sprays a fan of beams upward; the fans from facing columns
        // meet overhead in pointed arches. Every kick blooms the fans open, front pair first,
        // rippling back toward the stage.
        let colDepth: [Float] = [1.0, 0.74, 0.55, 0.41, 0.3]
        let since = stageSinceBeat
        for ci in stride(from: colDepth.count - 1, through: 0, by: -1) {
            let s = colDepth[ci]
            let yTop = vp.y + 0.62 * s
            let yBot = max(crowdTop, depthY(-1, s))
            let delay = Float(ci) * 0.055
            let bloom: Float = since >= delay ? exp(-(since - delay) * 4.5) * stageHit : 0
            let lvCol = 0.6 + 0.4 * s
            for side in sides {
                let x = side * W * s
                // Column (dim steel with braces).
                let tw = 0.045 * s
                let steel: GeometryBuilder.RGBA = (0.42, 0.45, 0.55, 0.22 + 0.2 * stageWhite + 0.15 * bloom)
                let brace: GeometryBuilder.RGBA = (steel.0, steel.1, steel.2, steel.3 * 0.7)
                g.line(SIMD2(x - tw * 0.5, yBot), SIMD2(x - tw * 0.5, yTop), width: 0.004 * s, steel)
                g.line(SIMD2(x + tw * 0.5, yBot), SIMD2(x + tw * 0.5, yTop), width: 0.004 * s, steel)
                let step = 0.06 * s
                var zig = 0
                var y = yBot
                while y + step < yTop {
                    let odd = zig & 1 == 1
                    let xa = x + (odd ? 0.5 : -0.5) * tw, xb = x + (odd ? -0.5 : 0.5) * tw
                    g.line(SIMD2(xa, y), SIMD2(xb, y + step), width: 0.0022 * s, brace)
                    y += step; zig += 1
                }
                // Blinder panels down the column.
                for b in 0..<2 {
                    let by = yBot + (yTop - yBot) * (0.35 + 0.3 * Float(b))
                    let lv = 0.2 + 0.8 * stageBlinder + 0.8 * stageWhite
                    let hw = 0.012 * s, hh = 0.02 * s
                    g.rect(x0: x - hw, y0: by - hh, x1: x + hw, y1: by + hh, rgba(mix3(fixC, white, 0.4), min(1, lv)))
                    g.softDot(SIMD2(x, by), radius: 0.08 * s * (1 + stageBlinder), rgba(fixC, 0.2 * lv), segments: 14)
                }
                // Two fans per column: a tall one from the top (the vault) and a smaller one partway
                // down (the lower arches).
                for h in 0..<2 {
                    let o = SIMD2<Float>(x, h == 0 ? yTop : yBot + (yTop - yBot) * 0.42)
                    let n = h == 0 ? 15 : 9
                    let spread = 0.75 + 0.2 * stageEnergy + 0.4 * bloom        // half-width of the fan (radians)
                    let tilt = 0.12 * sin(stageSweep * 0.9 + Float(ci) * 0.6)  // gentle sway, mirrored
                    let len = (h == 0 ? 0.85 : 0.5) * s * (0.9 + 0.25 * bloom)
                    var lv = beamLevel * lvCol * (0.22 + 0.4 * stageEnergy + 0.75 * bloom + 0.3 * stageWhite)
                    if h == 1 { lv *= 0.7 }
                    for j in 0..<n {
                        let u = Float(j) / Float(n - 1) * 2 - 1                 // -1 .. 1 across the fan
                        let ang = Float.pi * 0.5 - side * (u * spread + tilt)
                        let dir = SIMD2<Float>(cos(ang), sin(ang))
                        var L = len * (1 - 0.18 * abs(u))
                        // Beams leaning toward the middle run on until they cross the centre line, where
                        // they meet the opposite column's fan and close the arch.
                        if dir.x * side < -0.05 { L = min(1.9 * s, abs(o.x) / abs(dir.x) * 1.12) }
                        if dir.y < -0.05 { L = min(L, (o.y - crowdTop) / -dir.y) }
                        let e = o + dir * L
                        let nrm = SIMD2<Float>(-dir.y, dir.x)
                        let we = 0.035 * s + 0.03 * s * bloom
                        let edge = 1 - 0.35 * abs(u)
                        let c0 = rgba(beamC, min(0.85, 0.34 * lv * edge)), c1 = rgba(beamC, 0)
                        g.triangle(o, c0, e + nrm * we, c1, e - nrm * we, c1)
                        g.line(o, o + dir * (L * 0.6), width: 0.0025 * s + 0.001,
                               rgba(mix3(beamC, white, 0.55), 0.32 * lv * edge))
                    }
                    g.softDot(o, radius: 0.05 * s * (1 + bloom), rgba(mix3(fixC, white, 0.5), min(1, 0.5 * lv + 0.3)), segments: 10)
                }
            }
        }

        // 4. Lasers: a fan from the stage deck that fires on every beat and fills in with the hi-hats.
        let laser = min(1.3, stageLaser + laserSustain * (0.25 + 0.6 * stageEnergy))
        if laser > 0.02 {
            let o = SIMD2<Float>(0, floorY + 0.02)
            let n = 10 + Int(min(1, treb) * 26)
            let fanW: Float = stageLaserFlip ? 0.78 : 0.52
            let rot = 0.22 * sin(stageSweep * 1.3)
            for i in 0..<n {
                let u: Float = n > 1 ? Float(i) / Float(n - 1) : 0.5
                let ang = Float.pi * 0.5 + (u - 0.5) * 2 * fanW + rot
                let dir = SIMD2<Float>(cos(ang), sin(ang))
                let c = i % 3 == 1 ? lasB : lasA
                let flick = 0.75 + 0.25 * sin(f.t * 37 + Float(i) * 2.1)
                let lv = laser * flick
                let e = o + dir * 3
                g.line(o, e, width: 0.014, rgba(c, 0.07 * lv))
                g.line(o, e, width: 0.0026, rgba(mix3(c, white, 0.35), min(1, 0.85 * lv)))
            }
            // Side lasers from the columns, scanning just over the crowd's heads.
            let scan = laser * min(1, laserSustain * 1.6)
            if scan > 0.05 {
                for side in sides {
                    let so = SIMD2<Float>(side * W * 0.72, vp.y + 0.72 * 0.72)
                    for i in 0..<9 {
                        let u = Float(i) / 8
                        let tx = -side * (a * 0.9) * (u * 2 - 1) * 0.9 + 0.6 * sin(stageSweep * 1.7 + side)
                        let e = SIMD2<Float>(tx, crowdTop + 0.03 + 0.05 * sin(stageSweep * 2.3 + Float(i)))
                        let c = i % 2 == 1 ? lasB : lasA
                        g.line(so, e, width: 0.012, rgba(c, 0.06 * scan))
                        g.line(so, e, width: 0.0024, rgba(mix3(c, white, 0.3), min(1, 0.8 * scan)))
                    }
                    g.softDot(so, radius: 0.06, rgba(mix3(lasA, white, 0.6), 0.6 * min(1, scan)), segments: 12)
                }
            }
            g.softDot(o, radius: 0.12 + 0.06 * laser, rgba(mix3(lasA, white, 0.6), 0.55 * min(1, laser)), segments: 16)
        }

        // 5. The crowd: silhouettes bouncing to the bass, some arms up, phones glowing.
        if stageCrowd.isEmpty {
            var r = FXRandom(seed: 0xC0FFEE)
            for row in 0..<3 {                       // 0 = far, 2 = right in front of you
                let head = 0.03 + Float(row * row) * 0.017
                var x: Float = -3.2
                while x < 3.2 {
                    let hd = head * r.range(0.85, 1.15)
                    let phase = r.range(0, 6.28)
                    var arm: Float = 0
                    if r.next() < (row == 2 ? 0.22 : 0.3) { arm = r.next() < 0.5 ? -1 : 1 }
                    let phone = arm != 0 && r.next() < 0.6
                    let hOff = r.range(-0.02, 0.02) * Float(1 + row)
                    stageCrowd.append(CrowdPerson(x: x, row: row, head: hd, phase: phase, arm: arm, phone: phone, hOff: hOff))
                    x += head * 3.1 * r.range(0.75, 1.3)
                }
            }
        }
        stageBounce += dt * (2.2 + bass * 2)
        let ink: GeometryBuilder.RGBA = (0.006, 0.006, 0.012, 1)
        let rowY: [Float] = [-0.6, -0.66, -0.75]
        for p in stageCrowd where abs(p.x) <= a + 0.2 {
            let baseY = rowY[p.row] + p.hOff
            let hop = max(0, sin(stageBounce * 2 + p.phase)) * (0.006 + 0.02 * bass) * (1 + Float(p.row) * 0.4)
            let head = SIMD2<Float>(p.x, baseY + hop)
            let sh = p.head * 1.9
            over.quad(SIMD2(p.x - sh, -1.05), SIMD2(p.x + sh, -1.05), SIMD2(p.x + sh * 0.9, head.y - p.head * 1.1),
                      SIMD2(p.x - sh * 0.9, head.y - p.head * 1.1), ink)
            over.dot(head, radius: p.head, ink)
            if p.arm != 0 {
                let shoulder = SIMD2<Float>(p.x + p.arm * sh * 0.75, head.y - p.head * 1.2)
                let hand = SIMD2<Float>(p.x + p.arm * sh * 0.95 + 0.01 * sin(stageBounce + p.phase), head.y + p.head * 3.2 + hop)
                over.line(shoulder, hand, width: p.head * 0.55, ink)
                if p.phone {
                    let pw = p.head * 0.55, ph = p.head * 0.95
                    over.rect(x0: hand.x - pw, y0: hand.y, x1: hand.x + pw, y1: hand.y + ph * 2, ink)
                    g.rect(x0: hand.x - pw * 0.8, y0: hand.y + ph * 0.15, x1: hand.x + pw * 0.8, y1: hand.y + ph * 1.85,
                           rgba(mix3(haze, white, 0.55), 0.75))
                }
            }
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
