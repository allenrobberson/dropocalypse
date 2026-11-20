// ui3d.h: Dropocalypse's 3D controls for Dear ImGui (Windows and Linux).
//
// Raised keys with a thick bottom edge, mixer-style faders in recessed grooves, toggle switches,
// segmented buttons, raised pickers and menu panels, plus the toolbar layout that switches between
// full words and single letters when the window is narrow. Platform-free: include after imgui.h.
//
// Every key in a row has the same height and the same label centre line, pressed or not: a pressed
// key changes colour and thins its bottom edge but never moves, so the toolbar stays level.
#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ui3d {

inline float& scale() {   // DPI scale, set once per frame with setScale()
    static float s = 1.0f;
    return s;
}
inline void setScale(float s) { scale() = s; }
inline float px(float v) { return v * scale(); }

// ---- Palette (straight RGBA; drawn through GetColorU32 so disabled items fade like ImGui's own).
namespace col {
inline const ImVec4 key{0.204f, 0.204f, 0.271f, 1};         // #343445
inline const ImVec4 keyHover{0.267f, 0.267f, 0.361f, 1};    // #44445c
inline const ImVec4 keyDown{0.165f, 0.165f, 0.220f, 1};     // #2a2a38
inline const ImVec4 keyRim{0.353f, 0.353f, 0.439f, 1};      // #5a5a70
inline const ImVec4 keyRimHover{0.478f, 0.478f, 0.588f, 1}; // #7a7a96
inline const ImVec4 keyBase{0.051f, 0.051f, 0.078f, 1};     // #0d0d14 (bottom edge)
inline const ImVec4 label{0.941f, 0.941f, 0.973f, 1};       // #f0f0f8
inline const ImVec4 dim{0.627f, 0.627f, 0.722f, 1};         // #a0a0b8
inline const ImVec4 nameKey{0.133f, 0.133f, 0.180f, 1};     // #22222e (style name)
inline const ImVec4 nameRim{0.267f, 0.267f, 0.353f, 1};
inline const ImVec4 pink{1.0f, 0.310f, 0.659f, 1};          // #ff4fa8
inline const ImVec4 pinkRim{1.0f, 0.549f, 0.776f, 1};
inline const ImVec4 pinkBase{0.541f, 0.122f, 0.341f, 1};
inline const ImVec4 pinkLabel{0.165f, 0.039f, 0.102f, 1};
inline const ImVec4 green{0.122f, 0.561f, 0.416f, 1};       // #1f8f6a
inline const ImVec4 greenRim{0.373f, 0.827f, 0.659f, 1};
inline const ImVec4 greenBase{0.043f, 0.271f, 0.196f, 1};
inline const ImVec4 greenLabel{0.910f, 1.0f, 0.965f, 1};
inline const ImVec4 red{1.0f, 0.42f, 0.42f, 1};
inline const ImVec4 recRed{0.85f, 0.12f, 0.18f, 1};
inline const ImVec4 groove{0.043f, 0.043f, 0.067f, 1};      // #0b0b11
inline const ImVec4 grooveLip{0.204f, 0.204f, 0.290f, 1};   // lower lip highlight
inline const ImVec4 capTop{0.906f, 0.906f, 0.945f, 1};
inline const ImVec4 cap{0.784f, 0.784f, 0.851f, 1};
inline const ImVec4 capRidge{0.580f, 0.580f, 0.667f, 1};
inline const ImVec4 capBase{0.290f, 0.290f, 0.369f, 1};
inline const ImVec4 lcdText{1.0f, 0.702f, 0.863f, 1};
inline const ImVec4 panel{0.118f, 0.118f, 0.161f, 0.98f};   // #1e1e29
inline const ImVec4 panelRim{0.290f, 0.290f, 0.376f, 1};    // #4a4a60
inline const ImVec4 panelBase{0.031f, 0.031f, 0.051f, 1};
inline const ImVec4 well{0.078f, 0.078f, 0.110f, 1};        // #14141c
inline const ImVec4 bar{0.098f, 0.098f, 0.133f, 1};         // toolbar #191922
}  // namespace col

inline ImU32 u32(const ImVec4& c) { return ImGui::GetColorU32(c); }

/// The app-wide ImGui style: dark panels, recessed text fields, raised buttons and headers.
inline void applyTheme(ImGuiStyle& s) {
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0;
    s.PopupRounding = 10;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.GrabRounding = 4;
    s.WindowBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 1;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(9, 5);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12;
    s.GrabMinSize = 12;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col::label;
    c[ImGuiCol_TextDisabled] = ImVec4(0.604f, 0.604f, 0.690f, 1);
    c[ImGuiCol_WindowBg] = col::bar;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col::panel;
    c[ImGuiCol_Border] = col::panelRim;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = col::groove;                       // text fields and checkboxes sit recessed
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.090f, 0.090f, 0.125f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.110f, 0.110f, 0.153f, 1);
    c[ImGuiCol_Button] = col::key;
    c[ImGuiCol_ButtonHovered] = col::keyHover;
    c[ImGuiCol_ButtonActive] = col::keyDown;
    c[ImGuiCol_Header] = ImVec4(1.0f, 0.310f, 0.659f, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.310f, 0.659f, 0.20f);
    c[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.310f, 0.659f, 0.40f);
    c[ImGuiCol_CheckMark] = col::pink;
    c[ImGuiCol_SliderGrab] = col::pink;
    c[ImGuiCol_SliderGrabActive] = col::pinkRim;
    c[ImGuiCol_Separator] = ImVec4(0.231f, 0.231f, 0.302f, 1);
    c[ImGuiCol_TitleBg] = col::bar;
    c[ImGuiCol_TitleBgActive] = col::bar;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.2f);
    c[ImGuiCol_ScrollbarGrab] = col::key;
    c[ImGuiCol_ScrollbarGrabHovered] = col::keyHover;
    c[ImGuiCol_ScrollbarGrabActive] = col::keyRimHover;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

// ---------------------------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------------------------

enum class Look { Normal, On, Lit, Name, Alert, Recording };
enum class Glyph { None, Left, Right, Up, Down, Cycle, Dot, Stop };

struct KeyOpts {
    Look look = Look::Normal;
    Glyph glyph = Glyph::None;
    bool caret = false;            // small ▾ inside the bottom edge: this key opens a menu
    const char* tooltip = nullptr;
    bool spin = false;             // Cycle glyph turns slowly (auto-cycle on)
    bool alignLeft = false;        // text starts at the left (the style name)
};

inline float edge() { return px(3); }   // bottom edge thickness of a raised key

/// Visible part of an ImGui label ("Text##id" -> "Text").
inline std::string visibleLabel(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? std::string(label, hash) : std::string(label);
}

inline void drawGlyph(ImDrawList* dl, Glyph g, ImVec2 c, float sz, ImU32 colr, bool spin) {
    const float t = px(2.4f);
    switch (g) {
    case Glyph::Left: case Glyph::Right: case Glyph::Up: case Glyph::Down: {
        ImVec2 pts[3];
        if (g == Glyph::Left) { pts[0] = {c.x + sz * 0.3f, c.y - sz * 0.55f}; pts[1] = {c.x - sz * 0.3f, c.y}; pts[2] = {c.x + sz * 0.3f, c.y + sz * 0.55f}; }
        else if (g == Glyph::Right) { pts[0] = {c.x - sz * 0.3f, c.y - sz * 0.55f}; pts[1] = {c.x + sz * 0.3f, c.y}; pts[2] = {c.x - sz * 0.3f, c.y + sz * 0.55f}; }
        else if (g == Glyph::Up) { pts[0] = {c.x - sz * 0.5f, c.y + sz * 0.25f}; pts[1] = {c.x, c.y - sz * 0.3f}; pts[2] = {c.x + sz * 0.5f, c.y + sz * 0.25f}; }
        else { pts[0] = {c.x - sz * 0.5f, c.y - sz * 0.25f}; pts[1] = {c.x, c.y + sz * 0.3f}; pts[2] = {c.x + sz * 0.5f, c.y - sz * 0.25f}; }
        dl->AddPolyline(pts, 3, colr, 0, t);
        break;
    }
    case Glyph::Cycle: {
        const float r = sz * 0.55f;
        const float a0 = spin ? (float)ImGui::GetTime() * 2.2f : -1.2f;
        for (int half = 0; half < 2; ++half) {   // two arcs with arrowheads, like a refresh icon
            const float s0 = a0 + half * 3.14159f, s1 = s0 + 2.3f;
            dl->PathArcTo(c, r, s0, s1, 14);
            dl->PathStroke(colr, 0, px(2.0f));
            const ImVec2 tip{c.x + std::cos(s1) * r, c.y + std::sin(s1) * r};
            const ImVec2 dir{-std::sin(s1), std::cos(s1)};      // tangent
            const ImVec2 out{std::cos(s1), std::sin(s1)};
            const float h = px(3.6f);
            dl->AddTriangleFilled({tip.x + dir.x * h, tip.y + dir.y * h},
                                  {tip.x + out.x * h * 0.9f - dir.x * h * 0.3f, tip.y + out.y * h * 0.9f - dir.y * h * 0.3f},
                                  {tip.x - out.x * h * 0.9f - dir.x * h * 0.3f, tip.y - out.y * h * 0.9f - dir.y * h * 0.3f}, colr);
        }
        break;
    }
    case Glyph::Dot: dl->AddCircleFilled(c, sz * 0.42f, colr, 20); break;
    case Glyph::Stop: dl->AddRectFilled({c.x - sz * 0.32f, c.y - sz * 0.32f}, {c.x + sz * 0.32f, c.y + sz * 0.32f}, colr, px(2)); break;
    case Glyph::None: break;
    }
}

/// Paints a raised key in [a, b]. `down` = drawn pressed in (thin bottom edge, same outer size).
inline void paintKey(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& body, const ImVec4& rim, const ImVec4& base,
                     bool down, float rounding = -1) {
    const float r = rounding < 0 ? px(7) : rounding;
    const float e = down ? px(1) : edge();
    dl->AddRectFilled(a, b, u32(base), r);                          // the dark edge underneath
    dl->AddRectFilled(a, {b.x, b.y - e}, u32(body), r);             // the key face
    dl->AddRect(a, {b.x, b.y - e}, u32(rim), r, 0, px(1));          // lit rim
}

/// A raised key. Returns true when clicked. `size.y` is the full height including the bottom edge.
inline bool key(const char* id, const char* label, ImVec2 size, const KeyOpts& o = {}, bool* rightClicked = nullptr) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool rc = clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    const bool left = clicked && !rc;
    if (rightClicked) *rightClicked = rc;
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImVec2 b{a.x + size.x, a.y + size.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 body = col::key, rim = col::keyRim, base = col::keyBase, text = col::label;
    bool down = act;
    switch (o.look) {
    case Look::On: body = col::pink; rim = col::pinkRim; base = col::pinkBase; text = col::pinkLabel; down = true; break;
    case Look::Lit: body = col::green; rim = col::greenRim; base = col::greenBase; text = col::greenLabel; down = true; break;
    case Look::Name: body = col::nameKey; rim = col::nameRim; break;
    case Look::Alert: text = col::red; break;
    case Look::Recording: body = col::recRed; rim = col::red; base = ImVec4(0.35f, 0.04f, 0.06f, 1); down = true; break;
    case Look::Normal: break;
    }
    if (o.look == Look::Normal || o.look == Look::Name || o.look == Look::Alert) {
        if (act) body = col::keyDown;
        else if (hov) { body = o.look == Look::Name ? col::key : col::keyHover; rim = col::keyRimHover; }
    } else if (hov && !act) {
        rim = col::label;
    }
    paintKey(dl, a, b, body, rim, base, down);

    // Labels sit on one centre line for every key (the face of an unpressed key), pressed or not.
    const float cy = a.y + (size.y - edge()) * 0.5f;
    const std::string vis = visibleLabel(label);
    const ImU32 tc = u32(text);
    const float gsz = px(13);
    float tx = a.x;
    const ImVec2 ts = vis.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(vis.c_str());
    const float glyphW = o.glyph == Glyph::None ? 0 : gsz + (vis.empty() ? 0 : px(6));
    const float total = glyphW + ts.x;
    tx = o.alignLeft ? a.x + px(10) : a.x + (size.x - total) * 0.5f;
    if (o.glyph != Glyph::None) {
        ImU32 gc = tc;
        if (o.glyph == Glyph::Dot || o.glyph == Glyph::Stop)
            gc = u32(o.look == Look::Recording ? col::label : col::red);
        drawGlyph(dl, o.glyph, {tx + gsz * 0.5f, cy}, gsz, gc, o.spin);
    }
    std::string shownText = vis;
    if (!vis.empty() && ts.x > size.x - px(12) - glyphW) {   // too long: trim with an ellipsis
        while (!shownText.empty() && ImGui::CalcTextSize((shownText + "...").c_str()).x > size.x - px(12) - glyphW)
            shownText.pop_back();
        shownText += "...";
    }
    if (!vis.empty()) {
        ImGui::PushClipRect({a.x + px(4), a.y}, {b.x - px(4), b.y}, true);
        const float sw = ImGui::CalcTextSize(shownText.c_str()).x;
        const float sx = o.alignLeft ? a.x + px(10) + glyphW : a.x + (size.x - glyphW - sw) * 0.5f + glyphW;
        dl->AddText({sx, cy - ts.y * 0.5f}, tc, shownText.c_str());
        ImGui::PopClipRect();
    }
    if (o.caret) {   // ▾ in the key's lower margin, below the label line
        const float w = px(3.5f), y0 = b.y - edge() - px(6.5f);
        dl->AddTriangleFilled({a.x + size.x * 0.5f - w, y0}, {a.x + size.x * 0.5f + w, y0},
                              {a.x + size.x * 0.5f, y0 + px(3.5f)},
                              u32(o.look == Look::On ? col::pinkBase : col::dim));
    }
    if (hov && o.tooltip && *o.tooltip) ImGui::SetTooltip("%s", o.tooltip);
    return left;
}

/// Width of a key that shows `text` (and an optional glyph) with comfortable side padding.
inline float keyWidth(const char* text, bool glyph = false, float minW = 0) {
    float w = (text && *text ? ImGui::CalcTextSize(visibleLabel(text).c_str()).x : 0) + px(22);
    if (glyph) w += px(13) + (text && *text ? px(6) : 0);
    return std::max(w, minW);
}

/// Height of an in-panel key: an ImGui frame plus the raised edge.
inline float buttonHeight() { return ImGui::GetFrameHeight() + edge(); }

/// Drop-in replacement for ImGui::Button: same label/size rules, drawn as a raised key.
inline bool button(const char* label, const ImVec2& sizeArg = ImVec2(0, 0), const char* tooltip = nullptr) {
    ImVec2 size = sizeArg;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (size.x == 0) size.x = keyWidth(label);
    else if (size.x < 0) size.x = std::max(px(20), avail + size.x);
    if (size.y <= 0) size.y = buttonHeight();
    KeyOpts o;
    o.tooltip = tooltip;
    return key(label, label, size, o);
}

inline bool smallButton(const char* label) {
    const ImVec2 size{keyWidth(label), ImGui::GetFontSize() + px(10)};
    return key(label, label, size);
}

/// Arrow key (replaces ImGui::ArrowButton).
inline bool arrowKey(const char* id, ImGuiDir dir) {
    const float h = buttonHeight();
    KeyOpts o;
    o.glyph = dir == ImGuiDir_Left ? Glyph::Left : dir == ImGuiDir_Right ? Glyph::Right : dir == ImGuiDir_Up ? Glyph::Up : Glyph::Down;
    return key(id, "", ImVec2(h, h), o);
}

// ---------------------------------------------------------------------------------------------
// Faders, switches, segmented buttons, pickers
// ---------------------------------------------------------------------------------------------

inline int& wellDepth() { static int d = 0; return d; }

/// Mixer fader: label on its own line, then a recessed groove (pink up to the value), a ridged
/// cap and a small display with the value. Same arguments as ImGui::SliderFloat.
inline bool fader(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f") {
    ImGui::TextUnformatted(visibleLabel(label).c_str());
    char shown[32];
    std::snprintf(shown, sizeof(shown), fmt, *v);
    const float lcdW = std::max(px(52), ImGui::CalcTextSize(shown).x + px(14));
    const float capW = px(14), capH = px(24);
    const float rowH = capH + px(4);
    const float w = std::max(px(80), ImGui::GetContentRegionAvail().x - lcdW - ImGui::GetStyle().ItemSpacing.x -
                                         wellDepth() * px(10));

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (rowH - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, capW);
    ImGui::SetNextItemWidth(w);
    const std::string id = std::string("##fader_") + label;
    const bool changed = ImGui::SliderFloat(id.c_str(), v, lo, hi, "", ImGuiSliderFlags_NoInput);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6);
    const bool hov = ImGui::IsItemHovered() || ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float cy = (a.y + b.y) * 0.5f;
    const float t = hi > lo ? std::clamp((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const float capX = a.x + capW * 0.5f + t * (b.x - a.x - capW);
    const float gh = px(5);
    dl->AddRectFilled({a.x, cy - gh}, {b.x, cy + gh}, u32(col::groove), gh);                    // groove
    dl->AddLine({a.x + gh, cy + gh}, {b.x - gh, cy + gh}, u32(col::grooveLip), px(1));          // lower lip
    dl->AddRectFilled({a.x + px(1.5f), cy - gh + px(1.5f)}, {capX, cy + gh - px(1.5f)},
                      u32(hov ? col::pinkRim : col::pink), gh);                                  // level
    const ImVec2 ca{capX - capW * 0.5f, cy - capH * 0.5f}, cb{capX + capW * 0.5f, cy + capH * 0.5f};
    paintKey(dl, ca, cb, hov ? col::capTop : col::cap, col::capTop, col::capBase, false, px(3));
    for (int i = -1; i <= 1; ++i) {   // grip ridges
        const float yy = cy - px(1.5f) + i * px(4);
        dl->AddLine({ca.x + px(3), yy}, {cb.x - px(3), yy}, u32(col::capRidge), px(1));
    }

    ImGui::SameLine();
    const ImVec2 la = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(lcdW, rowH));
    const ImVec2 lb{la.x + lcdW, la.y + rowH};
    const float lh = ImGui::GetFontSize() + px(8);
    const ImVec2 da{la.x, cy - lh * 0.5f}, db{lb.x, cy + lh * 0.5f};
    dl->AddRectFilled(da, db, u32(col::groove), px(5));
    dl->AddLine({da.x + px(4), db.y}, {db.x - px(4), db.y}, u32(col::grooveLip), px(1));
    const ImVec2 ts = ImGui::CalcTextSize(shown);
    dl->AddText({db.x - px(7) - ts.x, cy - ts.y * 0.5f}, u32(col::lcdText), shown);
    return changed;
}

/// On/off switch (replaces ImGui::Checkbox): recessed track, raised knob, green when on.
inline bool toggle(const char* label, bool* v) {
    const float h = px(22), w = px(42);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const std::string vis = visibleLabel(label);
    const float fh = ImGui::GetFrameHeight();
    const float textW = vis.empty() ? 0 : ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(vis.c_str()).x;
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w + textW, std::max(h, fh)));
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = a.y + std::max(h, fh) * 0.5f;
    const ImVec2 ta{a.x, cy - h * 0.5f}, tb{a.x + w, cy + h * 0.5f};
    dl->AddRectFilled(ta, tb, u32(*v ? col::green : col::groove), h * 0.5f);
    dl->AddLine({ta.x + h * 0.4f, tb.y}, {tb.x - h * 0.4f, tb.y}, u32(col::grooveLip), px(1));
    const float kr = h * 0.5f - px(2);
    const float kx = *v ? tb.x - px(2) - kr : ta.x + px(2) + kr;
    dl->AddCircleFilled({kx, cy + px(1)}, kr, u32(col::capBase), 20);       // knob edge
    dl->AddCircleFilled({kx, cy - px(0.5f)}, kr - px(0.5f), u32(hov ? col::capTop : col::cap), 20);
    if (!vis.empty())
        dl->AddText({tb.x + ImGui::GetStyle().ItemInnerSpacing.x, cy - ImGui::GetFontSize() * 0.5f},
                    ImGui::GetColorU32(ImGuiCol_Text), vis.c_str());
    return pressed;
}

/// A row of keys where the chosen one stays pressed in and pink. Returns true when it changes.
inline bool segmented(const char* id, const char* const* labels, int count, int* sel, float width = 0) {
    ImGui::PushID(id);
    const float sp = px(4);
    if (width <= 0) width = ImGui::GetContentRegionAvail().x - wellDepth() * px(10);
    const float kw = (width - sp * (count - 1)) / count;
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, sp);
        KeyOpts o;
        o.look = i == *sel ? Look::On : Look::Normal;
        ImGui::PushID(i);
        if (key("##seg", labels[i], ImVec2(kw, buttonHeight()), o) && *sel != i) {
            *sel = i;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

/// Raised picker (replaces ImGui::BeginCombo; end with ImGui::EndCombo as usual).
inline bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0) {
    ImDrawList* dl = ImGui::GetWindowDrawList();       // the window the picker sits in
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::key);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::keyHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col::keyDown);
    ImGui::PushStyleColor(ImGuiCol_Button, col::key);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::keyHover);
    ImGui::PushStyleColor(ImGuiCol_Border, col::keyRim);
    const bool open = ImGui::BeginCombo(id, preview, flags);
    ImGui::PopStyleColor(6);
    // The raised edge, drawn into the picker's own window even when its list just opened.
    dl->AddRectFilled({a.x + px(1), a.y + h - px(1)}, {a.x + w - px(1), a.y + h + px(2)}, u32(col::keyBase),
                      px(6), ImDrawFlags_RoundCornersBottom);
    return open;
}

// ---------------------------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------------------------

/// Title of a drop-down menu: a pink accent and the menu's full name, then a divider.
inline void menuTitle(const char* title, const char* subtitle = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float fs = ImGui::GetFontSize() * 1.15f;
    dl->AddRectFilled({a.x, a.y + px(2)}, {a.x + px(4), a.y + fs - px(1)}, u32(col::pink), px(2));
    dl->AddText(ImGui::GetFont(), fs, {a.x + px(12), a.y}, u32(ImVec4(1, 1, 1, 1)), title);
    ImGui::Dummy(ImVec2(px(12) + ImGui::CalcTextSize(title).x * 1.15f, fs));
    if (subtitle && *subtitle) ImGui::TextDisabled("%s", subtitle);
    ImGui::Separator();
}

/// The thick dark edge under a drop-down panel, so the menu reads as a raised plate.
inline void panelEdge() {
    const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
    if (s.y <= 1) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, {p.x + s.x, p.y + s.y}, false);
    dl->AddRectFilled({p.x + px(1), p.y + s.y - px(4)}, {p.x + s.x - px(1), p.y + s.y}, u32(col::panelBase),
                      ImGui::GetStyle().PopupRounding, ImDrawFlags_RoundCornersBottom);
    dl->PopClipRect();
}

/// A recessed tray that groups faders (begin/end around them; don't nest).
struct WellState { ImVec2 start; float width = 0; };
inline WellState& wellState() { static WellState w; return w; }

inline void beginWell() {
    WellState& w = wellState();
    w.start = ImGui::GetCursorScreenPos();
    w.width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->ChannelsSplit(2);
    ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::Indent(px(10));
    ImGui::BeginGroup();
    ++wellDepth();
}

inline void endWell() {
    WellState& w = wellState();
    --wellDepth();
    ImGui::EndGroup();
    ImGui::Unindent(px(10));
    ImGui::Dummy(ImVec2(0, px(2)));
    const float y1 = ImGui::GetItemRectMax().y + px(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    const ImVec2 a = w.start, b{w.start.x + w.width, y1};
    dl->AddRectFilled(a, b, u32(col::well), px(9));
    dl->AddRect(a, b, u32(ImVec4(0, 0, 0, 1)), px(9), 0, px(1));
    dl->AddLine({a.x + px(8), b.y}, {b.x - px(8), b.y}, u32(col::grooveLip), px(1));
    dl->ChannelsMerge();
}

/// Raised, foldable section bar for the detached controls window (replaces CollapsingHeader).
inline bool section(const char* label, ImGuiTreeNodeFlags flags = 0, bool highlight = false) {
    ImGui::Spacing();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(14), px(9)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 a{std::max(ImGui::GetItemRectMin().x, wp.x + ImGui::GetWindowContentRegionMin().x), ImGui::GetItemRectMin().y};
    const ImVec2 b{std::min(ImGui::GetItemRectMax().x, wp.x + ImGui::GetWindowContentRegionMax().x),
                   ImGui::GetItemRectMax().y + edge()};
    dl->ChannelsSetCurrent(0);
    const ImVec4 body = highlight ? ImVec4(0.92f, 0.29f, 0.67f, 0.9f) : act ? col::keyDown : hov ? col::key : ImVec4(0.165f, 0.165f, 0.220f, 1);
    paintKey(dl, a, b, body, hov ? col::keyRimHover : ImVec4(0.306f, 0.306f, 0.392f, 1), col::keyBase, act, px(8));
    dl->AddRectFilled({a.x + px(5), a.y + px(8)}, {a.x + px(8), b.y - edge() - px(8)}, u32(col::pink), px(2));
    dl->ChannelsMerge();
    ImGui::Dummy(ImVec2(0, edge()));
    return open;
}

// ---------------------------------------------------------------------------------------------
// Toolbar layout: full words when they fit, single letters when they don't
// ---------------------------------------------------------------------------------------------

struct BarItem {
    const char* id = "";
    std::string word;              // shown when there's room ("Motion")
    std::string letter;            // shown on small screens ("M"); empty = same as word
    std::string name;              // tooltip and menu title ("Motion")
    Glyph glyph = Glyph::None;
    Look look = Look::Normal;
    bool caret = false;
    bool right = false;            // right-hand group
    bool divider = false;          // a thin divider instead of a key
    bool square = false;           // always a square key (arrows)
    bool spin = false;
    float wordWidth = 0;           // fixed width in word mode (the style name)
    float letterWidth = 0;         // fixed width in letter mode
    // Results
    bool clicked = false, rightClicked = false;
    ImVec2 min, max;
};

inline float barKeyHeight() { return px(36); }
inline float barHeight() { return px(50); }

/// The last toolbar drawn (positions and states), for tests and previews.
inline std::vector<BarItem>& lastToolbar() { static std::vector<BarItem> v; return v; }

/// Lays out and draws the toolbar keys inside the current window, between x0 and x1.
/// Returns true when it's showing letters. Each item's clicked/min/max are filled in.
inline bool toolbar(std::vector<BarItem>& items, float x0, float x1, float statusMinW = 0, const char* status = nullptr) {
    const float kh = barKeyHeight();
    const float gap = px(6), divGap = px(8);
    auto widthOf = [&](const BarItem& it, bool letters) {
        if (it.divider) return px(1) + divGap * 2 - gap;
        if (it.square) return kh;
        if (letters && !it.letter.empty()) {
            if (it.letterWidth > 0) return it.letterWidth;
            const bool onlyGlyph = it.letter == " ";
            return onlyGlyph ? kh : std::max(kh, keyWidth(it.letter.c_str(), it.glyph != Glyph::None));
        }
        if (it.wordWidth > 0) return it.wordWidth;
        return keyWidth(it.word.c_str(), it.glyph != Glyph::None, kh);
    };
    auto total = [&](bool letters) {
        float w = 0;
        for (const auto& it : items) w += widthOf(it, letters) + gap;
        return w + statusMinW;
    };
    const bool letters = total(false) > (x1 - x0);

    const float winY = ImGui::GetWindowPos().y;
    const float y = (barHeight() - kh) * 0.5f;
    float lx = x0, rx = x1;
    float rightW = 0;
    for (const auto& it : items) if (it.right) rightW += widthOf(it, letters) + gap;
    rx = x1 - rightW + gap;
    const float rightStart = rx;
    for (auto& it : items) {
        const float w = widthOf(it, letters);
        float& cx = it.right ? rx : lx;
        const ImVec2 pos{cx, y};
        cx += w + gap;
        if (it.divider) {
            ImGui::GetWindowDrawList()->AddLine({ImGui::GetWindowPos().x + pos.x + w * 0.5f + gap * 0.5f, winY + y + px(6)},
                                                {ImGui::GetWindowPos().x + pos.x + w * 0.5f + gap * 0.5f, winY + y + kh - px(6)},
                                                u32(ImVec4(0.231f, 0.231f, 0.302f, 1)), px(1));
            continue;
        }
        ImGui::SetCursorPos(pos);
        KeyOpts o;
        o.look = it.look;
        o.glyph = it.glyph;
        o.caret = it.caret;
        o.spin = it.spin;
        o.alignLeft = it.wordWidth > 0 && !it.square && it.glyph == Glyph::None && it.letter.empty();
        const bool useLetter = letters && !it.letter.empty();
        std::string text = useLetter ? (it.letter == " " ? std::string() : it.letter) : it.word;
        o.tooltip = (useLetter || it.square || text.empty()) ? it.name.c_str() : nullptr;
        if (it.look == Look::Name) o.tooltip = nullptr;
        it.clicked = key(it.id, text.c_str(), ImVec2(w, kh), o, &it.rightClicked);
        it.min = ImGui::GetItemRectMin();
        it.max = ImGui::GetItemRectMax();
    }
    lastToolbar() = items;
    if (status && *status) {   // audio status in the space between the two groups
        const float sw = rightStart - gap - lx - px(12);
        if (sw > px(70)) {
            std::string s = status;
            while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > sw) s.pop_back();
            if (s.size() < std::strlen(status)) s += "...";
            const ImVec2 wp = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddText({wp.x + lx + px(4), wp.y + y + (kh - edge() - ImGui::GetFontSize()) * 0.5f},
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), s.c_str());
        }
    }
    return letters;
}

/// Opens popup `id` just under a toolbar key (right-aligned for keys on the right).
struct Anchor { ImVec2 pos; ImVec2 pivot; };
inline Anchor& anchorFor(const char* id) {
    static std::vector<std::pair<std::string, Anchor>> anchors;
    for (auto& a : anchors) if (a.first == id) return a.second;
    anchors.push_back({id, Anchor{}});
    return anchors.back().second;
}
inline void openUnder(const char* popupId, const BarItem& it) {
    Anchor& a = anchorFor(popupId);
    a.pos = it.right ? ImVec2(it.max.x, it.max.y + px(6)) : ImVec2(it.min.x, it.max.y + px(6));
    a.pivot = it.right ? ImVec2(1, 0) : ImVec2(0, 0);
    ImGui::OpenPopup(popupId);
}
/// BeginPopup for a toolbar menu: placed under its key, raised edge drawn, title shown.
inline bool beginMenu(const char* popupId, const char* title, const char* subtitle = nullptr) {
    if (ImGui::IsPopupOpen(popupId)) {
        const Anchor& a = anchorFor(popupId);
        ImGui::SetNextWindowPos(a.pos, ImGuiCond_Appearing, a.pivot);
    }
    if (!ImGui::BeginPopup(popupId)) return false;
    panelEdge();
    if (title) menuTitle(title, subtitle);
    return true;
}

}  // namespace ui3d
