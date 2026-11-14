# Dropocalypse

**The drop hits. Your logo goes off.**

A free, open-source, Winamp / MilkDrop-style music visualizer for macOS that **makes your logo dance to the bass**.

Play music on any speaker. The Mac listens through its microphone or a line-in, and the visuals swirl, zoom and pulse. Your logo sits on top and **pulses, squashes and stretches, bounces, slides and wobbles** on every kick drum.

- 🎛 **10 built-in visual styles**: Classic Tunnel, Spectrum Skyline, Starburst, Oscilloscope Drift, Kaleido Bloom, Liquid Plasma, Vortex, Lissajous Neon, Mirror Hall and Black Hole. Pick one or let them auto-cycle with smooth crossfades.
- 🖼 **Your logo**: drag any image onto the window. Transparent PNGs look best. It's remembered between launches.
- 🥁 **Bass-driven motion**: separate sliders for pulse, squash & stretch, bounce, slide and wobble, plus glow, tint and "logo trails" that feed the logo into the visuals.
- ⚡ **Low latency**: roughly 10–20 ms from sound to screen (see [How it stays fast](#how-it-stays-fast)).
- 🎚 **Any input**: built-in mic, USB mic, audio interface or line-in, with a device picker.
- 🖥 **Full screen**, with an auto-hiding toolbar along the top edge for projectors, TVs and DJ booths.

## Download

1. Go to the **[latest release](../../releases/latest)** and download the `.dmg`.
2. Open it and drag **Dropocalypse** into **Applications**.
3. **First launch:** the app is free and isn't notarized by Apple, so macOS blocks it the first time.
   - **macOS 15 Sequoia and later:** try to open it once, then go to **System Settings → Privacy & Security**, scroll down, and click **Open Anyway**.
   - **macOS 13–14:** right-click the app → **Open** → **Open**.
   - Or run this in Terminal: `xattr -dr com.apple.quarantine "/Applications/Dropocalypse.app"`
4. Allow **microphone** access when asked. Audio is analyzed live and is never recorded or sent anywhere.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

## Using it

Move the mouse to the **top edge** (or click anywhere) and the toolbar slides down. It hides itself again after a couple of seconds.

| Toolbar | What it does |
|---|---|
| **Add Logo** | Choose, remove or hide your logo |
| **‹ Style name ›** | Switch between the 10 styles; ⟳ turns auto-cycle on/off |
| **Motion** | Pulse, squash & stretch, bounce, slide, wobble |
| **Logo** | Size, glow, color tint, logo trails |
| **Audio** | Input device, sensitivity, beat threshold |
| **Look** | Beat flash, brightness, auto-cycle timing |

| Key | Action |
|---|---|
| `F` | Full screen |
| `T` or `S` | Show / hide toolbar |
| `Esc` | Hide toolbar |
| `L` | Choose logo |
| `G` | Show / hide logo |
| `←` `→` | Previous / next style |
| `A` | Auto-cycle on / off |

**Tips**
- For the cleanest signal, plug the mixer or DJ controller's booth or record output into a USB audio interface and pick it under **Audio**.
- If the logo barely moves, raise **Sensitivity** or lower **Beat threshold**. If it reacts to everything, do the opposite.

## How it stays fast

Most visualizers feel laggy because of how they listen and analyze. This one:

1. **Reads each hardware audio buffer directly** with `AVAudioSinkNode` and asks Core Audio for 256-frame buffers (about 5 ms). The common `installTap` approach delivers audio in 100–400 ms chunks.
2. **Tracks bass sample by sample** using a 4th-order 140 Hz low-pass filter and an envelope follower with a 1 ms attack. Logo motion never waits for an FFT window to fill. The FFT is only used for drawing spectrum bars.
3. **Jumps instantly on a hit and only smooths the release**, with beat impulses driving stiff, damped springs.
4. **Renders at the display's refresh rate**, up to 120 Hz on ProMotion. It reads the newest audio values right before each frame and uses only two drawables to avoid an extra frame of queueing.

## Building from source

```bash
git clone https://github.com/allenrobberson/dropocalypse.git
cd dropocalypse
swift run -c release            # quick run
./scripts/build-app.sh          # builds dist/Dropocalypse.app, .zip and .dmg
```

You need Xcode 15 or later (or the Command Line Tools) on macOS 13 or later. There are no third-party dependencies.

**Releases are automatic.** GitHub Actions builds every push to `main`. To publish a new downloadable version, do either of these:
- On GitHub, go to **Actions → Build → Run workflow**, type a version such as `0.2.0`, and click **Run workflow**.
- Or push a tag: `git tag v0.2.0 && git push origin v0.2.0`

Either way, a GitHub Release is published with the `.dmg` and `.zip` attached.

### Project layout

```
Sources/Dropocalypse/
  Audio/AudioCapture.swift    mic / line-in capture, device list, small hardware buffers
  Audio/AudioAnalysis.swift   real-time filters, envelopes, auto-gain, beat detection
  Render/Renderer.swift       Metal pipeline: feedback warp → shapes → composite + logo
  Render/ShaderSource.swift   all Metal shaders
  Render/Scenes.swift         the 10 built-in styles (add your own here!)
  Render/WaveDrawer.swift     waveform / spectrum / dots geometry
  Render/LogoMotion.swift     spring physics for pulse, squash, bounce, slide, wobble
  Views/                      SwiftUI window and auto-hiding toolbar
```

### Adding a style

Add a `VisualScene(...)` entry to `VisualScene.builtIn` in `Scenes.swift`. Each style is a handful of numbers: zoom, rotation, warp, trail decay, mirror and kaleidoscope settings, wave style and colors. Pull requests with new styles are very welcome.

## Roadmap

- [ ] Load MilkDrop `.milk` presets through [projectM](https://github.com/projectM-visualizer/projectm) (LGPL), including the ~10,000-preset "Cream of the Crop" pack
- [ ] Capture the Mac's own audio output (Spotify, YouTube…) without a microphone
- [ ] Multiple logos and text overlays
- [ ] Syphon / NDI output for VJ software
- [ ] App icon and notarized builds

## License

[MIT](LICENSE): free to use, modify and share, including commercially.

Inspired by Ryan Geiss's MilkDrop and Nullsoft's Winamp. All visuals in this app are original code; no MilkDrop presets or Shadertoy shaders are bundled.
