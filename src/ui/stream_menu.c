/* ui/stream_menu.c - the Control Center over the stream (see stream_menu.h). */
#include "stream_menu.h"

#include <stdio.h>
#include <string.h>

#include "../app.h"
#include "../chiaki/session.h"
#include "../config/config.h"
#include "../input/input.h"
#include "art.h"
#include "controls.h"
#include "motion.h"
#include "ui.h"

#define SHEET_H   404
#define CARD_W    312
#define CARD_H    180
#define CARD_GAP  32
#define MARGIN_X  96
#define PAD_ART_H 40
#define BATT_W    64 /* the Controller card's battery gauge, top right of the pad art */
#define BATT_H    10

enum { C_RESUME, C_QUALITY, C_SMOOTH, C_PAD, C_DISCONNECT, C__N };

static const int g_smooth_ms[] = { 0, 100, 150, 200 };

static struct {
    lv_obj_t *sheet, *art_box, *name, *time, *chips, *chip[4], *cards[C__N], *vals[C__N], *note, *pad_art;
    lv_obj_t *batt, *batt_fill, *batt_pct; /* the Controller card's battery, when the pad reports one */
    lv_obj_t *hints, *icon[C__N];          /* the bottom hints, and the cards' glyphs */
    art_id_t pad_id;
    int focus;
    bool open;
    uint32_t started;
    lv_timer_t *tick;
} g;

/* ---------- values ---------- */

static int quality_index(void) {
    const stream_settings_t *s = &config_get()->stream;
    return (s->height >= 1080) * 2 + (s->fps >= 60);
}

static int smooth_index(void) {
    int ms = config_get()->stream.smooth_ms, best = 0;
    for (int i = 1; i < 4; i++)
        if (ms >= g_smooth_ms[i]) best = i;
    return best;
}

static const char *card_value(int i) {
    static char buf[80];
    const config_t *c = config_get();
    char v[48];
    bool left = false, right = false; /* the directions the value can still step */
    switch (i) {
        case C_RESUME: return "Back to the game";
        case C_QUALITY:
            snprintf(v, sizeof(v), "%dp" BULLET "%d fps", c->stream.height, c->stream.fps);
            left = quality_index() > 0;
            right = quality_index() < 3;
            break;
        case C_SMOOTH:
            if (c->stream.smooth_ms) snprintf(v, sizeof(v), "%d ms", c->stream.smooth_ms);
            else snprintf(v, sizeof(v), "Off");
            left = smooth_index() > 0;
            right = smooth_index() < 3;
            break;
        case C_PAD:
            snprintf(v, sizeof(v), "Rumble %s", c->input.rumble ? "on" : "off");
            left = c->input.rumble;
            right = !c->input.rumble;
            break;
        case C_DISCONNECT:
            snprintf(v, sizeof(v), "%s", c->session.sleep_on_disconnect ? "End and sleep" : "End the session");
            left = c->session.sleep_on_disconnect;
            right = !c->session.sleep_on_disconnect;
            break;
        default: return "End the session";
    }
    if (left && right) snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, v);
    else if (left) snprintf(buf, sizeof(buf), LV_SYMBOL_LEFT "   %s", v);
    else if (right) snprintf(buf, sizeof(buf), "%s   " LV_SYMBOL_RIGHT, v);
    else snprintf(buf, sizeof(buf), "%s", v);
    return buf;
}

static void chips_refresh(void) {
    const stream_settings_t *s = &config_get()->stream;
    lv_label_set_text_fmt(g.chip[0], "%dp" BULLET "%d fps", s->height, s->fps);
    lv_label_set_text(g.chip[1], app_stream_h264_only()
                                     ? config_codec_name(CODEC_H264) /* a v1 media TV decodes H.264 only */
                                 : s->hdr || s->codec == CODEC_H265_HDR ? "H.265 HDR"
                                                                        : config_codec_name(s->codec));
    lv_label_set_text_fmt(g.chip[2], "%d Mbps", s->bitrate_kbps / 1000);
    lv_label_set_text(g.chip[3], app_conn_over_psn() ? "Over PSN" : "Local network");
}

static void values_refresh(void) {
    for (int i = 0; i < C__N; i++) lv_label_set_text(g.vals[i], card_value(i));
    chips_refresh();
    /* the pad's battery, beside its art on the Controller card (a DualSense on webOS 6 reports none) */
    int pct = 0;
    if (input_pad_battery(&pct)) {
        uint32_t col = pct <= BATT_CRIT ? COL_ERR : pct <= BATT_LOW ? COL_WARN : COL_OK;
        lv_obj_set_style_bg_color(g.batt_fill, C(col), 0);
        lv_obj_set_width(g.batt_fill, BATT_W * pct / 100);
        lv_label_set_text_fmt(g.batt_pct, "%d%%", pct);
        lv_obj_clear_flag(g.batt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g.batt, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- painting ---------- */

static void paint(void) {
    for (int i = 0; i < C__N; i++) {
        bool on = i == g.focus, danger = i == C_DISCONNECT;
        motion_to(g.cards[i], MO_BG_OPA, on ? LV_OPA_COVER : LV_OPA_10, 200, 0, NULL);
        lv_obj_set_style_bg_color(g.cards[i], on && danger ? C(COL_ERR) : lv_color_white(), 0);
        motion_color(g.cards[i], LV_STYLE_TEXT_COLOR, on && !danger ? COL_ON_FROST : COL_TEXT, 200);
        motion_to(g.cards[i], MO_GROW, on ? 8 : 0, 320, 0, motion_spring);
        motion_to(g.cards[i], MO_TY, on ? -12 : 0, 320, 0, motion_spring);
        lv_obj_set_style_border_color(g.cards[i], C(COL_FROST), 0);
        lv_obj_set_style_border_width(g.cards[i], 2, 0);
        motion_to(g.cards[i], MO_BORDER_OPA, LV_OPA_10, 160, 0, NULL);
        if (danger && g.icon[i]) motion_color(g.icon[i], LV_STYLE_TEXT_COLOR, on ? COL_TEXT : COL_ERR, 200);
        motion_roll_opa(g.vals[i], on && danger ? LV_OPA_COVER : LV_OPA_70);
    }
}

/* The bottom hints follow the focused card: only cards with a Cross action show it, and only
 * Resume has no value to change. The remote's Up/Down change values only when no pad is in use;
 * otherwise the pad's own D-pad and stick would change them by accident. */
static void hints_refresh(void) {
    if (!g.hints) return;
    const char *change = input_pad_state() == PAD_ONLINE ? "L1/R1" : "Up/Down";
    lv_obj_clean(g.hints);
    ctl_hint_key(g.hints, LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, "Move");
    if (g.focus != C_RESUME) ctl_hint_key(g.hints, change, "Change");
    if (g.focus == C_RESUME) ctl_hint(g.hints, PS_CROSS, "Resume");
    else if (g.focus == C_DISCONNECT) ctl_hint(g.hints, PS_CROSS, "Disconnect");
    ctl_hint(g.hints, PS_CIRCLE, "Back to the game");
}

static void tick_cb(lv_timer_t *t) {
    (void)t;
    uint32_t s = lv_tick_elaps(g.started) / 1000;
    char rate[64];
    session_stats_t st;
    if (session_stats(&st)) snprintf(rate, sizeof(rate), BULLET "%.1f Mbps" BULLET "%.0f fps", st.mbps, st.fps);
    else snprintf(rate, sizeof(rate), BULLET "-- Mbps" BULLET "-- fps");
    lv_label_set_text_fmt(g.time, "Streaming" BULLET "%02u:%02u:%02u%s", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                          (unsigned)(s % 60), rate);
}

/* the Controller card shows the pad in use (a DualSense, DualShock 3 or DualShock 4) */
static void pad_art_set(art_id_t id) {
    if (g.pad_art && id == g.pad_id) return;
    if (g.pad_art) lv_obj_del(g.pad_art);
    g.pad_id = id;
    g.pad_art = art_create(g.cards[C_PAD], id, PAD_ART_H);
    lv_obj_set_pos(g.pad_art, 22, 26);
}

static void hidden_cb(lv_obj_t *sheet) {
    if (!g.open) lv_obj_add_flag(sheet, LV_OBJ_FLAG_HIDDEN);
}

void stream_menu_open(bool open) {
    if (open == g.open) return;
    g.open = open;
    if (open) {
        g.focus = C_RESUME;
        const app_host_t *h = app_host(app_selected());
        lv_obj_clean(g.art_box);
        if (h) {
            lv_obj_center(art_box(g.art_box, art_console(h->ps5), 80, 80));
            lv_label_set_text(g.name, h->name);
        }
        pad_art_set(art_pad(input_pad_name()));
        values_refresh();
        tick_cb(NULL);
        lv_label_set_text(g.note, "");
        hints_refresh();
        lv_obj_clear_flag(g.sheet, LV_OBJ_FLAG_HIDDEN);
        motion_from(g.sheet, MO_TY, SHEET_H + 60, 0, 420, 0, NULL);
        for (int i = 0; i < C__N; i++) {
            motion_from(g.cards[i], MO_TY, 70, i == C_RESUME ? -12 : 0, 420, 90 + (uint32_t)i * 45, NULL);
            motion_from(g.cards[i], MO_OPA, 0, 255, 300, 90 + (uint32_t)i * 45, NULL);
        }
        paint();
        lv_timer_resume(g.tick);
    } else {
        lv_timer_pause(g.tick);
        motion_then(g.sheet, MO_TY, SHEET_H + 60, 240, lv_anim_path_ease_in, hidden_cb);
    }
}

bool stream_menu_visible(void) { return g.sheet && !lv_obj_has_flag(g.sheet, LV_OBJ_FLAG_HIDDEN); }

/* ---------- keys ---------- */

/* changes the focused card's value: L1/Up steps back, R1/Down steps forward. The TV remote's
 * Up/Down call the same; Cross is left for Resume and Disconnect. */
static void adjust(int dir) {
    stream_settings_t *s = &config_get()->stream;
    if (g.focus == C_QUALITY) {
        int q = LV_CLAMP(0, quality_index() + dir, 3);
        if (q == quality_index()) {
            motion_from(g.cards[g.focus], MO_TX, dir * 14, 0, 380, 0, motion_shake);
            return;
        }
        s->width = q >= 2 ? 1920 : 1280;
        s->height = q >= 2 ? 1080 : 720;
        s->fps = q % 2 ? 60 : 30;
        motion_roll_text(g.note, LV_SYMBOL_REFRESH "   The stream restarts with the new quality when the menu closes",
                         1);
    } else if (g.focus == C_SMOOTH) {
        int cur = smooth_index(), i = LV_CLAMP(0, cur + dir, 3);
        if (i == cur) {
            motion_from(g.cards[g.focus], MO_TX, dir * 14, 0, 380, 0, motion_shake);
            return;
        }
        s->smooth_ms = g_smooth_ms[i];
        motion_roll_text(g.note, "Smooth playback applies right away", 1);
    } else if (g.focus == C_PAD) {
        bool rumble = dir > 0;
        if (rumble == config_get()->input.rumble) {
            motion_from(g.cards[g.focus], MO_TX, dir * 14, 0, 380, 0, motion_shake);
            return;
        }
        config_get()->input.rumble = rumble;
        if (!rumble) input_rumble(0, 0); /* a motor that is running stops now */
    } else if (g.focus == C_DISCONNECT) {
        bool sleep = dir > 0;
        session_settings_t *sess = &config_get()->session;
        if (sleep == sess->sleep_on_disconnect) {
            motion_from(g.cards[g.focus], MO_TX, dir * 14, 0, 380, 0, motion_shake);
            return;
        }
        sess->sleep_on_disconnect = sleep;
    } else {
        return;
    }
    app_settings_changed();
    motion_roll_text(g.vals[g.focus], card_value(g.focus), dir);
    chips_refresh();
}

/* L1/R1 change the focused card; the remote's Up/Down do the same when no pad is in use */
void stream_menu_adjust(int dir) {
    if (g.open) adjust(dir);
}

void stream_menu_key(uint32_t k) {
    if (!g.open) return;
    int prev = g.focus;
    if (k == LV_KEY_LEFT && g.focus > 0) g.focus--;
    else if (k == LV_KEY_RIGHT && g.focus < C__N - 1) g.focus++;
    else if (k == LV_KEY_UP || k == LV_KEY_DOWN) {
        if (input_pad_state() != PAD_ONLINE) stream_menu_adjust(k == LV_KEY_DOWN ? 1 : -1);
        return;
    } else if (k == LV_KEY_ENTER) {
        if (g.focus == C_RESUME) {
            ui_overlay_toggle();
            return;
        }
        if (g.focus == C_DISCONNECT) {
            app_disconnect();
            return;
        }
        return; /* the setting cards change with L1/R1; Cross has no action there */
    } else if (k == LV_KEY_ESC) {
        ui_overlay_toggle();
        return;
    }
    if (g.focus != prev) {
        if (g.focus == C_DISCONNECT || prev == C_DISCONNECT) lv_label_set_text(g.note, "");
        hints_refresh();
    }
    paint();
}

void stream_menu_refresh(void) {
    if (app_conn_state() == CONN_STREAMING && !g.started) g.started = lv_tick_get();
    if (app_conn_state() == CONN_IDLE) g.started = 0;
    if (g.open) {
        values_refresh();
        hints_refresh();
    }
}

/* ---------- building ---------- */

static lv_obj_t *chip(lv_obj_t *row, uint32_t dot) {
    lv_obj_t *c = ctl_rect(row, LV_SIZE_CONTENT, 48, LV_RADIUS_CIRCLE, COL_FROST, LV_OPA_10);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 10, 0);
    if (dot) ctl_dot(c, 12, dot);
    return ctl_label(c, FONT_CAPTION, COL_TEXT, "");
}

void stream_menu_create(lv_obj_t *parent) {
    memset(&g, 0, sizeof(g));
    g.sheet = ctl_rect(parent, LV_PCT(100), SHEET_H + 60, 44, COL_SHEET, 222);
    lv_obj_align(g.sheet, LV_ALIGN_BOTTOM_MID, 0, 60); /* its lower corners off-screen */
    lv_obj_add_flag(g.sheet, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_border_width(g.sheet, 2, 0);
    lv_obj_set_style_border_side(g.sheet, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g.sheet, lv_color_white(), 0);
    lv_obj_set_style_border_opa(g.sheet, LV_OPA_10, 0);
    lv_obj_add_flag(g.sheet, LV_OBJ_FLAG_HIDDEN);

    g.art_box = ctl_obj(g.sheet);
    lv_obj_set_size(g.art_box, 60, 80);
    lv_obj_set_pos(g.art_box, MARGIN_X, 26);
    g.name = ctl_label(g.sheet, FONT_HEADING, COL_TEXT, "");
    lv_obj_set_pos(g.name, MARGIN_X + 80, 30);
    g.time = ctl_label(g.sheet, FONT_CAPTION, COL_MUTED, "");
    lv_obj_set_pos(g.time, MARGIN_X + 80, 68);
    g.chips = ctl_row(g.sheet, SP_M);
    lv_obj_align(g.chips, LV_ALIGN_TOP_RIGHT, -MARGIN_X, 34);
    g.chip[0] = chip(g.chips, COL_OK);
    for (int i = 1; i < 4; i++) g.chip[i] = chip(g.chips, 0);

    static const char *const titles[C__N] = { "Resume", "Stream quality", "Smooth playback", "Controller",
                                              "Disconnect" };
    static const char *const syms[C__N] = { LV_SYMBOL_PLAY, LV_SYMBOL_VIDEO, LV_SYMBOL_LOOP, "", LV_SYMBOL_POWER };
    lv_coord_t x0 = (LV_HOR_RES - (C__N * CARD_W + (C__N - 1) * CARD_GAP)) / 2;
    for (int i = 0; i < C__N; i++) {
        lv_obj_t *c = g.cards[i] = ctl_rect(g.sheet, CARD_W, CARD_H, 30, COL_FROST, LV_OPA_10);
        lv_obj_set_pos(c, x0 + i * (CARD_W + CARD_GAP), 124);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_border_color(c, lv_color_white(), 0);
        lv_obj_set_style_border_opa(c, LV_OPA_10, 0);
        lv_obj_set_style_text_color(c, C(COL_TEXT), 0);
        if (i == C_PAD) {
            pad_art_set(ART_DS4);
            /* the pad's battery, top right of its art; hidden when it reports none (a DualSense on webOS 6) */
            g.batt = ctl_row(c, SP_S);
            lv_obj_align(g.batt, LV_ALIGN_TOP_RIGHT, -SP_XL, 34);
            lv_obj_t *track = ctl_rect(g.batt, BATT_W, BATT_H, R_PILL, COL_FROST, LV_OPA_20);
            g.batt_fill = ctl_rect(track, 0, BATT_H, R_PILL, COL_OK, LV_OPA_COVER);
            g.batt_pct = lv_label_create(g.batt);
            lv_label_set_text(g.batt_pct, "");
            lv_obj_set_style_text_font(g.batt_pct, FONT_CAPTION, 0);
            lv_obj_set_style_text_opa(g.batt_pct, LV_OPA_70, 0); /* the card's colour follows the focus */
            lv_obj_add_flag(g.batt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_t *ic = lv_label_create(c);
            g.icon[i] = ic;
            lv_label_set_text(ic, syms[i]);
            lv_obj_set_style_text_font(ic, FONT_TITLE, 0);
            if (i == C_DISCONNECT) lv_obj_set_style_text_color(ic, C(COL_ERR), 0);
            lv_obj_set_pos(ic, 28, 28);
        }
        lv_obj_t *t = lv_label_create(c);
        lv_label_set_text(t, titles[i]);
        lv_obj_set_style_text_font(t, FONT_HEADING, 0);
        lv_obj_set_pos(t, 28, 92);
        g.vals[i] = lv_label_create(c);
        lv_obj_set_style_text_font(g.vals[i], FONT_CAPTION, 0);
        lv_obj_set_style_text_opa(g.vals[i], LV_OPA_70, 0);
        lv_obj_set_pos(g.vals[i], 28, 132);
    }
    g.hints = ctl_hints(g.sheet);
    lv_obj_set_pos(g.hints, x0, 334);
    g.note = ctl_label(g.sheet, FONT_CAPTION, COL_ACCENT_HI, "");
    lv_obj_align(g.note, LV_ALIGN_TOP_RIGHT, -x0, 342);
    g.tick = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_pause(g.tick);
    values_refresh();
    hints_refresh();
}

int stream_menu_focus(void) { return g.focus; }
lv_obj_t *stream_menu_card(int i) { return i >= 0 && i < C__N ? g.cards[i] : NULL; }
const char *stream_menu_card_value(int i) { return i >= 0 && i < C__N ? lv_label_get_text(g.vals[i]) : ""; }

static bool label_has(lv_obj_t *o, const char *text) {
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), text)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        if (label_has(lv_obj_get_child(o, (int32_t)i), text)) return true;
    return false;
}

bool stream_menu_hint_has(const char *text) { return g.hints && label_has(g.hints, text); }

const char *stream_menu_session_text(void) { return g.time ? lv_label_get_text(g.time) : ""; }
const char *stream_menu_pad_battery(void) {
    return g.batt && !lv_obj_has_flag(g.batt, LV_OBJ_FLAG_HIDDEN) ? lv_label_get_text(g.batt_pct) : "";
}
