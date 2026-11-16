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
| ⬇ **Updates** | Always in the toolbar (and its own section in the detached controls window). Shows your version, **Check now** and **Check for updates automatically**. When a newer version is out it turns pink and reads **Update X.Y.Z**: choose **Update now** to download, install and reopen the new version automatically (your logo and settings are kept), **Download page** to get it yourself, or **Ignore for 24 hours**. |
| *Status text* | Shows the input being used, its sample rate and the audio buffer size (lower ms = faster reaction) |
| ⌨ / **Keys** | Shows the keyboard shortcuts |
| 🔴 **Record** / **Stop** | Records the visuals and the music to an MP4 video, without the toolbar. The **▾** next to it sets the video size (720p, 1080p, 4K) and where to save. See [Recording](#recording). |
| 🖥 / **Screen** | Shows the visuals full screen on another display (TV, projector, second monitor) |
| ⇥ / **Detach** | Moves the controls into their own window, stacked top to bottom, so they can stay on your main screen. See [Live Shows and TVs](Live-Shows-and-TVs#two-screens-controls-on-one-visuals-on-the-other). |
| ⤢ / **Full screen** | Full screen on / off |

> On Windows the toolbar shows text labels (**< Style >**, **Auto-cycle**, **Screen**, **Detach**, **Keys**, **Full screen**) instead of icons, but the controls are the same.

## Recording

1. Click **Record** in the toolbar (or press **R**). It turns red and becomes **Stop** with a running timer.
2. Play your music. Dropocalypse records the visuals (without the toolbar) together with the music it's listening to.
3. Click **Stop** (or press **R** again). A moment later you'll see **Saved Dropocalypse 2026-11-15 at 19.30.00.mp4** with a button to show it in Finder or Explorer.

Open the **▾** next to Record to choose the **video size** (720p or 1080p at 60 fps, or 4K at 30 fps) and **where to save**: your Movies folder (Mac) or Videos folder (Windows) unless you pick another. Recordings are MP4 files (H.264 video, AAC audio) that play anywhere and upload straight to YouTube, Instagram or TikTok. If your window isn't 16:9, the video gets black bars; full screen on a 16:9 display fills the frame.

## Style controls

| Control | What it does |
|---|---|
| **‹** / **›** | Previous / next style. You can also use the ← → arrow keys. |
| **Style name** | Opens the style list. Click a name to jump straight to any of the [24 styles](Styles). The **checkbox** to the left of each style decides whether auto-cycle includes it. **All** and **None** check or clear every box at once. |
| ⟳ **Auto-cycle** | When on, the app moves to the next **checked** style every 30 seconds (adjustable from 5 seconds to 6 minutes under **Look**), with a smooth 2.5-second crossfade. Unchecked styles are skipped; if nothing is checked, it uses every style. Choosing a style by hand restarts the timer, and the arrows and arrow keys still reach every style. |

## Look panel

| Control | Default | What it does |
|---|---|---|
| **Beat flash** | 0.40 | The whole screen brightens briefly on each beat. 0 turns it off. |
| **Brightness** | 1.00 | Overall brightness of the visuals (0.4 to 1.6). Lower it for dark rooms or bright projectors. |
| **Auto-cycle every** | 30 s | How long each style stays up when auto-cycle is on (5 to 360 seconds, up to 6 minutes) |

## Other panels

- **Motion** and **Logo** panels: see [[Your Logo and Motion]]
- **Audio** panel: see [[Audio Sources]]

## Where settings are saved

Everything (logo, style, sliders, audio source, auto-cycle checkboxes) is saved automatically.

- **Mac:** app preferences, plus your logo in `~/Library/Application Support/Dropocalypse`
- **Windows:** `%APPDATA%\Dropocalypse` (a plain `settings.ini` plus your logo)

**Reset to defaults** in the Motion panel resets the Motion, Logo and Look sliders.
