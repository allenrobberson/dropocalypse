import Foundation

/// What gets drawn into the feedback buffer each frame.
enum WaveStyle: Int, CaseIterable {
    case circle          // closed waveform ring
    case line            // horizontal oscilloscope
    case verticalLine    // vertical oscilloscope
    case bars            // spectrum bars along the bottom
    case radialBars      // spectrum rays from the center
    case spiralDots      // spectrum dots on a spinning spiral
    case lissajous       // phase-portrait scope
    case twinRings       // two counter-rotating rings
}

/// A built-in visual style. All motion is bass-reactive; these numbers set the character.
struct VisualScene {
    var name: String

    // Feedback warp (applied to the previous frame every frame, like MilkDrop).
    var zoom: Float = 1.01          // > 1 pulls the image outward, < 1 sucks it inward
    var zoomBass: Float = 0.03      // extra zoom per unit of bass
    var rotation: Float = 0.0       // radians per frame
    var rotationBass: Float = 0.0
    var warpAmount: Float = 0.004
    var warpScale: Float = 6
    var warpSpeed: Float = 0.8
    var decay: Float = 0.955        // how long trails last
    var driftX: Float = 0
    var driftY: Float = 0           // negative = content flows upward
    var kaleido: Float = 0          // number of mirror segments (0 = off)
    var mirror: Int32 = 0           // 0 none, 1 left/right, 2 four-way
    var hueShift: Float = 0.004     // hue rotation per frame
    var plasma: Float = 0

    // Foreground drawing.
    var wave: WaveStyle = .circle
    var waveScale: Float = 1
    var thickness: Float = 1
    var baseHue: Float = 0          // 0…1
    var hueSpeed: Float = 0.03      // palette drift per second
    var saturation: Float = 0.9
    var vignette: Float = 0.35
}

extension VisualScene {
    static let builtIn: [VisualScene] = [
        VisualScene(name: "Classic Tunnel",
              zoom: 1.025, zoomBass: 0.05, rotation: 0.004, rotationBass: 0.01,
              warpAmount: 0.003, decay: 0.95, hueShift: 0.006,
              wave: .circle, waveScale: 1, thickness: 1.2, baseHue: 0.55),

        VisualScene(name: "Spectrum Skyline",
              zoom: 1.0, zoomBass: 0.006, warpAmount: 0.0015, decay: 0.9,
              driftY: -0.006, hueShift: 0.002,
              wave: .bars, waveScale: 1, thickness: 1, baseHue: 0.75, hueSpeed: 0.05, vignette: 0.2),

        VisualScene(name: "Starburst",
              zoom: 1.045, zoomBass: 0.06, rotation: -0.002, rotationBass: -0.012,
              warpAmount: 0.002, decay: 0.93, hueShift: 0.01,
              wave: .radialBars, waveScale: 1.1, thickness: 1, baseHue: 0.08),

        VisualScene(name: "Oscilloscope Drift",
              zoom: 1.006, zoomBass: 0.02, rotation: 0.0015,
              warpAmount: 0.006, warpScale: 4, warpSpeed: 0.6, decay: 0.965,
              driftY: -0.002, hueShift: 0.003,
              wave: .line, waveScale: 1, thickness: 1.4, baseHue: 0.33),

        VisualScene(name: "Kaleido Bloom",
              zoom: 1.012, zoomBass: 0.035, rotation: 0.006, rotationBass: 0.015,
              warpAmount: 0.003, decay: 0.95, kaleido: 6, hueShift: 0.008,
              wave: .circle, waveScale: 0.8, thickness: 1.1, baseHue: 0.9, hueSpeed: 0.06),

        VisualScene(name: "Liquid Plasma",
              zoom: 1.004, zoomBass: 0.025, rotation: 0.001,
              warpAmount: 0.012, warpScale: 5, warpSpeed: 1.3, decay: 0.975,
              hueShift: 0.005, plasma: 1,
              wave: .twinRings, waveScale: 0.7, thickness: 0.8, baseHue: 0.6, saturation: 0.75),

        VisualScene(name: "Vortex",
              zoom: 1.03, zoomBass: 0.04, rotation: 0.025, rotationBass: 0.04,
              warpAmount: 0.002, decay: 0.94, hueShift: 0.012,
              wave: .spiralDots, waveScale: 1, thickness: 1.2, baseHue: 0.15, hueSpeed: 0.08),

        VisualScene(name: "Lissajous Neon",
              zoom: 1.008, zoomBass: 0.02, rotation: -0.004,
              warpAmount: 0.004, decay: 0.96, mirror: 1, hueShift: 0.004,
              wave: .lissajous, waveScale: 1.2, thickness: 1.1, baseHue: 0.45, saturation: 1),

        VisualScene(name: "Mirror Hall",
              zoom: 1.015, zoomBass: 0.03, rotation: 0.003,
              warpAmount: 0.005, warpScale: 8, decay: 0.955, mirror: 2, hueShift: 0.007,
              wave: .verticalLine, waveScale: 0.9, thickness: 1.2, baseHue: 0.8),

        VisualScene(name: "Black Hole",
              zoom: 0.975, zoomBass: -0.03, rotation: -0.012, rotationBass: -0.02,
              warpAmount: 0.003, decay: 0.965, hueShift: -0.006,
              wave: .twinRings, waveScale: 1.25, thickness: 1, baseHue: 0.02, hueSpeed: 0.02, vignette: 0.5),
    ]
}
