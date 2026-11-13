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
flag file `/tmp/chiaki-probe`. It adds `probe:` lines to `/tmp/chiaki.log`:

| Prefix | What |
| --- | --- |
| `probe: sdl:` | every SDL event: keys (sym, name, scancode, modifiers), text, pointer, window |
| `probe: pad:` | every input node and why it was or was not taken; the grabbed pad's ids, keys, axis ranges and force feedback; every button and every eighth of an axis |
| `probe: discovery:` | every console that answers: kind, name, address, MAC, state, firmware, running app |
| `probe: net:` | the interfaces with their addresses and broadcast addresses |
| `probe: frame:` | every 5 s: frames drawn per second and how much of the screen, LVGL time (average, worst), loops per second, memory, CPU |

libchiaki's debug lines are included too. The log always starts with the version, every command-line argument,
and whether SDL has text input and a screen keyboard.

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
  names its button (none `unmapped` except 0x136/0x137, the trigger clicks).

### 8. DualSense

Done on the 50UP75003LF: [dualsense-map-observed.txt](dualsense-map-observed.txt). Repeat on another TV or webOS
version: pair a DualSense with the TV and launch with probe mode.

- **Look for** `pad: ... "DualSense Wireless Controller": a gamepad`, the `pad: keys` list and the `pad: abs` ranges,
  then press every button and move every stick and trigger.
- **It decides** `g_dualsense_buttons` in `src/input/gamepad.c`, which assumes the DualShock 4's order. Save the
  log as `docs/dualsense-map-observed.txt` next to the DualShock 4's, and fix the table if a button lands wrong.
  Note whether `pad: force feedback rumble` is listed: adaptive triggers, the lightbar and haptics need raw HID
  output, which evdev does not offer.

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

## Recording the results

Update the table in [STATUS.md](STATUS.md), keep raw evidence next to it (like
[ds4-map-observed.txt](ds4-map-observed.txt)), and turn what was learned into code or a gotcha in
[AGENTS.md](../AGENTS.md).
