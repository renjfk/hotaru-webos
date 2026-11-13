/* design/menu/c_orbit.c - concept 02 "Orbit": the NXP smartwatch demo, at TV scale.
 *
 * Full-screen "activities" stacked vertically (Consoles, Stream, Controller): UP/DOWN slide
 * between them like the watch's swipe, LEFT/RIGHT move inside one. Consoles is a carousel of
 * big cards on a spring, the focused one centred and the others scaled back and dimmed; Stream
 * is four ring gauges; Controller is three round toggles that fill like activity rings. In the
 * stream the menu is a floating dock of round buttons with a tooltip that follows the focus,
 * and live stats rings in the corner.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "concept.h"

#define CX        960
#define CAR_Y     560
#define RING_D    300
#define RING_DF   350
#define TGL_D     250
#define DOCK_W    920
#define DOCK_H    150
#define DOCK_BTN  104
#define DOCK_BF   132
#define DOCK_STEP 172
#define N_CARDS   (MOCK_HOSTS + 1)

enum { PG_CONSOLES, PG_STREAM, PG_PAD, PG__N };
static const char *const g_page_names[PG__N] = { "Consoles", "Stream", "Controller" };

typedef struct {
    lv_obj_t *card, *art, *name, *status, *play, *veil;
    bool ps5, has_art;
} ocard_t;
typedef struct {
    lv_obj_t *box, *arc, *halo, *val, *unit, *cap, *arrows[2];
    uint32_t col;
} ring_t;
typedef struct { lv_obj_t *box, *fill, *name, *state; } tgl_t;

static struct {
    lv_obj_t *root, *home, *glow, *title, *pager_v[PG__N], *pages[PG__N], *dots[N_CARDS];
    int page, focus, ring, adjusting, tgl;
    ocard_t cards[N_CARDS];
    ring_t rings[4];
    tgl_t tgls[3];
    lv_obj_t *summary;
} g;

static struct {
    lv_obj_t *root, *dock, *btn[5], *tip, *tip_lbl, *stats, *chip, *chip_time, *mini[3], *mini_val[3];
    int focus;
    bool open;
    lv_timer_t *tick;
} ov;

/* ---------- ambience: satellites on the orbit rings ---------- */

static void sat_exec(void *o, int32_t v) {
    intptr_t r = (intptr_t)lv_obj_get_user_data(o);
    lv_coord_t d = lv_obj_get_width(o);
    lv_obj_set_pos(o, (lv_coord_t)(CX + r * lv_trigo_cos((int16_t)(v / 10)) / LV_TRIGO_SIN_MAX - d / 2),
                   (lv_coord_t)(CAR_Y + r * lv_trigo_sin((int16_t)(v / 10)) / LV_TRIGO_SIN_MAX - d / 2));
}

static void satellite(lv_obj_t *parent, int r, lv_coord_t d, uint32_t col, int start, uint32_t period) {
    lv_obj_t *s = kit_dot(parent, d, col);
    lv_obj_set_user_data(s, (void *)(intptr_t)r);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s);
    lv_anim_set_exec_cb(&a, sat_exec);
    lv_anim_set_values(&a, start * 10, start * 10 + 3600);
    lv_anim_set_time(&a, period);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* ---------- consoles carousel ---------- */

static void card_geom(int d, lv_coord_t *cx, lv_coord_t *w, lv_coord_t *h, lv_opa_t *veil) {
    int a = abs(d), s = d < 0 ? -1 : 1;
    static const lv_coord_t ws[] = { 520, 380, 300, 260 }, hs[] = { 640, 480, 390, 330 }, off[] = { 0, 530, 920, 1260 };
    static const lv_opa_t vs[] = { 0, 120, 175, 210 };
    if (a > 3) a = 3;
    *cx = CX + s * off[a];
    *w = ws[a];
    *h = hs[a];
    *veil = vs[a];
}

static void layout_cards(uint32_t ms) {
    for (int i = 0; i < N_CARDS; i++) {
        ocard_t *c = &g.cards[i];
        lv_coord_t cx, w, h;
        lv_opa_t veil;
        card_geom(i - g.focus, &cx, &w, &h, &veil);
        lv_anim_path_cb_t p = kit_path_spring;
        kit_anim(c->card, KA_X, cx - w / 2, ms, 0, p);
        kit_anim(c->card, KA_Y, CAR_Y - h / 2, ms, 0, p);
        kit_anim(c->card, KA_W, w, ms, 0, p);
        kit_anim(c->card, KA_H, h, ms, 0, p);
        kit_anim(c->veil, KA_BG_OPA, veil, ms / 2, 0, lv_anim_path_ease_out);
        /* the art sits in the upper 40% of the card whatever its size */
        const lv_img_dsc_t *ad = c->has_art ? lv_img_get_src(c->art) : NULL;
        lv_coord_t art_h = ad ? (lv_coord_t)ad->header.h : 150;
        kit_anim(c->art, KA_Y, h * 34 / 100 - art_h / 2, ms, 0, p);
        if (c->has_art) kit_anim(c->art, KA_ZOOM, kit_art_zoom(c->ps5, w * 62 / 100, h * 40 / 100), ms, 0, p);
        bool far = abs(i - g.focus) >= 2;
        kit_anim(c->name, KA_TEXT_OPA, far ? LV_OPA_TRANSP : LV_OPA_COVER, 200, 0, NULL);
        kit_anim(c->status, KA_OPA, far ? LV_OPA_TRANSP : LV_OPA_COVER, 200, 0, NULL);
        bool f = i == g.focus;
        kit_anim(c->play, KA_OPA, f ? LV_OPA_COVER : LV_OPA_TRANSP, f ? 260 : 120, f ? 160 : 0, NULL);
        kit_anim(c->card, KA_OUTLINE_OPA, f ? LV_OPA_COVER : LV_OPA_TRANSP, 200, 0, NULL);
        kit_anim(g.dots[i], KA_W, f ? 44 : 14, 300, 0, lv_anim_path_ease_out);
        kit_anim(g.dots[i], KA_BG_OPA, f ? LV_OPA_COVER : LV_OPA_40, 300, 0, NULL);
    }
    uint32_t tint = g.focus < MOCK_HOSTS ? mock_hosts[g.focus].tint : 0x2a3a5c;
    kit_mix_color(g.glow, LV_STYLE_IMG_RECOLOR, tint + 0x202020, ms ? 500 : 0);
}

static void build_card(lv_obj_t *page, int i) {
    ocard_t *c = &g.cards[i];
    const mock_host_t *h = i < MOCK_HOSTS ? &mock_hosts[i] : NULL;
    c->card = kit_obj(page);
    lv_obj_set_style_radius(c->card, 52, 0);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
    if (!h) kit_grad(c->card, 0x1b2540, 0x0d1322);
    else if (h->state == HS_OFFLINE) kit_grad(c->card, 0x2c3445, 0x121722);
    else kit_grad(c->card, h->tint + 0x141414, 0x0c1426);
    lv_obj_set_style_border_width(c->card, 2, 0);
    lv_obj_set_style_border_color(c->card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c->card, LV_OPA_20, 0);
    lv_obj_set_style_outline_width(c->card, 5, 0);
    lv_obj_set_style_outline_pad(c->card, 8, 0);
    lv_obj_set_style_outline_color(c->card, lv_color_white(), 0);
    lv_obj_set_style_outline_opa(c->card, LV_OPA_TRANSP, 0);
    if (h) {
        c->ps5 = h->ps5;
        c->has_art = true;
        c->art = kit_console_art(c->card, h->ps5);
        lv_obj_align(c->art, LV_ALIGN_TOP_MID, 0, 0);
        if (h->state == HS_OFFLINE) lv_obj_set_style_img_opa(c->art, LV_OPA_50, 0);
    } else {
        c->art = kit_rect(c->card, 150, 150, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
        lv_obj_set_style_border_width(c->art, 4, 0);
        lv_obj_set_style_border_color(c->art, lv_color_white(), 0);
        lv_obj_set_style_border_opa(c->art, LV_OPA_40, 0);
        lv_obj_align(c->art, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_center(kit_label(c->art, FONT_HERO, COL_TEXT, LV_SYMBOL_PLUS));
    }
    c->name = kit_label(c->card, FONT_TITLE, COL_TEXT, h ? h->name : "Add a console");
    lv_obj_set_width(c->name, LV_PCT(90));
    lv_obj_set_style_text_align(c->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(c->name, LV_LABEL_LONG_DOT);
    lv_obj_align(c->name, LV_ALIGN_BOTTOM_MID, 0, -176);
    c->status = kit_obj(c->card);
    lv_obj_set_size(c->status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c->status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c->status, 10, 0);
    if (h) kit_dot(c->status, 12, mock_state_col(h->state));
    kit_label(c->status, FONT_CAPTION, 0xb4bfd6, h ? mock_state_text(h->state) : "PIN or PSN account");
    lv_obj_align(c->status, LV_ALIGN_BOTTOM_MID, 0, -136);
    c->play = kit_rect(c->card, 380, 80, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_COVER);
    lv_obj_align(c->play, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_set_flex_flow(c->play, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c->play, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c->play, 12, 0);
    kit_ps_glyph(c->play, PS_CROSS, 40, 0x2a5bd7, 4);
    static const char *const verb[] = { "Start Remote Play", "Wake and play", "Offline", "Play over PSN" };
    kit_label(c->play, FONT_BODY, 0x0a1020, h ? verb[h->state] : "Pair a console");
    lv_obj_set_style_opa(c->play, LV_OPA_TRANSP, 0);
    c->veil = kit_rect(c->card, LV_PCT(100), LV_PCT(100), 52, 0x03050b, LV_OPA_TRANSP);
}

/* ---------- stream rings ---------- */

static int32_t ring_value(int i) {
    switch (i) {
        case 0: return (mock_set.res + 1) * 500;
        case 1: return (mock_set.fps + 1) * 500;
        case 2: return mock_set.bitrate * 20;
        default: {
            static const int ms[] = { 0, 100, 150, 200 };
            return ms[mock_set.smooth] * 5;
        }
    }
}

static void ring_text(int i, char *val, size_t n, const char **unit) {
    static const char *const units[] = { "Resolution", "Frames per second", "Mbps bitrate", "ms smoothing" };
    *unit = units[i];
    switch (i) {
        case 0: snprintf(val, n, "%s", mock_res_opts[mock_set.res]); break;
        case 1: snprintf(val, n, "%d", mock_set.fps ? 60 : 30); break;
        case 2: snprintf(val, n, "%d", mock_set.bitrate); break;
        default: snprintf(val, n, "%s", mock_set.smooth ? mock_smooth_opts[mock_set.smooth] : "Off"); break;
    }
    if (i == 3 && mock_set.smooth) val[strcspn(val, " ")] = 0;
}

static void ring_style(lv_obj_t *arc, uint32_t col, lv_coord_t w) {
    lv_obj_set_style_arc_width(arc, w, 0);
    lv_obj_set_style_arc_color(arc, lv_color_white(), 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_10, 0);
    lv_obj_set_style_arc_rounded(arc, true, 0);
    lv_obj_set_style_arc_width(arc, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, C(col), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
}

static lv_obj_t *new_arc(lv_obj_t *parent, lv_coord_t d, uint32_t col, lv_coord_t w) {
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, d, d);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, 0, 1000);
    ring_style(arc, col, w);
    return arc;
}

static void build_ring(lv_obj_t *page, int i) {
    static const uint32_t cols[4] = { 0x58c6ff, 0x2fd47a, 0x3592ff, 0xb18cff };
    static const char *const caps[4] = { "Higher needs more bandwidth", "Smoother in fast games",
                                         "Lower it if it stutters", "Hides Wi-Fi stalls" };
    ring_t *r = &g.rings[i];
    r->col = cols[i];
    lv_coord_t cx = CX + (i * 2 - 3) * 200;
    r->box = kit_obj(page);
    lv_obj_set_size(r->box, RING_DF + 60, RING_DF + 60);
    lv_obj_set_pos(r->box, cx - (RING_DF + 60) / 2, 520 - (RING_DF + 60) / 2);
    r->halo = kit_rect(r->box, RING_DF + 40, RING_DF + 40, LV_RADIUS_CIRCLE, r->col, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(r->halo, 2, 0);
    lv_obj_set_style_border_color(r->halo, C(r->col), 0);
    lv_obj_set_style_border_opa(r->halo, LV_OPA_TRANSP, 0);
    lv_obj_center(r->halo);
    r->arc = new_arc(r->box, RING_D, r->col, 22);
    lv_obj_center(r->arc);
    lv_arc_set_value(r->arc, 0);
    char v[24];
    const char *unit;
    ring_text(i, v, sizeof(v), &unit);
    r->val = kit_label(r->box, FONT_HERO, COL_TEXT, v);
    lv_obj_align(r->val, LV_ALIGN_CENTER, 0, -14);
    r->unit = kit_label(r->box, FONT_CAPTION, COL_MUTED, unit);
    lv_obj_align(r->unit, LV_ALIGN_CENTER, 0, 40);
    for (int s = 0; s < 2; s++) {
        r->arrows[s] = kit_label(r->box, FONT_TITLE, r->col, s ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
        lv_obj_align(r->arrows[s], s ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, s ? 6 : -6, 0);
        lv_obj_set_style_text_opa(r->arrows[s], LV_OPA_TRANSP, 0);
    }
    r->cap = kit_label(page, FONT_CAPTION, 0xb4bfd6, caps[i]);
    lv_obj_set_width(r->cap, 340);
    lv_obj_set_style_text_align(r->cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(r->cap, cx - 170, 520 + RING_DF / 2 + 54);
}

static void paint_rings(uint32_t ms) {
    for (int i = 0; i < 4; i++) {
        ring_t *r = &g.rings[i];
        bool f = i == g.ring, adj = f && g.adjusting;
        kit_anim(r->arc, KA_W, f ? RING_DF : RING_D, ms, 0, kit_path_spring);
        kit_anim(r->arc, KA_H, f ? RING_DF : RING_D, ms, 0, kit_path_spring);
        kit_anim(r->halo, KA_BORDER_OPA, adj ? LV_OPA_COVER : f ? LV_OPA_40 : LV_OPA_TRANSP, 220, 0, NULL);
        kit_anim(r->halo, KA_BG_OPA, adj ? LV_OPA_10 : LV_OPA_TRANSP, 220, 0, NULL);
        for (int s = 0; s < 2; s++)
            kit_anim(r->arrows[s], KA_TEXT_OPA, adj ? LV_OPA_COVER : LV_OPA_TRANSP, 200, 0, NULL);
        lv_obj_set_style_text_color(r->unit, C(f ? COL_TEXT : COL_MUTED), 0);
    }
}

static void ring_adjust(int dir) {
    int i = g.ring;
    int32_t before = ring_value(i);
    int old_bitrate = mock_set.bitrate;
    switch (i) {
        case 0: mock_set.res = dir > 0 ? 1 : 0; break;
        case 1: mock_set.fps = dir > 0 ? 1 : 0; break;
        case 2: mock_set.bitrate = LV_CLAMP(MOCK_BITRATE_MIN, mock_set.bitrate + dir * 5, MOCK_BITRATE_MAX); break;
        default: mock_set.smooth = LV_CLAMP(0, mock_set.smooth + dir, 3); break;
    }
    int32_t after = ring_value(i);
    if (after == before) {
        kit_anim_from(g.rings[i].box, KA_TX, dir * 14, 0, 300, 0, kit_path_spring);
        return;
    }
    kit_anim(g.rings[i].arc, KA_ARC, after, 480, 0, kit_path_spring);
    char v[24];
    const char *unit;
    ring_text(i, v, sizeof(v), &unit);
    if (i == 2) kit_count_to(g.rings[i].val, old_bitrate, mock_set.bitrate, "%d", 380);
    else kit_roll_text(g.rings[i].val, v, dir);
    lv_label_set_text(g.summary, mock_summary_text());
}

/* ---------- controller toggles ---------- */

static bool *tgl_val(int i) { return i == 0 ? &mock_set.rumble : i == 1 ? &mock_set.touchpad : &mock_set.motion; }

static void tgl_paint(int i, uint32_t ms) {
    tgl_t *t = &g.tgls[i];
    bool on = *tgl_val(i);
    kit_anim(t->fill, KA_W, on ? TGL_D : 0, ms, 0, kit_path_spring);
    kit_anim(t->fill, KA_H, on ? TGL_D : 0, ms, 0, kit_path_spring);
    lv_label_set_text(t->state, on ? "On" : "Off");
    bool f = g.page == PG_PAD && i == g.tgl;
    kit_anim(t->box, KA_OUTLINE_OPA, f ? LV_OPA_COVER : LV_OPA_TRANSP, 200, 0, NULL);
    kit_anim(t->box, KA_GROW, f ? 14 : 0, 360, 0, kit_path_spring);
}

static void build_tgl(lv_obj_t *page, int i) {
    static const char *const names[3] = { "Rumble", "Touchpad", "Motion" };
    static const char *const syms[3] = { LV_SYMBOL_CHARGE, LV_SYMBOL_KEYBOARD, LV_SYMBOL_GPS };
    tgl_t *t = &g.tgls[i];
    lv_coord_t cx = CX + (i - 1) * 440;
    t->box = kit_rect(page, TGL_D, TGL_D, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_set_pos(t->box, cx - TGL_D / 2, 500 - TGL_D / 2);
    lv_obj_set_style_border_width(t->box, 3, 0);
    lv_obj_set_style_border_color(t->box, lv_color_white(), 0);
    lv_obj_set_style_border_opa(t->box, LV_OPA_20, 0);
    lv_obj_set_style_outline_width(t->box, 5, 0);
    lv_obj_set_style_outline_pad(t->box, 12, 0);
    lv_obj_set_style_outline_color(t->box, lv_color_white(), 0);
    lv_obj_set_style_outline_opa(t->box, LV_OPA_TRANSP, 0);
    t->fill = kit_rect(t->box, 0, 0, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
    kit_grad(t->fill, COL_ACCENT_HI, COL_ACCENT_DEEP);
    lv_obj_center(t->fill);
    lv_obj_center(kit_label(t->box, FONT_HERO, COL_TEXT, syms[i]));
    t->name = kit_label(page, FONT_TITLE, COL_TEXT, names[i]);
    lv_obj_set_width(t->name, 300);
    lv_obj_set_style_text_align(t->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(t->name, cx - 150, 500 + TGL_D / 2 + 44);
    t->state = kit_label(page, FONT_BODY, COL_MUTED, "");
    lv_obj_set_width(t->state, 300);
    lv_obj_set_style_text_align(t->state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(t->state, cx - 150, 500 + TGL_D / 2 + 92);
}

/* ---------- pages ---------- */

static void page_hidden_cb(lv_obj_t *p) {
    if (p != g.pages[g.page]) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
}

static void page_enter_extras(int p) {
    if (p == PG_STREAM) { /* the rings sweep up to their values, one after the other */
        for (int i = 0; i < 4; i++) {
            lv_arc_set_value(g.rings[i].arc, 0);
            kit_anim_from(g.rings[i].arc, KA_ARC, 0, ring_value(i), 900, 200 + (uint32_t)i * 90, lv_anim_path_ease_out);
        }
    }
    if (p == PG_PAD)
        for (int i = 0; i < 3; i++) {
            lv_obj_set_size(g.tgls[i].fill, 0, 0);
            lv_obj_center(g.tgls[i].fill);
            if (*tgl_val(i)) {
                kit_anim_from(g.tgls[i].fill, KA_W, 0, TGL_D, 620, 220 + (uint32_t)i * 90, kit_path_spring);
                kit_anim_from(g.tgls[i].fill, KA_H, 0, TGL_D, 620, 220 + (uint32_t)i * 90, kit_path_spring);
            }
        }
}

static void paint_pager(void) {
    for (int i = 0; i < PG__N; i++) {
        kit_anim(g.pager_v[i], KA_H, i == g.page ? 48 : 14, 320, 0, lv_anim_path_ease_out);
        kit_anim(g.pager_v[i], KA_BG_OPA, i == g.page ? LV_OPA_COVER : LV_OPA_40, 320, 0, NULL);
    }
}

static void page_go(int to) {
    if (to < 0 || to >= PG__N || to == g.page) return;
    int dir = to > g.page ? 1 : -1;
    lv_obj_t *old = g.pages[g.page], *nw = g.pages[to];
    g.page = to;
    g.adjusting = 0;
    kit_anim_then(old, KA_TY, -dir * 180, 300, lv_anim_path_ease_in, page_hidden_cb);
    kit_anim(old, KA_OPA, LV_OPA_TRANSP, 260, 0, lv_anim_path_ease_in);
    lv_obj_clear_flag(nw, LV_OBJ_FLAG_HIDDEN);
    kit_anim_from(nw, KA_TY, dir * 240, 0, 620, 140, kit_path_spring);
    kit_anim_from(nw, KA_OPA, 0, 255, 320, 140, lv_anim_path_ease_out);
    kit_roll_text(g.title, g_page_names[to], dir);
    paint_pager();
    paint_rings(0);
    for (int i = 0; i < 3; i++) tgl_paint(i, 0);
    page_enter_extras(to);
    if (to == PG_CONSOLES) layout_cards(0);
    kit_mix_color(g.glow, LV_STYLE_IMG_RECOLOR,
                  to == PG_CONSOLES ? mock_hosts[g.focus % MOCK_HOSTS].tint + 0x202020
                  : to == PG_STREAM ? 0x2a5bd7
                                    : 0x6a3fb0,
                  500);
}

/* ---------- overlay: the dock ---------- */

static const char *ov_tip_text(void) {
    static char buf[96];
    switch (ov.focus) {
        case 0: return "Resume";
        case 1: snprintf(buf, sizeof(buf), "Quality" BULLET "%s", mock_quality_text()); return buf;
        case 2:
            snprintf(buf, sizeof(buf), "Smooth playback" BULLET "%s", mock_smooth_opts[mock_set.smooth]);
            return buf;
        case 3: snprintf(buf, sizeof(buf), "Rumble" BULLET "%s", mock_set.rumble ? "On" : "Off"); return buf;
        default: return "Disconnect";
    }
}

static lv_coord_t dock_x(void) { return (SCR_W - DOCK_W) / 2; }
static lv_coord_t dock_y(void) { return SCR_H - SAFE_Y - DOCK_H; }

/* the tooltip morphs to the new text's width while it glides over the focused button */
static void ov_tip_place(uint32_t ms, int dir) {
    const char *text = ov_tip_text();
    lv_point_t sz;
    lv_txt_get_size(&sz, text, FONT_BODY, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_coord_t w = sz.x + 64;
    lv_coord_t cx = dock_x() + DOCK_W / 2 + (ov.focus - 2) * DOCK_STEP;
    if (dir) kit_roll_text(ov.tip_lbl, text, dir);
    else lv_label_set_text(ov.tip_lbl, text);
    kit_anim(ov.tip, KA_W, w, ms, 0, lv_anim_path_ease_out);
    kit_anim(ov.tip, KA_X, cx - w / 2, ms, 0, lv_anim_path_ease_out);
}

static void ov_paint(uint32_t ms) {
    for (int i = 0; i < 5; i++) {
        bool f = i == ov.focus;
        lv_coord_t d = f ? DOCK_BF : DOCK_BTN;
        kit_anim(ov.btn[i], KA_W, d, ms, 0, kit_path_spring);
        kit_anim(ov.btn[i], KA_H, d, ms, 0, kit_path_spring);
        kit_anim(ov.btn[i], KA_BG_OPA, f ? LV_OPA_COVER : 36, 200, 0, NULL);
        lv_obj_set_style_bg_color(ov.btn[i], f && i == 4 ? C(COL_ERR) : lv_color_white(), 0);
        kit_mix_color(ov.btn[i], LV_STYLE_TEXT_COLOR, f && i != 4 ? 0x0a1020 : COL_TEXT, 200);
    }
    ov_tip_place(ms ? 280 : 0, 0);
}

static void ov_hidden_cb(lv_obj_t *o) {
    (void)o;
    if (!ov.open) lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
}

static void ov_open(bool open) {
    ov.open = open;
    if (open) {
        ov.focus = 0;
        lv_obj_clear_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
        ov_paint(0);
        kit_anim_from(ov.dock, KA_TY, 260, 0, 620, 0, kit_path_spring);
        for (int i = 0; i < 5; i++) {
            lv_coord_t d = i == 0 ? DOCK_BF : DOCK_BTN;
            kit_anim_from(ov.btn[i], KA_W, 24, d, 560, 120 + (uint32_t)i * 55, kit_path_spring);
            kit_anim_from(ov.btn[i], KA_H, 24, d, 560, 120 + (uint32_t)i * 55, kit_path_spring);
        }
        kit_anim_from(ov.tip, KA_TY, 30, 0, 360, 380, lv_anim_path_ease_out);
        kit_anim_from(ov.tip, KA_OPA, 0, 255, 300, 380, NULL);
        kit_anim_from(ov.stats, KA_TX, 700, 0, 520, 60, lv_anim_path_ease_out);
        kit_anim_from(ov.chip, KA_TX, -800, 0, 520, 60, lv_anim_path_ease_out);
        for (int i = 0; i < 3; i++) {
            int32_t v = lv_arc_get_value(ov.mini[i]);
            kit_anim_from(ov.mini[i], KA_ARC, 0, v, 900, 300 + (uint32_t)i * 80, lv_anim_path_ease_out);
        }
    } else {
        kit_anim_then(ov.dock, KA_TY, 300, 240, lv_anim_path_ease_in, ov_hidden_cb);
        kit_anim(ov.tip, KA_OPA, 0, 160, 0, NULL);
        kit_anim(ov.stats, KA_TX, 700, 260, 0, lv_anim_path_ease_in);
        kit_anim(ov.chip, KA_TX, -800, 260, 0, lv_anim_path_ease_in);
    }
}

static void ov_change(int dir) {
    if (ov.focus == 1) {
        int q = LV_CLAMP(0, mock_set.res * 2 + mock_set.fps + dir, 3);
        mock_set.res = q / 2;
        mock_set.fps = q % 2;
    } else if (ov.focus == 2) {
        mock_set.smooth = LV_CLAMP(0, mock_set.smooth + dir, 3);
        static const int ms[] = { 0, 100, 150, 200 };
        kit_anim(ov.mini[2], KA_ARC, ms[mock_set.smooth] * 5, 420, 0, kit_path_spring);
        lv_label_set_text_fmt(ov.mini_val[2], "%d", ms[mock_set.smooth]);
    } else if (ov.focus == 3) {
        mock_set.rumble = !mock_set.rumble;
    } else {
        return;
    }
    ov_tip_place(220, dir);
}

static void ov_nav(nav_t k) {
    if (!ov.open) {
        if (k == NAV_MENU) ov_open(true);
        return;
    }
    switch (k) {
        case NAV_LEFT:
            if (ov.focus > 0) {
                ov.focus--;
                ov_paint(360);
            }
            break;
        case NAV_RIGHT:
            if (ov.focus < 4) {
                ov.focus++;
                ov_paint(360);
            }
            break;
        case NAV_UP: ov_change(1); break;
        case NAV_DOWN: ov_change(-1); break;
        case NAV_OK:
            if (ov.focus == 0) ov_open(false);
            else if (ov.focus == 4) {
                ov_open(false);
                if (kit_stream_hook) kit_stream_hook(false);
            } else ov_change(1);
            break;
        case NAV_BACK:
        case NAV_MENU: ov_open(false); break;
    }
}

static void ov_tick(lv_timer_t *t) {
    (void)t;
    static int n;
    n++;
    static const int fps[] = { 60, 60, 59, 60, 60 }, mbps10[] = { 148, 151, 146, 150 };
    lv_label_set_text_fmt(ov.chip_time, "%s", mock_clock_text());
    lv_label_set_text_fmt(ov.mini_val[0], "%d", fps[n % 5]);
    kit_anim(ov.mini[0], KA_ARC, fps[n % 5] * 1000 / 60, 400, 0, NULL);
    lv_label_set_text_fmt(ov.mini_val[1], "%d.%d", mbps10[n % 4] / 10, mbps10[n % 4] % 10);
    kit_anim(ov.mini[1], KA_ARC, mbps10[n % 4] * 2, 400, 0, NULL);
}

static void build_overlay(void) {
    ov.root = kit_obj(g.root);
    lv_obj_set_size(ov.root, SCR_W, SCR_H);
    lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);

    ov.dock = kit_rect(ov.root, DOCK_W, DOCK_H, LV_RADIUS_CIRCLE, 0x070b16, 220);
    lv_obj_set_pos(ov.dock, dock_x(), dock_y());
    lv_obj_set_style_border_width(ov.dock, 2, 0);
    lv_obj_set_style_border_color(ov.dock, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ov.dock, LV_OPA_10, 0);
    static const char *const syms[5] = { LV_SYMBOL_PLAY, LV_SYMBOL_VIDEO, LV_SYMBOL_LOOP, "", LV_SYMBOL_POWER };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = ov.btn[i] = kit_rect(ov.dock, DOCK_BTN, DOCK_BTN, LV_RADIUS_CIRCLE, 0xffffff, 36);
        lv_obj_align(b, LV_ALIGN_CENTER, (i - 2) * DOCK_STEP, 0);
        lv_obj_set_style_text_color(b, C(COL_TEXT), 0);
        if (i == 3) lv_obj_center(kit_ps_diamond(b, 50, true));
        else {
            lv_obj_t *ic = lv_label_create(b);
            lv_label_set_text(ic, syms[i]);
            lv_obj_set_style_text_font(ic, FONT_TITLE, 0);
            lv_obj_center(ic);
        }
    }
    ov.tip = kit_rect(ov.root, 200, 60, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_COVER);
    lv_obj_set_y(ov.tip, dock_y() - 86);
    ov.tip_lbl = kit_label(ov.tip, FONT_BODY, 0x0a1020, "Resume");
    lv_obj_center(ov.tip_lbl);

    /* the session, top left */
    ov.chip = kit_rect(ov.root, LV_SIZE_CONTENT, 84, LV_RADIUS_CIRCLE, 0x070b16, 220);
    lv_obj_set_pos(ov.chip, SAFE_X, SAFE_Y);
    lv_obj_set_style_pad_left(ov.chip, 18, 0);
    lv_obj_set_style_pad_right(ov.chip, 32, 0);
    lv_obj_set_flex_flow(ov.chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ov.chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ov.chip, 16, 0);
    lv_obj_t *av = kit_rect(ov.chip, 56, 56, LV_RADIUS_CIRCLE, mock_hosts[0].tint, LV_OPA_COVER);
    lv_obj_t *art = kit_console_art(av, true);
    lv_obj_center(art);
    kit_art_fit(art, 40, 44);
    lv_obj_t *col = kit_obj(ov.chip);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    kit_label(col, FONT_BODY, COL_TEXT, mock_hosts[0].name);
    lv_obj_t *row = kit_obj(col);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    kit_dot(row, 10, COL_OK);
    ov.chip_time = kit_label(row, FONT_CAPTION, COL_MUTED, "00:42:13");

    /* live stats, top right: three small rings */
    ov.stats = kit_rect(ov.root, 520, 160, 80, 0x070b16, 220);
    lv_obj_set_pos(ov.stats, SCR_W - SAFE_X - 520, SAFE_Y);
    static const char *const units[3] = { "fps", "Mbps", "ms delay" };
    static const uint32_t cols[3] = { 0x2fd47a, 0x3592ff, 0xb18cff };
    static const int vals[3] = { 1000, 296, 750 };
    static const char *const txt[3] = { "60", "14.8", "150" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *box = kit_obj(ov.stats);
        lv_obj_set_size(box, 140, 140);
        lv_obj_set_pos(box, 30 + i * 160, 10);
        ov.mini[i] = new_arc(box, 112, cols[i], 9);
        lv_obj_align(ov.mini[i], LV_ALIGN_TOP_MID, 0, 0);
        lv_arc_set_value(ov.mini[i], vals[i]);
        ov.mini_val[i] = kit_label(box, FONT_BODY, COL_TEXT, txt[i]);
        lv_obj_align(ov.mini_val[i], LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_t *u = kit_label(box, FONT_SMALL, COL_MUTED, units[i]);
        lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 104);
    }
}

/* ---------- concept interface ---------- */

static void root_deleted_cb(lv_event_t *e) {
    (void)e;
    if (ov.tick) lv_timer_del(ov.tick);
    memset(&g, 0, sizeof(g));
    memset(&ov, 0, sizeof(ov));
}

static void build(lv_obj_t *root) {
    memset(&g, 0, sizeof(g));
    memset(&ov, 0, sizeof(ov));
    g.root = root;
    lv_obj_add_event_cb(root, root_deleted_cb, LV_EVENT_DELETE, NULL);
    g.home = kit_obj(root);
    lv_obj_set_size(g.home, SCR_W, SCR_H);
    lv_obj_t *bg = kit_rect(g.home, SCR_W, SCR_H, 0, 0x0f1c3a, LV_OPA_COVER);
    kit_grad(bg, 0x0f1c3a, 0x020409);

    g.glow = kit_glow(g.home, mock_hosts[0].tint + 0x202020, LV_OPA_60);
    lv_obj_set_pos(g.glow, CX - KIT_GLOW_D / 2, CAR_Y - KIT_GLOW_D / 2);
    static const lv_coord_t rings[3] = { 820, 1120, 1480 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = kit_rect(g.home, rings[i], rings[i], LV_RADIUS_CIRCLE, 0, LV_OPA_TRANSP);
        lv_obj_set_style_border_width(r, 2, 0);
        lv_obj_set_style_border_color(r, lv_color_white(), 0);
        lv_obj_set_style_border_opa(r, (lv_opa_t)(22 - i * 6), 0);
        lv_obj_set_pos(r, CX - rings[i] / 2, CAR_Y - rings[i] / 2);
    }
    satellite(g.home, 410, 12, COL_ACCENT_HI, 200, 60000);
    satellite(g.home, 560, 10, 0xb18cff, 20, 90000);
    satellite(g.home, 740, 14, 0x3fd3b6, 300, 120000);

    for (int p = 0; p < PG__N; p++) {
        g.pages[p] = kit_obj(g.home);
        lv_obj_set_size(g.pages[p], SCR_W, SCR_H);
        if (p) lv_obj_add_flag(g.pages[p], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < N_CARDS; i++) build_card(g.pages[PG_CONSOLES], i);
    lv_obj_t *dots = kit_obj(g.pages[PG_CONSOLES]);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, 14);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 12, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 910);
    for (int i = 0; i < N_CARDS; i++) g.dots[i] = kit_rect(dots, 14, 14, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_40);

    for (int i = 0; i < 4; i++) build_ring(g.pages[PG_STREAM], i);
    g.summary = kit_label(g.pages[PG_STREAM], FONT_BODY, 0xb4bfd6, mock_summary_text());
    lv_obj_align(g.summary, LV_ALIGN_TOP_MID, 0, 880);

    lv_obj_t *chip = kit_rect(g.pages[PG_PAD], LV_SIZE_CONTENT, 64, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_set_style_pad_hor(chip, 26, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chip, 14, 0);
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 214);
    kit_ps_diamond(chip, 40, true);
    kit_label(chip, FONT_BODY, COL_TEXT, "DualShock 4");
    kit_dot(chip, 12, COL_OK);
    kit_label(chip, FONT_CAPTION, COL_MUTED, "Connected");
    for (int i = 0; i < 3; i++) build_tgl(g.pages[PG_PAD], i);

    /* header, pager, hints */
    lv_obj_t *over = kit_label(g.home, FONT_SMALL, COL_ACCENT_HI, "CHIAKI WEBOS");
    lv_obj_set_style_text_letter_space(over, 3, 0);
    lv_obj_set_pos(over, SAFE_X, SAFE_Y);
    g.title = kit_label(g.home, FONT_HERO, COL_TEXT, g_page_names[0]);
    lv_obj_set_pos(g.title, SAFE_X - 2, SAFE_Y + 24);
    lv_obj_t *clock = kit_label(g.home, FONT_TITLE, COL_TEXT, "21:42");
    lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -SAFE_X, SAFE_Y + 10);
    lv_obj_t *pad = kit_obj(g.home);
    lv_obj_set_size(pad, 56, 44);
    lv_obj_align(pad, LV_ALIGN_TOP_RIGHT, -SAFE_X - 120, SAFE_Y + 6);
    lv_obj_align(kit_ps_diamond(pad, 40, false), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(kit_dot(pad, 12, COL_OK), LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *pager = kit_obj(g.home);
    lv_obj_set_size(pager, 14 + 24, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(pager, 12, 0);
    lv_obj_set_style_bg_color(pager, C(0x070b16), 0);
    lv_obj_set_style_bg_opa(pager, LV_OPA_80, 0);
    lv_obj_set_style_radius(pager, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pager, 14, 0);
    lv_obj_align(pager, LV_ALIGN_RIGHT_MID, -44, 20);
    for (int i = 0; i < PG__N; i++) g.pager_v[i] = kit_rect(pager, 14, 14, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_40);

    lv_obj_t *hints = kit_hints(g.home);
    lv_obj_align(hints, LV_ALIGN_BOTTOM_MID, 0, -SAFE_Y + 14);
    kit_hint_key(hints, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, "Choose");
    kit_hint_key(hints, LV_SYMBOL_UP " " LV_SYMBOL_DOWN, "Pages");
    kit_hint(hints, PS_CROSS, "Select");
    kit_hint(hints, PS_CIRCLE, "Back");

    build_overlay();

    g.page = PG_CONSOLES;
    paint_pager();
    for (int i = 0; i < 4; i++) lv_arc_set_value(g.rings[i].arc, ring_value(i));
    for (int i = 0; i < 3; i++) tgl_paint(i, 0);
    paint_rings(0);
    /* entrance: the cards spring out from the centre */
    for (int i = 0; i < N_CARDS; i++) {
        ocard_t *c = &g.cards[i];
        lv_obj_set_pos(c->card, CX - 130, CAR_Y - 165);
        lv_obj_set_size(c->card, 260, 330);
    }
    layout_cards(700);
}

static void connect_focused(void) {
    if (g.focus >= MOCK_HOSTS || mock_hosts[g.focus].state == HS_OFFLINE) {
        kit_anim_from(g.cards[g.focus].card, KA_TX, 18, 0, 420, 0, kit_path_spring);
        return;
    }
    kit_connect_screen(&mock_hosts[g.focus]);
}

static void nav(nav_t k) {
    if (ov.root && (ov.open || lv_obj_has_flag(g.home, LV_OBJ_FLAG_HIDDEN))) {
        ov_nav(k);
        return;
    }
    if (g.page == PG_STREAM && g.adjusting) {
        if (k == NAV_LEFT || k == NAV_RIGHT) ring_adjust(k == NAV_RIGHT ? 1 : -1);
        else if (k == NAV_OK || k == NAV_BACK) {
            g.adjusting = 0;
            paint_rings(300);
        }
        return;
    }
    if (k == NAV_UP) {
        page_go(g.page - 1);
        return;
    }
    if (k == NAV_DOWN) {
        page_go(g.page + 1);
        return;
    }
    int d = k == NAV_RIGHT ? 1 : k == NAV_LEFT ? -1 : 0;
    switch (g.page) {
        case PG_CONSOLES:
            if (d && g.focus + d >= 0 && g.focus + d < N_CARDS) {
                g.focus += d;
                layout_cards(560);
            } else if (k == NAV_OK) connect_focused();
            break;
        case PG_STREAM:
            if (d && g.ring + d >= 0 && g.ring + d < 4) {
                g.ring += d;
                paint_rings(420);
            } else if (k == NAV_OK) {
                g.adjusting = 1;
                paint_rings(300);
            } else if (k == NAV_BACK) page_go(PG_CONSOLES);
            break;
        case PG_PAD:
            if (d && g.tgl + d >= 0 && g.tgl + d < 3) {
                g.tgl += d;
                for (int i = 0; i < 3; i++) tgl_paint(i, 420);
            } else if (k == NAV_OK) {
                *tgl_val(g.tgl) = !*tgl_val(g.tgl);
                tgl_paint(g.tgl, 560);
            } else if (k == NAV_BACK) page_go(PG_CONSOLES);
            break;
    }
}

static void set_stream(bool on) {
    if (on) {
        lv_obj_add_flag(g.home, LV_OBJ_FLAG_HIDDEN);
        ov.open = false;
        lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
        if (!ov.tick) ov.tick = lv_timer_create(ov_tick, 1000, NULL);
    } else {
        if (ov.tick) {
            lv_timer_del(ov.tick);
            ov.tick = NULL;
        }
        ov.open = false;
        lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g.home, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < N_CARDS; i++) {
            lv_obj_set_pos(g.cards[i].card, CX - 130, CAR_Y - 165);
            lv_obj_set_size(g.cards[i].card, 260, 330);
        }
        layout_cards(700);
    }
}

/* clang-format off */
static const step_t g_script[] = {
    S_SHOT("home", "Consoles: a spring carousel, the focused card centred and the others scaled back"),
    S_REC("tour", "Browsing: cards spring into place, the glow follows the console"),
    S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(900),
    S_KEY(NAV_RIGHT), S_WAIT(1000), S_KEY(NAV_LEFT), S_WAIT(380), S_KEY(NAV_LEFT), S_WAIT(380), S_KEY(NAV_LEFT),
    S_WAIT(380), S_KEY(NAV_LEFT), S_WAIT(1200),
    S_REC_END,
    S_REC("settings", "Pages: DOWN slides to Stream (ring gauges) and Controller (round toggles)"),
    S_WAIT(400), S_KEY(NAV_DOWN), S_WAIT(1500), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(600),
    S_KEY(NAV_OK), S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_RIGHT),
    S_WAIT(700), S_KEY(NAV_LEFT), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(400), S_KEY(NAV_RIGHT), S_WAIT(500),
    S_KEY(NAV_OK), S_WAIT(400), S_KEY(NAV_LEFT), S_WAIT(800),
    S_SHOT("settings", "Stream: four ring gauges, Cross to adjust one with LEFT/RIGHT"),
    S_KEY(NAV_OK), S_WAIT(400), S_KEY(NAV_DOWN), S_WAIT(1500), S_KEY(NAV_OK), S_WAIT(800), S_KEY(NAV_RIGHT),
    S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_OK), S_WAIT(900),
    S_SHOT("controller", "Controller: round toggles that fill like activity rings"),
    S_KEY(NAV_UP), S_WAIT(900), S_KEY(NAV_UP), S_WAIT(1300),
    S_REC_END,
    S_REC("stream", "Play, then the dock over the game: UP/DOWN change the focused setting"),
    S_KEY(NAV_OK), S_WAIT(2900), S_KEY(NAV_MENU), S_WAIT(1300), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_DOWN),
    S_WAIT(600), S_KEY(NAV_UP), S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_DOWN), S_WAIT(500),
    S_KEY(NAV_UP), S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_OK), S_WAIT(500), S_KEY(NAV_LEFT),
    S_WAIT(1200),
    S_SHOT("stream-menu", "The dock: round buttons, a tooltip that follows the focus, live stats rings"),
    S_KEY(NAV_BACK), S_WAIT(1200), S_KEY(NAV_MENU), S_WAIT(1000), S_KEY(NAV_RIGHT), S_WAIT(250), S_KEY(NAV_RIGHT),
    S_WAIT(250), S_KEY(NAV_RIGHT), S_WAIT(250), S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1700),
    S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_orbit = {
    .id = "02-orbit",
    .name = "Orbit",
    .tagline = "The NXP smartwatch demo at TV scale: a spring carousel, ring gauges and a floating dock",
    .notes =
        "<p><b>Layout.</b> Full-screen activities stacked vertically, Consoles, Stream and Controller, with a "
        "pager on the right edge. Consoles is a carousel: the focused card centred and big, the neighbours "
        "scaled back and dimmed, under faint orbit rings with slowly circling satellites and a glow tinted by the "
        "console. Stream shows four ring gauges (resolution, frame rate, bitrate, smoothing); Controller shows "
        "three round toggles that fill from the centre.</p>"
        "<p><b>Navigation.</b> LEFT/RIGHT inside an activity, UP/DOWN slide to the next one like the watch's "
        "swipe. Cross plays the focused console, or puts a ring in adjust mode (arrows appear, LEFT/RIGHT sweep "
        "the ring and count the number).</p>"
        "<p><b>In the stream.</b> A floating pill dock at the bottom centre with five round buttons, a white "
        "tooltip gliding above the focused one, the session chip top left and three live stats rings top right. "
        "UP/DOWN change the focused setting (a crown-like gesture), Cross cycles it. Covers about a tenth of the "
        "video.</p>"
        "<p><b>Cost.</b> The most animated concept: every carousel step resizes five cards on a spring and repaints "
        "the middle band, page changes fade a full-screen layer, and the satellites keep a few small areas "
        "repainting all the time (they can be dropped on the TV).</p>",
    .build = build,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_script,
};
