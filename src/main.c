/* main.c - entry point: init the modules, wire them together, run the loop.
 *
 * Chiaki webOS is a PlayStation Remote Play client (chiaki-ng port) for LG
 * webOS TVs. Module map: see AGENTS.md / docs/STATUS.md.
 */
#include <SDL.h>
#include <lvgl.h>
#include <stdlib.h>

#include "app.h"
#include "config/config.h"
#include "input/input.h"
#include "log.h"
#include "platform/display.h"
#include "ui/splash.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#ifdef CHIAKI_SIM
static bool g_mouse_down;

/* window pixels -> 1920x1080 LVGL space, into the same path the stick drives */
static void sim_mouse(int wx, int wy) {
    int ww = 0, wh = 0;
    if (!display_window_size(&ww, &wh)) return;
    input_pointer_set((float)wx * lv_disp_get_hor_res(NULL) / (float)ww,
                      (float)wy * lv_disp_get_ver_res(NULL) / (float)wh, g_mouse_down);
}
#endif

static void poll_sdl(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) app_quit();
        else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) app_quit();
            else input_remote_key(k);
        }
#ifdef CHIAKI_SIM
        else if (e.type == SDL_MOUSEMOTION) sim_mouse(e.motion.x, e.motion.y);
        else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            g_mouse_down = true;
            sim_mouse(e.button.x, e.button.y);
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            g_mouse_down = false;
            sim_mouse(e.button.x, e.button.y);
        } else if (e.type == SDL_MOUSEWHEEL) input_pointer_scroll(e.wheel.preciseY);
#endif
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    log_init(LOG_PATH);
    log_msg("Chiaki webOS starting (LVGL %d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

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

    if (!getenv("CHIAKI_NO_SPLASH")) splash_show();

    while (app_running()) {
        poll_sdl();
        input_tick();
#ifdef CHIAKI_SIM
        input_sim_tick();
#endif
        app_tick();
        lv_timer_handler();
        SDL_Delay(5);
    }
    log_msg("exiting");
    display_shutdown();
    log_close();
    return 0;
}