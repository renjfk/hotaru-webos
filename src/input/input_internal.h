/* input/input_internal.h - state and functions shared by the input/ files only. */
#ifndef CHIAKI_INPUT_INTERNAL_H
#define CHIAKI_INPUT_INTERNAL_H

#include "input.h"

typedef struct {
    lv_group_t *group;
    input_hooks_t hooks;
    lv_indev_t *ptr_indev;
    lv_obj_t *cursor;
    float cx, cy;       /* pointer position in px */
    bool click;         /* Cross held in pointer mode */
    bool cursor_on;     /* pointer mode */
    int cursor_speed;   /* percent of CURSOR_SPEED */
} input_ctx_t;

extern input_ctx_t g_in;

/* keypad.c: remote + D-pad key queue -> LVGL keypad indev */
void keypad_init(lv_group_t *group);
void keypad_set_group(lv_group_t *group);
void keypad_push(lv_key_t key);
lv_key_t keypad_from_sdl(SDL_Keycode key);

/* pointer.c: cursor, LVGL pointer indev, hover, press-cancel, scrolling */
void pointer_init(lv_obj_t *cursor);
void pointer_show(bool on);
void pointer_keypad_mode(void);                      /* hide cursor, drop hover */
void pointer_tick(float nx, float ny, float dt);     /* left stick, normalised -1..1 */
void pointer_scroll(float sx, float sy, float dt);   /* right stick, normalised -1..1 */
void pointer_drop_press(void);                       /* pad lost mid-press: never click */

/* gamepad.c: evdev DualShock 4 */
bool gamepad_try_acquire(void);
void gamepad_tick(void);
pad_state_t gamepad_state(void);

#endif
