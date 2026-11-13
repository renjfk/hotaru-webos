/* chiaki/session.h - a PS Remote Play session and console registration, wrapping chiaki-ng's libchiaki.
 *
 * Two backends implement this API, selected at compile time:
 *   - chiaki/session_real.c (CHIAKI_HAVE_LIBCHIAKI, linked by CMakeLists.txt via
 *     CHIAKI_LIB_STAGING): real libchiaki; scripts/chiaki-lib-build-webos.sh is the
 *     proven cross-build recipe. session_available() returns true.
 *   - chiaki/session.c (otherwise): a simulation on the main thread (~3 s of progress,
 *     then connected; registration ~2 s) so the simulator and the host tests exercise
 *     the same app-side event mapping. session_available() returns false.
 * PSN: the OAuth token exchange, the account's device list, refreshing an expired access token
 * and the holepunch connection (connect to a via_psn console) are real in the libchiaki
 * backend; the PIN path is real too.
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
 *        session_psn_fetch_devices() yields REG_EV_PSN_DEVICES with the consoles
 *        attached to the account (chiaki_holepunch_list_devices).
 * One operation runs at a time; session_reg_stop() cancels it (REG_EV_CANCELED).
 *
 * STUB: while session_reg_available() is false the operations are simulated
 * from session_reg_poll() (no threads): progress over ~2 s, then a success
 * with an obviously fake console ("Simulated PS5", MAC 02:00:..), so the
 * simulator and the host tests can walk the whole flow. The simulation fails
 * on PIN 00000000 and on the code "fail" to exercise the error path, and a
 * session with an address ending in ".99" emits a login-PIN request mid-handshake;
 * one ending in ".98" is never reached on the local network (an unreachable quit).
 */
#ifndef CHIAKI_WEBOS_SESSION_H
#define CHIAKI_WEBOS_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "../config/config.h"

/* an OAuth authorization code (or the redirect URL around it) is long: chiaki-ng keeps no limit either */
#define SESSION_PSN_CODE_MAX 1024

typedef enum {
    SESSION_EV_PROGRESS,  /* connecting, progress 0..100 */
    SESSION_EV_CONNECTED, /* CHIAKI_EVENT_CONNECTED: stream is up */
    SESSION_EV_LOGIN_PIN, /* CHIAKI_EVENT_LOGIN_PIN_REQUEST: ask the user, then session_set_login_pin() */
    SESSION_EV_RUMBLE,    /* CHIAKI_EVENT_RUMBLE: forward to the pad (evdev FF_RUMBLE works on this TV) */
    SESSION_EV_QUIT,      /* CHIAKI_EVENT_QUIT: session over, see reason */
} session_event_type_t;

typedef struct {
    session_event_type_t type;
    int progress;
    uint8_t rumble_left, rumble_right;
    char reason[128];
    bool unreachable; /* QUIT of a local-network session that never reached the console (no answer on
                       * its session port): the console may be elsewhere, so PSN is worth a try */
} session_event_t;

/* session_pad_t.buttons bits: the canonical PS button layout, the same values as
 * input/input.h PAD_BTN_*; the real backend maps these to ChiakiControllerButton. */
#define SESSION_PAD_BTN_CROSS      (1u << 0)
#define SESSION_PAD_BTN_CIRCLE     (1u << 1)
#define SESSION_PAD_BTN_SQUARE     (1u << 2)
#define SESSION_PAD_BTN_TRIANGLE   (1u << 3)
#define SESSION_PAD_BTN_DPAD_LEFT  (1u << 4)
#define SESSION_PAD_BTN_DPAD_RIGHT (1u << 5)
#define SESSION_PAD_BTN_DPAD_UP    (1u << 6)
#define SESSION_PAD_BTN_DPAD_DOWN  (1u << 7)
#define SESSION_PAD_BTN_L1         (1u << 8)
#define SESSION_PAD_BTN_R1         (1u << 9)
#define SESSION_PAD_BTN_L3         (1u << 10)
#define SESSION_PAD_BTN_R3         (1u << 11)
#define SESSION_PAD_BTN_OPTIONS    (1u << 12)
#define SESSION_PAD_BTN_SHARE      (1u << 13)
#define SESSION_PAD_BTN_TOUCHPAD   (1u << 14)
#define SESSION_PAD_BTN_PS         (1u << 15)

/* pad state sent to the console; mirrors ChiakiControllerState */
typedef struct {
    uint32_t buttons; /* SESSION_PAD_BTN_* */
    uint8_t l2, r2;   /* 0..255 (ABS_RX/ABS_RY on this TV) */
    int16_t left_x, left_y, right_x, right_y;
    bool touch_down;
    uint16_t touch_x, touch_y; /* DS4 touchpad, ABS_MT_POSITION_X/Y */
} session_pad_t;

bool session_available(void); /* true when the real libchiaki backend is linked */
/* start a session: LAN when the host has an address, else over PSN (an async holepunch
 * connect when via_psn; progress/quit arrive through session_poll) */
bool session_start(const config_host_t *host, const stream_settings_t *stream);
void session_stop(void);
bool session_poll(session_event_t *out);        /* main thread: next queued event, if any */
const config_host_t *session_active_host(void); /* the console of the running session, or NULL when none */

void session_set_pad(const session_pad_t *pad);
void session_set_login_pin(const char *pin);
const session_pad_t *session_last_pad(void); /* the last state handed to the console (tests/diagnostics) */

/* wake a console in standby (chiaki_discovery_wakeup, UDP broadcast works on this TV) */
bool session_wakeup(const config_host_t *host);

/* ---------- discovery ----------
 * chiaki_discovery_service at chiaki-ng's pace: a search every 500 ms to 255.255.255.255 and
 * every interface's broadcast address, on the PS4 (987) and the PS5 (9302) port; a console that
 * misses 3 in a row is dropped. The list is copied on libchiaki's thread under a lock, so any
 * thread may read it. The simulation answers for the paired consoles in config (an address
 * ending in ".98" never answers, one ending in ".97" sleeps until woken) plus one unpaired
 * PS5. */

#define SESSION_FOUND_MAX 16

typedef struct {
    char addr[64];    /* where it answered from */
    char host_id[16]; /* its MAC as 12 hex digits (config_host_t.server_mac) */
    char name[64];
    char system_version[16]; /* firmware, e.g. "09400000" for 9.40 */
    char app_name[64];       /* what runs on it while it is awake, "" otherwise */
    bool ps5, ready;         /* ready: awake (200 Ok); else in rest mode (620 Server Standby) */
} session_found_t;

bool session_discovery_start(void);
void session_discovery_stop(void);
/* the consoles answering now, up to max; *gen (optional) changes whenever the list does */
int session_discovery_list(session_found_t *out, int max, unsigned *gen);

/* ---------- registration ---------- */

typedef enum {
    REG_EV_PROGRESS,    /* progress 0..100 of the running operation */
    REG_EV_PSN_LINKED,  /* code exchanged: psn is valid; store it, then session_psn_fetch_devices() */
    REG_EV_PSN_DEVICES, /* devices[]/device_count: the consoles attached to the PSN account */
    REG_EV_SUCCESS,     /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS: host is valid */
    REG_EV_FAILED,      /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED (or the token exchange): see reason */
    REG_EV_CANCELED,    /* CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED, after session_reg_stop() */
} session_reg_event_type_t;

#define SESSION_PSN_DEVICES_MAX 8

/* a console attached to the linked PSN account (chiaki_holepunch_list_devices) */
typedef struct {
    char name[32];
    uint8_t uid[32]; /* PSN device uid */
    bool ps5;
} session_psn_device_t;

typedef struct {
    session_reg_event_type_t type;
    unsigned op;      /* operation generation: the real backend drops stale results */
    bool from_regist; /* from a chiaki_regist operation (its fini path), set by the real backend */
    int progress;
    config_host_t host;                                    /* valid on REG_EV_SUCCESS */
    config_psn_t psn;                                      /* valid on REG_EV_PSN_LINKED */
    session_psn_device_t devices[SESSION_PSN_DEVICES_MAX]; /* valid on REG_EV_PSN_DEVICES */
    int device_count;
    char reason[128]; /* valid on REG_EV_FAILED */
} session_reg_event_t;

bool session_reg_available(void); /* true when the real libchiaki backend is linked */
bool session_reg_begin_pin(const char *addr, const char *pin, bool ps5);
bool session_reg_begin_psn(const uint8_t account_id[8], bool ps5);
bool session_reg_submit_code(const char *code); /* PSN redirect `code=` value -> tokens + account id */
bool session_psn_fetch_devices(void);           /* list the consoles attached to the linked account */
void session_reg_stop(void);
bool session_reg_running(void);
bool session_reg_poll(session_reg_event_t *out); /* main thread only: next queued event, if any */
const char *session_reg_psn_url(void);           /* Sony authorize URL with duid = config_device_uid() */

#endif
