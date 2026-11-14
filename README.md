# Dropocalypse

**The drop hits. Your logo goes off.**

A free, open-source, Winamp / MilkDrop-style music visualizer for **Mac and Windows** that **makes your logo dance to the bass**.

Play music and the visuals swirl, zoom and pulse. It listens through a microphone or line-in, or on Windows, straight to whatever the PC is playing. Your logo sits on top and **pulses, squashes and stretches, bounces, slides and wobbles** on every kick drum.

- 🎛 **20 built-in visual styles**: 10 Winamp/MilkDrop-style originals plus 10 takes on the most-loved visualizer looks: a bass ring around your logo, neon equalizer bars, ridgelines, a synthwave grid, hyperspace, a particle storm, lightning, digital rain, a lava lamp and flames. Pick one or let them auto-cycle with smooth crossfades.
- 🖼 **Your logo**: drag any image onto the window. Transparent PNGs look best. It's remembered between launches.
- 🥁 **Bass-driven motion**: separate sliders for pulse, squash & stretch, bounce, slide and wobble, plus glow, tint and "logo trails" that feed the logo into the visuals.
- ⚡ **Low latency**: roughly 10–20 ms from sound to screen (see [How it stays fast](#how-it-stays-fast)).
- 🎚 **Any input**: built-in mic, USB mic, audio interface or line-in, with a device picker. On Windows it can also capture **what the PC is playing**, with no mic needed.
- 🖥 **Full screen**, with an auto-hiding toolbar along the top edge for projectors, TVs and DJ booths.

**→ [Download](#download) · [User guide](#user-guide) · [Build from source](#building-from-source)**

## Download

Get both versions from the **[latest release](../../releases/latest)**.

### 🍎 Mac

1. Download the `macOS.dmg`, open it, and drag **Dropocalypse** into **Applications**.
2. **First launch:** the app is free and isn't notarized by Apple, so macOS blocks it the first time.
   - **macOS 15 Sequoia and later:** try to open it once, then go to **System Settings → Privacy & Security**, scroll down, and click **Open Anyway**.
   - **macOS 13–14:** right-click the app → **Open** → **Open**.
   - Or run this in Terminal: `xattr -dr com.apple.quarantine "/Applications/Dropocalypse.app"`
3. Allow **microphone** access when asked. Audio is analyzed live and is never recorded or sent anywhere.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

### 🪟 Windows (portable: no install, no admin rights)

1. Download the `Windows-portable.zip`, right-click it → **Extract All**, and open the folder.
2. Double-click **Dropocalypse.exe**. Nothing gets installed, and you can keep the folder anywhere, even on a USB stick.
3. **First launch:** if Windows says *"Windows protected your PC"*, click **More info → Run anyway**. This happens because the free app isn't code-signed.
4. By default it listens to **what your PC is playing** (Spotify, YouTube, DJ software), so you don't need a microphone. To use a mic or line-in, pick it under **Audio** in the toolbar. For a mic, turn on **Settings → Privacy & security → Microphone → Let desktop apps access your microphone**.

Requires Windows 10 or 11 (64-bit). Settings and your logo are saved in `%APPDATA%\Dropocalypse`. To remove the app, delete that folder and the app folder.

## User guide

- [Quick start](#quick-start)
- [The toolbar](#the-toolbar)
- [Logo menu](#logo-menu)
- [Style controls](#style-controls)
- [Motion panel](#motion-panel)
- [Logo panel](#logo-panel)
- [Audio panel](#audio-panel)
- [Look panel](#look-panel)
- [The 20 styles](#the-20-styles)
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
| **‹  Style name  ›** | Previous style, pick any of the 20 styles from the list, next style |
| ⟳ | Turns auto-cycle on or off (lit = on) |
| **Motion** | Opens the [Motion panel](#motion-panel): how the logo reacts to the bass |
| **Logo** | Opens the [Logo panel](#logo-panel): size, glow, tint, trails |
| **Audio** | Opens the [Audio panel](#audio-panel): input device and sensitivity. The mic icon is crossed out when audio isn't running. |
| **Look** | Opens the [Look panel](#look-panel): flash, brightness, auto-cycle timing |
| *Status text* | Shows the input being used, its sample rate and the audio buffer size (lower ms = faster reaction) |
| ⌨ / **Keys** | Shows the keyboard shortcuts |
| ⤢ / **Full screen** | Full screen on / off |

> On Windows the toolbar shows text labels (**< Style >**, **Auto-cycle**, **Keys**, **Full screen**) instead of icons, but the controls are the same.

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
| **Style name** | Click it to jump straight to any of the [20 styles](#the-20-styles) |
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
| **Input** | Mac: System default<br>Windows: System audio | Which audio to listen to: built-in mic, USB mic, audio interface or line-in. **Windows only:** *System audio (what this PC is playing)* listens directly to the PC's sound output, so no mic is needed, and *Playing on: …* listens to one specific output device. |
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

### The 20 styles

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

**Classic favorites:** our own versions of the visualizer looks people love most, from YouTube music channels to iTunes and synthwave.

| # | Style | What it looks like |
|---|---|---|
| 11 | **Bass Ring** | A circular spectrum that wraps around your logo and follows it, with sparks bursting off on every kick. It sizes itself to your logo automatically. |
| 12 | **Neon Equalizer** | Smooth neon spectrum bars across the screen with a glassy reflection underneath |
| 13 | **Pulsar Ridges** | Stacked white ridgelines that scroll up the screen, with the bass rising in the middle: the classic "joy plot" look |
| 14 | **Outrun Grid** | A synthwave sunset: striped sun, glowing horizon and a neon grid floor that races toward you faster when the bass hits |
| 15 | **Hyperspace** | A starfield that jumps to warp speed on the drops |
| 16 | **Particle Storm** | Hundreds of glowing particles flowing in currents around the logo, scattering outward on every beat |
| 17 | **Lightning Storm** | Lightning bolts crack out from behind the logo on the beat, over a stormy plasma sky |
| 18 | **Digital Rain** | Columns of glowing green glyphs raining down, falling faster with the bass |
| 19 | **Lava Lamp** | Big soft blobs of color drifting and melting into each other, swelling with the bass |
| 20 | **Inferno** | Embers and flames rising from the bottom of the screen, flaring up with each frequency band |

All styles react to the bass. Most also zoom, spin or ripple harder as it builds, and the mids add extra warp.

> **Tip:** **Bass Ring** and **Particle Storm** are built around a centered logo, so try turning **Bounce** and **Slide** down so the logo stays put. For **Pulsar Ridges** and **Outrun Grid**, a smaller **Size** (around 0.2) keeps the logo from covering the scene.

### Keyboard shortcuts and menus

| Key | Action |
|---|---|
| `F` | Full screen on / off |
| `T` or `S` | Show / hide the toolbar |
| `Esc` | Hide the toolbar (Windows: press again to exit full screen) |
| `F11` | Full screen (Windows) |
| `Ctrl+O` | Choose a logo (Windows) |
| `L` | Choose a logo |
| `G` | Show / hide the logo |
| `←` `→` | Previous / next style |
| `A` | Auto-cycle on / off |

On the Mac, the menu bar has the same actions: **File → Choose Logo… (⌘O)** and **Remove Logo**, and **Visualizer → Next Style (⌘→), Previous Style (⌘←), Show/Hide Toolbar (⌘,), Restart Audio**.

### Setting up for a live show

1. **Playing from the same Windows PC?** Pick *System audio* under **Audio**. It's the cleanest signal there is, with no cables or mic.
2. **Use a direct feed if you can.** Run the booth, record or aux output of your mixer or DJ controller into a USB audio interface, then pick that interface under **Audio → Input**. A direct feed reacts more cleanly than a mic hearing a loud room.
3. **Using a mic instead?** Point it toward the speakers or subwoofer. Raise **Sensitivity** if the room is quiet; lower it if crowd noise is driving the visuals.
4. **Tune the beat.** Watch the logo during a track with a clear kick. If it misses kicks, lower **Beat threshold**. If it jumps on hi-hats or vocals, raise it.
5. **Go full screen** on the projector or TV: drag the window onto that screen and press **F**. In full screen, the Mac's menu bar and Dock are hidden so they don't cover the visuals.
6. Turn **auto-cycle** on with 30 to 60 seconds per style so the show keeps changing without you touching it.

### Troubleshooting

| Problem | Fix |
|---|---|
| **Mac: "Dropocalypse can't be opened"** on first launch | The app isn't notarized by Apple. See [Download → Mac](#-mac). |
| **Windows: "Windows protected your PC"** | Click **More info → Run anyway**. The app isn't code-signed. |
| **Windows: nothing happens when I double-click** | Make sure you extracted the zip first; don't run the app from inside it. On work or school PCs, IT policies can block unapproved apps, so ask your IT team. |
| **Windows: visuals don't react to Spotify / YouTube** | Under **Audio**, choose *System audio (what this PC is playing)*. If you use headphones and speakers, pick the *Playing on: …* entry for the device you're listening on. |
| **Nothing reacts to the music** | Check that microphone access is on. **Mac:** System Settings → Privacy & Security → Microphone → Dropocalypse. **Windows:** Settings → Privacy & security → Microphone → *Let desktop apps access your microphone*. Then use **Audio → Restart audio** and make sure the right input is selected. |
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

On Windows the same design uses **WASAPI**: `IAudioClient3` asks for the smallest shared-mode period the driver allows, and the capture thread is registered with MMCSS ("Pro Audio") so it isn't starved. Rendering uses a flip-model **Direct3D 11** swap chain with a frame-latency waitable object and a maximum latency of one frame.

## Building from source

```bash
git clone https://github.com/allenrobberson/dropocalypse.git
cd dropocalypse
swift run -c release            # quick run
./scripts/build-app.sh          # builds dist/Dropocalypse.app, .zip and .dmg
```

You need Xcode 15 or later (or the Command Line Tools) on macOS 13 or later. There are no third-party dependencies.

**Windows** (Visual Studio 2022 with the "Desktop development with C++" workload, plus CMake):

```powershell
cmake -S windows -B build -A x64
cmake --build build --config Release
build\Release\Dropocalypse.exe
```

The Windows build downloads [Dear ImGui](https://github.com/ocornut/imgui) (MIT) for the toolbar and links the C runtime statically, so the `.exe` runs without installing anything.

**Releases are automatic.** GitHub Actions builds every push to `main`. To publish a new downloadable version, do either of these:
- On GitHub, go to **Actions → Build → Run workflow**, type a version such as `0.2.0`, and click **Run workflow**.
- Or push a tag: `git tag v0.2.0 && git push origin v0.2.0`

Either way, the Mac and Windows versions are built side by side and published together in one GitHub Release.

### Project layout

```
Sources/Dropocalypse/
  Audio/AudioCapture.swift    mic / line-in capture, device list, small hardware buffers
  Audio/AudioAnalysis.swift   real-time filters, envelopes, auto-gain, beat detection
  Render/Renderer.swift       Metal pipeline: feedback warp → shapes → composite + logo
  Render/ShaderSource.swift   all Metal shaders
  Render/Scenes.swift         the 20 built-in styles (add your own here!)
  Render/StyleFX.swift        particles, stars, lightning, ridges, rain for styles 11-20
  Render/WaveDrawer.swift     waveform / spectrum / dots geometry
  Render/LogoMotion.swift     spring physics for pulse, squash, bounce, slide, wobble
  Views/                      SwiftUI window and auto-hiding toolbar

windows/
  src/audio.cpp               WASAPI capture (mic, line-in, system-audio loopback) + the same DSP
  src/renderer.cpp            Direct3D 11 pipeline, styles, logo physics
  src/shaders.h               HLSL port of the Metal shaders
  src/main.cpp                window, swap chain, auto-hiding toolbar, settings
  app.manifest                "asInvoker": runs without admin rights
```

### Adding a style

Add a `VisualScene(...)` entry to `VisualScene.builtIn` in `Scenes.swift` (Mac) and the matching entry in `windows/src/scenes.h` (Windows). Each style is a handful of numbers: zoom, rotation, warp, trail decay, mirror and kaleidoscope settings, wave style and colors. Pull requests with new styles are very welcome.

## Roadmap

- [ ] Load MilkDrop `.milk` presets through [projectM](https://github.com/projectM-visualizer/projectm) (LGPL), including the ~10,000-preset "Cream of the Crop" pack
- [x] Windows version (portable, no admin rights)
- [x] Capture the PC's own audio output without a microphone (Windows)
- [ ] Capture the Mac's own audio output (Spotify, YouTube…) without a microphone
- [ ] Multiple logos and text overlays
- [ ] Syphon / NDI output for VJ software
- [ ] App icon, notarized Mac builds and code-signed Windows builds

## License

[MIT](LICENSE): free to use, modify and share, including commercially.

Inspired by Ryan Geiss's MilkDrop and Nullsoft's Winamp. All visuals in this app are original code; no MilkDrop presets or Shadertoy shaders are bundled. The Windows toolbar uses [Dear ImGui](https://github.com/ocornut/imgui) (MIT).
