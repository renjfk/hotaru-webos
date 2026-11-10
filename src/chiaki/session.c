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

#define SIM_PIN_MS  2000   /* simulated chiaki_regist round trip */
#define SIM_CODE_MS 1200   /* simulated PSN token exchange */
#define SIM_PSN_MS  1800   /* simulated registration over PSN */
#define REG_QUEUE   16

/* Sony's OAuth endpoint, as chiaki-ng gui/include/psnaccountid.h (LOGIN_URL) and
 * QmlBackend::psnLoginUrl() build it. The scope's spaces are sent as %20 (what
 * Qt's QUrl puts on the wire): a raw space would end the URL in a QR scanner. */
#define PSN_CLIENT_ID "ba495a24-818c-472b-b12d-ff231c1b5745"
#define PSN_REDIRECT  "https://remoteplay.dl.playstation.net/remoteplay/redirect"
#define PSN_SCOPE     "psn:clientapp%20referenceDataService:countryConfig.read%20" \
                      "pushNotification:webSocket.desktop.connect%20sessionManager:remotePlaySession.system.update"
#define PSN_AUTH_URL  "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"                    \
                      "?service_entity=urn:service-entity:psn&response_type=code&client_id=" PSN_CLIENT_ID    \
                      "&redirect_uri=" PSN_REDIRECT "&scope=" PSN_SCOPE "&request_locale=en_US&ui=pr"         \
                      "&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&"

bool session_available(void) { return false; }

/* TODO(libchiaki): chiaki_session_set_controller_state(&g_session, &state) every frame;
 * app.c/input will call this with the DS4 state once the pad is wired to the session */
void session_set_pad(const session_pad_t *pad) { (void)pad; }
void session_set_login_pin(const char *pin) { (void)pin; }

/* ---------- session event queue + the simulated session (see session.h) ---------- */

#define SIM_SESSION_MS 3000   /* simulated handshake length while libchiaki is missing */
#define SESS_QUEUE     16

static SDL_mutex *g_lock;
static session_event_t g_sessq[SESS_QUEUE];
static unsigned g_sess_head, g_sess_tail;

static struct {
    bool active, connected_sent;
    Uint32 start;
    int progress;
    config_host_t host;
    stream_settings_t stream;
} g_session;

static void lock_init(void) {   /* main thread, before any libchiaki thread can exist */
    if (!g_lock) g_lock = SDL_CreateMutex();
}

static void sess_push(const session_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_tail - g_sess_head < SESS_QUEUE) g_sessq[g_sess_tail++ % SESS_QUEUE] = *ev;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* TODO(libchiaki): the session callback, called on libchiaki's threads. Only queue, then
 * session_poll() drains it on the main thread:
 *   CHIAKI_EVENT_CONNECTED / _LOGIN_PIN_REQUEST / _RUMBLE / _QUIT -> the matching SESSION_EV_*,
 *   and the connect progress -> SESSION_EV_PROGRESS; chiaki_session_fini() when QUIT is polled. */
static void sim_session_step(void) {
    if (!g_session.active || session_available()) return;
    Uint32 elapsed = SDL_GetTicks() - g_session.start;
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
    /* TODO(libchiaki): chiaki_session_start with ChiakiConnectInfo built from the stored console:
     *   info.host = host->addr; info.ps5 = host->is_ps5; memcpy(info.regist_key, host->rp_regist_key, 16);
     *   memcpy(info.rp_key, host->rp_key, 16); info.video_profile = { .width, .height, .max_fps, .bitrate };
     *   chiaki_session_start(&g_session, &g_log, &info, session_cb, NULL); */
    log_msg("session: simulated start with %s (%s) %dx%d@%d", host->nickname, host->addr, stream->width, stream->height,
            stream->fps);
    g_session.active = true;
    g_session.connected_sent = false;
    g_session.progress = -1;
    g_session.start = SDL_GetTicks();
    g_session.host = *host;
    g_session.stream = *stream;
    return true;
}

void session_stop(void) {
    if (!g_session.active) return;
    /* TODO(libchiaki): chiaki_session_stop(&g_session); chiaki_session_fini(&g_session); */
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

/* a console in standby is woken with a UDP broadcast; needs a known address */
bool session_wakeup(const config_host_t *host) {
    if (!host || !host->addr[0]) return false;
    /* TODO(libchiaki): chiaki_discovery_wakeup(&discovery, &log, host->addr);
     * TODO(libchiaki): or the holepunch path for a PSN-registered console (no local address) */
    log_msg("session: simulated wake-up packet to %s", host->addr);
    return true;
}

/* ---------- registration: event queue (any thread pushes, the main loop polls) ---------- */

static session_reg_event_t g_queue[REG_QUEUE];
static unsigned g_head, g_tail;

static void reg_push(const session_reg_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_tail - g_head < REG_QUEUE) g_queue[g_tail++ % REG_QUEUE] = *ev;
    else if (ev->type != REG_EV_PROGRESS) g_queue[(g_tail - 1) % REG_QUEUE] = *ev;   /* full: a result wins */
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* TODO(libchiaki): the ChiakiRegistCb, called on libchiaki's regist thread. Only queue:
 *   static void regist_cb(ChiakiRegistEvent *e, void *user) {
 *       session_reg_event_t ev = { 0 };
 *       ev.type = e->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS ? REG_EV_SUCCESS
 *               : e->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED ? REG_EV_CANCELED : REG_EV_FAILED;
 *       if (ev.type == REG_EV_SUCCESS) {   // ChiakiRegisteredHost -> config_host_t
 *           const ChiakiRegisteredHost *h = e->registered_host;
 *           snprintf(ev.host.nickname, ..., "%s", h->server_nickname);
 *           ev.host.addr = the address we registered with; ev.host.is_ps5 = chiaki_target_is_ps5(h->target);
 *           memcpy server_mac, rp_regist_key (char[16], \0-padded), rp_key;
 *       } else if (ev.type == REG_EV_FAILED) snprintf(ev.reason, ..., "...");
 *       reg_push(&ev);
 *   }
 * then chiaki_regist_fini() from the main thread once the result is polled. */

/* ---------- registration: simulated backend ---------- */

typedef enum { OP_NONE, OP_PIN, OP_CODE, OP_PSN } reg_op_t;

static struct {
    reg_op_t op;
    Uint32 start, length;
    int progress;
    session_reg_event_t result;   /* delivered when the simulated time is up */
} g_sim;

static uint32_t fnv1a(const void *data, size_t n) {
    const uint8_t *p = data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

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
    /* TODO(libchiaki): local registration with the console's PIN:
     *   ChiakiRegistInfo info = { .target = ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10, .host = addr,
     *       .broadcast = false, .pin = (uint32_t)strtoul(pin, NULL, 10), .psn_online_id = NULL };
     *   memcpy(info.psn_account_id, config_psn()->account_id, 8);   // PS5 and PS4 >= 7.0 need the account id
     *   chiaki_regist_start(&g_regist, &g_log, &info, regist_cb, NULL);
     * (chiaki-ng gui/src/qmlbackend.cpp QmlBackend::registerHost + QmlRegist) */
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
    /* TODO(http): exchange the code for tokens, off the main thread, then reg_push() the result:
     *   POST https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token, Basic auth with PSN_CLIENT_ID and the
     *   client secret (chiaki-ng gui/include/psnaccountid.h), x-www-form-urlencoded body
     *   grant_type=authorization_code&code=<code>&scope=<scope>&redirect_uri=<PSN_REDIRECT>
     *   -> access_token, refresh_token, expires_in; then GET <token url>/<access_token> -> user_id + online_id;
     *   account id = user_id as 8 little-endian bytes (chiaki-ng gui/src/psnaccountid.cpp).
     *   REG_EV_PSN_LINKED carries them in ev.psn; any HTTP/JSON error is REG_EV_FAILED. */
    log_msg("reg: simulated PSN token exchange");
    sim_start(OP_CODE, SIM_CODE_MS);
    if (!strcmp(code, "fail")) {
        sim_fail("Sony did not accept the code (simulated)");
    } else {
        g_sim.result = (session_reg_event_t){ .type = REG_EV_PSN_LINKED, .progress = 100 };
        config_psn_t *p = &g_sim.result.psn;
        uint32_t h = fnv1a(code, strlen(code));
        const uint8_t id[8] = { 'S', 'I', 'M', 0, (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
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
    /* TODO(libchiaki): registration over PSN. chiaki-ng does it through the holepunch connection: the PSN
     * device list (TODO(http): QmlBackend::updatePsnHosts, needs config_psn()->access_token) names the
     * consoles and their duid, then a session with auto_regist (QmlBackend::autoRegister,
     * chiaki_holepunch_session_init/_start) registers and reports a ChiakiRegisteredHost like regist_cb.
     * The console has no local address then; discovery finds it by server_mac later. */
    log_msg("reg: simulated PSN registration");
    sim_start(OP_PSN, SIM_PSN_MS);
    g_sim.result = (session_reg_event_t){ .type = REG_EV_SUCCESS, .progress = 100 };
    sim_host(&g_sim.result.host, NULL, ps5, fnv1a(account_id, 8), true);
    return true;
}

void session_reg_stop(void) {
    if (!session_reg_running()) return;
    /* TODO(libchiaki): chiaki_regist_stop() + chiaki_regist_fini(); the callback reports FINISHED_CANCELED */
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
