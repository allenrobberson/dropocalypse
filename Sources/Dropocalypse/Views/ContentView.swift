import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var model: VisualizerModel
    @ObservedObject var settings: AppSettings
    @ObservedObject var capture: AudioCapture
    @ObservedObject var whatsNew: WhatsNew
    @State private var dropTargeted = false

    init(model: VisualizerModel) {
        self.model = model
        self.settings = model.settings
        self.capture = model.capture
        self.whatsNew = model.whatsNew
    }

    var body: some View {
        ZStack(alignment: .top) {
            MetalView(model: model)
                .ignoresSafeArea()

            if model.toolbarVisible && !model.controlsDetached {
                TopToolbar(model: model, settings: settings, capture: capture, updates: model.updates)
                    .padding(.top, model.topSafeInset)   // below the MacBook camera notch in full screen
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .zIndex(1)
            }

            // Recording messages ("Saved …"); shown in the controls window instead when it's detached.
            if let message = model.recordMessage, !model.controlsDetached {
                HStack(spacing: 10) {
                    Text(message)
                    if model.lastRecordingURL != nil, message.hasPrefix("Saved") {
                        Button("Show in Finder") { model.showLastRecording() }
                    }
                }
                .font(.system(size: 12, weight: .medium))
                .padding(10)
                .background(.black.opacity(0.65), in: RoundedRectangle(cornerRadius: 10))
                .padding(.bottom, 24)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            }

            // Problems stay on screen until fixed, independent of the toolbar.
            if capture.permissionDenied || model.errorMessage != nil {
                VStack(spacing: 8) {
                    if let error = model.errorMessage {
                        Text(error).foregroundStyle(.yellow)
                    }
                    if capture.permissionDenied {
                        Text(capture.statusText)
                        Button("Open Microphone Settings") {
                            if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone") {
                                NSWorkspace.shared.open(url)
                            }
                        }
                    }
                }
                .font(.system(size: 12, weight: .medium))
                .padding(12)
                .background(.black.opacity(0.6), in: RoundedRectangle(cornerRadius: 10))
                .padding(.bottom, 24)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            }

            if dropTargeted {
                RoundedRectangle(cornerRadius: 18)
                    .strokeBorder(.white.opacity(0.8), style: StrokeStyle(lineWidth: 3, dash: [10, 8]))
                    .padding(24)
                    .overlay(Text("Drop to add as a logo").font(.title2.bold()).foregroundStyle(.white))
                    .allowsHitTesting(false)
            }
        }
        .background(Color.black)
        .ignoresSafeArea()
        .sheet(isPresented: $whatsNew.isPresented, onDismiss: { whatsNew.markSeen() }) {
            WhatsNewView(whatsNew: whatsNew)
        }
        .frame(minWidth: 640, minHeight: 400)
        .onContinuousHover { phase in
            if case .active(let point) = phase { model.pointerMoved(toY: point.y) }
        }
        .onDrop(of: [.fileURL, .image], isTargeted: $dropTargeted) { providers in
            model.handleDrop(providers)
        }
        .onAppear { model.startAudio() }
        .onChange(of: settings.inputDeviceUID) { _ in model.startAudio() }
    }
}
