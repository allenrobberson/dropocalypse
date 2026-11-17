import SwiftUI
import MetalKit

/// Hosts the MTKView and owns the renderer.
struct MetalView: NSViewRepresentable {
    @ObservedObject var model: VisualizerModel

    final class Coordinator {
        var renderer: Renderer?
        weak var view: MTKView?
        var tokens: [NSObjectProtocol] = []

        func watch(_ view: MTKView) {
            self.view = view
            let refresh = { [weak view] in
                guard let view, let window = view.window else { return }
                let hidden = window.isMiniaturized || !window.occlusionState.contains(.visible)
                if view.isPaused != hidden { view.isPaused = hidden }
            }
            let names = [NSWindow.didChangeOcclusionStateNotification,
                         NSWindow.didMiniaturizeNotification,
                         NSWindow.didDeminiaturizeNotification]
            for name in names {
                tokens.append(NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { _ in
                    refresh()
                })
            }
        }

        func applyFrameRate(_ view: MTKView, cap: Int) {
            let screenRate = view.window?.screen?.maximumFramesPerSecond ?? 60
            let fps = cap == 0 ? screenRate : min(cap, screenRate)
            if view.preferredFramesPerSecond != fps { view.preferredFramesPerSecond = fps }
        }
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
        let coordinator = context.coordinator
        coordinator.watch(view)
        let owner = model
        let cap = model.settings.frameCap
        DispatchQueue.main.async { [weak view, weak owner] in
            owner?.attachWindow(view?.window)
            if let view { coordinator.applyFrameRate(view, cap: cap) }
        }
        return view
    }

    func updateNSView(_ nsView: MTKView, context: Context) {
        if let window = nsView.window { model.attachWindow(window) }
        context.coordinator.applyFrameRate(nsView, cap: model.settings.frameCap)
        if let window = nsView.window {
            let hidden = window.isMiniaturized || !window.occlusionState.contains(.visible)
            if nsView.isPaused != hidden { nsView.isPaused = hidden }
        }
    }
}
