/* design/menu/a_sheet.c - "Add a console" concept 03 "Sheet": a panel over Home.
 *
 * Home stays where it was (the Add tile focused) under a scrim, and a tall sheet slides in from the
 * right with the whole flow stacked in it: step pills at the top, the text, the control, then the
 * picture, the number pad or the QR card under it. The keyboard rises inside the sheet. When it is
 * done the sheet slides away and Home says what was added.
 */
#include <stdio.h>
#include <string.h>

#include "addkit.h"
#include "concept.h"

#define SHEET_W 800
#define PAD     56
#define PILLS   5
#define PILL_W  64

static struct {
    lv_obj_t *root, *home, *scrim, *sheet, *stage, *pill[PILLS], *step_lbl, *toast;
    bool open;
} H;

static void pills(void) {
    int n;
    const char *name;
    int i = ak_progress(&n, &name);
    bool done = ak_step() == AK_DONE || ak_step() == AK_FOUND;
    for (int p = 0; p < PILLS; p++) {
        if (p >= n) {
            lv_obj_add_flag(H.pill[p], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(H.pill[p], LV_OBJ_FLAG_HIDDEN);
        bool on = p < i || done;
        kit_mix_color(H.pill[p], LV_STYLE_BG_COLOR,
                      on ? (ak_method() == AK_BY_PSN && ak_step() != AK_METHOD ? 0x5fe0d2 : COL_ACCENT_HI) : 0xffffff,
                      300);
        kit_anim(H.pill[p], KA_BG_OPA, on ? LV_OPA_COVER : 50, 300, 0, lv_anim_path_ease_out);
        kit_anim(H.pill[p], KA_W, p == i - 1 && !done ? PILL_W * 2 : PILL_W, 360, 0, kit_path_spring);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%d / %d" BULLET "%s", i, n, name);
    kit_roll_text(H.step_lbl, buf, 1);
}

static void toast(const char *text) {
    if (H.toast) lv_obj_del(H.toast);
    H.toast = kit_rect(H.root, 520, 64, LV_RADIUS_CIRCLE, COL_SHEET, 235);
    lv_obj_set_style_border_width(H.toast, 2, 0);
    lv_obj_set_style_border_color(H.toast, lv_color_white(), 0);
    lv_obj_set_style_border_opa(H.toast, LV_OPA_20, 0);
    lv_obj_align(H.toast, LV_ALIGN_BOTTOM_LEFT, SAFE_X, -SAFE_Y + 4);
    lv_obj_center(kit_label(H.toast, FONT_BODY, COL_TEXT, text));
    kit_anim_from(H.toast, KA_TY, 30, 0, 340, 200, lv_anim_path_ease_out);
    kit_anim_from(H.toast, KA_OPA, 0, 255, 260, 200, lv_anim_path_ease_out);
}

static void on_step(ak_step_t s, int method) {
    (void)s;
    (void)method;
    pills();
}

static void sheet_gone_cb(lv_obj_t *sheet) {
    (void)sheet;
    if (H.open) return;
    lv_obj_add_flag(H.sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(H.stage);
}

static void on_close(bool added) {
    H.open = false;
    kit_anim_then(H.sheet, KA_TX, SHEET_W + 80, 320, lv_anim_path_ease_in, sheet_gone_cb);
    kit_anim(H.scrim, KA_BG_OPA, LV_OPA_TRANSP, 320, 0, lv_anim_path_ease_in);
    if (added) {
        char buf[64];
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK "   %s is on Home", ak_console_name());
        toast(buf);
    }
}

static void sheet_open(void) {
    H.open = true;
    if (H.toast) {
        lv_obj_del(H.toast);
        H.toast = NULL;
    }
    lv_obj_clear_flag(H.sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(H.stage);
    const ak_layout_t lay = {
        .x = PAD,
        .y = 168,
        .main_w = SHEET_W - 2 * PAD,
        .aside_x = 0,
        .aside_y = 516,
        .aside_w = SHEET_W - 2 * PAD,
        .aside_h = 350,
        .stacked = true,
        .title_font = FONT_DISPLAY,
        .key = 74,
        .kb_parent = H.stage,
        .kb_x = 0,
        .kb_w = SHEET_W,
        .kb_h = 380,
    };
    const ak_hooks_t hooks = { on_step, on_close, NULL };
    ak_create(H.stage, &lay, &hooks);
    pills();
    kit_anim_from(H.sheet, KA_TX, SHEET_W + 80, 0, 420, 0, lv_anim_path_ease_out);
    kit_anim(H.scrim, KA_BG_OPA, 150, 320, 0, lv_anim_path_ease_out);
}

static void build(lv_obj_t *root) {
    memset(&H, 0, sizeof(H));
    H.root = root;
    H.home = kit_obj(root);
    lv_obj_set_size(H.home, SCR_W, SCR_H);
    concept_home.build(H.home);
    for (int i = 0; i < MOCK_HOSTS; i++) concept_home.nav(NAV_RIGHT); /* on the Add tile */
    H.scrim = kit_rect(root, SCR_W, SCR_H, 0, 0x000000, LV_OPA_TRANSP);
    H.sheet = kit_rect(root, SHEET_W + 60, SCR_H, 44, COL_SHEET, 248);
    lv_obj_set_pos(H.sheet, SCR_W - SHEET_W, 0);
    lv_obj_set_style_border_width(H.sheet, 2, 0);
    lv_obj_set_style_border_color(H.sheet, lv_color_white(), 0);
    lv_obj_set_style_border_opa(H.sheet, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(H.sheet, 90, 0);
    lv_obj_set_style_shadow_opa(H.sheet, LV_OPA_50, 0);
    lv_obj_add_flag(H.sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *over = kit_label(H.sheet, FONT_SMALL, COL_ACCENT_HI, "ADD A CONSOLE");
    lv_obj_set_style_text_letter_space(over, 3, 0);
    lv_obj_set_pos(over, PAD, 62);
    H.step_lbl = kit_label(H.sheet, FONT_CAPTION, COL_SUBTLE, "");
    lv_obj_set_width(H.step_lbl, 340);
    lv_obj_set_style_text_align(H.step_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(H.step_lbl, SHEET_W - PAD - 340, 58);
    lv_obj_t *prow = kit_obj(H.sheet);
    lv_obj_set_size(prow, SHEET_W - 2 * PAD, 10);
    lv_obj_set_pos(prow, PAD, 106);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(prow, 10, 0);
    for (int p = 0; p < PILLS; p++) H.pill[p] = kit_rect(prow, PILL_W, 8, LV_RADIUS_CIRCLE, 0xffffff, 50);
    H.stage = kit_obj(H.sheet);
    lv_obj_set_size(H.stage, SHEET_W, SCR_H);
}

static void nav(nav_t k) {
    if (H.open) {
        ak_nav(k);
        if (H.open && ak_step() == AK_METHOD) pills();
    } else if (k == NAV_OK) {
        sheet_open();
    } else {
        concept_home.nav(k);
    }
}

#define K(k) S_KEY(k), S_WAIT(170)
/* clang-format off */
static const step_t g_script[] = {
    S_WAIT(1200),
    S_REC("pin-flow", "Cross on the Add tile: the sheet slides in over Home and carries the whole flow"),
    S_WAIT(300), S_KEY(NAV_OK), S_WAIT(1000),
    S_SHOT("method", "How: rows in the sheet, the picture under them"),
    S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_UP), S_WAIT(800), S_KEY(NAV_OK), S_WAIT(900),
    S_SHOT("kind", "Which console"),
    S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), S_WAIT(400),
    S_SHOT("address", "The address; the pad sits under it, Next above the pad"),
    K(NAV_UP), K(NAV_UP), K(NAV_UP), S_KEY(NAV_UP), S_WAIT(500), S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_DOWN), K(NAV_OK), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), K(NAV_DOWN), K(NAV_OK),
    K(NAV_UP), K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_OK),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), K(NAV_LEFT), K(NAV_LEFT), K(NAV_OK),
    S_WAIT(600),
    S_SHOT("pin", "The PIN"),
    S_KEY(NAV_OK), S_WAIT(1500),
    S_SHOT("pairing", "Pairing"),
    S_WAIT(2300),
    S_SHOT("done", "Ready"),
    S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1200),
    S_SHOT("added", "The sheet slides away; Home says what was added"),
    S_WAIT(500), S_REC_END,
    S_REC("psn-flow", "PSN in the sheet: the QR card under the steps, the keyboard rising inside the sheet"),
    S_KEY(NAV_OK), S_WAIT(800), S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1000),
    S_SHOT("psn", "Sign in with the phone"),
    S_KEY(NAV_OK), S_WAIT(700),
    K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_UP), K(NAV_OK),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT),
    S_WAIT(400),
    S_SHOT("psn-keyboard", "The keyboard inside the sheet"),
    S_KEY(NAV_OK), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(3000),
    S_SHOT("found", "The consoles on the account"),
    S_WAIT(800), S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_add_sheet = {
    .id = "03-sheet",
    .name = "Sheet",
    .tagline = "A panel slides in over Home and carries the whole flow",
    .notes = "<p><b>Idea.</b> A side sheet, like the PS5's own panels over the home screen: Home stays visible "
             "under a scrim, the flow is stacked in an 800 px sheet from the right with step pills on top. The "
             "keyboard rises inside the sheet, so Home never moves. When it is done the sheet slides away and a "
             "note on Home says what was added.</p>"
             "<p><b>Same steps and controls</b> as Setup, stacked: the picture, the number pad or the QR card "
             "sits under the text, and the button above the pad (UP from the pad's top row reaches it).</p>"
             "<p><b>Trade-off.</b> Lightest and quickest to dismiss; everything is smaller (the pad keys are 76 "
             "px, the QR 350 px), which is still comfortable at 1080p but the least roomy of the three.</p>",
    .build = build,
    .nav = nav,
    .set_stream = NULL,
    .script = g_script,
};
