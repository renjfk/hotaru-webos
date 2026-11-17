# TV checks

What the host tests and the simulator cannot prove, checked on a TV before a release. Each check says what to do,
what the log shows, and what in the code depends on the answer.

## Running a session

```bash
./build.sh                       # with libchiaki staged: scripts/chiaki-lib-build-webos.sh
scripts/tv.sh key <passphrase>   # once per Developer Mode session (the passphrase is in its app)
scripts/tv.sh install
scripts/tv.sh launch --probe     # probe mode for this run
scripts/tv.sh log -f             # watch while checking
scripts/tv.sh save               # keep the log: out/tv-logs/<time>.log
```

Probe mode can also be switched on in the app (Settings > About > Diagnostics, kept across launches) or with the
flag file `/tmp/hotaru-probe`. It adds `probe:` lines to `/tmp/hotaru.log`:

| Prefix | What |
| --- | --- |
| `probe: sdl:` | every SDL event: keys (sym, name, scancode, modifiers), text, pointer, window |
| `probe: pad:` | every input node and why it was or was not taken; the grabbed pad's ids, keys, axis ranges and force feedback; every button and every eighth of an axis |
| `probe: discovery:` | every console that answers: kind, name, address, MAC, state, firmware, running app |
| `probe: net:` | the interfaces with their addresses and broadcast addresses |
| `probe: frame:` | every 5 s: frames drawn per second and how much of the screen, LVGL time (average, worst), loops per second, memory, CPU |

libchiaki's debug lines are included too. The log always starts with the TV's model and webOS version
(`tv: 50UP75003LF (LM21A_DVB), webOS TV Lite 6.5.3`, read from `/var/run/nyx/`), the app version, every
command-line argument, and whether SDL has text input and a screen keyboard: paste that head with a report and no
one has to ask which TV or webOS it was.

## The checks

### 1. Launch parameters

`scripts/tv.sh launch --probe` sends `{"probe":true}` and also touches the flag file.

- **Look for** `argument 1: {"probe":true}` near the top of the log.
- **It decides** whether launch parameters can be used for anything else (opening a console straight from a
  shortcut). The flag file keeps probe mode working either way (`src/main.c`).

### 2. The UI on the TV

Walk through Home (both rows, the quick cards, the options menu), every Settings panel, Add a console (both ways in,
the keyboard), and the dialogs.

- **Look for** `probe: frame:` while moving: LVGL's average should stay well under 33 ms (the 30 fps refresh), and
  CPU near 1% of a core when nothing moves. Note any step that judders, and any colour that looks off (the TV maps
  the UI plane itself).
- **It decides** whether animations need trimming on armv7 (`ui/motion.c`, `ctl_margin` areas), and whether the
  focus ring and glow sizes need adjusting.

### 3. The remote's number keys

On Add a console's address or PIN step, press each number key on the remote.

- **Look for** `sdl: key down sym 0x31 "1"` and/or `sdl: text "1"`; the digit must appear once.
- **It decides** `input_remote_key()`: digits are typed from the key event, and a text echo is dropped. If the
  remote sends scancodes such as `SDL_SCANCODE_WEBOS_1` instead, add them to `digit_of()` in `src/input/input.c`.
  Also note the scancodes of the colour keys and any other key the remote has.

### 4. Magic Remote (if the TV has one)

Move the pointer, then press OK with the pointer showing.

- **Look for** `sdl: pointer at` and `sdl: pointer button`, or `sdl: key down` for OK.
- **It decides** whether OK with the pointer showing needs mapping to Cross: the app registers no pointer, so a
  click would do nothing.

### 5. Console discovery

With a console on, in rest mode, and off (or unplugged from the network):

- **Look for** `probe: discovery: PS5 <name> at <address>, id <MAC>, ready|standby, firmware ...` and
  `discovery: <name> is ready|in rest mode|offline`. Home shows the state under the console's name.
- **Then** Start Remote Play on a console in rest mode: the Connecting screen says "Waking up", and the session
  starts once it answers ready (note how long: the app waits 25 s and resends every second, like chiaki-ng). A
  console that is off and has a PSN route goes over PSN at once.
- **It decides** the timings in `src/app.c` (`DISC_MISSING_MS`, `WAKE_TIMEOUT_MS`).

### 6. PIN pairing

Add a console with its PIN (a PS5, and a PS4 if there is one); once with the account ID typed, once after signing in
with the phone.

- **Look for** `ui: [REG]` lines ending in success, and the console on Home. A refused PIN shows the reason on the
  PIN step.
- **It decides** nothing new in code: this is the first run of `chiaki_regist_start` against a console.

### 7. DualShock 4

Turn the controller off and on again while the app runs; press every button once.

- **Look for** `gamepad ... lost`, `ui: [INPUT] gamepad offline`, then `gamepad reconnected`; every `pad: key` line
  names the standard Linux code and what Hotaru read from it (`0x134 down BTN_WEST, Hotaru reads L1`), and the trigger
  clicks (0x136/0x137) read nothing.

### 8. DualSense

Done on the 50UP75003LF: [dualsense-map-observed.txt](dualsense-map-observed.txt). Repeat on another TV or webOS
version: pair a DualSense with the TV and launch with probe mode.

- **Look for** `pad: ... "DualSense Wireless Controller": a gamepad`, the `pad: keys` list and the `pad: abs` ranges,
  then press every control in the order the [user guide](user-guide.md#troubleshooting) gives. The `pad: key` lines
  carry the TV's code and name next to Hotaru's reading, so they say which layout the node reports.
- **It decides** which of two things to do. A DualSense button that lands wrong on a node whose keys are 0x130..0x13d
  is a table fix in `g_dualsense_buttons` (a DualShock 4 has its own, `g_ds4_buttons`), saved as
  `docs/dualsense-map-observed.txt`. A node with BTN_THUMBR (0x13e) but neither BTN_Z nor BTN_C, triggers on ABS_Z/RZ
  and the right stick on ABS_RX/RY is the standard Linux layout of a TV whose kernel driver (hid-playstation) owns
  the pad, and needs code rather than a table: save that log as `docs/dualsense-map-linux-observed.txt`. Note whether
  `pad: force feedback rumble` is listed: adaptive triggers, the lightbar and haptics need raw HID output, which
  evdev does not offer.

### 9. HDR10

On a PS5 with an HDR game, set the codec to HDR and stream.

- **Look for** the picture not being washed out (the TV must switch to HDR), and the menu over the game keeping its
  normal colours.

### 10. The menu over the game

Open it with Back and with Share + Options while a game streams.

- **Look for** the video playing above the sheet and through it, and the stream not stuttering while it is open
  (the `stats:` lines).

### 11. Login passcode

Connect to a console whose user has a passcode.

- **Look for** the passcode sheet over the Connecting screen; the session goes on once the 4 digits are in.

### 12. Updates keep the settings

Install a newer build over the installed one (same app id).

- **Look for** the consoles, the PSN account and the settings still there.
- **It decides** `TODO(path)` in `src/config/config.h`: if they are lost, the config needs a place outside the app
  directory.

### 13. The update notice

Launch a build older than the latest release: a local build is `appinfo.json`'s version, below every release. The
release must not be a prerelease: GitHub's `releases/latest` skips drafts and prereleases, so a repository with only
those answers like one with no release at all.

- **Look for** `ui: [UPDATE] Hotaru <version> is out` and, once the splash is gone, the notice over the top
  bar's right end for 9 s; Settings > About names the version too. `update: GitHub answered HTTP 404` means there is
  no release to see yet, not a certificate or network problem: the request reached GitHub and TLS verified.
  `update: GitHub not reached: <curl error>` is DNS, TLS or a timeout; the check tries 4 times, 15 s apart (a TV just
  switched on may not be on its network yet).
- **It decides** whether GitHub's API is reachable over the TV's CA bundle like PSN is (`src/net/update.c`).

### 14. Rest mode on disconnect

While a game streams, open the menu and focus **Disconnect**. Press R1 (or Down on the remote when no controller is
connected) for **End and sleep** and L1 (or Up) for **End the session**, then Cross once. Repeat with the other
action.

- **Look for** the stream to end and Home to come back; with End and sleep the console goes to rest mode, and with
  End the session it stays on. The card keeps the last choice after a restart.
- **It decides** whether `chiaki_session_goto_bed` reaches the console before `session_stop` tears the control channel
  down on webOS; the host tests only simulate the request.

### 15. The media library is found at runtime

Watch the log from launch, then start a stream. The app no longer links `libNDL_directmedia`; it opens
`libNDL_directmedia.so.1` at launch (Home's hero line asks for the answer) or, with no console to show, on the first
connect (a webOS 4 TV has only the v1 API there, and a direct call to a v2 entry point killed the app: issue #5).

- **Look for** `ndl: libNDL_directmedia.so.1 has the v2 API` once per launch (a near-miss name such as
  `libNDL_directmedia.so` would say `did not open`), then the usual `DirectMediaInit ok`. A TV without the v2 API
  says `has no NDL_DirectMediaLoad (the v1 API: webOS 4 and older)` and refuses the session with
  "This TV cannot play video".
- **It decides** the name in `src/media/ndl.c` (`NDL_LIBRARY`): it must be the SONAME the TV ships, not the
  unversioned dev symlink in the SDK sysroot.

## Recording the results

Update the table in [STATUS.md](STATUS.md), keep raw evidence next to it (like
[ds4-map-observed.txt](ds4-map-observed.txt)), and turn what was learned into code or a gotcha in
[AGENTS.md](../AGENTS.md).
