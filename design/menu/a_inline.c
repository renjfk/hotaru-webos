/* design/menu/a_inline.c - "Add a console" concept 02 "Inline": the flow lives in Home's hero.
 *
 * Home stays: the tab row, the top bar and the tile row with the Add tile focused. Cross on "Add a
 * console" turns the hero below the row into the flow: the two ways in as cards, then each step's
 * text on the left and its picture or the number pad where the key art stands. The caption beside
 * the Add tile shows the progress. When the console is added it slides into the row and Home shows
 * it, ready to play.
 */
#include <stdio.h>
#include <string.h>

#include "addkit.h"
#include "concept.h"

#define TILE_S  176
#define TILE_F  256
#define ROW_Y   168
#define HERO_Y  488
#define NAME_X  (SAFE_X + TILE_F + 36)
#define ART_CX  1520
#define FLOOR_Y 884

enum { M_HERO, M_FLOW, M_ADDED };

static struct {
    lv_obj_t *root, *bg, *glow, *top, *row, *add_tile, *new_tile, *ttl, *ttl_name, *ttl_sub, *hero, *h_over, *h_title,
        *h_meta, *h_btn, *h_btn_icon, *h_btn_lbl, *add_circle, *art, *stage;
    int mode;
} I;

static void tint(uint32_t c) {
    kit_mix_color(I.bg, LV_STYLE_BG_COLOR, c, 420);
    kit_mix_color(I.glow, LV_STYLE_IMG_RECOLOR, c + 0x202020, 420);
}

static lv_obj_t *tile(lv_obj_t *parent, uint32_t top, uint32_t bottom, lv_coord_t s) {
    lv_obj_t *t = kit_rect(parent, s, s, 30, top, LV_OPA_COVER);
    kit_grad(t, top, bottom);
    lv_obj_set_style_border_width(t, 2, 0);
    lv_obj_set_style_border_color(t, lv_color_white(), 0);
    lv_obj_set_style_border_opa(t, LV_OPA_10, 0);
    ak_outline(t, 5, 7);
    return t;
}

/* ---------- the hero: Add a console, or the console just added ---------- */

static void hero_show(bool added) {
    lv_obj_clear_flag(I.hero, LV_OBJ_FLAG_HIDDEN);
    if (added) {
        bool ps5 = ak_console_ps5();
        lv_label_set_text_fmt(I.h_over, "PLAYSTATION %s   " LV_SYMBOL_BULLET "   192.168.1.40", ps5 ? "5" : "4");
        lv_label_set_text(I.h_title, ak_console_name());
        lv_label_set_text(I.h_meta, "Paired with a PIN on this network");
        lv_label_set_text(I.h_btn_icon, LV_SYMBOL_PLAY);
        lv_label_set_text(I.h_btn_lbl, "Start Remote Play");
        lv_obj_add_flag(I.add_circle, LV_OBJ_FLAG_HIDDEN);
        if (I.art) lv_obj_del(I.art);
        I.art = kit_console_art_box(I.root, ps5, 580, 470);
        lv_obj_update_layout(I.art);
        lv_obj_set_pos(I.art, ART_CX - lv_obj_get_width(I.art) / 2, FLOOR_Y - lv_obj_get_height(I.art));
        kit_anim_from(I.art, KA_TX, 70, 0, 520, 100, lv_anim_path_ease_out);
        kit_anim_from(I.art, KA_IMG_OPA, 0, 255, 420, 100, lv_anim_path_ease_out);
        tint(AK_TINT_PIN);
    } else {
        lv_label_set_text(I.h_over, "NEW CONSOLE");
        lv_label_set_text(I.h_title, "Add a console");
        lv_label_set_text(I.h_meta, "Pair a PS5 or PS4 with its PIN, or sign in to PSN");
        lv_label_set_text(I.h_btn_icon, LV_SYMBOL_PLUS);
        lv_label_set_text(I.h_btn_lbl, "Add a console");
        lv_obj_clear_flag(I.add_circle, LV_OBJ_FLAG_HIDDEN);
        kit_anim_from(I.add_circle, KA_OPA, 0, 255, 360, 120, lv_anim_path_ease_out);
        tint(AK_TINT_NEUTRAL);
    }
    kit_anim_from(I.hero, KA_TX, 40, 0, 380, 60, lv_anim_path_ease_out);
    kit_anim_from(I.hero, KA_OPA, 0, 255, 300, 60, lv_anim_path_ease_out);
    ak_button_focus(I.h_btn, true);
}

static void build_hero(void) {
    I.add_circle = kit_rect(I.root, 340, 340, LV_RADIUS_CIRCLE, 0xffffff, LV_OPA_10);
    lv_obj_set_style_border_width(I.add_circle, 4, 0);
    lv_obj_set_style_border_color(I.add_circle, lv_color_white(), 0);
    lv_obj_set_style_border_opa(I.add_circle, LV_OPA_40, 0);
    lv_obj_set_pos(I.add_circle, ART_CX - 170, 650 - 170);
    lv_obj_center(kit_label(I.add_circle, FONT_HERO, COL_TEXT, LV_SYMBOL_PLUS));
    I.hero = kit_obj(I.root);
    lv_obj_set_size(I.hero, 1300, 540);
    lv_obj_set_pos(I.hero, SAFE_X, HERO_Y);
    I.h_over = kit_label(I.hero, FONT_SMALL, COL_ACCENT_HI, "");
    lv_obj_set_style_text_letter_space(I.h_over, 3, 0);
    I.h_title = kit_label(I.hero, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(I.h_title, -2, 26);
    I.h_meta = kit_label(I.hero, FONT_BODY, COL_SUBTLE, "");
    lv_obj_set_pos(I.h_meta, 0, 100);
    I.h_btn = ak_button(I.hero, 480, 100, LV_SYMBOL_PLUS, "Add a console");
    lv_obj_set_pos(I.h_btn, 0, 176);
    lv_obj_t *row = lv_obj_get_child(I.h_btn, 0);
    I.h_btn_icon = lv_obj_get_child(row, 0);
    I.h_btn_lbl = lv_obj_get_child(row, 1);
}

/* ---------- the tile row ---------- */

static void ttl_set(const char *name, const char *sub) {
    lv_label_set_text(I.ttl_name, name);
    kit_roll_text(I.ttl_sub, sub, 1);
}

static void build_top(void) {
    lv_obj_t *tabs = kit_obj(I.root);
    lv_obj_set_pos(tabs, SAFE_X, SAFE_Y + 2);
    lv_obj_set_size(tabs, 600, 60);
    lv_obj_set_pos(kit_label(tabs, FONT_TITLE, COL_TEXT, "Consoles"), 0, 0);
    lv_obj_set_pos(kit_label(tabs, FONT_TITLE, COL_DIM, "Settings"), 204, 0);
    I.top = tabs;
    lv_obj_t *right = kit_obj(I.root);
    lv_obj_set_size(right, 300, 60);
    lv_obj_set_pos(right, SCR_W - SAFE_X - 300, SAFE_Y - 4);
    lv_obj_t *pad = kit_pad_art(right, 44);
    lv_obj_align(pad, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_t *clock = kit_label(right, FONT_TITLE, COL_TEXT, "21:47");
    lv_obj_align(clock, LV_ALIGN_RIGHT_MID, 0, 0);

    I.row = kit_obj(I.root);
    lv_obj_set_size(I.row, SCR_W, TILE_F);
    lv_obj_set_pos(I.row, 0, ROW_Y);
    ak_margin(I.row, 40); /* the focus ring stands 12 px outside the tiles */
    I.add_tile = tile(I.row, 0x1a2338, 0x101626, TILE_F);
    lv_obj_set_style_bg_opa(I.add_tile, LV_OPA_40, 0);
    lv_obj_set_style_border_width(I.add_tile, 3, 0);
    lv_obj_set_style_border_opa(I.add_tile, LV_OPA_30, 0);
    lv_obj_set_pos(I.add_tile, SAFE_X, 0);
    lv_obj_center(kit_label(I.add_tile, FONT_HERO, COL_TEXT, LV_SYMBOL_PLUS));
    lv_obj_set_style_outline_opa(I.add_tile, LV_OPA_COVER, 0);
    I.ttl = kit_obj(I.root);
    lv_obj_set_size(I.ttl, 430, 120);
    lv_obj_set_pos(I.ttl, NAME_X, ROW_Y + TILE_F / 2 - 56);
    I.ttl_name = kit_label(I.ttl, FONT_TITLE, COL_TEXT, "Add a console");
    I.ttl_sub = kit_label(I.ttl, FONT_CAPTION, COL_SUBTLE, "PIN or PSN account");
    lv_obj_set_pos(I.ttl_sub, 0, 46);
}

/* ---------- the flow ---------- */

static void on_step(ak_step_t s, int method) {
    (void)s;
    (void)method;
    tint(ak_tint());
    int n;
    const char *name;
    int i = ak_progress(&n, &name);
    char buf[64];
    snprintf(buf, sizeof(buf), "Step %d of %d" BULLET "%s", i, n, name);
    ttl_set("Add a console", buf);
}

/* in the flow the row and the tabs step back; the caption beside the Add tile carries the progress */
static void row_dim(bool dim) {
    kit_anim(I.row, KA_OPA, dim ? 150 : LV_OPA_COVER, 300, 0, lv_anim_path_ease_out);
    kit_anim(I.top, KA_OPA, dim ? 90 : LV_OPA_COVER, 300, 0, lv_anim_path_ease_out);
}

static void on_keyboard(bool open) {
    lv_obj_t *o[] = { I.row, I.ttl, I.top };
    static const lv_opa_t back[] = { 150, LV_OPA_COVER, 90 };
    for (size_t i = 0; i < sizeof(o) / sizeof(o[0]); i++)
        kit_anim(o[i], KA_OPA, open ? LV_OPA_TRANSP : back[i], 260, 0, lv_anim_path_ease_out);
}

static void stage_gone_cb(lv_obj_t *stage) { lv_obj_clean(stage); }

static void on_close(bool added) {
    kit_anim_then(I.stage, KA_OPA, 0, 220, lv_anim_path_ease_in, stage_gone_cb);
    row_dim(false);
    I.mode = added ? M_ADDED : M_HERO;
    if (added) { /* the console takes its place in the row; Add moves along */
        bool ps5 = ak_console_ps5();
        I.new_tile = tile(I.row, ps5 ? 0x2d528f : 0x3b4fa0, 0x0f1a33, TILE_F);
        lv_obj_set_pos(I.new_tile, SAFE_X, 0);
        lv_obj_center(kit_console_art_box(I.new_tile, ps5, TILE_F * 86 / 100, TILE_F * 80 / 100));
        lv_obj_set_style_outline_opa(I.new_tile, LV_OPA_COVER, 0);
        kit_anim_from(I.new_tile, KA_GROW, -60, 0, 640, 200, kit_path_spring);
        kit_anim_from(I.new_tile, KA_OPA, 0, 255, 300, 200, lv_anim_path_ease_out);
        lv_obj_set_style_outline_opa(I.add_tile, LV_OPA_TRANSP, 0);
        kit_anim(I.add_tile, KA_X, SAFE_X + TILE_F + 28 + 430, 420, 0, lv_anim_path_ease_out);
        kit_anim(I.add_tile, KA_W, TILE_S, 420, 0, lv_anim_path_ease_out);
        kit_anim(I.add_tile, KA_H, TILE_S, 420, 0, lv_anim_path_ease_out);
        ttl_set(ak_console_name(), "Local network");
    } else {
        ttl_set("Add a console", "PIN or PSN account");
    }
    hero_show(added);
}

static void flow_start(void) {
    I.mode = M_FLOW;
    lv_obj_add_flag(I.hero, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(I.add_circle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(I.stage);
    lv_anim_del(I.stage, NULL);
    lv_obj_set_style_opa(I.stage, LV_OPA_COVER, 0);
    const ak_layout_t lay = {
        .x = SAFE_X,
        .y = 470,
        .main_w = 940,
        .aside_x = 1040,
        .aside_y = -20,
        .aside_w = SCR_W - 2 * SAFE_X - 1040,
        .aside_h = 580,
        .method_cards = true,
        .title_font = FONT_HERO,
        .key = 100,
        .kb_parent = I.stage,
        .kb_x = (SCR_W - 1480) / 2,
        .kb_w = 1480,
        .kb_h = 420,
    };
    const ak_hooks_t hooks = { on_step, on_close, on_keyboard };
    ak_create(I.stage, &lay, &hooks);
    on_step(AK_METHOD, 0);
    row_dim(true);
    kit_anim_from(I.stage, KA_OPA, 0, 255, 320, 0, lv_anim_path_ease_out);
}

/* ---------- concept ---------- */

static void build(lv_obj_t *root) {
    memset(&I, 0, sizeof(I));
    I.root = root;
    I.bg = kit_rect(root, SCR_W, SCR_H, 0, AK_TINT_NEUTRAL, LV_OPA_COVER);
    kit_grad(I.bg, AK_TINT_NEUTRAL, COL_BG_BOTTOM);
    lv_obj_set_style_bg_main_stop(I.bg, 0, 0);
    lv_obj_set_style_bg_grad_stop(I.bg, 235, 0);
    I.glow = kit_glow(root, AK_TINT_NEUTRAL + 0x202020, LV_OPA_50);
    lv_obj_set_pos(I.glow, ART_CX - KIT_GLOW_D / 2, 650 - KIT_GLOW_D / 2);
    build_hero();
    build_top();
    lv_obj_t *hints = kit_hints(root);
    lv_obj_align(hints, LV_ALIGN_BOTTOM_RIGHT, -SAFE_X, -SAFE_Y + 14);
    kit_hint(hints, PS_CROSS, "Select");
    kit_hint(hints, PS_CIRCLE, "Back");
    I.stage = kit_obj(root);
    lv_obj_set_size(I.stage, SCR_W, SCR_H);
    hero_show(false);
}

static void nav(nav_t k) {
    switch (I.mode) {
        case M_HERO:
            if (k == NAV_OK) flow_start();
            break;
        case M_FLOW:
            ak_nav(k);
            if (I.mode == M_FLOW && ak_step() == AK_METHOD) tint(ak_tint());
            break;
        case M_ADDED: /* the demo starts over */
            if (k == NAV_BACK || k == NAV_OK) {
                lv_obj_t *root = I.root;
                lv_obj_clean(root);
                lv_anim_del_all();
                build(root);
            }
            break;
    }
}

#define K(k) S_KEY(k), S_WAIT(170)
/* clang-format off */
static const step_t g_script[] = {
    S_WAIT(900),
    S_SHOT("home", "Home, the Add tile focused: the hero offers to add a console"),
    S_REC("pin-flow", "Cross turns the hero into the flow; Home's row stays above it with the progress"),
    S_WAIT(400), S_KEY(NAV_OK), S_WAIT(1000),
    S_SHOT("method", "How: two cards in the hero, the picture where the key art stands"),
    S_KEY(NAV_RIGHT), S_WAIT(900), S_KEY(NAV_LEFT), S_WAIT(800), S_KEY(NAV_OK), S_WAIT(900),
    S_SHOT("kind", "Which console"),
    S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), S_WAIT(400),
    S_SHOT("address", "The address, with the pad in the key art's place"),
    K(NAV_LEFT), S_KEY(NAV_LEFT), S_WAIT(500), S_KEY(NAV_OK), S_WAIT(900),
    K(NAV_DOWN), K(NAV_OK), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), K(NAV_DOWN), K(NAV_OK),
    K(NAV_UP), K(NAV_UP), K(NAV_UP), K(NAV_LEFT), K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_OK),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_OK), K(NAV_LEFT), K(NAV_LEFT), K(NAV_OK),
    S_WAIT(600),
    S_SHOT("pin", "The PIN"),
    S_KEY(NAV_OK), S_WAIT(1500),
    S_SHOT("pairing", "Pairing"),
    S_WAIT(2300),
    S_SHOT("done", "Ready"),
    S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1300),
    S_SHOT("added", "Back on Home: the new console takes its place in the row, ready to play"),
    S_WAIT(600), S_REC_END,
    S_KEY(NAV_BACK), S_WAIT(900),
    S_REC("psn-flow", "PSN: the QR card where the key art stands; the keyboard rises, the flow moves up above it"),
    S_KEY(NAV_OK), S_WAIT(800), S_KEY(NAV_RIGHT), S_WAIT(700), S_KEY(NAV_OK), S_WAIT(1000),
    S_SHOT("psn", "Sign in with the phone"),
    S_KEY(NAV_OK), S_WAIT(700),
    K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_OK), K(NAV_RIGHT), K(NAV_OK), K(NAV_UP), K(NAV_OK),
    K(NAV_DOWN), K(NAV_DOWN), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT), K(NAV_RIGHT),
    S_WAIT(400),
    S_SHOT("psn-keyboard", "The code on the keyboard: the row steps aside so the field stays in view"),
    S_KEY(NAV_OK), S_WAIT(600), S_KEY(NAV_OK), S_WAIT(3000),
    S_SHOT("found", "The consoles on the account"),
    S_WAIT(800), S_REC_END,
    S_END,
};
/* clang-format on */

const concept_t concept_add_inline = {
    .id = "02-inline",
    .name = "Inline",
    .tagline = "The flow takes over Home's hero; the new console lands in the row",
    .notes = "<p><b>Idea.</b> Adding a console never leaves Home. The Add tile stays focused in the row (its "
             "caption shows the step), and the hero under it becomes the flow: the two ways in as cards, each "
             "step's text and control on the left, its picture, the number pad or the QR card where the key art "
             "stands. When it is done the console slides into the row and Home shows it with Start Remote "
             "Play.</p>"
             "<p><b>Same steps and controls</b> as Setup: tiles for the console kind, the address as four octets "
             "on this TV's network, eight PIN boxes, a number pad, the QR card, the restyled keyboard. While the "
             "keyboard is up the row steps aside and the flow rises above it.</p>"
             "<p><b>Trade-off.</b> The most \"part of Home\" of the three; the hero is shorter than a full screen, "
             "so text has to stay brief.</p>",
    .build = build,
    .nav = nav,
    .set_stream = NULL,
    .script = g_script,
    .sheet = { "method", "pin", "added" },
};
