# How It Works

## How it stays fast

Most visualizers feel laggy because of how they listen and analyze. This one:

1. **Reads each hardware audio buffer directly** with `AVAudioSinkNode` (or a Core Audio process tap for app audio) and asks Core Audio for 128-frame buffers (about 2.7 ms). The common `installTap` approach delivers audio in 100–400 ms chunks.
2. **Tracks bass sample by sample** using a 4th-order 140 Hz low-pass filter and an envelope follower with a 1 ms attack. Logo motion never waits for an FFT window to fill. The FFT is only used for drawing spectrum bars.
3. **Jumps instantly on a hit and only smooths the release.** On each detected beat, part of the logo's squash, bounce, slide and wobble is applied on that very frame, and stiff, damped springs carry the follow-through, so the motion peaks on the kick instead of a few frames later.
4. **Renders at the display's refresh rate**, up to 120 Hz on ProMotion. It reads the newest audio values right before each frame and uses only two drawables to avoid an extra frame of queueing.

On Windows the same design uses **WASAPI**: `IAudioClient3` asks for the smallest shared-mode period the driver allows, and the capture thread is registered with MMCSS ("Pro Audio") so it isn't starved. For **System audio**, Dropocalypse also opens a silent helper stream on the same output at the driver's smallest engine period, so Windows mixes, and delivers loopback audio, in chunks of about 3 ms instead of the default 10 ms (on drivers that support it). Rendering uses a flip-model **Direct3D 11** swap chain with a frame-latency waitable object and a maximum latency of one frame.

## Listening to apps on the Mac

On macOS 14.2+, Dropocalypse creates a Core Audio **process tap** (`CATapDescription`) for the chosen app's processes (helper processes such as a browser's renderers are folded in), or a global tap for all Mac audio that excludes itself. The tap is wrapped in a private aggregate device and read with an IOProc on the HAL's real-time thread, so latency stays at one small hardware buffer. It watches for the app starting or stopping audio and for output-device changes, and rebuilds the tap when needed.

## The render pipeline

Every frame goes through the same three steps on both platforms (Metal on Mac, Direct3D 11 on Windows):

1. **Feedback warp:** last frame's image is zoomed, rotated, warped, mirrored or kaleidoscoped, color-shifted and faded into a pair of ping-pong `rgba16Float` textures. This is what makes MilkDrop-style trails and tunnels.
2. **Shapes:** each style draws waveforms, spectrum bars, particles, ridges, grids or the tesseract as triangles: an additive batch for glowing light, plus an alpha-blended batch for solid shapes such as the Outrun sun.
3. **Composite and logo:** the feedback image is presented with vignette and beat flash, then the logo is drawn on top with its spring-driven transform, glow, tint and optional trails fed back into the feedback texture.

Scenes are plain parameter sets (zoom, rotation, warp, decay, mirror, colors and a wave style), so switching styles is a 2.5-second crossfade between two sets of numbers.

## Logo motion

The logo's pulse, squash, bounce, slide and wobble are stiff, damped springs. Beat onsets (bass jumping above its recent average by the **Beat threshold**, with a refractory period) give the springs a kick, while the bass envelope drives the continuous pulse.

## Platform notes

| | Mac | Windows |
|---|---|---|
| Language | Swift, SwiftUI + AppKit | C++17, Win32 + Dear ImGui |
| Audio | AVAudioEngine sink node, Core Audio process taps | WASAPI (IAudioClient3), loopback for system audio |
| Graphics | Metal (shaders compiled at launch) | Direct3D 11, flip-model swap chain |
| Settings | UserDefaults | `%APPDATA%\Dropocalypse\settings.ini` |
