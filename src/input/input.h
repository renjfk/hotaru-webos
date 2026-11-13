/* input/input.h - all user input, fed into LVGL.
 *
 * Sources: the TV remote (SDL key events -> LVGL keypad indev) and a
 * DualShock 4 read straight from evdev (EVIOCGRAB, hotplug-aware). The pad:
 *   D-pad       -> keypad keys, same queue as the remote, auto-repeat
 *   left stick  -> the same keys as the D-pad (a second D-pad, with repeat)
 *   Cross       -> ENTER
 *   right stick -> scrolls the focused object, else hooks.scroll_fallback
 *   touchpad    -> the console only (forwarded while streaming)
 *   L1 / R1     -> hooks.tab_step, Circle -> hooks.back
 *   Share + Options together -> hooks.menu, also while streaming. Streaming forwards
 *               those two buttons PAD_CHORD_MS late, so the chord never reaches the console
 *               (a lone press still does, just slightly later)
 * The UI is focus driven: there is no pointer and no cursor, every key goes to the keypad
 * indev and from there to the focused object of the group handed to input_init.
 *
 * input never includes UI headers; it reports through input_hooks_t.
 * Streaming: while a session runs, input_set_streaming() stops the UI effects and
 * input_pad_get() exposes the raw pad the app forwards to chiaki/session.h.
 */
#ifndef CHIAKI_WEBOS_INPUT_H
#define CHIAKI_WEBOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include <lvgl.h>

typedef enum { PAD_NONE, PAD_ONLINE, PAD_OFFLINE } pad_state_t;

/* Canonical pad button bits (a DualShock 4 today). The session layer maps these to the
 * console protocol (chiaki/session.h SESSION_PAD_BTN_*), so input never depends on chiaki.
 * Values match ChiakiControllerButton's low bits, keeping the mapping a straight copy. */
typedef enum {
    PAD_BTN_CROSS = 1u << 0,
    PAD_BTN_CIRCLE = 1u << 1,
    PAD_BTN_SQUARE = 1u << 2,
    PAD_BTN_TRIANGLE = 1u << 3,
    PAD_BTN_DPAD_LEFT = 1u << 4,
    PAD_BTN_DPAD_RIGHT = 1u << 5,
    PAD_BTN_DPAD_UP = 1u << 6,
    PAD_BTN_DPAD_DOWN = 1u << 7,
    PAD_BTN_L1 = 1u << 8,
    PAD_BTN_R1 = 1u << 9,
    PAD_BTN_L3 = 1u << 10,
    PAD_BTN_R3 = 1u << 11,
    PAD_BTN_OPTIONS = 1u << 12,
    PAD_BTN_SHARE = 1u << 13,
    PAD_BTN_TOUCHPAD = 1u << 14,
    PAD_BTN_PS = 1u << 15,
} pad_button_t;

/* Raw pad state, for forwarding to a running session (see input_pad_get). Sticks are
 * -32768..32767 (evdev direction), triggers 0..255, touch is in the pad's own units. */
typedef struct {
    uint32_t buttons; /* PAD_BTN_* */
    uint8_t l2, r2;
    int16_t left_x, left_y, right_x, right_y;
    bool touch_down;
    uint16_t touch_x, touch_y;
} input_pad_t;

#define PAD_CHORD_MS 100 /* how late Share/Options reach the console while streaming */

/* A focused object with this flag takes the arrow keys itself: UP/DOWN reach it as
 * LV_KEY_UP/DOWN instead of moving the group focus (LV_KEY_PREV/NEXT). */
#define INPUT_FLAG_ARROWS LV_OBJ_FLAG_USER_1

typedef struct {
    void (*tab_step)(int dir, const char *button); /* L1 -> -1, R1 -> +1 */
    void (*quit)(void);                            /* app exit request */
    void (*pad_changed)(pad_state_t state);        /* controller grabbed / lost */
    lv_obj_t *(*scroll_fallback)(void);            /* right-stick target when nothing else scrolls */
    void (*back)(void);                            /* Circle while the pad drives the UI */
    void (*menu)(void);                            /* Share + Options pressed together */
    void (*shift)(void);                           /* L2 or R2 while the pad drives the UI */
} input_hooks_t;

/* registers the keypad indev on `group` and grabs a pad if one is present */
void input_init(lv_group_t *group, const input_hooks_t *hooks);
void input_tick(void); /* every main-loop iteration: pad, hotplug, repeat, scrolling */
/* SDL_KEYDOWN from the TV remote (or a keyboard). A plain digit key types its digit at once: the
 * TV may not send text events for the remote's number keys, and when it does (a desktop keyboard
 * does) the echo that follows is dropped. With Shift/Ctrl/Alt held the text event decides. */
void input_remote_key(SDL_Keycode key, Uint16 mod);
void input_remote_text(const char *utf8); /* SDL_TEXTINPUT: USB/desktop keyboard typing */
pad_state_t input_pad_state(void);
const char *input_pad_name(void); /* the attached controller's name, "" if none */
/* its battery, 0..100; false when the controller reports none. On this TV only the DualShock 4
 * does (hid-sony publishes it in sysfs); a DualSense is hid-generic and has no battery node. */
bool input_pad_battery(int *pct);
bool input_pad_rumbles(void); /* the attached controller can rumble on this TV (a DualSense cannot) */
bool input_pad_touches(void); /* its touchpad reports touches, not only the click (a DualSense: the click) */

/* While a session streams the pad drives the console, not the UI: its keys and scrolling
 * stop and the raw state is read with input_pad_get(). Set by ui.c on a
 * connection change. Buttons still held when streaming starts (the Circle that closed
 * the menu) stay out of input_pad_get() until they are released. */
void input_set_streaming(bool on);
bool input_streaming(void);                     /* the pad is captured for the console right now */
bool input_pad_get(input_pad_t *out);           /* false when no pad is attached */
void input_rumble(uint8_t left, uint8_t right); /* 0..255; 0/0 stops (pad FF_RUMBLE) */

#ifdef CHIAKI_SIM
void input_sim_wheel(float wheel_y); /* simulator: the mouse wheel stands in for the right stick */
#endif

#endif
