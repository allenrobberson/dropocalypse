// Headless preview renderer for docs/media.
//
// This is a CPU port of the Mac app's frame pipeline, used only to bake the README
// preview. It is not shipped in the app and CI does not build it.
//
//   g++ -O3 -std=c++17 -fopenmp -o /tmp/render-readme scripts/render-readme-preview.cpp
//   # logo.raw is the app icon as little-endian int32 width, height, then RGBA
//   /tmp/render-readme logo.raw hero  /tmp/frames 960 540 16
//   /tmp/render-readme logo.raw still /tmp/frames "Full Kaleidoscope" out.ppm 0 1280 720
//
// The GIF in docs/media/hero.gif was encoded at 800px, 12 fps, 96 colors, then
// `gifsicle -O3 --lossy=50`. Audio is a synthetic 126 BPM kick, not a recording.
//
// Ported from:
//   Sources/Dropocalypse/Render/ShaderSource.swift   (fs_warp, kaleidoField, fs_present, logo)
//   Sources/Dropocalypse/Render/Scenes.swift         (built-in style parameters)
//   Sources/Dropocalypse/Render/WaveDrawer.swift     (classic waveform styles)
//   Sources/Dropocalypse/Render/StyleFX.swift        (Neon Equalizer, Full Kaleidoscope overlay)
//   Sources/Dropocalypse/Render/LogoMotion.swift     (logo springs)
//   Sources/Dropocalypse/Render/Renderer.swift       (per-frame warp scaling, composite order)
//   Sources/DropocalypseCore/FrameLayout.swift       (portrait 9:16 frame)
//
// Audio is a synthetic kick pattern (no recorded music). Output is PPM (P6).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

static constexpr float PI = 3.14159265f;
static constexpr float TAU = 6.2831853f;

static float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }
static float satf(float x) { return clampf(x, 0.f, 1.f); }
static float fractf(float x) { return x - std::floor(x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smoothstepf(float e0, float e1, float x) {
    float t = satf((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

struct V3 {
    float r, g, b;
    V3() : r(0), g(0), b(0) {}
    V3(float r, float g, float b) : r(r), g(g), b(b) {}
    V3 operator+(V3 o) const { return {r + o.r, g + o.g, b + o.b}; }
    V3 operator*(float s) const { return {r * s, g * s, b * s}; }
};
static V3 lerp3(V3 a, V3 b, float t) { return a * (1.f - t) + b * t; }
static V3 max3(V3 c, float m) { return {std::max(c.r, m), std::max(c.g, m), std::max(c.b, m)}; }

static V3 hsv(float h, float s, float v) {
    float hh = fractf(h) * 6.f;
    int i = (int)hh % 6;
    if (i < 0) i += 6;
    float f = hh - std::floor(hh);
    float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
    switch (i) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}

static V3 hueRotate(V3 c, float a) {
    const float k = 0.57735027f;
    float ca = std::cos(a), sa = std::sin(a);
    float cr = k * (c.b - c.g);
    float cg = k * (c.r - c.b);
    float cb = k * (c.g - c.r);
    float dot = k * (c.r + c.g + c.b);
    float u = (1.f - ca);
    return {c.r * ca + cr * sa + k * dot * u,
            c.g * ca + cg * sa + k * dot * u,
            c.b * ca + cb * sa + k * dot * u};
}

// --- Kaleidoscope field (ShaderSource.swift) ---------------------------------

static float hash21(float x, float y) {
    float p3x = fractf(x * 0.1031f);
    float p3y = fractf(y * 0.1031f);
    float p3z = fractf(x * 0.1031f);
    float d = p3x * (p3y + 33.33f) + p3y * (p3z + 33.33f) + p3z * (p3x + 33.33f);
    p3x += d; p3y += d; p3z += d;
    return fractf((p3x + p3y) * p3z);
}

static float vnoise(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.f - 2.f * fx);
    fy = fy * fy * (3.f - 2.f * fy);
    float a = hash21(ix, iy);
    float b = hash21(ix + 1.f, iy);
    float c = hash21(ix, iy + 1.f);
    float d = hash21(ix + 1.f, iy + 1.f);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

static float fbm(float x, float y) {
    float v = 0.f, a = 0.5f;
    v += a * vnoise(x, y); x = x * 2.02f + 1.7f; y = y * 2.02f + 9.2f; a *= 0.5f;
    v += a * vnoise(x, y); x = x * 2.02f + 1.7f; y = y * 2.02f + 9.2f; a *= 0.5f;
    v += a * vnoise(x, y); x = x * 2.02f + 1.7f; y = y * 2.02f + 9.2f; a *= 0.5f;
    v += a * vnoise(x, y);
    return v;
}

static V3 neonPalette(float t) {
    t = fractf(t) * 5.f;
    float i = std::floor(t);
    float f = smoothstepf(0.f, 1.f, fractf(t));
    V3 a, b;
    if (i < 1.f) { a = {0.f, 0.96f, 1.f}; b = {1.f, 0.04f, 0.58f}; }
    else if (i < 2.f) { a = {1.f, 0.04f, 0.58f}; b = {0.12f, 0.28f, 1.f}; }
    else if (i < 3.f) { a = {0.12f, 0.28f, 1.f}; b = {1.f, 0.68f, 0.05f}; }
    else if (i < 4.f) { a = {1.f, 0.68f, 0.05f}; b = {0.48f, 1.f, 0.10f}; }
    else { a = {0.48f, 1.f, 0.10f}; b = {0.f, 0.96f, 1.f}; }
    return lerp3(a, b, f);
}

static V3 kaleidoField(float u, float v, float time, float bass, float mid, float treble, float aspect) {
    float px = (u - 0.5f) * std::max(aspect, 0.05f);
    float py = (v - 0.5f);
    float spin = time * 0.07f + bass * 0.22f;
    float cs = std::cos(spin), sn = std::sin(spin);
    float rx = cs * px - sn * py;
    float ry = sn * px + cs * py;
    float inv = 1.f / (1.f + bass * 0.28f);
    px = rx * inv; py = ry * inv;

    float p4x = std::fabs(px), p4y = std::fabs(py);
    float p8x = std::min(p4x, p4y), p8y = std::max(p4x, p4y);
    float eight = satf(0.5f + 0.5f * std::sin(time * 0.19f) + mid * 0.45f);
    float fold = smoothstepf(0.2f, 0.85f, eight);
    px = lerpf(p4x, p8x, fold);
    py = lerpf(p4y, p8y, fold);

    float qx = px, qy = py;
    float wt = time * (0.32f + mid * 0.25f);
    float amp = 0.07f + treble * 0.09f;
    qx += amp * std::sin(py * (3.1f + mid * 4.f) + wt);
    qy += amp * std::cos(px * (2.6f + treble * 5.f) - wt * 0.85f);

    float detail = 2.1f + mid * 2.8f + treble * 4.2f;
    float n = fbm(qx * detail + time * 0.06f, qy * detail + time * 0.06f);
    float n2 = fbm(qy * (detail * 1.85f) - time * 0.09f + 3.7f,
                   qx * (detail * 1.85f) - time * 0.09f + 3.7f);

    float cells = 3.2f + mid * 4.5f + treble * 3.5f;
    float cellx = std::fabs(fractf(qx * cells) - 0.5f);
    float celly = std::fabs(fractf(qy * cells) - 0.5f);
    float tile = smoothstepf(0.48f, 0.16f, std::max(cellx, celly));
    float grid = smoothstepf(0.08f, 0.f, std::min(cellx, celly));
    float rad = std::sqrt(p4x * p4x + p4y * p4y);
    float rings = std::pow(std::fabs(std::sin(rad * (7.5f + treble * 10.f) - time * (0.55f + bass * 0.8f))), 10.f);

    float hueT = n * 0.62f + n2 * 0.28f + time * 0.02f + rad * 0.06f + mid * 0.12f;
    V3 col = neonPalette(hueT);
    V3 alt = neonPalette(hueT + 0.38f + treble * 0.1f);
    col = lerp3(col, alt, satf(tile * 0.9f + rings * 0.65f));
    col = col * (0.42f + n * 0.85f + n2 * 0.45f + tile * 0.55f);
    col = col + alt * (rings * 0.9f);
    col = col + neonPalette(0.02f + bass * 0.04f) * (grid * 0.35f);
    float axis = std::exp(-std::min(p4x, p4y) * (7.f - bass * 2.f));
    col = col + neonPalette(0.52f) * (axis * (0.22f + bass * 0.18f) * (1.f - eight * 0.65f));

    float cx = (u - 0.5f) * std::max(aspect, 0.05f);
    float cy = (v - 0.5f);
    float edgeLen = std::sqrt(cx * cx + cy * cy);
    float corner = std::sqrt(std::max(aspect, 0.05f) * std::max(aspect, 0.05f) * 0.25f + 0.25f);
    float edge = edgeLen / std::max(corner, 0.001f);
    col = col * lerpf(1.2f, 0.22f, smoothstepf(0.22f, 1.f, edge));
    return col * (0.9f + bass * 0.45f);
}

// --- Scenes -------------------------------------------------------------------

enum Wave : int {
    WCircle = 0, WLine, WVertical, WBars, WRadial, WSpiral, WLissajous, WTwin,
    WNeon = 12, WFull = 25
};

struct Scene {
    const char* name;
    int wave;
    float zoom, zoomBass, rotation, rotationBass;
    float warpAmount, warpScale, warpSpeed, decay;
    float driftX, driftY, kaleido, hueShift, plasma, generative;
    int mirror;
    float centerRadius, waveScale, thickness, baseHue, hueSpeed, saturation, vignette;
};

static Scene baseScene(const char* name, int wave) {
    Scene s{};
    s.name = name;
    s.wave = wave;
    s.zoom = 1.01f; s.zoomBass = 0.03f; s.rotation = 0; s.rotationBass = 0;
    s.warpAmount = 0.004f; s.warpScale = 6.f; s.warpSpeed = 0.8f; s.decay = 0.955f;
    s.driftX = 0; s.driftY = 0; s.kaleido = 0; s.hueShift = 0.004f; s.plasma = 0; s.generative = 0;
    s.mirror = 0;
    s.centerRadius = 0.32f; s.waveScale = 1.f; s.thickness = 1.f;
    s.baseHue = 0; s.hueSpeed = 0.03f; s.saturation = 0.9f; s.vignette = 0.35f;
    return s;
}

static Scene sceneByName(const char* name) {
    if (!std::strcmp(name, "Classic Tunnel")) {
        Scene s = baseScene(name, WCircle);
        s.zoom = 1.025f; s.zoomBass = 0.05f; s.rotation = 0.004f; s.rotationBass = 0.01f;
        s.warpAmount = 0.003f; s.decay = 0.95f; s.hueShift = 0.006f;
        s.centerRadius = 0.03f; s.thickness = 1.2f; s.baseHue = 0.55f;
        return s;
    }
    if (!std::strcmp(name, "Spectrum Skyline")) {
        Scene s = baseScene(name, WBars);
        s.zoom = 1.f; s.zoomBass = 0.006f; s.warpAmount = 0.0015f; s.decay = 0.9f;
        s.driftY = -0.006f; s.hueShift = 0.002f;
        s.baseHue = 0.75f; s.hueSpeed = 0.05f; s.vignette = 0.2f;
        return s;
    }
    if (!std::strcmp(name, "Starburst")) {
        Scene s = baseScene(name, WRadial);
        s.zoom = 1.045f; s.zoomBass = 0.06f; s.rotation = -0.002f; s.rotationBass = -0.012f;
        s.warpAmount = 0.002f; s.decay = 0.93f; s.hueShift = 0.01f;
        s.centerRadius = 0; s.waveScale = 1.1f; s.baseHue = 0.08f;
        return s;
    }
    if (!std::strcmp(name, "Oscilloscope Drift")) {
        Scene s = baseScene(name, WLine);
        s.zoom = 1.006f; s.zoomBass = 0.02f; s.rotation = 0.0015f;
        s.warpAmount = 0.006f; s.warpScale = 4.f; s.warpSpeed = 0.6f; s.decay = 0.965f;
        s.driftY = -0.002f; s.hueShift = 0.003f;
        s.thickness = 1.4f; s.baseHue = 0.33f;
        return s;
    }
    if (!std::strcmp(name, "Kaleido Bloom")) {
        Scene s = baseScene(name, WCircle);
        s.zoom = 1.012f; s.zoomBass = 0.035f; s.rotation = 0.006f; s.rotationBass = 0.015f;
        s.warpAmount = 0.003f; s.decay = 0.95f; s.kaleido = 6.f; s.hueShift = 0.008f;
        s.centerRadius = 0.03f; s.waveScale = 0.8f; s.thickness = 1.1f; s.baseHue = 0.9f; s.hueSpeed = 0.06f;
        return s;
    }
    if (!std::strcmp(name, "Liquid Plasma")) {
        Scene s = baseScene(name, WTwin);
        s.zoom = 1.004f; s.zoomBass = 0.025f; s.rotation = 0.001f;
        s.warpAmount = 0.012f; s.warpScale = 5.f; s.warpSpeed = 1.3f; s.decay = 0.975f;
        s.hueShift = 0.005f; s.plasma = 1.f;
        s.centerRadius = 0.03f; s.waveScale = 0.7f; s.thickness = 0.8f; s.baseHue = 0.6f; s.saturation = 0.75f;
        return s;
    }
    if (!std::strcmp(name, "Vortex")) {
        Scene s = baseScene(name, WSpiral);
        s.zoom = 1.03f; s.zoomBass = 0.04f; s.rotation = 0.025f; s.rotationBass = 0.04f;
        s.warpAmount = 0.002f; s.decay = 0.94f; s.hueShift = 0.012f;
        s.centerRadius = 0; s.thickness = 1.2f; s.baseHue = 0.15f; s.hueSpeed = 0.08f;
        return s;
    }
    if (!std::strcmp(name, "Lissajous Neon")) {
        Scene s = baseScene(name, WLissajous);
        s.zoom = 1.008f; s.zoomBass = 0.02f; s.rotation = -0.004f;
        s.warpAmount = 0.004f; s.decay = 0.96f; s.mirror = 1; s.hueShift = 0.004f;
        s.waveScale = 1.2f; s.thickness = 1.1f; s.baseHue = 0.45f; s.saturation = 1.f;
        return s;
    }
    if (!std::strcmp(name, "Mirror Hall")) {
        Scene s = baseScene(name, WVertical);
        s.zoom = 1.015f; s.zoomBass = 0.03f; s.rotation = 0.003f;
        s.warpAmount = 0.005f; s.warpScale = 8.f; s.decay = 0.955f; s.mirror = 2; s.hueShift = 0.007f;
        s.waveScale = 0.9f; s.thickness = 1.2f; s.baseHue = 0.8f;
        return s;
    }
    if (!std::strcmp(name, "Black Hole")) {
        Scene s = baseScene(name, WTwin);
        s.zoom = 0.975f; s.zoomBass = -0.03f; s.rotation = -0.012f; s.rotationBass = -0.02f;
        s.warpAmount = 0.003f; s.decay = 0.965f; s.hueShift = -0.006f;
        s.centerRadius = 0.2f; s.waveScale = 1.25f; s.baseHue = 0.02f; s.hueSpeed = 0.02f; s.vignette = 0.5f;
        return s;
    }
    if (!std::strcmp(name, "Neon Equalizer")) {
        Scene s = baseScene(name, WNeon);
        s.zoom = 1.f; s.zoomBass = 0; s.warpAmount = 0; s.decay = 0.55f; s.hueShift = 0;
        s.baseHue = 0.8f; s.hueSpeed = 0.02f; s.vignette = 0.3f;
        return s;
    }
    if (!std::strcmp(name, "Full Kaleidoscope")) {
        Scene s = baseScene(name, WFull);
        s.zoom = 1.008f; s.zoomBass = 0.048f; s.rotation = 0.0012f; s.rotationBass = 0.016f;
        s.warpAmount = 0.004f; s.warpScale = 3.4f; s.warpSpeed = 0.55f; s.decay = 0.91f;
        s.mirror = 2; s.hueShift = 0.0015f; s.generative = 1.f;
        s.baseHue = 0.52f; s.hueSpeed = 0.015f; s.saturation = 1.f; s.vignette = 0.48f;
        return s;
    }
    std::fprintf(stderr, "unknown scene %s\n", name);
    return sceneByName("Classic Tunnel");
}

static Scene mixScene(Scene a, Scene b, float klin) {
    float k = klin * klin * (3.f - 2.f * klin);
    auto m = [&](float x, float y) { return lerpf(x, y, k); };
    Scene s = k < 0.5f ? a : b;
    s.zoom = m(a.zoom, b.zoom);
    s.zoomBass = m(a.zoomBass, b.zoomBass);
    s.rotation = m(a.rotation, b.rotation);
    s.rotationBass = m(a.rotationBass, b.rotationBass);
    s.warpAmount = m(a.warpAmount, b.warpAmount);
    s.warpScale = m(a.warpScale, b.warpScale);
    s.warpSpeed = m(a.warpSpeed, b.warpSpeed);
    s.decay = m(a.decay, b.decay);
    s.driftX = m(a.driftX, b.driftX);
    s.driftY = m(a.driftY, b.driftY);
    s.hueShift = m(a.hueShift, b.hueShift);
    s.plasma = m(a.plasma, b.plasma);
    s.generative = m(a.generative, b.generative);
    s.vignette = m(a.vignette, b.vignette);
    s.saturation = m(a.saturation, b.saturation);
    s.centerRadius = m(a.centerRadius, b.centerRadius);
    return s;
}

// --- Audio (synthetic, drives the same channels the app reads) ---------------

struct Audio {
    float bass, mid, treble, beatStrength;
    int beatCount;
    float wave[512];
    float bands[48];
};

struct AudioSynth {
    double prevPhase = 0;
    int beatCount = 0;
    float bands[48] = {};
    Audio step(double t) {
        const double bpm = 126.0;
        const double beat = 60.0 / bpm;
        double phase = std::fmod(t, beat) / beat;
        if (phase < prevPhase) beatCount++;
        prevPhase = phase;
        float kick = std::exp((float)(-phase * 9.5));
        double hatPhase = std::fmod(t + beat * 0.5, beat) / beat;
        float hat = std::exp((float)(-hatPhase * 28.0)) * 0.85f;

        Audio a{};
        a.bass = std::min(1.4f, kick * 1.3f + 0.06f);
        a.mid = std::min(1.15f, 0.22f + 0.35f * (float)std::pow(std::sin(t * 2.4), 2.0) + kick * 0.45f);
        a.treble = std::min(1.15f, 0.12f + hat * 0.9f);
        a.beatCount = beatCount;
        a.beatStrength = kick > 0.72f ? 1.15f : 0.f;

        for (int i = 0; i < 512; i++) {
            // A short loop of harmonics, the same shape the app draws from a tonal waveform.
            // Low spatial frequency keeps Classic Tunnel a lobed ring instead of a filled scribble.
            float u = i / 511.f;
            float w = 0.62f * std::sin(float(t) * 5.2f + u * TAU * 2.f)
                    + 0.26f * std::sin(float(t) * 2.1f + u * TAU * 3.f)
                    + 0.16f * std::sin(float(t) * 9.f - u * TAU) * (0.35f + a.treble);
            w += kick * 0.22f * std::sin(u * TAU * 4.f);
            a.wave[i] = clampf(w / 0.85f, -1.f, 1.f);
        }
        for (int b = 0; b < 48; b++) {
            float f = b / 47.f;
            float low = std::exp(-f * 3.1f) * (0.25f + 0.75f * kick);
            float midp = std::exp(-std::pow((f - 0.42f) / 0.16f, 2.f)) * a.mid;
            float hi = std::exp(-std::pow((f - 0.82f) / 0.14f, 2.f)) * a.treble;
            float wob = 0.72f + 0.28f * std::sin(float(t) * 3.f + b * 0.55f);
            float target = satf((low + midp + hi) * wob);
            bands[b] = target > bands[b] ? target : bands[b] * 0.82f + target * 0.18f;
            a.bands[b] = bands[b];
        }
        return a;
    }
};

// --- Logo motion (LogoMotion.swift, default slider amounts) ------------------

struct Spring { float x = 0, v = 0, k, c; };
struct Pose { float x = 0, y = 0, scaleX = 1, scaleY = 1, rotation = 0; };

struct LogoMotion {
    Spring squash{0, 0, 320.f, 15.f};
    Spring slide{0, 0, 38.f, 6.5f};
    Spring wobble{0, 0, 130.f, 9.f};
    float bounceY = 0, bounceV = 0, side = 1, time = 0;
    int lastBeat = 0;
    // Inside the app's slider range (0–1.5). Turned up from the defaults so a short
    // silent preview still reads as a bounce, squash and slide. Defaults are 0.70 / 0.60 / 0.50 / 0.40 / 0.35.
    float pulseA = 0.90f, squashA = 1.00f, bounceA = 1.15f, slideA = 0.75f, wobbleA = 0.85f;

    static void stepSpring(Spring& s, float h) {
        float a = -s.k * s.x - s.c * s.v;
        s.v += a * h;
        s.x += s.v * h;
    }

    Pose update(float rawDt, const Audio& audio) {
        float dt = clampf(rawDt, 0.f, 1.f / 20.f);
        time += dt;
        float bass = std::min(audio.bass, 1.4f);
        if (audio.beatCount != lastBeat) {
            bool fresh = lastBeat != 0;
            lastBeat = audio.beatCount;
            if (fresh) {
                float s = std::max(audio.beatStrength, 0.85f);
                side = -side;
                squash.x += squashA * 0.22f * s;
                squash.v += squashA * 3.5f * s;
                if (bounceY < 0.05f) {
                    bounceY += bounceA * 0.05f * s;
                    bounceV += bounceA * 1.7f * s;
                }
                slide.x += slideA * 0.09f * s * side;
                slide.v += slideA * 1.1f * s * side;
                wobble.x += wobbleA * 0.22f * s * side;
                wobble.v += wobbleA * 2.2f * s * side;
            }
        }
        const int steps = 4;
        float h = dt / steps;
        for (int i = 0; i < steps; i++) {
            stepSpring(squash, h);
            stepSpring(slide, h);
            stepSpring(wobble, h);
            bounceV -= 7.5f * h;
            bounceY += bounceV * h;
            if (bounceY < 0) {
                float impact = -bounceV;
                bounceY = 0;
                bounceV = impact > 0.35f ? impact * 0.3f : 0;
                if (impact > 0.2f) squash.v += squashA * std::min(impact, 2.f) * 1.5f;
            }
        }
        Pose pose;
        float pulse = 1.f + pulseA * 0.32f * bass;
        float stretch = squashA * 0.12f * bass;
        float sq = clampf(squash.x, -0.45f, 0.45f);
        pose.scaleX = pulse * (1.f + sq) * (1.f - stretch * 0.5f);
        pose.scaleY = pulse * (1.f - sq * 0.85f) * (1.f + stretch);
        pose.y = bounceY * 0.55f;
        pose.x = slide.x * 0.5f + std::sin(time * 0.45f) * 0.04f * slideA;
        pose.rotation = wobble.x * 0.35f + std::sin(time * 0.3f) * 0.02f * wobbleA;
        return pose;
    }
};

// --- Framebuffer + warp -------------------------------------------------------

struct Surf {
    int W = 0, H = 0;
    std::vector<float> rgb;
    void alloc(int w, int h) {
        W = w; H = h;
        rgb.assign((size_t)w * h * 3, 0.f);
    }
    void clear() { std::fill(rgb.begin(), rgb.end(), 0.f); }
};

struct Logo {
    int W = 0, H = 0;
    std::vector<float> rgba;      // straight alpha
    std::vector<float> glowA;     // blurred alpha
    float aspect = 1;
};

static V3 sampleRGBA(const Logo& logo, float u, float v, float& alpha) {
    int W = logo.W, H = logo.H;
    float x = u * W - 0.5f, y = v * H - 0.5f;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    auto at = [&](int x, int y, int c) -> float {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        return logo.rgba[((size_t)(y * W + x) * 4) + c];
    };
    float r = lerpf(lerpf(at(x0, y0, 0), at(x0 + 1, y0, 0), fx), lerpf(at(x0, y0 + 1, 0), at(x0 + 1, y0 + 1, 0), fx), fy);
    float g = lerpf(lerpf(at(x0, y0, 1), at(x0 + 1, y0, 1), fx), lerpf(at(x0, y0 + 1, 1), at(x0 + 1, y0 + 1, 1), fx), fy);
    float b = lerpf(lerpf(at(x0, y0, 2), at(x0 + 1, y0, 2), fx), lerpf(at(x0, y0 + 1, 2), at(x0 + 1, y0 + 1, 2), fx), fy);
    alpha = lerpf(lerpf(at(x0, y0, 3), at(x0 + 1, y0, 3), fx), lerpf(at(x0, y0 + 1, 3), at(x0 + 1, y0 + 1, 3), fx), fy);
    return {r, g, b};
}

static float sampleGlow(const Logo& logo, float u, float v) {
    int W = logo.W, H = logo.H;
    float x = u * W - 0.5f, y = v * H - 0.5f;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        return logo.glowA[(size_t)(y * W + x)];
    };
    return lerpf(lerpf(at(x0, y0), at(x0 + 1, y0), fx), lerpf(at(x0, y0 + 1), at(x0 + 1, y0 + 1), fx), fy);
}

static V3 sampleTex(const float* buf, int W, int H, float u, float v) {
    float x = u * W - 0.5f;
    float y = v * H - 0.5f;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    auto at = [&](int x, int y) -> V3 {
        if (x < 0 || y < 0 || x >= W || y >= H) return {};
        const float* p = buf + ((size_t)y * W + x) * 3;
        return {p[0], p[1], p[2]};
    };
    V3 a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return lerp3(lerp3(a, b, fx), lerp3(c, d, fx), fy);
}

struct WarpU {
    float aspect, time, zoom, rotation, warpAmount, warpScale, warpSpeed, decay;
    float driftX, driftY, kaleido, hueShift, plasma, bass, mid, treble, texelX, texelY, generative;
    int mirror;
};

static V3 warpPixel(const float* src, int W, int H, float u, float v, const WarpU& wp) {
    float px = (u - 0.5f) * wp.aspect;
    float py = v - 0.5f;
    if (wp.mirror == 1) px = std::fabs(px);
    else if (wp.mirror == 2) { px = std::fabs(px); py = std::fabs(py); }
    if (wp.kaleido > 0.5f) {
        float r = std::sqrt(px * px + py * py);
        float a = std::atan2(py, px);
        float seg = TAU / wp.kaleido;
        a = std::fmod(a + 25.1327412f, seg);
        if (a < 0) a += seg;
        a = std::fabs(a - seg * 0.5f);
        px = r * std::cos(a);
        py = r * std::sin(a);
    }
    float ca = std::cos(wp.rotation), sa = std::sin(wp.rotation);
    float rx = ca * px - sa * py;
    float ry = sa * px + ca * py;
    float z = wp.zoom != 0 ? wp.zoom : 1.f;
    px = rx / z; py = ry / z;
    float t = wp.time * wp.warpSpeed;
    px += wp.warpAmount * std::sin(py * wp.warpScale + t);
    py += wp.warpAmount * std::cos(px * wp.warpScale * 1.13f - t * 0.87f);
    px -= wp.driftX; py -= wp.driftY;
    float su = px / wp.aspect + 0.5f;
    float sv = py + 0.5f;
    V3 c = sampleTex(src, W, H, su, sv) * 0.6f
         + (sampleTex(src, W, H, su + wp.texelX, sv)
          + sampleTex(src, W, H, su - wp.texelX, sv)
          + sampleTex(src, W, H, su, sv + wp.texelY)
          + sampleTex(src, W, H, su, sv - wp.texelY)) * 0.1f;
    c = hueRotate(c, wp.hueShift) * wp.decay;
    if (wp.plasma > 0.f) {
        float qx = (u - 0.5f) * wp.aspect * 3.f;
        float qy = (v - 0.5f) * 3.f;
        float val = std::sin(qx + wp.time) + std::sin(qy * 1.3f - wp.time * 0.7f)
                  + std::sin((qx + qy) * 0.8f + wp.time * 1.1f)
                  + std::sin(std::sqrt(qx * qx + qy * qy) * 2.f - wp.time * 1.5f);
        float pr = 0.5f + 0.5f * std::cos(0.f + val * 1.2f + wp.time * 0.3f);
        float pg = 0.5f + 0.5f * std::cos(2.1f + val * 1.2f + wp.time * 0.3f);
        float pb = 0.5f + 0.5f * std::cos(4.2f + val * 1.2f + wp.time * 0.3f);
        float k = wp.plasma * (0.15f + wp.bass) * 0.04f;
        c.r += pr * k; c.g += pg * k; c.b += pb * k;
    }
    if (wp.generative > 0.001f) {
        V3 gen = kaleidoField(u, v, wp.time, wp.bass, wp.mid, wp.treble, wp.aspect);
        V3 base = max3(c, 0.f) * 0.42f + gen;
        c = lerp3(c, base, satf(wp.generative));
    }
    return max3(c, 0.f);
}

// --- Geometry -----------------------------------------------------------------

struct Vtx { float x, y, r, g, b, a; };

struct Draw {
    Surf* dst = nullptr;
    float aspect = 1;
    void tri(Vtx a, Vtx b, Vtx c, bool over) {
        int W = dst->W, H = dst->H;
        auto pxOf = [&](Vtx v, float& x, float& y) {
            x = (v.x + 1.f) * 0.5f * W;
            y = (1.f - v.y) * 0.5f * H;
        };
        float ax, ay, bx, by, cx, cy;
        pxOf(a, ax, ay); pxOf(b, bx, by); pxOf(c, cx, cy);
        auto edge = [](float x0, float y0, float x1, float y1, float px, float py) {
            return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
        };
        float area = edge(ax, ay, bx, by, cx, cy);
        if (area < 0) { std::swap(b, c); std::swap(bx, cx); std::swap(by, cy); area = -area; }
        if (area < 1e-4f) return;
        float inv = 1.f / area;
        int minx = std::max(0, (int)std::floor(std::min(ax, std::min(bx, cx))));
        int maxx = std::min(W - 1, (int)std::ceil(std::max(ax, std::max(bx, cx))));
        int miny = std::max(0, (int)std::floor(std::min(ay, std::min(by, cy))));
        int maxy = std::min(H - 1, (int)std::ceil(std::max(ay, std::max(by, cy))));
        float* buf = dst->rgb.data();
        for (int y = miny; y <= maxy; y++) {
            for (int x = minx; x <= maxx; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = edge(bx, by, cx, cy, px, py) * inv;
                float w1 = edge(cx, cy, ax, ay, px, py) * inv;
                float w2 = edge(ax, ay, bx, by, px, py) * inv;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float r = a.r * w0 + b.r * w1 + c.r * w2;
                float g = a.g * w0 + b.g * w1 + c.g * w2;
                float bl = a.b * w0 + b.b * w1 + c.b * w2;
                float al = a.a * w0 + b.a * w1 + c.a * w2;
                float* p = buf + ((size_t)y * W + x) * 3;
                if (over) {
                    float ia = 1.f - al;
                    p[0] = r * al + p[0] * ia;
                    p[1] = g * al + p[1] * ia;
                    p[2] = bl * al + p[2] * ia;
                } else {
                    p[0] += r * al; p[1] += g * al; p[2] += bl * al;
                }
            }
        }
    }
    Vtx xy(float hx, float hy, float r, float g, float b, float a) const {
        return {hx / aspect, hy, r, g, b, a};
    }
    void line(float x0, float y0, float x1, float y1, float width, float r, float g, float b, float a) {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) return;
        float minW = 1.05f / (dst->H * 0.5f);
        float w = std::max(width, minW);
        float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        Vtx A = xy(x0 + nx, y0 + ny, r, g, b, a);
        Vtx B = xy(x1 + nx, y1 + ny, r, g, b, a);
        Vtx C = xy(x1 - nx, y1 - ny, r, g, b, a);
        Vtx D = xy(x0 - nx, y0 - ny, r, g, b, a);
        tri(A, B, C, false);
        tri(A, C, D, false);
    }
    void rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
        Vtx A = xy(x0, y0, r, g, b, a);
        Vtx B = xy(x1, y0, r, g, b, a);
        Vtx C = xy(x1, y1, r, g, b, a);
        Vtx D = xy(x0, y1, r, g, b, a);
        tri(A, B, C, false);
        tri(A, C, D, false);
    }
    void quadGrad(float x0, float y0, float r0, float g0, float b0, float a0,
                  float x1, float y1, float r1, float g1, float b1, float a1,
                  float x2, float y2, float r2, float g2, float b2, float a2,
                  float x3, float y3, float r3, float g3, float b3, float a3) {
        tri(xy(x0, y0, r0, g0, b0, a0), xy(x1, y1, r1, g1, b1, a1), xy(x2, y2, r2, g2, b2, a2), false);
        tri(xy(x0, y0, r0, g0, b0, a0), xy(x2, y2, r2, g2, b2, a2), xy(x3, y3, r3, g3, b3, a3), false);
    }
    void dot(float cx, float cy, float radius, float r, float g, float b, float a, int seg = 8) {
        Vtx center = xy(cx, cy, r, g, b, a);
        float px = cx + radius, py = cy;
        for (int i = 1; i <= seg; i++) {
            float ang = (float)i / seg * TAU;
            float qx = cx + std::cos(ang) * radius, qy = cy + std::sin(ang) * radius;
            tri(center, xy(px, py, r, g, b, a), xy(qx, qy, r, g, b, a), false);
            px = qx; py = qy;
        }
    }
    void softDot(float cx, float cy, float radius, float r, float g, float b, float a, int seg = 8) {
        Vtx center = xy(cx, cy, r, g, b, a);
        float px = cx + radius, py = cy;
        for (int i = 1; i <= seg; i++) {
            float ang = (float)i / seg * TAU;
            float qx = cx + std::cos(ang) * radius, qy = cy + std::sin(ang) * radius;
            tri(center, xy(px, py, r, g, b, 0), xy(qx, qy, r, g, b, 0), false);
            px = qx; py = qy;
        }
    }
};

static void drawWaves(Draw& d, const Scene& scene, const Audio& audio, float t) {
    float bass = std::min(audio.bass, 1.4f);
    float width = 0.0065f * scene.thickness * (1.f + bass * 0.6f);
    float alpha = 0.55f + 0.45f * std::min(1.f, std::max(bass, audio.mid));
    float hue0 = scene.baseHue + t * scene.hueSpeed;
    auto color = [&](float frac, float boost, float& r, float& g, float& b, float& a) {
        V3 c = hsv(hue0 + frac * 0.35f, scene.saturation, 1.f);
        r = c.r; g = c.g; b = c.b; a = std::min(1.f, alpha * boost);
    };
    float s = scene.waveScale;
    float aspect = d.aspect;
    const float* wave = audio.wave;
    const float* bands = audio.bands;
    const int bandN = 48;

    auto ring = [&](int stride, int offset, float radius, float amplitude, float rotation, float hueAdd) {
        int n = (512 - offset) / stride;
        if (n < 17) return;
        const int blend = 24;
        std::vector<float> xs(n), ys(n);
        float first = wave[offset];
        for (int i = 0; i < n; i++) {
            float w = wave[offset + i * stride];
            if (i >= n - blend) {
                float k = (float)(i - (n - blend)) / blend;
                w += (first - w) * k;
            }
            float ang = (float)i / n * TAU + rotation;
            float rad = std::max(0.f, radius + w * amplitude);
            xs[i] = std::cos(ang) * rad;
            ys[i] = std::sin(ang) * rad;
        }
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float r, g, b, a;
            color((float)i / n + hueAdd, 1.f, r, g, b, a);
            d.line(xs[i], ys[i], xs[j], ys[j], width, r, g, b, a);
        }
    };

    switch (scene.wave) {
    case WCircle:
        ring(2, 0, scene.centerRadius * s * (1.f + 0.25f * bass), 0.12f * s, t * 0.2f, 0);
        break;
    case WTwin:
        ring(2, 0, scene.centerRadius * s * (1.f + 0.3f * bass), 0.07f * s, t * 0.5f, 0);
        ring(2, 1, std::max(0.42f * s, scene.centerRadius * s + 0.22f) * (1.f + 0.15f * bass),
             0.1f * s, -t * 0.4f, 0.5f);
        break;
    case WLine: {
        const int n = 256;
        float px = 0, py = 0;
        for (int i = 0; i < n; i++) {
            float x = -0.94f * aspect + (float)i / (n - 1) * 1.88f * aspect;
            float y = wave[i * 2] * 0.3f * s;
            if (i) {
                float r, g, b, a; color((float)i / n, 1.f, r, g, b, a);
                d.line(px, py, x, y, width, r, g, b, a);
            }
            px = x; py = y;
        }
        break;
    }
    case WVertical:
        for (float side : {-1.f, 1.f}) {
            const int n = 256;
            float px = 0, py = 0;
            for (int i = 0; i < n; i++) {
                float y = -0.94f + (float)i / (n - 1) * 1.88f;
                float x = side * 0.35f * aspect * 0.5f + wave[i * 2] * 0.22f * s;
                if (i) {
                    float r, g, b, a; color((float)i / n + (side > 0 ? 0.5f : 0.f), 1.f, r, g, b, a);
                    d.line(px, py, x, y, width, r, g, b, a);
                }
                px = x; py = y;
            }
        }
        break;
    case WBars: {
        float total = 1.9f * aspect;
        float slot = total / bandN;
        for (int i = 0; i < bandN; i++) {
            float x0 = -total / 2 + i * slot + slot * 0.15f;
            float h = 0.02f + bands[i] * 1.15f * s;
            float r, g, b, a; color((float)i / bandN, 0.9f, r, g, b, a);
            d.rect(x0, -0.98f, x0 + slot * 0.7f, -0.98f + h, r, g, b, a);
        }
        break;
    }
    case WRadial: {
        float inner = scene.centerRadius > 0 ? scene.centerRadius + 0.06f * bass : 0;
        for (int side = 0; side < 2; side++) {
            for (int i = 0; i < bandN; i++) {
                float frac = (float)i / bandN;
                float ang = (side == 0 ? frac : -frac) * PI + PI / 2 + t * 0.1f;
                float dx = std::cos(ang), dy = std::sin(ang);
                float len = 0.03f + bands[i] * 0.65f * s;
                float r, g, b, a; color(frac, 1.f, r, g, b, a);
                d.line(dx * inner, dy * inner, dx * (inner + len), dy * (inner + len), width * 1.2f, r, g, b, a);
            }
        }
        break;
    }
    case WSpiral:
        for (int arm = 0; arm < 3; arm++) {
            for (int i = 0; i < bandN; i++) {
                float frac = (float)i / bandN;
                float ang = i * 0.33f + arm * TAU / 3.f + t * 1.2f;
                float rad = scene.centerRadius + frac * 0.85f * s;
                float radius = 0.006f + bands[i] * 0.032f * scene.thickness;
                float r, g, b, a; color(frac + arm * 0.2f, 1.f, r, g, b, a);
                d.dot(std::cos(ang) * rad, std::sin(ang) * rad, radius, r, g, b, a);
            }
        }
        break;
    case WLissajous: {
        const int delay = 23;
        const int n = 512 - delay;
        float px = 0, py = 0; bool has = false; int drawn = 0;
        for (int i = 0; i < n; i += 2) {
            float x = wave[i] * 0.5f * s, y = wave[i + delay] * 0.5f * s;
            if (has) {
                float r, g, b, a; color((float)drawn / (n / 2), 1.f, r, g, b, a);
                d.line(px, py, x, y, width * 0.8f, r, g, b, a);
                drawn++;
            }
            px = x; py = y; has = true;
        }
        break;
    }
    case WNeon: {
        float total = 1.84f * aspect;
        float slot = total / bandN;
        float left = -total / 2;
        float base = -0.42f;
        float r, g, b, a; color(0.5f, 0.5f, r, g, b, a);
        d.rect(left, base - 0.002f, left + total, base + 0.002f, r, g, b, a);
        for (int i = 0; i < bandN; i++) {
            float prev = bands[std::max(0, i - 1)], next = bands[std::min(bandN - 1, i + 1)];
            float v = (prev + 2 * bands[i] + next) / 4.f;
            float h = 0.012f + std::pow(v, 1.15f) * 0.62f * s;
            float x0 = left + i * slot + slot * 0.18f;
            float x1 = x0 + slot * 0.64f;
            color((float)i / bandN, 1.f, r, g, b, a);
            d.rect(x0, base + 0.01f, x1, base + 0.01f + h, r, g, b, a);
            float ry = base - 0.01f, rh = h * 0.45f;
            d.quadGrad(x0, ry, r, g, b, 0.3f, x1, ry, r, g, b, 0.3f,
                       x1, ry - rh, r, g, b, 0, x0, ry - rh, r, g, b, 0);
        }
        break;
    }
    case WFull: {
        float pulse = 1.f + bass * 0.22f;
        float reach = std::min(0.92f, aspect * 0.92f);
        float hues[5] = {0.50f, 0.92f, 0.62f, 0.11f, 0.30f};
        auto neon = [&](int i, float alpha, float& r, float& g, float& b, float& a) {
            V3 c = hsv(hues[i % 5] + audio.mid * 0.04f, 1.f, 1.f);
            r = c.r; g = c.g; b = c.b; a = alpha;
        };
        const int segments = 72;
        for (int i = 0; i < 3; i++) {
            float radius = std::min(reach, (0.16f + i * 0.22f) * pulse);
            float r, g, b, a; neon(i, 0.28f + bass * 0.22f, r, g, b, a);
            float w = 0.0035f + bass * 0.003f;
            float px = radius, py = 0;
            for (int k = 1; k <= segments; k++) {
                float ang = (float)k / segments * TAU + t * 0.04f * (i + 1);
                float qx = std::cos(ang) * radius, qy = std::sin(ang) * radius;
                d.line(px, py, qx, qy, w, r, g, b, a);
                px = qx; py = qy;
            }
        }
        for (int arm = 0; arm < 8; arm++) {
            float ang = (float)arm / 8.f * TAU;
            float dx = std::cos(ang), dy = std::sin(ang);
            float band = bands[(arm * bandN / 8) % bandN];
            float inner = std::min(reach * 0.18f, 0.08f * pulse);
            float outer = std::min(reach, inner + 0.1f + band * (0.55f + audio.treble * 0.4f));
            float r, g, b, a;
            neon(arm, 0.4f + band * 0.45f, r, g, b, a);
            d.line(dx * inner, dy * inner, dx * outer, dy * outer, 0.005f + band * 0.008f, r, g, b, a);
            neon(arm + 1, 0.35f + band * 0.4f, r, g, b, a);
            d.softDot(dx * outer, dy * outer, 0.012f + band * 0.02f, r, g, b, a);
        }
        break;
    }
    default:
        break;
    }
}

// --- Engine -------------------------------------------------------------------

struct Engine {
    int canvasW, canvasH;
    bool portrait = false;
    Surf fb[2];
    int fi = 0;
    Surf presented; // float rgb of the content, then copied to canvas
    std::vector<uint8_t> canvas; // RGB8 of the full view
    Logo logo;
    LogoMotion motion;
    AudioSynth synth;
    Scene current{}, previous{};
    float blend = 1;
    double time = 0;
    float flash = 0;
    int contentW = 0, contentH = 0;
    float beatFlash = 0.40f;
    float brightness = 1.f;
    float glow = 0.60f;
    float logoSize = 0.32f;
    float logoTint = 0.f;

    Engine(int w, int h) : canvasW(w), canvasH(h) {
        canvas.assign((size_t)w * h * 3, 0);
        setPortrait(false);
    }

    void setPortrait(bool p) {
        portrait = p;
        if (!p) { contentW = canvasW; contentH = canvasH; }
        else {
            contentH = canvasH;
            contentW = std::max(16, (int)std::lround(canvasH * (9.0 / 16.0)));
        }
        fb[0].alloc(contentW, contentH);
        fb[1].alloc(contentW, contentH);
        presented.alloc(contentW, contentH);
        fi = 0;
    }

    bool loadLogo(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) { std::perror(path); return false; }
        int w = 0, h = 0;
        if (std::fread(&w, 4, 1, f) != 1 || std::fread(&h, 4, 1, f) != 1 || w < 2 || h < 2) {
            std::fclose(f); return false;
        }
        std::vector<uint8_t> raw((size_t)w * h * 4);
        if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) { std::fclose(f); return false; }
        std::fclose(f);
        logo.W = w; logo.H = h;
        logo.aspect = (float)w / (float)h;
        logo.rgba.resize(raw.size());
        logo.glowA.assign((size_t)w * h, 0);
        for (size_t i = 0; i < raw.size(); i++) logo.rgba[i] = raw[i] / 255.f;
        // Separable blur of alpha. Sigma is large so the halo matches the app's mip bias.
        std::vector<float> a((size_t)w * h), tmp((size_t)w * h);
        for (int i = 0; i < w * h; i++) a[i] = logo.rgba[i * 4 + 3];
        auto blur1 = [&](const std::vector<float>& in, std::vector<float>& out, bool horizontal) {
            const int radius = 28;
            const float sigma = 12.f;
            std::vector<float> k(radius * 2 + 1);
            float sum = 0;
            for (int i = -radius; i <= radius; i++) {
                k[i + radius] = std::exp(-(i * i) / (2 * sigma * sigma));
                sum += k[i + radius];
            }
            for (float& v : k) v /= sum;
            int dimA = horizontal ? h : w;
            int dimB = horizontal ? w : h;
            for (int aIdx = 0; aIdx < dimA; aIdx++) {
                for (int b = 0; b < dimB; b++) {
                    float acc = 0;
                    for (int t = -radius; t <= radius; t++) {
                        int s = std::clamp(b + t, 0, dimB - 1);
                        int x = horizontal ? s : aIdx;
                        int y = horizontal ? aIdx : s;
                        acc += in[(size_t)y * w + x] * k[t + radius];
                    }
                    int x = horizontal ? b : aIdx;
                    int y = horizontal ? aIdx : b;
                    out[(size_t)y * w + x] = acc;
                }
            }
        };
        blur1(a, tmp, true);
        blur1(tmp, logo.glowA, false);
        return true;
    }

    Scene blended() const { return blend >= 1.f ? current : mixScene(previous, current, blend); }

    void jumpScene(const Scene& s) {
        previous = s; current = s; blend = 1.f;
    }
    void fadeTo(const Scene& s) {
        previous = blended();
        current = s;
        blend = 0;
    }

    void step(float dt, bool save, const char* ppmPath) {
        Audio audio = synth.step(time);
        Scene scene = blended();
        float frames = clampf(dt * 60.f, 0.25f, 4.f);
        WarpU wp{};
        wp.aspect = (float)contentW / (float)contentH;
        wp.time = (float)time;
        float zoomBase = scene.zoom + scene.zoomBass * audio.bass;
        if (zoomBase < 0.2f) zoomBase = 0.2f;
        wp.zoom = std::pow(zoomBase, frames);
        wp.rotation = (scene.rotation + scene.rotationBass * audio.bass) * frames;
        wp.warpAmount = scene.warpAmount * (1.f + audio.mid * 0.8f);
        wp.warpScale = scene.warpScale;
        wp.warpSpeed = scene.warpSpeed;
        wp.decay = std::pow(std::max(scene.decay, 0.01f), frames);
        wp.driftX = scene.driftX * frames;
        wp.driftY = scene.driftY * frames;
        wp.kaleido = scene.kaleido;
        wp.mirror = scene.mirror;
        wp.generative = scene.generative;
        wp.hueShift = scene.hueShift * frames;
        wp.plasma = scene.plasma;
        wp.bass = audio.bass; wp.mid = audio.mid; wp.treble = audio.treble;
        wp.texelX = 1.f / contentW; wp.texelY = 1.f / contentH;

        Surf& src = fb[fi];
        Surf& dst = fb[1 - fi];
        const float* sbuf = src.rgb.data();
        float* dbuf = dst.rgb.data();
        const int W = contentW, H = contentH;
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float u = (x + 0.5f) / W;
                float v = (y + 0.5f) / H;
                V3 c = warpPixel(sbuf, W, H, u, v, wp);
                float* p = dbuf + ((size_t)y * W + x) * 3;
                p[0] = c.r; p[1] = c.g; p[2] = c.b;
            }
        }

        Draw draw;
        draw.dst = &dst;
        draw.aspect = wp.aspect;
        drawWaves(draw, scene, audio, (float)time);

        if (audio.beatStrength > 0.4f) flash = std::max(flash, audio.beatStrength);
        flash *= std::pow(0.02f, dt);

        Pose pose = motion.update(dt, audio);
        V3 tint = hsv(scene.baseHue + (float)time * scene.hueSpeed + 0.5f, 0.8f, 1.f);
        float size01 = logoSize;
        float baseH = logo.aspect >= 1 ? size01 / logo.aspect : size01;
        float baseW = baseH * logo.aspect;
        float hx = baseW * pose.scaleX;
        float hy = baseH * pose.scaleY;
        float cx = pose.x;
        float cy = pose.y - 0.08f * motion.bounceA;
        float rot = pose.rotation;

        // Present + logo into `presented`, then letterbox onto the canvas.
        float* out = presented.rgb.data();
        const float* srcF = dst.rgb.data();
        float vig = scene.vignette;
        float flashNow = flash * beatFlash;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const float* sp = srcF + ((size_t)y * W + x) * 3;
                float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
                float dx = u - 0.5f, dy = v - 0.5f;
                float r = sp[0] * brightness;
                float g = sp[1] * brightness;
                float b = sp[2] * brightness;
                float vigK = 1.f - vig * (dx * dx + dy * dy) * 2.f;
                r *= vigK; g *= vigK; b *= vigK;
                r += flashNow * 0.18f; g += flashNow * 0.18f; b += flashNow * 0.18f;
                r = r / (1.f + r * 0.25f);
                g = g / (1.f + g * 0.25f);
                b = b / (1.f + b * 0.25f);
                r = satf(r); g = satf(g); b = satf(b);

                // Logo in height units. +y is up; pixel v grows downward.
                float hux = ((x + 0.5f) / W * 2.f - 1.f) * wp.aspect;
                float huy = 1.f - (y + 0.5f) / H * 2.f;
                float lx = hux - cx, ly = huy - cy;
                float cr = std::cos(rot), sr = std::sin(rot);
                float localX = cr * lx + sr * ly;
                float localY = -sr * lx + cr * ly;
                if (glow > 0.01f && std::fabs(localX) < hx * 1.18f && std::fabs(localY) < hy * 1.18f) {
                    float gu = localX / (hx * 1.18f) * 0.5f + 0.5f;
                    float gv = 0.5f - localY / (hy * 1.18f) * 0.5f;
                    float ga = sampleGlow(logo, gu, gv);
                    float op = glow * (0.35f + std::min(audio.bass, 1.2f) * 0.9f);
                    r = satf(r + tint.r * ga * op);
                    g = satf(g + tint.g * ga * op);
                    b = satf(b + tint.b * ga * op);
                }
                if (std::fabs(localX) < hx && std::fabs(localY) < hy) {
                    float lu = localX / hx * 0.5f + 0.5f;
                    float lv = 0.5f - localY / hy * 0.5f;
                    float la = 0;
                    V3 lc = sampleRGBA(logo, lu, lv, la);
                    float tr = lerpf(lc.r, tint.r, logoTint);
                    float tg = lerpf(lc.g, tint.g, logoTint);
                    float tb = lerpf(lc.b, tint.b, logoTint);
                    r = tr * la + r * (1.f - la);
                    g = tg * la + g * (1.f - la);
                    b = tb * la + b * (1.f - la);
                }
                float* dp = out + ((size_t)y * W + x) * 3;
                dp[0] = r; dp[1] = g; dp[2] = b;
            }
        }

        std::fill(canvas.begin(), canvas.end(), 0);
        int ox = (canvasW - contentW) / 2;
        int oy = (canvasH - contentH) / 2;
        for (int y = 0; y < contentH; y++) {
            for (int x = 0; x < contentW; x++) {
                const float* sp = out + ((size_t)y * contentW + x) * 3;
                uint8_t* dp = canvas.data() + ((size_t)(y + oy) * canvasW + (x + ox)) * 3;
                dp[0] = (uint8_t)std::lround(satf(sp[0]) * 255.f);
                dp[1] = (uint8_t)std::lround(satf(sp[1]) * 255.f);
                dp[2] = (uint8_t)std::lround(satf(sp[2]) * 255.f);
            }
        }

        if (save && ppmPath) {
            FILE* f = std::fopen(ppmPath, "wb");
            if (!f) { std::perror(ppmPath); }
            else {
                std::fprintf(f, "P6\n%d %d\n255\n", canvasW, canvasH);
                std::fwrite(canvas.data(), 1, canvas.size(), f);
                std::fclose(f);
            }
        }

        fi = 1 - fi;
        time += dt;
        blend = std::min(1.f, blend + dt / 0.55f);
    }
};

static void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  render-readme-preview logo.raw hero <outdir> [w h fps]\n"
        "  render-readme-preview logo.raw still <outdir> <Scene Name> <file.ppm> [portrait] [w h]\n");
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    const char* logoPath = argv[1];
    std::string mode = argv[2];
    if (mode == "hero") {
        if (argc < 4) { usage(); return 1; }
        const char* outdir = argv[3];
        int W = argc > 4 ? std::atoi(argv[4]) : 800;
        int H = argc > 5 ? std::atoi(argv[5]) : 450;
        int fps = argc > 6 ? std::atoi(argv[6]) : 20;
        Engine eng(W, H);
        if (!eng.loadLogo(logoPath)) return 1;
        float dt = 1.f / fps;
        struct Seg { const char* name; float dur; bool portrait; };
        Seg segs[] = {
            {"Full Kaleidoscope", 1.85f, false},
            {"Spectrum Skyline", 1.55f, false},
            {"Starburst", 1.45f, false},
            {"Liquid Plasma", 1.5f, false},
            {"Vortex", 1.35f, false},
            {"Full Kaleidoscope", 1.85f, true},
        };
        eng.jumpScene(sceneByName(segs[0].name));
        int warm = (int)(1.1f * fps);
        std::fprintf(stderr, "warmup %d\n", warm);
        for (int i = 0; i < warm; i++) eng.step(dt, false, nullptr);
        int frame = 0;
        bool prevPortrait = false;
        for (size_t s = 0; s < sizeof(segs) / sizeof(segs[0]); s++) {
            if (segs[s].portrait != prevPortrait) {
                eng.setPortrait(segs[s].portrait);
                prevPortrait = segs[s].portrait;
                eng.jumpScene(sceneByName(segs[s].name));
                for (int i = 0; i < fps / 3; i++) eng.step(dt, false, nullptr);
            } else if (s > 0) {
                eng.fadeTo(sceneByName(segs[s].name));
            }
            int n = std::max(1, (int)std::lround(segs[s].dur * fps));
            std::fprintf(stderr, "segment %s portrait=%d frames=%d\n", segs[s].name, segs[s].portrait, n);
            for (int i = 0; i < n; i++) {
                char path[512];
                std::snprintf(path, sizeof(path), "%s/frame_%04d.ppm", outdir, frame);
                eng.step(dt, true, path);
                frame++;
                if (frame % 10 == 0) std::fprintf(stderr, "  frame %d\n", frame);
            }
        }
        std::fprintf(stderr, "wrote %d frames\n", frame);
        return 0;
    }
    if (mode == "still") {
        if (argc < 6) { usage(); return 1; }
        const char* sceneName = argv[4];
        const char* path = argv[5];
        bool portrait = argc > 6 && std::atoi(argv[6]) != 0;
        int W = argc > 7 ? std::atoi(argv[7]) : 960;
        int H = argc > 8 ? std::atoi(argv[8]) : 540;
        Engine eng(W, H);
        if (!eng.loadLogo(logoPath)) return 1;
        if (portrait) eng.setPortrait(true);
        eng.jumpScene(sceneByName(sceneName));
        const int fps = 24;
        float dt = 1.f / fps;
        int warm = portrait ? 36 : 56;
        if (const char* env = std::getenv("WARMUP")) warm = std::atoi(env);
        for (int i = 0; i < warm; i++) eng.step(dt, false, nullptr);
        // Land just after a kick so the logo is off the ground.
        const double beat = 60.0 / 126.0;
        for (int i = 0; i < 48; i++) {
            double ph = std::fmod(eng.time, beat) / beat;
            if (ph > 0.08 && ph < 0.2) break;
            eng.step(dt, false, nullptr);
        }
        eng.step(dt, true, path);
        std::fprintf(stderr, "still %s -> %s\n", sceneName, path);
        return 0;
    }
    usage();
    return 1;
}
