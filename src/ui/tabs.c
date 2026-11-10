/* ui/tabs.c - animated tabview with a sliding active pill (see tabs.h). */
#include "tabs.h"

#include <stdio.h>

#include "../input/input.h"
#include "../log.h"
#include "theme.h"

#define MAX_TABS 8

static lv_obj_t *g_tv;
static lv_group_t *g_group;
static const char *g_names[MAX_TABS];
static int g_count;
static float g_pos;   /* content scroll position in pages, 0..g_count-1 */

static void pill_area(lv_obj_t *bar, lv_area_t *out) {
    lv_btnmatrix_t *bm = (lv_btnmatrix_t *)bar;
    int last = bm->btn_cnt - 1, i = (int)g_pos;
    if (i < 0) i = 0;
    if (i > last) i = last;
    int j = i < last ? i + 1 : last;
    float f = g_pos - (float)i;
    const lv_area_t *a = &bm->button_areas[i], *b = &bm->button_areas[j];
    out->x1 = bar->coords.x1 + a->x1 + (lv_coord_t)((float)(b->x1 - a->x1) * f);
    out->x2 = bar->coords.x1 + a->x2 + (lv_coord_t)((float)(b->x2 - a->x2) * f);
    out->y1 = bar->coords.y1 + a->y1;
    out->y2 = bar->coords.y1 + a->y2;
}

/* the theme styles the bar's track (LV_PART_CUSTOM_FIRST) and pill (LV_PART_INDICATOR) */
static void bar_draw_cb(lv_event_t *e) {
    lv_obj_t *bar = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN_BEGIN) {
        /* under the tab labels: track, then the pill */
        lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        lv_obj_init_draw_rect_dsc(bar, LV_PART_CUSTOM_FIRST, &d);
        lv_draw_rect(ctx, &d, &bar->coords);
        lv_area_t pill;
        pill_area(bar, &pill);
        lv_draw_rect_dsc_init(&d);
        lv_obj_init_draw_rect_dsc(bar, LV_PART_INDICATOR, &d);
        lv_draw_rect(ctx, &d, &pill);
    } else {
        /* labels cross-fade as the pill passes */
        lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
        if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc) return;
        float dist = g_pos - (float)dsc->id;
        if (dist < 0) dist = -dist;
        lv_opa_t mix = dist >= 1.0f ? 0 : (lv_opa_t)((1.0f - dist) * 255);
        dsc->label_dsc->color = lv_color_mix(lv_color_white(), C(COL_MUTED), mix);
    }
}

static void content_scroll_cb(lv_event_t *e) {
    lv_obj_t *cont = lv_event_get_target(e);
    lv_coord_t step = lv_obj_get_content_width(cont) + lv_obj_get_style_pad_column(cont, 0);
    float pos = step > 0 ? (float)lv_obj_get_scroll_x(cont) / (float)step : 0;
    if (pos < 0) pos = 0;
    if (pos > (float)(g_count - 1)) pos = (float)(g_count - 1);
    if (pos != g_pos) {
        g_pos = pos;
        lv_obj_invalidate(lv_tabview_get_tab_btns(g_tv));
    }
}

/* tab bar click/ENTER: replace the tabview's instant jump with the slide */
static void bar_clicked_cb(lv_event_t *e) {
    uint16_t id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (id != LV_BTNMATRIX_BTN_NONE) tabs_go(id, "tab bar");
    lv_event_stop_processing(e);
}

static void bar_key_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return;
    uint16_t id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (id != LV_BTNMATRIX_BTN_NONE) tabs_go(id, "keypad");
}

lv_obj_t *tabs_create(lv_obj_t *parent, const char *const names[], const char *const icons[], int count) {
    g_count = count < MAX_TABS ? count : MAX_TABS;
    g_tv = lv_tabview_create(parent, LV_DIR_TOP, TABBAR_H);
    lv_obj_set_width(g_tv, LV_PCT(100));
    lv_obj_set_flex_grow(g_tv, 1);
    lv_obj_set_style_pad_hor(g_tv, PAGE_MARGIN, 0);
    lv_obj_set_style_pad_top(g_tv, SP_XS, 0);
    lv_obj_set_style_pad_bottom(g_tv, SP_XL, 0);
    lv_obj_set_style_pad_row(g_tv, SP_XL, 0);
    lv_obj_set_scrollbar_mode(g_tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *bar = lv_tabview_get_tab_btns(g_tv);
    lv_obj_set_width(bar, TABBAR_W);
    lv_obj_add_event_cb(bar, bar_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(bar, bar_clicked_cb, LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(bar, bar_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(bar, bar_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    lv_obj_t *content = lv_tabview_get_content(g_tv);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);   /* no swipe */
    lv_obj_add_event_cb(content, content_scroll_cb, LV_EVENT_SCROLL, NULL);

    for (int i = 0; i < g_count; i++) {
        char label[48];
        snprintf(label, sizeof(label), "%s   %s", icons[i], names[i]);
        g_names[i] = names[i];
        lv_obj_t *p = lv_tabview_add_tab(g_tv, label);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_gap(p, SP_XL, 0);
    }
    lv_btnmatrix_set_selected_btn(bar, 0);   /* so the first remote RIGHT moves to the second tab */
    return g_tv;
}

lv_obj_t *tabs_page(int index) { return lv_obj_get_child(lv_tabview_get_content(g_tv), index); }
lv_obj_t *tabs_bar(void) { return lv_tabview_get_tab_btns(g_tv); }
int tabs_active(void) { return g_tv ? (int)lv_tabview_get_tab_act(g_tv) : -1; }

int tabs_page_of(lv_obj_t *o) {
    lv_obj_t *cont = lv_tabview_get_content(g_tv);
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_get_parent(o) == cont) return (int)lv_obj_get_index(o);
    return -1;
}

void tabs_go(int index, const char *source) {
    if (index < 0 || index >= g_count) return;
    if (index != tabs_active()) log_event(LOG_INFO, "TAB", "switched to %s (%s)", g_names[index], source);
    lv_btnmatrix_set_selected_btn(tabs_bar(), (uint16_t)index);
    lv_tabview_set_act(g_tv, (uint32_t)index, LV_ANIM_ON);
}

void tabs_step(int dir, const char *source) {
    if (!g_tv || !g_count) return;
    int index = (tabs_active() + g_count + (dir < 0 ? -1 : 1)) % g_count;
    tabs_go(index, source);
    if (!g_group || tabs_page_of(lv_group_get_focused(g_group)) == index) return;
    lv_obj_t *target = tabs_bar(), **o;   /* a page without controls (Log) falls back to the tab bar */
    _LV_LL_READ(&g_group->obj_ll, o) {
        if (!lv_obj_has_state(*o, LV_STATE_DISABLED) && tabs_page_of(*o) == index) { target = *o; break; }
    }
    lv_group_focus_obj(target);
    if (input_keypad_mode()) lv_obj_add_state(target, LV_STATE_FOCUS_KEY);   /* show the ring */
}

static void group_focus_cb(lv_group_t *g) {
    int p = tabs_page_of(lv_group_get_focused(g));
    if (p >= 0 && p != tabs_active()) tabs_go(p, "focus");
}

void tabs_bind_group(lv_group_t *group) {
    g_group = group;
    lv_group_set_focus_cb(group, group_focus_cb);
}
