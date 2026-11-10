/* ui/page_video.c - Video tab: stream settings (config_get()->stream) + summary card. */
#include <stdbool.h>

#include "../app.h"
#include "../config/config.h"
#include "pages.h"
#include "widgets.h"

static struct {
    lv_obj_t *codec, *res, *fps, *bitrate, *bitrate_val, *hdr;
    lv_obj_t *sum_title, *sum_detail, *bw_bar, *bw_caption;
} g_video;

static void refresh(void) {
    const stream_settings_t *s = &config_get()->stream;
    int kbps = s->bitrate_kbps;
    lv_label_set_text_fmt(g_video.bitrate_val, "%d.%d Mbps", kbps / 1000, (kbps % 1000) / 100);
    lv_label_set_text_fmt(g_video.sum_title, "%dp%d", s->height, s->fps);
    lv_label_set_text_fmt(g_video.sum_detail, "%s" BULLET "%d.%d Mbps" BULLET "%s", config_codec_name(s->codec),
                          kbps / 1000, (kbps % 1000) / 100, s->hdr ? "HDR" : "SDR");
    lv_bar_set_value(g_video.bw_bar, kbps * 100 / 50000, LV_ANIM_ON);
    lv_label_set_text_fmt(g_video.bw_caption, "%d.%d of 50 Mbps", kbps / 1000, (kbps % 1000) / 100);
}

/* widgets -> config */
static void changed_cb(lv_event_t *e) {
    (void)e;
    stream_settings_t *s = &config_get()->stream;
    bool hd = lv_dropdown_get_selected(g_video.res) == 1;
    s->codec = (video_codec_t)lv_dropdown_get_selected(g_video.codec);
    s->width = hd ? 1920 : 1280;
    s->height = hd ? 1080 : 720;
    s->fps = lv_dropdown_get_selected(g_video.fps) == 1 ? 60 : 30;
    s->bitrate_kbps = lv_slider_get_value(g_video.bitrate);
    s->hdr = lv_obj_has_state(g_video.hdr, LV_STATE_CHECKED);
    refresh();
    app_settings_changed();
}

static void build_settings(lv_obj_t *page, const stream_settings_t *s) {
    lv_obj_t *card = ui_card(page, NULL);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_t *hdr = ui_card_header(card, LV_SYMBOL_VIDEO, "Stream quality", "Applied the next time you connect");
    lv_obj_set_style_pad_bottom(hdr, SP_S, 0);

    lv_obj_t *row = ui_setting_row(card, "Codec", "Decoded in hardware by the TV");
    g_video.codec = ui_dropdown(row, "H.264\nH.265\nH.265 HDR", (uint16_t)s->codec, changed_cb);
    row = ui_setting_row(card, "Resolution", "Higher needs more bandwidth");
    g_video.res = ui_dropdown(row, "1280x720\n1920x1080", s->height >= 1080, changed_cb);
    row = ui_setting_row(card, "Frame rate", "60 fps feels smoother in games");
    g_video.fps = ui_dropdown(row, "30 fps\n60 fps", s->fps >= 60, changed_cb);
    row = ui_setting_row(card, "Bitrate", "Lower it if the picture stutters");
    g_video.bitrate = ui_slider_control(row, 2000, 50000, s->bitrate_kbps, 1000, changed_cb, &g_video.bitrate_val);
    row = ui_setting_row(card, "HDR", "Needs an HDR picture mode on the TV");
    ui_last_row(row);
    g_video.hdr = lv_switch_create(row);
    lv_obj_set_size(g_video.hdr, 84, 44);
    if (s->hdr) lv_obj_add_state(g_video.hdr, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_video.hdr, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_summary(lv_obj_t *page) {
    lv_obj_t *sum = ui_card(page, NULL);
    lv_obj_set_size(sum, LV_PCT(34), LV_PCT(100));
    ui_card_header(sum, LV_SYMBOL_IMAGE, "Summary", "What the console will send");
    lv_obj_t *big = ui_box(sum, LV_FLEX_FLOW_COLUMN, SP_XS);
    lv_obj_set_flex_align(big, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_top(big, SP_L, 0);
    g_video.sum_title = ui_text(big, TXT_DISPLAY, "");
    g_video.sum_detail = ui_text(big, TXT_CAPTION, "");
    ui_spacer(sum);
    ui_text(sum, TXT_OVERLINE, "BANDWIDTH");
    g_video.bw_bar = lv_bar_create(sum);
    lv_obj_set_size(g_video.bw_bar, LV_PCT(100), 14);
    g_video.bw_caption = ui_text(sum, TXT_CAPTION, "");
    lv_obj_t *chips = ui_box(sum, LV_FLEX_FLOW_ROW_WRAP, SP_S);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_style_pad_top(chips, SP_S, 0);
    ui_chip(chips, LV_SYMBOL_CHARGE, "Low latency", NULL);
    ui_chip(chips, LV_SYMBOL_OK, "Hardware decode", NULL);
}

void page_video_build(lv_obj_t *page) {
    build_settings(page, &config_get()->stream);
    build_summary(page);
    refresh();
}
