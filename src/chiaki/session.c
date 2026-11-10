/* chiaki/session.c - STUB until libchiaki is linked: sessions are not available,
 * registration is simulated on the main thread (see session.h). */
#include "session.h"

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

/* ---------- registration: event queue (any thread pushes, the main loop polls) ---------- */

static SDL_mutex *g_lock;
static session_reg_event_t g_queue[REG_QUEUE];
static unsigned g_head, g_tail;

static void reg_init(void) {   /* main thread, before any libchiaki thread can exist */
    if (!g_lock) g_lock = SDL_CreateMutex();
}

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
    reg_init();
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
    reg_init();
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
    reg_init();
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
    reg_init();
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
