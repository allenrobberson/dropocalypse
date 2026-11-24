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
    float rotCos;     // cos/sin of this frame's rotation and hue shift, worked out on the CPU
    float warpAmount;
    float warpScale;
    float warpSpeed;
    float decay;
    float driftX;
    float driftY;
    float kaleido;
    float hueCos;
    float plasma;
    float bass;
    float mid;
    float treble;
    float texelX;
    float texelY;
    int   mirror;
    float generative; // 0 off, 1 = Full Kaleidoscope field mixed over the feedback
    float rotSin;
    float hueSin;
    float chrome;     // 0 off, 1 = Liquid Chrome field replaces the feedback
    float chromeHue;
};

// Liquid Chrome: a flowing height field shaded like polished metal reflecting a studio sky.
static float chromeHeight(float2 p, float t, float bass, float mid) {
    float2 q = p + 0.35 * float2(sin(p.y * 1.7 + t * 1.3), cos(p.x * 1.4 - t * 1.1));
    float h = sin(q.x * 2.1 + t) * cos(q.y * 1.8 - t * 0.8)
            + 0.5 * sin(length(q) * 3.2 - t * 2.0 - bass * 1.5)
            + 0.25 * sin((q.x - q.y) * 4.0 + t * 1.7 + mid * 2.0);
    return h * (0.55 + bass * 0.6);
}

static float3 chromeField(float2 uv, float time, float bass, float mid, float aspect, float hue) {
    float2 p = (uv - 0.5) * float2(max(aspect, 0.05), 1.0) * 2.2;
    float t = time * 0.35;
    const float e = 0.01;
    float h = chromeHeight(p, t, bass, mid);
    float hx = chromeHeight(p + float2(e, 0.0), t, bass, mid);
    float hy = chromeHeight(p + float2(0.0, e), t, bass, mid);
    float3 n = normalize(float3((h - hx) / e * 0.35, (h - hy) / e * 0.35, 1.0));
    float3 r = reflect(float3(0.0, 0.0, -1.0), n);
    float3 tint = 0.5 + 0.5 * cos(6.2831853 * (hue + float3(0.0, 0.33, 0.67)));
    float3 sky = mix(float3(0.03, 0.03, 0.05), float3(0.92, 0.95, 1.0), smoothstep(-0.15, 0.7, r.y));
    float horizon = exp(-pow((r.y - 0.08) * 8.0, 2.0));
    float3 col = sky * mix(float3(1.0), tint, 0.35) + horizon * mix(float3(1.0), tint, 0.6) * 1.2;
    col += pow(1.0 - saturate(n.z), 1.5) * tint * 0.8;
    col += pow(saturate(r.x * 0.5 + r.y * 0.8), 24.0) * 1.5;
    return col * (0.85 + bass * 0.3);
}

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

// Rotates color c around the grey axis by the angle whose cosine and sine are ca and sa.
static float3 hueRotate(float3 c, float ca, float sa) {
    const float3 k = float3(0.57735027);
    return c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca);
}

fragment float4 fs_warp(FSOut in [[stage_in]],
                        texture2d<half> prev [[texture(0)]],
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

    p = float2(u.rotCos * p.x - u.rotSin * p.y, u.rotSin * p.x + u.rotCos * p.y);
    p /= u.zoom;

    float t = u.time * u.warpSpeed;
    p += u.warpAmount * float2(sin(p.y * u.warpScale + t),
                               cos(p.x * u.warpScale * 1.13 - t * 0.87));
    p -= float2(u.driftX, u.driftY);

    p.x /= u.aspect;
    float2 uv = p + 0.5;

    // The feedback texture is 16-bit float, so it is sampled and blurred in half precision.
    // Coordinates stay 32-bit, and the hue rotation and decay, which repeat every frame, stay
    // 32-bit so long trails fade exactly as before.
    float2 tx = float2(u.texelX, u.texelY);
    half3 blur = prev.sample(s, uv).rgb * 0.6h
               + (prev.sample(s, uv + float2(tx.x, 0.0)).rgb
                + prev.sample(s, uv - float2(tx.x, 0.0)).rgb
                + prev.sample(s, uv + float2(0.0, tx.y)).rgb
                + prev.sample(s, uv - float2(0.0, tx.y)).rgb) * 0.1h;

    float3 c = hueRotate(float3(blur), u.hueCos, u.hueSin) * u.decay;

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

    if (u.chrome > 0.001) {
        // The feedback's uv runs top to bottom; flip so the chrome's sky is up.
        float2 cu = float2(in.uv.x, 1.0 - in.uv.y);
        c = mix(c, chromeField(cu, u.time, u.bass, u.mid, u.aspect, u.chromeHue), saturate(u.chrome));
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

// GPU expansion of the simple wave styles. Matches vs_signal in the Windows HLSL.
struct SignalUniforms {
    float aspect;
    float radius;
    float amplitude;
    float rotation;
    float halfWidth;
    float hue0;
    float saturation;
    float alpha;
    float p0;
    float p1;
    float p2;
    float p3;
    int mode;
    int count;
    int stride;
    int sampleOffset;
    int closed;
    int side;
    int pad0;
    int pad1;
};

float sigLoad(constant float* s, int i) { return s[clamp(i, 0, 559)]; }

float3 sigHsv(float h, float s, float v) {
    float hh = fract(h) * 6.0;
    int i = int(floor(hh)) % 6;
    float f = fract(hh);
    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f);
    float t = v * (1.0 - s * (1.0 - f));
    if (i <= 0) return float3(v, t, p);
    if (i == 1) return float3(q, v, p);
    if (i == 2) return float3(p, v, t);
    if (i == 3) return float3(p, q, v);
    if (i == 4) return float3(t, p, v);
    return float3(v, p, q);
}

float2 sigRing(constant float* samples, constant SignalUniforms& u, int i) {
    int n = max(u.count, 1);
    float w = sigLoad(samples, u.sampleOffset + i * u.stride);
    if (u.closed != 0 && i >= n - 24) {
        float k = float(i - (n - 24)) / 24.0;
        w += (sigLoad(samples, u.sampleOffset) - w) * k;
    }
    float a = float(i) / float(n) * 6.28318530718 + u.rotation;
    float r = max(0.0, u.radius + w * u.amplitude);
    return float2(cos(a), sin(a)) * r;
}

float2 sigPoint(constant float* samples, constant SignalUniforms& u, int i) {
    if (u.mode == 4) {
        int idx = i * 2;
        return float2(sigLoad(samples, idx) * u.p0, sigLoad(samples, idx + 23) * u.p0);
    }
    if (u.mode == 1 && u.side != 0) {
        float den = float(max(u.count - 1, 1));
        float y = u.p0 + float(i) / den * u.p1;
        float x = float(u.side) * u.p2 + sigLoad(samples, u.sampleOffset + i * u.stride) * u.p3;
        return float2(x, y);
    }
    if (u.mode == 1 && u.closed == 0) {
        float den = float(max(u.count - 1, 1));
        float x = u.p0 + float(i) / den * u.p1;
        return float2(x, sigLoad(samples, u.sampleOffset + i * u.stride) * u.p2);
    }
    return sigRing(samples, u, i);
}

int sigCorner(uint c) {
    if (c == 0 || c == 3) return 0;
    if (c == 1) return 1;
    if (c == 2 || c == 4) return 2;
    return 3;
}

vertex GeomOut vs_signal(uint vid [[vertex_id]],
                         constant SignalUniforms& u [[buffer(0)]],
                         constant float* samples [[buffer(1)]]) {
    uint corner = vid % 6;
    uint seg = vid / 6;
    float2 q[4];
    float4 col;
    float denom = float(max(u.count, 1));
    if (u.mode == 2) {
        float slot = u.p0 / denom;
        float x0 = -u.p0 * 0.5 + float(seg) * slot + slot * 0.15;
        float x1 = x0 + slot * 0.7;
        float y0 = -0.98;
        float y1 = y0 + 0.02 + sigLoad(samples, 512 + int(seg)) * u.p1;
        q[0] = float2(x0, y0); q[1] = float2(x1, y0); q[2] = float2(x1, y1); q[3] = float2(x0, y1);
        col = float4(sigHsv(u.hue0 + (float(seg) / denom) * 0.35, u.saturation, 1.0), u.alpha);
    } else if (u.mode == 5) {
        uint dot = vid / 24;
        uint tri = (vid % 24) / 3;
        uint corn = (vid % 24) % 3;
        int arm = int(dot) / 48;
        int bi = int(dot) % 48;
        float frac = float(bi) / 48.0;
        float ang = float(bi) * 0.33 + float(arm) * 2.09439510239 + u.rotation;
        float rr = u.p0 + frac * u.p1;
        float rad = 0.006 + sigLoad(samples, 512 + bi) * u.p2;
        float2 c = float2(cos(ang), sin(ang)) * rr;
        float a0 = float(tri) / 8.0 * 6.28318530718;
        float a1 = float(tri + 1) / 8.0 * 6.28318530718;
        float2 p = corn == 0 ? c : (corn == 1 ? c + float2(cos(a0), sin(a0)) * rad
                                              : c + float2(cos(a1), sin(a1)) * rad);
        p.x /= u.aspect;
        GeomOut o;
        o.position = float4(p, 0.0, 1.0);
        o.color = float4(sigHsv(u.hue0 + (frac + float(arm) * 0.2) * 0.35, u.saturation, 1.0), u.alpha);
        return o;
    } else if (u.mode == 3) {
        float frac = float(seg) / denom;
        float ang = (u.side == 0 ? frac : -frac) * 3.14159265359 + 1.57079632679 + u.rotation;
        float2 dir = float2(cos(ang), sin(ang));
        float len = 0.03 + sigLoad(samples, 512 + int(seg)) * u.p1;
        float2 a = dir * u.radius;
        float2 b = dir * (u.radius + len);
        float2 d = b - a;
        float L = max(length(d), 1e-6);
        float2 nrm = float2(-d.y, d.x) / L * u.halfWidth;
        q[0] = a + nrm; q[1] = b + nrm; q[2] = b - nrm; q[3] = a - nrm;
        col = float4(sigHsv(u.hue0 + frac * 0.35, u.saturation, 1.0), u.alpha);
    } else {
        int i0 = int(seg);
        int i1 = (u.closed != 0 && int(seg) == u.count - 1) ? 0 : i0 + 1;
        float2 a = sigPoint(samples, u, i0);
        float2 b = sigPoint(samples, u, i1);
        float2 d = b - a;
        float L = max(length(d), 1e-6);
        float2 nrm = float2(-d.y, d.x) / L * u.halfWidth;
        q[0] = a + nrm; q[1] = b + nrm; q[2] = b - nrm; q[3] = a - nrm;
        col = float4(sigHsv(u.hue0 + (float(i0) / denom) * 0.35, u.saturation, 1.0), u.alpha);
    }
    float2 p = q[sigCorner(corner)];
    p.x /= u.aspect;
    GeomOut o;
    o.position = float4(p, 0.0, 1.0);
    o.color = col;
    return o;
}

// ---------------------------------------------------------------- final composite

struct PresentUniforms {
    float flash;
    float vignette;
    float brightness;
    float milkMix;   // 0 = our picture, 1 = the MilkDrop preset (milk)
    float glitch;      // 0 off, 1 = VHS tape damage (Glitch VHS)
    float glitchTime;
    float glitchPulse; // jumps on the beat, then fades
    float pad;
};

static half3 presentBase(texture2d<half> tex, texture2d<half> milk, sampler s, float2 uv, float milkMix) {
    half3 base = tex.sample(s, uv).rgb;
    if (milkMix > 0.0) {
        // projectM draws with OpenGL, whose rows run bottom to top.
        half3 m = milk.sample(s, float2(uv.x, 1.0 - uv.y)).rgb;
        base = mix(base, m, half(milkMix));
    }
    return base;
}

static float glitchHash(float2 p) { return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }

// Color math in half precision: the source is a 16-bit texture and the screen is 8-bit.
fragment half4 fs_present(FSOut in [[stage_in]],
                          texture2d<half> tex [[texture(0)]],
                          texture2d<half> milk [[texture(1)]],
                          constant PresentUniforms& u [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    half3 base;
    if (u.glitch > 0.001) {
        // Tape wobble, torn bands that jump sideways on the beat, colour fringes, scanlines, grain.
        float g = u.glitch;
        float2 uv = in.uv;
        uv.x += sin(uv.y * 28.0 + u.glitchTime * 5.0) * 0.0022 * g;
        float band = floor(uv.y * 36.0), tick = floor(u.glitchTime * 14.0);
        if (glitchHash(float2(band, tick)) > 1.0 - g * (0.05 + 0.35 * u.glitchPulse))
            uv.x += (glitchHash(float2(tick, band)) - 0.5) * 0.12 * (0.3 + u.glitchPulse) * g;
        float off = (0.0025 + 0.018 * u.glitchPulse) * g;
        base = half3(presentBase(tex, milk, s, uv + float2(off, 0.0), u.milkMix).r,
                     presentBase(tex, milk, s, uv, u.milkMix).g,
                     presentBase(tex, milk, s, uv - float2(off, 0.0), u.milkMix).b);
        base *= half(1.0 - 0.16 * g * (0.5 + 0.5 * sin(in.uv.y * 1100.0)));
        base = max(base + half((glitchHash(in.uv * 917.0 + u.glitchTime) - 0.5) * 0.07 * g), half3(0.0h));
    } else {
        base = presentBase(tex, milk, s, in.uv, u.milkMix);
    }
    // The clamp keeps a very bright pixel from overflowing half; anything above about 1.4
    // already ends up pure white after the tone curve, so the picture is unchanged.
    half3 c = min(base * half(u.brightness), half3(4096.0h));
    float2 p = in.uv - 0.5;
    c *= half(1.0 - u.vignette * dot(p, p) * 2.0);
    c += half(u.flash * 0.18);
    c = c / (1.0h + c * 0.25h);
    return half4(saturate(c), 1.0h);
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
