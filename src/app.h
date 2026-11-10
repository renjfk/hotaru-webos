/* app.h - application state the UI renders: consoles, selection, connection, registration.
 *
 * The UI only displays this state and calls these functions; connection logic
 * never lives in widgets. Changes are announced through one listener.
 * The host list is the registered consoles in config (config_host()), in
 * registration order; it may be empty (app_selected() is then -1). Connecting
 * goes through chiaki/session.h (session_start + the session_poll events),
 * which simulates the handshake until libchiaki is linked; a console's
 * reachability stays unknown until discovery exists.
 *
 * Registration: app_reg_submit_*() start pairing (PIN, or PSN account via the
 * QR + code flow), app_tick() drives it from session_reg_poll(), and every
 * state/progress change is announced as APP_CHANGED_REG. On success the
 * console is stored in config (and saved), selected, and the list changes
 * (APP_CHANGED_HOSTS).
 */
#ifndef CHIAKI_APP_H
#define CHIAKI_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HOST_READY, HOST_STANDBY, HOST_OFFLINE, HOST_UNKNOWN } host_state_t;

/* UI view of a registered console; the pointers stay valid until the next APP_CHANGED_HOSTS */
typedef struct {
    const char *name, *model, *addr, *firmware;
    int max_height, max_fps;
    host_state_t state;
} app_host_t;

typedef enum { CONN_IDLE, CONN_CONNECTING, CONN_STREAMING } conn_state_t;

typedef enum {
    APP_CHANGED_HOST,       /* selection changed */
    APP_CHANGED_HOSTS,      /* the host list itself changed (registered / forgotten) */
    APP_CHANGED_CONN,       /* connection state changed */
    APP_CHANGED_PROGRESS,   /* connecting progress only */
    APP_CHANGED_REG,        /* registration state, progress or result changed */
} app_change_t;

typedef void (*app_listener_t)(app_change_t what);

void app_init(app_listener_t listener);   /* never calls the listener itself */
void app_tick(void);                      /* every main-loop iteration: session events, deferred saves */
bool app_running(void);
void app_quit(void);                      /* also writes pending settings */

int app_host_count(void);
const app_host_t *app_host(int index);    /* NULL if out of range */
int app_selected(void);                   /* -1 when there are no consoles */
void app_select(int index);

conn_state_t app_conn_state(void);
int app_conn_progress(void);              /* 0..100 */
void app_connect(void);                   /* selected host, config_get()->stream */
void app_disconnect(void);

void app_wake(void);
void app_forget(void);                    /* removes the selected console from config */

/* the UI edited config_get(): save shortly (debounced, sliders fire on every step) */
void app_settings_changed(void);

/* ---------- registration ---------- */

typedef enum { APP_REG_METHOD_PIN, APP_REG_METHOD_PSN } app_reg_method_t;
typedef enum { APP_REG_IDLE, APP_REG_RUNNING, APP_REG_SUCCESS, APP_REG_FAILED } app_reg_state_t;

void app_reg_submit_pin(const char *addr, const char *pin, bool ps5);
void app_reg_submit_psn(const uint8_t account_id[8], bool ps5);   /* an already linked account */
void app_reg_submit_psn_code(const char *code, bool ps5);         /* PSN path: code -> tokens -> register */
void app_reg_cancel(void);                /* stops a running registration, or clears a result back to IDLE */

app_reg_state_t app_reg_state(void);
app_reg_method_t app_reg_method(void);
int app_reg_progress(void);               /* 0..100 while RUNNING */
const char *app_reg_status(void);         /* what is happening / what happened, for the UI; "" when idle */
const char *app_reg_error(void);          /* "" when none */
const char *app_reg_psn_url(void);

bool app_local_ip(char *ifname, size_t ifname_len, char *ip, size_t ip_len);

#endif
