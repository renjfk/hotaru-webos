# AGENTS.md - Chiaki webOS

Guidelines for AI agents working in this repository. Keep this file concise - only document constraints and rules
an agent would get wrong without being told. Implementation details belong in code comments, not here. What works
and where it was proven: [docs/STATUS.md](docs/STATUS.md); what only the TV can answer: [docs/TV-CHECKS.md](docs/TV-CHECKS.md).

## Project Overview

**Chiaki webOS** is a native LG webOS TV app: a PlayStation Remote Play client on
[chiaki-ng](https://github.com/streetpea/chiaki-ng)'s libchiaki. C11, SDL2 + LVGL 8.3.11 (software rendered), no
web stack. AGPL-3.0 with an OpenSSL exception, like chiaki-ng.

- **App:** id `org.homebrew.chiakiwebos`, title "Chiaki webOS", binary `chiaki`, version in `appinfo.json` (three
  numbers; releases write the tag into it)
- **Target TV:** LG 50UP75003LF, webOS TV Lite 6.5.3, 1920x1080, armv7l, 4 cores, ~340 MB free RAM, on Wi-Fi that
  goes quiet ~115 ms twice a second ("Smooth playback" hides it)
- **Install:** Developer Mode sideload over SSH (`prisoner`, port 9922): `scripts/tv.sh`
- **Upstream:** `chiaki-ng/` is a git submodule; only `chiaki-ng/lib/` (libchiaki) is built

## Architecture

```
src/
  main.c            entry: init modules, wire hooks, main loop; probe mode triggers, SDL event probe, frame timing
  log.[ch]          the only logger: /tmp/chiaki.log, the Log panel's feed, probe lines
  app.[ch]          state the UI renders: consoles, discovery, connection, registration; glue to config/session
  platform/         display.[ch]: SDL window + streaming texture + LVGL display driver
  input/            remote + controller -> the LVGL keypad indev: keypad.c, gamepad.c (evdev), scroll.c
  ui/               ui.[ch] screen assembly and the key sink; home.[ch] + home_settings.c the main menu;
                    stream_menu.[ch] the menu over a running stream; register.[ch] "Add a console";
                    dialog.[ch] confirm card and passcode sheet; controls.[ch] focus-driven controls;
                    motion.[ch] tweens; art.[ch] device illustrations; theme.[ch] tokens and styles;
                    widgets.[ch], logo.[ch], log_view.[ch], splash.[ch], qr.[ch]
  config/           settings, consoles, PSN account, device UID; one key=value file (config.h)
  chiaki/           session.h: the libchiaki wrapper API; session_real.c the real backend
                    (CHIAKI_HAVE_LIBCHIAKI), session.c the simulation (host tests, simulator)
  media/            ndl.[ch] NDL hardware video + PCM audio; feeder.[ch] media worker threads;
                    pacer.[ch] even playout cadence for "Smooth playback"
  vendor/           qrcodegen.[ch]: Nayuki's QR encoder, unmodified
tests/host/         macOS behaviour tests (shim/ fakes <linux/input.h>)
sim/                macOS simulator: the whole app in a window
design/menu/        design studies and their gallery (not in the app); art/ holds the art originals
docs/               STATUS.md, TV-CHECKS.md, user-guide.md, raw hardware evidence (*-observed.txt)
screenshots/        README images: app renders from the host tests, demo.mp4 recorded on the TV
scripts/            webos-sdk.sh toolchain + packager; chiaki-lib-build-{webos,macos}.sh libchiaki,
                    chiaki-lib-update.sh moves chiaki-ng forward;
                    tv.sh install/launch/log on a TV; make-device-art.sh; make-screenshots.sh (README
                    images); check_format.py, check_tidy.py (lint, see mise.toml)
cmake/              AppVersion.cmake (version from appinfo.json), AresPackage.cmake (CPack -> .ipk)
LICENSES/           license texts of bundled code that keeps them in headers (the .ipk's licenses/);
                    THIRD_PARTY_NOTICES.md lists every bundled component
assets/             app icons; devices/ the console and pad art the app loads at runtime
.github/            CI, release, prerelease, slash command; actions/build-ipk (shared .ipk build)
```

### Key Invariants

- Dependency direction: `log` <- `platform`, `input`, `config` <- `app` <- `ui` <- `main`. `input` never includes
  `ui/` (it reports through `input_hooks_t`); `app` never includes LVGL (the UI hears of changes through
  `app_listener_t`); screens talk to `app`/`config`, never to each other (`ui.c` routes keys between them)
- LVGL is single-threaded: only the main loop touches it. libchiaki calls back on its own threads, so the session
  backends queue events for `session_poll()` / `session_reg_poll()` and copy discovery's list under a lock; all are
  drained in `app_tick()`. `log_event()` from another thread is queued for `log_pump()` (also in `app_tick()`)
- Public functions carry their module prefix (`input_`, `ui_`, `home_`, `stream_menu_`, `ctl_`, `motion_`, `art_`,
  `app_`, `config_`, `session_`, `ndl_`, `log_`); everything else is `static`; file globals are `g_*` (or one
  struct); shared styles are `st_*` from `ui/theme.h`
- New module: `src/<dir>/<name>.[ch]` (CMake globs `src/**/*.c`), init/tick it from `main.c`. Every screen is built
  from `ui/controls.h`, animated with `ui/motion.h` and takes its keys from `ui.c`'s sink: no LVGL group of its own,
  nothing clickable. A new settings panel: a `SET_*` entry in `ui/home_internal.h`, built in `home_settings.c`
- One console, one host: config keeps a PIN pairing and the PSN account's device as separate entries; `app.c`
  (`hosts_sync()`) shows them as one `app_host_t` with `lan`/`psn` routes when the PSN entry has the LAN entry's
  nickname and kind (what chiaki-ng matches on). Host indexes are app indexes, not config indexes: never
  `config_host(app_selected())`
- Discovery belongs to config entries (`g_entry[]` in `app.c`), matched by MAC, else by address; a console missing
  for `DISC_MISSING_MS` is offline. It drives connecting: rest mode is woken first, offline with a PSN route (when
  discovery hears other consoles) goes over PSN at once, a MAC at a new address updates the entry. It is paused
  while streaming
- The simulation (`chiaki/session.c`) has conventions the tests rely on: PIN `00000000` and the code `fail` fail,
  an address ending in ".99" asks for the login PIN, ".98" never answers, ".97" sleeps until woken, and an unpaired
  "PS5-240" answers at 192.168.1.240 (an entry with its MAC has moved there)
- Config mutators only change memory: save afterwards (`config_save()`, or `app_settings_changed()` for debounced
  settings edits); `app_*` re-reads the host list after its own changes

## Build Commands

```bash
scripts/webos-sdk.sh                 # once: toolchain + ares-cli-rs into ~/webos-sdk (macOS arm64)
scripts/chiaki-lib-build-webos.sh    # once: libchiaki + static deps -> out/chiaki-lib-staging
./build.sh                           # dist/org.homebrew.chiakiwebos_<version>_arm.ipk (fetches LVGL on first run)
tests/host/run.sh                    # host tests (brew install sdl2); exit status = failures
sim/run.sh                           # the app in a window
scripts/chiaki-lib-update.sh [REF]   # chiaki-ng to its newest commit (or REF), notices updated; then rebuild
mise install                         # once: the pinned lint tools (mise.toml)
mise run format                      # clang-format the C sources in place
mise run lint                        # what CI checks: clang-format, clang-tidy, shellcheck, actionlint
scripts/tv.sh install && scripts/tv.sh launch --probe && scripts/tv.sh log -f
```

- Every command is also a mise task (`mise tasks`; arguments pass through: `mise run tv log -f`)
- Without `out/chiaki-lib-staging`, `build.sh` builds the simulated backend; the sim builds the real one when
  `scripts/chiaki-lib-build-macos.sh` has staged `out/chiaki-lib-macos`
- Zero compiler warnings (`-Wall -Wextra`) in the TV build, the sim and the tests, and zero `mise run lint`
  findings. clang-tidy checks the code the macOS builds compile (the real session backend only when
  `out/chiaki-lib-macos` is staged); silence a finding only with `/* NOLINT(check): why */`
- Install with `scripts/tv.sh`, never `@webosose/ares-cli` (broken on modern Node). The SSH key's passphrase
  changes with each Developer Mode session: `scripts/tv.sh key <passphrase>`
- Only touch the TV when asked to. On it, the log must show `UI created`, `gamepad ... grabbed` (pad on) and
  `indev ready`, and `scripts/tv.sh running` must say running

## Testing

- `tests/host/run.sh` builds every module but `main.c` and the SDL display, renders offscreen, drives the real
  `gamepad.c` through a fake evdev FIFO, and asserts the whole UI (layout, focus rings, no overflow, every key
  path), consoles and discovery, the session, registration and the dialogs, with a screenshot of each step in
  `out/host-tests/shots/`. Its config is wiped per run and it seeds its own consoles. Run it after any change; add
  an assertion for new behaviour
- The host tests and the simulator do not replace the TV: rendering speed, the controller, discovery on a real
  network and webOS itself are proven on the device ([docs/TV-CHECKS.md](docs/TV-CHECKS.md))

## Gotchas

- **OpenSSL and libchiaki are linked statically.** The TV ships OpenSSL 1.0.2, the SDK 1.1: a dynamic `libssl`
  fails to load and the app is relaunched in a loop. Do not define `CHIAKI_LIB_ENABLE_MBEDTLS` (it changes
  `ecdh.h`). Our headers use `CHIAKI_WEBOS_*_H` guards: libchiaki's use `CHIAKI_LOG_H`, `CHIAKI_SESSION_H`, ...
- **The controller map is webOS's, not hid-sony's**: buttons in HID order from 0x130 (Cross 0x131, L1 0x134, R1
  0x135, Options 0x139), right stick `ABS_Z`/`ABS_RZ`, triggers `ABS_RX`/`ABS_RY`
  ([docs/ds4-map-observed.txt](docs/ds4-map-observed.txt)). Tables are per device name; the DualSense uses the same
  order but has no rumble and no touchpad touches on webOS
  ([docs/dualsense-map-observed.txt](docs/dualsense-map-observed.txt))
- **Text entry**: numbers are digit boxes and a number pad, free text a field and our own keyboard; a USB keyboard
  (`SDL_TEXTINPUT` -> `keypad_push_text`) and the remote's digit keys type straight in, as code points through the
  key sink. Digit key events type at once and their text echo is dropped (`input_remote_key()`)
- **Never block libchiaki's receive thread**: it calls the video and audio callbacks; a stall overflows the stream
  socket (FEC failures, IDR waits). Media runs on `media/feeder.c` threads, and CPU on the main thread while
  streaming (LVGL) takes time from them: LVGL's refresh is paused while only the game shows
- **NDL draws below the app surface**: while streaming `display_set_video()` clears it transparent and turns on
  `screen_transp` (without it LVGL writes alpha 0xFF and covers the video). Never tone-map the UI for HDR: the TV
  maps its SDR plane itself. `NDL_DirectVideoSetHDRInfo` takes 15 words (the SDK header says 12) for the VUI codes
- **Focus driven, no pointer**: one invisible key sink (`ui.c`, `INPUT_FLAG_ARROWS`) takes every key and hands it to
  what is in front: a dialog, Add a console, the stream menu while streaming, else Home. Keys act on the press, so
  the sink calls `lv_indev_wait_release()`. No pointer indev exists: a Magic Remote or a mouse does nothing
- **Back**: Circle and the remote's Back step out one level (`ui_back()`); at Home's top level it returns false and
  the remote's Back exits. Share + Options is the stream menu, also while streaming; those two reach the console
  `PAD_CHORD_MS` late so the chord never does
- **Clipping and stale edges**: containers must not clip (`ctl_obj()` sets `OVERFLOW_VISIBLE`); a border shrinks the
  content box. LVGL repaints only a container's area plus its ext draw size when it hides, fades or moves, so
  sliding containers get `ctl_margin()` or a focus ring's edge stays on screen (only the sim and the TV show it).
  Decorations sticking out of a tile are its siblings. `lv_textarea_set_one_line()` resizes: size after it
- **Fades**: LVGL 8.3 asks only an object's own opacity whether it covers an area, so under a fading parent an
  opaque child made refreshes skip what is behind it and the stale buffer showed through. The theme's cover check
  (`ui/theme.c`) accounts for fading parents; objects not styled by it (images, canvases) must not be opaque covers
  inside a fade. A full-screen fade or backdrop tint repaints the whole screen every frame: costly on the TV
- **Overlays** (`register.c`, `dialog.c`) live on `lv_layer_top()` and take keys from the sink (add them to
  `sink_key_cb()` and `ui_back()`, front to back). A full-screen one calls `ui_cover()` once it has faded in. A
  dialog runs its callback after it is gone, through `lv_async_call()`
- **Device art** comes from `design/menu/art/`, scaled by `scripts/make-device-art.sh` into `assets/devices/`; the
  ipk installs it next to the binary. The app icon is a checked-in PNG with the glyph only: the launcher draws the
  tile in `appinfo.json` `iconColor`
- **Logs**: no journald or stdout on the TV, only `/tmp/chiaki.log` (truncated per launch). Probe mode
  (`log_probe()`) is for what only the TV can answer; keep it off the hot paths unless throttled
- **Config**: `$CHIAKI_CONFIG_DIR/chiaki.conf`, else `$HOME/conf/chiaki.conf` on the TV (the app's directory), else
  `/tmp/chiaki-webos`. Mode 0600: it holds pairing keys and PSN tokens. Whether it survives an update: `TODO(path)`.
  Rename a key when its meaning changes (unknown keys are ignored): an old saved value would apply otherwise
- **Licenses**: the `.ipk` ships `LICENSE`, `THIRD_PARTY_NOTICES.md` and every bundled component's license text;
  a new statically linked dependency needs its license in `stage_licenses` (`scripts/chiaki-lib-build-webos.sh`)
  or `LICENSES/`, and a row in the notices

## Code Style

- C11, `snake_case`, formatted by `.clang-format` (4 spaces, K&R braces, 120 columns): run `mise run format`,
  never hand-align. Tables whose layout carries meaning (key maps, scripts) sit between `/* clang-format off */`
  and `/* clang-format on */`
- Every file starts with a `/* path - purpose */` comment, and headers document their module's contract
- UI: tokens from `ui/theme.h` (`COL_*`, `SP_*`, `R_*`, `FONT_*`), a screen's geometry as named `#define`s at the
  top of its file, no bare magic numbers; everything fits 1920x1080 inside the title-safe margins
- Every interaction works with the controller (D-pad or left stick, Cross, Circle, L1/R1) and the TV remote
  (arrows, OK, Back)
- Logging: lifecycle -> `log_msg()`; user-visible events -> `log_event(LOG_*, "TAG", ...)`; hardware detail ->
  `log_probe()`
- No em dashes in code, comments or docs
- Commits: conventional prefixes (`feat:`, `fix:`, `enhance:`, `chore:`, `docs:`, `build:`), lowercase, no period

## CI/CD

- `ci.yml` (push to `main`, PRs): lint (the `mise.toml` tools), host tests and the simulator on macOS, and the
  TV build (`actions/build-ipk`)
- `release.yml` (dispatched with a tag and notes, see [RELEASE_PROCESS.md](RELEASE_PROCESS.md)): tags, builds the
  `.ipk` on a macOS arm64 runner and publishes it with its checksum
- `prerelease.yml` / `slash-command.yml`: `/prerelease` on a PR builds and publishes a test `.ipk`
- Run `mise run lint:workflows` (actionlint) after editing workflows
