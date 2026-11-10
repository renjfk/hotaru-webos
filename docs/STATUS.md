# Status

What is wired and proven, and how. Device: LG 50UP75003LF, webOS TV Lite 6.5.3,
kernel 4.4.84 armv7l, 4 cores, 1.3 GB RAM (~340 MB free at runtime), Wi-Fi.

> Picking this up fresh? See [`HANDOVER.md`](HANDOVER.md) for the current state,
> how to build/test/verify, and the next task (console registration).

Evidence comes from three places:
- **TV**: the app running on the device, read back from `/tmp/chiaki.log` over SSH.
- **Probe**: the earlier standalone `webOS Probe` app (`~/webos-probe`), raw results
  copied into this folder: [`ds4-map-observed.txt`](ds4-map-observed.txt),
  [`network-capabilities.txt`](network-capabilities.txt).
- **Host**: `tests/host/run.sh` (macOS, real modules, fake evdev pad).

## Summary

| Area | State | Proven by |
| --- | --- | --- |
| Native app, packaging, sideload | done | TV |
| Display (LVGL -> streaming SDL texture, 1080p) | done | TV |
| UI shell (theme, tabs, pages, keyboard) | done | TV + host |
| Startup splash | done | host + sim (not yet observed on the TV) |
| macOS simulator (real app in a window) | done | sim |
| DS4 input in the UI (cursor, D-pad, L1/R1, scroll) | done | TV + host |
| DS4 hotplug (offline -> reconnect) | done | host; not yet observed on the TV |
| DS4 full map, touchpad, rumble | proven, not used by the app yet | probe |
| TV remote keypad | done | TV (earlier builds) + host |
| Network transport (UDP/bcast, DNS, TCP, TLS 1.3, listen) | proven, not used by the app yet | probe (17/17) |
| Video/audio (NDL) | not started | - |
| libchiaki cross-build (static + deps) | done | build + link test; not run on the TV |
| Session wiring (console -> Start Remote Play, events) | real via libchiaki; media dropped | TV build links (handshake unverified) |
| Settings, consoles, PSN account persistence | done (`key=value` file) | host; TV path unconfirmed |
| Registration PIN | real via libchiaki | not verified on the TV |
| Registration PSN (QR/code) | UI + authorize URL; http/holepunch pending | host + sim |
| Wake-up, discovery | wake-up via libchiaki; discovery not started | - |

## Platform

- Built with the webosbrew native toolchain, packaged by `ares-cli-rs`, installed via
  `luna-send-pub .../dev/install` (`webos-install.sh`). Runs as a jailed native app.
- Display: `SDL_GetCurrentDisplayMode` reports 1920x1080, renderer `opengles2`. LVGL
  8.3.11 renders in software into two full-screen buffers, flushed areas are copied
  into one streaming `SDL_Texture`. Idle CPU ~0.7% on the TV (7 ticks in 10 s,
  measured on the same UI before the module split); RSS ~33 MB.
- Logging: `/tmp/chiaki.log` only (no journald, no usable stdout).

```
Chiaki webOS starting (LVGL 8.3.11)
display mode 1920x1080
renderer name=opengles2
display 1920x1080 ready
ui: [APP] ready
ui: [NET] wlan0 10.10.10.12
ui: [CHIAKI] libchiaki not linked yet, sessions are simulated
ui: [INPUT] L-stick cursor, R-stick scroll, D-pad navigate, Cross select, L1/R1 tabs, Options quit
UI created (1920x1080)
no gamepad found
ui: [INPUT] no gamepad found, use the TV remote (still looking)
indev ready
```
(TV, current modular build, pad switched off: the app keeps rescanning.)

## Controller (DualShock 4)

- **Grab**: the pad is a webOS uhid device; `EVIOCGRAB` succeeds, so the app owns it
  exclusively and the TV stops reacting to it.
- **Map**: non-standard, see [`ds4-map-observed.txt`](ds4-map-observed.txt). Used by
  the app: left stick `ABS_X/Y`, right stick `ABS_Z/RZ`, D-pad `ABS_HAT0X/Y`, Cross
  0x131, L1 0x134, R1 0x135, Options 0x139.
- **Touchpad**: `ABS_MT_POSITION_X/Y`, `ABS_MT_TRACKING_ID`, click 0x13d on the same
  node (probe live view). Not read by the app yet.
- **Rumble**: node advertises `FF_RUMBLE`; an `EVIOCSFF` upload + `EV_FF` write ran the
  motors on the real controller (probe, node opened read-write). Not used by the app
  yet (the app opens the node read-only).
- **In the app**: pointer cursor, Cross click with press-cancel when the cursor leaves
  the widget, D-pad as keypad (auto-repeat, drives open dropdowns), L1/R1 animated
  tab switch, right-stick scrolling, Options quits. On the TV (input code identical to
  the current build, before the rename):
  ```
  gamepad /dev/input/event9 "Wireless Controller" grabbed
  ui: [INPUT] DualShock 4 ready, stick moves the cursor
  indev ready
  ui: [TAB] switched to Video (R1)
  ui: [TAB] switched to Input (R1)
  ui: [TAB] switched to Log (R1)
  ui: [TAB] switched to Input (L1)
  ui: [HOST] Selected Bedroom PS5 (10.10.10.41)
  ui: [HOST] Wake-up packet sent to Bedroom PS5
  ui: [STREAM] Session started with Bedroom PS5 at 1920x1080
  ```
- **Hotplug**: rescans every 1.5 s while offline (node number may change), health
  check every 2 s while attached; status light goes red when lost. Host tests cover
  unplug -> offline -> replug on a new node -> reconnected, a silent node removal, and
  a held press during the drop (no click). A real power-cycle has not been captured in
  the TV log yet; expect `gamepad ... lost: ...`, `ui: [INPUT] gamepad offline`, then
  `ui: [INPUT] gamepad reconnected: /dev/input/eventN "Wireless Controller"`.

## TV remote

SDL key events -> LVGL keypad: UP/DOWN move focus, LEFT/RIGHT adjust or switch tabs on
the tab bar, OK activates. Neither the remote nor the D-pad can type on our on-screen
keyboard yet (it is not in the focus group); only the stick cursor can.

## Network

From the probe, as a native app on this TV: interfaces, UDP unicast and broadcast
(needed for discovery/wake-up), DNS, TCP, TLS 1.3 (OpenSSL linked statically), and
listening sockets all pass: 17/17, see
[`network-capabilities.txt`](network-capabilities.txt). The app itself only reads the
interface address so far (`ui: [NET] wlan0 10.10.10.12`).

## UI

LVGL theme (PlayStation-blue on dark navy, 40/32/28/24/20/16 px type scale), four tabs
(Hosts, Video, Input, Log) with a sliding pill and no swipe, settings rows, dropdowns,
sliders, switches, on-screen keyboard, log console, status lights, controller hints,
focus rings that are never clipped, hover highlight under the cursor, app icon,
startup splash, and the "Add a console" overlay. Host tests assert no overflow on any tab
or on the overlay and no clipped focus ring (20 controls on the tabs, 14 on the overlay),
and that the splash shows on the top layer and auto-dismisses.
The Hosts list is the registered consoles from the config (no demo consoles any more);
with none it shows an empty state pointing at "Add host". A console's state, firmware
and address come from discovery, which does not exist yet: they show as "Unknown"/"-".
Connecting goes through `chiaki/session.c` (see Session), which simulates the 3 s handshake
until libchiaki is linked.

The **startup splash** (`ui/splash.c`) is a full-screen overlay on `lv_layer_top()`
shown once after `ui_create()`: the app mark (`ui_logo()`, the icon's play-waves glyph
drawn procedurally on an `lv_canvas` - a rounded gradient tile plus a white triangle and
two signal arcs) over "Chiaki webOS" and a spinner, fading in and dismissing itself after
1.5 s. `CHIAKI_NO_SPLASH=1` skips it and `CHIAKI_SPLASH_MS=<ms>` overrides the hold time.
The same `ui_logo()` mark is shown at the top-left of the app view.

## Registration

"Add host" (Hosts) and "Add a console" (Input) open `ui/register.c`, a full-screen overlay
on `lv_layer_top()` with its own keypad focus group and the borrowed on-screen keyboard:

- **PIN**: console IP + the 8-digit PIN from Settings > System > Remote Play > Link Device.
- **PSN account**: Sony's authorize URL (`duid=` the stable device UID from config) as a
  QR code (`ui/qr.c`, vendored Nayuki `qrcodegen` v1.8.0) plus the URL text; the user signs
  in on a phone and types the `code=` value (the whole redirect URL also works). The QR in
  the host-test screenshot decodes back to the exact URL (macOS CoreImage).

`app.c` runs the state machine (idle / running / success / failed, progress, error) and
announces `APP_CHANGED_REG`; on success the console is added to the config, saved,
selected and listed. Two backends live behind `chiaki/session.h`: `session_real.c`
(`CHIAKI_HAVE_LIBCHIAKI`) calls libchiaki, and `session.c` simulates the flow (~2 s, an
obviously fake "Simulated PS5", `02:00:..`; PIN `00000000` and the code `fail` fail) for the
host tests and the simulator. **The PIN path is real** (`chiaki_regist_start` + the
`ChiakiRegistEvent` callback fills a `config_host_t`). PSN is not wired yet: the authorize
URL/QR is real, but the code -> token exchange (`TODO(http)`) and the holepunch registration
(`TODO(libchiaki)`) are not.

## Session

`app_connect()` starts `session_start(config_host(app_selected()), &config_get()->stream)`;
`app_tick()` drains `session_poll()` and maps the events to app state: progress while connecting,
connected -> Streaming, quit -> Idle; a login-PIN request and rumble are logged (the PIN prompt
and the pad -> session forwarding are `TODO`). `app_disconnect()` calls `session_stop()`.

`session_real.c` builds a `ChiakiConnectInfo` from the stored console (regist key + rp_key as
`morning`, video profile from the stream settings), libchiaki runs the handshake on its own
threads and pushes events into the queue. **Media is not wired yet**: the video sample and the
audio frames are accepted and dropped (`TODO(ndl)`). `session.c` simulates the whole session
(~3 s) for the host tests and the simulator. `session_wakeup()` is real (a discovery wake-up)
and needs the console's address.

The host tests walk the whole path on the simulated backend: add a console (PIN and PSN), Start
Remote Play with it, reach Streaming, and Disconnect stops it.

## libchiaki

`scripts/chiaki-lib-build-webos.sh` cross-builds libchiaki (chiaki-ng 43728af) and its static
deps (OpenSSL 3.2.1, Opus, json-c, miniupnpc, libevent, curl with WebSockets, Jerasure +
GF-Complete, nanopb) into `out/chiaki-lib-staging/` (`include/`, `lib/`, `link.txt`). Patches
go to a **copy** of the sources for the SDK's glibc 2.12 (`pthread_clockjoin_np` ->
`pthread_timedjoin_np`, `aligned_alloc` -> `posix_memalign`). `build.sh` links the staging when
it exists, statically, so the binary needs no `libssl`/`libcrypto`/`libcurl`. **Nothing has
been exercised against a real console yet** (the TV is offline); PSN/holepunch TLS would need
`SSL_CERT_FILE`/`SSL_CERT_DIR` if the TV has no CA bundle.

## Config file

One `key=value` text file (format in `config/config.h`): stream + input settings, the
consoles with their registration keys, the PSN account and tokens, the device UID. Written
atomically (temp file, mode 0600, `rename`); a missing or corrupt file falls back to the
defaults line by line. Settings edits are saved debounced (0.8 s) and on quit,
registration/forget/PSN changes immediately.

- Location: `$CHIAKI_CONFIG_DIR/chiaki.conf` if set (host tests, simulator); on the TV
  `$HOME/conf/chiaki.conf`. The jailer sets `HOME` to the app's install directory
  (`/media/developer/apps/usr/palm/applications/org.homebrew.chiakiwebos`), which is
  writable on webOS 4+ and survives reboots; moonlight-tv keeps its pairing keys there the
  same way. **Not yet confirmed on this TV** (offline): that the directory is writable and
  what an update over the same app id does to `conf/` (an uninstall removes it). If it is
  not writable the app logs `[CONFIG] ... not writable` and falls back to
  `/tmp/chiaki-webos` (lost on reboot).

## Development tooling (macOS)

- `tests/host/run.sh`: behaviour tests, real modules rendered offscreen against a fake
  evdev pad (`brew install sdl2`). 48 checks, screenshots to `out/host-tests/shots/`; the
  config lives in `out/host-tests/config`, wiped per run, and the test seeds its own consoles.
  The registration overlay is driven end to end through its own fields and buttons (both the
  PIN and the PSN code path, success and the simulated failure).
- `sim/run.sh`: the **whole** app - including `main.c` and the SDL display - built and run
  in a resizable window for fast iteration. The LVGL display stays 1920x1080 and only the
  window is scaled, so the layout matches the TV; the mouse drives the cursor path and the
  keyboard is the TV remote. `gamepad.c` compiles against `tests/host/shim`, so no
  `/dev/input` is required. Verified running: SDL `metal` renderer, `/tmp/chiaki.log`
  shows the full startup including `ui: [APP] splash shown` / `splash dismissed`.

## Not started / stubs

- **Media** (`media/ndl.[ch]`): no decode or video plane yet. The NDL headers and stub
  library are not in this SDK's sysroot; chiaki-lg reaches NDL through
  `mariotaku/ss4s`. How the hardware video plane composes with the LVGL layer is
  unverified.
- **Protocol** (`chiaki/session.[ch]`): libchiaki **is** cross-built (`scripts/chiaki-lib-build-webos.sh`)
  and statically linked when the staging exists; the simulated backend (`session.c`) is used
  otherwise (host tests, simulator). The real handshake has never run against a console.
- **HTTP**: no client yet; the PSN token exchange is a `TODO(http)` (the staged `libcurl.a` is
  available for it), PSN registration is `TODO(libchiaki)` (holepunch).
- **Wake-up / discovery**: `session_wakeup` is real but has no discovery yet; console state is
  unknown, and the pad is not yet forwarded to a session (`session_set_pad` exists, unused).

## Constraints

- OpenSSL must be static (TV 1.0.2, SDK 1.1; a dynamic `libssl.so.1.1` loops the app).
- ~340 MB free RAM at runtime; the two LVGL draw buffers take ~16 MB.
- Wi-Fi only; stream bitrate must suit the TV's wireless link.
- No webOS system keyboard for native apps.
- Developer Mode SSH key passphrase rotates; re-fetch with `webos-install.sh get-key`.
