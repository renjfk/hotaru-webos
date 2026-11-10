/* ui/ui.c - assembles the screen and connects it to app/input (see ui.h). */
#include "ui.h"

#include <stdio.h>

#include "../chiaki/session.h"
#include "../log.h"
#include "logo.h"
#include "log_view.h"
#include "pages.h"
#include "tabs.h"
#include "widgets.h"

static const char *const g_tab_names[TAB__COUNT] = { "Hosts", "Video", "Input", "Log" };
static const char *const g_tab_icons[TAB__COUNT] = { LV_SYMBOL_HOME, LV_SYMBOL_VIDEO, LV_SYMBOL_SHUFFLE, LV_SYMBOL_LIST };

static lv_group_t *g_group;
static lv_obj_t *g_kb;
static struct {
    lv_obj_t *connect, *disc;
    ui_status_t status, pad;
} g_bar;
static pad_state_t g_pad_shown = (pad_state_t)-1;

/* ---------- app + input hooks ---------- */

static void action_bar_refresh(void) {
    conn_state_t s = app_conn_state();
    static const char *const txt[] = { "Idle", "Connecting", "Streaming" };
    static const uint32_t col[] = { COL_DIM, COL_WARN, COL_OK };
    ui_status_set(&g_bar.status, col[s], txt[s], s == CONN_CONNECTING);
    if (s == CONN_IDLE) {
        lv_obj_clear_state(g_bar.connect, LV_STATE_DISABLED);
        lv_obj_add_state(g_bar.disc, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(g_bar.connect, LV_STATE_DISABLED);
        lv_obj_clear_state(g_bar.disc, LV_STATE_DISABLED);
    }
}

void ui_app_changed(app_change_t what) {
    switch (what) {
        case APP_CHANGED_HOST: page_hosts_refresh(); break;
        case APP_CHANGED_CONN: page_log_refresh(); action_bar_refresh(); break;
        case APP_CHANGED_PROGRESS: page_log_progress(); break;
    }
}

void ui_tab_step(int dir, const char *button) { tabs_step(dir, button); }

/* top-bar controller light: green when grabbed, red once lost, grey if never found */
void ui_set_pad_state(pad_state_t state) {
    if (state == g_pad_shown || !g_bar.pad.led) return;
    g_pad_shown = state;
    static const uint32_t col[] = { COL_DIM, COL_OK, COL_ERR };
    static const char *const txt[] = { "No controller", "DualShock 4", "Controller offline" };
    ui_status_set(&g_bar.pad, col[state], txt[state], false);
}

lv_obj_t *ui_scroll_fallback(void) { return tabs_active() == TAB_LOG ? page_log_console() : NULL; }

lv_group_t *ui_group(void) { return g_group; }

/* ---------- keyboard ---------- */

static void textarea_cb(lv_event_t *e) {
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

void ui_keyboard_attach(lv_obj_t *ta, lv_keyboard_mode_t mode) {
    lv_obj_add_event_cb(ta, textarea_cb, LV_EVENT_ALL, (void *)(intptr_t)mode);
}

/* ---------- screen ---------- */

static void connect_cb(lv_event_t *e) { (void)e; app_connect(); }
static void disconnect_cb(lv_event_t *e) { (void)e; app_disconnect(); }

static void build_topbar(lv_obj_t *scr) {
    lv_obj_t *top = ui_box(scr, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_size(top, LV_PCT(100), TOPBAR_H);
    lv_obj_set_style_pad_hor(top, PAGE_MARGIN, 0);
    lv_obj_set_flex_grow(top, 0);
    ui_logo(top, TILE_MD);
    lv_obj_t *col = ui_box(top, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_text(col, TXT_TITLE, "Chiaki webOS");
    ui_text(col, TXT_CAPTION, "PlayStation Remote Play");
    ui_spacer(top);
    ui_status_init(&g_bar.pad, top, COL_DIM, "No controller");
    char ver[32];
    snprintf(ver, sizeof(ver), "LVGL %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ui_chip(top, LV_SYMBOL_SETTINGS, ver, NULL);
}

static void build_action_bar(lv_obj_t *scr) {
    lv_obj_t *bot = ui_box(scr, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_add_style(bot, &st_bar_bottom, 0);
    lv_obj_set_size(bot, LV_PCT(100), BOTBAR_H);
    lv_obj_set_style_pad_hor(bot, PAGE_MARGIN, 0);
    lv_obj_set_flex_grow(bot, 0);
    g_bar.connect = ui_btn(bot, BTN_PRIMARY, LV_SYMBOL_PLAY, "Connect");
    lv_obj_add_event_cb(g_bar.connect, connect_cb, LV_EVENT_CLICKED, NULL);
    g_bar.disc = ui_btn(bot, BTN_DANGER, LV_SYMBOL_STOP, "Disconnect");
    lv_obj_add_event_cb(g_bar.disc, disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gap = ui_box(bot, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_width(gap, SP_S);
    ui_status_init(&g_bar.status, bot, COL_DIM, "Idle");
    ui_spacer(bot);
    ui_hint(bot, "L", COL_TEXT, "Cursor");
    ui_hint(bot, "R", COL_TEXT, "Scroll");
    ui_hint(bot, "L1  R1", COL_TEXT, "Tabs");
    ui_hint(bot, "X", COL_PS_CROSS, "Select");
    ui_hint(bot, LV_SYMBOL_BARS, COL_TEXT, "Quit");
}

static void build_keyboard(lv_obj_t *scr) {
    /* floating bottom sheet, its lower corners pushed off-screen */
    g_kb = lv_keyboard_create(scr);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(g_kb, KB_W, KB_H + R_LG);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, R_LG);
}

static void group_add_controls(lv_obj_t *parent) {
    uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, (int32_t)i);
        if (lv_obj_check_type(c, &lv_dropdown_class) || lv_obj_check_type(c, &lv_slider_class) ||
            lv_obj_check_type(c, &lv_switch_class) || lv_obj_check_type(c, &lv_textarea_class) ||
            lv_obj_check_type(c, &lv_btn_class) || lv_obj_check_type(c, &lv_list_btn_class))
            lv_group_add_obj(g_group, c);
        else
            group_add_controls(c);
    }
}

void ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &st_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);   /* top bar, tabs (grow), action bar */

    build_topbar(scr);
    tabs_create(scr, g_tab_names, g_tab_icons, TAB__COUNT);
    page_hosts_build(tabs_page(TAB_HOSTS));
    page_video_build(tabs_page(TAB_VIDEO));
    page_input_build(tabs_page(TAB_INPUT));
    page_log_build(tabs_page(TAB_LOG));
    build_action_bar(scr);
    build_keyboard(scr);

    /* keypad focus order: tab bar, each page's controls, action bar */
    g_group = lv_group_create();
    lv_group_add_obj(g_group, tabs_bar());
    for (int i = 0; i < TAB__COUNT; i++) group_add_controls(tabs_page(i));
    lv_group_add_obj(g_group, g_bar.connect);
    lv_group_add_obj(g_group, g_bar.disc);
    lv_group_set_default(g_group);
    tabs_bind_group(g_group);

    action_bar_refresh();
    log_view_demo_backlog();
    log_event(LOG_INFO, "APP", "ready");
    char ifname[32], ip[64];
    if (app_local_ip(ifname, sizeof(ifname), ip, sizeof(ip))) log_event(LOG_INFO, "NET", "%s %s", ifname, ip);
    else log_event(LOG_WARN, "NET", "no IPv4 interface up");
    if (!session_available()) log_event(LOG_WARN, "CHIAKI", "libchiaki not linked yet, sessions are simulated");
    log_event(LOG_OK, "INPUT", "L-stick cursor, R-stick scroll, D-pad navigate, Cross select, L1/R1 tabs, Options quit");
}
