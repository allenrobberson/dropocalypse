# FAQ

**Is it really free?**
Yes. No account, no ads, no tracking, no watermark, no paid tier. It's MIT licensed, so you can use it anywhere, including paid gigs and commercial streams. [Sponsorships](Support-the-Project) are optional.

**Does it record or upload my audio?**
It never uploads anything. Audio is analyzed live on your computer and thrown away, unless you press **Record**, which saves an MP4 to a folder on your own computer. The only thing Dropocalypse ever connects to is GitHub: once a day to see whether a newer version is out, and to download an update when you click **Update now**. That request sends nothing about you or your audio, and you can turn it off in the **Updates** menu (**Check for updates automatically**).

**How do I know when there's an update?**
Open the **Updates** menu in the toolbar (it is also in the detached controls window; it is not in the Look panel). It turns pink and reads **Update X.Y.Z** when a new version is out. **Update now** downloads it, checks the published SHA-256 checksum, and installs it only if that check passes. On Mac it also requires a Developer ID signature from the same team as the copy you're running, accepted by Gatekeeper. On Windows, once the copy you're running is code-signed, the new one must be signed by the same publisher. If a check fails, the menu shows the reason and the app you have stays as it is. You can also pick **Download page**, or **Ignore for 24 hours**. The Microsoft Store edition is updated by the Store instead. You can also click **Watch → Custom → Releases** on the GitHub page to get an email for each release.

**Can I make videos for TikTok, Reels or YouTube?**
Yes. Press **Record** (or **R**) and Dropocalypse saves the visuals and the music as an MP4 in 720p, 1080p or 4K. For TikTok and Reels, set **Look → Frame** to **Portrait** first to record a vertical 9:16 video. See [[Controls and Settings]].

**Can I use it on stream (OBS, Twitch, YouTube)?**
Yes. Capture the Dropocalypse window in OBS (Window Capture on Windows and Linux, macOS Screen Capture on a Mac). Hide the toolbar with **Esc** first.

**Can I use my band's or my own logo?**
That's the point! Use any logo you have the right to use. Transparent PNGs look best. See [[Your Logo and Motion]].

**Does it work with Spotify?**
Yes. On a Mac (macOS 14.2+) pick **Spotify** under **Audio → Listen to**. On Windows and Linux the default already listens to what the computer is playing, so it hears Spotify too. See [[Audio Sources]].

**Why does my Mac say it can't open the app?**
Since 1.0.1 the Mac app is signed and notarized by Apple and opens normally. If you have an older copy, download the latest version; see [[Installation]].

**Why does Windows say "Windows protected your PC"?**
The portable Windows download isn't code-signed yet, so Windows doesn't recognize the publisher. Click **More info → Run anyway**, or skip the warning entirely by getting the [Microsoft Store edition](https://apps.microsoft.com/detail/9N1TJ9D5R5C8), which is already signed by Microsoft. It's the same app, updated by the Store.

**Do I need admin rights?**
No. The Windows version is portable (unzip and run, nothing is installed), the Linux AppImage is a single file you run, and on a Mac you drag the app to Applications. On Windows you can also [get it from the Microsoft Store](https://apps.microsoft.com/detail/9N1TJ9D5R5C8).

**Can I cast it to a Chromecast?**
Not built in, but Chrome's *Cast screen* works, and AirPlay, Miracast and HDMI work better. See [[Live Shows and TVs]].

**Will it run MilkDrop presets?**
Not yet. All 25 styles are original code. Loading `.milk` presets through projectM is on the roadmap.

**Which computers does it need?**
Mac: macOS 13 Ventura or later, Apple Silicon or Intel (app audio needs 14.2+). Windows: 10 or 11, 64-bit, with a DirectX 11 GPU (practically every PC from the last decade). Linux: 64-bit Ubuntu 22.04 or later, Debian 12, Fedora, Mint, Arch and other current distributions, with OpenGL 3.3, X11 or Wayland, and PulseAudio or PipeWire.

**Can I add my own style?**
Yes. See [[Adding a Style]] and send a pull request.

**How do I report a bug or suggest a feature?**
Use [Discussions](https://github.com/allenrobberson/dropocalypse/discussions) for ideas and questions, and [Issues](https://github.com/allenrobberson/dropocalypse/issues) for bugs.
