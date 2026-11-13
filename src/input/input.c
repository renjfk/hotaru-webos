/* input/input.c - public input API (see input.h); wires the keypad, the gamepad and scrolling. */
#include <string.h>

#include "../log.h"
#include "input_internal.h"

input_ctx_t g_in;

void input_init(lv_group_t *group, const input_hooks_t *hooks) {
    g_in.group = group;
    if (hooks) g_in.hooks = *hooks;
    keypad_init(group);
    if (gamepad_try_acquire()) {
        log_event(LOG_OK, "INPUT", "%s ready: D-pad or left stick to move, Cross to select",
                  strstr(gamepad_name(), "DualSense") ? "DualSense" : "DualShock 4");
    } else {
        log_msg("no gamepad found");
        log_event(LOG_WARN, "INPUT", "no gamepad found, use the TV remote (still looking)");
    }
    log_msg("indev ready");
}

void input_tick(void) { gamepad_tick(); }

#define TEXT_ECHO_MS 150 /* a digit's text event follows its key event within this */

static struct {
    char c;
    Uint32 at;
} g_typed; /* the digit a key event typed, until its echo */

static char digit_of(SDL_Keycode key) {
    if (key >= SDLK_0 && key <= SDLK_9) return (char)('0' + (key - SDLK_0));
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return (char)('1' + (key - SDLK_KP_1));
    if (key == SDLK_KP_0) return '0';
    return 0;
}

void input_remote_key(SDL_Keycode key, Uint16 mod) {
    char d = (mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI)) ? 0 : digit_of(key);
    if (d) {
        keypad_push((lv_key_t)d);
        g_typed.c = d;
        g_typed.at = SDL_GetTicks();
        return;
    }
    keypad_push(keypad_from_sdl(key));
}

void input_remote_text(const char *utf8) {
    if (!utf8) return;
    bool echo = g_typed.c && utf8[0] == g_typed.c && !utf8[1] && SDL_GetTicks() - g_typed.at < TEXT_ECHO_MS;
    g_typed.c = 0;
    if (!echo) keypad_push_text(utf8);
}

pad_state_t input_pad_state(void) { return gamepad_state(); }
const char *input_pad_name(void) { return gamepad_name(); }
bool input_pad_battery(int *pct) { return gamepad_battery(pct); }

void input_set_streaming(bool on) {
    if (g_in.streaming == on) return;
    g_in.streaming = on;
    scroll_reset();
    gamepad_streaming(on);
}

bool input_pad_get(input_pad_t *out) { return gamepad_get(out); }

bool input_streaming(void) { return g_in.streaming; }

void input_rumble(uint8_t left, uint8_t right) { gamepad_rumble(left, right); }
bool input_pad_rumbles(void) { return gamepad_can_rumble(); }
bool input_pad_touches(void) { return gamepad_can_touch(); }

#ifdef HOTARU_SIM
void input_sim_wheel(float wheel_y) { scroll_tick(0.0f, wheel_y, 0.06f); }
#endif
