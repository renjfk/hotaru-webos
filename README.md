# chiaki-webos

A port of [chiaki-ng](https://github.com/streetpea/chiaki-ng) (PlayStation Remote
Play) to LG **webOS** TVs - a native app.

## Layout

- `src/` - the app (SDL2 + LVGL UI, evdev gamepad input, later the chiaki session).
- `chiaki-ng/` - vendored upstream (git submodule); only `lib/` is used.
- `scripts/` - cross-build reference (dependency build recipe).
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
gamepad hotplug). The chiaki protocol library and the NDL media path are the
next milestones - see the project notes.

## Notes

- Designed for the LG 50UP75003LF (webOS TV Lite 6.5.3, 1080p).
- Link OpenSSL statically (the TV ships 1.0.2, the SDK provides 1.1).
- DS4 button/axis map is non-standard; see `~/webos-probe/docs/ds4-map-observed.txt`.