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

Open Hotaru and choose **Add a console** (the + tile on Home). There are two ways in; a console added both
ways for the same PSN account stays one console that uses the local network first. A console is paired per PSN
account, so two players on one console get a tile each.

**With the console's PIN** (on the same network as the TV):

1. Pick the console kind.
2. The account to pair it with: with none stored the app asks for your PSN account ID, which pairing needs, and
   you sign in with your phone (below) or type it. With two or more accounts stored, choose one on the account
   step, or add another.
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
3. The consoles on the account are added. A console you also paired with its PIN for that same account joins it.

## Two players, one console

A console is paired per PSN account. To let someone else play on the same console, sign their account in under
Settings > PSN account > **Add a PSN account**, then pair the console again with its PIN while their account is
chosen. Home then has a tile for each account, with that account's PSN avatar on it (its initials until the picture
has been fetched). The picture is fetched when the account signs in; after a change on PSN, Settings > PSN account >
**Refresh consoles** fetches it again. Start Remote Play on a tile starts the session as that player. Signing out of
one account drops only its PSN-found consoles: pairings made with a PIN keep their keys.

## Playing

Pick a console on Home: the line under its name says whether it is **ready**, in **rest mode** (Start Remote Play
wakes it first), or **not on this network** (it then plays over PSN if it has that route). Choose **Start Remote
Play**.

While playing, the controller drives the console. The TV remote's **Back**, or **Share + Options** together on the
controller (**Create + Options** on a DualSense, **Select + Start** on a DualShock 3; the Connecting screen reminds
you), opens the menu over the game:
stream quality, smooth playback, the controller, and **Disconnect**. Back or Circle closes it. Changing the video
settings there restarts the stream when the menu closes. Under the console's name the menu shows the session clock
and what the stream is actually doing right now (the video rate and the frame rate the console is sending), so you
can tell a stutter from a setting that is too high. Left/Right move between the cards; L1/R1 change the focused card
(with no controller connected, the TV remote's Up/Down do the same). Cross resumes from Resume and disconnects from
Disconnect, using the saved choice: the card says whether the console will stay on or go to rest mode. The choice is
remembered.

A console that asks for its user's login passcode shows a 4-digit passcode sheet while connecting.

On Home, the D-pad or the left stick moves, Cross selects, Circle goes back, and L1/R1 switch between Consoles and
Settings. The TV remote's arrows, OK and Back do the same.

## Settings

- **Stream**: a **Preset** picks the picture and the bandwidth together: **Data saver** (720p60, 8 Mbps),
  **Balanced** (1080p60, 15 Mbps, the default) or **High quality** (1080p60, 30 Mbps). **Custom** adds the fine
  settings, Quality (resolution and frame rate) and Bitrate. The **Codec** row is always there: H.264, H.265, or
  **HDR** on a PS5 (an HDR TV switches to HDR10; a PS4 always sends H.264 without HDR). A webOS 4 TV has no H.265 and
  no HDR, so there the row offers H.264 alone. **Smooth playback** adds a
  short delay that hides Wi-Fi hiccups. If the picture stutters, try a lower preset, 100 or 150 ms of Smooth
  playback, or Custom with a lower bitrate.
- **Controller**: rumble (turning it on gives a short buzz), whether the touchpad's touches reach the console, and
  the controller in use. A DualSense works for every stick and trigger and every button but Mute. On webOS 6 it
  cannot rumble and only its touchpad click reaches the console; on webOS 11 it rumbles, but neither its touches
  nor its click reach the console. Adaptive trigger effects never reach the console on either version. A
  DualShock 4 rumbles and its touchpad works. A DualShock 3 plays with every button, both sticks, its D-pad and its
  analog L2/R2, but it has no touchpad and no rumble, and Select + Start opens the menu over the game.
- **PSN account**: one row per linked account, with how many consoles each has. Cross on one opens **Refresh
  consoles** (lists that account's consoles again, adding the new ones to Home, and fetches its avatar again) and
  **Sign out**; **Add a PSN account** signs in another one next to it.
- **Log**: what just happened (the right stick scrolls it).
- **About**: the version, the network, and **Diagnostics**, which logs details for a bug report.

## Troubleshooting

- **The console is not found**: the TV and the console must be on the same network for the PIN way; check the
  console's Remote Play setting. Away from home, add it with your PSN account.
- **The same console appears twice**: it is paired for two PSN accounts. Each tile plays as the account on it;
  forget the one you do not need from its options button.
- **It does not wake up**: check the rest mode settings above. A console that is fully off cannot be woken.
- **"Remote Play is already in use"**: the console keeps a finished session for a few seconds; the app retries on
  its own.
- **The picture stutters**: connect the TV with an Ethernet cable if you can: TV Wi-Fi can pause for a moment
  several times a second, which no setting fully hides. On Wi-Fi, raise Smooth playback or lower the bitrate.
- **A controller's buttons are wrong**: turn Settings > About > **Diagnostics** on, close and reopen Hotaru (the
  switch is kept, and the fresh log opens with the pad's keys and axis ranges), then open Settings > **Log** with the
  remote. Press each control once: Cross, Circle, Square, Triangle, L1, R1, L2 and R2 fully down, L3, R3, Create
  (Share on a DualShock 4), Options, PS, the touchpad click, each D-pad direction in this order (up, down, left,
  right), the left stick up, the right stick up. Buttons add `pad: key` lines and the D-pad, sticks and triggers add
  `pad: abs` lines, each naming the code the TV sent and what Hotaru read from it, which is what tells a wrong layout
  from a broken pad. PS and the touchpad click may not reach the app at all. Attach the log as below.
- **Reporting a bug**: turn on Settings > About > **Diagnostics**, reproduce the problem, and attach the log to an
  [issue](https://github.com/renjfk/hotaru-webos/issues). The log is `/tmp/hotaru.log` on the TV; with Developer
  Mode, `scripts/tv.sh save` copies it.

## Uninstalling

Remove Hotaru from the TV's app list (or from Dev Manager). Its settings, pairings and PSN sign-in go with it.
