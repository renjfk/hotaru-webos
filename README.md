# Chiaki webOS

A port of [chiaki-ng](https://github.com/streetpea/chiaki-ng) (PlayStation Remote
Play) to LG **webOS** TVs - a native app. (Repo / directory name: `chiaki-webos`.)

## Layout

- `src/` - the app, one module per folder (display, input, ui, app state, and stubs
  for config, the chiaki session and NDL media); module map in [`AGENTS.md`](AGENTS.md).
- `chiaki-ng/` - vendored upstream (git submodule); only `lib/` is used.
- `docs/` - [`STATUS.md`](docs/STATUS.md) (what works and how it was proven) plus raw
  hardware evidence (DS4 map, network probe).
- `tests/host/` - macOS behaviour tests (`tests/host/run.sh`).
- `sim/` - macOS LG-TV simulator: the real app in a window (`sim/run.sh`).
- `scripts/` - cross-build reference (dependency build recipe).
- `assets/` - the app icon PNGs (see below).
- `build.sh` - cross-compile for webOS and package an `.ipk`.

## Build

Requires the webOS Homebrew native toolchain and `ares-cli-rs`:

```sh
# one-time (macOS ARM64)
mkdir -p ~/webos-sdk && cd ~/webos-sdk
gh release download webos-d7ed7ee.6 -R webosbrew/native-toolchain \
  -p 'arm-webos-linux-gnueabi_sdk-buildroot_darwin-arm64.tar.bz2'
tar xjf arm-webos-linux-gnueabi_sdk-buildroot_darwin-arm64.tar.bz2
(cd arm-webos-linux-gnueabi_sdk-buildroot && ./relocate-sdk.sh)
mkdir -p bin && cd bin
gh release download v0.7.0 -R webosbrew/ares-cli-rs -p 'ares-cli-rs-v0.7.0-macos-aarch64.tar.gz'
tar xzf ares-cli-rs-v0.7.0-macos-aarch64.tar.gz

./build.sh   # -> dist/org.homebrew.chiakiwebos_<ver>_arm.ipk
```

## App icon

A white Play triangle sending two Remote Play signal waves, on a bright
PlayStation-blue tile. Checked in as three PNGs under `assets/`:

- `assets/icon.png` (80x80) - launcher icon (`appinfo.json` `icon`)
- `assets/largeIcon.png` (130x130) - store listing (`largeIcon`)
- `assets/icon-400.png` (400x400) - preview / store artwork (not packaged)

`CMakeLists.txt` installs the first two to the app root inside the ipk. The icon is
final, so there is no generator script; the play-waves concept came from the icon
gallery, which is in git history.

## Simulator (macOS)

`sim/run.sh` builds the real app (including `main.c` and the SDL display) natively
and runs it in a resizable window, so UI and input work can be iterated in seconds
without touching the TV. The LVGL display stays 1920x1080 and the window is scaled,
so the layout is identical to the device.

- **Mouse** = the cursor (right stick path): move to hover, click to press.
- **Keyboard** = the TV remote: arrows are the D-pad, Enter selects, Backspace/Esc
  is back/quit.
- `CHIAKI_NO_SPLASH=1 sim/run.sh` skips the startup overlay while iterating.

```sh
brew install sdl2     # one-time
sim/run.sh            # -> window; logs to /tmp/chiaki.log
```

The simulator does not replace the device: rendering speed, the real DualShock and
webOS itself are only proven on the TV.

## Install / run

Uses the `webos-install.sh` from the `webos-tv-sideload` skill (Developer Mode +
key-server SSH, no ares-cli):

```sh
SKILL=~/dietpi/.opencode/skills/webos-tv-sideload
"$SKILL/webos-install.sh" install dist/org.homebrew.chiakiwebos_*_arm.ipk org.homebrew.chiakiwebos
"$SKILL/webos-install.sh" launch  org.homebrew.chiakiwebos
ssh -i ~/.ssh/webos_rsa -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  -p 9922 prisoner@LGwebOSTV.local 'cat /tmp/chiaki.log'
```

## Status

UI shell + input are in place (LVGL theme, stick-cursor + remote + D-pad nav,
gamepad hotplug, startup splash). The NDL media path and the chiaki protocol
library are the next milestones - see [`docs/STATUS.md`](docs/STATUS.md).

## Notes

- Designed for the LG 50UP75003LF (webOS TV Lite 6.5.3, 1080p).
- Link OpenSSL statically (the TV ships 1.0.2, the SDK provides 1.1).
- DS4 button/axis map is non-standard; see `~/webos-probe/docs/ds4-map-observed.txt`.