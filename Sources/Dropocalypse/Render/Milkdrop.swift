import Foundation
import Metal
import IOSurface
import OpenGL.GL3
import CoreVideo

// MilkDrop presets, played by projectM (https://github.com/projectM-visualizer/projectm, LGPL-2.1).
//
// projectM ships inside the app as Contents/Frameworks/libprojectM-4.dylib and is loaded at run
// time; without it the MilkDrop styles simply don't appear. It draws with OpenGL into IOSurfaces
// that Metal reads directly, so no pixels are copied.

/// One preset: shown in the style list under `name`.
struct MilkPreset {
    var name: String
    var url: URL
    var bundled: Bool
}

/// Bundled presets (from the app's Resources/MilkDrop) followed by every .milk file under the
/// user's folder, subfolders included.
struct MilkCatalog {
    var presets: [MilkPreset] = []
    var bundledCount = 0
    var userTruncated = false
    static let userLimit = 20_000

    var userCount: Int { presets.count - bundledCount }

    static var bundledFolder: URL? { Bundle.main.resourceURL?.appendingPathComponent("MilkDrop", isDirectory: true) }

    static func scan(bundled: URL?, user: URL?) -> MilkCatalog {
        var c = MilkCatalog()
        let fm = FileManager.default
        if let dir = bundled?.appendingPathComponent("presets", isDirectory: true),
           let files = try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) {
            c.presets = files.filter { $0.pathExtension.lowercased() == "milk" }
                .sorted { $0.lastPathComponent < $1.lastPathComponent }
                .map { url in
                    // Bundled files are named "01 Acid Mandala" to keep their order; show just the name.
                    var name = url.deletingPathExtension().lastPathComponent
                    if name.count > 3, name.prefix(2).allSatisfy(\.isNumber), name.dropFirst(2).first == " " {
                        name = String(name.dropFirst(3))
                    }
                    return MilkPreset(name: name, url: url, bundled: true)
                }
        }
        c.bundledCount = c.presets.count
        if let user, let walk = fm.enumerator(at: user, includingPropertiesForKeys: [.isRegularFileKey],
                                              options: [.skipsHiddenFiles]) {
            var found: [MilkPreset] = []
            for case let url as URL in walk where url.pathExtension.lowercased() == "milk" {
                if found.count >= userLimit { c.userTruncated = true; break }
                found.append(MilkPreset(name: url.deletingPathExtension().lastPathComponent, url: url, bundled: false))
            }
            found.sort { $0.url.path.localizedCaseInsensitiveCompare($1.url.path) == .orderedAscending }
            c.presets += found
        }
        return c
    }

    /// Folders projectM searches for the textures a preset names: a "Textures" folder beside or
    /// above the presets (the NestDrop and MilkDrop layout), the preset's own folder, and the bundled one.
    static func texturePaths(for preset: URL, userFolder: URL?) -> [String] {
        let fm = FileManager.default
        var out: [String] = []
        func add(_ url: URL) {
            var isDir: ObjCBool = false
            if fm.fileExists(atPath: url.path, isDirectory: &isDir), isDir.boolValue, !out.contains(url.path) {
                out.append(url.path)
            }
        }
        var dir = preset.deletingLastPathComponent()
        add(dir)
        for _ in 0..<8 {
            add(dir.appendingPathComponent("Textures"))
            add(dir.appendingPathComponent("textures"))
            if let user = userFolder, dir.standardizedFileURL == user.standardizedFileURL {
                add(user.deletingLastPathComponent().appendingPathComponent("Textures"))
                break
            }
            let up = dir.deletingLastPathComponent()
            if up.path == dir.path { break }
            dir = up
        }
        if let b = bundledFolder { add(b.appendingPathComponent("textures")) }
        return out
    }
}

/// The parts of projectM's C API that Dropocalypse uses, looked up in the bundled dylib.
private struct ProjectMApi {
    typealias Create = @convention(c) () -> OpaquePointer?
    typealias Destroy = @convention(c) (OpaquePointer?) -> Void
    typealias LoadPresetData = @convention(c) (OpaquePointer?, UnsafePointer<CChar>?, Bool) -> Void
    typealias RenderFbo = @convention(c) (OpaquePointer?, UInt32) -> Void
    typealias PcmAddFloat = @convention(c) (OpaquePointer?, UnsafePointer<Float>?, UInt32, Int32) -> Void
    typealias SetSize = @convention(c) (OpaquePointer?, Int, Int) -> Void
    typealias SetPaths = @convention(c) (OpaquePointer?, UnsafeMutablePointer<UnsafePointer<CChar>?>?, Int) -> Void
    typealias SetBool = @convention(c) (OpaquePointer?, Bool) -> Void
    typealias SetDouble = @convention(c) (OpaquePointer?, Double) -> Void
    typealias SetInt = @convention(c) (OpaquePointer?, Int32) -> Void

    let create: Create, destroy: Destroy, loadPresetData: LoadPresetData, renderFbo: RenderFbo
    let pcmAddFloat: PcmAddFloat, setWindowSize: SetSize, setTexturePaths: SetPaths
    let setPresetLocked: SetBool, setHardCutEnabled: SetBool, setPresetDuration: SetDouble
    let setSoftCutDuration: SetDouble, setFps: SetInt

    static func load() -> ProjectMApi? {
        var candidates: [String] = []
        if let fw = Bundle.main.privateFrameworksPath { candidates.append(fw + "/libprojectM-4.dylib") }
        candidates.append("libprojectM-4.dylib") // a development build with projectM installed
        guard let lib = candidates.lazy.compactMap({ dlopen($0, RTLD_NOW | RTLD_LOCAL) }).first else { return nil }
        func sym<T>(_ name: String, _: T.Type) -> T? {
            guard let p = dlsym(lib, name) else { return nil }
            return unsafeBitCast(p, to: T.self)
        }
        guard let create = sym("projectm_create", Create.self),
              let destroy = sym("projectm_destroy", Destroy.self),
              let loadPresetData = sym("projectm_load_preset_data", LoadPresetData.self),
              let renderFbo = sym("projectm_opengl_render_frame_fbo", RenderFbo.self),
              let pcmAddFloat = sym("projectm_pcm_add_float", PcmAddFloat.self),
              let setWindowSize = sym("projectm_set_window_size", SetSize.self),
              let setTexturePaths = sym("projectm_set_texture_search_paths", SetPaths.self),
              let setPresetLocked = sym("projectm_set_preset_locked", SetBool.self),
              let setHardCutEnabled = sym("projectm_set_hard_cut_enabled", SetBool.self),
              let setPresetDuration = sym("projectm_set_preset_duration", SetDouble.self),
              let setSoftCutDuration = sym("projectm_set_soft_cut_duration", SetDouble.self),
              let setFps = sym("projectm_set_fps", SetInt.self) else { return nil }
        return ProjectMApi(create: create, destroy: destroy, loadPresetData: loadPresetData, renderFbo: renderFbo,
                           pcmAddFloat: pcmAddFloat, setWindowSize: setWindowSize, setTexturePaths: setTexturePaths,
                           setPresetLocked: setPresetLocked, setHardCutEnabled: setHardCutEnabled,
                           setPresetDuration: setPresetDuration, setSoftCutDuration: setSoftCutDuration, setFps: setFps)
    }
}

/// Runs projectM in its own OpenGL context and hands each frame to Metal through an IOSurface.
final class MilkdropHost {
    private let device: MTLDevice
    private var api: ProjectMApi?
    private var pm: OpaquePointer?
    private var gl: CGLContextObj?
    private(set) var error: String?

    /// Three surfaces in turn, so a frame Metal is still reading is never drawn over.
    private struct Slot {
        var surface: IOSurfaceRef
        var glTexture: GLuint
        var fbo: GLuint
        var texture: MTLTexture
    }
    private var slots: [Slot] = []
    private var slotIndex = 0
    private var width = 0, height = 0

    var available: Bool { pm != nil }

    /// Whether the projectM library is in this build (checked once, without starting it).
    static let engineAvailable: Bool = ProjectMApi.load() != nil

    init(device: MTLDevice) {
        self.device = device
        guard let api = ProjectMApi.load() else {
            error = "The MilkDrop engine (projectM) isn't included in this build."
            return
        }
        self.api = api
        var attrs: [CGLPixelFormatAttribute] = [
            kCGLPFAOpenGLProfile, CGLPixelFormatAttribute(kCGLOGLPVersion_3_2_Core.rawValue),
            kCGLPFAColorSize, CGLPixelFormatAttribute(24),
            kCGLPFAAlphaSize, CGLPixelFormatAttribute(8),
            kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers,
            CGLPixelFormatAttribute(0),
        ]
        var pix: CGLPixelFormatObj?
        var count: GLint = 0
        guard CGLChoosePixelFormat(&attrs, &pix, &count) == kCGLNoError, let pix else {
            error = "This Mac couldn't make an OpenGL context for MilkDrop presets."
            return
        }
        defer { CGLDestroyPixelFormat(pix) }
        var ctx: CGLContextObj?
        guard CGLCreateContext(pix, nil, &ctx) == kCGLNoError, let ctx else {
            error = "This Mac couldn't make an OpenGL context for MilkDrop presets."
            return
        }
        gl = ctx
        CGLSetCurrentContext(ctx)
        defer { CGLSetCurrentContext(nil) }
        pm = api.create()
        guard let pm else {
            error = "The MilkDrop engine couldn't start on this Mac."
            return
        }
        api.setPresetLocked(pm, true)
        api.setHardCutEnabled(pm, false)
        api.setPresetDuration(pm, 1e9)
        api.setSoftCutDuration(pm, 2.5)
        api.setFps(pm, 60)
    }

    deinit {
        guard let gl else { return }
        CGLSetCurrentContext(gl)
        if let pm { api?.destroy(pm) }
        releaseSlots()
        CGLSetCurrentContext(nil)
        CGLDestroyContext(gl)
    }

    /// Switches to a preset. `smooth` blends from the one before, as MilkDrop does.
    func setPreset(_ preset: MilkPreset, userFolder: URL?, smooth: Bool) {
        guard let api, let pm, let gl, let data = try? Data(contentsOf: preset.url), !data.isEmpty else { return }
        CGLSetCurrentContext(gl)
        defer { CGLSetCurrentContext(nil) }
        let paths = MilkCatalog.texturePaths(for: preset.url, userFolder: userFolder)
        let cStrings = paths.map { strdup($0) }
        defer { cStrings.forEach { free($0) } }
        var pointers = cStrings.map { UnsafePointer<CChar>($0) }
        pointers.withUnsafeMutableBufferPointer { api.setTexturePaths(pm, $0.baseAddress, $0.count) }
        let bytes = data.map { CChar(bitPattern: $0) } + [0] // NUL-terminated text
        api.loadPresetData(pm, bytes, smooth)
    }

    /// Draws one frame at width x height, fed with the newest mono samples. The picture's rows run
    /// bottom to top (OpenGL's order), so it is sampled upside down.
    func render(width w: Int, height h: Int, samples: UnsafePointer<Float>, count: Int) -> MTLTexture? {
        guard let api, let pm, let gl, w >= 16, h >= 16 else { return nil }
        CGLSetCurrentContext(gl)
        defer { CGLSetCurrentContext(nil) }
        if w != width || h != height || slots.isEmpty {
            releaseSlots()
            width = w
            height = h
            for _ in 0..<3 { if let s = makeSlot(gl: gl, width: w, height: h) { slots.append(s) } }
            guard slots.count == 3 else { releaseSlots(); return nil }
            api.setWindowSize(pm, w, h)
        }
        if count > 0 { api.pcmAddFloat(pm, samples, UInt32(count), 1) }
        slotIndex = (slotIndex + 1) % slots.count
        let slot = slots[slotIndex]
        api.renderFbo(pm, slot.fbo)
        glFinish() // Metal reads the surface right after this
        return slot.texture
    }

    private func makeSlot(gl: CGLContextObj, width w: Int, height h: Int) -> Slot? {
        let props: [CFString: Any] = [
            kIOSurfaceWidth: w, kIOSurfaceHeight: h, kIOSurfaceBytesPerElement: 4,
            kIOSurfacePixelFormat: kCVPixelFormatType_32BGRA,
        ]
        guard let surface = IOSurfaceCreate(props as CFDictionary) else { return nil }
        var tex: GLuint = 0
        glGenTextures(1, &tex)
        glBindTexture(GLenum(GL_TEXTURE_RECTANGLE), tex)
        let err = CGLTexImageIOSurface2D(gl, GLenum(GL_TEXTURE_RECTANGLE), GLenum(GL_RGBA), GLsizei(w), GLsizei(h),
                                         GLenum(GL_BGRA), GLenum(GL_UNSIGNED_INT_8_8_8_8_REV), surface, 0)
        glBindTexture(GLenum(GL_TEXTURE_RECTANGLE), 0)
        var fbo: GLuint = 0
        glGenFramebuffers(1, &fbo)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), fbo)
        glFramebufferTexture2D(GLenum(GL_FRAMEBUFFER), GLenum(GL_COLOR_ATTACHMENT0), GLenum(GL_TEXTURE_RECTANGLE), tex, 0)
        let complete = glCheckFramebufferStatus(GLenum(GL_FRAMEBUFFER)) == GLenum(GL_FRAMEBUFFER_COMPLETE)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), 0)
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h, mipmapped: false)
        d.usage = [.shaderRead]
        guard err == kCGLNoError, complete, let texture = device.makeTexture(descriptor: d, iosurface: surface, plane: 0) else {
            glDeleteFramebuffers(1, &fbo)
            glDeleteTextures(1, &tex)
            return nil
        }
        return Slot(surface: surface, glTexture: tex, fbo: fbo, texture: texture)
    }

    private func releaseSlots() {
        for var s in slots {
            glDeleteFramebuffers(1, &s.fbo)
            glDeleteTextures(1, &s.glTexture)
        }
        slots.removeAll()
    }
}
