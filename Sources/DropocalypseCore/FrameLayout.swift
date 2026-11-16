import Foundation

/// Where the visuals sit on screen, and the pixel size of an MP4.
///
/// Landscape fills the view (today's behavior). Portrait is a 9:16 frame centered
/// in the view: pillarboxed on a landscape display, letterboxed when the view is
/// taller than 9:16. Recording sizes follow the same choice, so the file matches
/// the frame on screen.
///
/// The Windows copy of this math lives in `windows/src/core_logic.h`. Keep them in step.
public enum FrameLayout {
    public static let portraitAspect: Double = 9.0 / 16.0

    public struct Size: Equatable {
        public var width: Int
        public var height: Int
        public init(width: Int, height: Int) {
            self.width = width
            self.height = height
        }
    }

    /// Origin is the top-left, in the same pixel space as the view.
    public struct Rect: Equatable {
        public var x: Double
        public var y: Double
        public var width: Double
        public var height: Double
        public init(x: Double, y: Double, width: Double, height: Double) {
            self.x = x
            self.y = y
            self.width = width
            self.height = height
        }
        public var aspect: Double { height > 0 ? width / height : 1 }
    }

    /// 0 = 720p, 1 = 1080p, 2 = 4K. Out-of-range qualities clamp.
    /// Portrait is 720×1280, 1080×1920, or 2160×3840. Frame rate does not change.
    public static func recordingSize(quality: Int, portrait: Bool) -> Size {
        let q = min(2, max(0, quality))
        let landscape: [(Int, Int)] = [(1280, 720), (1920, 1080), (3840, 2160)]
        let pair = landscape[q]
        if portrait { return Size(width: pair.1, height: pair.0) }
        return Size(width: pair.0, height: pair.1)
    }

    public static func contentRect(viewWidth: Double, viewHeight: Double, portrait: Bool) -> Rect {
        let vw = max(0, viewWidth)
        let vh = max(0, viewHeight)
        if !portrait || vw < 1 || vh < 1 {
            return Rect(x: 0, y: 0, width: vw, height: vh)
        }
        let target = portraitAspect
        if vw / vh > target {
            let h = vh
            let w = h * target
            return Rect(x: (vw - w) * 0.5, y: 0, width: w, height: h)
        }
        let w = vw
        let h = w / target
        return Rect(x: 0, y: (vh - h) * 0.5, width: w, height: h)
    }
}
