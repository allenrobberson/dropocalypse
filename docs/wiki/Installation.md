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
2. **First launch:** the app is free and isn't notarized by Apple, so macOS blocks it the first time.
   - **macOS 15 Sequoia and later:** try to open it once, then go to **System Settings → Privacy & Security**, scroll down, and click **Open Anyway**.
   - **macOS 13–14:** right-click the app → **Open** → **Open**.
   - Or run this in Terminal: `xattr -dr com.apple.quarantine "/Applications/Dropocalypse.app"`
3. Click **Audio** in the toolbar and choose what to listen to: an app like **Spotify** or **All Mac audio** (macOS 14.2+), or a mic or line-in. macOS asks once for permission: *system audio recording* for apps, *microphone* for a mic. Audio is analyzed live and is never recorded or sent anywhere.

Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.

## 🪟 Windows (portable: no install, no admin rights)

1. Download the `Windows-portable.zip`, right-click it → **Extract All**, and open the folder.
2. Double-click **Dropocalypse.exe**. Nothing gets installed, and you can keep the folder anywhere, even on a USB stick.
3. **First launch:** if Windows says *"Windows protected your PC"*, click **More info → Run anyway**. This happens because the free app isn't code-signed.
4. By default it listens to **what your PC is playing** (Spotify, YouTube, DJ software), so you don't need a microphone. To use a mic or line-in, pick it under **Audio** in the toolbar. For a mic, turn on **Settings → Privacy & security → Microphone → Let desktop apps access your microphone**.

Requires Windows 10 or 11 (64-bit). Settings and your logo are saved in `%APPDATA%\Dropocalypse`. To remove the app, delete that folder and the app folder.

## Updating

From version 0.9.0, Dropocalypse tells you when a new version is out: a pink **Update available** button appears in the toolbar. Click it to open the download page. For an email on every release, click **Watch → Custom → Releases** on the GitHub page.

Download the new version and replace the old one: drag the new app into **Applications** on a Mac (choose **Replace**), or swap in the new folder on Windows. Your logo, style and settings are kept.

## Uninstalling

- **Mac:** drag **Dropocalypse** from Applications to the Trash. To also remove your saved logo and settings, delete `~/Library/Application Support/Dropocalypse`.
- **Windows:** delete the app folder and `%APPDATA%\Dropocalypse`. Nothing else was installed.

## Why the security warnings?

Dropocalypse is free and made by one developer. Apple charges $99 a year to notarize Mac apps, and Windows code-signing certificates cost money too, so for now the downloads aren't signed. The warnings mean "this app isn't from an identified developer", not that anything harmful was found. The full source code is public, and every download is built by GitHub Actions from that code. Signing the Mac app is the first [sponsorship](Support-the-Project) goal.

Next: [[Quick Start]]
