/* ui/dialog.h - the modal dialogs, in Home's style and focus driven.
 *
 * ui_confirm: a card over a scrim for a destructive action: what it is about (a console's
 * picture when there is one), what happens, Cancel (the focus) and the action, red when focused.
 * ui_passcode: a sheet rising from the bottom for a short number the console asks for mid-connect
 * (its login passcode): digit boxes and a number pad, the screen behind it stays in view; OK takes
 * the focus once every digit is in, and the remote's number keys type straight in.
 *
 * Both sit on the top layer and take every key from ui.c while open (ui_dialog_key, Circle /
 * Back = ui_dialog_back = cancel). A callback runs after the dialog is gone, through an async
 * call, so it may change app state (which can rebuild the UI).
 */
#ifndef HOTARU_DIALOG_H
#define HOTARU_DIALOG_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

#include "art.h"

typedef void (*ui_confirm_cb_t)(void *user);
typedef void (*ui_passcode_cb_t)(const char *code, void *user); /* code NULL on cancel */

/* art: the console it is about, ART__N for none. No-op while a dialog is open */
void ui_confirm(const char *title, const char *message, const char *confirm_text, art_id_t art,
                ui_confirm_cb_t on_confirm, void *user);
void ui_passcode(const char *title, const char *message, int digits, const char *ok_text, ui_passcode_cb_t on_done,
                 void *user);

bool ui_dialog_active(void);
void ui_dialog_key(uint32_t key); /* LV_KEY_*, or a typed digit */
void ui_dialog_back(void);        /* cancel */

/* for tests */
bool ui_confirm_active(void);
bool ui_passcode_active(void);
int ui_dialog_focus(void); /* 0 Cancel, 1 the action, 2 the number pad */
const char *ui_passcode_text(void);
lv_obj_t *ui_dialog_root(void);

#endif
