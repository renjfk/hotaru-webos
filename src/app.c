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
#include <strings.h>

#include "chiaki/session.h"
#include "config/config.h"
#include "input/input.h"
#include "log.h"
#include "net/update.h"

#define SETTINGS_SAVE_MS 800  /* quiet time after the last settings edit before it is written */
#define PSN_CODE_SHARE   40   /* PSN path: the code exchange is the first 40% of the progress */
#define RESTART_PAUSE_MS 2000 /* stream restart: let the console release the old session first */
#define RESTART_RETRY_MS 2000 /* then retry this often while it still reports it busy */
#define RESTART_TRIES    10
#define DISC_POLL_MS     250   /* how often discovery's list is read */
#define DISC_MISSING_MS  2000  /* a paired console missing from it this long is offline (4 searches) */
#define WAKE_TIMEOUT_MS  25000 /* how long a console in rest mode gets to answer ready, and how often */
#define WAKE_RESEND_MS   1000  /* the one-packet wake-up is resent (chiaki-ng gui/src/qmlbackend.cpp) */
#define WAKE_PROGRESS    15    /* the share of the progress bar waking takes */

/* app forwards input_pad_t to session_pad_t field by field and copies the button
 * bitmask; the two canonical layouts must stay identical */
_Static_assert(PAD_BTN_CROSS == SESSION_PAD_BTN_CROSS && PAD_BTN_PS == SESSION_PAD_BTN_PS &&
                   PAD_BTN_TOUCHPAD == SESSION_PAD_BTN_TOUCHPAD && PAD_BTN_OPTIONS == SESSION_PAD_BTN_OPTIONS,
               "input and session pad button bits must match");

static app_listener_t g_listener;
static bool g_running = true;
static app_host_t g_hosts[CONFIG_MAX_HOSTS];
static struct { int lan, psn; } g_routes[CONFIG_MAX_HOSTS]; /* each host's config entries, -1 = none */
static int g_host_count;
static int g_selected = -1;
static conn_state_t g_conn = CONN_IDLE;
static int g_progress;
static bool g_login_pin;                /* the console asked for the login PIN; the UI shows a prompt */
static Uint32 g_save_at;                /* 0 = no settings edit pending */
static Uint32 g_pad_next;               /* throttle for forwarding the pad to a running session */
static bool g_pad_fwd;                  /* the pad was being forwarded: send it released when that stops */
static stream_settings_t g_stream_used; /* the video settings the running session started with */
static int g_conn_host = -1;            /* the console it runs with */
static config_host_t g_conn_cfg;        /* ... and the entry it was started from (route applied) */
static bool g_conn_fallback;            /* that entry is the PSN fallback after the local network failed */
static char g_conn_error[128];          /* why the last session failed or ended on its own */
static Uint32 g_restart_at;             /* a stream restart is pending: reconnect then (0 = none) */
static int g_restart_tries;             /* attempts left in a stream restart (0 = not restarting) */
static struct {
    bool on;
    Uint32 start, until, resend;
} g_wake; /* connecting: waking the console first */

/* discovery: the list as last read, and what each paired console made of it */
static struct {
    bool on, heard; /* running; heard at least one console since it started (broadcasts work here) */
    Uint32 next;    /* next read */
    session_found_t list[SESSION_FOUND_MAX];
    int n;
    app_found_t unpaired[SESSION_FOUND_MAX];
    int unpaired_n;
} g_disc;
/* what discovery made of each config entry's console. The slot remembers which console it was, so a
 * forgotten entry's slot starts afresh; `since`: missing from the list since then (0 = it answers) */
static struct {
    uint8_t mac[6];
    char addr[64];
    Uint32 since;
    host_state_t state;
    char fw[16], app[64];
} g_entry[CONFIG_MAX_HOSTS];

static struct {
    app_reg_state_t state;
    app_reg_method_t method;
    bool ps5;
    bool linking;  /* PSN path: still exchanging the code for tokens */
    bool via_code; /* PSN path: started from a code, so the progress is split */
    int progress;
    char status[128], error[128];
} g_reg;

static void notify(app_change_t what) {
    if (g_listener) g_listener(what);
}

/* ---------- the console list: config entries grouped into consoles ---------- */

static bool uid_set(const uint8_t *uid) {
    for (int i = 0; i < 32; i++)
        if (uid[i]) return true;
    return false;
}
static bool has_lan(const config_host_t *c) { return c->addr[0] != '\0'; }
static bool has_psn(const config_host_t *c) { return c->via_psn && (uid_set(c->psn_uid) || !c->addr[0]); }
static bool psn_only(const config_host_t *c) { return has_psn(c) && !has_lan(c); }

/* the PSN device list and a PIN pairing name the console alike (its nickname) */
static bool same_name(const config_host_t *a, const config_host_t *b) {
    return a->nickname[0] && a->is_ps5 == b->is_ps5 && !strcasecmp(a->nickname, b->nickname);
}

/* the route choice lives on the PSN entry (config prefer_psn) and is saved at once */
static void remember_route(int psn_entry, host_route_t route) {
    const config_host_t *c = config_host(psn_entry);
    if (!c || c->prefer_psn == (route == ROUTE_PSN)) return;
    config_host_t upd = *c;
    upd.prefer_psn = route == ROUTE_PSN;
    if (config_host_update(psn_entry, &upd)) config_save();
}

/* the host a config entry belongs to, -1 if none */
static int host_of_entry(int entry) {
    for (int i = 0; i < g_host_count; i++)
        if (g_routes[i].lan == entry || g_routes[i].psn == entry) return i;
    return -1;
}

static void host_states(void);

/* The UI view of config's consoles; called after every change to config's host list. A PSN-only
 * entry joins the first LAN entry with its name and kind that has no PSN route yet; the console
 * keeps the LAN entry's place in the list. */
static void hosts_sync(void) {
    int n = config_host_count() < CONFIG_MAX_HOSTS ? config_host_count() : CONFIG_MAX_HOSTS;
    int joins[CONFIG_MAX_HOSTS]; /* PSN-only entry -> the LAN entry it joins */
    bool taken[CONFIG_MAX_HOSTS] = { false };
    for (int i = 0; i < n; i++) {
        joins[i] = -1;
        const config_host_t *c = config_host(i);
        if (!psn_only(c)) continue;
        for (int j = 0; j < n && joins[i] < 0; j++) {
            const config_host_t *d = config_host(j);
            if (taken[j] || !has_lan(d) || has_psn(d) || !same_name(c, d)) continue;
            joins[i] = j;
            taken[j] = true;
        }
    }
    g_host_count = 0;
    for (int i = 0; i < n; i++) {
        const config_host_t *c = config_host(i);
        if (joins[i] >= 0) continue;              /* shown with the LAN entry it joined */
        if (!has_lan(c) && !has_psn(c)) continue; /* no way to reach it (a hand-edited file) */
        int k = g_host_count++;
        g_routes[k].lan = has_lan(c) ? i : -1;
        g_routes[k].psn = has_psn(c) ? i : -1;
        for (int j = 0; j < n && g_routes[k].psn < 0; j++)
            if (joins[j] == i) g_routes[k].psn = j;
        const config_host_t *lan = has_lan(c) ? c : NULL; /* c names the console: its LAN entry, else PSN */
        const config_host_t *psn = g_routes[k].psn >= 0 ? config_host(g_routes[k].psn) : NULL;
        app_host_t *h = &g_hosts[k];
        h->name = c->nickname[0] ? c->nickname : c->addr[0] ? c->addr : "Console";
        h->ps5 = c->is_ps5;
        h->model = h->ps5 ? "PS5" : "PS4";
        h->addr = lan ? lan->addr : "via PSN";
        h->firmware = lan ? g_entry[g_routes[k].lan].fw : "";
        h->running = lan ? g_entry[g_routes[k].lan].app : "";
        h->max_height = h->ps5 ? 1080 : 720; /* what the console kind streams (a PS4 Pro does 1080p) */
        h->max_fps = 60;
        h->state = lan ? g_entry[g_routes[k].lan].state : HOST_UNKNOWN;
        h->lan = lan != NULL;
        h->psn = psn != NULL;
        h->route = !lan || (psn && psn->prefer_psn) ? ROUTE_PSN : ROUTE_LAN; /* the PIN pairing first */
    }
    if (g_selected >= g_host_count) g_selected = g_host_count - 1;
    if (g_selected < 0 && g_host_count > 0) g_selected = 0;
    host_states(); /* what discovery knows carries over */
}

/* ---------- discovery: state, firmware and address of the paired consoles ---------- */

static bool mac_set(const uint8_t *mac) { return mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]; }

static bool mac_is(const uint8_t *mac, const char *hex) {
    char m[16];
    snprintf(m, sizeof(m), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return mac_set(mac) && !strcasecmp(m, hex);
}

/* the console behind a config entry in the list: its MAC, else its address (an entry paired
 * before the MAC was known); -1 if it does not answer */
static int found_index(const config_host_t *c, bool *by_mac) {
    for (int i = 0; i < g_disc.n; i++)
        if (mac_is(c->server_mac, g_disc.list[i].host_id)) {
            *by_mac = true;
            return i;
        }
    *by_mac = false;
    if (!c->addr[0]) return -1;
    for (int i = 0; i < g_disc.n; i++)
        if (!strcmp(c->addr, g_disc.list[i].addr) && g_disc.list[i].ps5 == c->is_ps5) return i;
    return -1;
}

/* "09400000" -> "9.40"; anything else as it is, cut to 15 characters */
static void firmware_text(char *out, size_t cap, const char *v) {
    if (strlen(v) >= 4 && isdigit((unsigned char)v[0]) && isdigit((unsigned char)v[1]) &&
        isdigit((unsigned char)v[2]) && isdigit((unsigned char)v[3]))
        snprintf(out, cap, "%d.%c%c", (v[0] - '0') * 10 + (v[1] - '0'), v[2], v[3]);
    else snprintf(out, cap, "%.15s", v);
}

/* each host's state from the list; the consoles nobody claims are the unpaired ones. True when
 * something the UI shows changed. A console that stops answering keeps what it was until it has
 * been missing DISC_MISSING_MS, then it is offline. */
static bool host_states_changed(void) {
    bool changed = false, used[SESSION_FOUND_MAX] = { false };
    Uint32 now = SDL_GetTicks();
    for (int k = 0; k < g_host_count; k++) {
        app_host_t *h = &g_hosts[k];
        int e = g_routes[k].lan;
        if (e < 0) continue; /* over PSN only: discovery cannot hear it */
        const config_host_t *c = config_host(e);
        if (memcmp(g_entry[e].mac, c->server_mac, 6) != 0 || strcmp(g_entry[e].addr, c->addr) != 0) {
            memset(&g_entry[e], 0, sizeof(g_entry[e])); /* another console in this slot: afresh */
            memcpy(g_entry[e].mac, c->server_mac, 6);
            snprintf(g_entry[e].addr, sizeof(g_entry[e].addr), "%s", c->addr);
            g_entry[e].state = HOST_UNKNOWN;
            changed = true;
        }
        if (g_disc.on) { /* paused while streaming: what was known stays */
            bool by_mac;
            int m = found_index(c, &by_mac);
            host_state_t st = g_entry[e].state;
            char fw[16], app[64] = "";
            snprintf(fw, sizeof(fw), "%s", g_entry[e].fw); /* the last known firmware stays */
            if (m >= 0) {
                used[m] = true;
                g_entry[e].since = 0;
                st = g_disc.list[m].ready ? HOST_READY : HOST_STANDBY;
                firmware_text(fw, sizeof(fw), g_disc.list[m].system_version);
                snprintf(app, sizeof(app), "%s", g_disc.list[m].app_name);
            } else {
                if (!g_entry[e].since) g_entry[e].since = now ? now : 1;
                if (now - g_entry[e].since >= DISC_MISSING_MS) st = HOST_OFFLINE;
            }
            if (st != g_entry[e].state)
                log_msg("discovery: %s is %s", h->name,
                        st == HOST_READY     ? "ready"
                        : st == HOST_STANDBY ? "in rest mode"
                        : st == HOST_OFFLINE ? "offline"
                                             : "unknown");
            if (st != g_entry[e].state || strcmp(fw, g_entry[e].fw) != 0 || strcmp(app, g_entry[e].app) != 0)
                changed = true;
            g_entry[e].state = st;
            snprintf(g_entry[e].fw, sizeof(g_entry[e].fw), "%s", fw);
            snprintf(g_entry[e].app, sizeof(g_entry[e].app), "%s", app);
        }
        h->state = g_entry[e].state;
        h->firmware = g_entry[e].fw;
        h->running = g_entry[e].app;
    }
    g_disc.unpaired_n = 0;
    for (int i = 0; i < g_disc.n; i++) {
        if (used[i]) continue;
        bool known = false; /* an entry at that address that did not match (another kind) still owns it */
        for (int e = 0; e < config_host_count() && !known; e++)
            known = !strcmp(config_host(e)->addr, g_disc.list[i].addr);
        if (known) continue;
        app_found_t *f = &g_disc.unpaired[g_disc.unpaired_n++];
        f->name = g_disc.list[i].name[0] ? g_disc.list[i].name : g_disc.list[i].addr;
        f->addr = g_disc.list[i].addr;
        f->ps5 = g_disc.list[i].ps5;
        f->ready = g_disc.list[i].ready;
    }
    return changed;
}

static void host_states(void) { (void)host_states_changed(); }

/* a console found by its MAC at another address: the router gave it a new one */
static bool follow_moves(void) {
    bool moved = false;
    for (int e = 0; e < config_host_count(); e++) {
        const config_host_t *c = config_host(e);
        bool by_mac;
        int m = c->addr[0] ? found_index(c, &by_mac) : -1;
        if (m < 0 || !by_mac || !strcmp(c->addr, g_disc.list[m].addr)) continue;
        config_host_t upd = *c;
        snprintf(upd.addr, sizeof(upd.addr), "%s", g_disc.list[m].addr);
        log_event(LOG_INFO, "HOST", "%s moved to %s", c->nickname[0] ? c->nickname : c->addr, upd.addr);
        if (config_host_update(e, &upd)) moved = true;
    }
    return moved;
}

static void disc_set(bool on) {
    if (on == g_disc.on) return;
    if (on && !session_discovery_start()) return;
    if (!on) session_discovery_stop();
    g_disc.on = on;
    g_disc.heard = false;
    g_disc.n = g_disc.unpaired_n = 0;
    g_disc.next = 0;
    for (int e = 0; e < CONFIG_MAX_HOSTS; e++) g_entry[e].since = 0; /* missing counts from now */
}

static void disc_poll(void) {
    Uint32 now = SDL_GetTicks();
    if (!g_disc.on || (Sint32)(now - g_disc.next) < 0) return;
    g_disc.next = now + DISC_POLL_MS;
    g_disc.n = session_discovery_list(g_disc.list, SESSION_FOUND_MAX, NULL);
    if (g_disc.n) g_disc.heard = true;
    if (follow_moves()) {
        config_save();
        hosts_sync();
        notify(APP_CHANGED_HOSTS);
    }
    if (host_states_changed()) notify(APP_CHANGED_STATE);
}

int app_found_count(void) { return g_disc.unpaired_n; }
const app_found_t *app_found(int index) {
    return index >= 0 && index < g_disc.unpaired_n ? &g_disc.unpaired[index] : NULL;
}

/* the entry a host connects with over a route, as session_start() must see it: a PSN route on an
 * entry that also has an address loses the address, so the session goes over PSN. A route the
 * host lacks falls back to the one it has. */
static bool route_entry_for(int host, host_route_t route, config_host_t *out) {
    if (!app_host(host)) return false;
    int e = route == ROUTE_PSN ? g_routes[host].psn : g_routes[host].lan;
    if (e < 0) e = g_routes[host].lan >= 0 ? g_routes[host].lan : g_routes[host].psn;
    const config_host_t *c = config_host(e);
    if (!c) return false;
    *out = *c;
    if (e == g_routes[host].psn && (route == ROUTE_PSN || g_routes[host].lan < 0)) out->addr[0] = '\0';
    return true;
}

static bool route_entry(int host, config_host_t *out) {
    const app_host_t *h = app_host(host);
    return h && route_entry_for(host, h->route, out);
}

/* ---------- probe mode ---------- */

static bool g_probe_forced;

static void probe_network(void) {
    struct ifaddrs *list;
    if (getifaddrs(&list) != 0) return;
    for (struct ifaddrs *it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        char a[INET_ADDRSTRLEN] = "", m[INET_ADDRSTRLEN] = "", b[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &((const struct sockaddr_in *)it->ifa_addr)->sin_addr, a, sizeof(a));
        if (it->ifa_netmask) inet_ntop(AF_INET, &((const struct sockaddr_in *)it->ifa_netmask)->sin_addr, m, sizeof(m));
        if ((it->ifa_flags & IFF_BROADCAST) && it->ifa_broadaddr)
            inet_ntop(AF_INET, &((const struct sockaddr_in *)it->ifa_broadaddr)->sin_addr, b, sizeof(b));
        log_probe("net: %s %s/%s broadcast %s%s%s", it->ifa_name, a, m, b[0] ? b : "-",
                  (it->ifa_flags & IFF_UP) ? " up" : " down", (it->ifa_flags & IFF_LOOPBACK) ? " loopback" : "");
    }
    freeifaddrs(list);
}

static void probe_apply(void) {
    bool on = g_probe_forced || config_get()->debug.probe;
    if (on == log_probe_on()) return;
    log_set_probe(on);
    log_event(on ? LOG_WARN : LOG_INFO, "PROBE", "Diagnostics %s",
              on ? "on: raw input, discovery and timing are logged" : "off");
    if (on) probe_network();
}

void app_probe_force(void) {
    g_probe_forced = true;
    probe_apply();
}
bool app_probe_forced(void) { return g_probe_forced; }

static void save_pending(void) {
    if (!g_save_at) return;
    g_save_at = 0;
    config_save();
}

void app_init(app_listener_t listener) {
    g_listener = listener;
    hosts_sync();
    probe_apply();
    disc_set(true);
    update_start();
}
bool app_running(void) { return g_running; }
void app_quit(void) {
    save_pending();
    disc_set(false);
    session_stop(); /* release the session thread before the logger closes */
    session_reg_stop();
    g_running = false;
}

void app_settings_changed(void) {
    probe_apply();
    g_save_at = SDL_GetTicks() + SETTINGS_SAVE_MS;
    if (!g_save_at) g_save_at = 1;
}

const char *app_update_version(void) { return update_latest(); }

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
    if (s != CONN_CONNECTING) g_wake.on = false;
    disc_set(s != CONN_STREAMING); /* broadcasts every 500 ms would cost the stream Wi-Fi airtime */
    if (s == CONN_IDLE) g_progress = 0;
    if (s == CONN_STREAMING) {
        const stream_settings_t *st = &config_get()->stream;
        const app_host_t *h = app_host(g_selected);
        log_event(LOG_OK, "STREAM", "Session started with %s at %dx%d", h ? h->name : "the console", st->width,
                  st->height);
    }
    notify(APP_CHANGED_CONN);
}

void app_set_route(host_route_t route) {
    app_host_t *h = g_selected >= 0 && g_selected < g_host_count ? &g_hosts[g_selected] : NULL;
    if (!h || h->route == route || (route == ROUTE_LAN && !h->lan) || (route == ROUTE_PSN && !h->psn)) return;
    h->route = route;
    remember_route(g_routes[g_selected].psn, route);
    log_event(LOG_INFO, "HOST", "%s connects %s", h->name, route == ROUTE_PSN ? "over PSN" : "on the local network");
    notify(APP_CHANGED_HOST);
}

const char *app_conn_error(void) { return g_conn_error; }
bool app_conn_over_psn(void) { return g_conn != CONN_IDLE && !g_conn_cfg.addr[0]; }
bool app_conn_fallback(void) { return g_conn != CONN_IDLE && g_conn_fallback; }
bool app_conn_waking(void) { return g_conn == CONN_CONNECTING && g_wake.on; }

/* A local-network attempt that never reached the console: try the same console over PSN, once
 * per connect. Needs the console's PSN entry and a signed-in account (an expired access token is
 * refreshed by the backend). The saved route stays as it is: at home the PIN pairing wins again. */
static bool fall_back_to_psn(const char *why) {
    const app_host_t *h = app_host(g_conn_host);
    const config_psn_t *psn = config_psn();
    config_host_t ch;
    if (!h || !h->psn || !g_conn_cfg.addr[0] || g_conn_fallback || (!psn->access_token[0] && !psn->refresh_token[0]) ||
        !route_entry_for(g_conn_host, ROUTE_PSN, &ch))
        return false;
    log_event(LOG_WARN, "NET", "%s %s: trying over PSN", h->name, why);
    if (!session_start(&ch, &config_get()->stream)) return false;
    g_conn_cfg = ch;
    g_conn_fallback = true;
    g_progress = 0;
    notify(APP_CHANGED_PROGRESS);
    return true;
}

void app_connect(void) {
    if (g_conn != CONN_IDLE) return;
    const app_host_t *h = app_host(g_selected);
    config_host_t ch;
    if (!h || !route_entry(g_selected, &ch)) {
        log_event(LOG_WARN, "HOST", "No console yet: pair one with Add a console");
        return;
    }
    if (!ch.addr[0] && !ch.via_psn) {
        log_event(LOG_WARN, "NET", "%s has no address yet", h->name);
        return;
    }
    g_conn_error[0] = '\0';
    g_conn_host = g_selected;
    g_conn_cfg = ch;
    g_conn_fallback = false;
    g_stream_used = config_get()->stream;
    g_progress = 0;
    if (ch.addr[0] && h->state == HOST_STANDBY) { /* in rest mode: wake it, start once it answers ready */
        log_event(LOG_INFO, "NET", "Waking up %s (%s)", h->name, ch.addr);
        g_wake.start = SDL_GetTicks();
        g_wake.until = g_wake.start + WAKE_TIMEOUT_MS;
        g_wake.resend = 0;
        g_wake.on = true; /* before set_conn: the Connecting screen it brings up says waking */
        set_conn(CONN_CONNECTING);
        return;
    }
    /* not on this network while discovery hears others: over PSN at once, if it has that route */
    bool started = ch.addr[0] && h->state == HOST_OFFLINE && g_disc.heard && fall_back_to_psn("is not on this network");
    if (!started) {
        log_event(LOG_INFO, "NET", "Connecting to %s (%s)", h->name, ch.addr[0] ? ch.addr : "over PSN");
        started = session_start(&ch, &config_get()->stream) || fall_back_to_psn("could not start on the local network");
    }
    if (!started) {
        log_event(LOG_ERR, "NET", "Could not start the session with %s", h->name);
        snprintf(g_conn_error, sizeof(g_conn_error), "Could not start the session");
        notify(APP_CHANGED_CONN);
        return;
    }
    set_conn(CONN_CONNECTING);
}

/* app_tick while waking: start once discovery hears it ready; resend now and then; give up (or
 * go over PSN) after WAKE_TIMEOUT_MS */
static void wake_tick(void) {
    if (!g_wake.on) return;
    const app_host_t *h = app_host(g_conn_host);
    const config_host_t *lan = h ? config_host(g_routes[g_conn_host].lan) : NULL;
    Uint32 now = SDL_GetTicks();
    if (h && h->state == HOST_READY) {
        g_wake.on = false;
        log_event(LOG_OK, "NET", "%s is awake", h->name);
        if (session_start(&g_conn_cfg, &config_get()->stream)) {
            notify(APP_CHANGED_PROGRESS); /* waking -> connecting */
            return;
        }
        snprintf(g_conn_error, sizeof(g_conn_error), "Could not start the session");
        set_conn(CONN_IDLE);
    } else if ((Sint32)(now - g_wake.until) >= 0) {
        g_wake.on = false;
        if (fall_back_to_psn("did not wake up on the local network")) return;
        snprintf(g_conn_error, sizeof(g_conn_error), "%s did not wake up", h ? h->name : "The console");
        log_event(LOG_WARN, "NET", "%s", g_conn_error);
        set_conn(CONN_IDLE);
    } else {
        if (lan && (Sint32)(now - g_wake.resend) >= 0) {
            session_wakeup(lan);
            g_wake.resend = now + WAKE_RESEND_MS;
        }
        int p = (int)((now - g_wake.start) * WAKE_PROGRESS / WAKE_TIMEOUT_MS);
        if (p != g_progress) {
            g_progress = p;
            notify(APP_CHANGED_PROGRESS);
        }
    }
}

static void conn_close(void) {
    session_stop();
    input_rumble(0, 0); /* stop the motor if it was running */
    g_login_pin = false;
    set_conn(CONN_IDLE);
}

void app_disconnect(void) {
    g_restart_at = 0;
    g_restart_tries = 0;
    g_conn_error[0] = '\0';
    if (g_conn == CONN_IDLE) return;
    conn_close();
    log_event(LOG_WARN, "NET", "Session closed");
}

bool app_stream_settings_pending(void) {
    if (g_conn == CONN_IDLE) return false;
    const stream_settings_t *a = &g_stream_used, *b = &config_get()->stream;
    return a->codec != b->codec || a->width != b->width || a->height != b->height || a->fps != b->fps ||
           a->bitrate_kbps != b->bitrate_kbps || a->hdr != b->hdr;
}

static void restart_after(Uint32 ms) {
    g_restart_at = SDL_GetTicks() + ms;
    if (!g_restart_at) g_restart_at = 1;
}

/* The console keeps the old session for several seconds after it ends and refuses a new one
 * ("Remote Play is already in use"), so a restart stays Connecting and retries until it is
 * free, for up to RESTART_TRIES attempts. */
void app_restart_stream(void) {
    if (g_conn != CONN_STREAMING) return;
    const stream_settings_t *s = &config_get()->stream;
    log_event(LOG_INFO, "STREAM", "Restarting the stream for the new video settings (%dp%d, %d.%d Mbps)", s->height,
              s->fps, s->bitrate_kbps / 1000, (s->bitrate_kbps % 1000) / 100);
    session_stop();
    input_rumble(0, 0);
    g_login_pin = false;
    if (app_host(g_conn_host)) g_selected = g_conn_host;
    g_restart_tries = RESTART_TRIES;
    g_progress = 0;
    set_conn(CONN_CONNECTING);
    restart_after(RESTART_PAUSE_MS);
}

/* app_tick: the pause is over, start the next attempt (the state is already Connecting) */
static void restart_attempt(void) {
    g_restart_at = 0;
    if (g_conn != CONN_CONNECTING || !g_restart_tries) return;
    if (session_start(&g_conn_cfg, &config_get()->stream)) { /* same console, same route */
        g_stream_used = config_get()->stream;
        return;
    }
    g_restart_tries = 0;
    snprintf(g_conn_error, sizeof(g_conn_error), "Could not restart the session");
    set_conn(CONN_IDLE);
    log_event(LOG_ERR, "NET", "Could not restart the session");
}

bool app_login_pin_pending(void) { return g_login_pin; }

void app_login_pin_submit(const char *pin) {
    g_login_pin = false;
    if (pin && *pin) {
        session_set_login_pin(pin);
        log_event(LOG_INFO, "NET", "Login PIN submitted");
    } else {
        log_event(LOG_WARN, "NET", "Login PIN prompt cancelled");
    }
}

void app_wake(void) {
    const app_host_t *h = app_host(g_selected);
    if (!h) return;
    const config_host_t *ch = config_host(g_routes[g_selected].lan);
    if (ch && session_wakeup(ch)) log_event(LOG_INFO, "HOST", "Wake-up packet sent to %s", h->name);
    else log_event(LOG_WARN, "HOST", "Could not wake %s (it needs a PIN pairing on this network)", h->name);
}

/* Destructive: the caller (Home's "Forget this console") confirms in ui/dialog first. Forgetting drops the
 * pairing keys and needs a new registration. */
void app_forget(void) {
    const app_host_t *h = app_host(g_selected);
    if (!h) return;
    if (g_conn != CONN_IDLE) {
        log_event(LOG_WARN, "HOST", "Disconnect before forgetting %s", h->name);
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "%s", h->name);
    int a = g_routes[g_selected].lan, b = g_routes[g_selected].psn; /* the higher index first */
    if (a < b) {
        int t = a;
        a = b;
        b = t;
    }
    bool ok = a >= 0 && config_host_remove(a);
    if (b >= 0 && b != a) ok = config_host_remove(b) || ok;
    if (!ok) return;
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
    g_selected = host_of_entry(i);
    const app_host_t *h = app_host(g_selected);
    g_reg.state = APP_REG_SUCCESS;
    g_reg.progress = 100;
    snprintf(g_reg.status, sizeof(g_reg.status), "Paired with %s", h->name);
    log_event(LOG_OK, "REG", "Registered %s (%s)", h->name, h->addr);
    notify(APP_CHANGED_HOSTS);
    notify(APP_CHANGED_HOST);
    notify(APP_CHANGED_REG);
}

static void reg_devices(const session_reg_event_t *ev) {
    int added = 0;
    for (int i = 0; i < ev->device_count && i < SESSION_PSN_DEVICES_MAX; i++) {
        const session_psn_device_t *d = &ev->devices[i];
        config_host_t h;
        memset(&h, 0, sizeof(h));
        snprintf(h.nickname, sizeof(h.nickname), "%s", d->name);
        h.is_ps5 = d->ps5;
        h.via_psn = true;
        memcpy(h.psn_uid, d->uid, sizeof(h.psn_uid));
        int idx = config_host_find(&h);
        bool ok;
        if (idx >= 0) {
            config_host_t cur = *config_host(idx); /* keep keys/address already stored for this uid */
            snprintf(cur.nickname, sizeof(cur.nickname), "%s", d->name);
            cur.is_ps5 = d->ps5;
            cur.via_psn = true;
            memcpy(cur.psn_uid, d->uid, sizeof(cur.psn_uid));
            ok = config_host_update(idx, &cur);
        } else {
            ok = config_host_add(&h);
        }
        if (ok) added++;
    }
    if (added) {
        config_save();
        int sel_entry =
            g_selected >= 0 ? g_routes[g_selected].lan >= 0 ? g_routes[g_selected].lan : g_routes[g_selected].psn : -1;
        hosts_sync();
        if (sel_entry >= 0) g_selected = host_of_entry(sel_entry); /* the same console stays selected */
        if (g_selected < 0 && g_host_count) g_selected = 0;
    }
    g_reg.state = APP_REG_SUCCESS;
    g_reg.progress = 100;
    if (added) snprintf(g_reg.status, sizeof(g_reg.status), "Found %d console(s) on PSN", added);
    else snprintf(g_reg.status, sizeof(g_reg.status), "%s", "No consoles on this PSN account");
    log_event(LOG_OK, "REG", "PSN consoles attached: %d", added);
    if (added) {
        notify(APP_CHANGED_HOSTS);
        notify(APP_CHANGED_HOST);
    }
    notify(APP_CHANGED_REG);
}

static void reg_event(const session_reg_event_t *ev) {
    if (g_reg.state != APP_REG_RUNNING) return; /* canceled: late events are dropped */
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
            config_psn_set_expiry(ev->psn.expires_at);
            config_save();
            log_event(LOG_OK, "REG", "PSN account %s linked", ev->psn.online_id);
            g_reg.linking = false;
            g_reg.progress = PSN_CODE_SHARE;
            if (!session_psn_fetch_devices()) {
                reg_fail("Could not list the consoles on this PSN account");
                break;
            }
            reg_running("Looking for your consoles");
            break;
        case REG_EV_PSN_DEVICES: reg_devices(ev); break;
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

/* base64 decode of exactly 8 bytes (the PSN account id); accepts standard padding and whitespace */
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool b64_decode8(const char *s, uint8_t out[8]) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (; *s; s++) {
        if (*s == '=' || isspace((unsigned char)*s)) continue;
        int v = b64_val(*s);
        if (v < 0) return false;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < 8) out[n] = (uint8_t)((acc >> bits) & 0xff);
            n++;
        }
    }
    return n == 8;
}

bool app_psn_set_account_id(const char *b64) {
    uint8_t id[8];
    bool nonzero = false;
    if (!b64 || !b64_decode8(b64, id)) return false;
    for (int i = 0; i < 8; i++)
        if (id[i]) nonzero = true;
    if (!nonzero) return false;
    if (!config_psn_has_account_id() || memcmp(id, config_psn()->account_id, 8) != 0) {
        config_psn_set_account_id(id);
        config_save();
        log_event(LOG_OK, "REG", "PSN account ID saved");
    }
    return true;
}

void app_reg_submit_pin(const char *addr, const char *pin, bool ps5, const char *account_id_b64) {
    if (!reg_begin(APP_REG_METHOD_PIN, ps5)) return;
    /* an optional manually entered PSN account id: needed by a PS5 and a PS4 on 7.0+ */
    if (account_id_b64 && *account_id_b64 && !app_psn_set_account_id(account_id_b64)) {
        reg_fail("The PSN account ID must be base64 of 8 non-zero bytes");
        return;
    }
    char a[64];
    if (!trim_copy(a, sizeof(a), addr)) {
        reg_fail("Enter the console's IP address");
        return;
    }
    if (!pin_valid(pin)) {
        reg_fail("The PIN has 8 digits, as shown on the console");
        return;
    }
    if (session_available() && !config_psn_has_account_id()) {
        reg_fail("PIN pairing needs your PSN account ID: sign in to PSN or type the ID");
        return;
    }
    if (!session_reg_begin_pin(a, pin, ps5)) {
        reg_fail("Could not start the registration");
        return;
    }
    log_event(LOG_INFO, "REG", "Registering with %s (PIN)", a);
    reg_running("Registering with the console");
}

/* list the consoles attached to the linked PSN account and add them to the host list */
void app_psn_refresh(void) {
    if (!reg_begin(APP_REG_METHOD_PSN, true)) return;
    if (!config_psn_has_account_id()) {
        reg_fail("Link a PSN account first");
        return;
    }
    if (!config_psn()->access_token[0]) {
        reg_fail("Sign in to PSN to list your consoles");
        return;
    }
    if (!session_psn_fetch_devices()) {
        reg_fail("Could not list the PSN consoles");
        return;
    }
    log_event(LOG_INFO, "REG", "Looking for PSN consoles");
    reg_running("Looking for your consoles");
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
    if (!c[0]) {
        reg_fail("Enter the code from the sign-in page");
        return;
    }
    if (!session_reg_submit_code(c)) {
        reg_fail("Could not start the sign-in");
        return;
    }
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

/* one queued session event -> app state; the same mapping the real libchiaki backend feeds */
static void session_ev(const session_event_t *ev) {
    switch (ev->type) {
        case SESSION_EV_PROGRESS:
            if (g_conn == CONN_CONNECTING && ev->progress != g_progress) {
                g_progress = ev->progress;
                notify(APP_CHANGED_PROGRESS);
            }
            break;
        case SESSION_EV_CONNECTED:
            g_login_pin = false;
            g_progress = 100;
            g_restart_tries = 0;
            if (g_conn == CONN_CONNECTING) set_conn(CONN_STREAMING);
            break;
        case SESSION_EV_LOGIN_PIN:
            /* the console wants the on-screen Remote Play PIN: the UI prompts for it */
            g_login_pin = true;
            log_event(LOG_WARN, "NET", "The console asks for the login PIN");
            notify(APP_CHANGED_PIN);
            break;
        case SESSION_EV_RUMBLE:
            if (config_get()->input.rumble) input_rumble(ev->rumble_left, ev->rumble_right);
            break;
        case SESSION_EV_QUIT:
            if (g_conn == CONN_CONNECTING && g_restart_tries > 1) {
                /* a restart the console refused (usually: the old session is still in use) */
                session_stop();
                g_restart_tries--;
                log_event(LOG_INFO, "NET", "Console not ready yet%s%s, retrying (%d left)", ev->reason[0] ? ": " : "",
                          ev->reason, g_restart_tries);
                restart_after(RESTART_RETRY_MS);
                break;
            }
            g_restart_tries = 0;
            if (g_conn == CONN_CONNECTING && ev->unreachable) {
                session_stop();
                if (fall_back_to_psn("is not reachable on the local network")) break;
            }
            if (g_conn != CONN_IDLE) {
                session_stop(); /* the console ended it (or an error): release the backend */
                input_rumble(0, 0);
                g_login_pin = false;
                snprintf(g_conn_error, sizeof(g_conn_error), "%s",
                         ev->reason[0]               ? ev->reason
                         : g_conn == CONN_CONNECTING ? "The console did not accept the connection"
                                                     : "The session ended");
                set_conn(CONN_IDLE);
                log_event(LOG_WARN, "NET", "Session ended%s%s", ev->reason[0] ? ": " : "", ev->reason);
            }
            break;
    }
}

void app_tick(void) {
    log_pump();
    disc_poll();
    wake_tick();
    session_event_t ev;
    while (session_poll(&ev)) session_ev(&ev);
    session_reg_event_t rev;
    while (session_reg_poll(&rev)) reg_event(&rev);
    if (g_save_at && (Sint32)(SDL_GetTicks() - g_save_at) >= 0) save_pending();
    if (g_restart_at && (Sint32)(SDL_GetTicks() - g_restart_at) >= 0) restart_attempt();
    if (update_poll()) notify(APP_CHANGED_UPDATE);

    /* while streaming (and the menu is not up), forward the pad to the console (~120 Hz) */
    bool fwd = g_conn == CONN_STREAMING && input_streaming();
    if (!fwd && g_pad_fwd && g_conn == CONN_STREAMING) {
        session_pad_t idle = { 0 }; /* the menu opened: nothing stays held on the console */
        session_set_pad(&idle);
    }
    g_pad_fwd = fwd;
    if (fwd) {
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(now - g_pad_next) >= 8) {
            g_pad_next = now;
            input_pad_t ip;
            if (input_pad_get(&ip)) {
                session_pad_t sp = { 0 };
                sp.buttons = ip.buttons; /* input and session share the canonical bits */
                sp.l2 = ip.l2;
                sp.r2 = ip.r2;
                sp.left_x = ip.left_x;
                sp.left_y = ip.left_y;
                sp.right_x = ip.right_x;
                sp.right_y = ip.right_y;
                if (config_get()->input.touchpad) { /* the click is a button: it always goes */
                    sp.touch_down = ip.touch_down;
                    sp.touch_x = ip.touch_x;
                    sp.touch_y = ip.touch_y;
                }
                session_set_pad(&sp);
            }
        }
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
