import SwiftUI
import MetalKit

/// Hosts the MTKView and owns the renderer.
struct MetalView: NSViewRepresentable {
    @ObservedObject var model: VisualizerModel

    final class Coordinator {
        var renderer: Renderer?
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.autoResizeDrawable = true
        view.isPaused = false
        view.enableSetNeedsDisplay = false
        if let renderer = Renderer(view: view, capture: model.capture, settings: model.settings, model: model) {
            view.delegate = renderer
            context.coordinator.renderer = renderer
            model.renderer = renderer
        } else {
            model.reportError("This Mac doesn't support Metal, which the visualizer needs.")
        }
        return view
    }

    func updateNSView(_ nsView: MTKView, context: Context) {}
}
