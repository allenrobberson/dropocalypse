# Privacy Policy

_Last updated: November 17, 2026_

Dropocalypse is a free, open-source music visualizer for Mac, Windows and Linux. It is made to keep what you do private.

## Your audio

Dropocalypse listens to the audio source you choose (an app, all computer audio, a microphone or a line-in) only to animate the visuals. **Audio is analyzed live on your computer, in memory, and is never sent anywhere.** It is only saved if you click **Record** (see below).

## Your logo and settings

Your logo image and settings are stored only on your computer:

- **Mac:** in the app's preferences and in `~/Library/Application Support/Dropocalypse`
- **Windows:** in `%APPDATA%\Dropocalypse`
- **Linux:** in `~/.config/dropocalypse`

They are never uploaded.

## Recordings

When you click **Record**, the visuals and the audio Dropocalypse is listening to are saved as a video file in the folder you choose (Movies on Mac, or Videos on Windows and Linux, by default). Recordings stay on your computer; Dropocalypse never uploads them.

## Network use: update checks

Dropocalypse connects to the internet for one purpose: keeping the app up to date. It connects only to GitHub, where the project is hosted.

- **Update check:** about once a day, the app asks GitHub's public API (`api.github.com`) for the latest release of Dropocalypse. The request contains only what any web request contains (your IP address, plus a user agent naming the app and its version). It sends no personal information, no information about your audio, logo or settings, and no usage data or analytics.
- **Downloading an update:** only when you click **Update now**, the app downloads the new version and its checksum file from GitHub (`github.com` and GitHub's download servers). Before installing, the Mac app checks the download's Developer ID signature and asks macOS Gatekeeper whether the app is notarized. That check runs on your Mac; macOS may contact Apple to confirm the notarization ticket. On Windows, once this copy of Dropocalypse is code-signed, the app also checks the download's Authenticode signature locally.

GitHub's handling of these requests is covered by the [GitHub Privacy Statement](https://docs.github.com/site-policy/privacy-policies/github-privacy-statement).

**How to turn it off:** open the **Updates** menu in the toolbar and turn off **Check for updates automatically**. On Windows you can also choose **Don't check for updates** on the first-launch notice. With it off, Dropocalypse makes no network connections at all unless you click **Check now** or **Update now**.

The **Microsoft Store edition** is updated by the Microsoft Store, so it never checks GitHub and makes no network connections of its own.

## No tracking

Dropocalypse has no accounts, no ads, no analytics, no crash reporting and no tracking of any kind.

## Children

Dropocalypse does not collect personal information from anyone, including children.

## Changes

Any changes to this policy will be posted in this file, which is part of the public source code at <https://github.com/allenrobberson/dropocalypse>.

## Contact

Questions? Ask in [Discussions](https://github.com/allenrobberson/dropocalypse/discussions) or [open an issue](https://github.com/allenrobberson/dropocalypse/issues).
