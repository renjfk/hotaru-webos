/* ui/theme.c - LVGL styles built from the tokens in theme.h.
 *
 * The screens draw their own controls (ui/controls.c), so the theme only covers what LVGL creates
 * plainly: the screen, containers (transparent, no padding, a slim scrollbar when they scroll) and
 * the splash's spinner. The LVGL default theme stays as the parent for anything else.
 */
#include "theme.h"

/* exported variants (theme.h) */
lv_style_t st_txt[TXT__COUNT];
lv_style_t st_screen, st_console;

/* used by theme_apply() only */
static lv_style_t st_container, st_scrollbar, st_arc_track, st_arc_ind;

static void style_bg(lv_style_t *s, uint32_t top, uint32_t bottom) {
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_bg_color(s, C(top));
    if (top != bottom) {
        lv_style_set_bg_grad_color(s, C(bottom));
        lv_style_set_bg_grad_dir(s, LV_GRAD_DIR_VER);
    }
}

static void style_text(lv_style_t *s, const lv_font_t *font, uint32_t col) {
    lv_style_set_text_font(s, font);
    lv_style_set_text_color(s, C(col));
}

static void style_arc(lv_style_t *s, uint32_t col) {
    lv_style_init(s);
    lv_style_set_arc_color(s, C(col));
    lv_style_set_arc_width(s, 6);
    lv_style_set_arc_rounded(s, true);
}

static void theme_build(void) {
    /* typography scale */
    static const struct {
        const lv_font_t *font;
        uint32_t col;
        lv_coord_t ls;
    } txt[TXT__COUNT] = {
        [TXT_DISPLAY] = { FONT_DISPLAY, COL_TEXT, 0 },  [TXT_TITLE] = { FONT_TITLE, COL_TEXT, 0 },
        [TXT_HEADING] = { FONT_HEADING, COL_TEXT, 0 },  [TXT_BODY] = { FONT_BODY, COL_TEXT, 0 },
        [TXT_CAPTION] = { FONT_CAPTION, COL_MUTED, 0 }, [TXT_OVERLINE] = { FONT_SMALL, COL_ACCENT_HI, 3 },
    };
    for (int i = 0; i < TXT__COUNT; i++) {
        lv_style_init(&st_txt[i]);
        style_text(&st_txt[i], txt[i].font, txt[i].col);
        lv_style_set_text_letter_space(&st_txt[i], txt[i].ls);
    }

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

    /* the Log panel's console */
    lv_style_init(&st_console);
    style_bg(&st_console, COL_INSET, COL_INSET);
    lv_style_set_border_width(&st_console, 1);
    lv_style_set_border_color(&st_console, C(COL_LINE));
    lv_style_set_radius(&st_console, R_MD);
    lv_style_set_pad_hor(&st_console, SP_XL);
    lv_style_set_pad_ver(&st_console, SP_L);
    style_text(&st_console, FONT_BODY, COL_TEXT);
    lv_style_set_text_line_space(&st_console, SP_S);

    style_arc(&st_arc_track, COL_TRACK);
    style_arc(&st_arc_ind, COL_ACCENT_HI);
}

/* LVGL 8.3 decides whether an object covers an area by its own opacity alone. Under a fading parent
 * (style opa below cover) an opaque child still claims to, the refresh starts drawing from it and skips
 * everything behind, and the stale draw buffer shows through the fade as coloured static. */
static void cover_check_cb(lv_event_t *e) {
    lv_cover_check_info_t *info = lv_event_get_param(e);
    if (info->res != LV_COVER_RES_COVER) return;
    for (lv_obj_t *p = lv_obj_get_parent(lv_event_get_target(e)); p; p = lv_obj_get_parent(p)) {
        if (lv_obj_get_style_opa(p, LV_PART_MAIN) < LV_OPA_MAX) {
            info->res = LV_COVER_RES_NOT_COVER;
            return;
        }
    }
}

static void theme_apply(lv_theme_t *th, lv_obj_t *obj) {
    (void)th;
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_screen, 0);
    } else if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_container, 0);
        lv_obj_add_style(obj, &st_scrollbar, LV_PART_SCROLLBAR);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(obj, cover_check_cb, LV_EVENT_COVER_CHECK, NULL);
    } else if (lv_obj_has_class(obj, &lv_arc_class)) { /* the splash's spinner */
        lv_obj_remove_style_all(obj);
        lv_obj_add_style(obj, &st_arc_track, 0);
        lv_obj_add_style(obj, &st_arc_ind, LV_PART_INDICATOR);
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
