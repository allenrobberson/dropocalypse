import Foundation

// Splits a logo into layers and moves them. Same rules as windows/src/logo_layers.h.
//
// Every piece of the logo that doesn't touch another piece (each letter, icon or shape) becomes
// its own layer. Specks are folded into the nearest layer, and a logo with very many pieces keeps
// its largest ones as layers. The layers then jump on the beat in one of four ways.

public struct LogoLayer: Equatable {
    public var x0 = 0, y0 = 0, x1 = 0, y1 = 0 // bounding box in pixels, x1/y1 exclusive
    public var cx: Float = 0, cy: Float = 0   // centroid in pixels
    public var area = 0
}

public struct LayerSplit {
    public var width = 0, height = 0
    public var label: [Int] = []          // layer of each pixel, -1 = empty
    public var layers: [LogoLayer] = []   // ordered left to right (then top to bottom)

    /// The box `extractLayer` returns, as texture coordinates of the whole image (v down).
    public func uv(_ layer: Int) -> (u0: Float, v0: Float, u1: Float, v1: Float) {
        let l = layers[layer]
        return (Float(l.x0 - 1) / Float(width), Float(l.y0 - 1) / Float(height),
                Float(l.x1 + 1) / Float(width), Float(l.y1 + 1) / Float(height))
    }

    /// One layer's pixels (`rgba` is the whole image), cropped to its box with a 1-pixel clear border.
    public func extractLayer(_ rgba: [UInt8], _ layer: Int) -> (pixels: [UInt8], width: Int, height: Int) {
        let l = layers[layer]
        let w = l.x1 - l.x0 + 2, h = l.y1 - l.y0 + 2
        var out = [UInt8](repeating: 0, count: w * h * 4)
        for y in l.y0..<l.y1 {
            for x in l.x0..<l.x1 {
                let i = y * width + x
                guard label[i] == layer else { continue }
                let o = ((y - l.y0 + 1) * w + (x - l.x0 + 1)) * 4
                for c in 0..<4 { out[o + c] = rgba[i * 4 + c] }
            }
        }
        return (out, w, h)
    }
}

public enum LogoLayers {
    public static let maxLayers = 24

    /// `rgba` is width*height*4 bytes; only the alpha channel is read.
    public static func split(_ rgba: [UInt8], width: Int, height: Int, maxLayers: Int = LogoLayers.maxLayers) -> LayerSplit {
        var out = LayerSplit(width: width, height: height)
        let n = max(0, width * height)
        out.label = [Int](repeating: -1, count: n)
        guard width > 0, height > 0, rgba.count >= n * 4 else { return out }
        func alpha(_ i: Int) -> Int { Int(rgba[i * 4 + 3]) }
        func root(_ parent: inout [Int], _ a: Int) -> Int {
            var a = a
            while parent[a] != a { parent[a] = parent[parent[a]]; a = parent[a] }
            return a
        }

        // 1. Solid pixels joined to their 8 neighbours.
        var prov = [Int](repeating: -1, count: n)
        var parent: [Int] = []
        let offsets = [(-1, 0), (-1, -1), (0, -1), (1, -1)]
        for y in 0..<height {
            for x in 0..<width {
                let i = y * width + x
                if alpha(i) < 96 { continue }
                var best = -1
                for (ox, oy) in offsets {
                    let nx = x + ox, ny = y + oy
                    guard nx >= 0, nx < width, ny >= 0 else { continue }
                    let l = prov[ny * width + nx]
                    guard l >= 0 else { continue }
                    if best < 0 { best = l } else {
                        let ra = root(&parent, best), rb = root(&parent, l)
                        if ra != rb { parent[max(ra, rb)] = min(ra, rb) }
                    }
                }
                if best < 0 { best = parent.count; parent.append(best) }
                prov[i] = best
            }
        }
        if parent.isEmpty {
            out.layers = [LogoLayer(x0: 0, y0: 0, x1: width, y1: height, cx: Float(width) / 2, cy: Float(height) / 2, area: n)]
            for i in 0..<n where alpha(i) > 0 { out.label[i] = 0 }
            return out
        }

        // 2. Pieces and their sizes.
        var rootOf = [Int](repeating: -1, count: n)
        var area = [Int](repeating: 0, count: parent.count)
        var sx = [Double](repeating: 0, count: parent.count), sy = sx
        var total = 0
        for i in 0..<n where prov[i] >= 0 {
            let r = root(&parent, prov[i])
            rootOf[i] = r
            area[r] += 1
            sx[r] += Double(i % width)
            sy[r] += Double(i / width)
            total += 1
        }
        let pieces = (0..<parent.count).filter { area[$0] > 0 }.sorted { area[$0] > area[$1] }

        // 3. Keep the larger pieces; fold specks and extras into the nearest kept piece.
        let dust = max(4, total / 500)
        var kept: [Int] = []
        for r in pieces where kept.count < max(1, maxLayers) && (area[r] >= dust || kept.isEmpty) { kept.append(r) }
        var target = [Int](repeating: -1, count: parent.count)
        for (k, r) in kept.enumerated() { target[r] = k }
        for r in pieces where target[r] < 0 {
            let px = sx[r] / Double(area[r]), py = sy[r] / Double(area[r])
            var bestD = Double.greatestFiniteMagnitude, best = 0
            for (k, q) in kept.enumerated() {
                let dx = sx[q] / Double(area[q]) - px, dy = sy[q] / Double(area[q]) - py
                let d = dx * dx + dy * dy
                if d < bestD { bestD = d; best = k }
            }
            target[r] = best
        }
        for i in 0..<n where rootOf[i] >= 0 { out.label[i] = target[rootOf[i]] }

        // 4. Soft edges join the layer beside them.
        for _ in 0..<3 {
            var next = out.label
            var changed = false
            for y in 0..<height {
                for x in 0..<width {
                    let i = y * width + x
                    guard out.label[i] < 0, alpha(i) > 0 else { continue }
                    search: for dy in -1...1 {
                        for dx in -1...1 {
                            let xx = x + dx, yy = y + dy
                            guard xx >= 0, yy >= 0, xx < width, yy < height else { continue }
                            let l = out.label[yy * width + xx]
                            if l >= 0 { next[i] = l; changed = true; break search }
                        }
                    }
                }
            }
            out.label = next
            if !changed { break }
        }

        // 5. Boxes and centres; then number the layers left to right.
        let k = kept.count
        var layers = [LogoLayer](repeating: LogoLayer(x0: width, y0: height, x1: 0, y1: 0), count: k)
        var lx = [Double](repeating: 0, count: k), ly = lx
        for i in 0..<n {
            let l = out.label[i]
            guard l >= 0 else { continue }
            let x = i % width, y = i / width
            layers[l].x0 = min(layers[l].x0, x); layers[l].y0 = min(layers[l].y0, y)
            layers[l].x1 = max(layers[l].x1, x + 1); layers[l].y1 = max(layers[l].y1, y + 1)
            layers[l].area += 1
            lx[l] += Double(x); ly[l] += Double(y)
        }
        for l in 0..<k where layers[l].area > 0 {
            layers[l].cx = Float(lx[l] / Double(layers[l].area))
            layers[l].cy = Float(ly[l] / Double(layers[l].area))
        }
        let order = (0..<k).sorted { a, b in
            let ax = layers[a].cx, bx = layers[b].cx
            if abs(ax - bx) > Float(width) * 0.02 { return ax < bx }
            return layers[a].cy < layers[b].cy
        }
        var rank = [Int](repeating: 0, count: k)
        for (r, l) in order.enumerated() { rank[l] = r }
        for i in 0..<n where out.label[i] >= 0 { out.label[i] = rank[out.label[i]] }
        out.layers = order.map { layers[$0] }
        return out
    }
}

// MARK: - Layer motion

public enum LayerMode: Int, CaseIterable {
    case off = 0, wave, explode, bands, scatter
    public var name: String { ["Off", "Wave", "Explode", "Bands", "Scatter"][rawValue] }
}

/// Offset of one layer from where it sits in the logo. x and y are in units of the logo's longer half
/// side (y up), rotation in radians, scale 1 = unchanged.
public struct LayerPose: Equatable {
    public var x: Float = 0, y: Float = 0, rotation: Float = 0, scale: Float = 1
    public init(x: Float = 0, y: Float = 0, rotation: Float = 0, scale: Float = 1) {
        self.x = x; self.y = y; self.rotation = rotation; self.scale = scale
    }
}

/// Springs that pull every layer back into place after the beat kicks it.
public struct LayerMotion {
    private struct State {
        var p = LayerPose()
        var vx: Float = 0, vy: Float = 0, vr: Float = 0, vs: Float = 0
        var kick = LayerPose(x: 0, y: 0, rotation: 0, scale: 0)
        var kickAt: Float = 0
        var pending = false
    }
    private var state: [State] = []
    private var lastBeat = 0
    private var time: Float = 0

    public init() {}

    private static func hash01(_ v: UInt32) -> Float {
        var x = v
        x ^= x >> 16; x = x &* 0x7feb352d; x ^= x >> 15; x = x &* 0x846ca68b; x ^= x >> 16
        return Float(x & 0xffffff) / 16_777_216
    }

    /// `nx`/`ny`: each layer's centre relative to the logo's centre, -1..1 (y up).
    public mutating func update(dt: Float, mode: LayerMode, amount: Float, nx: [Float], ny: [Float],
                                bass: Float, mid: Float, treble: Float, beatCount: Int, beatStrength: Float) {
        let count = nx.count
        if state.count != count { state = [State](repeating: State(), count: count) }
        let dt = min(max(dt, 0), 0.05)
        time += dt
        guard mode != .off, count > 0 else {
            state = [State](repeating: State(), count: count)
            lastBeat = beatCount
            return
        }
        let amount = max(0, amount)
        let beat = beatCount != lastBeat && lastBeat != 0
        lastBeat = beatCount
        let punch = 0.6 + 0.6 * min(beatStrength, 1)

        for i in 0..<count {
            var s = state[i]
            if beat {
                switch mode {
                case .wave:
                    s.kickAt = time + (nx[i] + 1) * 0.5 * 0.32
                    s.kick = LayerPose(x: 0, y: 5.0 * amount * punch, rotation: 0, scale: 0.8 * amount * punch)
                    s.pending = true
                case .explode:
                    if beatStrength >= 0.35 {
                        var dx = nx[i], dy = ny[i]
                        var len = (dx * dx + dy * dy).squareRoot()
                        if len < 0.08 {
                            let a = Self.hash01(UInt32(i) &* 7919 &+ 13) * 6.2832
                            dx = cos(a); dy = sin(a); len = 1
                        }
                        s.vx += dx / len * 7.0 * amount * punch
                        s.vy += dy / len * 7.0 * amount * punch
                        s.vr += (Self.hash01(UInt32(i) &* 31 &+ UInt32(truncatingIfNeeded: beatCount)) - 0.5) * 6 * amount
                        s.vs += 1.2 * amount * punch
                    }
                case .scatter:
                    let seed = UInt32(truncatingIfNeeded: beatCount) &* 2_654_435_761 &+ UInt32(i) &* 40503
                    let a = Self.hash01(seed) * 6.2832
                    let sp = 3.2 + 3.2 * Self.hash01(seed ^ 0x9e37_79b9)
                    s.vx += cos(a) * sp * amount * punch
                    s.vy += sin(a) * sp * amount * punch
                    s.vr += (Self.hash01(seed &* 3 &+ 1) - 0.5) * 9 * amount
                    s.vs += (Self.hash01(seed &* 5 &+ 2) - 0.3) * 1.4 * amount
                default:
                    break
                }
            }
            if s.pending, time >= s.kickAt {
                s.pending = false
                s.vx += s.kick.x; s.vy += s.kick.y; s.vr += s.kick.rotation; s.vs += s.kick.scale
            }
            let tx: Float = 0
            var ty: Float = 0, ts: Float = 1
            if mode == .bands {
                let level = min([bass, mid, treble][i % 3], 1.4)
                ty = level * 0.35 * amount
                ts = 1 + level * 0.22 * amount
            }
            let k: Float = 170, c: Float = 13, kr: Float = 120, cr: Float = 11
            s.vx += (-k * (s.p.x - tx) - c * s.vx) * dt
            s.vy += (-k * (s.p.y - ty) - c * s.vy) * dt
            s.vr += (-kr * s.p.rotation - cr * s.vr) * dt
            s.vs += (-k * (s.p.scale - ts) - c * s.vs) * dt
            s.p.x += s.vx * dt
            s.p.y += s.vy * dt
            s.p.rotation += s.vr * dt
            s.p.scale = max(0.2, s.p.scale + s.vs * dt)
            state[i] = s
        }
    }

    public func pose(_ i: Int) -> LayerPose { i < state.count ? state[i].p : LayerPose() }
}

/// Where to draw one layer: the item's centre, half size and rotation, the layer's box within the
/// item's image (0..1, v down) and its pose. Same units as the logo shader (height units).
public struct LayerQuad: Equatable {
    public var cx: Float, cy: Float, hx: Float, hy: Float, rotation: Float

    public init(itemCx: Float, itemCy: Float, itemHx: Float, itemHy: Float, itemRotation: Float,
                u0: Float, v0: Float, u1: Float, v1: Float, pose p: LayerPose) {
        let reach = max(itemHx, itemHy) // moves scale with the item's longer side
        let lx = ((u0 + u1) - 1) * itemHx + p.x * reach
        let ly = (1 - (v0 + v1)) * itemHy + p.y * reach
        let c = cos(itemRotation), s = sin(itemRotation)
        cx = itemCx + c * lx - s * ly
        cy = itemCy + s * lx + c * ly
        hx = (u1 - u0) * itemHx * p.scale
        hy = (v1 - v0) * itemHy * p.scale
        rotation = itemRotation + p.rotation
    }
}

// MARK: - Kinetic Text

/// Rows of the first logo or text item sliding across the screen in alternating directions; the
/// row the beat lands on lights up. Units are the logo shader's: y -1...1 (up), x -aspect...aspect.
public struct KineticRow: Equatable {
    public var y: Float = 0, hx: Float = 0, hy: Float = 0   // row centre and each copy's half size
    public var x0: Float = 0, spacing: Float = 0            // first copy's centre, distance between copies
    public var opacity: Float = 0
    public var copies = 0
}

public enum KineticText {
    public static let rows = 7

    public static func layout(time t: Float, aspect rawAspect: Float, itemAspect: Float, beatCount: Int,
                              pulse: Float, bass: Float) -> [KineticRow] {
        let aspect = max(rawAspect, 0.1)
        let rowH = 2 / Float(rows)
        let lit = ((beatCount % rows) + rows) % rows
        return (0..<rows).map { r in
            var row = KineticRow()
            var hy = rowH * 0.36
            var hx = hy * max(itemAspect, 0.2)
            if hx > aspect * 0.9 { // very wide items: shrink to fit the screen
                hy *= aspect * 0.9 / hx
                hx = aspect * 0.9
            }
            let isLit = r == lit
            let grow: Float = isLit ? 1 + 0.12 * pulse : 1
            row.hx = hx * grow
            row.hy = hy * grow
            row.y = 1 - (Float(r) + 0.5) * rowH
            row.spacing = hx * 2 + hy * 0.9
            let dir: Float = r % 2 == 1 ? -1 : 1
            let speed = 0.12 + 0.05 * Float(r % 3) + 0.08 * min(bass, 1.5)
            let s = t * speed * dir
            let off = s - (s / row.spacing).rounded(.down) * row.spacing
            row.x0 = -aspect - row.spacing + off
            row.copies = min(40, Int(((2 * aspect + 2 * row.spacing) / row.spacing).rounded(.up)) + 1)
            row.opacity = min(1, 0.22 + (isLit ? 0.55 * pulse : 0) + 0.06 * min(bass, 1.5))
            return row
        }
    }
}
