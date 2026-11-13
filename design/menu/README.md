# Design studies

Three studies, built with the app's LVGL and theme tokens and recorded into a gallery: the main menu, "Add a
console" and the update notice (below). The first holds three alternative designs for the main menu and the menu over the stream, built with the app's
LVGL 8.3 and theme tokens, so what you see is what the TV would draw. **Home was picked and is now
the app's main menu** (`src/ui/home.c`, `home_settings.c`, `stream_menu.c`, with the building
blocks ported from `kit.c` to `src/ui/controls.c`, `motion.c` and `art.c`). This folder is kept as
the design study: the gallery still builds on its own, with mock consoles, settings and stats
(`kit.c`), and the original art lives here (the app ships scaled copies, see
`scripts/make-device-art.sh`).

```sh
design/menu/capture.sh        # record every concept -> out/menu-gallery/index.html, contact-sheet.png, art.png
design/menu/capture.sh orbit  # just one
design/menu/run.sh            # live in a window: 1-3 concept, arrows D-pad, Enter Cross,
                              # Backspace/Esc Circle, M Share+Options, V start/stop the stand-in stream
```

The recorder renders at 1920x1080 on LVGL's 30 fps refresh clock. While "streaming" the UI is
drawn with `screen_transp` on a transparent screen and alpha-composed over a stand-in game frame,
like the NDL video plane on the TV. Each clip reports how much of the screen it repaints per frame
and, for the stream menus, how much of the video the menu covers.

## The concepts

All three drop the stick cursor: focus moves with the D-pad (or the stick as a D-pad), Cross
selects, Circle steps back, Share + Options opens the menu over the stream. Values change in place
with LEFT/RIGHT (segmented choices, a bitrate bar, toggles), so there are no dropdown pop-ups.

| | Home | Orbit | Rail |
| --- | --- | --- | --- |
| Idea | PS5 home screen | NXP smartwatch demo | TV launcher with a side rail |
| Consoles | Tile row, focused tile enlarged at the left edge, big Play pill, tinted backdrop | Spring carousel, focused card centred, others scaled back | Grid of big cards, detail sheet from the right |
| Settings | Consoles / Settings at the top, rows of big controls | Activities stacked vertically: ring gauges, round toggles | Rail pages with big rows and one gliding focus frame |
| Stream menu | Control Center sheet from the bottom (~38% of the video) | Floating dock + stats rings (~15%) | Side sheet over the left third (~34%) |
| Motion | Tiles glide, key art and tint crossfade | Springs everywhere, rings sweep, ambient satellites | Calm: lift on focus, rail unfolds |

## Add a console (`--study add`)

The second study: the pairing flow redone in Home's language, since the old overlay (cards, a
dropdown, a cursor for the keyboard) no longer fit. Three places for it to live share one flow
(`addkit.[ch]`: the steps, a mock pairing and the controls). **Setup was picked, with the
dialogs**: it is the app's Add a console (`src/ui/register.c`, which adds a PSN account step the
mock does not have) and its dialogs (`src/ui/dialog.c`), the controls ported to
`src/ui/controls.c`.

| | Setup | Inline | Sheet |
| --- | --- | --- | --- |
| Where | the whole screen, like the PS5's own setup | Home's hero, under the tile row | an 800 px panel over Home |
| Layout | text left, picture or pad right | the same, the pad where the key art stands | stacked: text, then picture or pad |
| Progress | a line along the top | the caption beside the Add tile | pills on the sheet |
| Done | back to Home | the console slides into the row | the sheet goes, Home says what was added |

Shared by all three: the console kind as tiles, the address as four octets that start on this TV's
network, eight PIN boxes, a number pad (the remote's number keys would type too), the PSN sign-in as
a QR card with the code typed on the restyled keyboard, a progress ring while pairing, the consoles
found with a PIN-paired one grouped. `04 Dialogs` restyles the Forget confirmation and the login
passcode (four boxes and the pad, in a sheet over the Connecting screen).

```sh
design/menu/capture.sh --study add   # -> out/add-gallery/index.html, contact-sheet.png
design/menu/run.sh --study add       # live: 1-4 concept, arrows, Enter, Esc
```

Mock rules: a PIN starting with 0000 is refused; any PSN code finds two consoles.

## Update notice (`--study update`)

Ways to say that a newer release is out, each over Home as the app shows it once the splash is gone (`u_update.c`,
on top of the Home concept). The check behind them is the app's (`src/net/update.c`): once per launch, GitHub's
latest release against the build. **`07 Compact toast, top right` was picked** and is the app's notice
(`src/ui/home.c`).

| | Where | Says | Stays |
| --- | --- | --- | --- |
| 01 Toast | bottom left, like the app's other toasts | the version and the address, one line | 9 s |
| 02 Notification card | top right, under the top bar | the version, a QR code of the release | 15 s |
| 03 Top bar chip | in the top bar, beside the pad | the version | the whole run |
| 04 Card over Home | centred, over a scrim | what is new, a QR code; Later, Skip this version | until a choice |
| 05 Toast until you move | as 01 | as 01 | until a key 4 s after it appeared |
| 06 QR toast until you move | bottom right, over the key art | the version, a QR code | as 05 |
| 07 Compact toast, top right | over the top bar's right end | the app mark, the version, the address | 9 s |
| 08 Compact toast, bottom left | as 01, level with the hints | as 07 | 9 s |

No QR code made it: an update goes on from a computer (webOS Dev Manager or `scripts/tv.sh`), where the address is
easy to type and never changes. Only 04's card takes keys.

```sh
design/menu/capture.sh --study update   # -> out/update-gallery/index.html, contact-sheet.png
design/menu/run.sh --study update       # live: 1-8 concept, arrows, Enter, Esc
```

## Device art

The consoles and controllers are `art/ps5.png`, `ps4.png`, `ds4.png` and `dualsense.png`:
original illustrations made for this project (provenance and floor contacts in
[art/README.md](art/README.md)). `art.c` downscales them once per displayed size with an area filter.
`HOTARU_GALLERY_PAD=dualsense` makes the DualSense the connected pad, `HOTARU_GALLERY_ART_DIR=<dir>`
loads the drawings from another folder.

## Files

- `art.[ch]`, `art/`: the device drawings and their loader
- `kit.[ch]`: mock data, device art helpers and the glow, PlayStation glyphs and hints, animation helpers (property tweens, spring path, rolling text, counting numbers, colour
  fades), segmented choice, level bar, toggle, gliding focus frame, the connecting interstitial
- `concept.h`: what a concept gives the runner (build, nav, set_stream, its recording script)
- `c_home.c`, `c_orbit.c`, `c_rail.c`: the main menu concepts, each with its stream menu
- `addkit.[ch]`, `a_setup.c`, `a_inline.c`, `a_sheet.c`, `a_dialogs.c`: the "Add a console" study
- `u_update.c`: the update notice study
- `main.c`: the runner (window, recorder or `--art sheet.png`; `--study` picks the study), the stand-in game
  frame and the HTML gallery
- `lv_conf.h`: the app's config plus `LV_FONT_MONTSERRAT_48`, the PNG decoder and a manual tick

## How they were wired

Home and Setup (with the dialogs) are in the app now. The concepts' building blocks moved to
`src/ui/controls.c`, `motion.c` and `art.c`, nav keys reach the screen in front through one key sink instead
of a pointer, the mock data became `app_*`/`config_*`, and the art is pre-scaled at build time
(`scripts/make-device-art.sh`). The README's still of the menu over the game lays the app's render on this
gallery's stand-in game (`design/menu/capture.sh`, then `scripts/make-screenshots.sh`).
