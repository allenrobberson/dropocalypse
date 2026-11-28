# Phone Remote

Control Dropocalypse from your phone while the computer stays plugged into the screen or projector. There's no app to install: the controls open in your phone's web browser. Mac, Windows and Linux, since 1.6.5 (preview).

## Pair a phone
1. Open the **Phone** menu in the toolbar (or the **Phone remote** section of the detached controls window) and turn on **Phone remote**. It's off until you turn it on.
2. Press **Show QR code to pair a phone** and scan it with your phone's camera, then open the link.
3. That's it: the phone shows the controls, laid out like the detached controls window. Changes on the phone show in the app right away, and changes in the app show on the phone.

Each code works **once**. After a phone pairs, the QR code hides itself and a new code is made for the next phone, so an audience can't scan it off the big screen. You can also type the address shown under the QR code into the phone's browser.

## Same Wi-Fi
The phone and the computer must be on the **same Wi-Fi network**. The remote only works on your local network; it never goes over the internet.

No Wi-Fi at the venue? Turn on your phone's **hotspot** and join it from the computer. Then pair as usual.

If the computer has several networks (for example Ethernet and Wi-Fi, or a VPN), a **Network** list appears under the QR code: pick the network your phone is on.

## Permissions
- **Mac:** the first time, macOS may ask to let Dropocalypse find devices on your local network. Click **Allow**. If you said no, turn it on in System Settings, Privacy & Security, Local Network.
- **Windows:** Windows may ask whether to allow Dropocalypse on networks. Allow it on **private networks**. If your Wi-Fi is set to *Public*, set it to *Private* in Windows Settings, or allow Dropocalypse in Windows Security, Firewall.
- **Linux:** if you run a firewall (ufw, firewalld), allow TCP port 47800 on your local network.

## Phones you've paired
Paired phones reconnect on their own, even after Dropocalypse or the computer restarts: open the same page again (bookmark it or add it to the home screen). **Disconnect phones** (or **Forget paired phones** when none are connected) signs every phone out; they need to scan a new QR code.

## What's on the phone
In this preview: the style picker and arrows, auto-cycle and **Change style every**, the style list with its auto-cycle checkboxes, your MilkDrop presets folder with search, and the **Motion** section with the **Logo layers** sliders. The rest of the controls window, and adding a logo from the phone's photos or camera, are coming next.

## Troubleshooting
- **"Can't reach Dropocalypse"** on the phone: check both are on the same Wi-Fi, that Phone remote is still on, and the permissions above. Some guest and venue Wi-Fi networks block devices from talking to each other; use your phone's hotspot instead.
- **"This pairing code was already used"**: press **Show QR code** again and scan the new code.
- **Wrong address in the QR code**: pick the right network under **Network**. For unusual setups, start Dropocalypse with the environment variable `DROPOCALYPSE_REMOTE_ADDRESS` set to the computer's address (for example `192.168.1.20`).
