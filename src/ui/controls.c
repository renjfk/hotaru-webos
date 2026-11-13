/* ui/controls.c - focus-driven building blocks (see controls.h). */
#include "controls.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motion.h"

/* ---------- plain objects ---------- */

lv_obj_t *ctl_obj(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    return o;
}

lv_obj_t *ctl_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius, uint32_t col, lv_opa_t opa) {
    lv_obj_t *o = ctl_obj(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, C(col), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

void ctl_grad(lv_obj_t *o, uint32_t top, uint32_t bottom) {
    lv_obj_set_style_bg_color(o, C(top), 0);
    lv_obj_set_style_bg_grad_color(o, C(bottom), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

lv_obj_t *ctl_label(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, C(col), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *ctl_dot(lv_obj_t *parent, lv_coord_t d, uint32_t col) {
    lv_obj_t *o = ctl_rect(parent, d, d, LV_RADIUS_CIRCLE, col, LV_OPA_COVER);
    lv_obj_set_style_shadow_color(o, C(col), 0);
    lv_obj_set_style_shadow_width(o, d, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
    return o;
}

lv_obj_t *ctl_row(lv_obj_t *parent, lv_coord_t gap) {
    lv_obj_t *r = ctl_obj(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

void ctl_outline(lv_obj_t *o, lv_coord_t width, lv_coord_t pad) {
    lv_obj_set_style_outline_width(o, width, 0);
    lv_obj_set_style_outline_pad(o, pad, 0);
    lv_obj_set_style_outline_color(o, lv_color_white(), 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_TRANSP, 0);
}

static void margin_cb(lv_event_t *e) {
    lv_coord_t want = (lv_coord_t)(intptr_t)lv_event_get_user_data(e);
    lv_coord_t *cur = lv_event_get_param(e);
    if (*cur < want) *cur = want;
}

void ctl_margin(lv_obj_t *o, lv_coord_t px) {
    lv_obj_add_event_cb(o, margin_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, (void *)(intptr_t)px);
    lv_obj_refresh_ext_draw_size(o);
}

/* ---------- glow: a white radial falloff written straight into a canvas, shared ---------- */

static lv_obj_t *g_glow_cv;

static const lv_img_dsc_t *glow_img(void) {
    if (g_glow_cv) return lv_canvas_get_img(g_glow_cv);
    lv_obj_t *holder = ctl_obj(lv_layer_sys()); /* hidden owner, lives as long as the display */
    lv_obj_add_flag(holder, LV_OBJ_FLAG_HIDDEN);
    g_glow_cv = lv_canvas_create(holder);
    lv_color_t *px = malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(CTL_GLOW_D, CTL_GLOW_D));
    lv_canvas_set_buffer(g_glow_cv, px, CTL_GLOW_D, CTL_GLOW_D, LV_IMG_CF_TRUE_COLOR_ALPHA);
    float r = CTL_GLOW_D / 2.0f;
    for (int y = 0; y < CTL_GLOW_D; y++)
        for (int x = 0; x < CTL_GLOW_D; x++) {
            float dx = (x + 0.5f - r) / r, dy = (y + 0.5f - r) / r, d = sqrtf(dx * dx + dy * dy);
            float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
            lv_color_t c = lv_color_white();
            c.ch.alpha = (uint8_t)lroundf(a * 255.0f);
            px[y * CTL_GLOW_D + x] = c;
        }
    return lv_canvas_get_img(g_glow_cv);
}

lv_obj_t *ctl_glow(lv_obj_t *parent, uint32_t col, lv_opa_t opa) {
    lv_obj_t *img = lv_img_create(parent);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(img, glow_img());
    lv_obj_set_style_img_recolor(img, C(col), 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_img_opa(img, opa, 0);
    return img;
}

/* ---------- PlayStation glyphs ---------- */

const uint32_t ctl_ps_col[4] = { COL_PS_TRIANGLE, COL_PS_CIRCLE, COL_PS_CROSS, COL_PS_SQUARE };

static void free_user_cb(lv_event_t *e) { free(lv_event_get_user_data(e)); }

static void glyph_line(lv_obj_t *parent, const lv_point_t *pts, int n, uint32_t col, lv_coord_t w) {
    lv_point_t *p = malloc(sizeof(lv_point_t) * (size_t)n); /* lv_line keeps the pointer */
    memcpy(p, pts, sizeof(lv_point_t) * (size_t)n);
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_line_set_points(l, p, (uint16_t)n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, C(col), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, free_user_cb, LV_EVENT_DELETE, p);
}

#define P(a, b) { (lv_coord_t)((a) * size / 100), (lv_coord_t)((b) * size / 100) } /* a point in % of the size */
lv_obj_t *ctl_ps_glyph(lv_obj_t *parent, ps_glyph_t g, lv_coord_t size, uint32_t col, lv_coord_t stroke) {
    lv_obj_t *box = ctl_obj(parent);
    lv_obj_set_size(box, size, size);
    if (g == PS_CIRCLE || g == PS_SQUARE) {
        lv_coord_t d = (lv_coord_t)(size * (g == PS_CIRCLE ? 60 : 52) / 100);
        lv_obj_t *o = ctl_obj(box);
        lv_obj_set_size(o, d, d);
        lv_obj_center(o);
        lv_obj_set_style_radius(o, g == PS_CIRCLE ? LV_RADIUS_CIRCLE : size / 16, 0);
        lv_obj_set_style_border_width(o, stroke, 0);
        lv_obj_set_style_border_color(o, C(col), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    } else if (g == PS_CROSS) {
        lv_point_t a[] = { P(25, 25), P(75, 75) }, b[] = { P(75, 25), P(25, 75) };
        glyph_line(box, a, 2, col, stroke);
        glyph_line(box, b, 2, col, stroke);
    } else {
        lv_point_t t[] = { P(50, 20), P(19, 72), P(81, 72), P(50, 20) };
        glyph_line(box, t, 4, col, stroke);
    }
    return box;
}
#undef P

/* ---------- hints ---------- */

lv_obj_t *ctl_hints(lv_obj_t *parent) { return ctl_row(parent, SP_XL); }

static void hint_chip_style(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, C(COL_SHEET), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_70, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_20, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
}

void ctl_hint(lv_obj_t *row, ps_glyph_t g, const char *text) {
    lv_obj_t *it = ctl_row(row, SP_M);
    lv_obj_t *chip = ctl_obj(it);
    lv_obj_set_size(chip, 40, 40);
    hint_chip_style(chip);
    lv_obj_center(ctl_ps_glyph(chip, g, 36, ctl_ps_col[g], 3)); /* inside the 2 px border */
    ctl_label(it, FONT_CAPTION, COL_SUBTLE, text);
}

static void key_chip(lv_obj_t *parent, const char *key) {
    lv_obj_t *chip = ctl_obj(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_hor(chip, SP_M, 0);
    hint_chip_style(chip);
    lv_obj_center(ctl_label(chip, FONT_SMALL, COL_TEXT, key));
}

void ctl_hint_key(lv_obj_t *row, const char *key, const char *text) {
    lv_obj_t *it = ctl_row(row, SP_M);
    key_chip(it, key);
    ctl_label(it, FONT_CAPTION, COL_SUBTLE, text);
}

void ctl_hint_keys(lv_obj_t *row, const char *a, const char *b, const char *text) {
    lv_obj_t *it = ctl_row(row, SP_M);
    lv_obj_t *keys = ctl_row(it, SP_S);
    key_chip(keys, a);
    ctl_label(keys, FONT_CAPTION, COL_SUBTLE, "+");
    key_chip(keys, b);
    ctl_label(it, FONT_CAPTION, COL_SUBTLE, text);
}

/* ---------- segmented choice ---------- */

#define SEG_PAD       6
#define SEG_H         68
#define SEG_FOCUS_LOW 0xdfe7f5 /* the focused pill's gradient end: frost, slightly cooler */

static void seg_paint(ctl_seg_t *s) {
    for (int i = 0; i < s->n; i++)
        lv_obj_set_style_text_color(s->lbl[i], C(i != s->sel ? COL_MUTED : s->focused ? COL_ON_FROST : COL_TEXT), 0);
    if (s->focused) ctl_grad(s->pill, COL_FROST, SEG_FOCUS_LOW);
    else ctl_grad(s->pill, COL_ACCENT_HI, COL_ACCENT);
    lv_obj_set_style_border_opa(s->box, s->focused ? LV_OPA_40 : LV_OPA_10, 0);
}

void ctl_seg_create(ctl_seg_t *s, lv_obj_t *parent, const char *const *opts, int n, int sel, lv_coord_t seg_w) {
    memset(s, 0, sizeof(*s));
    s->n = n < CTL_SEG_MAX ? n : CTL_SEG_MAX;
    s->sel = sel;
    s->seg_w = seg_w;
    s->box = ctl_rect(parent, s->n * seg_w + 2 * SEG_PAD, SEG_H, LV_RADIUS_CIRCLE, COL_SHADE, LV_OPA_30);
    lv_obj_set_style_border_width(s->box, 2, 0);
    lv_obj_set_style_border_color(s->box, lv_color_white(), 0);
    s->pill = ctl_rect(s->box, seg_w, SEG_H - 2 * SEG_PAD, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
    lv_obj_set_pos(s->pill, SEG_PAD + sel * seg_w, SEG_PAD);
    for (int i = 0; i < s->n; i++) {
        s->lbl[i] = ctl_label(s->box, FONT_BODY, COL_MUTED, opts[i]);
        lv_obj_set_width(s->lbl[i], seg_w);
        lv_obj_set_style_text_align(s->lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s->lbl[i], LV_ALIGN_LEFT_MID, SEG_PAD + i * seg_w, 0);
    }
    seg_paint(s);
}

void ctl_seg_select(ctl_seg_t *s, int sel) {
    if (sel < 0 || sel >= s->n || sel == s->sel) return;
    s->sel = sel;
    motion_to(s->pill, MO_X, SEG_PAD + sel * s->seg_w, 340, 0, motion_spring);
    seg_paint(s);
}

void ctl_seg_focus(ctl_seg_t *s, bool on) {
    if (s->focused == on) return;
    s->focused = on;
    seg_paint(s);
}

/* ---------- level bar ---------- */

#define METER_FOCUS 0x9fc8ff /* the focused fill's gradient start: accent lifted towards frost */

static void meter_text(ctl_meter_t *m) { lv_label_set_text_fmt(m->value, m->fmt, m->val); }

void ctl_meter_create(ctl_meter_t *m, lv_obj_t *parent, lv_coord_t w, int min, int max, int val, const char *fmt) {
    memset(m, 0, sizeof(*m));
    m->min = min;
    m->max = max;
    m->val = LV_CLAMP(min, val, max);
    m->w = w;
    m->fmt = fmt;
    m->box = ctl_obj(parent);
    lv_obj_set_size(m->box, w + 200, SEG_H);
    m->track = ctl_rect(m->box, w, 20, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_10);
    lv_obj_align(m->track, LV_ALIGN_LEFT_MID, 0, 0);
    m->fill = ctl_rect(m->track, (lv_coord_t)((m->val - min) * w / (max - min)), 20, LV_RADIUS_CIRCLE, COL_ACCENT,
                       LV_OPA_COVER);
    lv_obj_set_style_bg_grad_dir(m->fill, LV_GRAD_DIR_HOR, 0);
    m->value = ctl_label(m->box, FONT_HEADING, COL_TEXT, "");
    lv_obj_set_width(m->value, 176);
    lv_obj_set_style_text_align(m->value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(m->value, LV_ALIGN_RIGHT_MID, 0, 0);
    meter_text(m);
    ctl_meter_focus(m, false);
}

void ctl_meter_set(ctl_meter_t *m, int val) {
    val = LV_CLAMP(m->min, val, m->max);
    if (val == m->val) return;
    m->val = val;
    meter_text(m);
    motion_to(m->fill, MO_W, (val - m->min) * m->w / (m->max - m->min), 260, 0, NULL);
}

void ctl_meter_focus(ctl_meter_t *m, bool on) {
    lv_obj_set_style_bg_color(m->fill, C(on ? METER_FOCUS : COL_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(m->fill, C(on ? COL_FROST : COL_ACCENT_HI), 0);
    lv_obj_set_style_bg_opa(m->track, on ? LV_OPA_20 : LV_OPA_10, 0);
}

/* ---------- toggle ---------- */

#define TGL_W 112
#define TGL_H 60
#define TGL_K 48

void ctl_toggle_create(ctl_toggle_t *t, lv_obj_t *parent, bool val) {
    t->val = val;
    t->track = ctl_rect(parent, TGL_W, TGL_H, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_20);
    t->on = ctl_rect(t->track, TGL_W, TGL_H, LV_RADIUS_CIRCLE, COL_ACCENT, val ? LV_OPA_COVER : LV_OPA_TRANSP);
    ctl_grad(t->on, COL_ACCENT_HI, COL_ACCENT);
    t->knob = ctl_rect(t->track, TGL_K, TGL_K, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_COVER);
    lv_obj_set_pos(t->knob, val ? TGL_W - TGL_K - 6 : 6, (TGL_H - TGL_K) / 2);
}

void ctl_toggle_set(ctl_toggle_t *t, bool val) {
    t->val = val;
    motion_to(t->knob, MO_X, val ? TGL_W - TGL_K - 6 : 6, 360, 0, motion_spring);
    motion_to(t->on, MO_BG_OPA, val ? LV_OPA_COVER : LV_OPA_TRANSP, 220, 0, NULL);
}

/* ---------- pop-up menu ---------- */

#define MENU_W      560
#define MENU_ROW_H  84
#define MENU_PAD    10
#define MENU_BORDER 2
#define MENU_H(n)   ((n) * MENU_ROW_H + 2 * (MENU_PAD + MENU_BORDER))

static void menu_paint(ctl_menu_t *m) {
    for (int i = 0; i < m->n; i++) {
        bool on = i == m->sel;
        motion_to(m->rows[i], MO_BG_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 140, 0, NULL);
        bool danger = lv_obj_get_user_data(m->rows[i]) != NULL;
        motion_color(m->rows[i], LV_STYLE_TEXT_COLOR, on ? COL_ON_FROST : danger ? COL_DANGER_TEXT : COL_TEXT, 140);
    }
}

static void menu_del_cb(lv_obj_t *card) { lv_obj_del_async(card); }

void ctl_menu_open(ctl_menu_t *m, lv_obj_t *parent, const lv_area_t *anchor, bool up, const ctl_menu_item_t *items,
                   int n, int sel) {
    if (m->card) ctl_menu_close(m);
    m->n = n < CTL_MENU_MAX ? n : CTL_MENU_MAX;
    m->sel = LV_CLAMP(0, sel, m->n - 1);
    m->card = ctl_rect(parent, MENU_W, MENU_H(m->n), 28, COL_SHEET, 248);
    lv_obj_add_flag(m->card, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_border_width(m->card, MENU_BORDER, 0);
    lv_obj_set_style_border_color(m->card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(m->card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(m->card, MENU_PAD, 0);
    for (int i = 0; i < m->n; i++) {
        const ctl_menu_item_t *it = &items[i];
        lv_obj_t *r = m->rows[i] =
            ctl_rect(m->card, MENU_W - 2 * (MENU_PAD + MENU_BORDER), MENU_ROW_H, 20, COL_FROST, LV_OPA_TRANSP);
        lv_obj_set_pos(r, 0, i * MENU_ROW_H);
        lv_obj_set_user_data(r, it->danger ? (void *)r : NULL);
        lv_obj_set_style_text_color(r, C(it->danger ? COL_DANGER_TEXT : COL_TEXT), 0);
        lv_obj_t *ic = lv_label_create(r);
        lv_label_set_text(ic, it->icon ? it->icon : "");
        lv_obj_set_style_text_font(ic, FONT_HEADING, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 22, 0);
        lv_obj_t *t = lv_label_create(r);
        lv_label_set_text(t, it->text);
        lv_obj_set_style_text_font(t, FONT_BODY, 0);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 72, it->detail ? -13 : 0);
        if (it->detail) {
            lv_obj_t *d = lv_label_create(r);
            lv_label_set_text(d, it->detail);
            lv_obj_set_style_text_font(d, FONT_SMALL, 0);
            lv_obj_set_style_text_opa(d, LV_OPA_70, 0);
            lv_obj_align(d, LV_ALIGN_LEFT_MID, 72, 17);
        }
        if (it->checked) {
            lv_obj_t *ck = lv_label_create(r);
            lv_label_set_text(ck, LV_SYMBOL_OK);
            lv_obj_set_style_text_font(ck, FONT_BODY, 0);
            lv_obj_align(ck, LV_ALIGN_RIGHT_MID, -22, 0);
        }
    }
    lv_coord_t h = MENU_H(m->n);
    lv_coord_t x = anchor->x1, y = up ? anchor->y1 - h - SP_M : anchor->y2 + SP_M;
    if (x + MENU_W > LV_HOR_RES - SP_XL) x = LV_HOR_RES - SP_XL - MENU_W;
    lv_obj_set_pos(m->card, x, y);
    motion_from(m->card, MO_OPA, 0, 255, 180, 0, NULL);
    motion_from(m->card, MO_TY, up ? 18 : -18, 0, 260, 0, NULL);
    menu_paint(m);
}

void ctl_menu_close(ctl_menu_t *m) {
    if (!m->card) return;
    lv_obj_t *card = m->card;
    m->card = NULL;
    m->n = 0;
    motion_then(card, MO_OPA, 0, 140, lv_anim_path_ease_in, menu_del_cb);
}

void ctl_menu_move(ctl_menu_t *m, int dir) {
    if (!m->card) return;
    int s = LV_CLAMP(0, m->sel + dir, m->n - 1);
    if (s == m->sel) return;
    m->sel = s;
    menu_paint(m);
}

bool ctl_menu_is_open(const ctl_menu_t *m) { return m->card != NULL; }

/* ---------- pills ---------- */

lv_obj_t *ctl_pill(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *b = ctl_rect(parent, w, h, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_20);
    lv_obj_set_style_text_color(b, C(COL_TEXT), 0);
    return b;
}

lv_obj_t *ctl_button(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *icon, const char *text) {
    lv_obj_t *b = ctl_pill(parent, w, h);
    lv_obj_t *row = ctl_row(b, 16);
    if (icon) {
        lv_obj_t *i = lv_label_create(row);
        lv_label_set_text(i, icon);
        lv_obj_set_style_text_font(i, FONT_TITLE, 0);
    }
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, FONT_HEADING, 0);
    lv_obj_center(row);
    return b;
}

void ctl_button_focus(lv_obj_t *b, bool on) {
    motion_to(b, MO_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, NULL);
    motion_color(b, LV_STYLE_TEXT_COLOR, on ? COL_ON_FROST : COL_TEXT, 200);
    motion_to(b, MO_GROW, on ? 6 : 0, 300, 0, motion_spring);
}

void ctl_button_danger_focus(lv_obj_t *b, bool on) {
    motion_color(b, LV_STYLE_BG_COLOR, on ? COL_ERR : COL_FROST, 200);
    motion_to(b, MO_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, NULL);
    motion_color(b, LV_STYLE_TEXT_COLOR, on ? COL_FROST : COL_DANGER_TEXT, 200);
    motion_to(b, MO_GROW, on ? 6 : 0, 300, 0, motion_spring);
}

void ctl_shake(lv_obj_t *o) { motion_from(o, MO_TX, 18, 0, 420, 0, motion_shake); }

static void frost(lv_obj_t *o, lv_opa_t opa) {
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, opa, 0);
}

/* ---------- number pad ---------- */

#define NP_REST 30 /* a key's frost */

static const char g_np_chars[CTL_NP_KEYS] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b' };

static lv_coord_t np_gap(lv_coord_t k) { return k / 5; }
lv_coord_t ctl_numpad_w(lv_coord_t k) { return 3 * k + 2 * np_gap(k); }
lv_coord_t ctl_numpad_h(lv_coord_t k) { return 4 * k + 3 * np_gap(k); }

static void np_paint(ctl_numpad_t *p) {
    for (int i = 0; i < CTL_NP_KEYS; i++) {
        bool on = p->focused && i == p->sel;
        motion_to(p->key[i], MO_BG_OPA, on ? LV_OPA_COVER : NP_REST, 160, 0, NULL);
        motion_color(p->key[i], LV_STYLE_TEXT_COLOR, on ? COL_ON_FROST : COL_TEXT, 160);
        motion_to(p->key[i], MO_GROW, on ? 8 : 0, 300, 0, motion_spring);
    }
}

void ctl_numpad_create(ctl_numpad_t *p, lv_obj_t *parent, lv_coord_t k, bool dot) {
    memset(p, 0, sizeof(*p));
    p->dot = dot;
    p->box = ctl_obj(parent);
    lv_obj_set_size(p->box, ctl_numpad_w(k), ctl_numpad_h(k));
    for (int i = 0; i < CTL_NP_KEYS; i++) {
        lv_obj_t *key = p->key[i] = ctl_rect(p->box, k, k, LV_RADIUS_CIRCLE, COL_FROST, NP_REST);
        frost(key, LV_OPA_10);
        lv_obj_set_pos(key, (i % 3) * (k + np_gap(k)), (i / 3) * (k + np_gap(k)));
        lv_obj_set_style_text_color(key, C(COL_TEXT), 0);
        char s[2] = { g_np_chars[i], 0 };
        lv_obj_t *l = lv_label_create(key);
        lv_label_set_text(l, i == 11 ? LV_SYMBOL_BACKSPACE : s);
        lv_obj_set_style_text_font(l, i == 11 ? FONT_HEADING : FONT_TITLE, 0);
        lv_obj_center(l);
        if (i == 9 && !dot) lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
    }
    np_paint(p);
}

void ctl_numpad_focus(ctl_numpad_t *p, bool on) {
    p->focused = on;
    np_paint(p);
}

void ctl_numpad_select(ctl_numpad_t *p, int index) {
    if (index < 0 || index >= CTL_NP_KEYS || (index == 9 && !p->dot)) return;
    p->sel = index;
    np_paint(p);
}

bool ctl_numpad_move(ctl_numpad_t *p, uint32_t k) {
    int r = p->sel / 3, c = p->sel % 3;
    if (k == LV_KEY_LEFT) c--;
    else if (k == LV_KEY_RIGHT) c++;
    else if (k == LV_KEY_UP) r--;
    else if (k == LV_KEY_DOWN) r++;
    else return false;
    if (r < 0 || r > 3 || c < 0 || c > 2) return false;
    int n = r * 3 + c;
    if (n == 9 && !p->dot) { /* the empty corner: step over it, or stop at the edge */
        if (k != LV_KEY_DOWN) return false;
        n = 10;
    }
    p->sel = n;
    np_paint(p);
    return true;
}

char ctl_numpad_press(ctl_numpad_t *p) {
    motion_from(p->key[p->sel], MO_GROW, -6, 8, 260, 0, motion_spring);
    return g_np_chars[p->sel];
}

/* ---------- digit boxes ---------- */

#define DIGIT_GAP   14
#define DIGIT_SPLIT 30

void ctl_digits_create(ctl_digits_t *d, lv_obj_t *parent, int n, lv_coord_t cell_w, lv_coord_t cell_h) {
    memset(d, 0, sizeof(*d));
    d->n = n > CTL_DIGITS_MAX ? CTL_DIGITS_MAX : n;
    lv_coord_t split = d->n == 8 ? DIGIT_SPLIT : 0;
    d->box = ctl_obj(parent);
    lv_obj_set_size(d->box, d->n * cell_w + (d->n - 1) * DIGIT_GAP + split, cell_h);
    for (int i = 0; i < d->n; i++) {
        lv_obj_t *c = d->cell[i] = ctl_rect(d->box, cell_w, cell_h, 22, COL_FROST, 16);
        frost(c, LV_OPA_20);
        lv_obj_set_pos(c, i * (cell_w + DIGIT_GAP) + (d->n == 8 && i >= 4 ? split : 0), 0);
        d->digit[i] = ctl_label(c, FONT_HERO, COL_TEXT, "");
        lv_obj_center(d->digit[i]);
    }
}

void ctl_digits_show(ctl_digits_t *d, const char *text, bool typing, bool error) {
    int len = (int)strlen(text);
    for (int i = 0; i < d->n; i++) {
        bool has = i < len, cur = typing && i == len;
        char s[2] = { has ? text[i] : 0, 0 };
        lv_label_set_text(d->digit[i], s);
        motion_to(d->cell[i], MO_BG_OPA, has ? 46 : 16, 160, 0, NULL);
        lv_obj_set_style_border_color(d->cell[i], C(error ? COL_ERR : cur ? COL_ACCENT_HI : COL_FROST), 0);
        lv_obj_set_style_border_width(d->cell[i], error || cur ? 4 : 2, 0);
        motion_to(d->cell[i], MO_BORDER_OPA, error || cur ? LV_OPA_COVER : LV_OPA_20, 160, 0, NULL);
    }
}

/* ---------- progress ring ---------- */

lv_obj_t *ctl_ring(lv_obj_t *parent, lv_coord_t d, uint32_t col) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, 14, 0);
    lv_obj_set_style_arc_color(a, lv_color_white(), 0);
    lv_obj_set_style_arc_opa(a, 30, 0);
    lv_obj_set_style_arc_width(a, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, C(col), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

static void ring_exec(void *ring, int32_t v) { lv_arc_set_value(ring, (int16_t)v); }

void ctl_ring_set(lv_obj_t *ring, int pct) {
    pct = LV_CLAMP(0, pct, 100);
    lv_anim_del(ring, ring_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, ring_exec);
    lv_anim_set_values(&a, lv_arc_get_value(ring), pct);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ---------- text field ---------- */

lv_obj_t *ctl_field(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *placeholder, uint32_t max_len) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_remove_style_all(ta);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_one_line(ta, true); /* sizes it to its content: the size comes after */
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, max_len);
    lv_obj_set_style_bg_color(ta, C(COL_FROST), 0);
    lv_obj_set_style_bg_opa(ta, 26, 0);
    lv_obj_set_style_radius(ta, LV_RADIUS_CIRCLE, 0);
    frost(ta, LV_OPA_20);
    lv_obj_set_style_pad_hor(ta, 34, 0);
    lv_obj_set_style_pad_ver(ta, (h - lv_font_get_line_height(FONT_HEADING)) / 2, 0);
    lv_obj_set_style_text_font(ta, FONT_HEADING, 0);
    lv_obj_set_style_text_color(ta, C(COL_TEXT), 0);
    lv_obj_set_style_text_color(ta, C(COL_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_width(ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, C(COL_ACCENT_HI), LV_PART_CURSOR);
    lv_obj_set_style_anim_time(ta, 500, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    ctl_outline(ta, 5, 6);
    return ta;
}

void ctl_field_focus(lv_obj_t *ta, bool focused, bool typing) {
    motion_to(ta, MO_OUTLINE_OPA, focused || typing ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
    if (typing == lv_obj_has_state(ta, LV_STATE_FOCUSED)) return;
    if (typing) {
        lv_obj_add_state(ta, LV_STATE_FOCUSED);
        lv_event_send(ta, LV_EVENT_FOCUSED, NULL); /* the cursor blinks */
    } else {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

/* ---------- keyboard ---------- */

/* clang-format off */
static const char *g_kb_lower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "-", "_", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ".", LV_SYMBOL_OK, "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", "+", "/", "=", "" };
static const char *g_kb_upper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "-", "_", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", ".", LV_SYMBOL_OK, "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", "+", "/", "=", "" };
#define KB_SPECIAL (LV_BTNMATRIX_CTRL_CHECKED | LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG)
static const lv_btnmatrix_ctrl_t g_kb_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KB_SPECIAL | 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KB_SPECIAL | 2,
    KB_SPECIAL | LV_BTNMATRIX_CTRL_RECOLOR | 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
/* clang-format on */
#define KB_FIRST_LETTER 11 /* "q" */
#define KB_SHIFT_BTN    34 /* "ABC" / "abc": its button id (no "\n"), and where the maps hold its text */
#define KB_SHIFT_MAP    37
/* with a controller the shift key also names the triggers that do the same, greyed (#COL_DIM) */
#define KB_SHIFT_LOWER_PAD "ABC  #5c6884 L2/R2#"
#define KB_SHIFT_UPPER_PAD "abc  #5c6884 L2/R2#"

lv_obj_t *ctl_keyboard(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_remove_style_all(kb);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(kb, w, h + 40); /* the lower corners sit off-screen */
    lv_obj_set_style_bg_color(kb, C(COL_SHEET), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(kb, 40, 0);
    lv_obj_set_style_border_width(kb, 2, 0);
    lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_opa(kb, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(kb, 22, 0);
    lv_obj_set_style_pad_bottom(kb, 62, 0);
    lv_obj_set_style_pad_row(kb, 12, 0);
    lv_obj_set_style_pad_column(kb, 12, 0);
    lv_obj_set_style_bg_color(kb, C(COL_FROST), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, 26, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C(COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, FONT_HEADING, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, 12, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, C(COL_SUBTLE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb, C(COL_ON_FROST), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb, C(COL_ON_FROST), LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, g_kb_lower, g_kb_ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, g_kb_upper, g_kb_ctrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    return kb;
}

void ctl_keyboard_focus(lv_obj_t *kb, lv_obj_t *field) {
    lv_keyboard_set_textarea(kb, field);
    if (field) {
        lv_obj_add_state(kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_btnmatrix_set_selected_btn(kb, KB_FIRST_LETTER);
    } else {
        lv_obj_clear_state(kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }
}

void ctl_keyboard_key(lv_obj_t *kb, uint32_t key) {
    if (key == LV_KEY_ENTER && lv_btnmatrix_get_selected_btn(kb) == KB_SHIFT_BTN) {
        ctl_keyboard_shift(kb);       /* its text may carry the trigger hint, which LVGL's keyboard does not know */
    } else if (key == LV_KEY_ENTER) { /* typing keys act on the press, OK on the release */
        lv_event_send(kb, LV_EVENT_PRESSED, NULL);
        if (lv_obj_is_valid(kb)) lv_event_send(kb, LV_EVENT_RELEASED, NULL);
    } else {
        lv_event_send(kb, LV_EVENT_KEY, &key);
    }
}

void ctl_keyboard_shift(lv_obj_t *kb) {
    bool upper = lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_UPPER;
    lv_keyboard_set_mode(kb, upper ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_TEXT_UPPER);
}

void ctl_keyboard_pad(lv_obj_t *kb, bool pad) {
    g_kb_lower[KB_SHIFT_MAP] = pad ? KB_SHIFT_LOWER_PAD : "ABC";
    g_kb_upper[KB_SHIFT_MAP] = pad ? KB_SHIFT_UPPER_PAD : "abc";
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, g_kb_lower, g_kb_ctrl);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, g_kb_upper, g_kb_ctrl);
}

/* ---------- the PSN emblem ---------- */

lv_obj_t *ctl_ps_diamond(lv_obj_t *parent, lv_coord_t s) {
    lv_obj_t *box = ctl_obj(parent);
    lv_obj_set_size(box, s, s);
    lv_coord_t g = s * 44 / 100, w = s / 18 > 2 ? s / 18 : 2;
    static const struct {
        ps_glyph_t g;
        lv_align_t a;
    } pos[4] = { { PS_TRIANGLE, LV_ALIGN_TOP_MID },
                 { PS_CIRCLE, LV_ALIGN_RIGHT_MID },
                 { PS_CROSS, LV_ALIGN_BOTTOM_MID },
                 { PS_SQUARE, LV_ALIGN_LEFT_MID } };
    for (int i = 0; i < 4; i++) lv_obj_align(ctl_ps_glyph(box, pos[i].g, g, COL_FROST, w), pos[i].a, 0, 0);
    return box;
}
