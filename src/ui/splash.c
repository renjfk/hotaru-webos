/* ui/splash.c - the startup overlay: the app mark and a short "starting" state.
 *
 * The mark is the live app icon (the play-waves glyph) drawn procedurally: a
 * rounded gradient tile plus a transparent LVGL canvas holding a white Play
 * triangle and two round-capped signal arcs. It is deleted once dismissed, so
 * the canvas buffer does not stay resident (the TV has ~340 MB free at run).
 */
#include "splash.h"

#include <stdlib.h>

#include "../log.h"
#include "theme.h"
#include "widgets.h"

#define SPLASH_MS 1500   /* how long the overlay stays up before it fades away */
#define MARK      300    /* mark tile + canvas size (px) */
#define MARK_R    68     /* tile corner radius */
#define ARC_W     16     /* signal arc stroke width */

/* mark geometry on a MARK x MARK grid, scaled from the icon's 512 grid */
static const lv_point_t g_tri[] = { { 78, 110 }, { 78, 190 }, { 140, 150 } };

static lv_obj_t *g_root, *g_canvas;
static lv_color_t *g_buf;
static lv_timer_t *g_timer;

static void draw_mark(lv_obj_t *cv) {
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_white();
    rd.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_polygon(cv, g_tri, 3, &rd);

    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_white();
    ad.width = ARC_W;
    ad.rounded = 1;
    ad.opa = LV_OPA_COVER;
    lv_canvas_draw_arc(cv, 166, 150, 52, 270, 90, &ad);   /* 0 deg = right, clockwise: right half */
    lv_canvas_draw_arc(cv, 193, 150, 84, 270, 90, &ad);
}

static void dismiss_cb(lv_timer_t *t) {
    (void)t;
    g_timer = NULL;   /* one-shot: LVGL frees it after this call */
    splash_hide();
}

void splash_hide(void) {
    if (g_timer) { lv_timer_del(g_timer); g_timer = NULL; }
    if (g_root) { lv_obj_del(g_root); g_root = NULL; g_canvas = NULL; }
    if (g_buf) { free(g_buf); g_buf = NULL; }
    log_msg("splash dismissed");
}

bool splash_active(void) { return g_root != NULL; }

void splash_show(void) {
    if (g_root) return;

    g_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_root, C(COL_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_color(g_root, C(COL_BG_TOP), 0);
    lv_obj_set_style_bg_grad_dir(g_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE);   /* swallow input while it is up */
    lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_root, SP_2XL, 0);

    /* the mark: blue gradient tile + transparent canvas with the white glyph */
    lv_obj_t *tile = lv_obj_create(g_root);
    lv_obj_remove_style_all(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tile, MARK, MARK);
    lv_obj_set_style_radius(tile, MARK_R, 0);
    lv_obj_set_style_bg_color(tile, C(0x2a86ea), 0);
    lv_obj_set_style_bg_grad_color(tile, C(0x0a3c8f), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    g_buf = malloc((size_t)MARK * MARK * sizeof(lv_color_t));
    if (g_buf) {
        g_canvas = lv_canvas_create(tile);
        lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_canvas_set_buffer(g_canvas, g_buf, MARK, MARK, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_TRANSP);
        lv_obj_center(g_canvas);
        draw_mark(g_canvas);
    } else {
        log_msg("splash: no canvas buffer, showing the tile only");
    }

    ui_text(g_root, TXT_DISPLAY, "Chiaki");
    lv_obj_t *sub = ui_text(g_root, TXT_CAPTION, "PlayStation Remote Play for webOS");
    lv_obj_set_style_text_color(sub, C(COL_MUTED), 0);

    lv_obj_t *row = ui_box(g_root, LV_FLEX_FLOW_ROW, SP_M);
    lv_obj_set_style_pad_top(row, SP_M, 0);
    lv_obj_t *sp = lv_spinner_create(row, 1000, 60);
    lv_obj_set_size(sp, 32, 32);
    lv_obj_set_style_arc_color(sp, C(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_t *lbl = ui_text(row, TXT_BODY, "Starting up");
    lv_obj_set_style_text_color(lbl, C(COL_MUTED), 0);

    lv_obj_fade_in(g_root, 300, 0);
    g_timer = lv_timer_create(dismiss_cb, SPLASH_MS, NULL);
    lv_timer_set_repeat_count(g_timer, 1);
    log_event(LOG_INFO, "APP", "splash shown");
}