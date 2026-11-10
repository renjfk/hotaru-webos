/* ui/theme.h - design tokens and the shared LVGL theme.
 *
 * All colours, spacing, radii, layout sizes and fonts live here; UI code uses
 * these tokens instead of magic numbers. theme_init() installs a custom LVGL
 * theme (on top of the default theme as fallback) that styles every standard
 * widget; the st_* styles below are the extra variants UI code adds itself.
 * Layout target: 1920x1080, nothing may overflow the screen.
 */
#ifndef CHIAKI_THEME_H
#define CHIAKI_THEME_H

#include <lvgl.h>

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

#define C(hex) lv_color_hex(hex)
#define BULLET "  " LV_SYMBOL_BULLET "  "

/* style variants for UI code (the theme styles plain widgets on its own) */
extern lv_style_t st_txt[TXT__COUNT];
extern lv_style_t st_screen, st_card, st_card_hero, st_tile, st_row, st_bar_bottom;
extern lv_style_t st_chip, st_badge, st_badge_soft, st_glyph, st_console;
extern lv_style_t st_btn_secondary, st_btn_danger, st_btn_ghost;

void theme_init(lv_disp_t *disp);

#endif
