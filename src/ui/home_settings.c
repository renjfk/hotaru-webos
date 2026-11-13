/* ui/home_settings.c - Home's settings panels: Stream, Controller, PSN account, Log, About.
 *
 * Each panel is big rows on the left and a summary card on the right. A row's value changes
 * in place: LEFT/RIGHT step a segmented choice, the bitrate bar or a toggle, Cross cycles or
 * flips it, or runs an action row. Every edit goes straight to config_get() and is saved
 * debounced (app_settings_changed()).
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../app.h"
#include "../chiaki/session.h"
#include "../config/config.h"
#include "../input/input.h"
#include "../log.h"
#include "art.h"
#include "controls.h"
#include "dialog.h"
#include "home_internal.h"
#include "log_view.h"
#include "logo.h"
#include "motion.h"
#include "register.h"

#define SOURCE_URL "github.com/renjfk/chiaki-webos" /* where the source of what runs is (AGPL-3.0 section 6) */
#define MAX_ROWS   5
#define SUM_X      (HOME_PROW_W + 40)
#define SUM_W      (HOME_PANEL_W - SUM_X)
#define SUM_H      (5 * HOME_PROW_H + 4 * HOME_PROW_GAP)
#define SUM_PAD    36
#define SUM_CW     (SUM_W - 2 * SUM_PAD - 4) /* the summary card's content width (inside its border) */
#define HOLD_MS    400                       /* presses closer than this are a held key: a meter speeds up */
#define BUZZ_MS    250                       /* turning rumble on: a short buzz shows it works */
#define BUZZ_LEVEL 160
#define PAD_BATT_W 132 /* the controller summary's battery gauge, and the level it shows as a share of it */
#define PAD_BATT_H 14
#define PAD_BATT_Y 326

typedef enum { ROW_SEG, ROW_METER, ROW_TOGGLE, ROW_ACTION, ROW_INFO } row_kind_t;
typedef enum { ACT_NONE, ACT_ADD, ACT_FIND, ACT_SIGN_OUT } row_action_t;

typedef struct {
    lv_obj_t *row, *caption;
    row_kind_t kind;
    row_action_t action;
    bool *flag; /* toggle: the config field it edits */
    ctl_seg_t seg;
    ctl_meter_t meter;
    ctl_toggle_t tgl;
} prow_t;

static const char *const g_names[SET__N] = { "Stream", "Controller", "PSN account", "Log", "About" };
static const uint32_t g_tints[SET__N] = { 0x1d4f9c, 0x58308f, 0x0e6b78, 0x334055, 0x27406a };
static const int g_smooth_ms[] = { 0, 100, 150, 200 };
static const char *const g_res_opts[] = { "720p", "1080p" };
static const char *const g_fps_opts[] = { "30 fps", "60 fps" };
static const char *const g_codec_opts[] = { "H.264", "H.265", "HDR" };
static const char *const g_smooth_opts[] = { "Off", "100 ms", "150 ms", "200 ms" };

static struct {
    lv_obj_t *panel[SET__N];
    prow_t rows[SET__N][MAX_ROWS];
    int nrows[SET__N];
    lv_obj_t *sum_big, *sum_txt, *sum_fill, *sum_note;                       /* Stream */
    lv_obj_t *pad_art_box, *pad_art, *pad_name, *pad_state;                  /* Controller */
    lv_obj_t *pad_batt, *pad_batt_fill, *pad_batt_pct;                       /* Controller: the battery gauge */
    lv_obj_t *acc_avatar, *acc_initials, *acc_name, *acc_state, *acc_status; /* PSN account */
    lv_obj_t *log_card, *log;                                                /* Log */
    lv_obj_t *about_net;                                                     /* About */
    art_id_t pad_art_id;
    int shown;
} g = { .shown = -1 };

const char *home_settings_name(int panel) { return panel >= 0 && panel < SET__N ? g_names[panel] : ""; }
uint32_t home_settings_tint(int panel) { return panel >= 0 && panel < SET__N ? g_tints[panel] : COL_TINT_NEUTRAL; }
lv_obj_t *home_settings_log(void) { return g.shown == SET_LOG ? g.log : NULL; }

/* ---------- stream settings <-> controls ---------- */

static int smooth_index(int ms) {
    int best = 0;
    for (int i = 1; i < (int)(sizeof(g_smooth_ms) / sizeof(g_smooth_ms[0])); i++)
        if (ms >= g_smooth_ms[i]) best = i;
    return best;
}

/* "HDR" is the H.265 HDR profile; the old separate HDR switch meant the same */
static int codec_index(const stream_settings_t *s) {
    return s->hdr || s->codec == CODEC_H265_HDR ? 2 : s->codec == CODEC_H264 ? 0 : 1;
}

static void stream_apply(int row, int v) {
    stream_settings_t *s = &config_get()->stream;
    switch (row) {
        case 0:
            s->width = v ? 1920 : 1280;
            s->height = v ? 1080 : 720;
            break;
        case 1: s->fps = v ? 60 : 30; break;
        case 2:
            s->codec = v == 0 ? CODEC_H264 : v == 1 ? CODEC_H265 : CODEC_H265_HDR;
            s->hdr = v == 2;
            break;
        case 3: s->bitrate_kbps = v * 1000; break;
        case 4: s->smooth_ms = g_smooth_ms[v]; break;
    }
    app_settings_changed();
}

/* ---------- building ---------- */

static prow_t *add_row(int p, const char *title, const char *caption) {
    prow_t *r = &g.rows[p][g.nrows[p]];
    int i = g.nrows[p]++;
    r->row = ctl_rect(g.panel[p], HOME_PROW_W, HOME_PROW_H, 26, COL_FROST, 12);
    lv_obj_set_pos(r->row, 0, i * (HOME_PROW_H + HOME_PROW_GAP));
    ctl_outline(r->row, 4, 4);
    lv_obj_t *t = ctl_label(r->row, FONT_HEADING, COL_TEXT, title);
    lv_obj_set_pos(t, 32, 14);
    r->caption = ctl_label(r->row, FONT_CAPTION, COL_MUTED, caption);
    lv_obj_set_pos(r->caption, 32, 52);
    return r;
}

static void row_seg(prow_t *r, const char *const *opts, int n, int sel, lv_coord_t w) {
    r->kind = ROW_SEG;
    ctl_seg_create(&r->seg, r->row, opts, n, sel, w);
    lv_obj_align(r->seg.box, LV_ALIGN_RIGHT_MID, -14, 0);
}

static void row_toggle(prow_t *r, bool *flag) {
    r->kind = ROW_TOGGLE;
    r->flag = flag;
    ctl_toggle_create(&r->tgl, r->row, *flag);
    lv_obj_align(r->tgl.track, LV_ALIGN_RIGHT_MID, -24, 0);
}

static void row_action(prow_t *r, row_action_t act, const char *sym, bool danger) {
    r->kind = ROW_ACTION;
    r->action = act;
    lv_obj_t *ic = ctl_label(r->row, FONT_TITLE, danger ? COL_ERR : COL_TEXT, sym);
    lv_obj_align(ic, LV_ALIGN_RIGHT_MID, -32, 0);
    if (danger) lv_obj_set_style_text_color(lv_obj_get_child(r->row, 0), C(COL_DANGER_TEXT), 0);
}

static lv_obj_t *summary(int p, const char *over) {
    lv_obj_t *s = ctl_rect(g.panel[p], SUM_W, SUM_H, 30, COL_SHADE, LV_OPA_20);
    lv_obj_set_pos(s, SUM_X, 0);
    lv_obj_set_style_border_width(s, 2, 0);
    lv_obj_set_style_border_color(s, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(s, SUM_PAD, 0);
    lv_obj_t *o = ctl_label(s, FONT_SMALL, COL_ACCENT_HI, over);
    lv_obj_set_style_text_letter_space(o, 3, 0);
    return s;
}

static void build_stream(void) {
    const stream_settings_t *s = &config_get()->stream;
    row_seg(add_row(SET_STREAM, "Resolution", "Higher needs more bandwidth"), g_res_opts, 2, s->height >= 1080, 150);
    row_seg(add_row(SET_STREAM, "Frame rate", "60 fps feels smoother in games"), g_fps_opts, 2, s->fps >= 60, 150);
    row_seg(add_row(SET_STREAM, "Codec", "Decoded in hardware; HDR needs an HDR picture mode"), g_codec_opts, 3,
            codec_index(s), 150);
    prow_t *r = add_row(SET_STREAM, "Bitrate", "Lower it if the picture stutters");
    r->kind = ROW_METER;
    ctl_meter_create(&r->meter, r->row, 300, 2, 50, s->bitrate_kbps / 1000, "%d Mbps");
    lv_obj_align(r->meter.box, LV_ALIGN_RIGHT_MID, -36, 0);
    row_seg(add_row(SET_STREAM, "Smooth playback", "Hides Wi-Fi stalls, adds this much delay"), g_smooth_opts, 4,
            smooth_index(s->smooth_ms), 130);

    lv_obj_t *c = summary(SET_STREAM, "WHAT THE CONSOLE SENDS");
    g.sum_big = ctl_label(c, FONT_HERO, COL_TEXT, "");
    lv_obj_set_pos(g.sum_big, 0, 40);
    g.sum_txt = ctl_label(c, FONT_BODY, COL_MUTED, "");
    lv_obj_set_pos(g.sum_txt, 0, 110);
    lv_obj_t *bw = ctl_label(c, FONT_SMALL, COL_ACCENT_HI, "BANDWIDTH");
    lv_obj_set_style_text_letter_space(bw, 3, 0);
    lv_obj_set_pos(bw, 0, 200);
    lv_obj_t *track = ctl_rect(c, SUM_CW, 16, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_10);
    lv_obj_set_pos(track, 0, 234);
    g.sum_fill = ctl_rect(track, 16, 16, LV_RADIUS_CIRCLE, COL_ACCENT_HI, LV_OPA_COVER);
    g.sum_note = ctl_label(c, FONT_CAPTION, COL_MUTED, "");
    lv_obj_set_width(g.sum_note, SUM_CW);
    lv_label_set_long_mode(g.sum_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(g.sum_note, 0, 280);
}

static void build_pad(void) {
    input_settings_t *in = &config_get()->input;
    row_toggle(add_row(SET_PAD, "Rumble", "Vibrate with in-game feedback"), &in->rumble);
    row_toggle(add_row(SET_PAD, "Touchpad", "Send touches to the console"), &in->touchpad);
    lv_obj_t *c = summary(SET_PAD, "CONTROLLER");
    g.pad_art_box = ctl_obj(c);
    lv_obj_set_size(g.pad_art_box, SUM_CW, 200);
    lv_obj_set_pos(g.pad_art_box, 0, 30);
    g.pad_name = ctl_label(c, FONT_TITLE, COL_TEXT, "");
    lv_obj_set_pos(g.pad_name, 0, 250);
    g.pad_state = ctl_label(c, FONT_CAPTION, COL_MUTED, "");
    lv_obj_set_pos(g.pad_state, 0, 296);
    /* the level hid-sony publishes for a DualShock 4; hidden for a pad that reports none */
    g.pad_batt = ctl_obj(c);
    lv_obj_set_size(g.pad_batt, SUM_CW, SP_XL);
    lv_obj_set_pos(g.pad_batt, 0, PAD_BATT_Y);
    lv_obj_t *track = ctl_rect(g.pad_batt, PAD_BATT_W, PAD_BATT_H, R_PILL, COL_FROST, LV_OPA_20);
    lv_obj_set_pos(track, 0, (SP_XL - PAD_BATT_H) / 2);
    g.pad_batt_fill = ctl_rect(track, 0, PAD_BATT_H, R_PILL, COL_OK, LV_OPA_COVER);
    g.pad_batt_pct = ctl_label(g.pad_batt, FONT_CAPTION, COL_SUBTLE, "");
    lv_obj_set_pos(g.pad_batt_pct, PAD_BATT_W + SP_S, 0);
    lv_obj_add_flag(g.pad_batt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *n = ctl_label(c, FONT_CAPTION, COL_MUTED, "Share + Options opens the menu while you play");
    lv_obj_set_width(n, SUM_CW);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(n, 0, 358);
    g.pad_art_id = ART__N;
}

static void build_account(void) {
    row_action(add_row(SET_ACCOUNT, "Add a console", "Pair with the console's PIN, or sign in to PSN"), ACT_ADD,
               LV_SYMBOL_PLUS, false);
    row_action(add_row(SET_ACCOUNT, "Find my consoles", "List the consoles on your PSN account"), ACT_FIND,
               LV_SYMBOL_REFRESH, false);
    row_action(add_row(SET_ACCOUNT, "Sign out of PSN", "Keeps the consoles paired with a PIN"), ACT_SIGN_OUT,
               LV_SYMBOL_CLOSE, true);
    lv_obj_t *c = summary(SET_ACCOUNT, "PLAYSTATION NETWORK");
    g.acc_avatar = ctl_rect(c, 120, 120, LV_RADIUS_CIRCLE, 0, LV_OPA_COVER);
    ctl_grad(g.acc_avatar, COL_PSN, COL_PSN_DEEP);
    lv_obj_set_pos(g.acc_avatar, 0, 44);
    g.acc_initials = ctl_label(g.acc_avatar, FONT_HERO, COL_TEXT, "");
    lv_obj_center(g.acc_initials);
    g.acc_name = ctl_label(c, FONT_TITLE, COL_TEXT, "");
    lv_obj_set_pos(g.acc_name, 0, 190);
    g.acc_state = ctl_label(c, FONT_CAPTION, COL_MUTED, "");
    lv_obj_set_pos(g.acc_state, 0, 236);
    g.acc_status = ctl_label(c, FONT_CAPTION, COL_ACCENT_HI, "");
    lv_obj_set_width(g.acc_status, SUM_CW);
    lv_label_set_long_mode(g.acc_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(g.acc_status, 0, 280);
}

static void build_log(void) {
    g.log_card = ctl_rect(g.panel[SET_LOG], HOME_PANEL_W, SUM_H, 30, COL_SHADE, LV_OPA_30);
    lv_obj_set_style_border_width(g.log_card, 2, 0);
    lv_obj_set_style_border_color(g.log_card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.log_card, LV_OPA_10, 0);
    ctl_outline(g.log_card, 4, 4);
    lv_obj_set_style_pad_all(g.log_card, SP_M, 0);
    lv_obj_set_flex_flow(g.log_card, LV_FLEX_FLOW_COLUMN);
    g.log = log_view_create(g.log_card);
    lv_obj_set_style_bg_opa(g.log, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g.log, 0, 0);
    g.nrows[SET_LOG] = 1; /* the console itself: UP/DOWN scroll it */
}

static void build_about(void) {
    char ver[64];
    snprintf(ver, sizeof(ver), "LVGL %d.%d.%d, SDL2, libchiaki from chiaki-ng", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH);
    add_row(SET_ABOUT, "Chiaki webOS " CHIAKI_WEBOS_VERSION, "PlayStation Remote Play for LG webOS TVs")->kind =
        ROW_INFO;
    add_row(SET_ABOUT, "Remote Play",
            session_available() ? "libchiaki: real sessions and registration"
                                : "Simulated sessions (libchiaki not linked)")
        ->kind = ROW_INFO;
    prow_t *net = add_row(SET_ABOUT, "Network", "");
    net->kind = ROW_INFO;
    g.about_net = net->caption;
    add_row(SET_ABOUT, "Built with", ver)->kind = ROW_INFO;
    row_toggle(add_row(SET_ABOUT, "Diagnostics", "Logs input, discovery and frame timing to /tmp/chiaki.log"),
               &config_get()->debug.probe);
    lv_obj_t *c = summary(SET_ABOUT, "ABOUT");
    lv_obj_t *logo = ui_logo(c, 140);
    lv_obj_set_pos(logo, 0, 50);
    lv_obj_t *n = ctl_label(c, FONT_TITLE, COL_TEXT, "Chiaki webOS");
    lv_obj_set_pos(n, 0, 220);
    lv_obj_t *d = ctl_label(c, FONT_CAPTION, COL_MUTED,
                            "Open source (AGPL-3.0), built on chiaki-ng. "
                            "Source: " SOURCE_URL);
    lv_obj_set_width(d, SUM_CW);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(d, 0, 266);
}

void home_settings_build(lv_obj_t *parent) {
    for (int p = 0; p < SET__N; p++) {
        g.panel[p] = ctl_obj(parent);
        lv_obj_set_size(g.panel[p], HOME_PANEL_W, SUM_H);
        lv_obj_set_pos(g.panel[p], HOME_SAFE_X, HOME_PANEL_Y);
        ctl_margin(g.panel[p], 16); /* a focused row's ring is 8 px outside it */
        lv_obj_add_flag(g.panel[p], LV_OBJ_FLAG_HIDDEN);
    }
    build_stream();
    build_pad();
    build_account();
    build_log();
    build_about();
    home_settings_refresh();
}

/* ---------- state ---------- */

static void refresh_stream(void) {
    const stream_settings_t *s = &config_get()->stream;
    int kbps = s->bitrate_kbps;
    lv_label_set_text_fmt(g.sum_big, "%dp%d", s->height, s->fps);
    lv_label_set_text_fmt(g.sum_txt, "%s" BULLET "%s" BULLET "%d Mbps", s->codec == CODEC_H264 ? "H.264" : "H.265",
                          codec_index(s) == 2 ? "HDR" : "SDR", kbps / 1000);
    motion_to(g.sum_fill, MO_W, 16 + (SUM_CW - 16) * kbps / 50000, 260, 0, NULL);
    lv_label_set_text(g.sum_note, app_conn_state() == CONN_IDLE
                                      ? "Video settings apply the next time you connect."
                                      : "Changes restart the stream when you close the menu.");
}

static void refresh_pad(void) {
    pad_state_t st = input_pad_state();
    const char *name = input_pad_name();
    art_id_t id = art_pad(name);
    if (id != g.pad_art_id) {
        g.pad_art_id = id;
        lv_obj_clean(g.pad_art_box);
        lv_obj_align(art_create(g.pad_art_box, id, 190), LV_ALIGN_LEFT_MID, -6, 0);
    }
    lv_label_set_text(g.pad_name, id == ART_DUALSENSE ? "DualSense" : "DualShock 4");
    lv_label_set_text(g.pad_state, st == PAD_ONLINE    ? "Connected"
                                   : st == PAD_OFFLINE ? "Offline: turn it on again"
                                                       : "Not found: pair it with the TV");
    lv_obj_set_style_text_color(g.pad_state, C(st == PAD_ONLINE ? COL_OK : st == PAD_OFFLINE ? COL_ERR : COL_MUTED), 0);
    /* its battery, when the pad reports one (a DualShock 4 does, a DualSense on this TV does not) */
    int pct = 0;
    if (st == PAD_ONLINE && input_pad_battery(&pct)) {
        uint32_t col = pct <= BATT_CRIT ? COL_ERR : pct <= BATT_LOW ? COL_WARN : COL_OK;
        lv_obj_set_style_bg_color(g.pad_batt_fill, C(col), 0);
        lv_obj_set_width(g.pad_batt_fill, PAD_BATT_W * pct / 100);
        lv_label_set_text_fmt(g.pad_batt_pct, "%d%%", pct);
        lv_obj_clear_flag(g.pad_batt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g.pad_batt, LV_OBJ_FLAG_HIDDEN);
    }
    /* what this pad can do on this TV (a DualSense: neither rumble nor touches reach the app) */
    bool on = st == PAD_ONLINE;
    lv_label_set_text(g.rows[SET_PAD][0].caption, on && !input_pad_rumbles()
                                                      ? "This controller cannot rumble on this TV"
                                                      : "Vibrate with in-game feedback");
    lv_label_set_text(g.rows[SET_PAD][1].caption, on && !input_pad_touches()
                                                      ? "Only its click reaches the console on this TV"
                                                      : "Send touches to the console");
}

static void refresh_account(void) {
    const config_psn_t *psn = config_psn();
    char ini[3] = { 0 };
    if (psn->linked && psn->online_id[0]) {
        ini[0] = (char)toupper((unsigned char)psn->online_id[0]);
        if (psn->online_id[1]) ini[1] = (char)toupper((unsigned char)psn->online_id[1]);
    }
    lv_label_set_text(g.acc_initials, ini[0] ? ini : LV_SYMBOL_WIFI);
    lv_label_set_text(g.acc_name, psn->linked ? (psn->online_id[0] ? psn->online_id : "Signed in") : "Not signed in");
    lv_label_set_text(g.acc_state, psn->linked                   ? "Your PSN consoles show up on Home"
                                   : config_psn_has_account_id() ? "Account ID set for PIN pairing"
                                                                 : "Sign in to find your consoles");
    app_reg_state_t rs = app_reg_state();
    lv_label_set_text(g.acc_status, rs == APP_REG_FAILED ? app_reg_error()
                                    : rs == APP_REG_IDLE ? ""
                                                         : app_reg_status());
    lv_obj_set_style_text_color(g.acc_status, C(rs == APP_REG_FAILED ? COL_DANGER_TEXT : COL_ACCENT_HI), 0);
    /* Find needs a token, Sign out something to sign out of */
    lv_obj_set_style_opa(g.rows[SET_ACCOUNT][1].row, psn->access_token[0] ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_opa(g.rows[SET_ACCOUNT][2].row,
                         psn->linked || config_psn_has_account_id() ? LV_OPA_COVER : LV_OPA_50, 0);
}

void home_settings_refresh(void) {
    refresh_stream();
    refresh_pad();
    refresh_account();
    char ifname[32], ip[64];
    if (app_local_ip(ifname, sizeof(ifname), ip, sizeof(ip))) lv_label_set_text_fmt(g.about_net, "%s  %s", ifname, ip);
    else lv_label_set_text(g.about_net, "No network");
}

void home_settings_show(int panel, bool anim) {
    for (int p = 0; p < SET__N; p++)
        if (p != panel) lv_obj_add_flag(g.panel[p], LV_OBJ_FLAG_HIDDEN);
    g.shown = panel;
    if (panel < 0 || panel >= SET__N) return;
    lv_obj_clear_flag(g.panel[panel], LV_OBJ_FLAG_HIDDEN);
    if (anim) motion_enter(g.panel[panel], 40, 28);
    if (panel == SET_LOG) lv_obj_scroll_to_y(g.log, LV_COORD_MAX, LV_ANIM_OFF);
}

/* the first row the focus can take: info rows are only read */
int home_settings_first_row(int panel) {
    if (panel == SET_LOG) return 0;
    for (int i = 0; panel >= 0 && panel < SET__N && i < g.nrows[panel]; i++)
        if (g.rows[panel][i].kind != ROW_INFO) return i;
    return -1;
}

bool home_settings_enterable(int panel) { return home_settings_first_row(panel) >= 0; }

void home_settings_focus(int panel, int row) {
    for (int p = 0; p < SET__N; p++) {
        if (p == SET_LOG) {
            bool on = p == panel && row >= 0;
            motion_to(g.log_card, MO_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
            continue;
        }
        for (int i = 0; i < g.nrows[p]; i++) {
            prow_t *r = &g.rows[p][i];
            bool on = p == panel && i == row;
            motion_to(r->row, MO_OUTLINE_OPA, on ? LV_OPA_COVER : LV_OPA_TRANSP, 160, 0, NULL);
            motion_to(r->row, MO_BG_OPA, on ? LV_OPA_20 : 12, 160, 0, NULL);
            if (r->kind == ROW_SEG) ctl_seg_focus(&r->seg, on);
            if (r->kind == ROW_METER) ctl_meter_focus(&r->meter, on);
        }
    }
}

static void bump(lv_obj_t *o, int dir) { motion_from(o, MO_TX, dir * 14, 0, 380, 0, motion_shake); }

static void sign_out_confirmed(void *user) {
    (void)user;
    config_psn_clear();
    config_save();
    log_event(LOG_WARN, "REG", "Signed out of PSN");
    home_settings_refresh();
}

static void run_action(prow_t *r) {
    const config_psn_t *psn = config_psn();
    switch (r->action) {
        case ACT_ADD: ui_register_open(); break;
        case ACT_FIND:
            if (psn->access_token[0]) app_psn_refresh();
            else bump(r->row, 1);
            break;
        case ACT_SIGN_OUT:
            if (psn->linked || config_psn_has_account_id())
                ui_confirm("Sign out of PSN",
                           "The consoles found through your PSN account stay on Home but cannot "
                           "connect over PSN until you sign in again.",
                           "Sign out", ART__N, sign_out_confirmed, NULL);
            else bump(r->row, 1);
            break;
        case ACT_NONE: break;
    }
}

static void buzz_end_cb(lv_timer_t *t) {
    (void)t;
    input_rumble(0, 0);
}

static void edit_row(int panel, int row, uint32_t key) {
    prow_t *r = &g.rows[panel][row];
    int d = key == LV_KEY_LEFT ? -1 : 1;
    switch (r->kind) {
        case ROW_SEG: {
            int s = key == LV_KEY_ENTER ? (r->seg.sel + 1) % r->seg.n : r->seg.sel + d;
            if (s < 0 || s >= r->seg.n) {
                bump(r->seg.box, d);
                return;
            }
            ctl_seg_select(&r->seg, s);
            if (panel == SET_STREAM) stream_apply(row, s);
            break;
        }
        case ROW_METER: {
            if (key == LV_KEY_ENTER) return;
            if ((d < 0 && r->meter.val == r->meter.min) || (d > 0 && r->meter.val == r->meter.max)) {
                bump(r->meter.box, d);
                return;
            }
            /* held: 1 a step at first, then 2, then 5, landing on multiples of the step */
            static uint32_t held_at;
            static int held_run, held_dir;
            held_run = d == held_dir && lv_tick_elaps(held_at) < HOLD_MS ? held_run + 1 : 0;
            held_dir = d;
            held_at = lv_tick_get();
            int step = held_run < 4 ? 1 : held_run < 10 ? 2 : 5;
            int v = d > 0 ? (r->meter.val / step + 1) * step : ((r->meter.val + step - 1) / step - 1) * step;
            ctl_meter_set(&r->meter, LV_CLAMP(r->meter.min, v, r->meter.max));
            if (panel == SET_STREAM) stream_apply(row, r->meter.val);
            break;
        }
        case ROW_TOGGLE: {
            bool v = key == LV_KEY_ENTER ? !r->tgl.val : d > 0;
            if (v == r->tgl.val) {
                bump(r->tgl.track, d);
                return;
            }
            ctl_toggle_set(&r->tgl, v);
            *r->flag = v;
            app_settings_changed();
            if (v && r->flag == &config_get()->input.rumble && input_pad_rumbles()) {
                input_rumble(BUZZ_LEVEL, BUZZ_LEVEL);
                lv_timer_set_repeat_count(lv_timer_create(buzz_end_cb, BUZZ_MS, NULL), 1);
            }
            break;
        }
        case ROW_ACTION:
            if (key == LV_KEY_ENTER) run_action(r);
            return;
        case ROW_INFO: return;
    }
    refresh_stream();
    home_values_changed();
}

bool home_settings_key(int panel, int *row, uint32_t key) {
    if (panel == SET_LOG) { /* the console: UP/DOWN scroll it, UP at the top leaves */
        lv_coord_t step = 3 * (lv_font_get_line_height(FONT_BODY) + SP_S);
        if (key == LV_KEY_UP) {
            if (lv_obj_get_scroll_top(g.log) <= 0) return false;
            lv_obj_scroll_by(g.log, 0, LV_MIN(step, lv_obj_get_scroll_top(g.log)), LV_ANIM_ON);
        } else if (key == LV_KEY_DOWN) {
            lv_obj_scroll_by(g.log, 0, -LV_MIN(step, lv_obj_get_scroll_bottom(g.log)), LV_ANIM_ON);
        }
        return true;
    }
    if (key == LV_KEY_UP || key == LV_KEY_DOWN) { /* the next row that is not an info row */
        int d = key == LV_KEY_UP ? -1 : 1, r = *row + d;
        while (r >= 0 && r < g.nrows[panel] && g.rows[panel][r].kind == ROW_INFO) r += d;
        if (r < 0) return false;
        if (r < g.nrows[panel]) *row = r;
    } else if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_ENTER) {
        edit_row(panel, *row, key);
    }
    home_settings_focus(panel, *row);
    return true;
}

const char *home_settings_caption(int panel) {
    static char buf[64];
    const config_psn_t *psn = config_psn();
    const stream_settings_t *s = &config_get()->stream;
    switch (panel) {
        case SET_STREAM:
            snprintf(buf, sizeof(buf), "%dp" BULLET "%d fps" BULLET "%d Mbps", s->height, s->fps,
                     s->bitrate_kbps / 1000);
            return buf;
        case SET_PAD: return input_pad_state() == PAD_ONLINE ? input_pad_name() : "No controller";
        case SET_ACCOUNT: return psn->linked ? (psn->online_id[0] ? psn->online_id : "Signed in") : "Not signed in";
        case SET_LOG: return "What just happened";
        case SET_ABOUT: return "Versions, network, diagnostics";
    }
    return "";
}
