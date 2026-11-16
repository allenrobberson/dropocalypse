import MetalKit
import AppKit
import QuartzCore

// Uniform layouts. Plain Float/Int32 fields only, matching the structs in ShaderSource.swift.

private struct WarpUniforms {
    var aspect: Float = 1, time: Float = 0, zoom: Float = 1, rotation: Float = 0
    var warpAmount: Float = 0, warpScale: Float = 6, warpSpeed: Float = 1, decay: Float = 0.95
    var driftX: Float = 0, driftY: Float = 0, kaleido: Float = 0, hueShift: Float = 0
    var plasma: Float = 0, bass: Float = 0, mid: Float = 0, treble: Float = 0
    var texelX: Float = 0, texelY: Float = 0
    var mirror: Int32 = 0
    var pad: Float = 0
}

private struct PresentUniforms {
    var flash: Float = 0, vignette: Float = 0.3, brightness: Float = 1, pad: Float = 0
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

    private var feedback: [MTLTexture] = []
    private var feedbackIndex = 0
    private let feedbackFormat: MTLPixelFormat = .rgba16Float

    private static let maxVertices = 96_000
    private static let additiveCapacity = 52_000 // the rest is for the alpha-blended "over" batch
    private var vertexBuffers: [MTLBuffer] = []
    private var bufferIndex = 0
    private let inflight = DispatchSemaphore(value: 3)

    private var logoTexture: MTLTexture?
    private var logoAspect: Float = 1

    private let spectrum = Spectrum()
    private let fx = StyleFX()
    private var motion = LogoMotion()
    private var lastTime = CACurrentMediaTime()
    private var startTime = CACurrentMediaTime()
    private var flash: Float = 0
    private var lastFlashBeat = 0

    // VisualScene blending: crossfade warp/draw parameters when the scene changes.
    private var currentScene: VisualScene = VisualScene.builtIn[0]
    private var previousScene: VisualScene = VisualScene.builtIn[0]
    private var sceneBlend: Float = 1
    private var activeSceneIndex = -1
    private var sceneStartTime = CACurrentMediaTime()

    private var shaderError: String?

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
        view.preferredFramesPerSecond = 120 // MTKView caps this to the display's real rate
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

        logoTexture = LogoTexture.placeholder(device: device)
        logoAspect = logoTexture.map { Float($0.width) / Float($0.height) } ?? 1
    }

    // MARK: Logo

    func setLogo(_ image: NSImage?) {
        if let image, let tex = LogoTexture.make(from: image, device: device, queue: queue) {
            logoTexture = tex
        } else {
            logoTexture = LogoTexture.placeholder(device: device)
        }
        logoAspect = logoTexture.map { Float($0.width) / Float($0.height) } ?? 1
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
        ensureFeedback(for: size)
    }

    func draw(in view: MTKView) {
        guard shaderError == nil, warpPipeline != nil else { return }
        let size = view.drawableSize
        guard size.width > 0, size.height > 0 else { return }
        ensureFeedback(for: size)
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

        // Beat flash.
        if audio.beatCount != lastFlashBeat {
            if lastFlashBeat != 0 { flash = max(flash, audio.beatStrength) }
            lastFlashBeat = audio.beatCount
        }
        flash *= pow(0.02, dt) // fast fade

        let aspect = Float(size.width / size.height)
        let src = feedback[feedbackIndex]
        let dst = feedback[1 - feedbackIndex]

        // ---- Logo motion.
        let amounts = LogoMotion.Amounts(pulse: Float(settings.pulse), squash: Float(settings.squash),
                                         bounce: Float(settings.bounce), slide: Float(settings.slide),
                                         wobble: Float(settings.wobble))
        let pose = motion.update(dt: dt, audio: audio, amounts: amounts)
        let color = hsv(scene.baseHue + t * scene.hueSpeed + 0.5, 0.8, 1)

        var lu = LogoUniforms()
        let size01 = Float(settings.logoSize)
        // Fit inside size01 (fraction of screen height) whichever side is longer.
        let baseH = logoAspect >= 1 ? size01 / logoAspect : size01
        let baseW = baseH * logoAspect
        lu.hx = baseW * pose.scaleX
        lu.hy = baseH * pose.scaleY
        lu.cx = pose.x
        lu.cy = pose.y - 0.08 * Float(settings.bounce)
        lu.rotation = pose.rotation
        lu.aspect = aspect
        lu.r = color.0; lu.g = color.1; lu.b = color.2

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
            wu.rotation = (scene.rotation + scene.rotationBass * audio.bass) * frames
            wu.warpAmount = scene.warpAmount * (1 + audio.mid * 0.8)
            wu.warpScale = scene.warpScale
            wu.warpSpeed = scene.warpSpeed
            wu.decay = pow(scene.decay, frames)
            wu.driftX = scene.driftX * frames
            wu.driftY = scene.driftY * frames
            wu.kaleido = scene.kaleido
            wu.mirror = scene.mirror
            wu.hueShift = scene.hueShift * frames
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
            let logoRadius = settings.showLogo ? max(baseW, baseH) * 0.9 : 0
            WaveDrawer.draw(scene: scene, audio: audio, spectrum: spectrum, time: t, dt: dt,
                            logoCenter: SIMD2(lu.cx, lu.cy), logoRadius: logoRadius, fx: fx,
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
            enc.endEncoding()
        }

        // ---- Optional logo trail into the feedback buffer.
        if settings.showLogo, settings.logoTrails, let logo = logoTexture {
            let trp = MTLRenderPassDescriptor()
            trp.colorAttachments[0].texture = dst
            trp.colorAttachments[0].loadAction = .load
            trp.colorAttachments[0].storeAction = .store
            if let enc = cb.makeRenderCommandEncoder(descriptor: trp) {
                var tu = lu
                tu.opacity = 0.18
                tu.tintAmount = 0.6
                enc.setRenderPipelineState(logoTrailPipeline)
                enc.setVertexBytes(&tu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                enc.setFragmentBytes(&tu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                enc.setFragmentTexture(logo, index: 0)
                enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
                enc.endEncoding()
            }
        }

        // ---- Pass 3: composite to screen.
        guard let drawable = view.currentDrawable, let srp = view.currentRenderPassDescriptor else {
            cb.commit()
            feedbackIndex = 1 - feedbackIndex
            return
        }
        srp.colorAttachments[0].loadAction = .dontCare

        // The final picture: feedback image + logo. Drawn to the screen, and again into the
        // recording (at its own resolution) so the screen's drawable never has to be read back.
        let composite: (MTLRenderCommandEncoder) -> Void = { enc in
            var pu = PresentUniforms(flash: flash * Float(settings.beatFlash),
                                     vignette: scene.vignette,
                                     brightness: Float(settings.brightness), pad: 0)
            enc.setRenderPipelineState(presentPipeline)
            enc.setFragmentTexture(dst, index: 0)
            enc.setFragmentBytes(&pu, length: MemoryLayout<PresentUniforms>.stride, index: 0)
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)

            if settings.showLogo, let logo = logoTexture {
                // Glow: a larger, heavily mip-blurred, tinted copy added underneath.
                let glow = Float(settings.glow)
                if glow > 0.01 {
                    var gu = lu
                    gu.hx *= 1.18
                    gu.hy *= 1.18
                    gu.mipBias = 5
                    gu.tintAmount = 1
                    gu.opacity = glow * (0.35 + min(audio.bass, 1.2) * 0.9)
                    enc.setRenderPipelineState(logoGlowPipeline)
                    enc.setVertexBytes(&gu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                    enc.setFragmentBytes(&gu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                    enc.setFragmentTexture(logo, index: 0)
                    enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        }

                lu.opacity = 1
                lu.tintAmount = Float(settings.logoTint)
                enc.setRenderPipelineState(logoPipeline)
                enc.setVertexBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                enc.setFragmentBytes(&lu, length: MemoryLayout<LogoUniforms>.stride, index: 0)
                enc.setFragmentTexture(logo, index: 0)
                enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
            }
        }
        if let enc = cb.makeRenderCommandEncoder(descriptor: srp) {
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
                let sa = Double(size.width / size.height), ta = tw / th
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

    // MARK: Scenes

    private func updateScene(now: CFTimeInterval, dt: Float, audio: AudioAnalysis.Snapshot) {
        let scenes = VisualScene.builtIn
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
