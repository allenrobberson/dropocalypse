// Dropocalypse for Windows - HLSL shaders (port of the macOS Metal shaders).
// Compiled at startup with D3DCompile. CI extracts the text between the HLSL-BEGIN / HLSL-END
// markers and compiles every entry point with fxc so shader errors fail the build.
#pragma once

static const char* kShaderSource = R"HLSL(
// HLSL-BEGIN
struct FSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0; // (0,0) = top-left
};

FSOut vs_fullscreen(uint vid : SV_VertexID) {
    float2 p = float2((vid << 1) & 2, vid & 2);
    FSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}

Texture2D tex0 : register(t0);
SamplerState sampZero : register(s0);  // linear, border = transparent black
SamplerState sampClamp : register(s1); // linear, clamp
SamplerState sampMip : register(s2);   // trilinear, border = transparent black

// ---------------------------------------------------------------- feedback warp

cbuffer WarpCB : register(b0) {
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
    float warpPad;
};

float3 hueRotate(float3 c, float a) {
    const float3 k = float3(0.57735027, 0.57735027, 0.57735027);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

float4 fs_warp(FSOut i) : SV_Target {
    float2 p = i.uv - 0.5;
    p.x *= aspect;

    if (mirror == 1) {
        p.x = abs(p.x);
    } else if (mirror == 2) {
        p = abs(p);
    }

    if (kaleido > 0.5) {
        float r = length(p);
        float a = atan2(p.y, p.x);
        float seg = 6.2831853 / kaleido;
        a = fmod(a + 25.1327412, seg);
        a = abs(a - seg * 0.5);
        p = r * float2(cos(a), sin(a));
    }

    float ca = cos(rotation);
    float sa = sin(rotation);
    p = float2(ca * p.x - sa * p.y, sa * p.x + ca * p.y);
    p /= zoom;

    float t = time * warpSpeed;
    p += warpAmount * float2(sin(p.y * warpScale + t), cos(p.x * warpScale * 1.13 - t * 0.87));
    p -= float2(driftX, driftY);

    p.x /= aspect;
    float2 uv = p + 0.5;

    float2 tx = float2(texelX, texelY);
    float3 c = tex0.Sample(sampZero, uv).rgb * 0.6
             + (tex0.Sample(sampZero, uv + float2(tx.x, 0.0)).rgb
              + tex0.Sample(sampZero, uv - float2(tx.x, 0.0)).rgb
              + tex0.Sample(sampZero, uv + float2(0.0, tx.y)).rgb
              + tex0.Sample(sampZero, uv - float2(0.0, tx.y)).rgb) * 0.1;

    c = hueRotate(c, hueShift) * decay;

    if (plasma > 0.0) {
        float2 q = (i.uv - 0.5) * float2(aspect, 1.0) * 3.0;
        float v = sin(q.x + time)
                + sin(q.y * 1.3 - time * 0.7)
                + sin((q.x + q.y) * 0.8 + time * 1.1)
                + sin(length(q) * 2.0 - time * 1.5);
        float3 pc = 0.5 + 0.5 * cos(float3(0.0, 2.1, 4.2) + v * 1.2 + time * 0.3);
        c += pc * plasma * (0.15 + bass) * 0.04;
    }

    return float4(max(c, float3(0.0, 0.0, 0.0)), 1.0);
}

// ---------------------------------------------------------------- waveforms, bars, dots

struct GeomIn {
    float2 pos : POSITION;
    float4 color : COLOR;
};

struct GeomOut {
    float4 pos : SV_Position;
    float4 color : COLOR;
};

GeomOut vs_geom(GeomIn v) {
    GeomOut o;
    o.pos = float4(v.pos, 0.0, 1.0);
    o.color = v.color;
    return o;
}

float4 fs_geom(GeomOut i) : SV_Target {
    return float4(i.color.rgb * i.color.a, i.color.a);
}

// ---------------------------------------------------------------- final composite

cbuffer PresentCB : register(b1) {
    float flash;
    float vignette;
    float brightness;
    float presentPad;
};

float4 fs_present(FSOut i) : SV_Target {
    float3 c = tex0.Sample(sampClamp, i.uv).rgb * brightness;
    float2 p = i.uv - 0.5;
    c *= 1.0 - vignette * dot(p, p) * 2.0;
    c += flash * 0.18;
    c = c / (1.0 + c * 0.25);
    return float4(saturate(c), 1.0);
}

// ---------------------------------------------------------------- logo

cbuffer LogoCB : register(b2) {
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

struct LogoOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

LogoOut vs_logo(uint vid : SV_VertexID) {
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float2 local = corner * float2(hx, hy);
    float c = cos(logoRotation);
    float s = sin(logoRotation);
    float2 p = float2(c * local.x - s * local.y, s * local.x + c * local.y) + float2(cx, cy);
    LogoOut o;
    o.pos = float4(p.x / logoAspect, p.y, 0.0, 1.0);
    o.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    return o;
}

float4 fs_logo(LogoOut i) : SV_Target {
    float4 c = tex0.SampleBias(sampMip, i.uv, mipBias); // premultiplied alpha
    float3 tinted = lerp(c.rgb, float3(tintR, tintG, tintB) * c.a, tintAmount);
    return float4(tinted, c.a) * opacity;
}
// HLSL-END
)HLSL";
