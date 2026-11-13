/* ui/splash.c - the startup overlay: the app mark and a short "starting" state.
 *
 * The mark is ui_logo() (the same glyph as the icon), so the splash,
 * the top bar and the launcher all show the same thing. The overlay is on the
 * top layer, swallows input, and deletes itself when dismissed.
 */
#include "splash.h"

#include <stdlib.h>

#include "../log.h"
#include "logo.h"
#include "theme.h"
#include "widgets.h"

#define SPLASH_MS 1500 /* how long the overlay stays up before it dismisses */
#define MARK      300  /* mark size (px) */

static lv_obj_t *g_root;
static lv_timer_t *g_timer;

/* CHIAKI_SPLASH_MS overrides the hold time (dev: keep the splash up to inspect it) */
static uint32_t splash_ms(void) {
    const char *v = getenv("CHIAKI_SPLASH_MS");
    int ms = v && *v ? atoi(v) : 0;
    return ms > 0 ? (uint32_t)ms : SPLASH_MS;
}

static void dismiss_cb(lv_timer_t *t) {
    (void)t;
    g_timer = NULL; /* one-shot: LVGL frees it after this call */
    splash_hide();
}

void splash_hide(void) {
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
    if (g_root) {
        lv_obj_del(g_root);
        g_root = NULL;
    }
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
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_CLICKABLE); /* swallow input while it is up */
    lv_obj_set_flex_flow(g_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_root, SP_2XL, 0);

    ui_logo(g_root, MARK);
    ui_text(g_root, TXT_DISPLAY, "Chiaki webOS");
    lv_obj_t *sub = ui_text(g_root, TXT_CAPTION, "PlayStation Remote Play");
    lv_obj_set_style_text_color(sub, C(COL_MUTED), 0);

    lv_obj_t *row = ui_box(g_root, LV_FLEX_FLOW_ROW, SP_M);
    lv_obj_set_style_pad_top(row, SP_M, 0);
    lv_obj_t *sp = lv_spinner_create(row, 1000, 60);
    lv_obj_set_size(sp, 32, 32);
    lv_obj_set_style_arc_color(sp, C(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_t *lbl = ui_text(row, TXT_BODY, "Starting up");
    lv_obj_set_style_text_color(lbl, C(COL_MUTED), 0);

    lv_obj_fade_in(g_root, 300, 0);
    g_timer = lv_timer_create(dismiss_cb, splash_ms(), NULL);
    lv_timer_set_repeat_count(g_timer, 1);
    log_event(LOG_INFO, "APP", "splash shown");
}
