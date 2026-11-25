# Installation

Get every version from the **[latest release](https://github.com/allenrobberson/dropocalypse/releases/latest)**. Every release includes:

| File | For |
|---|---|
| `Dropocalypse-…-macOS.dmg` | Mac (drag-to-Applications installer) |
| `Dropocalypse-…-macOS.zip` | Mac (the app in a zip, same thing) |
| `Dropocalypse-…-Windows-portable.zip` | Windows 10/11, 64-bit, no install, updates from GitHub |
| `Dropocalypse-…-x86_64.AppImage` | Linux, 64-bit, one file, updates from GitHub or with AppImageUpdate |
| `Dropocalypse-…-Linux-x86_64.tar.gz` | Linux, the same app as a plain folder |
| [`Dropocalypse-User-Guide.pdf`](https://github.com/allenrobberson/dropocalypse/releases/latest/download/Dropocalypse-User-Guide.pdf) | A printable, full-color guide for Mac, Windows and Linux |

On Windows you can also **[get Dropocalypse from the Microsoft Store](https://apps.microsoft.com/detail/9N1TJ9D5R5C8)**. That copy is already signed by Microsoft, so it opens without a security warning, and it is updated by the Store, so it has no **Update now** and does not check GitHub. Your logo and settings still live in `%APPDATA%\Dropocalypse`.

## 🍎 Mac

1. Download the `macOS.dmg`, open it, and drag **Dropocalypse** into **Applications**.
2. **Open it.** Dropocalypse is signed and notarized by Apple (since 1.0.1), so it opens like any other app with no security warning.
   - Still on a version before 1.0.1? macOS may block it the first time. On macOS 15 Sequoia and later, try to open it once, then click **Open Anyway** in **System Settings → Privacy & Security**; on macOS 13–14, right-click the app → **Open** → **Open**. Updating to the latest version fixes this for good.
3. Click **Audio** in the toolbar and choose what to listen to: an app like **Spotify** or **All Mac audio** (macOS 14.2+), or a mic or line-in. macOS asks once for permission: *system audio recording* for apps, *microphone* for a mic. Audio is analyzed live and never sent anywhere; it's only saved if you press **Record**, and then only to your own computer.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

## 🪟 Windows

> 💡 **Want to skip the security warning?** **[Get Dropocalypse from the Microsoft Store](https://apps.microsoft.com/detail/9N1TJ9D5R5C8)**. The Store edition is already signed by Microsoft, so Windows opens it without a "Windows protected your PC" warning, and the Store keeps it up to date. The portable zip below isn't code-signed yet.

**Portable zip** (no install, no admin rights):

1. Download the `Windows-portable.zip`, right-click it → **Extract All**, and open the folder.
2. Double-click **Dropocalypse.exe**. Nothing gets installed, and you can keep the folder anywhere, even on a USB stick.
3. **First launch:** if Windows says *"Windows protected your PC"*, click **More info → Run anyway**. This happens because the portable download isn't code-signed yet. The [Microsoft Store edition](https://apps.microsoft.com/detail/9N1TJ9D5R5C8) doesn't show this warning.
4. By default it listens to **what your PC is playing** (Spotify, YouTube, DJ software), so you don't need a microphone. To use a mic or line-in, pick it under **Audio** in the toolbar. For a mic, turn on **Settings → Privacy & security → Microphone → Let desktop apps access your microphone**.

Requires Windows 10 or 11 (64-bit). Settings and your logo are saved in `%APPDATA%\Dropocalypse`. To remove the app, delete that folder and the app folder.

## 🐧 Linux (AppImage: one file, nothing to install)

1. Download the `x86_64.AppImage`.
2. Make it executable: right-click it → **Properties** → **Allow executing file as program**, or run `chmod +x Dropocalypse-*.AppImage`.
3. Double-click it (or run `./Dropocalypse-*.AppImage`).
4. By default it listens to **what the computer is playing**, so you don't need a microphone. Pick a specific output, a mic or a line-in under **Audio**.

Works on 64-bit Ubuntu 22.04 or later, Debian 12, Fedora, Linux Mint, Arch and other current distributions, with X11 or Wayland and PulseAudio or PipeWire. Recording needs **ffmpeg** (`sudo apt install ffmpeg`), and choosing files uses zenity or kdialog. Prefer a plain folder? Use the `Linux-x86_64.tar.gz`. Settings and your logo are saved in `~/.config/dropocalypse`.

If the AppImage won't start on Ubuntu 22.04 or older with a message about FUSE, install `libfuse2` or run it with `--appimage-extract-and-run`.

## Updating

From version **1.0**, Dropocalypse updates itself: when a new version is out, a pink **Update** button appears in the toolbar. Click it, then **Update now**. It downloads the new version, checks the SHA-256 checksum published with the release, and installs it only if that check passes. On Mac it also requires a valid Developer ID signature from the same team as the copy you're running, accepted by Gatekeeper. On Windows, once the copy you're running is code-signed, the new one must be signed by the same publisher; until then the checksum is enough. Your logo and settings are kept. (0.9.x shows the button but only opens the download page; update to 1.0 by hand once.)

- **Mac:** Dropocalypse must be in a folder it can edit, such as **Applications**. If you run it straight from the disk image, it will ask you to move it first.
- **Linux:** the AppImage replaces itself, so keep it in a folder you can edit, such as your home folder. The `.tar.gz` copy updates by downloading the new one.
- **Windows:** keep the Dropocalypse folder somewhere you can edit (Documents, Desktop, a USB stick). The old version is swapped out in place; no admin rights are needed.
- If anything fails, nothing is changed and the **Updates** menu says why. That includes no internet, a bad download, a locked folder, a Mac download that isn't signed by the same Developer ID or that Gatekeeper rejects, and (once your Windows copy is code-signed) a download signed by a different publisher. Use **Download page** in that menu, or try again later. For an email on every release, click **Watch → Custom → Releases** on the GitHub page. The Microsoft Store edition does not use this updater.

To update by hand instead, download the new version and replace the old one: drag the new app into **Applications** on a Mac (choose **Replace**), swap in the new folder on Windows, or replace the AppImage on Linux. Your logo, style and settings are kept.

## Uninstalling

- **Mac:** drag **Dropocalypse** from Applications to the Trash. To also remove your saved logo and settings, delete `~/Library/Application Support/Dropocalypse`.
- **Windows:** delete the app folder and `%APPDATA%\Dropocalypse`. Nothing else was installed.
- **Linux:** delete the AppImage (or the folder) and `~/.config/dropocalypse`.

## Why the security warnings?

The Mac app has been signed and notarized since 1.0.1, so it opens without a security warning. The portable Windows download is not code-signed yet, so Windows may still say "Windows protected your PC" the first time: click **More info → Run anyway**. That means the file has no publisher signature, not that anything harmful was found. The [Microsoft Store edition](https://apps.microsoft.com/detail/9N1TJ9D5R5C8) is signed by Microsoft and opens without the warning. Linux AppImages are not signed; check the download against `SHA256SUMS.txt` on the release if you like. The full source code is public, and every download is built by GitHub Actions from that code. Sponsorships help pay for the Mac signing membership and a Windows signing certificate: see [Support the Project](Support-the-Project).

Next: [[Quick Start]]
