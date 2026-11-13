/* chiaki/session.c - the simulated session/registration backend (see session.h).
 *
 * Compiled only when libchiaki is NOT linked; the real backend lives in
 * chiaki/session_real.c and is selected by CHIAKI_HAVE_LIBCHIAKI. Sessions and
 * registration run on the main thread here, so the UI can walk the whole flow;
 * no libchiaki threads exist in this build.
 */
#include "session.h"

#ifndef CHIAKI_HAVE_LIBCHIAKI

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "../log.h"

#define SIM_PIN_MS     2000 /* simulated chiaki_regist round trip */
#define SIM_CODE_MS    1200 /* simulated PSN token exchange */
#define SIM_PSN_MS     1800 /* simulated registration over PSN */
#define SIM_DEVICES_MS 1200 /* simulated PSN device list */
#define REG_QUEUE      16

/* Sony's OAuth endpoint, as chiaki-ng gui/include/psnaccountid.h (LOGIN_URL) and
 * QmlBackend::psnLoginUrl() build it. The scope's spaces are sent as %20 (what
 * Qt's QUrl puts on the wire): a raw space would end the URL in a QR scanner. */
#define PSN_CLIENT_ID "ba495a24-818c-472b-b12d-ff231c1b5745"
#define PSN_REDIRECT  "https://remoteplay.dl.playstation.net/remoteplay/redirect"
#define PSN_SCOPE                                                                                                      \
    "psn:clientapp%20referenceDataService:countryConfig.read%20"                                                       \
    "pushNotification:webSocket.desktop.connect%20sessionManager:remotePlaySession.system.update"
#define PSN_AUTH_URL                                                                                                   \
    "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"                                                \
    "?service_entity=urn:service-entity:psn&response_type=code&client_id=" PSN_CLIENT_ID "&redirect_uri=" PSN_REDIRECT \
    "&scope=" PSN_SCOPE "&request_locale=en_US&ui=pr"                                                                  \
    "&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&"

bool session_available(void) { return false; }

/* the simulation does not consume the pad, but records it so the host tests
 * can assert what the app forwarded */
static session_pad_t g_last_pad;
void session_set_pad(const session_pad_t *pad) {
    if (pad) g_last_pad = *pad;
}
const session_pad_t *session_last_pad(void) { return &g_last_pad; }
void session_set_login_pin(const char *pin) {
    log_msg("session: login PIN submitted (%s)", pin && *pin ? "set" : "empty");
}

/* ---------- session event queue + the simulated session (see session.h) ---------- */

#define SIM_SESSION_MS     3000 /* simulated handshake length while libchiaki is missing */
#define SIM_UNREACHABLE_MS 1000 /* a ".98" console gives up this soon on the local network */
#define SESS_QUEUE         16

static SDL_mutex *g_lock;
static session_event_t g_sessq[SESS_QUEUE];
static unsigned g_sess_head, g_sess_tail;

static struct {
    bool active, connected_sent, login_sent, quit_sent;
    Uint32 start;
    int progress;
    config_host_t host;
    stream_settings_t stream;
} g_session;

static void lock_init(void) { /* main thread, before any libchiaki thread can exist */
    if (!g_lock) g_lock = SDL_CreateMutex();
}

static void sess_push(const session_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_tail - g_sess_head < SESS_QUEUE) g_sessq[g_sess_tail++ % SESS_QUEUE] = *ev;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* Magic addresses the host tests use: ".99" asks for the login PIN mid-handshake, ".98" never
 * answers on the local network; others connect straight through. */
static bool sim_addr_is(const char *addr, const char *end) {
    size_t n = addr ? strlen(addr) : 0, m = strlen(end);
    return n >= m && !strcmp(addr + n - m, end);
}

static void sim_session_step(void) {
    if (!g_session.active || session_available()) return;
    Uint32 elapsed = SDL_GetTicks() - g_session.start;
    if (g_session.quit_sent) return;
    if (sim_addr_is(g_session.host.addr, ".98") && elapsed >= SIM_UNREACHABLE_MS) {
        g_session.quit_sent = true;
        session_event_t ev = { .type = SESSION_EV_QUIT, .unreachable = true };
        snprintf(ev.reason, sizeof(ev.reason), "No answer from the console on the local network (simulated)");
        sess_push(&ev);
        return;
    }
    if (!g_session.login_sent && sim_addr_is(g_session.host.addr, ".99") && elapsed >= SIM_SESSION_MS / 2) {
        g_session.login_sent = true;
        session_event_t ev = { .type = SESSION_EV_LOGIN_PIN };
        sess_push(&ev);
    }
    if (elapsed >= SIM_SESSION_MS) {
        if (g_session.connected_sent) return;
        g_session.connected_sent = true;
        session_event_t ev = { .type = SESSION_EV_CONNECTED, .progress = 100 };
        sess_push(&ev);
        return;
    }
    int p = (int)(elapsed * 100 / SIM_SESSION_MS);
    if (p == g_session.progress) return;
    g_session.progress = p;
    session_event_t ev = { .type = SESSION_EV_PROGRESS, .progress = p };
    sess_push(&ev);
}

bool session_start(const config_host_t *host, const stream_settings_t *stream) {
    lock_init();
    if (g_session.active || !host || !stream) return false;
    log_msg("session: simulated start with %s (%s) %dx%d@%d", host->nickname, host->addr, stream->width, stream->height,
            stream->fps);
    g_session.active = true;
    g_session.connected_sent = false;
    g_session.login_sent = false;
    g_session.quit_sent = false;
    g_session.progress = -1;
    g_session.start = SDL_GetTicks();
    g_session.host = *host;
    g_session.stream = *stream;
    return true;
}

void session_stop(void) {
    if (!g_session.active) return;
    log_msg("session: simulated stop with %s", g_session.host.nickname);
    g_session.active = false;
}

bool session_poll(session_event_t *out) {
    lock_init();
    sim_session_step();
    bool got = false;
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_head != g_sess_tail) {
        *out = g_sessq[g_sess_head++ % SESS_QUEUE];
        got = true;
    }
    if (g_lock) SDL_UnlockMutex(g_lock);
    return got;
}

const config_host_t *session_active_host(void) { return g_session.active ? &g_session.host : NULL; }

static uint32_t fnv1a(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* ---------- discovery (simulated) ---------- */

#define SIM_PING_MS    500             /* the first answer comes with the first search, as on the TV */
#define SIM_WAKE_MS    2500            /* a woken console answers ready this much later */
#define SIM_FOUND_ADDR "192.168.1.240" /* a PS5 on the network that is not paired yet */
#define SIM_WOKEN_MAX  8

static bool g_disc_on;
static Uint32 g_disc_since;
static struct {
    char addr[64];
    Uint32 ready_at;
} g_woken[SIM_WOKEN_MAX];
static int g_woken_n;

static bool sim_awake(const char *addr) {
    if (!sim_addr_is(addr, ".97")) return true; /* ".97" sleeps until woken */
    for (int i = 0; i < g_woken_n; i++)
        if (!strcmp(g_woken[i].addr, addr)) return (Sint32)(SDL_GetTicks() - g_woken[i].ready_at) >= 0;
    return false;
}

/* a console in standby is woken with a UDP broadcast; needs a known address */
bool session_wakeup(const config_host_t *host) {
    if (!host || !host->addr[0]) return false;
    log_msg("session: simulated wake-up packet to %s", host->addr);
    int i = 0;
    while (i < g_woken_n && strcmp(g_woken[i].addr, host->addr) != 0) i++;
    if (i == g_woken_n) {
        if (g_woken_n == SIM_WOKEN_MAX) return true;
        g_woken_n++;
        snprintf(g_woken[i].addr, sizeof(g_woken[i].addr), "%s", host->addr);
        g_woken[i].ready_at = SDL_GetTicks() + SIM_WAKE_MS;
    }
    return true;
}

bool session_discovery_start(void) {
    if (g_disc_on) return true;
    g_disc_on = true;
    g_disc_since = SDL_GetTicks();
    log_msg("session: simulated discovery started");
    return true;
}

void session_discovery_stop(void) {
    if (g_disc_on) log_msg("session: simulated discovery stopped");
    g_disc_on = false;
}

static void sim_found(session_found_t *f, const char *addr, const char *name, const uint8_t mac[6], bool ps5,
                      bool ready) {
    memset(f, 0, sizeof(*f));
    snprintf(f->addr, sizeof(f->addr), "%s", addr);
    snprintf(f->name, sizeof(f->name), "%s", name);
    snprintf(f->host_id, sizeof(f->host_id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    snprintf(f->system_version, sizeof(f->system_version), "%s", ps5 ? "09400000" : "11520000");
    f->ps5 = ps5;
    f->ready = ready;
}

/* the paired consoles that have an address (not ".98"), then the unpaired PS5. An entry with the
 * unpaired PS5's MAC is that console: it answers from SIM_FOUND_ADDR, wherever the entry says it
 * was (the router moved it) */
int session_discovery_list(session_found_t *out, int max, unsigned *gen) {
    static const uint8_t sim_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x02, 0x40 };
    session_found_t list[SESSION_FOUND_MAX];
    int n = 0;
    if (g_disc_on && SDL_GetTicks() - g_disc_since >= SIM_PING_MS) {
        bool unpaired_taken = false;
        for (int i = 0; i < config_host_count() && n < SESSION_FOUND_MAX - 1; i++) {
            const config_host_t *c = config_host(i);
            bool moved = !memcmp(c->server_mac, sim_mac, 6);
            const char *addr = moved ? SIM_FOUND_ADDR : c->addr;
            if (!c->addr[0] || sim_addr_is(addr, ".98")) continue;
            bool dup = false;
            for (int j = 0; j < n; j++) dup = dup || !strcmp(list[j].addr, addr);
            if (dup) continue;
            if (!strcmp(addr, SIM_FOUND_ADDR)) unpaired_taken = true;
            sim_found(&list[n++], addr, c->nickname, c->server_mac, c->is_ps5, sim_awake(addr));
        }
        if (!unpaired_taken) sim_found(&list[n++], SIM_FOUND_ADDR, "PS5-240", sim_mac, true, true);
    }
    if (n > max) n = max;
    if (n > 0) memcpy(out, list, sizeof(list[0]) * (size_t)n);
    if (gen) *gen = n ? fnv1a(list, sizeof(list[0]) * (size_t)n) : 0;
    return n;
}

/* ---------- registration: event queue (any thread pushes, the main loop polls) ---------- */

static session_reg_event_t g_queue[REG_QUEUE];
static unsigned g_head, g_tail;

static void reg_push(const session_reg_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_tail - g_head < REG_QUEUE) g_queue[g_tail++ % REG_QUEUE] = *ev;
    else if (ev->type != REG_EV_PROGRESS) g_queue[(g_tail - 1) % REG_QUEUE] = *ev; /* full: a result wins */
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* ---------- registration: simulated backend ---------- */

typedef enum { OP_NONE, OP_PIN, OP_CODE, OP_PSN, OP_DEVICES } reg_op_t;

static struct {
    reg_op_t op;
    Uint32 start, length;
    int progress;
    session_reg_event_t result; /* delivered when the simulated time is up */
} g_sim;

static void sim_start(reg_op_t op, Uint32 length) {
    g_sim.op = op;
    g_sim.start = SDL_GetTicks();
    g_sim.length = length;
    g_sim.progress = -1;
}

static void sim_fail(const char *reason) {
    memset(&g_sim.result, 0, sizeof(g_sim.result));
    g_sim.result.type = REG_EV_FAILED;
    snprintf(g_sim.result.reason, sizeof(g_sim.result.reason), "%s", reason);
}

/* an obviously fake console: "Simulated" nickname, a locally administered MAC (02:..),
 * keys spelling out what they are */
static void sim_host(config_host_t *h, const char *addr, bool ps5, uint32_t id, bool psn) {
    memset(h, 0, sizeof(*h));
    snprintf(h->nickname, sizeof(h->nickname), "Simulated %s%s", ps5 ? "PS5" : "PS4", psn ? " (PSN)" : "");
    snprintf(h->addr, sizeof(h->addr), "%s", addr ? addr : "");
    h->is_ps5 = ps5;
    const uint8_t mac[6] = { 0x02, 0x00, psn ? 0x01 : 0x00, (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)id };
    memcpy(h->server_mac, mac, sizeof(mac));
    memcpy(h->rp_regist_key, "SIMULATED-REGIST", sizeof(h->rp_regist_key));
    memcpy(h->rp_key, "SIMULATED-RPKEY!", sizeof(h->rp_key));
}

static void sim_step(void) {
    if (g_sim.op == OP_NONE || session_reg_available()) return;
    Uint32 elapsed = SDL_GetTicks() - g_sim.start;
    if (elapsed >= g_sim.length) {
        g_sim.op = OP_NONE;
        reg_push(&g_sim.result);
        return;
    }
    int p = (int)(elapsed * 100 / g_sim.length);
    if (p == g_sim.progress) return;
    g_sim.progress = p;
    session_reg_event_t ev = { .type = REG_EV_PROGRESS, .progress = p };
    reg_push(&ev);
}

static bool pin_valid(const char *pin) { return pin && strlen(pin) == 8 && strspn(pin, "0123456789") == 8; }

bool session_reg_available(void) { return false; }
bool session_reg_running(void) { return g_sim.op != OP_NONE; }

bool session_reg_begin_pin(const char *addr, const char *pin, bool ps5) {
    lock_init();
    if (session_reg_running() || !addr || !*addr || !pin_valid(pin)) return false;
    log_msg("reg: simulated PIN registration with %s", addr);
    sim_start(OP_PIN, SIM_PIN_MS);
    if (!strcmp(pin, "00000000")) {
        sim_fail("The console rejected the PIN (simulated)");
    } else {
        g_sim.result = (session_reg_event_t){ .type = REG_EV_SUCCESS, .progress = 100 };
        sim_host(&g_sim.result.host, addr, ps5, fnv1a(addr, strlen(addr)), false);
    }
    return true;
}

bool session_reg_submit_code(const char *code) {
    lock_init();
    if (session_reg_running() || !code || !*code) return false;
    log_msg("reg: simulated PSN token exchange");
    sim_start(OP_CODE, SIM_CODE_MS);
    if (!strcmp(code, "fail")) {
        sim_fail("Sony did not accept the code (simulated)");
    } else {
        g_sim.result = (session_reg_event_t){ .type = REG_EV_PSN_LINKED, .progress = 100 };
        config_psn_t *p = &g_sim.result.psn;
        uint32_t h = fnv1a(code, strlen(code));
        const uint8_t id[8] = {
            'S', 'I', 'M', 0, (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h
        };
        p->linked = true;
        memcpy(p->account_id, id, sizeof(id));
        snprintf(p->online_id, sizeof(p->online_id), "SimulatedUser");
        snprintf(p->access_token, sizeof(p->access_token), "simulated-access-token");
        snprintf(p->refresh_token, sizeof(p->refresh_token), "simulated-refresh-token");
    }
    return true;
}

bool session_reg_begin_psn(const uint8_t account_id[8], bool ps5) {
    lock_init();
    if (session_reg_running() || !account_id) return false;
    log_msg("reg: simulated PSN registration");
    sim_start(OP_PSN, SIM_PSN_MS);
    g_sim.result = (session_reg_event_t){ .type = REG_EV_SUCCESS, .progress = 100 };
    sim_host(&g_sim.result.host, NULL, ps5, fnv1a(account_id, 8), true);
    return true;
}

bool session_psn_fetch_devices(void) {
    lock_init();
    if (session_reg_running() || !config_psn_has_account_id()) return false;
    log_msg("reg: simulated PSN device list");
    sim_start(OP_DEVICES, SIM_DEVICES_MS);
    memset(&g_sim.result, 0, sizeof(g_sim.result));
    g_sim.result.type = REG_EV_PSN_DEVICES;
    const struct {
        const char *name;
        bool ps5;
        uint32_t seed;
    } dev[] = {
        { "Simulated PS5", true, 0x50533520 },
        { "Simulated PS4", false, 0x50533420 },
    };
    for (int i = 0; i < 2; i++) {
        session_psn_device_t *d = &g_sim.result.devices[i];
        snprintf(d->name, sizeof(d->name), "%s", dev[i].name);
        d->ps5 = dev[i].ps5;
        for (int b = 0; b < 32; b++) d->uid[b] = (uint8_t)(dev[i].seed + i * 0x40 + b * 7 + 1); /* distinct, non-zero */
    }
    g_sim.result.device_count = 2;
    return true;
}

void session_reg_stop(void) {
    if (!session_reg_running()) return;
    g_sim.op = OP_NONE;
    session_reg_event_t ev = { .type = REG_EV_CANCELED };
    reg_push(&ev);
}

bool session_reg_poll(session_reg_event_t *out) {
    lock_init();
    sim_step();
    bool got = false;
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_head != g_tail) {
        *out = g_queue[g_head++ % REG_QUEUE];
        got = true;
    }
    if (g_lock) SDL_UnlockMutex(g_lock);
    return got;
}

const char *session_reg_psn_url(void) {
    static char url[sizeof(PSN_AUTH_URL) + CONFIG_DUID_LEN + 8];
    snprintf(url, sizeof(url), "%sduid=%s&", PSN_AUTH_URL, config_device_uid());
    return url;
}

#endif /* !CHIAKI_HAVE_LIBCHIAKI */
