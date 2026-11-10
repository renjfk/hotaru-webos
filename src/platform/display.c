/* platform/display.c - SDL window + LVGL display driver (see display.h). */
#include "display.h"

#include <SDL.h>
#include <stdlib.h>

#include "../log.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static lv_disp_t *g_disp;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(g_tex, &r, px, w * (int)sizeof(lv_color_t));
    if (lv_disp_flush_is_last(drv)) {
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
    }
    lv_disp_flush_ready(drv);
}

bool display_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { log_msg("SDL_Init: %s", SDL_GetError()); return false; }

    /* LVGL always renders at the TV's 1920x1080 so the UI layout is identical
     * in the simulator; the sim just presents that texture into a window. */
    int w = 1920, h = 1080;
#ifdef CHIAKI_SIM
    int ww = w / 2, wh = h / 2;   /* a desktop-sized window; keeps 16:9 */
    g_win = SDL_CreateWindow("Chiaki webOS (sim)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             ww, wh, SDL_WINDOW_RESIZABLE);
    if (g_win) SDL_ShowCursor(SDL_DISABLE);   /* the app draws its own cursor, like the TV */
    log_msg("simulator window %dx%d (display %dx%d)", ww, wh, w, h);
#else
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0) { w = mode.w; h = mode.h; }
    log_msg("display mode %dx%d", w, h);
    g_win = SDL_CreateWindow("Chiaki webOS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                             SDL_WINDOW_FULLSCREEN);
#endif
    if (!g_win) { log_msg("window: %s", SDL_GetError()); return false; }

    SDL_RendererInfo info;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (g_ren && SDL_GetRendererInfo(g_ren, &info) == 0) log_msg("renderer name=%s", info.name);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { log_msg("no renderer: %s", SDL_GetError()); return false; }

#ifndef CHIAKI_SIM
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(g_ren, &ow, &oh) == 0 && ow > 0 && oh > 0) { w = ow; h = oh; }
#endif
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_tex) { log_msg("no texture: %s", SDL_GetError()); return false; }

    lv_color_t *buf1 = malloc((size_t)w * h * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc((size_t)w * h * sizeof(lv_color_t));
    if (!buf1 || !buf2) { log_msg("no memory for %dx%d draw buffers", w, h); return false; }
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, (uint32_t)(w * h));
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &draw_buf;
    drv.flush_cb = flush_cb;
    drv.hor_res = w;
    drv.ver_res = h;
    g_disp = lv_disp_drv_register(&drv);
    log_msg("display %dx%d ready", w, h);
    return true;
}

void display_shutdown(void) {
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

lv_disp_t *display_get(void) { return g_disp; }

#ifdef CHIAKI_SIM
bool display_window_size(int *w, int *h) {
    if (!g_win) return false;
    SDL_GetWindowSize(g_win, w, h);
    return *w > 0 && *h > 0;
}
#endif
