/* chiaki/session.h - a PS Remote Play session and console registration, wrapping chiaki-ng's libchiaki.
 *
 * STUB: libchiaki is not linked yet. session_available() returns false; while
 * it does, session_start()/session_poll() run a simulated session on the main
 * thread (~3 s of progress, then connected) so app.c, the simulator and the
 * host tests exercise the real event mapping. Implement this file against
 * chiaki-ng/lib (chiaki_session_*, chiaki_regist_*, chiaki_discovery_wakeup);
 * scripts/chiaki-lg-build-webos.sh is the proven cross-build recipe.
 *
 * Threading: libchiaki calls back on its own threads. Queue events here and
 * hand them to the main loop through session_poll() / session_reg_poll();
 * never touch LVGL or the app state from a libchiaki thread.
 * Media: the still-encoded H.264/H.265 NAL units and Opus frames go straight
 * to media/ndl.h (hardware decode), never through LVGL.
 *
 * Registration (pairing) has two ways in, both ending in REG_EV_SUCCESS with
 * the keys to store via config_host_add():
 *   PIN: session_reg_begin_pin(addr, pin, ps5) - the PIN from the console's
 *        Settings > System > Remote Play > Link Device (chiaki_regist_start).
 *   PSN: show session_reg_psn_url() (QR + text), the user signs in on a phone
 *        and types the `code=` value of the redirect page into
 *        session_reg_submit_code(); that yields REG_EV_PSN_LINKED with the
 *        account and tokens (store via config_psn_set()), then
 *        session_reg_begin_psn(account_id, ps5) registers over PSN.
 * One operation runs at a time; session_reg_stop() cancels it (REG_EV_CANCELED).
 *
 * STUB: while session_reg_available() is false the operations are simulated
 * from session_reg_poll() (no threads): progress over ~2 s, then a success
 * with an obviously fake console ("Simulated PS5", MAC 02:00:..), so the
 * simulator and the host tests can walk the whole flow. The simulation fails
 * on PIN 00000000 and on the code "fail" to exercise the error path.
 */
#ifndef CHIAKI_SESSION_H
#define CHIAKI_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "../config/config.h"

/* an OAuth authorization code (or the redirect URL around it) is long: chiaki-ng keeps no limit either */
#define SESSION_PSN_CODE_MAX 1024

typedef enum {
    SESSION_EV_PROGRESS,    /* connecting, progress 0..100 */
    SESSION_EV_CONNECTED,   /* CHIAKI_EVENT_CONNECTED: stream is up */
    SESSION_EV_LOGIN_PIN,   /* CHIAKI_EVENT_LOGIN_PIN_REQUEST: ask the user, then session_set_login_pin() */
    SESSION_EV_RUMBLE,      /* CHIAKI_EVENT_RUMBLE: forward to the pad (evdev FF_RUMBLE works on this TV) */
    SESSION_EV_QUIT,        /* CHIAKI_EVENT_QUIT: session over, see reason */
} session_event_type_t;

typedef struct {
    session_event_type_t type;
    int progress;
    uint8_t rumble_left, rumble_right;
    char reason[128];
} session_event_t;

/* pad state sent to the console; mirrors ChiakiControllerState */
typedef struct {
    uint32_t buttons;               /* ChiakiControllerButton bitmask */
    uint8_t l2, r2;                 /* 0..255 (ABS_RX/ABS_RY on this TV) */
    int16_t left_x, left_y, right_x, right_y;
    bool touch_down;
    uint16_t touch_x, touch_y;      /* DS4 touchpad, ABS_MT_POSITION_X/Y */
} session_pad_t;

bool session_available(void);       /* false until libchiaki is linked */

bool session_start(const config_host_t *host, const stream_settings_t *stream);
void session_stop(void);
bool session_poll(session_event_t *out);   /* main thread: next queued event, if any */
const config_host_t *session_active_host(void);   /* the console of the running (or last) session, or NULL */

void session_set_pad(const session_pad_t *pad);
void session_set_login_pin(const char *pin);

/* wake a console in standby (chiaki_discovery_wakeup, UDP broadcast works on this TV) */
bool session_wakeup(const config_host_t *host);

/* ---------- registration ---------- */

typedef enum {
    REG_EV_PROGRESS,     /* progress 0..100 of the running operation */
    REG_EV_PSN_LINKED,   /* code exchanged: psn is valid; store it, then session_reg_begin_psn() */
    REG_EV_SUCCESS,      /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS: host is valid */
    REG_EV_FAILED,       /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED (or the token exchange): see reason */
    REG_EV_CANCELED,     /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED, after session_reg_stop() */
} session_reg_event_type_t;

typedef struct {
    session_reg_event_type_t type;
    int progress;
    config_host_t host;        /* valid on REG_EV_SUCCESS */
    config_psn_t psn;          /* valid on REG_EV_PSN_LINKED */
    char reason[128];          /* valid on REG_EV_FAILED */
} session_reg_event_t;

bool session_reg_available(void);                 /* false until libchiaki is linked (and http exists) */
bool session_reg_begin_pin(const char *addr, const char *pin, bool ps5);
bool session_reg_begin_psn(const uint8_t account_id[8], bool ps5);
bool session_reg_submit_code(const char *code);   /* PSN redirect `code=` value -> tokens + account id */
void session_reg_stop(void);
bool session_reg_running(void);
bool session_reg_poll(session_reg_event_t *out);  /* main thread only: next queued event, if any */
const char *session_reg_psn_url(void);            /* Sony authorize URL with duid = config_device_uid() */

#endif
