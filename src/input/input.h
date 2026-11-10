/* input/input.h - all user input, fed into LVGL.
 *
 * Sources: the TV remote (SDL key events -> LVGL keypad indev) and a
 * DualShock 4 read straight from evdev (EVIOCGRAB, hotplug-aware). The pad:
 *   left stick  -> pointer cursor (LVGL pointer indev)
 *   Cross       -> click in pointer mode, ENTER in keypad mode
 *   D-pad       -> keypad keys, same queue as the remote, auto-repeat
 *   right stick -> scrolls the scrollable object under the cursor/focus
 *   L1 / R1     -> hooks.tab_step, Options -> hooks.quit
 * Pointer mode = cursor visible. Keypad mode = remote/D-pad used: the cursor
 * hides and the focus ring is in charge; moving the left stick switches back.
 *
 * input never includes UI headers; it reports through input_hooks_t.
 * Streaming (roadmap): while a session runs, gamepad.c will forward raw pad
 * state to chiaki/session.h instead of driving the UI.
 */
#ifndef CHIAKI_INPUT_H
#define CHIAKI_INPUT_H

#include <stdbool.h>
#include <SDL.h>
#include <lvgl.h>

typedef enum { PAD_NONE, PAD_ONLINE, PAD_OFFLINE } pad_state_t;

typedef struct {
    void (*tab_step)(int dir, const char *button);   /* L1 -> -1, R1 -> +1 */
    void (*quit)(void);                               /* Options */
    void (*pad_changed)(pad_state_t state);           /* controller grabbed / lost */
    lv_obj_t *(*scroll_fallback)(void);               /* right-stick target when nothing else scrolls */
} input_hooks_t;

/* registers the keypad + pointer indevs and grabs a pad if one is present */
void input_init(lv_group_t *group, lv_obj_t *cursor, const input_hooks_t *hooks);
void input_tick(void);                    /* every main-loop iteration: pad, hotplug, cursor */
void input_remote_key(SDL_Keycode key);   /* SDL_KEYDOWN from the TV remote */
bool input_keypad_mode(void);             /* focus ring rather than cursor is in charge */
void input_set_cursor_speed(int percent);
pad_state_t input_pad_state(void);

#ifdef CHIAKI_SIM
/* Simulator only: feed the desktop mouse into the same pointer path the stick
 * drives, so hover/press-cancel behave exactly as on the TV. Coordinates are in
 * LVGL display space (display.c maps the window). */
void input_pointer_set(float x, float y, bool pressed);
void input_pointer_scroll(float wheel_y);   /* one notch = ~+/-1 */
void input_sim_tick(void);                  /* refresh the cursor + hover each frame */
#endif

#endif
