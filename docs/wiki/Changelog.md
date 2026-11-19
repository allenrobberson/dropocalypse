# Changelog

All versions are on the [Releases page](https://github.com/allenrobberson/dropocalypse/releases). Each release has Mac, Windows and Linux downloads plus the PDF guide.

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
