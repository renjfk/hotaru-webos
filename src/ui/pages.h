/* ui/pages.h - one file per tab page, private to ui/.
 *
 * To add a page: add an entry to the TAB_* enum and the name/icon tables in
 * ui.c, write page_<name>.c with a page_<name>_build(page) function, and
 * declare it here. Pages read and write app.h/config.h state; they do not
 * talk to each other.
 */
#ifndef CHIAKI_PAGES_H
#define CHIAKI_PAGES_H

#include <lvgl.h>

enum { TAB_HOSTS, TAB_VIDEO, TAB_INPUT, TAB_LOG, TAB__COUNT };

void page_hosts_build(lv_obj_t *page);
void page_hosts_refresh(void);       /* selection changed */
void page_hosts_reload(void);        /* the host list changed: rebuilds the list (ui.c then rebuilds the group) */

void page_video_build(lv_obj_t *page);

void page_input_build(lv_obj_t *page);
void page_input_refresh(void);       /* registration / PSN account / host count changed */

void page_log_build(lv_obj_t *page);
void page_log_refresh(void);         /* connection state changed */
void page_log_progress(void);        /* connecting progress only */
lv_obj_t *page_log_console(void);

/* ui.c: show the on-screen keyboard for a text area when it gains focus */
void ui_keyboard_attach(lv_obj_t *textarea, lv_keyboard_mode_t mode);
/* ui.c: the keyboard lives on the screen; an overlay on the top layer borrows it (NULL gives it back) */
void ui_keyboard_host(lv_obj_t *parent);

#endif
