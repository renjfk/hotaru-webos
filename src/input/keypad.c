/* input/keypad.c - TV remote and D-pad keys -> LVGL keypad indev.
 *
 * Keys are queued and replayed to LVGL as press+release pairs. Remote UP/DOWN
 * and D-pad UP/DOWN are LV_KEY_PREV/NEXT (move group focus).
 */
#include "input_internal.h"

#define QN 32
static lv_key_t g_q[QN];
static int g_qh, g_qt;
static lv_key_t g_cur;
static int g_release;

lv_key_t keypad_from_sdl(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return LV_KEY_PREV;
        case SDLK_DOWN: return LV_KEY_NEXT;
        case SDLK_LEFT: return LV_KEY_LEFT;
        case SDLK_RIGHT: return LV_KEY_RIGHT;
        case SDLK_RETURN: case SDLK_KP_ENTER: return LV_KEY_ENTER;
        case SDLK_ESCAPE: return LV_KEY_ESC;
#ifdef SDLK_AC_BACK
        case SDLK_AC_BACK: return LV_KEY_ESC;
#endif
        default: return 0;
    }
}

void keypad_push(lv_key_t k) {
    if (!k) return;
    g_q[g_qt % QN] = k;
    g_qt++;
}

/* PREV/NEXT always move group focus in LVGL, which would close an open
 * dropdown; while one is open, UP/DOWN (remote or D-pad) move its selection. */
static lv_key_t keypad_translate(lv_key_t k) {
    if (k != LV_KEY_PREV && k != LV_KEY_NEXT) return k;
    lv_obj_t *f = g_in.group ? lv_group_get_focused(g_in.group) : NULL;
    if (f && lv_obj_check_type(f, &lv_dropdown_class) && lv_dropdown_is_open(f))
        return k == LV_KEY_PREV ? LV_KEY_UP : LV_KEY_DOWN;
    return k;
}

static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (g_release) { data->state = LV_INDEV_STATE_RELEASED; data->key = g_cur; g_release = 0; return; }
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
    lv_indev_set_group(lv_indev_drv_register(&drv), group);
}
