/* main.c - entry point: init the modules, wire them together, run the loop.
 *
 * chiaki-webos is a PlayStation Remote Play client (chiaki-ng port) for LG
 * webOS TVs. Module map: see AGENTS.md / docs/ARCHITECTURE.md.
 */
#include <SDL.h>
#include <lvgl.h>

#include "app.h"
#include "config/config.h"
#include "input/input.h"
#include "log.h"
#include "platform/display.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "ui/widgets.h"

static void poll_sdl(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) app_quit();
        else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) app_quit();
            else input_remote_key(k);
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    log_init(LOG_PATH);
    log_msg("chiaki starting (LVGL %d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();
    if (!display_init()) return 1;
    theme_init(display_get());
    config_load();
    app_init(ui_app_changed);
    ui_create();
    log_msg("UI created (%dx%d)", lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    const input_hooks_t hooks = {
        .tab_step = ui_tab_step,
        .quit = app_quit,
        .pad_changed = ui_set_pad_state,
        .scroll_fallback = ui_scroll_fallback,
    };
    input_init(ui_group(), ui_cursor_create(), &hooks);

    while (app_running()) {
        poll_sdl();
        input_tick();
        app_tick();
        lv_timer_handler();
        SDL_Delay(5);
    }
    log_msg("exiting");
    display_shutdown();
    log_close();
    return 0;
}
