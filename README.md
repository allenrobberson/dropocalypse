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

**→ [Download](#download) · [User guide](#user-guide) · [Build from source](#building-from-source)**

## Download

1. Go to the **[latest release](../../releases/latest)** and download the `.dmg`.
2. Open it and drag **Dropocalypse** into **Applications**.
3. **First launch:** the app is free and isn't notarized by Apple, so macOS blocks it the first time.
   - **macOS 15 Sequoia and later:** try to open it once, then go to **System Settings → Privacy & Security**, scroll down, and click **Open Anyway**.
   - **macOS 13–14:** right-click the app → **Open** → **Open**.
   - Or run this in Terminal: `xattr -dr com.apple.quarantine "/Applications/Dropocalypse.app"`
4. Allow **microphone** access when asked. Audio is analyzed live and is never recorded or sent anywhere.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

## User guide

- [Quick start](#quick-start)
- [The toolbar](#the-toolbar)
- [Logo menu](#logo-menu)
- [Style controls](#style-controls)
- [Motion panel](#motion-panel)
- [Logo panel](#logo-panel)
- [Audio panel](#audio-panel)
- [Look panel](#look-panel)
- [The 10 styles](#the-10-styles)
- [Keyboard shortcuts and menus](#keyboard-shortcuts-and-menus)
- [Setting up for a live show](#setting-up-for-a-live-show)
- [Troubleshooting](#troubleshooting)

### Quick start

1. Open **Dropocalypse** and allow microphone access.
2. Play some music. The visuals start reacting right away.
3. Drag your logo (PNG, JPG, PDF or SVG) onto the window, or press **L** to pick a file.
4. Press **F** for full screen.

Your logo, style and all settings are saved automatically and come back the next time you open the app.

### The toolbar

Every control lives in one slim bar along the **top edge** of the window.

- **To show it:** move the mouse toward the top of the screen, click anywhere, or press **T**.
- **It hides itself** after about 2.5 seconds without activity, and the mouse cursor hides with it, leaving a clean picture.
- **It stays open** while your pointer is over it or while one of its panels is open.
- **To hide it right away:** press **Esc**.

From left to right:

| Control | What it does |
|---|---|
| 🖼 **Add Logo / logo name** | Opens the [Logo menu](#logo-menu) |
| **‹  Style name  ›** | Previous style, pick a style from the list, next style |
| ⟳ | Turns auto-cycle on or off (lit = on) |
| **Motion** | Opens the [Motion panel](#motion-panel): how the logo reacts to the bass |
| **Logo** | Opens the [Logo panel](#logo-panel): size, glow, tint, trails |
| **Audio** | Opens the [Audio panel](#audio-panel): input device and sensitivity. The mic icon is crossed out when audio isn't running. |
| **Look** | Opens the [Look panel](#look-panel): flash, brightness, auto-cycle timing |
| *Status text* | Shows the input being used, its sample rate and the audio buffer size (lower ms = faster reaction) |
| ⌨ | Shows the keyboard shortcuts |
| ⤢ | Full screen on / off |

### Logo menu

| Item | What it does |
|---|---|
| **Choose Logo… (L)** | Pick an image file. PNGs with a transparent background look best. Empty transparent edges are trimmed automatically so the logo bounces around its visible shape. |
| **Remove Logo** | Removes your logo and goes back to the "YOUR LOGO" placeholder |
| **Show Logo (G)** | Hides or shows the logo without removing it, so you get visuals only |

You can also **drag an image** from Finder, or straight from a web browser, onto the window at any time.

### Style controls

| Control | What it does |
|---|---|
| **‹** / **›** | Previous / next style. You can also use the ← → arrow keys. |
| **Style name** | Click it to jump straight to any of the [10 styles](#the-10-styles) |
| ⟳ **Auto-cycle** | When on, the app moves to the next style every 30 seconds (adjustable under **Look**), with a smooth 2.5-second crossfade. Choosing a style by hand restarts the timer. |

### Motion panel

These sliders control how your logo reacts to the bass. **0 turns a motion off completely**; the highest setting is 1.5.

| Slider | Default | What it does |
|---|---|---|
| **Pulse** | 0.70 | The logo grows with the bass and shrinks back as it fades. This is the most direct "breathing" motion. |
| **Squash & stretch** | 0.60 | On each kick the logo squashes wide and short, then springs back like rubber. Also stretches it taller during sustained bass. |
| **Bounce** | 0.50 | Kicks launch the logo upward and it falls back with gravity. A small squash happens when it lands. |
| **Slide** | 0.40 | Each beat pushes the logo left or right, alternating sides, and a spring pulls it back to center. Adds a slow side-to-side drift too. |
| **Wobble** | 0.35 | Each beat tips the logo into a quick rotation that settles back to level |
| **Reset to defaults** | | Puts the Motion, Logo and Look sliders (except auto-cycle timing) back to their defaults |

> **Tip:** for a calm, branded look, try Pulse 0.5 and everything else at 0. For a festival-style "everything goes off" look, push Squash, Bounce and Wobble to 1.2 or higher.

### Logo panel

| Control | Default | What it does |
|---|---|---|
| **Size** | 0.32 | How big the logo is, as a fraction of the screen height (0.08 to 0.8). The longer side of the logo is fitted to this size. |
| **Glow** | 0.60 | A soft, colored halo behind the logo that flares brighter with the bass. 0 turns it off. |
| **Color tint** | 0.00 | Blends the logo toward the current style's color. 0 keeps your logo's original colors; 1 recolors it completely. |
| **Logo trails** | Off | Feeds a faint copy of the logo into the visuals every frame, so it leaves echoes that swirl, zoom and fade with the effect |
| **Show logo** | On | Same as **Show Logo** in the Logo menu |

### Audio panel

| Control | Default | What it does |
|---|---|---|
| **Input** | System default | Which microphone or audio input to listen to: built-in mic, USB mic, audio interface, line-in, and so on |
| **Refresh devices** | | Updates the list after you plug something in |
| **Restart audio** | | Restarts listening. Use it if the visuals stop reacting after a device change or sleep. |
| **Sensitivity** | 1.00 | How easily quiet audio drives the visuals (0.2 to 4). Raise it in a quiet room or when the mic is far from the speakers. Lower it if room noise makes things twitch when no music is playing. |
| **Beat threshold** | 1.35× | How much the bass has to jump above its recent average to count as a "beat" for squash, bounce, slide, wobble and flash (1.1× to 2.2×). **Lower = more beats**, so it reacts to more of the music. **Higher = only big kicks** count. |
| *Status line* | | The active input, sample rate and buffer size, or a message if something is wrong |

The app adjusts its own levels to the loudness of the input (automatic gain), so it works with both a quiet room mic and a hot line-level signal without manual tweaking.

### Look panel

| Control | Default | What it does |
|---|---|---|
| **Beat flash** | 0.40 | The whole screen brightens briefly on each beat. 0 turns it off. |
| **Brightness** | 1.00 | Overall brightness of the visuals (0.4 to 1.6). Lower it for dark rooms or bright projectors. |
| **Auto-cycle every** | 30 s | How long each style stays up when auto-cycle is on (5 to 120 seconds) |

### The 10 styles

| # | Style | What it looks like |
|---|---|---|
| 1 | **Classic Tunnel** | The waveform bursts out of the center and streams outward into an endless, slowly twisting tunnel. The classic Winamp look. |
| 2 | **Spectrum Skyline** | Spectrum bars along the bottom, with trails that rise up the screen like smoke |
| 3 | **Starburst** | Rays shoot straight out of the center, one per frequency band, and explode outward on the bass |
| 4 | **Oscilloscope Drift** | A glowing horizontal waveform that ripples, rotates slowly and leaves drifting trails |
| 5 | **Kaleido Bloom** | A six-way kaleidoscope that blooms outward from the center, spinning faster with the bass |
| 6 | **Liquid Plasma** | Flowing, liquid plasma colors with waveform rings melting into them |
| 7 | **Vortex** | A three-armed spiral of dots that whips around the center, flinging trails outward in a fast spin |
| 8 | **Lissajous Neon** | A mirrored neon scribble drawn by the sound wave itself; smooth tones make loops, noisy sounds make chaos |
| 9 | **Mirror Hall** | Two vertical waveforms reflected four ways into an infinite hall of mirrors |
| 10 | **Black Hole** | Everything is pulled inward and spirals down into the center |

All styles use the bass to zoom, spin and ripple harder. The mids add extra warp.

### Keyboard shortcuts and menus

| Key | Action |
|---|---|
| `F` | Full screen on / off |
| `T` or `S` | Show / hide the toolbar |
| `Esc` | Hide the toolbar |
| `L` | Choose a logo |
| `G` | Show / hide the logo |
| `←` `→` | Previous / next style |
| `A` | Auto-cycle on / off |

The menu bar has the same actions: **File → Choose Logo… (⌘O)** and **Remove Logo**, and **Visualizer → Next Style (⌘→), Previous Style (⌘←), Show/Hide Toolbar (⌘,), Restart Audio**.

### Setting up for a live show

1. **Use a direct feed if you can.** Run the booth, record or aux output of your mixer or DJ controller into a USB audio interface, then pick that interface under **Audio → Input**. A direct feed reacts more cleanly than a mic hearing a loud room.
2. **Using a mic instead?** Point it toward the speakers or subwoofer. Raise **Sensitivity** if the room is quiet; lower it if crowd noise is driving the visuals.
3. **Tune the beat.** Watch the logo during a track with a clear kick. If it misses kicks, lower **Beat threshold**. If it jumps on hi-hats or vocals, raise it.
4. **Go full screen** on the projector or TV: drag the window onto that screen and press **F**. In full screen, the Mac's menu bar and Dock are hidden so they don't cover the visuals.
5. Turn **auto-cycle** on with 30 to 60 seconds per style so the show keeps changing without you touching it.

### Troubleshooting

| Problem | Fix |
|---|---|
| **"Dropocalypse can't be opened"** on first launch | The app isn't notarized by Apple. See step 3 under [Download](#download). |
| **Nothing reacts to the music** | Check that microphone access is on: **System Settings → Privacy & Security → Microphone → Dropocalypse**. Then use **Audio → Restart audio** and make sure the right input is selected. |
| **It reacts, but only a little** | Raise **Sensitivity** and lower **Beat threshold**. Check that the input's level isn't turned down in **System Settings → Sound → Input**. |
| **It twitches when no music is playing** | Lower **Sensitivity**, or switch to a direct line-in feed |
| **My USB interface isn't listed** | Plug it in, then click **Refresh devices** in the Audio panel |
| **My logo looks blurry** | Use a larger image; around 1000 px on the long side is ideal |
| **My logo has a white box around it** | The image has a solid background. Export it as a PNG with a transparent background. |
| **The toolbar won't appear** | Move the mouse to the very top of the window, click anywhere, or press **T** |
| **Settings or logo reset after updating from "Logo Visualizer"** | The app was renamed. Your saved logo is copied over automatically, but slider settings start fresh, and macOS asks for microphone permission again. |

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
