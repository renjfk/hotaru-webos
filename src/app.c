/* app.c - application state and glue between UI, config and session (see app.h). */
#include "app.h"

#include <SDL.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include "chiaki/session.h"
#include "config/config.h"
#include "log.h"

#define DEMO_CONNECT_MS 3000   /* simulated handshake length while libchiaki is missing */

/* DEMO: replaced by config_host() + discovery once registration works */
static const app_host_t g_hosts[] = {
    { "Living-room PS5", "PS5", "10.10.10.40", "24.06-09.00", 1080, 60, HOST_READY },
    { "Bedroom PS5", "PS5", "10.10.10.41", "24.05-08.60", 1080, 60, HOST_STANDBY },
    { "Study PS4 Pro", "PS4", "10.10.10.52", "11.50", 1080, 30, HOST_OFFLINE },
};
#define HOST_COUNT ((int)(sizeof(g_hosts) / sizeof(g_hosts[0])))

static app_listener_t g_listener;
static bool g_running = true;
static int g_selected;
static conn_state_t g_conn = CONN_IDLE;
static int g_progress;
static Uint32 g_connect_at;

static void notify(app_change_t what) {
    if (g_listener) g_listener(what);
}

void app_init(app_listener_t listener) { g_listener = listener; }
bool app_running(void) { return g_running; }
void app_quit(void) { g_running = false; }

int app_host_count(void) { return HOST_COUNT; }
const app_host_t *app_host(int index) { return index >= 0 && index < HOST_COUNT ? &g_hosts[index] : NULL; }
int app_selected(void) { return g_selected; }

void app_select(int index) {
    if (!app_host(index)) return;
    g_selected = index;
    log_event(LOG_INFO, "HOST", "Selected %s (%s)", g_hosts[index].name, g_hosts[index].addr);
    notify(APP_CHANGED_HOST);
}

conn_state_t app_conn_state(void) { return g_conn; }
int app_conn_progress(void) { return g_progress; }

static void set_conn(conn_state_t s) {
    g_conn = s;
    if (s == CONN_IDLE) g_progress = 0;
    if (s == CONN_STREAMING) {
        const stream_settings_t *st = &config_get()->stream;
        log_event(LOG_OK, "STREAM", "Session started with %s at %dx%d", g_hosts[g_selected].name, st->width,
                  st->height);
    }
    notify(APP_CHANGED_CONN);
}

void app_connect(void) {
    if (g_conn != CONN_IDLE) return;
    const app_host_t *h = &g_hosts[g_selected];
    log_event(LOG_INFO, "NET", "Connecting to %s (%s)", h->name, h->addr);
    /* TODO(session): session_start(config_host(...), &config_get()->stream) once registration fills config */
    g_progress = 0;
    g_connect_at = SDL_GetTicks();
    set_conn(CONN_CONNECTING);
}

void app_disconnect(void) {
    if (g_conn == CONN_IDLE) return;
    if (session_available()) session_stop();
    set_conn(CONN_IDLE);
    log_event(LOG_WARN, "NET", "Session closed");
}

void app_tick(void) {
    session_event_t ev;
    while (session_poll(&ev)) {
        /* TODO(session): map SESSION_EV_* to set_conn()/progress, LOGIN_PIN prompt, RUMBLE -> pad */
    }
    if (g_conn != CONN_CONNECTING || session_available()) return;
    /* DEMO: simulated connection progress */
    int p = (int)((SDL_GetTicks() - g_connect_at) * 100 / DEMO_CONNECT_MS);
    if (p >= 100) {
        g_progress = 100;
        set_conn(CONN_STREAMING);
    } else if (p != g_progress) {
        g_progress = p;
        notify(APP_CHANGED_PROGRESS);
    }
}

void app_wake(void) {
    log_event(LOG_INFO, "HOST", "Wake-up packet sent to %s", g_hosts[g_selected].name);   /* DEMO: session_wakeup() */
}

void app_forget(void) {
    log_event(LOG_WARN, "HOST", "Forget %s (dummy)", g_hosts[g_selected].name);   /* DEMO: config_host_remove() */
}

void app_register(const char *addr, const char *pin) {
    (void)addr;
    (void)pin;
    log_event(LOG_INFO, "REG", "Registration request sent (dummy)");   /* DEMO: session_register() */
}

/* first non-loopback IPv4 interface (the TV: wlan0) */
bool app_local_ip(char *ifname, size_t ifname_len, char *ip, size_t ip_len) {
    struct ifaddrs *list, *it;
    bool found = false;
    if (getifaddrs(&list) != 0) return false;
    for (it = list; it && !found; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || (it->ifa_flags & IFF_LOOPBACK)) continue;
        const struct sockaddr_in *sin = (const struct sockaddr_in *)it->ifa_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, (socklen_t)ip_len)) continue;
        snprintf(ifname, ifname_len, "%s", it->ifa_name);
        found = true;
    }
    freeifaddrs(list);
    return found;
}
