# Building from Source

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

Before releasing, add the new version to the top of `docs/wiki/Changelog.md` and run `python3 scripts/make-guide.py` (needs `reportlab`). The printable User Guide shows the version on its cover and on every page, taken from the newest changelog entry, and the build warns if the guide's version doesn't match the release.

## Project layout

```
Sources/Dropocalypse/
  Audio/AudioCapture.swift    mic / line-in capture, device list, small hardware buffers
  Audio/ProcessTap.swift      Core Audio process tap: listen to one app or all Mac audio (14.2+)
  Audio/AudioAnalysis.swift   real-time filters, envelopes, auto-gain, beat detection
  Render/Renderer.swift       Metal pipeline: feedback warp → shapes → composite + logo
  Render/ShaderSource.swift   all Metal shaders
  Render/Scenes.swift         the 24 built-in styles (add your own here!)
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

See also [[How It Works]] and [[Adding a Style]].
