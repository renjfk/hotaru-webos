/* ui/page_input.c - Input tab: controller options (config_get()->input) + console registration. */
#include <stdbool.h>

#include "../app.h"
#include "../config/config.h"
#include "../input/input.h"
#include "pages.h"
#include "widgets.h"

static lv_obj_t *g_speed_val, *g_ip, *g_pin;

static void switch_cb(lv_event_t *e) {
    bool *flag = lv_event_get_user_data(e);
    *flag = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void speed_cb(lv_event_t *e) {
    int pct = lv_slider_get_value(lv_event_get_target(e));
    config_get()->input.cursor_speed = pct;
    input_set_cursor_speed(pct);
    lv_label_set_text_fmt(g_speed_val, "%d%%", pct);
}

static void register_cb(lv_event_t *e) {
    (void)e;
    app_register(lv_textarea_get_text(g_ip), lv_textarea_get_text(g_pin));
}

static lv_obj_t *mk_textarea(lv_obj_t *parent, const char *text, const char *placeholder, const char *accept,
                             uint32_t max_len) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_accepted_chars(ta, accept);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_text(ta, text);
    lv_obj_set_width(ta, 300);
    ui_keyboard_attach(ta, LV_KEYBOARD_MODE_NUMBER);
    return ta;
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
    lv_obj_t *speed = ui_slider_control(row, 50, 200, in->cursor_speed, 10, speed_cb, &g_speed_val);
    lv_event_send(speed, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_registration(lv_obj_t *page) {
    lv_obj_t *reg = ui_card(page, NULL);
    lv_obj_set_height(reg, LV_PCT(100));
    lv_obj_set_flex_grow(reg, 1);
    lv_obj_set_style_pad_row(reg, 0, 0);
    lv_obj_t *hdr = ui_card_header(reg, LV_SYMBOL_EDIT, "Registration", "Pair this TV with a console");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    lv_obj_t *row = ui_setting_row(reg, "PS5 IP address", "Same network as the TV");
    g_ip = mk_textarea(row, "192.168.1.100", "192.168.1.100", "0123456789.", 15);
    row = ui_setting_row(reg, "Registration PIN", "Shown on the console");
    ui_last_row(row);
    g_pin = mk_textarea(row, "", "8 digits", "0123456789", 8);
    lv_obj_t *act = ui_box(reg, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_top(act, SP_L, 0);
    lv_obj_t *b = ui_btn(act, BTN_PRIMARY, LV_SYMBOL_OK, "Register");
    lv_obj_add_event_cb(b, register_cb, LV_EVENT_CLICKED, NULL);
    ui_spacer(reg);
    ui_text(reg, TXT_CAPTION, "On the PS5: Settings > System > Remote Play > Link Device");
}

void page_input_build(lv_obj_t *page) {
    build_controller(page, &config_get()->input);
    build_registration(page);
}
