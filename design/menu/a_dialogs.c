/* design/menu/a_dialogs.c - "Add a console" study, entry 04 "Dialogs": the app's two dialogs in
 * Home's style. The Forget confirmation names the console and shows it, Cancel first, Forget red
 * when focused. The login passcode a console can ask for mid-connect is four boxes and the number
 * pad in a sheet that rises over the Connecting screen (like the stream menu), instead of a text
 * field and the full keyboard.
 */
#include <string.h>

#include "addkit.h"
#include "concept.h"

#define CONFIRM_W 920
#define PASS_H    480
#define PASS_X    160
#define BTN_W     240
#define BTN_H     84

enum { D_CONFIRM, D_PASSCODE, D_NONE };

static struct {
    lv_obj_t *root, *home, *conn, *scrim, *card, *btn[2];
    int state, focus;
    ak_numpad_t np;
    ak_digits_t dg;
    char code[5];
} D;

static void open_confirm(void);
static void open_passcode(void);

static lv_obj_t *card(lv_coord_t w, lv_coord_t h) {
    lv_obj_t *c = kit_rect(D.root, w, h, 36, COL_SHEET, 250);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(c, 90, 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_60, 0);
    lv_obj_center(c);
    kit_anim_from(c, KA_GROW, -40, 0, 420, 0, kit_path_spring);
    kit_anim_from(c, KA_OPA, 0, 255, 220, 0, lv_anim_path_ease_out);
    kit_anim(D.scrim, KA_BG_OPA, 160, 260, 0, lv_anim_path_ease_out);
    return c;
}

/* a sheet from the bottom, its lower corners off-screen */
static lv_obj_t *sheet(lv_coord_t h) {
    lv_obj_t *c = kit_rect(D.root, SCR_W, h + 60, 44, COL_SHEET, 250);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, LV_OPA_20, 0);
    lv_obj_set_pos(c, 0, SCR_H - h);
    kit_anim_from(c, KA_TY, h + 60, 0, 420, 0, lv_anim_path_ease_out);
    kit_anim(D.scrim, KA_BG_OPA, 90, 260, 0, lv_anim_path_ease_out);
    return c;
}

static void close_card(void) {
    if (D.card) lv_obj_del(D.card);
    D.card = NULL;
    D.state = D_NONE;
    kit_anim(D.scrim, KA_BG_OPA, LV_OPA_TRANSP, 260, 0, lv_anim_path_ease_in);
}

static void paint(void) {
    if (D.state == D_CONFIRM) {
        ak_button_focus(D.btn[0], D.focus == 0);
        ak_button_danger_focus(D.btn[1], D.focus == 1);
    } else if (D.state == D_PASSCODE) {
        ak_numpad_focus(&D.np, D.focus == 2);
        ak_button_focus(D.btn[0], D.focus == 0);
        ak_button_focus(D.btn[1], D.focus == 1);
        ak_digits_show(&D.dg, D.code, D.focus == 2, false);
        lv_obj_set_style_opa(D.btn[1], strlen(D.code) == 4 ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

static lv_obj_t *buttons(lv_obj_t *parent, const char *ok_icon, const char *ok) {
    lv_obj_t *row = kit_obj(parent);
    lv_obj_set_size(row, 2 * BTN_W + 20, BTN_H);
    D.btn[0] = ak_button(row, BTN_W, BTN_H, NULL, "Cancel");
    D.btn[1] = ak_button(row, BTN_W, BTN_H, ok_icon, ok);
    lv_obj_set_pos(D.btn[1], BTN_W + 20, 0);
    return row;
}

/* ---------- Forget ---------- */

static void open_confirm(void) {
    const mock_host_t *h = &mock_hosts[0];
    D.state = D_CONFIRM;
    D.focus = 0;
    D.card = card(CONFIRM_W, 340);
    lv_obj_t *t = kit_rect(D.card, 150, 150, 34, 0, LV_OPA_COVER);
    kit_grad(t, 0x2d528f, 0x0f1a33);
    lv_obj_set_pos(t, 44, 44);
    lv_obj_center(kit_console_art_box(t, h->ps5, 128, 120));
    lv_obj_t *ttl = kit_label(D.card, FONT_TITLE, COL_TEXT, "");
    lv_label_set_text_fmt(ttl, "Forget %s?", h->name);
    lv_obj_set_pos(ttl, 230, 48);
    lv_obj_t *body = kit_label(D.card, FONT_BODY, COL_SUBTLE,
                               "Its pairing keys leave this TV, and its PSN entry with them. Pair it again with "
                               "its PIN to play it here.");
    lv_obj_set_width(body, CONFIRM_W - 230 - 48);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(body, 230, 100);
    lv_obj_t *row = buttons(D.card, LV_SYMBOL_TRASH, "Forget");
    lv_obj_align(row, LV_ALIGN_BOTTOM_RIGHT, -44, -40);
    paint();
}

/* ---------- the login passcode, mid-connect ---------- */

static void open_passcode(void) {
    const mock_host_t *h = &mock_hosts[0];
    D.state = D_PASSCODE;
    D.focus = 2;
    D.code[0] = 0;
    lv_obj_clear_flag(D.conn, LV_OBJ_FLAG_HIDDEN);
    kit_anim_from(D.conn, KA_OPA, 0, 255, 300, 0, lv_anim_path_ease_out);
    D.card = sheet(PASS_H);
    lv_obj_t *ttl = kit_label(D.card, FONT_HERO, COL_TEXT, "Login passcode");
    lv_obj_set_pos(ttl, PASS_X, 48);
    lv_obj_t *body = kit_label(D.card, FONT_BODY, COL_SUBTLE, "");
    lv_label_set_text_fmt(body, "%s asks for the 4-digit passcode of the user signed in on it.", h->name);
    lv_obj_set_width(body, 760);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(body, PASS_X, 118);
    ak_digits_create(&D.dg, D.card, 4, 104, 124);
    lv_obj_set_pos(D.dg.box, PASS_X, 176);
    lv_obj_t *row = buttons(D.card, LV_SYMBOL_OK, "Connect");
    lv_obj_set_pos(row, PASS_X, 340);
    ak_numpad_create(&D.np, D.card, 80, false);
    lv_obj_set_pos(D.np.box, SCR_W - PASS_X - ak_numpad_w(80), 48);
    paint();
}

static void after_cb(lv_timer_t *t) {
    (void)t;
    lv_obj_add_flag(D.conn, LV_OBJ_FLAG_HIDDEN);
    open_confirm();
}

/* ---------- concept ---------- */

static void build(lv_obj_t *root) {
    memset(&D, 0, sizeof(D));
    D.root = root;
    D.home = kit_obj(root);
    lv_obj_set_size(D.home, SCR_W, SCR_H);
    concept_home.build(D.home);
    D.conn = kit_rect(root, SCR_W, SCR_H, 0, AK_TINT_PIN, LV_OPA_COVER); /* the Connecting screen */
    kit_grad(D.conn, AK_TINT_PIN, COL_BG_BOTTOM);
    lv_obj_t *g = kit_glow(D.conn, AK_TINT_PIN, LV_OPA_60);
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, 250 - KIT_GLOW_D / 2);
    lv_obj_t *art = kit_console_art_box(D.conn, true, 400, 330);
    lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_t *t = kit_label(D.conn, FONT_TITLE, COL_TEXT, "");
    lv_label_set_text_fmt(t, "Connecting to %s", mock_hosts[0].name);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 438);
    lv_obj_t *w = kit_label(D.conn, FONT_CAPTION, COL_SUBTLE, "It asks for the login passcode");
    lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 486);
    lv_obj_add_flag(D.conn, LV_OBJ_FLAG_HIDDEN);
    D.scrim = kit_rect(root, SCR_W, SCR_H, 0, 0x000000, LV_OPA_TRANSP);
    open_confirm();
}

static void nav(nav_t k) {
    if (D.state == D_CONFIRM) {
        if (k == NAV_LEFT || k == NAV_RIGHT) D.focus = k == NAV_RIGHT;
        else if (k == NAV_OK || k == NAV_BACK) { /* the demo goes on to the passcode either way */ close_card();
            open_passcode();
            return;
        }
    } else if (D.state == D_PASSCODE) {
        size_t n = strlen(D.code);
        if (k == NAV_BACK) {
            close_card();
            lv_timer_set_repeat_count(lv_timer_create(after_cb, 600, NULL), 1);
            return;
        }
        if (D.focus == 2) {
            if (k == NAV_OK) {
                char c = ak_numpad_press(&D.np);
                if (c == '\b' && n) D.code[n - 1] = 0;
                else if (c >= '0' && c <= '9' && n < 4) {
                    D.code[n] = c;
                    D.code[n + 1] = 0;
                    if (n + 1 == 4) D.focus = 1;
                }
            } else if (!ak_numpad_move(&D.np, k) && k == NAV_LEFT) {
                D.focus = 1;
            }
        } else {
            if (k == NAV_LEFT && D.focus == 1) D.focus = 0;
            else if (k == NAV_RIGHT) D.focus = D.focus == 0 ? 1 : 2;
            else if (k == NAV_OK) {
                if (D.focus == 1 && n < 4) {
                    ak_shake(D.btn[1]);
                    return;
                }
                close_card();
                lv_timer_set_repeat_count(lv_timer_create(after_cb, 1200, NULL), 1);
                return;
            }
        }
    }
    paint();
}

#define K(k) S_KEY(k), S_WAIT(200)
/* clang-format off */
static const step_t g_script[] = {
    S_WAIT(900),
    S_SHOT("confirm", "Forget: it names and shows the console; Cancel has the focus"),
    S_REC("dialogs", "The confirmation, then the login passcode a console can ask for mid-connect"),
    S_WAIT(600), S_KEY(NAV_RIGHT), S_WAIT(700),
    S_SHOT("confirm-forget", "Forget turns red when it has the focus"),
    S_KEY(NAV_LEFT), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(1000),
    K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_DOWN), K(NAV_OK), K(NAV_DOWN), K(NAV_OK),
    S_WAIT(600),
    S_SHOT("passcode", "The login passcode: a sheet over the Connecting screen, four boxes and the pad, no keyboard"),
    S_KEY(NAV_OK), S_WAIT(1800), S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_add_dialogs = {
    .id = "04-dialogs",
    .name = "Dialogs",
    .tagline = "The confirmation and the login passcode, in Home's style (goes with any of the three)",
    .notes = "<p><b>Forget.</b> A dark card over a scrim, like the stream menu's sheet: the console's tile, a "
             "title that names it, what forgetting does, and two pills. Cancel has the focus; Forget turns red "
             "when it gets it.</p>"
             "<p><b>Login passcode.</b> A console can ask for its user's 4-digit passcode while it connects. "
             "Instead of a text field and the full keyboard, four boxes and the number pad in a sheet that rises "
             "from the bottom (like the stream menu), so the Connecting screen stays in view; Connect takes the "
             "focus when the four digits are in.</p>"
             "<p>The keyboard itself, restyled the same way (frosted keys, the white focus, a sheet that rises), "
             "is in the PSN steps of the three flows.</p>",
    .build = build,
    .nav = nav,
    .set_stream = NULL,
    .script = g_script,
    .sheet = { "confirm", "confirm-forget", "passcode" },
};
