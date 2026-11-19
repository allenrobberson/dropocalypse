// Dropocalypse for Windows - Direct3D 11 renderer.
//
// Each frame:
//  1. warp the previous frame (zoom / rotate / ripple / fade) into a new feedback texture
//  2. add this frame's waveform, spectrum and (optionally) a logo trail on top
//  3. composite to the back buffer with the bouncing logo and its glow

#include "renderer.h"
#include "shaders.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

static constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------------------------
// Constant buffer layouts (must match shaders.h; all sizes are multiples of 16 bytes)
// ---------------------------------------------------------------------------------------------

struct WarpCB {
    float aspect, time, zoom, rotation;
    float warpAmount, warpScale, warpSpeed, decay;
    float driftX, driftY, kaleido, hueShift;
    float plasma, bass, mid, treble;
    float texelX, texelY;
    int32_t mirror;
    float generative;
};
static_assert(sizeof(WarpCB) == 80, "WarpCB layout");

struct PresentCB {
    float flash, vignette, brightness, pad;
};

struct LogoCB {
    float cx, cy, hx, hy;
    float rotation, aspect, opacity, mipBias;
    float r, g, b, tintAmount;
};
static_assert(sizeof(LogoCB) == 48, "LogoCB layout");

struct GeomVertex {
    float x, y, r, g, b, a;
};

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

struct Rgb { float r, g, b; };

static Rgb hsv(float h, float s, float v) {
    float hh = (h - std::floor(h)) * 6.0f;
    int i = ((int)hh) % 6;
    float f = hh - std::floor(hh);
    float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}

template <typename T>
static void updateCB(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const T& data) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, &data, sizeof(T));
        ctx->Unmap(buf, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Spectrum (render thread)
// ---------------------------------------------------------------------------------------------

Spectrum::Spectrum() {
    for (int i = 0; i < FFTSize; ++i) window_[i] = 0.5f - 0.5f * std::cos(2 * kPi * i / (FFTSize - 1));
    std::fill(std::begin(bandPeaks_), std::end(bandPeaks_), 1e-4f);
}

static void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2 * kPi / len;
        float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                int a = i + k, b = i + k + len / 2;
                float tr = re[b] * cr - im[b] * ci;
                float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

void Spectrum::update(const AudioAnalysis& analysis, double sampleRate) {
    double sr = sampleRate > 0 ? sampleRate : 48000;
    if (sr != configuredRate_) {
        configuredRate_ = sr;
        double binHz = sr / FFTSize;
        double lo = 35.0, hi = std::min(16000.0, sr / 2 - binHz);
        for (int i = 0; i <= BandCount; ++i) {
            double f = lo * std::pow(hi / lo, (double)i / BandCount);
            bandEdges_[i] = std::max(1, std::min(FFTSize / 2 - 1, (int)(f / binHz)));
        }
    }

    analysis.copyRecent(samples_, FFTSize);

    float peak = 0;
    const int offset = FFTSize - WaveCount;
    for (int i = 0; i < WaveCount; ++i) peak = std::max(peak, std::fabs(samples_[offset + i]));
    wavePeak_ = std::max({peak, wavePeak_ * 0.995f, 0.01f});
    for (int i = 0; i < WaveCount; ++i) wave[i] = samples_[offset + i] / wavePeak_;

    for (int i = 0; i < FFTSize; ++i) { re_[i] = samples_[i] * window_[i]; im_[i] = 0; }
    fft(re_, im_, FFTSize);
    for (int i = 0; i < FFTSize / 2; ++i) mags_[i] = std::sqrt(re_[i] * re_[i] + im_[i] * im_[i]);

    float frameMax = 0;
    for (int b = 0; b < BandCount; ++b)
        for (int k = bandEdges_[b]; k < std::max(bandEdges_[b + 1], bandEdges_[b] + 1); ++k)
            frameMax = std::max(frameMax, mags_[k]);
    overallPeak_ = std::max({frameMax, overallPeak_ * 0.998f, 1e-3f});

    for (int b = 0; b < BandCount; ++b) {
        int a = bandEdges_[b], e = std::max(bandEdges_[b + 1], a + 1);
        float m = 0;
        for (int k = a; k < e; ++k) m = std::max(m, mags_[k]);
        bandPeaks_[b] = std::max({m, bandPeaks_[b] * 0.997f, overallPeak_ * 0.2f});
        float v = std::min(1.0f, m / bandPeaks_[b]);
        bands[b] = v > bands[b] ? v : bands[b] * 0.86f + v * 0.14f;
    }
}

// ---------------------------------------------------------------------------------------------
// Logo motion (same physics as the macOS version)
// ---------------------------------------------------------------------------------------------

LogoPose LogoMotion::update(float rawDt, const AudioSnapshot& audio, const RenderSettings& s,
                            const MotionVariant& variant) {
    float dt = std::min(std::max(rawDt, 0.0f), 1.0f / 20.0f);
    time_ += dt;
    float bass = std::min(audio.bass, 1.4f);
    if (!started_) {
        started_ = true;
        side_ = variant.side;
    }

    if (audio.beatCount != lastBeat_) {
        bool fresh = lastBeat_ != 0;
        lastBeat_ = audio.beatCount;
        if (fresh) {
            // A kick still waiting from the last beat lands now, so none are lost.
            if (pending_) kick(pendingStrength_, s);
            pending_ = true;
            pendingStrength_ = audio.beatStrength * variant.amount;
            pendingAt_ = time_ + variant.delay;
        }
    }
    // Item 0 has no delay, so its kick lands on the same frame as the beat.
    if (pending_ && time_ >= pendingAt_) {
        pending_ = false;
        kick(pendingStrength_, s);
    }

    const int steps = 4;
    float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        squash_.step(h);
        slide_.step(h);
        wobble_.step(h);
        bounceV_ -= 7.5f * h;
        bounceY_ += bounceV_ * h;
        if (bounceY_ < 0) {
            float impact = -bounceV_;
            bounceY_ = 0;
            bounceV_ = impact > 0.35f ? impact * 0.3f : 0;
            if (impact > 0.2f) squash_.v += s.squash * std::min(impact, 2.0f) * 1.5f;
        }
    }

    LogoPose pose;
    float pulse = 1 + s.pulse * 0.32f * bass;
    float stretch = s.squash * 0.12f * bass;
    float sq = std::max(-0.45f, std::min(0.45f, squash_.x));
    pose.scaleX = pulse * (1 + sq) * (1 - stretch * 0.5f);
    pose.scaleY = pulse * (1 - sq * 0.85f) * (1 + stretch);
    pose.y = bounceY_ * 0.55f;
    pose.x = slide_.x * 0.5f + std::sin(time_ * 0.45f + variant.phase) * 0.04f * s.slide;
    pose.rotation = wobble_.x * 0.35f + std::sin(time_ * 0.3f + variant.phase) * 0.02f * s.wobble;
    return pose;
}

void LogoMotion::kick(float st, const RenderSettings& s) {
    side_ = -side_;
    // Snap on the hit: part of every kick moves the logo this frame (direct displacement),
    // the rest goes into the spring's velocity for the follow-through.
    squash_.x += s.squash * 0.22f * st;
    squash_.v += s.squash * 3.5f * st;
    if (bounceY_ < 0.05f) {
        bounceY_ += s.bounce * 0.05f * st;
        bounceV_ += s.bounce * 1.7f * st;
    }
    slide_.x += s.slide * 0.09f * st * side_;
    slide_.v += s.slide * 1.1f * st * side_;
    wobble_.x += s.wobble * 0.22f * st * side_;
    wobble_.v += s.wobble * 2.2f * st * side_;
}

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------

namespace geo {

struct V2 {
    float x, y;
    V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
    V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
    V2 operator*(float k) const { return {x * k, y * k}; }
};

struct RGBA { float r, g, b, a; };

// Writes colored triangles. Coordinates are in "height units": y in -1..1, x in -aspect..aspect.
struct GeometryBuilder {
    GeomVertex* ptr;
    int capacity;
    float aspect;
    int count = 0;

    void v(V2 p, RGBA c) { ptr[count++] = {p.x / aspect, p.y, c.r, c.g, c.b, c.a}; }

    void triangle(V2 a, V2 b, V2 c, RGBA col) {
        if (count + 3 > capacity) return;
        v(a, col); v(b, col); v(c, col);
    }
    void quad(V2 a, V2 b, V2 c, V2 d, RGBA col) { triangle(a, b, c, col); triangle(a, c, d, col); }
    // Per-corner colors (gradients, soft edges).
    void triangle(V2 a, RGBA ca, V2 b, RGBA cb, V2 c, RGBA cc) {
        if (count + 3 > capacity) return;
        v(a, ca); v(b, cb); v(c, cc);
    }
    void quad(V2 a, RGBA ca, V2 b, RGBA cb, V2 c, RGBA cc, V2 d, RGBA cd) {
        triangle(a, ca, b, cb, c, cc);
        triangle(a, ca, c, cc, d, cd);
    }
    // Disc that fades from `col` in the middle to transparent at the rim.
    void softDot(V2 center, float radius, RGBA col, int segments = 16) {
        RGBA rim{col.r, col.g, col.b, 0};
        V2 prev = center + V2{radius, 0};
        for (int i = 1; i <= segments; ++i) {
            float a = (float)i / segments * 2 * kPi;
            V2 p = center + V2{std::cos(a), std::sin(a)} * radius;
            triangle(center, col, prev, rim, p, rim);
            prev = p;
        }
    }
    // Cheap 4-sided dot for large particle counts.
    void spark(V2 c, float r, RGBA col) { quad(c + V2{r, 0}, c + V2{0, r}, c + V2{-r, 0}, c + V2{0, -r}, col); }
    void line(V2 a, V2 b, float width, RGBA col) {
        V2 d = b - a;
        float len = std::max(std::sqrt(d.x * d.x + d.y * d.y), 1e-6f);
        V2 n = V2{-d.y / len, d.x / len} * (width * 0.5f);
        quad(a + n, b + n, b - n, a - n, col);
    }
    void rect(float x0, float y0, float x1, float y1, RGBA col) {
        quad({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, col);
    }
    void dot(V2 c, float radius, RGBA col) {
        const int seg = 8;
        V2 prev = c + V2{radius, 0};
        for (int i = 1; i <= seg; ++i) {
            float a = (float)i / seg * 2 * kPi;
            V2 p = c + V2{std::cos(a), std::sin(a)} * radius;
            triangle(c, prev, p, col);
            prev = p;
        }
    }
    template <typename ColorFn>
    void polyline(const std::vector<V2>& pts, bool closed, float width, ColorFn color) {
        if (pts.size() < 2) return;
        for (size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i], pts[i + 1], width, color((int)i));
        if (closed) line(pts.back(), pts.front(), width, color((int)pts.size() - 1));
    }
};

template <typename ColorFn>
void ring(GeometryBuilder& g, const float* wave, int waveCount, int stride, int offset, float radius,
          float amplitude, float rotation, float width, ColorFn color) {
    int n = (waveCount - offset) / stride;
    if (n <= 16) return;
    const int blend = 24;
    std::vector<V2> pts;
    pts.reserve(n);
    float first = wave[offset];
    for (int i = 0; i < n; ++i) {
        float w = wave[offset + i * stride];
        if (i >= n - blend) {
            float k = (float)(i - (n - blend)) / blend;
            w += (first - w) * k;
        }
        float a = (float)i / n * 2 * kPi + rotation;
        float r = std::max(0.0f, radius + w * amplitude);
        pts.push_back(V2{std::cos(a), std::sin(a)} * r);
    }
    g.polyline(pts, true, width, [&](int i) { return color((float)i / n); });
}

} // namespace geo
using namespace geo;

// ---------------------------------------------------------------------------------------------
// Classic favorites (styles 11-20): persistent particles, stars, bolts, ridgelines and rain.
// Mirrors Sources/Dropocalypse/Render/StyleFX.swift.
// ---------------------------------------------------------------------------------------------

struct Palette {
    float hue0, sat, alpha;
    RGBA operator()(float frac, float boost) const {
        Rgb c = hsv(hue0 + frac * 0.35f, sat, 1);
        return RGBA{c.r, c.g, c.b, std::min(1.0f, alpha * boost)};
    }
};

struct FXFrame {
    const VisualScene& scene;
    const AudioSnapshot& audio;
    const float* bands;
    int bandCount;
    float t, dt, aspect;
    V2 logoCenter;
    float logoRadius;
    Palette color;
    float bass() const { return std::min(audio.bass, 1.4f); }
};

static RGBA rgba(Rgb c, float a) { return RGBA{c.r, c.g, c.b, a}; }

class StyleFX {
public:
    void begin(const FXFrame& f) {
        beatFired_ = f.audio.beatCount != lastBeat_ && lastBeat_ != 0;
        lastBeat_ = f.audio.beatCount;
        beatStrength_ = f.audio.beatStrength;
    }

    // ---- 11. Bass Ring
    void bassRing(const FXFrame& f, GeometryBuilder& g) {
        float bass = f.bass();
        V2 c = f.logoCenter;
        float r0 = std::max(0.22f, f.logoRadius * 1.02f + 0.03f) * (1 + 0.12f * bass) * f.scene.waveScale;
        int n = f.bandCount;
        float barWidth = std::min(0.022f, 2 * kPi * r0 / (n * 2) * 0.6f) * f.scene.thickness;
        for (float side : {-1.0f, 1.0f}) {
            for (int i = 0; i < n; ++i) {
                float frac = (i + 0.5f) / n;
                float a = kPi / 2 - side * frac * kPi;
                V2 dir{std::cos(a), std::sin(a)};
                float v = std::pow(f.bands[i], 1.3f);
                float len = 0.012f + v * 0.36f;
                g.line(c + dir * r0, c + dir * (r0 + len), barWidth, f.color(frac * 0.6f, 1));
            }
        }
        std::vector<V2> ring;
        const int segs = 96;
        for (int i = 0; i < segs; ++i) {
            float a = (float)i / segs * 2 * kPi;
            ring.push_back(c + V2{std::cos(a), std::sin(a)} * (r0 - 0.012f));
        }
        RGBA ringColor = f.color(0.1f, 0.7f);
        g.polyline(ring, true, 0.004f, [&](int) { return ringColor; });

        int spawn = (int)(2 + f.audio.treble * 6);
        if (beatFired_) spawn += (int)(20 + beatStrength_ * 30);
        spawn = std::min(spawn, 80);
        for (int k = 0; k < spawn && sparks_.size() < 700; ++k) {
            float a = rng_.range(0, 2 * kPi);
            V2 dir{std::cos(a), std::sin(a)};
            float speed = rng_.range(0.12f, 0.45f) + bass * 0.5f;
            float life = rng_.range(1.0f, 2.2f);
            sparks_.push_back({c + dir * r0, dir * speed, life, life, rng_.range(0.002f, 0.006f), rng_.range(0, 0.6f)});
        }
        for (int i = (int)sparks_.size() - 1; i >= 0; --i) {
            Particle& q = sparks_[i];
            q.life -= f.dt;
            if (q.life <= 0) { sparks_[i] = sparks_.back(); sparks_.pop_back(); continue; }
            q.v = q.v * (1 - 0.4f * f.dt);
            q.p = q.p + q.v * f.dt;
            float k = q.life / q.maxLife;
            g.spark(q.p, q.size * (0.4f + 0.6f * k), f.color(q.hue, k));
        }
    }

    // ---- 12. Neon Equalizer
    void neonEqualizer(const FXFrame& f, GeometryBuilder& g) {
        int n = f.bandCount;
        float total = 1.84f * f.aspect;
        float slot = total / n;
        float left = -total / 2;
        const float base = -0.42f;
        float s = f.scene.waveScale;
        g.rect(left, base - 0.002f, left + total, base + 0.002f, f.color(0.5f, 0.5f));
        for (int i = 0; i < n; ++i) {
            float prev = f.bands[std::max(0, i - 1)], next = f.bands[std::min(n - 1, i + 1)];
            float v = (prev + 2 * f.bands[i] + next) / 4;
            float h = 0.012f + std::pow(v, 1.15f) * 0.62f * s;
            float x0 = left + i * slot + slot * 0.18f;
            float x1 = x0 + slot * 0.64f;
            RGBA col = f.color((float)i / n, 1);
            g.rect(x0, base + 0.01f, x1, base + 0.01f + h, col);
            RGBA top{col.r, col.g, col.b, 0.3f}, bottom{col.r, col.g, col.b, 0};
            float ry = base - 0.01f, rh = h * 0.45f;
            g.quad({x0, ry}, top, {x1, ry}, top, {x1, ry - rh}, bottom, {x0, ry - rh}, bottom);
        }
    }

    // ---- 13. Pulsar Ridges (in the "over" batch so lower ridges hide the ones behind)
    void pulsarRidges(const FXFrame& f, GeometryBuilder& over) {
        const int rows = RidgeRows, pts = RidgePoints;
        if (ridges_.empty()) ridges_.assign(rows, std::vector<float>(pts, 0.0f));
        ridgeTimer_ += f.dt;
        if (ridgeTimer_ >= 1.0f / 20.0f) {
            ridgeTimer_ = 0;
            std::vector<float> row(pts, 0.0f);
            int n = f.bandCount;
            for (int j = 0; j < pts; ++j) {
                float x01 = (float)j / (pts - 1);
                float d = std::fabs(x01 - 0.5f) * 2;
                float e = (x01 - 0.5f) / 0.22f;
                float env = std::exp(-e * e) * 0.85f + 0.15f;
                int idx = std::min(n - 1, (int)(d * n * 0.7f));
                row[j] = (std::pow(f.bands[idx], 1.4f) + rng_.range(0, 0.06f)) * env;
            }
            ridges_.erase(ridges_.begin());
            ridges_.push_back(row);
        }
        float spacing = 1.3f / rows;
        float halfWidth = f.aspect * 1.02f; // edge to edge (slightly past, so no visible ends)
        RGBA ink = f.color(0, 1);
        RGBA lineColor{ink.r, ink.g, ink.b, 0.95f};
        RGBA fill{0, 0, 0, 1};
        float s = f.scene.waveScale * (1 + f.bass() * 0.15f);
        for (int k = 0; k < rows; ++k) {
            const auto& row = ridges_[k];
            float baseY = 0.68f - k * spacing;
            V2 prev{-halfWidth, baseY + row[0] * 0.32f * s};
            for (int j = 1; j < pts; ++j) {
                float x = -halfWidth + (float)j / (pts - 1) * 2 * halfWidth;
                V2 p{x, baseY + row[j] * 0.32f * s};
                over.quad(prev, fill, p, fill, {p.x, baseY - 0.04f}, fill, {prev.x, baseY - 0.04f}, fill);
                over.line(prev, p, 0.0045f * f.scene.thickness, lineColor);
                prev = p;
            }
        }
    }

    // ---- 14. Outrun Grid
    void outrunGrid(const FXFrame& f, GeometryBuilder& g, GeometryBuilder& over) {
        const float horizon = -0.02f;
        float bass = f.bass();
        float w = f.aspect * 1.05f;
        RGBA glow{0.9f, 0.15f, 0.6f, 0.35f}, clear{0.9f, 0.15f, 0.6f, 0};
        over.quad({-w, horizon}, glow, {w, horizon}, glow, {w, horizon + 0.55f}, clear, {-w, horizon + 0.55f}, clear);

        drawSun(0.34f * (1 + 0.08f * bass), horizon + 0.3f, horizon, over);
        over.rect(-w, -1.05f, w, horizon, RGBA{0.02f, 0, 0.05f, 1});

        gridPhase_ += (0.45f + bass * 2.2f + (beatFired_ ? 0.6f : 0)) * f.dt;
        gridPhase_ -= std::floor(gridPhase_);
        float gridHue = f.scene.baseHue;
        Rgb gc = hsv(gridHue, 0.85f, 1);
        for (int i = 0; i < 26; ++i) {
            float z = 1 + i + (1 - gridPhase_);
            float y = horizon - 0.95f / z;
            float a = std::min(1.0f, 1.8f / z);
            g.line({-w, y}, {w, y}, 0.0025f + 0.004f / z, rgba(gc, a));
        }
        for (int k = -16; k <= 16; ++k) {
            float xw = k * 0.5f;
            V2 nearP{xw * 0.95f, horizon - 0.95f};
            V2 farP{xw * 0.95f / 30, horizon - 0.95f / 30};
            g.line(farP, nearP, 0.003f, rgba(gc, 0.75f));
        }
        g.rect(-w, horizon - 0.003f, w, horizon + 0.003f, rgba(hsv(gridHue + 0.05f, 0.5f, 1), 0.9f));
        // Edge to edge, colored from the sun's gold through pink and magenta to the grid's purple.
        int n = f.bandCount;
        float slot = w / n;
        for (int i = 0; i < n; ++i) {
            float frac = (float)i / (n - 1);
            float v = (f.bands[std::max(0, i - 1)] + 2 * f.bands[i] + f.bands[std::min(n - 1, i + 1)]) / 4;
            float h = 0.012f + std::pow(v, 1.1f) * 0.2f * f.scene.waveScale;
            float hue = 0.12f - frac * 0.37f;
            Rgb c = hsv(hue, 0.9f, 1), t = hsv(hue + 0.03f, 0.5f, 1);
            RGBA foot = rgba(c, 0.85f), tip = rgba(t, 1), refl = rgba(c, 0.28f), fade = rgba(c, 0);
            for (float side : {-1.0f, 1.0f}) {
                float xa = side * (i * slot + slot * 0.18f), xb = side * (i * slot + slot * 0.82f);
                float x0 = std::min(xa, xb), x1 = std::max(xa, xb);
                g.quad({x0, horizon + 0.003f}, foot, {x1, horizon + 0.003f}, foot, {x1, horizon + h}, tip, {x0, horizon + h}, tip);
                g.quad({x0, horizon - 0.003f}, refl, {x1, horizon - 0.003f}, refl, {x1, horizon - h * 0.45f}, fade,
                       {x0, horizon - h * 0.45f}, fade);
            }
        }
    }

    // Smooth striped synthwave sun: slices whose edges follow the circle, a smooth gradient and a
    // soft feathered rim, so it never looks stair-stepped.
    void drawSun(float r, float cy, float horizon, GeometryBuilder& over) {
        float top = cy + r, bottom = std::max(cy - r, horizon);
        auto halfW = [&](float y) { return std::sqrt(std::max(0.0f, r * r - (y - cy) * (y - cy))); };
        auto col = [&](float y, float a) { return rgba(hsv(0.13f - (top - y) / (2 * r) * 0.21f, 0.85f, 1), a); };
        std::vector<std::pair<float, float>> bands;
        float stripeStart = top - 0.45f * 2 * r;
        bands.push_back({top, stripeStart});
        float period = 2 * r / 13, y = stripeStart;
        while (y > bottom) {
            float t = (stripeStart - y) / std::max(0.001f, stripeStart - (cy - r));
            float gap = period * (0.16f + 0.42f * t);
            float solidBottom = std::max(bottom, y - (period - gap));
            bands.push_back({y - gap * 0.5f, solidBottom});
            y = solidBottom - gap * 0.5f;
        }
        const float feather = 0.006f;
        for (auto [y0, y1] : bands) {
            if (y0 <= y1) continue;
            int pieces = std::max(1, (int)std::ceil((y0 - y1) / 0.006f));
            for (int k = 0; k < pieces; ++k) {
                float ya = y0 - (y0 - y1) * k / pieces, yb = y0 - (y0 - y1) * (k + 1) / pieces;
                float wa = halfW(ya), wb = halfW(yb);
                RGBA ca = col(ya, 1), cb = col(yb, 1), ea = col(ya, 0), eb = col(yb, 0);
                over.quad({-wb, yb}, cb, {wb, yb}, cb, {wa, ya}, ca, {-wa, ya}, ca);
                over.quad({wb, yb}, cb, {wb + feather, yb}, eb, {wa + feather, ya}, ea, {wa, ya}, ca);
                over.quad({-wb - feather, yb}, eb, {-wb, yb}, cb, {-wa, ya}, ca, {-wa - feather, ya}, ea);
            }
            float wt = halfW(y0), wbm = halfW(y1), fe = feather * 0.6f;
            over.quad({-wt, y0}, col(y0, 1), {wt, y0}, col(y0, 1), {wt, y0 + fe}, col(y0, 0), {-wt, y0 + fe}, col(y0, 0));
            over.quad({-wbm, y1 - fe}, col(y1, 0), {wbm, y1 - fe}, col(y1, 0), {wbm, y1}, col(y1, 1), {-wbm, y1}, col(y1, 1));
        }
    }

    // ---- 21. Mirror Equalizer: bars up and down from a center line, sunset rainbow
    void mirrorEqualizer(const FXFrame& f, GeometryBuilder& g) {
        int n = f.bandCount;
        float half = 0.94f * f.aspect, slot = half / n, mid = -0.02f, s = f.scene.waveScale;
        float pulse = 1 + 0.12f * f.bass();
        for (int i = 0; i < n; ++i) {
            float v = (f.bands[std::max(0, i - 1)] + 2 * f.bands[i] + f.bands[std::min(n - 1, i + 1)]) / 4;
            float h = (0.008f + std::pow(v, 1.2f) * 0.42f * s) * pulse;
            float frac = (float)i / (n - 1);
            Rgb c = hsv(0.93f + frac * 0.24f + 0.03f * std::sin(f.t * 0.4f), 0.9f, 1);
            RGBA core = rgba(c, 1);
            RGBA tip{std::min(1.0f, c.r + 0.35f), std::min(1.0f, c.g + 0.35f), std::min(1.0f, c.b + 0.35f), 1};
            for (float side : {-1.0f, 1.0f}) {
                float xa = side * (i * slot + slot * 0.16f), xb = side * (i * slot + slot * 0.84f);
                float x0 = std::min(xa, xb), x1 = std::max(xa, xb);
                g.quad({x0, mid}, core, {x1, mid}, core, {x1, mid + h}, tip, {x0, mid + h}, tip);
                g.quad({x0, mid - h}, tip, {x1, mid - h}, tip, {x1, mid}, core, {x0, mid}, core);
            }
        }
        g.rect(-half, mid - 0.0015f, half, mid + 0.0015f, rgba(hsv(0.95f, 0.4f, 1), 0.8f));
    }

    // ---- 22. LED Meter: segmented green-yellow-red LEDs with peak hold
    void ledMeter(const FXFrame& f, GeometryBuilder& g) {
        const int cols = 24, segs = 18;
        int n = f.bandCount;
        if ((int)ledPeaks_.size() != cols) { ledPeaks_.assign(cols, 0); ledHold_.assign(cols, 0); }
        float total = 1.7f * f.aspect, slot = total / cols, left = -total / 2;
        const float base = -0.62f, height = 1.18f, segH = height / segs;
        for (int c = 0; c < cols; ++c) {
            int a = c * n / cols, b = std::max(a + 1, (c + 1) * n / cols);
            float v = 0;
            for (int k = a; k < b; ++k) v = std::max(v, f.bands[k]);
            float level = std::min(1.0f, std::pow(v, 1.1f) * 1.05f * f.scene.waveScale);
            int lit = (int)std::lround(level * segs);
            if ((float)lit >= ledPeaks_[c]) { ledPeaks_[c] = (float)lit; ledHold_[c] = 0.45f; }
            else {
                ledHold_[c] -= f.dt;
                if (ledHold_[c] <= 0) ledPeaks_[c] = std::max(0.0f, ledPeaks_[c] - f.dt * 14);
            }
            float x0 = left + c * slot + slot * 0.12f, x1 = x0 + slot * 0.76f;
            for (int k = 0; k < segs; ++k) {
                float y0 = base + k * segH + segH * 0.12f, y1 = y0 + segH * 0.76f;
                float frac = (float)k / (segs - 1);
                float hue = frac < 0.6f ? 0.33f - frac / 0.6f * 0.12f : 0.21f - (frac - 0.6f) / 0.4f * 0.21f;
                g.rect(x0, y0, x1, y1, rgba(hsv(hue, 0.95f, 1), k < lit ? 1.0f : 0.07f));
            }
            int pk = (int)ledPeaks_[c];
            if (pk > 0 && pk <= segs) {
                float y0 = base + (pk - 1) * segH + segH * 0.12f;
                g.rect(x0, y0, x1, y0 + segH * 0.76f, RGBA{1, 1, 1, 0.95f});
            }
        }
    }

    // ---- 23. Rainbow Peaks: thin rainbow bars with bouncing glowing caps
    void rainbowPeaks(const FXFrame& f, GeometryBuilder& g) {
        int n = f.bandCount;
        if ((int)capY_.size() != n) { capY_.assign(n, 0); capV_.assign(n, 0); }
        float total = 1.86f * f.aspect, slot = total / n, left = -total / 2;
        const float base = -0.5f;
        std::vector<V2> tops;
        tops.reserve(n);
        for (int i = 0; i < n; ++i) {
            float v = (f.bands[std::max(0, i - 1)] + 2 * f.bands[i] + f.bands[std::min(n - 1, i + 1)]) / 4;
            float h = 0.01f + std::pow(v, 1.1f) * 0.75f * f.scene.waveScale;
            float x = left + (i + 0.5f) * slot;
            Rgb c = hsv((float)i / n + f.t * 0.08f, 0.9f, 1);
            float bw = slot * 0.22f;
            g.quad({x - bw, base}, rgba(c, 0.35f), {x + bw, base}, rgba(c, 0.35f), {x + bw, base + h}, rgba(c, 1),
                   {x - bw, base + h}, rgba(c, 1));
            capV_[i] -= 2.4f * f.dt;
            capY_[i] += capV_[i] * f.dt;
            if (capY_[i] < h + 0.012f) {
                capY_[i] = h + 0.012f;
                capV_[i] = std::max(capV_[i], (beatFired_ ? 0.9f : 0.25f) * v);
            }
            float cy = base + capY_[i];
            g.softDot({x, cy}, slot * 0.55f, rgba(c, 0.35f), 10);
            g.rect(x - slot * 0.32f, cy - 0.004f, x + slot * 0.32f, cy + 0.004f, RGBA{1, 1, 1, 0.95f});
            tops.push_back({x, base + h});
        }
        g.polyline(tops, false, 0.004f, [&](int i) { return rgba(hsv((float)i / n + f.t * 0.08f + 0.5f, 0.6f, 1), 0.55f); });
        g.rect(left, base - 0.002f, left + total, base + 0.002f, RGBA{1, 1, 1, 0.35f});
    }

    // ---- 25. Full Kaleidoscope: symmetric rings and spokes over the shader mandala.
    void fullKaleidoscope(const FXFrame& f, GeometryBuilder& g) {
        const float bass = f.bass();
        const float pulse = 1 + bass * 0.22f;
        const float reach = std::min(0.92f, f.aspect * 0.92f);
        const float hues[5] = {0.50f, 0.92f, 0.62f, 0.11f, 0.30f};
        auto neon = [&](int i, float alpha) {
            Rgb c = hsv(hues[i % 5] + f.audio.mid * 0.04f, 1, 1);
            return rgba(c, alpha);
        };
        const int segments = 72;
        for (int i = 0; i < 3; ++i) {
            float radius = std::min(reach, (0.16f + i * 0.22f) * pulse);
            std::vector<V2> pts;
            pts.reserve(segments);
            for (int k = 0; k < segments; ++k) {
                float a = (float)k / segments * 2 * kPi + f.t * 0.04f * (i + 1);
                pts.push_back(V2{std::cos(a), std::sin(a)} * radius);
            }
            RGBA col = neon(i, 0.28f + bass * 0.22f);
            g.polyline(pts, true, 0.0035f + bass * 0.003f, [&](int) { return col; });
        }
        const int arms = 8;
        const int n = f.bandCount;
        for (int arm = 0; arm < arms && n > 0; ++arm) {
            float a = (float)arm / arms * 2 * kPi;
            V2 dir{std::cos(a), std::sin(a)};
            float band = f.bands[(arm * n / arms) % n];
            float inner = std::min(reach * 0.18f, 0.08f * pulse);
            float outer = std::min(reach, inner + 0.1f + band * (0.55f + f.audio.treble * 0.4f));
            g.line(dir * inner, dir * outer, 0.005f + band * 0.008f, neon(arm, 0.4f + band * 0.45f));
            g.softDot(dir * outer, 0.012f + band * 0.02f, neon(arm + 1, 0.35f + band * 0.4f), 8);
        }
    }

    // ---- 24. Tesseract: a 4D hypercube rotating through the fourth dimension
    void tesseract(const FXFrame& f, GeometryBuilder& g) {
        if (beatFired_) tessKick_ = std::min(3.0f, tessKick_ + 0.9f + beatStrength_ * 0.6f);
        tessKick_ *= std::exp(-f.dt * 2.5f);
        float spin = (0.22f + 0.55f * f.bass() + tessKick_) * f.dt;
        const float rate[4] = {0.9f, 0.65f, 0.4f, 0.25f};
        for (int k = 0; k < 4; ++k) tessAngles_[k] += rate[k] * spin;
        auto rotate = [](float* p, int i, int j, float a) {
            float c = std::cos(a), s = std::sin(a), pi = p[i], pj = p[j];
            p[i] = pi * c - pj * s;
            p[j] = pi * s + pj * c;
        };
        float scale = 1.55f * (1 + 0.14f * f.bass()) * f.scene.waveScale;
        V2 pts[16];
        float w[16];
        for (int i = 0; i < 16; ++i) {
            float p[4] = {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f, (i & 8) ? 1.0f : -1.0f};
            rotate(p, 0, 3, tessAngles_[0]);
            rotate(p, 1, 3, tessAngles_[1]);
            rotate(p, 2, 3, tessAngles_[2]);
            rotate(p, 0, 1, tessAngles_[3]);
            float k4 = 1 / (2.6f - p[3]);
            float qx = p[0] * k4, qy = p[1] * k4, qz = p[2] * k4;
            float k3 = 1 / (2.8f - qz);
            pts[i] = V2{qx * k3 * scale, qy * k3 * scale};
            w[i] = p[3];
        }
        float width = 0.0045f * f.scene.thickness * (1 + 0.6f * f.bass());
        for (int i = 0; i < 16; ++i) {
            for (int bit = 0; bit < 4; ++bit) {
                int j = i ^ (1 << bit);
                if (j <= i) continue;
                float nearness = ((w[i] + w[j]) / 2 + 2) / 4;
                RGBA col = f.color(nearness * 0.85f + bit * 0.04f, 1);
                float lw = width * (0.6f + 0.8f * nearness);
                g.line(pts[i], pts[j], lw * 4, RGBA{col.r, col.g, col.b, 0.16f});
                g.line(pts[i], pts[j], lw, RGBA{col.r, col.g, col.b, std::min(1.0f, 0.55f + 0.45f * nearness)});
            }
        }
        for (int i = 0; i < 16; ++i) {
            float nearness = (w[i] + 2) / 4;
            RGBA col = f.color(nearness * 0.85f, 1);
            g.softDot(pts[i], 0.014f * (1 + f.bass()) * (0.6f + 0.8f * nearness), RGBA{col.r, col.g, col.b, 0.5f}, 12);
            g.softDot(pts[i], 0.005f * (0.6f + 0.8f * nearness), RGBA{1, 1, 1, 0.9f}, 8);
        }
    }

    // ---- 15. Hyperspace
    void hyperspace(const FXFrame& f, GeometryBuilder& g) {
        if (stars_.empty()) {
            for (int i = 0; i < 600; ++i)
                stars_.push_back({rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(0.05f, 1), rng_.range(-0.08f, 0.08f)});
        }
        if (beatFired_) warpBoost_ = std::max(warpBoost_, 0.6f + beatStrength_ * 0.8f);
        warpBoost_ *= std::pow(0.05f, f.dt);
        float speed = (0.18f + f.bass() * 1.3f + warpBoost_) * f.scene.waveScale;
        const float proj = 0.35f;
        for (Star& s : stars_) {
            s.z -= speed * f.dt;
            V2 p{s.x / s.z * proj, s.y / s.z * proj};
            if (s.z <= 0.03f || std::fabs(p.x) > f.aspect * 1.2f || std::fabs(p.y) > 1.2f) {
                s = Star{rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(0.85f, 1), s.hue};
                p = V2{s.x / s.z * proj, s.y / s.z * proj};
            }
            float tailZ = std::min(1.0f, s.z + speed * 0.07f);
            V2 tail{s.x / tailZ * proj, s.y / tailZ * proj};
            float nearness = 1 - s.z;
            Rgb c = hsv(f.scene.baseHue + s.hue, f.scene.saturation * (0.4f + nearness * 0.6f), 1);
            g.line(tail, p, (0.0015f + 0.005f * nearness) * f.scene.thickness, rgba(c, std::min(1.0f, nearness * 1.4f)));
        }
    }

    // ---- 16. Particle Storm
    void particleStorm(const FXFrame& f, GeometryBuilder& g) {
        if (swarm_.empty()) {
            for (int i = 0; i < 900; ++i) {
                float a = rng_.range(0, 2 * kPi), r = rng_.range(0.1f, 0.8f);
                swarm_.push_back({V2{std::cos(a), std::sin(a)} * r, V2{0, 0}, 1, 1, rng_.range(0.002f, 0.005f),
                                  rng_.range(0, 1)});
            }
        }
        float t = f.t, dt = f.dt, bass = f.bass();
        float burst = beatFired_ ? (0.5f + beatStrength_ * 1.1f) : 0;
        for (Particle& q : swarm_) {
            float a = std::sin(q.p.x * 2.1f + t * 0.35f) * std::cos(q.p.y * 2.3f - t * 0.28f) * 3 + t * 0.15f;
            V2 flow = V2{std::cos(a), std::sin(a)} * (0.1f + bass * 0.45f);
            float r = std::max(0.001f, std::sqrt(q.p.x * q.p.x + q.p.y * q.p.y));
            V2 radial = q.p * (1 / r);
            V2 tangent{-radial.y, radial.x};
            V2 target = flow + tangent * 0.22f + radial * ((0.55f - r) * 0.6f);
            q.v = q.v + (target - q.v) * std::min(1.0f, dt * 3);
            if (burst > 0) q.v = q.v + radial * burst;
            q.p = q.p + q.v * dt;
            if (std::fabs(q.p.x) > f.aspect + 0.1f) q.p.x = -q.p.x * 0.98f;
            if (std::fabs(q.p.y) > 1.1f) q.p.y = -q.p.y * 0.98f;
            float hue = std::atan2(q.p.y, q.p.x) / (2 * kPi) * 0.4f + q.hue * 0.15f;
            g.spark(q.p, (q.size + f.audio.treble * 0.003f) * f.scene.thickness, f.color(hue, 0.85f));
        }
    }

    // ---- 17. Lightning Storm
    void lightning(const FXFrame& f, GeometryBuilder& g) {
        int spawn = 0;
        if (beatFired_) spawn += 1 + (int)(beatStrength_ * 2.5f);
        if (f.audio.treble > 0.7f && rng_.next() < f.dt * 3) spawn += 1;
        for (int i = 0; i < spawn && bolts_.size() < 24; ++i) makeBolt(f);

        g.softDot(f.logoCenter, 0.15f + f.bass() * 0.12f, f.color(0.1f, 0.18f + f.bass() * 0.2f), 24);

        for (int i = (int)bolts_.size() - 1; i >= 0; --i) {
            bolts_[i].life -= f.dt;
            if (bolts_[i].life <= 0) bolts_.erase(bolts_.begin() + i);
        }
        for (const Bolt& b : bolts_) {
            float k = b.life / b.maxLife;
            Rgb c = hsv(b.hue, 0.6f, 1);
            for (size_t j = 1; j < b.points.size(); ++j) {
                g.line(b.points[j - 1], b.points[j], 0.02f * b.width, rgba(c, 0.2f * k));
                g.line(b.points[j - 1], b.points[j], 0.0045f * b.width, RGBA{0.9f, 0.95f, 1, k});
            }
        }
    }

    // ---- 18. Digital Rain
    void digitalRain(const FXFrame& f, GeometryBuilder& g) {
        const float cell = 0.05f;
        int columns = (int)(2 * f.aspect / cell) + 1;
        if ((int)rain_.size() != columns) {
            rain_.clear();
            for (int i = 0; i < columns; ++i) rain_.push_back(newColumn(rng_.range(-1, 1.8f)));
        }
        int tick = (int)(f.t * 7);
        float tempo = (0.55f + f.bass() * 1.6f + (beatFired_ ? 0.5f : 0)) * f.scene.waveScale;
        Rgb trail = hsv(f.scene.baseHue, f.scene.saturation, 1);
        for (int c = 0; c < columns; ++c) {
            RainColumn& col = rain_[c];
            col.y -= col.speed * tempo * f.dt;
            if (col.y + col.length * cell < -1.05f) col = newColumn(rng_.range(1.05f, 1.6f));
            float x = -f.aspect + (c + 0.5f) * cell;
            int headRow = (int)std::floor(col.y / cell);
            for (int k = 0; k < col.length; ++k) {
                int row = headRow + k;
                float cy = (row + 0.5f) * cell;
                if (cy < -1.05f || cy > 1.05f) continue;
                uint32_t h = col.seed * 2654435761u + (uint32_t)(row * 40503);
                if ((h & 7u) == 0) h += (uint32_t)(tick * 9973);
                float fade = std::pow(1 - (float)k / col.length, 1.3f);
                RGBA color = k == 0 ? RGBA{0.85f, 1, 0.9f, 1} : rgba(trail, 0.9f * fade);
                glyph(g, {x, cy}, cell, h, color);
            }
        }
    }

    // ---- 19. Lava Lamp
    void lavaLamp(const FXFrame& f, GeometryBuilder& g) {
        lavaTime_ += f.dt * (0.5f + f.bass() * 1.2f);
        float t = lavaTime_;
        for (int i = 0; i < 7; ++i) {
            float fi = (float)i;
            float x = std::sin(t * (0.21f + fi * 0.037f) + fi * 1.7f) * f.aspect * 0.55f;
            float y = std::sin(t * (0.17f + fi * 0.029f) + fi * 2.3f) * 0.7f;
            float r = (0.16f + 0.06f * std::sin(t * 0.8f + fi)) * (1 + 0.35f * f.bass()) * f.scene.waveScale;
            g.softDot({x, y}, r, f.color(fi * 0.07f, 0.35f), 28);
            g.softDot({x, y}, r * 0.5f, f.color(fi * 0.07f + 0.05f, 0.25f), 20);
        }
    }

    // ---- 20. Inferno
    void inferno(const FXFrame& f, GeometryBuilder& g) {
        const int emitters = 24;
        float width = f.aspect * 1.9f;
        for (int e = 0; e < emitters; ++e) {
            float band = f.bands[std::min(f.bandCount - 1, e * 2)];
            float rate = (0.4f + band * 3 + f.bass() * 2) * f.dt * 30;
            int count = (int)rate;
            if (rng_.next() < rate - count) ++count;
            for (int k = 0; k < count && embers_.size() < 1100; ++k) {
                float x = -width / 2 + (e + rng_.next()) / emitters * width;
                float life = rng_.range(0.9f, 1.6f);
                embers_.push_back({V2{x, -1.02f},
                                   V2{rng_.range(-0.05f, 0.05f), 0.35f + band * 0.9f + rng_.range(0, 0.3f) + f.bass() * 0.4f},
                                   life, life, rng_.range(0.018f, 0.04f) * (0.7f + band) * f.scene.waveScale,
                                   rng_.range(0, 1)});
            }
        }
        float t = f.t;
        for (int i = (int)embers_.size() - 1; i >= 0; --i) {
            Particle& q = embers_[i];
            q.life -= f.dt;
            if (q.life <= 0) { embers_[i] = embers_.back(); embers_.pop_back(); continue; }
            q.v.y *= (1 - 0.3f * f.dt);
            q.v.x += std::sin(t * 3 + q.p.y * 6 + q.hue * 10) * 0.35f * f.dt;
            q.p = q.p + q.v * f.dt;
            float k = q.life / q.maxLife;
            g.softDot(q.p, q.size * std::sqrt(k), rgba(hsv(0.13f * k, 0.95f, 1), 0.5f * k), 8);
        }
    }

    // ---- 26. Main Stage: the view from an arena floor. Haze, a stage of nested LED frames behind
    // the logo, truss towers with blinders and moving-head beams, a laser fan from the stage and a
    // crowd with phones up. The look changes every 8 beats, like a lighting desk running cues.
    struct StageLook { Rgb haze, beam, fixture, laserA, laserB, led; float laserSustain, beamLevel; };
    static StageLook stageLook(int i) {
        switch (i % 4) {
        case 0: return {{1.0f, 0.52f, 0.22f}, {1.0f, 0.82f, 0.58f}, {1.0f, 0.92f, 0.78f},
                        {1.0f, 0.95f, 0.85f}, {1.0f, 0.75f, 0.35f}, {1.0f, 0.62f, 0.3f}, 0.15f, 1.0f};   // amber
        case 1: return {{0.08f, 0.75f, 0.55f}, {0.6f, 1.0f, 0.82f}, {0.35f, 1.0f, 0.6f},
                        {0.25f, 1.0f, 0.45f}, {0.75f, 1.0f, 0.85f}, {0.55f, 1.0f, 0.75f}, 0.25f, 0.9f};  // teal
        case 2: return {{0.42f, 0.32f, 0.92f}, {0.85f, 0.8f, 1.0f}, {0.7f, 0.75f, 1.0f},
                        {0.78f, 0.55f, 1.0f}, {0.95f, 0.92f, 1.0f}, {0.8f, 0.7f, 1.0f}, 0.75f, 0.55f}; // lasers
        default: return {{0.08f, 0.16f, 0.95f}, {0.45f, 0.6f, 1.0f}, {0.3f, 0.45f, 1.0f},
                         {0.35f, 0.85f, 1.0f}, {0.6f, 0.65f, 1.0f}, {0.35f, 0.5f, 1.0f}, 0.3f, 0.6f};   // deep blue
        }
    }
    static Rgb mixRgb(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }

    void mainStage(const FXFrame& f, GeometryBuilder& g, GeometryBuilder& over) {
        const float dt = std::min(f.dt, 0.1f);
        const float bass = f.bass();
        const float mid = std::min(f.audio.mid, 1.4f), treb = std::min(f.audio.treble, 1.4f);
        const float a = f.aspect;

        // Cues and envelopes.
        if (beatFired_) {
            ++stageBeats_;
            stageBlinder_ = std::max(stageBlinder_, 0.45f + beatStrength_ * 0.75f);
            stageLaser_ = std::max(stageLaser_, 0.55f + beatStrength_ * 0.7f);
            stageLaserFlip_ = !stageLaserFlip_;
            if (stageBeats_ % 8 == 0) { stagePrevLook_ = stageCurLook_; stageCurLook_ = (stageCurLook_ + 1) % 4; stageLookMix_ = 0; }
            stageSinceBeat_ = 0;
            stageHit_ = 0.5f + 0.5f * beatStrength_;
            if (beatStrength_ > 0.85f && stageWhiteCool_ <= 0) { stageWhite_ = 1; stageWhiteCool_ = 6; }
        }
        stageSinceBeat_ += dt;
        stageBlinder_ = std::max(0.0f, stageBlinder_ - dt * 3.2f);
        stageLaser_ = std::max(0.0f, stageLaser_ - dt * 2.2f);
        stageWhite_ = std::max(0.0f, stageWhite_ - dt * 1.6f);
        stageWhiteCool_ -= dt;
        stageLookMix_ = std::min(1.0f, stageLookMix_ + dt * 2.5f);
        stageEnergy_ += (std::min(1.0f, bass * 0.6f + mid * 0.4f) - stageEnergy_) * std::min(1.0f, dt * 1.5f);
        stageSweep_ += dt * (0.35f + mid * 0.9f);
        stageChase_ += dt * (1.2f + bass * 3.0f);

        StageLook A = stageLook(stagePrevLook_), B = stageLook(stageCurLook_);
        float k = stageLookMix_ * stageLookMix_ * (3 - 2 * stageLookMix_);
        Rgb haze = mixRgb(A.haze, B.haze, k), beamC = mixRgb(A.beam, B.beam, k), fixC = mixRgb(A.fixture, B.fixture, k);
        Rgb lasA = mixRgb(A.laserA, B.laserA, k), lasB = mixRgb(A.laserB, B.laserB, k), ledC = mixRgb(A.led, B.led, k);
        float laserSustain = A.laserSustain + (B.laserSustain - A.laserSustain) * k;
        float beamLevel = A.beamLevel + (B.beamLevel - A.beamLevel) * k;
        Rgb white{1, 1, 1};
        haze = mixRgb(haze, white, stageWhite_ * 0.8f);

        const V2 vp{0, 0.04f};             // vanishing point, behind the logo
        const float floorY = -0.3f;         // stage deck in front
        const float crowdTop = -0.48f;      // nothing additive reaches below this
        const float W = std::min(a * 0.88f, 1.45f);
        auto depthY = [&](float y, float s) { return vp.y + (y - vp.y) * s; };

        // 1. Haze, drawn with normal blending so the crowd can sit in front of it.
        float hz = 0.16f + 0.2f * stageEnergy_ + 0.18f * stageBlinder_ + 0.45f * stageWhite_;
        over.softDot(vp, 1.05f * std::max(0.75f, std::min(a, 1.3f)), rgba(haze, std::min(0.85f, hz)), 28);
        over.softDot({0, floorY + 0.05f}, 0.75f * std::max(0.6f, std::min(a, 1.2f)), rgba(mixRgb(haze, white, 0.35f), std::min(0.7f, hz * 0.9f)), 24);
        for (int side = -1; side <= 1; side += 2)
            over.softDot({side * W * 0.75f, vp.y + 0.45f}, 0.55f, rgba(haze, hz * 0.45f), 20);
        // Light spilling over the crowd from the stage, so their heads read as silhouettes.
        {
            float wash = std::min(0.8f, 0.25f + 0.3f * stageEnergy_ + 0.35f * stageBlinder_ + 0.5f * stageWhite_);
            RGBA lit = rgba(mixRgb(haze, white, 0.25f), wash), none = rgba(haze, 0);
            float xl = -a - 0.1f, xr = a + 0.1f;
            over.quad({xl, crowdTop + 0.04f}, none, {xr, crowdTop + 0.04f}, none, {xr, -0.6f}, lit, {xl, -0.6f}, lit);
            over.quad({xl, -0.6f}, lit, {xr, -0.6f}, lit, {xr, -0.75f}, none, {xl, -0.75f}, none);
        }

        // 2. Stage: nested house-shaped LED frames running back to the vanishing point.
        const int frames = 5;
        float chase = std::fmod(stageChase_, (float)frames + 2);
        for (int i = frames - 1; i >= 0; --i) {
            float s = std::pow(0.7f, (float)i);
            float w = W * 0.78f * s;
            float yb = depthY(floorY, s), yt = vp.y + 0.62f * s, ya = vp.y + 0.98f * s;
            float d = chase - (float)i;
            float pulse = std::exp(-d * d * 1.5f);
            float lv = (0.3f + 0.5f * pulse + 0.35f * stageBlinder_ + 0.5f * stageWhite_) * (0.55f + 0.45f * s);
            V2 pts[5] = {{-w, yb}, {-w, yt}, {0, ya}, {w, yt}, {w, yb}};
            for (int e = 0; e < 4; ++e) {
                g.line(pts[e], pts[e + 1], 0.035f * s, rgba(ledC, 0.09f * lv));
                g.line(pts[e], pts[e + 1], 0.005f * s + 0.0015f, rgba(mixRgb(ledC, white, 0.5f), std::min(1.0f, 0.5f * lv)));
            }
            // A row of small fixtures along each frame's top edge.
            for (int j = 1; j <= 3; ++j) {
                float u = j / 4.0f;
                for (int side = -1; side <= 1; side += 2) {
                    V2 p{side * w * (1 - u * 0.0f) * (1 - u), yt + (ya - yt) * u};
                    g.softDot(p, 0.035f * s, rgba(fixC, 0.25f * lv + 0.4f * stageBlinder_), 10);
                }
            }
        }

        // 3. The nave: pairs of truss columns running back to the stage. Each column is topped with
        // a cluster of moving heads that sprays a fan of beams upward; the fans from facing columns
        // meet overhead in pointed arches, so the room reads like a cathedral's vaulted ceiling.
        // Every kick blooms the fans open, front pair first, rippling back toward the stage.
        const int cols = 5;
        const float colDepth[cols] = {1.0f, 0.74f, 0.55f, 0.41f, 0.3f};
        float since = stageSinceBeat_;
        for (int ci = cols - 1; ci >= 0; --ci) {
            float s = colDepth[ci];
            float yTop = vp.y + 0.62f * s;
            float yBot = std::max(crowdTop, depthY(-1.0f, s));
            float delay = ci * 0.055f;
            float bloom = since >= delay ? std::exp(-(since - delay) * 4.5f) * stageHit_ : 0.0f;
            float lvCol = (0.6f + 0.4f * s);
            for (int side = -1; side <= 1; side += 2) {
                float x = side * W * s;
                // Column (dim steel with braces).
                float tw = 0.045f * s;
                RGBA steel{0.42f, 0.45f, 0.55f, 0.22f + 0.2f * stageWhite_ + 0.15f * bloom};
                g.line({x - tw * 0.5f, yBot}, {x - tw * 0.5f, yTop}, 0.004f * s, steel);
                g.line({x + tw * 0.5f, yBot}, {x + tw * 0.5f, yTop}, 0.004f * s, steel);
                float step = 0.06f * s;
                int zig = 0;
                for (float y = yBot; y + step < yTop; y += step, ++zig) {
                    float xa = x + (zig & 1 ? 0.5f : -0.5f) * tw, xb = x + (zig & 1 ? -0.5f : 0.5f) * tw;
                    g.line({xa, y}, {xb, y + step}, 0.0022f * s, RGBA{steel.r, steel.g, steel.b, steel.a * 0.7f});
                }
                // Blinder panels down the column.
                for (int b = 0; b < 2; ++b) {
                    float y = yBot + (yTop - yBot) * (0.35f + 0.3f * b);
                    float lv = 0.2f + 0.8f * stageBlinder_ + 0.8f * stageWhite_;
                    float hw = 0.012f * s, hh = 0.02f * s;
                    g.rect(x - hw, y - hh, x + hw, y + hh, rgba(mixRgb(fixC, white, 0.4f), std::min(1.0f, lv)));
                    g.softDot({x, y}, 0.08f * s * (1 + stageBlinder_), rgba(fixC, 0.2f * lv), 14);
                }
                // Two fans per column: a tall one from the top (the vault) and a smaller one partway
                // down (the lower arches).
                for (int h = 0; h < 2; ++h) {
                    V2 o{x, h == 0 ? yTop : yBot + (yTop - yBot) * 0.42f};
                    int n = h == 0 ? 15 : 9;
                    float open = 0.75f + 0.2f * stageEnergy_ + 0.4f * bloom;      // half-width of the fan (radians)
                    float tilt = 0.12f * std::sin(stageSweep_ * 0.9f + ci * 0.6f);  // gentle sway, mirrored
                    float len = (h == 0 ? 0.85f : 0.5f) * s * (0.9f + 0.25f * bloom);
                    float lv = beamLevel * lvCol * (0.22f + 0.4f * stageEnergy_ + 0.75f * bloom + 0.3f * stageWhite_);
                    if (h == 1) lv *= 0.7f;
                    for (int j = 0; j < n; ++j) {
                        float u = (float)j / (n - 1) * 2 - 1;                       // -1 outward .. 1 inward
                        float ang = kPi * 0.5f - side * (u * open + tilt);
                        V2 dir{std::cos(ang), std::sin(ang)};
                        float L = len * (1.0f - 0.18f * std::fabs(u));
                        // Beams leaning toward the middle run on until they cross the centre line, where
                        // they meet the opposite column's fan and close the arch.
                        if (dir.x * side < -0.05f) L = std::min(1.9f * s, std::fabs(o.x) / std::fabs(dir.x) * 1.12f);
                        if (dir.y < -0.05f) L = std::min(L, (o.y - crowdTop) / -dir.y);
                        V2 e = o + dir * L;
                        V2 nrm{-dir.y, dir.x};
                        float we = 0.035f * s + 0.03f * s * bloom;
                        float edge = 1.0f - 0.35f * std::fabs(u);
                        RGBA c0 = rgba(beamC, std::min(0.85f, 0.34f * lv * edge)), c1 = rgba(beamC, 0);
                        g.triangle(o, c0, e + nrm * we, c1, e - nrm * we, c1);
                        g.line(o, o + dir * (L * 0.6f), 0.0025f * s + 0.001f, rgba(mixRgb(beamC, white, 0.55f), 0.32f * lv * edge));
                    }
                    g.softDot(o, 0.05f * s * (1 + bloom), rgba(mixRgb(fixC, white, 0.5f), std::min(1.0f, 0.5f * lv + 0.3f)), 10);
                }
            }
        }

        // 5. Lasers: a fan from the stage deck that fires on every beat and fills in with the hi-hats.
        float laser = std::min(1.3f, stageLaser_ + laserSustain * (0.25f + 0.6f * stageEnergy_));
        if (laser > 0.02f) {
            V2 o{0, floorY + 0.02f};
            int n = 10 + (int)(std::min(1.0f, treb) * 26);
            float fanW = stageLaserFlip_ ? 0.78f : 0.52f;
            float rot = 0.22f * std::sin(stageSweep_ * 1.3f);
            for (int i = 0; i < n; ++i) {
                float u = n > 1 ? (float)i / (n - 1) : 0.5f;
                float ang = kPi * 0.5f + (u - 0.5f) * 2 * fanW + rot;
                V2 dir{std::cos(ang), std::sin(ang)};
                Rgb c = (i % 3 == 1) ? lasB : lasA;
                float flick = 0.75f + 0.25f * std::sin(f.t * 37 + i * 2.1f);
                float lv = laser * flick;
                V2 e = o + dir * 3.0f;
                g.line(o, e, 0.014f, rgba(c, 0.07f * lv));
                g.line(o, e, 0.0026f, rgba(mixRgb(c, white, 0.35f), std::min(1.0f, 0.85f * lv)));
            }
            // Side lasers from the towers, scanning just over the crowd's heads.
            float scan = laser * std::min(1.0f, laserSustain * 1.6f);
            if (scan > 0.05f) {
                for (int side = -1; side <= 1; side += 2) {
                    V2 so{side * W * 0.72f, vp.y + 0.72f * 0.72f};
                    for (int i = 0; i < 9; ++i) {
                        float u = i / 8.0f;
                        float tx = -side * (a * 0.9f) * (u * 2 - 1) * 0.9f + 0.6f * std::sin(stageSweep_ * 1.7f + side);
                        V2 e{tx, crowdTop + 0.03f + 0.05f * std::sin(stageSweep_ * 2.3f + i)};
                        Rgb c = (i % 2) ? lasB : lasA;
                        g.line(so, e, 0.012f, rgba(c, 0.06f * scan));
                        g.line(so, e, 0.0024f, rgba(mixRgb(c, white, 0.3f), std::min(1.0f, 0.8f * scan)));
                    }
                    g.softDot(so, 0.06f, rgba(mixRgb(lasA, white, 0.6f), 0.6f * std::min(1.0f, scan)), 12);
                }
            }
            g.softDot(o, 0.12f + 0.06f * laser, rgba(mixRgb(lasA, white, 0.6f), 0.55f * std::min(1.0f, laser)), 16);
        }

        // 6. The crowd: silhouettes bouncing to the bass, some arms up, phones glowing.
        if (stageCrowd_.empty()) {
            Rng r;
            r.s = 0xC0FFEEu;
            for (int row = 0; row < 3; ++row) {            // 0 = far, 2 = right in front of you
                float head = 0.03f + row * row * 0.017f;
                for (float x = -3.2f; x < 3.2f; x += head * 3.1f * r.range(0.75f, 1.3f)) {
                    CrowdPerson p;
                    p.x = x; p.row = row;
                    p.head = head * r.range(0.85f, 1.15f);
                    p.phase = r.range(0, 6.28f);
                    p.arm = r.next() < (row == 2 ? 0.22f : 0.3f) ? (r.next() < 0.5f ? -1 : 1) : 0;
                    p.phone = p.arm != 0 && r.next() < 0.6f;
                    p.hOff = r.range(-0.02f, 0.02f) * (1 + row);
                    stageCrowd_.push_back(p);
                }
            }
        }
        stageBounce_ += dt * (2.2f + bass * 2.0f);
        RGBA ink{0.006f, 0.006f, 0.012f, 1};
        for (const CrowdPerson& p : stageCrowd_) {
            if (std::fabs(p.x) > a + 0.2f) continue;
            const float rowY[3] = {-0.6f, -0.66f, -0.75f};
            float baseY = rowY[p.row] + p.hOff;
            float hop = std::max(0.0f, std::sin(stageBounce_ * 2.0f + p.phase)) * (0.006f + 0.02f * bass) * (1 + p.row * 0.4f);
            V2 head{p.x, baseY + hop};
            float sh = p.head * 1.9f;
            over.quad({p.x - sh, -1.05f}, {p.x + sh, -1.05f}, {p.x + sh * 0.9f, head.y - p.head * 1.1f},
                      {p.x - sh * 0.9f, head.y - p.head * 1.1f}, ink);
            over.dot(head, p.head, ink);
            if (p.arm != 0) {
                V2 shoulder{p.x + p.arm * sh * 0.75f, head.y - p.head * 1.2f};
                V2 hand{p.x + p.arm * sh * 0.95f + 0.01f * std::sin(stageBounce_ + p.phase), head.y + p.head * 3.2f + hop};
                over.line(shoulder, hand, p.head * 0.55f, ink);
                if (p.phone) {
                    float pw = p.head * 0.55f, ph = p.head * 0.95f;
                    over.rect(hand.x - pw, hand.y, hand.x + pw, hand.y + ph * 2, ink);
                    Rgb sc = mixRgb(haze, white, 0.55f);
                    g.rect(hand.x - pw * 0.8f, hand.y + ph * 0.15f, hand.x + pw * 0.8f, hand.y + ph * 1.85f, rgba(sc, 0.75f));
                }
            }
        }
    }

private:
    struct Rng {
        uint32_t s = 0x12345678u;
        float next() {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float)(s & 0x00FFFFFFu) / (float)0x01000000;
        }
        float range(float lo, float hi) { return lo + (hi - lo) * next(); }
    };
    struct Particle { V2 p, v; float life, maxLife, size, hue; };
    struct Star { float x, y, z, hue; };
    struct Bolt { std::vector<V2> points; float life, maxLife, width, hue; };
    struct RainColumn { float y, speed; int length; uint32_t seed; };

    static constexpr int RidgeRows = 30;
    static constexpr int RidgePoints = 96;

    RainColumn newColumn(float y) {
        return RainColumn{y, rng_.range(0.35f, 0.9f), (int)rng_.range(8, 22), (uint32_t)(rng_.next() * 1000000)};
    }

    static float lengthOf(V2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
    static V2 normalized(V2 v) { float l = std::max(lengthOf(v), 1e-6f); return V2{v.x / l, v.y / l}; }

    std::vector<V2> jagged(V2 a, V2 b, int levels, float roughness) {
        std::vector<V2> pts{a, b};
        float amount = lengthOf(b - a) * roughness;
        for (int l = 0; l < levels; ++l) {
            std::vector<V2> next{pts[0]};
            for (size_t i = 1; i < pts.size(); ++i) {
                V2 p0 = pts[i - 1], p1 = pts[i];
                V2 d = p1 - p0;
                V2 n = normalized(V2{-d.y, d.x});
                next.push_back((p0 + p1) * 0.5f + n * rng_.range(-amount, amount));
                next.push_back(p1);
            }
            pts.swap(next);
            amount *= 0.5f;
        }
        return pts;
    }

    void makeBolt(const FXFrame& f) {
        float a = rng_.range(0, 2 * kPi);
        V2 start = f.logoCenter + V2{std::cos(a), std::sin(a)} * rng_.range(0.03f, 0.12f);
        float a2 = a + rng_.range(-0.35f, 0.35f);
        float reach = rng_.range(0.9f, 1.4f);
        V2 end{std::cos(a2) * reach * f.aspect, std::sin(a2) * reach};
        std::vector<V2> mainPts = jagged(start, end, 6, 0.18f);
        float life = rng_.range(0.18f, 0.35f);
        float hue = f.scene.baseHue + rng_.range(-0.06f, 0.06f);
        bolts_.push_back(Bolt{mainPts, life, life, 1.0f, hue});
        for (int b = 0; b < 2; ++b) {
            int idx = (int)(rng_.range(0.3f, 0.7f) * (mainPts.size() - 1));
            V2 from = mainPts[idx];
            V2 dir = normalized(end - start);
            float sgn = rng_.next() < 0.5f ? -1.0f : 1.0f;
            V2 side{-dir.y * sgn, dir.x * sgn};
            V2 to = from + (dir * 0.3f + side * 0.25f) * rng_.range(0.5f, 1.0f);
            bolts_.push_back(Bolt{jagged(from, to, 4, 0.2f), life * 0.8f, life * 0.8f, 0.6f, hue});
        }
    }

    void glyph(GeometryBuilder& g, V2 c, float size, uint32_t hash, RGBA col) {
        float w = size * 0.32f, h = size * 0.4f;
        const V2 p[6] = {{-w, h}, {w, h}, {-w, 0}, {w, 0}, {-w, -h}, {w, -h}};
        static const int segs[10][2] = {{0, 1}, {2, 3}, {4, 5}, {0, 2}, {2, 4}, {1, 3}, {3, 5}, {0, 3}, {2, 5}, {1, 2}};
        int drawn = 0;
        for (int i = 0; i < 10 && drawn < 4; ++i) {
            if ((hash >> i) & 1u) {
                g.line(c + p[segs[i][0]], c + p[segs[i][1]], size * 0.07f, col);
                ++drawn;
            }
        }
        if (drawn == 0) g.line(c + p[0], c + p[5], size * 0.07f, col);
    }

    Rng rng_;
    int lastBeat_ = 0;
    bool beatFired_ = false;
    float beatStrength_ = 0;
    std::vector<Particle> sparks_, swarm_, embers_;
    std::vector<Star> stars_;
    float warpBoost_ = 0;
    std::vector<Bolt> bolts_;
    std::vector<std::vector<float>> ridges_;
    float ridgeTimer_ = 0;
    std::vector<RainColumn> rain_;
    float gridPhase_ = 0;
    float lavaTime_ = 0;
    std::vector<float> ledPeaks_, ledHold_, capY_, capV_;
    float tessAngles_[4] = {0.3f, 0.8f, 0.2f, 0.5f};
    float tessKick_ = 0;
    struct CrowdPerson { float x; int row; float head, phase; int arm; bool phone; float hOff; };
    int stageBeats_ = 0, stageCurLook_ = 0, stagePrevLook_ = 0;
    float stageLookMix_ = 1, stageBlinder_ = 0, stageLaser_ = 0, stageWhite_ = 0, stageWhiteCool_ = 0;
    float stageSinceBeat_ = 10, stageHit_ = 0;
    float stageEnergy_ = 0, stageSweep_ = 0, stageChase_ = 0, stageBounce_ = 0;
    bool stageLaserFlip_ = false;
    std::vector<CrowdPerson> stageCrowd_;
};


// `g` is blended additively (glowing light); `over` is drawn first with normal alpha blending.
static void drawWave(const VisualScene& scene, const AudioSnapshot& audio, const Spectrum& sp, float t, float dt,
                     V2 logoCenter, float logoRadius, StyleFX& fx, GeometryBuilder& g, GeometryBuilder& over) {
    float alpha = 0.55f + 0.45f * std::min(1.0f, std::max(std::min(audio.bass, 1.4f), audio.mid));
    float hue0 = scene.baseHue + t * scene.hueSpeed;
    const float* bands = sp.bands;
    const int bandCount = Spectrum::BandCount;
    float aspect = g.aspect;

    FXFrame frame{scene, audio, bands, bandCount, t, dt, aspect, logoCenter, logoRadius,
                  Palette{hue0, scene.saturation, alpha}};
    fx.begin(frame);
    // Circle, lines, bars and dots are expanded in vs_signal. The CPU only uploads samples.
    switch (scene.wave) {
    case WaveStyle::Circle:
    case WaveStyle::TwinRings:
    case WaveStyle::Line:
    case WaveStyle::VerticalLine:
    case WaveStyle::Bars:
    case WaveStyle::RadialBars:
    case WaveStyle::SpiralDots:
    case WaveStyle::Lissajous:
        return;
    default:
        break;
    }

    switch (scene.wave) {
    case WaveStyle::BassRing: fx.bassRing(frame, g); break;
    case WaveStyle::NeonEqualizer: fx.neonEqualizer(frame, g); break;
    case WaveStyle::PulsarRidges: fx.pulsarRidges(frame, over); break;
    case WaveStyle::OutrunGrid: fx.outrunGrid(frame, g, over); break;
    case WaveStyle::Hyperspace: fx.hyperspace(frame, g); break;
    case WaveStyle::ParticleStorm: fx.particleStorm(frame, g); break;
    case WaveStyle::Lightning: fx.lightning(frame, g); break;
    case WaveStyle::DigitalRain: fx.digitalRain(frame, g); break;
    case WaveStyle::LavaLamp: fx.lavaLamp(frame, g); break;
    case WaveStyle::Inferno: fx.inferno(frame, g); break;
    case WaveStyle::MirrorEqualizer: fx.mirrorEqualizer(frame, g); break;
    case WaveStyle::LedMeter: fx.ledMeter(frame, g); break;
    case WaveStyle::RainbowPeaks: fx.rainbowPeaks(frame, g); break;
    case WaveStyle::Tesseract: fx.tesseract(frame, g); break;
    case WaveStyle::FullKaleidoscope: fx.fullKaleidoscope(frame, g); break;
    case WaveStyle::MainStage: fx.mainStage(frame, g, over); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------------------------

Renderer::Renderer() : fx_(std::make_unique<StyleFX>()) {}
Renderer::~Renderer() = default;

bool Renderer::compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& out, std::wstring& error) {
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "shaders.hlsl", nullptr, nullptr, entry,
                            profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
    if (FAILED(hr)) {
        std::string msg = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize())
                                 : std::string("unknown error");
        error = L"Shader " + widen(entry) + L" failed to compile: " + widen(msg);
        return false;
    }
    return true;
}

struct SignalCB {
    float aspect = 0, radius = 0, amplitude = 0, rotation = 0;
    float halfWidth = 0, hue0 = 0, saturation = 0, alpha = 0;
    float p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    int mode = 0, count = 0, stride = 0, sampleOffset = 0;
    int closed = 0, side = 0, pad0 = 0, pad1 = 0;
};
static_assert(sizeof(SignalCB) == 80, "SignalCB must match the HLSL cbuffer");

bool Renderer::init(ID3D11Device* device, ID3D11DeviceContext* ctx, std::wstring& error) {
    device_ = device;
    ctx_ = ctx;

    ComPtr<ID3DBlob> b;
    if (!compile("vs_fullscreen", "vs_5_0", b, error)) return false;
    device_->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsFullscreen_);
    if (!compile("vs_logo", "vs_5_0", b, error)) return false;
    device_->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsLogo_);
    if (!compile("vs_geom", "vs_5_0", b, error)) return false;
    device_->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsGeom_);
    if (!compile("vs_signal", "vs_5_0", b, error)) return false;
    device_->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsSignal_);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layout, 2, b->GetBufferPointer(), b->GetBufferSize(), &geomLayout_);

    if (!compile("fs_warp", "ps_5_0", b, error)) return false;
    device_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psWarp_);
    if (!compile("fs_geom", "ps_5_0", b, error)) return false;
    device_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psGeom_);
    if (!compile("fs_present", "ps_5_0", b, error)) return false;
    device_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psPresent_);
    if (!compile("fs_logo", "ps_5_0", b, error)) return false;
    device_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psLogo_);

    auto makeCB = [&](UINT size, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = size;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&d, nullptr, &out);
    };
    makeCB(sizeof(WarpCB), warpCB_);
    makeCB(sizeof(PresentCB), presentCB_);
    makeCB(sizeof(LogoCB), logoCB_);
    makeCB(sizeof(SignalCB), signalCB_);
    {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = 560 * (UINT)sizeof(float);
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&d, nullptr, &sampleBuf_);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = 560;
        if (sampleBuf_) device_->CreateShaderResourceView(sampleBuf_.Get(), &sv, &sampleSRV_);
    }

    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = sizeof(GeomVertex) * MaxVertices;
    vb.Usage = D3D11_USAGE_DYNAMIC;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&vb, nullptr, &vertexBuffer_);

    auto makeBlend = [&](int mode, ComPtr<ID3D11BlendState>& out) {
        D3D11_BLEND_DESC d{};
        auto& rt = d.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (mode != 0) {
            rt.BlendEnable = TRUE;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlend = mode == 1 ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
            rt.DestBlendAlpha = mode == 1 ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
        }
        device_->CreateBlendState(&d, &out);
    };
    makeBlend(0, blendNone_);
    makeBlend(1, blendAdd_);
    makeBlend(2, blendPremul_);

    auto makeSampler = [&](D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE mode, ComPtr<ID3D11SamplerState>& out) {
        D3D11_SAMPLER_DESC d{};
        d.Filter = filter;
        d.AddressU = d.AddressV = d.AddressW = mode;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        // BorderColor defaults to 0,0,0,0 = transparent black.
        device_->CreateSamplerState(&d, &out);
    };
    makeSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, sampZero_);
    makeSampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, sampClamp_);
    makeSampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_BORDER, sampMip_);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rd, &raster_);

    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcStart_);
    qpcLast_ = qpcStart_;

    current_ = previous_ = builtInScenes()[0];
    setItems({});
    return true;
}

void Renderer::setItems(const std::vector<LogoImage>& images) {
    if (!device_) return;
    std::vector<LogoImage> list;
    for (const LogoImage& img : images)
        if (img.width > 0 && img.height > 0 && (int)list.size() < kMaxOverlayItems) list.push_back(img);
    if (list.empty()) list.push_back(placeholderLogo());
    int count = 0;
    for (const LogoImage& img : list) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = img.width;
        d.Height = img.height;
        d.MipLevels = 0; // full chain
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device_->CreateTexture2D(&d, nullptr, &tex))) continue;
        ctx_->UpdateSubresource(tex.Get(), 0, nullptr, img.rgba.data(), img.width * 4, 0);
        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) continue;
        ctx_->GenerateMips(srv.Get());
        logoTex_[count] = tex;
        logoSRV_[count] = srv;
        logoAspect_[count] = (float)img.width / (float)img.height;
        ++count;
    }
    for (int i = count; i < kMaxOverlayItems; ++i) {
        logoTex_[i].Reset();
        logoSRV_[i].Reset();
    }
    itemCount_ = count;
}

void Renderer::ensureFeedback(int width, int height) {
    // Cap the feedback buffer so 4K screens stay smooth; it's upscaled when presented.
    float scale = std::min(1.0f, 1920.0f / (float)std::max(width, height));
    int w = std::max(16, (int)(width * scale));
    int h = std::max(16, (int)(height * scale));
    if (w == fbWidth_ && h == fbHeight_ && feedback_[0].tex) return;

    for (auto& t : feedback_) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        t = Target{};
        device_->CreateTexture2D(&d, nullptr, &t.tex);
        if (!t.tex) continue;
        device_->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv);
        device_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv);
        const float black[4] = {0, 0, 0, 1};
        if (t.rtv) ctx_->ClearRenderTargetView(t.rtv.Get(), black);
    }
    fbWidth_ = w;
    fbHeight_ = h;
    feedbackIndex_ = 0;
}

VisualScene Renderer::blendedScene() const {
    if (sceneBlend_ >= 1) return current_;
    float k = sceneBlend_ * sceneBlend_ * (3 - 2 * sceneBlend_);
    const VisualScene& a = previous_;
    const VisualScene& b = current_;
    auto mix = [k](float x, float y) { return x + (y - x) * k; };
    VisualScene s = k < 0.5f ? a : b;
    s.zoom = mix(a.zoom, b.zoom);
    s.zoomBass = mix(a.zoomBass, b.zoomBass);
    s.rotation = mix(a.rotation, b.rotation);
    s.rotationBass = mix(a.rotationBass, b.rotationBass);
    s.warpAmount = mix(a.warpAmount, b.warpAmount);
    s.warpScale = mix(a.warpScale, b.warpScale);
    s.warpSpeed = mix(a.warpSpeed, b.warpSpeed);
    s.decay = mix(a.decay, b.decay);
    s.driftX = mix(a.driftX, b.driftX);
    s.driftY = mix(a.driftY, b.driftY);
    s.hueShift = mix(a.hueShift, b.hueShift);
    s.plasma = mix(a.plasma, b.plasma);
    s.generative = mix(a.generative, b.generative);
    s.vignette = mix(a.vignette, b.vignette);
    s.saturation = mix(a.saturation, b.saturation);
    s.centerRadius = mix(a.centerRadius, b.centerRadius);
    return s;
}

void Renderer::updateScene(double now, float dt, RenderSettings& st) {
    const auto& scenes = builtInScenes();
    const int n = (int)scenes.size();
    int requested = ((st.sceneIndex % n) + n) % n;
    if (st.autoCycle && now - sceneStart_ > std::max(5.0f, st.cycleSeconds)) {
        requested = nextCycleIndex(requested, n, st.cycleSkip); // nothing checked -> use every style
        sceneStart_ = now;
        st.sceneIndex = requested;
    }
    if (requested != activeScene_) {
        previous_ = activeScene_ < 0 ? scenes[requested] : blendedScene();
        current_ = scenes[requested];
        sceneBlend_ = activeScene_ < 0 ? 1.0f : 0.0f;
        activeScene_ = requested;
        sceneStart_ = now;
    }
    sceneBlend_ = std::min(1.0f, sceneBlend_ + dt / 2.5f);
}

void Renderer::drawGpuSignal(const VisualScene& scene, const AudioSnapshot& audio, float t, float aspect) {
    if (!vsSignal_ || !sampleSRV_) return;
    switch (scene.wave) {
    case WaveStyle::Circle:
    case WaveStyle::TwinRings:
    case WaveStyle::Line:
    case WaveStyle::VerticalLine:
    case WaveStyle::Bars:
    case WaveStyle::RadialBars:
    case WaveStyle::SpiralDots:
    case WaveStyle::Lissajous:
        break;
    default:
        return;
    }

    float packed[560] = {};
    for (int i = 0; i < Spectrum::WaveCount; ++i) packed[i] = spectrum_.wave[i];
    for (int i = 0; i < Spectrum::BandCount; ++i) packed[512 + i] = spectrum_.bands[i];
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx_->Map(sampleBuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, packed, sizeof(packed));
    ctx_->Unmap(sampleBuf_.Get(), 0);

    const float bass = std::min(audio.bass, 1.4f);
    const float width = 0.0065f * scene.thickness * (1 + bass * 0.6f);
    const float alpha = 0.55f + 0.45f * std::min(1.0f, std::max(bass, audio.mid));
    const float hue0 = scene.baseHue + t * scene.hueSpeed;
    const float scale = scene.waveScale;

    auto issue = [&](SignalCB cb, GpuSignalMode mode, int count, bool closed) {
        const int verts = gpuSignalVertices(mode, count, closed);
        if (verts <= 0) return;
        cb.mode = (int)mode;
        cb.count = count;
        cb.closed = closed ? 1 : 0;
        updateCB(ctx_, signalCB_.Get(), cb);
        ID3D11Buffer* buf = signalCB_.Get();
        ctx_->VSSetConstantBuffers(3, 1, &buf);
        ctx_->VSSetShader(vsSignal_.Get(), nullptr, 0);
        ctx_->PSSetShader(psGeom_.Get(), nullptr, 0);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->OMSetBlendState(blendAdd_.Get(), nullptr, 0xffffffff);
        ID3D11ShaderResourceView* srv = sampleSRV_.Get();
        ctx_->VSSetShaderResources(1, 1, &srv);
        ctx_->Draw(verts, 0);
    };

    SignalCB base{};
    base.aspect = aspect;
    base.hue0 = hue0;
    base.saturation = scene.saturation;
    base.alpha = alpha;
    base.halfWidth = width * 0.5f;

    if (scene.wave == WaveStyle::Circle || scene.wave == WaveStyle::TwinRings) {
        SignalCB cb = base;
        cb.radius = scene.centerRadius * scale * (1 + (scene.wave == WaveStyle::Circle ? 0.25f : 0.3f) * bass);
        cb.amplitude = (scene.wave == WaveStyle::Circle ? 0.12f : 0.07f) * scale;
        cb.rotation = t * (scene.wave == WaveStyle::Circle ? 0.2f : 0.5f);
        cb.stride = 2;
        issue(cb, GpuSignalMode::Polyline, 256, true);
        if (scene.wave == WaveStyle::TwinRings) {
            cb.radius = std::max(0.42f * scale, scene.centerRadius * scale + 0.22f) * (1 + 0.15f * bass);
            cb.amplitude = 0.1f * scale;
            cb.rotation = -t * 0.4f;
            cb.sampleOffset = 1;
            cb.hue0 = hue0 + 0.5f * 0.35f;
            issue(cb, GpuSignalMode::Polyline, 256, true);
        }
    } else if (scene.wave == WaveStyle::Line) {
        SignalCB cb = base;
        cb.p0 = -0.94f * aspect;
        cb.p1 = 1.88f * aspect;
        cb.p2 = 0.3f * scale;
        cb.stride = 2;
        issue(cb, GpuSignalMode::Polyline, 256, false);
    } else if (scene.wave == WaveStyle::VerticalLine) {
        for (int side : {-1, 1}) {
            SignalCB cb = base;
            cb.p0 = -0.94f;
            cb.p1 = 1.88f;
            cb.p2 = 0.35f * aspect * 0.5f;
            cb.p3 = 0.22f * scale;
            cb.stride = 2;
            cb.side = side;
            if (side > 0) cb.hue0 = hue0 + 0.5f * 0.35f;
            issue(cb, GpuSignalMode::Polyline, 256, false);
        }
    } else if (scene.wave == WaveStyle::Bars) {
        SignalCB cb = base;
        cb.alpha = alpha * 0.9f;
        cb.p0 = 1.9f * aspect;
        cb.p1 = 1.15f * scale;
        issue(cb, GpuSignalMode::Bars, 48, false);
    } else if (scene.wave == WaveStyle::RadialBars) {
        const float inner = scene.centerRadius > 0 ? scene.centerRadius + 0.06f * bass : 0.0f;
        for (int side = 0; side < 2; ++side) {
            SignalCB cb = base;
            cb.radius = inner;
            cb.p1 = 0.65f * scale;
            cb.rotation = t * 0.1f;
            cb.halfWidth = width * 1.2f * 0.5f;
            cb.side = side;
            issue(cb, GpuSignalMode::Radial, 48, false);
        }
    } else if (scene.wave == WaveStyle::Lissajous) {
        SignalCB cb = base;
        cb.p0 = 0.5f * scale;
        cb.halfWidth = width * 0.8f * 0.5f;
        issue(cb, GpuSignalMode::Lissajous, (512 - 23) / 2, false);
    } else if (scene.wave == WaveStyle::SpiralDots) {
        SignalCB cb = base;
        cb.p0 = scene.centerRadius;
        cb.p1 = 0.85f * scale;
        cb.p2 = 0.032f * scene.thickness;
        cb.rotation = t * 1.2f;
        issue(cb, GpuSignalMode::Dots, 144, false);
    }

    ID3D11ShaderResourceView* none = nullptr;
    ctx_->VSSetShaderResources(1, 1, &none);
}

void Renderer::render(ID3D11RenderTargetView* target, int width, int height, AudioAnalysis& analysis,
                      double sampleRate, RenderSettings& st) {
    if (width <= 0 || height <= 0) return;
    const ContentRect frame = fitContentRect((float)width, (float)height, st.portrait);
    const int contentW = std::max(1, (int)std::lround(frame.width));
    const int contentH = std::max(1, (int)std::lround(frame.height));
    ensureFeedback(contentW, contentH);
    if (!feedback_[0].rtv || !feedback_[1].rtv) return;

    LARGE_INTEGER nowQ;
    QueryPerformanceCounter(&nowQ);
    float dt = (float)((double)(nowQ.QuadPart - qpcLast_.QuadPart) / qpcFreq_.QuadPart);
    qpcLast_ = nowQ;
    double now = (double)(nowQ.QuadPart - qpcStart_.QuadPart) / qpcFreq_.QuadPart;
    float t = (float)now;

    // Audio: newest features right before drawing.
    AudioSnapshot audio = analysis.snapshot();
    spectrum_.update(analysis, sampleRate);

    updateScene(now, dt, st);
    VisualScene scene = blendedScene();

    if (audio.beatCount != lastFlashBeat_) {
        if (lastFlashBeat_ != 0) flash_ = std::max(flash_, audio.beatStrength);
        lastFlashBeat_ = audio.beatCount;
    }
    flash_ *= std::pow(0.02f, dt);

    const float aspect = frame.height > 0.5f ? frame.width / frame.height : 1.0f;
    Target& src = feedback_[feedbackIndex_];
    Target& dst = feedback_[1 - feedbackIndex_];

    ID3D11ShaderResourceView* nullSRV[1] = {nullptr};
    ID3D11SamplerState* samplers[3] = {sampZero_.Get(), sampClamp_.Get(), sampMip_.Get()};
    ctx_->PSSetSamplers(0, 3, samplers);
    ctx_->RSSetState(raster_.Get());
    ctx_->PSSetShaderResources(0, 1, nullSRV);

    // ---- Pass 1: warp previous frame into dst ----
    D3D11_VIEWPORT vp{0, 0, (float)fbWidth_, (float)fbHeight_, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* dstRTV = dst.rtv.Get();
    ctx_->OMSetRenderTargets(1, &dstRTV, nullptr);
    ctx_->OMSetBlendState(blendNone_.Get(), nullptr, 0xffffffff);

    float frames = std::max(0.25f, std::min(4.0f, dt * 60));
    WarpCB wu{};
    wu.aspect = aspect;
    wu.time = t;
    wu.zoom = std::pow(scene.zoom + scene.zoomBass * audio.bass, frames);
    wu.rotation = (scene.rotation + scene.rotationBass * audio.bass) * frames;
    wu.warpAmount = scene.warpAmount * (1 + audio.mid * 0.8f);
    wu.warpScale = scene.warpScale;
    wu.warpSpeed = scene.warpSpeed;
    wu.decay = std::pow(scene.decay, frames);
    wu.driftX = scene.driftX * frames;
    wu.driftY = scene.driftY * frames;
    wu.kaleido = scene.kaleido;
    wu.hueShift = scene.hueShift * frames;
    wu.plasma = scene.plasma;
    wu.bass = audio.bass;
    wu.mid = audio.mid;
    wu.treble = audio.treble;
    wu.texelX = 1.0f / fbWidth_;
    wu.texelY = 1.0f / fbHeight_;
    wu.mirror = scene.mirror;
    wu.generative = scene.generative;
    updateCB(ctx_, warpCB_.Get(), wu);

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vsFullscreen_.Get(), nullptr, 0);
    ctx_->PSSetShader(psWarp_.Get(), nullptr, 0);
    ID3D11Buffer* cb0 = warpCB_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &cb0);
    ID3D11ShaderResourceView* srcSRV = src.srv.Get();
    ctx_->PSSetShaderResources(0, 1, &srcSRV);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 1, nullSRV);

    // ---- Logo and text item poses: a row in landscape, a column in portrait ----
    Rgb col = hsv(scene.baseHue + t * scene.hueSpeed + 0.5f, 0.8f, 1);
    const int items = std::max(1, itemCount_);
    LogoCB lus[kMaxOverlayItems]{};
    float firstRadius = 0;
    for (int i = 0; i < items; ++i) {
        ItemSlot slot = itemSlot(i, items, aspect);
        ItemExtent ext = itemExtent(logoAspect_[i], st.logoSize, slot);
        LogoPose pose = motion_[i].update(dt, audio, st, motionVariant(i));
        LogoCB& lu = lus[i];
        lu.hx = ext.halfW * pose.scaleX;
        lu.hy = ext.halfH * pose.scaleY;
        lu.cx = slot.cx + pose.x;
        lu.cy = slot.cy + pose.y - 0.08f * st.bounce;
        lu.rotation = pose.rotation;
        lu.aspect = aspect;
        lu.r = col.r; lu.g = col.g; lu.b = col.b;
        if (i == 0) firstRadius = std::max(ext.halfW, ext.halfH) * 0.9f;
    }

    // ---- Pass 2: shapes (solid "over" shapes first, then glowing additive ones) ----
    D3D11_MAPPED_SUBRESOURCE mapped;
    int additiveCount = 0, overCount = 0;
    if (SUCCEEDED(ctx_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        GeomVertex* verts = (GeomVertex*)mapped.pData;
        GeometryBuilder g{verts, AdditiveCapacity, aspect};
        GeometryBuilder over{verts + AdditiveCapacity, MaxVertices - AdditiveCapacity, aspect};
        // Styles built around the logo (Bass Ring, Particle Storm, Lightning) follow the first item.
        float logoRadius = st.showLogo ? firstRadius : 0.0f;
        drawWave(scene, audio, spectrum_, t, dt, V2{lus[0].cx, lus[0].cy}, logoRadius, *fx_, g, over);
        additiveCount = g.count;
        overCount = over.count;
        ctx_->Unmap(vertexBuffer_.Get(), 0);
    }
    if (additiveCount > 0 || overCount > 0) {
        UINT stride = sizeof(GeomVertex), offset = 0;
        ID3D11Buffer* vbuf = vertexBuffer_.Get();
        ctx_->IASetInputLayout(geomLayout_.Get());
        ctx_->IASetVertexBuffers(0, 1, &vbuf, &stride, &offset);
        ctx_->VSSetShader(vsGeom_.Get(), nullptr, 0);
        ctx_->PSSetShader(psGeom_.Get(), nullptr, 0);
        if (overCount > 0) {
            ctx_->OMSetBlendState(blendPremul_.Get(), nullptr, 0xffffffff);
            ctx_->Draw(overCount, AdditiveCapacity);
        }
        if (additiveCount > 0) {
            ctx_->OMSetBlendState(blendAdd_.Get(), nullptr, 0xffffffff);
            ctx_->Draw(additiveCount, 0);
        }
    }
    drawGpuSignal(scene, audio, t, aspect);

    ID3D11Buffer* cb2 = logoCB_.Get();
    ID3D11ShaderResourceView* logoSRV = nullptr;
    auto drawLogo = [&](const LogoCB& data, ID3D11BlendState* blend) {
        updateCB(ctx_, logoCB_.Get(), data);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx_->VSSetShader(vsLogo_.Get(), nullptr, 0);
        ctx_->PSSetShader(psLogo_.Get(), nullptr, 0);
        ctx_->VSSetConstantBuffers(2, 1, &cb2);
        ctx_->PSSetConstantBuffers(2, 1, &cb2);
        ctx_->PSSetShaderResources(0, 1, &logoSRV);
        ctx_->OMSetBlendState(blend, nullptr, 0xffffffff);
        ctx_->Draw(4, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    };

    if (st.showLogo && st.logoTrails) {
        for (int i = 0; i < items; ++i) {
            logoSRV = logoSRV_[i].Get();
            if (!logoSRV) continue;
            LogoCB tu = lus[i];
            tu.opacity = 0.18f;
            tu.tintAmount = 0.6f;
            drawLogo(tu, blendPremul_.Get());
        }
    }

    // ---- Pass 3: composite to the screen. Portrait is a centered 9:16 viewport; bars stay black. ----
    const float black[4] = {0, 0, 0, 1};
    ctx_->ClearRenderTargetView(target, black);
    D3D11_VIEWPORT svp{frame.x, frame.y, std::max(frame.width, 1.0f), std::max(frame.height, 1.0f), 0, 1};
    ctx_->RSSetViewports(1, &svp);
    ctx_->OMSetRenderTargets(1, &target, nullptr);
    ctx_->OMSetBlendState(blendNone_.Get(), nullptr, 0xffffffff);
    PresentCB pu{flash_ * st.beatFlash, scene.vignette, st.brightness, 0};
    updateCB(ctx_, presentCB_.Get(), pu);
    ID3D11Buffer* cb1 = presentCB_.Get();
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vsFullscreen_.Get(), nullptr, 0);
    ctx_->PSSetShader(psPresent_.Get(), nullptr, 0);
    ctx_->PSSetConstantBuffers(1, 1, &cb1);
    ID3D11ShaderResourceView* dstSRV = dst.srv.Get();
    ctx_->PSSetShaderResources(0, 1, &dstSRV);
    ctx_->Draw(3, 0);

    for (int i = 0; st.showLogo && i < items; ++i) {
        logoSRV = logoSRV_[i].Get();
        if (!logoSRV) continue;
        LogoCB& lu = lus[i];
        if (st.glow > 0.01f) {
            LogoCB gu = lu;
            gu.hx *= 1.18f;
            gu.hy *= 1.18f;
            gu.mipBias = 5;
            gu.tintAmount = 1;
            gu.opacity = st.glow * (0.35f + std::min(audio.bass, 1.2f) * 0.9f);
            drawLogo(gu, blendAdd_.Get());
        }
        lu.opacity = 1;
        lu.tintAmount = st.logoTint;
        drawLogo(lu, blendPremul_.Get());
    }

    ctx_->PSSetShaderResources(0, 1, nullSRV);
    // The toolbar is drawn into this same target afterwards, so give it the full window again.
    D3D11_VIEWPORT full{0, 0, (float)width, (float)height, 0, 1};
    ctx_->RSSetViewports(1, &full);
    feedbackIndex_ = 1 - feedbackIndex_;
}
