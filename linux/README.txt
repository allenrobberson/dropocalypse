DROPOCALYPSE for Linux
The drop hits. Your logo goes off.
https://github.com/allenrobberson/dropocalypse

RUN IT
  AppImage (recommended): make it executable and double-click it, or in a terminal:
    chmod +x Dropocalypse-*-x86_64.AppImage
    ./Dropocalypse-*-x86_64.AppImage
  This folder (the .tar.gz): run ./dropocalypse. It needs these common libraries:
  libcurl, libpulse (PulseAudio or PipeWire) and OpenGL 3.3 graphics.

QUICK START
  1. Play some music. Dropocalypse listens to what the computer is playing by default.
  2. Drag your logo image onto the window, or press L.
  3. Press F for full screen.

  Move the mouse to the top edge to show the toolbar. It hides itself after a few seconds.

KEYS
  F / F11        Full screen
  T / S          Show / hide toolbar
  Esc            Hide toolbar / exit full screen
  L  (Ctrl+O)    Choose logo
  G              Show / hide logo
  Left / Right   Previous / next style
  A              Auto-cycle on / off
  D              Detach / attach the controls (two screens)
  R              Start / stop recording
  Ctrl+Q         Quit

AUDIO
  Toolbar > Audio lets you pick:
    - What this computer is playing: no microphone needed
    - "Playing on: ..." to listen to a specific output (speakers, HDMI, USB headset)
    - Any microphone, USB interface or line-in
  Works with PulseAudio and PipeWire (through pipewire-pulse, which current desktops include).

RECORDING
  Recording to MP4 uses ffmpeg. Install it with your package manager, for example:
    sudo apt install ffmpeg        (Debian, Ubuntu, Mint)
    sudo dnf install ffmpeg        (Fedora, with RPM Fusion)
    sudo pacman -S ffmpeg          (Arch)
  Choosing a logo or folder uses zenity or kdialog (usually already installed).

WHERE THINGS ARE SAVED
  Settings and your logo: ~/.config/dropocalypse
  Recordings: your Videos folder, unless you choose another.
  To remove Dropocalypse completely, delete the AppImage (or this folder) and ~/.config/dropocalypse.

Full user guide: https://github.com/allenrobberson/dropocalypse#user-guide
