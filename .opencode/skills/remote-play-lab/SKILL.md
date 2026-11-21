---
name: Remote Play lab
description: Drive a real PS5 or PS4 headlessly from this Mac through a libchiaki session with a command socket, Lua scripting and screen reading (rplab/), to test what the Remote Play protocol and the console actually do without the TV. Use when asked to probe or prove protocol behaviour, check how a console reacts to input or session setup, reproduce a session problem against a real console, or script the console's UI.
---

# Remote Play lab

`rplab/` holds one Remote Play session to a real console and takes commands, so an agent can try
an idea in seconds: send input or protocol messages, watch the console's replies, read its screen.
The TV is not involved, and nothing in it reaches the app's builds.

- `rplab/rpd.c`: the daemon. libchiaki session, H.264 decode for screenshots, Unix socket, LuaJIT.
- `rplab/rp`: the client, one command per call.
- `rplab/chiaki-probe.patch`: experiment hooks for libchiaki (`chiaki/probe.h`). `rplab/build.sh`
  applies it to a copy of chiaki-ng in `out/rplab/chiaki-ng` and builds that into its own staging,
  `out/rplab/chiaki-lib`. The submodule, `out/chiaki-lib-macos` and the sim/TV builds never see it.
- `rplab/prelude.lua`: the Lua helpers, read again on `reload`. `rplab/scripts/`: saved experiments.
- `out/rplab/`: runtime state. Holds the socket, `rpd-notable.log`, `rpd-full.log`, `shots/*.png`
  and `config/`.

Findings so far: [docs/multi-controller-observed.txt](../../../docs/multi-controller-observed.txt).

## Principles

- **A real console, nothing mocked.** A result is what the console did, so state it with the
  console's own reply or screen.
- **Every experiment needs a positive control in the same session.** Show the pad 0 equivalent
  working before trusting "nothing happened". Also check the screen is the one the test assumes: a
  run once measured the PS5's Welcome page while believing it was in the Control Center.
- **Keep the session up.** The console refuses a new session for ~10 s after one ends. The daemon
  retries for 60 s, but restarts cost time, so iterate in Lua and `reload` instead of restarting.
  Rebuild and restart only for C changes.
- **The TV is off limits unless asked.** `scripts/tv.sh config` only reads the app's config file.
- **The config holds pairing keys and PSN tokens.** Never print it, commit it or paste it.
- **When the experiment is done, leave the console as the user wants.** `rest` puts it in rest mode.

## Start

```sh
scripts/tv.sh config                 # once: the TV app's consoles -> out/rplab/config (else the sim's config is used)
rplab/run.sh                         # builds (first time ~45 s, then <1 s) and runs: start it in the background
rplab/rp lua 'print(rp.wait_connected())'
rplab/rp help
```

- `--host N` picks the config's Nth console, which must have a LAN address (a PIN pairing).
- `--res 1080` asks for 1080p (default 720p; screenshots are 1280 wide either way).
- `--no-connect` waits for `connect`.
- Stop with `rplab/rp quit`. A console in rest mode needs `wake`, ~20 s, then `connect`.

## Commands

Each reply is `ok ...` or `err ...`, then the console's notable log lines from while the command
ran (plus 400 ms). Per-frame noise and heartbeats are filtered out of those lines.

| Command | What |
| --- | --- |
| `status` | session, video counters, the console's player index and LED, pad 0, probe settings |
| `connect`, `reconnect`, `disconnect`, `wake`, `rest`, `quit` | session control; `reconnect` applies `rp.setup` |
| `press <btn[,btn]> [ms]` | momentary press on pad 0 (`cross circle square triangle up down left right l1 r1 l2 r2 l3 r3 options share touchpad ps`) |
| `pad0 [idle] [buttons=..] [lx= ly= rx= ry= l2= r2=]` | hold pad 0 state; sticks are -32768..32767 |
| `states <1..4> [byte3=<v>\|auto]`, `slot <i> lx=.. \| raw=<hex> \| clear` | extra controller states in each feedback state packet |
| `cc [id=<n>\|none] [connected=0\|1] [type=ds4\|dualsense\|<n>]` | a ControllerConnection message |
| `hist <hex>` | one raw feedback history event (<= 5 bytes), numbered like pad 0's |
| `shot [name]` | the next decoded frame -> `out/rplab/shots/<name>.png` |
| `wait <ms>`, `log [n]` | collect the console's messages; the last n notable lines |
| `lua <code>`, `run <file> [args]`, `reload` | Lua; `rplab/rp lua - <<'EOF'` sends a multi-line chunk |

## Lua

One state lives as long as the daemon, so globals survive between runs. The native core:

- **Commands:** `rp.cmd(line)` runs any command above and returns its reply.
- **Time:** `rp.sleep(ms)` and `rp.now()`.
- **The log:** `rp.seq()` and `rp.lines(since)`.
- **The session:** `rp.info()` returns `{state, player_index, led, frames_in, frames_out, quit_reason, host}`.
- **The screen:** `rp.fresh()` waits for a new frame, and `rp.luma(x, y, w, h)` reads a region's
  brightness, both in 1280x720 coordinates.
- **Session setup:** `rp.setup{...}`, applied at the next `reconnect`.
- **Output:** `print`.

`rplab/prelude.lua` builds the rest on it:

- **Input:** `rp.press`, `rp.tap(btn, n)`, `rp.nav("down*2 right*3 cross")`, `rp.pad0{...}`,
  `rp.states`, `rp.slot`, `rp.cc{...}`, `rp.hist`.
- **Observation:** `rp.shot`, `rp.wait(ms)` and `rp.watch(ms, fn)` return log lines,
  `rp.find(pattern, since)`, `rp.grid()` and `rp.diff(a, b)` compare whole screens.
- **PS5 UI:** `rp.cc_open()`, `rp.cc_focus()` and `rp.cc_goto(name)` drive the Control Center.
- **Session:** `rp.wait_connected()`, `rp.reconnect()`, `rp.rest()`.

`rp.show_log = false` stops a script's reply from ending in its log lines.

`rp.setup{controller_type=, client_type=, con_path=, pad_proc_no=, ctrl={"RP-X: v"}, init={...},
controllers={"ds4",...}, game_spec=',"k":v', start_cc={1,2}, start_cc_type="ds4", id0=true}`
replaces the whole setup; `rp.setup()` resets it.

Save an experiment you may repeat as `rplab/scripts/<name>.lua` (`rplab/rp run <name> [args]`;
arguments arrive as `...`). Follow `setup_matrix.lua`: a table of variants, one line of verdict
each, the control in the same line.

## Reading the screen

Don't read screenshots in the main context. They are expensive, and most questions are numbers:

1. **Measure first.**
   - `rp.grid()` before and after an action, then `rp.diff(a, b)`, tells whether anything changed
     (Control Center opening: ~130 cells of 144; animation noise: under 20).
   - `rp.cc_focus()` reads the focused Control Center icon from its label, and returns nil when the
     row is not on screen.
   - `rp.luma` is for anything else with a fixed position.
2. **Look through a cheap subagent when a picture is needed.**
   - Start one `explore` session once, telling it what the console is and the exact report format
     (screen, focused element, text verbatim, controllers listed, anything unusual).
   - Send it PNG paths in batches, reusing its `sessionID`, so it keeps the context.
   - Ask for differences from a named earlier frame when that is the question.
   - Act on its report; don't open the image yourself.
3. **Take a shot only on a flagged change**, or at a step whose state you must confirm.

## PS5 UI notes

- Presses closer than ~250 ms are dropped: `rp.tap` waits 300 ms between them.
- PS toggles the Control Center: pressing it while the Control Center is open closes it.
  `rp.cc_open()` backs out with Circle first, then verifies.
- The Control Center's icon row (11 icons at y 675, x 267 + 74.6 * (i - 1) in 1280x720) wraps
  around and remembers its last focus, so count steps from `rp.cc_focus()`, never from an edge.
  Down twice from the cards row reaches it.
- Holding a stick moves focus every few frames: hold ~350 ms for one or two steps, or a hold can
  wrap the row back to where it started.
- The Accessories card (icon 9, `rplab/rp run accessories`) lists the controllers the console sees.

## libchiaki hooks

`chiaki/probe.h` covers:

- extra feedback states;
- ControllerConnection messages;
- raw history events;
- the session setup: the ctrl and session request headers, `connectedControllers`, extra launch
  spec members, and pads announced at stream start;
- readable `Probe ... response header` log lines.

To change them:

1. Edit `out/rplab/chiaki-ng/lib/`.
2. Run `rplab/build.sh`.
3. Restart the daemon.
4. Run `rplab/build.sh --save-patch` to write the change into `rplab/chiaki-probe.patch`.

Keep hooks inert unless rpd calls them, and mark them `HOTARU PROBE`. The copy is made again from
the submodule when its commit or the patch changes, which would drop unsaved edits.

## Gotchas

- `reconnect` right after a session ends meets "Remote Play is already in use" for a few tries.
  That's expected; the daemon retries.
- A malformed `hist` event poisons the next 8 history packets, so pad 0 looks dead. Push about 10
  valid releases (`hist 808300`) to flush it.
- `RP-ClientType 14` makes a watch-only session that ignores all input. `RP-ControllerType 1`
  breaks the stream. `RP-SupportCmd` on the session request (`init`) is refused.
- The notable log drops per-frame lines and heartbeats; `out/rplab/rpd-full.log` has everything.
