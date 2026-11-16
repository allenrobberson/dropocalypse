# Installation

Get both versions from the **[latest release](https://github.com/allenrobberson/dropocalypse/releases/latest)**. Every release includes:

| File | For |
|---|---|
| `Dropocalypse-…-macOS.dmg` | Mac (drag-to-Applications installer) |
| `Dropocalypse-…-macOS.zip` | Mac (the app in a zip, same thing) |
| `Dropocalypse-…-Windows-portable.zip` | Windows 10/11, 64-bit, no install |
| `Dropocalypse-User-Guide.pdf` | A printable, full-color guide for both |

## 🍎 Mac

1. Download the `macOS.dmg`, open it, and drag **Dropocalypse** into **Applications**.
2. **Open it.** Dropocalypse is signed and notarized by Apple (since 1.0.1), so it opens like any other app with no security warning.
   - Still on a version before 1.0.1? macOS may block it the first time. On macOS 15 Sequoia and later, try to open it once, then click **Open Anyway** in **System Settings → Privacy & Security**; on macOS 13–14, right-click the app → **Open** → **Open**. Updating to the latest version fixes this for good.
3. Click **Audio** in the toolbar and choose what to listen to: an app like **Spotify** or **All Mac audio** (macOS 14.2+), or a mic or line-in. macOS asks once for permission: *system audio recording* for apps, *microphone* for a mic. Audio is analyzed live and never sent anywhere; it's only saved if you press **Record**, and then only to your own computer.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

## 🪟 Windows (portable: no install, no admin rights)

1. Download the `Windows-portable.zip`, right-click it → **Extract All**, and open the folder.
2. Double-click **Dropocalypse.exe**. Nothing gets installed, and you can keep the folder anywhere, even on a USB stick.
3. **First launch:** if Windows says *"Windows protected your PC"*, click **More info → Run anyway**. This happens because the free app isn't code-signed.
4. By default it listens to **what your PC is playing** (Spotify, YouTube, DJ software), so you don't need a microphone. To use a mic or line-in, pick it under **Audio** in the toolbar. For a mic, turn on **Settings → Privacy & security → Microphone → Let desktop apps access your microphone**.

Requires Windows 10 or 11 (64-bit). Settings and your logo are saved in `%APPDATA%\Dropocalypse`. To remove the app, delete that folder and the app folder.

## Updating

From version **1.0**, Dropocalypse updates itself: when a new version is out, a pink **Update** button appears in the toolbar. Click it, then **Update now**. It downloads the new version, checks the SHA-256 checksum published with the release, and installs it only if that check passes. On Mac it also requires a valid Developer ID signature from the same team as the copy you're running, accepted by Gatekeeper. On Windows, once the copy you're running is code-signed, the new one must be signed by the same publisher; until then the checksum is enough. Your logo and settings are kept. (0.9.x shows the button but only opens the download page; update to 1.0 by hand once.)

- **Mac:** Dropocalypse must be in a folder it can edit, such as **Applications**. If you run it straight from the disk image, it will ask you to move it first.
- **Windows:** keep the Dropocalypse folder somewhere you can edit (Documents, Desktop, a USB stick). The old version is swapped out in place; no admin rights are needed.
- If anything fails (no internet, a bad download, a locked folder), nothing is changed. Use **Download page** instead. For an email on every release, click **Watch → Custom → Releases** on the GitHub page.

Download the new version and replace the old one: drag the new app into **Applications** on a Mac (choose **Replace**), or swap in the new folder on Windows. Your logo, style and settings are kept.

## Uninstalling

- **Mac:** drag **Dropocalypse** from Applications to the Trash. To also remove your saved logo and settings, delete `~/Library/Application Support/Dropocalypse`.
- **Windows:** delete the app folder and `%APPDATA%\Dropocalypse`. Nothing else was installed.

## Why the security warnings?

The Mac app has been signed and notarized since 1.0.1, so it opens without a security warning. The portable Windows download is not code-signed yet (signing through SignPath is in progress), so Windows may still say "Windows protected your PC" the first time: click **More info → Run anyway**. That means the file has no publisher signature, not that anything harmful was found. The full source code is public, and every download is built by GitHub Actions from that code. Sponsorships help keep the Mac signing membership and Windows signing going: see [Support the Project](Support-the-Project).

Next: [[Quick Start]]
