/*
 * chiaki-webos - a chiaki-ng port for LG webOS.
 *
 * Dummy UI exercising the widgets a PS Remote Play client needs: tabview,
 * list (hosts), dropdowns, sliders, switches, text areas, on-screen keyboard,
 * spinner, progress bar, buttons and a log view, all styled by one
 * console-like theme (design tokens + theme_apply) meant to be reused.
 *
 * Input: left stick moves a pointer cursor (Cross clicks); the TV remote still
 * drives an LVGL keypad indev in parallel. Whatever the cursor points at gets
 * LV_STATE_HOVERED so it lights up like a focused tile.
 *
 * Display: plain streaming SDL_Texture sized to the renderer output (works
 * with the TV's opengles2 renderer and avoids letterboxing).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <SDL.h>
#include <lvgl.h>

#define LOG_PATH "/tmp/chiaki.log"
static FILE *g_log;
static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

/* ======================================================================
 * Design tokens
 * ==================================================================== */

/* palette: deep navy surfaces, PlayStation blue accent */
#define COL_BG_TOP       0x0f1f40
#define COL_BG_BOTTOM    0x04060b
#define COL_SURFACE_TOP  0x172238   /* cards: vertical gradient top */
#define COL_SURFACE      0x101726   /* cards: vertical gradient bottom */
#define COL_RAISED       0x1b2640   /* list items, fields, secondary buttons */
#define COL_RAISED_HI    0x283759   /* hovered option, special keys */
#define COL_INSET        0x080d17   /* text inputs, log console */
#define COL_TRACK        0x222e4b   /* slider/bar/switch tracks */
#define COL_LINE         0x2a3756   /* borders */
#define COL_DIVIDER      0x1c2640   /* row separators */
#define COL_ACCENT       0x0070d1   /* PlayStation blue */
#define COL_ACCENT_HI    0x3592ff
#define COL_ACCENT_DEEP  0x004496
#define COL_ACCENT_TINT  0x14305d   /* selected item background */
#define COL_TEXT         0xeef2f8
#define COL_MUTED        0x8f9bb4
#define COL_DIM          0x5c6884
#define COL_OK           0x2fd47a
#define COL_WARN         0xffb21e
#define COL_ERR          0xff5468
#define COL_INFO         0x58c6ff
#define COL_FOCUS        0xffffff
#define COL_DANGER_BG    0x3a1622
#define COL_DANGER_LINE  0x6e2638
#define COL_DANGER_TEXT  0xff8f9d
#define COL_PS_CROSS     0x9dbcff   /* button hint glyph */
#define COL_DECO_TRI     0x2a809d   /* face-button decoration, pre-blended onto the hero card */
#define COL_DECO_CIRCLE  0x7f537f
#define COL_DECO_CROSS   0x4d74b9
#define COL_DECO_SQUARE  0x7963a8

/* spacing: 4px base, 8px rhythm */
#define SP_XS   4
#define SP_S    8
#define SP_M    12
#define SP_L    16
#define SP_XL   24
#define SP_2XL  32
#define SP_3XL  48

/* corner radii */
#define R_SM    10
#define R_MD    16
#define R_LG    24
#define R_PILL  LV_RADIUS_CIRCLE

/* layout (1920x1080 reference; everything fits without scrolling) */
#define PAGE_MARGIN  48
#define TOPBAR_H     96
#define BOTBAR_H     104
#define TABBAR_H     64
#define TABBAR_W     860
#define ROW_H        104
#define CTRL_W       440
#define SLIDER_VAL_W 130
#define TILE_SM      48
#define TILE_MD      56
#define FOCUS_W      3
#define FOCUS_PAD    3
#define FOCUS_EXT    (FOCUS_W + FOCUS_PAD)   /* how far a focus/hover ring reaches outside a widget */
#define BTN_SHADOW_W 24
#define BTN_SHADOW_Y 6
#define DECOR_EXT    (BTN_SHADOW_W / 2 + BTN_SHADOW_Y)  /* deepest decoration: button shadow below */
#define SLIDER_HIT   SP_XL   /* extra click area around sliders: forgiving for a stick cursor */
#define CURSOR_D     34
#define KB_W         1480
#define KB_H         470
#define DECO_SIZE    84            /* faint PlayStation face-button diamond */
#define DECO_GAP     118
#define DECO_W       7

/* motion */
#define TRANS_MS     120

/* typography */
#define FONT_DISPLAY (&lv_font_montserrat_40)
#define FONT_TITLE   (&lv_font_montserrat_32)
#define FONT_HEADING (&lv_font_montserrat_28)
#define FONT_BODY    (&lv_font_montserrat_24)
#define FONT_CAPTION (&lv_font_montserrat_20)
#define FONT_SMALL   (&lv_font_montserrat_16)

typedef enum { TXT_DISPLAY, TXT_TITLE, TXT_HEADING, TXT_BODY, TXT_CAPTION, TXT_OVERLINE, TXT__COUNT } txt_kind_t;
typedef enum { BTN_PRIMARY, BTN_SECONDARY, BTN_DANGER, BTN_GHOST } btn_kind_t;

#define C(hex) lv_color_hex(hex)
#define BULLET "  " LV_SYMBOL_BULLET "  "

static bool running = true;
static lv_obj_t *g_kb, *g_tv;
static lv_group_t *g_group;
static lv_indev_t *g_ptr_indev;
enum { TAB_HOSTS, TAB_VIDEO, TAB_INPUT, TAB_LOG, TAB__COUNT };
static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static int g_w = 1920, g_h = 1080;
static lv_disp_t *g_disp;

/* ---------- display: LVGL -> streaming texture ---------- */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(g_tex, &r, px, w * (int)sizeof(lv_color_t));
    if (lv_disp_flush_is_last(drv)) {
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
    }
    lv_disp_flush_ready(drv);
}
static void display_init(void) {
    SDL_RendererInfo info;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (g_ren && SDL_GetRendererInfo(g_ren, &info) == 0)
        logf_("renderer name=%s", info.name);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { logf_("no renderer: %s", SDL_GetError()); return; }

    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(g_ren, &ow, &oh) == 0 && ow > 0 && oh > 0) { g_w = ow; g_h = oh; }
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_w, g_h);
    if (!g_tex) logf_("no texture: %s", SDL_GetError());

    lv_color_t *buf1 = malloc((size_t)g_w * g_h * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc((size_t)g_w * g_h * sizeof(lv_color_t));
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, g_w * g_h);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &draw_buf;
    drv.flush_cb = flush_cb;
    drv.hor_res = g_w;
    drv.ver_res = g_h;
    g_disp = lv_disp_drv_register(&drv);
    logf_("display %dx%d ready", g_w, g_h);
}

/* ======================================================================
 * Theme
 *
 * The LVGL default theme stays as the parent (fallback for widgets not
 * covered here); for every covered class theme_apply() wipes the parent's
 * styles and applies ours, so the look is fully defined by the tokens above.
 * ==================================================================== */
static lv_style_t st_txt[TXT__COUNT];
static lv_style_t st_screen, st_container, st_scrollbar, st_card, st_card_hero, st_tile, st_row;
static lv_style_t st_bar_bottom, st_chip, st_badge, st_badge_soft, st_glyph, st_console;
static lv_style_t st_btn, st_btn_secondary, st_btn_danger, st_btn_ghost;
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
static void theme_init(void) {
    theme_build();
    lv_theme_t *base = lv_theme_default_init(g_disp, C(COL_ACCENT), C(COL_ACCENT_HI), true, FONT_BODY);
    g_theme = *base;
    lv_theme_set_parent(&g_theme, base);
    lv_theme_set_apply_cb(&g_theme, theme_apply);
    lv_disp_set_theme(g_disp, &g_theme);
}

/* ======================================================================
 * Reusable components
 * ==================================================================== */
typedef struct { lv_obj_t *chip, *led, *label; } ui_status_t;

/* Layout boxes never clip: a focus ring, slider knob or button shadow may
 * reach past them, and the enclosing card (padded for it) does the clipping. */
static void ui_no_clip(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_clip_corner(o, false, 0);
}
static lv_obj_t *ui_box(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap) {
    lv_obj_t *o = lv_obj_create(parent);
    ui_no_clip(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}
static lv_obj_t *ui_spacer(lv_obj_t *parent) {
    lv_obj_t *o = ui_box(parent, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(o, 1, 1);
    lv_obj_set_flex_grow(o, 1);
    return o;
}
static lv_obj_t *ui_text(lv_obj_t *parent, txt_kind_t kind, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &st_txt[kind], 0);
    lv_label_set_text(l, text);
    return l;
}
static lv_obj_t *ui_card(lv_obj_t *parent, lv_style_t *variant) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_add_style(c, &st_card, 0);
    if (variant) lv_obj_add_style(c, variant, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    return c;
}
/* rounded square holding a symbol or short text (logo, console badge) */
static lv_obj_t *ui_tile(lv_obj_t *parent, lv_style_t *style, const char *text, lv_coord_t size,
                         const lv_font_t *font) {
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_add_style(t, style, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(t, size, size);
    lv_obj_t *l = lv_label_create(t);
    lv_label_set_text(l, text);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    return t;
}
/* card heading: soft icon tile + title + optional caption; returns the row */
static lv_obj_t *ui_card_header(lv_obj_t *card, const char *icon, const char *title, const char *caption) {
    lv_obj_t *row = ui_box(card, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(row, LV_PCT(100));
    ui_tile(row, &st_badge_soft, icon, TILE_SM, NULL);
    lv_obj_t *col = ui_box(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(col, 1);
    ui_text(col, TXT_HEADING, title);
    if (caption) ui_text(col, TXT_CAPTION, caption);
    return row;
}
/* settings row: title/caption on the left, the caller adds the control */
static lv_obj_t *ui_setting_row(lv_obj_t *parent, const char *title, const char *caption) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_style(row, &st_row, 0);
    ui_no_clip(row);   /* controls sit flush with the row's right edge */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *col = ui_box(row, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(col, 1);
    ui_text(col, TXT_BODY, title);
    if (caption) ui_text(col, TXT_CAPTION, caption);
    return row;
}
static void ui_last_row(lv_obj_t *row) { lv_obj_set_style_border_width(row, 0, 0); }

static lv_obj_t *ui_btn(lv_obj_t *parent, btn_kind_t kind, const char *icon, const char *text) {
    lv_obj_t *b = lv_btn_create(parent);
    if (kind == BTN_SECONDARY) lv_obj_add_style(b, &st_btn_secondary, 0);
    else if (kind == BTN_DANGER) lv_obj_add_style(b, &st_btn_danger, 0);
    else if (kind == BTN_GHOST) lv_obj_add_style(b, &st_btn_ghost, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (icon) lv_label_set_text(lv_label_create(b), icon);
    if (text) lv_label_set_text(lv_label_create(b), text);
    return b;
}
static lv_obj_t *ui_led(lv_obj_t *parent, uint32_t col, lv_coord_t size) {
    lv_obj_t *led = lv_led_create(parent);
    lv_obj_set_size(led, size, size);
    lv_led_set_color(led, C(col));
    lv_led_on(led);
    return led;
}
static lv_obj_t *ui_chip(lv_obj_t *parent, const char *icon, const char *text, lv_obj_t **label_out) {
    lv_obj_t *chip = ui_box(parent, LV_FLEX_FLOW_ROW, SP_S);
    lv_obj_add_style(chip, &st_chip, 0);
    if (icon) {
        lv_obj_t *i = lv_label_create(chip);
        lv_label_set_text(i, icon);
        lv_obj_set_style_text_color(i, C(COL_ACCENT_HI), 0);
    }
    lv_obj_t *l = lv_label_create(chip);
    lv_label_set_text(l, text);
    if (label_out) *label_out = l;
    return chip;
}
static void led_pulse_cb(void *led, int32_t v) { lv_led_set_brightness(led, (uint8_t)v); }
static void ui_status_set(ui_status_t *s, uint32_t col, const char *text, bool pulse) {
    lv_anim_del(s->led, led_pulse_cb);
    lv_led_set_color(s->led, C(col));
    lv_led_set_brightness(s->led, LV_LED_BRIGHT_MAX);
    lv_label_set_text(s->label, text);
    if (pulse) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s->led);
        lv_anim_set_exec_cb(&a, led_pulse_cb);
        lv_anim_set_values(&a, LV_LED_BRIGHT_MAX, LV_LED_BRIGHT_MIN);
        lv_anim_set_time(&a, 600);
        lv_anim_set_playback_time(&a, 600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}
/* status chip: glowing LED + label */
static void ui_status_init(ui_status_t *s, lv_obj_t *parent, uint32_t col, const char *text) {
    s->chip = ui_box(parent, LV_FLEX_FLOW_ROW, SP_M);
    lv_obj_add_style(s->chip, &st_chip, 0);
    lv_obj_set_style_pad_hor(s->chip, SP_XL - 4, 0);
    s->led = ui_led(s->chip, col, 14);
    s->label = lv_label_create(s->chip);
    ui_status_set(s, col, text, false);
}
/* controller button hint: round glyph + caption */
static void ui_hint(lv_obj_t *parent, const char *glyph, uint32_t glyph_col, const char *text) {
    lv_obj_t *row = ui_box(parent, LV_FLEX_FLOW_ROW, SP_S + 2);
    lv_obj_t *g = ui_tile(row, &st_glyph, glyph, 40, NULL);
    lv_obj_set_width(g, LV_SIZE_CONTENT);   /* round for one glyph, pill for "L1 R1" */
    lv_obj_set_style_min_width(g, 40, 0);
    lv_obj_set_style_pad_hor(g, SP_M, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(g, 0), C(glyph_col), 0);
    ui_text(row, TXT_CAPTION, text);
}
/* LVGL sliders step by 1 per key press; make remote LEFT/RIGHT move by `step` */
static void slider_step_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return;
    lv_obj_t *s = lv_event_get_target(e);
    int32_t step = (int32_t)(intptr_t)lv_event_get_user_data(e) - 1;
    lv_slider_set_value(s, lv_slider_get_value(s) + (key == LV_KEY_RIGHT ? step : -step), LV_ANIM_ON);
    lv_event_send(s, LV_EVENT_VALUE_CHANGED, NULL);
}
/* slider + right-aligned value label, sized like the other row controls */
static lv_obj_t *ui_slider_control(lv_obj_t *row, int32_t min, int32_t max, int32_t val, int32_t key_step,
                                   lv_event_cb_t changed_cb, lv_obj_t **value_label) {
    lv_obj_t *ctl = ui_box(row, LV_FLEX_FLOW_ROW, SP_XL);
    lv_obj_set_width(ctl, CTRL_W);
    lv_obj_t *s = lv_slider_create(ctl);
    lv_obj_set_height(s, 10);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_ext_click_area(s, SLIDER_HIT);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_add_event_cb(s, slider_step_cb, LV_EVENT_KEY, (void *)(intptr_t)key_step);
    lv_obj_add_event_cb(s, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    *value_label = ui_text(ctl, TXT_BODY, "");
    lv_obj_set_width(*value_label, SLIDER_VAL_W);
    lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, 0);
    return s;
}

/* PlayStation face buttons as a faint diamond: triangle, circle, cross, square */
#define DI (DECO_W / 2 + 4)
static const lv_point_t g_deco_tri[] = {
    { DI, DECO_SIZE - 12 }, { DECO_SIZE / 2, 10 }, { DECO_SIZE - DI, DECO_SIZE - 12 }, { DI, DECO_SIZE - 12 } };
static const lv_point_t g_deco_x1[] = { { 14, 14 }, { DECO_SIZE - 14, DECO_SIZE - 14 } };
static const lv_point_t g_deco_x2[] = { { DECO_SIZE - 14, 14 }, { 14, DECO_SIZE - 14 } };
static lv_obj_t *deco_cell(lv_obj_t *parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, DECO_SIZE, DECO_SIZE);
    lv_obj_set_pos(o, x, y);
    return o;
}
/* solid colours: translucent strokes would double up where segments overlap */
static void deco_line(lv_obj_t *cell, const lv_point_t *pts, uint16_t n, uint32_t col) {
    lv_obj_t *l = lv_line_create(cell);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, DECO_W, 0);
    lv_obj_set_style_line_color(l, C(col), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}
static void deco_ring(lv_obj_t *cell, lv_coord_t size, lv_coord_t radius, uint32_t col) {
    lv_obj_t *r = lv_obj_create(cell);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, size, size);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_border_width(r, DECO_W, 0);
    lv_obj_set_style_border_color(r, C(col), 0);
    lv_obj_center(r);
}
static lv_obj_t *ui_ps_buttons(lv_obj_t *parent) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(d, 2 * DECO_GAP + DECO_SIZE, 2 * DECO_GAP + DECO_SIZE);
    deco_line(deco_cell(d, DECO_GAP, 0), g_deco_tri, 4, COL_DECO_TRI);
    deco_ring(deco_cell(d, 2 * DECO_GAP, DECO_GAP), DECO_SIZE - 8, R_PILL, COL_DECO_CIRCLE);
    lv_obj_t *x = deco_cell(d, DECO_GAP, 2 * DECO_GAP);
    deco_line(x, g_deco_x1, 2, COL_DECO_CROSS);
    deco_line(x, g_deco_x2, 2, COL_DECO_CROSS);
    deco_ring(deco_cell(d, 0, DECO_GAP), DECO_SIZE - 18, 6, COL_DECO_SQUARE);
    return d;
}

/* ======================================================================
 * Log view: recoloured, timestamped lines in a scrollable console
 * ==================================================================== */
#define LOG_LINES 240
#define LOG_LINE_LEN 160
static char g_log_lines[LOG_LINES][LOG_LINE_LEN];
static int g_log_count;
static lv_obj_t *g_log_view, *g_log_label;

static void log_store(const char *when, const char *tag, uint32_t tag_col, const char *msg) {
    if (g_log_count == LOG_LINES) {
        memmove(g_log_lines[0], g_log_lines[1], sizeof(g_log_lines[0]) * (LOG_LINES - 1));
        g_log_count--;
    }
    snprintf(g_log_lines[g_log_count++], LOG_LINE_LEN, "#%06x %s#   #%06x %s#   %s",
             (unsigned)COL_DIM, when, (unsigned)tag_col, tag, msg);
}
static void log_refresh(void) {
    static char buf[LOG_LINES * (LOG_LINE_LEN + 1)];
    size_t n = 0;
    for (int i = 0; i < g_log_count; i++)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "\n" : "", g_log_lines[i]);
    lv_label_set_text(g_log_label, buf);
    lv_obj_update_layout(g_log_view);
    lv_obj_scroll_to_y(g_log_view, LV_COORD_MAX, LV_ANIM_ON);
}
static void ui_log(const char *tag, uint32_t tag_col, const char *fmt, ...) {
    char msg[128];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    for (char *p = msg; *p; p++) if (*p == '#') *p = ' ';  /* '#' is the recolour marker */
    logf_("ui: %s %s", tag, msg);
    if (!g_log_label) return;
    unsigned s = SDL_GetTicks() / 1000;
    char when[16];
    snprintf(when, sizeof(when), "%02u:%02u", s / 60, s % 60);
    log_store(when, tag, tag_col, msg);
    log_refresh();
}
/* Dummy history from an earlier session (UI only, not mirrored to the file
 * log) so the console holds enough lines for scrolling to be visible. */
static void ui_log_backlog(void) {
    enum { N = 90 };
    char when[16], msg[128];
    unsigned r = 12345;
    for (int i = 0; i < N; i++) {
        unsigned ago = (unsigned)(N - i) * 7;
        r = r * 1103515245u + 12345u;
        unsigned v = (r >> 16) % 100;
        snprintf(when, sizeof(when), "-%02u:%02u", ago / 60, ago % 60);
        switch (i % 6) {
        case 0: snprintf(msg, sizeof(msg), "%u fps, %u.%u Mbps, decode %u.%u ms", 58 + v % 3, 13 + v % 4, v % 10, 4 + v % 3, v % 10);
                log_store(when, "[VIDEO]", COL_ACCENT_HI, msg); break;
        case 1: snprintf(msg, sizeof(msg), "rtt %u ms, jitter %u ms, loss %u.%u%%", 6 + v % 9, 1 + v % 4, v % 2, v % 10);
                log_store(when, "[NET]", COL_INFO, msg); break;
        case 2: snprintf(msg, sizeof(msg), "opus 48 kHz stereo, buffer %u ms", 20 + v % 30);
                log_store(when, "[AUDIO]", COL_OK, msg); break;
        case 3: snprintf(msg, sizeof(msg), "DualShock 4 reports at %u Hz", 248 + v % 5);
                log_store(when, "[INPUT]", COL_OK, msg); break;
        case 4: snprintf(msg, sizeof(msg), "keyframe %u received (%u KB)", 1000 + (unsigned)i * 37, 90 + v);
                log_store(when, "[VIDEO]", COL_ACCENT_HI, msg); break;
        default: if (v < 30) {
                    snprintf(msg, sizeof(msg), "frame %u arrived %u ms late", 4000 + (unsigned)i * 61, 8 + v % 20);
                    log_store(when, "[WARN]", COL_WARN, msg);
                 } else {
                    snprintf(msg, sizeof(msg), "bitrate target %u.%u Mbps", 12 + v % 5, v % 10);
                    log_store(when, "[NET]", COL_INFO, msg);
                 }
        }
    }
    log_store("--:--", "[APP]", COL_ACCENT_HI, "previous session ended, new session below");
    log_refresh();
}

/* ======================================================================
 * Input: SDL -> LVGL keypad (TV remote)
 * ==================================================================== */
#define QN 32
static lv_key_t g_q[QN];
static int g_qh = 0, g_qt = 0;
static lv_key_t g_cur = 0;
static int g_release = 0;
static lv_key_t map_key(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return LV_KEY_PREV;
        case SDLK_DOWN: return LV_KEY_NEXT;
        case SDLK_LEFT: return LV_KEY_LEFT;
        case SDLK_RIGHT: return LV_KEY_RIGHT;
        case SDLK_RETURN: case SDLK_KP_ENTER: return LV_KEY_ENTER;
        case SDLK_ESCAPE: return LV_KEY_ESC;
#ifdef SDLK_AC_BACK
        case SDLK_AC_BACK: return LV_KEY_ESC;
#endif
        default: return 0;
    }
}
static void push_key(lv_key_t k) { if (!k) return; g_q[g_qt % QN] = k; g_qt++; }
/* PREV/NEXT always move group focus in LVGL, which would close an open
 * dropdown; while one is open, UP/DOWN (remote or D-pad) move its selection. */
static lv_key_t keypad_translate(lv_key_t k) {
    if (k != LV_KEY_PREV && k != LV_KEY_NEXT) return k;
    lv_obj_t *f = g_group ? lv_group_get_focused(g_group) : NULL;
    if (f && lv_obj_check_type(f, &lv_dropdown_class) && lv_dropdown_is_open(f))
        return k == LV_KEY_PREV ? LV_KEY_UP : LV_KEY_DOWN;
    return k;
}
static void kb_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (g_release) { data->state = LV_INDEV_STATE_RELEASED; data->key = g_cur; g_release = 0; return; }
    if (g_qh != g_qt) {
        g_cur = keypad_translate(g_q[g_qh % QN]); g_qh++;
        data->key = g_cur; data->state = LV_INDEV_STATE_PRESSED; g_release = 1;
    }
    else data->state = LV_INDEV_STATE_RELEASED;
}

/* ======================================================================
 * Input: gamepad (DS4) -> LVGL pointer + cursor + hover highlight
 * ==================================================================== */
/* DS4 codes as observed on this TV: webOS's uhid stack shifts the buttons
 * relative to hid-sony (see ~/webos-probe/docs/ds4-map-observed.txt). */
#define DS4_CROSS      0x131
#define DS4_L1         0x134
#define DS4_R1         0x135   /* 0x136/0x137 are the L2/R2 trigger clicks: unused */
#define DS4_OPTIONS    0x139
#define DS4_RSTICK_X   ABS_Z   /* not ABS_RX: that is the L2 trigger here */
#define DS4_RSTICK_Y   ABS_RZ

#define STICK_DEADZONE    0.15f
#define CURSOR_SPEED      1400.0f   /* px/s at full left-stick deflection */
#define SCROLL_SPEED      1600.0f   /* px/s at full right-stick deflection */
#define DPAD_REPEAT_DELAY 350       /* ms before a held D-pad starts repeating */
#define DPAD_REPEAT_MS    180

typedef struct { int value, min, max; } axis_t;
static int g_pad_fd = -1;
static axis_t g_lx = { 128, 0, 255 }, g_ly = { 128, 0, 255 }, g_rx = { 128, 0, 255 }, g_ry = { 128, 0, 255 };
static int g_hatx = 0, g_haty = 0;
static lv_key_t g_dpad_key;
static Uint32 g_dpad_next;
static float g_cx = 960, g_cy = 540, g_scroll_acc_x, g_scroll_acc_y;
static int g_click = 0;
static Uint32 g_last_tick = 0;
static void tab_step(int dir, const char *src);
static lv_obj_t *g_cursor = NULL;
static int g_cursor_speed = 100;   /* percent of the base stick speed */
static bool g_cursor_on = false;
static int g_cursor_px = -1, g_cursor_py = -1, g_cursor_click = 0;
static lv_obj_t *g_hover = NULL, *g_hover_list = NULL;
static Uint32 g_hover_at = 0;

#define GETBIT(b, n) (((const unsigned char *)(b))[(n) / 8] >> ((n) % 8) & 1)
static int looks_like_gamepad(int fd) {
    unsigned char key[(KEY_CNT + 7) / 8] = { 0 }, abs[(ABS_CNT + 7) / 8] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0) return 0;
    int btn = GETBIT(key, BTN_A) || GETBIT(key, BTN_B) || GETBIT(key, BTN_X) ||
              GETBIT(key, BTN_GAMEPAD) || GETBIT(key, BTN_JOYSTICK);
    int sticks = GETBIT(abs, ABS_X) && GETBIT(abs, ABS_Y);
    return btn && sticks;
}
/* Hotplug: while offline, /dev/input is rescanned every PAD_RESCAN_MS (the
 * node number can change across reconnects). While attached, a read error or
 * EOF drops the pad at once, and every PAD_HEALTH_MS the node is checked for
 * ENODEV or for having disappeared/been replaced, in case a drop is silent. */
#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#define PAD_RESCAN_MS 1500
#define PAD_HEALTH_MS 2000
static char g_pad_path[64], g_pad_name[160];
static bool g_pad_seen;   /* attached at least once: offline vs never found */
static Uint32 g_pad_next_scan, g_pad_next_health;
static void status_timer_cb(lv_timer_t *t);

static bool gamepad_try_acquire(void) {
    DIR *d = opendir(INPUT_DIR);
    if (!d) return false;
    struct dirent *e;
    while (g_pad_fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[160] = { 0 };
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strncmp(name, "LGE", 3) == 0 || strncmp(name, "LG ", 3) == 0 || !looks_like_gamepad(fd)) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) logf_("gamepad %s grab FAILED: %s", path, strerror(errno));
        else logf_("gamepad %s \"%s\" grabbed", path, name);
        struct { int code; axis_t *axis; } axes[] = {
            { ABS_X, &g_lx }, { ABS_Y, &g_ly }, { DS4_RSTICK_X, &g_rx }, { DS4_RSTICK_Y, &g_ry } };
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(axes[i].code), &ai) == 0 && ai.maximum > ai.minimum) {
                axes[i].axis->min = ai.minimum;
                axes[i].axis->max = ai.maximum;
                axes[i].axis->value = (ai.minimum + ai.maximum) / 2;
            }
        }
        snprintf(g_pad_path, sizeof(g_pad_path), "%s", path);
        snprintf(g_pad_name, sizeof(g_pad_name), "%s", name);
        g_pad_fd = fd;
        g_pad_seen = true;
        g_pad_next_health = SDL_GetTicks() + PAD_HEALTH_MS;
        g_last_tick = 0;
    }
    closedir(d);
    if (g_pad_fd >= 0) status_timer_cb(NULL);
    return g_pad_fd >= 0;
}
/* drop the pad and reset transient input so nothing stays held; the cursor stays put */
static void gamepad_lost(const char *why) {
    if (g_pad_fd < 0) return;
    ioctl(g_pad_fd, EVIOCGRAB, 0);
    close(g_pad_fd);
    g_pad_fd = -1;
    if (g_click && g_ptr_indev) lv_indev_wait_release(g_ptr_indev);   /* a held press must not click */
    g_click = 0;
    g_hatx = g_haty = 0;
    g_dpad_key = 0;
    g_lx.value = (g_lx.min + g_lx.max) / 2;
    g_ly.value = (g_ly.min + g_ly.max) / 2;
    g_rx.value = (g_rx.min + g_rx.max) / 2;
    g_ry.value = (g_ry.min + g_ry.max) / 2;
    g_scroll_acc_x = g_scroll_acc_y = 0;
    g_pad_next_scan = SDL_GetTicks() + PAD_RESCAN_MS;
    logf_("gamepad %s lost: %s", g_pad_path, why);
    ui_log("[INPUT]", COL_ERR, "gamepad offline");
    status_timer_cb(NULL);
}
static bool gamepad_healthy(void) {
    int ver;
    struct stat st_path, st_fd;
    if (ioctl(g_pad_fd, EVIOCGVERSION, &ver) < 0 && (errno == ENODEV || errno == EBADF)) return false;
    if (stat(g_pad_path, &st_path) < 0 || fstat(g_pad_fd, &st_fd) < 0) return false;
    return st_path.st_rdev == st_fd.st_rdev && st_path.st_ino == st_fd.st_ino;
}

static void cursor_create(void) {
    g_cursor = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_cursor);
    lv_obj_set_size(g_cursor, CURSOR_D, CURSOR_D);
    lv_obj_set_style_radius(g_cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_cursor, C(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_cursor, LV_OPA_40, 0);
    lv_obj_set_style_border_color(g_cursor, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_cursor, 3, 0);
    lv_obj_set_style_shadow_color(g_cursor, C(COL_ACCENT_HI), 0);
    lv_obj_set_style_shadow_width(g_cursor, 18, 0);
    lv_obj_set_style_shadow_opa(g_cursor, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(g_cursor, C(COL_ACCENT_HI), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_cursor, LV_OPA_90, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(g_cursor, -4, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(g_cursor, -4, LV_STATE_PRESSED);
    lv_obj_clear_flag(g_cursor, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_cursor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *dot = lv_obj_create(g_cursor);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(dot);
    g_cx = g_w / 2; g_cy = g_h / 2;
}
static void cursor_show(bool on) {
    if (!g_cursor || on == g_cursor_on) return;
    g_cursor_on = on;
    if (on) lv_obj_clear_flag(g_cursor, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_cursor, LV_OBJ_FLAG_HIDDEN);
}

/* Interactive object under the cursor: a group member or button; btnmatrix
 * widgets (tab bar, keyboard) handle their own per-key feedback. */
static lv_obj_t *hover_target(lv_obj_t *o) {
    for (; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_check_type(o, &lv_dropdownlist_class)) return o;
        if (lv_obj_has_class(o, &lv_btnmatrix_class)) return NULL;
        if (lv_obj_get_group(o) || lv_obj_has_class(o, &lv_btn_class)) return o;
    }
    return NULL;
}
#define DROPDOWN_PR_NONE 0xFFFF   /* LV_DROPDOWN_PR_NONE is private to lv_dropdown.c */

/* highlight the dropdown option under the cursor (same maths as LVGL's get_id_on_point) */
static void hover_list_option(lv_obj_t *list, const lv_point_t *p) {
    lv_dropdown_t *dd = (lv_dropdown_t *)((lv_dropdown_list_t *)list)->dropdown;
    lv_obj_t *label = lv_obj_get_child(list, 0);
    if (!label || !dd->option_cnt) return;
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_coord_t ls = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_coord_t h = lv_font_get_line_height(font) + ls;
    lv_coord_t y = p->y - label->coords.y1 + ls / 2;
    uint16_t id = y < 0 ? 0 : (uint16_t)(y / h);
    if (id >= dd->option_cnt) id = dd->option_cnt - 1;
    if (dd->pr_opt_id != id) { dd->pr_opt_id = id; lv_obj_invalidate(list); }
}
static void hover_update(bool force) {
    Uint32 now = SDL_GetTicks();
    int px = (int)g_cx, py = (int)g_cy;
    if (!force && px == g_cursor_px && py == g_cursor_py && now - g_hover_at < 120) return;
    g_hover_at = now;

    lv_obj_t *target = NULL;
    lv_point_t p = { (lv_coord_t)px, (lv_coord_t)py };
    if (g_cursor_on) {
        lv_obj_t *o = lv_indev_search_obj(lv_layer_top(), &p);
        if (!o) o = lv_indev_search_obj(lv_scr_act(), &p);
        target = hover_target(o);
    }
    lv_obj_t *list = target && lv_obj_check_type(target, &lv_dropdownlist_class) ? target : NULL;
    if (list != g_hover_list) {
        if (g_hover_list && lv_obj_is_valid(g_hover_list)) {
            lv_dropdown_t *dd = (lv_dropdown_t *)((lv_dropdown_list_t *)g_hover_list)->dropdown;
            dd->pr_opt_id = DROPDOWN_PR_NONE;
            lv_obj_invalidate(g_hover_list);
        }
        g_hover_list = list;
    }
    if (list) { hover_list_option(list, &p); target = NULL; }
    if (target != g_hover) {
        if (g_hover && lv_obj_is_valid(g_hover)) lv_obj_clear_state(g_hover, LV_STATE_HOVERED);
        g_hover = target;
        if (target) lv_obj_add_state(target, LV_STATE_HOVERED);
    }
}

/* Keypad mode (TV remote or D-pad in use): hide the cursor so Cross acts on
 * the focus ring instead of on an invisible pointer. The stick brings it back. */
static void keypad_mode(void) {
    if (!g_cursor_on) return;
    cursor_show(false);
    hover_update(true);
}

/* D-pad -> the same keypad queue as the TV remote, with auto-repeat while held */
static void dpad_event(int code, int value) {
    int *axis = code == ABS_HAT0X ? &g_hatx : &g_haty;
    if (*axis == value) return;
    *axis = value;
    lv_key_t k = 0;
    if (value) k = code == ABS_HAT0X ? (value < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT) : (value < 0 ? LV_KEY_PREV : LV_KEY_NEXT);
    else if (g_haty) k = g_haty < 0 ? LV_KEY_PREV : LV_KEY_NEXT;   /* other axis still held */
    else if (g_hatx) k = g_hatx < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT;
    if (value) { keypad_mode(); push_key(k); }
    g_dpad_key = k;
    g_dpad_next = SDL_GetTicks() + DPAD_REPEAT_DELAY;
}

/* normalised stick axis in -1..1 with deadzone and a soft response curve */
static float stick_axis(const axis_t *a) {
    int c = (a->min + a->max) / 2, h = (a->max - a->min) / 2;
    float v = (float)(a->value - c) / (h < 1 ? 1 : h);
    if (v > -STICK_DEADZONE && v < STICK_DEADZONE) return 0;
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return v * (0.4f + 0.6f * (v < 0 ? -v : v));
}

/* A press only clicks if it starts and ends on the same widget: once the
 * cursor leaves the pressed widget (and no scroll is in progress) LVGL is told
 * to drop the press, so it gets LV_EVENT_PRESS_LOST and never RELEASED/CLICKED. */
static bool press_cancellable(lv_obj_t *o) {
    return o && !lv_obj_check_type(o, &lv_obj_class) && !lv_obj_check_type(o, &lv_tabview_class);
}
static void press_cancel_check(void) {
    if (!g_ptr_indev || !g_click) return;
    _lv_indev_proc_t *proc = &g_ptr_indev->proc;
    lv_obj_t *obj = proc->types.pointer.act_obj;
    if (proc->wait_until_release || proc->types.pointer.scroll_obj || !press_cancellable(obj)) return;
    lv_area_t a;
    lv_obj_get_click_area(obj, &a);
    lv_point_t p = { (lv_coord_t)g_cx, (lv_coord_t)g_cy };
    if (_lv_area_is_point_on(&a, &p, 0)) return;
    lv_indev_wait_release(g_ptr_indev);
    lv_event_send(obj, LV_EVENT_PRESS_LOST, g_ptr_indev);
    logf_("input: press cancelled, cursor left the widget");
}

/* Right stick scrolling: the scrollable object under the cursor, else the
 * focused object's nearest scrollable ancestor, else the log on the Log tab. */
static bool can_scroll(lv_obj_t *o, bool x, bool y) {
    if (!o || !lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) return false;
    lv_dir_t dir = lv_obj_get_scroll_dir(o);
    bool sy = (dir & LV_DIR_VER) && (lv_obj_get_scroll_top(o) > 0 || lv_obj_get_scroll_bottom(o) > 0);
    bool sx = (dir & LV_DIR_HOR) && (lv_obj_get_scroll_left(o) > 0 || lv_obj_get_scroll_right(o) > 0);
    return (y && sy) || (x && sx);
}
static lv_obj_t *scrollable_ancestor(lv_obj_t *o, bool x, bool y) {
    for (; o; o = lv_obj_get_parent(o)) if (can_scroll(o, x, y)) return o;
    return NULL;
}
static lv_obj_t *obj_at(lv_obj_t *o, const lv_point_t *p) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || !_lv_area_is_point_on(&o->coords, p, 0)) return NULL;
    for (int32_t i = (int32_t)lv_obj_get_child_cnt(o) - 1; i >= 0; i--) {
        lv_obj_t *c = obj_at(lv_obj_get_child(o, i), p);
        if (c) return c;
    }
    return o;
}
static lv_obj_t *scroll_target(bool x, bool y) {
    lv_obj_t *t = NULL;
    if (g_cursor_on) {
        lv_point_t p = { (lv_coord_t)g_cx, (lv_coord_t)g_cy };
        t = scrollable_ancestor(obj_at(lv_scr_act(), &p), x, y);
    }
    if (!t && g_group) t = scrollable_ancestor(lv_group_get_focused(g_group), x, y);
    if (!t && g_tv && lv_tabview_get_tab_act(g_tv) == TAB_LOG && can_scroll(g_log_view, x, y)) t = g_log_view;
    return t;
}
static void rstick_scroll(float dt) {
    float sx = stick_axis(&g_rx), sy = stick_axis(&g_ry);
    if (sx == 0 && sy == 0) { g_scroll_acc_x = g_scroll_acc_y = 0; return; }
    g_scroll_acc_x += sx * SCROLL_SPEED * dt;
    g_scroll_acc_y += sy * SCROLL_SPEED * dt;
    int dx = (int)g_scroll_acc_x, dy = (int)g_scroll_acc_y;
    if (!dx && !dy) return;
    g_scroll_acc_x -= dx;
    g_scroll_acc_y -= dy;
    lv_obj_t *t = scroll_target(dx != 0, dy != 0);
    if (!t) return;
    /* stick down/right reveals content below/right, so the content moves the other way */
    lv_coord_t mx = -dx, my = -dy;
    if (my > 0) my = LV_MIN(my, lv_obj_get_scroll_top(t)); else my = LV_MAX(my, -lv_obj_get_scroll_bottom(t));
    if (mx > 0) mx = LV_MIN(mx, lv_obj_get_scroll_left(t)); else mx = LV_MAX(mx, -lv_obj_get_scroll_right(t));
    if (mx || my) lv_obj_scroll_by(t, mx, my, LV_ANIM_OFF);
}

static void gamepad_button(int code, int value) {
    if (code == DS4_CROSS) {
        if (value == 1) {
            if (g_cursor_on) g_click = 1;          /* pointer: LVGL decides press/release/click */
            else push_key(LV_KEY_ENTER);           /* keypad mode: activate the focused control */
        } else if (value == 0) {
            g_click = 0;
        }
    } else if (value != 1) {
        return;
    } else if (code == DS4_OPTIONS) {
        running = false;
    } else if (code == DS4_L1) {
        tab_step(-1, "L1");
    } else if (code == DS4_R1) {
        tab_step(+1, "R1");
    }
}

static void gamepad_tick(void) {
    Uint32 now = SDL_GetTicks();
    if (g_pad_fd < 0) {
        if ((Sint32)(now - g_pad_next_scan) < 0) return;
        g_pad_next_scan = now + PAD_RESCAN_MS;
        bool again = g_pad_seen;
        if (!gamepad_try_acquire()) return;
        ui_log("[INPUT]", COL_OK, "gamepad %s: %s \"%s\"", again ? "reconnected" : "connected", g_pad_path, g_pad_name);
    }
    if ((Sint32)(now - g_pad_next_health) >= 0) {
        g_pad_next_health = now + PAD_HEALTH_MS;
        if (!gamepad_healthy()) { gamepad_lost("node gone"); return; }
    }
    struct input_event ev; ssize_t n;
    while ((n = read(g_pad_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY) gamepad_button(ev.code, ev.value);
        else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) g_lx.value = ev.value;
            else if (ev.code == ABS_Y) g_ly.value = ev.value;
            else if (ev.code == DS4_RSTICK_X) g_rx.value = ev.value;
            else if (ev.code == DS4_RSTICK_Y) g_ry.value = ev.value;
            else if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) dpad_event(ev.code, ev.value);
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        gamepad_lost(n == 0 ? "end of stream" : strerror(errno));
        return;
    }
    float dt = g_last_tick ? (now - g_last_tick) / 1000.0f : 0.0f;
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    if (g_dpad_key && now >= g_dpad_next) {
        push_key(g_dpad_key);
        g_dpad_next = now + DPAD_REPEAT_MS;
    }
    rstick_scroll(dt);

    float nx = stick_axis(&g_lx), ny = stick_axis(&g_ly);
    const float speed = CURSOR_SPEED * g_cursor_speed / 100.0f;
    g_cx += nx * speed * dt;
    g_cy += ny * speed * dt;
    if (g_cx < 0) g_cx = 0;
    if (g_cy < 0) g_cy = 0;
    if (g_cx > g_w) g_cx = g_w;
    if (g_cy > g_h) g_cy = g_h;

    if (nx != 0 || ny != 0) cursor_show(true);
    press_cancel_check();
    if (!g_cursor) return;
    hover_update(false);
    if ((int)g_cx != g_cursor_px || (int)g_cy != g_cursor_py) {
        g_cursor_px = (int)g_cx; g_cursor_py = (int)g_cy;
        lv_obj_set_pos(g_cursor, g_cursor_px - CURSOR_D / 2, g_cursor_py - CURSOR_D / 2);
    }
    if (g_click != g_cursor_click) {
        g_cursor_click = g_click;
        if (g_click) lv_obj_add_state(g_cursor, LV_STATE_PRESSED);
        else lv_obj_clear_state(g_cursor, LV_STATE_PRESSED);
    }
}
/* plain state report: no synthetic clicks, LVGL derives them from press/release */
static void ptr_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point.x = (lv_coord_t)g_cx;
    data->point.y = (lv_coord_t)g_cy;
    data->state = g_click ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ======================================================================
 * Demo UI
 * ==================================================================== */

enum { HOST_READY, HOST_STANDBY, HOST_OFFLINE };
static const uint32_t host_state_col[] = { COL_OK, COL_WARN, COL_DIM };
static const char *const host_state_txt[] = { "Ready", "Standby", "Offline" };
static const struct { const char *name, *model, *addr, *fw, *quality; int state; } g_hosts[] = {
    { "Living-room PS5", "PS5", "10.10.10.40", "24.06-09.00", "1080p" BULLET "60 fps", HOST_READY },
    { "Bedroom PS5", "PS5", "10.10.10.41", "24.05-08.60", "1080p" BULLET "60 fps", HOST_STANDBY },
    { "Study PS4 Pro", "PS4", "10.10.10.52", "11.50", "1080p" BULLET "30 fps", HOST_OFFLINE },
};
#define HOST_COUNT ((int)(sizeof(g_hosts) / sizeof(g_hosts[0])))
static lv_obj_t *g_host_btns[HOST_COUNT];
static int g_host_sel;
static struct {
    lv_obj_t *name, *model, *addr, *fw, *quality, *play, *wake;
    ui_status_t status;
} g_hero;
static lv_obj_t *g_speed_val;
static struct {
    lv_obj_t *codec, *res, *fps, *bitrate, *bitrate_val, *hdr;
    lv_obj_t *sum_title, *sum_detail, *bw_bar, *bw_caption;
} g_video;
static struct {
    lv_obj_t *spinner, *icon, *state, *bar, *pct, *connect, *disc;
    ui_status_t bottom, pad;
} g_conn_ui;
typedef enum { CONN_IDLE, CONN_CONNECTING, CONN_STREAMING } conn_t;
static conn_t g_conn = CONN_IDLE;
static int g_conn_progress;
static lv_timer_t *g_conn_timer;
static int g_pad_shown = -2;

static void host_select(int i) {
    g_host_sel = i;
    for (int j = 0; j < HOST_COUNT; j++) {
        if (j == i) lv_obj_add_state(g_host_btns[j], LV_STATE_CHECKED);
        else lv_obj_clear_state(g_host_btns[j], LV_STATE_CHECKED);
    }
    lv_label_set_text(g_hero.name, g_hosts[i].name);
    lv_label_set_text(g_hero.model, g_hosts[i].model);
    lv_label_set_text(g_hero.addr, g_hosts[i].addr);
    lv_label_set_text(g_hero.fw, g_hosts[i].fw);
    lv_label_set_text(g_hero.quality, g_hosts[i].quality);
    ui_status_set(&g_hero.status, host_state_col[g_hosts[i].state], host_state_txt[g_hosts[i].state], false);
    if (g_hosts[i].state == HOST_STANDBY) lv_obj_clear_state(g_hero.wake, LV_STATE_DISABLED);
    else lv_obj_add_state(g_hero.wake, LV_STATE_DISABLED);
    if (g_hosts[i].state == HOST_OFFLINE) lv_obj_add_state(g_hero.play, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_hero.play, LV_STATE_DISABLED);
}
static void speed_changed_cb(lv_event_t *e) {
    g_cursor_speed = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(g_speed_val, "%d%%", g_cursor_speed);
}
static void host_click_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    host_select(i);
    ui_log("[HOST]", COL_ACCENT_HI, "Selected %s (%s)", g_hosts[i].name, g_hosts[i].addr);
}
static void action_cb(lv_event_t *e) {
    const char *what = lv_event_get_user_data(e);
    const char *host = g_hosts[g_host_sel].name;
    if (!strcmp(what, "wake")) ui_log("[HOST]", COL_ACCENT_HI, "Wake-up packet sent to %s", host);
    else if (!strcmp(what, "forget")) ui_log("[HOST]", COL_WARN, "Forget %s (dummy)", host);
    else if (!strcmp(what, "add")) ui_log("[HOST]", COL_ACCENT_HI, "Add host: enter the console IP on the Input tab");
    else if (!strcmp(what, "register")) ui_log("[REG]", COL_INFO, "Registration request sent (dummy)");
}

static void video_refresh(void) {
    char codec[24], res[24], fps[24];
    lv_dropdown_get_selected_str(g_video.codec, codec, sizeof(codec));
    lv_dropdown_get_selected_str(g_video.res, res, sizeof(res));
    lv_dropdown_get_selected_str(g_video.fps, fps, sizeof(fps));
    int kbps = lv_slider_get_value(g_video.bitrate);
    bool hdr = lv_obj_has_state(g_video.hdr, LV_STATE_CHECKED);
    const char *x = strchr(res, 'x');
    lv_label_set_text_fmt(g_video.bitrate_val, "%d.%d Mbps", kbps / 1000, (kbps % 1000) / 100);
    lv_label_set_text_fmt(g_video.sum_title, "%dp%d", x ? atoi(x + 1) : 0, atoi(fps));
    lv_label_set_text_fmt(g_video.sum_detail, "%s" BULLET "%d.%d Mbps" BULLET "%s",
                          codec, kbps / 1000, (kbps % 1000) / 100, hdr ? "HDR" : "SDR");
    lv_bar_set_value(g_video.bw_bar, kbps * 100 / 50000, LV_ANIM_ON);
    lv_label_set_text_fmt(g_video.bw_caption, "%d.%d of 50 Mbps", kbps / 1000, (kbps % 1000) / 100);
}
static void video_changed_cb(lv_event_t *e) { (void)e; video_refresh(); }

static void conn_set(conn_t s);
static void conn_timer_cb(lv_timer_t *t) {
    (void)t;
    g_conn_progress += 2;
    if (g_conn_progress >= 100) { g_conn_progress = 100; conn_set(CONN_STREAMING); return; }
    lv_bar_set_value(g_conn_ui.bar, g_conn_progress, LV_ANIM_ON);
    lv_label_set_text_fmt(g_conn_ui.pct, "%d%%", g_conn_progress);
}
static void conn_set(conn_t s) {
    g_conn = s;
    if (s != CONN_CONNECTING && g_conn_timer) { lv_timer_del(g_conn_timer); g_conn_timer = NULL; }
    if (s == CONN_IDLE) g_conn_progress = 0;
    lv_bar_set_value(g_conn_ui.bar, g_conn_progress, LV_ANIM_ON);
    lv_label_set_text_fmt(g_conn_ui.pct, "%d%%", g_conn_progress);
    if (s == CONN_CONNECTING) {
        lv_obj_clear_flag(g_conn_ui.spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_conn_ui.icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_conn_ui.spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_conn_ui.icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_conn_ui.icon, s == CONN_STREAMING ? LV_SYMBOL_OK : LV_SYMBOL_PAUSE);
        lv_obj_set_style_text_color(g_conn_ui.icon, C(s == CONN_STREAMING ? COL_OK : COL_DIM), 0);
    }
    static const char *const txt[] = { "Idle", "Connecting", "Streaming" };
    static const uint32_t col[] = { COL_DIM, COL_WARN, COL_OK };
    lv_label_set_text(g_conn_ui.state, txt[s]);
    ui_status_set(&g_conn_ui.bottom, col[s], txt[s], s == CONN_CONNECTING);
    if (s == CONN_IDLE) {
        lv_obj_clear_state(g_conn_ui.connect, LV_STATE_DISABLED);
        lv_obj_add_state(g_conn_ui.disc, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(g_conn_ui.connect, LV_STATE_DISABLED);
        lv_obj_clear_state(g_conn_ui.disc, LV_STATE_DISABLED);
    }
    if (s == CONN_STREAMING) {
        char res[24];
        lv_dropdown_get_selected_str(g_video.res, res, sizeof(res));
        ui_log("[STREAM]", COL_OK, "Session started with %s at %s", g_hosts[g_host_sel].name, res);
    }
}
static void connect_cb(lv_event_t *e) {
    (void)e;
    if (g_conn != CONN_IDLE) return;
    ui_log("[NET]", COL_INFO, "Connecting to %s (%s)", g_hosts[g_host_sel].name, g_hosts[g_host_sel].addr);
    g_conn_progress = 0;
    conn_set(CONN_CONNECTING);
    g_conn_timer = lv_timer_create(conn_timer_cb, 60, NULL);
}
static void disconnect_cb(lv_event_t *e) {
    (void)e;
    if (g_conn == CONN_IDLE) return;
    conn_set(CONN_IDLE);
    ui_log("[NET]", COL_WARN, "Session closed");
}
/* top-bar controller light: green when grabbed, red once lost, grey if never found */
static void status_timer_cb(lv_timer_t *t) {
    (void)t;
    int state = g_pad_fd >= 0 ? 1 : g_pad_seen ? 2 : 0;
    if (state == g_pad_shown || !g_conn_ui.pad.led) return;
    g_pad_shown = state;
    static const uint32_t col[] = { COL_DIM, COL_OK, COL_ERR };
    static const char *const txt[] = { "No controller", "DualShock 4", "Controller offline" };
    ui_status_set(&g_conn_ui.pad, col[state], txt[state], false);
}

static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_mode(g_kb, (lv_keyboard_mode_t)(intptr_t)lv_event_get_user_data(e));
        lv_keyboard_set_textarea(g_kb, ta);
        lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_kb);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(g_kb, NULL);
        lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    }
}
static lv_obj_t *mk_dropdown(lv_obj_t *parent, const char *opts, uint16_t sel) {
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_dropdown_set_options(d, opts);
    lv_dropdown_set_selected(d, sel);
    lv_obj_set_width(d, CTRL_W);
    lv_obj_add_event_cb(d, video_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return d;
}
static lv_obj_t *mk_textarea(lv_obj_t *parent, const char *text, const char *placeholder,
                             const char *accept, uint32_t max_len, lv_coord_t w, lv_keyboard_mode_t mode) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (accept) lv_textarea_set_accepted_chars(ta, accept);
    if (max_len) lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, text);
    lv_obj_set_width(ta, w);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, (void *)(intptr_t)mode);
    return ta;
}
/* ---------- tabs ----------
 * Pages change only through tab_go(): tab bar click, L1/R1, remote/D-pad
 * LEFT/RIGHT on the tab bar, or keypad focus moving onto another page. The
 * content is not user-scrollable (no swipe); lv_tabview_set_act(..., LV_ANIM_ON)
 * still slides it because lv_obj_scroll_to_x() ignores LV_OBJ_FLAG_SCROLLABLE.
 * The active pill follows the content's scroll position, so it slides in
 * lockstep with the page and the tab labels cross-fade as it passes. */
static const char *const g_tab_names[TAB__COUNT] = { "Hosts", "Video", "Input", "Log" };
static const char *const g_tab_icons[TAB__COUNT] = { LV_SYMBOL_HOME, LV_SYMBOL_VIDEO, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LIST };
static float g_tab_pos;   /* content scroll position in pages, 0..TAB__COUNT-1 */

static void tabbar_pill_area(lv_obj_t *bar, lv_area_t *out) {
    lv_btnmatrix_t *bm = (lv_btnmatrix_t *)bar;
    int last = bm->btn_cnt - 1, i = (int)g_tab_pos;
    if (i < 0) i = 0;
    if (i > last) i = last;
    int j = i < last ? i + 1 : last;
    float f = g_tab_pos - (float)i;
    const lv_area_t *a = &bm->button_areas[i], *b = &bm->button_areas[j];
    out->x1 = bar->coords.x1 + a->x1 + (lv_coord_t)((b->x1 - a->x1) * f);
    out->x2 = bar->coords.x1 + a->x2 + (lv_coord_t)((b->x2 - a->x2) * f);
    out->y1 = bar->coords.y1 + a->y1;
    out->y2 = bar->coords.y1 + a->y2;
}
static void tabbar_draw_cb(lv_event_t *e) {
    lv_obj_t *bar = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN_BEGIN) {
        /* under the tab labels: track, then the pill */
        lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        lv_obj_init_draw_rect_dsc(bar, LV_PART_CUSTOM_FIRST, &d);
        lv_draw_rect(ctx, &d, &bar->coords);
        lv_area_t pill;
        tabbar_pill_area(bar, &pill);
        lv_draw_rect_dsc_init(&d);
        lv_obj_init_draw_rect_dsc(bar, LV_PART_INDICATOR, &d);
        lv_draw_rect(ctx, &d, &pill);
    } else {
        lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
        if (dsc->part != LV_PART_ITEMS || !dsc->label_dsc) return;
        float dist = g_tab_pos - (float)dsc->id;
        if (dist < 0) dist = -dist;
        lv_opa_t mix = dist >= 1.0f ? 0 : (lv_opa_t)((1.0f - dist) * 255);
        dsc->label_dsc->color = lv_color_mix(lv_color_white(), C(COL_MUTED), mix);
    }
}
static void tab_content_scroll_cb(lv_event_t *e) {
    lv_obj_t *cont = lv_event_get_target(e);
    lv_coord_t step = lv_obj_get_content_width(cont) + lv_obj_get_style_pad_column(cont, 0);
    float pos = step > 0 ? (float)lv_obj_get_scroll_x(cont) / (float)step : 0;
    if (pos < 0) pos = 0;
    if (pos > TAB__COUNT - 1) pos = TAB__COUNT - 1;
    if (pos != g_tab_pos) {
        g_tab_pos = pos;
        lv_obj_invalidate(lv_tabview_get_tab_btns(g_tv));
    }
}
static void tab_go(uint32_t idx, const char *src) {
    if (idx >= TAB__COUNT) return;
    if (idx != lv_tabview_get_tab_act(g_tv)) ui_log("[TAB]", COL_ACCENT_HI, "switched to %s (%s)", g_tab_names[idx], src);
    lv_btnmatrix_set_selected_btn(lv_tabview_get_tab_btns(g_tv), idx);
    lv_tabview_set_act(g_tv, idx, LV_ANIM_ON);
}
static int page_of(lv_obj_t *o) {
    lv_obj_t *cont = lv_tabview_get_content(g_tv);
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_get_parent(o) == cont) return (int)lv_obj_get_index(o);
    return -1;
}
/* keypad focus moved onto another page (D-pad/remote UP/DOWN): follow it */
static void group_focus_cb(lv_group_t *g) {
    int p = page_of(lv_group_get_focused(g));
    if (p >= 0 && (uint32_t)p != lv_tabview_get_tab_act(g_tv)) tab_go((uint32_t)p, "focus");
}
/* L1/R1: change page and move keypad focus to its first control */
static void tab_step(int dir, const char *src) {
    if (!g_tv) return;
    uint32_t idx = (lv_tabview_get_tab_act(g_tv) + TAB__COUNT + (uint32_t)(dir < 0 ? -1 : 1)) % TAB__COUNT;
    tab_go(idx, src);
    if (page_of(lv_group_get_focused(g_group)) == (int)idx) return;
    lv_obj_t *target = lv_tabview_get_tab_btns(g_tv), **o;   /* Log has no controls: fall back to the tab bar */
    _LV_LL_READ(&g_group->obj_ll, o) {
        if (!lv_obj_has_state(*o, LV_STATE_DISABLED) && page_of(*o) == (int)idx) { target = *o; break; }
    }
    lv_group_focus_obj(target);
    if (!g_cursor_on) lv_obj_add_state(target, LV_STATE_FOCUS_KEY);   /* show the ring in keypad mode */
}
/* tab bar click/ENTER: replace the tabview's instant jump with the slide */
static void tabbar_clicked_cb(lv_event_t *e) {
    uint16_t id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (id != LV_BTNMATRIX_BTN_NONE) tab_go(id, "tab bar");
    lv_event_stop_processing(e);
}
static void tabbar_key_cb(lv_event_t *e) {
    uint32_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return;
    uint16_t id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (id != LV_BTNMATRIX_BTN_NONE) tab_go(id, "keypad");
}
static lv_obj_t *mk_page(lv_obj_t *tv, const char *name) {
    lv_obj_t *p = lv_tabview_add_tab(tv, name);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(p, SP_XL, 0);
    return p;
}

static void build_topbar(lv_obj_t *scr) {
    lv_obj_t *top = ui_box(scr, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_size(top, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_pad_hor(top, PAGE_MARGIN, 0);
    lv_obj_set_flex_grow(top, 0);
    ui_tile(top, &st_badge, LV_SYMBOL_PLAY, TILE_MD, FONT_HEADING);
    lv_obj_t *col = ui_box(top, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_text(col, TXT_TITLE, "Chiaki");
    ui_text(col, TXT_CAPTION, "PS Remote Play for webOS");
    ui_spacer(top);
    ui_status_init(&g_conn_ui.pad, top, COL_DIM, "No controller");
    char ver[32];
    snprintf(ver, sizeof(ver), "LVGL %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ui_chip(top, LV_SYMBOL_SETTINGS, ver, NULL);
}

static void build_hosts(lv_obj_t *page) {
    lv_obj_t *left = ui_card(page, NULL);
    lv_obj_set_size(left, LV_PCT(40), LV_PCT(100));
    ui_card_header(left, LV_SYMBOL_HOME, "Consoles", "3 found on your network");
    lv_obj_t *list = lv_list_create(left);
    ui_no_clip(list);   /* three hosts always fit; let the item focus rings breathe */
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, FOCUS_EXT + 2, 0);
    for (int i = 0; i < HOST_COUNT; i++) {
        lv_obj_t *b = lv_list_add_btn(list, NULL, NULL);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_tile(b, g_hosts[i].state == HOST_OFFLINE ? &st_badge_soft : &st_badge, g_hosts[i].model,
                TILE_MD, FONT_CAPTION);
        lv_obj_t *col = ui_box(b, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        ui_text(col, TXT_BODY, g_hosts[i].name);
        ui_text(col, TXT_CAPTION, g_hosts[i].addr);
        lv_obj_t *st = ui_box(b, LV_FLEX_FLOW_ROW, SP_S);
        ui_led(st, host_state_col[g_hosts[i].state], 12);
        ui_text(st, TXT_CAPTION, host_state_txt[g_hosts[i].state]);
        lv_obj_add_event_cb(b, host_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_host_btns[i] = b;
    }
    lv_obj_t *add = ui_btn(left, BTN_GHOST, LV_SYMBOL_PLUS, "Add host");
    lv_obj_set_width(add, LV_PCT(100));
    lv_obj_add_event_cb(add, action_cb, LV_EVENT_CLICKED, "add");

    lv_obj_t *hero = ui_card(page, &st_card_hero);
    lv_obj_set_height(hero, LV_PCT(100));
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_align(ui_ps_buttons(hero), LV_ALIGN_TOP_RIGHT, -SP_L, SP_S);
    ui_text(hero, TXT_OVERLINE, "SELECTED CONSOLE");
    g_hero.name = ui_text(hero, TXT_DISPLAY, "");
    lv_obj_t *chips = ui_box(hero, LV_FLEX_FLOW_ROW, SP_M);
    ui_status_init(&g_hero.status, chips, COL_OK, "");
    ui_chip(chips, LV_SYMBOL_VIDEO, "", &g_hero.model);
    ui_chip(chips, LV_SYMBOL_WIFI, "", &g_hero.addr);
    ui_spacer(hero);
    lv_obj_t *stats = ui_box(hero, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(stats, LV_PCT(100));
    const char *stat_names[] = { "Firmware", "Best quality", "Remote Play" };
    lv_obj_t **stat_vals[] = { &g_hero.fw, &g_hero.quality, NULL };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *t = ui_box(stats, LV_FLEX_FLOW_COLUMN, SP_XS);
        lv_obj_add_style(t, &st_tile, 0);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_grow(t, 1);
        ui_text(t, TXT_CAPTION, stat_names[i]);
        lv_obj_t *v = ui_text(t, TXT_HEADING, i == 2 ? "Registered" : "");
        if (stat_vals[i]) *stat_vals[i] = v;
    }
    lv_obj_t *actions = ui_box(hero, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_style_pad_top(actions, SP_S, 0);
    g_hero.play = ui_btn(actions, BTN_PRIMARY, LV_SYMBOL_PLAY, "Start Remote Play");
    lv_obj_set_style_pad_hor(g_hero.play, SP_3XL, 0);
    lv_obj_add_event_cb(g_hero.play, connect_cb, LV_EVENT_CLICKED, NULL);
    g_hero.wake = ui_btn(actions, BTN_SECONDARY, LV_SYMBOL_POWER, "Wake");
    lv_obj_add_event_cb(g_hero.wake, action_cb, LV_EVENT_CLICKED, "wake");
    ui_spacer(actions);
    lv_obj_t *forget = ui_btn(actions, BTN_DANGER, LV_SYMBOL_TRASH, "Forget");
    lv_obj_add_event_cb(forget, action_cb, LV_EVENT_CLICKED, "forget");
}

static void build_video(lv_obj_t *page) {
    lv_obj_t *card = ui_card(page, NULL);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_t *hdr = ui_card_header(card, LV_SYMBOL_VIDEO, "Stream quality", "Applied the next time you connect");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);

    lv_obj_t *row = ui_setting_row(card, "Codec", "Decoded in hardware by the TV");
    g_video.codec = mk_dropdown(row, "H.264\nH.265\nH.265 HDR", 1);
    row = ui_setting_row(card, "Resolution", "Higher needs more bandwidth");
    g_video.res = mk_dropdown(row, "1280x720\n1920x1080", 1);
    row = ui_setting_row(card, "Frame rate", "60 fps feels smoother in games");
    g_video.fps = mk_dropdown(row, "30 fps\n60 fps", 1);
    row = ui_setting_row(card, "Bitrate", "Lower it if the picture stutters");
    g_video.bitrate = ui_slider_control(row, 2000, 50000, 15000, 1000, video_changed_cb, &g_video.bitrate_val);
    row = ui_setting_row(card, "HDR", "Needs an HDR picture mode on the TV");
    ui_last_row(row);
    g_video.hdr = lv_switch_create(row);
    lv_obj_set_size(g_video.hdr, 84, 44);
    lv_obj_add_event_cb(g_video.hdr, video_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sum = ui_card(page, NULL);
    lv_obj_set_size(sum, LV_PCT(34), LV_PCT(100));
    ui_card_header(sum, LV_SYMBOL_IMAGE, "Summary", "What the console will send");
    lv_obj_t *big = ui_box(sum, LV_FLEX_FLOW_COLUMN, SP_XS);
    lv_obj_set_flex_align(big, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(big, SP_L, 0);
    g_video.sum_title = ui_text(big, TXT_DISPLAY, "");
    g_video.sum_detail = ui_text(big, TXT_CAPTION, "");
    ui_spacer(sum);
    ui_text(sum, TXT_OVERLINE, "BANDWIDTH");
    g_video.bw_bar = lv_bar_create(sum);
    lv_obj_set_size(g_video.bw_bar, LV_PCT(100), 14);
    g_video.bw_caption = ui_text(sum, TXT_CAPTION, "");
    lv_obj_t *chips = ui_box(sum, LV_FLEX_FLOW_ROW_WRAP, SP_S);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_style_pad_top(chips, SP_S, 0);
    ui_chip(chips, LV_SYMBOL_CHARGE, "Low latency", NULL);
    ui_chip(chips, LV_SYMBOL_OK, "Hardware decode", NULL);
    video_refresh();
}

static void build_input(lv_obj_t *page) {
    lv_obj_t *pad = ui_card(page, NULL);
    lv_obj_set_height(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_style_pad_row(pad, 0, 0);
    lv_obj_t *hdr = ui_card_header(pad, LV_SYMBOL_SHUFFLE, "Controller", "DualShock 4 and TV remote");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    const char *names[] = { "Rumble", "Touchpad gestures", "Motion controls" };
    const char *caps[] = { "Vibrate with in-game feedback", "Map swipes to the PS touchpad", "Use the gyro for aiming" };
    lv_obj_t *row = NULL;
    for (int i = 0; i < 3; i++) {
        row = ui_setting_row(pad, names[i], caps[i]);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 84, 44);
        if (i != 1) lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    row = ui_setting_row(pad, "Cursor speed", "Left stick pointer speed");
    ui_last_row(row);
    lv_obj_t *speed = ui_slider_control(row, 50, 200, g_cursor_speed, 10, speed_changed_cb, &g_speed_val);
    lv_event_send(speed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *reg = ui_card(page, NULL);
    lv_obj_set_height(reg, LV_PCT(100));
    lv_obj_set_flex_grow(reg, 1);
    lv_obj_set_style_pad_row(reg, 0, 0);
    hdr = ui_card_header(reg, LV_SYMBOL_EDIT, "Registration", "Pair this TV with a console");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    row = ui_setting_row(reg, "PS5 IP address", "Same network as the TV");
    mk_textarea(row, "192.168.1.100", "192.168.1.100", "0123456789.", 15, 300, LV_KEYBOARD_MODE_NUMBER);
    row = ui_setting_row(reg, "Registration PIN", "Shown on the console");
    ui_last_row(row);
    mk_textarea(row, "", "8 digits", "0123456789", 8, 300, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_t *act = ui_box(reg, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_top(act, SP_L, 0);
    lv_obj_t *b = ui_btn(act, BTN_PRIMARY, LV_SYMBOL_OK, "Register");
    lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, "register");
    ui_spacer(reg);
    ui_text(reg, TXT_CAPTION, "On the PS5: Settings > System > Remote Play > Link Device");
}

static void build_log(lv_obj_t *page) {
    lv_obj_t *card = ui_card(page, NULL);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_t *hdr = ui_card_header(card, LV_SYMBOL_LIST, "Session log", "Live diagnostics from the streaming session");
    lv_obj_t *st = ui_box(hdr, LV_FLEX_FLOW_ROW, SP_L);
    g_conn_ui.spinner = lv_spinner_create(st, 1000, 60);
    lv_obj_set_size(g_conn_ui.spinner, 36, 36);
    g_conn_ui.icon = ui_text(st, TXT_HEADING, LV_SYMBOL_PAUSE);
    g_conn_ui.state = ui_text(st, TXT_BODY, "Idle");
    lv_obj_set_width(g_conn_ui.state, 150);
    g_conn_ui.bar = lv_bar_create(st);
    lv_obj_set_size(g_conn_ui.bar, 320, 14);
    g_conn_ui.pct = ui_text(st, TXT_CAPTION, "0%");
    lv_obj_set_width(g_conn_ui.pct, 60);
    lv_obj_set_style_text_align(g_conn_ui.pct, LV_TEXT_ALIGN_RIGHT, 0);

    g_log_view = lv_obj_create(card);
    lv_obj_add_style(g_log_view, &st_console, 0);
    lv_obj_set_width(g_log_view, LV_PCT(100));
    lv_obj_set_flex_grow(g_log_view, 1);
    lv_obj_set_scroll_dir(g_log_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_log_view, LV_SCROLLBAR_MODE_AUTO);
    g_log_label = lv_label_create(g_log_view);
    lv_obj_set_width(g_log_label, LV_PCT(100));
    lv_label_set_recolor(g_log_label, true);
    lv_label_set_text(g_log_label, "");
}

static void build_bottombar(lv_obj_t *scr) {
    lv_obj_t *bot = ui_box(scr, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_add_style(bot, &st_bar_bottom, 0);
    lv_obj_set_size(bot, LV_PCT(100), BOTBAR_H);
    lv_obj_set_style_pad_hor(bot, PAGE_MARGIN, 0);
    lv_obj_set_flex_grow(bot, 0);
    g_conn_ui.connect = ui_btn(bot, BTN_PRIMARY, LV_SYMBOL_PLAY, "Connect");
    lv_obj_add_event_cb(g_conn_ui.connect, connect_cb, LV_EVENT_CLICKED, NULL);
    g_conn_ui.disc = ui_btn(bot, BTN_DANGER, LV_SYMBOL_STOP, "Disconnect");
    lv_obj_add_event_cb(g_conn_ui.disc, disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gap = ui_box(bot, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_width(gap, SP_S);
    ui_status_init(&g_conn_ui.bottom, bot, COL_DIM, "Idle");
    ui_spacer(bot);
    ui_hint(bot, "L", COL_TEXT, "Cursor");
    ui_hint(bot, "R", COL_TEXT, "Scroll");
    ui_hint(bot, "L1  R1", COL_TEXT, "Tabs");
    ui_hint(bot, "X", COL_PS_CROSS, "Select");
    ui_hint(bot, LV_SYMBOL_BARS, COL_TEXT, "Quit");
}

static void group_add_children(lv_obj_t *parent) {
    uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, &lv_dropdown_class) || lv_obj_check_type(c, &lv_slider_class) ||
            lv_obj_check_type(c, &lv_switch_class) || lv_obj_check_type(c, &lv_textarea_class) ||
            lv_obj_check_type(c, &lv_btn_class) || lv_obj_check_type(c, &lv_list_btn_class))
            lv_group_add_obj(g_group, c);
        else
            group_add_children(c);
    }
}

static void ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &st_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

    build_topbar(scr);

    /* tabview grows to fill the middle */
    g_tv = lv_tabview_create(scr, LV_DIR_TOP, TABBAR_H);
    lv_obj_set_width(g_tv, LV_PCT(100));
    lv_obj_set_flex_grow(g_tv, 1);
    lv_obj_set_style_pad_hor(g_tv, PAGE_MARGIN, 0);
    lv_obj_set_style_pad_top(g_tv, SP_XS, 0);
    lv_obj_set_style_pad_bottom(g_tv, SP_XL, 0);
    lv_obj_set_style_pad_row(g_tv, SP_XL, 0);
    lv_obj_set_scrollbar_mode(g_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *tabbar = lv_tabview_get_tab_btns(g_tv);
    lv_obj_set_width(tabbar, TABBAR_W);
    lv_obj_add_event_cb(tabbar, tabbar_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(tabbar, tabbar_clicked_cb, LV_EVENT_VALUE_CHANGED | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(tabbar, tabbar_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(tabbar, tabbar_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    /* no drag/swipe between pages; programmatic slides still work */
    lv_obj_t *content = lv_tabview_get_content(g_tv);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(content, tab_content_scroll_cb, LV_EVENT_SCROLL, NULL);

    lv_obj_t *pages[TAB__COUNT];
    for (int i = 0; i < TAB__COUNT; i++) {
        char name[48];
        snprintf(name, sizeof(name), "%s   %s", g_tab_icons[i], g_tab_names[i]);
        pages[i] = mk_page(g_tv, name);
    }
    build_hosts(pages[TAB_HOSTS]);
    build_video(pages[TAB_VIDEO]);
    build_input(pages[TAB_INPUT]);
    build_log(pages[TAB_LOG]);
    lv_btnmatrix_set_selected_btn(tabbar, 0);   /* so the first remote RIGHT moves to "Video" */

    build_bottombar(scr);

    /* keyboard: floating bottom sheet, its lower corners pushed off-screen */
    g_kb = lv_keyboard_create(scr);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(g_kb, KB_W, KB_H + R_LG);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, R_LG);

    /* focus group: tab bar, then every page's controls in order, then the action bar */
    g_group = lv_group_create();
    lv_group_add_obj(g_group, tabbar);
    group_add_children(pages[TAB_HOSTS]);
    group_add_children(pages[TAB_VIDEO]);
    group_add_children(pages[TAB_INPUT]);
    lv_group_add_obj(g_group, g_conn_ui.connect);
    lv_group_add_obj(g_group, g_conn_ui.disc);
    lv_group_set_default(g_group);
    lv_group_set_focus_cb(g_group, group_focus_cb);

    host_select(0);
    conn_set(CONN_IDLE);
    status_timer_cb(NULL);
    lv_timer_create(status_timer_cb, 500, NULL);
    ui_log_backlog();
    ui_log("[APP]", COL_ACCENT_HI, "ready");
    ui_log("[NET]", COL_INFO, "wlan0 10.10.10.12");
    ui_log("[CHIAKI]", COL_WARN, "lib not linked in this probe");
    ui_log("[INPUT]", COL_OK, "L-stick cursor, R-stick scroll, D-pad navigate, Cross select, L1/R1 tabs, Options quit");
}

static void indev_init(void) {
    static lv_indev_drv_t key_drv;
    lv_indev_drv_init(&key_drv);
    key_drv.type = LV_INDEV_TYPE_KEYPAD;
    key_drv.read_cb = kb_read_cb;
    lv_indev_set_group(lv_indev_drv_register(&key_drv), g_group);

    static lv_indev_drv_t ptr_drv;
    lv_indev_drv_init(&ptr_drv);
    ptr_drv.type = LV_INDEV_TYPE_POINTER;
    ptr_drv.read_cb = ptr_read_cb;
    g_ptr_indev = lv_indev_drv_register(&ptr_drv);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    g_log = fopen(LOG_PATH, "w");
    logf_("chiaki starting (LVGL %d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { logf_("SDL_Init: %s", SDL_GetError()); return 1; }
    lv_init();

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0) { g_w = mode.w; g_h = mode.h; }
    logf_("display mode %dx%d", g_w, g_h);
    g_win = SDL_CreateWindow("chiaki", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             g_w, g_h, SDL_WINDOW_FULLSCREEN);
    if (!g_win) { logf_("window: %s", SDL_GetError()); return 1; }

    display_init();
    if (!g_tex) return 1;
    theme_init();
    ui_create();
    logf_("UI created (%dx%d)", g_w, g_h);

    cursor_create();
    if (gamepad_try_acquire()) {
        cursor_show(true);
        ui_log("[INPUT]", COL_OK, "DualShock 4 ready, stick moves the cursor");
    } else {
        logf_("no gamepad found");
        ui_log("[INPUT]", COL_WARN, "no gamepad found, use the TV remote (still looking)");
    }
    indev_init();
    logf_("indev ready");

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = false;
                else {
                    push_key(map_key(k));
                    keypad_mode();
                }
            }
        }
        gamepad_tick();
        lv_timer_handler();
        SDL_Delay(5);
    }
    logf_("exiting");
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
