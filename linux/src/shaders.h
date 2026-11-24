// Dropocalypse for Linux - GLSL 3.30 shaders (port of the Windows HLSL / macOS Metal shaders).
//
// Conventions match the other versions: `uv` (0,0) is the top-left of the picture. OpenGL puts
// row 0 of a render target at the bottom, so samples of the feedback buffer flip y (fbTex()).
// Logo textures are uploaded top row first and are sampled with uv as is.
#pragma once

static const char* kCommonVS = R"GLSL(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(p.x, 1.0 - p.y);
}
)GLSL";

static const char* kWarpFS = R"GLSL(#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D tex0;          // border = transparent black
layout(std140) uniform WarpCB {
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

vec3 fbTex(vec2 t) { return texture(tex0, vec2(t.x, 1.0 - t.y)).rgb; }

// Full Kaleidoscope: a mandala that fills the frame. Four-way mirror always;
// a diagonal fold fades in and out so the same picture sometimes reads as eight-way.
// Bass pulses and turns it. Mids and highs raise the detail. Keep in step with shaders.h on Windows.
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    v += a * vnoise(p); p = p * 2.02 + vec2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p); p = p * 2.02 + vec2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p); p = p * 2.02 + vec2(1.7, 9.2); a *= 0.5;
    v += a * vnoise(p);
    return v;
}

vec3 neonPalette(float t) {
    t = fract(t) * 5.0;
    float i = floor(t);
    float f = smoothstep(0.0, 1.0, fract(t));
    vec3 a = vec3(0.0, 0.96, 1.0);
    vec3 b = vec3(1.0, 0.04, 0.58);
    if (i < 1.0) { a = vec3(0.0, 0.96, 1.0); b = vec3(1.0, 0.04, 0.58); }
    else if (i < 2.0) { a = vec3(1.0, 0.04, 0.58); b = vec3(0.12, 0.28, 1.0); }
    else if (i < 3.0) { a = vec3(0.12, 0.28, 1.0); b = vec3(1.0, 0.68, 0.05); }
    else if (i < 4.0) { a = vec3(1.0, 0.68, 0.05); b = vec3(0.48, 1.0, 0.10); }
    else { a = vec3(0.48, 1.0, 0.10); b = vec3(0.0, 0.96, 1.0); }
    return mix(a, b, f);
}

vec3 kaleidoField(vec2 uv) {
    vec2 p = (uv - 0.5) * vec2(max(aspect, 0.05), 1.0);
    float spin = time * 0.07 + bass * 0.22;
    float cs = cos(spin);
    float sn = sin(spin);
    p = vec2(cs * p.x - sn * p.y, sn * p.x + cs * p.y) / (1.0 + bass * 0.28);

    vec2 p4 = abs(p);
    vec2 p8 = vec2(min(p4.x, p4.y), max(p4.x, p4.y));
    float eight = clamp(0.5 + 0.5 * sin(time * 0.19) + mid * 0.45, 0.0, 1.0);
    p = mix(p4, p8, smoothstep(0.2, 0.85, eight));

    vec2 q = p;
    float wt = time * (0.32 + mid * 0.25);
    float amp = 0.07 + treble * 0.09;
    q += amp * vec2(sin(p.y * (3.1 + mid * 4.0) + wt), cos(p.x * (2.6 + treble * 5.0) - wt * 0.85));

    float detail = 2.1 + mid * 2.8 + treble * 4.2;
    float n = fbm(q * detail + time * 0.06);
    float n2 = fbm(q.yx * (detail * 1.85) - time * 0.09 + 3.7);

    float cells = 3.2 + mid * 4.5 + treble * 3.5;
    vec2 cell = abs(fract(q * cells) - 0.5);
    float tile = smoothstep(0.48, 0.16, max(cell.x, cell.y));
    float grid = smoothstep(0.08, 0.0, min(cell.x, cell.y));
    float r = length(p4);
    float rings = pow(abs(sin(r * (7.5 + treble * 10.0) - time * (0.55 + bass * 0.8))), 10.0);

    float hueT = n * 0.62 + n2 * 0.28 + time * 0.02 + r * 0.06 + mid * 0.12;
    vec3 col = neonPalette(hueT);
    vec3 alt = neonPalette(hueT + 0.38 + treble * 0.1);
    col = mix(col, alt, clamp(tile * 0.9 + rings * 0.65, 0.0, 1.0));
    col *= 0.42 + n * 0.85 + n2 * 0.45 + tile * 0.55;
    col += alt * rings * 0.9;
    col += neonPalette(0.02 + bass * 0.04) * grid * 0.35;
    float axis = exp(-min(p4.x, p4.y) * (7.0 - bass * 2.0));
    col += neonPalette(0.52) * axis * (0.22 + bass * 0.18) * (1.0 - eight * 0.65);

    vec2 centered = (uv - 0.5) * vec2(max(aspect, 0.05), 1.0);
    float edge = length(centered) / max(length(vec2(max(aspect, 0.05), 1.0) * 0.5), 0.001);
    col *= mix(1.2, 0.22, smoothstep(0.22, 1.0, edge));
    return col * (0.9 + bass * 0.45);
}

vec3 hueRotate(vec3 c, float a) {
    const vec3 k = vec3(0.57735027);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

void main() {
    vec2 p = uv - 0.5;
    p.x *= aspect;

    if (mirror == 1) {
        p.x = abs(p.x);
    } else if (mirror == 2) {
        p = abs(p);
    }

    if (kaleido > 0.5) {
        float r = length(p);
        float a = atan(p.y, p.x);
        float seg = 6.2831853 / kaleido;
        a = mod(a + 25.1327412, seg);
        a = abs(a - seg * 0.5);
        p = r * vec2(cos(a), sin(a));
    }

    float ca = cos(rotation);
    float sa = sin(rotation);
    p = vec2(ca * p.x - sa * p.y, sa * p.x + ca * p.y);
    p /= zoom;

    float t = time * warpSpeed;
    p += warpAmount * vec2(sin(p.y * warpScale + t), cos(p.x * warpScale * 1.13 - t * 0.87));
    p -= vec2(driftX, driftY);

    p.x /= aspect;
    vec2 q0 = p + 0.5;

    vec2 tx = vec2(texelX, texelY);
    vec3 c = fbTex(q0) * 0.6
           + (fbTex(q0 + vec2(tx.x, 0.0))
            + fbTex(q0 - vec2(tx.x, 0.0))
            + fbTex(q0 + vec2(0.0, tx.y))
            + fbTex(q0 - vec2(0.0, tx.y))) * 0.1;

    c = hueRotate(c, hueShift) * decay;

    if (plasma > 0.0) {
        vec2 q = (uv - 0.5) * vec2(aspect, 1.0) * 3.0;
        float v = sin(q.x + time)
                + sin(q.y * 1.3 - time * 0.7)
                + sin((q.x + q.y) * 0.8 + time * 1.1)
                + sin(length(q) * 2.0 - time * 1.5);
        vec3 pc = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + v * 1.2 + time * 0.3);
        c += pc * plasma * (0.15 + bass) * 0.04;
    }

    if (generative > 0.001) {
        vec3 gen = kaleidoField(uv);
        c = mix(c, max(c, vec3(0.0)) * 0.42 + gen, clamp(generative, 0.0, 1.0));
    }

    fragColor = vec4(max(c, vec3(0.0)), 1.0);
}
)GLSL";

static const char* kGeomVS = R"GLSL(#version 330 core
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;
out vec4 vColor;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    vColor = color;
}
)GLSL";

static const char* kGeomFS = R"GLSL(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
)GLSL";

// GPU expansion of the simple wave styles. The CPU uploads the 512 wave samples and 48 band
// levels; this shader builds the same quads the CPU polyline used to. Port of vs_signal.
static const char* kSignalVS = R"GLSL(#version 330 core
layout(std140) uniform SignalCB {
    float sigAspect;
    float sigRadius;
    float sigAmplitude;
    float sigRotation;
    float sigHalfWidth;
    float sigHue0;
    float sigSaturation;
    float sigAlpha;
    float sigP0;
    float sigP1;
    float sigP2;
    float sigP3;
    int sigMode;
    int sigCount;
    int sigStride;
    int sigOffset;
    int sigClosed;
    int sigSide;
    int sigPad0;
    int sigPad1;
};
uniform samplerBuffer sigSamples;
out vec4 vColor;

float sigLoad(int i) { return texelFetch(sigSamples, clamp(i, 0, 559)).r; }
float sigWave(int i) { return sigLoad(i); }
float sigBand(int i) { return sigLoad(512 + i); }

vec3 sigHsv(float h, float s, float v) {
    float hh = fract(h) * 6.0;
    int i = int(floor(hh)) % 6;
    float f = fract(hh);
    float p = v * (1.0 - s);
    float q = v * (1.0 - s * f);
    float t = v * (1.0 - s * (1.0 - f));
    if (i <= 0) return vec3(v, t, p);
    if (i == 1) return vec3(q, v, p);
    if (i == 2) return vec3(p, v, t);
    if (i == 3) return vec3(p, q, v);
    if (i == 4) return vec3(t, p, v);
    return vec3(v, p, q);
}

vec4 sigColor(float frac) {
    return vec4(sigHsv(sigHue0 + frac * 0.35, sigSaturation, 1.0), sigAlpha);
}

vec2 sigRing(int i) {
    int n = max(sigCount, 1);
    float w = sigWave(sigOffset + i * sigStride);
    if (sigClosed != 0 && i >= n - 24) {
        float k = float(i - (n - 24)) / 24.0;
        w += (sigWave(sigOffset) - w) * k;
    }
    float a = float(i) / float(n) * 6.28318530718 + sigRotation;
    float r = max(0.0, sigRadius + w * sigAmplitude);
    return vec2(cos(a), sin(a)) * r;
}

vec2 sigLine(int i) {
    float den = float(max(sigCount - 1, 1));
    float x = sigP0 + float(i) / den * sigP1;
    return vec2(x, sigWave(sigOffset + i * sigStride) * sigP2);
}

vec2 sigVertical(int i) {
    float den = float(max(sigCount - 1, 1));
    float y = sigP0 + float(i) / den * sigP1;
    float x = float(sigSide) * sigP2 + sigWave(sigOffset + i * sigStride) * sigP3;
    return vec2(x, y);
}

vec2 sigLissa(int j) {
    int i = j * 2;
    return vec2(sigWave(i) * sigP0, sigWave(i + 23) * sigP0);
}

vec2 sigPoint(int i) {
    if (sigMode == 4) return sigLissa(i);
    if (sigMode == 1 && sigSide != 0) return sigVertical(i);
    if (sigMode == 1 && sigClosed == 0) return sigLine(i);
    return sigRing(i);
}

int sigCorner(int c) {
    if (c == 0 || c == 3) return 0;
    if (c == 1) return 1;
    if (c == 2 || c == 4) return 2;
    return 3;
}

void main() {
    int vid = gl_VertexID;
    int corner = vid % 6;
    int seg = vid / 6;
    vec2 q[4];
    vec4 col;
    if (sigMode == 2) {
        float slot = sigP0 / float(max(sigCount, 1));
        float x0 = -sigP0 * 0.5 + float(seg) * slot + slot * 0.15;
        float x1 = x0 + slot * 0.7;
        float y0 = -0.98;
        float y1 = y0 + 0.02 + sigBand(seg) * sigP1;
        q[0] = vec2(x0, y0); q[1] = vec2(x1, y0); q[2] = vec2(x1, y1); q[3] = vec2(x0, y1);
        col = sigColor(float(seg) / float(max(sigCount, 1)));
    } else if (sigMode == 5) {
        int dotI = vid / 24;
        int tri = (vid % 24) / 3;
        int corn = (vid % 24) % 3;
        int arm = dotI / 48;
        int bi = dotI % 48;
        float frac = float(bi) / 48.0;
        float ang = float(bi) * 0.33 + float(arm) * 2.09439510239 + sigRotation;
        float rr = sigP0 + frac * sigP1;
        float rad = 0.006 + sigBand(bi) * sigP2;
        vec2 c = vec2(cos(ang), sin(ang)) * rr;
        float a0 = float(tri) / 8.0 * 6.28318530718;
        float a1 = float(tri + 1) / 8.0 * 6.28318530718;
        vec2 p = corn == 0 ? c : (corn == 1 ? c + vec2(cos(a0), sin(a0)) * rad
                                            : c + vec2(cos(a1), sin(a1)) * rad);
        p.x /= sigAspect;
        gl_Position = vec4(p, 0.0, 1.0);
        vColor = sigColor(frac + float(arm) * 0.2);
        return;
    } else if (sigMode == 3) {
        float frac = float(seg) / float(max(sigCount, 1));
        float ang = (sigSide == 0 ? frac : -frac) * 3.14159265359 + 1.57079632679 + sigRotation;
        vec2 dir = vec2(cos(ang), sin(ang));
        float len = 0.03 + sigBand(seg) * sigP1;
        vec2 a = dir * sigRadius;
        vec2 b = dir * (sigRadius + len);
        vec2 d = b - a;
        float L = max(length(d), 1e-6);
        vec2 nrm = vec2(-d.y, d.x) / L * sigHalfWidth;
        q[0] = a + nrm; q[1] = b + nrm; q[2] = b - nrm; q[3] = a - nrm;
        col = sigColor(frac);
    } else {
        int i0 = seg;
        int i1 = (sigClosed != 0 && seg == sigCount - 1) ? 0 : i0 + 1;
        vec2 a = sigPoint(i0);
        vec2 b = sigPoint(i1);
        vec2 d = b - a;
        float L = max(length(d), 1e-6);
        vec2 nrm = vec2(-d.y, d.x) / L * sigHalfWidth;
        q[0] = a + nrm; q[1] = b + nrm; q[2] = b - nrm; q[3] = a - nrm;
        float denom = float(max(sigCount, 1));
        col = sigColor(float(i0) / denom);
    }
    vec2 p = q[sigCorner(corner)];
    p.x /= sigAspect;
    gl_Position = vec4(p, 0.0, 1.0);
    vColor = col;
}
)GLSL";

static const char* kPresentFS = R"GLSL(#version 330 core
in vec2 uv;
out vec4 fragColor;
uniform sampler2D tex0;          // clamp to edge
uniform sampler2D tex1;          // the MilkDrop picture
layout(std140) uniform PresentCB {
    float flash;
    float vignette;
    float brightness;
    float milkMix;   // 0 = our picture, 1 = the MilkDrop preset
};
void main() {
    vec2 st = vec2(uv.x, 1.0 - uv.y);
    vec3 c = mix(texture(tex0, st).rgb, texture(tex1, st).rgb, milkMix) * brightness;
    vec2 p = uv - 0.5;
    c *= 1.0 - vignette * dot(p, p) * 2.0;
    c += flash * 0.18;
    c = c / (1.0 + c * 0.25);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

static const char* kLogoBlock = R"GLSL(
layout(std140) uniform LogoCB {
    float cx;
    float cy;
    float hx;
    float hy;
    float logoRotation;
    float logoAspect;
    float opacity;
    float mipBias;
    float tintR;
    float tintG;
    float tintB;
    float tintAmount;
};
)GLSL";

static const char* kLogoVS = R"GLSL(#version 330 core
#include LOGO_BLOCK
out vec2 uv;
void main() {
    int vid = gl_VertexID;
    vec2 corner = vec2((vid & 1) != 0 ? 1.0 : -1.0, (vid & 2) != 0 ? 1.0 : -1.0);
    vec2 local = corner * vec2(hx, hy);
    float c = cos(logoRotation);
    float s = sin(logoRotation);
    vec2 p = vec2(c * local.x - s * local.y, s * local.x + c * local.y) + vec2(cx, cy);
    gl_Position = vec4(p.x / logoAspect, p.y, 0.0, 1.0);
    uv = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
}
)GLSL";

static const char* kLogoFS = R"GLSL(#version 330 core
#include LOGO_BLOCK
in vec2 uv;
out vec4 fragColor;
uniform sampler2D tex0;          // trilinear, border = transparent black
void main() {
    vec4 c = texture(tex0, uv, mipBias); // premultiplied alpha
    vec3 tinted = mix(c.rgb, vec3(tintR, tintG, tintB) * c.a, tintAmount);
    fragColor = vec4(tinted, c.a) * opacity;
}
)GLSL";
