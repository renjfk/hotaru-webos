/* ui/ui.c - assembles the screen and connects it to app/input (see ui.h). */
#include "ui.h"

#include <stdint.h>
#include <stdio.h>

#include "../chiaki/session.h"
#include "../log.h"
#include "controls.h"
#include "dialog.h"
#include "home.h"
#include "register.h"
#include "stream_menu.h"
#include "theme.h"

#define LOGIN_DIGITS 4 /* the console user's login passcode */

static lv_group_t *g_group;
static lv_obj_t *g_sink; /* the main group's only member: every key of every screen */
static bool g_ready, g_covered;

/* ---------- streaming: the menu over the stream ---------- */

static bool g_overlay; /* the stream menu is (being) opened */

bool ui_overlay_open(void) { return stream_menu_visible(); }

/* streaming: Home goes away and the screen turns transparent, so only the stream menu (when
 * open) is drawn over the video; the pad drives the console unless the menu is open */
/* home_fade: Home fades in when it comes back (after a stream), else it is there at once */
static void overlay_apply_ex(bool home_fade) {
    bool streaming = app_conn_state() == CONN_STREAMING;
    if (!streaming && g_overlay) {
        g_overlay = false;
        stream_menu_open(false);
    }
    lv_obj_t *scr = lv_scr_act();
    if (streaming) lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    else lv_obj_remove_local_style_prop(scr, LV_STYLE_BG_OPA, 0);
    home_show(!streaming && !g_covered, home_fade);
    input_set_streaming(streaming && !g_overlay);
}
static void overlay_apply(void) { overlay_apply_ex(true); }

void ui_overlay_toggle(void) {
    if (app_conn_state() != CONN_STREAMING) return;
    if (g_overlay && app_stream_settings_pending()) {
        /* closing the menu with edited video settings: they only apply to a new session */
        g_overlay = false;
        stream_menu_open(false);
        app_restart_stream();
        return;
    }
    g_overlay = !g_overlay;
    stream_menu_open(g_overlay);
    overlay_apply();
    log_event(LOG_INFO, "STREAM", "menu %s over the stream", g_overlay ? "shown" : "hidden");
}

/* a full-screen overlay (Add a console) covers Home: Home stops drawing under it */
void ui_cover(bool on) {
    g_covered = on;
    overlay_apply_ex(false);
}

/* ---------- app + input hooks ---------- */

/* the login passcode sheet reported its result (NULL = cancelled) */
static void login_pin_done(const char *code, void *user) {
    (void)user;
    app_login_pin_submit(code);
}

void ui_app_changed(app_change_t what) {
    if (!g_ready) return;
    home_app_changed(what);
    switch (what) {
        case APP_CHANGED_CONN:
            overlay_apply(); /* streaming: transparent screen, pad -> console; menu state */
            stream_menu_refresh();
            break;
        case APP_CHANGED_HOST:
        case APP_CHANGED_HOSTS: stream_menu_refresh(); break;
        case APP_CHANGED_REG: ui_register_refresh(); break;
        case APP_CHANGED_PIN:
            if (app_login_pin_pending() && !ui_dialog_active()) {
                const app_host_t *h = app_host(app_selected());
                char msg[160];
                snprintf(msg, sizeof(msg), "%s asks for the passcode of the user signed in on it.",
                         h ? h->name : "The console");
                ui_passcode("Login passcode", msg, LOGIN_DIGITS, "Connect", login_pin_done, NULL);
            }
            break;
        case APP_CHANGED_PROGRESS:
        case APP_CHANGED_STATE:
        case APP_CHANGED_UPDATE: break;
    }
}

/* Back, one level: a dialog cancels, Add a console steps back; over the stream the menu leaves
 * adjust mode, closes, or opens; else Home steps back. False only at Home's top level. */
bool ui_back(void) {
    if (ui_dialog_active()) {
        ui_dialog_back();
        return true;
    }
    if (ui_register_active()) {
        ui_register_back();
        return true;
    }
    if (app_conn_state() == CONN_STREAMING) {
        if (!g_overlay || !stream_menu_back()) ui_overlay_toggle();
        return true;
    }
    return home_back();
}

void ui_pad_back(void) { (void)ui_back(); }

void ui_pad_shift(void) {
    if (!ui_dialog_active()) ui_register_shift();
}

/* L1/R1: Home's sections; nothing while something covers Home or a session streams */
void ui_tab_step(int dir, const char *button) {
    (void)button;
    if (ui_register_active() || ui_dialog_active() || app_conn_state() == CONN_STREAMING) return;
    home_section_step(dir);
}

void ui_set_pad_state(pad_state_t state) { home_set_pad(state); }

lv_obj_t *ui_scroll_fallback(void) { return home_scroll_target(); }

lv_group_t *ui_group(void) { return g_group; }

/* Every key lands here, and goes to what is in front: a dialog, Add a console, the stream menu,
 * or Home. Acting on the press would leave its release to whatever the key opened (a dialog
 * focused on Cancel would get CLICKED): the keypad waits for the release. */
static void sink_key_cb(lv_event_t *e) {
    uint32_t k = lv_event_get_key(e);
    lv_indev_t *in = lv_indev_get_act();
    if (in && lv_indev_get_type(in) == LV_INDEV_TYPE_KEYPAD) lv_indev_wait_release(in);
    if (ui_dialog_active()) ui_dialog_key(k);
    else if (ui_register_active()) ui_register_key(k);
    else if (app_conn_state() == CONN_STREAMING) stream_menu_key(k);
    else home_key(k);
}

/* ---------- screen ---------- */

void ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &st_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    home_create(scr);
    stream_menu_create(scr);

    g_group = lv_group_create();
    g_sink = ctl_obj(scr);
    lv_obj_set_size(g_sink, 1, 1);
    lv_obj_clear_flag(g_sink, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(g_sink, INPUT_FLAG_ARROWS);
    lv_obj_add_event_cb(g_sink, sink_key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(g_group, g_sink);
    lv_group_focus_obj(g_sink);

    g_ready = true;

    log_event(LOG_INFO, "APP", "ready");
    char ifname[32], ip[64];
    if (app_local_ip(ifname, sizeof(ifname), ip, sizeof(ip))) log_event(LOG_INFO, "NET", "%s %s", ifname, ip);
    else log_event(LOG_WARN, "NET", "no IPv4 interface up");
    if (!session_available()) log_event(LOG_WARN, "CHIAKI", "libchiaki is not linked: sessions are simulated");
    log_event(LOG_OK, "INPUT",
              "D-pad or left stick move, Cross select, Circle back, L1/R1 Consoles/Settings, "
              "Share+Options stream menu");
}
