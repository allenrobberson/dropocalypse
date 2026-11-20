import SwiftUI

// The 3D controls used by the toolbar, its menus and the detached controls window: raised keys
// with a thick bottom edge, mixer faders in recessed grooves, switches, segmented keys, trays and
// foldable section bars. The Windows and Linux versions draw the same parts (windows/src/ui3d.h).
//
// Every key keeps the same size and label line whether it's pressed or not: a pressed key changes
// colour and thins its bottom edge, so a row of keys always stays level.

enum Palette3D {
    static let key = Color(red: 0.204, green: 0.204, blue: 0.271)
    static let keyHover = Color(red: 0.267, green: 0.267, blue: 0.361)
    static let keyDown = Color(red: 0.165, green: 0.165, blue: 0.220)
    static let rim = Color(red: 0.353, green: 0.353, blue: 0.439)
    static let rimHover = Color(red: 0.478, green: 0.478, blue: 0.588)
    static let base = Color(red: 0.051, green: 0.051, blue: 0.078)
    static let label = Color(red: 0.941, green: 0.941, blue: 0.973)
    static let dim = Color(red: 0.627, green: 0.627, blue: 0.722)
    static let nameKey = Color(red: 0.133, green: 0.133, blue: 0.180)
    static let pink = Color(red: 1.0, green: 0.310, blue: 0.659)
    static let pinkRim = Color(red: 1.0, green: 0.549, blue: 0.776)
    static let pinkBase = Color(red: 0.541, green: 0.122, blue: 0.341)
    static let pinkLabel = Color(red: 0.165, green: 0.039, blue: 0.102)
    static let green = Color(red: 0.122, green: 0.561, blue: 0.416)
    static let greenRim = Color(red: 0.373, green: 0.827, blue: 0.659)
    static let greenBase = Color(red: 0.043, green: 0.271, blue: 0.196)
    static let red = Color(red: 1.0, green: 0.42, blue: 0.42)
    static let recRed = Color(red: 0.85, green: 0.12, blue: 0.18)
    static let groove = Color(red: 0.043, green: 0.043, blue: 0.067)
    static let grooveLip = Color(red: 0.204, green: 0.204, blue: 0.290)
    static let cap = Color(red: 0.784, green: 0.784, blue: 0.851)
    static let capTop = Color(red: 0.906, green: 0.906, blue: 0.945)
    static let capRidge = Color(red: 0.580, green: 0.580, blue: 0.667)
    static let capBase = Color(red: 0.290, green: 0.290, blue: 0.369)
    static let lcd = Color(red: 1.0, green: 0.702, blue: 0.863)
    static let panel = Color(red: 0.118, green: 0.118, blue: 0.161)
    static let well = Color(red: 0.078, green: 0.078, blue: 0.110)
    static let bar = Color(red: 0.098, green: 0.098, blue: 0.133)
    static let section = Color(red: 0.165, green: 0.165, blue: 0.220)
}

enum KeyLook { case normal, on, lit, name, alert, recording }

/// Small ▾ drawn inside a key's lower margin: this key opens a menu.
private struct CaretShape: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        p.closeSubpath()
        return p
    }
}

/// The look of one key in one state.
private struct KeyColors {
    var face: Color, rim: Color, base: Color, text: Color, down: Bool

    init(look: KeyLook, pressed: Bool, hover: Bool) {
        face = Palette3D.key; rim = Palette3D.rim; base = Palette3D.base; text = Palette3D.label; down = pressed
        switch look {
        case .on: face = Palette3D.pink; rim = Palette3D.pinkRim; base = Palette3D.pinkBase; text = Palette3D.pinkLabel; down = true
        case .lit: face = Palette3D.green; rim = Palette3D.greenRim; base = Palette3D.greenBase; text = .white; down = true
        case .recording: face = Palette3D.recRed; rim = Palette3D.red; base = Color(red: 0.35, green: 0.04, blue: 0.06); text = .white; down = true
        case .name: face = Palette3D.nameKey
        case .alert: text = Palette3D.red
        case .normal: break
        }
        switch look {
        case .normal, .name, .alert:
            if pressed { face = Palette3D.keyDown } else if hover { face = look == .name ? Palette3D.key : Palette3D.keyHover; rim = Palette3D.rimHover }
        default:
            if hover && !pressed { rim = Palette3D.label }
        }
    }
}

/// A raised key around any label. `height` includes the 3-point bottom edge.
struct RaisedKey<Label: View>: View {
    var look: KeyLook = .normal
    var pressed = false
    var height: CGFloat = 32
    var minWidth: CGFloat? = nil
    var fill = false
    var caret = false
    var label: () -> Label
    @State private var hover = false

    init(look: KeyLook = .normal, pressed: Bool = false, height: CGFloat = 32, minWidth: CGFloat? = nil,
         fill: Bool = false, caret: Bool = false, @ViewBuilder label: @escaping () -> Label) {
        self.look = look
        self.pressed = pressed
        self.height = height
        self.minWidth = minWidth
        self.fill = fill
        self.caret = caret
        self.label = label
    }

    var body: some View {
        let c = KeyColors(look: look, pressed: pressed, hover: hover)
        let edge: CGFloat = c.down ? 1 : 3
        label()
            .foregroundStyle(c.text)
            .lineLimit(1)
            .padding(.horizontal, 10)
            .frame(minWidth: minWidth, maxWidth: fill ? .infinity : nil)
            .frame(height: height - 3)          // the face: every label sits on its centre line
            .padding(.bottom, 3)
            .background(
                ZStack(alignment: .top) {
                    RoundedRectangle(cornerRadius: 7).fill(c.base)
                    RoundedRectangle(cornerRadius: 7)
                        .fill(c.face)
                        .overlay(RoundedRectangle(cornerRadius: 7).strokeBorder(c.rim, lineWidth: 1))
                        .padding(.bottom, edge)
                }
            )
            .overlay(alignment: .bottom) {
                if caret {
                    CaretShape()
                        .fill(look == .on ? Palette3D.pinkBase : Palette3D.dim)
                        .frame(width: 7, height: 3.5)
                        .padding(.bottom, 6)
                }
            }
            .contentShape(Rectangle())
            .onHover { hover = $0 }
    }
}

/// Button style that draws the button as a raised key.
struct RaisedKeyStyle: ButtonStyle {
    var look: KeyLook = .normal
    var height: CGFloat = 30
    var minWidth: CGFloat? = nil
    var fill = false
    var caret = false

    func makeBody(configuration: Configuration) -> some View {
        RaisedKey(look: look, pressed: configuration.isPressed, height: height, minWidth: minWidth,
                  fill: fill, caret: caret) {
            configuration.label.font(.system(size: 13, weight: .medium))
        }
    }
}

/// On/off switch: recessed track, raised knob, green when on.
struct Switch3DStyle: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 8) {
            ZStack(alignment: configuration.isOn ? .trailing : .leading) {
                Capsule().fill(configuration.isOn ? Palette3D.green : Palette3D.groove)
                    .overlay(Capsule().strokeBorder(Color.black, lineWidth: 1))
                    .frame(width: 40, height: 22)
                Circle()
                    .fill(Palette3D.cap)
                    .overlay(Circle().strokeBorder(Palette3D.capTop, lineWidth: 1))
                    .shadow(color: .black.opacity(0.5), radius: 0, x: 0, y: 1.5)
                    .frame(width: 18, height: 18)
                    .padding(.horizontal, 2)
            }
            .animation(.easeOut(duration: 0.12), value: configuration.isOn)
            configuration.label
        }
        .contentShape(Rectangle())
        .onTapGesture { configuration.isOn.toggle() }
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(.isButton)
        .accessibilityValue(configuration.isOn ? "On" : "Off")
    }
}

/// A row of keys where the chosen one stays pressed in and pink.
struct Segmented3D<Value: Hashable>: View {
    @Binding var selection: Value
    let options: [(label: String, value: Value)]

    var body: some View {
        HStack(spacing: 4) {
            ForEach(options.indices, id: \.self) { i in
                Button(options[i].label) { selection = options[i].value }
                    .buttonStyle(RaisedKeyStyle(look: selection == options[i].value ? .on : .normal,
                                                height: 30, fill: true))
            }
        }
    }
}

/// A mixer fader: recessed groove that fills pink up to a ridged cap, and a small display.
struct Fader: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var format: (Double) -> String
    @State private var hover = false

    init(_ title: String, value: Binding<Double>, range: ClosedRange<Double>,
         format: @escaping (Double) -> String = { String(format: "%.2f", $0) }) {
        self.title = title
        self._value = value
        self.range = range
        self.format = format
    }

    private var fraction: CGFloat {
        let span = range.upperBound - range.lowerBound
        guard span > 0 else { return 0 }
        return CGFloat(min(1, max(0, (value - range.lowerBound) / span)))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title).font(.caption)
            HStack(spacing: 8) {
                GeometryReader { geo in
                    let capW: CGFloat = 14
                    let travel = max(1, geo.size.width - capW)
                    let x = capW / 2 + fraction * travel
                    ZStack(alignment: .leading) {
                        Capsule().fill(Palette3D.groove)
                            .overlay(Capsule().strokeBorder(Color.black, lineWidth: 1))
                            .frame(height: 10)
                        Capsule().fill(hover ? Palette3D.pinkRim : Palette3D.pink)
                            .frame(width: max(8, x), height: 7)
                            .padding(.leading, 1.5)
                        FaderCap(lit: hover)
                            .frame(width: capW, height: 24)
                            .offset(x: x - capW / 2)
                    }
                    .frame(width: geo.size.width, height: geo.size.height)
                    .contentShape(Rectangle())
                    .gesture(DragGesture(minimumDistance: 0).onChanged { drag in
                        let t = Double(min(1, max(0, (drag.location.x - capW / 2) / travel)))
                        value = range.lowerBound + t * (range.upperBound - range.lowerBound)
                    })
                    .onHover { hover = $0 }
                }
                .frame(height: 28)
                Text(format(value))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Palette3D.lcd)
                    .frame(minWidth: 40, alignment: .trailing)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 3)
                    .background(RoundedRectangle(cornerRadius: 5).fill(Palette3D.groove))
                    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.black, lineWidth: 1))
            }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(title)
        .accessibilityValue(format(value))
        .accessibilityAdjustableAction { direction in
            let step = (range.upperBound - range.lowerBound) / 20
            switch direction {
            case .increment: value = min(range.upperBound, value + step)
            case .decrement: value = max(range.lowerBound, value - step)
            @unknown default: break
            }
        }
    }
}

private struct FaderCap: View {
    var lit: Bool
    var body: some View {
        ZStack(alignment: .top) {
            RoundedRectangle(cornerRadius: 3).fill(Palette3D.capBase)
            RoundedRectangle(cornerRadius: 3)
                .fill(lit ? Palette3D.capTop : Palette3D.cap)
                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(Palette3D.capTop, lineWidth: 1))
                .overlay(
                    VStack(spacing: 3) {
                        ForEach(0..<3, id: \.self) { _ in
                            Rectangle().fill(Palette3D.capRidge).frame(height: 1)
                        }
                    }
                    .padding(.horizontal, 3)
                )
                .padding(.bottom, 3)
        }
    }
}

/// A recessed tray that groups faders.
struct Well<Content: View>: View {
    var content: () -> Content
    init(@ViewBuilder content: @escaping () -> Content) { self.content = content }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) { content() }
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 9).fill(Palette3D.well))
            .overlay(RoundedRectangle(cornerRadius: 9).strokeBorder(Color.black, lineWidth: 1))
    }
}

/// A drop-down menu's contents: pink accent, the menu's full name, an optional line under it, then
/// the controls, on a dark panel. Buttons inside are raised keys and toggles are switches.
struct MenuPanel<Content: View>: View {
    let title: String
    var subtitle: String? = nil
    var width: CGFloat = 300
    @ViewBuilder let content: () -> Content

    init(title: String, subtitle: String? = nil, width: CGFloat = 300, @ViewBuilder content: @escaping () -> Content) {
        self.title = title
        self.subtitle = subtitle
        self.width = width
        self.content = content
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                RoundedRectangle(cornerRadius: 2).fill(Palette3D.pink).frame(width: 4, height: 17)
                Text(title).font(.system(size: 15, weight: .semibold)).foregroundStyle(.white)
            }
            if let subtitle {
                Text(subtitle).font(.caption).foregroundStyle(.secondary)
            }
            Divider()
            content()
        }
        .padding(14)
        .frame(width: width)
        .background(Palette3D.panel)
        .buttonStyle(RaisedKeyStyle(height: 28))
        .toggleStyle(Switch3DStyle())
    }
}

/// Foldable section bar for the detached controls window: a raised key with a pink accent.
struct Raised3DDisclosureStyle: DisclosureGroupStyle {
    func makeBody(configuration: Configuration) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Button {
                withAnimation(.easeOut(duration: 0.15)) { configuration.isExpanded.toggle() }
            } label: {
                HStack(spacing: 10) {
                    RoundedRectangle(cornerRadius: 2).fill(Palette3D.pink).frame(width: 3, height: 16)
                    Image(systemName: "chevron.right")
                        .font(.system(size: 11, weight: .bold))
                        .rotationEffect(.degrees(configuration.isExpanded ? 90 : 0))
                    configuration.label.font(.system(size: 14, weight: .semibold))
                    Spacer(minLength: 0)
                }
            }
            .buttonStyle(RaisedKeyStyle(height: 36, fill: true))
            if configuration.isExpanded {
                configuration.content
                    .padding(.horizontal, 2)
            }
        }
    }
}
