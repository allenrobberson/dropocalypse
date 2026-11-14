// Dropocalypse for Windows - the 20 built-in styles (same values as the macOS Scenes.swift).
#pragma once

#include <array>

enum class WaveStyle {
    Circle, Line, VerticalLine, Bars, RadialBars, SpiralDots, Lissajous, TwinRings,
    // Classic favorites
    BassRing, NeonEqualizer, PulsarRidges, OutrunGrid, Hyperspace, ParticleStorm, Lightning, DigitalRain, LavaLamp,
    Inferno
};

struct VisualScene {
    const char* name = "";
    // Feedback warp.
    float zoom = 1.01f, zoomBass = 0.03f, rotation = 0, rotationBass = 0;
    float warpAmount = 0.004f, warpScale = 6, warpSpeed = 0.8f, decay = 0.955f;
    float driftX = 0, driftY = 0, kaleido = 0;
    int mirror = 0;
    float hueShift = 0.004f, plasma = 0;
    // Foreground drawing.
    WaveStyle wave = WaveStyle::Circle;
    float centerRadius = 0.32f, waveScale = 1, thickness = 1;
    float baseHue = 0, hueSpeed = 0.03f, saturation = 0.9f, vignette = 0.35f;
};

inline const std::array<VisualScene, 20>& builtInScenes() {
    static const std::array<VisualScene, 20> scenes = [] {
        std::array<VisualScene, 20> s{};
        VisualScene* c;

        c = &s[0]; c->name = "Classic Tunnel";
        c->zoom = 1.025f; c->zoomBass = 0.05f; c->rotation = 0.004f; c->rotationBass = 0.01f;
        c->warpAmount = 0.003f; c->decay = 0.95f; c->hueShift = 0.006f;
        c->wave = WaveStyle::Circle; c->centerRadius = 0.03f; c->thickness = 1.2f; c->baseHue = 0.55f;

        c = &s[1]; c->name = "Spectrum Skyline";
        c->zoom = 1.0f; c->zoomBass = 0.006f; c->warpAmount = 0.0015f; c->decay = 0.9f;
        c->driftY = -0.006f; c->hueShift = 0.002f;
        c->wave = WaveStyle::Bars; c->baseHue = 0.75f; c->hueSpeed = 0.05f; c->vignette = 0.2f;

        c = &s[2]; c->name = "Starburst";
        c->zoom = 1.045f; c->zoomBass = 0.06f; c->rotation = -0.002f; c->rotationBass = -0.012f;
        c->warpAmount = 0.002f; c->decay = 0.93f; c->hueShift = 0.01f;
        c->wave = WaveStyle::RadialBars; c->centerRadius = 0; c->waveScale = 1.1f; c->baseHue = 0.08f;

        c = &s[3]; c->name = "Oscilloscope Drift";
        c->zoom = 1.006f; c->zoomBass = 0.02f; c->rotation = 0.0015f;
        c->warpAmount = 0.006f; c->warpScale = 4; c->warpSpeed = 0.6f; c->decay = 0.965f;
        c->driftY = -0.002f; c->hueShift = 0.003f;
        c->wave = WaveStyle::Line; c->thickness = 1.4f; c->baseHue = 0.33f;

        c = &s[4]; c->name = "Kaleido Bloom";
        c->zoom = 1.012f; c->zoomBass = 0.035f; c->rotation = 0.006f; c->rotationBass = 0.015f;
        c->warpAmount = 0.003f; c->decay = 0.95f; c->kaleido = 6; c->hueShift = 0.008f;
        c->wave = WaveStyle::Circle; c->centerRadius = 0.03f; c->waveScale = 0.8f; c->thickness = 1.1f;
        c->baseHue = 0.9f; c->hueSpeed = 0.06f;

        c = &s[5]; c->name = "Liquid Plasma";
        c->zoom = 1.004f; c->zoomBass = 0.025f; c->rotation = 0.001f;
        c->warpAmount = 0.012f; c->warpScale = 5; c->warpSpeed = 1.3f; c->decay = 0.975f;
        c->hueShift = 0.005f; c->plasma = 1;
        c->wave = WaveStyle::TwinRings; c->centerRadius = 0.03f; c->waveScale = 0.7f; c->thickness = 0.8f;
        c->baseHue = 0.6f; c->saturation = 0.75f;

        c = &s[6]; c->name = "Vortex";
        c->zoom = 1.03f; c->zoomBass = 0.04f; c->rotation = 0.025f; c->rotationBass = 0.04f;
        c->warpAmount = 0.002f; c->decay = 0.94f; c->hueShift = 0.012f;
        c->wave = WaveStyle::SpiralDots; c->centerRadius = 0; c->thickness = 1.2f; c->baseHue = 0.15f; c->hueSpeed = 0.08f;

        c = &s[7]; c->name = "Lissajous Neon";
        c->zoom = 1.008f; c->zoomBass = 0.02f; c->rotation = -0.004f;
        c->warpAmount = 0.004f; c->decay = 0.96f; c->mirror = 1; c->hueShift = 0.004f;
        c->wave = WaveStyle::Lissajous; c->waveScale = 1.2f; c->thickness = 1.1f; c->baseHue = 0.45f; c->saturation = 1;

        c = &s[8]; c->name = "Mirror Hall";
        c->zoom = 1.015f; c->zoomBass = 0.03f; c->rotation = 0.003f;
        c->warpAmount = 0.005f; c->warpScale = 8; c->decay = 0.955f; c->mirror = 2; c->hueShift = 0.007f;
        c->wave = WaveStyle::VerticalLine; c->waveScale = 0.9f; c->thickness = 1.2f; c->baseHue = 0.8f;

        c = &s[9]; c->name = "Black Hole";
        c->zoom = 0.975f; c->zoomBass = -0.03f; c->rotation = -0.012f; c->rotationBass = -0.02f;
        c->warpAmount = 0.003f; c->decay = 0.965f; c->hueShift = -0.006f;
        c->wave = WaveStyle::TwinRings; c->centerRadius = 0.2f; c->waveScale = 1.25f;
        c->baseHue = 0.02f; c->hueSpeed = 0.02f; c->vignette = 0.5f;

        // ---- Classic favorites ----
        c = &s[10]; c->name = "Bass Ring";
        c->zoom = 1.006f; c->zoomBass = 0.01f; c->warpAmount = 0; c->decay = 0.8f; c->hueShift = 0.002f;
        c->wave = WaveStyle::BassRing; c->baseHue = 0.55f; c->hueSpeed = 0.03f; c->saturation = 0.85f; c->vignette = 0.45f;

        c = &s[11]; c->name = "Neon Equalizer";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0; c->decay = 0.55f; c->hueShift = 0;
        c->wave = WaveStyle::NeonEqualizer; c->baseHue = 0.8f; c->hueSpeed = 0.02f; c->vignette = 0.3f;

        c = &s[12]; c->name = "Pulsar Ridges";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0; c->decay = 0.25f; c->hueShift = 0;
        c->wave = WaveStyle::PulsarRidges; c->baseHue = 0; c->hueSpeed = 0; c->saturation = 0; c->vignette = 0.3f;

        c = &s[13]; c->name = "Outrun Grid";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0; c->decay = 0.55f; c->hueShift = 0;
        c->wave = WaveStyle::OutrunGrid; c->baseHue = 0.86f; c->hueSpeed = 0; c->vignette = 0.35f;

        c = &s[14]; c->name = "Hyperspace";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0; c->decay = 0.7f; c->hueShift = 0;
        c->wave = WaveStyle::Hyperspace; c->baseHue = 0.6f; c->hueSpeed = 0.01f; c->saturation = 0.35f; c->vignette = 0.4f;

        c = &s[15]; c->name = "Particle Storm";
        c->zoom = 1.0f; c->zoomBass = 0.004f; c->warpAmount = 0.002f; c->decay = 0.88f; c->hueShift = 0.003f;
        c->wave = WaveStyle::ParticleStorm; c->baseHue = 0.5f; c->hueSpeed = 0.04f; c->saturation = 0.8f;

        c = &s[16]; c->name = "Lightning Storm";
        c->zoom = 1.006f; c->zoomBass = 0.01f; c->warpAmount = 0.003f; c->decay = 0.78f; c->hueShift = 0; c->plasma = 0.35f;
        c->wave = WaveStyle::Lightning; c->baseHue = 0.66f; c->hueSpeed = 0.01f; c->saturation = 0.6f; c->vignette = 0.5f;

        c = &s[17]; c->name = "Digital Rain";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0; c->decay = 0.6f; c->hueShift = 0;
        c->wave = WaveStyle::DigitalRain; c->baseHue = 0.33f; c->hueSpeed = 0;

        c = &s[18]; c->name = "Lava Lamp";
        c->zoom = 1.0f; c->zoomBass = 0.004f; c->warpAmount = 0.012f; c->warpScale = 3; c->warpSpeed = 0.5f; c->decay = 0.9f;
        c->hueShift = 0.002f; c->wave = WaveStyle::LavaLamp; c->baseHue = 0.95f; c->hueSpeed = 0.015f; c->saturation = 0.85f;
        c->vignette = 0.4f;

        c = &s[19]; c->name = "Inferno";
        c->zoom = 1.0f; c->zoomBass = 0; c->warpAmount = 0.007f; c->warpScale = 10; c->warpSpeed = 2.5f; c->decay = 0.9f;
        c->driftY = -0.008f; c->hueShift = 0; c->wave = WaveStyle::Inferno; c->baseHue = 0.05f; c->hueSpeed = 0;
        c->saturation = 0.95f; c->vignette = 0.45f;
        return s;
    }();
    return scenes;
}
