# User guide

- [Before you start](#before-you-start)
- [Installing](#installing)
- [Preparing the console](#preparing-the-console)
- [Adding a console](#adding-a-console)
- [Playing](#playing)
- [Settings](#settings)
- [Troubleshooting](#troubleshooting)
- [Uninstalling](#uninstalling)

## Before you start

Hotaru is not in the LG Content Store, so the TV has to accept apps from elsewhere. Either:

- **Developer Mode**: install LG's [Developer Mode app](https://webostv.developer.lge.com/develop/getting-started/developer-mode-app)
  from the Content Store, sign in with a free LG developer account, and turn on **Dev Mode Status** (the TV
  restarts) and **Key Server**. A Developer Mode session runs for a limited time: the app shows what is left, and
  **Extend** adds more. If it runs out, Developer Mode turns off and the apps installed with it are uninstalled,
  pairings and all, so extend it in time.
- **Root**: on a rooted TV, the [Homebrew Channel](https://github.com/webosbrew/webos-homebrew-channel) installs and
  keeps apps without Developer Mode's time limit.

You also need a PS5 or PS4, and for playing, a DualShock 4 paired with the TV over Bluetooth (in the TV's Bluetooth
settings; hold Share and PS on the controller until its light flashes). The TV remote drives everything except the
game itself.

## Installing

Download `com.renjfk.hotaru_<version>_arm.ipk` from the
[latest release](https://github.com/renjfk/hotaru-webos/releases/latest), then either:

- **webOS Dev Manager**: install [Dev Manager](https://github.com/webosbrew/dev-manager-desktop) on a computer, add
  the TV with the passphrase shown in its Developer Mode app, and install the `.ipk` from the Apps tab.
- **From this repository** (macOS or Linux with `ssh` and `curl`):

  ```bash
  WEBOS_HOST=<the TV's address> scripts/tv.sh key <passphrase>
  WEBOS_HOST=<the TV's address> scripts/tv.sh install com.renjfk.hotaru_<version>_arm.ipk
  ```

Hotaru then shows up among the TV's apps. Installing a newer `.ipk` the same way updates it.

## Preparing the console

On a **PS5**:

- Settings > System > Remote Play: turn on **Enable Remote Play**
- To start it from Hotaru while it is in rest mode: Settings > System > Power Saving > Features Available in
  Rest Mode: turn on **Stay Connected to the Internet** and **Enable Turning On PS5 from Network**

On a **PS4**:

- Settings > Remote Play Connection Settings: turn on **Enable Remote Play**
- For rest mode: Settings > Power Save Settings > Set Features Available in Rest Mode: turn on **Stay Connected to
  the Internet** and **Enable Turning On PS4 from Network**

## Adding a console

Open Hotaru and choose **Add a console** (the + tile on Home, or Settings > PSN account). There are two ways
in; a console added both ways stays one console that uses the local network first.

**With the console's PIN** (on the same network as the TV):

1. Pick the console kind.
2. The first time, the app asks for your PSN account ID, which pairing needs: sign in with your phone (below) and
   it is filled in, or type it if another Remote Play app showed it to you.
3. The address: when the app finds the console on the network it is filled in already; otherwise the start of it
   is (most homes are one network) and you type the rest. The console shows it under Settings > Network > Connection
   Status.
4. On the console, open Settings > System > Remote Play > **Link Device** and type the 8-digit PIN it shows, with
   the number pad or the remote's number keys. The PIN is valid for 5 minutes.

**With your PSN account** (works from anywhere and finds every console on the account):

1. Scan the QR code with your phone and sign in to PlayStation Network.
2. The last page stays blank: its address bar holds `...redirect?code=...`. Type the part after `code=` into the
   field (the on-screen keyboard, or a USB keyboard plugged into the TV) and choose **Link**. On the on-screen
   keyboard, **L2** or **R2** switches between capitals and small letters, like its **ABC** key.
3. The consoles on the account are added. A console you also paired with its PIN joins it.

## Playing

Pick a console on Home: the line under its name says whether it is **ready**, in **rest mode** (Start Remote Play
wakes it first), or **not on this network** (it then plays over PSN if it has that route). Choose **Start Remote
Play**.

While playing, the controller drives the console. The TV remote's **Back**, or **Share + Options** together on the
controller (**Create + Options** on a DualSense; the Connecting screen reminds you), opens the menu over the game: stream quality, smooth playback, the controller, and **Disconnect**. Back
or Circle closes it. Changing the video settings there restarts the stream when the menu closes.

A console that asks for its user's login passcode shows a 4-digit passcode sheet while connecting.

On Home, the D-pad or the left stick moves, Cross selects, Circle goes back, and L1/R1 switch between Consoles and
Settings. The TV remote's arrows, OK and Back do the same.

## Settings

- **Stream**: resolution, frame rate, codec (H.264, H.265, or HDR on a PS5), bitrate, and **Smooth playback**: a
  short delay that hides Wi-Fi hiccups. If the picture stutters, try 100 or 150 ms, or a lower bitrate.
- **Controller**: rumble (turning it on gives a short buzz), whether the touchpad's touches reach the console, and
  the controller in use. On LG TVs a DualSense works for every button, stick and trigger, but webOS passes no
  rumble, adaptive trigger effects or touchpad touches through; a DualShock 4 rumbles and its touchpad works.
- **PSN account**: add a console, find the consoles on your account again, sign out.
- **Log**: what just happened (the right stick scrolls it).
- **About**: the version, the network, and **Diagnostics**, which logs details for a bug report.

## Troubleshooting

- **The console is not found**: the TV and the console must be on the same network for the PIN way; check the
  console's Remote Play setting. Away from home, add it with your PSN account.
- **It does not wake up**: check the rest mode settings above. A console that is fully off cannot be woken.
- **"Remote Play is already in use"**: the console keeps a finished session for a few seconds; the app retries on
  its own.
- **The picture stutters**: connect the TV with an Ethernet cable if you can: TV Wi-Fi can pause for a moment
  several times a second, which no setting fully hides. On Wi-Fi, raise Smooth playback or lower the bitrate.
- **Reporting a bug**: turn on Settings > About > **Diagnostics**, reproduce the problem, and attach the log to an
  [issue](https://github.com/renjfk/hotaru-webos/issues). The log is `/tmp/hotaru.log` on the TV; with Developer
  Mode, `scripts/tv.sh save` copies it.

## Uninstalling

Remove Hotaru from the TV's app list (or from Dev Manager). Its settings, pairings and PSN sign-in go with it.
