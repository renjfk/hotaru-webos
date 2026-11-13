/* ui/home.c - Home: tiles, hero, quick cards, the connecting screen (see home.h). */
#include "home.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../config/config.h"
#include "../log.h"
#include "art.h"
#include "controls.h"
#include "dialog.h"
#include "home_internal.h"
#include "logo.h"
#include "motion.h"
#include "register.h"
#include "splash.h"

#define TILE_S      176                     /* resting tile */
#define TILE_F      256                     /* focused tile */
#define ICON_D      (104 * TILE_F / TILE_S) /* a settings tile's circle, at the focused size: 104 px at rest */
#define LOGO_D      (112 * TILE_F / TILE_S)
#define TILE_GAP    28
#define PEEK_OPA    LV_OPA_50 /* the tile before the focus, cut by the screen edge: there is more that way */
#define ROW_Y       168
#define NAME_W      430 /* the focused tile's name, beside it */
#define HERO_Y      488
#define ART_CX      1520 /* key art: centre line and the floor it stands on */
#define ART_CY      650
#define FLOOR_Y     884
#define ART_W       580
#define ART_H       470
#define QCARD_W     360
#define QCARD_H     132
#define QCARD_SUB_Y 96 /* under a quick card's value: the controller's battery */
#define MOVE_MS     280
#define MAX_TILES   (CONFIG_MAX_HOSTS + 1)
#define TOAST_MS    4500
#define GLOW_LIFT   0x202020 /* the glow is the backdrop tint, lighter */
#define CONN_GLOW   0x3a6fd0
/* tile gradients: console tiles by kind, the Add tile, settings tiles (tint lifted by SET_LIFT) */
#define TILE_PS5     0x2d528f
#define TILE_PS4     0x3b4fa0
#define TILE_LOW     0x0f1a33
#define TILE_ADD     0x1a2338
#define TILE_ADD_LOW 0x101626
#define TILE_SET_LOW 0x0c1424
#define SET_LIFT     0x101010
/* the update notice: the app mark beside two lines, over the top bar's right end (its controller,
 * avatar and clock take no keys), centred on the top bar's 52 px row */
#define UPDATE_MS     9000 /* long enough to read the address */
#define UPDATE_WAIT   500  /* how often a waiting notice looks whether Home is free */
#define UPDATE_LOGO   48   /* as tall as the two lines */
#define UPDATE_PAD    18   /* above and below them */
#define UPDATE_SIDE   SP_XL
#define UPDATE_GAP    20
#define UPDATE_BORDER 2
#define UPDATE_H      (UPDATE_LOGO + 2 * UPDATE_PAD + 2 * UPDATE_BORDER)
#define UPDATE_Y      (HOME_SAFE_Y - 4 + 26 - UPDATE_H / 2)
#define UPDATE_RISE   24 /* it drops in from above and rises away */

enum { SEC__N = 2 };
enum { ACT__N = 3 };
enum { CARD_QUALITY, CARD_SMOOTH, CARD_PAD, CARD__N };
enum { MENU_NONE, MENU_ROUTE, MENU_OPTIONS };
enum { KEY_ART_PS4, KEY_ART_PS5, KEY_ART_UNKNOWN, KEY_ART__N }; /* the hero's key art, one at a time */

/* backdrop tints, picked per console by its name so each keeps its colour */
static const uint32_t g_tints[] = { 0x1d57b8, 0x4b3a92, 0x0e6b78, 0x7a3560, 0x2f5f8a, 0x3a4356 };

typedef struct {
    lv_obj_t *tile, *art;
    bool zooms;
} tile_t;

static struct {
    lv_obj_t *root, *bg, *glow, *art[KEY_ART__N], *tabs[SEC__N], *row[SEC__N], *ttl, *ttl_name, *ttl_sub, *hints;
    tile_t tiles[SEC__N][MAX_TILES];
    int n[SEC__N], sec, focus[SEC__N], item;
    home_zone_t zone;
    lv_obj_t *hero, *h_text, *h_over, *h_title, *h_meta, *h_dot, *actions, *btn[ACT__N], *route_lbl;
    lv_obj_t *cards, *card[CARD__N], *card_val[CARD__N], *card_pad_box, *card_pad_batt;
    lv_obj_t *pad_box, *pad_img, *pad_dot, *avatar, *avatar_lbl, *clock;
    lv_obj_t *conn, *conn_art_box, *conn_title, *conn_bar, *conn_pct, *conn_hints;
    lv_obj_t *toast, *toast_lbl;
    lv_obj_t *update, *update_title, *update_addr;
    lv_timer_t *clock_timer, *toast_timer, *update_wait, *update_hide;
    ctl_menu_t menu;
    int menu_kind;
    art_id_t pad_art;
    conn_state_t conn_shown;
} g;

static void paint(void);

/* ---------- small helpers ---------- */

static uint32_t host_tint(const app_host_t *h) {
    if (!h) return COL_TINT_NEUTRAL;
    uint32_t hash = 5381;
    for (const char *p = h->name; *p; p++) hash = hash * 33 + (unsigned char)*p;
    return g_tints[hash % (sizeof(g_tints) / sizeof(g_tints[0]))];
}

static void shake(lv_obj_t *o) { motion_from(o, MO_TX, 18, 0, 420, 0, motion_shake); }

static bool on_add_tile(void) { return g.focus[HOME_CONSOLES] >= app_host_count(); }
static const app_host_t *focused_host(void) { return app_host(g.focus[HOME_CONSOLES]); }

/* frosted pill/round button: white with dark text when focused */
static lv_obj_t *button(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *b = ctl_obj(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_color(b, C(COL_TEXT), 0);
    return b;
}

static void button_focus(lv_obj_t *b, bool on) {
    motion_to(b, MO_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, NULL);
    motion_color(b, LV_STYLE_TEXT_COLOR, on ? COL_ON_FROST : COL_TEXT, 200);
    motion_to(b, MO_GROW, on ? 6 : 0, 300, 0, motion_spring);
}

static bool action_visible(int a) { return !lv_obj_has_flag(g.btn[a], LV_OBJ_FLAG_HIDDEN); }

/* ---------- toast ---------- */

static void toast_hide_cb(lv_timer_t *t) {
    (void)t;
    g.toast_timer = NULL;
    motion_to(g.toast, MO_OPA, LV_OPA_TRANSP, 300, 0, NULL);
}

static lv_coord_t text_width(lv_obj_t *label) {
    lv_point_t sz;
    lv_txt_get_size(&sz, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0), 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* width from the text: a content-sized parent with a centred child is not resolved by LVGL 8 */
static void fit_width(lv_obj_t *box, lv_obj_t *label, lv_coord_t pad) {
    lv_obj_set_width(box, text_width(label) + 2 * pad);
}

static void toast(const char *text, uint32_t col) {
    lv_label_set_text(g.toast_lbl, text);
    fit_width(g.toast, g.toast_lbl, SP_2XL);
    lv_obj_set_style_text_color(g.toast_lbl, C(col), 0);
    motion_from(g.toast, MO_TY, 30, 0, 320, 0, NULL);
    motion_to(g.toast, MO_OPA, LV_OPA_COVER, 200, 0, NULL);
    if (g.toast_timer) lv_timer_del(g.toast_timer);
    g.toast_timer = lv_timer_create(toast_hide_cb, TOAST_MS, NULL);
    lv_timer_set_repeat_count(g.toast_timer, 1);
}

/* ---------- the update notice ---------- */

static void update_hidden_cb(lv_obj_t *o) {
    if (!g.update_hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void update_hide_cb(lv_timer_t *t) {
    (void)t;
    g.update_hide = NULL;
    motion_to(g.update, MO_TY, -UPDATE_RISE, 320, 0, lv_anim_path_ease_in);
    motion_then(g.update, MO_OPA, LV_OPA_TRANSP, 280, lv_anim_path_ease_in, update_hidden_cb);
}

/* a newer release: the notice waits until Home is in front and idle (the splash is gone, no dialog,
 * no session), so it is seen */
static void update_wait_cb(lv_timer_t *t) {
    if (lv_obj_has_flag(g.root, LV_OBJ_FLAG_HIDDEN) || app_conn_state() != CONN_IDLE || splash_active() ||
        ui_dialog_active())
        return;
    lv_timer_del(t);
    g.update_wait = NULL;
    if (!app_update_version()[0]) return;
    lv_label_set_text_fmt(g.update_title, "Hotaru %s is available", app_update_version());
    lv_coord_t x = UPDATE_SIDE + UPDATE_LOGO + UPDATE_GAP;
    lv_obj_set_width(g.update, x + LV_MAX(text_width(g.update_title), text_width(g.update_addr)) + UPDATE_SIDE +
                                   2 * UPDATE_BORDER);
    lv_obj_clear_flag(g.update, LV_OBJ_FLAG_HIDDEN);
    motion_from(g.update, MO_TY, -UPDATE_RISE, 0, 360, 0, NULL);
    motion_to(g.update, MO_OPA, LV_OPA_COVER, 240, 0, NULL);
    if (g.update_hide) lv_timer_del(g.update_hide);
    g.update_hide = lv_timer_create(update_hide_cb, UPDATE_MS, NULL);
    lv_timer_set_repeat_count(g.update_hide, 1);
}

static void update_changed(void) {
    home_settings_refresh(); /* About names the new version too */
    if (!app_update_version()[0] || g.update_wait) return;
    g.update_wait = lv_timer_create(update_wait_cb, UPDATE_WAIT, NULL);
    lv_timer_ready(g.update_wait);
}

static void build_update(void) {
    g.update = ctl_rect(g.root, 400, UPDATE_H, 26, COL_SHEET, LV_OPA_COVER); /* opaque: it hides what it covers */
    lv_obj_set_style_border_width(g.update, UPDATE_BORDER, 0);
    lv_obj_set_style_border_color(g.update, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.update, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(g.update, 50, 0);
    lv_obj_set_style_shadow_opa(g.update, LV_OPA_40, 0);
    lv_obj_align(g.update, LV_ALIGN_TOP_RIGHT, -HOME_SAFE_X + SP_L, UPDATE_Y);
    lv_obj_set_pos(ui_logo(g.update, UPDATE_LOGO), UPDATE_SIDE, UPDATE_PAD);
    lv_coord_t x = UPDATE_SIDE + UPDATE_LOGO + UPDATE_GAP;
    g.update_title = ctl_label(g.update, FONT_BODY, COL_TEXT, "");
    lv_obj_set_pos(g.update_title, x, UPDATE_PAD - 5);
    g.update_addr = ctl_label(g.update, FONT_CAPTION, COL_MUTED, HOME_SOURCE_URL);
    lv_obj_set_pos(g.update_addr, x, UPDATE_PAD + 27);
    lv_obj_set_style_opa(g.update, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(g.update, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- tiles ---------- */

static uint16_t tile_zoom(const tile_t *t, lv_coord_t s) { return t->zooms ? (uint16_t)(256 * s / TILE_F) : 256; }

static tile_t *new_tile(int sec, uint32_t top, uint32_t bottom) {
    tile_t *t = &g.tiles[sec][g.n[sec]++];
    memset(t, 0, sizeof(*t));
    t->tile = ctl_rect(g.row[sec], TILE_S, TILE_S, 30, top, LV_OPA_COVER);
    ctl_grad(t->tile, top, bottom);
    lv_obj_set_style_border_width(t->tile, 2, 0);
    lv_obj_set_style_border_color(t->tile, lv_color_white(), 0);
    lv_obj_set_style_border_opa(t->tile, LV_OPA_10, 0);
    ctl_outline(t->tile, 5, 7);
    return t;
}

/* the art is made for the focused size and zooms down with the tile */
static void tile_art(tile_t *t, lv_obj_t *art) {
    t->art = art;
    t->zooms = true;
    lv_obj_center(art);
    lv_img_set_zoom(art, tile_zoom(t, TILE_S));
}

/* An icon is drawn once at the focused tile's size and kept as an image, so it zooms with its
 * tile like the art does: drawn objects would keep their size while the tile grows. */
static const lv_img_dsc_t *icon_image(lv_obj_t *drawn) {
    lv_obj_update_layout(drawn);
    const lv_img_dsc_t *img = lv_snapshot_take(drawn, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_del(drawn);
    return img;
}

/* a symbol on a circle, flat or a vertical gradient (top != bottom) */
static const lv_img_dsc_t *icon_circle(uint32_t top, uint32_t bottom, lv_opa_t opa, const char *sym) {
    lv_obj_t *c = ctl_rect(lv_layer_sys(), ICON_D, ICON_D, LV_RADIUS_CIRCLE, top, opa);
    if (top != bottom) ctl_grad(c, top, bottom);
    lv_obj_center(ctl_label(c, FONT_ICON, COL_TEXT, sym));
    return icon_image(c);
}

static void tile_icon(tile_t *t, const lv_img_dsc_t *img) {
    lv_obj_t *o = lv_img_create(t->tile);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    if (img) {
        lv_img_set_src(o, img);
        lv_img_set_pivot(o, (lv_coord_t)(img->header.w / 2), (lv_coord_t)(img->header.h / 2));
        lv_img_set_antialias(o, true);
    }
    tile_art(t, o);
}

static void build_console_tiles(void) {
    lv_obj_clean(g.row[HOME_CONSOLES]);
    g.n[HOME_CONSOLES] = 0;
    for (int i = 0; i < app_host_count() && i < MAX_TILES - 1; i++) {
        const app_host_t *h = app_host(i);
        tile_t *t = new_tile(HOME_CONSOLES, h->ps5 ? TILE_PS5 : TILE_PS4, TILE_LOW);
        tile_art(t, art_box(t->tile, art_console(h->ps5), TILE_F * 88 / 100, TILE_F * 80 / 100));
        if (h->psn && !h->lan) { /* reached through PSN only: a small badge */
            lv_obj_t *b = ctl_rect(t->tile, LV_SIZE_CONTENT, 30, LV_RADIUS_CIRCLE, COL_SHADE, LV_OPA_50);
            lv_obj_set_style_pad_hor(b, 10, 0);
            lv_obj_center(ctl_label(b, FONT_SMALL, COL_TEXT, "PSN"));
            lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -12, 12);
        }
    }
    tile_t *add = new_tile(HOME_CONSOLES, TILE_ADD, TILE_ADD_LOW);
    lv_obj_set_style_bg_opa(add->tile, LV_OPA_40, 0);
    lv_obj_set_style_border_width(add->tile, 3, 0);
    lv_obj_set_style_border_opa(add->tile, LV_OPA_30, 0);
    static const lv_img_dsc_t *plus; /* the row is rebuilt with every console change: drawn once */
    if (!plus) plus = icon_image(ctl_label(lv_layer_sys(), FONT_ICON, COL_TEXT, LV_SYMBOL_PLUS));
    tile_icon(add, plus);
}

static void build_settings_tiles(void) {
    static const char *const sym[SET__N] = { LV_SYMBOL_VIDEO, "", "", LV_SYMBOL_LIST, "" };
    for (int i = 0; i < SET__N; i++) {
        uint32_t tint = home_settings_tint(i);
        tile_t *t = new_tile(HOME_SETTINGS, tint + SET_LIFT, TILE_SET_LOW);
        if (i == SET_PAD) {
            tile_art(t, art_create(t->tile, art_pad(input_pad_name()), TILE_F * 56 / 100));
        } else if (i == SET_ACCOUNT) {
            tile_icon(t, icon_circle(COL_PSN, COL_PSN_DEEP, LV_OPA_COVER, LV_SYMBOL_WIFI));
        } else if (i == SET_ABOUT) {
            tile_icon(t, icon_image(ui_logo(lv_layer_sys(), LOGO_D)));
        } else {
            tile_icon(t, icon_circle(COL_FROST, COL_FROST, LV_OPA_20, sym[i]));
        }
    }
}

static void layout_row(int sec, uint32_t ms) {
    int f = g.focus[sec];
    for (int i = 0; i < g.n[sec]; i++) {
        tile_t *t = &g.tiles[sec][i];
        lv_coord_t s = i == f ? TILE_F : TILE_S, x;
        if (i < f) x = HOME_SAFE_X - (f - i) * (TILE_S + TILE_GAP);
        else if (i == f) x = HOME_SAFE_X;
        else x = HOME_SAFE_X + TILE_F + TILE_GAP + NAME_W + (i - f - 1) * (TILE_S + TILE_GAP);
        motion_to(t->tile, MO_X, x, ms, 0, NULL);
        motion_to(t->tile, MO_W, s, ms, 0, NULL);
        motion_to(t->tile, MO_H, s, ms, 0, NULL);
        lv_opa_t opa = i == f - 1 ? PEEK_OPA : i < f ? LV_OPA_TRANSP : LV_OPA_COVER;
        motion_to(t->tile, MO_OPA, opa, ms * 2 / 3, 0, NULL);
        if (t->zooms) motion_to(t->art, MO_ZOOM, tile_zoom(t, s), ms, 0, NULL);
        bool ring = i == f && g.zone == HOME_ROW && g.sec == sec;
        motion_to(t->tile, MO_OUTLINE_OPA, ring ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
    }
}

/* ---------- hero ---------- */

static void route_text(void) {
    const app_host_t *h = focused_host();
    lv_label_set_text(g.route_lbl,
                      h && h->route == ROUTE_PSN ? "Over PSN   " LV_SYMBOL_DOWN : "Local network   " LV_SYMBOL_DOWN);
    fit_width(g.btn[HOME_ACT_ROUTE], g.route_lbl, SP_2XL);
}

/* the dot and the line under the name: what discovery knows, else how it is paired */
static void hero_state(const app_host_t *h) {
    char buf[160];
    uint32_t col = COL_INFO;
    switch (h->state) {
        case HOST_READY:
            col = COL_OK;
            if (h->running[0]) snprintf(buf, sizeof(buf), "Ready, playing %s", h->running);
            else snprintf(buf, sizeof(buf), "Ready on this network");
            break;
        case HOST_STANDBY:
            col = COL_WARN;
            snprintf(buf, sizeof(buf), "In rest mode: Start Remote Play wakes it up");
            break;
        case HOST_OFFLINE:
            col = COL_MUTED;
            snprintf(buf, sizeof(buf), "%s",
                     h->psn ? "Not on this network: plays over PSN" : "Not found on this network");
            break;
        case HOST_UNKNOWN:
            snprintf(buf, sizeof(buf), "%s",
                     h->lan && h->psn ? "Paired with a PIN, also on your PSN account"
                     : h->lan         ? "Paired with a PIN on this network"
                                      : "On your PSN account: plays over the internet");
            break;
    }
    lv_label_set_text(g.h_meta, buf);
    lv_obj_set_style_bg_color(g.h_dot, C(col), 0);
    lv_obj_set_style_shadow_color(g.h_dot, C(col), 0);
}

static void hero_set(bool anim) {
    const app_host_t *h = on_add_tile() ? NULL : focused_host();
    if (h) {
        if (h->lan && h->psn)
            lv_label_set_text_fmt(g.h_over, "PLAYSTATION %s   " LV_SYMBOL_BULLET "   %s   " LV_SYMBOL_BULLET "   PSN",
                                  h->ps5 ? "5" : "4", h->addr);
        else
            lv_label_set_text_fmt(g.h_over, "PLAYSTATION %s   " LV_SYMBOL_BULLET "   %s", h->ps5 ? "5" : "4",
                                  h->lan ? h->addr : "VIA PSN");
        lv_label_set_text(g.h_title, h->name);
        hero_state(h);
        lv_obj_clear_flag(g.h_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(g.h_meta, 30);
        lv_obj_clear_flag(g.actions, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g.cards, LV_OBJ_FLAG_HIDDEN);
        if (h->lan && h->psn) lv_obj_clear_flag(g.btn[HOME_ACT_ROUTE], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g.btn[HOME_ACT_ROUTE], LV_OBJ_FLAG_HIDDEN);
        route_text();
    } else { /* the tile itself opens Add a console; the stream settings belong to a console */
        lv_label_set_text(g.h_over, "NEW CONSOLE");
        lv_label_set_text(g.h_title, "Add a console");
        lv_label_set_text(g.h_meta, "Pair a PS5 or PS4 with its PIN, or sign in to PSN");
        lv_obj_add_flag(g.h_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(g.h_meta, 0);
        lv_obj_add_flag(g.actions, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g.cards, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t ms = anim ? 420 : 0, tint = host_tint(h);
    motion_color(g.bg, LV_STYLE_BG_COLOR, tint, ms);
    motion_color(g.glow, LV_STYLE_IMG_RECOLOR, tint + GLOW_LIFT, ms);
    if (anim) {
        motion_from(g.h_text, MO_TX, 36, 0, 360, 0, NULL);
        motion_from(g.h_text, MO_OPA, 0, 255, 300, 0, NULL);
    }
    /* key art: the console slides in from the right, the others fade away */
    int shown = !h ? KEY_ART_UNKNOWN : h->ps5 ? KEY_ART_PS5 : KEY_ART_PS4;
    for (int i = 0; i < KEY_ART__N; i++) {
        if (i != shown) {
            motion_to(g.art[i], MO_IMG_OPA, LV_OPA_TRANSP, anim ? 160 : 0, 0, NULL);
            continue;
        }
        if (anim) {
            motion_from(g.art[i], MO_TX, 70, 0, 520, 0, NULL);
            motion_from(g.art[i], MO_IMG_OPA, 0, 255, 420, 0, NULL);
        } else {
            motion_set(g.art[i], MO_IMG_OPA, LV_OPA_COVER);
        }
    }
}

static lv_obj_t *quick_card(int i, const char *title) {
    lv_obj_t *c = ctl_rect(g.cards, QCARD_W, QCARD_H, 26, COL_FROST, LV_OPA_10);
    lv_obj_set_pos(c, i * (QCARD_W + 28), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, LV_OPA_10, 0);
    ctl_outline(c, 5, 6);
    lv_obj_t *ic = ctl_rect(c, 72, 72, 20, COL_ACCENT, LV_OPA_COVER);
    ctl_grad(ic, COL_ACCENT_HI, COL_ACCENT_DEEP);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 24, 0);
    if (i == CARD_PAD) g.card_pad_box = ic;
    else lv_obj_center(ctl_label(ic, FONT_TITLE, COL_TEXT, i == CARD_QUALITY ? LV_SYMBOL_VIDEO : LV_SYMBOL_LOOP));
    lv_obj_t *t = ctl_label(c, FONT_CAPTION, COL_MUTED, title);
    lv_obj_set_pos(t, 120, 30);
    g.card_val[i] = ctl_label(c, FONT_HEADING, COL_TEXT, "");
    lv_obj_set_pos(g.card_val[i], 120, 60);
    lv_obj_set_width(g.card_val[i], QCARD_W - 136);
    lv_label_set_long_mode(g.card_val[i], LV_LABEL_LONG_DOT);
    if (i == CARD_PAD) { /* the controller's battery, when it reports one */
        g.card_pad_batt = ctl_label(c, FONT_CAPTION, COL_MUTED, "");
        lv_obj_set_pos(g.card_pad_batt, 120, QCARD_SUB_Y);
        lv_obj_add_flag(g.card_pad_batt, LV_OBJ_FLAG_HIDDEN);
    }
    return c;
}

static void cards_refresh(void) {
    const stream_settings_t *s = &config_get()->stream;
    lv_label_set_text_fmt(g.card_val[CARD_QUALITY], "%dp" BULLET "%d fps", s->height, s->fps);
    if (s->smooth_ms) lv_label_set_text_fmt(g.card_val[CARD_SMOOTH], "%d ms", s->smooth_ms);
    else lv_label_set_text(g.card_val[CARD_SMOOTH], "Off");
    pad_state_t st = input_pad_state();
    art_id_t id = art_pad(input_pad_name());
    lv_label_set_text(g.card_val[CARD_PAD],
                      st == PAD_ONLINE ? (id == ART_DUALSENSE ? "DualSense" : "DualShock 4") : "No controller");
    int pct = 0;
    if (st == PAD_ONLINE && input_pad_battery(&pct)) {
        lv_label_set_text_fmt(g.card_pad_batt, "Battery %d%%", pct);
        uint32_t col = pct <= BATT_CRIT ? COL_ERR : pct <= BATT_LOW ? COL_WARN : COL_MUTED;
        lv_obj_set_style_text_color(g.card_pad_batt, C(col), 0);
        lv_obj_clear_flag(g.card_pad_batt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g.card_pad_batt, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_hero(void) {
    g.glow = ctl_glow(g.root, COL_TINT_NEUTRAL, LV_OPA_60);
    lv_obj_set_pos(g.glow, ART_CX - CTL_GLOW_D / 2, ART_CY - CTL_GLOW_D / 2);
    /* each stands on the floor line, its contact shadow below it */
    static const struct {
        art_id_t id;
        float floor;
    } key[KEY_ART__N] = {
        [KEY_ART_PS4] = { ART_PS4, ART_PS4_FLOOR },
        [KEY_ART_PS5] = { ART_PS5, ART_PS5_FLOOR },
        [KEY_ART_UNKNOWN] = { ART_UNKNOWN, ART_UNKNOWN_FLOOR },
    };
    for (int i = 0; i < KEY_ART__N; i++) {
        g.art[i] = art_box(g.root, key[i].id, ART_W, ART_H);
        const lv_img_dsc_t *d = lv_img_get_src(g.art[i]);
        if (d) {
            lv_coord_t below = (lv_coord_t)((float)d->header.h * key[i].floor);
            lv_obj_set_pos(g.art[i], ART_CX - (lv_coord_t)d->header.w / 2, FLOOR_Y - (lv_coord_t)d->header.h + below);
        }
        lv_obj_set_style_img_opa(g.art[i], LV_OPA_TRANSP, 0);
    }

    g.hero = ctl_obj(g.root);
    lv_obj_set_size(g.hero, 1300, 540);
    lv_obj_set_pos(g.hero, HOME_SAFE_X, HERO_Y);
    ctl_margin(g.hero, 24); /* a focused button or card reaches past its left edge */
    g.h_text = ctl_obj(g.hero);
    lv_obj_set_size(g.h_text, 1180, 150);
    g.h_over = ctl_label(g.h_text, FONT_SMALL, COL_ACCENT_HI, "");
    lv_obj_set_style_text_letter_space(g.h_over, 3, 0);
    g.h_title = ctl_label(g.h_text, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.h_title, -2, 26);
    lv_obj_set_width(g.h_title, 1180);
    lv_label_set_long_mode(g.h_title, LV_LABEL_LONG_DOT);
    g.h_dot = ctl_dot(g.h_text, 14, COL_INFO);
    lv_obj_set_pos(g.h_dot, 2, 108);
    g.h_meta = ctl_label(g.h_text, FONT_BODY, COL_SUBTLE, "");
    lv_obj_set_pos(g.h_meta, 30, 100);

    g.actions = ctl_row(g.hero, SP_XL);
    lv_obj_set_pos(g.actions, 0, 176);
    g.btn[HOME_ACT_PLAY] = button(g.actions, 480, 100);
    lv_obj_t *pr = ctl_row(g.btn[HOME_ACT_PLAY], SP_L);
    lv_obj_center(pr);
    lv_obj_t *play_icon = lv_label_create(pr);
    lv_label_set_text(play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(play_icon, FONT_TITLE, 0);
    lv_obj_t *play_lbl = lv_label_create(pr);
    lv_label_set_text(play_lbl, "Start Remote Play");
    lv_obj_set_style_text_font(play_lbl, FONT_HEADING, 0);
    g.btn[HOME_ACT_ROUTE] = button(g.actions, 300, 100);
    g.route_lbl = lv_label_create(g.btn[HOME_ACT_ROUTE]);
    lv_obj_set_style_text_font(g.route_lbl, FONT_BODY, 0);
    lv_obj_center(g.route_lbl);
    g.btn[HOME_ACT_OPTIONS] = button(g.actions, 100, 100);
    lv_obj_t *dots = lv_label_create(g.btn[HOME_ACT_OPTIONS]);
    lv_label_set_text(dots, LV_SYMBOL_BULLET LV_SYMBOL_BULLET LV_SYMBOL_BULLET);
    lv_obj_set_style_text_font(dots, FONT_TITLE, 0);
    lv_obj_set_style_text_letter_space(dots, 4, 0);
    lv_obj_center(dots);

    g.cards = ctl_obj(g.hero);
    lv_obj_set_size(g.cards, 1300, QCARD_H);
    lv_obj_set_pos(g.cards, 0, 334);
    g.card[CARD_QUALITY] = quick_card(CARD_QUALITY, "Stream quality");
    g.card[CARD_SMOOTH] = quick_card(CARD_SMOOTH, "Smooth playback");
    g.card[CARD_PAD] = quick_card(CARD_PAD, "Controller");
}

/* ---------- top bar, connecting screen, toast ---------- */

static void clock_cb(lv_timer_t *t) {
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    lv_label_set_text_fmt(g.clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void build_topbar(void) {
    lv_obj_t *tabs = ctl_row(g.root, SP_S);
    lv_obj_set_pos(tabs, HOME_SAFE_X - 26, HOME_SAFE_Y - 8);
    static const char *const names[SEC__N] = { "Consoles", "Settings" };
    for (int s = 0; s < SEC__N; s++) {
        g.tabs[s] = ctl_obj(tabs);
        lv_obj_set_size(g.tabs[s], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(g.tabs[s], 26, 0);
        lv_obj_set_style_pad_ver(g.tabs[s], 10, 0);
        lv_obj_set_style_radius(g.tabs[s], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g.tabs[s], lv_color_white(), 0);
        lv_obj_set_style_text_color(g.tabs[s], C(s ? COL_DIM : COL_TEXT), 0);
        lv_obj_t *l = lv_label_create(g.tabs[s]);
        lv_label_set_text(l, names[s]);
        lv_obj_set_style_text_font(l, FONT_TITLE, 0);
    }
    lv_obj_t *right = ctl_row(g.root, SP_XL);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -HOME_SAFE_X, HOME_SAFE_Y - 4);
    g.pad_box = ctl_obj(right);
    lv_obj_set_size(g.pad_box, 82, 50);
    g.pad_dot = ctl_dot(g.pad_box, 12, COL_DIM);
    lv_obj_align(g.pad_dot, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    g.avatar = ctl_rect(right, 52, 52, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    ctl_grad(g.avatar, COL_PSN, COL_PSN_DEEP);
    g.avatar_lbl = ctl_label(g.avatar, FONT_CAPTION, COL_TEXT, "");
    lv_obj_center(g.avatar_lbl);
    g.clock = ctl_label(right, FONT_TITLE, COL_TEXT, "");
    clock_cb(NULL);
    g.clock_timer = lv_timer_create(clock_cb, 10000, NULL);

    g.hints = ctl_hints(g.root);
    lv_obj_align(g.hints, LV_ALIGN_BOTTOM_RIGHT, -HOME_SAFE_X, -HOME_SAFE_Y + 14);
    ctl_hint(g.hints, PS_CROSS, "Select");
    ctl_hint(g.hints, PS_CIRCLE, "Back");
}

static void topbar_refresh(void) {
    const config_psn_t *psn = config_psn();
    if (psn->linked && psn->online_id[0]) {
        char ini[3] = { psn->online_id[0], psn->online_id[1], 0 };
        for (int i = 0; i < 2; i++)
            if (ini[i] >= 'a' && ini[i] <= 'z') ini[i] = (char)(ini[i] - 'a' + 'A');
        lv_label_set_text(g.avatar_lbl, ini);
        lv_obj_clear_flag(g.avatar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g.avatar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_connecting(void) {
    g.conn = ctl_rect(g.root, LV_PCT(100), LV_PCT(100), 0, COL_TINT_NEUTRAL, LV_OPA_COVER);
    ctl_grad(g.conn, COL_TINT_NEUTRAL, COL_BG_BOTTOM);
    lv_obj_set_style_bg_main_stop(g.conn, 0, 0);
    lv_obj_set_style_bg_grad_stop(g.conn, 200, 0);
    lv_obj_add_flag(g.conn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *glow = ctl_glow(g.conn, CONN_GLOW, LV_OPA_50);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, -110);
    g.conn_art_box = ctl_obj(g.conn);
    lv_obj_set_size(g.conn_art_box, 440, 380);
    lv_obj_align(g.conn_art_box, LV_ALIGN_CENTER, 0, -110);
    g.conn_title = ctl_label(g.conn, FONT_TITLE, COL_TEXT, "");
    lv_obj_align(g.conn_title, LV_ALIGN_CENTER, 0, 140);
    g.conn_bar = ctl_rect(g.conn, 420, 10, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_10);
    lv_obj_align(g.conn_bar, LV_ALIGN_CENTER, 0, 210);
    lv_obj_t *fill = ctl_rect(g.conn_bar, 0, 10, LV_RADIUS_CIRCLE, COL_ACCENT_HI, LV_OPA_COVER);
    (void)fill;
    g.conn_pct = ctl_label(g.conn, FONT_CAPTION, COL_MUTED, "");
    lv_obj_align(g.conn_pct, LV_ALIGN_CENTER, 0, 250);
    g.conn_hints = ctl_hints(g.conn);
    lv_obj_align(g.conn_hints, LV_ALIGN_BOTTOM_MID, 0, -HOME_SAFE_Y);
}

/* the last screen before the game says how to reach this app's menu from it */
static void connecting_hints(void) {
    lv_obj_clean(g.conn_hints);
    ctl_hint(g.conn_hints, PS_CIRCLE, "Cancel");
    if (input_pad_state() == PAD_ONLINE)
        ctl_hint_keys(g.conn_hints, art_pad(input_pad_name()) == ART_DUALSENSE ? "Create" : "Share", "Options",
                      "The menu during the game");
    else ctl_hint_key(g.conn_hints, "Back", "The menu during the game");
}

static void breathe_cb(void *img, int32_t v) { lv_img_set_zoom(img, (uint16_t)v); }

static void conn_hidden_cb(lv_obj_t *o) {
    if (app_conn_state() != CONN_CONNECTING) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

/* the title and the step follow the attempt: a local-network attempt that found nothing goes on
 * over PSN under the same screen */
static void connecting_text(void) {
    const app_host_t *h = app_host(app_selected());
    int p = app_conn_progress();
    char buf[160];
    if (app_conn_waking()) snprintf(buf, sizeof(buf), "Waking up %s", h ? h->name : "the console");
    else
        snprintf(buf, sizeof(buf), "Connecting to %s%s", h ? h->name : "the console",
                 app_conn_over_psn() ? " over PSN" : "");
    set_text(g.conn_title, buf);
    snprintf(buf, sizeof(buf), "%s" BULLET "%d%%",
             app_conn_waking()               ? "It was in rest mode"
             : app_conn_fallback() && p < 60 ? "Not on this network, trying over PSN"
             : p < 60                        ? "Reaching the console"
                                             : "Starting Remote Play",
             p);
    set_text(g.conn_pct, buf);
}

static void connecting_refresh(void) {
    conn_state_t s = app_conn_state();
    int p = app_conn_progress();
    lv_obj_t *fill = lv_obj_get_child(g.conn_bar, 0);
    motion_to(fill, MO_W, 420 * p / 100, 300, 0, NULL);
    if (s == CONN_CONNECTING) connecting_text();
    if (s == CONN_CONNECTING && g.conn_shown != CONN_CONNECTING) {
        connecting_hints();
        const app_host_t *h = app_host(app_selected());
        uint32_t tint = host_tint(h);
        lv_obj_set_style_bg_color(g.conn, C(tint), 0);
        lv_obj_clean(g.conn_art_box);
        if (h) {
            lv_obj_t *art = art_box(g.conn_art_box, art_console(h->ps5), 420, 360);
            lv_obj_center(art);
            lv_anim_t a; /* the console breathes while it connects */
            lv_anim_init(&a);
            lv_anim_set_var(&a, art);
            lv_anim_set_exec_cb(&a, breathe_cb);
            lv_anim_set_values(&a, 256, 266);
            lv_anim_set_time(&a, 900);
            lv_anim_set_playback_time(&a, 900);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_start(&a);
        }
        if (ctl_menu_is_open(&g.menu)) ctl_menu_close(&g.menu);
        lv_obj_clear_flag(g.conn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g.conn);
        motion_from(g.conn, MO_OPA, 0, 255, 260, 0, NULL);
    } else if (s != CONN_CONNECTING && g.conn_shown == CONN_CONNECTING) {
        if (s == CONN_STREAMING) lv_obj_add_flag(g.conn, LV_OBJ_FLAG_HIDDEN);
        else motion_then(g.conn, MO_OPA, LV_OPA_TRANSP, 300, NULL, conn_hidden_cb);
    }
    if (s == CONN_IDLE && g.conn_shown != CONN_IDLE && app_conn_error()[0]) { /* failed, or dropped */
        char msg[192];
        snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING "   %s: %s",
                 g.conn_shown == CONN_CONNECTING ? "Could not connect" : "The stream ended", app_conn_error());
        toast(msg, COL_DANGER_TEXT);
    }
    g.conn_shown = s;
}

/* ---------- focus painting ---------- */

static void paint(void) {
    for (int s = 0; s < SEC__N; s++) {
        bool act = s == g.sec, foc = act && g.zone == HOME_TOP;
        motion_to(g.tabs[s], MO_BG_OPA, foc ? LV_OPA_COVER : LV_OPA_TRANSP, 180, 0, NULL);
        motion_color(g.tabs[s], LV_STYLE_TEXT_COLOR, foc ? COL_ON_FROST : act ? COL_TEXT : COL_DIM, 180);
    }
    layout_row(g.sec, MOVE_MS);
    bool cons = g.sec == HOME_CONSOLES;
    for (int a = 0; a < ACT__N; a++) button_focus(g.btn[a], cons && g.zone == HOME_ACTIONS && g.item == a);
    for (int i = 0; i < CARD__N; i++) {
        bool on = cons && g.zone == HOME_CARDS && g.item == i;
        motion_to(g.card[i], MO_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
        motion_to(g.card[i], MO_GROW, on ? 6 : 0, 300, 0, motion_spring);
        motion_to(g.card[i], MO_BG_OPA, on ? LV_OPA_20 : LV_OPA_10, 160, 0, NULL);
    }
    home_settings_focus(g.focus[HOME_SETTINGS], !cons && g.zone == HOME_PANEL ? g.item : -1);
}

/* the name beside the focused tile follows the tile in, once it has mostly landed */
static void ttl_set(bool anim, int dir) {
    int f = g.focus[g.sec];
    if (g.sec == HOME_CONSOLES) {
        const app_host_t *h = app_host(f);
        lv_label_set_text(g.ttl_name, h ? h->name : "Add a console");
        lv_label_set_text(g.ttl_sub, !h                 ? "PIN or PSN account"
                                     : h->lan && h->psn ? "Local network and PSN"
                                     : h->lan           ? "Local network"
                                                        : "PSN");
    } else {
        lv_label_set_text(g.ttl_name, home_settings_name(f));
        lv_label_set_text(g.ttl_sub, home_settings_caption(f));
    }
    if (anim) {
        motion_from(g.ttl, MO_TX, dir * 48, 0, 320, 150, NULL);
        motion_from(g.ttl, MO_OPA, 0, 255, 260, 150, NULL);
    }
}

static void section_set(int sec, bool anim) {
    if (ctl_menu_is_open(&g.menu)) ctl_menu_close(&g.menu);
    g.sec = sec;
    for (int s = 0; s < SEC__N; s++) {
        if (s == sec) lv_obj_clear_flag(g.row[s], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g.row[s], LV_OBJ_FLAG_HIDDEN);
    }
    bool cons = sec == HOME_CONSOLES;
    lv_obj_t *cons_objs[] = { g.hero, g.glow, g.art[KEY_ART_PS4], g.art[KEY_ART_PS5], g.art[KEY_ART_UNKNOWN] };
    for (size_t i = 0; i < sizeof(cons_objs) / sizeof(cons_objs[0]); i++) {
        if (cons) lv_obj_clear_flag(cons_objs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cons_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (anim) { /* the new row deals in from the right, one tile after the other */
        for (int i = 0; i < g.n[sec]; i++)
            motion_from(g.tiles[sec][i].tile, MO_TX, 260 + i * 40, 0, 460, (uint32_t)i * 40, NULL);
        motion_from(g.row[sec], MO_OPA, 0, 255, 300, 0, NULL);
    }
    ttl_set(anim, 1);
    if (cons) {
        hero_set(anim);
        home_settings_show(-1, false);
    } else {
        home_settings_show(g.focus[HOME_SETTINGS], anim);
        motion_color(g.bg, LV_STYLE_BG_COLOR, home_settings_tint(g.focus[HOME_SETTINGS]), anim ? 420 : 0);
    }
}

/* ---------- menus ---------- */

static void open_menu(int kind) {
    const app_host_t *h = focused_host();
    if (!h) return;
    ctl_menu_item_t items[2];
    int n = 0, sel = 0;
    memset(items, 0, sizeof(items));
    if (kind == MENU_ROUTE) {
        static char lan_detail[96];
        snprintf(lan_detail, sizeof(lan_detail), "PIN pairing" BULLET "%s", h->addr);
        items[n++] = (ctl_menu_item_t){ LV_SYMBOL_HOME, "Local network", lan_detail, h->route == ROUTE_LAN, false };
        items[n++] = (ctl_menu_item_t){ LV_SYMBOL_WIFI, "Over PSN", "Through your PSN account, from anywhere",
                                        h->route == ROUTE_PSN, false };
        sel = h->route == ROUTE_PSN;
    } else {
        if (h->lan)
            items[n++] = (ctl_menu_item_t){ LV_SYMBOL_POWER, "Wake up", "Turn it on from rest mode", false, false };
        items[n++] = (ctl_menu_item_t){ LV_SYMBOL_TRASH, "Forget this console", "Removes its pairing from this TV",
                                        false, true };
    }
    lv_obj_t *anchor = g.btn[kind == MENU_ROUTE ? HOME_ACT_ROUTE : HOME_ACT_OPTIONS];
    lv_obj_update_layout(anchor);
    lv_area_t a;
    lv_obj_get_coords(anchor, &a);
    ctl_menu_open(&g.menu, g.root, &a, false, items, n, sel);
    g.menu_kind = kind;
}

static void forget_confirmed(void *user) {
    (void)user;
    app_forget();
}

static void menu_choose(void) {
    const app_host_t *h = focused_host();
    int sel = g.menu.sel, kind = g.menu_kind;
    ctl_menu_close(&g.menu);
    g.menu_kind = MENU_NONE;
    if (!h) return;
    if (kind == MENU_ROUTE) {
        app_set_route(sel == 1 ? ROUTE_PSN : ROUTE_LAN);
        route_text();
        return;
    }
    if (h->lan && sel == 0) {
        app_wake();
        toast(LV_SYMBOL_POWER "   Wake-up sent", COL_TEXT);
        return;
    }
    char ttl[96];
    snprintf(ttl, sizeof(ttl), "Forget %s?", h->name);
    const char *msg = h->lan && h->psn
                          ? "Its PIN pairing and its PSN entry leave this TV. Add it again to play it here."
                      : h->lan ? "Its pairing keys leave this TV. Pair it again with its PIN to play it here."
                               : "It leaves Home. Find my consoles, in PSN account, brings it back.";
    ui_confirm(ttl, msg, "Forget", art_console(h->ps5), forget_confirmed, NULL);
}

/* ---------- navigation ---------- */

static void select_console(int i) {
    g.focus[HOME_CONSOLES] = i;
    if (i < app_host_count()) app_select(i);
}

static void play(void) {
    if (on_add_tile()) {
        ui_register_open();
        return;
    }
    app_connect();
    if (app_conn_state() == CONN_IDLE) shake(g.btn[HOME_ACT_PLAY]);
}

static int next_action(int from, int dir) {
    for (int a = from + dir; a >= 0 && a < ACT__N; a += dir)
        if (action_visible(a)) return a;
    return from;
}

static void card_jump(int card) {
    static const int panel[CARD__N] = { SET_STREAM, SET_STREAM, SET_PAD }, row[CARD__N] = { 0, 4, 0 };
    g.focus[HOME_SETTINGS] = panel[card];
    section_set(HOME_SETTINGS, true);
    g.zone = HOME_PANEL;
    g.item = row[card];
}

static void key_consoles(uint32_t k) {
    int *f = &g.focus[HOME_CONSOLES];
    switch (g.zone) {
        case HOME_ROW:
            if ((k == LV_KEY_LEFT && *f > 0) || (k == LV_KEY_RIGHT && *f < g.n[HOME_CONSOLES] - 1)) {
                int dir = k == LV_KEY_RIGHT ? 1 : -1;
                select_console(*f + dir);
                ttl_set(true, dir);
                hero_set(true);
            } else if (k == LV_KEY_UP) {
                g.zone = HOME_TOP;
            } else if (k == LV_KEY_DOWN && !on_add_tile()) {
                g.zone = HOME_ACTIONS;
                g.item = HOME_ACT_PLAY;
            } else if (k == LV_KEY_ENTER) {
                play();
            }
            break;
        case HOME_ACTIONS:
            if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) g.item = next_action(g.item, k == LV_KEY_RIGHT ? 1 : -1);
            else if (k == LV_KEY_UP) g.zone = HOME_ROW;
            else if (k == LV_KEY_DOWN) {
                g.zone = HOME_CARDS;
                g.item = 0;
            } else if (k == LV_KEY_ENTER) {
                if (g.item == HOME_ACT_PLAY) play();
                else open_menu(g.item == HOME_ACT_ROUTE ? MENU_ROUTE : MENU_OPTIONS);
            }
            break;
        case HOME_CARDS:
            if (k == LV_KEY_LEFT && g.item > 0) g.item--;
            else if (k == LV_KEY_RIGHT && g.item < CARD__N - 1) g.item++;
            else if (k == LV_KEY_UP) {
                g.zone = HOME_ACTIONS;
                g.item = HOME_ACT_PLAY;
            } else if (k == LV_KEY_ENTER) card_jump(g.item);
            break;
        default: break;
    }
}

static void key_settings(uint32_t k) {
    int *f = &g.focus[HOME_SETTINGS];
    switch (g.zone) {
        case HOME_ROW:
            if ((k == LV_KEY_LEFT && *f > 0) || (k == LV_KEY_RIGHT && *f < g.n[HOME_SETTINGS] - 1)) {
                int dir = k == LV_KEY_RIGHT ? 1 : -1;
                *f += dir;
                ttl_set(true, dir);
                home_settings_show(*f, true);
                motion_color(g.bg, LV_STYLE_BG_COLOR, home_settings_tint(*f), 420);
            } else if (k == LV_KEY_UP) {
                g.zone = HOME_TOP;
            } else if ((k == LV_KEY_DOWN || k == LV_KEY_ENTER) && home_settings_enterable(*f)) {
                g.zone = HOME_PANEL;
                g.item = home_settings_first_row(*f);
            }
            break;
        case HOME_PANEL:
            if (!home_settings_key(*f, &g.item, k)) g.zone = HOME_ROW;
            break;
        default: break;
    }
}

void home_key(uint32_t k) {
    if (app_conn_state() == CONN_CONNECTING) return; /* only Circle (cancel) while connecting */
    if (ctl_menu_is_open(&g.menu)) {
        if (k == LV_KEY_UP || k == LV_KEY_DOWN) ctl_menu_move(&g.menu, k == LV_KEY_DOWN ? 1 : -1);
        else if (k == LV_KEY_ENTER) menu_choose();
        else if (k == LV_KEY_ESC) {
            ctl_menu_close(&g.menu);
            g.menu_kind = MENU_NONE;
        }
        return;
    }
    if (k == LV_KEY_ESC) {
        home_back();
        return;
    }
    if (g.zone == HOME_TOP) {
        if ((k == LV_KEY_LEFT && g.sec > 0) || (k == LV_KEY_RIGHT && g.sec < SEC__N - 1))
            section_set(g.sec + (k == LV_KEY_RIGHT ? 1 : -1), true);
        else if (k == LV_KEY_DOWN || k == LV_KEY_ENTER) g.zone = HOME_ROW;
    } else if (g.sec == HOME_CONSOLES) {
        key_consoles(k);
    } else {
        key_settings(k);
    }
    paint();
}

bool home_back(void) {
    if (app_conn_state() == CONN_CONNECTING) {
        app_disconnect();
        return true;
    }
    if (ctl_menu_is_open(&g.menu)) {
        ctl_menu_close(&g.menu);
        g.menu_kind = MENU_NONE;
        return true;
    }
    bool used = true;
    switch (g.zone) {
        case HOME_TOP: g.zone = HOME_ROW; break;
        case HOME_PANEL: g.zone = HOME_ROW; break;
        case HOME_CARDS:
            g.zone = HOME_ACTIONS;
            g.item = HOME_ACT_PLAY;
            break;
        case HOME_ACTIONS: g.zone = HOME_ROW; break;
        case HOME_ROW:
            if (g.sec == HOME_SETTINGS) section_set(HOME_CONSOLES, true);
            else used = false; /* the top level: the remote's Back may exit */
            break;
    }
    paint();
    return used;
}

void home_section_step(int dir) {
    int s = g.sec + (dir > 0 ? 1 : -1);
    if (s < 0 || s >= SEC__N || app_conn_state() == CONN_CONNECTING) return;
    section_set(s, true);
    g.zone = HOME_ROW;
    paint();
}

/* ---------- app state ---------- */

static void hosts_changed(void) {
    if (ctl_menu_is_open(&g.menu)) ctl_menu_close(&g.menu);
    build_console_tiles();
    int sel = app_selected();
    g.focus[HOME_CONSOLES] = sel >= 0 ? sel : 0;
    if (g.focus[HOME_CONSOLES] >= g.n[HOME_CONSOLES]) g.focus[HOME_CONSOLES] = g.n[HOME_CONSOLES] - 1;
    if (g.sec == HOME_CONSOLES && (g.zone == HOME_ACTIONS || g.zone == HOME_CARDS) && on_add_tile()) g.zone = HOME_ROW;
    layout_row(HOME_CONSOLES, 0);
    if (g.sec == HOME_CONSOLES) {
        ttl_set(false, 1);
        hero_set(false);
    }
    paint();
}

void home_app_changed(app_change_t what) {
    switch (what) {
        case APP_CHANGED_HOSTS: hosts_changed(); break;
        case APP_CHANGED_HOST:
            if (g.sec == HOME_CONSOLES && app_selected() >= 0 && app_selected() != g.focus[HOME_CONSOLES]) {
                g.focus[HOME_CONSOLES] = app_selected();
                ttl_set(false, 1);
                hero_set(false);
                paint();
            } else if (g.sec == HOME_CONSOLES) {
                route_text();
                ttl_set(false, 1);
            }
            break;
        case APP_CHANGED_CONN:
        case APP_CHANGED_PROGRESS:
            connecting_refresh();
            home_settings_refresh();
            break;
        case APP_CHANGED_REG:
            topbar_refresh();
            home_settings_refresh();
            break;
        case APP_CHANGED_STATE:
            if (g.sec == HOME_CONSOLES && !on_add_tile() && focused_host()) hero_state(focused_host());
            break;
        case APP_CHANGED_UPDATE: update_changed(); break;
        case APP_CHANGED_PIN: break;
    }
}

void home_values_changed(void) {
    cards_refresh();
    if (g.sec == HOME_SETTINGS) lv_label_set_text(g.ttl_sub, home_settings_caption(g.focus[HOME_SETTINGS]));
}

void home_set_pad(pad_state_t state) {
    static const uint32_t col[] = { COL_DIM, COL_OK, COL_ERR };
    lv_obj_set_style_bg_color(g.pad_dot, C(col[state]), 0);
    lv_obj_set_style_shadow_color(g.pad_dot, C(col[state]), 0);
    art_id_t id = art_pad(input_pad_name());
    if (id != g.pad_art) { /* a DualSense or a DualShock 4: the drawings follow */
        g.pad_art = id;
        if (g.pad_img) lv_obj_del(g.pad_img);
        g.pad_img = art_create(g.pad_box, id, 44);
        lv_obj_center(g.pad_img);
        lv_obj_move_foreground(g.pad_dot);
        lv_obj_clean(g.card_pad_box);
        lv_obj_center(art_create(g.card_pad_box, id, 40));
        tile_t *t = &g.tiles[HOME_SETTINGS][SET_PAD];
        lv_obj_del(t->art);
        tile_art(t, art_create(t->tile, id, TILE_F * 56 / 100));
        layout_row(HOME_SETTINGS, 0);
    }
    cards_refresh();
    home_settings_refresh();
    if (g.sec == HOME_SETTINGS) ttl_set(false, 1);
}

void home_show(bool on, bool fade) {
    if (on == !lv_obj_has_flag(g.root, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) {
        lv_obj_clear_flag(g.root, LV_OBJ_FLAG_HIDDEN);
        if (fade) motion_from(g.root, MO_OPA, 0, 255, 420, 0, NULL);
        home_settings_refresh();
        cards_refresh();
        paint();
    } else {
        if (ctl_menu_is_open(&g.menu)) ctl_menu_close(&g.menu);
        lv_obj_add_flag(g.root, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *home_scroll_target(void) { return g.sec == HOME_SETTINGS ? home_settings_log() : NULL; }

/* ---------- building ---------- */

void home_create(lv_obj_t *parent) {
    memset(&g, 0, sizeof(g));
    g.pad_art = ART__N;
    g.conn_shown = CONN_IDLE;
    g.root = ctl_obj(parent);
    lv_obj_set_size(g.root, LV_PCT(100), LV_PCT(100));
    g.bg = ctl_rect(g.root, LV_PCT(100), LV_PCT(100), 0, COL_TINT_NEUTRAL, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(g.bg, C(COL_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(g.bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(g.bg, 0, 0);
    lv_obj_set_style_bg_grad_stop(g.bg, 235, 0);

    build_hero();
    for (int s = 0; s < SEC__N; s++) {
        g.row[s] = ctl_obj(g.root);
        lv_obj_set_size(g.row[s], LV_PCT(100), TILE_F);
        lv_obj_set_pos(g.row[s], 0, ROW_Y);
        ctl_margin(g.row[s], 24); /* the focus ring stands 12 px outside the tiles */
    }
    build_console_tiles();
    build_settings_tiles();
    home_settings_build(g.root);

    g.ttl = ctl_obj(g.root);
    lv_obj_set_size(g.ttl, NAME_W, 120);
    lv_obj_set_pos(g.ttl, HOME_SAFE_X + TILE_F + TILE_GAP + 8, ROW_Y + TILE_F / 2 - 56);
    g.ttl_name = ctl_label(g.ttl, FONT_TITLE, COL_TEXT, "");
    lv_obj_set_width(g.ttl_name, NAME_W - 20);
    lv_label_set_long_mode(g.ttl_name, LV_LABEL_LONG_DOT);
    g.ttl_sub = ctl_label(g.ttl, FONT_CAPTION, COL_SUBTLE, "");
    lv_obj_set_pos(g.ttl_sub, 0, 46);
    lv_obj_set_width(g.ttl_sub, NAME_W - 20);
    lv_label_set_long_mode(g.ttl_sub, LV_LABEL_LONG_DOT);

    build_topbar();
    g.toast = ctl_rect(g.root, 400, 64, LV_RADIUS_CIRCLE, COL_SHEET, 235);
    lv_obj_set_style_border_width(g.toast, 2, 0);
    lv_obj_set_style_border_color(g.toast, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.toast, LV_OPA_20, 0);
    lv_obj_align(g.toast, LV_ALIGN_BOTTOM_LEFT, HOME_SAFE_X, -HOME_SAFE_Y + 4);
    g.toast_lbl = ctl_label(g.toast, FONT_BODY, COL_TEXT, "");
    lv_obj_center(g.toast_lbl);
    lv_obj_set_style_opa(g.toast, LV_OPA_TRANSP, 0);
    build_update();
    build_connecting();

    int sel = app_selected();
    g.focus[HOME_CONSOLES] = sel >= 0 ? sel : 0;
    g.zone = HOME_ROW;
    topbar_refresh();
    home_set_pad(input_pad_state());
    cards_refresh();
    section_set(HOME_CONSOLES, false);
    layout_row(HOME_CONSOLES, 0);
    layout_row(HOME_SETTINGS, 0);
    paint();
    /* entrance: the row deals in, the hero rises */
    for (int i = 0; i < g.n[HOME_CONSOLES]; i++)
        motion_from(g.tiles[HOME_CONSOLES][i].tile, MO_TX, 200 + i * 60, 0, 600, (uint32_t)i * 50, NULL);
    motion_enter(g.hero, 180, 40);
    hero_set(true);
}

/* ---------- tests ---------- */

home_zone_t home_zone(void) { return g.zone; }
int home_section(void) { return g.sec; }
int home_index(void) { return g.focus[g.sec]; }
int home_item(void) { return g.item; }
bool home_menu_open(void) { return ctl_menu_is_open(&g.menu); }
bool home_connecting(void) { return g.root && !lv_obj_has_flag(g.conn, LV_OBJ_FLAG_HIDDEN); }
lv_obj_t *home_root(void) { return g.root; }
const char *home_update_note(void) { return g.update_hide ? lv_label_get_text(g.update_title) : ""; }
lv_obj_t *home_row(int section) { return section >= 0 && section < SEC__N ? g.row[section] : NULL; }
const char *home_pad_battery(void) { /* "" when the controller reports no battery */
    return g.card_pad_batt && !lv_obj_has_flag(g.card_pad_batt, LV_OBJ_FLAG_HIDDEN) ? lv_label_get_text(g.card_pad_batt)
                                                                                    : "";
}
