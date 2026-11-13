/* log.h - the app's single logging facility.
 *
 * Everything goes to LOG_PATH (there is no journald on the TV; read it over
 * SSH), every line prefixed with the local time ("HH:MM:SS.mmm "). Entry points:
 *   log_msg()   lifecycle/diagnostic lines, file only ("display ... ready").
 *   log_event() user-visible events, written as "ui: [TAG] msg" and forwarded
 *               to the registered sink (the on-screen Log panel).
 *   log_probe() diagnostics for checking the hardware on the TV, written as
 *               "probe: msg" only while probe mode is on (log_set_probe).
 * Any thread may log. The sink is only ever called on the main thread: events
 * logged elsewhere (libchiaki, PSN workers) wait for log_pump(), which the
 * main loop calls every iteration.
 */
#ifndef HOTARU_LOG_H
#define HOTARU_LOG_H

#include <stdbool.h>

#define LOG_PATH "/tmp/hotaru.log"

typedef enum { LOG_INFO, LOG_OK, LOG_WARN, LOG_ERR } log_level_t;

/* receives every log_event(); tag is without brackets, msg has no '#' */
typedef void (*log_sink_t)(log_level_t level, const char *tag, const char *msg);

void log_init(const char *path); /* on the main thread */
void log_close(void);
void log_set_sink(log_sink_t sink);
void log_pump(void); /* main thread: hand events from other threads to the sink */

void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_event(log_level_t level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* probe mode: raw input, discovery answers, the network and the frame timing, for the checks
 * that only the TV can answer (docs/TV-CHECKS.md) */
void log_set_probe(bool on);
bool log_probe_on(void);
void log_probe(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
