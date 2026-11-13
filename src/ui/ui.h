/* ui/ui.h - the app screen: Home, the stream menu, the overlays, and the input glue.
 *
 * Built once by ui_create() after theme_init(). Renders app.h state (pass ui_app_changed to
 * app_init) and is driven by input through the hooks wired in main.c. The UI is focus driven:
 * the main group holds one invisible key sink that takes every key (the arrows too,
 * INPUT_FLAG_ARROWS) and hands it to what is in front: a dialog (dialog.h), Add a console
 * (register.h), the stream menu (stream_menu.h) or Home (home.h). No screen uses an LVGL group of
 * its own, and nothing turns the pointer on. Everything must fit 1920x1080 with no overflow.
 */
#ifndef HOTARU_UI_H
#define HOTARU_UI_H

#include <stdbool.h>
#include <lvgl.h>

#include "../app.h"
#include "../input/input.h"

void ui_create(void);
lv_group_t *ui_group(void); /* the main group: the key sink */

void ui_app_changed(app_change_t what);        /* app_listener_t */
void ui_tab_step(int dir, const char *button); /* input hook: L1/R1, Home's Consoles <-> Settings */
void ui_set_pad_state(pad_state_t state);      /* input hook: the controller light and art */
lv_obj_t *ui_scroll_fallback(void);            /* input hook: the log console on its panel */

/* the menu over a running stream: the TV remote's Back or the pad's Share + Options (input
 * hook: menu) toggle it over the running stream (video + audio keep going); Disconnect ends it.
 * Closing it with edited video settings restarts the stream. */
void ui_overlay_toggle(void);
bool ui_overlay_open(void); /* the stream menu is on screen (also while it slides away) */
void ui_pad_back(void);     /* input hook: Circle, one level back */
void ui_pad_shift(void);    /* input hook: L2/R2, the on-screen keyboard's capitals */
bool ui_back(void);         /* one level back; false at Home's top level (Back exits) */
void ui_cover(bool on);     /* a full-screen overlay covers Home: Home stops drawing */

#endif
