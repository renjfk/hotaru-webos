/* input/input.c - public input API (see input.h); wires keypad, pointer and gamepad. */
#include "../log.h"
#include "input_internal.h"

input_ctx_t g_in = { .cursor_speed = 100 };

void input_init(lv_group_t *group, lv_obj_t *cursor, const input_hooks_t *hooks) {
    g_in.group = group;
    if (hooks) g_in.hooks = *hooks;
    keypad_init(group);   /* first: LVGL treats the first indev as the default for focus */
    pointer_init(cursor);
    if (gamepad_try_acquire()) {
        pointer_show(true);
        log_event(LOG_OK, "INPUT", "DualShock 4 ready, stick moves the cursor");
    } else {
        log_msg("no gamepad found");
        log_event(LOG_WARN, "INPUT", "no gamepad found, use the TV remote (still looking)");
    }
    log_msg("indev ready");
}

void input_tick(void) { gamepad_tick(); }

void input_remote_key(SDL_Keycode key) {
    keypad_push(keypad_from_sdl(key));
    pointer_keypad_mode();
}

bool input_keypad_mode(void) { return !g_in.cursor_on; }

void input_set_cursor_speed(int percent) { g_in.cursor_speed = percent; }

pad_state_t input_pad_state(void) { return gamepad_state(); }

#ifdef CHIAKI_SIM
/* Desktop mouse -> the stick pointer path, so the sim behaves like the TV. */
void input_pointer_set(float x, float y, bool pressed) {
    g_in.cx = x;
    g_in.cy = y;
    g_in.click = pressed;
    pointer_show(true);
}

void input_pointer_scroll(float wheel_y) { pointer_scroll(0.0f, wheel_y, 0.06f); }

void input_sim_tick(void) { pointer_tick(0.0f, 0.0f, 0.0f); }
#endif
