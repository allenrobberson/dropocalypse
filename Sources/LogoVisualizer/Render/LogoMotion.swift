import Foundation

/// Physics for the logo: everything is driven by the bass envelope (continuous, instant attack)
/// plus impulses on detected beats, integrated with damped springs so motion snaps on the hit
/// and settles naturally.
struct LogoMotion {
    struct Amounts {
        var pulse: Float
        var squash: Float
        var bounce: Float
        var slide: Float
        var wobble: Float
    }

    struct Pose {
        var x: Float = 0        // in screen-height units, 0 = center
        var y: Float = 0
        var scaleX: Float = 1
        var scaleY: Float = 1
        var rotation: Float = 0
    }

    private struct Spring {
        var x: Float = 0
        var v: Float = 0
        let k: Float
        let c: Float
        mutating func step(_ dt: Float) {
            let a = -k * x - c * v
            v += a * dt
            x += v * dt
        }
    }

    private var squash = Spring(k: 320, c: 15)
    private var slide = Spring(k: 38, c: 6.5)
    private var wobble = Spring(k: 130, c: 9)
    private var bounceY: Float = 0
    private var bounceV: Float = 0
    private var lastBeat = 0
    private var side: Float = 1
    private var time: Float = 0

    mutating func update(dt rawDt: Float, audio: AudioAnalysis.Snapshot, amounts a: Amounts) -> Pose {
        let dt = min(max(rawDt, 0), 1.0 / 20.0)
        time += dt
        let bass = min(audio.bass, 1.4)

        // Beat impulses.
        if audio.beatCount != lastBeat {
            let fresh = lastBeat != 0
            lastBeat = audio.beatCount
            if fresh {
                let s = audio.beatStrength
                side = -side
                squash.v += a.squash * 7.5 * s
                if bounceY < 0.05 { bounceV += a.bounce * 1.8 * s }
                slide.v += a.slide * 1.6 * s * side
                wobble.v += a.wobble * 4.0 * s * side
            }
        }

        // Integrate in small substeps so stiff springs stay stable at any frame rate.
        let steps = 4
        let h = dt / Float(steps)
        for _ in 0..<steps {
            squash.step(h)
            slide.step(h)
            wobble.step(h)

            bounceV -= 7.5 * h
            bounceY += bounceV * h
            if bounceY < 0 {
                let impact = -bounceV
                bounceY = 0
                bounceV = impact > 0.35 ? impact * 0.3 : 0
                if impact > 0.2 { squash.v += a.squash * min(impact, 2) * 1.5 }
            }
        }

        var pose = Pose()
        let pulse = 1 + a.pulse * 0.32 * bass
        let stretch = a.squash * 0.12 * bass       // continuous bass stretch, taller on the low end
        let sq = max(-0.45, min(0.45, squash.x))   // positive = wide & short
        pose.scaleX = pulse * (1 + sq) * (1 - stretch * 0.5)
        pose.scaleY = pulse * (1 - sq * 0.85) * (1 + stretch)
        pose.y = bounceY * 0.55
        pose.x = slide.x * 0.5 + sin(time * 0.45) * 0.04 * a.slide
        pose.rotation = wobble.x * 0.35 + sin(time * 0.3) * 0.02 * a.wobble
        return pose
    }
}
