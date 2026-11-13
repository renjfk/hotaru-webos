/* input/keypad.c - TV remote and D-pad keys -> LVGL keypad indev.
 *
 * Keys are queued and replayed to LVGL as press+release pairs. Remote UP/DOWN
 * and D-pad UP/DOWN are LV_KEY_PREV/NEXT (move group focus), except for a focused
 * object that takes the arrows itself (INPUT_FLAG_ARROWS: the UI's key sink).
 */
#include "input_internal.h"

#define QN 32
static lv_key_t g_q[QN];
static int g_qh, g_qt;
static lv_key_t g_cur;
static int g_release;
static lv_indev_t *g_indev;

lv_key_t keypad_from_sdl(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return LV_KEY_PREV;
        case SDLK_DOWN: return LV_KEY_NEXT;
        case SDLK_LEFT: return LV_KEY_LEFT;
        case SDLK_RIGHT: return LV_KEY_RIGHT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return LV_KEY_ENTER;
        case SDLK_BACKSPACE: return LV_KEY_BACKSPACE;
        case SDLK_ESCAPE: return LV_KEY_ESC;
#ifdef SDLK_AC_BACK
        case SDLK_AC_BACK: return LV_KEY_ESC;
#endif
        default: return 0;
    }
}

/* a physical (USB / desktop) keyboard types whole characters, not keycodes: decode
 * the UTF-8 produced by SDL_TEXTINPUT and queue each codepoint as a key, which a
 * focused lv_textarea turns back into text (LVGL sends unknown keys to add_char). */
void keypad_push_text(const char *utf8) {
    if (!utf8) return;
    for (const unsigned char *p = (const unsigned char *)utf8; *p;) {
        uint32_t cp;
        int n;
        if (*p < 0x80) {
            cp = *p;
            n = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            n = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            n = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            n = 4;
        } else { /* a stray continuation byte */
            p++;
            continue;
        }
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                cp = 0;
                break;
            }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += (cp || n == 1) ? n : 1;
        if (cp) keypad_push((lv_key_t)cp);
    }
}

void keypad_push(lv_key_t k) {
    if (!k) return;
    g_q[g_qt % QN] = k;
    g_qt++;
}

/* PREV/NEXT always move group focus in LVGL: an object that takes the arrows itself gets
 * UP/DOWN instead */
static lv_key_t keypad_translate(lv_key_t k) {
    if (k != LV_KEY_PREV && k != LV_KEY_NEXT) return k;
    lv_obj_t *f = g_in.group ? lv_group_get_focused(g_in.group) : NULL;
    if (f && lv_obj_has_flag(f, INPUT_FLAG_ARROWS)) return k == LV_KEY_PREV ? LV_KEY_UP : LV_KEY_DOWN;
    return k;
}

static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (g_release) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = g_cur;
        g_release = 0;
        return;
    }
    if (g_qh != g_qt) {
        g_cur = keypad_translate(g_q[g_qh % QN]);
        g_qh++;
        data->key = g_cur;
        data->state = LV_INDEV_STATE_PRESSED;
        g_release = 1;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void keypad_init(lv_group_t *group) {
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_KEYPAD;
    drv.read_cb = read_cb;
    g_indev = lv_indev_drv_register(&drv);
    lv_indev_set_group(g_indev, group);
}
