/* ui/tabs.h - the tab bar + pages (one LVGL tabview).
 *
 * Pages change only via tabs_go(): tab pill click, L1/R1 (tabs_step), remote
 * or D-pad LEFT/RIGHT on the tab bar, or keypad focus moving onto another
 * page. No drag/swipe: the content is not user-scrollable, yet
 * lv_tabview_set_act(..., LV_ANIM_ON) still slides it because
 * lv_obj_scroll_to_x() ignores LV_OBJ_FLAG_SCROLLABLE. The active pill tracks
 * the content's scroll position, so it slides in lockstep with the page.
 */
#ifndef CHIAKI_TABS_H
#define CHIAKI_TABS_H

#include <lvgl.h>

lv_obj_t *tabs_create(lv_obj_t *parent, const char *const names[], const char *const icons[], int count);
lv_obj_t *tabs_page(int index);
lv_obj_t *tabs_bar(void);
int tabs_active(void);
int tabs_page_of(lv_obj_t *obj);   /* page index containing obj, -1 if none */

void tabs_go(int index, const char *source);    /* animated; logs "[TAB] switched to ..." */
void tabs_step(int dir, const char *source);    /* next/prev with wrap; focus lands on the page's first control */
void tabs_bind_group(lv_group_t *group);        /* keypad focus moving to another page follows it */

#endif
