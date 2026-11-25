# Controls and Settings

## The toolbar

Every control lives in one slim bar along the **top edge** of the window.

- **To show it:** move the mouse toward the top of the screen, click anywhere, or press **T**.
- **It hides itself** after about 2.5 seconds without activity, and the mouse cursor hides with it, leaving a clean picture.
- **It stays open** while your pointer is over it or while one of its menus is open.
- **To hide it right away:** press **Esc**.

From left to right:

| Control | What it does |
|---|---|
| **‹** · **Style name** · **›** | Previous style, open the style list, next style. In the list, click a name to show it, and use the **checkboxes** to choose which styles auto-cycle rotates through |
| ⟳ **Auto** | Turns auto-cycle on or off. It glows green while auto-cycle is on. Set how long each style stays up (5 seconds to 6 minutes) with **Change style every** at the top of the style list (click the style name) |
| **Logo** | Opens the [Logo menu](Your-Logo-and-Motion#logos-and-text): your logos and text items, plus size, glow, tint and trails |
| **Motion** | Opens the [Motion menu](Your-Logo-and-Motion#motion-panel): how the logo reacts to the bass |
| **Audio** | Opens the [Audio menu](Audio-Sources#audio-panel): what to listen to (an app, all Mac audio, a mic or line-in) and sensitivity. It turns red when audio isn't running. |
| **Look** | Opens the [Look menu](Controls-and-Settings#look-panel): beat flash, brightness, landscape or portrait, frame rate |
| ● **Record** / ■ **Stop** | Red while recording, with the running time. Records the visuals and the music to an MP4 video. The **▾** next to it sets the video size and where to save. See [Recording](#recording). |
| *Status text* | The input being used, its sample rate and the audio buffer size (lower ms = faster reaction) |
| **Detach** | Moves the controls into their own window, stacked top to bottom, so they can stay on your laptop while the visuals go on a TV or projector. See [Two screens](Live-Shows-and-TVs#two-screens-controls-on-one-visuals-on-the-other). |
| **Screen** | Picks the display that shows the visuals full screen (TV, projector, second monitor), and turns full screen on or off (also **F**) |
| **Updates** | Your version, **Check now** and **Check for updates automatically**. When a newer version is out it turns pink and reads **Update X.Y.Z**: **Update now** checks the download and, if the checks pass, installs it and reopens. Your logo and settings are kept. If a check fails, nothing is changed. **Download page** gets it yourself, or **Ignore for 24 hours**. Also in the detached controls window. |
| **Keys** | Shows the keyboard shortcuts |

**On small screens the menus become letters.** When the window is too narrow for the full names (a 720p projector, a small laptop, a narrowed window), the menus show one letter each:

| Letter | Menu |
|---|---|
| **L** | Logo |
| **M** | Motion |
| **A** | Audio |
| **V** | Look ("visuals"; L is already Logo) |
| **S** | Screen |
| **U** | Updates |
| **?** | Keys |

Hold the mouse over a letter to see its name; the open menu shows its full name as its title. **Detach** always shows the whole word so it's easy to find. When there's room again, the full names come back by themselves. The letters are only on the toolbar: the keyboard shortcuts don't change.

Every button is drawn as a raised key and every slider as a mixer fader, the same on Mac, Windows and Linux. A pink key has its menu open; the **Auto** key glows green while auto-cycle is on.

## Recording

1. Click **Record** in the toolbar (or press **R**). It turns red and becomes **Stop** with a running timer.
2. Play your music. Dropocalypse records the visuals (without the toolbar) together with the music it's listening to.
3. Click **Stop** (or press **R** again). A moment later you'll see **Saved Dropocalypse 2026-11-15 at 19.30.00.mp4** with a button to show it in Finder, File Explorer or your file manager.

Open the **▾** next to Record to choose the **video size** (720p or 1080p at 60 fps, or 4K at 30 fps) and **where to save**: your Movies folder (Mac) or Videos folder (Windows and Linux) unless you pick another. On Linux, recording needs **ffmpeg**. Recordings are MP4 files (H.264 video, AAC audio) that play anywhere and upload straight to YouTube, Instagram or TikTok. In **Landscape**, if your window isn't 16:9 the video gets black bars; full screen on a 16:9 display fills the frame. **Portrait** (in the Look menu) records a vertical 9:16 frame: 720×1280, 1080×1920 or 2160×3840. On a landscape screen that frame is centered with black bars on either side, so the preview matches the file.

## Style controls

| Control | What it does |
|---|---|
| **‹** / **›** | Previous / next style. You can also use the ← → arrow keys. |
| **Style name** | Opens the style list. Click a name to jump straight to any of the [31 styles](Styles). The **checkbox** to the left of each style decides whether auto-cycle includes it. **All** and **None** check or clear every box at once. |
| ⟳ **Auto** | When on, the app moves to the next **checked** style every 30 seconds (adjustable from 5 seconds to 6 minutes with **Change style every** at the top of the style list), with a smooth 2.5-second crossfade. Unchecked styles are skipped; if nothing is checked, it uses every style. Choosing a style by hand restarts the timer, and the arrows and arrow keys still reach every style. |

## Look panel

| Control | Default | What it does |
|---|---|---|
| **Beat flash** | 0.40 | The whole screen brightens briefly on each beat. 0 turns it off. |
| **Brightness** | 1.00 | Overall brightness of the visuals (0.4 to 1.6). Lower it for dark rooms or bright projectors. |
| **Frame** | Landscape | **Landscape** fills the window. **Portrait** centers a 9:16 frame. Recordings use the same shape (720×1280, 1080×1920 or 2160×3840 in portrait). It can't be changed while a recording is in progress. |
| **Frame rate** | Display | **Display** matches the monitor. **60** and **30** use less CPU. |

## Other panels

- **Motion** and **Logo** panels: see [[Your Logo and Motion]]
- **Audio** panel: see [[Audio Sources]]

## Where settings are saved

Everything (logo, style, sliders, audio source, auto-cycle checkboxes) is saved automatically.

- **Mac:** app preferences, plus your logo in `~/Library/Application Support/Dropocalypse`
- **Windows:** `%APPDATA%\Dropocalypse` (a plain `settings.ini` plus your logo)
- **Linux:** `~/.config/dropocalypse` (a plain `settings.ini` plus your logo)

**Reset to defaults** in the Motion panel resets the Motion, Logo and Look sliders.
