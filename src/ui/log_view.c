/* ui/log_view.c - recoloured, timestamped lines in a scrollable console (see log_view.h). */
#include "log_view.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "../log.h"
#include "theme.h"

#define LOG_LINES 240
#define LOG_LINE_LEN 160
static char g_lines[LOG_LINES][LOG_LINE_LEN];
static int g_count;
static lv_obj_t *g_view, *g_label;

static uint32_t tag_color(log_level_t level, const char *tag) {
    switch (level) {
        case LOG_OK: return COL_OK;
        case LOG_WARN: return COL_WARN;
        case LOG_ERR: return COL_ERR;
        case LOG_INFO: break;
    }
    if (!strcmp(tag, "NET") || !strcmp(tag, "REG")) return COL_INFO;
    return COL_ACCENT_HI;
}

static void store(const char *when, const char *tag, uint32_t tag_col, const char *msg) {
    if (g_count == LOG_LINES) {
        memmove(g_lines[0], g_lines[1], sizeof(g_lines[0]) * (LOG_LINES - 1));
        g_count--;
    }
    snprintf(g_lines[g_count++], LOG_LINE_LEN, "#%06x %s#   #%06x [%s]#   %s", (unsigned)COL_DIM, when,
             (unsigned)tag_col, tag, msg);
}

static void refresh(void) {
    static char buf[LOG_LINES * (LOG_LINE_LEN + 1)];
    size_t n = 0;
    for (int i = 0; i < g_count; i++)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "\n" : "", g_lines[i]);
    lv_label_set_text(g_label, buf);
    lv_obj_update_layout(g_view);
    lv_obj_scroll_to_y(g_view, LV_COORD_MAX, LV_ANIM_ON);
}

static void sink(log_level_t level, const char *tag, const char *msg) {
    unsigned s = SDL_GetTicks() / 1000;
    char when[16];
    snprintf(when, sizeof(when), "%02u:%02u", s / 60, s % 60);
    store(when, tag, tag_color(level, tag), msg);
    refresh();
}

lv_obj_t *log_view_create(lv_obj_t *parent) {
    g_view = lv_obj_create(parent);
    lv_obj_add_style(g_view, &st_console, 0);
    lv_obj_set_width(g_view, LV_PCT(100));
    lv_obj_set_flex_grow(g_view, 1);
    lv_obj_set_scroll_dir(g_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_view, LV_SCROLLBAR_MODE_AUTO);
    g_label = lv_label_create(g_view);
    lv_obj_set_width(g_label, LV_PCT(100));
    lv_label_set_recolor(g_label, true);
    lv_label_set_text(g_label, "");
    log_set_sink(sink);
    return g_view;
}

void log_view_demo_backlog(void) {
    enum { N = 90 };
    char when[16], msg[128];
    unsigned r = 12345;
    for (int i = 0; i < N; i++) {
        unsigned ago = (unsigned)(N - i) * 7;
        r = r * 1103515245u + 12345u;
        unsigned v = (r >> 16) % 100;
        snprintf(when, sizeof(when), "-%02u:%02u", ago / 60, ago % 60);
        switch (i % 6) {
        case 0: snprintf(msg, sizeof(msg), "%u fps, %u.%u Mbps, decode %u.%u ms", 58 + v % 3, 13 + v % 4, v % 10, 4 + v % 3, v % 10);
                store(when, "VIDEO", COL_ACCENT_HI, msg); break;
        case 1: snprintf(msg, sizeof(msg), "rtt %u ms, jitter %u ms, loss %u.%u%%", 6 + v % 9, 1 + v % 4, v % 2, v % 10);
                store(when, "NET", COL_INFO, msg); break;
        case 2: snprintf(msg, sizeof(msg), "opus 48 kHz stereo, buffer %u ms", 20 + v % 30);
                store(when, "AUDIO", COL_OK, msg); break;
        case 3: snprintf(msg, sizeof(msg), "DualShock 4 reports at %u Hz", 248 + v % 5);
                store(when, "INPUT", COL_OK, msg); break;
        case 4: snprintf(msg, sizeof(msg), "keyframe %u received (%u KB)", 1000 + (unsigned)i * 37, 90 + v);
                store(when, "VIDEO", COL_ACCENT_HI, msg); break;
        default: if (v < 30) {
                    snprintf(msg, sizeof(msg), "frame %u arrived %u ms late", 4000 + (unsigned)i * 61, 8 + v % 20);
                    store(when, "WARN", COL_WARN, msg);
                 } else {
                    snprintf(msg, sizeof(msg), "bitrate target %u.%u Mbps", 12 + v % 5, v % 10);
                    store(when, "NET", COL_INFO, msg);
                 }
        }
    }
    store("--:--", "APP", COL_ACCENT_HI, "previous session ended, new session below");
    refresh();
}
