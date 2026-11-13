/* design/menu/c_home.c - concept 01 "Home": the PS5 home screen.
 *
 * A row of tiles with the focused one enlarged at the left edge and its name beside it; below,
 * the focused console's title and a big Play button on a backdrop tinted by that console, plus
 * quick cards. "Consoles | Settings" at the top switches the row, like Games | Media. Settings
 * are big rows whose values change with LEFT/RIGHT in place. In the stream the menu is a
 * Control Center sheet that rises from the bottom over the game.
 */
#include <stdio.h>
#include <string.h>

#include "art.h"
#include "concept.h"

#define TILE_S   176
#define TILE_F   256
#define TILE_GAP 28
#define ROW_Y    168
#define NAME_W   430
#define HERO_Y   488
#define ART_CX   1520
#define ART_CY   650
#define FLOOR_Y  884 /* the key art stands on this line */

/* Where each key-art drawing touches the floor: the share of its height below that line (its
 * contact shadow), and a sideways nudge that centres its visual mass. [0] PS4, [1] PS5. From
 * art/README.md; update them when the drawings change. */
static const float g_floor_below[2] = { 20.0f / 724.0f, 10.0f / 1232.0f };
static const lv_coord_t g_art_nudge[2] = { 0, 0 };
#define QCARD_W  360
#define PANEL_Y  468
#define PROW_W   1160
#define PROW_H   96
#define PROW_GAP 10
#define SHEET_H  404
#define CARD_W   312
#define CARD_H   180
#define CARD_GAP 32
#define MS_MOVE  280

enum { SEC_CONSOLES, SEC_SETTINGS, SEC__N };
enum { Z_TOP, Z_ROW, Z_ACTIONS, Z_CARDS, Z_PANEL };
enum { SET_STREAM, SET_PAD, SET_ACCOUNT, SET_LOG, SET_ABOUT, SET__N };
#define MAX_ITEMS 6

typedef struct {
    lv_obj_t *tile, *art, *dot;
    bool ps5, has_art;
} tile_t;
typedef struct {
    lv_obj_t *row;
    int kind;
    kit_seg_t seg;
    kit_meter_t meter;
    kit_toggle_t tgl;
} prow_t;

static const char *const g_set_names[SET__N] = { "Stream", "Controller", "PSN account", "Log", "About" };
static const char *g_set_caps[SET__N] = { "Quality and smoothness", "DualShock 4", "Signed in as player_one",
                                          "What just happened", "Chiaki webOS 0.4" };
static const uint32_t g_set_tint[SET__N] = { 0x1d4f9c, 0x58308f, 0x0e6b78, 0x334055, 0x27406a };

static struct {
    lv_obj_t *root, *home, *bg, *glow, *art[2], *add_art, *tabs[SEC__N], *row[SEC__N], *ttl, *ttl_name, *ttl_sub;
    tile_t tiles[SEC__N][MAX_ITEMS];
    int n[SEC__N], sec, focus[SEC__N], zone, action, card, prow;
    lv_obj_t *hero, *h_text, *h_over, *h_title, *h_meta, *h_dot, *play, *play_icon, *play_lbl, *opts, *cards[3],
        *card_val[3], *hints;
    lv_obj_t *panel[SET__N];
    prow_t prows[SET__N][5];
    int nprows[SET__N];
    lv_obj_t *sum_big, *sum_txt, *sum_fill;
    bool shown_ps5;
} g;

static struct {
    lv_obj_t *root, *sheet, *time, *chip[4], *cards[5], *card_val[5], *card_title[5], *note;
    int focus;
    bool open, adjusting;
    lv_timer_t *tick;
} ov;

/* ---------- helpers ---------- */

static int32_t path_shake(const lv_anim_t *a) {
    float t = (float)a->act_time / (float)a->time;
    if (t >= 1) return a->end_value;
    return (int32_t)(lv_trigo_sin((int16_t)(t * 1080)) * (1 - t) * a->start_value / LV_TRIGO_SIN_MAX);
}

static void shake(lv_obj_t *o) { kit_anim_from(o, KA_TX, 18, 0, 420, 0, path_shake); }

static lv_obj_t *icon_label(lv_obj_t *parent, const char *sym, const lv_font_t *font, uint32_t col) {
    return kit_label(parent, font, col, sym);
}

/* a pill/round button: white when focused (dark text), frosted otherwise */
static void btn_style(lv_obj_t *b) {
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_color(b, C(COL_TEXT), 0);
}

static void btn_focus(lv_obj_t *b, bool on) {
    kit_anim(b, KA_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, lv_anim_path_ease_out);
    kit_mix_color(b, LV_STYLE_TEXT_COLOR, on ? 0x0a1020 : COL_TEXT, 200);
    kit_anim(b, KA_GROW, on ? 6 : 0, 300, 0, kit_path_spring);
}

static void outline(lv_obj_t *o, lv_coord_t w, lv_coord_t pad) {
    lv_obj_set_style_outline_width(o, w, 0);
    lv_obj_set_style_outline_pad(o, pad, 0);
    lv_obj_set_style_outline_color(o, lv_color_white(), 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_TRANSP, 0);
}

/* ---------- tiles ---------- */

/* tile art is rasterized for the focused size and zoomed down with the tile */
static uint16_t tile_zoom(const tile_t *t, lv_coord_t s) { return t->has_art ? (uint16_t)(256 * s / TILE_F) : 256; }

static tile_t *new_tile(int sec, uint32_t top, uint32_t bottom) {
    tile_t *t = &g.tiles[sec][g.n[sec]++];
    t->tile = kit_obj(g.row[sec]);
    lv_obj_set_size(t->tile, TILE_S, TILE_S);
    lv_obj_set_style_radius(t->tile, 30, 0);
    lv_obj_set_style_bg_opa(t->tile, LV_OPA_COVER, 0);
    kit_grad(t->tile, top, bottom);
    lv_obj_set_style_border_width(t->tile, 2, 0);
    lv_obj_set_style_border_color(t->tile, lv_color_white(), 0);
    lv_obj_set_style_border_opa(t->tile, LV_OPA_10, 0);
    outline(t->tile, 5, 7);
    return t;
}

static void build_rows(void) {
    for (int s = 0; s < SEC__N; s++) {
        g.row[s] = kit_obj(g.home);
        lv_obj_set_size(g.row[s], SCR_W, TILE_F);
        lv_obj_set_pos(g.row[s], 0, ROW_Y);
    }
    for (int i = 0; i < MOCK_HOSTS; i++) {
        const mock_host_t *h = &mock_hosts[i];
        bool off = h->state == HS_OFFLINE;
        tile_t *t = new_tile(SEC_CONSOLES, off ? 0x323a4b : h->ps5 ? 0x2d528f : 0x3b4fa0, off ? 0x161b26 : 0x0f1a33);
        t->ps5 = h->ps5;
        t->has_art = true;
        t->art = kit_console_art_box(t->tile, h->ps5, TILE_F * 88 / 100, TILE_F * 80 / 100);
        lv_obj_center(t->art);
        lv_img_set_zoom(t->art, tile_zoom(t, TILE_S));
        if (off) lv_obj_set_style_img_opa(t->art, LV_OPA_50, 0);
        t->dot = kit_dot(t->tile, 18, mock_state_col(h->state));
        lv_obj_align(t->dot, LV_ALIGN_TOP_RIGHT, -18, 18);
    }
    tile_t *add = new_tile(SEC_CONSOLES, 0x1a2338, 0x101626);
    lv_obj_set_style_bg_opa(add->tile, LV_OPA_40, 0);
    lv_obj_set_style_border_width(add->tile, 3, 0);
    lv_obj_set_style_border_opa(add->tile, LV_OPA_30, 0);
    add->art = icon_label(add->tile, LV_SYMBOL_PLUS, FONT_HERO, COL_TEXT);
    lv_obj_center(add->art);

    for (int i = 0; i < SET__N; i++) {
        tile_t *t = new_tile(SEC_SETTINGS, g_set_tint[i] + 0x101010, 0x0c1424);
        if (i == SET_PAD) {
            t->art = kit_pad_art(t->tile, TILE_F * 56 / 100);
            t->has_art = true;
            lv_img_set_zoom(t->art, tile_zoom(t, TILE_S));
        } else if (i == SET_ACCOUNT) {
            t->art = kit_rect(t->tile, 92, 92, LV_RADIUS_CIRCLE, 0x16a3b8, LV_OPA_COVER);
            kit_grad(t->art, 0x2bd2c4, 0x0b6f9c);
            lv_obj_center(kit_label(t->art, FONT_TITLE, 0xffffff, "P1"));
        } else {
            static const char *const sym[SET__N] = { LV_SYMBOL_VIDEO, "", "", LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS };
            t->art = kit_rect(t->tile, 104, 104, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_20);
            lv_obj_center(icon_label(t->art, sym[i], FONT_HERO, COL_TEXT));
        }
        lv_obj_center(t->art);
    }
}

static void layout_row(int sec, uint32_t ms) {
    int f = g.focus[sec];
    for (int i = 0; i < g.n[sec]; i++) {
        tile_t *t = &g.tiles[sec][i];
        lv_coord_t s = i == f ? TILE_F : TILE_S, x;
        if (i < f) x = SAFE_X - (f - i) * (TILE_S + TILE_GAP);
        else if (i == f) x = SAFE_X;
        else x = SAFE_X + TILE_F + TILE_GAP + NAME_W + (i - f - 1) * (TILE_S + TILE_GAP);
        kit_anim(t->tile, KA_X, x, ms, 0, lv_anim_path_ease_out);
        kit_anim(t->tile, KA_W, s, ms, 0, lv_anim_path_ease_out);
        kit_anim(t->tile, KA_H, s, ms, 0, lv_anim_path_ease_out);
        kit_anim(t->tile, KA_OPA, i < f ? LV_OPA_TRANSP : LV_OPA_COVER, ms * 2 / 3, 0, lv_anim_path_ease_out);
        if (t->has_art) kit_anim(t->art, KA_ZOOM, tile_zoom(t, s), ms, 0, lv_anim_path_ease_out);
        kit_anim(t->tile, KA_OUTLINE_OPA, i == f && g.zone == Z_ROW && g.sec == sec ? LV_OPA_COVER : LV_OPA_TRANSP, 160,
                 0, lv_anim_path_ease_out);
    }
}

/* ---------- hero (consoles) ---------- */

static void hero_set(bool anim) {
    int f = g.focus[SEC_CONSOLES];
    const mock_host_t *h = f < MOCK_HOSTS ? &mock_hosts[f] : NULL;
    uint32_t tint = h ? h->tint : 0x24324f;
    if (h) {
        lv_label_set_text_fmt(g.h_over, "%s   " LV_SYMBOL_BULLET "   %s", h->ps5 ? "PLAYSTATION 5" : "PLAYSTATION 4",
                              h->addr);
        lv_label_set_text(g.h_title, h->name);
        lv_label_set_text_fmt(g.h_meta, "%s" BULLET "%s", mock_state_text(h->state),
                              h->state == HS_OFFLINE ? "Turn it on or check its network" : h->last);
        lv_obj_set_style_bg_color(g.h_dot, C(mock_state_col(h->state)), 0);
        lv_obj_set_style_shadow_color(g.h_dot, C(mock_state_col(h->state)), 0);
        lv_obj_clear_flag(g.h_dot, LV_OBJ_FLAG_HIDDEN);
        static const char *const verb[] = { "Start Remote Play", "Wake and play", "Offline", "Play over PSN" };
        static const char *const sym[] = { LV_SYMBOL_PLAY, LV_SYMBOL_POWER, LV_SYMBOL_CLOSE, LV_SYMBOL_PLAY };
        lv_label_set_text(g.play_lbl, verb[h->state]);
        lv_label_set_text(g.play_icon, sym[h->state]);
        lv_obj_set_style_opa(g.play, h->state == HS_OFFLINE ? LV_OPA_60 : LV_OPA_COVER, 0);
        lv_obj_clear_flag(g.opts, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g.h_over, "NEW CONSOLE");
        lv_label_set_text(g.h_title, "Add a console");
        lv_label_set_text(g.h_meta, "Pair a PS5 or PS4 with its PIN, or sign in with PSN");
        lv_obj_add_flag(g.h_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g.play_lbl, "Pair a console");
        lv_label_set_text(g.play_icon, LV_SYMBOL_PLUS);
        lv_obj_set_style_opa(g.play, LV_OPA_COVER, 0);
        lv_obj_add_flag(g.opts, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t ms = anim ? 420 : 0;
    kit_mix_color(g.bg, LV_STYLE_BG_COLOR, tint, ms);
    kit_mix_color(g.glow, LV_STYLE_IMG_RECOLOR, tint + 0x202020, ms);
    if (anim) {
        kit_anim_from(g.h_text, KA_TX, 36, 0, 360, 0, lv_anim_path_ease_out);
        kit_anim_from(g.h_text, KA_OPA, 0, 255, 300, 0, lv_anim_path_ease_out);
    }
    /* key art: the console slides in from the right, the other one fades */
    for (int i = 0; i < 2; i++) {
        bool on = h && h->ps5 == (i == 1);
        if (!on) {
            kit_anim(g.art[i], KA_IMG_OPA, 0, anim ? 160 : 0, 0, NULL);
            continue;
        }
        lv_obj_set_style_img_opa(g.art[i], h->state == HS_OFFLINE ? LV_OPA_60 : LV_OPA_COVER, 0);
        if (anim) {
            kit_anim_from(g.art[i], KA_TX, 70, 0, 520, 0, lv_anim_path_ease_out);
            kit_anim_from(g.art[i], KA_IMG_OPA, 0, h->state == HS_OFFLINE ? LV_OPA_60 : LV_OPA_COVER, 420, 0, NULL);
        }
    }
    if (h) lv_obj_add_flag(g.add_art, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_clear_flag(g.add_art, LV_OBJ_FLAG_HIDDEN);
        if (anim) kit_anim_from(g.add_art, KA_OPA, 0, 255, 360, 0, NULL);
    }
}

static lv_obj_t *quick_card(lv_obj_t *parent, int i, const char *title, const char *value, lv_obj_t **val_out) {
    lv_obj_t *c = kit_rect(parent, QCARD_W, 132, 26, 0xffffff, LV_OPA_10);
    lv_obj_set_pos(c, i * (QCARD_W + 28), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, LV_OPA_10, 0);
    outline(c, 5, 6);
    lv_obj_t *ic = kit_rect(c, 72, 72, 20, COL_ACCENT, LV_OPA_COVER);
    kit_grad(ic, COL_ACCENT_HI, COL_ACCENT_DEEP);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 24, 0);
    if (i == 2) lv_obj_center(kit_pad_art(ic, 40));
    else lv_obj_center(icon_label(ic, i == 0 ? LV_SYMBOL_VIDEO : LV_SYMBOL_LOOP, FONT_TITLE, 0xffffff));
    lv_obj_t *t = kit_label(c, FONT_CAPTION, COL_MUTED, title);
    lv_obj_set_pos(t, 120, 30);
    *val_out = kit_label(c, FONT_HEADING, COL_TEXT, value);
    lv_obj_set_pos(*val_out, 120, 60);
    return c;
}

static void build_hero(void) {
    /* key art on the right, like a product shot: a glow behind, the console standing on its shadow */
    g.glow = kit_glow(g.home, mock_hosts[0].tint, LV_OPA_60);
    lv_obj_align(g.glow, LV_ALIGN_TOP_LEFT, ART_CX - KIT_GLOW_D / 2, ART_CY - KIT_GLOW_D / 2);
    for (int i = 0; i < 2; i++) {
        /* both stand on the floor: the drawings end with their ground shadow */
        g.art[i] = kit_console_art_box(g.home, i == 1, 580, 470);
        const lv_img_dsc_t *d = lv_img_get_src(g.art[i]);
        if (!d) continue;
        lv_coord_t below = (lv_coord_t)((float)d->header.h * g_floor_below[i]);
        lv_obj_set_pos(g.art[i], ART_CX - (lv_coord_t)d->header.w / 2 + g_art_nudge[i],
                       FLOOR_Y - (lv_coord_t)d->header.h + below);
        lv_obj_set_style_img_opa(g.art[i], 0, 0);
    }
    g.add_art = kit_rect(g.home, 340, 340, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_set_style_border_width(g.add_art, 4, 0);
    lv_obj_set_style_border_color(g.add_art, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.add_art, LV_OPA_40, 0);
    lv_obj_set_pos(g.add_art, ART_CX - 170, ART_CY - 170);
    lv_obj_center(icon_label(g.add_art, LV_SYMBOL_PLUS, FONT_HERO, COL_TEXT));

    g.hero = kit_obj(g.home);
    lv_obj_set_size(g.hero, 1300, 540);
    lv_obj_set_pos(g.hero, SAFE_X, HERO_Y);
    g.h_text = kit_obj(g.hero);
    lv_obj_set_size(g.h_text, 1100, 150);
    g.h_over = kit_label(g.h_text, FONT_SMALL, COL_ACCENT_HI, "");
    lv_obj_set_style_text_letter_space(g.h_over, 3, 0);
    g.h_title = kit_label(g.h_text, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.h_title, -2, 26);
    g.h_dot = kit_dot(g.h_text, 14, COL_OK);
    lv_obj_set_pos(g.h_dot, 2, 108);
    g.h_meta = kit_label(g.h_text, FONT_BODY, 0xb4bfd6, "");
    lv_obj_set_pos(g.h_meta, 30, 100);

    g.play = kit_obj(g.hero);
    lv_obj_set_size(g.play, 480, 100);
    lv_obj_set_pos(g.play, 0, 176);
    btn_style(g.play);
    lv_obj_t *pr = kit_obj(g.play);
    lv_obj_set_size(pr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pr, SP_L, 0);
    lv_obj_center(pr);
    g.play_icon = lv_label_create(pr);
    lv_obj_set_style_text_font(g.play_icon, FONT_TITLE, 0);
    g.play_lbl = lv_label_create(pr);
    lv_obj_set_style_text_font(g.play_lbl, FONT_HEADING, 0);

    g.opts = kit_obj(g.hero);
    lv_obj_set_size(g.opts, 100, 100);
    lv_obj_set_pos(g.opts, 504, 176);
    btn_style(g.opts);
    lv_obj_t *dots = lv_label_create(g.opts);
    lv_label_set_text(dots, LV_SYMBOL_BULLET LV_SYMBOL_BULLET LV_SYMBOL_BULLET);
    lv_obj_set_style_text_font(dots, FONT_TITLE, 0);
    lv_obj_set_style_text_letter_space(dots, 4, 0);
    lv_obj_center(dots);

    lv_obj_t *cards = kit_obj(g.hero);
    lv_obj_set_size(cards, 1300, 132);
    lv_obj_set_pos(cards, 0, 334);
    g.cards[0] = quick_card(cards, 0, "Stream quality", mock_quality_text(), &g.card_val[0]);
    g.cards[1] = quick_card(cards, 1, "Smooth playback", mock_smooth_opts[mock_set.smooth], &g.card_val[1]);
    g.cards[2] = quick_card(cards, 2, "Controller", mock_pad_name(), &g.card_val[2]);
}

/* ---------- settings panels ---------- */

enum { PK_SEG, PK_METER, PK_TOGGLE, PK_INFO };

static prow_t *panel_row(int p, const char *title, const char *caption) {
    prow_t *r = &g.prows[p][g.nprows[p]];
    int i = g.nprows[p]++;
    r->row = kit_rect(g.panel[p], PROW_W, PROW_H, 26, 0xffffff, LV_OPA_10 / 2);
    lv_obj_set_pos(r->row, 0, i * (PROW_H + PROW_GAP));
    outline(r->row, 4, 4);
    lv_obj_t *t = kit_label(r->row, FONT_HEADING, COL_TEXT, title);
    lv_obj_set_pos(t, 32, 14);
    lv_obj_t *c = kit_label(r->row, FONT_CAPTION, COL_MUTED, caption);
    lv_obj_set_pos(c, 32, 52);
    return r;
}

static void row_seg(prow_t *r, const char *const *opts, int n, int sel, lv_coord_t w) {
    r->kind = PK_SEG;
    kit_seg_create(&r->seg, r->row, opts, n, sel, w);
    lv_obj_align(r->seg.box, LV_ALIGN_RIGHT_MID, -14, 0);
}

static void row_toggle(prow_t *r, bool v) {
    r->kind = PK_TOGGLE;
    kit_toggle_create(&r->tgl, r->row, v);
    lv_obj_align(r->tgl.track, LV_ALIGN_RIGHT_MID, -24, 0);
}

static lv_obj_t *summary_card(lv_obj_t *panel, const char *over) {
    lv_obj_t *s = kit_rect(panel, 1728 - PROW_W - 40, 5 * PROW_H + 4 * PROW_GAP, 30, 0x000000, LV_OPA_20);
    lv_obj_set_pos(s, PROW_W + 40, 0);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(s, 36, 0);
    lv_obj_t *o = kit_label(s, FONT_SMALL, COL_ACCENT_HI, over);
    lv_obj_set_style_text_letter_space(o, 3, 0);
    return s;
}

static void summary_refresh(void) {
    if (!g.sum_big) return;
    lv_label_set_text_fmt(g.sum_big, "%sp%d", mock_set.res ? "1080" : "720", mock_set.fps ? 60 : 30);
    lv_label_set_text(g.sum_txt, mock_summary_text());
    kit_anim(g.sum_fill, KA_W, 16 + mock_set.bitrate * 400 / MOCK_BITRATE_MAX, 260, 0, NULL);
}

static void build_panels(void) {
    for (int p = 0; p < SET__N; p++) {
        g.panel[p] = kit_obj(g.home);
        lv_obj_set_size(g.panel[p], 1728, 560);
        lv_obj_set_pos(g.panel[p], SAFE_X, PANEL_Y);
        lv_obj_add_flag(g.panel[p], LV_OBJ_FLAG_HIDDEN);
    }
    row_seg(panel_row(SET_STREAM, "Resolution", "Higher needs more bandwidth"), mock_res_opts, 2, mock_set.res, 150);
    row_seg(panel_row(SET_STREAM, "Frame rate", "60 fps feels smoother in games"), mock_fps_opts, 2, mock_set.fps, 150);
    row_seg(panel_row(SET_STREAM, "Codec", "Decoded in hardware by the TV"), mock_codec_opts, 3, mock_set.codec, 150);
    prow_t *r = panel_row(SET_STREAM, "Bitrate", "Lower it if the picture stutters");
    r->kind = PK_METER;
    kit_meter_create(&r->meter, r->row, 300, MOCK_BITRATE_MIN, MOCK_BITRATE_MAX, mock_set.bitrate);
    lv_obj_align(r->meter.box, LV_ALIGN_RIGHT_MID, -36, 0);
    row_seg(panel_row(SET_STREAM, "Smooth playback", "Hides Wi-Fi stalls, adds this much delay"), mock_smooth_opts, 4,
            mock_set.smooth, 130);
    lv_obj_t *s = summary_card(g.panel[SET_STREAM], "WHAT THE CONSOLE SENDS");
    g.sum_big = kit_label(s, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.sum_big, 0, 40);
    g.sum_txt = kit_label(s, FONT_BODY, COL_MUTED, "");
    lv_obj_set_pos(g.sum_txt, 0, 110);
    lv_obj_t *bw = kit_label(s, FONT_SMALL, COL_ACCENT_HI, "BANDWIDTH");
    lv_obj_set_style_text_letter_space(bw, 3, 0);
    lv_obj_set_pos(bw, 0, 210);
    lv_obj_t *track = kit_rect(s, 416, 16, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_set_pos(track, 0, 244);
    g.sum_fill = kit_rect(track, 100, 16, LV_RADIUS_CIRCLE, COL_ACCENT_HI, LV_OPA_COVER);
    lv_obj_t *n = kit_label(s, FONT_CAPTION, COL_MUTED, "Video settings apply the next time\nyou connect.");
    lv_obj_set_pos(n, 0, 300);
    summary_refresh();

    row_toggle(panel_row(SET_PAD, "Rumble", "Vibrate with in-game feedback"), mock_set.rumble);
    row_toggle(panel_row(SET_PAD, "Touchpad gestures", "Map swipes to the PS touchpad"), mock_set.touchpad);
    row_toggle(panel_row(SET_PAD, "Motion controls", "Use the gyro for aiming"), mock_set.motion);
    s = summary_card(g.panel[SET_PAD], "CONNECTED");
    lv_obj_t *pad = kit_pad_art(s, 190);
    lv_obj_set_pos(pad, -6, 36);
    lv_obj_t *pn = kit_label(s, FONT_TITLE, COL_TEXT, mock_pad_name());
    lv_obj_set_pos(pn, 0, 240);
    lv_obj_t *pc = kit_label(s, FONT_CAPTION, COL_MUTED, "Bluetooth" BULLET "Cross selects, Circle goes back");
    lv_obj_set_pos(pc, 0, 286);

    static const char *const acc[][2] = { { "Signed in as player_one", "Consoles linked to this account show up here" },
                                          { "Find my consoles", "Looks up the consoles on your account" },
                                          { "Sign out", "Keeps the consoles you paired with a PIN" } };
    for (int i = 0; i < 3; i++) panel_row(SET_ACCOUNT, acc[i][0], acc[i][1])->kind = PK_INFO;
    static const char *const logl[][2] = {
        { "21:40  Stream started with Living Room PS5", "1080p, 60 fps, H.265 at 15 Mbps" },
        { "21:40  Controller ready", "Wireless Controller grabbed" },
        { "21:38  Living Room PS5 woke up", "Wake-up sent over the LAN" },
    };
    for (int i = 0; i < 3; i++) panel_row(SET_LOG, logl[i][0], logl[i][1])->kind = PK_INFO;
    static const char *const about[][2] = { { "Chiaki webOS 0.4", "PlayStation Remote Play for LG webOS TVs" },
                                            { "Built on chiaki-ng", "libchiaki, LVGL 8.3, SDL2" } };
    for (int i = 0; i < 2; i++) panel_row(SET_ABOUT, about[i][0], about[i][1])->kind = PK_INFO;
}

static bool panel_interactive(int p) { return p == SET_STREAM || p == SET_PAD; }

/* ---------- focus painting ---------- */

static void paint(void) {
    for (int s = 0; s < SEC__N; s++) {
        bool act = s == g.sec, foc = act && g.zone == Z_TOP;
        kit_anim(g.tabs[s], KA_BG_OPA, foc ? LV_OPA_COVER : LV_OPA_TRANSP, 180, 0, NULL);
        kit_mix_color(g.tabs[s], LV_STYLE_TEXT_COLOR, foc ? 0x0a1020 : act ? COL_TEXT : COL_DIM, 180);
    }
    layout_row(g.sec, MS_MOVE);
    bool cons = g.sec == SEC_CONSOLES;
    btn_focus(g.play, cons && g.zone == Z_ACTIONS && g.action == 0);
    btn_focus(g.opts, cons && g.zone == Z_ACTIONS && g.action == 1);
    for (int i = 0; i < 3; i++) {
        bool on = cons && g.zone == Z_CARDS && g.card == i;
        kit_anim(g.cards[i], KA_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
        kit_anim(g.cards[i], KA_GROW, on ? 6 : 0, 300, 0, kit_path_spring);
        kit_anim(g.cards[i], KA_BG_OPA, on ? LV_OPA_20 : LV_OPA_10, 160, 0, NULL);
    }
    int p = g.focus[SEC_SETTINGS];
    for (int i = 0; i < g.nprows[p]; i++) {
        prow_t *r = &g.prows[p][i];
        bool on = !cons && g.zone == Z_PANEL && g.prow == i;
        kit_anim(r->row, KA_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
        kit_anim(r->row, KA_BG_OPA, on ? LV_OPA_20 : LV_OPA_10 / 2, 160, 0, NULL);
        if (r->kind == PK_SEG && r->seg.focused != on) kit_seg_focus(&r->seg, on);
        if (r->kind == PK_METER) kit_meter_focus(&r->meter, on);
    }
}

/* title next to the focused tile: it follows the tile in, after the tile has mostly landed */
static void ttl_set(bool anim, int dir) {
    int f = g.focus[g.sec];
    if (g.sec == SEC_CONSOLES) {
        const mock_host_t *h = f < MOCK_HOSTS ? &mock_hosts[f] : NULL;
        lv_label_set_text(g.ttl_name, h ? h->name : "Add a console");
        lv_label_set_text(g.ttl_sub, h ? mock_state_text(h->state) : "PIN or PSN account");
    } else {
        lv_label_set_text(g.ttl_name, g_set_names[f]);
        lv_label_set_text(g.ttl_sub, g_set_caps[f]);
    }
    if (anim) {
        kit_anim_from(g.ttl, KA_TX, dir * 48, 0, 320, 150, lv_anim_path_ease_out);
        kit_anim_from(g.ttl, KA_OPA, 0, 255, 260, 150, lv_anim_path_ease_out);
    }
}

static void panel_show(bool anim) {
    for (int p = 0; p < SET__N; p++) lv_obj_add_flag(g.panel[p], LV_OBJ_FLAG_HIDDEN);
    if (g.sec != SEC_SETTINGS) return;
    lv_obj_t *pn = g.panel[g.focus[SEC_SETTINGS]];
    lv_obj_clear_flag(pn, LV_OBJ_FLAG_HIDDEN);
    if (anim) kit_enter(pn, 40, 28);
    kit_mix_color(g.bg, LV_STYLE_BG_COLOR, g_set_tint[g.focus[SEC_SETTINGS]], anim ? 420 : 0);
}

static void section_set(int sec, bool anim) {
    g.sec = sec;
    for (int s = 0; s < SEC__N; s++) {
        bool on = s == sec;
        if (on) lv_obj_clear_flag(g.row[s], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g.row[s], LV_OBJ_FLAG_HIDDEN);
    }
    bool cons = sec == SEC_CONSOLES;
    lv_obj_t *cons_objs[] = { g.hero, g.glow, g.art[0], g.art[1] };
    for (unsigned i = 0; i < sizeof(cons_objs) / sizeof(cons_objs[0]); i++) {
        if (cons) lv_obj_clear_flag(cons_objs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cons_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!cons) lv_obj_add_flag(g.add_art, LV_OBJ_FLAG_HIDDEN);
    if (anim) {
        /* the new row deals in from the right, one tile after the other */
        for (int i = 0; i < g.n[sec]; i++) {
            lv_obj_t *t = g.tiles[sec][i].tile;
            kit_anim_from(t, KA_TX, 260 + i * 40, 0, 460, (uint32_t)i * 40, lv_anim_path_ease_out);
        }
        kit_anim_from(g.row[sec], KA_OPA, 0, 255, 300, 0, lv_anim_path_ease_out);
    }
    ttl_set(anim, 1);
    if (cons) hero_set(anim);
    panel_show(anim);
}

/* ---------- connect / overlay ---------- */

static void connect_focused(void) {
    int f = g.focus[SEC_CONSOLES];
    if (f >= MOCK_HOSTS) {
        shake(g.play);
        return;
    }
    if (mock_hosts[f].state == HS_OFFLINE) {
        shake(g.play);
        return;
    }
    kit_connect_screen(&mock_hosts[f]);
}

/* a card's value; while it is being adjusted, between the LEFT/RIGHT arrows */
static const char *ov_val(int i) {
    static char buf[80];
    const char *v = i == 1            ? mock_quality_text()
                    : i == 2          ? mock_smooth_opts[mock_set.smooth]
                    : mock_set.rumble ? "Rumble on"
                                      : "Rumble off";
    if (ov.adjusting && i == ov.focus) snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, v);
    else snprintf(buf, sizeof(buf), "%s", v);
    return buf;
}

static void ov_values(void) {
    for (int i = 1; i <= 3; i++) lv_label_set_text(ov.card_val[i], ov_val(i));
}

static void ov_paint(void) {
    for (int i = 0; i < 5; i++) {
        bool on = i == ov.focus;
        bool danger = i == 4;
        kit_anim(ov.cards[i], KA_BG_OPA, on ? LV_OPA_COVER : LV_OPA_10, 200, 0, NULL);
        lv_obj_set_style_bg_color(ov.cards[i], on && danger ? C(COL_ERR) : lv_color_white(), 0);
        kit_mix_color(ov.cards[i], LV_STYLE_TEXT_COLOR, on && !danger ? 0x0a1020 : COL_TEXT, 200);
        kit_anim(ov.cards[i], KA_GROW, on ? 8 : 0, 320, 0, kit_path_spring);
        kit_anim(ov.cards[i], KA_TY, on ? -12 : 0, 320, 0, kit_path_spring);
        bool adj = on && ov.adjusting;
        lv_obj_set_style_border_color(ov.cards[i], C(adj ? COL_ACCENT_HI : 0xffffff), 0);
        kit_anim(ov.cards[i], KA_BORDER_OPA, adj ? LV_OPA_COVER : LV_OPA_10, 160, 0, NULL);
        lv_obj_set_style_border_width(ov.cards[i], adj ? 5 : 2, 0);
    }
}

static void ov_hidden_cb(lv_obj_t *sheet) { lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN); }

static void ov_open(bool open) {
    ov.open = open;
    ov.adjusting = false;
    lv_anim_del(ov.sheet, NULL);
    if (open) {
        ov.focus = 0;
        ov_values();
        lv_obj_clear_flag(ov.sheet, LV_OBJ_FLAG_HIDDEN);
        kit_anim_from(ov.sheet, KA_TY, SHEET_H + 60, 0, 420, 0, lv_anim_path_ease_out);
        for (int i = 0; i < 5; i++) {
            kit_anim_from(ov.cards[i], KA_TY, 70, i == 0 ? -12 : 0, 420, 90 + (uint32_t)i * 45, lv_anim_path_ease_out);
            kit_anim_from(ov.cards[i], KA_OPA, 0, 255, 300, 90 + (uint32_t)i * 45, NULL);
        }
        ov_paint();
        lv_label_set_text(ov.note, "");
    } else {
        kit_anim_then(ov.sheet, KA_TY, SHEET_H + 60, 240, lv_anim_path_ease_in, ov_hidden_cb);
    }
}

static void ov_adjust(int dir) {
    if (ov.focus == 1) {
        int q = mock_set.res * 2 + mock_set.fps + dir;
        q = q < 0 ? 0 : q > 3 ? 3 : q;
        mock_set.res = q / 2;
        mock_set.fps = q % 2;
        kit_roll_text(ov.card_val[1], ov_val(1), dir);
        kit_roll_text(ov.note, LV_SYMBOL_REFRESH "  The stream restarts with the new quality when the menu closes", 1);
    } else if (ov.focus == 2) {
        int s = mock_set.smooth + dir;
        mock_set.smooth = s < 0 ? 0 : s > 3 ? 3 : s;
        kit_roll_text(ov.card_val[2], ov_val(2), dir);
        kit_roll_text(ov.note, "Smooth playback applies right away", 1);
    } else if (ov.focus == 3) {
        mock_set.rumble = !mock_set.rumble;
        kit_roll_text(ov.card_val[3], ov_val(3), dir);
    }
}

static void ov_nav(nav_t k) {
    if (!ov.open) {
        if (k == NAV_MENU) ov_open(true);
        return;
    }
    if (ov.adjusting) {
        if (k == NAV_LEFT || k == NAV_RIGHT) ov_adjust(k == NAV_RIGHT ? 1 : -1);
        else if (k == NAV_OK || k == NAV_BACK) {
            ov.adjusting = false;
            ov_values();
            ov_paint();
        }
        return;
    }
    switch (k) {
        case NAV_LEFT:
            if (ov.focus > 0) ov.focus--;
            ov_paint();
            break;
        case NAV_RIGHT:
            if (ov.focus < 4) ov.focus++;
            ov_paint();
            break;
        case NAV_OK:
            if (ov.focus == 0) ov_open(false);
            else if (ov.focus == 4) {
                ov_open(false);
                if (kit_stream_hook) kit_stream_hook(false);
            } else {
                ov.adjusting = true;
                ov_values();
                ov_paint();
            }
            break;
        case NAV_BACK:
        case NAV_MENU: ov_open(false); break;
        default: break;
    }
}

static void ov_tick(lv_timer_t *t) {
    (void)t;
    static int n;
    n++;
    static const char *const mbps[] = { "14.8 Mbps", "15.1 Mbps", "14.6 Mbps", "15.0 Mbps" };
    lv_label_set_text_fmt(ov.time, "Streaming" BULLET "%s", mock_clock_text());
    lv_label_set_text(ov.chip[0], n % 5 == 3 ? "59 fps" : "60 fps");
    lv_label_set_text(ov.chip[1], mbps[n % 4]);
}

static lv_obj_t *ov_chip(lv_obj_t *row, const char *text, uint32_t dot) {
    lv_obj_t *c = kit_obj(row);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_10, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 10, 0);
    if (dot) kit_dot(c, 12, dot);
    return kit_label(c, FONT_CAPTION, COL_TEXT, text);
}

static void build_overlay(void) {
    ov.root = kit_obj(g.root);
    lv_obj_set_size(ov.root, SCR_W, SCR_H);
    lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
    ov.sheet = kit_rect(ov.root, SCR_W, SHEET_H + 60, 44, 0x070b16, 222);
    lv_obj_set_pos(ov.sheet, 0, SCR_H - SHEET_H);
    lv_obj_set_style_border_width(ov.sheet, 2, 0);
    lv_obj_set_style_border_side(ov.sheet, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(ov.sheet, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ov.sheet, LV_OPA_10, 0);
    lv_obj_add_flag(ov.sheet, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *art = kit_console_art_h(ov.sheet, true, 80);
    const lv_img_dsc_t *d = lv_img_get_src(art);
    if (d) lv_obj_set_pos(art, SAFE_X + 30 - (lv_coord_t)d->header.w / 2, 66 - (lv_coord_t)d->header.h / 2);
    lv_obj_t *name = kit_label(ov.sheet, FONT_HEADING, COL_TEXT, mock_hosts[0].name);
    lv_obj_set_pos(name, SAFE_X + 80, 30);
    ov.time = kit_label(ov.sheet, FONT_CAPTION, COL_MUTED, "Streaming");
    lv_obj_set_pos(ov.time, SAFE_X + 80, 68);
    lv_obj_t *chips = kit_obj(ov.sheet);
    lv_obj_set_size(chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, SP_M, 0);
    lv_obj_align(chips, LV_ALIGN_TOP_RIGHT, -SAFE_X, 34);
    ov.chip[0] = ov_chip(chips, "60 fps", COL_OK);
    ov.chip[1] = ov_chip(chips, "14.8 Mbps", 0);
    ov.chip[2] = ov_chip(chips, "0 dropped", 0);
    ov.chip[3] = ov_chip(chips, "Wi-Fi stable", 0);

    static const char *const titles[5] = { "Resume", "Stream quality", "Smooth playback", "Controller", "Disconnect" };
    static const char *const vals[5] = { "Back to the game", "", "", "", "End the session" };
    static const char *const syms[5] = { LV_SYMBOL_PLAY, LV_SYMBOL_VIDEO, LV_SYMBOL_LOOP, "", LV_SYMBOL_POWER };
    lv_coord_t x0 = (SCR_W - (5 * CARD_W + 4 * CARD_GAP)) / 2;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *c = ov.cards[i] = kit_rect(ov.sheet, CARD_W, CARD_H, 30, 0xffffff, LV_OPA_10);
        lv_obj_set_pos(c, x0 + i * (CARD_W + CARD_GAP), 124);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_border_color(c, lv_color_white(), 0);
        lv_obj_set_style_border_opa(c, LV_OPA_10, 0);
        lv_obj_set_style_text_color(c, C(COL_TEXT), 0);
        if (i == 3) {
            lv_obj_t *pad = kit_pad_art(c, 40);
            lv_obj_set_pos(pad, 22, 26);
        } else {
            lv_obj_t *ic = lv_label_create(c);
            lv_label_set_text(ic, syms[i]);
            lv_obj_set_style_text_font(ic, FONT_TITLE, 0);
            if (i == 4) lv_obj_set_style_text_color(ic, C(COL_ERR), 0);
            lv_obj_set_pos(ic, 28, 28);
        }
        ov.card_title[i] = lv_label_create(c);
        lv_label_set_text(ov.card_title[i], titles[i]);
        lv_obj_set_style_text_font(ov.card_title[i], FONT_HEADING, 0);
        lv_obj_set_pos(ov.card_title[i], 28, 92);
        ov.card_val[i] = lv_label_create(c);
        lv_label_set_text(ov.card_val[i], vals[i]);
        lv_obj_set_style_text_font(ov.card_val[i], FONT_CAPTION, 0);
        lv_obj_set_style_text_opa(ov.card_val[i], LV_OPA_70, 0);
        lv_obj_set_pos(ov.card_val[i], 28, 132);
    }
    lv_obj_t *hints = kit_hints(ov.sheet);
    lv_obj_set_pos(hints, x0, 334);
    kit_hint_key(hints, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, "Move");
    kit_hint(hints, PS_CROSS, "Select / adjust");
    kit_hint(hints, PS_CIRCLE, "Back to the game");
    ov.note = kit_label(ov.sheet, FONT_CAPTION, COL_ACCENT_HI, "");
    lv_obj_align(ov.note, LV_ALIGN_TOP_RIGHT, -x0, 342);
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
    g_set_caps[SET_PAD] = mock_pad_name();
    g.root = root;
    lv_obj_add_event_cb(root, root_deleted_cb, LV_EVENT_DELETE, NULL);
    g.home = kit_obj(root);
    lv_obj_set_size(g.home, SCR_W, SCR_H);
    g.bg = kit_rect(g.home, SCR_W, SCR_H, 0, mock_hosts[0].tint, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(g.bg, C(COL_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(g.bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(g.bg, 0, 0);
    lv_obj_set_style_bg_grad_stop(g.bg, 235, 0);

    build_hero();
    build_rows();
    build_panels();

    g.ttl = kit_obj(g.home);
    lv_obj_set_size(g.ttl, NAME_W, 120);
    lv_obj_set_pos(g.ttl, SAFE_X + TILE_F + TILE_GAP + 8, ROW_Y + TILE_F / 2 - 56);
    g.ttl_name = kit_label(g.ttl, FONT_TITLE, COL_TEXT, "");
    lv_obj_set_width(g.ttl_name, NAME_W - 20);
    lv_label_set_long_mode(g.ttl_name, LV_LABEL_LONG_DOT);
    g.ttl_sub = kit_label(g.ttl, FONT_CAPTION, 0xb4bfd6, "");
    lv_obj_set_pos(g.ttl_sub, 0, 46);

    /* top bar: Consoles | Settings, and the controller, account and clock on the right */
    lv_obj_t *tabs = kit_obj(g.home);
    lv_obj_set_size(tabs, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, SP_S, 0);
    lv_obj_set_pos(tabs, SAFE_X - 26, SAFE_Y - 8);
    static const char *const sec_names[SEC__N] = { "Consoles", "Settings" };
    for (int s = 0; s < SEC__N; s++) {
        g.tabs[s] = kit_obj(tabs);
        lv_obj_set_size(g.tabs[s], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(g.tabs[s], 26, 0);
        lv_obj_set_style_pad_ver(g.tabs[s], 10, 0);
        lv_obj_set_style_radius(g.tabs[s], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g.tabs[s], lv_color_white(), 0);
        lv_obj_set_style_text_color(g.tabs[s], C(s ? COL_DIM : COL_TEXT), 0);
        lv_obj_t *l = lv_label_create(g.tabs[s]);
        lv_label_set_text(l, sec_names[s]);
        lv_obj_set_style_text_font(l, FONT_TITLE, 0);
    }
    lv_obj_t *right = kit_obj(g.home);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, SP_XL, 0);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -SAFE_X, SAFE_Y - 4);
    lv_obj_t *pad = kit_obj(right);
    lv_obj_set_size(pad, 72, 44);
    lv_obj_align(kit_pad_art(pad, 40), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(kit_dot(pad, 12, COL_OK), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *av = kit_rect(right, 52, 52, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    kit_grad(av, 0x2bd2c4, 0x0b6f9c);
    lv_obj_center(kit_label(av, FONT_CAPTION, 0xffffff, "P1"));
    kit_label(right, FONT_TITLE, COL_TEXT, "21:42");

    g.hints = kit_hints(g.home);
    lv_obj_align(g.hints, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -SAFE_Y + 14);
    kit_hint(g.hints, PS_CROSS, "Select");
    kit_hint(g.hints, PS_CIRCLE, "Back");

    build_overlay();

    g.zone = Z_ROW;
    section_set(SEC_CONSOLES, false);
    layout_row(SEC_CONSOLES, 0);
    paint();
    /* entrance: the row deals in, the hero rises */
    for (int i = 0; i < g.n[SEC_CONSOLES]; i++)
        kit_anim_from(g.tiles[SEC_CONSOLES][i].tile, KA_TX, 200 + i * 60, 0, 600, (uint32_t)i * 50,
                      lv_anim_path_ease_out);
    kit_enter(g.hero, 180, 40);
    hero_set(true);
}

static void nav(nav_t k) {
    if (ov.root && !lv_obj_has_flag(ov.root, LV_OBJ_FLAG_HIDDEN)) {
        ov_nav(k);
        return;
    }
    int *f = &g.focus[g.sec];
    bool cons = g.sec == SEC_CONSOLES;
    switch (g.zone) {
        case Z_TOP:
            if ((k == NAV_LEFT && g.sec > 0) || (k == NAV_RIGHT && g.sec < SEC__N - 1)) {
                section_set(g.sec + (k == NAV_RIGHT ? 1 : -1), true);
            } else if (k == NAV_DOWN || k == NAV_OK) g.zone = Z_ROW;
            break;
        case Z_ROW:
            if ((k == NAV_LEFT && *f > 0) || (k == NAV_RIGHT && *f < g.n[g.sec] - 1)) {
                *f += k == NAV_RIGHT ? 1 : -1;
                ttl_set(true, k == NAV_RIGHT ? 1 : -1);
                if (cons) hero_set(true);
                else panel_show(true);
            } else if (k == NAV_UP) g.zone = Z_TOP;
            else if (k == NAV_DOWN || k == NAV_OK) {
                if (cons) {
                    if (k == NAV_OK) {
                        connect_focused();
                        break;
                    }
                    g.zone = Z_ACTIONS;
                    g.action = 0;
                } else if (panel_interactive(*f)) {
                    g.zone = Z_PANEL;
                    g.prow = 0;
                }
            }
            break;
        case Z_ACTIONS:
            if (k == NAV_LEFT || k == NAV_RIGHT) {
                if (!lv_obj_has_flag(g.opts, LV_OBJ_FLAG_HIDDEN)) g.action = k == NAV_RIGHT;
            } else if (k == NAV_UP || k == NAV_BACK) g.zone = Z_ROW;
            else if (k == NAV_DOWN) {
                g.zone = Z_CARDS;
                g.card = 0;
            } else if (k == NAV_OK && g.action == 0) connect_focused();
            else if (k == NAV_OK) shake(g.opts);
            break;
        case Z_CARDS:
            if (k == NAV_LEFT && g.card > 0) g.card--;
            else if (k == NAV_RIGHT && g.card < 2) g.card++;
            else if (k == NAV_UP || k == NAV_BACK) g.zone = Z_ACTIONS;
            else if (k == NAV_OK) {
                /* a quick card jumps straight to its setting */
                static const int panel[3] = { SET_STREAM, SET_STREAM, SET_PAD }, row[3] = { 0, 4, 0 };
                g.focus[SEC_SETTINGS] = panel[g.card];
                section_set(SEC_SETTINGS, true);
                g.zone = Z_PANEL;
                g.prow = row[g.card];
            }
            break;
        case Z_PANEL: {
            int p = *f;
            prow_t *r = &g.prows[p][g.prow];
            if (k == NAV_UP) {
                if (g.prow > 0) g.prow--;
                else g.zone = Z_ROW;
            } else if (k == NAV_DOWN && g.prow < g.nprows[p] - 1) g.prow++;
            else if (k == NAV_BACK) g.zone = Z_ROW;
            else if (k == NAV_LEFT || k == NAV_RIGHT || k == NAV_OK) {
                int d = k == NAV_LEFT ? -1 : 1;
                if (r->kind == PK_SEG) {
                    int s = k == NAV_OK ? (r->seg.sel + 1) % r->seg.n : r->seg.sel + d;
                    if (s < 0 || s >= r->seg.n) {
                        shake(r->seg.box);
                        break;
                    }
                    kit_seg_select(&r->seg, s);
                    if (p == SET_STREAM) {
                        int *v[] = { &mock_set.res, &mock_set.fps, &mock_set.codec, NULL, &mock_set.smooth };
                        if (v[g.prow]) *v[g.prow] = s;
                    }
                } else if (r->kind == PK_METER && k != NAV_OK) {
                    kit_meter_set(&r->meter, r->meter.val + d * 5);
                    mock_set.bitrate = r->meter.val;
                } else if (r->kind == PK_TOGGLE) {
                    kit_toggle_set(&r->tgl, k == NAV_OK ? !r->tgl.val : d > 0);
                }
                summary_refresh();
                lv_label_set_text(g.card_val[0], mock_quality_text());
                lv_label_set_text(g.card_val[1], mock_smooth_opts[mock_set.smooth]);
            }
            break;
        }
    }
    paint();
}

static void set_stream(bool on) {
    if (on) {
        lv_obj_add_flag(g.home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ov.sheet, LV_OBJ_FLAG_HIDDEN);
        ov.open = false;
        if (!ov.tick) ov.tick = lv_timer_create(ov_tick, 1000, NULL);
        ov_tick(NULL);
    } else {
        if (ov.tick) {
            lv_timer_del(ov.tick);
            ov.tick = NULL;
        }
        lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g.home, LV_OBJ_FLAG_HIDDEN);
        g.zone = Z_ACTIONS;
        paint();
        kit_anim_from(g.home, KA_OPA, 0, 255, 420, 0, lv_anim_path_ease_out);
    }
}

/* clang-format off */
static const step_t g_script[] = {
    S_SHOT("home", "Home: the focused console sits enlarged at the left edge, its name beside it"),
    S_REC("tour", "Browsing consoles: tiles glide, the backdrop and key art follow the focus"),
    S_WAIT(800), S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(900),
    S_KEY(NAV_RIGHT), S_WAIT(1000), S_KEY(NAV_LEFT), S_WAIT(450), S_KEY(NAV_LEFT), S_WAIT(450), S_KEY(NAV_LEFT),
    S_WAIT(450), S_KEY(NAV_LEFT), S_WAIT(900), S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_RIGHT), S_WAIT(600),
    S_KEY(NAV_LEFT), S_WAIT(400), S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_RIGHT), S_WAIT(550), S_KEY(NAV_RIGHT),
    S_WAIT(800), S_KEY(NAV_UP), S_WAIT(450), S_KEY(NAV_UP), S_WAIT(800),
    S_REC_END,
    S_REC("settings", "Settings: Consoles | Settings at the top, big rows, LEFT/RIGHT change values in place"),
    S_WAIT(400), S_KEY(NAV_UP), S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(1100), S_KEY(NAV_DOWN), S_WAIT(500),
    S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_LEFT), S_WAIT(700), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_DOWN),
    S_WAIT(450), S_KEY(NAV_DOWN), S_WAIT(450), S_KEY(NAV_DOWN), S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(300),
    S_KEY(NAV_RIGHT), S_WAIT(300), S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_DOWN), S_WAIT(500), S_KEY(NAV_LEFT),
    S_WAIT(700), S_KEY(NAV_RIGHT), S_WAIT(800),
    S_SHOT("settings", "Settings: segmented choices and a bitrate bar instead of dropdowns"),
    S_KEY(NAV_UP), S_WAIT(250), S_KEY(NAV_UP), S_WAIT(250), S_KEY(NAV_UP), S_WAIT(250), S_KEY(NAV_UP), S_WAIT(250),
    S_KEY(NAV_UP), S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_DOWN), S_WAIT(500), S_KEY(NAV_OK),
    S_WAIT(600), S_KEY(NAV_DOWN), S_WAIT(400), S_KEY(NAV_DOWN), S_WAIT(400), S_KEY(NAV_OK), S_WAIT(900),
    S_KEY(NAV_UP), S_WAIT(200), S_KEY(NAV_UP), S_WAIT(200), S_KEY(NAV_UP), S_WAIT(300), S_KEY(NAV_UP), S_WAIT(500),
    S_KEY(NAV_LEFT), S_WAIT(1000),
    S_REC_END,
    S_REC("stream", "Play, then the Control Center over the game (Share + Options)"),
    S_KEY(NAV_DOWN), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(2900), S_KEY(NAV_MENU), S_WAIT(1100), S_KEY(NAV_RIGHT),
    S_WAIT(500), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(450), S_KEY(NAV_LEFT), S_WAIT(550),
    S_KEY(NAV_RIGHT), S_WAIT(800), S_KEY(NAV_OK), S_WAIT(400), S_KEY(NAV_LEFT), S_WAIT(400), S_KEY(NAV_OK),
    S_WAIT(400), S_KEY(NAV_LEFT), S_WAIT(1400),
    S_SHOT("stream-menu", "Control Center: a sheet over the bottom third, the game stays in view"),
    S_KEY(NAV_BACK), S_WAIT(600), S_KEY(NAV_BACK), S_WAIT(1200), S_KEY(NAV_MENU), S_WAIT(900), S_KEY(NAV_RIGHT),
    S_WAIT(300), S_KEY(NAV_RIGHT), S_WAIT(300), S_KEY(NAV_RIGHT), S_WAIT(300), S_KEY(NAV_RIGHT), S_WAIT(700),
    S_KEY(NAV_OK), S_WAIT(1600),
    S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_home = {
    .id = "01-home",
    .name = "Home",
    .tagline = "The PS5 home screen: a tile row, a big Play button, and a Control Center over the game",
    .notes =
        "<p><b>Layout.</b> Consoles | Settings at the top (like Games | Media). The focused tile is enlarged at the "
        "left edge with its name beside it; the backdrop tint and the key art on the right follow it. Below: the "
        "console's title, a white Play pill, an options button, and quick cards that jump to their setting. "
        "Settings reuse the row: each tile opens a panel of big rows with a summary card.</p>"
        "<p><b>Navigation.</b> LEFT/RIGHT along the row, DOWN to Play, DOWN again to the quick cards, UP to the top "
        "tabs. In settings, LEFT/RIGHT change the focused value in place: segmented choices, a bitrate bar and "
        "toggles, no dropdown pop-ups. Cross plays from the row too.</p>"
        "<p><b>In the stream.</b> Share + Options raises a Control Center sheet over the bottom ~37% of the screen: "
        "session header with live stats, then Resume, Stream quality, Smooth playback, Controller and Disconnect. "
        "Cross on a card enters adjust mode (LEFT/RIGHT change it); quality changes note the restart.</p>"
        "<p><b>Cost.</b> Moving along the row repaints the row band plus the hero; the tint crossfade repaints the "
        "whole backdrop for ~0.4 s, the heaviest moment. The sheet keeps two thirds of the video untouched.</p>",
    .build = build,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_script,
};
