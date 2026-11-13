/* design/menu/u_update.c - "Update notice" study: ways to say a newer release is out, each over Home
 * as the app shows it once the splash is gone. The answer arrives ARRIVE_MS after the concept is
 * built (the splash, then GitHub). 01 is a one-line toast at the bottom left; 02 a notification
 * card with a QR code at the top right; 03 a chip that joins the top bar and stays; 04 a card over
 * Home with what is new. 05 and 06 stay until the user moves: 01's toast, and a smaller card with
 * the QR code over the key art. 07 and 08 are a compact card with the app mark, over the top bar's
 * right end or at the bottom left; 07 is the app's (src/ui/home.c). Every key goes to Home except
 * while 04's card is up; in 05 and 06 the first key KEY_MIN_MS after it appeared also sends the
 * notice away.
 */
#include <stdio.h>
#include <string.h>

#include "addkit.h"
#include "concept.h"
#include "ui/logo.h"
#include "ui/qr.h"

#define VERSION     "0.2"
#define INSTALLED   "0.1"
#define REPO        "github.com/renjfk/chiaki-webos"
#define RELEASE_URL "https://" REPO "/releases/latest"
#define ARRIVE_MS   1400 /* the splash is gone and GitHub has answered */

#define TOAST_H  64
#define TOAST_MS 9000

#define CARD_W  600
#define CARD_H  208
#define CARD_QR 160
#define CARD_Y  (SAFE_Y + 76) /* under the top bar */
#define CARD_MS 15000

#define CHIP_H     52
#define CHIP_RIGHT 254 /* Home's top bar right of it: the pad, the avatar, the clock */

#define SHEET_W  1120
#define SHEET_H  540
#define SHEET_QR 240
#define SHEET_P  56
#define BTN_H    76

#define MINI_LOGO 48 /* 07, 08: the app mark, the height of the two lines beside it */
#define MINI_PAD  18 /* above and below */
#define MINI_SIDE SP_XL
#define MINI_GAP  20
#define MINI_H    (MINI_LOGO + 2 * MINI_PAD + 4) /* and the border */
#define MINI_TOP  (SAFE_Y - 4 + 26 - MINI_H / 2) /* 07: centred on the top bar's 52 px row, over its right end */
#define MINI_LOW  (SAFE_Y - 14)                  /* 08: level with the hints, under the quick cards */

#define KEY_MIN_MS 4000 /* 05, 06: a key this soon after it appeared was not meant for it */
#define QT_W       540  /* 06: over the key art, clear of the quick cards */
#define QT_H       200
#define QT_QR      152
#define QT_BOTTOM  (SAFE_Y + 56) /* above the hints */

enum { U_TOAST, U_CARD, U_CHIP, U_SHEET, U_TOAST_KEY, U_QR_KEY, U_MINI, U_MINI_LOW };

static struct {
    lv_obj_t *home, *layer, *note, *scrim, *btn[2];
    lv_timer_t *timer;
    int kind, focus;
    bool open;         /* 04's card is up and takes the keys */
    bool sticky;       /* 05, 06: up until a key comes KEY_MIN_MS after it appeared */
    uint32_t shown_at; /* lv_tick when it appeared */
} U;

static lv_obj_t *frame(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius, lv_opa_t opa) {
    lv_obj_t *o = kit_rect(parent, w, h, radius, COL_SHEET, opa);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_20, 0);
    return o;
}

static lv_obj_t *overline(lv_obj_t *parent, const char *text) {
    lv_obj_t *l = kit_label(parent, FONT_SMALL, COL_ACCENT_HI, text);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    return l;
}

static lv_obj_t *wrapped(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text, lv_coord_t w) {
    lv_obj_t *l = kit_label(parent, font, col, text);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void later(uint32_t ms, lv_timer_cb_t cb) {
    U.timer = lv_timer_create(cb, ms, NULL);
    lv_timer_set_repeat_count(U.timer, 1);
}

/* width from the text: a content-sized parent with a centred child is not resolved by LVGL 8 */
static void fit_width(lv_obj_t *box, lv_obj_t *label, lv_coord_t pad) {
    lv_point_t sz;
    lv_txt_get_size(&sz, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0), 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    lv_obj_set_width(box, sz.x + 2 * pad);
    lv_obj_center(label);
}

/* ---------- 01 the toast ---------- */

static void toast_gone_cb(lv_timer_t *t) {
    (void)t;
    U.timer = NULL;
    kit_anim(U.note, KA_OPA, LV_OPA_TRANSP, 300, 0, lv_anim_path_ease_in);
}

static void show_toast(bool sticky) {
    U.note = frame(U.layer, 400, TOAST_H, LV_RADIUS_CIRCLE, 235);
    fit_width(U.note,
              kit_label(U.note, FONT_BODY, COL_TEXT,
                        LV_SYMBOL_DOWNLOAD "   Chiaki webOS " VERSION " is available" BULLET REPO),
              SP_2XL);
    lv_obj_align(U.note, LV_ALIGN_BOTTOM_LEFT, SAFE_X, -SAFE_Y + 4);
    kit_anim_from(U.note, KA_TY, 30, 0, 320, 0, lv_anim_path_ease_out);
    kit_anim_from(U.note, KA_OPA, 0, 255, 200, 0, lv_anim_path_ease_out);
    if (!sticky) later(TOAST_MS, toast_gone_cb);
}

/* ---------- 02 the notification card ---------- */

static void card_gone_cb(lv_timer_t *t) {
    (void)t;
    U.timer = NULL;
    kit_anim(U.note, KA_TX, CARD_W / 3, 360, 0, lv_anim_path_ease_in);
    kit_anim(U.note, KA_OPA, LV_OPA_TRANSP, 300, 60, lv_anim_path_ease_in);
}

static void show_card(void) {
    U.note = frame(U.layer, CARD_W, CARD_H, 28, 250);
    lv_obj_set_style_shadow_width(U.note, 60, 0);
    lv_obj_set_style_shadow_opa(U.note, LV_OPA_50, 0);
    lv_obj_align(U.note, LV_ALIGN_TOP_RIGHT, -SAFE_X, CARD_Y);
    lv_obj_t *qr = ui_qr(U.note, RELEASE_URL, CARD_QR);
    lv_obj_set_pos(qr, SP_XL, SP_XL);
    lv_coord_t x = CARD_QR + 2 * SP_XL + SP_S, w = CARD_W - x - SP_XL;
    lv_obj_set_pos(overline(U.note, "UPDATE AVAILABLE"), x, 30);
    lv_obj_set_pos(kit_label(U.note, FONT_TITLE, COL_TEXT, "Chiaki webOS " VERSION), x, 56);
    lv_obj_set_pos(wrapped(U.note, FONT_CAPTION, COL_SUBTLE, "Scan it with a phone:\nthe download and what is new", w),
                   x, 104);
    lv_obj_set_pos(kit_label(U.note, FONT_SMALL, COL_MUTED, REPO), x, 164);
    kit_anim_from(U.note, KA_TX, CARD_W / 3, 0, 420, 0, lv_anim_path_ease_out);
    kit_anim_from(U.note, KA_OPA, 0, 255, 260, 0, lv_anim_path_ease_out);
    later(CARD_MS, card_gone_cb);
}

/* ---------- 03 the chip in the top bar ---------- */

static void show_chip(void) {
    U.note = kit_rect(U.layer, 200, CHIP_H, LV_RADIUS_CIRCLE, COL_ACCENT, LV_OPA_COVER);
    kit_grad(U.note, COL_ACCENT_HI, COL_ACCENT);
    lv_obj_set_style_shadow_color(U.note, C(COL_ACCENT_HI), 0);
    lv_obj_set_style_shadow_width(U.note, 48, 0);
    lv_obj_set_style_shadow_spread(U.note, 4, 0);
    lv_obj_set_style_shadow_opa(U.note, LV_OPA_TRANSP, 0);
    fit_width(U.note, kit_label(U.note, FONT_CAPTION, COL_TEXT, LV_SYMBOL_DOWNLOAD "  Update " VERSION), SP_XL);
    lv_obj_align(U.note, LV_ALIGN_TOP_RIGHT, -(SAFE_X + CHIP_RIGHT + SP_XL), SAFE_Y - 4);
    kit_anim_from(U.note, KA_TX, 40, 0, 420, 0, kit_path_spring);
    kit_anim_from(U.note, KA_OPA, 0, 255, 260, 0, lv_anim_path_ease_out);
    kit_anim_from(U.note, KA_SHADOW_OPA, 200, 0, 1800, 450, lv_anim_path_ease_out); /* one glow, then quiet */
}

/* ---------- 04 the card over Home ---------- */

static void sheet_paint(void) {
    ak_button_focus(U.btn[0], U.focus == 0);
    ak_button_focus(U.btn[1], U.focus == 1);
}

static void show_sheet(void) {
    U.open = true;
    U.focus = 0;
    U.scrim = kit_rect(U.layer, SCR_W, SCR_H, 0, 0x000000, LV_OPA_TRANSP);
    kit_anim(U.scrim, KA_BG_OPA, 160, 260, 0, lv_anim_path_ease_out);
    U.note = frame(U.layer, SHEET_W, SHEET_H, 36, 250);
    lv_obj_set_style_shadow_width(U.note, 90, 0);
    lv_obj_set_style_shadow_opa(U.note, LV_OPA_60, 0);
    lv_obj_center(U.note);
    kit_anim_from(U.note, KA_GROW, -40, 0, 420, 0, kit_path_spring);
    kit_anim_from(U.note, KA_OPA, 0, 255, 220, 0, lv_anim_path_ease_out);

    lv_coord_t x = SHEET_P + 96 + SP_XL, text_w = SHEET_W - SHEET_QR - 2 * SHEET_P - SP_2XL;
    lv_obj_set_pos(kit_app_mark(U.note, 96), SHEET_P, SHEET_P);
    lv_obj_set_pos(overline(U.note, "UPDATE AVAILABLE"), x, SHEET_P + 4);
    lv_obj_set_pos(kit_label(U.note, FONT_TITLE, COL_TEXT, "Chiaki webOS " VERSION " is out"), x, SHEET_P + 28);
    lv_obj_set_pos(kit_label(U.note, FONT_CAPTION, COL_SUBTLE, "This TV has " INSTALLED), x, SHEET_P + 74);
    lv_obj_t *news = wrapped(U.note, FONT_BODY, COL_TEXT,
                             LV_SYMBOL_BULLET "   Wakes a console in rest mode before it connects\n" LV_SYMBOL_BULLET
                                              "   Smoother picture on Wi-Fi with Smooth playback\n" LV_SYMBOL_BULLET
                                              "   The DualShock 4's battery on Home",
                             text_w);
    lv_obj_set_style_text_line_space(news, SP_M, 0);
    lv_obj_set_pos(news, SHEET_P, SHEET_P + 140);

    lv_obj_t *qr = ui_qr(U.note, RELEASE_URL, SHEET_QR);
    lv_obj_set_pos(qr, SHEET_W - SHEET_P - SHEET_QR, SHEET_P);
    lv_obj_t *cap = wrapped(U.note, FONT_CAPTION, COL_MUTED, "Scan it for the download", SHEET_QR);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cap, SHEET_W - SHEET_P - SHEET_QR, SHEET_P + SHEET_QR + SP_M);

    U.btn[0] = ak_button(U.note, 220, BTN_H, NULL, "Later");
    U.btn[1] = ak_button(U.note, 340, BTN_H, NULL, "Skip this version");
    lv_obj_set_pos(U.btn[0], SHEET_P, SHEET_H - SHEET_P - BTN_H);
    lv_obj_set_pos(U.btn[1], SHEET_P + 220 + SP_L, SHEET_H - SHEET_P - BTN_H);
    sheet_paint();
}

static void sheet_close(void) {
    U.open = false;
    kit_anim(U.note, KA_OPA, LV_OPA_TRANSP, 200, 0, lv_anim_path_ease_in);
    kit_anim(U.scrim, KA_BG_OPA, LV_OPA_TRANSP, 260, 0, lv_anim_path_ease_in);
}

/* ---------- 06 the QR toast, over the key art ---------- */

static void show_qr_toast(void) {
    U.note = frame(U.layer, QT_W, QT_H, 28, 250);
    lv_obj_set_style_shadow_width(U.note, 60, 0);
    lv_obj_set_style_shadow_opa(U.note, LV_OPA_50, 0);
    lv_obj_align(U.note, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -QT_BOTTOM);
    lv_coord_t m = (QT_H - QT_QR) / 2;
    lv_obj_set_pos(ui_qr(U.note, RELEASE_URL, QT_QR), m, m);
    lv_coord_t x = QT_QR + 2 * m + SP_S, w = QT_W - x - SP_XL;
    lv_obj_set_pos(overline(U.note, "UPDATE AVAILABLE"), x, 28);
    lv_obj_set_pos(kit_label(U.note, FONT_HEADING, COL_TEXT, "Chiaki webOS " VERSION), x, 54);
    lv_obj_set_pos(wrapped(U.note, FONT_CAPTION, COL_SUBTLE, "Scan it with a phone for the download", w), x, 100);
    kit_anim_from(U.note, KA_TY, 40, 0, 360, 0, lv_anim_path_ease_out);
    kit_anim_from(U.note, KA_OPA, 0, 255, 240, 0, lv_anim_path_ease_out);
}

/* ---------- 07 the compact toast: the app mark, the version, the address ---------- */

static int mini_dir(void) { return U.kind == U_MINI ? -1 : 1; } /* it comes from its edge and goes back */

static void mini_gone_cb(lv_timer_t *t) {
    (void)t;
    U.timer = NULL;
    kit_anim(U.note, KA_TY, 24 * mini_dir(), 320, 0, lv_anim_path_ease_in);
    kit_anim(U.note, KA_OPA, LV_OPA_TRANSP, 280, 0, lv_anim_path_ease_in);
}

static void show_mini(void) {
    U.note = frame(U.layer, 400, MINI_H, 26, LV_OPA_COVER); /* hides what it covers */
    lv_obj_set_style_shadow_width(U.note, 50, 0);
    lv_obj_set_style_shadow_opa(U.note, LV_OPA_40, 0);
    lv_obj_set_pos(ui_logo(U.note, MINI_LOGO), MINI_SIDE, MINI_PAD);
    lv_coord_t x = MINI_SIDE + MINI_LOGO + MINI_GAP;
    lv_obj_t *title = kit_label(U.note, FONT_BODY, COL_TEXT, "Chiaki webOS " VERSION " is available");
    lv_obj_set_pos(title, x, MINI_PAD - 5);
    lv_obj_t *addr = kit_label(U.note, FONT_CAPTION, COL_MUTED, REPO);
    lv_obj_set_pos(addr, x, MINI_PAD + 27);
    lv_point_t a, b;
    lv_txt_get_size(&a, lv_label_get_text(title), FONT_BODY, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_txt_get_size(&b, lv_label_get_text(addr), FONT_CAPTION, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(U.note, x + LV_MAX(a.x, b.x) + MINI_SIDE + 4);
    if (U.kind == U_MINI) lv_obj_align(U.note, LV_ALIGN_TOP_RIGHT, -SAFE_X + SP_L, MINI_TOP);
    else lv_obj_align(U.note, LV_ALIGN_BOTTOM_LEFT, SAFE_X, -MINI_LOW);
    kit_anim_from(U.note, KA_TY, 24 * mini_dir(), 0, 360, 0, lv_anim_path_ease_out);
    kit_anim_from(U.note, KA_OPA, 0, 255, 240, 0, lv_anim_path_ease_out);
    later(TOAST_MS, mini_gone_cb);
}

/* 05, 06: the first key long enough after it appeared sends it away (and still moves Home) */
static void sticky_key(void) {
    if (!U.sticky || lv_tick_elaps(U.shown_at) < KEY_MIN_MS) return;
    U.sticky = false;
    kit_anim(U.note, KA_TY, 30, 300, 0, lv_anim_path_ease_in);
    kit_anim(U.note, KA_OPA, LV_OPA_TRANSP, 260, 0, lv_anim_path_ease_in);
}

/* ---------- concepts ---------- */

static void arrive_cb(lv_timer_t *t) {
    (void)t;
    U.timer = NULL;
    switch (U.kind) {
        case U_TOAST: show_toast(false); break;
        case U_CARD: show_card(); break;
        case U_CHIP: show_chip(); break;
        case U_SHEET: show_sheet(); break;
        case U_TOAST_KEY: show_toast(true); break;
        case U_QR_KEY: show_qr_toast(); break;
        case U_MINI:
        case U_MINI_LOW: show_mini(); break;
    }
    U.sticky = U.kind == U_TOAST_KEY || U.kind == U_QR_KEY;
    U.shown_at = lv_tick_get();
}

static void layer_deleted_cb(lv_event_t *e) {
    (void)e;
    if (U.timer) lv_timer_del(U.timer);
    memset(&U, 0, sizeof(U));
}

static void build_kind(lv_obj_t *root, int kind) {
    memset(&U, 0, sizeof(U));
    U.kind = kind;
    U.home = kit_obj(root);
    lv_obj_set_size(U.home, SCR_W, SCR_H);
    concept_home.build(U.home);
    U.layer = kit_obj(root);
    lv_obj_set_size(U.layer, SCR_W, SCR_H);
    lv_obj_add_event_cb(U.layer, layer_deleted_cb, LV_EVENT_DELETE, NULL);
    later(ARRIVE_MS, arrive_cb);
}

static void nav(nav_t k) {
    if (!U.open) {
        sticky_key();
        concept_home.nav(k);
        return;
    }
    if (k == NAV_LEFT || k == NAV_RIGHT) {
        U.focus = k == NAV_RIGHT;
        sheet_paint();
    } else if (k == NAV_OK || k == NAV_BACK) {
        sheet_close();
    }
}

static void set_stream(bool on) {
    concept_home.set_stream(on);
    if (on) lv_obj_add_flag(U.layer, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(U.layer, LV_OBJ_FLAG_HIDDEN);
}

static void build_toast(lv_obj_t *root) { build_kind(root, U_TOAST); }
static void build_card(lv_obj_t *root) { build_kind(root, U_CARD); }
static void build_chip(lv_obj_t *root) { build_kind(root, U_CHIP); }
static void build_sheet(lv_obj_t *root) { build_kind(root, U_SHEET); }
static void build_toast_key(lv_obj_t *root) { build_kind(root, U_TOAST_KEY); }
static void build_qr_key(lv_obj_t *root) { build_kind(root, U_QR_KEY); }
static void build_mini(lv_obj_t *root) { build_kind(root, U_MINI); }
static void build_mini_low(lv_obj_t *root) { build_kind(root, U_MINI_LOW); }

/* clang-format off */
static const step_t g_toast_script[] = {
    S_REC("arrives", "Once the splash is gone: the toast rises at the bottom left and leaves after 9 s"),
    S_WAIT(2400),
    S_SHOT("shown", "The app's toast, with the address to get it from"),
    S_KEY(NAV_UP), S_WAIT(400), S_KEY(NAV_RIGHT), S_WAIT(1400),
    S_SHOT("later", "Still up on Settings: it sits under the panels"),
    S_WAIT(7400), S_REC_END,
    S_END,
};
static const step_t g_card_script[] = {
    S_REC("arrives", "A notification card slides in at the top right and leaves after 15 s"),
    S_WAIT(2400),
    S_SHOT("shown", "The QR code opens the release on a phone: the TV cannot follow a link"),
    S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_RIGHT), S_WAIT(900),
    S_SHOT("later", "Browsing goes on under it; with many consoles it covers the end of the row"),
    S_WAIT(13000), S_REC_END,
    S_END,
};
static const step_t g_chip_script[] = {
    S_REC("arrives", "A chip joins the top bar with one glow, and stays for the run"),
    S_WAIT(3000),
    S_SHOT("shown", "Beside the controller and the clock: there whenever you look"),
    S_KEY(NAV_UP), S_WAIT(400), S_KEY(NAV_RIGHT), S_WAIT(1400),
    S_SHOT("later", "Still there on Settings: a state, not a message"),
    S_KEY(NAV_LEFT), S_WAIT(800), S_KEY(NAV_DOWN), S_WAIT(1000), S_REC_END,
    S_END,
};
static const step_t g_sheet_script[] = {
    S_REC("arrives", "A card over Home: what is new, a QR code, Later or Skip this version"),
    S_WAIT(2400),
    S_SHOT("shown", "Hard to miss: Later has the focus, Circle closes it too"),
    S_KEY(NAV_RIGHT), S_WAIT(700),
    S_SHOT("later", "Skip this version: quiet until the next release"),
    S_KEY(NAV_LEFT), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(1400), S_REC_END,
    S_END,
};
/* arrives ~0.9 s into the clip: a key at ~2 s is too soon to clear it, the one at ~12 s does */
static const step_t g_toast_key_script[] = {
    S_REC("arrives", "It stays until you move: a key in its first 4 s leaves it up, the next one clears it"),
    S_WAIT(2000), S_KEY(NAV_RIGHT), S_WAIT(2500),
    S_SHOT("shown", "Up after a key in its first seconds"),
    S_WAIT(6000),
    S_SHOT("later", "Still up 11 s on, for whoever looks up late"),
    S_KEY(NAV_LEFT), S_WAIT(1400), S_REC_END,
    S_END,
};
static const step_t g_qr_key_script[] = {
    S_REC("arrives", "Over the key art until you move; the QR code is there for as long as it takes"),
    S_WAIT(2000), S_KEY(NAV_RIGHT), S_WAIT(2500),
    S_SHOT("shown", "The QR code where the console art stands, clear of the buttons and cards"),
    S_WAIT(6000),
    S_SHOT("later", "Still up 11 s on: time to find the phone"),
    S_KEY(NAV_LEFT), S_WAIT(1400), S_REC_END,
    S_END,
};
static const step_t g_mini_script[] = {
    S_REC("arrives", "Drops into the top bar's right end for 9 s, over the controller, the avatar and the clock"),
    S_WAIT(2400),
    S_SHOT("shown", "Where the PS5 shows its notifications, above the tile row"),
    S_KEY(NAV_UP), S_WAIT(400), S_KEY(NAV_RIGHT), S_WAIT(1400),
    S_SHOT("later", "On Settings: still clear of the tiles"),
    S_WAIT(7400), S_REC_END,
    S_END,
};
static const step_t g_mini_low_script[] = {
    S_REC("arrives", "Rises at the bottom left, level with the hints, for 9 s"),
    S_WAIT(2400),
    S_SHOT("shown", "Where the app's other toasts are, under the quick cards"),
    S_KEY(NAV_UP), S_WAIT(400), S_KEY(NAV_RIGHT), S_WAIT(1400),
    S_SHOT("later", "On Settings: it meets the last row's edge"),
    S_WAIT(7400), S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_update_toast = {
    .id = "01-toast",
    .name = "Toast",
    .tagline = "A pill at the bottom for 9 s, then gone",
    .notes = "<p>The app's own toast (the one that says a wake-up was sent), with a download icon, the version "
             "and the address. It waits for the splash and for Home to be idle, never covers a game, and Settings "
             "&gt; About keeps naming the version after it goes (true of every entry here).</p>"
             "<p>Cheap and familiar. The address is all it can offer: nothing to press, and gone if nobody looked "
             "at the screen in those 9 s.</p>",
    .build = build_toast,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_toast_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_card = {
    .id = "02-card",
    .name = "Notification card",
    .tagline = "The PS5's notification place, with a QR code for the download",
    .notes = "<p>A card slides in under the top bar at the right, where the PS5 shows its notifications: the QR "
             "code of the latest release on the left, the version and the address beside it. It leaves after "
             "15 s; Home keeps the keys meanwhile.</p>"
             "<p>The QR code is the one thing a TV app can hand over: the phone opens the release page with the "
             "<code>.ipk</code> and its notes. With four consoles and the Add tile it covers the end of the row "
             "while it is up.</p>",
    .build = build_card,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_card_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_chip = {
    .id = "03-chip",
    .name = "Top bar chip",
    .tagline = "Quiet: a chip beside the clock, there for the whole run",
    .notes = "<p>A blue pill joins the top bar with one glow and stays until the app closes, on both Consoles "
             "and Settings. Nothing moves after that.</p>"
             "<p>Never in the way and never missed for long, but it says little: the version only. It wants a "
             "place to say more, such as Settings &gt; About with the QR code of 02.</p>",
    .build = build_chip,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_chip_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_sheet = {
    .id = "04-sheet",
    .name = "Card over Home",
    .tagline = "What is new and a QR code; Later, or Skip this version",
    .notes = "<p>A card in the style of the app's dialogs, over a scrim: the app mark, the new version and the "
             "one this TV has, a few lines of what is new (the release notes' first lines; mock here), and a large "
             "QR code. Later has the focus; Skip this version keeps it quiet until the next release.</p>"
             "<p>Nobody misses it, and it is the only one that says why to update. It also stands between the "
             "user and their game on every launch until they update or skip, and the scrim repaints the whole "
             "screen while it fades.</p>",
    .build = build_sheet,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_sheet_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_toast_key = {
    .id = "05-toast-until-key",
    .name = "Toast until you move",
    .tagline = "01's toast, up until the first key that comes 4 s after it",
    .notes = "<p>The same pill as 01, but no timer: it stays until the user moves. A key in its first 4 s (already "
             "on the way when it appeared) leaves it up; the next one sends it away, and still moves Home, so it "
             "never costs a press.</p>"
             "<p>Nobody who looks up late misses it, and it is gone the moment they get going. It still only names "
             "the address: Settings &gt; About would carry the QR code for later.</p>",
    .build = build_toast_key,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_toast_key_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_qr_key = {
    .id = "06-qr-until-key",
    .name = "QR toast until you move",
    .tagline = "A card with the QR code over the key art, up until the first key that comes 4 s after it",
    .notes = "<p>A small card where the console art stands at the bottom right, clear of the buttons, the quick "
             "cards and the hints: the QR code of the latest release, the version, and what to do with it. It goes "
             "the way 05 does: on the first key 4 s after it appeared, a key that still moves Home.</p>"
             "<p>The only way the notice can hand something over is a QR code, and here it stays for as long as it "
             "takes to find the phone. It only ever shows on Home's first view, since the key that leaves Home "
             "takes it along.</p>",
    .build = build_qr_key,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_qr_key_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_mini = {
    .id = "07-compact-top",
    .name = "Compact toast, top right",
    .tagline = "The app mark beside the version and the address, over the top bar's right end, 9 s (picked)",
    .notes = "<p>01 made small and given the app mark: the mark, then two lines (the version, and the address in "
             "grey), in a dark card that drops into the top bar's right end, where the PS5 shows its notifications. "
             "For its 9 s it covers the controller, the avatar and the clock, nothing that takes a key; Home keeps "
             "the keys.</p>"
             "<p>No QR code: an update goes on from a computer (webOS Dev Manager or <code>scripts/tv.sh</code>), "
             "where the address is easy to type and never changes. In the top bar it stays clear of the tile row "
             "however many consoles there are, and of the Settings tiles.</p>",
    .build = build_mini,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_mini_script,
    .sheet = { "shown", "later", "" },
};

const concept_t concept_update_mini_low = {
    .id = "08-compact-bottom",
    .name = "Compact toast, bottom left",
    .tagline = "The same card where the app's other toasts are, level with the hints, 9 s",
    .notes = "<p>07's card at the bottom left, where the app says a wake-up was sent or a stream ended, lowered "
             "to the hints' line so it stays under the quick cards.</p>"
             "<p>Consistent with the other toasts. Two lines are taller than their one, so on Settings it meets the "
             "edge of the last row while it is up.</p>",
    .build = build_mini_low,
    .nav = nav,
    .set_stream = set_stream,
    .script = g_mini_low_script,
    .sheet = { "shown", "later", "" },
};
