# AGENTS.md - Chiaki webOS

Guidelines for AI agents working in this repository. Keep this file concise -
only document constraints and rules an agent would get wrong without being told.
Implementation details belong in code comments, not here. What works today and
what is unproven: [`docs/STATUS.md`](docs/STATUS.md).

## Project Overview

**Chiaki webOS** (repo `chiaki-webos`) is a native LG webOS TV app: a PlayStation
Remote Play client, porting [chiaki-ng](https://github.com/streetpea/chiaki-ng).
C11, SDL2 + LVGL 8.3.11 (software rendered). No web stack.

- **App:** id `org.homebrew.chiakiwebos`, title "Chiaki webOS", binary `chiaki`, version in `appinfo.json`
- **Target TV:** LG 50UP75003LF, webOS TV Lite 6.5.3, 1920x1080, kernel 4.4.84 armv7l, 4 cores,
  1.3 GB RAM (~340 MB free at runtime), Wi-Fi only (no Ethernet)
- **Install:** Developer Mode sideload; SSH as `prisoner` on port 9922
- **Upstream:** `chiaki-ng/` is a git submodule; only `chiaki-ng/lib/` (libchiaki) will be used

## Architecture

```
src/
  main.c            entry: init modules, wire hooks, main loop (no logic here)
  log.[ch]          the only logger: /tmp/chiaki.log + on-screen Log page feed
  app.[ch]          state the UI renders: hosts, selection, connection, registration; glue to config/session
  platform/         display.[ch]: SDL window + streaming texture + LVGL display driver
  input/            remote + DualShock 4 -> LVGL indevs: input.h API, keypad.c, gamepad.c, pointer.c
  ui/               theme.[ch] tokens+styles, widgets.[ch] components, logo.[ch] the app mark,
                    tabs.[ch], log_view.[ch], splash.[ch] startup overlay, register.[ch] the
                    "Add a console" overlay, qr.[ch] QR widget, ui.[ch] screen assembly,
                    page_<name>.c one per tab (pages.h)
  config/           settings, consoles, PSN account, device UID; one key=value file (config.h)
  chiaki/           session.[ch]: libchiaki wrapper (stub; registration simulated)
  media/            ndl.[ch]: NDL hardware video/audio (stub)
  vendor/           qrcodegen.[ch]: Nayuki's QR encoder (MIT), vendored unmodified
docs/               STATUS.md, raw hardware evidence (DS4 map, network probe)
tests/host/         macOS behaviour tests (see Testing)
sim/                macOS TV simulator: the real app in a window (see Testing)
scripts/            chiaki-lg cross-build recipe for libchiaki + deps (reference, not wired in)
assets/             the app icon PNGs (icon.png 80, largeIcon.png 130, icon-400.png 400)
```

### Key Invariants

- Dependency direction: `log` <- `platform`, `input`, `config` <- `app` <- `ui` <- `main`.
  `input` never includes `ui/` (it reports through `input_hooks_t`); `app` never includes
  LVGL (the UI is told about changes through `app_listener_t`); pages talk to `app`/`config`,
  never to each other
- LVGL is single-threaded: only the main loop touches it. libchiaki calls back on its own
  threads, so `session.c` must queue events for `session_poll()` (drained in `app_tick()`)
- Public functions carry their module prefix (`input_`, `ui_`, `tabs_`, `page_<name>_`, `app_`,
  `config_`, `session_`, `ndl_`, `log_`); everything else is `static`; file globals are `g_*`;
  shared styles are `st_*` from `ui/theme.h`
- New module: add `src/<dir>/<name>.[ch]` (CMake globs `src/**/*.c`) and init/tick it from
  `main.c`. New tab page: follow the note in `ui/pages.h`. Build screens from `ui/widgets.h`
- Stubs return false/"not available" honestly; keep the `DEMO`/`TODO(session)` markers in
  `app.c` and the `TODO(libchiaki)`/`TODO(http)` call sites in `session.c` until the real
  path replaces them
- Config mutators only change memory: save afterwards (`config_save()`, or
  `app_settings_changed()` for debounced settings edits); `app_*` re-reads the host list
  itself after its own changes

## Build, Deploy, Logs

```sh
./build.sh   # -> dist/org.homebrew.chiakiwebos_<ver>_arm.ipk (fetches LVGL v8.3.11 into third_party/ on first run)

SKILL=~/dietpi/.opencode/skills/webos-tv-sideload
WEBOS_HOST=10.10.10.12 $SKILL/webos-install.sh terminate org.homebrew.chiakiwebos
WEBOS_HOST=10.10.10.12 $SKILL/webos-install.sh install dist/org.homebrew.chiakiwebos_*_arm.ipk org.homebrew.chiakiwebos
WEBOS_HOST=10.10.10.12 $SKILL/webos-install.sh launch org.homebrew.chiakiwebos

ssh -i ~/.ssh/webos_rsa -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  -p 9922 prisoner@10.10.10.12 'tail -30 /tmp/chiaki.log'

sim/run.sh   # macOS: build the real app and run it in a window (see Testing)
```

- Toolchain: `~/webos-sdk/arm-webos-linux-gnueabi_sdk-buildroot` (webosbrew native toolchain),
  packager: `~/webos-sdk/bin/ares-cli-rs-v0.7.0-macos-aarch64` (setup in `README.md`)
- Install with the skill's `webos-install.sh`, never `@webosose/ares-cli` (broken on modern Node)
- SSH refuses the 6-char passphrase as a password; when SSH fails, re-fetch the key:
  `WEBOS_PASSPHRASE=XXXXXX $SKILL/webos-install.sh get-key` (passphrase rotates with Dev Mode)

Verify every change on the TV: zero compiler warnings (`-Wall -Wextra`), install, launch, then
the log must show `UI created`, `gamepad ... grabbed` (if the pad is on) and `indev ready`, and
`luna://com.webos.applicationManager/dev/running` must list the app. Keep those log lines.

## Testing

- `tests/host/run.sh` (macOS, `brew install sdl2`): builds every module except `main.c` and the
  SDL display, renders offscreen and drives the real `gamepad.c` through a fake evdev FIFO. It
  asserts config persistence, layout (no overflow, no clipped focus rings), tabs,
  D-pad/dropdown, press-cancel, the session flow, scrolling, hotplug, the registration overlay,
  a simulated registration, the empty host list and the startup splash, and writes screenshots
  to `out/host-tests/shots/`. Its config is `out/host-tests/config` (wiped per run, there are
  no demo consoles: it seeds its own). Run it after UI/input changes; add an assertion for new
  behaviour
- `sim/run.sh` (macOS) builds the **whole** app (incl. `main.c`, `display.c`) and runs it in a
  window for fast iteration: the mouse drives the pointer path (hover/click), the keyboard is
  the TV remote, `CHIAKI_NO_SPLASH=1` skips the splash, settings persist in `out/sim/config`.
  It compiles `gamepad.c` against `tests/host/shim`, so no `/dev/input` is needed. The LVGL
  display is still 1920x1080; only the window is scaled, so the layout matches the device
- The host tests and the simulator do not replace the TV check: rendering speed, the real DS4
  and webOS are only proven on the device

## Gotchas

- **OpenSSL must be linked statically.** The TV ships 1.0.2, the SDK sysroot 1.1; a runtime
  `libssl.so.1.1` dependency fails to load and WebAppMgr relaunches the app in a loop
- **The DS4 map is non-standard** (webOS uhid stack, not hid-sony): buttons are the HID order
  shifted onto 0x130+ (Cross 0x131, L1 0x134, R1 0x135, L2/R2 clicks 0x136/0x137, Options
  0x139), right stick is `ABS_Z`/`ABS_RZ`, triggers are `ABS_RX`/`ABS_RY`. Read
  `docs/ds4-map-observed.txt` before touching `input/gamepad.c`
- **No system keyboard for native apps**: the webOS virtual keyboard is a web-app feature;
  text entry uses our LVGL keyboard (`ui.c`, `ui_keyboard_attach()`)
- **Display path**: LVGL renders in software into a streaming `SDL_Texture` (renderer is
  `opengles2`); `lv_draw_sdl` is not used. Keep both full-screen draw buffers in mind for RAM
- **No journald / stdout on the TV**: log only through `log.h`; `/tmp/chiaki.log` is truncated
  on each launch
- **NDL headers are not in this SDK's sysroot** (despite `scripts/chiaki-lg-CMakeLists.txt`);
  chiaki-lg reaches NDL through `mariotaku/ss4s` modules
- **Tabs have no swipe on purpose**: the tabview content is not `SCROLLABLE`, but
  `lv_tabview_set_act(..., LV_ANIM_ON)` still animates; keypad focus moving to another page is
  followed by `tabs.c` (LVGL's scroll-on-focus no longer does it)
- **LVGL 8.3 input quirks we work around**: `LV_KEY_PREV/NEXT` always move group focus, so
  `keypad.c` turns them into `UP/DOWN` while a dropdown is open; `PRESS_LOCK` makes a press
  dragged off a widget still click, so `pointer.c` cancels it (`lv_indev_wait_release`)
- **Clipping**: layout containers must not clip (`ui_box()`/`ui_no_clip()` set
  `OVERFLOW_VISIBLE`); cards are the clip boundary, padded for focus rings and button shadows
  (static assert in `theme.c`). Raw `lv_obj_create` containers will cut focus rings
- **Icon**: the app icon lives in `assets/` (icon.png 80 for the launcher, largeIcon.png
  130, icon-400.png for preview); `CMakeLists.txt` installs the first two to the app root.
  It is a checked-in asset with no generator script - redraw it externally if it changes
- **The app mark** lives in `ui/logo.c` (`ui_logo()`), used by the top bar and the splash
  so both match the launcher icon; it draws the play-waves glyph procedurally on an
  `lv_canvas` (rounded gradient tile + a white triangle/arcs), no embedded image
- **Startup splash**: `ui/splash.c` is on `lv_layer_top()`, swallows input, and deletes
  itself after `SPLASH_MS`; `CHIAKI_NO_SPLASH=1` skips it and `CHIAKI_SPLASH_MS=<ms>`
  overrides the hold time (both handy in the simulator)
- **Interactive overlays** (`ui/register.c`) on `lv_layer_top()` need: their own focus group
  handed to the keypad (`input_set_group()`, give `ui_group()` back on close), the default
  group set to NULL while building (LVGL auto-adds buttons to it), `lv_obj_move_background()`
  to stay under the stick cursor (also on the top layer), the keyboard borrowed with
  `ui_keyboard_host()` (the screen is below the top layer), and closing from their own
  button/key event through `lv_async_call()` (the keypad still uses the object afterwards)
- **Config file**: `$CHIAKI_CONFIG_DIR/chiaki.conf`, else `$HOME/conf/chiaki.conf` on the TV
  (HOME is the app dir; survives reboots, surviving a reinstall is unconfirmed: `TODO(path)`),
  else `/tmp/chiaki-webos`. It holds registration keys and PSN tokens (mode 0600)

## Code Style

- C11, 4-space indent, K&R braces, `snake_case`, ~120 columns; every file starts with a
  `/* path - purpose */` comment, headers document their module's contract
- UI: only design tokens from `ui/theme.h` (`COL_*`, `SP_*`, `R_*`, `FONT_*`, layout sizes), no
  magic numbers; everything fits 1920x1080 with no overflow (top bar and action bar fixed, tabs
  grow)
- Every interaction must work with both the DS4 (stick cursor, D-pad, Cross) and the TV remote
- Logging: lifecycle/diagnostics -> `log_msg()`; user-visible events ->
  `log_event(LOG_*, "TAG", ...)` (upper-case tag, no brackets), written as `ui: [TAG] msg`
- Commit as the personal author:
  `git -c user.email=sonerkoksal@gmail.com -c user.name="Soner Köksal" commit`

## Roadmap

1. NDL media: hardware decode of H.264/H.265 + Opus via `media/ndl.h`, measure latency
2. Build libchiaki for the TV (reuse `scripts/chiaki-lg-build-webos.sh`) and do a real PS5
   handshake in `chiaki/session.c`
3. Wire the session into the UI (`app.c` replaces the simulated connection; pad -> session)
4. Real registration: `chiaki_regist_start` for the PIN path, the PSN token exchange over HTTP
   and PSN registration (the `TODO(libchiaki)`/`TODO(http)` sites in `chiaki/session.c`),
   discovery for console state, wake-up from standby
