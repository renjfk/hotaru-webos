/* main.c - entry point: init the modules, wire them together, run the loop.
 *
 * Chiaki webOS is a PlayStation Remote Play client (chiaki-ng port) for LG
 * webOS TVs. Module map: see AGENTS.md.
 *
 * Probe mode (log.h) is forced on for the run by CHIAKI_PROBE=1, by the flag file
 * PROBE_FLAG (touch it over SSH before launching), or by a launch parameter
 * {"probe": true} (webOS hands a native app its parameters on the command line; every
 * argument is logged at startup to confirm how). In probe mode the loop also logs every
 * SDL event and, every PROBE_FRAME_MS, how many frames were drawn and what they cost.
 */
#include <SDL.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CHIAKI_SIM
#include <SDL_webOS.h> /* SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, SDL_WEBOS_SCANCODE_BACK */
#endif

#include "app.h"
#include "config/config.h"
#include "input/input.h"
#include "log.h"
#include "platform/display.h"
#include "ui/splash.h"
#include "ui/theme.h"
#include "ui/ui.h"

#define PROBE_FLAG     "/tmp/chiaki-probe"
#define PROBE_FRAME_MS 5000

static bool probe_requested(int argc, char **argv) {
    const char *env = getenv("CHIAKI_PROBE");
    if (env && *env && strcmp(env, "0") != 0) return true;
    if (access(PROBE_FLAG, F_OK) == 0) return true;
    for (int i = 1; i < argc; i++) {
        const char *p = strstr(argv[i], "\"probe\"");
        if (!strcmp(argv[i], "--probe") || (p && strstr(p, "true"))) return true;
    }
    return false;
}

/* ---------- probe mode: SDL events and frame timing ---------- */

static void probe_sdl_event(const SDL_Event *e) {
    static Uint32 motion_next;
    if (!log_probe_on()) return;
    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            log_probe("sdl: key %s sym 0x%x \"%s\" scancode %d mod 0x%x%s", e->type == SDL_KEYDOWN ? "down" : "up",
                      (unsigned)e->key.keysym.sym, SDL_GetKeyName(e->key.keysym.sym), (int)e->key.keysym.scancode,
                      (unsigned)e->key.keysym.mod, e->key.repeat ? " repeat" : "");
            break;
        case SDL_TEXTINPUT: log_probe("sdl: text \"%s\"", e->text.text); break;
        case SDL_TEXTEDITING: log_probe("sdl: editing \"%s\"", e->edit.text); break;
        case SDL_MOUSEMOTION:
            if ((Sint32)(SDL_GetTicks() - motion_next) < 0) break; /* a pointer streams these */
            motion_next = SDL_GetTicks() + 250;
            log_probe("sdl: pointer at %d,%d", e->motion.x, e->motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            log_probe("sdl: pointer button %d %s at %d,%d", e->button.button,
                      e->type == SDL_MOUSEBUTTONDOWN ? "down" : "up", e->button.x, e->button.y);
            break;
        case SDL_MOUSEWHEEL: log_probe("sdl: wheel %d,%d", e->wheel.x, e->wheel.y); break;
        case SDL_WINDOWEVENT: log_probe("sdl: window event %d", e->window.event); break;
        default: log_probe("sdl: event 0x%x", e->type); break;
    }
}

/* resident memory and CPU time from /proc (the TV); -1 where there is none (macOS) */
static long proc_rss_kb(void) {
    long pages = -1, rss = -1;
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = -1;
    fclose(f);
    return rss < 0 ? -1 : rss * (sysconf(_SC_PAGESIZE) / 1024);
}

static long proc_cpu_ticks(void) {
    char buf[1024];
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *p = strrchr(buf, ')'); /* the command name may hold spaces */
    unsigned long ut = 0, st = 0;
    if (!p || sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) != 2) return -1;
    return (long)(ut + st);
}

static void probe_frame(double handler_ms) {
    static struct {
        Uint32 since;
        unsigned loops;
        double sum, max;
        long cpu;
    } s;
    Uint32 now = SDL_GetTicks();
    s.loops++;
    s.sum += handler_ms;
    if (handler_ms > s.max) s.max = handler_ms;
    if (s.since && now - s.since < PROBE_FRAME_MS) return;
    unsigned frames;
    unsigned long px;
    display_take_stats(&frames, &px);
    long cpu = proc_cpu_ticks();
    if (s.since && log_probe_on()) {
        double secs = (now - s.since) / 1000.0;
        long rss = proc_rss_kb();
        int load = cpu >= 0 && s.cpu >= 0 ? (int)((cpu - s.cpu) * 100.0 / sysconf(_SC_CLK_TCK) / secs) : -1;
        log_probe("frame: %.1f drawn/s (%.0f%% of the screen each), lvgl %.2f ms avg, %.1f ms max, %.0f loops/s, "
                  "rss %ld MB, cpu %d%% of a core",
                  frames / secs,
                  frames ? 100.0 * px / frames / (lv_disp_get_hor_res(NULL) * lv_disp_get_ver_res(NULL)) : 0.0,
                  s.sum / s.loops, s.max, s.loops / secs, rss < 0 ? -1 : rss / 1024, load);
    }
    s.since = now ? now : 1;
    s.loops = 0;
    s.sum = s.max = 0;
    s.cpu = cpu;
}

static void poll_sdl(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        probe_sdl_event(&e);
        if (e.type == SDL_QUIT) {
            log_msg("sdl QUIT");
            /* over a running stream, Back/Home opens the menu instead of quitting */
            if (app_conn_state() == CONN_STREAMING) ui_overlay_toggle();
            else app_quit();
        } else if (e.type == SDL_TEXTINPUT) input_remote_text(e.text.text);
        else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode k = e.key.keysym.sym;
            bool back = (k == SDLK_ESCAPE);
#ifdef SDLK_AC_BACK
            if (k == SDLK_AC_BACK) back = true;
#endif
#ifndef CHIAKI_SIM
            if (e.key.keysym.scancode == SDL_WEBOS_SCANCODE_BACK) back = true; /* the claimed TV remote Back */
#endif
            if (back) {
                /* one level back: over a stream it opens/closes the menu; at Home's top level it exits */
                if (!ui_back()) app_quit();
            } else {
                input_remote_key(k, e.key.keysym.mod);
            }
        }
#ifdef CHIAKI_SIM
        else if (e.type == SDL_MOUSEWHEEL)
            input_sim_wheel(e.wheel.preciseY);
#endif
    }
}

int main(int argc, char **argv) {
    log_init(LOG_PATH);
    log_msg("Chiaki webOS %s starting (LVGL %d.%d.%d)", CHIAKI_WEBOS_VERSION, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH);
    for (int i = 1; i < argc; i++) log_msg("argument %d: %s", i, argv[i]);

#ifndef CHIAKI_SIM
    /* the TV keeps the Back key for itself unless the app claims it; after this the
     * press arrives as SDL_WEBOS_SCANCODE_BACK (handled in poll_sdl) */
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
#endif

    lv_init();
    if (!display_init()) return 1;
    theme_init(display_get());
    config_load();
    if (probe_requested(argc, argv)) app_probe_force();
    app_init(ui_app_changed);
    ui_create();
    log_msg("UI created (%dx%d)", lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
#ifndef CHIAKI_SIM
    log_msg("backend: BACK key policy=%s", SDL_GetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK));
    log_msg("backend: SDL %d.%d.%d, video driver=%s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
            SDL_GetCurrentVideoDriver());
#endif
    log_msg("sdl: text input %s, screen keyboard %s", SDL_IsTextInputActive() ? "active" : "inactive",
            SDL_HasScreenKeyboardSupport() ? "supported" : "not supported");

    const input_hooks_t hooks = {
        .tab_step = ui_tab_step,
        .quit = app_quit,
        .pad_changed = ui_set_pad_state,
        .scroll_fallback = ui_scroll_fallback,
        .back = ui_pad_back,
        .menu = ui_overlay_toggle,
        .shift = ui_pad_shift,
    };
    input_init(ui_group(), &hooks);

    if (!getenv("CHIAKI_NO_SPLASH")) splash_show();

    while (app_running()) {
        poll_sdl();
        input_tick();
        app_tick();
        /* while streaming the NDL video plane shows through the app surface; the LVGL UI is
         * drawn only for the overlay menu (nothing when the menu is hidden) */
        bool streaming = app_conn_state() == CONN_STREAMING;
        display_set_video(streaming);
        display_set_draw(!streaming || ui_overlay_open());
        Uint64 t0 = SDL_GetPerformanceCounter();
        lv_timer_handler();
        probe_frame((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        SDL_Delay(5);
    }
    log_msg("exiting");
    display_shutdown();
    log_close();
    return 0;
}
