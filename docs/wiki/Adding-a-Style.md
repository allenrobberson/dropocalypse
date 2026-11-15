# Adding a Style

Every style is a set of numbers plus an optional drawing routine, so simple styles take a few lines and fancy ones a single function.

## 1. A parameter-only style

Add a `VisualScene(...)` entry to `VisualScene.builtIn` in `Sources/Dropocalypse/Render/Scenes.swift` (Mac), and the matching entry in `windows/src/scenes.h` (Windows). The most useful parameters:

| Parameter | Effect |
|---|---|
| `zoom`, `zoomBass` | How fast the feedback image zooms in (and how much the bass adds) |
| `rotation`, `rotationBass` | Spin per frame |
| `warpAmount`, `warpScale`, `warpSpeed` | Liquid ripple in the feedback |
| `decay` | How long trails last (closer to 1 = longer) |
| `driftX`, `driftY` | Make the whole image flow sideways or upward |
| `mirror`, `kaleido` | Mirror modes and kaleidoscope segments |
| `hueShift`, `plasma` | Color rotation per frame, and a plasma color wash |
| `wave` | Which shape is drawn (a `WaveStyle`: circle, line, bars, radialBars, spiralDots, lissajous…) |
| `baseHue`, `hueSpeed`, `saturation` | Colors |
| `waveScale`, `thickness`, `centerRadius` | Size and line width of the drawn shapes; `centerRadius` near 0 makes shapes burst from the exact center |
| `vignette` | Darkening at the edges |

Remember to increase the array size in `windows/src/scenes.h` (`std::array<VisualScene, N>`).

## 2. A style with its own drawing

For something new (like Pulsar Ridges or Tesseract):

1. Add a case to `enum WaveStyle` in `Scenes.swift` and to `enum class WaveStyle` in `windows/src/scenes.h`.
2. Write the drawing function in `Render/StyleFX.swift` and port it to the `StyleFX` class in `windows/src/renderer.cpp`. You get an `FXFrame` with the time, frame delta, smoothed bass, spectrum bands, waveform, screen aspect, logo position and the scene's palette, and a `GeometryBuilder` for lines, quads, rectangles, dots, soft dots and polylines.
3. Call it from the `switch` in `WaveDrawer.swift` and in `renderer.cpp`.
4. Add the style to the README style table, `scripts/make-guide.py` (the PDF guide) and the [[Styles]] wiki page.

Coordinates are normalized: `y` runs from -1 (bottom) to 1 (top) and `x` from `-aspect` to `aspect`. Keep anything real-time (no allocation per frame in hot paths) and test both platforms. CI builds both on every push and checks the Metal and HLSL shaders compile.

## 3. Send it in

Open a pull request with a short clip or screenshot of the style in action. Ideas without code are welcome in [Discussions](https://github.com/allenrobberson/dropocalypse/discussions) too.
