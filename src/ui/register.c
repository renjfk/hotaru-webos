/* ui/register.c - "Add a console", the full-screen setup flow (see register.h). */
#include "register.h"

#include <stdio.h>
#include <string.h>

#include "../app.h"
#include "../config/config.h"
#include "../log.h"
#include "art.h"
#include "controls.h"
#include "motion.h"
#include "qr.h"
#include "ui.h"

#define SAFE_X    96
#define SAFE_Y    54
#define TRACK_Y   118
#define TRACK_W   (1920 - 2 * SAFE_X)
#define FLOW_Y    200
#define HEAD_ROOM 40 /* how far the flow may rise for the keyboard before the header makes way */
#define MAIN_W    820
#define ASIDE_X   900
#define ASIDE_Y   (-10)
#define ASIDE_W   (TRACK_W - ASIDE_X)
#define ASIDE_H   720
#define KEY       104 /* number pad keys */
#define GAP       24
#define OPT_H     150 /* a way in, as a row */
#define ICON_D    96
#define TILE      232 /* console kind tiles */
#define OCT_GAP   44
#define CELL_H    104
#define BTN_H     88
#define FIELD_GAP 20
#define LINK_W    260
#define STEP_DX   70  /* how far a step slides in and out */
#define FADE_MS   220 /* opening and closing: a crossfade with Home */
#define FOUND_MAX 4
#define DONE_TILE 300 /* the new console, ready */
#define BADGE_D   76
#define PIN_LEN   8

enum { BY_PIN, BY_PSN };
enum { ICON_LAN, ICON_PSN, ICON_TYPE };

static struct {
    lv_obj_t *root, *bg, *glow, *head, *fill, *step_lbl, *hints, *flow, *main[REG__N], *aside[REG__N];
    reg_step_t step;
    int method, focus;
    bool ps5, need_account, account_then_pin, typing_id, started;
    /* how */
    lv_obj_t *opt[2], *illus[2];
    /* which console */
    lv_obj_t *kind_tile[2], *kind_art[2];
    /* your PSN account */
    lv_obj_t *acc_opt[2], *acc_row, *acc_ta, *acc_next, *acc_msg;
    /* address */
    lv_obj_t *oct_cell[4], *oct_lbl[4], *addr_next, *addr_text;
    char addr_hint[200]; /* the address step's text when nothing was found */
    char oct[4][4];
    int oct_i;
    ctl_numpad_t np_addr;
    /* PIN */
    ctl_digits_t pin;
    char pin_txt[PIN_LEN + 1], err[160];
    bool pin_err;
    lv_obj_t *pin_msg, *pin_pair;
    ctl_numpad_t np_pin;
    /* pairing, signing in */
    lv_obj_t *pair_title, *pair_ring, *pair_pct, *pair_art_box, *link_ring, *link_pct, *link_status;
    /* ready */
    lv_obj_t *done_title, *done_btn[2], *done_box;
    /* PSN sign-in */
    lv_obj_t *code_ta, *link_btn, *psn_msg;
    /* the consoles found */
    lv_obj_t *found_title, *found_text, *found_list, *found_done;
    /* the keyboard */
    lv_obj_t *kb, *kb_ta;
    bool kb_open;
} R;

static void paint(void);
static void go(reg_step_t s, int dir);

/* ---------- the frame: backdrop, progress, hints ---------- */

static uint32_t tint(void) {
    switch (R.step) {
        case REG_METHOD: return R.focus == BY_PSN ? COL_TINT_PSN : COL_TINT_LAN;
        case REG_PSN:
        case REG_LINKING:
        case REG_FOUND: return COL_TINT_PSN;
        default: return COL_TINT_LAN;
    }
}

/* the step number in its path, the path's length, and the step's name */
static int progress(int *count, const char **name) {
    bool psn = R.step == REG_METHOD ? R.focus == BY_PSN : R.method == BY_PSN;
    if (psn) {
        static const char *const n[] = { "How", "Sign in", "Consoles" };
        int i = R.step == REG_METHOD ? 0 : R.step == REG_PSN ? 1 : 2;
        *count = 3;
        *name = n[i];
        return i + 1;
    }
    static const struct {
        reg_step_t s;
        const char *n;
    } path[] = {
        { REG_METHOD, "How" },   { REG_KIND, "Console" }, { REG_ACCOUNT, "Account" },
        { REG_ADDR, "Address" }, { REG_PIN, "PIN" },      { REG_PAIRING, "Pairing" },
    };
    reg_step_t s = R.step == REG_PSN || R.step == REG_LINKING ? REG_ACCOUNT : R.step; /* signing in for the ID */
    int len = 0, at = -1;
    for (size_t i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
        if (path[i].s == REG_ACCOUNT && !R.need_account) continue;
        if (path[i].s == s) {
            at = len;
            *name = path[i].n;
        }
        len++;
    }
    *count = len;
    if (at < 0) { /* ready */
        *name = "Ready";
        return len;
    }
    return at + 1;
}

static void follow(void) {
    uint32_t t = tint();
    motion_color(R.bg, LV_STYLE_BG_COLOR, t, 420);
    motion_color(R.glow, LV_STYLE_IMG_RECOLOR, t + 0x202020, 420);
    int n = 1;
    const char *name = "";
    int i = progress(&n, &name);
    bool done = R.step == REG_DONE || R.step == REG_FOUND;
    motion_to(R.fill, MO_W, TRACK_W * (done ? n : i) / n, 420, 0, NULL);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d / %d" BULLET "%s", i, n, name);
    motion_roll_text(R.step_lbl, buf, 1);
}

/* ---------- building blocks of a step ---------- */

static lv_obj_t *col(reg_step_t s) {
    lv_obj_t *m = R.main[s] = ctl_obj(R.flow);
    lv_obj_set_width(m, MAIN_W);
    lv_obj_set_height(m, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(m, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(m, 22, 0);
    lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    ctl_margin(m, 40);
    lv_obj_t *a = R.aside[s] = ctl_obj(R.flow);
    lv_obj_set_size(a, ASIDE_W, ASIDE_H);
    lv_obj_set_pos(a, ASIDE_X, ASIDE_Y);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    ctl_margin(a, CTL_GLOW_D / 2);
    return m;
}

static lv_obj_t *wrap(lv_obj_t *parent, const lv_font_t *font, uint32_t c, const char *t, lv_coord_t w) {
    lv_obj_t *l = ctl_label(parent, font, c, t);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *title(lv_obj_t *m, const char *t) { return wrap(m, FONT_HERO, COL_TEXT, t, MAIN_W); }
static lv_obj_t *text(lv_obj_t *m, const char *t) { return wrap(m, FONT_BODY, COL_SUBTLE, t, MAIN_W); }
static void space(lv_obj_t *m, lv_coord_t h) { lv_obj_set_size(ctl_obj(m), 1, h); }

static void frost(lv_obj_t *o, lv_opa_t opa) {
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, opa, 0);
}

static lv_obj_t *icon_tile(lv_obj_t *parent, int icon, lv_coord_t d) {
    lv_obj_t *t = ctl_rect(parent, d, d, d * 30 / 100, 0, LV_OPA_COVER);
    if (icon == ICON_PSN) {
        ctl_grad(t, COL_PSN, COL_PSN_DEEP);
        lv_obj_center(ctl_ps_diamond(t, d * 56 / 100));
    } else {
        ctl_grad(t, COL_ACCENT_HI, COL_ACCENT_DEEP);
        lv_obj_center(ctl_label(t, FONT_HERO, COL_TEXT, icon == ICON_LAN ? LV_SYMBOL_HOME : LV_SYMBOL_KEYBOARD));
    }
    return t;
}

static lv_obj_t *option(lv_obj_t *parent, int icon, const char *tag, const char *ttl, const char *cap) {
    lv_obj_t *o = ctl_rect(parent, MAIN_W, OPT_H, 30, COL_FROST, 18);
    frost(o, LV_OPA_10);
    ctl_outline(o, 5, 6);
    lv_obj_align(icon_tile(o, icon, ICON_D), LV_ALIGN_LEFT_MID, 26, 0);
    if (tag) {
        lv_obj_t *tg = ctl_label(o, FONT_SMALL, icon == ICON_PSN ? COL_PSN_HI : COL_ACCENT_HI, tag);
        lv_obj_set_style_text_letter_space(tg, 3, 0);
        lv_obj_set_pos(tg, 150, 26);
    }
    lv_obj_set_pos(ctl_label(o, FONT_TITLE, COL_TEXT, ttl), 150, tag ? 50 : 34);
    lv_obj_set_pos(wrap(o, FONT_CAPTION, COL_SUBTLE, cap, MAIN_W - 230), 150, tag ? 92 : 78);
    lv_obj_align(ctl_label(o, FONT_TITLE, COL_TEXT, LV_SYMBOL_RIGHT), LV_ALIGN_RIGHT_MID, -32, 0);
    return o;
}

static void option_focus(lv_obj_t *o, bool on) {
    motion_to(o, MO_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
    motion_to(o, MO_BG_OPA, on ? 40 : 18, 160, 0, NULL);
    motion_to(o, MO_GROW, on ? 6 : 0, 300, 0, motion_spring);
}

static lv_obj_t *console_tile(lv_obj_t *parent, bool ps5, lv_coord_t s) {
    lv_obj_t *t = ctl_rect(parent, s, s, s * 14 / 100, 0, LV_OPA_COVER);
    ctl_grad(t, ps5 ? 0x2d528f : 0x3b4fa0, 0x0f1a33);
    frost(t, LV_OPA_10);
    lv_obj_center(art_box(t, art_console(ps5), s * 86 / 100, s * 80 / 100));
    return t;
}

/* a field and its button on one row */
static lv_obj_t *field_row(lv_obj_t *m, const char *placeholder, uint32_t max, lv_obj_t **ta, lv_obj_t **btn,
                           const char *btn_text) {
    lv_obj_t *row = ctl_obj(m);
    lv_obj_set_size(row, MAIN_W, BTN_H);
    *ta = ctl_field(row, MAIN_W - LINK_W - FIELD_GAP, BTN_H, placeholder, max);
    *btn = ctl_button(row, LINK_W, BTN_H, LV_SYMBOL_OK, btn_text);
    lv_obj_set_pos(*btn, MAIN_W - LINK_W, 0);
    return row;
}

/* ---------- illustrations: where the PIN comes from, and the phone ---------- */

static lv_obj_t *illus_pin(lv_obj_t *aside) {
    lv_obj_t *box = ctl_obj(aside);
    lv_obj_set_size(box, ASIDE_W, ASIDE_H);
    lv_coord_t cw = 470, ch = 290;
    lv_obj_t *art = art_box(box, ART_PS5, ASIDE_W - cw + 140, ASIDE_H * 76 / 100);
    lv_obj_align(art, LV_ALIGN_CENTER, -cw / 2 + 20, 0);
    lv_obj_t *card = ctl_rect(box, cw, ch, 30, COL_SHEET, 240);
    frost(card, LV_OPA_20);
    lv_obj_set_style_shadow_width(card, 60, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
    lv_obj_align(card, LV_ALIGN_CENTER, (ASIDE_W - cw) / 2 - 20, 30);
    lv_obj_t *crumb = ctl_label(card, FONT_SMALL, COL_MUTED, "SETTINGS  >  SYSTEM  >  REMOTE PLAY");
    lv_obj_set_style_text_letter_space(crumb, 1, 0);
    lv_obj_set_pos(crumb, 32, 30);
    lv_obj_set_pos(ctl_label(card, FONT_TITLE, COL_TEXT, "Link Device"), 32, 62);
    lv_obj_t *code = ctl_label(card, FONT_HERO, COL_ACCENT_HI, "4801 2297");
    lv_obj_set_style_text_letter_space(code, 6, 0);
    lv_obj_set_pos(code, 32, 128);
    lv_obj_set_pos(wrap(card, FONT_CAPTION, COL_MUTED, "On your console: the PIN this TV asks for", cw - 64), 32, 206);
    return box;
}

static lv_obj_t *illus_psn(lv_obj_t *aside) {
    lv_obj_t *box = ctl_obj(aside);
    lv_obj_set_size(box, ASIDE_W, ASIDE_H);
    lv_coord_t ph = 400, pw = ph * 52 / 100;
    lv_obj_t *phone = ctl_rect(box, pw, ph, pw / 5, COL_SHEET, LV_OPA_COVER);
    lv_obj_set_style_border_width(phone, 6, 0);
    lv_obj_set_style_border_color(phone, lv_color_white(), 0);
    lv_obj_set_style_border_opa(phone, 70, 0);
    lv_obj_align(phone, LV_ALIGN_CENTER, pw / 2 + 20, 0);
    lv_obj_align(ctl_rect(phone, pw / 3, 10, LV_RADIUS_CIRCLE, COL_FROST, 60), LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *scr = ctl_rect(phone, pw - 40, pw - 40, 16, COL_FROST, LV_OPA_COVER);
    lv_obj_align(scr, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_center(ui_qr(scr, app_reg_psn_url(), pw - 60));
    for (int i = 0; i < 3; i++) {
        lv_obj_t *line = ctl_rect(phone, (pw - 60) * (3 - i) / 3, 12, LV_RADIUS_CIRCLE, COL_FROST, 50);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, 22, pw + 34 + i * 28);
    }
    lv_obj_t *em = ctl_rect(box, ph * 42 / 100, ph * 42 / 100, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    ctl_grad(em, COL_PSN, COL_PSN_DEEP);
    lv_obj_align(em, LV_ALIGN_CENTER, -pw / 2 - 30, -ph / 6);
    lv_obj_center(ctl_ps_diamond(em, ph * 26 / 100));
    return box;
}

/* ---------- the steps ---------- */

static void build_method(void) {
    lv_obj_t *m = col(REG_METHOD);
    title(m, "Add a console");
    text(m, "Pick how this TV reaches it. Add it both ways and it stays one console, with the local network "
            "tried first.");
    space(m, 6);
    R.opt[BY_PIN] = option(m, ICON_LAN, "LOCAL NETWORK", "With the console's PIN",
                           "On this network. The quickest way in, and it can wake the console up.");
    R.opt[BY_PSN] = option(m, ICON_PSN, "ANYWHERE", "With your PSN account",
                           "Sign in on your phone. Works from anywhere and finds every console on it.");
    R.illus[BY_PIN] = illus_pin(R.aside[REG_METHOD]);
    R.illus[BY_PSN] = illus_psn(R.aside[REG_METHOD]);
}

static void build_kind(void) {
    lv_obj_t *m = col(REG_KIND);
    title(m, "Which console is it?");
    text(m, "Pick the one you are pairing.");
    space(m, 10);
    lv_obj_t *row = ctl_obj(m);
    lv_obj_set_size(row, MAIN_W, TILE + 60);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = R.kind_tile[i] = console_tile(row, i == 0, TILE);
        ctl_outline(t, 5, 7);
        lv_obj_set_pos(t, i * (TILE + 40), 0);
        lv_obj_set_pos(ctl_label(row, FONT_HEADING, COL_TEXT, i ? "PlayStation 4" : "PlayStation 5"),
                       i * (TILE + 40) + 6, TILE + 22);
        lv_obj_t *big = R.kind_art[i] = ctl_obj(R.aside[REG_KIND]);
        lv_obj_set_size(big, ASIDE_W, ASIDE_H);
        lv_obj_center(art_box(big, art_console(i == 0), ASIDE_W, ASIDE_H));
        lv_obj_set_style_opa(big, i ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    }
}

static void build_account(void) {
    lv_obj_t *m = col(REG_ACCOUNT);
    title(m, "Your PSN account");
    text(m, "Pairing with a PIN uses your PSN account ID. Sign in once with your phone and this TV keeps it, or "
            "type the ID if you have it.");
    space(m, 6);
    R.acc_opt[0] = option(m, ICON_PSN, NULL, "Sign in with your phone",
                          "Scan a code and sign in; the console's PIN comes right after.");
    R.acc_opt[1] =
        option(m, ICON_TYPE, NULL, "Type the account ID", "12 characters of base64, from another Remote Play app.");
    R.acc_row = field_row(m, "Account ID", 24, &R.acc_ta, &R.acc_next, "Next");
    lv_obj_add_flag(R.acc_row, LV_OBJ_FLAG_HIDDEN);
    R.acc_msg = wrap(m, FONT_BODY, COL_DANGER_TEXT, "", MAIN_W);
    illus_psn(R.aside[REG_ACCOUNT]);
}

/* ----- address ----- */

static bool addr_valid(void) {
    for (int i = 0; i < 4; i++) {
        int v = 0;
        if (!R.oct[i][0] || sscanf(R.oct[i], "%d", &v) != 1 || v > 255) return false;
    }
    return true;
}

static void addr_text(char *out, size_t cap) {
    snprintf(out, cap, "%s.%s.%s.%s", R.oct[0], R.oct[1], R.oct[2], R.oct[3]);
}

static void addr_show(void) {
    for (int i = 0; i < 4; i++) {
        bool cur = R.focus == 0 && i == R.oct_i;
        int v = 0;
        bool bad = R.oct[i][0] && sscanf(R.oct[i], "%d", &v) == 1 && v > 255;
        lv_label_set_text(R.oct_lbl[i], R.oct[i]);
        lv_obj_set_style_border_color(R.oct_cell[i], C(bad ? COL_ERR : cur ? COL_ACCENT_HI : COL_FROST), 0);
        lv_obj_set_style_border_width(R.oct_cell[i], bad || cur ? 4 : 2, 0);
        motion_to(R.oct_cell[i], MO_BORDER_OPA, bad || cur ? LV_OPA_COVER : LV_OPA_20, 160, 0, NULL);
        motion_to(R.oct_cell[i], MO_BG_OPA, R.oct[i][0] ? 46 : 16, 160, 0, NULL);
    }
    lv_obj_set_style_opa(R.addr_next, addr_valid() ? LV_OPA_COVER : LV_OPA_50, 0);
}

static void addr_type(char c) {
    char *o = R.oct[R.oct_i];
    size_t n = strlen(o);
    R.focus = 0; /* editing: back on the pad, Next again once it is complete */
    if (c == '\b') {
        if (!n && R.oct_i > 0) o = R.oct[--R.oct_i], n = strlen(o);
        if (n) o[n - 1] = 0;
    } else if (c == '.') {
        if (n && R.oct_i < 3) R.oct_i++;
    } else if (c >= '0' && c <= '9') {
        if (n == 3 && R.oct_i < 3) o = R.oct[++R.oct_i], n = 0;
        if (n == 1 && o[0] == '0') n = 0; /* no leading zeros: 10.0.0.007 is not an address */
        if (n < 3) {
            o[n] = c;
            o[n + 1] = 0;
        }
        if (n + 1 == 3 && R.oct_i < 3) R.oct_i++;
        if (R.oct_i == 3 && strlen(R.oct[3]) == 3) R.focus = 1; /* complete: on to Next */
    }
}

/* a console of the chosen kind that discovery hears and that is not paired: its whole address
 * is filled in and Next takes the focus. Only while the last octet is still empty, so going back
 * to the step keeps what was typed. */
static void addr_found(void) {
    if (R.oct[3][0]) return;
    for (int i = 0; i < app_found_count(); i++) {
        const app_found_t *f = app_found(i);
        int a, b, c, d;
        if (f->ps5 != R.ps5 || sscanf(f->addr, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) continue;
        snprintf(R.oct[0], 4, "%d", a & 255);
        snprintf(R.oct[1], 4, "%d", b & 255);
        snprintf(R.oct[2], 4, "%d", c & 255);
        snprintf(R.oct[3], 4, "%d", d & 255);
        R.oct_i = 3;
        R.focus = 1;
        lv_label_set_text_fmt(R.addr_text,
                              "Found %s on this network, so its address is filled in. Another console? "
                              "Type its address: Settings > Network > Connection status.",
                              f->name);
        return;
    }
    lv_label_set_text(R.addr_text, R.addr_hint);
}

/* the address starts on this TV's own network: most homes are one /24 */
static void addr_prefill(void) {
    char ifname[16], ip[64];
    memset(R.oct, 0, sizeof(R.oct));
    R.oct_i = 0;
    if (!app_local_ip(ifname, sizeof(ifname), ip, sizeof(ip))) return;
    int a, b, c, d;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return;
    snprintf(R.oct[0], 4, "%d", a & 255);
    snprintf(R.oct[1], 4, "%d", b & 255);
    snprintf(R.oct[2], 4, "%d", c & 255);
    R.oct_i = 3;
}

static void build_addr(void) {
    lv_obj_t *m = col(REG_ADDR);
    char ifname[16], ip[64];
    if (app_local_ip(ifname, sizeof(ifname), ip, sizeof(ip)))
        snprintf(R.addr_hint, sizeof(R.addr_hint),
                 "On the console: Settings > Network > Connection status. This TV "
                 "is on %s, so the start is filled in.",
                 ip);
    else snprintf(R.addr_hint, sizeof(R.addr_hint), "On the console: Settings > Network > Connection status.");
    title(m, "Where is it on your network?");
    R.addr_text = text(m, R.addr_hint);
    space(m, 10);
    lv_coord_t ow = 148;
    lv_obj_t *row = ctl_obj(m);
    lv_obj_set_size(row, 4 * ow + 3 * OCT_GAP, CELL_H);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = R.oct_cell[i] = ctl_rect(row, ow, CELL_H, 22, COL_FROST, 16);
        frost(c, LV_OPA_20);
        lv_obj_set_pos(c, i * (ow + OCT_GAP), 0);
        R.oct_lbl[i] = ctl_label(c, FONT_HERO, COL_TEXT, "");
        lv_obj_center(R.oct_lbl[i]);
        if (i < 3) lv_obj_set_pos(ctl_label(row, FONT_HERO, COL_MUTED, "."), i * (ow + OCT_GAP) + ow + 12, 40);
    }
    ctl_label(m, FONT_CAPTION, COL_MUTED, "The pad types it, so do the remote's number keys");
    space(m, 8);
    R.addr_next = ctl_button(m, 300, BTN_H, LV_SYMBOL_RIGHT, "Next");
    ctl_numpad_create(&R.np_addr, R.aside[REG_ADDR], KEY, true);
    lv_obj_center(R.np_addr.box);
}

/* ----- PIN ----- */

static void pin_type(char c) {
    size_t n = strlen(R.pin_txt);
    R.pin_err = false;
    R.focus = 0;
    if (c == '\b') {
        if (n) R.pin_txt[n - 1] = 0;
    } else if (c >= '0' && c <= '9' && n < PIN_LEN) {
        R.pin_txt[n] = c;
        R.pin_txt[n + 1] = 0;
        if (n + 1 == PIN_LEN) R.focus = 1; /* complete: on to Pair */
    }
}

static void build_pin(void) {
    lv_obj_t *m = col(REG_PIN);
    title(m, "Enter the PIN it shows");
    text(m, "On the console: Settings > System > Remote Play > Link Device. The PIN stays valid for 5 minutes.");
    space(m, 10);
    ctl_digits_create(&R.pin, m, PIN_LEN, 84, CELL_H);
    R.pin_msg = wrap(m, FONT_BODY, COL_DANGER_TEXT, "", MAIN_W);
    R.pin_pair = ctl_button(m, 300, BTN_H, LV_SYMBOL_OK, "Pair");
    ctl_numpad_create(&R.np_pin, R.aside[REG_PIN], KEY, false);
    lv_obj_center(R.np_pin.box);
}

/* ----- pairing, signing in ----- */

static lv_obj_t *ring_box(lv_obj_t *aside, lv_obj_t **ring, uint32_t col) {
    lv_coord_t d = 440;
    *ring = ctl_ring(aside, d, col);
    lv_obj_center(*ring);
    lv_obj_t *inner = ctl_obj(*ring);
    lv_obj_set_size(inner, d * 64 / 100, d * 64 / 100);
    lv_obj_center(inner);
    return inner;
}

static void build_pairing(void) {
    lv_obj_t *m = col(REG_PAIRING);
    R.pair_title = title(m, "");
    text(m, "Keep the Link Device screen open on the console. This takes a few seconds.");
    R.pair_pct = ctl_label(m, FONT_HERO, COL_ACCENT_HI, "0%");
    R.pair_art_box = ring_box(R.aside[REG_PAIRING], &R.pair_ring, COL_ACCENT_HI);
}

static void build_linking(void) {
    lv_obj_t *m = col(REG_LINKING);
    title(m, "Signing in");
    R.link_status = text(m, "");
    R.link_pct = ctl_label(m, FONT_HERO, COL_PSN_HI, "0%");
    lv_obj_t *inner = ring_box(R.aside[REG_LINKING], &R.link_ring, COL_PSN_HI);
    lv_coord_t d = 440 * 64 / 100 * 80 / 100;
    lv_obj_t *em = ctl_rect(inner, d, d, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    ctl_grad(em, COL_PSN, COL_PSN_DEEP);
    lv_obj_center(em);
    lv_obj_center(ctl_ps_diamond(em, d * 56 / 100));
}

/* ----- ready ----- */

static void build_done(void) {
    lv_obj_t *m = col(REG_DONE);
    R.done_title = title(m, "");
    text(m, "It is on Home now, next to your other consoles. Remote Play can start right away.");
    space(m, 10);
    lv_obj_t *row = ctl_row(m, 20);
    R.done_btn[0] = ctl_button(row, 430, BTN_H + 8, LV_SYMBOL_PLAY, "Start Remote Play");
    R.done_btn[1] = ctl_button(row, 300, BTN_H + 8, NULL, "Back to Home");
    R.done_box = ctl_obj(R.aside[REG_DONE]);
    lv_obj_set_size(R.done_box, ASIDE_W, ASIDE_H);
}

static void done_show(void) {
    const app_host_t *h = app_host(app_selected());
    bool ps5 = h ? h->ps5 : R.ps5;
    lv_label_set_text_fmt(R.done_title, "%s is ready", h ? h->name : "Your console");
    lv_obj_clean(R.done_box);
    /* the tile, its badge over the top-right corner and its name under it: siblings, so the tile
     * keeps to its own box */
    lv_coord_t s = DONE_TILE, x = (ASIDE_W - s) / 2, y = (ASIDE_H - s) / 2 - 30;
    lv_obj_t *t = console_tile(R.done_box, ps5, s);
    lv_obj_set_pos(t, x, y);
    lv_obj_t *badge = ctl_rect(R.done_box, BADGE_D, BADGE_D, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    ctl_grad(badge, 0x4be39a, 0x1f9e5c);
    lv_obj_set_style_border_width(badge, 5, 0);
    lv_obj_set_style_border_color(badge, C(COL_SHEET), 0);
    lv_obj_center(ctl_label(badge, FONT_TITLE, COL_TEXT, LV_SYMBOL_OK));
    lv_obj_set_pos(badge, x + s - BADGE_D + 26, y - 26);
    lv_obj_t *name = ctl_label(R.done_box, FONT_TITLE, COL_TEXT, h ? h->name : "");
    lv_obj_set_width(name, ASIDE_W);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, 0, y + s + 22);
    motion_from(t, MO_GROW, -40, 0, 600, 100, motion_spring);
    motion_from(badge, MO_GROW, -38, 0, 500, 420, motion_spring);
    motion_from(badge, MO_OPA, 0, 255, 200, 420, NULL);
}

/* ----- PSN sign-in ----- */

static void build_psn(void) {
    lv_obj_t *m = col(REG_PSN);
    title(m, "Sign in with your phone");
    static const char *const steps[3] = { "Scan the code with your phone's camera", "Sign in to PlayStation Network",
                                          "The last page stays blank: copy the code= value from its address bar" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = ctl_obj(m);
        lv_obj_set_size(r, MAIN_W, LV_SIZE_CONTENT);
        lv_obj_t *n = ctl_rect(r, 48, 48, LV_RADIUS_CIRCLE, COL_FROST, 36);
        char d[2] = { (char)('1' + i), 0 };
        lv_obj_center(ctl_label(n, FONT_HEADING, COL_TEXT, d));
        lv_obj_set_pos(wrap(r, FONT_BODY, COL_SUBTLE, steps[i], MAIN_W - 72), 72, 9);
    }
    space(m, 6);
    field_row(m, "Code", 64, &R.code_ta, &R.link_btn, "Link");
    R.psn_msg = wrap(m, FONT_BODY, COL_DANGER_TEXT, "", MAIN_W);
    lv_coord_t s = 470;
    lv_obj_t *card = ctl_rect(R.aside[REG_PSN], s, s, 36, COL_FROST, LV_OPA_COVER);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_shadow_width(card, 70, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_center(ui_qr(card, app_reg_psn_url(), s - 44));
    lv_obj_t *cap = ctl_label(R.aside[REG_PSN], FONT_CAPTION, COL_SUBTLE, "Scan with your phone's camera");
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, s / 2 + 10);
}

/* ----- the consoles found ----- */

static void build_found(void) {
    lv_obj_t *m = col(REG_FOUND);
    R.found_title = title(m, "");
    R.found_text = text(m, "");
    R.found_list = ctl_obj(m);
    lv_obj_set_width(R.found_list, MAIN_W);
    lv_obj_set_height(R.found_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(R.found_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(R.found_list, 14, 0);
    space(m, 4);
    R.found_done = ctl_button(m, 300, BTN_H, LV_SYMBOL_OK, "Done");
    lv_obj_t *a = R.aside[REG_FOUND];
    lv_obj_align(art_box(a, ART_PS5, ASIDE_W / 2, ASIDE_H * 78 / 100), LV_ALIGN_BOTTOM_MID, -ASIDE_W / 6, -40);
    lv_obj_align(art_box(a, ART_PS4, ASIDE_W * 52 / 100, ASIDE_H / 2), LV_ALIGN_BOTTOM_MID, ASIDE_W / 5, -30);
}

static void found_show(void) {
    lv_obj_clean(R.found_list);
    int n = 0;
    for (int i = 0; i < app_host_count() && n < FOUND_MAX; i++) {
        const app_host_t *h = app_host(i);
        if (!h->psn) continue;
        n++;
        lv_obj_t *r = ctl_rect(R.found_list, MAIN_W, 116, 28, COL_FROST, 18);
        frost(r, LV_OPA_10);
        lv_obj_t *t = console_tile(r, h->ps5, 88);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_set_pos(ctl_label(r, FONT_TITLE, COL_TEXT, h->name), 128, 20);
        lv_obj_t *cap =
            ctl_label(r, FONT_CAPTION, h->lan ? COL_PSN_HI : COL_SUBTLE,
                      h->lan ? "Also paired with its PIN: one console, both routes" : "Plays over PSN, from anywhere");
        lv_obj_set_pos(cap, 128, 66);
    }
    if (n) lv_label_set_text_fmt(R.found_title, "%d console%s on your account", n, n == 1 ? "" : "s");
    else lv_label_set_text(R.found_title, "No consoles on your account");
    lv_label_set_text(R.found_text,
                      n ? "They are on Home now." : "Turn on Remote Play on the console, then try again.");
}

/* ---------- the keyboard ---------- */

static void kb_close(int focus);

static void kb_ready_cb(lv_event_t *e) {
    (void)e;
    kb_close(-1); /* OK: on to the field's button */
}

static void kb_open(lv_obj_t *ta) {
    if (R.kb_open) return;
    R.kb_open = true;
    R.kb_ta = ta;
    lv_obj_clear_flag(R.kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(R.kb);
    ctl_keyboard_pad(R.kb, input_pad_state() == PAD_ONLINE);
    ctl_keyboard_focus(R.kb, ta);
    ctl_field_focus(ta, true, true);
    motion_from(R.kb, MO_TY, KB_H + 60, 0, 360, 0, NULL);
    motion_to(R.hints, MO_OPA, LV_OPA_TRANSP, 220, 0, NULL);
    /* the field stays above the keyboard: the flow rises by what it would cover */
    lv_obj_update_layout(ta);
    lv_area_t f;
    lv_obj_get_coords(ta, &f);
    lv_coord_t over = f.y2 + 28 - (lv_obj_get_style_translate_y(R.flow, 0)) - (1080 - KB_H);
    motion_to(R.flow, MO_TY, over > 0 ? -over : 0, 360, 0, NULL);
    if (over > HEAD_ROOM) motion_to(R.head, MO_OPA, LV_OPA_TRANSP, 220, 0, NULL); /* the title rises into it */
}

/* focus -1: the field's button (the text is in), else that focus */
static void kb_close(int focus) {
    if (!R.kb_open) return;
    R.kb_open = false;
    ctl_keyboard_focus(R.kb, NULL);
    ctl_field_focus(R.kb_ta, false, false);
    motion_to(R.kb, MO_TY, KB_H + 60, 260, 0, lv_anim_path_ease_in);
    motion_to(R.flow, MO_TY, 0, 300, 0, NULL);
    motion_to(R.hints, MO_OPA, LV_OPA_COVER, 220, 0, NULL);
    motion_to(R.head, MO_OPA, LV_OPA_COVER, 220, 0, NULL);
    R.focus = focus >= 0 ? focus : R.step == REG_ACCOUNT ? 3 : 1;
    R.kb_ta = NULL;
    paint();
}

static bool printable(uint32_t k) { return k >= ' ' && k != LV_KEY_DEL && k < 0x110000; }

static void kb_key(uint32_t k) {
    if (k == LV_KEY_ESC) kb_close(R.step == REG_ACCOUNT ? 2 : 0);
    else if (k == LV_KEY_BACKSPACE) lv_textarea_del_char(R.kb_ta);
    else if (printable(k)) lv_textarea_add_char(R.kb_ta, k); /* typed on the remote or a USB keyboard */
    else if (k == LV_KEY_UP || k == LV_KEY_DOWN || k == LV_KEY_LEFT || k == LV_KEY_RIGHT || k == LV_KEY_ENTER)
        ctl_keyboard_key(R.kb, k);
    if (R.root) paint();
}

void ui_register_shift(void) {
    if (R.kb_open) ctl_keyboard_shift(R.kb);
}

/* ---------- moving between steps ---------- */

static void hide_cb(lv_obj_t *o) {
    if (o == R.main[R.step] || o == R.aside[R.step]) return; /* came back to it meanwhile */
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_x(o, 0, 0);
}

static void slide_out(lv_obj_t *o, int dir) {
    motion_to(o, MO_TX, -dir * STEP_DX, 220, 0, lv_anim_path_ease_in);
    motion_then(o, MO_OPA, 0, 200, lv_anim_path_ease_in, hide_cb);
}

static void slide_in(lv_obj_t *o, int dir, uint32_t delay) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    motion_from(o, MO_TX, dir * STEP_DX, 0, 380, delay, NULL);
    motion_from(o, MO_OPA, 0, 255, 300, delay, NULL);
}

/* pairing and signing in start after the slide: app_reg_* may answer at once */
static void start_async(void *unused) {
    (void)unused;
    if (!R.root) return;
    app_reg_cancel(); /* no result left over from an earlier attempt */
    R.started = true;
    if (R.step == REG_PAIRING) {
        char addr[24];
        addr_text(addr, sizeof(addr));
        app_reg_submit_pin(addr, R.pin_txt, R.ps5, NULL);
    } else if (R.step == REG_LINKING) {
        app_reg_submit_psn_code(lv_textarea_get_text(R.code_ta), true);
    }
    ui_register_refresh();
}

static void enter(reg_step_t s) {
    R.started = false;
    switch (s) {
        case REG_METHOD: R.focus = R.method; break;
        case REG_KIND: R.focus = R.ps5 ? 0 : 1; break;
        case REG_ACCOUNT:
            R.focus = 0;
            R.typing_id = false;
            R.account_then_pin = false;
            lv_obj_add_flag(R.acc_row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(R.acc_opt[1], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(R.acc_msg, "");
            break;
        case REG_ADDR:
            R.focus = 0;
            ctl_numpad_select(&R.np_addr, 10); /* on 0: the address goes on with digits */
            addr_found();
            break;
        case REG_PIN:
            R.focus = 0;
            if (!R.pin_err) R.pin_txt[0] = 0;
            ctl_numpad_select(&R.np_pin, 0);
            lv_label_set_text(R.pin_msg, R.pin_err ? R.err : "");
            break;
        case REG_PAIRING: {
            lv_label_set_text_fmt(R.pair_title, "Pairing with your %s", R.ps5 ? "PS5" : "PS4");
            lv_label_set_text(R.pair_pct, "0%");
            lv_arc_set_value(R.pair_ring, 0);
            lv_obj_clean(R.pair_art_box);
            lv_obj_center(art_box(R.pair_art_box, art_console(R.ps5), lv_obj_get_style_width(R.pair_art_box, 0),
                                  lv_obj_get_style_height(R.pair_art_box, 0)));
            lv_async_call(start_async, NULL);
            break;
        }
        case REG_DONE:
            R.focus = 0;
            done_show();
            break;
        case REG_PSN:
            R.focus = 0;
            lv_label_set_text(R.psn_msg, R.err[0] && R.method == BY_PSN ? R.err : "");
            break;
        case REG_LINKING:
            lv_label_set_text(R.link_status, "Getting your PSN account and the consoles on it.");
            lv_label_set_text(R.link_pct, "0%");
            lv_arc_set_value(R.link_ring, 0);
            lv_async_call(start_async, NULL);
            break;
        case REG_FOUND:
            R.focus = 0;
            found_show();
            break;
        default: break;
    }
}

static void go(reg_step_t s, int dir) {
    reg_step_t old = R.step;
    if (R.kb_open) kb_close(0);
    if (old != s) {
        slide_out(R.main[old], dir);
        slide_out(R.aside[old], dir);
    }
    R.step = s;
    enter(s);
    slide_in(R.main[s], dir, 80);
    slide_in(R.aside[s], dir, 140);
    paint();
    follow();
    log_msg("add a console: step %d", (int)s);
}

static void paint(void) {
    switch (R.step) {
        case REG_METHOD:
            for (int i = 0; i < 2; i++) {
                option_focus(R.opt[i], i == R.focus);
                motion_to(R.illus[i], MO_OPA, i == R.focus ? LV_OPA_COVER : LV_OPA_TRANSP, 300, 0, NULL);
            }
            break;
        case REG_KIND:
            for (int i = 0; i < 2; i++) {
                bool on = i == R.focus;
                motion_to(R.kind_tile[i], MO_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
                motion_to(R.kind_tile[i], MO_GROW, on ? 10 : 0, 320, 0, motion_spring);
                motion_to(R.kind_art[i], MO_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 300, 0, NULL);
            }
            break;
        case REG_ACCOUNT:
            for (int i = 0; i < 2; i++) option_focus(R.acc_opt[i], i == R.focus);
            ctl_field_focus(R.acc_ta, R.focus == 2, R.kb_open);
            ctl_button_focus(R.acc_next, R.focus == 3);
            lv_obj_set_style_opa(R.acc_next, lv_textarea_get_text(R.acc_ta)[0] ? LV_OPA_COVER : LV_OPA_50, 0);
            break;
        case REG_ADDR:
            ctl_numpad_focus(&R.np_addr, R.focus == 0);
            ctl_button_focus(R.addr_next, R.focus == 1);
            addr_show();
            break;
        case REG_PIN:
            ctl_numpad_focus(&R.np_pin, R.focus == 0);
            ctl_button_focus(R.pin_pair, R.focus == 1);
            ctl_digits_show(&R.pin, R.pin_txt, R.focus == 0, R.pin_err);
            lv_obj_set_style_opa(R.pin_pair, strlen(R.pin_txt) == PIN_LEN ? LV_OPA_COVER : LV_OPA_50, 0);
            break;
        case REG_DONE:
            for (int i = 0; i < 2; i++) ctl_button_focus(R.done_btn[i], i == R.focus);
            break;
        case REG_PSN:
            ctl_field_focus(R.code_ta, R.focus == 0, R.kb_open);
            ctl_button_focus(R.link_btn, R.focus == 1);
            lv_obj_set_style_opa(R.link_btn, lv_textarea_get_text(R.code_ta)[0] ? LV_OPA_COVER : LV_OPA_50, 0);
            break;
        case REG_FOUND: ctl_button_focus(R.found_done, true); break;
        default: break;
    }
}

/* ---------- keys ---------- */

/* a step with a number pad and one button: the pad has the focus, LEFT off its edge reaches the
 * button and RIGHT comes back; the remote's digits type straight in */
static void pad_key(uint32_t k, ctl_numpad_t *np, lv_obj_t *btn, bool ready, void (*type)(char), reg_step_t next,
                    bool dot) {
    if ((k >= '0' && k <= '9') || (dot && k == '.')) {
        type((char)k);
    } else if (k == LV_KEY_BACKSPACE) {
        type('\b');
    } else if (R.focus == 0) {
        if (k == LV_KEY_ENTER) type(ctl_numpad_press(np));
        else if (!ctl_numpad_move(np, k) && k == LV_KEY_LEFT) R.focus = 1;
    } else if (k == LV_KEY_RIGHT) {
        R.focus = 0;
    } else if (k == LV_KEY_ENTER) {
        if (ready) {
            go(next, 1);
            return;
        }
        ctl_shake(btn);
    }
    paint();
}

/* a field with its button: Cross on the field brings the keyboard up; typed characters go in */
static void field_key(uint32_t k, lv_obj_t *ta, int field_focus) {
    if (k == LV_KEY_ENTER) kb_open(ta);
    else if (k == LV_KEY_BACKSPACE) lv_textarea_del_char(ta);
    else if (printable(k)) lv_textarea_add_char(ta, k);
    else if (k == LV_KEY_RIGHT) R.focus = field_focus + 1;
    paint();
}

void ui_register_key(uint32_t k) {
    if (!R.root) return;
    if (R.kb_open) {
        kb_key(k);
        return;
    }
    if (k == LV_KEY_ESC) {
        ui_register_back();
        return;
    }
    switch (R.step) {
        case REG_METHOD:
            if (k == LV_KEY_UP || k == LV_KEY_DOWN) R.focus = k == LV_KEY_DOWN;
            else if (k == LV_KEY_ENTER) {
                R.method = R.focus;
                R.err[0] = 0;
                go(R.method == BY_PIN ? REG_KIND : REG_PSN, 1);
                return;
            }
            follow(); /* the backdrop follows the focused way in */
            break;
        case REG_KIND:
            if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) R.focus = k == LV_KEY_RIGHT;
            else if (k == LV_KEY_ENTER) {
                R.ps5 = R.focus == 0;
                go(R.need_account ? REG_ACCOUNT : REG_ADDR, 1);
                return;
            }
            break;
        case REG_ACCOUNT:
            if (R.focus >= 2) {
                if (R.focus == 2 && k != LV_KEY_UP) {
                    field_key(k, R.acc_ta, 2);
                    return;
                }
                if (k == LV_KEY_UP) R.focus = 0;
                else if (k == LV_KEY_LEFT) R.focus = 2;
                else if (k == LV_KEY_ENTER) {
                    if (app_psn_set_account_id(lv_textarea_get_text(R.acc_ta))) {
                        go(REG_ADDR, 1);
                        return;
                    }
                    lv_label_set_text(R.acc_msg, "That is not an account ID: it is 12 characters of base64, like "
                                                 "AbCdEfGhIjk=");
                    ctl_shake(R.acc_row);
                }
            } else if (k == LV_KEY_UP || k == LV_KEY_DOWN) {
                R.focus = k == LV_KEY_UP ? 0 : R.typing_id ? 2 : 1;
            } else if (k == LV_KEY_ENTER && R.focus == 0) {
                R.account_then_pin = true; /* signed in, the flow goes on to the address */
                R.err[0] = 0;
                go(REG_PSN, 1);
                return;
            } else if (k == LV_KEY_ENTER) {
                R.typing_id = true; /* the field takes the option's place */
                lv_obj_add_flag(R.acc_opt[1], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(R.acc_row, LV_OBJ_FLAG_HIDDEN);
                motion_from(R.acc_row, MO_OPA, 0, 255, 260, 0, NULL);
                R.focus = 2;
            }
            break;
        case REG_ADDR: pad_key(k, &R.np_addr, R.addr_next, addr_valid(), addr_type, REG_PIN, true); return;
        case REG_PIN:
            pad_key(k, &R.np_pin, R.pin_pair, strlen(R.pin_txt) == PIN_LEN, pin_type, REG_PAIRING, false);
            return;
        case REG_DONE:
            if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) R.focus = k == LV_KEY_RIGHT;
            else if (k == LV_KEY_ENTER) {
                bool play = R.focus == 0;
                ui_register_close();
                if (play) app_connect(); /* the new console is the selected one */
                return;
            }
            break;
        case REG_PSN:
            if (R.focus == 0) {
                field_key(k, R.code_ta, 0);
                return;
            }
            if (k == LV_KEY_LEFT) R.focus = 0;
            else if (k == LV_KEY_ENTER) {
                if (lv_textarea_get_text(R.code_ta)[0]) {
                    go(REG_LINKING, 1);
                    return;
                }
                ctl_shake(R.link_btn);
                R.focus = 0;
            }
            break;
        case REG_FOUND:
            if (k == LV_KEY_ENTER) {
                ui_register_close();
                return;
            }
            break;
        default: break;
    }
    paint();
}

void ui_register_back(void) {
    if (!R.root) return;
    if (R.kb_open) {
        kb_close(R.step == REG_ACCOUNT ? 2 : 0);
        return;
    }
    switch (R.step) {
        case REG_METHOD:
        case REG_DONE:
        case REG_FOUND: ui_register_close(); break;
        case REG_KIND: go(REG_METHOD, -1); break;
        case REG_ACCOUNT:
            if (R.typing_id) {
                R.typing_id = false;
                lv_obj_add_flag(R.acc_row, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(R.acc_opt[1], LV_OBJ_FLAG_HIDDEN);
                R.focus = 1;
                paint();
            } else {
                go(REG_KIND, -1);
            }
            break;
        case REG_ADDR: go(R.need_account ? REG_ACCOUNT : REG_KIND, -1); break;
        case REG_PIN:
            R.pin_err = false;
            go(REG_ADDR, -1);
            break;
        case REG_PAIRING:
            app_reg_cancel();
            go(REG_PIN, -1);
            break;
        case REG_PSN: go(R.account_then_pin ? REG_ACCOUNT : REG_METHOD, -1); break;
        case REG_LINKING:
            app_reg_cancel();
            go(REG_PSN, -1);
            break;
        default: break;
    }
}

/* ---------- following the app ---------- */

void ui_register_refresh(void) {
    if (!R.root || !R.started) return;
    app_reg_state_t s = app_reg_state();
    if (R.step == REG_PAIRING) {
        if (s == APP_REG_RUNNING) {
            ctl_ring_set(R.pair_ring, app_reg_progress());
            lv_label_set_text_fmt(R.pair_pct, "%d%%", app_reg_progress());
        } else if (s == APP_REG_SUCCESS) {
            R.started = false;
            go(REG_DONE, 1);
        } else if (s == APP_REG_FAILED) {
            R.started = false;
            snprintf(R.err, sizeof(R.err), "%s", app_reg_error());
            R.pin_err = true;
            go(REG_PIN, -1);
            ctl_shake(R.pin.box);
        }
    } else if (R.step == REG_LINKING) {
        if (s == APP_REG_RUNNING) {
            ctl_ring_set(R.link_ring, app_reg_progress());
            lv_label_set_text_fmt(R.link_pct, "%d%%", app_reg_progress());
            if (app_reg_status()[0]) lv_label_set_text(R.link_status, app_reg_status());
        } else if (s == APP_REG_SUCCESS) {
            R.started = false;
            if (R.account_then_pin && config_psn_has_account_id()) go(REG_ADDR, 1);
            else go(REG_FOUND, 1);
        } else if (s == APP_REG_FAILED) {
            R.started = false;
            snprintf(R.err, sizeof(R.err), "%s", app_reg_error());
            go(REG_PSN, -1);
            lv_label_set_text(R.psn_msg, R.err);
            ctl_shake(R.code_ta);
        }
    }
}

/* ---------- open, close ---------- */

/* Home draws under the sheet only while it fades in */
static void opened_cb(lv_obj_t *root) {
    if (root == R.root) ui_cover(true);
}

static void closed_cb(lv_obj_t *root) { lv_obj_del_async(root); }

void ui_register_open(void) {
    if (R.root) return;
    memset(&R, 0, sizeof(R));
    app_reg_cancel();                                     /* start clean: no result left from last time */
    R.ps5 = app_found_count() ? app_found(0)->ps5 : true; /* the kind of a console found on the network */
    R.need_account = !config_psn_has_account_id();        /* PIN pairing asks for it: one more step */
    addr_prefill();
    R.root = ctl_obj(lv_layer_top());
    lv_obj_set_size(R.root, LV_PCT(100), LV_PCT(100));
    R.bg = ctl_rect(R.root, LV_PCT(100), LV_PCT(100), 0, COL_TINT_LAN, LV_OPA_COVER);
    ctl_grad(R.bg, COL_TINT_LAN, COL_BG_BOTTOM);
    lv_obj_set_style_bg_main_stop(R.bg, 0, 0);
    lv_obj_set_style_bg_grad_stop(R.bg, 235, 0);
    R.glow = ctl_glow(R.root, COL_TINT_LAN + 0x202020, LV_OPA_60); /* the one light behind every step's art */
    lv_obj_set_pos(R.glow, SAFE_X + ASIDE_X + ASIDE_W / 2 - CTL_GLOW_D / 2, 560 - CTL_GLOW_D / 2);

    R.head = ctl_obj(R.root);
    lv_obj_set_size(R.head, LV_PCT(100), TRACK_Y + 6);
    lv_obj_t *over = ctl_label(R.head, FONT_SMALL, COL_ACCENT_HI, "ADD A CONSOLE");
    lv_obj_set_style_text_letter_space(over, 3, 0);
    lv_obj_set_pos(over, SAFE_X, 66);
    R.step_lbl = ctl_label(R.head, FONT_CAPTION, COL_SUBTLE, "");
    lv_obj_set_width(R.step_lbl, 600);
    lv_obj_set_style_text_align(R.step_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(R.step_lbl, 1920 - SAFE_X - 600, 62);
    lv_obj_t *track = ctl_rect(R.head, TRACK_W, 6, LV_RADIUS_CIRCLE, COL_FROST, 30);
    lv_obj_set_pos(track, SAFE_X, TRACK_Y);
    R.fill = ctl_rect(track, 0, 6, LV_RADIUS_CIRCLE, COL_ACCENT_HI, LV_OPA_COVER);
    R.hints = ctl_hints(R.root);
    lv_obj_align(R.hints, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -SAFE_Y + 14);
    ctl_hint(R.hints, PS_CROSS, "Select");
    ctl_hint(R.hints, PS_CIRCLE, "Back");

    R.flow = ctl_obj(R.root);
    lv_obj_set_pos(R.flow, SAFE_X, FLOW_Y);
    lv_obj_set_size(R.flow, TRACK_W, ASIDE_H);
    ctl_margin(R.flow, 60);
    build_method();
    build_kind();
    build_account();
    build_addr();
    build_pin();
    build_pairing();
    build_done();
    build_psn();
    build_linking();
    build_found();
    R.kb = ctl_keyboard(R.root, KB_W, KB_H);
    lv_obj_align(R.kb, LV_ALIGN_BOTTOM_MID, 0, 40);
    lv_obj_add_event_cb(R.kb, kb_ready_cb, LV_EVENT_READY, NULL);

    R.step = REG_METHOD;
    enter(REG_METHOD);
    lv_obj_clear_flag(R.main[REG_METHOD], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(R.aside[REG_METHOD], LV_OBJ_FLAG_HIDDEN);
    paint();
    follow();
    lv_obj_set_style_opa(R.root, LV_OPA_TRANSP, 0);
    motion_then(R.root, MO_OPA, LV_OPA_COVER, FADE_MS, NULL, opened_cb);
    motion_from(R.flow, MO_TY, 30, 0, 420, 0, NULL);
    log_event(LOG_INFO, "REG", "Add a console: PIN or PSN account");
}

void ui_register_close(void) {
    if (!R.root) return;
    app_reg_cancel();
    lv_obj_t *root = R.root;
    memset(&R, 0, sizeof(R));
    ui_cover(false); /* Home is back at once, under the fading sheet; keys go to it already */
    motion_then(root, MO_OPA, LV_OPA_TRANSP, FADE_MS, NULL, closed_cb);
    log_msg("add a console: closed");
}

bool ui_register_active(void) { return R.root != NULL; }
reg_step_t ui_register_step(void) { return R.step; }
int ui_register_focus(void) { return R.focus; }
bool ui_register_typing(void) { return R.kb_open; }
lv_obj_t *ui_register_root(void) { return R.root; }

const char *ui_register_text(void) {
    static char buf[80];
    if (R.kb_open) return lv_textarea_get_text(R.kb_ta);
    switch (R.step) {
        case REG_ADDR: addr_text(buf, sizeof(buf)); return buf;
        case REG_PIN: return R.pin_txt;
        case REG_PSN: return lv_textarea_get_text(R.code_ta);
        case REG_ACCOUNT: return lv_textarea_get_text(R.acc_ta);
        default: return "";
    }
}
