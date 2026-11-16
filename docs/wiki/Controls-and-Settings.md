# Controls and Settings

## The toolbar

Every control lives in one slim bar along the **top edge** of the window.

- **To show it:** move the mouse toward the top of the screen, click anywhere, or press **T**.
- **It hides itself** after about 2.5 seconds without activity, and the mouse cursor hides with it, leaving a clean picture.
- **It stays open** while your pointer is over it or while one of its panels is open.
- **To hide it right away:** press **Esc**.

From left to right:

| Control | What it does |
|---|---|
| 🖼 **Add Logo / logo name** | Opens the [Logo menu](Your-Logo-and-Motion#logo-menu) |
| **‹  Style name  ›** | Previous style, open the style list, next style. In the list, click a name to show it, and use the **checkboxes** to choose which styles auto-cycle rotates through |
| ⟳ | Turns auto-cycle on or off (lit = on) |
| **Motion** | Opens the [Motion panel](Your-Logo-and-Motion#motion-panel): how the logo reacts to the bass |
| **Logo** | Opens the [Logo panel](Your-Logo-and-Motion#logo-panel): size, glow, tint, trails |
| **Audio** | Opens the [Audio panel](Audio-Sources#audio-panel): what to listen to (an app, all Mac audio, a mic or line-in) and sensitivity. The mic icon is crossed out when audio isn't running. |
| **Look** | Opens the [Look panel](Controls-and-Settings#look-panel): flash, brightness, auto-cycle timing |
| 🩷 **Update X.Y.Z** | Appears only when a newer version is out. Click it and choose **Update now** to download, install and reopen the new version automatically (your logo and settings are kept), **Download page** to get it yourself, or **Ignore for 24 hours** to hide the button until tomorrow. |
| *Status text* | Shows the input being used, its sample rate and the audio buffer size (lower ms = faster reaction) |
| ⌨ / **Keys** | Shows the keyboard shortcuts |
| ⤢ / **Full screen** | Full screen on / off |

> On Windows the toolbar shows text labels (**< Style >**, **Auto-cycle**, **Keys**, **Full screen**) instead of icons, but the controls are the same.

## Style controls

| Control | What it does |
|---|---|
| **‹** / **›** | Previous / next style. You can also use the ← → arrow keys. |
| **Style name** | Opens the style list. Click a name to jump straight to any of the [24 styles](Styles). The **checkbox** to the left of each style decides whether auto-cycle includes it. **All** and **None** check or clear every box at once. |
| ⟳ **Auto-cycle** | When on, the app moves to the next **checked** style every 30 seconds (adjustable under **Look**), with a smooth 2.5-second crossfade. Unchecked styles are skipped; if nothing is checked, it uses every style. Choosing a style by hand restarts the timer, and the arrows and arrow keys still reach every style. |

## Look panel

| Control | Default | What it does |
|---|---|---|
| **Beat flash** | 0.40 | The whole screen brightens briefly on each beat. 0 turns it off. |
| **Brightness** | 1.00 | Overall brightness of the visuals (0.4 to 1.6). Lower it for dark rooms or bright projectors. |
| **Auto-cycle every** | 30 s | How long each style stays up when auto-cycle is on (5 to 120 seconds) |
| **Check for updates automatically** | On | Once a day, asks GitHub whether a newer version is out. If so, a pink **Update available** button appears in the toolbar. **Check now** checks right away; the panel also shows which version you have. |

## Other panels

- **Motion** and **Logo** panels: see [[Your Logo and Motion]]
- **Audio** panel: see [[Audio Sources]]

## Where settings are saved

Everything (logo, style, sliders, audio source, auto-cycle checkboxes) is saved automatically.

- **Mac:** app preferences, plus your logo in `~/Library/Application Support/Dropocalypse`
- **Windows:** `%APPDATA%\Dropocalypse` (a plain `settings.ini` plus your logo)

**Reset to defaults** in the Motion panel resets the Motion, Logo and Look sliders.
