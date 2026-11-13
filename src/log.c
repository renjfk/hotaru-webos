/* log.c - file log + on-screen event feed (see log.h). */
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PENDING 64 /* events from other threads waiting for log_pump() */

static FILE *g_file;
static log_sink_t g_sink;
static pthread_t g_main;
static volatile bool g_probe;

static pthread_mutex_t g_pending_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    log_level_t level;
    char tag[16], msg[128];
} g_pending[PENDING];
static unsigned g_head, g_tail;

void log_init(const char *path) {
    g_file = fopen(path, "w");
    g_main = pthread_self();
}

void log_close(void) {
    /* do not fclose: libchiaki/PSN worker threads may still log while the process winds down, and
     * writing to a closed FILE* is undefined. Flushing keeps the log complete; the OS closes it. */
    if (g_file) fflush(g_file);
    g_file = NULL;
}

void log_set_sink(log_sink_t sink) { g_sink = sink; }

static void vlog(const char *prefix, const char *fmt, va_list ap) {
    FILE *f = g_file;
    if (!f) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t secs = ts.tv_sec;
    struct tm tm;
    localtime_r(&secs, &tm);
    /* one line may be written from several threads (libchiaki logs from its own); keep it atomic */
    flockfile(f);
    fprintf(f, "%02d:%02d:%02d.%03ld %s", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, prefix);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
    funlockfile(f);
}

void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog("", fmt, ap);
    va_end(ap);
}

void log_event(log_level_t level, const char *tag, const char *fmt, ...) {
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    for (char *p = msg; *p; p++)
        if (*p == '#') *p = ' '; /* '#' is LVGL's label recolour marker */
    log_msg("ui: [%s] %s", tag, msg);
    if (!g_sink) return;
    if (pthread_equal(pthread_self(), g_main)) {
        g_sink(level, tag, msg);
        return;
    }
    pthread_mutex_lock(&g_pending_lock);      /* the sink draws: it waits for the main thread */
    if (g_tail - g_head == PENDING) g_head++; /* full: the oldest goes */
    unsigned i = g_tail++ % PENDING;
    g_pending[i].level = level;
    snprintf(g_pending[i].tag, sizeof(g_pending[i].tag), "%s", tag);
    snprintf(g_pending[i].msg, sizeof(g_pending[i].msg), "%s", msg);
    pthread_mutex_unlock(&g_pending_lock);
}

void log_pump(void) {
    for (;;) {
        log_level_t level;
        char tag[16], msg[128];
        pthread_mutex_lock(&g_pending_lock);
        bool any = g_head != g_tail;
        if (any) {
            unsigned i = g_head++ % PENDING;
            level = g_pending[i].level;
            memcpy(tag, g_pending[i].tag, sizeof(tag));
            memcpy(msg, g_pending[i].msg, sizeof(msg));
        }
        pthread_mutex_unlock(&g_pending_lock);
        if (!any) return;
        if (g_sink) g_sink(level, tag, msg);
    }
}

void log_set_probe(bool on) {
    if (on == g_probe) return;
    g_probe = on;
    log_msg("probe: mode %s", on ? "on" : "off");
}

bool log_probe_on(void) { return g_probe; }

void log_probe(const char *fmt, ...) {
    if (!g_probe) return;
    va_list ap;
    va_start(ap, fmt);
    vlog("probe: ", fmt, ap);
    va_end(ap);
}
