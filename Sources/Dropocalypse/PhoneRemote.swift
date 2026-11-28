import AppKit
import DropocalypseCore
import DropoRemote
import SwiftUI

/// Phone remote: runs the shared web server (shared/remote, the same code as Windows and Linux),
/// shows its QR code, and keeps the phone page in step with the app. The phone page is built from
/// shared/remote/schema.json; every id there is read in `values()` and written in `set` / `act`.
final class PhoneRemote: ObservableObject {
    @Published private(set) var pairURL = ""
    @Published private(set) var phones: [String] = []
    @Published private(set) var error: String?
    @Published private(set) var qr: (size: Int, modules: [UInt8]) = (0, [])
    /// The QR code stays hidden until asked for, so an audience can't scan it off the big screen.
    @Published var showQR = false
    /// This Mac's local networks: (address, interface name).
    @Published private(set) var addresses: [NetAddress] = []
    struct NetAddress: Hashable { let ip: String; let name: String }
    private var pairCode = ""
    private var addressesAt = Date.distantPast

    private unowned let model: VisualizerModel
    private var settings: AppSettings { model.settings }
    private var server: OpaquePointer?
    private var timer: Timer?
    private var sent: [String: String] = [:]
    private var stylesKey = ""

    init(model: VisualizerModel) {
        self.model = model
        if settings.phoneRemote { start() }
    }

    func setEnabled(_ on: Bool) {
        settings.phoneRemote = on
        if on { start() } else { stop() }
    }

    /// The address the QR code uses ("" = automatic).
    func setAddress(_ ip: String) {
        settings.phoneAddress = ip
        if let server { remote_set_address(server, ip) }
        objectWillChange.send()
    }

    var currentAddress: String {
        guard let server else { return "" }
        return String(cString: remote_local_ip(server))
    }

    func disconnectAll() {
        if let server { remote_disconnect_all(server) }
    }

    private func start() {
        guard server == nil else { return }
        error = nil
        guard let s = remote_start(47800, "mac") else {
            error = String(cString: remote_last_error())
            return
        }
        server = s
        remote_set_tokens(s, settings.phoneTokens)
        remote_set_address(s, settings.phoneAddress)
        timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in self?.tick() }
        tick()
    }

    private func stop() {
        timer?.invalidate()
        timer = nil
        if let server { remote_stop(server) }
        server = nil
        pairURL = ""
        phones = []
        sent = [:]
        stylesKey = ""
    }

    private var lastSync = Date.distantPast

    private func tick() {
        guard let server else { return }
        var fresh = false
        var ev = RemoteEvent()
        while remote_next_event(server, &ev) != 0 {
            switch Int(ev.type) {
            case Int(REMOTE_EVENT_CONNECTED):
                send(stylesMessage(), to: ev.client)
                var all: [String: Any] = [:]
                for (k, v) in values() { all[k] = v }
                send(["t": "state", "v": all], to: ev.client)
            case Int(REMOTE_EVENT_MESSAGE):
                let data = Data(bytes: ev.data, count: ev.size)
                guard let msg = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
                      let id = msg["id"] as? String else { continue }
                if msg["t"] as? String == "set" { set(id, msg["v"]) }
                else if msg["t"] as? String == "act" { act(id, msg) }
                fresh = true
            default:
                break
            }
        }

        let tokens = String(cString: remote_tokens(server))
        if tokens != settings.phoneTokens { settings.phoneTokens = tokens }
        let code = String(cString: remote_pair_code(server))
        if code != pairCode {   // a phone just used the code: hide the new one again
            if !pairCode.isEmpty { showQR = false }
            pairCode = code
        }
        if Date().timeIntervalSince(addressesAt) > 3 {
            addressesAt = Date()
            let list = String(cString: remote_addresses(server)).split(separator: "\n").map { line -> NetAddress in
                let parts = line.split(separator: "\t", maxSplits: 1, omittingEmptySubsequences: false)
                return NetAddress(ip: String(parts[0]), name: parts.count > 1 ? String(parts[1]) : "")
            }
            if list != addresses { addresses = list }
        }
        let url = String(cString: remote_pair_url(server))
        if url != pairURL {
            pairURL = url
            var modules = [UInt8](repeating: 0, count: 177 * 177)
            let size = url.isEmpty ? 0 : Int(remote_qr(url, &modules, Int32(modules.count)))
            qr = (size, modules)
        }
        let list = String(cString: remote_clients(server)).split(separator: "\n").map(String.init)
        if list != phones {
            model.objectWillChange.send()   // the toolbar's Phone key lights up while a phone is connected
            phones = list
        }

        guard fresh || Date().timeIntervalSince(lastSync) >= 0.1 else { return }
        lastSync = Date()
        guard !list.isEmpty else { sent = [:]; stylesKey = ""; return }
        let scenes = VisualScene.all
        let key = "\(scenes.count)/\(settings.userPresetStart ?? -1)/\(model.milkCatalog.bundledCount)/\(scenes.last?.name ?? "")"
        if key != stylesKey {
            stylesKey = key
            send(stylesMessage(), to: -1)
        }
        var diff: [String: Any] = [:]
        for (k, v) in values() {
            let text = Self.json(v)
            if sent[k] != text { sent[k] = text; diff[k] = v }
        }
        if !diff.isEmpty { send(["t": "state", "v": diff], to: -1) }
    }

    // MARK: Options

    private func values() -> [(String, Any)] {
        let scenes = VisualScene.all
        let n = max(1, scenes.count)
        let bundledEnd = min(scenes.count, VisualScene.builtInCount + model.milkCatalog.bundledCount)
        let cycle = scenes.prefix(bundledEnd).map { settings.isInCycle($0.name) ? "1" : "0" }.joined()
        let folder = model.milkFolder?.path.replacingOccurrences(of: NSHomeDirectory(), with: "~") ?? ""
        let milk: [String: Any] = [
            "available": model.milkEngineError == nil, "error": model.milkEngineError ?? "",
            "folder": folder, "count": model.milkFolder == nil ? 0 : model.milkCatalog.userCount,
            "truncated": model.milkCatalog.userTruncated, "scanning": model.milkScanning,
        ]
        return [
            ("style.index", ((settings.sceneIndex % n) + n) % n),
            ("style.autoCycle", settings.autoCycle),
            ("style.cycleSeconds", settings.cycleSeconds),
            ("style.cycle", cycle),
            ("milk.status", milk),
            ("milk.cycleUser", settings.cycleUserPresets),
            ("motion.pulse", settings.pulse), ("motion.squash", settings.squash),
            ("motion.bounce", settings.bounce), ("motion.slide", settings.slide),
            ("motion.wobble", settings.wobble),
            ("layers.wave", settings.layerWave), ("layers.explode", settings.layerExplode),
            ("layers.bands", settings.layerBands), ("layers.scatter", settings.layerScatter),
        ]
    }

    private func set(_ id: String, _ value: Any?) {
        let d = (value as? NSNumber)?.doubleValue ?? 0
        let on = (value as? Bool) ?? (d != 0)
        func clamp(_ lo: Double, _ hi: Double) -> Double { min(hi, max(lo, d)) }
        switch id {
        case "style.index": settings.sceneIndex = min(max(0, Int(d)), VisualScene.all.count - 1)
        case "style.autoCycle": settings.autoCycle = on
        case "style.cycleSeconds": settings.cycleSeconds = clamp(5, 360)
        case "milk.cycleUser": settings.cycleUserPresets = on
        case "motion.pulse": settings.pulse = clamp(0, 1.5)
        case "motion.squash": settings.squash = clamp(0, 1.5)
        case "motion.bounce": settings.bounce = clamp(0, 1.5)
        case "motion.slide": settings.slide = clamp(0, 1.5)
        case "motion.wobble": settings.wobble = clamp(0, 1.5)
        case "layers.wave": settings.layerWave = clamp(0, 100)
        case "layers.explode": settings.layerExplode = clamp(0, 100)
        case "layers.bands": settings.layerBands = clamp(0, 100)
        case "layers.scatter": settings.layerScatter = clamp(0, 100)
        default: break
        }
    }

    private func act(_ id: String, _ msg: [String: Any]) {
        switch id {
        case "style.prev": model.nextScene(-1)
        case "style.next": model.nextScene(1)
        case "style.cycleAll": settings.setAllInCycle(true)
        case "style.cycleNone": settings.setAllInCycle(false)
        case "style.setCycle":
            let i = (msg["i"] as? NSNumber)?.intValue ?? -1
            let on = (msg["v"] as? Bool) ?? false
            if i >= 0 && i < VisualScene.all.count { settings.setInCycle(VisualScene.all[i].name, on) }
        case "milk.choose": model.chooseMilkFolder()
        case "milk.open": model.openMilkFolder()
        case "milk.rescan": model.refreshMilkdrop()
        case "milk.remove": model.removeMilkFolder()
        case "motion.reset": settings.resetMotion()
        default: break
        }
    }

    private func stylesMessage() -> [String: Any] {
        [
            "t": "styles", "names": VisualScene.all.map(\.name), "builtIn": VisualScene.builtInCount,
            "bundledEnd": VisualScene.builtInCount + model.milkCatalog.bundledCount,
            "userStart": settings.userPresetStart ?? -1,
        ]
    }

    private func send(_ obj: [String: Any], to client: Int32) {
        guard let server else { return }
        let text = Self.json(obj)
        text.withCString { remote_send(server, client, $0, strlen($0)) }
    }

    private static func json(_ v: Any) -> String {
        guard let data = try? JSONSerialization.data(withJSONObject: v, options: [.fragmentsAllowed, .sortedKeys])
        else { return "null" }
        return String(decoding: data, as: UTF8.self)
    }
}

/// The Phone remote panel: the switch, the QR code to scan and the connected phones. Used by the
/// toolbar's Phone menu and the detached controls window.
struct PhoneRemotePanel: View {
    @ObservedObject var remote: PhoneRemote
    @ObservedObject var settings: AppSettings

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Toggle("Phone remote", isOn: Binding(get: { settings.phoneRemote }, set: { remote.setEnabled($0) }))
            Text("Control Dropocalypse from your phone's web browser. No app to install.")
                .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            if settings.phoneRemote {
                if let error = remote.error {
                    Text(error).font(.caption).foregroundStyle(.red)
                } else if remote.pairURL.isEmpty {
                    Text("No local network found. Connect this Mac to Wi-Fi (or to your phone's hotspot) to use the phone remote.")
                        .font(.caption).foregroundStyle(.red).fixedSize(horizontal: false, vertical: true)
                } else if !remote.showQR {
                    Button("Show QR code to pair a phone") { remote.showQR = true }
                    Text("Hidden so an audience can't scan it off the screen. Paired phones reconnect on their own.")
                        .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                } else {
                    QRCodeView(size: remote.qr.size, modules: remote.qr.modules)
                        .frame(width: 190, height: 190)
                    Text("Scan with your phone's camera.").font(.callout)
                    Text("Or open \(remote.pairURL)").font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                    Text("The code works once; each phone gets a new one.").font(.caption).foregroundStyle(.secondary)
                    Button("Hide QR code") { remote.showQR = false }
                }
                if remote.addresses.count > 1 || !settings.phoneAddress.isEmpty {
                    Picker("Network", selection: Binding(get: { settings.phoneAddress },
                                                         set: { remote.setAddress($0) })) {
                        Text("Automatic (\(remote.currentAddress))").tag("")
                        ForEach(remote.addresses, id: \.ip) { entry in
                            Text(entry.name.isEmpty ? entry.ip : "\(entry.ip)  (\(entry.name))").tag(entry.ip)
                        }
                        if !settings.phoneAddress.isEmpty && !remote.addresses.contains(where: { $0.ip == settings.phoneAddress }) {
                            Text("\(settings.phoneAddress) (not connected)").tag(settings.phoneAddress)
                        }
                    }
                    .font(.caption)
                }
                Text("The phone and this Mac must be on the same Wi-Fi (or this Mac joins the phone's hotspot). If macOS asks, allow Dropocalypse to find devices on your local network.")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                if remote.phones.isEmpty {
                    Text("No phones connected.").font(.caption).foregroundStyle(.secondary)
                    if !settings.phoneTokens.isEmpty {
                        Button("Forget paired phones") { remote.disconnectAll() }
                    }
                } else {
                    Text("Connected:").font(.callout)
                    ForEach(remote.phones, id: \.self) { Text("• \($0)").font(.caption) }
                    Button("Disconnect phones") { remote.disconnectAll() }
                    Text("Disconnected phones need to scan a new QR code.").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
        .frame(width: 280, alignment: .leading)
    }
}

private struct QRCodeView: View {
    let size: Int
    let modules: [UInt8]

    var body: some View {
        Canvas { ctx, area in
            ctx.fill(Path(roundedRect: CGRect(origin: .zero, size: area), cornerRadius: 6), with: .color(.white))
            guard size > 0 else { return }
            let cell = area.width / CGFloat(size + 8)
            var path = Path()
            for y in 0..<size {
                for x in 0..<size where modules[y * size + x] != 0 {
                    path.addRect(CGRect(x: CGFloat(x + 4) * cell, y: CGFloat(y + 4) * cell,
                                        width: cell + 0.5, height: cell + 0.5))
                }
            }
            ctx.fill(path, with: .color(.black))
        }
    }
}
