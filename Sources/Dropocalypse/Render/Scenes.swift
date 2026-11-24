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
    // Classic favorites (StyleFX.swift)
    case bassRing        // circular spectrum hugging the logo, with sparks
    case neonEqualizer   // mirrored neon bars with reflections
    case pulsarRidges    // stacked ridgelines (joy plot)
    case outrunGrid      // synthwave grid and striped sun
    case hyperspace      // warp-speed starfield
    case particleStorm   // flowing particle swarm
    case lightning       // lightning bolts on the beat
    case digitalRain     // falling glyph columns
    case lavaLamp        // drifting soft blobs
    case inferno         // rising embers and flames
    // Equalizer family (StyleFX.swift)
    case mirrorEqualizer // bars mirrored up/down from a center line, sunset rainbow
    case ledMeter        // segmented green-yellow-red LEDs with peak hold
    case rainbowPeaks    // thin rainbow bars with bouncing glowing caps
    case tesseract       // a 4D hypercube rotating through the fourth dimension
    case fullKaleidoscope // full-frame 4-way / 8-way mandala
    case mainStage       // arena floor: cathedral beam vaults, lasers, crowd
    case monstercatBars  // a row of tidy white-ish spectrum bars above the logo
    case lofiChill       // a night town under a big moon, snow and fog
    case glitchVHS       // a spectrum and sweep line; tape damage comes from fs_present
    case liquidChrome    // polished flowing metal (the warp shader's chrome field)
    case kineticText     // rows of the first item sliding across (drawn by the renderer)
    case milkdrop        // a MilkDrop preset played by projectM (Milkdrop.swift); `milk` says which
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
    /// 0 = ordinary feedback. 1 = the Full Kaleidoscope field (see the warp shader).
    var generative: Float = 0
    /// 0 = off. 1 = the Liquid Chrome field replaces the feedback (warp shader).
    var chrome: Float = 0
    /// 0 = off. 1 = VHS tape damage in the final pass (Glitch VHS).
    var glitch: Float = 0

    // Foreground drawing.
    var wave: WaveStyle = .circle
    /// Radius of the shape's center, in screen-height units. 0 (or close to it) makes the
    /// waveform burst straight out of the middle with no hollow ring.
    var centerRadius: Float = 0.32
    var waveScale: Float = 1
    var thickness: Float = 1
    var baseHue: Float = 0          // 0…1
    var hueSpeed: Float = 0.03      // palette drift per second
    var saturation: Float = 0.9
    var vignette: Float = 0.35
    /// For `.milkdrop`: index into the MilkDrop preset list (bundled first, then the user's
    /// folder). -1 for every built-in style.
    var milk: Int = -1
}

extension VisualScene {
    static let builtInCount = 31

    /// Every style: the 31 built-ins, then one per MilkDrop preset. Main thread only.
    private(set) static var all: [VisualScene] = builtIn

    /// Replaces the MilkDrop styles with one per name (bundled presets first, then the user's folder).
    static func setMilkdrop(names: [String]) {
        all = builtIn + names.enumerated().map { i, name in
            // Our own feedback layer rests quietly underneath while a preset plays.
            var s = VisualScene(name: name, zoom: 1, zoomBass: 0, warpAmount: 0, decay: 0.8, hueShift: 0,
                                wave: .milkdrop, vignette: 0.15)
            s.milk = i
            return s
        }
    }
}

extension VisualScene {
    static let builtIn: [VisualScene] = [
        VisualScene(name: "Classic Tunnel",
              zoom: 1.025, zoomBass: 0.05, rotation: 0.004, rotationBass: 0.01,
              warpAmount: 0.003, decay: 0.95, hueShift: 0.006,
              wave: .circle, centerRadius: 0.03, waveScale: 1, thickness: 1.2, baseHue: 0.55),

        VisualScene(name: "Spectrum Skyline",
              zoom: 1.0, zoomBass: 0.006, warpAmount: 0.0015, decay: 0.9,
              driftY: -0.006, hueShift: 0.002,
              wave: .bars, waveScale: 1, thickness: 1, baseHue: 0.75, hueSpeed: 0.05, vignette: 0.2),

        VisualScene(name: "Starburst",
              zoom: 1.045, zoomBass: 0.06, rotation: -0.002, rotationBass: -0.012,
              warpAmount: 0.002, decay: 0.93, hueShift: 0.01,
              wave: .radialBars, centerRadius: 0, waveScale: 1.1, thickness: 1, baseHue: 0.08),

        VisualScene(name: "Oscilloscope Drift",
              zoom: 1.006, zoomBass: 0.02, rotation: 0.0015,
              warpAmount: 0.006, warpScale: 4, warpSpeed: 0.6, decay: 0.965,
              driftY: -0.002, hueShift: 0.003,
              wave: .line, waveScale: 1, thickness: 1.4, baseHue: 0.33),

        VisualScene(name: "Kaleido Bloom",
              zoom: 1.012, zoomBass: 0.035, rotation: 0.006, rotationBass: 0.015,
              warpAmount: 0.003, decay: 0.95, kaleido: 6, hueShift: 0.008,
              wave: .circle, centerRadius: 0.03, waveScale: 0.8, thickness: 1.1, baseHue: 0.9, hueSpeed: 0.06),

        VisualScene(name: "Liquid Plasma",
              zoom: 1.004, zoomBass: 0.025, rotation: 0.001,
              warpAmount: 0.012, warpScale: 5, warpSpeed: 1.3, decay: 0.975,
              hueShift: 0.005, plasma: 1,
              wave: .twinRings, centerRadius: 0.03, waveScale: 0.7, thickness: 0.8, baseHue: 0.6, saturation: 0.75),

        VisualScene(name: "Vortex",
              zoom: 1.03, zoomBass: 0.04, rotation: 0.025, rotationBass: 0.04,
              warpAmount: 0.002, decay: 0.94, hueShift: 0.012,
              wave: .spiralDots, centerRadius: 0, waveScale: 1, thickness: 1.2, baseHue: 0.15, hueSpeed: 0.08),

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
              wave: .twinRings, centerRadius: 0.2, waveScale: 1.25, thickness: 1, baseHue: 0.02, hueSpeed: 0.02, vignette: 0.5),

        // ---- Classic favorites ----

        VisualScene(name: "Bass Ring",
                    zoom: 1.006, zoomBass: 0.01, warpAmount: 0, decay: 0.8, hueShift: 0.002,
                    wave: .bassRing, baseHue: 0.55, hueSpeed: 0.03, saturation: 0.85, vignette: 0.45),

        VisualScene(name: "Neon Equalizer",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.55, hueShift: 0,
                    wave: .neonEqualizer, baseHue: 0.8, hueSpeed: 0.02, vignette: 0.3),

        VisualScene(name: "Pulsar Ridges",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.25, hueShift: 0,
                    wave: .pulsarRidges, baseHue: 0, hueSpeed: 0, saturation: 0, vignette: 0.3),

        VisualScene(name: "Outrun Grid",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.55, hueShift: 0,
                    wave: .outrunGrid, baseHue: 0.86, hueSpeed: 0, vignette: 0.35),

        VisualScene(name: "Hyperspace",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.7, hueShift: 0,
                    wave: .hyperspace, baseHue: 0.6, hueSpeed: 0.01, saturation: 0.35, vignette: 0.4),

        VisualScene(name: "Particle Storm",
                    zoom: 1.0, zoomBass: 0.004, warpAmount: 0.002, decay: 0.88, hueShift: 0.003,
                    wave: .particleStorm, baseHue: 0.5, hueSpeed: 0.04, saturation: 0.8),

        VisualScene(name: "Lightning Storm",
                    zoom: 1.006, zoomBass: 0.01, warpAmount: 0.003, decay: 0.78, hueShift: 0, plasma: 0.35,
                    wave: .lightning, baseHue: 0.66, hueSpeed: 0.01, saturation: 0.6, vignette: 0.5),

        VisualScene(name: "Digital Rain",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.6, hueShift: 0,
                    wave: .digitalRain, baseHue: 0.33, hueSpeed: 0),

        VisualScene(name: "Lava Lamp",
                    zoom: 1.0, zoomBass: 0.004, warpAmount: 0.012, warpScale: 3, warpSpeed: 0.5, decay: 0.9,
                    hueShift: 0.002, wave: .lavaLamp, baseHue: 0.95, hueSpeed: 0.015, saturation: 0.85, vignette: 0.4),

        VisualScene(name: "Inferno",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0.007, warpScale: 10, warpSpeed: 2.5, decay: 0.9,
                    driftY: -0.008, hueShift: 0, wave: .inferno, baseHue: 0.05, hueSpeed: 0, saturation: 0.95,
                    vignette: 0.45),

        // ---- Equalizer family ----

        VisualScene(name: "Mirror Equalizer",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.6, hueShift: 0,
                    wave: .mirrorEqualizer, baseHue: 0.93, hueSpeed: 0, vignette: 0.35),

        VisualScene(name: "LED Meter",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.35, hueShift: 0,
                    wave: .ledMeter, baseHue: 0.33, hueSpeed: 0, vignette: 0.3),

        VisualScene(name: "Rainbow Peaks",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.72, hueShift: 0,
                    wave: .rainbowPeaks, baseHue: 0, hueSpeed: 0.08, vignette: 0.3),

        VisualScene(name: "Tesseract",
                    zoom: 1.012, zoomBass: 0.03, rotation: 0.0015, warpAmount: 0.002, decay: 0.86, hueShift: 0.002,
                    wave: .tesseract, baseHue: 0.55, hueSpeed: 0.02, saturation: 0.9, vignette: 0.4),

        VisualScene(name: "Full Kaleidoscope",
                    zoom: 1.008, zoomBass: 0.048, rotation: 0.0012, rotationBass: 0.016,
                    warpAmount: 0.004, warpScale: 3.4, warpSpeed: 0.55, decay: 0.91,
                    mirror: 2, hueShift: 0.0015, generative: 1,
                    wave: .fullKaleidoscope, baseHue: 0.52, hueSpeed: 0.015, saturation: 1, vignette: 0.48),

        VisualScene(name: "Main Stage",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.84, hueShift: 0,
                    wave: .mainStage, baseHue: 0.08, hueSpeed: 0, vignette: 0.4),

        VisualScene(name: "Monstercat Bars",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.3, hueShift: 0,
                    wave: .monstercatBars, baseHue: 0.55, hueSpeed: 0.01, vignette: 0.3),

        VisualScene(name: "Lo-fi Chill",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.5, hueShift: 0,
                    wave: .lofiChill, baseHue: 0.75, hueSpeed: 0, vignette: 0.45),

        VisualScene(name: "Glitch VHS",
                    zoom: 1.006, zoomBass: 0.02, warpAmount: 0.001, decay: 0.72, hueShift: 0.003, glitch: 1,
                    wave: .glitchVHS, baseHue: 0.5, vignette: 0.5),

        VisualScene(name: "Liquid Chrome",
                    zoom: 1.0, zoomBass: 0, warpAmount: 0, decay: 0.9, hueShift: 0, chrome: 1,
                    wave: .liquidChrome, baseHue: 0.58, hueSpeed: 0.01, vignette: 0.4),

        VisualScene(name: "Kinetic Text",
                    zoom: 1.004, zoomBass: 0.01, warpAmount: 0.0015, decay: 0.86, hueShift: 0.004,
                    wave: .kineticText, baseHue: 0.9, hueSpeed: 0.02, vignette: 0.35),
    ]
}
