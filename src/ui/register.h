/* ui/register.h - "Add a console": a full-screen setup flow, the way the PS5's own setup and
 * sign-in screens look (design/menu, study "add", concept Setup).
 *
 * Text and the step's control on the left, a picture, the number pad or the QR card on the
 * right, a progress line on top; blue on the local-network path, teal on the PSN one. Steps:
 *   PIN: how -> which console -> [your PSN account] -> its address -> its PIN -> pairing -> ready
 *   PSN: how -> sign in with the phone (QR, then the code) -> signing in -> the consoles found
 * The account step comes up when no PSN account ID is stored (PIN pairing needs it): sign in on
 * the phone (the flow then goes on to the address) or type the ID. The address starts on this
 * TV's network. Pairing and sign-in run through app_reg_*; the flow follows APP_CHANGED_REG.
 *
 * Focus driven, no LVGL group: ui.c hands it every key from the key sink while it is open
 * (ui_register_key: LV_KEY_*, or a typed character from the remote's digits or a USB keyboard)
 * and Circle / Back (ui_register_back). The keyboard for the PSN code and the account ID is its
 * own (ctl_keyboard). It crossfades with Home: once it covers the screen, ui_cover() stops Home
 * drawing under it, and closing brings Home back at once under the fading sheet.
 */
#ifndef HOTARU_REGISTER_H
#define HOTARU_REGISTER_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

void ui_register_open(void);  /* the first step; no-op while open */
void ui_register_close(void); /* cancels a running registration */
bool ui_register_active(void);
void ui_register_refresh(void); /* app registration state changed */
void ui_register_key(uint32_t key);
void ui_register_back(void);  /* one step back; closes from the first */
void ui_register_shift(void); /* L2/R2: the keyboard, when it is up, between capitals and small letters */

/* for tests */
typedef enum {
    REG_METHOD,
    REG_KIND,
    REG_ACCOUNT,
    REG_ADDR,
    REG_PIN,
    REG_PAIRING,
    REG_DONE,
    REG_PSN,
    REG_LINKING,
    REG_FOUND,
    REG__N
} reg_step_t;
reg_step_t ui_register_step(void);
int ui_register_focus(void);
bool ui_register_typing(void);      /* the keyboard is up */
const char *ui_register_text(void); /* the address, the PIN, or the field being typed */
lv_obj_t *ui_register_root(void);

#endif
