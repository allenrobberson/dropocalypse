# Audio Sources

Dropocalypse can listen to music in three ways. Choose the source under **Audio** in the toolbar.

| Source | Mac | Windows | Best for |
|---|---|---|---|
| **An app** (Spotify, Music, a browser, DJ software) | ✅ macOS 14.2+ | (use System audio) | Music playing on the same computer |
| **All computer audio** | ✅ *All Mac audio*, macOS 14.2+ | ✅ *System audio*, the default | Anything playing on the same computer |
| **Microphone / line-in / audio interface** | ✅ | ✅ | Music from another device, a mixer or a live room |

## 🍎 Mac: listen to Spotify or any app (macOS 14.2+)

1. Click **Audio** in the toolbar and open the **Listen to** drop-down.
2. Under **Mac audio: no mic needed**, choose an app (apps playing right now are listed first and marked **♪ playing**) or **All Mac audio**.
3. The first time, macOS asks to allow **system audio recording**. Click **Allow**.

Dropocalypse hears exactly what that app plays, at full quality, while you keep hearing it normally. If the app hasn't played anything yet, the status says *Waiting for … to play something* and capture starts the moment it does. It also keeps working when you switch to headphones or another speaker.

This uses Apple's Core Audio *process taps*, so you don't need BlackHole, Loopback or any other extra software. On macOS 13 to 14.1 these choices don't appear; use a mic or line-in instead.

If you denied permission by mistake, turn it on in **System Settings → Privacy & Security → Screen & System Audio Recording** (under *System Audio Recording Only*), then click **Restart audio**.

## 🪟 Windows: listen to what the PC is playing

Windows listens to **System audio (what this PC is playing)** by default, so Spotify, YouTube and DJ software work with no setup. If you have more than one output (say, headphones and speakers), pick the matching **Playing on: …** entry.

## 🎤 Microphones, line-in and audio interfaces

Pick the device in the same drop-down (Mac: under **Microphone or line-in**). macOS and Windows ask for **microphone** permission the first time:

- **Mac:** System Settings → Privacy & Security → Microphone → Dropocalypse
- **Windows:** Settings → Privacy & security → Microphone → *Let desktop apps access your microphone*

For live shows, a direct feed from your mixer into a USB audio interface reacts more cleanly than a mic in a loud room. See [[Live Shows and TVs]].

## Audio panel

| Control | Default | What it does |
|---|---|---|
| **Listen to** (Mac) | System default input | A drop-down of everything you can listen to. **Mac audio** (macOS 14.2+): *All Mac audio*, or any open app such as **Spotify**, Music, Safari or Chrome. Apps playing right now are listed first and marked ♪. Dropocalypse hears exactly what that app plays, at full quality, with no mic or extra software. If the app hasn't played anything yet, it starts the moment it does. **Microphone or line-in**: built-in mic, USB mic, audio interface or line-in. |
| **Input** (Windows) | System audio | *System audio (what this PC is playing)* listens directly to the PC's sound output, so no mic is needed, and *Playing on: …* listens to one specific output device. You can also pick a mic, audio interface or line-in. |
| **Refresh devices** | | Updates the list after you plug something in |
| **Restart audio** | | Restarts listening. Use it if the visuals stop reacting after a device change or sleep. |
| **Sensitivity** | 1.00 | How easily quiet audio drives the visuals (0.2 to 4). Raise it in a quiet room or when the mic is far from the speakers. Lower it if room noise makes things twitch when no music is playing. |
| **Beat threshold** | 1.35× | How much the bass has to jump above its recent average to count as a "beat" for squash, bounce, slide, wobble and flash (1.1× to 2.2×). **Lower = more beats**, so it reacts to more of the music. **Higher = only big kicks** count. |
| *Status line* | | The active input, sample rate and buffer size, or a message if something is wrong |

The app adjusts its own levels to the loudness of the input (automatic gain), so it works with both a quiet room mic and a hot line-level signal without manual tweaking.

Audio is analyzed live on your computer. It is never recorded, saved or sent anywhere.
