/* input/input_internal.h - state and functions shared by the input/ files only. */
#ifndef CHIAKI_WEBOS_INPUT_INTERNAL_H
#define CHIAKI_WEBOS_INPUT_INTERNAL_H

#include "input.h"

typedef struct {
    lv_group_t *group;
    input_hooks_t hooks;
    bool streaming; /* a session runs: the pad drives the console, not the UI */
} input_ctx_t;

extern input_ctx_t g_in;

/* keypad.c: remote + D-pad key queue -> LVGL keypad indev */
void keypad_init(lv_group_t *group);
void keypad_push(lv_key_t key);
void keypad_push_text(const char *utf8); /* physical keyboard: a UTF-8 string -> keys */
lv_key_t keypad_from_sdl(SDL_Keycode key);

/* scroll.c: the right stick scrolls the focused object, else hooks.scroll_fallback */
void scroll_tick(float sx, float sy, float dt); /* normalised -1..1; 0, 0 stops */
void scroll_reset(void);                        /* pad lost, streaming: drop what is left over */

/* gamepad.c: evdev DualShock 4 */
bool gamepad_try_acquire(void);
void gamepad_tick(void);
pad_state_t gamepad_state(void);
const char *gamepad_name(void);                   /* the evdev device name, for the UI label */
bool gamepad_battery(int *pct);                   /* the uhid node's power_supply capacity; false when it has none */
bool gamepad_get(input_pad_t *out);               /* state for the session (chord-filtered); false when no pad */
void gamepad_streaming(bool on);                  /* streaming starts: hide the buttons held right now until released */
void gamepad_rumble(uint8_t left, uint8_t right); /* FF_RUMBLE (no-op without FF support) */
bool gamepad_can_rumble(void);                    /* the node takes FF_RUMBLE */
bool gamepad_can_touch(void);                     /* the node reports touchpad touches (ABS_MT_*) */

#endif
