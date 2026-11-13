/* design/menu/addkit.c - the "Add a console" flow and its controls (see addkit.h). */
#include "addkit.h"

#include <stdio.h>
#include <string.h>

#include "art.h"
#include "ui/qr.h"

#define GAP     24
#define OPT_H   150 /* a way in, as a row */
#define CARD_H  290 /* ... as a card */
#define ICON_D  96
#define TILE    232 /* the console kind tiles */
#define OCT_W   148
#define CELL_H  104
#define BTN_H   88
#define STEP_DX 70 /* how far a step slides in and out */
#define PAIR_MS 2600
#define LINK_MS 2200
#define TV_IP   "192.168.1.20"
#define PSN_URL                                                                                                        \
    "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn"          \
    "&response_type=code&client_id=ba495a24-818c-472b-b12d-ff231c1b5745&redirect_uri=https://remoteplay.dl."           \
    "playstation.net/remoteplay/redirect&scope=psn:clientapp%20referenceDataService:countryConfig.read%20"             \
    "pushNotification:webSocket.desktop.connect%20sessionManager:remotePlaySession.system.update&request_locale="      \
    "en_US&ui=pr&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&"         \
    "duid=0000000700410080AB12CD34EF56AB78CD90EF12AB34CD56&"

/* ---------- controls ---------- */

static int32_t path_shake(const lv_anim_t *a) {
    float t = (float)a->act_time / (float)a->time;
    if (t >= 1) return a->end_value;
    return (int32_t)(lv_trigo_sin((int16_t)(t * 1080)) * (1 - t) * a->start_value / LV_TRIGO_SIN_MAX);
}

void ak_shake(lv_obj_t *o) { kit_anim_from(o, KA_TX, 18, 0, 420, 0, path_shake); }

void ak_outline(lv_obj_t *o, lv_coord_t w, lv_coord_t pad) {
    lv_obj_set_style_outline_width(o, w, 0);
    lv_obj_set_style_outline_pad(o, pad, 0);
    lv_obj_set_style_outline_color(o, lv_color_white(), 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_TRANSP, 0);
}

static void frost(lv_obj_t *o, lv_opa_t opa) {
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, opa, 0);
}

lv_obj_t *ak_button(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *icon, const char *text) {
    lv_obj_t *b = kit_rect(parent, w, h, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_20);
    lv_obj_set_style_text_color(b, C(COL_TEXT), 0);
    lv_obj_t *row = kit_obj(b);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
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

void ak_button_focus(lv_obj_t *b, bool on) {
    kit_anim(b, KA_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, lv_anim_path_ease_out);
    kit_mix_color(b, LV_STYLE_TEXT_COLOR, on ? 0x0a1020 : COL_TEXT, 200);
    kit_anim(b, KA_GROW, on ? 6 : 0, 300, 0, kit_path_spring);
}

void ak_button_danger_focus(lv_obj_t *b, bool on) {
    kit_mix_color(b, LV_STYLE_BG_COLOR, on ? COL_ERR : 0xffffff, 200);
    kit_anim(b, KA_BG_OPA, on ? LV_OPA_COVER : LV_OPA_20, 200, 0, lv_anim_path_ease_out);
    kit_mix_color(b, LV_STYLE_TEXT_COLOR, on ? 0xffffff : COL_DANGER_TEXT, 200);
    kit_anim(b, KA_GROW, on ? 6 : 0, 300, 0, kit_path_spring);
}

/* ----- number pad ----- */

static const char g_np_chars[AK_NP_KEYS] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b' };

static lv_coord_t np_gap(lv_coord_t k) { return k / 5; }
lv_coord_t ak_numpad_w(lv_coord_t k) { return 3 * k + 2 * np_gap(k); }
lv_coord_t ak_numpad_h(lv_coord_t k) { return 4 * k + 3 * np_gap(k); }

static void np_paint(ak_numpad_t *p) {
    for (int i = 0; i < AK_NP_KEYS; i++) {
        bool on = p->focused && i == p->sel;
        kit_anim(p->key[i], KA_BG_OPA, on ? LV_OPA_COVER : 30, 160, 0, lv_anim_path_ease_out);
        kit_mix_color(p->key[i], LV_STYLE_TEXT_COLOR, on ? 0x0a1020 : COL_TEXT, 160);
        kit_anim(p->key[i], KA_GROW, on ? 8 : 0, 300, 0, kit_path_spring);
    }
}

void ak_numpad_create(ak_numpad_t *p, lv_obj_t *parent, lv_coord_t k, bool dot) {
    memset(p, 0, sizeof(*p));
    p->dot = dot;
    p->box = kit_obj(parent);
    lv_obj_set_size(p->box, ak_numpad_w(k), ak_numpad_h(k));
    for (int i = 0; i < AK_NP_KEYS; i++) {
        lv_obj_t *key = p->key[i] = kit_rect(p->box, k, k, LV_RADIUS_CIRCLE, 0xffffff, 30);
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

void ak_numpad_focus(ak_numpad_t *p, bool on) {
    p->focused = on;
    np_paint(p);
}

bool ak_numpad_move(ak_numpad_t *p, nav_t k) {
    int r = p->sel / 3, c = p->sel % 3;
    if (k == NAV_LEFT) c--;
    else if (k == NAV_RIGHT) c++;
    else if (k == NAV_UP) r--;
    else if (k == NAV_DOWN) r++;
    else return false;
    if (r < 0 || r > 3 || c < 0 || c > 2) return false;
    int n = r * 3 + c;
    if (n == 9 && !p->dot) { /* the empty corner: step over it, or stop at the edge */
        if (k != NAV_DOWN) return false;
        n = 10;
    }
    p->sel = n;
    np_paint(p);
    return true;
}

char ak_numpad_press(ak_numpad_t *p) {
    kit_anim_from(p->key[p->sel], KA_GROW, -6, 8, 260, 0, kit_path_spring);
    return g_np_chars[p->sel];
}

/* ----- digit boxes ----- */

#define DIGIT_GAP   14
#define DIGIT_SPLIT 30

void ak_digits_create(ak_digits_t *d, lv_obj_t *parent, int n, lv_coord_t cell_w, lv_coord_t cell_h) {
    memset(d, 0, sizeof(*d));
    d->n = n > AK_DIGITS_MAX ? AK_DIGITS_MAX : n;
    lv_coord_t split = d->n == 8 ? DIGIT_SPLIT : 0;
    d->box = kit_obj(parent);
    lv_obj_set_size(d->box, d->n * cell_w + (d->n - 1) * DIGIT_GAP + split, cell_h);
    for (int i = 0; i < d->n; i++) {
        lv_obj_t *c = d->cell[i] = kit_rect(d->box, cell_w, cell_h, 22, 0xffffff, 16);
        frost(c, LV_OPA_20);
        lv_obj_set_pos(c, i * (cell_w + DIGIT_GAP) + (d->n == 8 && i >= 4 ? split : 0), 0);
        d->digit[i] = kit_label(c, FONT_HERO, COL_TEXT, "");
        lv_obj_center(d->digit[i]);
    }
}

void ak_digits_show(ak_digits_t *d, const char *text, bool active, bool error) {
    int len = (int)strlen(text);
    for (int i = 0; i < d->n; i++) {
        bool has = i < len, cur = active && i == len;
        char s[2] = { has ? text[i] : 0, 0 };
        lv_label_set_text(d->digit[i], s);
        kit_anim(d->cell[i], KA_BG_OPA, has ? 46 : 16, 160, 0, lv_anim_path_ease_out);
        lv_obj_set_style_border_color(d->cell[i], C(error ? COL_ERR : cur ? COL_ACCENT_HI : 0xffffff), 0);
        lv_obj_set_style_border_width(d->cell[i], error || cur ? 4 : 2, 0);
        kit_anim(d->cell[i], KA_BORDER_OPA, error || cur ? LV_OPA_COVER : LV_OPA_20, 160, 0, lv_anim_path_ease_out);
    }
}

/* ---------- the flow ---------- */

static struct {
    ak_layout_t lay;
    ak_hooks_t hooks;
    lv_obj_t *root, *main[AK__N], *aside[AK__N];
    ak_step_t step;
    int method, focus;
    bool ps5;
    /* how */
    lv_obj_t *opt[2], *illus[2];
    /* which console */
    lv_obj_t *kind_tile[2], *kind_art[2];
    /* address */
    lv_obj_t *oct_cell[4], *oct_lbl[4], *addr_next;
    char oct[4][4];
    int oct_i;
    ak_numpad_t np_addr;
    /* PIN */
    ak_digits_t pin;
    char pin_txt[AK_DIGITS_MAX + 1];
    bool pin_err;
    lv_obj_t *pin_msg, *pin_pair;
    ak_numpad_t np_pin;
    /* pairing, signing in */
    lv_obj_t *pair_title, *pair_ring, *pair_pct, *pair_art_box, *link_ring, *link_pct;
    /* ready */
    lv_obj_t *done_title, *done_btn[2], *done_tile_box;
    /* PSN */
    lv_obj_t *code_ta, *link_btn, *kb;
    bool kb_open;
    /* found */
    lv_obj_t *found_row[2], *found_check[2], *found_add;
    bool found_on[2];
} A;

static void paint(void);
static void go(ak_step_t s, int dir);

ak_step_t ak_step(void) { return A.step; }
int ak_method(void) { return A.method; }
bool ak_console_ps5(void) { return A.ps5; }
const char *ak_console_name(void) { return A.ps5 ? "PS5-724" : "PS4-311"; }

uint32_t ak_tint(void) {
    switch (A.step) {
        case AK_METHOD: return A.focus == AK_BY_PSN ? AK_TINT_PSN : AK_TINT_PIN;
        case AK_PSN:
        case AK_LINKING:
        case AK_FOUND: return AK_TINT_PSN;
        default: return AK_TINT_PIN;
    }
}

int ak_progress(int *count, const char **name) {
    static const char *const pin_names[] = { "How", "Console", "Address", "PIN", "Pairing" };
    static const char *const psn_names[] = { "How", "Sign in", "Consoles" };
    bool psn = A.step == AK_METHOD ? A.focus == AK_BY_PSN : A.method == AK_BY_PSN;
    if (A.step == AK_DONE || A.step == AK_FOUND) {
        if (count) *count = psn ? 3 : 5;
        if (name) *name = A.step == AK_DONE ? "Ready" : "Consoles";
        return psn ? 3 : 5;
    }
    int i;
    switch (A.step) {
        case AK_METHOD: i = 0; break;
        case AK_KIND: i = 1; break;
        case AK_ADDR: i = 2; break;
        case AK_PIN: i = 3; break;
        case AK_PAIRING:
        case AK_DONE: i = 4; break;
        case AK_PSN: i = 1; break;
        default: i = 2; break;
    }
    if (count) *count = psn ? 3 : 5;
    if (name) *name = psn ? psn_names[LV_MIN(i, 2)] : pin_names[i];
    return i + 1;
}

/* ----- building blocks of a step ----- */

/* LVGL invalidates a parent's own area (plus this) when it moves or hides: a focused button grown
 * past the column's edge, or a glow wider than the aside, would leave its edge behind otherwise */
static void ext_cb(lv_event_t *e) {
    lv_coord_t want = (lv_coord_t)(intptr_t)lv_event_get_user_data(e);
    lv_coord_t *cur = lv_event_get_param(e);
    if (*cur < want) *cur = want;
}

void ak_margin(lv_obj_t *o, lv_coord_t px) {
    lv_obj_add_event_cb(o, ext_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, (void *)(intptr_t)px);
    lv_obj_refresh_ext_draw_size(o);
}

static lv_obj_t *col(ak_step_t s) {
    lv_obj_t *m = A.main[s] = kit_obj(A.root);
    lv_obj_set_width(m, A.lay.main_w);
    lv_obj_set_height(m, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(m, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(m, 22, 0);
    lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *a = A.aside[s] = kit_obj(A.root);
    lv_obj_set_size(a, A.lay.aside_w, A.lay.aside_h);
    lv_obj_set_pos(a, A.lay.aside_x, A.lay.aside_y);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    ak_margin(m, 40);
    ak_margin(a, KIT_GLOW_D / 2);
    return m;
}

static lv_obj_t *wrap(lv_obj_t *parent, const lv_font_t *font, uint32_t c, const char *text, lv_coord_t w) {
    lv_obj_t *l = kit_label(parent, font, c, text);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *title(lv_obj_t *m, const char *text) {
    return wrap(m, A.lay.title_font, COL_TEXT, text, A.lay.main_w);
}
static lv_obj_t *text(lv_obj_t *m, const char *t) { return wrap(m, FONT_BODY, COL_SUBTLE, t, A.lay.main_w); }
static void space(lv_obj_t *m, lv_coord_t h) {
    lv_obj_t *s = kit_obj(m);
    lv_obj_set_size(s, 1, h);
}

static lv_obj_t *icon_tile(lv_obj_t *parent, bool psn, lv_coord_t d) {
    lv_obj_t *t = kit_rect(parent, d, d, d * 30 / 100, 0, LV_OPA_COVER);
    if (psn) kit_grad(t, 0x2bd2c4, 0x0b6f9c);
    else kit_grad(t, COL_ACCENT_HI, COL_ACCENT_DEEP);
    if (psn) lv_obj_center(kit_ps_diamond(t, d * 56 / 100, false));
    else lv_obj_center(kit_label(t, FONT_HERO, 0xffffff, LV_SYMBOL_HOME));
    return t;
}

/* the console art standing in a box, its glow behind */
static lv_obj_t *hero_art(lv_obj_t *parent, bool ps5, lv_coord_t w, lv_coord_t h, uint32_t glow) {
    lv_obj_t *box = kit_obj(parent);
    lv_obj_set_size(box, w, h);
    if (glow) {
        lv_obj_t *g = kit_glow(box, glow, LV_OPA_50);
        lv_obj_center(g);
    }
    lv_obj_center(kit_console_art_box(box, ps5, w, h));
    return box;
}

static lv_obj_t *ring(lv_obj_t *parent, lv_coord_t d, uint32_t c) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, 14, 0);
    lv_obj_set_style_arc_color(a, lv_color_white(), 0);
    lv_obj_set_style_arc_opa(a, 30, 0);
    lv_obj_set_style_arc_width(a, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, C(c), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

/* ----- illustrations: where the PIN comes from, and the phone ----- */

static lv_obj_t *illus_pin(lv_obj_t *aside) {
    lv_coord_t w = A.lay.aside_w, h = A.lay.aside_h;
    lv_obj_t *box = kit_obj(aside);
    lv_obj_set_size(box, w, h);
    lv_coord_t card_w = w * 62 / 100 > 470 ? 470 : w * 62 / 100, card_h = 290;
    lv_obj_t *g = kit_glow(box, AK_TINT_PIN, LV_OPA_50);
    lv_obj_center(g);
    /* the console, and its Link Device screen in front of it */
    lv_obj_t *art = kit_console_art_box(box, true, w - card_w + 140, h * 76 / 100);
    lv_obj_align(art, LV_ALIGN_CENTER, -card_w / 2 + 20, 0);
    lv_obj_t *card = kit_rect(box, card_w, card_h, 30, COL_SHEET, 240);
    frost(card, LV_OPA_20);
    lv_obj_align(card, LV_ALIGN_CENTER, (w - card_w) / 2 - 20, 30);
    lv_obj_set_style_shadow_width(card, 60, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_t *crumb = kit_label(card, FONT_SMALL, COL_MUTED, "SETTINGS  >  SYSTEM  >  REMOTE PLAY");
    lv_obj_set_style_text_letter_space(crumb, 1, 0);
    lv_obj_set_pos(crumb, 32, 30);
    lv_obj_set_pos(kit_label(card, FONT_TITLE, COL_TEXT, "Link Device"), 32, 62);
    lv_obj_t *code = kit_label(card, FONT_HERO, COL_ACCENT_HI, "4801 2297");
    lv_obj_set_style_text_letter_space(code, 6, 0);
    lv_obj_set_pos(code, 32, 128);
    lv_obj_t *cap = wrap(card, FONT_CAPTION, COL_MUTED, "On your console: the PIN this TV asks for", card_w - 64);
    lv_obj_set_pos(cap, 32, 206);
    return box;
}

static lv_obj_t *illus_psn(lv_obj_t *aside) {
    lv_coord_t w = A.lay.aside_w, h = A.lay.aside_h;
    lv_obj_t *box = kit_obj(aside);
    lv_obj_set_size(box, w, h);
    lv_obj_t *g = kit_glow(box, AK_TINT_PSN, LV_OPA_60);
    lv_obj_center(g);
    lv_coord_t ph = h - 30 > 400 ? 400 : h - 30, pw = ph * 52 / 100;
    lv_obj_t *phone = kit_rect(box, pw, ph, pw / 5, COL_SHEET, LV_OPA_COVER);
    lv_obj_set_style_border_width(phone, 6, 0);
    lv_obj_set_style_border_color(phone, lv_color_white(), 0);
    lv_obj_set_style_border_opa(phone, 70, 0);
    lv_obj_align(phone, LV_ALIGN_CENTER, pw / 2 + 20, 0);
    lv_obj_t *notch = kit_rect(phone, pw / 3, 10, LV_RADIUS_CIRCLE, 0xffffff, 60);
    lv_obj_align(notch, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *scr = kit_rect(phone, pw - 40, pw - 40, 16, 0xffffff, LV_OPA_COVER);
    lv_obj_align(scr, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_center(ui_qr(scr, PSN_URL, pw - 60));
    for (int i = 0; i < 3; i++) {
        lv_obj_t *line = kit_rect(phone, (pw - 60) * (3 - i) / 3, 12, LV_RADIUS_CIRCLE, 0xffffff, 50);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, 22, pw + 34 + i * 28);
    }
    lv_obj_t *em = kit_rect(box, ph * 42 / 100, ph * 42 / 100, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    kit_grad(em, 0x2bd2c4, 0x0b6f9c);
    lv_obj_align(em, LV_ALIGN_CENTER, -pw / 2 - 30, -ph / 6);
    lv_obj_center(kit_ps_diamond(em, ph * 26 / 100, false));
    return box;
}

/* ----- step: how ----- */

static lv_obj_t *option(lv_obj_t *parent, int which, lv_coord_t w) {
    static const char *const ttl[2] = { "With the console's PIN", "With your PSN account" };
    static const char *const cap[2] = { "On this network. The quickest way in, and it can wake the console up.",
                                        "Sign in on your phone. Works from anywhere and finds every console on it." };
    static const char *const tag[2] = { "LOCAL NETWORK", "ANYWHERE" };
    bool card = A.lay.method_cards;
    lv_obj_t *o = kit_rect(parent, w, card ? CARD_H : OPT_H, 30, 0xffffff, 18);
    frost(o, LV_OPA_10);
    ak_outline(o, 5, 6);
    lv_obj_t *ic = icon_tile(o, which == AK_BY_PSN, ICON_D);
    lv_obj_t *t = kit_label(o, FONT_TITLE, COL_TEXT, ttl[which]);
    lv_obj_t *tg = kit_label(o, FONT_SMALL, which ? 0x5fe0d2 : COL_ACCENT_HI, tag[which]);
    lv_obj_set_style_text_letter_space(tg, 3, 0);
    if (card) {
        lv_obj_set_pos(ic, 30, 30);
        lv_obj_align(tg, LV_ALIGN_TOP_RIGHT, -30, 36);
        lv_obj_set_pos(t, 30, 150);
        lv_obj_set_pos(wrap(o, FONT_CAPTION, COL_SUBTLE, cap[which], w - 60), 30, 196);
    } else {
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 26, 0);
        lv_obj_set_pos(tg, 150, 26);
        lv_obj_set_pos(t, 150, 50);
        lv_obj_set_pos(wrap(o, FONT_CAPTION, COL_SUBTLE, cap[which], w - 230), 150, 92);
        lv_obj_t *chev = kit_label(o, FONT_TITLE, COL_TEXT, LV_SYMBOL_RIGHT);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -32, 0);
    }
    return o;
}

static void build_method(void) {
    lv_obj_t *m = col(AK_METHOD);
    title(m, "Add a console");
    text(m, "Pick how this TV reaches it. Add it both ways and it stays one console, with the local network "
            "tried first.");
    space(m, 6);
    if (A.lay.method_cards) {
        lv_obj_t *row = kit_obj(m);
        lv_obj_set_size(row, A.lay.main_w, CARD_H);
        lv_coord_t cw = (A.lay.main_w - GAP) / 2;
        for (int i = 0; i < 2; i++) {
            A.opt[i] = option(row, i, cw);
            lv_obj_set_pos(A.opt[i], i * (cw + GAP), 0);
        }
    } else {
        for (int i = 0; i < 2; i++) A.opt[i] = option(m, i, A.lay.main_w);
    }
    A.illus[0] = illus_pin(A.aside[AK_METHOD]);
    A.illus[1] = illus_psn(A.aside[AK_METHOD]);
}

/* ----- step: which console ----- */

static void build_kind(void) {
    lv_obj_t *m = col(AK_KIND);
    title(m, "Which console is it?");
    text(m, "Pick the one you are pairing.");
    space(m, 10);
    lv_obj_t *row = kit_obj(m);
    lv_obj_set_size(row, A.lay.main_w, TILE + 60);
    for (int i = 0; i < 2; i++) {
        bool ps5 = i == 0;
        lv_obj_t *t = A.kind_tile[i] = kit_rect(row, TILE, TILE, 34, 0, LV_OPA_COVER);
        kit_grad(t, ps5 ? 0x2d528f : 0x3b4fa0, 0x0f1a33);
        frost(t, LV_OPA_10);
        ak_outline(t, 5, 7);
        lv_obj_set_pos(t, i * (TILE + 40), 0);
        lv_obj_center(kit_console_art_box(t, ps5, TILE * 86 / 100, TILE * 80 / 100));
        lv_obj_t *l = kit_label(row, FONT_HEADING, COL_TEXT, ps5 ? "PlayStation 5" : "PlayStation 4");
        lv_obj_set_pos(l, i * (TILE + 40) + 6, TILE + 22);
    }
    if (A.lay.stacked) return;
    for (int i = 0; i < 2; i++) {
        A.kind_art[i] = hero_art(A.aside[AK_KIND], i == 0, A.lay.aside_w, A.lay.aside_h, i ? AK_TINT_PIN : 0x3a6fd0);
        lv_obj_set_style_opa(A.kind_art[i], i ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    }
}

/* ----- step: address ----- */

static bool addr_valid(void) {
    for (int i = 0; i < 4; i++) {
        int v = 0;
        if (!A.oct[i][0] || sscanf(A.oct[i], "%d", &v) != 1 || v > 255) return false;
    }
    return true;
}

static void addr_show(void) {
    for (int i = 0; i < 4; i++) {
        bool cur = A.focus == 0 && i == A.oct_i;
        int v = 0;
        bool bad = A.oct[i][0] && sscanf(A.oct[i], "%d", &v) == 1 && v > 255;
        lv_label_set_text(A.oct_lbl[i], A.oct[i]);
        lv_obj_set_style_border_color(A.oct_cell[i], C(bad ? COL_ERR : cur ? COL_ACCENT_HI : 0xffffff), 0);
        lv_obj_set_style_border_width(A.oct_cell[i], bad || cur ? 4 : 2, 0);
        kit_anim(A.oct_cell[i], KA_BORDER_OPA, bad || cur ? LV_OPA_COVER : LV_OPA_20, 160, 0, lv_anim_path_ease_out);
        kit_anim(A.oct_cell[i], KA_BG_OPA, A.oct[i][0] ? 46 : 16, 160, 0, lv_anim_path_ease_out);
    }
    lv_obj_set_style_opa(A.addr_next, addr_valid() ? LV_OPA_COVER : LV_OPA_50, 0);
}

static void addr_type(char c) {
    char *o = A.oct[A.oct_i];
    size_t n = strlen(o);
    if (c == '\b') {
        if (!n && A.oct_i > 0) o = A.oct[--A.oct_i], n = strlen(o);
        if (n) o[n - 1] = 0;
    } else if (c == '.') {
        if (n && A.oct_i < 3) A.oct_i++;
    } else {
        if (n == 3 && A.oct_i < 3) o = A.oct[++A.oct_i], n = 0;
        if (n < 3) {
            o[n] = c;
            o[n + 1] = 0;
        }
        if (n + 1 == 3 && A.oct_i < 3) A.oct_i++;
        if (A.oct_i == 3 && strlen(A.oct[3]) == 3) A.focus = 1; /* complete: on to Next */
    }
}

static void build_addr(void) {
    lv_obj_t *m = col(AK_ADDR);
    title(m, "Where is it on your network?");
    text(m, "On the console: Settings > Network > Connection status. This TV is on " TV_IP ", so the start is "
            "filled in.");
    space(m, 10);
    lv_coord_t ow = (A.lay.main_w - 3 * 44) / 4 > OCT_W ? OCT_W : (A.lay.main_w - 3 * 44) / 4;
    lv_obj_t *row = kit_obj(m);
    lv_obj_set_size(row, 4 * ow + 3 * 44, CELL_H);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = A.oct_cell[i] = kit_rect(row, ow, CELL_H, 22, 0xffffff, 16);
        frost(c, LV_OPA_20);
        lv_obj_set_pos(c, i * (ow + 44), 0);
        A.oct_lbl[i] = kit_label(c, FONT_HERO, COL_TEXT, "");
        lv_obj_center(A.oct_lbl[i]);
        if (i < 3) {
            lv_obj_t *d = kit_label(row, FONT_HERO, COL_MUTED, ".");
            lv_obj_set_pos(d, i * (ow + 44) + ow + 12, 40);
        }
    }
    lv_obj_t *hint = kit_label(m, FONT_CAPTION, COL_MUTED, "The pad types it, so do the remote's number keys");
    (void)hint;
    space(m, 8);
    A.addr_next = ak_button(m, 300, BTN_H, LV_SYMBOL_RIGHT, "Next");
    ak_numpad_create(&A.np_addr, A.aside[AK_ADDR], A.lay.key, true);
    lv_obj_align(A.np_addr.box, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, 0);
}

/* ----- step: PIN ----- */

static void pin_type(char c) {
    size_t n = strlen(A.pin_txt);
    A.pin_err = false;
    if (c == '\b') {
        if (n) A.pin_txt[n - 1] = 0;
    } else if (c >= '0' && c <= '9' && n < 8) {
        A.pin_txt[n] = c;
        A.pin_txt[n + 1] = 0;
        if (n + 1 == 8) A.focus = 1; /* complete: on to Pair */
    }
}

static void build_pin(void) {
    lv_obj_t *m = col(AK_PIN);
    title(m, "Enter the PIN it shows");
    text(m, "On the console: Settings > System > Remote Play > Link Device. The PIN stays valid for 5 minutes.");
    space(m, 10);
    lv_coord_t cw = (A.lay.main_w - 7 * DIGIT_GAP - DIGIT_SPLIT) / 8;
    if (cw > 84) cw = 84;
    ak_digits_create(&A.pin, m, 8, cw, CELL_H);
    A.pin_msg = wrap(m, FONT_BODY, COL_DANGER_TEXT, "", A.lay.main_w);
    A.pin_pair = ak_button(m, 300, BTN_H, LV_SYMBOL_OK, "Pair");
    ak_numpad_create(&A.np_pin, A.aside[AK_PIN], A.lay.key, false);
    lv_obj_align(A.np_pin.box, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, 0);
}

/* ----- steps: pairing, signing in ----- */

static void ring_exec(void *ring_obj, int32_t v) {
    lv_arc_set_value(ring_obj, (int16_t)v);
    lv_obj_t *pct = ring_obj == A.pair_ring ? A.pair_pct : A.link_pct;
    lv_label_set_text_fmt(pct, "%d%%", (int)v);
}

static void ring_done(lv_anim_t *a) {
    if (a->var == A.pair_ring && A.step == AK_PAIRING) {
        if (!strncmp(A.pin_txt, "0000", 4)) {
            A.pin_err = true;
            go(AK_PIN, -1);
            ak_shake(A.pin.box);
        } else {
            go(AK_DONE, 1);
        }
    } else if (a->var == A.link_ring && A.step == AK_LINKING) {
        go(AK_FOUND, 1);
    }
}

static void ring_run(lv_obj_t *r, uint32_t ms) {
    lv_anim_del(r, ring_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_exec_cb(&a, ring_exec);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, ms);
    lv_anim_set_delay(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, ring_done);
    lv_anim_start(&a);
}

static lv_obj_t *ring_box(lv_obj_t *aside, lv_obj_t **ring_out, uint32_t c) {
    lv_coord_t d = (A.lay.aside_h < A.lay.aside_w ? A.lay.aside_h : A.lay.aside_w) - 20;
    if (d > 440) d = 440;
    lv_obj_t *g = kit_glow(aside, c, LV_OPA_50);
    lv_obj_center(g);
    *ring_out = ring(aside, d, c == AK_TINT_PSN ? 0x5fe0d2 : COL_ACCENT_HI);
    lv_obj_align(*ring_out, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *inner = kit_obj(*ring_out);
    lv_obj_set_size(inner, d * 64 / 100, d * 64 / 100);
    lv_obj_center(inner);
    return inner;
}

static void build_pairing(void) {
    lv_obj_t *m = col(AK_PAIRING);
    A.pair_title = title(m, "");
    text(m, "Keep the Link Device screen open on the console. This takes a few seconds.");
    A.pair_pct = kit_label(m, FONT_HERO, COL_ACCENT_HI, "0%");
    A.pair_art_box = ring_box(A.aside[AK_PAIRING], &A.pair_ring, AK_TINT_PIN);
}

static void build_linking(void) {
    lv_obj_t *m = col(AK_LINKING);
    title(m, "Signing in");
    text(m, "Getting your PSN account and the consoles on it.");
    A.link_pct = kit_label(m, FONT_HERO, 0x5fe0d2, "0%");
    lv_obj_t *inner = ring_box(A.aside[AK_LINKING], &A.link_ring, AK_TINT_PSN);
    lv_coord_t d = lv_obj_get_style_width(inner, 0) * 80 / 100;
    lv_obj_t *em = kit_rect(inner, d, d, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    kit_grad(em, 0x2bd2c4, 0x0b6f9c);
    lv_obj_center(em);
    lv_obj_center(kit_ps_diamond(em, d * 56 / 100, false));
}

/* ----- step: ready ----- */

static void build_done(void) {
    lv_obj_t *m = col(AK_DONE);
    A.done_title = title(m, "");
    text(m, "It is on Home now, next to your other consoles. Remote Play can start right away.");
    space(m, 10);
    lv_obj_t *row = kit_obj(m);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 20, 0);
    A.done_btn[0] = ak_button(row, A.lay.stacked ? 380 : 430, BTN_H + 8, LV_SYMBOL_PLAY, "Start Remote Play");
    A.done_btn[1] = ak_button(row, A.lay.stacked ? 280 : 300, BTN_H + 8, NULL, "Back to Home");
    A.done_tile_box = kit_obj(A.aside[AK_DONE]);
    lv_obj_set_size(A.done_tile_box, A.lay.aside_w, A.lay.aside_h);
}

static void done_tile(void) {
    lv_obj_clean(A.done_tile_box);
    lv_coord_t s = A.lay.aside_h - 90 > 300 ? 300 : A.lay.aside_h - 90;
    lv_obj_t *g = kit_glow(A.done_tile_box, A.ps5 ? 0x3a6fd0 : AK_TINT_PIN, LV_OPA_60);
    lv_obj_align(g, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0,
                 A.lay.stacked ? s / 2 - KIT_GLOW_D / 2 : -30);
    lv_obj_t *t = kit_rect(A.done_tile_box, s, s, s * 13 / 100, 0, LV_OPA_COVER);
    kit_grad(t, A.ps5 ? 0x2d528f : 0x3b4fa0, 0x0f1a33);
    frost(t, LV_OPA_20);
    lv_obj_align(t, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, A.lay.stacked ? 0 : -30);
    lv_obj_center(kit_console_art_box(t, A.ps5, s * 86 / 100, s * 80 / 100));
    lv_obj_t *badge = kit_rect(t, 76, 76, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    kit_grad(badge, 0x4be39a, 0x1f9e5c);
    lv_obj_set_style_border_width(badge, 5, 0);
    lv_obj_set_style_border_color(badge, C(COL_SHEET), 0);
    lv_obj_center(kit_label(badge, FONT_TITLE, 0xffffff, LV_SYMBOL_OK));
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, 26, -26);
    lv_obj_t *name = kit_label(t, FONT_TITLE, COL_TEXT, ak_console_name());
    lv_obj_set_width(name, s);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, 0, s + 22);
    kit_anim_from(t, KA_GROW, -40, 0, 600, 100, kit_path_spring);
    kit_anim_from(badge, KA_GROW, -38, 0, 500, 420, kit_path_spring);
    kit_anim_from(badge, KA_OPA, 0, 255, 200, 420, lv_anim_path_ease_out);
}

/* ----- step: PSN sign-in ----- */

/* clang-format off */
static const char *const g_kb_lower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "-", "_", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ".", LV_SYMBOL_OK, "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" };
static const char *const g_kb_upper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "-", "_", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", ".", LV_SYMBOL_OK, "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, "" };
#define KB_SPECIAL (LV_BTNMATRIX_CTRL_CHECKED | LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG)
static const lv_btnmatrix_ctrl_t g_kb_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KB_SPECIAL | 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, KB_SPECIAL | 2,
    KB_SPECIAL | 3, 1, 1, 1, 1, 1, 1, 1, KB_SPECIAL | 1, KB_SPECIAL | 1 };
/* clang-format on */
#define KB_FIRST_LETTER 11 /* "q" */

static void kb_style(lv_obj_t *kb) {
    lv_obj_remove_style_all(kb);
    lv_obj_set_style_bg_color(kb, C(COL_SHEET), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(kb, 40, 0);
    lv_obj_set_style_border_width(kb, 2, 0);
    lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_opa(kb, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(kb, 22, 0);
    lv_obj_set_style_pad_bottom(kb, 62, 0); /* the lower corners sit off-screen */
    lv_obj_set_style_pad_row(kb, 12, 0);
    lv_obj_set_style_pad_column(kb, 12, 0);
    lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, 26, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C(COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, FONT_HEADING, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, 12, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, C(COL_SUBTLE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb, C(0x0a1020), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb, C(0x0a1020), LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(kb, C(COL_ACCENT), LV_PART_ITEMS | LV_STATE_PRESSED);
}

static void kb_open(bool open) {
    if (open == A.kb_open) return;
    A.kb_open = open;
    if (A.hooks.keyboard) A.hooks.keyboard(open);
    lv_coord_t hidden = A.lay.kb_h + 60;
    if (open) {
        lv_obj_clear_flag(A.kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(A.kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_btnmatrix_set_selected_btn(A.kb, KB_FIRST_LETTER);
        lv_obj_add_state(A.code_ta, LV_STATE_FOCUSED);
        lv_event_send(A.code_ta, LV_EVENT_FOCUSED, NULL); /* the cursor blinks */
        kit_anim_from(A.kb, KA_TY, hidden, 0, 360, 0, lv_anim_path_ease_out);
        /* the field stays above the keyboard: the flow rises by what it would cover */
        lv_obj_update_layout(A.code_ta);
        lv_area_t f, p;
        lv_obj_get_coords(A.code_ta, &f);
        lv_obj_get_coords(A.lay.kb_parent, &p);
        lv_coord_t top = p.y2 - A.lay.kb_h, over = f.y2 + 28 - top - lv_obj_get_style_translate_y(A.root, 0);
        kit_anim(A.root, KA_TY, over > 0 ? -over : 0, 360, 0, lv_anim_path_ease_out);
        if (A.lay.stacked) kit_anim(A.aside[AK_PSN], KA_OPA, LV_OPA_TRANSP, 200, 0, lv_anim_path_ease_out);
    } else {
        if (A.lay.stacked) kit_anim(A.aside[AK_PSN], KA_OPA, LV_OPA_COVER, 300, 0, lv_anim_path_ease_out);
        lv_obj_clear_state(A.kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_obj_clear_state(A.code_ta, LV_STATE_FOCUSED);
        kit_anim(A.kb, KA_TY, hidden, 260, 0, lv_anim_path_ease_in);
        kit_anim(A.root, KA_TY, 0, 300, 0, lv_anim_path_ease_out);
    }
}

static void kb_ready_cb(lv_event_t *e) {
    (void)e;
    kb_open(false);
    A.focus = 1; /* on to Link */
    paint();
}

static void build_psn(void) {
    lv_obj_t *m = col(AK_PSN);
    title(m, "Sign in with your phone");
    static const char *const steps[3] = { "Scan the code with your phone's camera", "Sign in to PlayStation Network",
                                          "The last page stays blank: copy the code= value from its address bar" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = kit_obj(m);
        lv_obj_set_size(r, A.lay.main_w, LV_SIZE_CONTENT);
        lv_obj_t *n = kit_rect(r, 48, 48, LV_RADIUS_CIRCLE, 0xffffff, 36);
        lv_obj_center(kit_label(n, FONT_HEADING, COL_TEXT, i == 0 ? "1" : i == 1 ? "2" : "3"));
        lv_obj_t *t = wrap(r, FONT_BODY, COL_SUBTLE, steps[i], A.lay.main_w - 72);
        lv_obj_set_pos(t, 72, 9);
    }
    space(m, 6);
    lv_obj_t *row = kit_obj(m);
    lv_obj_set_size(row, A.lay.main_w, BTN_H);
    A.code_ta = lv_textarea_create(row);
    lv_obj_remove_style_all(A.code_ta);
    lv_obj_set_size(A.code_ta, A.lay.main_w - 260 - 20, BTN_H);
    lv_textarea_set_one_line(A.code_ta, true);
    lv_textarea_set_placeholder_text(A.code_ta, "Code");
    lv_textarea_set_max_length(A.code_ta, 64);
    lv_obj_set_style_bg_color(A.code_ta, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(A.code_ta, 26, 0);
    lv_obj_set_style_radius(A.code_ta, LV_RADIUS_CIRCLE, 0);
    frost(A.code_ta, LV_OPA_20);
    lv_obj_set_style_pad_hor(A.code_ta, 34, 0);
    lv_obj_set_style_pad_ver(A.code_ta, 26, 0);
    lv_obj_set_style_text_font(A.code_ta, FONT_HEADING, 0);
    lv_obj_set_style_text_color(A.code_ta, C(COL_TEXT), 0);
    lv_obj_set_style_text_color(A.code_ta, C(COL_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_width(A.code_ta, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(A.code_ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_color(A.code_ta, C(COL_ACCENT_HI), LV_PART_CURSOR);
    lv_obj_set_style_anim_time(A.code_ta, 500, LV_PART_CURSOR);
    lv_obj_set_style_opa(A.code_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_opa(A.code_ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_clear_flag(A.code_ta, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    ak_outline(A.code_ta, 5, 6);
    A.link_btn = ak_button(row, 260, BTN_H, LV_SYMBOL_OK, "Link");
    lv_obj_set_pos(A.link_btn, A.lay.main_w - 260, 0);

    lv_coord_t s = A.lay.aside_h - (A.lay.stacked ? 10 : 70);
    if (s > A.lay.aside_w) s = A.lay.aside_w;
    if (s > 470) s = 470;
    lv_obj_t *g = kit_glow(A.aside[AK_PSN], AK_TINT_PSN, LV_OPA_60);
    lv_obj_center(g);
    lv_obj_t *card = kit_rect(A.aside[AK_PSN], s, s, 36, 0xffffff, LV_OPA_COVER);
    lv_obj_align(card, A.lay.stacked ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, A.lay.stacked ? 0 : -24);
    lv_obj_set_style_shadow_width(card, 70, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_center(ui_qr(card, PSN_URL, s - 44));
    if (!A.lay.stacked) {
        lv_obj_t *cap = kit_label(A.aside[AK_PSN], FONT_CAPTION, COL_SUBTLE, "Scan with your phone's camera");
        lv_obj_align_to(cap, card, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    }

    A.kb = lv_keyboard_create(A.lay.kb_parent);
    kb_style(A.kb);
    lv_keyboard_set_map(A.kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char **)g_kb_lower, g_kb_ctrl);
    lv_keyboard_set_map(A.kb, LV_KEYBOARD_MODE_TEXT_UPPER, (const char **)g_kb_upper, g_kb_ctrl);
    lv_keyboard_set_mode(A.kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(A.kb, A.code_ta);
    lv_obj_set_size(A.kb, A.lay.kb_w, A.lay.kb_h + 40);
    lv_obj_align(A.kb, LV_ALIGN_BOTTOM_LEFT, A.lay.kb_x, 40);
    lv_obj_add_flag(A.kb, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(A.kb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(A.kb, kb_ready_cb, LV_EVENT_READY, NULL);
}

/* ----- step: the consoles found ----- */

static void found_paint_check(int i) {
    lv_obj_t *c = A.found_check[i];
    bool on = A.found_on[i];
    lv_obj_set_style_bg_color(c, C(on ? COL_ACCENT : 0xffffff), 0);
    kit_anim(c, KA_BG_OPA, on ? LV_OPA_COVER : 20, 160, 0, lv_anim_path_ease_out);
    lv_obj_set_style_text_opa(c, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    int n = A.found_on[0] + A.found_on[1];
    lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(A.found_add, 0), 1), n ? "Add %d console%s" : "Add", n,
                          n == 1 ? "" : "s");
    lv_obj_set_style_opa(A.found_add, n ? LV_OPA_COVER : LV_OPA_50, 0);
}

static void build_found(void) {
    static const struct {
        const char *name, *cap;
        bool ps5, grouped;
    } dev[2] = {
        { "Living Room PS5", "Already paired with its PIN: one console, both routes", true, true },
        { "Cabin PS4", "New: plays over PSN, from anywhere", false, false },
    };
    lv_obj_t *m = col(AK_FOUND);
    title(m, "2 consoles on your account");
    text(m, "Pick the ones to add to Home.");
    for (int i = 0; i < 2; i++) {
        lv_obj_t *r = A.found_row[i] = kit_rect(m, A.lay.main_w, 124, 28, 0xffffff, 18);
        frost(r, LV_OPA_10);
        ak_outline(r, 5, 6);
        lv_obj_t *t = kit_rect(r, 92, 92, 24, 0, LV_OPA_COVER);
        kit_grad(t, dev[i].ps5 ? 0x2d528f : 0x3b4fa0, 0x0f1a33);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_center(kit_console_art_box(t, dev[i].ps5, 80, 76));
        lv_obj_set_pos(kit_label(r, FONT_TITLE, COL_TEXT, dev[i].name), 132, 22);
        lv_obj_t *cap = kit_label(r, FONT_CAPTION, dev[i].grouped ? 0x5fe0d2 : COL_SUBTLE, dev[i].cap);
        lv_obj_set_width(cap, A.lay.main_w - 132 - 100);
        lv_label_set_long_mode(cap, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(cap, 132, 70);
        lv_obj_t *c = A.found_check[i] = kit_rect(r, 52, 52, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
        frost(c, LV_OPA_40);
        lv_obj_set_style_text_color(c, lv_color_white(), 0);
        lv_obj_center(kit_label(c, FONT_HEADING, 0xffffff, LV_SYMBOL_OK));
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -28, 0);
        A.found_on[i] = true;
    }
    space(m, 4);
    A.found_add = ak_button(m, 340, BTN_H, LV_SYMBOL_PLUS, "Add 2 consoles");
    for (int i = 0; i < 2; i++) found_paint_check(i);
    if (A.lay.stacked) return;
    lv_obj_t *g = kit_glow(A.aside[AK_FOUND], AK_TINT_PSN, LV_OPA_50);
    lv_obj_center(g);
    lv_obj_t *a5 = kit_console_art_box(A.aside[AK_FOUND], true, A.lay.aside_w / 2, A.lay.aside_h * 78 / 100);
    lv_obj_align(a5, LV_ALIGN_BOTTOM_MID, -A.lay.aside_w / 6, -40);
    lv_obj_t *a4 = kit_console_art_box(A.aside[AK_FOUND], false, A.lay.aside_w * 52 / 100, A.lay.aside_h / 2);
    lv_obj_align(a4, LV_ALIGN_BOTTOM_MID, A.lay.aside_w / 5, -30);
}

/* ---------- moving between steps ---------- */

static void hide_cb(lv_obj_t *o) {
    if (o == A.main[A.step] || o == A.aside[A.step]) return; /* came back to it meanwhile */
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_x(o, 0, 0);
}

static void slide_out(lv_obj_t *o, int dir) {
    kit_anim(o, KA_TX, -dir * STEP_DX, 220, 0, lv_anim_path_ease_in);
    kit_anim_then(o, KA_OPA, 0, 200, lv_anim_path_ease_in, hide_cb);
}

static void slide_in(lv_obj_t *o, int dir, uint32_t delay) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    kit_anim_from(o, KA_TX, dir * STEP_DX, 0, 380, delay, lv_anim_path_ease_out);
    kit_anim_from(o, KA_OPA, 0, 255, 300, delay, lv_anim_path_ease_out);
}

static void enter(ak_step_t s) {
    switch (s) {
        case AK_METHOD: A.focus = A.method; break;
        case AK_KIND: A.focus = A.ps5 ? 0 : 1; break;
        case AK_ADDR:
            A.focus = 0;
            A.np_addr.sel = 10; /* on 0: the address goes on with digits */
            break;
        case AK_PIN:
            A.focus = 0;
            if (!A.pin_err) A.pin_txt[0] = 0;
            A.np_pin.sel = 0;
            lv_label_set_text(A.pin_msg, A.pin_err ? "The console did not take that PIN. Check the 8 digits on its "
                                                     "screen, or ask it for a new one."
                                                   : "");
            break;
        case AK_PAIRING:
            lv_label_set_text_fmt(A.pair_title, "Pairing with your %s", A.ps5 ? "PS5" : "PS4");
            lv_obj_clean(A.pair_art_box);
            lv_obj_center(kit_console_art_box(A.pair_art_box, A.ps5, lv_obj_get_style_width(A.pair_art_box, 0),
                                              lv_obj_get_style_height(A.pair_art_box, 0)));
            ring_run(A.pair_ring, PAIR_MS);
            break;
        case AK_DONE:
            A.focus = 0;
            lv_label_set_text_fmt(A.done_title, "%s is ready", ak_console_name());
            done_tile();
            break;
        case AK_PSN: A.focus = 0; break;
        case AK_LINKING: ring_run(A.link_ring, LINK_MS); break;
        case AK_FOUND: A.focus = 2; break;
        default: break;
    }
}

static void go(ak_step_t s, int dir) {
    ak_step_t old = A.step;
    if (old == AK_PAIRING) lv_anim_del(A.pair_ring, ring_exec);
    if (old == AK_LINKING) lv_anim_del(A.link_ring, ring_exec);
    if (A.kb_open) kb_open(false);
    if (old != s) {
        slide_out(A.main[old], dir);
        slide_out(A.aside[old], dir);
    }
    A.step = s;
    enter(s);
    slide_in(A.main[s], dir, 80);
    slide_in(A.aside[s], dir, 140);
    paint();
    if (A.hooks.step) A.hooks.step(s, A.method);
}

static void paint(void) {
    switch (A.step) {
        case AK_METHOD:
            for (int i = 0; i < 2; i++) {
                bool on = i == A.focus;
                kit_anim(A.opt[i], KA_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, lv_anim_path_ease_out);
                kit_anim(A.opt[i], KA_BG_OPA, on ? 40 : 18, 160, 0, lv_anim_path_ease_out);
                kit_anim(A.opt[i], KA_GROW, on ? 6 : 0, 300, 0, kit_path_spring);
                kit_anim(A.illus[i], KA_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 300, 0, lv_anim_path_ease_out);
            }
            break;
        case AK_KIND:
            for (int i = 0; i < 2; i++) {
                bool on = i == A.focus;
                kit_anim(A.kind_tile[i], KA_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0,
                         lv_anim_path_ease_out);
                kit_anim(A.kind_tile[i], KA_GROW, on ? 10 : 0, 320, 0, kit_path_spring);
                if (A.kind_art[i]) kit_anim(A.kind_art[i], KA_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 300, 0, NULL);
            }
            break;
        case AK_ADDR:
            ak_numpad_focus(&A.np_addr, A.focus == 0);
            ak_button_focus(A.addr_next, A.focus == 1);
            addr_show();
            break;
        case AK_PIN:
            ak_numpad_focus(&A.np_pin, A.focus == 0);
            ak_button_focus(A.pin_pair, A.focus == 1);
            ak_digits_show(&A.pin, A.pin_txt, A.focus == 0, A.pin_err);
            lv_obj_set_style_opa(A.pin_pair, strlen(A.pin_txt) == 8 ? LV_OPA_COVER : LV_OPA_50, 0);
            break;
        case AK_DONE:
            for (int i = 0; i < 2; i++) ak_button_focus(A.done_btn[i], i == A.focus);
            break;
        case AK_PSN:
            kit_anim(A.code_ta, KA_OUTLINE_OPA, A.focus == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
            ak_button_focus(A.link_btn, A.focus == 1);
            lv_obj_set_style_opa(A.link_btn, lv_textarea_get_text(A.code_ta)[0] ? LV_OPA_COVER : LV_OPA_50, 0);
            break;
        case AK_FOUND:
            for (int i = 0; i < 2; i++) {
                bool on = i == A.focus;
                kit_anim(A.found_row[i], KA_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
                kit_anim(A.found_row[i], KA_BG_OPA, on ? 40 : 18, 160, 0, NULL);
            }
            ak_button_focus(A.found_add, A.focus == 2);
            break;
        default: break;
    }
}

/* ---------- keys ---------- */

/* a step with a number pad and one button: the pad has the focus, the button is beside it
 * (LEFT of a wide aside) or above it (UP, stacked) */
static void pad_nav(nav_t k, ak_numpad_t *np, lv_obj_t *btn, bool ready, void (*type)(char), ak_step_t next,
                    ak_step_t back) {
    nav_t to_btn = A.lay.stacked ? NAV_UP : NAV_LEFT, to_pad = A.lay.stacked ? NAV_DOWN : NAV_RIGHT;
    if (k == NAV_BACK) {
        go(back, -1);
        return;
    }
    if (A.focus == 0) {
        if (k == NAV_OK) type(ak_numpad_press(np));
        else if (!ak_numpad_move(np, k) && k == to_btn) A.focus = 1;
    } else if (k == to_pad) {
        A.focus = 0;
    } else if (k == NAV_OK) {
        if (ready) go(next, 1);
        else ak_shake(btn);
    }
    paint();
}

static void kb_nav(nav_t k) {
    if (k == NAV_BACK) {
        kb_open(false);
        A.focus = 0;
    } else if (k == NAV_OK) {
        lv_event_send(A.kb, LV_EVENT_PRESSED, NULL);
        if (A.kb_open) lv_event_send(A.kb, LV_EVENT_RELEASED, NULL);
        return;
    } else {
        uint32_t key = k == NAV_UP     ? LV_KEY_UP
                       : k == NAV_DOWN ? LV_KEY_DOWN
                       : k == NAV_LEFT ? LV_KEY_LEFT
                                       : LV_KEY_RIGHT;
        lv_event_send(A.kb, LV_EVENT_KEY, &key);
    }
    paint();
}

void ak_nav(nav_t k) {
    bool cards = A.lay.method_cards;
    switch (A.step) {
        case AK_METHOD:
            if ((cards && (k == NAV_LEFT || k == NAV_RIGHT)) || (!cards && (k == NAV_UP || k == NAV_DOWN))) {
                A.focus = k == NAV_LEFT || k == NAV_UP ? 0 : 1;
            } else if (k == NAV_OK) {
                A.method = A.focus;
                go(A.method == AK_BY_PIN ? AK_KIND : AK_PSN, 1);
                return;
            } else if (k == NAV_BACK) {
                if (A.hooks.close) A.hooks.close(false);
                return;
            }
            break;
        case AK_KIND:
            if (k == NAV_LEFT || k == NAV_RIGHT) A.focus = k == NAV_RIGHT;
            else if (k == NAV_OK) {
                A.ps5 = A.focus == 0;
                go(AK_ADDR, 1);
                return;
            } else if (k == NAV_BACK) {
                go(AK_METHOD, -1);
                return;
            }
            break;
        case AK_ADDR: pad_nav(k, &A.np_addr, A.addr_next, addr_valid(), addr_type, AK_PIN, AK_KIND); return;
        case AK_PIN: pad_nav(k, &A.np_pin, A.pin_pair, strlen(A.pin_txt) == 8, pin_type, AK_PAIRING, AK_ADDR); return;
        case AK_PAIRING:
            if (k == NAV_BACK) go(AK_PIN, -1);
            return;
        case AK_LINKING:
            if (k == NAV_BACK) go(AK_PSN, -1);
            return;
        case AK_DONE:
            if (k == NAV_LEFT || k == NAV_RIGHT) A.focus = k == NAV_RIGHT;
            else if (k == NAV_OK || k == NAV_BACK) {
                if (A.hooks.close) A.hooks.close(true);
                return;
            }
            break;
        case AK_PSN:
            if (A.kb_open) {
                kb_nav(k);
                return;
            }
            if (k == NAV_BACK) {
                go(AK_METHOD, -1);
                return;
            }
            if (k == NAV_RIGHT && A.focus == 0) A.focus = 1;
            else if (k == NAV_LEFT && A.focus == 1) A.focus = 0;
            else if (k == NAV_OK && A.focus == 0) kb_open(true);
            else if (k == NAV_OK) {
                if (lv_textarea_get_text(A.code_ta)[0]) {
                    go(AK_LINKING, 1);
                    return;
                }
                ak_shake(A.link_btn);
                A.focus = 0;
            }
            break;
        case AK_FOUND:
            if (k == NAV_UP && A.focus > 0) A.focus--;
            else if (k == NAV_DOWN && A.focus < 2) A.focus++;
            else if (k == NAV_OK && A.focus < 2) {
                A.found_on[A.focus] = !A.found_on[A.focus];
                found_paint_check(A.focus);
            } else if (k == NAV_OK && (A.found_on[0] || A.found_on[1])) {
                if (A.hooks.close) A.hooks.close(true);
                return;
            } else if (k == NAV_BACK) {
                go(AK_PSN, -1);
                return;
            }
            break;
        default: break;
    }
    paint();
}

/* ---------- creation ---------- */

void ak_create(lv_obj_t *parent, const ak_layout_t *lay, const ak_hooks_t *hooks) {
    memset(&A, 0, sizeof(A));
    A.lay = *lay;
    if (hooks) A.hooks = *hooks;
    A.ps5 = true;
    snprintf(A.oct[0], 4, "192"); /* TV_IP's network */
    snprintf(A.oct[1], 4, "168");
    snprintf(A.oct[2], 4, "1");
    A.oct_i = 3;
    A.root = kit_obj(parent);
    lv_obj_set_pos(A.root, lay->x, lay->y);
    lv_obj_set_size(A.root, lay->stacked ? lay->main_w : lay->aside_x + lay->aside_w, lay->aside_y + lay->aside_h);
    build_method();
    build_kind();
    build_addr();
    build_pin();
    build_pairing();
    build_done();
    build_psn();
    build_linking();
    build_found();
    A.step = AK_METHOD;
    enter(AK_METHOD);
    lv_obj_clear_flag(A.main[AK_METHOD], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(A.aside[AK_METHOD], LV_OBJ_FLAG_HIDDEN);
    paint();
}
