/* ui/stream_menu.h - the menu over a running stream: a Control Center sheet from the bottom.
 *
 * The game keeps playing above it (the app surface is transparent, ui.c). A header with the
 * console and the session, then cards: Resume, Stream quality, Smooth playback, Controller,
 * Disconnect. The Controller card shows the pad in use and, when the pad reports one, its
 * battery beside the art. LEFT/RIGHT move; Cross on a setting card enters adjust mode, where
 * LEFT/RIGHT change it (quality restarts the stream when the menu closes, app_restart_stream;
 * smoothing and rumble apply at once). It covers the bottom ~37% of the screen, which also keeps
 * the repaint cheap while the menu is up.
 */
#ifndef HOTARU_STREAM_MENU_H
#define HOTARU_STREAM_MENU_H

#include <stdbool.h>
#include <lvgl.h>

void stream_menu_create(lv_obj_t *parent);
void stream_menu_open(bool open);   /* slides in / out */
bool stream_menu_visible(void);     /* on screen, also while sliding out */
void stream_menu_key(uint32_t key); /* LV_KEY_* while open */
bool stream_menu_back(void);        /* Circle: leaves adjust mode (true), else false: close the menu */
void stream_menu_refresh(void);     /* connection or settings changed */

/* for tests */
int stream_menu_focus(void);
bool stream_menu_adjusting(void);
lv_obj_t *stream_menu_card(int i);         /* 0 Resume .. 4 Disconnect */
const char *stream_menu_pad_battery(void); /* the Controller card's level, "" when the pad reports none */

#endif
