/* app.c - application state and glue between UI, config and session (see app.h). */
#include "app.h"

#include <SDL.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include "chiaki/session.h"
#include "config/config.h"
#include "log.h"

#define DEMO_CONNECT_MS  3000   /* simulated handshake length while libchiaki is missing */
#define SETTINGS_SAVE_MS 800    /* quiet time after the last settings edit before it is written */
#define PSN_CODE_SHARE   40     /* PSN path: the code exchange is the first 40% of the progress */

static app_listener_t g_listener;
static bool g_running = true;
static app_host_t g_hosts[CONFIG_MAX_HOSTS];
static int g_host_count;
static int g_selected = -1;
static conn_state_t g_conn = CONN_IDLE;
static int g_progress;
static Uint32 g_connect_at;
static Uint32 g_save_at;   /* 0 = no settings edit pending */

static struct {
    app_reg_state_t state;
    app_reg_method_t method;
    bool ps5;
    bool linking;    /* PSN path: still exchanging the code for tokens */
    bool via_code;   /* PSN path: started from a code, so the progress is split */
    int progress;
    char status[128], error[128];
} g_reg;

static void notify(app_change_t what) {
    if (g_listener) g_listener(what);
}

/* the UI view of config's consoles; called after every change to config's host list */
static void hosts_sync(void) {
    g_host_count = 0;
    for (int i = 0; i < config_host_count() && i < CONFIG_MAX_HOSTS; i++) {
        const config_host_t *c = config_host(i);
        app_host_t *h = &g_hosts[g_host_count++];
        h->name = c->nickname[0] ? c->nickname : c->addr[0] ? c->addr : "Console";
        h->model = c->is_ps5 ? "PS5" : "PS4";
        h->addr = c->addr[0] ? c->addr : "via PSN";
        /* TODO(libchiaki): discovery (chiaki_discovery) reports the state, firmware and current address */
        h->firmware = "-";
        h->max_height = c->is_ps5 ? 1080 : 720;   /* what the console kind streams (a PS4 Pro does 1080p) */
        h->max_fps = 60;
        h->state = HOST_UNKNOWN;
    }
    if (g_selected >= g_host_count) g_selected = g_host_count - 1;
    if (g_selected < 0 && g_host_count > 0) g_selected = 0;
}

static void save_pending(void) {
    if (!g_save_at) return;
    g_save_at = 0;
    config_save();
}

void app_init(app_listener_t listener) {
    g_listener = listener;
    hosts_sync();
}
bool app_running(void) { return g_running; }
void app_quit(void) {
    save_pending();
    g_running = false;
}

void app_settings_changed(void) {
    g_save_at = SDL_GetTicks() + SETTINGS_SAVE_MS;
    if (!g_save_at) g_save_at = 1;
}

int app_host_count(void) { return g_host_count; }
const app_host_t *app_host(int index) { return index >= 0 && index < g_host_count ? &g_hosts[index] : NULL; }
int app_selected(void) { return g_selected; }

void app_select(int index) {
    const app_host_t *h = app_host(index);
    if (!h) return;
    g_selected = index;
    log_event(LOG_INFO, "HOST", "Selected %s (%s)", h->name, h->addr);
    notify(APP_CHANGED_HOST);
}

conn_state_t app_conn_state(void) { return g_conn; }
int app_conn_progress(void) { return g_progress; }

static void set_conn(conn_state_t s) {
    g_conn = s;
    if (s == CONN_IDLE) g_progress = 0;
    if (s == CONN_STREAMING) {
        const stream_settings_t *st = &config_get()->stream;
        const app_host_t *h = app_host(g_selected);
        log_event(LOG_OK, "STREAM", "Session started with %s at %dx%d", h ? h->name : "the console", st->width,
                  st->height);
    }
    notify(APP_CHANGED_CONN);
}

void app_connect(void) {
    if (g_conn != CONN_IDLE) return;
    const app_host_t *h = app_host(g_selected);
    if (!h) {
        log_event(LOG_WARN, "HOST", "No console yet: pair one with Add host");
        return;
    }
    log_event(LOG_INFO, "NET", "Connecting to %s (%s)", h->name, h->addr);
    /* TODO(session): session_start(config_host(g_selected), &config_get()->stream) once libchiaki is linked */
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

void app_wake(void) {
    const app_host_t *h = app_host(g_selected);
    if (!h) return;
    log_event(LOG_INFO, "HOST", "Wake-up packet sent to %s", h->name);   /* DEMO: session_wakeup(config_host()) */
}

/* TODO(ui): ask for confirmation first; forgetting drops the keys and needs a new registration */
void app_forget(void) {
    const app_host_t *h = app_host(g_selected);
    if (!h) return;
    if (g_conn != CONN_IDLE) {
        log_event(LOG_WARN, "HOST", "Disconnect before forgetting %s", h->name);
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "%s", h->name);
    if (!config_host_remove(g_selected)) return;
    config_save();
    hosts_sync();
    log_event(LOG_WARN, "HOST", "Forgot %s", name);
    notify(APP_CHANGED_HOSTS);
    notify(APP_CHANGED_HOST);
}

/* ---------- registration ---------- */

app_reg_state_t app_reg_state(void) { return g_reg.state; }
app_reg_method_t app_reg_method(void) { return g_reg.method; }
int app_reg_progress(void) { return g_reg.progress; }
const char *app_reg_status(void) { return g_reg.status; }
const char *app_reg_error(void) { return g_reg.error; }
const char *app_reg_psn_url(void) { return session_reg_psn_url(); }

static bool reg_begin(app_reg_method_t method, bool ps5) {
    if (g_reg.state == APP_REG_RUNNING) {
        log_msg("reg: a registration is already running");
        return false;
    }
    g_reg.method = method;
    g_reg.ps5 = ps5;
    g_reg.linking = g_reg.via_code = false;
    g_reg.progress = 0;
    g_reg.error[0] = '\0';
    return true;
}

static void reg_running(const char *status) {
    g_reg.state = APP_REG_RUNNING;
    snprintf(g_reg.status, sizeof(g_reg.status), "%s", status);
    notify(APP_CHANGED_REG);
}

static void reg_fail(const char *reason) {
    g_reg.state = APP_REG_FAILED;
    g_reg.progress = 0;
    snprintf(g_reg.status, sizeof(g_reg.status), "Registration failed");
    snprintf(g_reg.error, sizeof(g_reg.error), "%s", reason);
    log_event(LOG_ERR, "REG", "%s", reason);
    notify(APP_CHANGED_REG);
}

static void reg_success(const config_host_t *host) {
    int i = config_host_find(host);
    bool ok = i >= 0 ? config_host_update(i, host) : config_host_add(host);
    if (!ok) {
        reg_fail(config_host_count() >= CONFIG_MAX_HOSTS ? "Too many consoles: forget one first"
                                                         : "Could not store the console");
        return;
    }
    if (i < 0) i = config_host_count() - 1;
    config_save();
    hosts_sync();
    g_selected = i;
    const app_host_t *h = app_host(i);
    g_reg.state = APP_REG_SUCCESS;
    g_reg.progress = 100;
    snprintf(g_reg.status, sizeof(g_reg.status), "Paired with %s", h->name);
    log_event(LOG_OK, "REG", "Registered %s (%s)", h->name, h->addr);
    notify(APP_CHANGED_HOSTS);
    notify(APP_CHANGED_HOST);
    notify(APP_CHANGED_REG);
}

static void reg_event(const session_reg_event_t *ev) {
    if (g_reg.state != APP_REG_RUNNING) return;   /* canceled: late events are dropped */
    switch (ev->type) {
        case REG_EV_PROGRESS: {
            int p = ev->progress;
            if (g_reg.linking) p = p * PSN_CODE_SHARE / 100;
            else if (g_reg.via_code) p = PSN_CODE_SHARE + p * (100 - PSN_CODE_SHARE) / 100;
            if (p != g_reg.progress) {
                g_reg.progress = p;
                notify(APP_CHANGED_REG);
            }
            break;
        }
        case REG_EV_PSN_LINKED:
            config_psn_set(ev->psn.account_id, ev->psn.online_id, ev->psn.access_token, ev->psn.refresh_token);
            config_save();
            log_event(LOG_OK, "REG", "PSN account %s linked", ev->psn.online_id);
            g_reg.linking = false;
            g_reg.progress = PSN_CODE_SHARE;
            if (!session_reg_begin_psn(config_psn()->account_id, g_reg.ps5)) {
                reg_fail("Could not start the registration");
                break;
            }
            reg_running("Registering over PSN");
            break;
        case REG_EV_SUCCESS: reg_success(&ev->host); break;
        case REG_EV_FAILED: reg_fail(ev->reason[0] ? ev->reason : "The console did not accept the registration"); break;
        case REG_EV_CANCELED: reg_fail("The registration was canceled"); break;
    }
}

static bool pin_valid(const char *pin) { return pin && strlen(pin) == 8 && strspn(pin, "0123456789") == 8; }

/* trimmed copy; returns the length */
static size_t trim_copy(char *out, size_t cap, const char *in) {
    if (!in) in = "";
    while (isspace((unsigned char)*in)) in++;
    size_t n = strlen(in);
    while (n && isspace((unsigned char)in[n - 1])) n--;
    if (n >= cap) n = cap - 1;
    memcpy(out, in, n);
    out[n] = '\0';
    return n;
}

void app_reg_submit_pin(const char *addr, const char *pin, bool ps5) {
    if (!reg_begin(APP_REG_METHOD_PIN, ps5)) return;
    char a[64];
    if (!trim_copy(a, sizeof(a), addr)) { reg_fail("Enter the console's IP address"); return; }
    if (!pin_valid(pin)) { reg_fail("The PIN has 8 digits, as shown on the console"); return; }
    /* TODO(libchiaki): a PS5 (and a PS4 on 7.0+) also needs the PSN account id (config_psn()); without a
     * linked account, ask for it (chiaki-ng lets the user type it, base64) before session_reg_begin_pin() */
    if (!session_reg_begin_pin(a, pin, ps5)) { reg_fail("Could not start the registration"); return; }
    log_event(LOG_INFO, "REG", "Registering with %s (PIN)", a);
    reg_running("Registering with the console");
}

void app_reg_submit_psn(const uint8_t account_id[8], bool ps5) {
    if (!reg_begin(APP_REG_METHOD_PSN, ps5)) return;
    if (!account_id) { reg_fail("Link a PSN account first"); return; }
    if (!session_reg_begin_psn(account_id, ps5)) { reg_fail("Could not start the registration"); return; }
    log_event(LOG_INFO, "REG", "Registering over PSN");
    reg_running("Registering over PSN");
}

/* accepts the bare code or the whole redirect URL (its code= parameter) */
void app_reg_submit_psn_code(const char *code, bool ps5) {
    if (!reg_begin(APP_REG_METHOD_PSN, ps5)) return;
    char in[SESSION_PSN_CODE_MAX + 1], c[SESSION_PSN_CODE_MAX + 1];
    trim_copy(in, sizeof(in), code);
    const char *p = strstr(in, "code=");
    p = p ? p + 5 : in;
    size_t n = strcspn(p, "&#");
    snprintf(c, sizeof(c), "%.*s", (int)n, p);
    if (!c[0]) { reg_fail("Enter the code from the sign-in page"); return; }
    if (!session_reg_submit_code(c)) { reg_fail("Could not start the sign-in"); return; }
    g_reg.linking = g_reg.via_code = true;
    log_event(LOG_INFO, "REG", "Linking the PSN account");
    reg_running("Signing in to PSN");
}

void app_reg_cancel(void) {
    if (g_reg.state == APP_REG_RUNNING) {
        session_reg_stop();
        log_event(LOG_WARN, "REG", "Registration canceled");
    }
    if (g_reg.state == APP_REG_IDLE) return;
    g_reg.state = APP_REG_IDLE;
    g_reg.linking = g_reg.via_code = false;
    g_reg.progress = 0;
    g_reg.status[0] = g_reg.error[0] = '\0';
    notify(APP_CHANGED_REG);
}

/* ---------- main loop ---------- */

void app_tick(void) {
    session_event_t ev;
    while (session_poll(&ev)) {
        /* TODO(session): map SESSION_EV_* to set_conn()/progress, LOGIN_PIN prompt, RUMBLE -> pad */
    }
    session_reg_event_t rev;
    while (session_reg_poll(&rev)) reg_event(&rev);
    if (g_save_at && (Sint32)(SDL_GetTicks() - g_save_at) >= 0) save_pending();

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
