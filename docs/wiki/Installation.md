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

From version **1.0**, Dropocalypse updates itself: when a new version is out, a pink **Update** button appears in the toolbar. Click it, then **Update now**. It downloads the new version, checks it against the release's published SHA-256 checksums, installs it and reopens, with your logo and settings kept. (0.9.x shows the button but only opens the download page; update to 1.0 by hand once.)

- **Mac:** Dropocalypse must be in a folder it can edit, such as **Applications**. If you run it straight from the disk image, it will ask you to move it first.
- **Windows:** keep the Dropocalypse folder somewhere you can edit (Documents, Desktop, a USB stick). The old version is swapped out in place; no admin rights are needed.
- If anything fails (no internet, a bad download, a locked folder), nothing is changed. Use **Download page** instead. For an email on every release, click **Watch → Custom → Releases** on the GitHub page.

Download the new version and replace the old one: drag the new app into **Applications** on a Mac (choose **Replace**), or swap in the new folder on Windows. Your logo, style and settings are kept.

## Uninstalling

- **Mac:** drag **Dropocalypse** from Applications to the Trash. To also remove your saved logo and settings, delete `~/Library/Application Support/Dropocalypse`.
- **Windows:** delete the app folder and `%APPDATA%\Dropocalypse`. Nothing else was installed.

## Why the security warnings?

Dropocalypse is free and made by one developer. Apple charges $99 a year to notarize Mac apps, and Windows code-signing certificates cost money too, so for now the downloads aren't signed. The warnings mean "this app isn't from an identified developer", not that anything harmful was found. The full source code is public, and every download is built by GitHub Actions from that code. Signing the Mac app is the first [sponsorship](Support-the-Project) goal.

Next: [[Quick Start]]
