import MetalKit
import AppKit
import QuartzCore
import DropocalypseCore

// Uniform layouts. Plain Float/Int32 fields only, matching the structs in ShaderSource.swift.

private struct WarpUniforms {
    // Rotation and hue shift are the same for every pixel, so their cos/sin are worked out once
    // per frame here instead of in the shader.
    var aspect: Float = 1, time: Float = 0, zoom: Float = 1, rotCos: Float = 1
    var warpAmount: Float = 0, warpScale: Float = 6, warpSpeed: Float = 1, decay: Float = 0.95
    var driftX: Float = 0, driftY: Float = 0, kaleido: Float = 0, hueCos: Float = 1
    var plasma: Float = 0, bass: Float = 0, mid: Float = 0, treble: Float = 0
    var texelX: Float = 0, texelY: Float = 0
    var mirror: Int32 = 0
    var generative: Float = 0
    var rotSin: Float = 0, hueSin: Float = 0
    var chrome: Float = 0, chromeHue: Float = 0
}

private struct PresentUniforms {
    var flash: Float = 0, vignette: Float = 0.3, brightness: Float = 1, milkMix: Float = 0
    var glitch: Float = 0, glitchTime: Float = 0, glitchPulse: Float = 0, pad: Float = 0
}

private struct LogoUniforms {
    var cx: Float = 0, cy: Float = 0, hx: Float = 0.3, hy: Float = 0.3
    var rotation: Float = 0, aspect: Float = 1, opacity: Float = 1, mipBias: Float = 0
    var r: Float = 1, g: Float = 1, b: Float = 1, tintAmount: Float = 0
}

struct GeomVertex {
    var x: Float, y: Float, r: Float, g: Float, b: Float, a: Float
}

/// Draws one frame per display refresh:
///  1. warp the previous frame (zoom / rotate / ripple / fade) into a new feedback texture
///  2. add this frame's waveform, spectrum and (optionally) a logo trail on top
///  3. composite to the screen with the bouncing logo and its glow
final class Renderer: NSObject, MTKViewDelegate {
    let device: MTLDevice
    private let queue: MTLCommandQueue
    private let capture: AudioCapture
    private let settings: AppSettings
    private let model: VisualizerModel

    private var warpPipeline: MTLRenderPipelineState!
    private var geomPipeline: MTLRenderPipelineState!
    private var geomOverPipeline: MTLRenderPipelineState!
    private var presentPipeline: MTLRenderPipelineState!
    private var blitPipeline: MTLRenderPipelineState!
    private var logoPipeline: MTLRenderPipelineState!
    private var logoGlowPipeline: MTLRenderPipelineState!
    private var logoTrailPipeline: MTLRenderPipelineState!
    private var signalPipeline: MTLRenderPipelineState!

    private var feedback: [MTLTexture] = []
    private var feedbackIndex = 0
    private let feedbackFormat: MTLPixelFormat = .rgba16Float

    private static let maxVertices = 96_000
    private static let additiveCapacity = 52_000 // the rest is for the alpha-blended "over" batch
    private var vertexBuffers: [MTLBuffer] = []
    private var bufferIndex = 0
    private let inflight = DispatchSemaphore(value: 3)

    /// Logos and text items, left to right (top to bottom in portrait). Never empty: with no
    /// items it holds the "YOUR LOGO" placeholder.
    private var itemTextures: [MTLTexture] = []
    /// Textures by item key, so editing one text item doesn't reload every image.
    private var textureCache: [String: MTLTexture] = [:]
    private var placeholderTexture: MTLTexture?
    /// Wave (512) and band (48) samples for vs_signal. Copied into the command stream with
    /// setVertexBytes each frame, so frames still in flight never see the next frame's samples.
    private static let signalSampleCount = 560
    private var signalScratch = [Float](repeating: 0, count: signalSampleCount)

    private let spectrum = Spectrum()
    private let fx = StyleFX()
    private var motions = [LogoMotion](repeating: LogoMotion(), count: OverlayLayout.maxItems)
    private var lastTime = CACurrentMediaTime()
    private var startTime = CACurrentMediaTime()
    private var flash: Float = 0
    private var lastFlashBeat = 0
    private var glitchPulse: Float = 0
    private var lastGlitchBeat = 0

    /// Each item split into its separate shapes (LogoLayers.swift), one texture per shape.
    private final class ItemLayers {
        var textures: [MTLTexture] = []
        var uv: [(u0: Float, v0: Float, u1: Float, v1: Float)] = []
        var nx: [Float] = [], ny: [Float] = []
        var motion = LayerMotion()
    }
    /// Parallel to itemTextures; nil when an item is a single shape.
    private var itemLayers: [ItemLayers?] = []
    private var layerCache: [String: ItemLayers] = [:]

    // VisualScene blending: crossfade warp/draw parameters when the scene changes.
    private var currentScene: VisualScene = VisualScene.all[0]
    private var previousScene: VisualScene = VisualScene.all[0]
    private var sceneBlend: Float = 1
    private var activeSceneIndex = -1
    private var sceneStartTime = CACurrentMediaTime()

    private var shaderError: String?

    // MilkDrop presets (Milkdrop.swift): the engine, and which preset it has loaded.
    private lazy var milk = MilkdropHost(device: device)
    private var milkLoaded = -1
    private var milkGeneration = -1
    private var milkSamples = [Float](repeating: 0, count: 2048)

    init?(view: MTKView, capture: AudioCapture, settings: AppSettings, model: VisualizerModel) {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else { return nil }
        self.device = device
        self.queue = queue
        self.capture = capture
        self.settings = settings
        self.model = model
        super.init()

        view.device = device
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true    // keeps presentation fast; the recorder draws its own copy
        view.preferredFramesPerSecond = 60 // MetalView retargets this to the display, or the Look panel cap
        view.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        if let layer = view.layer as? CAMetalLayer {
            // Two drawables instead of three removes a frame of queueing latency.
            layer.maximumDrawableCount = 2
            layer.displaySyncEnabled = true
        }

        do {
            try buildPipelines(pixelFormat: view.colorPixelFormat)
        } catch {
            shaderError = "\(error)"
            model.reportError("Shader compile failed: \(error.localizedDescription)")
        }

        for _ in 0..<3 {
            if let b = device.makeBuffer(length: Self.maxVertices * MemoryLayout<GeomVertex>.stride,
                                         options: .storageModeShared) {
                vertexBuffers.append(b)
            }
        }

        placeholderTexture = LogoTexture.placeholder(device: device)
        itemTextures = placeholderTexture.map { [$0] } ?? []
    }

    // MARK: Logos and text

    enum ItemSource {
        /// `key` identifies the image (its saved file) so its texture is reused.
        case image(key: String, NSImage)
        case text(String, font: Int, color: Int)

        var cacheKey: String {
            switch self {
            case .image(let key, _): return "image:" + key
            case .text(let text, let font, let color): return "text:\(font):\(color):" + text
            }
        }
    }

    /// The items to show, in order (up to OverlayLayout.maxItems). An empty list shows the placeholder.
    func setItems(_ sources: [ItemSource]) {
        var textures: [MTLTexture] = []
        var cache: [String: MTLTexture] = [:]
        for source in sources.prefix(OverlayLayout.maxItems) {
            let key = source.cacheKey
            var tex = textureCache[key] ?? cache[key]
            if tex == nil {
                switch source {
                case .image(_, let image): tex = LogoTexture.make(from: image, device: device, queue: queue)
                case .text(let text, let font, let color):
                    tex = LogoTexture.text(text, font: font, color: color, device: device, queue: queue)
                }
            }
            if let tex {
                textures.append(tex)
                cache[key] = tex
            }
        }
        textureCache = cache
        // Split each item into its shapes once, reusing the split while the item is unchanged.
        var layers: [ItemLayers?] = []
        var newLayerCache: [String: ItemLayers] = [:]
        for source in sources.prefix(OverlayLayout.maxItems) {
            let key = source.cacheKey
            guard let tex = cache[key] else { continue }
            if let known = layerCache[key] ?? newLayerCache[key] {
                newLayerCache[key] = known
                layers.append(known.textures.count > 1 ? known : nil)
                continue
            }
            let made = makeLayers(tex)
            newLayerCache[key] = made
            layers.append(made.textures.count > 1 ? made : nil)
        }
        layerCache = newLayerCache
        if textures.isEmpty, let placeholder = placeholderTexture {
            textures = [placeholder]
            layers = [nil]
        }
        itemTextures = textures
        itemLayers = layers
    }

    /// Reads an item's pixels back (its texture is CPU-visible) and makes a texture per shape.
    private func makeLayers(_ tex: MTLTexture) -> ItemLayers {
        let result = ItemLayers()
        let w = tex.width, h = tex.height
        guard w > 0, h > 0, w * h <= 4096 * 4096, tex.storageMode != .private else { return result }
        var rgba = [UInt8](repeating: 0, count: w * h * 4)
        rgba.withUnsafeMutableBytes { buf in
            tex.getBytes(buf.baseAddress!, bytesPerRow: w * 4, from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0)
        }
        let split = LogoLayers.split(rgba, width: w, height: h)
        guard split.layers.count > 1 else { return result }
        for l in 0..<split.layers.count {
            let piece = split.extractLayer(rgba, l)
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: piece.width,
                                                             height: piece.height, mipmapped: true)
            d.usage = [.shaderRead]
            d.storageMode = .managed
            guard let lt = device.makeTexture(descriptor: d) else { return ItemLayers() }
            piece.pixels.withUnsafeBytes { buf in
                lt.replace(region: MTLRegionMake2D(0, 0, piece.width, piece.height), mipmapLevel: 0,
                           withBytes: buf.baseAddress!, bytesPerRow: piece.width * 4)
            }
            result.textures.append(lt)
            result.uv.append(split.uv(l))
            result.nx.append(split.layers[l].cx / Float(w) * 2 - 1)
            result.ny.append(1 - split.layers[l].cy / Float(h) * 2)
        }
        if let cb = queue.makeCommandBuffer(), let blit = cb.makeBlitCommandEncoder() {
            for lt in result.textures { blit.generateMipmaps(for: lt) }
            blit.endEncoding()
            cb.commit()
            cb.waitUntilCompleted()
        }
        return result
    }

    // MARK: Pipelines

    private func buildPipelines(pixelFormat: MTLPixelFormat) throws {
        let library = try device.makeLibrary(source: ShaderSource.metal, options: nil)
        func fn(_ name: String) throws -> MTLFunction {
            guard let f = library.makeFunction(name: name) else {
                throw NSError(domain: "Dropocalypse", code: 1,
                              userInfo: [NSLocalizedDescriptionKey: "Missing shader function \(name)"])
            }
            return f
        }

        enum Blend { case none, additive, premultiplied }
        func make(_ v: String, _ f: String, _ format: MTLPixelFormat, _ blend: Blend) throws -> MTLRenderPipelineState {
            let d = MTLRenderPipelineDescriptor()
            d.vertexFunction = try fn(v)
            d.fragmentFunction = try fn(f)
            let att = d.colorAttachments[0]!
            att.pixelFormat = format
            switch blend {
            case .none:
                att.isBlendingEnabled = false
            case .additive:
                att.isBlendingEnabled = true
                att.rgbBlendOperation = .add
                att.alphaBlendOperation = .add
                att.sourceRGBBlendFactor = .one
                att.destinationRGBBlendFactor = .one
                att.sourceAlphaBlendFactor = .one
                att.destinationAlphaBlendFactor = .one
            case .premultiplied:
                att.isBlendingEnabled = true
                att.rgbBlendOperation = .add
                att.alphaBlendOperation = .add
                att.sourceRGBBlendFactor = .one
                att.destinationRGBBlendFactor = .oneMinusSourceAlpha
                att.sourceAlphaBlendFactor = .one
                att.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            }
            return try device.makeRenderPipelineState(descriptor: d)
        }

        warpPipeline = try make("vs_fullscreen", "fs_warp", feedbackFormat, .none)
        geomPipeline = try make("vs_geom", "fs_geom", feedbackFormat, .additive)
        geomOverPipeline = try make("vs_geom", "fs_geom", feedbackFormat, .premultiplied)
        logoTrailPipeline = try make("vs_logo", "fs_logo", feedbackFormat, .premultiplied)
        presentPipeline = try make("vs_fullscreen", "fs_present", pixelFormat, .none)
        blitPipeline = try make("vs_fullscreen", "fs_blit", .bgra8Unorm, .none)
        logoGlowPipeline = try make("vs_logo", "fs_logo", pixelFormat, .additive)
        logoPipeline = try make("vs_logo", "fs_logo", pixelFormat, .premultiplied)
        signalPipeline = try make("vs_signal", "fs_geom", feedbackFormat, .additive)
    }

    // MARK: Feedback textures

    private func ensureFeedback(for drawableSize: CGSize) {
        // Cap the feedback buffer so 4K/5K displays stay smooth; it's upscaled when presented.
        let maxDim: CGFloat = 1920
        let scale = min(1, maxDim / max(drawableSize.width, drawableSize.height))
        let w = max(16, Int(drawableSize.width * scale))
        let h = max(16, Int(drawableSize.height * scale))
        if let t = feedback.first, t.width == w, t.height == h { return }

        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: feedbackFormat, width: w, height: h, mipmapped: false)
        d.usage = [.renderTarget, .shaderRead]
        d.storageMode = .private
        feedback = (0..<2).compactMap { _ in device.makeTexture(descriptor: d) }
        feedbackIndex = 0

        // Clear both to black.
        if let cb = queue.makeCommandBuffer() {
            for t in feedback {
                let rp = MTLRenderPassDescriptor()
                rp.colorAttachments[0].texture = t
                rp.colorAttachments[0].loadAction = .clear
                rp.colorAttachments[0].storeAction = .store
                rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
                cb.makeRenderCommandEncoder(descriptor: rp)?.endEncoding()
            }
            cb.commit()
        }
    }

    // MARK: MTKViewDelegate

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        let content = FrameLayout.contentRect(viewWidth: Double(size.width), viewHeight: Double(size.height),
                                              portrait: settings.pictureOrientation != 0)
        ensureFeedback(for: CGSize(width: content.width, height: content.height))
    }

    func draw(in view: MTKView) {
        guard shaderError == nil, warpPipeline != nil else { return }
        let size = view.drawableSize
        guard size.width > 0, size.height > 0 else { return }
        let content = FrameLayout.contentRect(viewWidth: Double(size.width), viewHeight: Double(size.height),
                                              portrait: settings.pictureOrientation != 0)
        guard content.width >= 1, content.height >= 1 else { return }
        ensureFeedback(for: CGSize(width: content.width, height: content.height))
        guard feedback.count == 2, !vertexBuffers.isEmpty else { return }

        inflight.wait()

        // Timing.
        let now = CACurrentMediaTime()
        let dt = Float(now - lastTime)
        lastTime = now
        let t = Float(now - startTime)

        // Audio: read the newest features right before drawing (lowest latency path).
        let analysis = capture.analysis
        analysis.sensitivity = Float(settings.sensitivity)
        analysis.beatThreshold = Float(settings.beatThreshold)
        let audio = analysis.snapshot()
        spectrum.update(from: analysis)

        updateScene(now: now, dt: dt, audio: audio)
        let scene = blendedScene()

        // ---- MilkDrop: fade its picture in and out with the style blend; projectM itself blends
        // from one preset to the next.
        var milkMix: Float = 0
        var milkTexture: MTLTexture?
        if model.milkCatalog.presets.count > 0 || currentScene.milk >= 0 {
            if milkGeneration != model.milkGeneration {
                milkGeneration = model.milkGeneration
                milkLoaded = -1 // the list changed: load the current preset again
            }
            let k = sceneBlend * sceneBlend * (3 - 2 * sceneBlend)
            let a: Float = previousScene.milk >= 0 ? 1 : 0, b: Float = currentScene.milk >= 0 ? 1 : 0
            milkMix = a + (b - a) * k
            let catalog = model.milkCatalog
            if milk.available, currentScene.milk >= 0, currentScene.milk != milkLoaded,
               currentScene.milk < catalog.presets.count {
                milk.setPreset(catalog.presets[currentScene.milk], userFolder: model.milkFolder, smooth: milkLoaded >= 0)
                milkLoaded = currentScene.milk
            }
            if milk.available, milkMix > 0.001, let fb = feedback.first {
                let count = max(64, min(2048, Int(Double(capture.sampleRate) * Double(max(dt, 0)))))
                milkSamples.withUnsafeMutableBufferPointer { buf in
                    analysis.copyRecentSamples(into: buf.baseAddress!, count: count)
                }
                let scale = min(1, 1280 / Float(max(fb.width, fb.height)))
                milkTexture = milkSamples.withUnsafeBufferPointer { buf in
                    milk.render(width: max(16, Int(Float(fb.width) * scale)), height: max(16, Int(Float(fb.height) * scale)),
                                samples: buf.baseAddress!, count: count)
                }
            }
            if milkTexture == nil { milkMix = 0 }
            if currentScene.milk < 0, milkMix <= 0 { milkLoaded = -1 }
        }

        // Beat flash.
        if audio.beatCount != lastFlashBeat {
            if lastFlashBeat != 0 { flash = max(flash, audio.beatStrength) }
            lastFlashBeat = audio.beatCount
        }
        flash *= pow(0.02, dt) // fast fade
        if audio.beatCount != lastGlitchBeat {
            if lastGlitchBeat != 0 { glitchPulse = max(glitchPulse, min(audio.beatStrength, 1)) }
            lastGlitchBeat = audio.beatCount
        }
        glitchPulse *= pow(0.004, max(dt, 0))

        // Kinetic Text fades in and out with the style blend, like MilkDrop.
        let kineticMix: Float = {
            let k = sceneBlend * sceneBlend * (3 - 2 * sceneBlend)
            let a: Float = previousScene.wave == .kineticText ? 1 : 0
            let b: Float = currentScene.wave == .kineticText ? 1 : 0
            return sceneBlend >= 1 ? b : a + (b - a) * k
        }()

        let aspect = Float(content.aspect)
        let src = feedback[feedbackIndex]
        let dst = feedback[1 - feedbackIndex]

        // ---- Logos and text: a row in landscape, a column in portrait, each with its own motion.
        let amounts = LogoMotion.Amounts(pulse: Float(settings.pulse), squash: Float(settings.squash),
                                         bounce: Float(settings.bounce), slide: Float(settings.slide),
                                         wobble: Float(settings.wobble))
        let color = hsv(scene.baseHue + t * scene.hueSpeed + 0.5, 0.8, 1)
        let textures = itemTextures
        var items: [LogoUniforms] = []
        var firstRadius: Float = 0
        let layerAmounts = settings.layerAmounts
        for (i, tex) in textures.enumerated() {
            let slot = OverlayLayout.slot(index: i, count: textures.count, aspect: aspect)
            let ext = OverlayLayout.extent(imageAspect: Float(tex.width) / Float(max(tex.height, 1)),
                                           size: Float(settings.logoSize), slot: slot)
            let pose = motions[i].update(dt: dt, audio: audio, amounts: amounts,
                                         variant: OverlayLayout.motionVariant(i))
            var lu = LogoUniforms()
            lu.hx = ext.halfW * pose.scaleX
            lu.hy = ext.halfH * pose.scaleY
            lu.cx = slot.cx + pose.x
            lu.cy = slot.cy + pose.y - 0.08 * Float(settings.bounce)
            lu.rotation = pose.rotation
            lu.aspect = aspect
            lu.r = color.0; lu.g = color.1; lu.b = color.2
            items.append(lu)
            if i == 0 { firstRadius = max(ext.halfW, ext.halfH) * 0.9 }
            if i < itemLayers.count, let layers = itemLayers[i] {
                layers.motion.update(dt: dt, amounts: layerAmounts, nx: layers.nx, ny: layers.ny,
                                     bass: audio.bass, mid: audio.mid, treble: audio.treble,
                                     beatCount: audio.beatCount, beatStrength: audio.beatStrength)
            }
        }
        let layersOn = layerAmounts.any
        let itemLayersNow = itemLayers
        /// Draws one item: whole, or shape by shape when logo layers are on.
        func drawItem(_ enc: MTLRenderCommandEncoder, _ i: Int, _ u: LogoUniforms, _ logo: MTLTexture) {
            if layersOn, i < itemLayersNow.count, let layers = itemLayersNow[i] {
                for l in 0..<layers.textures.count {
                    let uv = layers.uv[l]
                    let q = LayerQuad(itemCx: u.cx, itemCy: u.cy, itemHx: u.hx, itemHy: u.hy, itemRotation: u.rotation,
                                      u0: uv.u0, v0: uv.v0, u1: uv.u1, v1: uv.v1, pose: layers.motion.pose(l))
                    var lu = u
                    lu.cx = q.cx; lu.cy = q.cy; lu.hx = q.hx; lu.hy = q.hy; lu.rotation = q.rotation
                    enc.setVertexBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                    enc.setFragmentBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                    enc.setFragmentTexture(layers.textures[l], index: 0)
                    enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
                }
                return
            }
            var lu = u
            enc.setVertexBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
            enc.setFragmentBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
            enc.setFragmentTexture(logo, index: 0)
            enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        }
        let firstCenter = items.first.map { SIMD2($0.cx, $0.cy) } ?? SIMD2<Float>(0, 0)

        guard let cb = queue.makeCommandBuffer() else { inflight.signal(); return }
        let sem = inflight
        cb.addCompletedHandler { _ in sem.signal() }

        // ---- Pass 1+2: warp previous frame, then draw new shapes on top.
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = dst
        rp.colorAttachments[0].loadAction = .dontCare
        rp.colorAttachments[0].storeAction = .store

        if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
            // Frame-rate independent feedback: tuned for 60 fps, scaled for others.
            let frames = max(0.25, min(4, dt * 60))
            var wu = WarpUniforms()
            wu.aspect = aspect
            wu.time = t
            wu.zoom = pow(scene.zoom + scene.zoomBass * audio.bass, frames)
            let rotation = (scene.rotation + scene.rotationBass * audio.bass) * frames
            wu.rotCos = cos(rotation)
            wu.rotSin = sin(rotation)
            wu.warpAmount = scene.warpAmount * (1 + audio.mid * 0.8)
            wu.warpScale = scene.warpScale
            wu.warpSpeed = scene.warpSpeed
            wu.decay = pow(scene.decay, frames)
            wu.driftX = scene.driftX * frames
            wu.driftY = scene.driftY * frames
            wu.kaleido = scene.kaleido
            wu.mirror = scene.mirror
            wu.generative = scene.generative
            wu.chrome = scene.chrome
            wu.chromeHue = scene.baseHue + t * scene.hueSpeed
            let hueShift = scene.hueShift * frames
            wu.hueCos = cos(hueShift)
            wu.hueSin = sin(hueShift)
            wu.plasma = scene.plasma
            wu.bass = audio.bass
            wu.mid = audio.mid
            wu.treble = audio.treble
            wu.texelX = 1 / Float(src.width)
            wu.texelY = 1 / Float(src.height)

            enc.setRenderPipelineState(warpPipeline)
            enc.setFragmentTexture(src, index: 0)
            enc.setFragmentBytes(&wu, length: MemoryLayout<WarpUniforms>.stride, index: 0)
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)

            // Shapes: solid "over" shapes first, then glowing additive ones.
            let vb = vertexBuffers[bufferIndex]
            bufferIndex = (bufferIndex + 1) % vertexBuffers.count
            var builder = GeometryBuilder(buffer: vb, capacity: Self.additiveCapacity, aspect: aspect)
            var overBuilder = GeometryBuilder(buffer: vb, start: Self.additiveCapacity,
                                              capacity: Self.maxVertices - Self.additiveCapacity, aspect: aspect)
            // Styles built around the logo (Bass Ring, Particle Storm, Lightning) follow the first item.
            let logoRadius = settings.showLogo ? firstRadius : 0
            WaveDrawer.draw(scene: scene, audio: audio, spectrum: spectrum, time: t, dt: dt,
                            logoCenter: firstCenter, logoRadius: logoRadius, fx: fx,
                            into: &builder, over: &overBuilder)
            enc.setVertexBuffer(vb, offset: 0, index: 0)
            if overBuilder.count > 0 {
                enc.setRenderPipelineState(geomOverPipeline)
                enc.drawPrimitives(type: .triangle, vertexStart: Self.additiveCapacity, vertexCount: overBuilder.count)
            }
            if builder.count > 0 {
                enc.setRenderPipelineState(geomPipeline)
                enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: builder.count)
            }
            drawGpuSignal(enc, scene: scene, audio: audio, time: t, aspect: aspect)

            // Optional logo trail into the feedback buffer, in the same pass (a second pass on
            // the same texture would make the GPU store and reload it).
            if settings.showLogo, settings.logoTrails, !textures.isEmpty {
                enc.setRenderPipelineState(logoTrailPipeline)
                for (i, logo) in textures.enumerated() {
                    var tu = items[i]
                    tu.opacity = 0.18
                    tu.tintAmount = 0.6
                    drawItem(enc, i, tu, logo)
                }
            }
            enc.endEncoding()
        }

        // ---- Pass 3: composite to screen.
        guard let drawable = view.currentDrawable, let srp = view.currentRenderPassDescriptor else {
            cb.commit()
            feedbackIndex = 1 - feedbackIndex
            return
        }
        srp.colorAttachments[0].loadAction = .clear
        srp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)

        // The final picture: feedback image + logo. Drawn to the screen, and again into the
        // recording (at its own resolution) so the screen's drawable never has to be read back.
        // In portrait the viewport is the centered 9:16 frame; the bars stay black.
        func composite(_ enc: MTLRenderCommandEncoder) {
            var pu = PresentUniforms(flash: flash * Float(settings.beatFlash),
                                     vignette: scene.vignette,
                                     brightness: Float(settings.brightness), milkMix: milkMix,
                                     glitch: scene.glitch, glitchTime: fmod(t, 1000), glitchPulse: glitchPulse)
            enc.setRenderPipelineState(presentPipeline)
            enc.setFragmentTexture(dst, index: 0)
            enc.setFragmentTexture(milkTexture ?? dst, index: 1)
            enc.setFragmentBytes(&pu, length: MemoryLayout<PresentUniforms>.stride, index: 0)
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)

            // Kinetic Text: rows of the first logo or text item sliding across, the beat lighting a row.
            if kineticMix > 0.001, let first = textures.first {
                let rows = KineticText.layout(time: t, aspect: aspect,
                                              itemAspect: Float(first.width) / Float(max(first.height, 1)),
                                              beatCount: audio.beatCount, pulse: glitchPulse, bass: audio.bass)
                enc.setRenderPipelineState(logoGlowPipeline)
                enc.setFragmentTexture(first, index: 0)
                for (r, row) in rows.enumerated() {
                    let rc = hsv(scene.baseHue + t * scene.hueSpeed + Float(r) * 0.07, 0.75, 1)
                    for c in 0..<row.copies {
                        var kc = LogoUniforms()
                        kc.cx = row.x0 + Float(c) * row.spacing
                        kc.cy = row.y
                        kc.hx = row.hx
                        kc.hy = row.hy
                        kc.aspect = aspect
                        kc.r = rc.0; kc.g = rc.1; kc.b = rc.2
                        kc.tintAmount = 0.85
                        kc.mipBias = 0.5
                        kc.opacity = row.opacity * kineticMix
                        enc.setVertexBytes(&kc, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                        enc.setFragmentBytes(&kc, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                        enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
                    }
                }
            }

            guard settings.showLogo else { return }
            for (i, logo) in textures.enumerated() {
                // Glow: a larger, heavily mip-blurred, tinted copy added underneath.
                let glow = Float(settings.glow)
                if glow > 0.01 {
                    var gu = items[i]
                    gu.hx *= 1.18
                    gu.hy *= 1.18
                    gu.mipBias = 5
                    gu.tintAmount = 1
                    gu.opacity = glow * (0.35 + min(audio.bass, 1.2) * 0.9)
                    enc.setRenderPipelineState(logoGlowPipeline)
                    drawItem(enc, i, gu, logo)
                }

                var lu = items[i]
                lu.opacity = 1
                lu.tintAmount = Float(settings.logoTint)
                enc.setRenderPipelineState(logoPipeline)
                drawItem(enc, i, lu, logo)
            }
        }
        if let enc = cb.makeRenderCommandEncoder(descriptor: srp) {
            enc.setViewport(MTLViewport(originX: content.x, originY: content.y,
                                        width: content.width, height: content.height, znear: 0, zfar: 1))
            composite(enc)
            enc.endEncoding()
        }

        // Recording: draw the same picture (no toolbar) into the video frame, letterboxed.
        if let recorder = model.recorder, let frame = recorder.nextFrameTarget(now: now) {
            let rp = MTLRenderPassDescriptor()
            rp.colorAttachments[0].texture = frame.texture
            rp.colorAttachments[0].loadAction = .clear
            rp.colorAttachments[0].storeAction = .store
            rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
            if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
                let tw = Double(recorder.width), th = Double(recorder.height)
                let sa = content.aspect, ta = tw / th
                let vw = sa > ta ? tw : th * sa
                let vh = sa > ta ? tw / sa : th
                enc.setViewport(MTLViewport(originX: (tw - vw) / 2, originY: (th - vh) / 2,
                                            width: vw, height: vh, znear: 0, zfar: 1))
                composite(enc)
                enc.endEncoding()
                cb.addCompletedHandler { _ in recorder.appendFrame(frame) }
            }
        }

        cb.present(drawable)
        cb.commit()
        feedbackIndex = 1 - feedbackIndex
    }

    // MARK: GPU waveforms

    private struct SignalUniforms {
        var aspect: Float = 0, radius: Float = 0, amplitude: Float = 0, rotation: Float = 0
        var halfWidth: Float = 0, hue0: Float = 0, saturation: Float = 0, alpha: Float = 0
        var p0: Float = 0, p1: Float = 0, p2: Float = 0, p3: Float = 0
        var mode: Int32 = 0, count: Int32 = 0, stride: Int32 = 0, sampleOffset: Int32 = 0
        var closed: Int32 = 0, side: Int32 = 0, pad0: Int32 = 0, pad1: Int32 = 0
    }

    private func drawGpuSignal(_ enc: MTLRenderCommandEncoder, scene: VisualScene,
                               audio: AudioAnalysis.Snapshot, time t: Float, aspect: Float) {
        guard let pipeline = signalPipeline else { return }
        let gpu: Bool
        switch scene.wave {
        case .circle, .twinRings, .line, .verticalLine, .bars, .radialBars, .spiralDots, .lissajous:
            gpu = true
        default:
            gpu = false
        }
        guard gpu else { return }
        signalScratch.withUnsafeMutableBufferPointer { dst in
            spectrum.wave.withUnsafeBufferPointer { w in
                dst.baseAddress!.update(from: w.baseAddress!, count: Spectrum.waveCount)
            }
            spectrum.bands.withUnsafeBufferPointer { b in
                (dst.baseAddress! + Spectrum.waveCount).update(from: b.baseAddress!, count: Spectrum.bandCount)
            }
        }
        let sampleBytes = Self.signalSampleCount * MemoryLayout<Float>.stride

        let bass = min(audio.bass, 1.4)
        let width = 0.0065 * scene.thickness * (1 + bass * 0.6)
        let alpha = 0.55 + 0.45 * min(1, max(bass, audio.mid))
        let hue0 = scene.baseHue + t * scene.hueSpeed
        let scale = scene.waveScale

        func verts(_ mode: Int32, _ count: Int, _ closed: Bool) -> Int {
            switch mode {
            case 1, 4:
                let segs = closed ? count : count - 1
                return segs > 0 ? segs * 6 : 0
            case 2, 3: return count * 6
            case 5: return count * 24
            default: return 0
            }
        }
        func issue(_ cb: inout SignalUniforms, mode: Int32, count: Int, closed: Bool) {
            let n = verts(mode, count, closed)
            guard n > 0 else { return }
            cb.mode = mode
            cb.count = Int32(count)
            cb.closed = closed ? 1 : 0
            enc.setRenderPipelineState(pipeline)
            enc.setVertexBytes(&cb, length: MemoryLayout<SignalUniforms>.stride, index: 0)
            signalScratch.withUnsafeBytes { enc.setVertexBytes($0.baseAddress!, length: sampleBytes, index: 1) }
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: n)
        }

        var base = SignalUniforms()
        base.aspect = aspect
        base.hue0 = hue0
        base.saturation = scene.saturation
        base.alpha = alpha
        base.halfWidth = width * 0.5

        switch scene.wave {
        case .circle, .twinRings:
            var cb = base
            cb.radius = scene.centerRadius * scale * (1 + (scene.wave == .circle ? 0.25 : 0.3) * bass)
            cb.amplitude = (scene.wave == .circle ? 0.12 : 0.07) * scale
            cb.rotation = t * (scene.wave == .circle ? 0.2 : 0.5)
            cb.stride = 2
            issue(&cb, mode: 1, count: 256, closed: true)
            if scene.wave == .twinRings {
                cb.radius = max(0.42 * scale, scene.centerRadius * scale + 0.22) * (1 + 0.15 * bass)
                cb.amplitude = 0.1 * scale
                cb.rotation = -t * 0.4
                cb.sampleOffset = 1
                cb.hue0 = hue0 + 0.5 * 0.35
                issue(&cb, mode: 1, count: 256, closed: true)
            }
        case .line:
            var cb = base
            cb.p0 = -0.94 * aspect
            cb.p1 = 1.88 * aspect
            cb.p2 = 0.3 * scale
            cb.stride = 2
            issue(&cb, mode: 1, count: 256, closed: false)
        case .verticalLine:
            for side: Int32 in [-1, 1] {
                var cb = base
                cb.p0 = -0.94
                cb.p1 = 1.88
                cb.p2 = 0.35 * aspect * 0.5
                cb.p3 = 0.22 * scale
                cb.stride = 2
                cb.side = side
                if side > 0 { cb.hue0 = hue0 + 0.5 * 0.35 }
                issue(&cb, mode: 1, count: 256, closed: false)
            }
        case .bars:
            var cb = base
            cb.alpha = alpha * 0.9
            cb.p0 = 1.9 * aspect
            cb.p1 = 1.15 * scale
            issue(&cb, mode: 2, count: 48, closed: false)
        case .radialBars:
            let inner = scene.centerRadius > 0 ? scene.centerRadius + 0.06 * bass : 0
            for side: Int32 in [0, 1] {
                var cb = base
                cb.radius = inner
                cb.p1 = 0.65 * scale
                cb.rotation = t * 0.1
                cb.halfWidth = width * 1.2 * 0.5
                cb.side = side
                issue(&cb, mode: 3, count: 48, closed: false)
            }
        case .lissajous:
            var cb = base
            cb.p0 = 0.5 * scale
            cb.halfWidth = width * 0.8 * 0.5
            issue(&cb, mode: 4, count: (512 - 23) / 2, closed: false)
        case .spiralDots:
            var cb = base
            cb.p0 = scene.centerRadius
            cb.p1 = 0.85 * scale
            cb.p2 = 0.032 * scene.thickness
            cb.rotation = t * 1.2
            issue(&cb, mode: 5, count: 144, closed: false)
        default:
            break
        }
    }

    // MARK: Scenes

    private func updateScene(now: CFTimeInterval, dt: Float, audio: AudioAnalysis.Snapshot) {
        let scenes = VisualScene.all
        var requested = ((settings.sceneIndex % scenes.count) + scenes.count) % scenes.count

        if settings.autoCycle, now - sceneStartTime > max(5, settings.cycleSeconds) {
            requested = settings.nextCycleIndex(after: requested)
            sceneStartTime = now
            settings.sceneIndex = requested // draw(in:) runs on the main thread
        }

        if requested != activeSceneIndex {
            previousScene = activeSceneIndex < 0 ? scenes[requested] : blendedScene()
            currentScene = scenes[requested]
            sceneBlend = activeSceneIndex < 0 ? 1 : 0
            activeSceneIndex = requested
            sceneStartTime = now
            model.currentSceneName = currentScene.name
        }
        sceneBlend = min(1, sceneBlend + dt / 2.5) // 2.5 s crossfade
    }

    private func blendedScene() -> VisualScene {
        if sceneBlend >= 1 { return currentScene }
        let k = sceneBlend * sceneBlend * (3 - 2 * sceneBlend)
        let a = previousScene, b = currentScene
        func mix(_ x: Float, _ y: Float) -> Float { x + (y - x) * k }
        var s = k < 0.5 ? a : b // discrete fields switch at the midpoint
        s.zoom = mix(a.zoom, b.zoom)
        s.zoomBass = mix(a.zoomBass, b.zoomBass)
        s.rotation = mix(a.rotation, b.rotation)
        s.rotationBass = mix(a.rotationBass, b.rotationBass)
        s.warpAmount = mix(a.warpAmount, b.warpAmount)
        s.warpScale = mix(a.warpScale, b.warpScale)
        s.warpSpeed = mix(a.warpSpeed, b.warpSpeed)
        s.decay = mix(a.decay, b.decay)
        s.driftX = mix(a.driftX, b.driftX)
        s.driftY = mix(a.driftY, b.driftY)
        s.hueShift = mix(a.hueShift, b.hueShift)
        s.plasma = mix(a.plasma, b.plasma)
        s.generative = mix(a.generative, b.generative)
        s.chrome = mix(a.chrome, b.chrome)
        s.glitch = mix(a.glitch, b.glitch)
        s.vignette = mix(a.vignette, b.vignette)
        s.saturation = mix(a.saturation, b.saturation)
        s.centerRadius = mix(a.centerRadius, b.centerRadius)
        return s
    }
}

// MARK: - Color helper

func hsv(_ h: Float, _ s: Float, _ v: Float) -> (Float, Float, Float) {
    let hh = (h - floor(h)) * 6
    let i = Int(hh) % 6
    let f = hh - floor(hh)
    let p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f))
    switch i {
    case 0: return (v, t, p)
    case 1: return (q, v, p)
    case 2: return (p, v, t)
    case 3: return (p, q, v)
    case 4: return (t, p, v)
    default: return (v, p, q)
    }
}
