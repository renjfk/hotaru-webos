/* chiaki/session.h - a PS Remote Play session, wrapping chiaki-ng's libchiaki.
 *
 * STUB: libchiaki is not linked yet. session_available() returns false and
 * app.c simulates a connection instead. Implement this file against
 * chiaki-ng/lib (chiaki_session_*, chiaki_regist_*, chiaki_discovery_wakeup);
 * scripts/chiaki-lg-build-webos.sh is the proven cross-build recipe.
 *
 * Threading: libchiaki calls back on its own threads. Queue events here and
 * hand them to the main loop through session_poll(); never touch LVGL or the
 * app state from a libchiaki thread.
 * Media: the still-encoded H.264/H.265 NAL units and Opus frames go straight
 * to media/ndl.h (hardware decode), never through LVGL.
 */
#ifndef CHIAKI_SESSION_H
#define CHIAKI_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "../config/config.h"

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

void session_set_pad(const session_pad_t *pad);
void session_set_login_pin(const char *pin);

/* wake a console in standby (chiaki_discovery_wakeup, UDP broadcast works on this TV) */
bool session_wakeup(const config_host_t *host);
/* pair with a console (chiaki_regist_start); fills out the keys to store via config_host_add() */
bool session_register(const char *addr, const char *pin, const uint8_t psn_account_id[8], bool ps5,
                      config_host_t *out);

#endif
