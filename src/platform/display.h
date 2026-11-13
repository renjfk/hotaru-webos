/* platform/display.h - SDL window + LVGL display driver.
 *
 * LVGL renders in software into full-screen buffers; flushed areas are copied
 * into one streaming SDL_Texture sized to the renderer output, which the TV's
 * opengles2 renderer presents. (lv_draw_sdl is not used: the streaming texture
 * is the path proven on this TV and avoids letterboxing.)
 */
#ifndef HOTARU_DISPLAY_H
#define HOTARU_DISPLAY_H

#include <stdbool.h>
#include <lvgl.h>

/* SDL video + fullscreen window + renderer + LVGL driver; call after lv_init() */
bool display_init(void);
void display_shutdown(void);
lv_disp_t *display_get(void);

/* Streaming: NDL draws on a hardware plane below the app surface, so clear the app
 * surface to transparent (instead of black) and render the UI with per-pixel alpha
 * (LVGL screen_transp), so translucent menu surfaces show the video through. */
void display_set_video(bool on);

/* Whether the LVGL UI is drawn. While streaming the surface stays transparent, so the
 * overlay menu is drawn on top of the live video; with the menu hidden, draw nothing: the
 * surface is cleared once and LVGL stops rendering (its refresh timer is paused) until
 * drawing resumes, so the stream keeps the CPU. The UI stays plain sRGB even when the stream is HDR: the TV converts
 * its graphics plane into the HDR output itself, so the app must not tone-map it. */
void display_set_draw(bool draw);

/* frames presented and pixels redrawn since the last call (probe mode's frame line) */
void display_take_stats(unsigned *frames, unsigned long *pixels);

#endif
