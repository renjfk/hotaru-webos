/* platform/display.h - SDL window + LVGL display driver.
 *
 * LVGL renders in software into full-screen buffers; flushed areas are copied
 * into one streaming SDL_Texture sized to the renderer output, which the TV's
 * opengles2 renderer presents. (lv_draw_sdl is not used: the streaming texture
 * is the path proven on this TV and avoids letterboxing.)
 */
#ifndef CHIAKI_DISPLAY_H
#define CHIAKI_DISPLAY_H

#include <stdbool.h>
#include <lvgl.h>

/* SDL video + fullscreen window + renderer + LVGL driver; call after lv_init() */
bool display_init(void);
void display_shutdown(void);
lv_disp_t *display_get(void);

#ifdef CHIAKI_SIM
/* Simulator only: current window size in window pixels (the LVGL display stays
 * 1920x1080; main.c scales mouse coordinates by this). */
bool display_window_size(int *w, int *h);
#endif

#endif
