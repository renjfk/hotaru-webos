/* app.h - application state the UI renders: consoles, selection, connection.
 *
 * The UI only displays this state and calls these functions; connection logic
 * never lives in widgets. Changes are announced through one listener.
 * Until chiaki/session.h is implemented the host list is demo data and
 * connecting is simulated (both marked DEMO in app.c).
 */
#ifndef CHIAKI_APP_H
#define CHIAKI_APP_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { HOST_READY, HOST_STANDBY, HOST_OFFLINE } host_state_t;

typedef struct {
    const char *name, *model, *addr, *firmware;
    int max_height, max_fps;
    host_state_t state;
} app_host_t;

typedef enum { CONN_IDLE, CONN_CONNECTING, CONN_STREAMING } conn_state_t;

typedef enum {
    APP_CHANGED_HOST,       /* selection changed */
    APP_CHANGED_CONN,       /* connection state changed */
    APP_CHANGED_PROGRESS,   /* connecting progress only */
} app_change_t;

typedef void (*app_listener_t)(app_change_t what);

void app_init(app_listener_t listener);   /* never calls the listener itself */
void app_tick(void);                      /* every main-loop iteration: session events */
bool app_running(void);
void app_quit(void);

int app_host_count(void);
const app_host_t *app_host(int index);
int app_selected(void);
void app_select(int index);

conn_state_t app_conn_state(void);
int app_conn_progress(void);              /* 0..100 */
void app_connect(void);                   /* selected host, config_get()->stream */
void app_disconnect(void);

void app_wake(void);
void app_forget(void);
void app_register(const char *addr, const char *pin);

bool app_local_ip(char *ifname, size_t ifname_len, char *ip, size_t ip_len);

#endif
