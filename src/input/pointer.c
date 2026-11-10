/* input/pointer.c - stick cursor -> LVGL pointer indev.
 *
 * - The cursor sprite (ui_cursor_create) follows g_in.cx/cy on the top layer.
 * - Whatever interactive object is under it gets LV_STATE_HOVERED; an open
 *   dropdown list highlights the option under it.
 * - read_cb only reports position + pressed state: LVGL derives clicks. A
 *   press is cancelled (LV_EVENT_PRESS_LOST, no click) once the cursor leaves
 *   the pressed widget, so a click must start and end on the same widget.
 * - The right stick scrolls the scrollable object under the cursor, else the
 *   focused object's nearest scrollable ancestor, else hooks.scroll_fallback.
 */
#include <SDL.h>

#include "../log.h"
#include "input_internal.h"

#define SCROLL_SPEED 1600.0f   /* px/s at full right-stick deflection */
#define CURSOR_SPEED 1400.0f   /* px/s at full left-stick deflection, scaled by cursor_speed */
#define DROPDOWN_PR_NONE 0xFFFF   /* LV_DROPDOWN_PR_NONE is private to lv_dropdown.c */

static int g_cursor_px = -1, g_cursor_py = -1, g_cursor_click;
static lv_obj_t *g_hover, *g_hover_list;
static Uint32 g_hover_at;
static float g_scroll_acc_x, g_scroll_acc_y;

/* ---------- hover ---------- */

/* Interactive object under the cursor: a group member or button; btnmatrix
 * widgets (tab bar, keyboard) handle their own per-key feedback. */
static lv_obj_t *hover_target(lv_obj_t *o) {
    for (; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_check_type(o, &lv_dropdownlist_class)) return o;
        if (lv_obj_has_class(o, &lv_btnmatrix_class)) return NULL;
        if (lv_obj_get_group(o) || lv_obj_has_class(o, &lv_btn_class)) return o;
    }
    return NULL;
}
/* highlight the dropdown option under the cursor (same maths as LVGL's get_id_on_point) */
static void hover_list_option(lv_obj_t *list, const lv_point_t *p) {
    lv_dropdown_t *dd = (lv_dropdown_t *)((lv_dropdown_list_t *)list)->dropdown;
    lv_obj_t *label = lv_obj_get_child(list, 0);
    if (!label || !dd->option_cnt) return;
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_coord_t ls = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_coord_t h = lv_font_get_line_height(font) + ls;
    lv_coord_t y = p->y - label->coords.y1 + ls / 2;
    uint16_t id = y < 0 ? 0 : (uint16_t)(y / h);
    if (id >= dd->option_cnt) id = dd->option_cnt - 1;
    if (dd->pr_opt_id != id) { dd->pr_opt_id = id; lv_obj_invalidate(list); }
}
static void hover_update(bool force) {
    Uint32 now = SDL_GetTicks();
    int px = (int)g_in.cx, py = (int)g_in.cy;
    if (!force && px == g_cursor_px && py == g_cursor_py && now - g_hover_at < 120) return;
    g_hover_at = now;

    lv_obj_t *target = NULL;
    lv_point_t p = { (lv_coord_t)px, (lv_coord_t)py };
    if (g_in.cursor_on) {
        lv_obj_t *o = lv_indev_search_obj(lv_layer_top(), &p);
        if (!o) o = lv_indev_search_obj(lv_scr_act(), &p);
        target = hover_target(o);
    }
    lv_obj_t *list = target && lv_obj_check_type(target, &lv_dropdownlist_class) ? target : NULL;
    if (list != g_hover_list) {
        if (g_hover_list && lv_obj_is_valid(g_hover_list)) {
            lv_dropdown_t *dd = (lv_dropdown_t *)((lv_dropdown_list_t *)g_hover_list)->dropdown;
            dd->pr_opt_id = DROPDOWN_PR_NONE;
            lv_obj_invalidate(g_hover_list);
        }
        g_hover_list = list;
    }
    if (list) { hover_list_option(list, &p); target = NULL; }
    if (target != g_hover) {
        if (g_hover && lv_obj_is_valid(g_hover)) lv_obj_clear_state(g_hover, LV_STATE_HOVERED);
        g_hover = target;
        if (target) lv_obj_add_state(target, LV_STATE_HOVERED);
    }
}

/* ---------- press cancel ---------- */

static bool press_cancellable(lv_obj_t *o) {
    return o && !lv_obj_check_type(o, &lv_obj_class) && !lv_obj_check_type(o, &lv_tabview_class);
}
static void press_cancel_check(void) {
    if (!g_in.ptr_indev || !g_in.click) return;
    _lv_indev_proc_t *proc = &g_in.ptr_indev->proc;
    lv_obj_t *obj = proc->types.pointer.act_obj;
    if (proc->wait_until_release || proc->types.pointer.scroll_obj || !press_cancellable(obj)) return;
    lv_area_t a;
    lv_obj_get_click_area(obj, &a);
    lv_point_t p = { (lv_coord_t)g_in.cx, (lv_coord_t)g_in.cy };
    if (_lv_area_is_point_on(&a, &p, 0)) return;
    lv_indev_wait_release(g_in.ptr_indev);
    lv_event_send(obj, LV_EVENT_PRESS_LOST, g_in.ptr_indev);
    log_msg("input: press cancelled, cursor left the widget");
}

/* ---------- right-stick scrolling ---------- */

static bool can_scroll(lv_obj_t *o, bool x, bool y) {
    if (!o || !lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) return false;
    lv_dir_t dir = lv_obj_get_scroll_dir(o);
    bool sy = (dir & LV_DIR_VER) && (lv_obj_get_scroll_top(o) > 0 || lv_obj_get_scroll_bottom(o) > 0);
    bool sx = (dir & LV_DIR_HOR) && (lv_obj_get_scroll_left(o) > 0 || lv_obj_get_scroll_right(o) > 0);
    return (y && sy) || (x && sx);
}
static lv_obj_t *scrollable_ancestor(lv_obj_t *o, bool x, bool y) {
    for (; o; o = lv_obj_get_parent(o))
        if (can_scroll(o, x, y)) return o;
    return NULL;
}
static lv_obj_t *obj_at(lv_obj_t *o, const lv_point_t *p) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || !_lv_area_is_point_on(&o->coords, p, 0)) return NULL;
    for (int32_t i = (int32_t)lv_obj_get_child_cnt(o) - 1; i >= 0; i--) {
        lv_obj_t *c = obj_at(lv_obj_get_child(o, i), p);
        if (c) return c;
    }
    return o;
}
static lv_obj_t *scroll_target(bool x, bool y) {
    lv_obj_t *t = NULL;
    if (g_in.cursor_on) {
        lv_point_t p = { (lv_coord_t)g_in.cx, (lv_coord_t)g_in.cy };
        t = scrollable_ancestor(obj_at(lv_scr_act(), &p), x, y);
    }
    if (!t && g_in.group) t = scrollable_ancestor(lv_group_get_focused(g_in.group), x, y);
    if (!t && g_in.hooks.scroll_fallback) {
        lv_obj_t *f = g_in.hooks.scroll_fallback();
        if (can_scroll(f, x, y)) t = f;
    }
    return t;
}
void pointer_scroll(float sx, float sy, float dt) {
    if (sx == 0 && sy == 0) { g_scroll_acc_x = g_scroll_acc_y = 0; return; }
    g_scroll_acc_x += sx * SCROLL_SPEED * dt;
    g_scroll_acc_y += sy * SCROLL_SPEED * dt;
    int dx = (int)g_scroll_acc_x, dy = (int)g_scroll_acc_y;
    if (!dx && !dy) return;
    g_scroll_acc_x -= dx;
    g_scroll_acc_y -= dy;
    lv_obj_t *t = scroll_target(dx != 0, dy != 0);
    if (!t) return;
    /* stick down/right reveals content below/right, so the content moves the other way */
    lv_coord_t mx = -dx, my = -dy;
    if (my > 0) my = LV_MIN(my, lv_obj_get_scroll_top(t)); else my = LV_MAX(my, -lv_obj_get_scroll_bottom(t));
    if (mx > 0) mx = LV_MIN(mx, lv_obj_get_scroll_left(t)); else mx = LV_MAX(mx, -lv_obj_get_scroll_right(t));
    if (mx || my) lv_obj_scroll_by(t, mx, my, LV_ANIM_OFF);
}

/* ---------- cursor + indev ---------- */

void pointer_show(bool on) {
    if (!g_in.cursor || on == g_in.cursor_on) return;
    g_in.cursor_on = on;
    if (on) lv_obj_clear_flag(g_in.cursor, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_in.cursor, LV_OBJ_FLAG_HIDDEN);
}

void pointer_keypad_mode(void) {
    if (!g_in.cursor_on) return;
    pointer_show(false);
    hover_update(true);
}

void pointer_drop_press(void) {
    if (g_in.click && g_in.ptr_indev) lv_indev_wait_release(g_in.ptr_indev);   /* a held press must not click */
    g_in.click = false;
    g_scroll_acc_x = g_scroll_acc_y = 0;
}

void pointer_tick(float nx, float ny, float dt) {
    const float speed = CURSOR_SPEED * (float)g_in.cursor_speed / 100.0f;
    lv_coord_t w = lv_disp_get_hor_res(NULL), h = lv_disp_get_ver_res(NULL);
    g_in.cx += nx * speed * dt;
    g_in.cy += ny * speed * dt;
    if (g_in.cx < 0) g_in.cx = 0;
    if (g_in.cy < 0) g_in.cy = 0;
    if (g_in.cx > w) g_in.cx = (float)w;
    if (g_in.cy > h) g_in.cy = (float)h;

    if (nx != 0 || ny != 0) pointer_show(true);
    press_cancel_check();
    if (!g_in.cursor) return;
    hover_update(false);
    if ((int)g_in.cx != g_cursor_px || (int)g_in.cy != g_cursor_py) {
        g_cursor_px = (int)g_in.cx;
        g_cursor_py = (int)g_in.cy;
        lv_obj_set_pos(g_in.cursor, g_cursor_px - lv_obj_get_width(g_in.cursor) / 2,
                       g_cursor_py - lv_obj_get_height(g_in.cursor) / 2);
    }
    if (g_in.click != g_cursor_click) {
        g_cursor_click = g_in.click;
        if (g_in.click) lv_obj_add_state(g_in.cursor, LV_STATE_PRESSED);
        else lv_obj_clear_state(g_in.cursor, LV_STATE_PRESSED);
    }
}

/* plain state report: no synthetic clicks, LVGL derives them from press/release */
static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point.x = (lv_coord_t)g_in.cx;
    data->point.y = (lv_coord_t)g_in.cy;
    data->state = g_in.click ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void pointer_init(lv_obj_t *cursor) {
    g_in.cursor = cursor;
    g_in.cx = lv_disp_get_hor_res(NULL) / 2;
    g_in.cy = lv_disp_get_ver_res(NULL) / 2;
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = read_cb;
    g_in.ptr_indev = lv_indev_drv_register(&drv);
}
