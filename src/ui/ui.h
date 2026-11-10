/* ui/ui.h - the app screen: top bar, tabbed pages, action bar, keyboard.
 *
 * Built once by ui_create() after theme_init(). Renders app.h state (pass
 * ui_app_changed to app_init) and is driven by input through the hooks wired
 * in main.c. Everything must fit 1920x1080 with no overflow.
 */
#ifndef CHIAKI_UI_H
#define CHIAKI_UI_H

#include <lvgl.h>

#include "../app.h"
#include "../input/input.h"

void ui_create(void);
lv_group_t *ui_group(void);                       /* keypad focus order: tab bar, page controls, action bar */

void ui_app_changed(app_change_t what);           /* app_listener_t */
void ui_tab_step(int dir, const char *button);    /* input hook: L1/R1 */
void ui_set_pad_state(pad_state_t state);         /* input hook: controller chip */
lv_obj_t *ui_scroll_fallback(void);               /* input hook: the log console on the Log tab */

#endif
