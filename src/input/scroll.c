/* input/scroll.c - the right stick scrolls: the focused object's nearest scrollable ancestor,
 * else hooks.scroll_fallback (the Log panel's console). The stick's deflection sets the speed;
 * the fractions of a pixel carry over between ticks.
 */
#include "input_internal.h"

#define SCROLL_SPEED 1600.0f /* px/s at full right-stick deflection */

static float g_acc_x, g_acc_y;

static bool can_scroll(lv_obj_t *o, bool x, bool y) {
    if (!o || !lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) return false;
    lv_dir_t dir = lv_obj_get_scroll_dir(o);
    bool sy = (dir & LV_DIR_VER) && (lv_obj_get_scroll_top(o) > 0 || lv_obj_get_scroll_bottom(o) > 0);
    bool sx = (dir & LV_DIR_HOR) && (lv_obj_get_scroll_left(o) > 0 || lv_obj_get_scroll_right(o) > 0);
    return (y && sy) || (x && sx);
}

static lv_obj_t *target(bool x, bool y) {
    lv_obj_t *o = g_in.group ? lv_group_get_focused(g_in.group) : NULL;
    for (; o; o = lv_obj_get_parent(o))
        if (can_scroll(o, x, y)) return o;
    if (g_in.hooks.scroll_fallback) {
        lv_obj_t *f = g_in.hooks.scroll_fallback();
        if (can_scroll(f, x, y)) return f;
    }
    return NULL;
}

void scroll_tick(float sx, float sy, float dt) {
    if (sx == 0 && sy == 0) {
        scroll_reset();
        return;
    }
    g_acc_x += sx * SCROLL_SPEED * dt;
    g_acc_y += sy * SCROLL_SPEED * dt;
    int dx = (int)g_acc_x, dy = (int)g_acc_y;
    if (!dx && !dy) return;
    g_acc_x -= dx;
    g_acc_y -= dy;
    lv_obj_t *t = target(dx != 0, dy != 0);
    if (!t) return;
    /* stick down/right reveals content below/right, so the content moves the other way */
    lv_coord_t mx = -dx, my = -dy;
    if (my > 0) my = LV_MIN(my, lv_obj_get_scroll_top(t));
    else my = LV_MAX(my, -lv_obj_get_scroll_bottom(t));
    if (mx > 0) mx = LV_MIN(mx, lv_obj_get_scroll_left(t));
    else mx = LV_MAX(mx, -lv_obj_get_scroll_right(t));
    if (mx || my) lv_obj_scroll_by(t, mx, my, LV_ANIM_OFF);
}

void scroll_reset(void) { g_acc_x = g_acc_y = 0; }
