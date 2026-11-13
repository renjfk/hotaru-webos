/* platform/display.c - SDL window + LVGL display driver (see display.h). */
#include "display.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "../log.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static lv_disp_t *g_disp;
static bool g_video;       /* streaming: clear the app surface transparent (NDL plane below) */
static bool g_draw = true; /* draw the LVGL UI (false while streaming with the menu hidden) */

/* the NDL video plane is composited below the app surface: while the menu is open over the
 * stream, keep repainting it. With the menu hidden LVGL does not render at all (display_set_draw) */
static void video_tick(lv_timer_t *t) {
    (void)t;
    if (g_video && g_draw) lv_obj_invalidate(lv_scr_act());
}

/* one empty frame: transparent over the video, black otherwise */
static void present_empty(void) {
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, g_video ? 0 : 255);
    SDL_RenderClear(g_ren);
    SDL_RenderPresent(g_ren);
}

/* LVGL clears the area it is about to draw (screen_transp): only that area, not the
 * whole full-screen buffer it passes */
static void clear_cb(lv_disp_drv_t *drv, uint8_t *buf, uint32_t size) {
    const lv_area_t *a = drv->draw_ctx ? drv->draw_ctx->buf_area : NULL;
    uint32_t n = a ? lv_area_get_size(a) : size;
    memset(buf, 0, (size_t)(n < size ? n : size) * sizeof(lv_color_t));
}

void display_set_video(bool on) {
    if (g_video == on) return;
    g_video = on;
    /* Per-pixel alpha is what lets the video through. LVGL keeps alpha only with screen_transp
     * (otherwise every blend writes alpha 0xFF and the menu covers the video completely), so turn
     * it on while streaming; the opaque path is cheaper for the normal UI. */
    if (g_disp) {
        g_disp->driver->screen_transp = on ? 1 : 0;
        lv_obj_invalidate(lv_scr_act()); /* no stale alpha left in the texture either way */
    }
    log_event(LOG_INFO, "STREAM", "app surface %s (NDL plane below, LVGL alpha %s)", on ? "transparent" : "opaque",
              on ? "on" : "off");
}

/* whether the LVGL UI is drawn (streaming keeps the surface transparent, so the menu
 * overlays the live video; with the menu hidden nothing is drawn at all). Hidden: the surface
 * is cleared once and LVGL's refresh timer is paused. Rendering the full-screen UI only to
 * discard it cost about half a core on the TV, CPU the stream's receive and decode threads need. */
void display_set_draw(bool draw) {
    if (g_draw == draw) return;
    g_draw = draw;
    lv_timer_t *refr = g_disp ? _lv_disp_get_refr_timer(g_disp) : NULL;
    if (draw) {
        if (refr) lv_timer_resume(refr);
        if (g_disp) lv_obj_invalidate(lv_scr_act());
    } else {
        if (refr) lv_timer_pause(refr);
        if (g_ren) present_empty();
    }
    log_msg("display: LVGL rendering %s", draw ? "resumed" : "paused (nothing drawn over the stream)");
}

static unsigned g_frames;       /* presented since display_take_stats() */
static unsigned long g_flushed; /* pixels LVGL redrew in them */

void display_take_stats(unsigned *frames, unsigned long *pixels) {
    *frames = g_frames;
    *pixels = g_flushed;
    g_frames = 0;
    g_flushed = 0;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    g_flushed += (unsigned long)w * (unsigned long)h;
    if (g_draw) {
        SDL_Rect r = { area->x1, area->y1, w, h };
        SDL_UpdateTexture(g_tex, &r, px, w * (int)sizeof(lv_color_t));
    }
    if (lv_disp_flush_is_last(drv)) {
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
        if (g_video) SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 0); /* transparent over the video */
        else SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        if (g_draw) {
            SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        }
        SDL_RenderPresent(g_ren);
        g_frames++;
    }
    lv_disp_flush_ready(drv);
}

bool display_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log_msg("SDL_Init: %s", SDL_GetError());
        return false;
    }

    /* LVGL always renders at the TV's 1920x1080 so the UI layout is identical
     * in the simulator; the sim just presents that texture into a window. */
    int w = 1920, h = 1080;
#ifdef CHIAKI_SIM
    int ww = w / 2, wh = h / 2; /* a desktop-sized window; keeps 16:9 */
    g_win = SDL_CreateWindow("Chiaki webOS (sim)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ww, wh,
                             SDL_WINDOW_RESIZABLE);
    if (g_win) SDL_ShowCursor(SDL_DISABLE); /* no pointer in this UI, like on the TV */
    log_msg("simulator window %dx%d (display %dx%d)", ww, wh, w, h);
#else
    SDL_DisplayMode mode = { 0 };
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0) {
        w = mode.w;
        h = mode.h;
    }
    log_msg("display mode %dx%d @ %d Hz", w, h, mode.refresh_rate);
    /* An 8-bit alpha channel on the app surface so the NDL video plane shows through
     * while streaming; opaque otherwise. */
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    g_win = SDL_CreateWindow("Chiaki webOS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                             SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
#endif
    if (!g_win) {
        log_msg("window: %s", SDL_GetError());
        return false;
    }
    SDL_StartTextInput(); /* physical (USB / desktop) keyboard -> SDL_TEXTINPUT */

    SDL_RendererInfo info;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (g_ren && SDL_GetRendererInfo(g_ren, &info) == 0) log_msg("renderer name=%s", info.name);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        log_msg("no renderer: %s", SDL_GetError());
        return false;
    }

#ifndef CHIAKI_SIM
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(g_ren, &ow, &oh) == 0 && ow > 0 && oh > 0) {
        w = ow;
        h = oh;
    }
#endif
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_tex) {
        log_msg("no texture: %s", SDL_GetError());
        return false;
    }

    lv_color_t *buf1 = malloc((size_t)w * h * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc((size_t)w * h * sizeof(lv_color_t));
    if (!buf1 || !buf2) {
        log_msg("no memory for %dx%d draw buffers", w, h);
        free(buf1);
        free(buf2);
        return false;
    }
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, (uint32_t)(w * h));
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &draw_buf;
    drv.flush_cb = flush_cb;
    drv.clear_cb = clear_cb;
    drv.hor_res = w;
    drv.ver_res = h;
    g_disp = lv_disp_drv_register(&drv);
    lv_timer_create(video_tick, 30, NULL); /* keep the menu over the stream refreshed */
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
