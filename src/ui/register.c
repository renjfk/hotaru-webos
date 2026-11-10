/* ui/register.c - the "Add a console" overlay (see register.h).
 *
 * Layout: top bar (title + Close), then a method/status card on the left and
 * the active method's pane on the right. The text fields sit in the upper
 * half so the on-screen keyboard (bottom sheet) never covers the one in use.
 */
#include "register.h"

#include <stdint.h>
#include <string.h>

#include "../app.h"
#include "../config/config.h"
#include "../input/input.h"
#include "../log.h"
#include "pages.h"
#include "qr.h"
#include "ui.h"
#include "widgets.h"

#define METHODS 2

static const char *const g_method_names[METHODS] = { "PIN from the console", "PSN account" };
static const char *const g_method_icons[METHODS] = { LV_SYMBOL_KEYBOARD, LV_SYMBOL_WIFI };
static const char *const g_idle_hint[METHODS] = {
    "Enter the console's IP address and the PIN it shows, then choose Register.",
    "Sign in on your phone, type the code from the page you land on, then choose Link.",
};

static struct {
    lv_obj_t *root;
    lv_group_t *group;
    lv_obj_t *close, *method[METHODS], *console, *pane[METHODS];
    lv_obj_t *addr, *pin, *submit_pin;            /* PIN pane */
    lv_obj_t *qr, *code, *link, *use_linked, *account;   /* PSN pane */
    ui_status_t status;
    lv_obj_t *bar, *msg;
    app_reg_state_t shown;
} g;
static app_reg_method_t g_method = APP_REG_METHOD_PIN;   /* remembered between opens */

static void set_enabled(lv_obj_t *o, bool on) {
    if (on) lv_obj_clear_state(o, LV_STATE_DISABLED);
    else lv_obj_add_state(o, LV_STATE_DISABLED);
}

static void focus(lv_obj_t *o) {
    lv_group_focus_obj(o);
    if (input_keypad_mode()) lv_obj_add_state(o, LV_STATE_FOCUS_KEY);   /* show the ring */
}

static bool target_ps5(void) { return lv_dropdown_get_selected(g.console) == 0; }

/* ---------- actions ---------- */

static void set_method(app_reg_method_t m) {
    g_method = m;
    for (int i = 0; i < METHODS; i++) {
        lv_obj_remove_style(g.method[i], &st_btn_secondary, 0);   /* active = primary look */
        if (i != (int)m) lv_obj_add_style(g.method[i], &st_btn_secondary, 0);
        if (i == (int)m) lv_obj_clear_flag(g.pane[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g.pane[i], LV_OBJ_FLAG_HIDDEN);
    }
    app_reg_cancel();   /* drop the other method's result; no-op while idle */
    ui_register_refresh();
}

static void method_cb(lv_event_t *e) {
    if (app_reg_state() != APP_REG_RUNNING) set_method((app_reg_method_t)(intptr_t)lv_event_get_user_data(e));
}
static void pin_cb(lv_event_t *e) {
    (void)e;
    app_reg_submit_pin(lv_textarea_get_text(g.addr), lv_textarea_get_text(g.pin), target_ps5());
}
static void link_cb(lv_event_t *e) {
    (void)e;
    app_reg_submit_psn_code(lv_textarea_get_text(g.code), target_ps5());
}
static void use_linked_cb(lv_event_t *e) {
    (void)e;
    if (config_psn()->linked) app_reg_submit_psn(config_psn()->account_id, target_ps5());
}
/* from an event of one of our own controls: close after LVGL is done with that event,
 * since the indev still holds the control (and the keypad sends CANCEL after ESC) */
static void close_async_cb(void *user) { (void)user; ui_register_close(); }
static void close_cb(lv_event_t *e) { (void)e; lv_async_call(close_async_cb, NULL); }
static void esc_cb(lv_event_t *e) {   /* remote BACK */
    if (lv_event_get_key(e) == LV_KEY_ESC) lv_async_call(close_async_cb, NULL);
}

/* ---------- building ---------- */

static lv_obj_t *button(lv_obj_t *parent, btn_kind_t kind, const char *icon, const char *text, lv_event_cb_t cb,
                        void *user) {
    lv_obj_t *b = ui_btn(parent, kind, icon, text);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(b, esc_cb, LV_EVENT_KEY, NULL);
    return b;
}

static lv_obj_t *textarea(lv_obj_t *parent, const char *placeholder, const char *accept, uint32_t max_len,
                          lv_keyboard_mode_t mode) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (accept) lv_textarea_set_accepted_chars(ta, accept);
    lv_textarea_set_max_length(ta, max_len);
    lv_obj_set_width(ta, CTRL_W);
    ui_keyboard_attach(ta, mode);
    return ta;
}

static lv_obj_t *wrapped(lv_obj_t *parent, txt_kind_t kind, const char *text) {
    lv_obj_t *l = ui_text(parent, kind, text);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void build_topbar(lv_obj_t *root) {
    lv_obj_t *top = ui_box(root, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_size(top, LV_PCT(100), TOPBAR_H);
    ui_tile(top, &st_badge_soft, LV_SYMBOL_PLUS, TILE_MD, NULL);
    lv_obj_t *col = ui_box(top, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_text(col, TXT_TITLE, "Add a console");
    ui_text(col, TXT_CAPTION, "Pair this TV with a PS5 or PS4 for Remote Play");
    ui_spacer(top);
    g.close = button(top, BTN_SECONDARY, LV_SYMBOL_CLOSE, "Close", close_cb, NULL);
}

static void build_side(lv_obj_t *body) {
    lv_obj_t *side = ui_card(body, NULL);
    lv_obj_set_size(side, REG_SIDE_W, LV_PCT(100));
    ui_card_header(side, LV_SYMBOL_SETTINGS, "How to pair", "Either way the console lands on Hosts");
    for (int i = 0; i < METHODS; i++) {
        g.method[i] = button(side, BTN_PRIMARY, g_method_icons[i], g_method_names[i], method_cb, (void *)(intptr_t)i);
        lv_obj_set_width(g.method[i], LV_PCT(100));
    }
    ui_text(side, TXT_CAPTION, "Console");
    g.console = ui_dropdown(side, "PS5\nPS4", 0, NULL);
    lv_obj_set_width(g.console, LV_PCT(100));
    ui_spacer(side);
    ui_text(side, TXT_OVERLINE, "STATUS");
    ui_status_init(&g.status, side, COL_DIM, "Ready");
    g.bar = lv_bar_create(side);
    lv_obj_set_size(g.bar, LV_PCT(100), 14);
    g.msg = wrapped(side, TXT_CAPTION, "");
}

static void build_pin_pane(lv_obj_t *card) {
    lv_obj_t *p = g.pane[APP_REG_METHOD_PIN] = ui_box(card, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *hdr = ui_card_header(p, LV_SYMBOL_KEYBOARD, "Link with a PIN",
                                   "On the console: Settings > System > Remote Play > Link Device");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);
    lv_obj_t *row = ui_setting_row(p, "IP address", "The console's address on your network");
    g.addr = textarea(row, "192.168.1.100", "0123456789.", 15, LV_KEYBOARD_MODE_NUMBER);
    row = ui_setting_row(p, "PIN", "The 8 digits the console shows");
    ui_last_row(row);
    g.pin = textarea(row, "12345678", "0123456789", 8, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_t *act = ui_box(p, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_top(act, SP_L, 0);
    g.submit_pin = button(act, BTN_PRIMARY, LV_SYMBOL_OK, "Register", pin_cb, NULL);
}

static void build_psn_pane(lv_obj_t *card) {
    lv_obj_t *p = g.pane[APP_REG_METHOD_PSN] = ui_box(card, LV_FLEX_FLOW_COLUMN, SP_L);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_card_header(p, LV_SYMBOL_WIFI, "Sign in with PSN", "Scan with your phone and sign in; the page you land on "
                                                           "has the code");
    lv_obj_t *content = ui_box(p, LV_FLEX_FLOW_ROW, SP_2XL);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    g.qr = ui_qr(content, app_reg_psn_url(), QR_SIZE);

    lv_obj_t *col = ui_box(content, LV_FLEX_FLOW_COLUMN, SP_M);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    wrapped(col, TXT_BODY, "1.  Scan the code, or open the link below");
    wrapped(col, TXT_BODY, "2.  Sign in to PlayStation Network");
    wrapped(col, TXT_BODY, "3.  Type the code= value from the address bar");
    lv_obj_t *entry = ui_box(col, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_ver(entry, SP_S, 0);
    g.code = textarea(entry, "Code", NULL, 128, LV_KEYBOARD_MODE_TEXT_LOWER);
    g.link = button(entry, BTN_PRIMARY, LV_SYMBOL_OK, "Link", link_cb, NULL);
    ui_text(col, TXT_OVERLINE, "OR OPEN THIS LINK");
    lv_obj_t *url = wrapped(col, TXT_CAPTION, app_reg_psn_url());
    lv_obj_set_style_text_font(url, FONT_SMALL, 0);
    lv_obj_t *acc = ui_box(col, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_style_pad_top(acc, SP_S, 0);
    g.use_linked = button(acc, BTN_SECONDARY, LV_SYMBOL_REFRESH, "Use linked account", use_linked_cb, NULL);
    g.account = ui_text(acc, TXT_CAPTION, "");
}

static void build(void) {
    g.root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g.root);
    lv_obj_add_style(g.root, &st_screen, 0);
    lv_obj_set_size(g.root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(g.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g.root, LV_OBJ_FLAG_CLICKABLE);   /* swallow input meant for the screen below */
    lv_obj_set_flex_flow(g.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(g.root, PAGE_MARGIN, 0);
    lv_obj_set_style_pad_bottom(g.root, PAGE_MARGIN, 0);

    build_topbar(g.root);
    lv_obj_t *body = ui_box(g.root, LV_FLEX_FLOW_ROW, SP_XL);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    build_side(body);
    lv_obj_t *card = ui_card(body, NULL);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    build_pin_pane(card);
    build_psn_pane(card);
}

/* ---------- public ---------- */

void ui_register_refresh(void) {
    if (!g.root) return;
    app_reg_state_t s = app_reg_state();
    static const uint32_t col[] = { COL_DIM, COL_WARN, COL_OK, COL_ERR };
    static const char *const txt[] = { "Ready", "Pairing", "Paired", "Failed" };
    ui_status_set(&g.status, col[s], txt[s], s == APP_REG_RUNNING);
    lv_bar_set_value(g.bar, s == APP_REG_RUNNING || s == APP_REG_SUCCESS ? app_reg_progress() : 0, LV_ANIM_ON);
    lv_label_set_text(g.msg, s == APP_REG_FAILED ? app_reg_error() : s == APP_REG_IDLE ? g_idle_hint[g_method]
                                                                                        : app_reg_status());
    lv_obj_set_style_text_color(g.msg, C(s == APP_REG_FAILED ? COL_DANGER_TEXT : s == APP_REG_SUCCESS ? COL_OK
                                                                                                       : COL_MUTED), 0);
    bool busy = s == APP_REG_RUNNING;
    const config_psn_t *psn = config_psn();
    for (int i = 0; i < METHODS; i++) set_enabled(g.method[i], !busy);
    set_enabled(g.console, !busy);
    set_enabled(g.submit_pin, !busy);
    set_enabled(g.link, !busy);
    set_enabled(g.use_linked, !busy && psn->linked);
    if (psn->linked) lv_label_set_text_fmt(g.account, "Linked: %s", psn->online_id);
    else lv_label_set_text(g.account, "No PSN account linked yet");
    lv_label_set_text(lv_obj_get_child(g.close, 1), s == APP_REG_SUCCESS ? "Done" : "Close");
    if (s == APP_REG_SUCCESS && g.shown != s) focus(g.close);
    g.shown = s;
}

void ui_register_open(void) {
    if (g.root) return;
    app_reg_cancel();   /* start clean: no result left from last time */
    lv_group_t *def = lv_group_get_default();
    lv_group_set_default(NULL);   /* our controls join our own group, not the screen's */
    build();
    lv_group_set_default(def);

    /* keypad order: method, console, the panes top to bottom, Close (hidden panes are skipped) */
    g.group = lv_group_create();
    lv_obj_t *order[] = { g.method[0], g.method[1], g.console, g.addr, g.pin, g.submit_pin,
                          g.code, g.link, g.use_linked, g.close };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) lv_group_add_obj(g.group, order[i]);
    lv_obj_move_background(g.root);   /* below the stick cursor, which is on the top layer too */
    ui_keyboard_host(g.root);
    input_set_group(g.group);
    g.shown = app_reg_state();
    set_method(g_method);
    focus(g.method[g_method]);
    log_event(LOG_INFO, "REG", "Add a console: PIN or PSN account");
}

void ui_register_close(void) {
    if (!g.root) return;
    app_reg_cancel();
    ui_keyboard_host(NULL);
    input_set_group(ui_group());
    lv_group_del(g.group);
    lv_obj_t *root = g.root;
    memset(&g, 0, sizeof(g));
    lv_obj_del(root);
    lv_obj_t *f = lv_group_get_focused(ui_group());   /* back where the overlay was opened from */
    if (f && input_keypad_mode()) lv_obj_add_state(f, LV_STATE_FOCUS_KEY);
    log_msg("registration overlay closed");
}

bool ui_register_active(void) { return g.root != NULL; }
lv_obj_t *ui_register_root(void) { return g.root; }
lv_group_t *ui_register_group(void) { return g.group; }
