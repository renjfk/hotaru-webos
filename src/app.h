/* app.h - application state the UI renders: consoles, selection, connection, registration.
 *
 * The UI only displays this state and calls these functions; connection logic
 * never lives in widgets. Changes are announced through one listener.
 * The host list is the consoles behind config's entries (config_host()), in
 * registration order; it may be empty (app_selected() is then -1). One console can
 * have two entries, a PIN pairing (LAN) and the linked PSN account's device: they are
 * one app host with two routes when the PSN entry has the LAN entry's name and kind
 * (the only thing they share; chiaki-ng matches them the same way). Connect uses the
 * host's route: the LAN when it has one, unless app_set_route() picked PSN (saved in
 * config). When the LAN attempt never reaches the console (it is not on this network,
 * or asleep) and the host has a PSN route, the same connect carries on over PSN
 * (app_conn_fallback); the saved route does not change. Connecting goes through
 * chiaki/session.h (session_start + the session_poll events).
 *
 * Discovery (session_discovery_*, paused while streaming) gives every console with a PIN
 * pairing its state: ready, in rest mode, or offline once it has not answered for a while;
 * its firmware and what runs on it; and its address, followed when the router hands it a
 * new one (matched by MAC). Connecting acts on it: a console in rest mode is woken first and
 * the session starts once it answers ready; one that is offline while discovery hears other
 * consoles (so broadcasts work here) goes straight over PSN when it has that route.
 * Consoles found but not paired are offered to Add a console (app_found_*).
 *
 * Registration: app_reg_submit_*() start pairing (PIN, or PSN account via the
 * QR + code flow), app_tick() drives it from session_reg_poll(), and every
 * state/progress change is announced as APP_CHANGED_REG. On success the
 * console is stored in config (and saved), selected, and the list changes
 * (APP_CHANGED_HOSTS).
 *
 * Updates: app_init() starts the one release check of the run (net/update.h); its answer is
 * announced as APP_CHANGED_UPDATE.
 */
#ifndef HOTARU_APP_H
#define HOTARU_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* READY / STANDBY: discovery hears it (awake / in rest mode); OFFLINE: a console with a PIN pairing
 * that has not answered for a while; UNKNOWN: not heard yet, or reached over PSN only */
typedef enum { HOST_READY, HOST_STANDBY, HOST_OFFLINE, HOST_UNKNOWN } host_state_t;
typedef enum { ROUTE_LAN, ROUTE_PSN } host_route_t; /* how Start Remote Play reaches the console */

/* UI view of a console; the pointers stay valid until the next APP_CHANGED_HOSTS */
typedef struct {
    const char *name, *model, *addr, *firmware; /* addr: the LAN address, or "via PSN"; firmware "" if unknown */
    const char *running;                        /* what runs on it (discovery), "" if nothing or unknown */
    int max_height, max_fps;
    host_state_t state;
    bool ps5;
    bool lan, psn;      /* the routes it has: a PIN pairing on the LAN, the PSN account's device */
    host_route_t route; /* the one app_connect() uses */
} app_host_t;

typedef enum { CONN_IDLE, CONN_CONNECTING, CONN_STREAMING } conn_state_t;

typedef enum {
    APP_CHANGED_HOST,     /* selection changed */
    APP_CHANGED_HOSTS,    /* the host list itself changed (registered / forgotten) */
    APP_CHANGED_CONN,     /* connection state changed */
    APP_CHANGED_PROGRESS, /* connecting progress only */
    APP_CHANGED_REG,      /* registration state, progress or result changed */
    APP_CHANGED_PIN,      /* the console asked for the Remote Play login PIN */
    APP_CHANGED_STATE,    /* discovery: a console's state, firmware or running app changed */
    APP_CHANGED_UPDATE,   /* the release check answered: app_update_version() */
} app_change_t;

typedef void (*app_listener_t)(app_change_t what);

void app_init(app_listener_t listener); /* never calls the listener itself */
void app_tick(void);                    /* every main-loop iteration: session events, deferred saves */
bool app_running(void);
void app_quit(void); /* also writes pending settings */

int app_host_count(void);
const app_host_t *app_host(int index); /* NULL if out of range */
int app_selected(void);                /* -1 when there are no consoles */
void app_select(int index);

conn_state_t app_conn_state(void);
int app_conn_progress(void); /* 0..100 */
void app_connect(void);      /* selected host over its route, config_get()->stream */
void app_disconnect(void);
void app_set_route(host_route_t route); /* the selected host's route, saved; ignored when it lacks that one */
const char *app_conn_error(void);       /* why the last session failed or ended on its own; "" otherwise */
bool app_conn_over_psn(void);           /* the session (or the attempt) runs over PSN */
bool app_conn_fallback(void);           /* ... because the local network did not reach the console */
bool app_conn_waking(void);             /* connecting: waiting for a console in rest mode to wake up */

/* The console negotiates the video settings when the session starts, so an edit made while
 * connected only applies through a restart: pending = they differ from what the session started
 * with; restart = close it and reconnect to the same console after a short pause. */
bool app_stream_settings_pending(void);
void app_restart_stream(void);

void app_wake(void);   /* over the selected host's LAN route */
void app_forget(void); /* removes the selected console (all its entries) from config */

/* the console asked for the Remote Play login PIN mid-connect: the UI shows a prompt
 * while app_login_pin_pending(); app_login_pin_submit(NULL) cancels */
bool app_login_pin_pending(void);
void app_login_pin_submit(const char *pin);

/* the UI edited config_get(): save shortly (debounced, sliders fire on every step) */
void app_settings_changed(void);

/* a release newer than this build: its version ("0.3"), "" while none is known */
const char *app_update_version(void);

/* probe mode (log.h log_probe): on while Settings > About > Diagnostics is, or for this whole run
 * once forced (a launch parameter, HOTARU_PROBE=1 or the flag file; see main.c) */
void app_probe_force(void);
bool app_probe_forced(void);

/* ---------- registration ---------- */

typedef enum { APP_REG_METHOD_PIN, APP_REG_METHOD_PSN } app_reg_method_t;
typedef enum { APP_REG_IDLE, APP_REG_RUNNING, APP_REG_SUCCESS, APP_REG_FAILED } app_reg_state_t;

/* account_id_b64 is optional (base64; a PS5 or a PS4 on 7.0+ needs it) */
void app_reg_submit_pin(const char *addr, const char *pin, bool ps5, const char *account_id_b64);
void app_psn_refresh(void);                   /* list the PSN account's consoles */
bool app_psn_set_account_id(const char *b64); /* base64 of 8 non-zero bytes: stored and saved; false if not */
void app_reg_submit_psn_code(const char *code, bool ps5); /* PSN path: code -> tokens -> register */
void app_reg_cancel(void); /* stops a running registration, or clears a result back to IDLE */

app_reg_state_t app_reg_state(void);
app_reg_method_t app_reg_method(void);
int app_reg_progress(void);       /* 0..100 while RUNNING */
const char *app_reg_status(void); /* what is happening / what happened, for the UI; "" when idle */
const char *app_reg_error(void);  /* "" when none */
const char *app_reg_psn_url(void);

bool app_local_ip(char *ifname, size_t ifname_len, char *ip, size_t ip_len);

/* consoles discovery hears that have no PIN pairing here yet; valid until the next app_tick() */
typedef struct {
    const char *name, *addr;
    bool ps5, ready;
} app_found_t;
int app_found_count(void);
const app_found_t *app_found(int index);

#endif
