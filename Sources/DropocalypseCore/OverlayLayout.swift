import Foundation

/// Logos and text items: up to four on screen at once.
///
/// Units match the renderer: x runs from -aspect to aspect and y from -1 (bottom) to 1 (top).
/// Landscape (aspect >= 1) spaces the items evenly across the width; portrait stacks them top to
/// bottom. The Windows/Linux copy of this math lives in `windows/src/core_logic.h`. Keep them in step.
public enum OverlayLayout {
    public static let maxItems = 4

    public struct Slot: Equatable {
        public var cx: Float = 0
        public var cy: Float = 0
        public var maxHalfW: Float = 1
        public var maxHalfH: Float = 1
        public init(cx: Float = 0, cy: Float = 0, maxHalfW: Float = 1, maxHalfH: Float = 1) {
            self.cx = cx
            self.cy = cy
            self.maxHalfW = maxHalfW
            self.maxHalfH = maxHalfH
        }
    }

    /// Where item `index` of `count` sits. maxHalfW/maxHalfH bound its half size so neighbors
    /// don't overlap at rest (the beat can still push them a little past it).
    public static func slot(index: Int, count: Int, aspect rawAspect: Float) -> Slot {
        let n = min(max(count, 1), maxItems)
        let i = min(max(index, 0), n - 1)
        let aspect: Float = rawAspect > 0.05 ? rawAspect : 1
        if n == 1 { return Slot(cx: 0, cy: 0, maxHalfW: aspect * 0.95, maxHalfH: 0.95) }
        let spanFrac: Float = 0.86
        let gapFrac: Float = 0.86
        if aspect >= 1 {
            let span = 2 * aspect * spanFrac
            let cell = span / Float(n)
            return Slot(cx: -span * 0.5 + cell * (Float(i) + 0.5), cy: 0,
                        maxHalfW: cell * 0.5 * gapFrac, maxHalfH: 0.9)
        }
        let span = 2 * spanFrac
        let cell = span / Float(n)
        return Slot(cx: 0, cy: span * 0.5 - cell * (Float(i) + 0.5),
                    maxHalfW: aspect * 0.92, maxHalfH: cell * 0.5 * gapFrac)
    }

    /// Half size at rest: the longer side is `size` (the Logo panel's Size slider), then the
    /// item shrinks if needed to stay inside its slot. `imageAspect` is width / height.
    public static func extent(imageAspect rawAspect: Float, size: Float, slot: Slot) -> (halfW: Float, halfH: Float) {
        let imageAspect: Float = rawAspect > 0 ? rawAspect : 1
        var halfH = imageAspect >= 1 ? size / imageAspect : size
        var halfW = halfH * imageAspect
        var fit: Float = 1
        if halfW > slot.maxHalfW { fit = slot.maxHalfW / halfW }
        if halfH * fit > slot.maxHalfH { fit = slot.maxHalfH / halfH }
        halfW *= fit
        halfH *= fit
        return (halfW, halfH)
    }

    /// Each item reacts to the same beat with small differences so they don't move like clones.
    /// Item 0 is exactly the single-logo motion.
    public struct MotionVariant: Equatable {
        public var delay: Float      // seconds before the kick lands
        public var side: Float       // first slide / wobble direction
        public var phase: Float      // radians added to the slow drift
        public var amount: Float     // multiplies the beat impulses
        public init(delay: Float, side: Float, phase: Float, amount: Float) {
            self.delay = delay
            self.side = side
            self.phase = phase
            self.amount = amount
        }
    }

    public static func motionVariant(_ index: Int) -> MotionVariant {
        let v: [MotionVariant] = [
            MotionVariant(delay: 0, side: 1, phase: 0, amount: 1),
            MotionVariant(delay: 0.045, side: -1, phase: 1.7, amount: 0.92),
            MotionVariant(delay: 0.020, side: 1, phase: 3.1, amount: 1.06),
            MotionVariant(delay: 0.065, side: -1, phase: 4.6, amount: 0.96),
        ]
        return v[max(index, 0) % maxItems]
    }

    /// Text item colors (0xRRGGBB) and names, in the order the color buttons show them.
    public static let textColors: [UInt32] = [0xFFFFFF, 0xFF3EA5, 0x2EE6FF, 0xFFC83D, 0x9BFF3A, 0xA65CFF]
    public static let textColorNames = ["White", "Pink", "Cyan", "Gold", "Lime", "Purple"]
    /// Text item fonts. Each platform maps these to fonts it has.
    public static let textFontNames = ["Bold", "Heavy", "Serif", "Mono"]

    public static func textColor(_ index: Int) -> UInt32 {
        textColors.indices.contains(index) ? textColors[index] : textColors[0]
    }

    /// Text is stored on one line: line breaks become spaces, it is trimmed and capped at
    /// 60 characters.
    public static func cleanText(_ s: String) -> String {
        let oneLine = s.map { ($0 == "\n" || $0 == "\r" || $0 == "\t" || $0 == "\r\n") ? " " : String($0) }.joined()
        let trimmed = oneLine.trimmingCharacters(in: .whitespaces)
        return String(trimmed.prefix(60))
    }
}
