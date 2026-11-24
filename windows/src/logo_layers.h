// Dropocalypse - splits a logo into layers and moves them (platform-free; the Mac app has the
// same rules in Sources/DropocalypseCore/LogoLayers.swift).
//
// Every piece of the logo that doesn't touch another piece (each letter, icon or shape) becomes
// its own layer. Specks are folded into the nearest layer, and a logo with very many pieces keeps
// its largest ones as layers. The layers then jump on the beat in one of four ways.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

constexpr int kMaxLogoLayers = 24;

struct LogoLayer {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // bounding box in pixels, x1/y1 exclusive
    float cx = 0, cy = 0;               // centroid in pixels
    int area = 0;
};

struct LayerSplit {
    int width = 0, height = 0;
    std::vector<int> label;         // layer of each pixel, -1 = empty
    std::vector<LogoLayer> layers;  // ordered left to right (then top to bottom)
};

namespace logo_layers_detail {
inline int findRoot(std::vector<int>& parent, int a) {
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}
} // namespace logo_layers_detail

/// `rgba` is width*height*4 bytes; only the alpha channel is read.
inline LayerSplit splitLogoLayers(const std::uint8_t* rgba, int width, int height, int maxLayers = kMaxLogoLayers) {
    using logo_layers_detail::findRoot;
    LayerSplit out;
    out.width = width;
    out.height = height;
    const int n = width * height;
    out.label.assign((size_t)std::max(n, 0), -1);
    if (width <= 0 || height <= 0) return out;
    auto alpha = [&](int i) { return (int)rgba[(size_t)i * 4 + 3]; };

    // 1. Solid pixels joined to their 8 neighbours (union-find over provisional labels).
    std::vector<int> prov((size_t)n, -1), parent;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = y * width + x;
            if (alpha(i) < 96) continue;
            int best = -1;
            const int nx[4] = {x - 1, x - 1, x, x + 1}, ny[4] = {y, y - 1, y - 1, y - 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || nx[k] >= width || ny[k] < 0) continue;
                const int l = prov[(size_t)(ny[k] * width + nx[k])];
                if (l < 0) continue;
                if (best < 0) best = l;
                else {
                    int ra = findRoot(parent, best), rb = findRoot(parent, l);
                    if (ra != rb) parent[(size_t)std::max(ra, rb)] = std::min(ra, rb);
                }
            }
            if (best < 0) {
                best = (int)parent.size();
                parent.push_back(best);
            }
            prov[(size_t)i] = best;
        }
    }
    if (parent.empty()) {   // nothing solid: one layer covering the whole image
        out.layers.push_back(LogoLayer{0, 0, width, height, width * 0.5f, height * 0.5f, n});
        for (int i = 0; i < n; ++i) if (alpha(i) > 0) out.label[(size_t)i] = 0;
        return out;
    }

    // 2. Pieces and their sizes.
    std::vector<int> root((size_t)n, -1);
    std::vector<int> area(parent.size(), 0);
    std::vector<double> sx(parent.size(), 0), sy(parent.size(), 0);
    int total = 0;
    for (int i = 0; i < n; ++i) {
        if (prov[(size_t)i] < 0) continue;
        const int r = findRoot(parent, prov[(size_t)i]);
        root[(size_t)i] = r;
        ++area[(size_t)r];
        sx[(size_t)r] += i % width;
        sy[(size_t)r] += i / width;
        ++total;
    }
    std::vector<int> pieces;
    for (size_t r = 0; r < parent.size(); ++r) if (area[r] > 0) pieces.push_back((int)r);
    std::sort(pieces.begin(), pieces.end(), [&](int a, int b) { return area[(size_t)a] > area[(size_t)b]; });

    // 3. Keep the larger pieces; fold specks and extras into the nearest kept piece.
    const int dust = std::max(4, total / 500);
    std::vector<int> kept;
    for (int r : pieces)
        if ((int)kept.size() < std::max(1, maxLayers) && (area[(size_t)r] >= dust || kept.empty())) kept.push_back(r);
    std::vector<int> target(parent.size(), -1);
    for (size_t k = 0; k < kept.size(); ++k) target[(size_t)kept[k]] = (int)k;
    for (int r : pieces) {
        if (target[(size_t)r] >= 0) continue;
        const double px = sx[(size_t)r] / area[(size_t)r], py = sy[(size_t)r] / area[(size_t)r];
        double bestD = 1e30;
        int best = 0;
        for (size_t k = 0; k < kept.size(); ++k) {
            const int q = kept[k];
            const double dx = sx[(size_t)q] / area[(size_t)q] - px, dy = sy[(size_t)q] / area[(size_t)q] - py;
            const double d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = (int)k; }
        }
        target[(size_t)r] = best;
    }
    for (int i = 0; i < n; ++i)
        if (root[(size_t)i] >= 0) out.label[(size_t)i] = target[(size_t)root[(size_t)i]];

    // 4. Soft edges (anti-aliased pixels below the solid threshold) join the layer beside them.
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<int> next = out.label;
        bool changed = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int i = y * width + x;
                if (out.label[(size_t)i] >= 0 || alpha(i) == 0) continue;
                for (int dy = -1; dy <= 1 && next[(size_t)i] < 0; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        const int l = out.label[(size_t)(yy * width + xx)];
                        if (l >= 0) { next[(size_t)i] = l; changed = true; break; }
                    }
            }
        out.label.swap(next);
        if (!changed) break;
    }

    // 5. Boxes and centres; then number the layers left to right.
    const int k = (int)kept.size();
    std::vector<LogoLayer> layers((size_t)k);
    for (auto& l : layers) { l.x0 = width; l.y0 = height; l.x1 = 0; l.y1 = 0; }
    std::vector<double> lx((size_t)k, 0), ly((size_t)k, 0);
    for (int i = 0; i < n; ++i) {
        const int l = out.label[(size_t)i];
        if (l < 0) continue;
        const int x = i % width, y = i / width;
        LogoLayer& L = layers[(size_t)l];
        L.x0 = std::min(L.x0, x); L.y0 = std::min(L.y0, y);
        L.x1 = std::max(L.x1, x + 1); L.y1 = std::max(L.y1, y + 1);
        ++L.area;
        lx[(size_t)l] += x;
        ly[(size_t)l] += y;
    }
    for (int l = 0; l < k; ++l) {
        layers[(size_t)l].cx = layers[(size_t)l].area ? (float)(lx[(size_t)l] / layers[(size_t)l].area) : 0;
        layers[(size_t)l].cy = layers[(size_t)l].area ? (float)(ly[(size_t)l] / layers[(size_t)l].area) : 0;
    }
    std::vector<int> order((size_t)k);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const float ax = layers[(size_t)a].cx, bx = layers[(size_t)b].cx;
        if (std::fabs(ax - bx) > width * 0.02f) return ax < bx;
        return layers[(size_t)a].cy < layers[(size_t)b].cy;
    });
    std::vector<int> rank((size_t)k);
    for (int r = 0; r < k; ++r) rank[(size_t)order[(size_t)r]] = r;
    for (int i = 0; i < n; ++i)
        if (out.label[(size_t)i] >= 0) out.label[(size_t)i] = rank[(size_t)out.label[(size_t)i]];
    for (int r = 0; r < k; ++r) out.layers.push_back(layers[(size_t)order[(size_t)r]]);
    return out;
}

/// One layer's pixels, cropped to its box (with a 1-pixel clear border so edges stay soft).
/// Returns the image size in `w`/`h`.
inline std::vector<std::uint8_t> extractLogoLayer(const std::uint8_t* rgba, const LayerSplit& split, int layer, int& w,
                                                  int& h) {
    const LogoLayer& L = split.layers[(size_t)layer];
    w = L.x1 - L.x0 + 2;
    h = L.y1 - L.y0 + 2;
    std::vector<std::uint8_t> out((size_t)w * h * 4, 0);
    for (int y = L.y0; y < L.y1; ++y)
        for (int x = L.x0; x < L.x1; ++x) {
            const size_t i = (size_t)y * split.width + x;
            if (split.label[i] != layer) continue;
            const size_t o = ((size_t)(y - L.y0 + 1) * w + (x - L.x0 + 1)) * 4;
            for (int c = 0; c < 4; ++c) out[o + c] = rgba[i * 4 + c];
        }
    return out;
}

/// The box `extractLogoLayer` returns, as texture coordinates of the whole image (v down).
inline void logoLayerUV(const LayerSplit& split, int layer, float& u0, float& v0, float& u1, float& v1) {
    const LogoLayer& L = split.layers[(size_t)layer];
    u0 = (L.x0 - 1) / (float)split.width;
    v0 = (L.y0 - 1) / (float)split.height;
    u1 = (L.x1 + 1) / (float)split.width;
    v1 = (L.y1 + 1) / (float)split.height;
}

// ---------------------------------------------------------------------------------------------
// Layer motion

enum LayerMode { kLayersOff = 0, kLayersWave = 1, kLayersExplode = 2, kLayersBands = 3, kLayersScatter = 4 };
constexpr int kLayerModeCount = 5;
inline const char* layerModeName(int m) {
    static const char* names[kLayerModeCount] = {"Off", "Wave", "Explode", "Bands", "Scatter"};
    return names[(m >= 0 && m < kLayerModeCount) ? m : 0];
}

/// Offset of one layer from where it sits in the logo. x and y are in units of the logo's longer
/// half side (y up), rotation in radians, scale 1 = unchanged.
struct LayerPose {
    float x = 0, y = 0, rotation = 0, scale = 1;
};

/// Springs that pull every layer back into place after the beat kicks it.
class LayerMotion {
public:
    /// `nx`/`ny`: each layer's centre relative to the logo's centre, -1..1 (y up).
    void update(float dt, int mode, float amount, const std::vector<float>& nx, const std::vector<float>& ny,
                float bass, float mid, float treble, int beatCount, float beatStrength) {
        const size_t count = nx.size();
        if (state_.size() != count) state_.assign(count, State());
        dt = std::min(std::max(dt, 0.0f), 0.05f);
        time_ += dt;
        if (mode <= kLayersOff || count == 0) {
            for (auto& s : state_) s = State();
            lastBeat_ = beatCount;
            return;
        }
        amount = std::max(0.0f, amount);
        const bool beat = beatCount != lastBeat_ && lastBeat_ != 0;
        lastBeat_ = beatCount;
        const float punch = 0.6f + 0.6f * std::min(beatStrength, 1.0f);

        for (size_t i = 0; i < count; ++i) {
            State& s = state_[i];
            if (beat) {
                switch (mode) {
                case kLayersWave: // left to right, like a stadium wave
                    s.kickAt = time_ + (nx[i] + 1) * 0.5f * 0.32f;
                    s.kick = {0, 5.0f * amount * punch, 0, 0.8f * amount * punch};
                    s.pending = true;
                    break;
                case kLayersExplode: { // outward from the centre on bigger hits
                    if (beatStrength < 0.35f) break;
                    float dx = nx[i], dy = ny[i];
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 0.08f) {
                        const float a = hash01((uint32_t)i * 7919u + 13u) * 6.2832f;
                        dx = std::cos(a); dy = std::sin(a); len = 1;
                    }
                    s.vx += dx / len * 7.0f * amount * punch;
                    s.vy += dy / len * 7.0f * amount * punch;
                    s.vr += (hash01((uint32_t)i * 31u + (uint32_t)beatCount) - 0.5f) * 6 * amount;
                    s.vs += 1.2f * amount * punch;
                    break;
                }
                case kLayersScatter: { // every layer its own direction and spin
                    const uint32_t seed = (uint32_t)beatCount * 2654435761u + (uint32_t)i * 40503u;
                    const float a = hash01(seed) * 6.2832f;
                    const float sp = 3.2f + 3.2f * hash01(seed ^ 0x9e3779b9u);
                    s.vx += std::cos(a) * sp * amount * punch;
                    s.vy += std::sin(a) * sp * amount * punch;
                    s.vr += (hash01(seed * 3u + 1u) - 0.5f) * 9 * amount;
                    s.vs += (hash01(seed * 5u + 2u) - 0.3f) * 1.4f * amount;
                    break;
                }
                default: break;
                }
            }
            if (s.pending && time_ >= s.kickAt) {
                s.pending = false;
                s.vx += s.kick.x; s.vy += s.kick.y; s.vr += s.kick.rotation; s.vs += s.kick.scale;
            }

            float tx = 0, ty = 0, ts = 1;
            if (mode == kLayersBands) { // each layer rides its own part of the music
                const float levels[3] = {bass, mid, treble};
                const float level = std::min(levels[i % 3], 1.4f);
                ty = level * 0.35f * amount;
                ts = 1 + level * 0.22f * amount;
            }
            // Damped springs back to the resting place (or the band's target).
            const float k = 170, c = 13, kr = 120, cr = 11;
            s.vx += (-k * (s.p.x - tx) - c * s.vx) * dt;
            s.vy += (-k * (s.p.y - ty) - c * s.vy) * dt;
            s.vr += (-kr * s.p.rotation - cr * s.vr) * dt;
            s.vs += (-k * (s.p.scale - ts) - c * s.vs) * dt;
            s.p.x += s.vx * dt;
            s.p.y += s.vy * dt;
            s.p.rotation += s.vr * dt;
            s.p.scale = std::max(0.2f, s.p.scale + s.vs * dt);
        }
    }

    LayerPose pose(size_t i) const { return i < state_.size() ? state_[i].p : LayerPose(); }

private:
    struct State {
        LayerPose p;
        float vx = 0, vy = 0, vr = 0, vs = 0;
        LayerPose kick{0, 0, 0, 0};
        float kickAt = 0;
        bool pending = false;
    };
    static float hash01(uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
        return (x & 0xffffff) / 16777216.0f;
    }
    std::vector<State> state_;
    int lastBeat_ = 0;
    float time_ = 0;
};

/// Where to draw one layer: the item's centre, half size and rotation, the layer's box within the
/// item's image (0..1, v down) and its pose. Same units as the logo shader (height units).
struct LayerQuad {
    float cx, cy, hx, hy, rotation;
};
inline LayerQuad layerQuad(float itemCx, float itemCy, float itemHx, float itemHy, float itemRotation, float u0,
                           float v0, float u1, float v1, const LayerPose& p) {
    const float reach = std::max(itemHx, itemHy);              // moves scale with the item's longer side
    const float lx = ((u0 + u1) - 1) * itemHx + p.x * reach;   // layer centre in the item's frame
    const float ly = (1 - (v0 + v1)) * itemHy + p.y * reach;
    const float c = std::cos(itemRotation), s = std::sin(itemRotation);
    LayerQuad q;
    q.cx = itemCx + c * lx - s * ly;
    q.cy = itemCy + s * lx + c * ly;
    q.hx = (u1 - u0) * itemHx * p.scale;
    q.hy = (v1 - v0) * itemHy * p.scale;
    q.rotation = itemRotation + p.rotation;
    return q;
}

// ---------------------------------------------------------------------------------------------
// Kinetic Text: rows of the first logo or text item sliding across the screen in alternating
// directions; the row the beat lands on lights up. Units are the logo shader's: y -1..1 (up),
// x -aspect..aspect.

constexpr int kKineticRows = 7;

struct KineticRow {
    float y = 0, hx = 0, hy = 0;   // row centre and the half size of each copy
    float x0 = 0, spacing = 0;     // centre of the first copy, distance between copies
    float opacity = 0;
    int copies = 0;
};

inline int kineticRows(KineticRow* rows, float t, float aspect, float itemAspect, int beatCount, float pulse,
                       float bass) {
    aspect = std::max(aspect, 0.1f);
    const float rowH = 2.0f / kKineticRows;
    const int lit = ((beatCount % kKineticRows) + kKineticRows) % kKineticRows;
    for (int r = 0; r < kKineticRows; ++r) {
        KineticRow& row = rows[r];
        float hy = rowH * 0.36f;
        float hx = hy * std::max(itemAspect, 0.2f);
        if (hx > aspect * 0.9f) { // very wide items: shrink to fit the screen
            hy *= aspect * 0.9f / hx;
            hx = aspect * 0.9f;
        }
        const bool isLit = r == lit;
        const float grow = isLit ? 1 + 0.12f * pulse : 1.0f;
        row.hx = hx * grow;
        row.hy = hy * grow;
        row.y = 1 - (r + 0.5f) * rowH;
        row.spacing = hx * 2 + hy * 0.9f;
        const float dir = (r % 2) ? -1.0f : 1.0f;
        const float speed = 0.12f + 0.05f * (r % 3) + 0.08f * std::min(bass, 1.5f);
        const float s = t * speed * dir;
        const float off = s - std::floor(s / row.spacing) * row.spacing;
        row.x0 = -aspect - row.spacing + off;
        row.copies = std::min(40, (int)std::ceil((2 * aspect + 2 * row.spacing) / row.spacing) + 1);
        row.opacity = std::min(1.0f, 0.22f + (isLit ? 0.55f * pulse : 0.0f) + 0.06f * std::min(bass, 1.5f));
    }
    return kKineticRows;
}
