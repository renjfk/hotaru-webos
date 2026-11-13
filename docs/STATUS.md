# Status

What works, and where it was proven. The device: LG 50UP75003LF, webOS TV Lite 6.5.3, kernel 4.4.84 armv7l,
4 cores, 1.3 GB RAM (~340 MB free while the app runs), on 5 GHz Wi-Fi.

Evidence comes from three places:

- **TV**: the app on the device, read back from `/tmp/chiaki.log` (`scripts/tv.sh log`)
- **Probe**: a standalone probe app run on the TV before this one; its raw results are kept here:
  [ds4-map-observed.txt](ds4-map-observed.txt), [dualsense-map-observed.txt](dualsense-map-observed.txt),
  [pad-battery-observed.txt](pad-battery-observed.txt), [network-capabilities.txt](network-capabilities.txt),
  [wifi-stalls-observed.txt](wifi-stalls-observed.txt)
- **Host**: `tests/host/run.sh` (the real modules on macOS, a fake controller, a simulated console)

What still needs the TV is listed in [TV-CHECKS.md](TV-CHECKS.md).

## Summary

| Area | Proven by |
| --- | --- |
| Native app, packaging, sideload, the display (LVGL into an SDL texture, 1080p) | TV |
| Remote Play with a PS5 on the local network and over PSN (holepunch) | TV |
| Hardware video (H.264, H.265) and 48 kHz audio through NDL DirectMedia | TV |
| 60 fps with no socket drops or FEC failures; Smooth playback hides the Wi-Fi stalls | TV |
| PSN sign-in (OAuth code from the phone), the account's console list, token refresh | TV + host |
| Settings, consoles and the PSN account kept in `conf/chiaki.conf` | TV + host |
| DualShock 4: grab, the button map, forwarding to the console, rumble | TV (probe + earlier builds) + host |
| DualShock 4 battery on Home, the Controller panel and the stream menu (sysfs `power_supply`) | TV (probe) + host |
| Home, Add a console, the dialogs, the menu over the game (this UI) | host + simulator: the earlier tabbed UI ran on the TV |
| Console discovery: state, firmware, waking before connecting, following an address | host (simulated console) |
| One console with two routes, PSN fallback, the saved route choice | host |
| PIN pairing | host: the libchiaki path is real but not yet run against a console |
| HDR10 (`NDL_DirectVideoSetHDRInfo` with the VUI codes) | wired, not yet looked at on the TV |
| DualSense: the button map (the DualShock 4's); no rumble or touches reach the app | TV (probe) + host |
| Remote number keys, Magic Remote, launch parameters | wired for both possibilities; the probe decides |

## Proven on the TV

**Platform.** Built with webosbrew's native toolchain, packaged with ares-cli-rs, installed with
`luna-send-pub .../dev/install`; runs as a jailed native app. `SDL_GetCurrentDisplayMode` reports 1920x1080 and a
refresh rate of 0 Hz; the renderer is `opengles2`. LVGL renders in software into two full-screen buffers that are
copied into one streaming texture. Idle CPU ~0.7% of a core, RSS ~33-46 MB. There is no journald and no usable
stdout: the app logs to `/tmp/chiaki.log` only. Over SSH, `/usr/bin/python`, `node` and `nc` exist; `ping` is not
allowed for `prisoner` and `iw`/`ip` are missing; per-thread CPU and run-queue waits are in
`/proc/<pid>/task/*/schedstat`.

**Controller.** The DualShock 4 is a webOS uhid device: `EVIOCGRAB` works, so the TV stops reacting to it. Its map
is not hid-sony's (buttons in HID order from 0x130, the right stick on `ABS_Z`/`ABS_RZ`, the triggers on
`ABS_RX`/`ABS_RY`): [ds4-map-observed.txt](ds4-map-observed.txt). The touchpad is `ABS_MT_*` on the same node.
`FF_RUMBLE` works with the node opened read-write. hid-sony also publishes its battery on the uhid node
(`power_supply/.../capacity`, 60% while discharging), which Home and the Controller panel show; a DualSense has no
such node (hid-generic): [pad-battery-observed.txt](pad-battery-observed.txt).

**Network.** UDP unicast and broadcast, DNS, TCP, TLS 1.3 (OpenSSL linked statically) and listening sockets all work
for a native app: [network-capabilities.txt](network-capabilities.txt). PSN's TLS needed the CA bundle path set for
libcurl.

**Streaming.** libchiaki calls the video and audio callbacks on its receive thread; doing NDL and Opus work there
let the stream socket overflow under load (1,700+ drops in `/proc/net/udp`, FEC failures, ~170 ms IDR waits), while
the main thread burnt ~55% of a core repainting a UI nobody saw. The fixes: media on `media/feeder.c` threads, the
socket buffer raised to `rmem_max` once connected, and LVGL's refresh paused while the game shows. After them: 0
drops, 0 FEC failures, the main thread at ~1% of a core. A `stats:` line every 10 s shows fps, Mbps, losses and
queue depths.

**NDL.** `NDL_DirectAudioPlay` blocks once ~80 ms of PCM is queued (which paces the audio feeder);
`NDL_DirectVideoPlay` returns in 2 ms or less in steady state (up to 80 ms right after load); the render queue holds
5 frames at most. NDL draws on a plane below the app surface, so while streaming the surface is cleared
transparent and only the menu is drawn on it.

**Wi-Fi stalls.** This TV's Wi-Fi receives nothing for ~115 ms twice a second, all the time: the `wlan0` counter
sampled at 2 ms shows it, a wired PS5 stalls the same way, a Mac on the same access point does not, and it persists
with the controller off and with AirPlay, TV On With Mobile and Quick Start+ off. The cause on the TV is unknown
(a MediaTek Wi-Fi/Bluetooth combo with Miracast's `p2p0` up). Smooth playback (`media/pacer.c`) plays the video on
an even cadence behind an adaptive delay; 150 ms made the stream fluid. On an Ethernet cable the stream is fluid
without it: the stalls are the TV's Wi-Fi only. Raw data and the probe scripts:
[wifi-stalls-observed.txt](wifi-stalls-observed.txt).

**Restarts.** The PS5 refuses a new session for several seconds after one ends ("Remote Play is already in use",
~9 s seen), so restarting the stream for new video settings retries every 2 s, 10 times at most.

## libchiaki

`scripts/chiaki-lib-build-webos.sh` cross-builds libchiaki and its static dependencies (OpenSSL 3, Opus, json-c,
miniupnpc, libevent, curl with WebSockets, Jerasure, GF-Complete, nanopb) into `out/chiaki-lib-staging/`. Patches go
to a copy of the sources, for the SDK's glibc 2.12 (`pthread_clockjoin_np`, `aligned_alloc`). Everything is linked
statically: the binary needs no `libssl`, `libcrypto` or `libcurl` (the TV ships OpenSSL 1.0.2, the SDK 1.1).
`scripts/chiaki-lib-build-macos.sh` builds the same natively, so the simulator can run the real backend.

## Config

One `key=value` file, written atomically with mode 0600 (it holds the pairing keys and PSN tokens): on the TV
`$HOME/conf/chiaki.conf`, where the jailer sets `HOME` to the app's install directory. Writing it works and it
survives reboots; whether it survives an update over the same app id is one of the [TV checks](TV-CHECKS.md). If the
directory is not writable the app falls back to `/tmp/chiaki-webos`, which a reboot clears.

## Constraints

- One `.ipk` for every TV: the binary is 32-bit ARM (EABI5, soft float), what webOS TVs run even on 64-bit chips
  (moonlight-tv ships one `_arm.ipk` for all of them), needs glibc 2.12 at most and links only the TV's SDL2 and
  `libNDL_directmedia`. Its NDL calls are the v2 API, which ss4s (moonlight-tv's media layer) uses from webOS 5 on;
  a webOS 4 TV has the v1 API only, so reaching it takes a second media backend picked at runtime, not a second
  package
- OpenSSL must be linked statically: a dynamic `libssl.so.1.1` fails to load and the app is relaunched in a loop
- ~340 MB of free RAM; the two LVGL draw buffers take ~16 MB
- Wi-Fi: the bitrate must suit the link, and the stalls above need Smooth playback; on Ethernet they are gone
- The webOS on-screen keyboard is for web apps: this app has its own
- The Developer Mode SSH key's passphrase changes with every Developer Mode session (`scripts/tv.sh key`)
