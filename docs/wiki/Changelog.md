# Changelog

All versions are on the [Releases page](https://github.com/allenrobberson/dropocalypse/releases). Each release has Mac, Windows and Linux downloads plus the PDF guide.

## 1.6.3
- 🐧 **AppImage name and update information.** The Linux AppImage is now `Dropocalypse-<version>-x86_64.AppImage` (no "Linux", as the AppImage catalog asks) and embeds `gh-releases-zsync` update information; each release publishes the matching `.zsync` file, so AppImageUpdate and similar tools can update it. The in-app updater accepts both names.
- ⚠️ **One manual step on Linux:** 1.6.2 and older look for the old file name, so their **Update now** can't find 1.6.3. Download 1.6.3 once from the [latest release](https://github.com/allenrobberson/dropocalypse/releases/latest); updates work in one click again after that.

## 1.6.2
- 🖼 **User guide with pictures.** The PDF guide, the README and the wiki now show a preview of all 31 styles and the 9 MilkDrop presets, a demo of each logo layers move (Wave, Explode, Bands, Scatter) and the current Motion menu. The new style, logo layers and MilkDrop previews were recorded in the Mac app with real music. The guide bundled in the downloads is the new one.
- 🔗 Every "PDF guide" link points to the guide on the latest release, the same file as the download list, and that file is kept up to date automatically.

## 1.6.1
- 🎚 **Logo layers sliders.** The single Off/Wave/Explode/Bands/Scatter choice and the Layer jump slider are replaced by four sliders, **Wave**, **Explode**, **Bands** and **Scatter**, each 0 (no movement) to 100 (super wild). They mix: turn up several at once. A move picked in 1.6.0 starts at 50. Mac, Windows and Linux.
- 🧩 **Logo layers move far more.** Moves now scale with the logo's longer side and kick harder, so letters in a wide logo or text item visibly fly apart (in 1.6.0 they moved only a few pixels). At high values Explode also fires on softer beats and Wave flips the shapes.
- 🎞 **README previews** for the five new styles and the MilkDrop presets, recorded in the Mac app; `scripts/make-style-previews.sh` and a **README previews** workflow can record them from the Linux app.

## 1.6.0
- 🌀 **MilkDrop presets, through projectM.** Nine classic presets are built in as styles 32 to 40: Acid Mandala, Flame Face, Mind Blob, Geiss Lightning, Dark Galaxy, Hypraxis, Planar Travel, Box of Tricks and Lorenz Chaser. They crossfade like every other style. The open-source [projectM](https://github.com/projectM-visualizer/projectm) engine (LGPL) ships beside the app and is loaded at run time. Mac, Windows and Linux.
- 📂 **Your own MilkDrop folder.** In the style list, under **Your MilkDrop presets**, choose a folder of `.milk` files (subfolders too, up to 20,000 presets). Search them, rescan, open or remove the folder. A `Textures` folder next to it is used for presets that need pictures. Your own presets stay out of auto-cycle unless you include them. The 9 bundled presets can be downloaded as a zip from the [milkdrop folder](https://github.com/allenrobberson/dropocalypse/tree/main/milkdrop).
- 🎨 **Five new styles** (27 to 31): **Monstercat Bars**, **Lo-fi Chill**, **Glitch VHS** (tape tears, colour fringes, scanlines and grain on the beat), **Liquid Chrome** (flowing polished metal) and **Kinetic Text** (rows of your first logo or text item sliding across, the beat lighting a row).
- 🧩 **Logo layers.** Logos are split into their separate shapes, which jump on the beat in four ways: **Wave**, **Explode**, **Bands** and **Scatter**, in the **Motion** menu; off by default.

## 1.5.3
- 📊 **Mirror Equalizer fills the screen.** The bars grow nearly twice as tall, reaching almost the top and bottom edges at full volume (capped just short so they're never cut off). Mac, Windows and Linux.

## 1.5.2
- ⚡ **Mac: faster drawing.** Less work per frame on the CPU and the graphics card: no throwaway lists or repeated colour and trig maths in the styles, the audio analysis uses the Mac's fast vector maths, logo trails are drawn in the main pass instead of a second one, and the screen pass uses 16-bit colour maths. The picture is unchanged.
- 🛠️ **Mac: steadier waveforms.** The wave and spectrum samples are copied into each frame's own commands, so a frame still being drawn can never pick up the next frame's samples.

## 1.5.1
- 🎨 **Mac: the style list uses the 3D menu look**, like every other menu.
- 🧹 **Mac: the detached controls window** no longer repeats each menu's title under its section bar.

## 1.5.0
- 🎛️ **New 3D toolbar and menus.** Raised keys, mixer-style faders with a value display, on/off switches and segmented keys, on Mac, Windows and Linux, in the drop-downs and the detached controls window. Every key has the same height and label line, pressed or not.
- 🔤 **Letters on small screens.** When the full names don't fit, the menus show **L M A V** and **S U ?**, with the name on hover and as the title of the open menu. **Detach** always shows its full name.
- 🧭 **New layout.** Left: ‹ style ›, the **Auto** key (green while on; right-click for its timer), Logo, Motion, Audio, Look and Record. Right: Detach, Screen, Updates and Keys. The two logo buttons are now one **Logo** menu, and full screen moved into the **Screen** menu (**F** still works).

## 1.4.0
- 🏟️ **New style: Main Stage** (style 26). The view from an arena floor: truss columns run back to a stage of glowing LED frames, and fans of moving-head beams meet overhead like a cathedral ceiling, blooming open on every kick from the front pair back. Lasers spray from the stage on the beat (more beams when the hi-hats are busy) and scan over the crowd during the laser cue, blinders flash, and a crowd with phones up bounces to the bass. The lighting changes look every 8 beats: amber, teal, lavender lasers and deep blue.
- 🆕 **What's New window.** The first time Dropocalypse starts after an update, it shows what changed in plain words. Open it again any time from the **Updates** menu (on Mac, also from the Dropocalypse menu).

## 1.3.2
- ✨ **Neon glow icon.** Richer colours and a soft glow on every platform. The smallest sizes (32 px and below) skip the glow so they stay crisp.
- 🛠️ **Safer Mac updates.** If the new version can't be copied into place, the updater now always keeps your current version instead of risking a half-installed app.
- 🔁 **Auto-cycle fix (Mac).** Style cycling handles an out-of-range position safely, so it can't get stuck or crash.

## 1.3.1
- 💧 **New app icon.** The drop with its rainbow bars, now on a transparent background with a crisp outline, on the Mac Dock, the Windows taskbar and Start menu, and Linux app launchers. Small sizes use fewer, bolder bars so the icon stays sharp.

## 1.3.0
- 🖼🖼 **Up to 4 logos at once.** Add a headliner, support acts or sponsors. In landscape they sit side by side; in **Portrait** they stack top to bottom. Each one reacts to the same beat with its own small offset in timing and direction, so they don't move like clones.
- 🔤 **Text items.** Add a DJ name, a night or a hashtag as text: pick **Bold**, **Heavy**, **Serif** or **Mono** and one of six colors. Text gets a dark outline so it reads over bright visuals, and it glows, tints and trails like a logo.
- The **Logo menu** lists every item, with **Replace…**, move left/right (up/down in Portrait) and **Remove**. Type and style text in the **Logo** panel (or the detached controls window). Drop several images on the window to add them all. Your current logo becomes item 1.

## 1.2.12
- 🐧 **Dropocalypse for Linux.** A new AppImage download (one file, nothing to install) for Ubuntu, Debian, Fedora, Mint, Arch and other 64-bit distributions, plus a plain .tar.gz. Same 25 styles, logo motion, two-screen mode, recording and one-click updates as Mac and Windows. By default it listens to **what the computer is playing**; works with X11 or Wayland and PulseAudio or PipeWire. Recording needs ffmpeg.

## 1.2.11
- ⚡ **Lower CPU, same picture.** The simple waveform styles (tunnel, lines, bars, dots, Lissajous) are drawn on the GPU. The app sleeps while its window is minimized or covered, and it skips the toolbar work while the toolbar is hidden. **Look → Frame rate** stays on **Display** (the monitor's refresh). Choose **60** or **30** to use less CPU. While the toolbar is showing, the status line includes a frame time such as **6.2 ms**.

## 1.2.10
- 🪟 **Windows: Update now leaves one copy running.** The old Dropocalypse closes before the new version starts, so you no longer end up with two windows. Opening the app again while it is already running brings that window forward.

## 1.2.9
- 🪟 **Windows: other apps keep playing.** System audio is shared-mode loopback only. Dropocalypse no longer opens a silent playback stream or changes the output's period, so Windows Media Player and other apps can play while it is open. The status line shows a buffer of about 10 ms.

## 1.2.8
- 🔮 **Full Kaleidoscope** (style 25), on Mac and Windows: a mandala that fills the frame. Strong 4-way mirroring that sometimes tightens into an 8-way radial pattern, in saturated neon cyan, magenta, electric blue, gold and acid green. Bass zooms, pulses and turns it; mids and highs change the color and detail. It is in the style list and in auto-cycle, and the logo still sits on top.
- 📱 **Frame** in the Look panel (toolbar and the detached controls): **Landscape** fills the window, as before. **Portrait** centers a 9:16 frame, including in full screen on a landscape display, and recordings match it: 720×1280, 1080×1920 or 2160×3840. The choice is saved. It can't be changed while a recording is in progress.

## 1.2.7
- 🔏 **Update now** still checks the release's SHA-256 checksum, and it now checks the download's code signature before replacing the app. On Mac the new app must be validly signed with the same Developer ID team as the copy you're running, and macOS Gatekeeper must accept it. On Windows that signature check turns on once the copy you're running is itself code-signed; unsigned builds keep updating after the checksum matches.
- If a check fails, Dropocalypse shows the reason in the **Updates** menu and leaves the current version in place. Use **Download page** in that same menu, or try again later.
- Automatic checks, **Check now** and **Update now** are in the **Updates** menu in the toolbar and in the detached controls window (not in the Look panel).
- 🪟 A **Microsoft Store** edition. The Store installs its updates, so that copy has no GitHub updater and no **Update now**. The portable zip on the Releases page is the edition that updates from GitHub.

## 1.2.6
- 📸 The printable User Guide now has screenshots of the toolbar, the Motion, Audio and recording panels, the style list and the detached controls window.
- The guide shows its version number on the cover and every page, and its "What's new" lists just the two latest releases.

## 1.2.5
- 📖 Refreshed README, wiki and User Guide: signed Mac app (no more "Open Anyway"), two screens, recording, the Updates menu and new troubleshooting tips.

## 1.2.4
- ⏱ Auto-cycle can now hold each style for up to 6 minutes (360 seconds), up from 2 minutes.

## 1.2.3
- 🖥 Mac: in full screen on a MacBook with a camera notch, the toolbar now sits just below the notch so no buttons hide behind it. The visuals still fill the whole screen.

## 1.2.2
- 🛠 Mac: fixed the stutter in the first few minutes after opening the app. Audio-system change notices no longer rebuild the audio engine on the drawing thread, so the visuals stay smooth from the start.

## 1.2.1
- ⬇ A dedicated **Updates** menu, always in the toolbar and in the detached controls window (moved out of the Look panel). It turns pink when a new version is out.
- 🔴 The **Record** button is grey until you record, and red only while recording.
- 🎞 Smoother: the Mac version presents frames the fast way again (recording now draws its own copy of the picture), and recordings use an exact 60/30 fps timeline.

## 1.2.0
- 🔴 Record the visuals and the music to an MP4 video (H.264 + AAC) with the **Record** / **Stop** button or **R**. The toolbar is never in the video.
- Choose 720p60, 1080p60 or 4K30, and where recordings are saved (Movies / Videos by default, or any folder).
- In the toolbar and the detached controls window, on Mac and Windows. "Saved …" messages appear in the controls window when it's detached, so the audience doesn't see them.

## 1.1.0
- 🖥️🖥️ Detachable controls: **Detach** (or **D**) moves the toolbar into its own window with every control stacked top to bottom, so it can stay on the main screen while the visuals play full screen on another display. **Attach** or closing the window puts it back; the choice is remembered (Mac and Windows).
- 📺 Screen picker: send the visuals full screen to any connected display in one click.
- Mac: the menu bar and Dock stay usable when the visuals are full screen on a second display. Windows: full screen stays exact when moving between displays with different scaling.

## 1.0.1
- 🎨 New app icon (Mac and Windows).
- 🔏 Mac download signed with Developer ID and notarized by Apple: no more "unidentified developer" warning.
- 🍎 Mac App Store edition (sandboxed, updates through the App Store) built and uploaded automatically.

## 1.0.0
- 🚀 **One-click updates:** **Update now** downloads the new version, verifies it against the release's SHA-256 checksums, installs it over the old one and reopens (Mac and Windows). Releases now include `SHA256SUMS.txt`.

## 0.9.2
- ⚡ **Faster reactions:** the logo's squash, bounce, slide and wobble now hit on the frame the beat is detected; Mac audio buffers halved to ~2.7 ms; Windows *System audio* delivered in ~3 ms chunks instead of 10 ms (low-latency helper stream).

## 0.9.1
- 🧪 Rebuild to test update notifications.

## 0.9.0
- 🔔 **Update notifications:** a daily check for new versions, with an **Update available** button in the toolbar (**Download** or **Ignore for 24 hours**) (Look panel: on/off and **Check now**; Mac: **Check for Updates…** menu item).

## 0.8.0
- 🧊 New **Tesseract** style (24): a glowing neon 4D hypercube rotating through the fourth dimension, spinning harder on every beat.

## 0.7.0
- ☑️ **Style list with checkboxes**: choose which styles auto-cycle rotates through (All / None, saved between launches).
- 🌇 **Outrun Grid**: the horizon spectrum meter now runs the full width of the screen in sunset colors, with a reflection on the floor.

## 0.6.0
- 🌈 Three new styles: **Mirror Equalizer**, **LED Meter** and **Rainbow Peaks** (23 in total).
- 🌅 **Outrun Grid**: the sun is smooth instead of stair-stepped.

## 0.5.1
- 📖 "What's new" sections in the README and PDF guide.

## 0.5.0
- 🎧 **Mac: listen to Spotify or any app** directly, or all Mac audio, with no mic (macOS 14.2+, Core Audio process taps). The Audio panel's source picker became a drop-down of open apps.

## 0.4.4
- 📖 Documentation updates for full screen, Pulsar Ridges and Discussions.

## 0.4.3
- 🖥 **Mac full screen covers every pixel**, including beside the MacBook notch, with the menu bar and Dock hidden. Esc exits full screen.

## 0.4.2
- 〰️ **Pulsar Ridges** stretch edge to edge.
- ❤️ GitHub Sponsors button and support section.

## 0.4.1
- 📄 A printable, full-color **PDF user guide** in every download.

## 0.4.0
- ✨ **10 classic-favorite styles**: Bass Ring, Neon Equalizer, Pulsar Ridges, Outrun Grid, Hyperspace, Particle Storm, Lightning Storm, Digital Rain, Lava Lamp and Inferno (20 in total).

## 0.3.0
- 🪟 **Portable Windows version**: no install and no admin rights, with system-audio capture.

## 0.2.0
- 🔥 Renamed to **Dropocalypse**, with a full user guide in the README.

## 0.1.1
- 🎯 Tunnel, Starburst, Kaleido Bloom, Liquid Plasma and Vortex now burst from the exact center (no hollow circle).

## 0.1.0
- 🎉 First release for Mac: 10 Winamp/MilkDrop-style styles, a bass-reactive logo (pulse, squash, bounce, slide, wobble), an auto-hiding top toolbar and low-latency audio.
