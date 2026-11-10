/* log.h - the app's single logging facility.
 *
 * Everything goes to LOG_PATH (there is no journald on the TV; read it over
 * SSH). Two entry points:
 *   log_msg()   lifecycle/diagnostic lines, file only ("display ... ready").
 *   log_event() user-visible events, written as "ui: [TAG] msg" and forwarded
 *               to the registered sink (the on-screen Log page).
 */
#ifndef CHIAKI_LOG_H
#define CHIAKI_LOG_H

#define LOG_PATH "/tmp/chiaki.log"

typedef enum { LOG_INFO, LOG_OK, LOG_WARN, LOG_ERR } log_level_t;

/* receives every log_event(); tag is without brackets, msg has no '#' */
typedef void (*log_sink_t)(log_level_t level, const char *tag, const char *msg);

void log_init(const char *path);
void log_close(void);
void log_set_sink(log_sink_t sink);

void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_event(log_level_t level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
