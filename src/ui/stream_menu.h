/* ui/stream_menu.h - the menu over a running stream: a Control Center sheet from the bottom.
 *
 * The game keeps playing above it (the app surface is transparent, ui.c). A header with the
 * console, the session (clock, the live rate and frame rate session_stats() reports) and chips
 * for what the console is set to, then cards: Resume, Stream quality, Smooth playback, Controller,
 * Disconnect. The Controller card shows the pad in use and, when the pad reports one, its
 * battery beside the art. LEFT/RIGHT move; L1/R1 (and Up/Down on the TV remote when no controller
 * is connected) change the focused card (quality restarts the stream when the menu closes,
 * app_restart_stream; smoothing and rumble apply at once). Cross resumes from Resume and ends the
 * stream from Disconnect; the Disconnect card also chooses whether the console goes to rest mode,
 * and the choice is remembered (config session settings). It covers the bottom ~37% of the screen,
 * which also keeps the repaint cheap while the menu is up.
 */
#ifndef HOTARU_STREAM_MENU_H
#define HOTARU_STREAM_MENU_H

#include <stdbool.h>
#include <lvgl.h>

void stream_menu_create(lv_obj_t *parent);
void stream_menu_open(bool open);   /* slides in / out */
bool stream_menu_visible(void);     /* on screen, also while sliding out */
void stream_menu_key(uint32_t key); /* LV_KEY_* while open */
void stream_menu_adjust(int dir);   /* L1/R1: change the focused card */
void stream_menu_refresh(void);     /* connection, settings or pad changed */

/* for tests */
int stream_menu_focus(void);
lv_obj_t *stream_menu_card(int i);           /* 0 Resume .. 4 Disconnect */
const char *stream_menu_card_value(int i);   /* the value line as drawn, with its arrows */
bool stream_menu_hint_has(const char *text); /* the bottom hints contain this label */
const char *stream_menu_pad_battery(void);   /* the Controller card's level, "" when the pad reports none */
const char *stream_menu_session_text(void);  /* the session line: clock, live Mbps and fps */

#endif
