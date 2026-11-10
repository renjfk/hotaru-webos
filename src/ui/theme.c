/* ui/theme.c - LVGL styles built from the tokens in theme.h.
 *
 * The LVGL default theme stays as the parent (fallback for widgets not
 * covered here); for every covered class theme_apply() wipes the parent's
 * styles and applies ours, so the look is fully defined by the tokens.
 * Focus/hover rings reach FOCUS_EXT outside a widget and button shadows
 * DECOR_EXT below it: cards are padded for both (see widgets.c ui_no_clip).
 */
#include "theme.h"

/* exported variants (theme.h) */
lv_style_t st_txt[TXT__COUNT];
lv_style_t st_screen, st_card, st_card_hero, st_tile, st_row, st_bar_bottom;
lv_style_t st_chip, st_badge, st_badge_soft, st_glyph, st_console;
lv_style_t st_btn_secondary, st_btn_danger, st_btn_ghost;

/* used by theme_apply() only */
static lv_style_t st_container, st_scrollbar, st_btn;
static lv_style_t st_pressed, st_hover, st_focus, st_disabled;
static lv_style_t st_list, st_list_btn, st_list_btn_checked;
static lv_style_t st_field, st_field_inset, st_field_active, st_ta_cursor, st_placeholder;
static lv_style_t st_ddlist, st_ddlist_sel, st_ddlist_sel_checked;
static lv_style_t st_track, st_indicator, st_knob, st_knob_focus, st_knob_hover, st_knob_pressed;
static lv_style_t st_switch_track, st_switch_ind, st_switch_knob;
static lv_style_t st_arc_track, st_arc_ind, st_led;
static lv_style_t st_tabbar, st_tabbar_track, st_tab_pill, st_tab, st_tab_checked, st_item_focus;
static lv_style_t st_btnm, st_btnm_item;
static lv_style_t st_kb, st_kb_key, st_kb_key_special, st_kb_key_pressed;
static lv_style_t st_popup;

static lv_style_transition_dsc_t g_trans;
static const lv_style_prop_t g_trans_props[] = {
    LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_COLOR_FILTER_OPA,
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, 0
};
static lv_color_filter_dsc_t g_filter_lighten, g_filter_darken;
static lv_color_t filter_lighten_cb(const lv_color_filter_dsc_t *d, lv_color_t c, lv_opa_t opa) {
    (void)d; return lv_color_lighten(c, opa);
}
static lv_color_t filter_darken_cb(const lv_color_filter_dsc_t *d, lv_color_t c, lv_opa_t opa) {
    (void)d; return lv_color_darken(c, opa);
}

static void style_bg(lv_style_t *s, uint32_t top, uint32_t bottom) {
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_bg_color(s, C(top));
    if (top != bottom) {
        lv_style_set_bg_grad_color(s, C(bottom));
        lv_style_set_bg_grad_dir(s, LV_GRAD_DIR_VER);
    }
}
static void style_border(lv_style_t *s, lv_coord_t w, uint32_t col) {
    lv_style_set_border_width(s, w);
    lv_style_set_border_color(s, C(col));
    lv_style_set_border_opa(s, LV_OPA_COVER);
}
static void style_text(lv_style_t *s, const lv_font_t *font, uint32_t col) {
    lv_style_set_text_font(s, font);
    lv_style_set_text_color(s, C(col));
}

static void theme_build(void) {
    lv_style_transition_dsc_init(&g_trans, g_trans_props, lv_anim_path_ease_out, TRANS_MS, 0, NULL);
    lv_color_filter_dsc_init(&g_filter_lighten, filter_lighten_cb);
    lv_color_filter_dsc_init(&g_filter_darken, filter_darken_cb);

    /* typography scale */
    static const struct { const lv_font_t *font; uint32_t col; lv_coord_t ls; } txt[TXT__COUNT] = {
        [TXT_DISPLAY]  = { FONT_DISPLAY, COL_TEXT, 0 },
        [TXT_TITLE]    = { FONT_TITLE, COL_TEXT, 0 },
        [TXT_HEADING]  = { FONT_HEADING, COL_TEXT, 0 },
        [TXT_BODY]     = { FONT_BODY, COL_TEXT, 0 },
        [TXT_CAPTION]  = { FONT_CAPTION, COL_MUTED, 0 },
        [TXT_OVERLINE] = { FONT_SMALL, COL_ACCENT_HI, 3 },
    };
    for (int i = 0; i < TXT__COUNT; i++) {
        lv_style_init(&st_txt[i]);
        style_text(&st_txt[i], txt[i].font, txt[i].col);
        lv_style_set_text_letter_space(&st_txt[i], txt[i].ls);
    }

    /* surfaces */
    lv_style_init(&st_screen);
    style_bg(&st_screen, COL_BG_TOP, COL_BG_BOTTOM);
    lv_style_set_bg_grad_stop(&st_screen, 190);
    style_text(&st_screen, FONT_BODY, COL_TEXT);

    lv_style_init(&st_container);
    lv_style_set_bg_opa(&st_container, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_container, 0);
    lv_style_set_pad_all(&st_container, 0);
    lv_style_set_pad_gap(&st_container, 0);
    lv_style_set_clip_corner(&st_container, false);

    lv_style_init(&st_scrollbar);
    lv_style_set_bg_opa(&st_scrollbar, LV_OPA_50);
    lv_style_set_bg_color(&st_scrollbar, C(COL_ACCENT_HI));
    lv_style_set_radius(&st_scrollbar, R_PILL);
    lv_style_set_width(&st_scrollbar, 6);
    lv_style_set_pad_right(&st_scrollbar, SP_S);
    lv_style_set_pad_top(&st_scrollbar, SP_S);
    lv_style_set_pad_bottom(&st_scrollbar, SP_S);

    lv_style_init(&st_card);
    style_bg(&st_card, COL_SURFACE_TOP, COL_SURFACE);
    style_border(&st_card, 1, COL_LINE);
    lv_style_set_radius(&st_card, R_LG);
    lv_style_set_clip_corner(&st_card, false);
    /* cards are the clipping boundary for focus rings and shadows of their controls */
    _Static_assert(SP_XL >= FOCUS_EXT && SP_XL >= DECOR_EXT, "card padding must contain rings and shadows");
    lv_style_set_pad_all(&st_card, SP_XL);
    lv_style_set_pad_gap(&st_card, SP_L);

    lv_style_init(&st_card_hero);
    style_bg(&st_card_hero, 0x1a4a92, COL_SURFACE);
    lv_style_set_bg_grad_stop(&st_card_hero, 170);
    style_border(&st_card_hero, 1, 0x2d5294);
    lv_style_set_pad_all(&st_card_hero, SP_2XL);

    lv_style_init(&st_tile);
    style_bg(&st_tile, COL_INSET, COL_INSET);
    lv_style_set_bg_opa(&st_tile, LV_OPA_60);
    style_border(&st_tile, 1, COL_LINE);
    lv_style_set_radius(&st_tile, R_MD);
    lv_style_set_pad_hor(&st_tile, SP_XL);
    lv_style_set_pad_ver(&st_tile, SP_L);
    lv_style_set_pad_gap(&st_tile, SP_XS);

    lv_style_init(&st_row);
    lv_style_set_bg_opa(&st_row, LV_OPA_TRANSP);
    lv_style_set_border_side(&st_row, LV_BORDER_SIDE_BOTTOM);
    style_border(&st_row, 1, COL_DIVIDER);
    lv_style_set_pad_hor(&st_row, 0);
    lv_style_set_pad_ver(&st_row, SP_S);
    lv_style_set_pad_column(&st_row, SP_XL);
    lv_style_set_min_height(&st_row, ROW_H);

    lv_style_init(&st_bar_bottom);
    style_bg(&st_bar_bottom, COL_SURFACE_TOP, COL_SURFACE);
    lv_style_set_border_side(&st_bar_bottom, LV_BORDER_SIDE_TOP);
    style_border(&st_bar_bottom, 1, COL_LINE);

    lv_style_init(&st_chip);
    style_bg(&st_chip, COL_RAISED, COL_RAISED);
    style_border(&st_chip, 1, COL_LINE);
    lv_style_set_radius(&st_chip, R_PILL);
    lv_style_set_pad_hor(&st_chip, SP_L);
    lv_style_set_pad_ver(&st_chip, SP_S);
    lv_style_set_pad_column(&st_chip, SP_S);
    style_text(&st_chip, FONT_CAPTION, COL_TEXT);

    lv_style_init(&st_badge);
    style_bg(&st_badge, COL_ACCENT_HI, COL_ACCENT_DEEP);
    lv_style_set_radius(&st_badge, R_MD);
    style_text(&st_badge, FONT_CAPTION, 0xffffff);

    lv_style_init(&st_badge_soft);
    style_bg(&st_badge_soft, COL_ACCENT_TINT, COL_ACCENT_TINT);
    style_border(&st_badge_soft, 1, 0x234780);
    lv_style_set_radius(&st_badge_soft, R_MD);
    style_text(&st_badge_soft, FONT_BODY, COL_ACCENT_HI);

    lv_style_init(&st_glyph);
    style_bg(&st_glyph, COL_RAISED, COL_RAISED);
    style_border(&st_glyph, 2, COL_LINE);
    lv_style_set_radius(&st_glyph, R_PILL);
    style_text(&st_glyph, FONT_SMALL, COL_TEXT);

    lv_style_init(&st_console);
    style_bg(&st_console, COL_INSET, COL_INSET);
    style_border(&st_console, 1, COL_LINE);
    lv_style_set_radius(&st_console, R_MD);
    lv_style_set_pad_hor(&st_console, SP_XL);
    lv_style_set_pad_ver(&st_console, SP_L);
    style_text(&st_console, FONT_BODY, COL_TEXT);
    lv_style_set_text_line_space(&st_console, SP_S);

    /* buttons: primary is the base, variants override it */
    lv_style_init(&st_btn);
    style_bg(&st_btn, COL_ACCENT_HI, COL_ACCENT);
    lv_style_set_radius(&st_btn, R_MD);
    lv_style_set_border_width(&st_btn, 0);
    lv_style_set_pad_hor(&st_btn, SP_2XL);
    lv_style_set_pad_ver(&st_btn, SP_L);
    lv_style_set_pad_column(&st_btn, SP_M);
    style_text(&st_btn, FONT_BODY, 0xffffff);
    lv_style_set_shadow_width(&st_btn, BTN_SHADOW_W);
    lv_style_set_shadow_ofs_y(&st_btn, BTN_SHADOW_Y);
    lv_style_set_shadow_color(&st_btn, C(COL_ACCENT));
    lv_style_set_shadow_opa(&st_btn, LV_OPA_40);
    lv_style_set_transition(&st_btn, &g_trans);

    lv_style_init(&st_btn_secondary);
    style_bg(&st_btn_secondary, COL_RAISED_HI, COL_RAISED);
    style_border(&st_btn_secondary, 1, COL_LINE);
    lv_style_set_text_color(&st_btn_secondary, C(COL_TEXT));
    lv_style_set_shadow_opa(&st_btn_secondary, LV_OPA_TRANSP);

    lv_style_init(&st_btn_danger);
    style_bg(&st_btn_danger, COL_DANGER_BG, 0x2a1019);
    style_border(&st_btn_danger, 1, COL_DANGER_LINE);
    lv_style_set_text_color(&st_btn_danger, C(COL_DANGER_TEXT));
    lv_style_set_shadow_opa(&st_btn_danger, LV_OPA_TRANSP);

    lv_style_init(&st_btn_ghost);
    lv_style_set_bg_opa(&st_btn_ghost, LV_OPA_TRANSP);
    style_border(&st_btn_ghost, 2, COL_LINE);
    lv_style_set_text_color(&st_btn_ghost, C(COL_MUTED));
    lv_style_set_shadow_opa(&st_btn_ghost, LV_OPA_TRANSP);

    /* interaction states shared by every focusable widget */
    lv_style_init(&st_pressed);
    lv_style_set_color_filter_dsc(&st_pressed, &g_filter_darken);
    lv_style_set_color_filter_opa(&st_pressed, LV_OPA_20);
    lv_style_set_transform_width(&st_pressed, -3);
    lv_style_set_transform_height(&st_pressed, -3);
    lv_style_set_transition(&st_pressed, &g_trans);

    lv_style_init(&st_hover);
    lv_style_set_color_filter_dsc(&st_hover, &g_filter_lighten);
    lv_style_set_color_filter_opa(&st_hover, LV_OPA_10);
    lv_style_set_outline_width(&st_hover, 2);
    lv_style_set_outline_color(&st_hover, C(COL_ACCENT_HI));
    lv_style_set_outline_opa(&st_hover, LV_OPA_COVER);
    lv_style_set_outline_pad(&st_hover, FOCUS_PAD);
    lv_style_set_transition(&st_hover, &g_trans);

    lv_style_init(&st_focus);
    lv_style_set_outline_width(&st_focus, FOCUS_W);
    lv_style_set_outline_color(&st_focus, C(COL_FOCUS));
    lv_style_set_outline_opa(&st_focus, LV_OPA_COVER);
    lv_style_set_outline_pad(&st_focus, FOCUS_PAD);

    lv_style_init(&st_disabled);
    lv_style_set_color_filter_dsc(&st_disabled, &g_filter_darken);
    lv_style_set_color_filter_opa(&st_disabled, LV_OPA_40);
    lv_style_set_text_opa(&st_disabled, LV_OPA_50);
    lv_style_set_shadow_opa(&st_disabled, LV_OPA_TRANSP);

    /* list */
    lv_style_init(&st_list);
    lv_style_set_bg_opa(&st_list, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_list, 0);
    lv_style_set_pad_all(&st_list, SP_S);
    lv_style_set_pad_row(&st_list, SP_M);

    lv_style_init(&st_list_btn);
    style_bg(&st_list_btn, COL_RAISED, COL_RAISED);
    style_border(&st_list_btn, 2, COL_LINE);
    lv_style_set_radius(&st_list_btn, R_MD);
    lv_style_set_pad_hor(&st_list_btn, SP_L);
    lv_style_set_pad_ver(&st_list_btn, SP_M);
    lv_style_set_pad_column(&st_list_btn, SP_L);
    style_text(&st_list_btn, FONT_BODY, COL_TEXT);
    lv_style_set_transition(&st_list_btn, &g_trans);

    lv_style_init(&st_list_btn_checked);
    style_bg(&st_list_btn_checked, 0x1a3a70, COL_ACCENT_TINT);
    lv_style_set_border_color(&st_list_btn_checked, C(COL_ACCENT_HI));

    /* text fields and dropdowns */
    lv_style_init(&st_field);
    style_bg(&st_field, COL_RAISED, COL_RAISED);
    style_border(&st_field, 2, COL_LINE);
    lv_style_set_radius(&st_field, R_MD);
    lv_style_set_pad_hor(&st_field, SP_XL);
    lv_style_set_pad_ver(&st_field, SP_M + 2);
    style_text(&st_field, FONT_BODY, COL_TEXT);
    lv_style_set_transition(&st_field, &g_trans);

    lv_style_init(&st_field_inset);
    style_bg(&st_field_inset, COL_INSET, COL_INSET);
    style_border(&st_field_inset, 2, COL_LINE);
    lv_style_set_radius(&st_field_inset, R_MD);
    lv_style_set_pad_hor(&st_field_inset, SP_XL);
    lv_style_set_pad_ver(&st_field_inset, SP_M + 2);
    style_text(&st_field_inset, FONT_BODY, COL_TEXT);
    lv_style_set_transition(&st_field_inset, &g_trans);

    lv_style_init(&st_field_active);
    lv_style_set_border_color(&st_field_active, C(COL_ACCENT_HI));
    lv_style_set_transition(&st_field_active, &g_trans);

    lv_style_init(&st_ta_cursor);
    lv_style_set_border_color(&st_ta_cursor, C(COL_ACCENT_HI));
    lv_style_set_border_width(&st_ta_cursor, 3);
    lv_style_set_border_side(&st_ta_cursor, LV_BORDER_SIDE_LEFT);
    lv_style_set_pad_left(&st_ta_cursor, -1);
    lv_style_set_anim_time(&st_ta_cursor, 500);

    lv_style_init(&st_placeholder);
    lv_style_set_text_color(&st_placeholder, C(COL_DIM));

    lv_style_init(&st_ddlist);
    style_bg(&st_ddlist, COL_RAISED, COL_RAISED);
    lv_style_set_border_width(&st_ddlist, 0);
    lv_style_set_outline_width(&st_ddlist, 1);
    lv_style_set_outline_color(&st_ddlist, C(COL_LINE));
    lv_style_set_outline_opa(&st_ddlist, LV_OPA_COVER);
    lv_style_set_radius(&st_ddlist, R_MD);
    lv_style_set_pad_hor(&st_ddlist, SP_XL);
    lv_style_set_pad_ver(&st_ddlist, SP_M);    /* = line_space / 2 so the first highlight fits */
    lv_style_set_text_line_space(&st_ddlist, SP_XL);
    style_text(&st_ddlist, FONT_BODY, COL_TEXT);
    lv_style_set_max_height(&st_ddlist, 440);
    lv_style_set_translate_y(&st_ddlist, SP_S);   /* float slightly below the field */
    lv_style_set_shadow_width(&st_ddlist, 40);
    lv_style_set_shadow_ofs_y(&st_ddlist, 12);
    lv_style_set_shadow_color(&st_ddlist, lv_color_black());
    lv_style_set_shadow_opa(&st_ddlist, LV_OPA_60);

    lv_style_init(&st_ddlist_sel);
    style_bg(&st_ddlist_sel, COL_RAISED_HI, COL_RAISED_HI);
    lv_style_set_radius(&st_ddlist_sel, R_MD);

    lv_style_init(&st_ddlist_sel_checked);
    style_bg(&st_ddlist_sel_checked, COL_ACCENT_HI, COL_ACCENT);
    lv_style_set_text_color(&st_ddlist_sel_checked, lv_color_white());

    /* slider / bar / switch */
    lv_style_init(&st_track);
    style_bg(&st_track, COL_TRACK, COL_TRACK);
    lv_style_set_radius(&st_track, R_PILL);

    lv_style_init(&st_indicator);
    lv_style_set_bg_opa(&st_indicator, LV_OPA_COVER);
    lv_style_set_bg_color(&st_indicator, C(COL_ACCENT));
    lv_style_set_bg_grad_color(&st_indicator, C(COL_ACCENT_HI));
    lv_style_set_bg_grad_dir(&st_indicator, LV_GRAD_DIR_HOR);
    lv_style_set_radius(&st_indicator, R_PILL);
    lv_style_set_transition(&st_indicator, &g_trans);

    lv_style_init(&st_knob);
    style_bg(&st_knob, 0xffffff, 0xd9e2f2);
    lv_style_set_radius(&st_knob, R_PILL);
    lv_style_set_pad_all(&st_knob, 9);
    lv_style_set_shadow_width(&st_knob, 12);
    lv_style_set_shadow_ofs_y(&st_knob, 3);
    lv_style_set_shadow_color(&st_knob, lv_color_black());
    lv_style_set_shadow_opa(&st_knob, LV_OPA_50);
    lv_style_set_transition(&st_knob, &g_trans);

    lv_style_init(&st_knob_focus);
    lv_style_set_outline_width(&st_knob_focus, FOCUS_W);
    lv_style_set_outline_color(&st_knob_focus, C(COL_FOCUS));
    lv_style_set_outline_opa(&st_knob_focus, LV_OPA_COVER);
    lv_style_set_outline_pad(&st_knob_focus, FOCUS_PAD);

    lv_style_init(&st_knob_hover);
    lv_style_set_outline_width(&st_knob_hover, 2);
    lv_style_set_outline_color(&st_knob_hover, C(COL_ACCENT_HI));
    lv_style_set_outline_opa(&st_knob_hover, LV_OPA_COVER);
    lv_style_set_outline_pad(&st_knob_hover, FOCUS_PAD);

    lv_style_init(&st_knob_pressed);
    lv_style_set_transform_width(&st_knob_pressed, 4);
    lv_style_set_transform_height(&st_knob_pressed, 4);
    lv_style_set_transition(&st_knob_pressed, &g_trans);

    lv_style_init(&st_switch_track);
    style_bg(&st_switch_track, COL_TRACK, COL_TRACK);
    lv_style_set_radius(&st_switch_track, R_PILL);
    lv_style_set_transition(&st_switch_track, &g_trans);

    lv_style_init(&st_switch_ind);
    lv_style_set_bg_opa(&st_switch_ind, LV_OPA_TRANSP);
    lv_style_set_radius(&st_switch_ind, R_PILL);

    lv_style_init(&st_switch_knob);
    style_bg(&st_switch_knob, 0xffffff, 0xd9e2f2);
    lv_style_set_radius(&st_switch_knob, R_PILL);
    lv_style_set_pad_all(&st_switch_knob, -5);

    /* spinner / arc */
    lv_style_init(&st_arc_track);
    lv_style_set_arc_color(&st_arc_track, C(COL_TRACK));
    lv_style_set_arc_width(&st_arc_track, 6);
    lv_style_set_arc_rounded(&st_arc_track, true);

    lv_style_init(&st_arc_ind);
    lv_style_set_arc_color(&st_arc_ind, C(COL_ACCENT_HI));
    lv_style_set_arc_width(&st_arc_ind, 6);
    lv_style_set_arc_rounded(&st_arc_ind, true);

    /* status LED: lv_led tints these white values with its own colour */
    lv_style_init(&st_led);
    style_bg(&st_led, 0xffffff, 0xbdbdbd);
    lv_style_set_radius(&st_led, R_PILL);
    lv_style_set_shadow_width(&st_led, 16);
    lv_style_set_shadow_spread(&st_led, 2);
    lv_style_set_shadow_color(&st_led, lv_color_white());

    /* tab bar: segmented pill control */
    /* MAIN only lays the tabs out; the track (LV_PART_CUSTOM_FIRST) and the
     * sliding active pill (LV_PART_INDICATOR) are drawn by tabbar_draw_cb */
    lv_style_init(&st_tabbar);
    lv_style_set_bg_opa(&st_tabbar, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_tabbar, 0);
    lv_style_set_pad_all(&st_tabbar, 6);
    lv_style_set_pad_gap(&st_tabbar, 6);
    style_text(&st_tabbar, FONT_BODY, COL_MUTED);

    lv_style_init(&st_tabbar_track);
    style_bg(&st_tabbar_track, COL_SURFACE_TOP, COL_SURFACE);
    style_border(&st_tabbar_track, 1, COL_LINE);
    lv_style_set_radius(&st_tabbar_track, R_PILL);

    lv_style_init(&st_tab_pill);
    style_bg(&st_tab_pill, COL_ACCENT_HI, COL_ACCENT);
    lv_style_set_radius(&st_tab_pill, R_PILL);

    lv_style_init(&st_tab);
    lv_style_set_bg_opa(&st_tab, LV_OPA_TRANSP);
    lv_style_set_radius(&st_tab, R_PILL);
    lv_style_set_text_color(&st_tab, C(COL_MUTED));

    lv_style_init(&st_tab_checked);
    style_bg(&st_tab_checked, COL_ACCENT_HI, COL_ACCENT);
    lv_style_set_text_color(&st_tab_checked, lv_color_white());

    lv_style_init(&st_item_focus);
    style_border(&st_item_focus, FOCUS_W, COL_FOCUS);

    lv_style_init(&st_btnm);
    lv_style_set_bg_opa(&st_btnm, LV_OPA_TRANSP);
    lv_style_set_border_width(&st_btnm, 0);
    lv_style_set_pad_all(&st_btnm, 0);
    lv_style_set_pad_gap(&st_btnm, SP_M);

    lv_style_init(&st_btnm_item);
    style_bg(&st_btnm_item, COL_RAISED_HI, COL_RAISED);
    style_border(&st_btnm_item, 1, COL_LINE);
    lv_style_set_radius(&st_btnm_item, R_MD);
    style_text(&st_btnm_item, FONT_BODY, COL_TEXT);

    /* on-screen keyboard: bottom sheet of rounded key caps */
    lv_style_init(&st_kb);
    style_bg(&st_kb, COL_SURFACE_TOP, COL_SURFACE);
    style_border(&st_kb, 1, COL_LINE);
    lv_style_set_radius(&st_kb, R_LG);
    lv_style_set_pad_all(&st_kb, SP_XL);
    lv_style_set_pad_bottom(&st_kb, SP_XL + R_LG);
    lv_style_set_pad_gap(&st_kb, SP_S + 2);
    lv_style_set_shadow_width(&st_kb, 60);
    lv_style_set_shadow_ofs_y(&st_kb, -10);
    lv_style_set_shadow_color(&st_kb, lv_color_black());
    lv_style_set_shadow_opa(&st_kb, LV_OPA_70);

    lv_style_init(&st_kb_key);
    style_bg(&st_kb_key, COL_RAISED_HI, COL_RAISED);
    lv_style_set_radius(&st_kb_key, R_SM + 2);
    lv_style_set_border_width(&st_kb_key, 0);
    style_text(&st_kb_key, FONT_HEADING, COL_TEXT);

    lv_style_init(&st_kb_key_special);
    style_bg(&st_kb_key_special, 0x1d3866, COL_ACCENT_TINT);
    lv_style_set_text_color(&st_kb_key_special, C(COL_ACCENT_HI));

    lv_style_init(&st_kb_key_pressed);
    style_bg(&st_kb_key_pressed, COL_ACCENT_HI, COL_ACCENT);
    lv_style_set_text_color(&st_kb_key_pressed, lv_color_white());

    lv_style_init(&st_popup);
    style_bg(&st_popup, COL_RAISED, COL_SURFACE);
    style_border(&st_popup, 1, COL_LINE);
    lv_style_set_radius(&st_popup, R_LG);
    lv_style_set_pad_all(&st_popup, SP_XL);
    lv_style_set_pad_gap(&st_popup, SP_L);
    lv_style_set_shadow_width(&st_popup, 60);
    lv_style_set_shadow_ofs_y(&st_popup, 16);
    lv_style_set_shadow_color(&st_popup, lv_color_black());
    lv_style_set_shadow_opa(&st_popup, LV_OPA_70);
}

static void add_interaction(lv_obj_t *obj, lv_style_selector_t part) {
    lv_obj_add_style(obj, &st_pressed, part | LV_STATE_PRESSED);
    lv_obj_add_style(obj, &st_hover, part | LV_STATE_HOVERED);
    lv_obj_add_style(obj, &st_focus, part | LV_STATE_FOCUS_KEY);
    lv_obj_add_style(obj, &st_disabled, part | LV_STATE_DISABLED);
}

static void theme_apply(lv_theme_t *th, lv_obj_t *obj) {
    (void)th;
    lv_obj_t *parent = lv_obj_get_parent(obj);

    if (parent == NULL) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_screen, 0);
    } else if (lv_obj_check_type(obj, &lv_obj_class) || lv_obj_check_type(obj, &lv_tabview_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_container, 0);
        lv_obj_add_style(obj, &st_scrollbar, LV_PART_SCROLLBAR);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    } else if (lv_obj_check_type(obj, &lv_btn_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_btn, 0);
        add_interaction(obj, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_list_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_list, 0);
        lv_obj_add_style(obj, &st_scrollbar, LV_PART_SCROLLBAR);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    } else if (lv_obj_check_type(obj, &lv_list_btn_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_list_btn, 0);
        lv_obj_add_style(obj, &st_list_btn_checked, LV_STATE_CHECKED);
        add_interaction(obj, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_list_text_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_txt[TXT_OVERLINE], 0);
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_field_inset, 0);
        lv_obj_add_style(obj, &st_field_active, LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &st_hover, LV_STATE_HOVERED);
        lv_obj_add_style(obj, &st_focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &st_disabled, LV_STATE_DISABLED);
        lv_obj_add_style(obj, &st_scrollbar, LV_PART_SCROLLBAR);
        lv_obj_add_style(obj, &st_ta_cursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_add_style(obj, &st_placeholder, LV_PART_TEXTAREA_PLACEHOLDER);
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_field, 0);
        lv_obj_add_style(obj, &st_field_active, LV_STATE_CHECKED);
        add_interaction(obj, LV_PART_MAIN);
    } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_ddlist, 0);
        lv_obj_add_style(obj, &st_ddlist_sel, LV_PART_SELECTED);
        lv_obj_add_style(obj, &st_ddlist_sel_checked, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &st_scrollbar, LV_PART_SCROLLBAR);
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_track, 0);
        lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &st_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &st_knob_focus, LV_PART_KNOB | LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &st_knob_hover, LV_PART_KNOB | LV_STATE_HOVERED);
        lv_obj_add_style(obj, &st_knob_pressed, LV_PART_KNOB | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &st_disabled, LV_STATE_DISABLED);
    } else if (lv_obj_check_type(obj, &lv_bar_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_track, 0);
        lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR);
    } else if (lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_switch_track, 0);
        lv_obj_add_style(obj, &st_switch_ind, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &st_switch_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &st_hover, LV_STATE_HOVERED);
        lv_obj_add_style(obj, &st_focus, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &st_disabled, LV_STATE_DISABLED);
    } else if (lv_obj_has_class(obj, &lv_arc_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_arc_track, 0);
        lv_obj_add_style(obj, &st_arc_ind, LV_PART_INDICATOR);
        if (lv_obj_check_type(obj, &lv_arc_class)) {   /* spinners have no knob */
            lv_obj_add_style(obj, &st_knob, LV_PART_KNOB);
            lv_obj_add_style(obj, &st_knob_focus, LV_PART_KNOB | LV_STATE_FOCUS_KEY);
            lv_obj_add_style(obj, &st_knob_hover, LV_PART_KNOB | LV_STATE_HOVERED);
        }
    } else if (lv_obj_check_type(obj, &lv_led_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_led, 0);
    } else if (lv_obj_check_type(obj, &lv_keyboard_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_kb, 0);
        lv_obj_add_style(obj, &st_kb_key, LV_PART_ITEMS);
        lv_obj_add_style(obj, &st_kb_key_special, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &st_kb_key_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &st_item_focus, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    } else if (lv_obj_check_type(obj, &lv_btnmatrix_class)) {
        lv_obj_remove_style_all(obj);
        if (lv_obj_check_type(parent, &lv_tabview_class)) {
            lv_obj_add_style(obj, &st_tabbar, 0);
            lv_obj_add_style(obj, &st_tabbar_track, LV_PART_CUSTOM_FIRST);
            lv_obj_add_style(obj, &st_tab_pill, LV_PART_INDICATOR);
            lv_obj_add_style(obj, &st_tab, LV_PART_ITEMS);
        } else {
            lv_obj_add_style(obj, &st_btnm, 0);
            lv_obj_add_style(obj, &st_btnm_item, LV_PART_ITEMS);
            lv_obj_add_style(obj, &st_tab_checked, LV_PART_ITEMS | LV_STATE_CHECKED);
        }
        lv_obj_add_style(obj, &st_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
        lv_obj_add_style(obj, &st_item_focus, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    } else if (lv_obj_check_type(obj, &lv_msgbox_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_popup, 0);
    }
}

static lv_theme_t g_theme;
void theme_init(lv_disp_t *disp) {
    theme_build();
    lv_theme_t *base = lv_theme_default_init(disp, C(COL_ACCENT), C(COL_ACCENT_HI), true, FONT_BODY);
    g_theme = *base;
    lv_theme_set_parent(&g_theme, base);
    lv_theme_set_apply_cb(&g_theme, theme_apply);
    lv_disp_set_theme(disp, &g_theme);
}
