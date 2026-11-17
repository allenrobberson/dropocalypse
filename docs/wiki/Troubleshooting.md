# Troubleshooting

| Problem | Fix |
|---|---|
| **Mac: "Dropocalypse can't be opened"** on first launch | Download the latest version: since 1.0.1 the Mac app is signed and notarized and opens normally. On an older version, see [[Installation]]. |
| **Windows: "Windows protected your PC"** | Click **More info → Run anyway**. The portable download isn't code-signed yet. The Microsoft Store edition, once it is listed, is signed by the Store. |
| **Update now doesn't install** | Nothing was changed. Open the **Updates** menu (toolbar, or the detached controls window), not the Look panel. Mac: keep the app in **Applications**. The download must be signed with the same Developer ID and accepted by Gatekeeper. Windows: keep the folder somewhere you can edit. Once this copy is code-signed, the download must be signed by the same publisher. Or use **Download page**. |
| **Windows: nothing happens when I double-click** | Make sure you extracted the zip first; don't run the app from inside it. On work or school PCs, IT policies can block unapproved apps, so ask your IT team. |
| **Windows: visuals don't react to Spotify / YouTube** | Under **Audio**, choose *System audio (what this PC is playing)*. If you use headphones and speakers, pick the *Playing on: …* entry for the device you're listening on. |
| **Windows: another app can't play while Dropocalypse is open** | Fixed in 1.2.9. Windows Media Player used to say it encountered a problem while playing the file. Open **Updates** and click **Update now**. System audio only listens, in shared mode, and the status line shows a buffer of about 10 ms. |
| **Nothing reacts when listening to an app** (Mac) | Make sure the app is actually playing, then allow Dropocalypse in **System Settings → Privacy & Security → Screen & System Audio Recording** (under *System Audio Recording Only*) and click **Restart audio**. Needs macOS 14.2 or later. |
| **Nothing reacts to the music** | Check that microphone access is on. **Mac:** System Settings → Privacy & Security → Microphone → Dropocalypse. **Windows:** Settings → Privacy & security → Microphone → *Let desktop apps access your microphone*. Then use **Audio → Restart audio** and make sure the right input is selected. |
| **It reacts, but only a little** | Raise **Sensitivity** and lower **Beat threshold**. Check that the input's level isn't turned down in **System Settings → Sound → Input**. |
| **It twitches when no music is playing** | Lower **Sensitivity**, or switch to a direct line-in feed |
| **My USB interface or app isn't listed** | Plug it in or open the app, then click **Refresh list** (Windows: **Refresh devices**) in the Audio panel |
| **My logo looks blurry** | Use a larger image; around 1000 px on the long side is ideal |
| **My logo has a white box around it** | The image has a solid background. Export it as a PNG with a transparent background. |
| **The toolbar won't appear** | Move the mouse to the very top of the window, click anywhere, or press **T** |
| **Settings or logo reset after updating from "Logo Visualizer"** | The app was renamed. Your saved logo is copied over automatically, but slider settings start fresh, and macOS asks for microphone permission again. |

## Still stuck?

Ask in [Discussions](https://github.com/allenrobberson/dropocalypse/discussions) or [open an issue](https://github.com/allenrobberson/dropocalypse/issues). Please include:

- your OS and version (for example macOS 15.1 or Windows 11 24H2)
- the Dropocalypse version (the release you downloaded)
- which style you were on
- your audio source, and what the status line in the toolbar says
