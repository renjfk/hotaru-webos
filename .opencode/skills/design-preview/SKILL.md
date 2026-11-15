---
name: Design preview
description: Render alternative layouts of Hotaru's UI from the real LVGL screens at 1920x1080 and publish them as an HTML page, so a design can be chosen before it is built. Use when asked to compare designs, preview a UI change, or produce screenshots of a candidate screen.
---

# Design preview

Choose a UI design by looking at it on the real renderer. A page of real frames
settles questions that prose cannot: what fits in the title-safe area, what a
chip or a row looks like at 3 m, whether a row set overflows.

## Principles

- Every frame is a real render of the app's own screens (`src/ui/*`) offscreen at 1920x1080 through
  `tests/host`, not a mock, not a browser mock-up. The host renderer runs the same LVGL 8.3 and theme
  tokens the TV does.
- Previews are throwaway. Code that exists only to show a candidate goes behind `#ifdef HOTARU_PREVIEW`
  and is deleted once the design is picked. The shipping build never compiles it.
- Stand-in values (a live bitrate, a battery level) must be named as stand-ins on the page.
- Never make a candidate layout work by changing behaviour that the user has not chosen yet. If a
  variant needs a different focus model, say so as its cost instead.

## Get a frame: two routes

### 1. The state is reachable in the app

Add a temporary `shot("name")` to `tests/host/harness.c` at the point the UI is in that state
(`shot_ex("name", true)` for an overlay over the running stream: the UI keeps per-pixel alpha). Run
`tests/host/run.sh`, then use `out/host-tests/shots/name.ppm` (and `name.pam` for overlays). Delete
the shot afterwards; the harness already asserts everything it needs.

Reach a screen through the real keys: `home_section_step(1)` is the L1/R1 switch, `home_key(LV_KEY_*)`
the sink's entry, `input_remote_key(SDLK_*, KMOD_NONE)` a remote key, `pad_btn(0x131)` Cross.

### 2. The state does not exist yet (candidate layouts)

Add a preview hook to the screen module and a tiny program that drives it:

```c
#ifdef HOTARU_PREVIEW
/* Candidate layouts, driven by a variant name. A HOTARU_PREVIEW build only. */
void stream_menu_preview(const char *v) { ... }
#endif
```

`tests/host/preview.c` is the program: the harness's offscreen driver, no assertions, one shot.

```c
static uint32_t *fb;
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * W + a->x1], &px[(y - a->y1) * w], (size_t)w * 4);
    lv_disp_flush_ready(drv);
}
static void pump(int ms) { /* app_tick(); lv_timer_handler(); SDL_Delay(5); until ms */ }
static void shot(const char *name); /* lv_refr_now(), then a P6 PPM of fb to SHOTS_DIR */

int main(int argc, char **argv) {
    log_init("/dev/stdout");
    SDL_Init(SDL_INIT_TIMER);
    lv_init();
    /* draw buffer + lv_disp_drv_register exactly as tests/host/harness.c does */
    setenv("HOTARU_CONFIG_DIR", CONFIG_DIR, 1); /* the target's own dir, wiped at start */
    theme_init(disp);
    config_load();
    /* seed a console so screens have something to show, as the harness does */
    app_init(ui_app_changed);
    ui_create();
    pump(700);
    /* navigate to the screen, e.g. Settings > Stream: */
    home_section_step(1);
    home_key(LV_KEY_DOWN);
    pump(500);
    stream_menu_preview(getenv("HOTARU_PREVIEW") ? getenv("HOTARU_PREVIEW") : "");
    pump(500);
    shot(argv[1] ? argv[1] : "preview");
}
```

The CMake target, next to `host-tests` in `tests/host/CMakeLists.txt`:

```cmake
add_executable(host-preview preview.c ${SRC})
target_compile_definitions(host-preview PRIVATE INPUT_DIR="${CMAKE_BINARY_DIR}/devinput"
                                               SYS_INPUT_DIR="${CMAKE_BINARY_DIR}/sys/class/input"
                                               SHOTS_DIR="${CMAKE_BINARY_DIR}/shots"
                                               CONFIG_DIR="${CMAKE_BINARY_DIR}/preview-config"
                                               HOTARU_ASSETS_DIR="${APP}/assets"
                                               HOTARU_VERSION="${HOTARU_VERSION}"
                                               HOTARU_PREVIEW)
target_include_directories(host-preview PRIVATE ${CMAKE_SOURCE_DIR}/shim ${APP}/src ${SDL2_INCLUDE_DIRS})
target_compile_options(host-preview PRIVATE -Wall -Wextra -Wno-unused-function)
target_link_libraries(host-preview PRIVATE lvgl ${SDL2_LINK_LIBRARIES})
```

One variant per process, so each starts from a clean config:

```sh
cmake -S tests/host -B out/host-tests -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build out/host-tests --target host-preview -j"$(sysctl -n hw.ncpu)"
HOTARU_PREVIEW=a out/host-tests/host-preview stream-live-a
```

## Compose the frames

Home, dialogs and the splash are opaque: the PPM is the picture.

The stream menu is drawn with per-pixel alpha over a separate video plane. Take its `.pam` and lay it
over the design study's stand-in game, the same frame the README stills use:

```sh
ffmpeg -v error -ss 3 -i out/menu-gallery/01-home/stream.mp4 -frames:v 1 game.png
magick game.png out/host-tests/shots/stream-menu.pam -composite menu.png
magick menu.png -resize 1440x810 menu-small.png
magick menu.png -crop 1920x210+0+670 +repage menu-header.png   # the detail, at native resolution
```

Needs ImageMagick 7 and ffmpeg (`brew install imagemagick ffmpeg`); the design gallery
(`design/menu/capture.sh`) supplies the game still when `out/menu-gallery` is not there yet.

## The page

Write `out/<topic>/index.html` (gitignored): the app's dark palette, the intro naming the constraint
that drives the options (title-safe height, panel width, what exists today), then one block per option:

- title and a tag (`recommended`, `cheapest`, `most code`);
- what changes, in one short paragraph;
- the full frame, then a native-resolution crop of the part that matters;
- a facts list: how it lays out, what is good, the trade-off.

End with a side-by-side table and a note on the behaviour every option shares (update cadence,
sampling window, what the number counts). Open it with `open out/<topic>/index.html`.

## Clean up

1. Delete the preview hook, `tests/host/preview.c` and the `host-preview` target; revert any temporary
   `shot()` in the harness.
2. Implement the chosen design for real, with a host-test assertion for its behaviour.
3. `mise run format`, `mise run lint`, `tests/host/run.sh`.
4. `out/` is gitignored: the page can stay for reference, the code must not.

## Gotchas

- clang-tidy checks the macOS builds, so it does not see `HOTARU_PREVIEW` code, but `mise run lint:format`
  does. Format everything.
- `getenv()` before `setenv(HOTARU_CONFIG_DIR, ...)`, or copy it: clang-analyzer flags the invalidated pointer.
- `shot_ex(name, true)` composites over a checkerboard stand-in so alpha is visible; the page should use
  the game still instead. Keep the `.pam` for that.
- The host test run wipes `CONFIG_DIR` and seeds its own consoles; a preview program that seeds config
  must do the same or it will show the developer's own hosts.
- Anything animated settles after a `pump`: 500-900 ms covers Home's entrance, the sheet's slide and the
  focus springs. A popped-up `ctl_menu` needs its own `pump` after opening.
- Check the title-safe margins, not just 1920x1080: a sixth 92 px settings row ends at y 1070, outside
  the 54 px margin, which is why presets had to swap rows instead of adding one.
