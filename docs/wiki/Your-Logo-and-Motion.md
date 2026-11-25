# Your Logo and Motion

Dropocalypse is built around putting **your** logo on screen: a band, DJ name, venue, stream or brand. You can show up to four at once, and any of them can be text.

## Adding your logo

- **Drag an image** from Finder, File Explorer or a web browser onto the window, or
- press **L** (Windows and Linux: also **Ctrl+O**) or use **Add Logo…** in the **Logo** menu in the toolbar (**L** on small screens).

On a Mac, PNG, JPG, PDF and SVG work; on Windows, PNG, JPG, BMP, GIF and TIFF; on Linux, PNG, JPG, BMP, GIF and TGA. A **PNG with a transparent background**, around 1000 px on its long side, looks best. Empty transparent edges are trimmed automatically, so the logo bounces around its visible shape. Your logos are remembered between launches.

## Logos and text

Show **up to 4 logos and text items at once**. In landscape they sit side by side, left to right; in **Portrait** (Look menu) they stack top to bottom. Each one shrinks to fit its space, and **Size** in the Logo menu sets them all. They all react to the same beat, each with a small offset in timing and direction so they don't move like clones.

The **Logo menu** in the toolbar lists your items:

| Item | What it does |
|---|---|
| **1. band.png**, **2. "DJ NIGHT"** … | Each item, with **Replace…** (images), the **‹ ›** keys to move it (up / down in Portrait) and **Remove** |
| **Add Logo… / Add image… (L)** | Adds an image. PNGs with a transparent background look best. Empty transparent edges are trimmed automatically so each logo bounces around its visible shape. |
| **Add Text** | Adds a text item. Type the words right in the list. |
| **Remove all** | Removes every item (Mac: **File → Remove All Logos**) and goes back to the "YOUR LOGO" placeholder |
| **Show Logos (G)** | Hides or shows them all without removing them, so you get visuals only |

**Text items:** type the words (up to 60 characters), pick a font (**Bold**, **Heavy**, **Serif** or **Mono**) and a color (white, pink, cyan, gold, lime or purple). Text gets a dark outline so it stays readable over bright visuals, and glows, tints and trails like a logo. Edit text and reorder items in the **Logo** menu, or in the detached controls window.

You can also **drag images** from Finder, File Explorer, your file manager or a web browser onto the window at any time. Drop several to add them all. When all 4 spaces are used, a dropped image replaces the last item.

## Motion panel

These sliders control how your logo reacts to the bass. **0 turns a motion off completely**; the highest setting is 1.5. The four **Logo layers** sliders at the bottom go from 0 to 100.

<p align="center"><img src="https://github.com/allenrobberson/dropocalypse/raw/main/docs/guide-images/motion.jpg" width="300" alt="The Motion menu: Pulse, Squash and stretch, Bounce, Slide and Wobble, then the Logo layers sliders Wave, Explode, Bands and Scatter"><br><sub>The Motion menu, with the Logo layers sliders at the bottom</sub></p>

| Slider | Default | What it does |
|---|---|---|
| **Pulse** | 0.70 | The logo grows with the bass and shrinks back as it fades. This is the most direct "breathing" motion. |
| **Squash & stretch** | 0.60 | On each kick the logo squashes wide and short, then springs back like rubber. Also stretches it taller during sustained bass. |
| **Bounce** | 0.50 | Kicks launch the logo upward and it falls back with gravity. A small squash happens when it lands. |
| **Slide** | 0.40 | Each beat pushes the logo left or right, alternating sides, and a spring pulls it back to center. Adds a slow side-to-side drift too. |
| **Wobble** | 0.35 | Each beat tips the logo into a quick rotation that settles back to level |
| **Wave** (logo layers) | 0 | The shapes hop one after another from left to right. 0 = still, 100 = super wild (at the top they also flip). |
| **Explode** (logo layers) | 0 | The shapes burst outward from the centre and spring back. Higher values also fire on softer beats. |
| **Bands** (logo layers) | 0 | Each shape rides the bass, mids or treble, rising and growing with its part of the music. |
| **Scatter** (logo layers) | 0 | Every shape flies off in its own direction and spins. |
| **Reset to defaults** | | Puts the Motion, Logo and Look sliders (except auto-cycle timing) back to their defaults |

The four **logo layers** sliders split your logo into its separate shapes (each letter, icon or piece that doesn't touch another) and move them on their own. They go from 0 (still) to 100 (super wild), and you can mix as many as you like. They work on images and text items.

<table>
  <tr>
    <td align="center"><img src="https://github.com/allenrobberson/dropocalypse/raw/main/docs/media/styles/layers-wave.gif" width="200" alt="Logo layers: Wave"><br><sub><b>Wave</b> 75</sub></td>
    <td align="center"><img src="https://github.com/allenrobberson/dropocalypse/raw/main/docs/media/styles/layers-explode.gif" width="200" alt="Logo layers: Explode"><br><sub><b>Explode</b> 85</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="https://github.com/allenrobberson/dropocalypse/raw/main/docs/media/styles/layers-bands.gif" width="200" alt="Logo layers: Bands"><br><sub><b>Bands</b> 85</sub></td>
    <td align="center"><img src="https://github.com/allenrobberson/dropocalypse/raw/main/docs/media/styles/layers-scatter.gif" width="200" alt="Logo layers: Scatter"><br><sub><b>Scatter</b> 80</sub></td>
  </tr>
</table>

> **Tip:** for a calm, branded look, try Pulse 0.5 and everything else at 0. For a festival-style "everything goes off" look, push Squash, Bounce and Wobble to 1.2 or higher.

## Logo panel

These sit under the list of items in the **Logo** menu, and in the detached controls window.

| Control | Default | What it does |
|---|---|---|
| **Size** | 0.32 | How big the logo is, as a fraction of the screen height (0.08 to 0.8). The longer side of the logo is fitted to this size. |
| **Glow** | 0.60 | A soft, colored halo behind the logo that flares brighter with the bass. 0 turns it off. |
| **Color tint** | 0.00 | Blends the logo toward the current style's color. 0 keeps your logo's original colors; 1 recolors it completely. |
| **Logo trails** | Off | Feeds a faint copy of the logo into the visuals every frame, so it leaves echoes that swirl, zoom and fade with the effect |
| **Show logos (G)** | On | Hides or shows every logo and text item without removing them |

## Recipes

| Look | Settings |
|---|---|
| **Calm and branded** | Pulse 0.5, everything else 0, Glow 0.4 |
| **Festival chaos** | Squash 1.2, Bounce 1.3, Wobble 1.2, Slide 0.8, Glow 1.0 |
| **Letters on the drop** | Explode 60, Scatter 15, Bounce 0.3 |
| **Stadium wave** | Wave 50, Bands 20, Slide 0 |
| **Melt into the visuals** | Logo trails on, Color tint 0.4 |
| **Show off the Tesseract** | Size 0.15–0.2 so the inner cube is visible |
| **Visuals only** | Press **G** to hide the logo |
