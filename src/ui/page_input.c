/* ui/page_input.c - Input tab: controller options (config_get()->input) + registration summary. */
#include <stdbool.h>

#include "../app.h"
#include "../config/config.h"
#include "../input/input.h"
#include "pages.h"
#include "register.h"
#include "widgets.h"

static lv_obj_t *g_speed_val, *g_consoles, *g_psn;

static void switch_cb(lv_event_t *e) {
    bool *flag = lv_event_get_user_data(e);
    *flag = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    app_settings_changed();
}

static void show_speed(int pct) {
    input_set_cursor_speed(pct);
    lv_label_set_text_fmt(g_speed_val, "%d%%", pct);
}

static void speed_cb(lv_event_t *e) {
    int pct = lv_slider_get_value(lv_event_get_target(e));
    config_get()->input.cursor_speed = pct;
    show_speed(pct);
    app_settings_changed();
}

static void add_cb(lv_event_t *e) { (void)e; ui_register_open(); }

void page_input_refresh(void) {
    const config_psn_t *psn = config_psn();
    lv_label_set_text_fmt(g_consoles, "%d", app_host_count());
    if (psn->linked) lv_label_set_text_fmt(g_psn, "%s", psn->online_id[0] ? psn->online_id : "Linked");
    else lv_label_set_text(g_psn, "Not linked");
}

static void build_controller(lv_obj_t *page, input_settings_t *in) {
    lv_obj_t *pad = ui_card(page, NULL);
    lv_obj_set_height(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_style_pad_row(pad, 0, 0);
    lv_obj_t *hdr = ui_card_header(pad, LV_SYMBOL_SHUFFLE, "Controller", "DualShock 4 and TV remote");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    const struct { const char *name, *caption; bool *flag; } opts[] = {
        { "Rumble", "Vibrate with in-game feedback", &in->rumble },
        { "Touchpad gestures", "Map swipes to the PS touchpad", &in->touchpad },
        { "Motion controls", "Use the gyro for aiming", &in->motion },
    };
    for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
        lv_obj_t *row = ui_setting_row(pad, opts[i].name, opts[i].caption);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 84, 44);
        if (*opts[i].flag) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, opts[i].flag);
    }
    lv_obj_t *row = ui_setting_row(pad, "Cursor speed", "Left stick pointer speed");
    ui_last_row(row);
    ui_slider_control(row, 50, 200, in->cursor_speed, 10, speed_cb, &g_speed_val);
    show_speed(in->cursor_speed);
}

static void build_registration(lv_obj_t *page) {
    lv_obj_t *reg = ui_card(page, NULL);
    lv_obj_set_height(reg, LV_PCT(100));
    lv_obj_set_flex_grow(reg, 1);
    lv_obj_set_style_pad_row(reg, 0, 0);
    lv_obj_t *hdr = ui_card_header(reg, LV_SYMBOL_EDIT, "Registration", "Pair this TV with a console");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    lv_obj_t *row = ui_setting_row(reg, "Consoles", "Registered with this TV");
    g_consoles = ui_text(row, TXT_BODY, "");
    row = ui_setting_row(reg, "PSN account", "Pairs over PSN and finds your consoles");
    ui_last_row(row);
    g_psn = ui_text(row, TXT_BODY, "");
    lv_obj_t *act = ui_box(reg, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_top(act, SP_L, 0);
    lv_obj_t *b = ui_btn(act, BTN_PRIMARY, LV_SYMBOL_PLUS, "Add a console");
    lv_obj_add_event_cb(b, add_cb, LV_EVENT_CLICKED, NULL);
    ui_spacer(reg);
    ui_text(reg, TXT_CAPTION, "On the PS5: Settings > System > Remote Play > Link Device");
    page_input_refresh();
}

void page_input_build(lv_obj_t *page) {
    build_controller(page, &config_get()->input);
    build_registration(page);
}
