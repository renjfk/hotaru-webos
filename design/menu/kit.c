/* design/menu/kit.c - shared pieces for the main menu concepts (see kit.h). */
#include "kit.h"

#include "art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- mock data ---------- */

const mock_host_t mock_hosts[MOCK_HOSTS] = {
    { "Living Room PS5", "192.168.1.40", "Played 2 hours ago", true, HS_READY, 0x1d57b8 },
    { "Bedroom PS5", "192.168.1.41", "Played yesterday", true, HS_STANDBY, 0x4b3a92 },
    { "Study PS4", "192.168.1.52", "Played last week", false, HS_OFFLINE, 0x3a4356 },
    { "Office PS5", "via PSN", "Remote, over the internet", true, HS_REMOTE, 0x0e6b78 },
};

const char *mock_state_text(host_state_t s) {
    static const char *const t[] = { "Ready", "Rest mode", "Offline", "Remote" };
    return t[s];
}

uint32_t mock_state_col(host_state_t s) {
    static const uint32_t c[] = { COL_OK, COL_WARN, COL_DIM, COL_INFO };
    return c[s];
}

static const mock_settings_t g_defaults = {
    .res = 1, .fps = 1, .codec = 1, .smooth = 2, .bitrate = 15, .rumble = true, .touchpad = true
};
mock_settings_t mock_set = g_defaults;

void mock_reset(void) { mock_set = g_defaults; }
const char *const mock_res_opts[2] = { "720p", "1080p" };
const char *const mock_fps_opts[2] = { "30 fps", "60 fps" };
const char *const mock_codec_opts[3] = { "H.264", "H.265", "HDR" };
const char *const mock_smooth_opts[4] = { "Off", "100 ms", "150 ms", "200 ms" };

const char *mock_quality_text(void) {
    static char buf[48];
    snprintf(buf, sizeof(buf), "%s" BULLET "%s", mock_res_opts[mock_set.res], mock_fps_opts[mock_set.fps]);
    return buf;
}

const char *mock_summary_text(void) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s" BULLET "%s" BULLET "%d Mbps", mock_set.codec == 0 ? "H.264" : "H.265",
             mock_set.codec == 2 ? "HDR" : "SDR", mock_set.bitrate);
    return buf;
}

const char *mock_clock_text(void) {
    static char buf[16];
    uint32_t s = 42 * 60 + 13 + lv_tick_get() / 1000;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return buf;
}

bool mock_pad_dualsense(void) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("HOTARU_GALLERY_PAD");
        on = e && !strcmp(e, "dualsense") && art_exists(ART_DUALSENSE);
    }
    return on;
}

const char *mock_pad_name(void) { return mock_pad_dualsense() ? "DualSense" : "DualShock 4"; }

void (*kit_stream_hook)(bool on);

/* ---------- building blocks ---------- */

lv_obj_t *kit_obj(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    return o;
}

lv_obj_t *kit_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius, uint32_t col, lv_opa_t opa) {
    lv_obj_t *o = kit_obj(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, C(col), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

void kit_grad(lv_obj_t *o, uint32_t top, uint32_t bottom) {
    lv_obj_set_style_bg_color(o, C(top), 0);
    lv_obj_set_style_bg_grad_color(o, C(bottom), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, C(col), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *kit_dot(lv_obj_t *parent, lv_coord_t d, uint32_t col) {
    lv_obj_t *o = kit_rect(parent, d, d, LV_RADIUS_CIRCLE, col, LV_OPA_COVER);
    lv_obj_set_style_shadow_color(o, C(col), 0);
    lv_obj_set_style_shadow_width(o, d, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_60, 0);
    return o;
}

lv_obj_t *kit_app_mark(lv_obj_t *parent, lv_coord_t size) {
    lv_obj_t *tile = kit_rect(parent, size, size, size * 22 / 100, 0x2a86ea, LV_OPA_COVER);
    kit_grad(tile, 0x2a86ea, 0x0a3c8f);
    lv_obj_center(kit_ps_diamond(tile, size * 62 / 100, false));
    return tile;
}

/* ---------- procedural art ---------- */

static lv_obj_t *g_art_holder; /* hidden: owns the glow canvas */
static lv_obj_t *g_glow_cv;

static lv_obj_t *art_canvas(lv_coord_t w, lv_coord_t h) {
    lv_obj_t *cv = lv_canvas_create(g_art_holder);
    void *buf = malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(w, h));
    lv_canvas_set_buffer(cv, buf, w, h, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    return cv;
}

/* white radial falloff, written straight into the canvas buffer */
static void draw_glow(void) {
    lv_obj_t *cv = g_glow_cv = art_canvas(KIT_GLOW_D, KIT_GLOW_D);
    lv_color_t *px = (lv_color_t *)lv_canvas_get_img(cv)->data;
    float r = KIT_GLOW_D / 2.0f;
    for (int y = 0; y < KIT_GLOW_D; y++) {
        for (int x = 0; x < KIT_GLOW_D; x++) {
            float dx = (x + 0.5f - r) / r, dy = (y + 0.5f - r) / r, d = sqrtf(dx * dx + dy * dy);
            float a = d >= 1.0f ? 0.0f : powf(1.0f - d, 2.2f);
            lv_color_t c = lv_color_white();
            c.ch.alpha = (uint8_t)lroundf(a * 255.0f);
            px[y * KIT_GLOW_D + x] = c;
        }
    }
}

/* the default size: a PS5 KIT_ART_H tall, a PS4 as wide as that PS5 drawing is tall */
static lv_coord_t art_default_h(bool ps5) { return ps5 ? KIT_ART_H : (lv_coord_t)(KIT_ART_H / art_aspect(ART_PS4)); }

lv_obj_t *kit_console_art_h(lv_obj_t *parent, bool ps5, lv_coord_t h) {
    return art_create(parent, ps5 ? ART_PS5 : ART_PS4, h);
}

lv_obj_t *kit_console_art_box(lv_obj_t *parent, bool ps5, lv_coord_t w, lv_coord_t h) {
    float a = art_aspect(ps5 ? ART_PS5 : ART_PS4);
    lv_coord_t fit_h = (lv_coord_t)((float)w / a);
    return kit_console_art_h(parent, ps5, fit_h < h ? fit_h : h);
}

lv_obj_t *kit_console_art(lv_obj_t *parent, bool ps5) { return kit_console_art_h(parent, ps5, art_default_h(ps5)); }

lv_obj_t *kit_pad_art(lv_obj_t *parent, lv_coord_t h) {
    return art_create(parent, mock_pad_dualsense() ? ART_DUALSENSE : ART_DS4, h);
}

uint16_t kit_art_zoom(bool ps5, lv_coord_t w, lv_coord_t h) {
    lv_coord_t ah = art_default_h(ps5), aw = art_width(ps5 ? ART_PS5 : ART_PS4, ah);
    int32_t zw = (int32_t)w * 256 / aw, zh = (int32_t)h * 256 / ah;
    return (uint16_t)(zw < zh ? zw : zh);
}

void kit_art_fit(lv_obj_t *img, lv_coord_t w, lv_coord_t h) { lv_img_set_zoom(img, art_zoom_fit(img, w, h)); }

lv_obj_t *kit_glow(lv_obj_t *parent, uint32_t col, lv_opa_t opa) {
    lv_obj_t *img = lv_img_create(parent);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(img, lv_canvas_get_img(g_glow_cv));
    lv_obj_set_style_img_recolor(img, C(col), 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_img_opa(img, opa, 0);
    return img;
}

/* ---------- PlayStation glyphs ---------- */

const uint32_t kit_ps_col[4] = { 0x3fd3b6, 0xff6b81, 0x7fa8ff, 0xe58ad8 };

static void free_user_cb(lv_event_t *e) { free(lv_event_get_user_data(e)); }

static lv_obj_t *glyph_line(lv_obj_t *parent, const lv_point_t *pts, int n, uint32_t col, lv_coord_t w) {
    lv_point_t *p = malloc(sizeof(lv_point_t) * (size_t)n);
    memcpy(p, pts, sizeof(lv_point_t) * (size_t)n);
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_line_set_points(l, p, (uint16_t)n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, C(col), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, free_user_cb, LV_EVENT_DELETE, p);
    return l;
}

#define P(a, b) { (lv_coord_t)((a) * size / 100), (lv_coord_t)((b) * size / 100) } /* a point in % of the size */
lv_obj_t *kit_ps_glyph(lv_obj_t *parent, ps_glyph_t g, lv_coord_t size, uint32_t col, lv_coord_t stroke) {
    lv_obj_t *box = kit_obj(parent);
    lv_obj_set_size(box, size, size);
    if (g == PS_CIRCLE || g == PS_SQUARE) {
        lv_coord_t d = (lv_coord_t)(size * (g == PS_CIRCLE ? 60 : 52) / 100);
        lv_obj_t *o = kit_obj(box);
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

lv_obj_t *kit_ps_diamond(lv_obj_t *parent, lv_coord_t s, bool colour) {
    lv_obj_t *box = kit_obj(parent);
    lv_obj_set_size(box, s, s);
    lv_coord_t g = s * 46 / 100, w = s / 18 < 2 ? 2 : s / 18;
    static const ps_glyph_t order[4] = { PS_TRIANGLE, PS_CIRCLE, PS_CROSS, PS_SQUARE };
    static const lv_align_t at[4] = { LV_ALIGN_TOP_MID, LV_ALIGN_RIGHT_MID, LV_ALIGN_BOTTOM_MID, LV_ALIGN_LEFT_MID };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = kit_ps_glyph(box, order[i], g, colour ? kit_ps_col[order[i]] : 0xffffff, w);
        lv_obj_align(o, at[i], 0, 0);
    }
    return box;
}

lv_obj_t *kit_hints(lv_obj_t *parent) {
    lv_obj_t *row = kit_obj(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, SP_XL, 0);
    return row;
}

static lv_obj_t *hint_item(lv_obj_t *row) {
    lv_obj_t *it = kit_obj(row);
    lv_obj_set_size(it, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(it, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(it, SP_M, 0);
    return it;
}

static void hint_chip_style(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, C(0x070b16), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_70, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_20, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
}

void kit_hint(lv_obj_t *row, ps_glyph_t g, const char *text) {
    lv_obj_t *it = hint_item(row);
    lv_obj_t *chip = kit_obj(it);
    lv_obj_set_size(chip, 40, 40);
    hint_chip_style(chip);
    lv_obj_center(kit_ps_glyph(chip, g, 40, kit_ps_col[g], 3));
    kit_label(it, FONT_CAPTION, 0xc3cbe0, text);
}

void kit_hint_key(lv_obj_t *row, const char *key, const char *text) {
    lv_obj_t *it = hint_item(row);
    lv_obj_t *chip = kit_obj(it);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_hor(chip, SP_M, 0);
    hint_chip_style(chip);
    lv_obj_t *l = kit_label(chip, FONT_SMALL, COL_TEXT, key);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_center(l);
    kit_label(it, FONT_CAPTION, 0xc3cbe0, text);
}

/* ---------- animation ---------- */

static void ex_x(void *o, int32_t v) { lv_obj_set_x(o, (lv_coord_t)v); }
static void ex_y(void *o, int32_t v) { lv_obj_set_y(o, (lv_coord_t)v); }
static void ex_w(void *o, int32_t v) { lv_obj_set_width(o, (lv_coord_t)v); }
static void ex_h(void *o, int32_t v) { lv_obj_set_height(o, (lv_coord_t)v); }
static void ex_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, (lv_coord_t)v, 0); }
static void ex_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, (lv_coord_t)v, 0); }
static lv_opa_t clamp_opa(int32_t v) { return (lv_opa_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static void ex_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, clamp_opa(v), 0); }
static void ex_bg_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa(o, clamp_opa(v), 0); }
static void ex_text_opa(void *o, int32_t v) { lv_obj_set_style_text_opa(o, clamp_opa(v), 0); }
static void ex_img_opa(void *o, int32_t v) { lv_obj_set_style_img_opa(o, clamp_opa(v), 0); }
static void ex_zoom(void *o, int32_t v) { lv_img_set_zoom(o, (uint16_t)(v < 1 ? 1 : v)); }
static void ex_grow(void *o, int32_t v) {
    lv_obj_set_style_transform_width(o, (lv_coord_t)v, 0);
    lv_obj_set_style_transform_height(o, (lv_coord_t)v, 0);
}
static void ex_border_opa(void *o, int32_t v) { lv_obj_set_style_border_opa(o, clamp_opa(v), 0); }
static void ex_outline_opa(void *o, int32_t v) { lv_obj_set_style_outline_opa(o, clamp_opa(v), 0); }
static void ex_arc(void *o, int32_t v) { lv_arc_set_value(o, (int16_t)v); }
static void ex_bar(void *o, int32_t v) { lv_bar_set_value(o, v, LV_ANIM_OFF); }
static void ex_shadow_opa(void *o, int32_t v) { lv_obj_set_style_shadow_opa(o, clamp_opa(v), 0); }

static const lv_anim_exec_xcb_t g_exec[] = {
    [KA_X] = ex_x,
    [KA_Y] = ex_y,
    [KA_W] = ex_w,
    [KA_H] = ex_h,
    [KA_TX] = ex_tx,
    [KA_TY] = ex_ty,
    [KA_OPA] = ex_opa,
    [KA_BG_OPA] = ex_bg_opa,
    [KA_TEXT_OPA] = ex_text_opa,
    [KA_IMG_OPA] = ex_img_opa,
    [KA_ZOOM] = ex_zoom,
    [KA_GROW] = ex_grow,
    [KA_BORDER_OPA] = ex_border_opa,
    [KA_OUTLINE_OPA] = ex_outline_opa,
    [KA_ARC] = ex_arc,
    [KA_BAR] = ex_bar,
    [KA_SHADOW_OPA] = ex_shadow_opa,
};

int32_t kit_prop_get(lv_obj_t *o, kit_prop_t p) {
    switch (p) {
        case KA_X: return lv_obj_get_style_x(o, 0);
        case KA_Y: return lv_obj_get_style_y(o, 0);
        case KA_W: return lv_obj_get_style_width(o, 0);
        case KA_H: return lv_obj_get_style_height(o, 0);
        case KA_TX: return lv_obj_get_style_translate_x(o, 0);
        case KA_TY: return lv_obj_get_style_translate_y(o, 0);
        case KA_OPA: return lv_obj_get_style_opa(o, 0);
        case KA_BG_OPA: return lv_obj_get_style_bg_opa(o, 0);
        case KA_TEXT_OPA: return lv_obj_get_style_text_opa(o, 0);
        case KA_IMG_OPA: return lv_obj_get_style_img_opa(o, 0);
        case KA_ZOOM: return lv_img_get_zoom(o);
        case KA_GROW: return lv_obj_get_style_transform_width(o, 0);
        case KA_BORDER_OPA: return lv_obj_get_style_border_opa(o, 0);
        case KA_OUTLINE_OPA: return lv_obj_get_style_outline_opa(o, 0);
        case KA_ARC: return lv_arc_get_value(o);
        case KA_BAR: return lv_bar_get_value(o);
        case KA_SHADOW_OPA: return lv_obj_get_style_shadow_opa(o, 0);
    }
    return 0;
}

void kit_prop_set(lv_obj_t *o, kit_prop_t p, int32_t v) {
    lv_anim_del(o, g_exec[p]);
    g_exec[p](o, v);
}

void kit_anim_from(lv_obj_t *o, kit_prop_t p, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                   lv_anim_path_cb_t path) {
    if (ms == 0 && delay == 0) {
        kit_prop_set(o, p, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, g_exec[p]);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms ? ms : 1);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void kit_anim(lv_obj_t *o, kit_prop_t p, int32_t to, uint32_t ms, uint32_t delay, lv_anim_path_cb_t path) {
    int32_t from = kit_prop_get(o, p);
    if (from == to && !lv_anim_get(o, g_exec[p])) return;
    kit_anim_from(o, p, from, to, ms, delay, path);
}

static void then_ready_cb(lv_anim_t *a) {
    void (*ready)(lv_obj_t *) = (void (*)(lv_obj_t *))lv_anim_get_user_data(a);
    if (ready) ready(a->var);
}

void kit_anim_then(lv_obj_t *o, kit_prop_t p, int32_t to, uint32_t ms, lv_anim_path_cb_t path,
                   void (*ready)(lv_obj_t *o)) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, g_exec[p]);
    lv_anim_set_values(&a, kit_prop_get(o, p), to);
    lv_anim_set_time(&a, ms ? ms : 1);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, (void *)ready);
    lv_anim_set_ready_cb(&a, then_ready_cb);
    lv_anim_start(&a);
}

int32_t kit_path_spring(const lv_anim_t *a) {
    if (a->act_time >= (int32_t)a->time) return a->end_value;
    float t = (float)a->act_time / (float)a->time;
    float p = 1.0f - expf(-6.5f * t) * cosf(8.5f * t);
    return a->start_value + (int32_t)lroundf(p * (float)(a->end_value - a->start_value));
}

void kit_enter(lv_obj_t *o, uint32_t delay, lv_coord_t dy) {
    kit_anim_from(o, KA_TY, dy, 0, 340, delay, lv_anim_path_ease_out);
    kit_anim_from(o, KA_OPA, 0, 255, 260, delay, lv_anim_path_ease_out);
}

/* rolling text: one exec for both halves, v in -256..256 (0 = in place, sign = side) */
#define ROLL_DY    26
#define ROLL_SLOTS 32
static struct {
    lv_obj_t *label;
    char text[96];
    int dir;
} g_roll[ROLL_SLOTS];

static int roll_slot(lv_obj_t *l) {
    int free_i = -1;
    for (int i = 0; i < ROLL_SLOTS; i++) {
        if (g_roll[i].label == l) return i;
        if (!g_roll[i].label && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;
    g_roll[free_i].label = l;
    return free_i;
}

static void roll_exec(void *o, int32_t v) {
    lv_obj_set_style_translate_y(o, (lv_coord_t)(v * ROLL_DY / 256), 0);
    lv_obj_set_style_text_opa(o, clamp_opa(255 - LV_ABS(v) * 255 / 256), 0);
}

static void roll_done(lv_anim_t *a) {
    for (int i = 0; i < ROLL_SLOTS; i++)
        if (g_roll[i].label == a->var) g_roll[i].label = NULL;
}

static void roll_swap(lv_anim_t *a) {
    lv_obj_t *l = a->var;
    int i = roll_slot(l);
    lv_label_set_text(l, g_roll[i].text);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, l);
    lv_anim_set_exec_cb(&b, roll_exec);
    lv_anim_set_values(&b, g_roll[i].dir * 256, 0);
    lv_anim_set_time(&b, 200);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&b, roll_done);
    lv_anim_start(&b);
}

static void roll_forget_cb(lv_event_t *e) {
    lv_obj_t *l = lv_event_get_target(e);
    for (int i = 0; i < ROLL_SLOTS; i++)
        if (g_roll[i].label == l) g_roll[i].label = NULL;
}

void kit_roll_text(lv_obj_t *label, const char *text, int dir) {
    if (!strcmp(lv_label_get_text(label), text) && !lv_anim_get(label, roll_exec)) return;
    int i = roll_slot(label);
    snprintf(g_roll[i].text, sizeof(g_roll[i].text), "%s", text);
    g_roll[i].dir = dir >= 0 ? 1 : -1;
    lv_obj_remove_event_cb(label, roll_forget_cb);
    lv_obj_add_event_cb(label, roll_forget_cb, LV_EVENT_DELETE, NULL);
    int32_t cur = lv_obj_get_style_translate_y(label, 0) * 256 / ROLL_DY;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, roll_exec);
    lv_anim_set_values(&a, cur, -g_roll[i].dir * 256); /* leaves on the opposite side it enters from */
    lv_anim_set_time(&a, 110);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, roll_swap);
    lv_anim_start(&a);
}

/* counting numbers */
#define COUNT_SLOTS 16
static struct {
    lv_obj_t *label;
    char fmt[24];
} g_count[COUNT_SLOTS];

static void count_exec(void *o, int32_t v) {
    for (int i = 0; i < COUNT_SLOTS; i++)
        if (g_count[i].label == o) {
            lv_label_set_text_fmt(o, g_count[i].fmt, (int)v);
            return;
        }
}

static void count_forget_cb(lv_event_t *e) {
    lv_obj_t *l = lv_event_get_target(e);
    for (int i = 0; i < COUNT_SLOTS; i++)
        if (g_count[i].label == l) g_count[i].label = NULL;
}

void kit_count_to(lv_obj_t *label, int from, int to, const char *fmt, uint32_t ms) {
    int slot = -1;
    for (int i = 0; i < COUNT_SLOTS && slot < 0; i++)
        if (g_count[i].label == label) slot = i;
    for (int i = 0; i < COUNT_SLOTS && slot < 0; i++)
        if (!g_count[i].label) slot = i;
    if (slot < 0) slot = 0;
    g_count[slot].label = label;
    snprintf(g_count[slot].fmt, sizeof(g_count[slot].fmt), "%s", fmt);
    lv_obj_remove_event_cb(label, count_forget_cb);
    lv_obj_add_event_cb(label, count_forget_cb, LV_EVENT_DELETE, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, count_exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* colour fades: from the current colour of that property to `to` */
#define MIX_SLOTS 24
static struct {
    lv_obj_t *obj;
    lv_style_prop_t prop;
    lv_color_t from, to;
} g_mix[MIX_SLOTS];

static void mix_apply(void *o, int32_t v, lv_style_prop_t prop) {
    for (int i = 0; i < MIX_SLOTS; i++) {
        if (g_mix[i].obj != o || g_mix[i].prop != prop) continue;
        lv_color_t c = lv_color_mix(g_mix[i].to, g_mix[i].from, (uint8_t)v);
        lv_obj_set_local_style_prop(o, prop, (lv_style_value_t){ .color = c }, 0);
        return;
    }
}
static void ex_mix_bg(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BG_COLOR); }
static void ex_mix_grad(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BG_GRAD_COLOR); }
static void ex_mix_text(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_TEXT_COLOR); }
static void ex_mix_recolor(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_IMG_RECOLOR); }
static void ex_mix_border(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BORDER_COLOR); }

static void mix_forget_cb(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    for (int i = 0; i < MIX_SLOTS; i++)
        if (g_mix[i].obj == o) g_mix[i].obj = NULL;
}

void kit_mix_color(lv_obj_t *o, lv_style_prop_t prop, uint32_t to, uint32_t ms) {
    lv_anim_exec_xcb_t ex = prop == LV_STYLE_BG_COLOR        ? ex_mix_bg
                            : prop == LV_STYLE_BG_GRAD_COLOR ? ex_mix_grad
                            : prop == LV_STYLE_TEXT_COLOR    ? ex_mix_text
                            : prop == LV_STYLE_IMG_RECOLOR   ? ex_mix_recolor
                                                             : ex_mix_border;
    int slot = -1;
    for (int i = 0; i < MIX_SLOTS && slot < 0; i++)
        if (g_mix[i].obj == o && g_mix[i].prop == prop) slot = i;
    for (int i = 0; i < MIX_SLOTS && slot < 0; i++)
        if (!g_mix[i].obj) slot = i;
    if (slot < 0) slot = 0;
    lv_color_t cur = lv_obj_get_style_prop(o, 0, prop).color;
    if (cur.full == C(to).full && !lv_anim_get(o, ex)) return;
    lv_obj_remove_event_cb(o, mix_forget_cb);
    lv_obj_add_event_cb(o, mix_forget_cb, LV_EVENT_DELETE, NULL);
    g_mix[slot].obj = o;
    g_mix[slot].prop = prop;
    g_mix[slot].from = lv_obj_get_style_prop(o, 0, prop).color;
    g_mix[slot].to = C(to);
    if (!ms) {
        ex(o, 255);
        lv_anim_del(o, ex);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, ex);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* ---------- controls ---------- */

#define SEG_PAD 6
#define SEG_H   68

static void seg_paint(kit_seg_t *s) {
    for (int i = 0; i < s->n; i++) {
        uint32_t c = i != s->sel ? COL_MUTED : s->focused ? 0x0a1020 : 0xffffff;
        lv_obj_set_style_text_color(s->lbl[i], C(c), 0);
    }
    if (s->focused) kit_grad(s->pill, 0xffffff, 0xdfe7f5);
    else kit_grad(s->pill, COL_ACCENT_HI, COL_ACCENT);
    lv_obj_set_style_border_opa(s->box, s->focused ? LV_OPA_40 : LV_OPA_10, 0);
}

void kit_seg_create(kit_seg_t *s, lv_obj_t *parent, const char *const *opts, int n, int sel, lv_coord_t seg_w) {
    memset(s, 0, sizeof(*s));
    s->n = n;
    s->sel = sel;
    s->seg_w = seg_w;
    s->box = kit_rect(parent, n * seg_w + 2 * SEG_PAD, SEG_H, LV_RADIUS_CIRCLE, 0x000000, LV_OPA_30);
    lv_obj_set_style_border_width(s->box, 2, 0);
    lv_obj_set_style_border_color(s->box, lv_color_white(), 0);
    s->pill = kit_rect(s->box, seg_w, SEG_H - 2 * SEG_PAD, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
    lv_obj_set_pos(s->pill, SEG_PAD + sel * seg_w, SEG_PAD);
    for (int i = 0; i < n; i++) {
        s->lbl[i] = kit_label(s->box, FONT_BODY, COL_MUTED, opts[i]);
        lv_obj_set_width(s->lbl[i], seg_w);
        lv_obj_set_style_text_align(s->lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s->lbl[i], LV_ALIGN_LEFT_MID, SEG_PAD + i * seg_w, 0);
    }
    seg_paint(s);
}

void kit_seg_select(kit_seg_t *s, int sel) {
    if (sel < 0 || sel >= s->n || sel == s->sel) return;
    s->sel = sel;
    kit_anim(s->pill, KA_X, SEG_PAD + sel * s->seg_w, 340, 0, kit_path_spring);
    seg_paint(s);
}

void kit_seg_focus(kit_seg_t *s, bool on) {
    s->focused = on;
    seg_paint(s);
}

void kit_meter_create(kit_meter_t *m, lv_obj_t *parent, lv_coord_t w, int min, int max, int val) {
    memset(m, 0, sizeof(*m));
    m->min = min;
    m->max = max;
    m->val = val;
    m->w = w;
    m->box = kit_obj(parent);
    lv_obj_set_size(m->box, w + 200, SEG_H);
    m->track = kit_rect(m->box, w, 20, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_align(m->track, LV_ALIGN_LEFT_MID, 0, 0);
    m->fill =
        kit_rect(m->track, (lv_coord_t)((val - min) * w / (max - min)), 20, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
    kit_grad(m->fill, COL_ACCENT_HI, COL_ACCENT);
    lv_obj_set_style_bg_grad_dir(m->fill, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_color(m->fill, C(COL_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(m->fill, C(COL_ACCENT_HI), 0);
    m->value = kit_label(m->box, FONT_HEADING, COL_TEXT, "");
    lv_label_set_text_fmt(m->value, "%d Mbps", val);
    lv_obj_set_width(m->value, 176);
    lv_obj_set_style_text_align(m->value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(m->value, LV_ALIGN_RIGHT_MID, 0, 0);
}

void kit_meter_set(kit_meter_t *m, int val) {
    if (val < m->min) val = m->min;
    if (val > m->max) val = m->max;
    if (val == m->val) return;
    kit_count_to(m->value, m->val, val, "%d Mbps", 260);
    m->val = val;
    kit_anim(m->fill, KA_W, (val - m->min) * m->w / (m->max - m->min), 260, 0, lv_anim_path_ease_out);
}

void kit_meter_focus(kit_meter_t *m, bool on) {
    lv_obj_set_style_bg_color(m->fill, C(on ? 0x9fc8ff : COL_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(m->fill, C(on ? 0xffffff : COL_ACCENT_HI), 0);
    lv_obj_set_style_bg_opa(m->track, on ? LV_OPA_20 : LV_OPA_10, 0);
}

#define TGL_W 112
#define TGL_H 60
#define TGL_K 48

void kit_toggle_create(kit_toggle_t *t, lv_obj_t *parent, bool val) {
    t->val = val;
    t->track = kit_rect(parent, TGL_W, TGL_H, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_20);
    t->on = kit_rect(t->track, TGL_W, TGL_H, LV_RADIUS_CIRCLE, COL_ACCENT, val ? LV_OPA_COVER : LV_OPA_TRANSP);
    kit_grad(t->on, COL_ACCENT_HI, COL_ACCENT);
    t->knob = kit_rect(t->track, TGL_K, TGL_K, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_COVER);
    lv_obj_set_pos(t->knob, val ? TGL_W - TGL_K - 6 : 6, (TGL_H - TGL_K) / 2);
}

void kit_toggle_set(kit_toggle_t *t, bool val) {
    t->val = val;
    kit_anim(t->knob, KA_X, val ? TGL_W - TGL_K - 6 : 6, 360, 0, kit_path_spring);
    kit_anim(t->on, KA_BG_OPA, val ? LV_OPA_COVER : LV_OPA_TRANSP, 220, 0, lv_anim_path_ease_out);
}

lv_obj_t *kit_focus_frame(lv_obj_t *parent, lv_coord_t radius) {
    lv_obj_t *f = kit_obj(parent);
    lv_obj_add_flag(f, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_radius(f, radius, 0);
    lv_obj_set_style_border_width(f, 4, 0);
    lv_obj_set_style_border_color(f, lv_color_white(), 0);
    lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(f, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_10, 0);
    return f;
}

void kit_glide(lv_obj_t *frame, lv_obj_t *target, lv_coord_t pad, uint32_t ms) {
    lv_obj_update_layout(target);
    lv_obj_t *par = lv_obj_get_parent(frame);
    lv_area_t t, p;
    lv_obj_get_coords(target, &t);
    lv_obj_get_coords(par, &p);
    /* the target's resting place: its coords include a translation that may be mid-animation */
    lv_coord_t x = t.x1 - p.x1 - lv_obj_get_style_pad_left(par, 0) - pad - lv_obj_get_style_translate_x(target, 0);
    lv_coord_t y = t.y1 - p.y1 - lv_obj_get_style_pad_top(par, 0) - pad - lv_obj_get_style_translate_y(target, 0);
    lv_coord_t w = lv_area_get_width(&t) + 2 * pad, h = lv_area_get_height(&t) + 2 * pad;
    kit_anim(frame, KA_X, x, ms, 0, lv_anim_path_ease_out);
    kit_anim(frame, KA_Y, y, ms, 0, lv_anim_path_ease_out);
    kit_anim(frame, KA_W, w, ms, 0, lv_anim_path_ease_out);
    kit_anim(frame, KA_H, h, ms, 0, lv_anim_path_ease_out);
}

/* ---------- connecting interstitial ---------- */

static void del_ready_cb(lv_anim_t *a) { lv_obj_del_async(a->var); }

static void connect_done_cb(lv_timer_t *t) {
    lv_obj_t *scr = t->user_data;
    if (kit_stream_hook) kit_stream_hook(true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scr);
    lv_anim_set_exec_cb(&a, ex_opa);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_time(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, del_ready_cb);
    lv_anim_start(&a);
}

void kit_connect_screen(const mock_host_t *h) {
    lv_obj_t *scr = kit_obj(lv_layer_top());
    lv_obj_set_size(scr, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    kit_grad(scr, h->tint, COL_BG_BOTTOM);
    lv_obj_set_style_bg_main_stop(scr, 0, 0);
    lv_obj_set_style_bg_grad_stop(scr, 200, 0);
    lv_obj_t *glow = kit_glow(scr, h->tint, LV_OPA_70);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, -110);
    lv_obj_t *art = kit_console_art_box(scr, h->ps5, 420, 360);
    lv_obj_align(art, LV_ALIGN_CENTER, 0, -110);
    uint16_t z = 256;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, art);
    lv_anim_set_exec_cb(&a, ex_zoom);
    lv_anim_set_values(&a, z, z * 104 / 100);
    lv_anim_set_time(&a, 900);
    lv_anim_set_playback_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    lv_obj_t *t = kit_label(scr, FONT_TITLE, COL_TEXT, "");
    lv_label_set_text_fmt(t, "Connecting to %s", h->name);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 150);
    lv_obj_t *c = kit_label(scr, FONT_CAPTION, COL_MUTED, "Waking it up and starting Remote Play");
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 196);
    lv_obj_t *sp = lv_spinner_create(scr, 900, 70);
    lv_obj_set_size(sp, 64, 64);
    lv_obj_clear_flag(sp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(sp, 6, 0);
    lv_obj_set_style_arc_color(sp, lv_color_white(), 0);
    lv_obj_set_style_arc_opa(sp, LV_OPA_20, 0);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, C(COL_ACCENT_HI), LV_PART_INDICATOR);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, 290);
    kit_anim_from(scr, KA_OPA, 0, 255, 260, 0, lv_anim_path_ease_out);
    lv_timer_t *tm = lv_timer_create(connect_done_cb, 1800, scr);
    lv_timer_set_repeat_count(tm, 1);
}

void kit_init(void) {
    g_art_holder = kit_obj(lv_layer_sys());
    lv_obj_add_flag(g_art_holder, LV_OBJ_FLAG_HIDDEN);
    draw_glow();
}
