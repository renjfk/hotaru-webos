/* ui/theme.h - design tokens and the shared LVGL theme.
 *
 * All colours, spacing, radii, layout sizes and fonts live here; UI code uses
 * these tokens instead of magic numbers. theme_init() installs a small LVGL
 * theme for what LVGL creates plainly (the screen, containers, the spinner);
 * the st_* styles below are the variants UI code adds itself.
 * Layout target: 1920x1080, nothing may overflow the screen.
 */
#ifndef HOTARU_THEME_H
#define HOTARU_THEME_H

#include <lvgl.h>

/* palette: deep navy surfaces, PlayStation blue accent */
#define COL_BG_TOP      0x0f1f40
#define COL_BG_BOTTOM   0x04060b
#define COL_INSET       0x080d17 /* the log console */
#define COL_TRACK       0x222e4b /* the splash spinner's track */
#define COL_LINE        0x2a3756 /* borders */
#define COL_ACCENT      0x0070d1 /* PlayStation blue */
#define COL_ACCENT_HI   0x3592ff
#define COL_ACCENT_DEEP 0x004496
#define COL_TEXT        0xeef2f8
#define COL_MUTED       0x8f9bb4
#define COL_DIM         0x5c6884
#define COL_OK          0x2fd47a
#define COL_WARN        0xffb21e
#define COL_ERR         0xff5468
#define COL_INFO        0x58c6ff
#define COL_DANGER_TEXT 0xff8f9d
#define COL_PS_CROSS    0x9dbcff /* button hint glyph */
#define COL_PS_CIRCLE   0xff8f9a /* button hint glyph */
#define COL_PS_TRIANGLE 0x3fd3b6 /* face-button glyphs on Home and the stream menu */
#define COL_PS_SQUARE   0xe58ad8

/* a battery level, in percent: at or below these it reads amber, then red */
#define BATT_LOW  20
#define BATT_CRIT 10

/* Home and the stream menu: focus-driven screens on a tinted backdrop */
#define COL_FROST        0xffffff /* frosted surfaces: white at a low opacity, solid when focused */
#define COL_ON_FROST     0x0a1020 /* text and icons on a solid (focused) frost surface */
#define COL_SHADE        0x000000 /* wells and summary cards: black at a low opacity */
#define COL_SHEET        0x070b16 /* near-black sheets: stream menu, pop-up menus, toast, hint chips */
#define COL_SUBTLE       0xb4bfd6 /* secondary text on a tinted backdrop */
#define COL_PSN          0x2bd2c4 /* PSN avatar gradient */
#define COL_PSN_DEEP     0x0b6f9c
#define COL_PSN_HI       0x5fe0d2 /* PSN's accent: progress, tags on the PSN path */
#define COL_TINT_LAN     0x1d57b8 /* backdrops: the local-network path, */
#define COL_TINT_PSN     0x0e6b78 /* the PSN path, */
#define COL_TINT_NEUTRAL 0x24324f /* and neither (Add a console) */

/* spacing: 4px base, 8px rhythm */
#define SP_S   8
#define SP_M   12
#define SP_L   16
#define SP_XL  24
#define SP_2XL 32

/* corner radii */
#define R_SM   10
#define R_MD   16
#define R_PILL LV_RADIUS_CIRCLE

/* layout (1920x1080 reference; everything fits without scrolling) */
#define KB_W 1480
#define KB_H 420 /* the on-screen keyboard, without its off-screen corners */

/* typography */
#define FONT_HERO    (&lv_font_montserrat_48) /* Home's titles */
#define FONT_DISPLAY (&lv_font_montserrat_40)
#define FONT_TITLE   (&lv_font_montserrat_32)
#define FONT_HEADING (&lv_font_montserrat_28)
#define FONT_BODY    (&lv_font_montserrat_24)
#define FONT_CAPTION (&lv_font_montserrat_20)
#define FONT_SMALL   (&lv_font_montserrat_16)
#define FONT_ICON    (&font_icons_72) /* tile icons, drawn at the focused size: only their four symbols */
LV_FONT_DECLARE(font_icons_72)

typedef enum { TXT_DISPLAY, TXT_TITLE, TXT_HEADING, TXT_BODY, TXT_CAPTION, TXT_OVERLINE, TXT__COUNT } txt_kind_t;

#define C(hex) lv_color_hex(hex)
#define BULLET "  " LV_SYMBOL_BULLET "  "

/* style variants for UI code (the theme styles plain widgets on its own) */
extern lv_style_t st_txt[TXT__COUNT]; /* the type scale (the splash, the QR placeholder) */
extern lv_style_t st_screen, st_console;

void theme_init(lv_disp_t *disp);

#endif
