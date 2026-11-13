/* design/menu/a_setup.c - "Add a console" concept 01 "Setup": the flow takes the whole screen,
 * the way the PS5's own setup and sign-in screens do. Text and the step's control on the left, a
 * picture or the number pad on the right, a progress line on top; the backdrop is blue on the
 * local-network path and teal on the PSN one. Leaving or finishing goes back to Home.
 */
#include <stdio.h>
#include <string.h>

#include "addkit.h"
#include "concept.h"

#define TRACK_Y 118
#define TRACK_W (SCR_W - 2 * SAFE_X)

static struct { lv_obj_t *root, *bg, *glow, *stage, *fill, *step_lbl, *hints; } S;

static void follow(void) {
    uint32_t tint = ak_tint();
    kit_mix_color(S.bg, LV_STYLE_BG_COLOR, tint, 420);
    kit_mix_color(S.glow, LV_STYLE_IMG_RECOLOR, tint + 0x202020, 420);
    int n;
    const char *name;
    int i = ak_progress(&n, &name);
    bool done = ak_step() == AK_DONE || ak_step() == AK_FOUND;
    kit_anim(S.fill, KA_W, TRACK_W * (done ? n : i) / n, 420, 0, lv_anim_path_ease_out);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d / %d" BULLET "%s", i, n, name);
    kit_roll_text(S.step_lbl, buf, 1);
}

static void restart(void);

static void on_step(ak_step_t s, int method) {
    (void)s;
    (void)method;
    follow();
}

static void on_keyboard(bool open) {
    kit_anim(S.hints, KA_OPA, open ? LV_OPA_TRANSP : LV_OPA_COVER, 220, 0, lv_anim_path_ease_out);
}

static void on_close(bool added) {
    (void)added;
    restart(); /* the gallery has no Home behind this one: start over */
}

static void restart(void) {
    lv_obj_clean(S.stage);
    lv_anim_del(S.stage, NULL);
    const ak_layout_t lay = {
        .x = SAFE_X,
        .y = 200,
        .main_w = 820,
        .aside_x = 900,
        .aside_y = -10,
        .aside_w = TRACK_W - 900,
        .aside_h = 720,
        .title_font = FONT_HERO,
        .key = 104,
        .kb_parent = S.stage,
        .kb_x = (SCR_W - 1480) / 2,
        .kb_w = 1480,
        .kb_h = 420,
    };
    const ak_hooks_t hooks = { on_step, on_close, on_keyboard };
    ak_create(S.stage, &lay, &hooks);
    follow();
    kit_anim_from(S.stage, KA_OPA, 0, 255, 360, 0, lv_anim_path_ease_out);
    kit_anim_from(S.stage, KA_TY, 30, 0, 420, 0, lv_anim_path_ease_out);
}

static void build(lv_obj_t *root) {
    memset(&S, 0, sizeof(S));
    S.root = root;
    S.bg = kit_rect(root, SCR_W, SCR_H, 0, AK_TINT_PIN, LV_OPA_COVER);
    kit_grad(S.bg, AK_TINT_PIN, COL_BG_BOTTOM);
    lv_obj_set_style_bg_main_stop(S.bg, 0, 0);
    lv_obj_set_style_bg_grad_stop(S.bg, 235, 0);
    S.glow = kit_glow(root, AK_TINT_PIN + 0x202020, LV_OPA_40);
    lv_obj_set_pos(S.glow, SAFE_X + 900 + (TRACK_W - 900) / 2 - KIT_GLOW_D / 2, 560 - KIT_GLOW_D / 2);

    lv_obj_t *over = kit_label(root, FONT_SMALL, COL_ACCENT_HI, "ADD A CONSOLE");
    lv_obj_set_style_text_letter_space(over, 3, 0);
    lv_obj_set_pos(over, SAFE_X, 66);
    S.step_lbl = kit_label(root, FONT_CAPTION, COL_SUBTLE, "");
    lv_obj_set_width(S.step_lbl, 600);
    lv_obj_set_style_text_align(S.step_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(S.step_lbl, SCR_W - SAFE_X - 600, 62);
    lv_obj_t *track = kit_rect(root, TRACK_W, 6, LV_RADIUS_CIRCLE, 0xffffff, 30);
    lv_obj_set_pos(track, SAFE_X, TRACK_Y);
    S.fill = kit_rect(track, 0, 6, LV_RADIUS_CIRCLE, COL_ACCENT_HI, LV_OPA_COVER);

    S.hints = kit_hints(root);
    lv_obj_align(S.hints, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -SAFE_Y + 14);
    kit_hint(S.hints, PS_CROSS, "Select");
    kit_hint(S.hints, PS_CIRCLE, "Back");

    S.stage = kit_obj(root);
    lv_obj_set_size(S.stage, SCR_W, SCR_H);
    restart();
}

static void nav(nav_t k) {
    ak_nav(k);
    if (ak_step() == AK_METHOD) follow(); /* the backdrop follows the focused way in */
}

#define K(k) S_KEY(k), S_WAIT(170)
/* clang-format off */
static const step_t g_script[] = {
    S_WAIT(900),
    S_SHOT("method", "How: two ways in, as big rows; the backdrop and the picture follow the focus"),
    S_REC("pin-flow", "The PIN path with the D-pad or remote: the console, its address (this TV's network is "
                      "filled in), the PIN, pairing"),
    S_WAIT(500), S_KEY(NAV_DOWN), S_WAIT(900), S_KEY(NAV_UP), S_WAIT(800),
    S_KEY(NAV_OK), S_WAIT(800),
    S_SHOT("kind", "Which console: two tiles instead of a dropdown"),
    S_KEY(NAV_RIGHT), S_WAIT(600), S_KEY(NAV_LEFT), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), S_WAIT(400),
    S_SHOT("address", "The address: four octets that start on this TV's network, typed on the pad"),
    K(NAV_LEFT), S_KEY(NAV_LEFT), S_WAIT(500), S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_DOWN), K(NAV_OK),                                     /* 4 */
    K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK),                       /* 8 */
    K(NAV_DOWN), K(NAV_OK),                                     /* 0 */
    K(NAV_UP), K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK),    /* 1 */
    K(NAV_RIGHT), K(NAV_OK), K(NAV_OK),                         /* 2 2 */
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK),          /* 9 */
    K(NAV_LEFT), K(NAV_LEFT), K(NAV_OK),                        /* 7: complete, Pair takes the focus */
    S_WAIT(600),
    S_SHOT("pin", "The PIN: eight boxes and the pad; Pair takes the focus once all eight are in"),
    S_KEY(NAV_OK), S_WAIT(1500),
    S_SHOT("pairing", "Pairing: the console in a progress ring"),
    S_WAIT(2300),
    S_SHOT("done", "Ready: the new tile, and Remote Play one press away"),
    S_WAIT(900), S_REC_END,
    S_KEY(NAV_OK), S_WAIT(900),
    S_REC("pin-error", "A wrong PIN: the boxes turn red, shake, and say what to check"),
    S_KEY(NAV_OK), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(800),
    K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_LEFT), S_WAIT(300), S_KEY(NAV_OK), S_WAIT(800),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_DOWN),
    K(NAV_OK), K(NAV_OK), K(NAV_OK), K(NAV_OK), K(NAV_OK), K(NAV_OK), K(NAV_OK), K(NAV_OK),
    S_WAIT(400), S_KEY(NAV_OK), S_WAIT(3500),
    S_SHOT("error", "The PIN was refused: red boxes and what to check"),
    S_WAIT(700), S_REC_END,
    S_KEY(NAV_BACK), S_WAIT(500), S_KEY(NAV_BACK), S_WAIT(500), S_KEY(NAV_BACK), S_WAIT(900),
    S_REC("psn-flow", "PSN: scan the code, sign in on the phone, type the code with the keyboard, pick the consoles"),
    S_KEY(NAV_DOWN), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1000),
    S_SHOT("psn", "Sign in with the phone: the QR card, three steps, the code field"),
    S_KEY(NAV_OK), S_WAIT(700),
    K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_UP), K(NAV_OK),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT),
    S_WAIT(400),
    S_SHOT("psn-keyboard", "The keyboard, restyled: frosted keys, the white focus, the field kept above it"),
    S_KEY(NAV_OK), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(1300),
    S_SHOT("linking", "Signing in"),
    S_WAIT(1700),
    S_SHOT("found", "The consoles on the account; a PIN-paired one is grouped, not added twice"),
    S_WAIT(900), S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_add_setup = {
    .id = "01-setup",
    .name = "Setup",
    .tagline = "A full-screen flow, like the PS5's own setup and sign-in screens",
    .notes = "<p><b>Idea.</b> Adding a console is a short setup: the screen belongs to it. Text and the step's "
             "control sit on the left, a picture of what to do (or the number pad) on the right, and a progress "
             "line runs along the top. Blue is the local-network path, teal the PSN one.</p>"
             "<p><b>Steps.</b> How (two big rows) &rarr; which console (tiles) &rarr; its address &rarr; its PIN "
             "&rarr; pairing &rarr; ready. Or: how &rarr; sign in with the phone &rarr; the consoles found. Circle "
             "goes back one step; from the first one it leaves.</p>"
             "<p><b>Typing.</b> No cursor and no text field for numbers: the address is four octets that start on "
             "this TV's own network (most homes are one /24, so only the last number is typed), the PIN is eight "
             "boxes, and a big number pad sits beside them. The remote's number keys would type straight in "
             "(to verify on the TV). The PSN code is the one free-text field, typed on the restyled keyboard.</p>"
             "<p><b>Trade-off.</b> The clearest and roomiest of the three, but it leaves Home entirely.</p>",
    .build = build,
    .nav = nav,
    .set_stream = NULL,
    .script = g_script,
};
