import AppKit
import DropocalypseCore
import Metal

/// Converts the user's image into a premultiplied RGBA texture with mipmaps
/// (the blurred mip levels double as a cheap glow).
enum LogoTexture {
    static let maxSize = 1024

    static func make(from image: NSImage, device: MTLDevice, queue: MTLCommandQueue) -> MTLTexture? {
        var rect = CGRect(origin: .zero, size: image.size)
        guard let cg = image.cgImage(forProposedRect: &rect, context: nil, hints: nil) else { return nil }
        return make(from: cg, device: device, queue: queue)
    }

    static func make(from cg: CGImage, device: MTLDevice, queue: MTLCommandQueue) -> MTLTexture? {
        // Trim fully transparent borders so the logo bounces around its visible shape.
        let source = trimmed(cg) ?? cg
        let w0 = CGFloat(source.width), h0 = CGFloat(source.height)
        guard w0 > 0, h0 > 0 else { return nil }
        let scale = min(1, CGFloat(maxSize) / max(w0, h0))
        // Pad by 6% so the mip-blurred glow has room to spread.
        let pad = Int(max(w0, h0) * scale * 0.06)
        let w = Int(w0 * scale) + pad * 2
        let h = Int(h0 * scale) + pad * 2

        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .high
        ctx.clear(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.draw(source, in: CGRect(x: CGFloat(pad), y: CGFloat(pad), width: w0 * scale, height: h0 * scale))
        return upload(ctx, device: device, queue: queue)
    }

    /// A text item, drawn with a dark outline so it stays readable over bright visuals.
    /// `font` and `color` index `OverlayLayout.textFontNames` / `textColors`.
    static func text(_ raw: String, font: Int, color: Int, device: MTLDevice, queue: MTLCommandQueue) -> MTLTexture? {
        let text = OverlayLayout.cleanText(raw)
        guard !text.isEmpty else { return nil }
        var pointSize: CGFloat = 200
        func makeFont(_ size: CGFloat) -> NSFont {
            switch font {
            case 1: return NSFont.systemFont(ofSize: size, weight: .black)
            case 2:
                let base = NSFont.systemFont(ofSize: size, weight: .bold)
                if let serif = base.fontDescriptor.withDesign(.serif), let f = NSFont(descriptor: serif, size: size) {
                    return f
                }
                return NSFont(name: "Georgia-Bold", size: size) ?? base
            case 3: return NSFont.monospacedSystemFont(ofSize: size, weight: .bold)
            default: return NSFont.systemFont(ofSize: size, weight: .bold)
            }
        }
        var nsFont = makeFont(pointSize)
        var size = (text as NSString).size(withAttributes: [.font: nsFont])
        if size.width > 2400 {   // shrink very long text so it stays under 2400 px
            pointSize = max(60, pointSize * 2400 / size.width)
            nsFont = makeFont(pointSize)
            size = (text as NSString).size(withAttributes: [.font: nsFont])
        }
        let outline = max(4, pointSize / 18)
        let margin = outline + 4
        let w = Int(ceil(size.width + margin * 2)), h = Int(ceil(size.height + margin * 2))
        guard w > 0, h > 0,
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.clear(CGRect(x: 0, y: 0, width: w, height: h))
        let rgb = OverlayLayout.textColor(color)
        let fill = NSColor(srgbRed: CGFloat((rgb >> 16) & 0xFF) / 255, green: CGFloat((rgb >> 8) & 0xFF) / 255,
                           blue: CGFloat(rgb & 0xFF) / 255, alpha: 1)
        let rect = CGRect(x: margin, y: margin, width: size.width + 1, height: size.height + 1)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
        // Outline first (a wide stroke, half of it outside the letters), then the fill on top.
        // strokeWidth is a percentage of the point size; positive means stroke only.
        NSAttributedString(string: text, attributes: [
            .font: nsFont,
            .strokeColor: NSColor.black.withAlphaComponent(0.75),
            .strokeWidth: outline * 2 / pointSize * 100,
        ]).draw(in: rect)
        NSAttributedString(string: text, attributes: [.font: nsFont, .foregroundColor: fill]).draw(in: rect)
        NSGraphicsContext.restoreGraphicsState()
        guard let image = ctx.makeImage() else { return nil }
        return make(from: image, device: device, queue: queue)
    }

    /// Shown until the user picks their own logo.
    static func placeholder(device: MTLDevice) -> MTLTexture? {
        let w = 1024, h = 400
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.clear(CGRect(x: 0, y: 0, width: w, height: h))
        let ns = NSGraphicsContext(cgContext: ctx, flipped: false)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = ns

        let para = NSMutableParagraphStyle()
        para.alignment = .center
        let title = NSAttributedString(string: "YOUR LOGO", attributes: [
            .font: NSFont.systemFont(ofSize: 150, weight: .black),
            .foregroundColor: NSColor.white,
            .paragraphStyle: para,
        ])
        let sub = NSAttributedString(string: "drop an image here or press L", attributes: [
            .font: NSFont.systemFont(ofSize: 44, weight: .semibold),
            .foregroundColor: NSColor.white.withAlphaComponent(0.85),
            .paragraphStyle: para,
        ])
        title.draw(in: CGRect(x: 0, y: 150, width: w, height: 200))
        sub.draw(in: CGRect(x: 0, y: 70, width: w, height: 70))

        NSGraphicsContext.restoreGraphicsState()
        guard let queue = device.makeCommandQueue() else { return nil }
        return upload(ctx, device: device, queue: queue)
    }

    private static func upload(_ ctx: CGContext, device: MTLDevice, queue: MTLCommandQueue) -> MTLTexture? {
        guard let data = ctx.data else { return nil }
        let w = ctx.width, h = ctx.height
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: true)
        d.usage = [.shaderRead]
        d.storageMode = .managed
        guard let tex = device.makeTexture(descriptor: d) else { return nil }
        tex.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0, withBytes: data, bytesPerRow: ctx.bytesPerRow)

        if let cb = queue.makeCommandBuffer(), let blit = cb.makeBlitCommandEncoder() {
            blit.generateMipmaps(for: tex)
            blit.endEncoding()
            cb.commit()
            cb.waitUntilCompleted()
        }
        return tex
    }

    /// Crops away rows/columns that are fully transparent.
    private static func trimmed(_ image: CGImage) -> CGImage? {
        let w = image.width, h = image.height
        guard w > 2, h > 2, w * h <= 64_000_000 else { return nil }
        switch image.alphaInfo {
        case .none, .noneSkipLast, .noneSkipFirst: return nil
        default: break
        }
        guard let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let raw = ctx.data else { return nil }
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
        let px = raw.bindMemory(to: UInt8.self, capacity: w * h * 4)

        var minX = w, minY = h, maxX = -1, maxY = -1
        for y in 0..<h {
            let row = y * w * 4
            for x in 0..<w where px[row + x * 4 + 3] > 8 {
                if x < minX { minX = x }
                if x > maxX { maxX = x }
                if y < minY { minY = y }
                if y > maxY { maxY = y }
            }
        }
        guard maxX >= minX, maxY >= minY else { return nil }
        if minX == 0, minY == 0, maxX == w - 1, maxY == h - 1 { return nil }
        // Memory row 0 is the top of the image, which matches CGImage.cropping's coordinates.
        return image.cropping(to: CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1))
    }
}
