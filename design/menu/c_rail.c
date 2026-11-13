/* design/menu/c_rail.c - concept 03 "Rail": a TV launcher with a side rail (Apple TV, Steam
 * Big Picture).
 *
 * An icon rail on the left that widens with labels when it has the focus and folds away when
 * the focus moves into the content; the content follows the rail item live. Consoles are a grid
 * of big cards that lift on focus, and Cross slides a detail sheet in from the right with large
 * actions. Settings are rows with a single focus frame gliding between them; UP/DOWN pick a
 * row, LEFT/RIGHT change it, Circle goes back to the rail. In the stream the same rail idea
 * becomes a side sheet over the left third of the game.
 */
#include <stdio.h>
#include <string.h>

#include "concept.h"

#define RAIL_C   128
#define RAIL_E   420
#define ITEM_H   88
#define ITEM_GAP 14
#define ITEM_Y   220
#define CX0      (RAIL_C + 64)
#define CW       (SCR_W - SAFE_X - CX0)
#define GRID_GAP 32
#define CARD_W   ((CW - 2 * GRID_GAP) / 3)
#define CARD_H   360
#define GRID_Y   200
#define RROW_W   1040
#define RROW_H   100
#define RROW_GAP 12
#define SHEET_W  760
#define SIDE_W   620

enum { P_CONSOLES, P_STREAM, P_PAD, P_ACCOUNT, P_LOG, P_ADD, P__N };
enum { Z_RAIL, Z_CONTENT, Z_DETAIL };
enum { RK_SEG, RK_METER, RK_TOGGLE, RK_ACTION };

static const char *const g_names[P__N] = { "Consoles", "Stream", "Controller", "Account", "Log", "Add console" };

typedef struct {
    lv_obj_t *row, *title;
    int kind;
    kit_seg_t seg;
    kit_meter_t meter;
    kit_toggle_t tgl;
} rrow_t;
typedef struct {
    lv_obj_t *card, *art;
    bool ps5, has_art;
} rcard_t;

static struct {
    lv_obj_t *root, *home, *rail, *pill, *items[P__N], *labels[P__N], *brand, *content, *veil, *pages[P__N];
    int page, zone, card, row[P__N], add, detail_btn;
    bool expanded;
    rcard_t cards[MOCK_HOSTS + 1];
    rrow_t rows[P__N][6];
    int nrows[P__N];
    lv_obj_t *frame[P__N]; /* the gliding focus frame of each row page */
    lv_obj_t *sum_big, *sum_txt;
    lv_obj_t *add_cards[2];
    lv_obj_t *sheet, *sheet_art[2], *sheet_name, *sheet_state, *sheet_btns[4], *sheet_frame;
} g;

static struct {
    lv_obj_t *root, *sheet, *time, *stat[4], *rows[5], *vals[5], *frame, *note;
    kit_toggle_t rumble;
    int focus;
    bool open;
    lv_timer_t *tick;
} ov;

/* ---------- rail ---------- */

static lv_coord_t item_y(int i) { return i == P_ADD ? SCR_H - SAFE_Y - ITEM_H : ITEM_Y + i * (ITEM_H + ITEM_GAP); }

static void rail_paint(uint32_t ms) {
    bool focus = g.zone == Z_RAIL;
    if (focus != g.expanded) {
        g.expanded = focus;
        kit_anim(g.rail, KA_W, focus ? RAIL_E : RAIL_C, ms, 0, lv_anim_path_ease_out);
        kit_anim(g.pill, KA_W, (focus ? RAIL_E : RAIL_C) - 32, ms, 0, lv_anim_path_ease_out);
        for (int i = 0; i < P__N; i++)
            kit_anim(g.labels[i], KA_TEXT_OPA, focus ? LV_OPA_COVER : LV_OPA_TRANSP, focus ? ms : ms / 2,
                     focus ? ms / 3 : 0, NULL);
        kit_anim(g.brand, KA_TEXT_OPA, focus ? LV_OPA_COVER : LV_OPA_TRANSP, ms, focus ? ms / 3 : 0, NULL);
        kit_anim(g.veil, KA_BG_OPA, focus ? 120 : LV_OPA_TRANSP, ms, 0, NULL);
    }
    kit_anim(g.pill, KA_Y, item_y(g.page) + 4, 260, 0, kit_path_spring);
    kit_anim(g.pill, KA_BG_OPA, focus ? LV_OPA_COVER : 70, 200, 0, NULL);
    kit_anim(g.pill, KA_OUTLINE_OPA, focus ? LV_OPA_COVER : LV_OPA_TRANSP, 200, 0, NULL);
}

static void rail_item(int i, const char *sym) {
    lv_obj_t *it = g.items[i] = kit_obj(g.rail);
    lv_obj_set_size(it, RAIL_E, ITEM_H);
    lv_obj_set_pos(it, 0, item_y(i));
    lv_obj_t *ic;
    if (i == P_PAD) ic = kit_ps_diamond(it, 40, false);
    else if (i == P_ACCOUNT) {
        ic = kit_rect(it, 40, 40, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
        kit_grad(ic, 0x2bd2c4, 0x0b6f9c);
        lv_obj_center(kit_label(ic, FONT_SMALL, 0xffffff, "P1"));
    } else ic = kit_label(it, FONT_TITLE, COL_TEXT, sym);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, (RAIL_C - (i == P_PAD || i == P_ACCOUNT ? 40 : 32)) / 2, 0);
    g.labels[i] = kit_label(it, FONT_BODY, COL_TEXT, g_names[i]);
    lv_obj_align(g.labels[i], LV_ALIGN_LEFT_MID, RAIL_C - 8, 0);
    lv_obj_set_style_text_opa(g.labels[i], LV_OPA_TRANSP, 0);
}

static void build_rail(void) {
    g.rail = kit_rect(g.home, RAIL_C, SCR_H, 0, 0x08101f, LV_OPA_COVER);
    lv_obj_set_style_border_width(g.rail, 2, 0);
    lv_obj_set_style_border_side(g.rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(g.rail, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.rail, LV_OPA_10, 0);
    lv_obj_t *logo = kit_app_mark(g.rail, 64);
    lv_obj_set_pos(logo, (RAIL_C - 64) / 2, SAFE_Y);
    g.brand = kit_label(g.rail, FONT_HEADING, COL_TEXT, "Hotaru");
    lv_obj_set_pos(g.brand, RAIL_C - 8, SAFE_Y + 16);
    lv_obj_set_style_text_opa(g.brand, LV_OPA_TRANSP, 0);
    g.pill = kit_rect(g.rail, RAIL_C - 32, ITEM_H - 8, 24, COL_ACCENT, LV_OPA_COVER);
    kit_grad(g.pill, COL_ACCENT_HI, COL_ACCENT);
    lv_obj_set_pos(g.pill, 16, item_y(0) + 4);
    lv_obj_set_style_outline_width(g.pill, 3, 0);
    lv_obj_set_style_outline_pad(g.pill, 4, 0);
    lv_obj_set_style_outline_color(g.pill, lv_color_white(), 0);
    static const char *const syms[P__N] = { LV_SYMBOL_HOME, LV_SYMBOL_VIDEO, "", "", LV_SYMBOL_LIST, LV_SYMBOL_PLUS };
    for (int i = 0; i < P__N; i++) rail_item(i, syms[i]);
}

/* ---------- content pages ---------- */

static lv_obj_t *page_header(int p, const char *caption) {
    lv_obj_t *pg = g.pages[p] = kit_obj(g.content);
    lv_obj_set_size(pg, SCR_W - CX0, SCR_H);
    lv_obj_add_flag(pg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *t = kit_label(pg, FONT_HERO, COL_TEXT, g_names[p]);
    lv_obj_set_pos(t, -2, SAFE_Y + 8);
    lv_obj_t *c = kit_label(pg, FONT_BODY, COL_MUTED, caption);
    lv_obj_set_pos(c, 0, SAFE_Y + 76);
    return pg;
}

static lv_coord_t card_x(int i) { return (i % 3) * (CARD_W + GRID_GAP); }
static lv_coord_t card_y(int i) { return GRID_Y + (i / 3) * (CARD_H + GRID_GAP); }

static void build_consoles(void) {
    lv_obj_t *pg = page_header(P_CONSOLES, "4 consoles" BULLET "PSN linked as player_one");
    for (int i = 0; i <= MOCK_HOSTS; i++) {
        rcard_t *c = &g.cards[i];
        const mock_host_t *h = i < MOCK_HOSTS ? &mock_hosts[i] : NULL;
        c->card = kit_rect(pg, CARD_W, CARD_H, 32, 0x18233c, h ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_obj_set_pos(c->card, card_x(i), card_y(i));
        kit_grad(c->card, 0x1a2642, 0x0f1729);
        lv_obj_set_style_border_width(c->card, h ? 2 : 3, 0);
        lv_obj_set_style_border_color(c->card, lv_color_white(), 0);
        lv_obj_set_style_border_opa(c->card, h ? LV_OPA_10 : LV_OPA_30, 0);
        lv_obj_set_style_outline_width(c->card, 5, 0);
        lv_obj_set_style_outline_pad(c->card, 6, 0);
        lv_obj_set_style_outline_color(c->card, lv_color_white(), 0);
        lv_obj_set_style_outline_opa(c->card, LV_OPA_TRANSP, 0);
        if (!h) {
            lv_obj_t *ring = kit_rect(c->card, 128, 128, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
            lv_obj_set_style_border_width(ring, 3, 0);
            lv_obj_set_style_border_color(ring, lv_color_white(), 0);
            lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);
            lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 60);
            lv_obj_center(kit_label(ring, FONT_HERO, COL_TEXT, LV_SYMBOL_PLUS));
            lv_obj_t *t = kit_label(c->card, FONT_TITLE, COL_TEXT, "Add a console");
            lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 222);
            lv_obj_t *s = kit_label(c->card, FONT_CAPTION, COL_MUTED, "Pair with a PIN or your PSN account");
            lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 270);
            continue;
        }
        lv_obj_t *media = kit_rect(c->card, CARD_W - 24, 212, 22, h->tint, LV_OPA_COVER);
        lv_obj_set_pos(media, 12, 12);
        kit_grad(media, h->state == HS_OFFLINE ? 0x343c4e : h->tint + 0x181818,
                 h->state == HS_OFFLINE ? 0x1a1f2a : 0x0d1628);
        c->ps5 = h->ps5;
        c->has_art = true;
        c->art = kit_console_art(media, h->ps5);
        lv_obj_center(c->art);
        lv_img_set_zoom(c->art, kit_art_zoom(h->ps5, 320, 176));
        if (h->state == HS_OFFLINE) lv_obj_set_style_img_opa(c->art, LV_OPA_50, 0);
        lv_obj_align(kit_dot(media, 16, mock_state_col(h->state)), LV_ALIGN_TOP_RIGHT, -18, 18);
        lv_obj_t *name = kit_label(c->card, FONT_TITLE, COL_TEXT, h->name);
        lv_obj_set_pos(name, 28, 244);
        lv_obj_t *st = kit_label(c->card, FONT_CAPTION, 0xb4bfd6, "");
        lv_label_set_text_fmt(st, "%s" BULLET "%s", mock_state_text(h->state), h->addr);
        lv_obj_set_pos(st, 28, 292);
        lv_obj_t *badge = kit_rect(c->card, LV_SIZE_CONTENT, 34, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
        lv_obj_set_style_pad_hor(badge, 14, 0);
        lv_obj_center(kit_label(badge, FONT_SMALL, COL_TEXT, h->ps5 ? "PS5" : "PS4"));
        lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, -24, -30);
    }
}

static rrow_t *add_row(int p, const char *title, const char *caption) {
    rrow_t *r = &g.rows[p][g.nrows[p]];
    int i = g.nrows[p]++;
    r->row = kit_rect(g.pages[p], RROW_W, RROW_H, 26, 0xffffff, 12);
    lv_obj_set_pos(r->row, 0, GRID_Y + i * (RROW_H + RROW_GAP));
    r->title = kit_label(r->row, FONT_HEADING, COL_TEXT, title);
    lv_obj_set_pos(r->title, 32, caption ? 16 : 32);
    if (caption) {
        lv_obj_t *c = kit_label(r->row, FONT_CAPTION, COL_MUTED, caption);
        lv_obj_set_pos(c, 32, 56);
    }
    return r;
}

static void row_seg(rrow_t *r, const char *const *opts, int n, int sel, lv_coord_t w) {
    r->kind = RK_SEG;
    kit_seg_create(&r->seg, r->row, opts, n, sel, w);
    lv_obj_align(r->seg.box, LV_ALIGN_RIGHT_MID, -16, 0);
}

static void row_toggle(rrow_t *r, bool v) {
    r->kind = RK_TOGGLE;
    kit_toggle_create(&r->tgl, r->row, v);
    lv_obj_align(r->tgl.track, LV_ALIGN_RIGHT_MID, -24, 0);
}

static void row_action(rrow_t *r, const char *sym, uint32_t col) {
    r->kind = RK_ACTION;
    lv_obj_t *ic = kit_label(r->row, FONT_TITLE, col, sym);
    lv_obj_align(ic, LV_ALIGN_RIGHT_MID, -32, 0);
    if (col == COL_ERR) lv_obj_set_style_text_color(r->title, C(COL_DANGER_TEXT), 0);
}

static lv_obj_t *side_card(int p, const char *over) {
    lv_obj_t *s = kit_rect(g.pages[p], CW - RROW_W - 32, 6 * RROW_H + 5 * RROW_GAP, 30, 0x000000, LV_OPA_20);
    lv_obj_set_pos(s, RROW_W + 32, GRID_Y);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(s, 36, 0);
    lv_obj_t *o = kit_label(s, FONT_SMALL, COL_ACCENT_HI, over);
    lv_obj_set_style_text_letter_space(o, 3, 0);
    return s;
}

static void summary_refresh(void) {
    lv_label_set_text_fmt(g.sum_big, "%sp%d", mock_set.res ? "1080" : "720", mock_set.fps ? 60 : 30);
    lv_label_set_text(g.sum_txt, mock_summary_text());
}

static void build_settings(void) {
    page_header(P_STREAM, "Applied the next time you connect");
    row_seg(add_row(P_STREAM, "Resolution", "Higher needs more bandwidth"), mock_res_opts, 2, mock_set.res, 150);
    row_seg(add_row(P_STREAM, "Frame rate", "60 fps feels smoother in games"), mock_fps_opts, 2, mock_set.fps, 150);
    row_seg(add_row(P_STREAM, "Codec", "Decoded in hardware by the TV"), mock_codec_opts, 3, mock_set.codec, 140);
    rrow_t *r = add_row(P_STREAM, "Bitrate", "Lower it if the picture stutters");
    r->kind = RK_METER;
    kit_meter_create(&r->meter, r->row, 260, MOCK_BITRATE_MIN, MOCK_BITRATE_MAX, mock_set.bitrate);
    lv_obj_align(r->meter.box, LV_ALIGN_RIGHT_MID, -36, 0);
    row_seg(add_row(P_STREAM, "Smooth playback", "Hides Wi-Fi stalls with a little delay"), mock_smooth_opts, 4,
            mock_set.smooth, 124);
    row_toggle(add_row(P_STREAM, "Stream stats", "fps and bitrate in a corner while you play"), false);
    lv_obj_t *s = side_card(P_STREAM, "WHAT THE CONSOLE SENDS");
    g.sum_big = kit_label(s, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.sum_big, 0, 40);
    g.sum_txt = kit_label(s, FONT_BODY, COL_MUTED, "");
    lv_obj_set_pos(g.sum_txt, 0, 110);
    lv_obj_t *n =
        kit_label(s, FONT_CAPTION, COL_MUTED, "Changing these during a stream\nrestarts it when the menu closes.");
    lv_obj_set_pos(n, 0, 190);
    summary_refresh();

    page_header(P_PAD, "DualShock 4" BULLET "connected over Bluetooth");
    row_toggle(add_row(P_PAD, "Rumble", "Vibrate with in-game feedback"), mock_set.rumble);
    row_toggle(add_row(P_PAD, "Touchpad gestures", "Map swipes to the PS touchpad"), mock_set.touchpad);
    row_toggle(add_row(P_PAD, "Motion controls", "Use the gyro for aiming"), mock_set.motion);
    static const char *const confirm[2] = { "Cross", "Circle" };
    row_seg(add_row(P_PAD, "Confirm button", "Which face button selects in menus"), confirm, 2, 0, 150);
    s = side_card(P_PAD, "CONTROLLER");
    lv_obj_t *dia = kit_ps_diamond(s, 160, true);
    lv_obj_set_pos(dia, 0, 50);
    lv_obj_t *pn = kit_label(s, FONT_TITLE, COL_TEXT, "DualShock 4");
    lv_obj_set_pos(pn, 0, 250);
    lv_obj_t *pc = kit_label(s, FONT_CAPTION, COL_MUTED, "Share + Options opens the menu\nwhile you play");
    lv_obj_set_pos(pc, 0, 296);

    page_header(P_ACCOUNT, "Signed in as player_one");
    row_action(add_row(P_ACCOUNT, "Find my consoles", "Lists the consoles on your PSN account"), LV_SYMBOL_REFRESH,
               COL_TEXT);
    row_action(add_row(P_ACCOUNT, "Pair with a PIN", "For a console on this network"), LV_SYMBOL_PLUS, COL_TEXT);
    row_action(add_row(P_ACCOUNT, "Sign out", "Keeps the consoles paired with a PIN"), LV_SYMBOL_CLOSE, COL_ERR);
    s = side_card(P_ACCOUNT, "PSN");
    lv_obj_t *av = kit_rect(s, 140, 140, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    kit_grad(av, 0x2bd2c4, 0x0b6f9c);
    lv_obj_set_pos(av, 0, 50);
    lv_obj_center(kit_label(av, FONT_HERO, 0xffffff, "P1"));
    lv_obj_t *an = kit_label(s, FONT_TITLE, COL_TEXT, "player_one");
    lv_obj_set_pos(an, 0, 230);

    page_header(P_LOG, "What just happened");
    static const char *const lines[][2] = {
        { "21:40  Stream started with Living Room PS5", "1080p, 60 fps, H.265 at 15 Mbps" },
        { "21:40  DualShock 4 ready", "Wireless Controller grabbed" },
        { "21:38  Living Room PS5 woke up", "Wake-up sent over the LAN" },
        { "21:37  Found 4 consoles", "3 on this network, 1 over PSN" },
    };
    for (int i = 0; i < 4; i++) add_row(P_LOG, lines[i][0], lines[i][1])->kind = RK_ACTION;

    page_header(P_ADD, "Pair a PS5 or PS4 with this TV");
    static const char *const at[2][3] = {
        { "With a PIN", "On the console: Settings > System >\nRemote Play > Link Device", LV_SYMBOL_KEYBOARD },
        { "With PSN", "Scan a code with your phone and sign in,\nyour consoles show up by themselves", LV_SYMBOL_WIFI },
    };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = g.add_cards[i] = kit_rect(g.pages[P_ADD], 700, 520, 36, 0x18233c, LV_OPA_COVER);
        kit_grad(c, i ? 0x0f5f6e : 0x1d4f9c, 0x0d1628);
        lv_obj_set_pos(c, i * (700 + GRID_GAP), GRID_Y);
        lv_obj_set_style_outline_width(c, 5, 0);
        lv_obj_set_style_outline_pad(c, 6, 0);
        lv_obj_set_style_outline_color(c, lv_color_white(), 0);
        lv_obj_set_style_outline_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_t *ic = kit_rect(c, 140, 140, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_20);
        lv_obj_set_pos(ic, 48, 56);
        lv_obj_center(kit_label(ic, FONT_HERO, COL_TEXT, at[i][2]));
        lv_obj_t *t = kit_label(c, FONT_HERO, COL_TEXT, at[i][0]);
        lv_obj_set_pos(t, 48, 250);
        lv_obj_t *d = kit_label(c, FONT_BODY, 0xb4bfd6, at[i][1]);
        lv_obj_set_pos(d, 48, 330);
    }
    for (int p = P_STREAM; p <= P_LOG; p++) {
        g.frame[p] = kit_focus_frame(g.pages[p], 30);
        lv_obj_set_style_opa(g.frame[p], LV_OPA_TRANSP, 0);
    }
}

/* ---------- console detail sheet ---------- */

static void build_sheet(void) {
    g.sheet = kit_rect(g.home, SHEET_W + 60, SCR_H, 44, 0x0a1222, 250);
    lv_obj_set_pos(g.sheet, SCR_W - SHEET_W, 0);
    lv_obj_set_style_border_width(g.sheet, 2, 0);
    lv_obj_set_style_border_color(g.sheet, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.sheet, LV_OPA_10, 0);
    lv_obj_add_flag(g.sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *glow = kit_glow(g.sheet, 0x2a5bd7, LV_OPA_40);
    lv_obj_set_pos(glow, SHEET_W / 2 - KIT_GLOW_D / 2, 250 - KIT_GLOW_D / 2);
    for (int i = 0; i < 2; i++) {
        g.sheet_art[i] = kit_console_art(g.sheet, i == 1);
        const lv_img_dsc_t *d = lv_img_get_src(g.sheet_art[i]);
        if (d)
            lv_obj_set_pos(g.sheet_art[i], SHEET_W / 2 - (lv_coord_t)d->header.w / 2,
                           250 - (lv_coord_t)d->header.h / 2);
        kit_art_fit(g.sheet_art[i], 300, 300);
    }
    g.sheet_name = kit_label(g.sheet, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.sheet_name, 64, 440);
    g.sheet_state = kit_label(g.sheet, FONT_BODY, 0xb4bfd6, "");
    lv_obj_set_pos(g.sheet_state, 66, 508);
    g.sheet_frame = kit_focus_frame(g.sheet, 28);
    static const char *const labels[4] = { LV_SYMBOL_PLAY "   Start Remote Play", LV_SYMBOL_POWER "   Wake up",
                                           LV_SYMBOL_EDIT "   Rename", LV_SYMBOL_TRASH "   Forget" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = g.sheet_btns[i] = kit_rect(g.sheet, SHEET_W - 128, 88, 24, 0xffffff, LV_OPA_10);
        lv_obj_set_pos(b, 64, 580 + i * (88 + 14));
        if (i == 0) kit_grad(b, COL_ACCENT_HI, COL_ACCENT);
        if (i == 0) lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_t *l = kit_label(b, FONT_HEADING, i == 3 ? COL_DANGER_TEXT : COL_TEXT, labels[i]);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 32, 0);
    }
    lv_obj_move_foreground(g.sheet_frame);
}

static void sheet_hidden_cb(lv_obj_t *s) {
    if (g.zone != Z_DETAIL) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

static void sheet_open(bool open) {
    if (open) {
        const mock_host_t *h = &mock_hosts[g.card];
        lv_label_set_text(g.sheet_name, h->name);
        lv_label_set_text_fmt(g.sheet_state, "%s" BULLET "%s" BULLET "%s", mock_state_text(h->state), h->addr,
                              h->ps5 ? "PS5" : "PS4");
        for (int i = 0; i < 2; i++) lv_obj_set_style_img_opa(g.sheet_art[i], h->ps5 == (i == 1) ? LV_OPA_COVER : 0, 0);
        g.zone = Z_DETAIL;
        g.detail_btn = 0;
        lv_obj_clear_flag(g.sheet, LV_OBJ_FLAG_HIDDEN);
        kit_anim_from(g.sheet, KA_TX, SHEET_W + 60, 0, 420, 0, lv_anim_path_ease_out);
        for (int i = 0; i < 4; i++) {
            kit_anim_from(g.sheet_btns[i], KA_TX, 120, 0, 420, 120 + (uint32_t)i * 50, lv_anim_path_ease_out);
            kit_anim_from(g.sheet_btns[i], KA_OPA, 0, 255, 300, 120 + (uint32_t)i * 50, NULL);
        }
        kit_anim_from(g.sheet_art[h->ps5], KA_TY, 40, 0, 560, 100, kit_path_spring);
        kit_anim(g.veil, KA_BG_OPA, 150, 300, 0, NULL);
        lv_obj_update_layout(g.sheet);
        kit_glide(g.sheet_frame, g.sheet_btns[0], 6, 0);
        kit_anim_from(g.sheet_frame, KA_OPA, 0, 255, 300, 300, NULL);
    } else {
        g.zone = Z_CONTENT;
        kit_anim_then(g.sheet, KA_TX, SHEET_W + 60, 260, lv_anim_path_ease_in, sheet_hidden_cb);
        kit_anim(g.veil, KA_BG_OPA, LV_OPA_TRANSP, 260, 0, NULL);
    }
}

/* ---------- focus painting ---------- */

static void content_paint(void) {
    for (int i = 0; i <= MOCK_HOSTS; i++) {
        rcard_t *c = &g.cards[i];
        bool f = g.page == P_CONSOLES && g.zone == Z_CONTENT && g.card == i;
        kit_anim(c->card, KA_OUTLINE_OPA, f ? LV_OPA_COVER : LV_OPA_TRANSP, 180, 0, NULL);
        kit_anim(c->card, KA_GROW, f ? 10 : 0, 380, 0, kit_path_spring);
        kit_anim(c->card, KA_TY, f ? -10 : 0, 380, 0, kit_path_spring);
        if (c->has_art) {
            uint16_t z = kit_art_zoom(c->ps5, 320, 176);
            kit_anim(c->art, KA_ZOOM, f ? z * 112 / 100 : z, 420, 0, kit_path_spring);
        }
    }
    for (int i = 0; i < 2; i++) {
        bool f = g.page == P_ADD && g.zone == Z_CONTENT && g.add == i;
        kit_anim(g.add_cards[i], KA_OUTLINE_OPA, f ? LV_OPA_COVER : LV_OPA_TRANSP, 180, 0, NULL);
        kit_anim(g.add_cards[i], KA_GROW, f ? 10 : 0, 380, 0, kit_path_spring);
    }
    for (int p = P_STREAM; p <= P_LOG; p++) {
        bool on = g.page == p && g.zone == Z_CONTENT;
        for (int i = 0; i < g.nrows[p]; i++) {
            rrow_t *r = &g.rows[p][i];
            bool f = on && g.row[p] == i;
            if (r->kind == RK_SEG && r->seg.focused != f) kit_seg_focus(&r->seg, f);
            if (r->kind == RK_METER) kit_meter_focus(&r->meter, f);
        }
        if (!g.frame[p]) continue;
        if (on) kit_glide(g.frame[p], g.rows[p][g.row[p]].row, 6, lv_obj_get_style_opa(g.frame[p], 0) ? 240 : 0);
        kit_anim(g.frame[p], KA_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 200, 0, NULL);
    }
}

static void paint(void) {
    rail_paint(300);
    content_paint();
    for (int i = 0; i < 4; i++) {
        bool f = g.zone == Z_DETAIL && g.detail_btn == i;
        kit_anim(g.sheet_btns[i], KA_GROW, f ? 6 : 0, 300, 0, kit_path_spring);
    }
    if (g.zone == Z_DETAIL) kit_glide(g.sheet_frame, g.sheet_btns[g.detail_btn], 6, 240);
}

static void page_show(int p, int dir) {
    for (int i = 0; i < P__N; i++)
        if (i != p) lv_obj_add_flag(g.pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g.pages[p], LV_OBJ_FLAG_HIDDEN);
    kit_anim_from(g.pages[p], KA_TY, dir * 60, 0, 420, 0, lv_anim_path_ease_out);
    kit_anim_from(g.pages[p], KA_OPA, 0, 255, 300, 0, lv_anim_path_ease_out);
}

/* ---------- overlay: the side sheet ---------- */

static const char *ov_val(int i) {
    static char buf[64];
    if (i == 1)
        snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "  %sp%d  " LV_SYMBOL_RIGHT, mock_set.res ? "1080" : "720",
                 mock_set.fps ? 60 : 30);
    else snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, mock_smooth_opts[mock_set.smooth]);
    return buf;
}

static void ov_paint(uint32_t ms) {
    kit_glide(ov.frame, ov.rows[ov.focus], 6, ms);
    for (int i = 0; i < 5; i++) {
        bool f = i == ov.focus;
        kit_anim(ov.rows[i], KA_BG_OPA, f ? LV_OPA_20 : 12, 200, 0, NULL);
        if (ov.vals[i]) kit_mix_color(ov.vals[i], LV_STYLE_TEXT_COLOR, f ? COL_TEXT : COL_MUTED, 200);
    }
}

static void ov_hidden_cb(lv_obj_t *s) {
    if (!ov.open) lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
}

static void ov_open(bool open) {
    ov.open = open;
    if (open) {
        ov.focus = 0;
        lv_label_set_text(ov.note, "");
        lv_obj_clear_flag(ov.sheet, LV_OBJ_FLAG_HIDDEN);
        kit_anim_from(ov.sheet, KA_TX, -SIDE_W - 20, 0, 420, 0, lv_anim_path_ease_out);
        for (int i = 0; i < 5; i++) {
            kit_anim_from(ov.rows[i], KA_TX, -80, 0, 420, 120 + (uint32_t)i * 45, lv_anim_path_ease_out);
            kit_anim_from(ov.rows[i], KA_OPA, 0, 255, 300, 120 + (uint32_t)i * 45, NULL);
        }
        for (int i = 0; i < 4; i++) kit_enter(ov.stat[i], 80 + (uint32_t)i * 40, 20);
        lv_obj_update_layout(ov.sheet);
        ov_paint(0);
        kit_anim_from(ov.frame, KA_OPA, 0, 255, 260, 360, NULL);
    } else {
        kit_anim_then(ov.sheet, KA_TX, -SIDE_W - 20, 260, lv_anim_path_ease_in, ov_hidden_cb);
    }
}

static void ov_change(int dir) {
    if (ov.focus == 1) {
        int q = LV_CLAMP(0, mock_set.res * 2 + mock_set.fps + dir, 3);
        if (q == mock_set.res * 2 + mock_set.fps) {
            kit_anim_from(ov.vals[1], KA_TX, dir * 12, 0, 300, 0, kit_path_spring);
            return;
        }
        mock_set.res = q / 2;
        mock_set.fps = q % 2;
        kit_roll_text(ov.vals[1], ov_val(1), dir);
        kit_roll_text(ov.note, LV_SYMBOL_REFRESH "  Restarts the stream with the new quality when the menu closes", 1);
    } else if (ov.focus == 2) {
        int s = LV_CLAMP(0, mock_set.smooth + dir, 3);
        if (s == mock_set.smooth) {
            kit_anim_from(ov.vals[2], KA_TX, dir * 12, 0, 300, 0, kit_path_spring);
            return;
        }
        mock_set.smooth = s;
        kit_roll_text(ov.vals[2], ov_val(2), dir);
        static const char *const ms[] = { "0", "100", "150", "200" };
        kit_roll_text(ov.stat[3], ms[s], dir);
        kit_roll_text(ov.note, "Smooth playback applies right away", 1);
    } else if (ov.focus == 3) {
        mock_set.rumble = dir > 0;
        kit_toggle_set(&ov.rumble, mock_set.rumble);
    }
}

static void ov_nav(nav_t k) {
    if (!ov.open) {
        if (k == NAV_MENU) ov_open(true);
        return;
    }
    switch (k) {
        case NAV_UP:
            if (ov.focus > 0) {
                ov.focus--;
                ov_paint(240);
            }
            break;
        case NAV_DOWN:
            if (ov.focus < 4) {
                ov.focus++;
                ov_paint(240);
            }
            break;
        case NAV_LEFT:
        case NAV_RIGHT: ov_change(k == NAV_RIGHT ? 1 : -1); break;
        case NAV_OK:
            if (ov.focus == 0) ov_open(false);
            else if (ov.focus == 4) {
                ov_open(false);
                if (kit_stream_hook) kit_stream_hook(false);
            } else if (ov.focus == 3) ov_change(mock_set.rumble ? -1 : 1);
            else ov_change(1);
            break;
        case NAV_BACK:
        case NAV_MENU: ov_open(false); break;
    }
}

static void ov_tick(lv_timer_t *t) {
    (void)t;
    static int n;
    n++;
    static const char *const mbps[] = { "14.8", "15.1", "14.6", "15.0" };
    lv_label_set_text_fmt(ov.time, "Streaming" BULLET "%s", mock_clock_text());
    lv_label_set_text(ov.stat[0], n % 5 == 2 ? "59" : "60");
    lv_label_set_text(ov.stat[1], mbps[n % 4]);
}

static void build_overlay(void) {
    ov.root = kit_obj(g.root);
    lv_obj_set_size(ov.root, SCR_W, SCR_H);
    lv_obj_add_flag(ov.root, LV_OBJ_FLAG_HIDDEN);
    ov.sheet = kit_rect(ov.root, SIDE_W, SCR_H, 0, 0x070b16, 228);
    lv_obj_set_style_border_width(ov.sheet, 2, 0);
    lv_obj_set_style_border_side(ov.sheet, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(ov.sheet, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ov.sheet, LV_OPA_10, 0);
    lv_obj_add_flag(ov.sheet, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *av = kit_rect(ov.sheet, 72, 72, 20, mock_hosts[0].tint, LV_OPA_COVER);
    kit_grad(av, mock_hosts[0].tint + 0x202020, 0x0d1628);
    lv_obj_set_pos(av, 48, SAFE_Y);
    lv_obj_t *art = kit_console_art(av, true);
    lv_obj_center(art);
    kit_art_fit(art, 50, 56);
    lv_obj_t *name = kit_label(ov.sheet, FONT_TITLE, COL_TEXT, mock_hosts[0].name);
    lv_obj_set_pos(name, 140, SAFE_Y + 2);
    ov.time = kit_label(ov.sheet, FONT_CAPTION, COL_MUTED, "Streaming");
    lv_obj_set_pos(ov.time, 140, SAFE_Y + 44);

    static const char *const units[4] = { "fps", "Mbps", "dropped", "ms delay" };
    static const char *const vals[4] = { "60", "14.8", "0", "150" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *t = kit_rect(ov.sheet, 254, 96, 22, 0xffffff, 14);
        lv_obj_set_pos(t, 48 + (i % 2) * 270, 168 + (i / 2) * 110);
        ov.stat[i] = kit_label(t, FONT_TITLE, COL_TEXT, vals[i]);
        lv_obj_set_pos(ov.stat[i], 24, 14);
        lv_obj_t *u = kit_label(t, FONT_CAPTION, COL_MUTED, units[i]);
        lv_obj_set_pos(u, 24, 56);
        if (i == 0) lv_obj_align(kit_dot(t, 12, COL_OK), LV_ALIGN_TOP_RIGHT, -20, 20);
    }

    static const char *const titles[5] = { "Resume", "Quality", "Smooth playback", "Rumble", "Disconnect" };
    static const char *const syms[5] = { LV_SYMBOL_PLAY, LV_SYMBOL_VIDEO, LV_SYMBOL_LOOP, LV_SYMBOL_CHARGE,
                                         LV_SYMBOL_POWER };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *r = ov.rows[i] = kit_rect(ov.sheet, SIDE_W - 96, 88, 24, 0xffffff, 12);
        lv_obj_set_pos(r, 48, 400 + i * (88 + 12));
        lv_obj_t *ic = kit_label(r, FONT_TITLE, i == 4 ? COL_ERR : COL_TEXT, syms[i]);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 26, 0);
        lv_obj_t *t = kit_label(r, FONT_HEADING, i == 4 ? COL_DANGER_TEXT : COL_TEXT, titles[i]);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 84, 0);
        if (i == 1 || i == 2) {
            ov.vals[i] = kit_label(r, FONT_BODY, COL_MUTED, ov_val(i));
            lv_obj_align(ov.vals[i], LV_ALIGN_RIGHT_MID, -24, 0);
        }
        if (i == 3) {
            kit_toggle_create(&ov.rumble, r, mock_set.rumble);
            lv_obj_align(ov.rumble.track, LV_ALIGN_RIGHT_MID, -18, 0);
        }
    }
    ov.frame = kit_focus_frame(ov.sheet, 28);
    ov.note = kit_label(ov.sheet, FONT_CAPTION, COL_ACCENT_HI, "");
    lv_obj_set_width(ov.note, SIDE_W - 96);
    lv_label_set_long_mode(ov.note, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ov.note, 48, 908);
    lv_obj_t *hints = kit_hints(ov.sheet);
    lv_obj_set_style_pad_column(hints, SP_L, 0);
    lv_obj_set_pos(hints, 48, SCR_H - SAFE_Y - 40);
    kit_hint_key(hints, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, "Change");
    kit_hint(hints, PS_CROSS, "Select");
    kit_hint(hints, PS_CIRCLE, "Game");
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
    lv_obj_t *bg = kit_rect(g.home, SCR_W, SCR_H, 0, COL_BG_TOP, LV_OPA_COVER);
    kit_grad(bg, 0x13254a, COL_BG_BOTTOM);
    lv_obj_set_style_bg_grad_stop(bg, 230, 0);

    g.content = kit_obj(g.home);
    lv_obj_set_size(g.content, SCR_W - CX0, SCR_H);
    lv_obj_set_pos(g.content, CX0, 0);
    build_consoles();
    build_settings();
    lv_obj_t *hints = kit_hints(g.home);
    lv_obj_align(hints, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -SAFE_Y + 14);
    kit_hint(hints, PS_CROSS, "Select");
    kit_hint(hints, PS_CIRCLE, "Back");
    g.veil = kit_rect(g.home, SCR_W, SCR_H, 0, 0x000000, LV_OPA_TRANSP);
    build_rail();
    build_sheet();
    build_overlay();

    g.zone = Z_CONTENT;
    g.page = P_CONSOLES;
    lv_obj_clear_flag(g.pages[P_CONSOLES], LV_OBJ_FLAG_HIDDEN);
    paint();
    for (int i = 0; i <= MOCK_HOSTS; i++) {
        kit_anim_from(g.cards[i].card, KA_TY, 70, 0, 520, 80 + (uint32_t)i * 55, lv_anim_path_ease_out);
        kit_anim_from(g.cards[i].card, KA_OPA, 0, 255, 360, 80 + (uint32_t)i * 55, NULL);
    }
    kit_anim_from(g.rail, KA_TX, -RAIL_C, 0, 420, 0, lv_anim_path_ease_out);
}

static void connect_card(void) {
    const mock_host_t *h = &mock_hosts[g.card];
    if (h->state == HS_OFFLINE) {
        kit_anim_from(g.sheet_btns[0], KA_TX, 18, 0, 420, 0, kit_path_spring);
        return;
    }
    kit_connect_screen(h);
}

static void nav_rows(int p, nav_t k) {
    int *ri = &g.row[p];
    rrow_t *r = &g.rows[p][*ri];
    if (k == NAV_UP && *ri > 0) (*ri)--;
    else if (k == NAV_DOWN && *ri < g.nrows[p] - 1) (*ri)++;
    else if (k == NAV_BACK || (k == NAV_LEFT && r->kind == RK_ACTION)) g.zone = Z_RAIL;
    else if (k == NAV_LEFT || k == NAV_RIGHT || k == NAV_OK) {
        int d = k == NAV_LEFT ? -1 : 1;
        if (r->kind == RK_SEG) {
            int s = k == NAV_OK ? (r->seg.sel + 1) % r->seg.n : r->seg.sel + d;
            if (s < 0 || s >= r->seg.n) {
                kit_anim_from(r->seg.box, KA_TX, d * 14, 0, 320, 0, kit_path_spring);
                return;
            }
            kit_seg_select(&r->seg, s);
            if (p == P_STREAM) {
                int *v[] = { &mock_set.res, &mock_set.fps, &mock_set.codec, NULL, &mock_set.smooth, NULL };
                if (v[*ri]) *v[*ri] = s;
                summary_refresh();
            }
        } else if (r->kind == RK_METER && k != NAV_OK) {
            kit_meter_set(&r->meter, r->meter.val + d * 5);
            mock_set.bitrate = r->meter.val;
            summary_refresh();
        } else if (r->kind == RK_TOGGLE) {
            kit_toggle_set(&r->tgl, k == NAV_OK ? !r->tgl.val : d > 0);
        } else if (r->kind == RK_ACTION && k == NAV_OK) {
            kit_anim_from(r->row, KA_GROW, 8, 0, 360, 0, kit_path_spring);
        }
    }
}

static void nav(nav_t k) {
    if (ov.root && (ov.open || lv_obj_has_flag(g.home, LV_OBJ_FLAG_HIDDEN))) {
        ov_nav(k);
        return;
    }
    switch (g.zone) {
        case Z_RAIL:
            if ((k == NAV_UP && g.page > 0) || (k == NAV_DOWN && g.page < P__N - 1)) {
                int d = k == NAV_DOWN ? 1 : -1;
                g.page += d;
                page_show(g.page, d);
            } else if (k == NAV_RIGHT || k == NAV_OK) g.zone = Z_CONTENT;
            break;
        case Z_CONTENT:
            if (g.page == P_CONSOLES) {
                int n = MOCK_HOSTS + 1, c = g.card;
                if (k == NAV_LEFT) {
                    if (c % 3 == 0) g.zone = Z_RAIL;
                    else g.card--;
                } else if (k == NAV_RIGHT && c % 3 < 2 && c + 1 < n) g.card++;
                else if (k == NAV_DOWN && c + 3 < n) g.card += 3;
                else if (k == NAV_DOWN && c / 3 == 0 && n > 3) g.card = n - 1;
                else if (k == NAV_UP && c >= 3) g.card -= 3;
                else if (k == NAV_BACK) g.zone = Z_RAIL;
                else if (k == NAV_OK) {
                    if (c == MOCK_HOSTS) {
                        g.page = P_ADD;
                        page_show(P_ADD, 1);
                        g.add = 0;
                    } else sheet_open(true);
                }
            } else if (g.page == P_ADD) {
                if (k == NAV_LEFT) {
                    if (g.add == 0) g.zone = Z_RAIL;
                    else g.add = 0;
                } else if (k == NAV_RIGHT) g.add = 1;
                else if (k == NAV_BACK) g.zone = Z_RAIL;
                else if (k == NAV_OK) kit_anim_from(g.add_cards[g.add], KA_GROW, 20, 10, 360, 0, kit_path_spring);
            } else {
                nav_rows(g.page, k);
            }
            break;
        case Z_DETAIL:
            if (k == NAV_UP && g.detail_btn > 0) g.detail_btn--;
            else if (k == NAV_DOWN && g.detail_btn < 3) g.detail_btn++;
            else if (k == NAV_BACK) sheet_open(false);
            else if (k == NAV_OK && g.detail_btn == 0) connect_card();
            else if (k == NAV_OK) kit_anim_from(g.sheet_btns[g.detail_btn], KA_TX, 16, 0, 400, 0, kit_path_spring);
            break;
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
        if (g.zone == Z_DETAIL) {
            g.zone = Z_CONTENT;
            lv_obj_add_flag(g.sheet, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(g.veil, LV_OPA_TRANSP, 0);
        }
        paint();
        kit_anim_from(g.home, KA_OPA, 0, 255, 420, 0, lv_anim_path_ease_out);
    }
}

/* clang-format off */
static const step_t g_script[] = {
    S_SHOT("home", "Consoles: a grid of big cards, the rail folded to icons while the focus is in the content"),
    S_REC("tour", "Cards lift on focus; LEFT from the first column unfolds the rail, the content follows it"),
    S_WAIT(800), S_KEY(NAV_RIGHT), S_WAIT(650), S_KEY(NAV_RIGHT), S_WAIT(650), S_KEY(NAV_DOWN), S_WAIT(650),
    S_KEY(NAV_LEFT), S_WAIT(650), S_KEY(NAV_UP), S_WAIT(650), S_KEY(NAV_LEFT), S_WAIT(650), S_KEY(NAV_LEFT),
    S_WAIT(1000), S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_DOWN), S_WAIT(700),
    S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_UP), S_WAIT(300), S_KEY(NAV_UP),
    S_WAIT(300), S_KEY(NAV_UP), S_WAIT(300), S_KEY(NAV_UP), S_WAIT(300), S_KEY(NAV_UP), S_WAIT(900),
    S_KEY(NAV_RIGHT), S_WAIT(900),
    S_REC_END,
    S_REC("settings", "Settings: one focus frame glides between rows, LEFT/RIGHT change values, Circle back"),
    S_KEY(NAV_LEFT), S_WAIT(700), S_KEY(NAV_DOWN), S_WAIT(800), S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_LEFT),
    S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_DOWN), S_WAIT(450), S_KEY(NAV_DOWN), S_WAIT(450),
    S_KEY(NAV_DOWN), S_WAIT(450), S_KEY(NAV_RIGHT), S_WAIT(300), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_DOWN),
    S_WAIT(450), S_KEY(NAV_LEFT), S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_DOWN), S_WAIT(450),
    S_KEY(NAV_OK), S_WAIT(900),
    S_SHOT("settings", "Stream settings: segmented choices, a bitrate bar and toggles, with a summary"),
    S_KEY(NAV_BACK), S_WAIT(700), S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_OK),
    S_WAIT(600), S_KEY(NAV_BACK), S_WAIT(500), S_KEY(NAV_UP), S_WAIT(400), S_KEY(NAV_UP), S_WAIT(800),
    S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1100), S_KEY(NAV_DOWN), S_WAIT(450), S_KEY(NAV_DOWN),
    S_WAIT(450), S_KEY(NAV_UP), S_WAIT(450), S_KEY(NAV_UP), S_WAIT(700),
    S_SHOT("detail", "Cross on a console: a detail sheet with big actions slides in from the right"),
    S_KEY(NAV_BACK), S_WAIT(900),
    S_REC_END,
    S_REC("stream", "Play, then the side sheet over the game: UP/DOWN pick, LEFT/RIGHT change in place"),
    S_KEY(NAV_OK), S_WAIT(800), S_KEY(NAV_OK), S_WAIT(2900), S_KEY(NAV_MENU), S_WAIT(1000), S_KEY(NAV_DOWN),
    S_WAIT(500), S_KEY(NAV_LEFT), S_WAIT(700), S_KEY(NAV_DOWN), S_WAIT(500), S_KEY(NAV_LEFT), S_WAIT(500),
    S_KEY(NAV_RIGHT), S_WAIT(500), S_KEY(NAV_DOWN), S_WAIT(500), S_KEY(NAV_LEFT), S_WAIT(700), S_KEY(NAV_UP),
    S_WAIT(400), S_KEY(NAV_UP), S_WAIT(1200),
    S_SHOT("stream-menu", "Side sheet: live stats on top, big rows below, the right two thirds stay clear"),
    S_KEY(NAV_BACK), S_WAIT(1200), S_KEY(NAV_MENU), S_WAIT(900), S_KEY(NAV_DOWN), S_WAIT(250), S_KEY(NAV_DOWN),
    S_WAIT(250), S_KEY(NAV_DOWN), S_WAIT(250), S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1600),
    S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_rail = {
    .id = "03-rail",
    .name = "Rail",
    .tagline = "A TV launcher: a side rail that unfolds on focus, big cards, and a side sheet over the game",
    .notes =
        "<p><b>Layout.</b> An icon rail on the left (Consoles, Stream, Controller, Account, Log, Add console) that "
        "widens with labels and dims the content while it has the focus, and folds back to icons when the focus "
        "moves right. The content follows the rail item live. Consoles is a grid of big cards with the console art; "
        "Cross on one slides a detail sheet in from the right with Start Remote Play, Wake up, Rename and Forget. "
        "The settings pages are big rows with a summary card, closest to today's tabs.</p>"
        "<p><b>Navigation.</b> The plainest model: the rail is UP/DOWN, the grid is all four directions, rows are "
        "UP/DOWN to pick and LEFT/RIGHT to change in place, Circle always steps back one level (detail sheet, then "
        "the rail). One white focus frame glides between rows instead of each row lighting up.</p>"
        "<p><b>In the stream.</b> A side sheet over the left third: the session, four live stats tiles, then "
        "Resume, Quality, Smooth playback, Rumble and Disconnect as rows that change with LEFT/RIGHT directly, no "
        "adjust mode. The right two thirds of the game stay clear.</p>"
        "<p><b>Cost.</b> The calmest: focus moves repaint one card or two rows, the rail unfolding repaints the "
        "rail and the content veil for 0.3 s. Easiest to wire, since it maps one to one onto the current tabs.</p>",
    .build = build,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_script,
};
