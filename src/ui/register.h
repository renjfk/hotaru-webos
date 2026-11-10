/* ui/register.h - the "Add a console" overlay: pair with a PIN or a PSN account.
 *
 * A full-screen overlay on the top layer (like ui/splash.c) that swallows
 * clicks and deletes itself on close. Left: the method switch (PIN / PSN
 * account), the console type and the status (app_reg_state(), progress,
 * error/result). Right: the active method's pane - console IP + PIN, or the
 * PSN sign-in QR (app_reg_psn_url()) with the URL text and the code entry.
 * Its controls are a focus group of their own, handed to the keypad while
 * it is open, and it borrows the on-screen keyboard; closing gives both back
 * and cancels a running registration. ui.c calls ui_register_refresh() on
 * APP_CHANGED_REG. Opened from Hosts "Add host" and Input "Add a console".
 */
#ifndef CHIAKI_REGISTER_H
#define CHIAKI_REGISTER_H

#include <stdbool.h>
#include <lvgl.h>

void ui_register_open(void);      /* no-op if already open */
void ui_register_close(void);     /* no-op if not open */
bool ui_register_active(void);
void ui_register_refresh(void);   /* app registration state changed */

/* for tests: the overlay root and its focus group (NULL while closed) */
lv_obj_t *ui_register_root(void);
lv_group_t *ui_register_group(void);

#endif
