/* ui/widgets.c - reusable, themed UI components (see widgets.h). */
#include "widgets.h"


/* Layout boxes never clip: a focus ring, slider knob or button shadow may
 * reach past them, and the enclosing card (padded for it) does the clipping. */
void ui_no_clip(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_clip_corner(o, false, 0);
}
lv_obj_t *ui_box(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap) {
    lv_obj_t *o = lv_obj_create(parent);
    ui_no_clip(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}
lv_obj_t *ui_spacer(lv_obj_t *parent) {
    lv_obj_t *o = ui_box(parent, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(o, 1, 1);
    lv_obj_set_flex_grow(o, 1);
    return o;
}
lv_obj_t *ui_text(lv_obj_t *parent, txt_kind_t kind, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &st_txt[kind], 0);
    lv_label_set_text(l, text);
    return l;
}
lv_obj_t *ui_card(lv_obj_t *parent, lv_style_t *variant) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_add_style(c, &st_card, 0);
    if (variant) lv_obj_add_style(c, variant, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    return c;
}
/* rounded square holding a symbol or short text (logo, console badge) */
lv_obj_t *ui_tile(lv_obj_t *parent, lv_style_t *style, const char *text, lv_coord_t size,
                   const lv_font_t *font) {
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_add_style(t, style, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(t, size, size);
    lv_obj_t *l = lv_label_create(t);
    lv_label_set_text(l, text);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    return t;
}
/* card heading: soft icon tile + title + optional caption; returns the row */
lv_obj_t *ui_card_header(lv_obj_t *card, const char *icon, const char *title, const char *caption) {
    lv_obj_t *row = ui_box(card, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(row, LV_PCT(100));
    ui_tile(row, &st_badge_soft, icon, TILE_SM, NULL);
    lv_obj_t *col = ui_box(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(col, 1);
    ui_text(col, TXT_HEADING, title);
    if (caption) ui_text(col, TXT_CAPTION, caption);
    return row;
}
/* settings row: title/caption on the left, the caller adds the control */
lv_obj_t *ui_setting_row(lv_obj_t *parent, const char *title, const char *caption) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_style(row, &st_row, 0);
    ui_no_clip(row);   /* controls sit flush with the row's right edge */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *col = ui_box(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(col, 1);
    ui_text(col, TXT_BODY, title);
    if (caption) ui_text(col, TXT_CAPTION, caption);
    return row;
}
void ui_last_row(lv_obj_t *row) { lv_obj_set_style_border_width(row, 0, 0); }

lv_obj_t *ui_btn(lv_obj_t *parent, btn_kind_t kind, const char *icon, const char *text) {
    lv_obj_t *b = lv_btn_create(parent);
    if (kind == BTN_SECONDARY) lv_obj_add_style(b, &st_btn_secondary, 0);
    else if (kind == BTN_DANGER) lv_obj_add_style(b, &st_btn_danger, 0);
    else if (kind == BTN_GHOST) lv_obj_add_style(b, &st_btn_ghost, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (icon) lv_label_set_text(lv_label_create(b), icon);
    if (text) lv_label_set_text(lv_label_create(b), text);
    return b;
}
lv_obj_t *ui_led(lv_obj_t *parent, uint32_t col, lv_coord_t size) {
    lv_obj_t *led = lv_led_create(parent);
    lv_obj_set_size(led, size, size);
    lv_led_set_color(led, C(col));
    lv_led_on(led);
    return led;
}
lv_obj_t *ui_chip(lv_obj_t *parent, const char *icon, const char *text, lv_obj_t **label_out) {
    lv_obj_t *chip = ui_box(parent, LV_FLEX_FLOW_ROW, SP_S);
    lv_obj_add_style(chip, &st_chip, 0);
    if (icon) {
        lv_obj_t *i = lv_label_create(chip);
        lv_label_set_text(i, icon);
        lv_obj_set_style_text_color(i, C(COL_ACCENT_HI), 0);
    }
    lv_obj_t *l = lv_label_create(chip);
    lv_label_set_text(l, text);
    if (label_out) *label_out = l;
    return chip;
}
static void led_pulse_cb(void *led, int32_t v) { lv_led_set_brightness(led, (uint8_t)v); }
void ui_status_set(ui_status_t *s, uint32_t col, const char *text, bool pulse) {
    lv_anim_del(s->led, led_pulse_cb);
    lv_led_set_color(s->led, C(col));
    lv_led_set_brightness(s->led, LV_LED_BRIGHT_MAX);
    lv_label_set_text(s->label, text);
    if (pulse) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s->led);
        lv_anim_set_exec_cb(&a, led_pulse_cb);
        lv_anim_set_values(&a, LV_LED_BRIGHT_MAX, LV_LED_BRIGHT_MIN);
        lv_anim_set_time(&a, 600);
        lv_anim_set_playback_time(&a, 600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}
/* status chip: glowing LED + label */
void ui_status_init(ui_status_t *s, lv_obj_t *parent, uint32_t col, const char *text) {
    s->chip = ui_box(parent, LV_FLEX_FLOW_ROW, SP_M);
    lv_obj_add_style(s->chip, &st_chip, 0);
    lv_obj_set_style_pad_hor(s->chip, SP_XL - 4, 0);
    s->led = ui_led(s->chip, col, 14);
    s->label = lv_label_create(s->chip);
    ui_status_set(s, col, text, false);
}
/* controller button hint: round glyph + caption */
void ui_hint(lv_obj_t *parent, const char *glyph, uint32_t glyph_col, const char *text) {
    lv_obj_t *row = ui_box(parent, LV_FLEX_FLOW_ROW, SP_S + 2);
    lv_obj_t *g = ui_tile(row, &st_glyph, glyph, 40, NULL);
    lv_obj_set_width(g, LV_SIZE_CONTENT);   /* round for one glyph, pill for "L1 R1" */
    lv_obj_set_style_min_width(g, 40, 0);
    lv_obj_set_style_pad_hor(g, SP_M, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(g, 0), C(glyph_col), 0);
    ui_text(row, TXT_CAPTION, text);
}
/* LVGL sliders step by 1 per key press; make remote LEFT/RIGHT move by `step` */
static void slider_step_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return;
    lv_obj_t *s = lv_event_get_target(e);
    int32_t step = (int32_t)(intptr_t)lv_event_get_user_data(e) - 1;
    lv_slider_set_value(s, lv_slider_get_value(s) + (key == LV_KEY_RIGHT ? step : -step), LV_ANIM_ON);
    lv_event_send(s, LV_EVENT_VALUE_CHANGED, NULL);
}
/* slider + right-aligned value label, sized like the other row controls */
lv_obj_t *ui_slider_control(lv_obj_t *row, int32_t min, int32_t max, int32_t val, int32_t key_step,
                            lv_event_cb_t changed_cb, lv_obj_t **value_label) {
    lv_obj_t *ctl = ui_box(row, LV_FLEX_FLOW_ROW, SP_XL);
    lv_obj_set_width(ctl, CTRL_W);
    lv_obj_t *s = lv_slider_create(ctl);
    lv_obj_set_height(s, 10);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_ext_click_area(s, SLIDER_HIT);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, slider_step_cb, LV_EVENT_KEY, (void *)(intptr_t)key_step);
    lv_obj_add_event_cb(s, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    *value_label = ui_text(ctl, TXT_BODY, "");
    lv_obj_set_width(*value_label, SLIDER_VAL_W);
    lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, 0);
    return s;
}

/* PlayStation face buttons as a faint diamond: triangle, circle, cross, square */
#define DI (DECO_W / 2 + 4)
static const lv_point_t g_deco_tri[] = {
    { DI, DECO_SIZE - 12 }, { DECO_SIZE / 2, 10 }, { DECO_SIZE - DI, DECO_SIZE - 12 }, { DI, DECO_SIZE - 12 } };
static const lv_point_t g_deco_x1[] = { { 14, 14 }, { DECO_SIZE - 14, DECO_SIZE - 14 } };
static const lv_point_t g_deco_x2[] = { { DECO_SIZE - 14, 14 }, { 14, DECO_SIZE - 14 } };
static lv_obj_t *deco_cell(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, DECO_SIZE, DECO_SIZE);
    lv_obj_set_pos(o, x, y);
    return o;
}
/* solid colours: translucent strokes would double up where segments overlap */
static void deco_line(lv_obj_t *cell, const lv_point_t *pts, uint16_t n, uint32_t col) {
    lv_obj_t *l = lv_line_create(cell);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, DECO_W, 0);
    lv_obj_set_style_line_color(l, C(col), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}
static void deco_ring(lv_obj_t *cell, lv_coord_t size, lv_coord_t radius, uint32_t col) {
    lv_obj_t *r = lv_obj_create(cell);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, size, size);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_border_width(r, DECO_W, 0);
    lv_obj_set_style_border_color(r, C(col), 0);
    lv_obj_center(r);
}
lv_obj_t *ui_ps_buttons(lv_obj_t *parent) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(d, 2 * DECO_GAP + DECO_SIZE, 2 * DECO_GAP + DECO_SIZE);
    deco_line(deco_cell(d, DECO_GAP, 0), g_deco_tri, 4, COL_DECO_TRI);
    deco_ring(deco_cell(d, 2 * DECO_GAP, DECO_GAP), DECO_SIZE - 8, R_PILL, COL_DECO_CIRCLE);
    lv_obj_t *x = deco_cell(d, DECO_GAP, 2 * DECO_GAP);
    deco_line(x, g_deco_x1, 2, COL_DECO_CROSS);
    deco_line(x, g_deco_x2, 2, COL_DECO_CROSS);
    deco_ring(deco_cell(d, 0, DECO_GAP), DECO_SIZE - 18, 6, COL_DECO_SQUARE);
    return d;
}

lv_obj_t *ui_dropdown(lv_obj_t *parent, const char *options, uint16_t selected, lv_event_cb_t changed_cb) {
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_dropdown_set_options(d, options);
    lv_dropdown_set_selected(d, selected);
    lv_obj_set_width(d, CTRL_W);
    if (changed_cb) lv_obj_add_event_cb(d, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return d;
}

lv_obj_t *ui_cursor_create(void) {
    lv_obj_t *c = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, CURSOR_D, CURSOR_D);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, C(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_40, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_set_style_shadow_color(c, C(COL_ACCENT_HI), 0);
    lv_obj_set_style_shadow_width(c, 18, 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(c, C(COL_ACCENT_HI), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(c, LV_OPA_90, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(c, -4, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(c, -4, LV_STATE_PRESSED);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *dot = lv_obj_create(c);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(dot);
    return c;
}
