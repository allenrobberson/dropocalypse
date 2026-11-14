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
    float pad;
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

LogoPose LogoMotion::update(float rawDt, const AudioSnapshot& audio, const RenderSettings& s) {
    float dt = std::min(std::max(rawDt, 0.0f), 1.0f / 20.0f);
    time_ += dt;
    float bass = std::min(audio.bass, 1.4f);

    if (audio.beatCount != lastBeat_) {
        bool fresh = lastBeat_ != 0;
        lastBeat_ = audio.beatCount;
        if (fresh) {
            float st = audio.beatStrength;
            side_ = -side_;
            squash_.v += s.squash * 7.5f * st;
            if (bounceY_ < 0.05f) bounceV_ += s.bounce * 1.8f * st;
            slide_.v += s.slide * 1.6f * st * side_;
            wobble_.v += s.wobble * 4.0f * st * side_;
        }
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
    pose.x = slide_.x * 0.5f + std::sin(time_ * 0.45f) * 0.04f * s.slide;
    pose.rotation = wobble_.x * 0.35f + std::sin(time_ * 0.3f) * 0.02f * s.wobble;
    return pose;
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

        float r = 0.34f * (1 + 0.08f * bass);
        float cy = horizon + 0.3f;
        const int strips = 40;
        for (int k = 0; k < strips; ++k) {
            float frac = (float)k / (strips - 1);
            float yTop = cy + r - k * (2 * r / strips);
            float yBot = yTop - 2 * r / strips;
            if (yBot < horizon) break;
            if (frac > 0.45f && k % 4 == 3) continue;
            if (frac > 0.7f && k % 4 == 2) continue;
            float ym = (yTop + yBot) / 2;
            float hw = std::sqrt(std::max(0.0f, r * r - (ym - cy) * (ym - cy)));
            over.rect(-hw, yBot, hw, yTop, rgba(hsv(0.13f - frac * 0.21f, 0.85f, 1), 1));
        }
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
        int n = f.bandCount;
        for (int i = 0; i < n; ++i) {
            float frac = (float)i / (n - 1);
            float h = f.bands[i] * 0.12f * f.scene.waveScale;
            Rgb c = hsv(0.5f + frac * 0.1f, 0.8f, 1);
            for (float side : {-1.0f, 1.0f}) {
                float x = side * (0.38f + frac * (w - 0.4f));
                g.line({x, horizon}, {x, horizon + h}, 0.006f, rgba(c, 0.8f));
            }
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
};


// `g` is blended additively (glowing light); `over` is drawn first with normal alpha blending.
static void drawWave(const VisualScene& scene, const AudioSnapshot& audio, const Spectrum& sp, float t, float dt,
                     V2 logoCenter, float logoRadius, StyleFX& fx, GeometryBuilder& g, GeometryBuilder& over) {
    float bass = std::min(audio.bass, 1.4f);
    float width = 0.0065f * scene.thickness * (1 + bass * 0.6f);
    float alpha = 0.55f + 0.45f * std::min(1.0f, std::max(bass, audio.mid));
    float hue0 = scene.baseHue + t * scene.hueSpeed;
    auto color = [&](float frac, float boost = 1.0f) {
        Rgb c = hsv(hue0 + frac * 0.35f, scene.saturation, 1);
        return RGBA{c.r, c.g, c.b, std::min(1.0f, alpha * boost)};
    };

    const float* wave = sp.wave;
    const int waveCount = Spectrum::WaveCount;
    const float* bands = sp.bands;
    const int bandCount = Spectrum::BandCount;
    float s = scene.waveScale;
    float aspect = g.aspect;

    FXFrame frame{scene, audio, bands, bandCount, t, dt, aspect, logoCenter, logoRadius,
                  Palette{hue0, scene.saturation, alpha}};
    fx.begin(frame);

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

    case WaveStyle::Circle:
        ring(g, wave, waveCount, 2, 0, scene.centerRadius * s * (1 + 0.25f * bass), 0.12f * s, t * 0.2f, width,
             [&](float f) { return color(f); });
        break;

    case WaveStyle::TwinRings:
        ring(g, wave, waveCount, 2, 0, scene.centerRadius * s * (1 + 0.3f * bass), 0.07f * s, t * 0.5f, width,
             [&](float f) { return color(f); });
        ring(g, wave, waveCount, 2, 1, std::max(0.42f * s, scene.centerRadius * s + 0.22f) * (1 + 0.15f * bass),
             0.1f * s, -t * 0.4f, width, [&](float f) { return color(f + 0.5f); });
        break;

    case WaveStyle::Line: {
        const int n = 256;
        std::vector<V2> pts;
        pts.reserve(n);
        for (int i = 0; i < n; ++i) {
            float x = -0.94f * aspect + (float)i / (n - 1) * 1.88f * aspect;
            pts.push_back({x, wave[i * 2] * 0.3f * s});
        }
        g.polyline(pts, false, width, [&](int i) { return color((float)i / n); });
        break;
    }

    case WaveStyle::VerticalLine: {
        const int n = 256;
        for (float side : {-1.0f, 1.0f}) {
            std::vector<V2> pts;
            pts.reserve(n);
            for (int i = 0; i < n; ++i) {
                float y = -0.94f + (float)i / (n - 1) * 1.88f;
                pts.push_back({side * 0.35f * aspect * 0.5f + wave[i * 2] * 0.22f * s, y});
            }
            g.polyline(pts, false, width, [&](int i) { return color((float)i / n + (side > 0 ? 0.5f : 0.0f)); });
        }
        break;
    }

    case WaveStyle::Bars: {
        float total = 1.9f * aspect;
        float slot = total / bandCount;
        for (int i = 0; i < bandCount; ++i) {
            float x0 = -total / 2 + i * slot + slot * 0.15f;
            float h = 0.02f + bands[i] * 1.15f * s;
            g.rect(x0, -0.98f, x0 + slot * 0.7f, -0.98f + h, color((float)i / bandCount, 0.9f));
        }
        break;
    }

    case WaveStyle::RadialBars: {
        float inner = scene.centerRadius > 0 ? scene.centerRadius + 0.06f * bass : 0.0f;
        for (int side = 0; side < 2; ++side) {
            for (int i = 0; i < bandCount; ++i) {
                float frac = (float)i / bandCount;
                float a = (side == 0 ? frac : -frac) * kPi + kPi / 2 + t * 0.1f;
                V2 dir{std::cos(a), std::sin(a)};
                float len = 0.03f + bands[i] * 0.65f * s;
                g.line(dir * inner, dir * (inner + len), width * 1.2f, color(frac));
            }
        }
        break;
    }

    case WaveStyle::SpiralDots:
        for (int arm = 0; arm < 3; ++arm) {
            for (int i = 0; i < bandCount; ++i) {
                float frac = (float)i / bandCount;
                float a = i * 0.33f + arm * 2 * kPi / 3 + t * 1.2f;
                float r = scene.centerRadius + frac * 0.85f * s;
                float radius = 0.006f + bands[i] * 0.032f * scene.thickness;
                g.dot(V2{std::cos(a), std::sin(a)} * r, radius, color(frac + arm * 0.2f));
            }
        }
        break;

    case WaveStyle::Lissajous: {
        const int delay = 23;
        int n = waveCount - delay;
        std::vector<V2> pts;
        pts.reserve(n / 2);
        for (int i = 0; i < n; i += 2) pts.push_back({wave[i] * 0.5f * s, wave[i + delay] * 0.5f * s});
        int total = (int)pts.size();
        g.polyline(pts, false, width * 0.8f, [&](int i) { return color((float)i / total); });
        break;
    }
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
    setLogo(placeholderLogo());
    return true;
}

void Renderer::setLogo(const LogoImage& img) {
    if (img.width <= 0 || img.height <= 0 || !device_) return;
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
    if (FAILED(device_->CreateTexture2D(&d, nullptr, &tex))) return;
    ctx_->UpdateSubresource(tex.Get(), 0, nullptr, img.rgba.data(), img.width * 4, 0);
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return;
    ctx_->GenerateMips(srv.Get());
    logoTex_ = tex;
    logoSRV_ = srv;
    logoAspect_ = (float)img.width / (float)img.height;
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
        requested = (requested + 1) % n;
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

void Renderer::render(ID3D11RenderTargetView* target, int width, int height, AudioAnalysis& analysis,
                      double sampleRate, RenderSettings& st) {
    if (width <= 0 || height <= 0) return;
    ensureFeedback(width, height);
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

    const float aspect = (float)width / (float)height;
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

    // ---- Logo pose ----
    LogoPose pose = motion_.update(dt, audio, st);
    Rgb col = hsv(scene.baseHue + t * scene.hueSpeed + 0.5f, 0.8f, 1);
    LogoCB lu{};
    float baseH = logoAspect_ >= 1 ? st.logoSize / logoAspect_ : st.logoSize;
    float baseW = baseH * logoAspect_;
    lu.hx = baseW * pose.scaleX;
    lu.hy = baseH * pose.scaleY;
    lu.cx = pose.x;
    lu.cy = pose.y - 0.08f * st.bounce;
    lu.rotation = pose.rotation;
    lu.aspect = aspect;
    lu.r = col.r; lu.g = col.g; lu.b = col.b;

    // ---- Pass 2: shapes (solid "over" shapes first, then glowing additive ones) ----
    D3D11_MAPPED_SUBRESOURCE mapped;
    int additiveCount = 0, overCount = 0;
    if (SUCCEEDED(ctx_->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        GeomVertex* verts = (GeomVertex*)mapped.pData;
        GeometryBuilder g{verts, AdditiveCapacity, aspect};
        GeometryBuilder over{verts + AdditiveCapacity, MaxVertices - AdditiveCapacity, aspect};
        float logoRadius = st.showLogo ? std::max(baseW, baseH) * 0.9f : 0.0f;
        drawWave(scene, audio, spectrum_, t, dt, V2{lu.cx, lu.cy}, logoRadius, *fx_, g, over);
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

    ID3D11Buffer* cb2 = logoCB_.Get();
    ID3D11ShaderResourceView* logoSRV = logoSRV_.Get();
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

    if (st.showLogo && st.logoTrails && logoSRV) {
        LogoCB tu = lu;
        tu.opacity = 0.18f;
        tu.tintAmount = 0.6f;
        drawLogo(tu, blendPremul_.Get());
    }

    // ---- Pass 3: composite to the screen ----
    D3D11_VIEWPORT svp{0, 0, (float)width, (float)height, 0, 1};
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

    if (st.showLogo && logoSRV) {
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
    feedbackIndex_ = 1 - feedbackIndex_;
}
