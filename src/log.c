/* log.c - file log + on-screen event feed (see log.h). */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *g_file;
static log_sink_t g_sink;

void log_init(const char *path) { g_file = fopen(path, "w"); }

void log_close(void) {
    if (g_file) fclose(g_file);
    g_file = NULL;
}

void log_set_sink(log_sink_t sink) { g_sink = sink; }

void log_msg(const char *fmt, ...) {
    if (!g_file) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
    fflush(g_file);
}

void log_event(log_level_t level, const char *tag, const char *fmt, ...) {
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    for (char *p = msg; *p; p++)
        if (*p == '#') *p = ' ';   /* '#' is LVGL's label recolour marker */
    log_msg("ui: [%s] %s", tag, msg);
    if (g_sink) g_sink(level, tag, msg);
}
