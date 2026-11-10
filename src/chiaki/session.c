/* chiaki/session.c - STUB until libchiaki is linked (see session.h). */
#include "session.h"

bool session_available(void) { return false; }

bool session_start(const config_host_t *host, const stream_settings_t *stream) {
    (void)host;
    (void)stream;
    return false;
}
void session_stop(void) {}
bool session_poll(session_event_t *out) { (void)out; return false; }

void session_set_pad(const session_pad_t *pad) { (void)pad; }
void session_set_login_pin(const char *pin) { (void)pin; }

bool session_wakeup(const config_host_t *host) { (void)host; return false; }
bool session_register(const char *addr, const char *pin, const uint8_t psn_account_id[8], bool ps5,
                      config_host_t *out) {
    (void)addr;
    (void)pin;
    (void)psn_account_id;
    (void)ps5;
    (void)out;
    return false;
}
