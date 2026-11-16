// Metal shaders, compiled at launch with `MTLDevice.makeLibrary(source:)`.
//
// They live in a Swift string so the app is a single self-contained binary with no resource
// bundle. CI extracts the text between the METAL-BEGIN / METAL-END markers and compiles it
// with `xcrun metal` so shader errors fail the build instead of showing up at runtime.

enum ShaderSource {
    static let metal = #"""
// METAL-BEGIN
#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------- fullscreen pass

struct FSOut {
    float4 position [[position]];
    float2 uv; // (0,0) = top-left
};

vertex FSOut vs_fullscreen(uint vid [[vertex_id]]) {
    float2 pos = float2((vid << 1) & 2, vid & 2);
    FSOut o;
    o.position = float4(pos * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(pos.x, 1.0 - pos.y);
    return o;
}

// ---------------------------------------------------------------- feedback warp (MilkDrop-style)

struct WarpUniforms {
    float aspect;
    float time;
    float zoom;
    float rotation;
    float warpAmount;
    float warpScale;
    float warpSpeed;
    float decay;
    float driftX;
    float driftY;
    float kaleido;
    float hueShift;
    float plasma;
    float bass;
    float mid;
    float treble;
    float texelX;
    float texelY;
    int   mirror;
    float generative; // 0 off, 1 = Full Kaleidoscope field mixed over the feedback
};

// Full Kaleidoscope: a mandala that fills the frame. Four-way mirror always;
// a diagonal fold fades in and out so the same picture sometimes reads as eight-way.
// Bass pulses and turns it. Mids and highs raise the detail.
static float hash21(float2 p) {
    float3 p3 = fract(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

static float vnoise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

static float fbm(float2 p) {
    float v = 0.0;
    float a = 0.5;
    v += a * vnoise(p); p = p * 2.02 + float2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p); p = p * 2.02 + float2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p); p = p * 2.02 + float2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p);
    return v;
}

static float3 neonPalette(float t) {
    t = fract(t) * 5.0;
    float i = floor(t);
    float f = smoothstep(0.0, 1.0, fract(t));
    float3 a = float3(0.0, 0.96, 1.0);
    float3 b = float3(1.0, 0.04, 0.58);
    if (i < 1.0) { a = float3(0.0, 0.96, 1.0); b = float3(1.0, 0.04, 0.58); }
    else if (i < 2.0) { a = float3(1.0, 0.04, 0.58); b = float3(0.12, 0.28, 1.0); }
    else if (i < 3.0) { a = float3(0.12, 0.28, 1.0); b = float3(1.0, 0.68, 0.05); }
    else if (i < 4.0) { a = float3(1.0, 0.68, 0.05); b = float3(0.48, 1.0, 0.10); }
    else { a = float3(0.48, 1.0, 0.10); b = float3(0.0, 0.96, 1.0); }
    return mix(a, b, f);
}

static float3 kaleidoField(float2 uv, float time, float bass, float mid, float treble, float aspect) {
    float2 p = (uv - 0.5) * float2(max(aspect, 0.05), 1.0);
    float spin = time * 0.07 + bass * 0.22;
    float cs = cos(spin);
    float sn = sin(spin);
    p = float2(cs * p.x - sn * p.y, sn * p.x + cs * p.y) / (1.0 + bass * 0.28);

    float2 p4 = abs(p);
    float2 p8 = float2(min(p4.x, p4.y), max(p4.x, p4.y));
    float eight = saturate(0.5 + 0.5 * sin(time * 0.19) + mid * 0.45);
    p = mix(p4, p8, smoothstep(0.2, 0.85, eight));

    float2 q = p;
    float wt = time * (0.32 + mid * 0.25);
    float amp = 0.07 + treble * 0.09;
    q += amp * float2(sin(p.y * (3.1 + mid * 4.0) + wt), cos(p.x * (2.6 + treble * 5.0) - wt * 0.85));

    float detail = 2.1 + mid * 2.8 + treble * 4.2;
    float n = fbm(q * detail + time * 0.06);
    float n2 = fbm(q.yx * (detail * 1.85) - time * 0.09 + 3.7);

    float cells = 3.2 + mid * 4.5 + treble * 3.5;
    float2 cell = abs(fract(q * cells) - 0.5);
    float tile = smoothstep(0.48, 0.16, max(cell.x, cell.y));
    float grid = smoothstep(0.08, 0.0, min(cell.x, cell.y));
    float r = length(p4);
    float rings = pow(abs(sin(r * (7.5 + treble * 10.0) - time * (0.55 + bass * 0.8))), 10.0);

    float hueT = n * 0.62 + n2 * 0.28 + time * 0.02 + r * 0.06 + mid * 0.12;
    float3 col = neonPalette(hueT);
    float3 alt = neonPalette(hueT + 0.38 + treble * 0.1);
    col = mix(col, alt, saturate(tile * 0.9 + rings * 0.65));
    col *= 0.42 + n * 0.85 + n2 * 0.45 + tile * 0.55;
    col += alt * rings * 0.9;
    col += neonPalette(0.02 + bass * 0.04) * grid * 0.35;
    float axis = exp(-min(p4.x, p4.y) * (7.0 - bass * 2.0));
    col += neonPalette(0.52) * axis * (0.22 + bass * 0.18) * (1.0 - eight * 0.65);

    float2 centered = (uv - 0.5) * float2(max(aspect, 0.05), 1.0);
    float edge = length(centered) / max(length(float2(max(aspect, 0.05), 1.0) * 0.5), 0.001);
    col *= mix(1.2, 0.22, smoothstep(0.22, 1.0, edge));
    return col * (0.9 + bass * 0.45);
}

static float3 hueRotate(float3 c, float a) {
    const float3 k = float3(0.57735027);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

fragment float4 fs_warp(FSOut in [[stage_in]],
                        texture2d<float> prev [[texture(0)]],
                        constant WarpUniforms& u [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_zero);

    float2 p = in.uv - 0.5;
    p.x *= u.aspect;

    if (u.mirror == 1) {
        p.x = abs(p.x);
    } else if (u.mirror == 2) {
        p = abs(p);
    }

    if (u.kaleido > 0.5) {
        float r = length(p);
        float a = atan2(p.y, p.x);
        float seg = 6.2831853 / u.kaleido;
        a = fmod(a + 25.1327412, seg);
        a = abs(a - seg * 0.5);
        p = r * float2(cos(a), sin(a));
    }

    float ca = cos(u.rotation);
    float sa = sin(u.rotation);
    p = float2(ca * p.x - sa * p.y, sa * p.x + ca * p.y);
    p /= u.zoom;

    float t = u.time * u.warpSpeed;
    p += u.warpAmount * float2(sin(p.y * u.warpScale + t),
                               cos(p.x * u.warpScale * 1.13 - t * 0.87));
    p -= float2(u.driftX, u.driftY);

    p.x /= u.aspect;
    float2 uv = p + 0.5;

    float2 tx = float2(u.texelX, u.texelY);
    float3 c = prev.sample(s, uv).rgb * 0.6
             + (prev.sample(s, uv + float2(tx.x, 0.0)).rgb
              + prev.sample(s, uv - float2(tx.x, 0.0)).rgb
              + prev.sample(s, uv + float2(0.0, tx.y)).rgb
              + prev.sample(s, uv - float2(0.0, tx.y)).rgb) * 0.1;

    c = hueRotate(c, u.hueShift) * u.decay;

    if (u.plasma > 0.0) {
        float2 q = (in.uv - 0.5) * float2(u.aspect, 1.0) * 3.0;
        float v = sin(q.x + u.time)
                + sin(q.y * 1.3 - u.time * 0.7)
                + sin((q.x + q.y) * 0.8 + u.time * 1.1)
                + sin(length(q) * 2.0 - u.time * 1.5);
        float3 pc = 0.5 + 0.5 * cos(float3(0.0, 2.1, 4.2) + v * 1.2 + u.time * 0.3);
        c += pc * u.plasma * (0.15 + u.bass) * 0.04;
    }

    if (u.generative > 0.001) {
        float3 gen = kaleidoField(in.uv, u.time, u.bass, u.mid, u.treble, u.aspect);
        c = mix(c, max(c, float3(0.0)) * 0.42 + gen, saturate(u.generative));
    }

    return float4(max(c, float3(0.0)), 1.0);
}

// ---------------------------------------------------------------- waveforms, bars, dots

struct GeomVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

struct GeomOut {
    float4 position [[position]];
    float4 color;
};

vertex GeomOut vs_geom(uint vid [[vertex_id]],
                       const device GeomVertex* verts [[buffer(0)]]) {
    GeomVertex g = verts[vid];
    GeomOut o;
    o.position = float4(g.x, g.y, 0.0, 1.0);
    o.color = float4(g.r, g.g, g.b, g.a);
    return o;
}

fragment float4 fs_geom(GeomOut in [[stage_in]]) {
    return float4(in.color.rgb * in.color.a, in.color.a);
}

// ---------------------------------------------------------------- final composite

struct PresentUniforms {
    float flash;
    float vignette;
    float brightness;
    float pad;
};

fragment float4 fs_present(FSOut in [[stage_in]],
                           texture2d<float> tex [[texture(0)]],
                           constant PresentUniforms& u [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 c = tex.sample(s, in.uv).rgb * u.brightness;
    float2 p = in.uv - 0.5;
    c *= 1.0 - u.vignette * dot(p, p) * 2.0;
    c += u.flash * 0.18;
    c = c / (1.0 + c * 0.25);
    return float4(saturate(c), 1.0);
}

// ---------------------------------------------------------------- logo

struct LogoUniforms {
    float cx;
    float cy;
    float hx;
    float hy;
    float rotation;
    float aspect;
    float opacity;
    float mipBias;
    float r;
    float g;
    float b;
    float tintAmount;
};

struct LogoOut {
    float4 position [[position]];
    float2 uv;
};

vertex LogoOut vs_logo(uint vid [[vertex_id]],
                       constant LogoUniforms& u [[buffer(0)]]) {
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float2 local = corner * float2(u.hx, u.hy);
    float c = cos(u.rotation);
    float s = sin(u.rotation);
    float2 p = float2(c * local.x - s * local.y, s * local.x + c * local.y) + float2(u.cx, u.cy);
    LogoOut o;
    o.position = float4(p.x / u.aspect, p.y, 0.0, 1.0);
    o.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    return o;
}

fragment float4 fs_logo(LogoOut in [[stage_in]],
                        texture2d<float> tex [[texture(0)]],
                        constant LogoUniforms& u [[buffer(0)]]) {
    constexpr sampler s(filter::linear, mip_filter::linear, address::clamp_to_zero);
    float4 c = tex.sample(s, in.uv, bias(u.mipBias)); // premultiplied alpha
    float3 tinted = mix(c.rgb, float3(u.r, u.g, u.b) * c.a, u.tintAmount);
    return float4(tinted, c.a) * u.opacity;
}
// ---------------------------------------------------------------- recording
// Copies the finished frame into the recording's pixel buffer (scaled to the viewport).
fragment float4 fs_blit(FSOut in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return float4(tex.sample(s, in.uv).rgb, 1.0);
}

// METAL-END
"""#
}
