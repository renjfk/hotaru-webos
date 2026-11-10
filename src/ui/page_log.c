/* ui/page_log.c - Log tab: connection status header + the scrollable console. */
#include "../app.h"
#include "log_view.h"
#include "pages.h"
#include "widgets.h"

static lv_obj_t *g_spinner, *g_icon, *g_state, *g_bar, *g_pct, *g_console;

void page_log_progress(void) {
    int p = app_conn_progress();
    lv_bar_set_value(g_bar, p, LV_ANIM_ON);
    lv_label_set_text_fmt(g_pct, "%d%%", p);
}

void page_log_refresh(void) {
    conn_state_t s = app_conn_state();
    static const char *const txt[] = { "Idle", "Connecting", "Streaming" };
    page_log_progress();
    if (s == CONN_CONNECTING) {
        lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_icon, s == CONN_STREAMING ? LV_SYMBOL_OK : LV_SYMBOL_PAUSE);
        lv_obj_set_style_text_color(g_icon, C(s == CONN_STREAMING ? COL_OK : COL_DIM), 0);
    }
    lv_label_set_text(g_state, txt[s]);
}

lv_obj_t *page_log_console(void) { return g_console; }

void page_log_build(lv_obj_t *page) {
    lv_obj_t *card = ui_card(page, NULL);
    lv_obj_set_size(card, LV_PCT(100), LV_PCT(100));
    lv_obj_t *hdr = ui_card_header(card, LV_SYMBOL_LIST, "Session log", "Live diagnostics from the streaming session");
    lv_obj_t *st = ui_box(hdr, LV_FLEX_FLOW_ROW, SP_L);
    g_spinner = lv_spinner_create(st, 1000, 60);
    lv_obj_set_size(g_spinner, 36, 36);
    g_icon = ui_text(st, TXT_HEADING, LV_SYMBOL_PAUSE);
    g_state = ui_text(st, TXT_BODY, "Idle");
    lv_obj_set_width(g_state, 150);
    g_bar = lv_bar_create(st);
    lv_obj_set_size(g_bar, 320, 14);
    g_pct = ui_text(st, TXT_CAPTION, "0%");
    lv_obj_set_width(g_pct, 60);
    lv_obj_set_style_text_align(g_pct, LV_TEXT_ALIGN_RIGHT, 0);
    g_console = log_view_create(card);
    page_log_refresh();
}
