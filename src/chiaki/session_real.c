/* chiaki/session_real.c - the real libchiaki backend (see session.h).
 *
 * Compiled only when CHIAKI_HAVE_LIBCHIAKI is set (libchiaki is linked, see
 * CHIAKI_LIB_STAGING in CMakeLists.txt); the simulated backend is session.c.
 *
 * Threading: libchiaki runs the session and the registration on its own
 * threads, so every callback only queues a copy of the event; session_poll()
 * and session_reg_poll() drain the queues on the main thread. No LVGL or app
 * state is ever touched from a libchiaki thread.
 *
 * Media is not wired yet: the video sample and the audio frames are accepted
 * and dropped (TODO(ndl): decode in media/ndl.h). PSN is also not wired yet
 * (TODO(http) token exchange, TODO(libchiaki) holepunch registration); only the
 * local PIN path is real so far.
 */
#include "session.h"

#ifdef CHIAKI_HAVE_LIBCHIAKI

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chiaki/audioreceiver.h>
#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <chiaki/discovery.h>
#include <chiaki/log.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include "../log.h"

#define EV_QUEUE 32

/* Sony's OAuth endpoint, exactly as chiaki-ng gui/include/psnaccountid.h builds it */
#define PSN_CLIENT_ID "ba495a24-818c-472b-b12d-ff231c1b5745"
#define PSN_REDIRECT  "https://remoteplay.dl.playstation.net/remoteplay/redirect"
#define PSN_SCOPE     "psn:clientapp%20referenceDataService:countryConfig.read%20" \
                      "pushNotification:webSocket.desktop.connect%20sessionManager:remotePlaySession.system.update"
#define PSN_AUTH_URL  "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize"                    \
                      "?service_entity=urn:service-entity:psn&response_type=code&client_id=" PSN_CLIENT_ID    \
                      "&redirect_uri=" PSN_REDIRECT "&scope=" PSN_SCOPE "&request_locale=en_US&ui=pr"         \
                      "&service_logo=ps&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&"

static SDL_mutex *g_lock;
static bool g_lib_init;
static ChiakiLog g_log;

static ChiakiSession g_session;
static bool g_session_active;
static config_host_t g_host;
static session_event_t g_sessq[EV_QUEUE];
static unsigned g_sess_head, g_sess_tail;

static ChiakiRegist g_regist;
static bool g_regist_active;
static char g_regist_addr[64];
static session_reg_event_t g_regq[EV_QUEUE];
static unsigned g_reg_head, g_reg_tail;

static void lock_init(void) {
    if (!g_lock) g_lock = SDL_CreateMutex();
}

/* libchiaki logs from its own threads; forward it into our single logger */
static void log_cb(ChiakiLogLevel level, const char *msg, void *user) {
    (void)user;
    log_msg("chiaki: [%c] %s", chiaki_log_level_char(level), msg);
}

static void lib_init(void) {
    lock_init();
    if (g_lib_init) return;
    chiaki_lib_init();
    chiaki_log_init(&g_log, CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR, log_cb, NULL);
    g_lib_init = true;
}

static void sess_push(const session_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_tail - g_sess_head < EV_QUEUE) g_sessq[g_sess_tail++ % EV_QUEUE] = *ev;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

static void reg_push(const session_reg_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_reg_tail - g_reg_head < EV_QUEUE) g_regq[g_reg_tail++ % EV_QUEUE] = *ev;
    else if (ev->type != REG_EV_PROGRESS) g_regq[(g_reg_tail - 1) % EV_QUEUE] = *ev;   /* full: a result wins */
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* ---------- libchiaki callbacks (their threads: queue only) ---------- */

static bool video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
    (void)buf;
    (void)buf_size;
    (void)frames_lost;
    (void)frame_recovered;
    (void)user;
    /* TODO(ndl): hand the still-encoded H.264/H.265 frame straight to media/ndl.h for hardware decode */
    return true;
}

static void audio_header_cb(ChiakiAudioHeader *header, void *user) {
    (void)header;
    (void)user;
    /* TODO(ndl): configure the Opus/audio path (see chiaki_opus_decoder_*) */
}

static void audio_frame_cb(uint8_t *buf, size_t buf_size, void *user) {
    (void)buf;
    (void)buf_size;
    (void)user;
}

static void session_event_cb(ChiakiEvent *event, void *user) {
    (void)user;
    session_event_t ev = { 0 };
    switch (event->type) {
        case CHIAKI_EVENT_CONNECTED:
            ev.type = SESSION_EV_CONNECTED;
            ev.progress = 100;
            break;
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
            ev.type = SESSION_EV_LOGIN_PIN;
            break;
        case CHIAKI_EVENT_RUMBLE:
            ev.type = SESSION_EV_RUMBLE;
            ev.rumble_left = event->rumble.left;
            ev.rumble_right = event->rumble.right;
            break;
        case CHIAKI_EVENT_QUIT:
            ev.type = SESSION_EV_QUIT;
            snprintf(ev.reason, sizeof(ev.reason), "%s", event->quit.reason_str
                                                             ? event->quit.reason_str
                                                             : chiaki_quit_reason_string(event->quit.reason));
            break;
        default:
            return;   /* keyboard/dualsense/etc. events are not used by the UI yet */
    }
    sess_push(&ev);
}

static void regist_cb(ChiakiRegistEvent *event, void *user) {
    (void)user;
    session_reg_event_t ev = { 0 };
    switch (event->type) {
        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS: {
            const ChiakiRegisteredHost *rh = event->registered_host;
            ev.type = REG_EV_SUCCESS;
            snprintf(ev.host.nickname, sizeof(ev.host.nickname), "%s", rh->server_nickname);
            if (!ev.host.nickname[0]) snprintf(ev.host.nickname, sizeof(ev.host.nickname), "%s", g_regist_addr);
            snprintf(ev.host.addr, sizeof(ev.host.addr), "%s", g_regist_addr);
            ev.host.is_ps5 = chiaki_target_is_ps5(rh->target);
            memcpy(ev.host.server_mac, rh->server_mac, sizeof(ev.host.server_mac));
            memcpy(ev.host.rp_regist_key, rh->rp_regist_key, sizeof(ev.host.rp_regist_key));
            memcpy(ev.host.rp_key, rh->rp_key, sizeof(ev.host.rp_key));
            break;
        }
        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
            ev.type = REG_EV_CANCELED;
            break;
        default:
            ev.type = REG_EV_FAILED;
            snprintf(ev.reason, sizeof(ev.reason), "The console did not accept the registration");
            break;
    }
    reg_push(&ev);
}

/* ---------- session ---------- */

bool session_available(void) { return true; }

static ChiakiTarget target_of(bool ps5) { return ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10; }

bool session_start(const config_host_t *host, const stream_settings_t *stream) {
    lib_init();
    if (g_session_active || !host || !stream) return false;
    memset(&g_session, 0, sizeof(g_session));

    ChiakiConnectInfo info = { 0 };
    info.ps5 = host->is_ps5;
    info.host = host->addr;
    memcpy(info.regist_key, host->rp_regist_key, sizeof(info.regist_key));   /* 16, \0-padded */
    memcpy(info.morning, host->rp_key, sizeof(info.morning));                /* the console's rp_key, as chiaki-ng does */
    info.video_profile_auto_downgrade = true;
    info.enable_idr_on_fec_failure = true;
    /* TODO(input): enable_dualsense needs a haptics sink (chiaki_session_set_haptics_sink) to be wired first */
    ChiakiVideoResolutionPreset res = stream->width <= 1280 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p
                                                            : CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
    chiaki_connect_video_profile_preset(&info.video_profile, res,
                                        stream->fps >= 60 ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30);
    info.video_profile.bitrate = (unsigned int)stream->bitrate_kbps;
    info.video_profile.codec = (ChiakiCodec)stream->codec;   /* our video_codec_t matches ChiakiCodec */
    if (config_psn()->linked) memcpy(info.psn_account_id, config_psn()->account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);

    ChiakiErrorCode err = chiaki_session_init(&g_session, &info, &g_log);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "NET", "chiaki_session_init failed: %s", chiaki_error_string(err));
        return false;
    }
    chiaki_session_set_event_cb(&g_session, session_event_cb, NULL);
    chiaki_session_set_video_sample_cb(&g_session, video_sample_cb, NULL);
    ChiakiAudioSink sink = { .user = NULL, .header_cb = audio_header_cb, .frame_cb = audio_frame_cb };
    chiaki_session_set_audio_sink(&g_session, &sink);

    err = chiaki_session_start(&g_session);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "NET", "chiaki_session_start failed: %s", chiaki_error_string(err));
        chiaki_session_fini(&g_session);
        return false;
    }
    g_host = *host;
    g_host.nickname[sizeof(g_host.nickname) - 1] = '\0';
    g_host.addr[sizeof(g_host.addr) - 1] = '\0';
    g_session_active = true;
    log_event(LOG_INFO, "NET", "Session started with %s (%s)", g_host.nickname, g_host.addr);
    return true;
}

void session_stop(void) {
    if (!g_session_active) return;
    chiaki_session_stop(&g_session);
    chiaki_session_join(&g_session);
    chiaki_session_fini(&g_session);
    log_event(LOG_WARN, "NET", "Session closed with %s", g_host.nickname);
    g_session_active = false;
}

bool session_poll(session_event_t *out) {
    lock_init();
    bool got = false;
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_head != g_sess_tail) {
        *out = g_sessq[g_sess_head++ % EV_QUEUE];
        got = true;
    }
    if (g_lock) SDL_UnlockMutex(g_lock);
    return got;
}

const config_host_t *session_active_host(void) { return g_session_active ? &g_host : NULL; }

void session_set_pad(const session_pad_t *pad) {
    if (!g_session_active || !pad) return;
    ChiakiControllerState st;
    chiaki_controller_state_set_idle(&st);
    st.buttons = pad->buttons;   /* session_pad_t.buttons is already a ChiakiControllerButton bitmask */
    st.l2_state = pad->l2;
    st.r2_state = pad->r2;
    st.left_x = pad->left_x;
    st.left_y = pad->left_y;
    st.right_x = pad->right_x;
    st.right_y = pad->right_y;
    if (pad->touch_down) {
        st.touches[0].x = pad->touch_x;
        st.touches[0].y = pad->touch_y;
        st.touches[0].id = 0;
        st.touch_id_next = 1;
    }
    chiaki_session_set_controller_state(&g_session, &st);
}

void session_set_login_pin(const char *pin) {
    if (!g_session_active || !pin) return;
    chiaki_session_set_login_pin(&g_session, (const uint8_t *)pin, strlen(pin));
}

/* the wakeup credential is the regist key read as a hex number (chiaki-ng DiscoveryManager) */
static uint64_t regist_credential(const uint8_t key[16]) {
    char s[17];
    memcpy(s, key, 16);
    s[16] = '\0';
    return strtoull(s, NULL, 16);
}

bool session_wakeup(const config_host_t *host) {
    lib_init();
    if (!host || !host->addr[0]) return false;
    ChiakiErrorCode err = chiaki_discovery_wakeup(&g_log, NULL, host->addr, regist_credential(host->rp_regist_key),
                                                  host->is_ps5);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_WARN, "HOST", "Wake-up failed: %s", chiaki_error_string(err));
        return false;
    }
    log_msg("chiaki: wake-up packet sent to %s", host->addr);
    return true;
}

/* ---------- registration ---------- */

bool session_reg_available(void) { return true; }
bool session_reg_running(void) { return g_regist_active; }

bool session_reg_begin_pin(const char *addr, const char *pin, bool ps5) {
    lib_init();
    if (g_regist_active || !addr || !*addr || !pin) return false;
    memset(&g_regist, 0, sizeof(g_regist));
    snprintf(g_regist_addr, sizeof(g_regist_addr), "%s", addr);

    ChiakiRegistInfo info = { 0 };
    info.target = target_of(ps5);
    info.host = g_regist_addr;
    info.broadcast = false;
    info.pin = (uint32_t)strtoul(pin, NULL, 10);
    if (config_psn()->linked) memcpy(info.psn_account_id, config_psn()->account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);

    ChiakiErrorCode err = chiaki_regist_start(&g_regist, &g_log, &info, regist_cb, NULL);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "REG", "chiaki_regist_start failed: %s", chiaki_error_string(err));
        return false;
    }
    g_regist_active = true;
    log_event(LOG_INFO, "REG", "Registering with %s (PIN)", g_regist_addr);
    return true;
}

bool session_reg_submit_code(const char *code) {
    (void)code;
    /* TODO(http): POST https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token with libcurl (the
     * staged libcurl.a), Basic auth with the client id and secret, form body grant_type=authorization_code
     * &code=...&scope=...&redirect_uri=..., then GET <token_url>/<access_token> for user_id + online_id,
     * and push REG_EV_PSN_LINKED. See chiaki-ng gui/src/psnaccountid.cpp. */
    log_event(LOG_ERR, "REG", "PSN sign-in is not implemented yet");
    return false;
}

bool session_reg_begin_psn(const uint8_t account_id[8], bool ps5) {
    (void)account_id;
    (void)ps5;
    /* TODO(libchiaki): PSN registration goes through the holepunch session (chiaki_holepunch_*) with the PSN
     * device list (TODO(http)) and auto_regist; the console then has no local address, so discovery is needed. */
    log_event(LOG_ERR, "REG", "PSN registration is not implemented yet");
    return false;
}

void session_reg_stop(void) {
    if (!g_regist_active) return;
    chiaki_regist_stop(&g_regist);
    chiaki_regist_fini(&g_regist);
    g_regist_active = false;
}

bool session_reg_poll(session_reg_event_t *out) {
    lock_init();
    bool got = false;
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_reg_head != g_reg_tail) {
        *out = g_regq[g_reg_head++ % EV_QUEUE];
        got = true;
    }
    if (g_lock) SDL_UnlockMutex(g_lock);
    /* a terminal event means the regist thread is done: join and free it on the main thread
     * (chiaki_regist_stop already finalized a canceled one, so only do this while still active) */
    if (got && g_regist_active &&
        (out->type == REG_EV_SUCCESS || out->type == REG_EV_FAILED || out->type == REG_EV_CANCELED)) {
        chiaki_regist_fini(&g_regist);
        g_regist_active = false;
    }
    return got;
}

const char *session_reg_psn_url(void) {
    static char url[sizeof(PSN_AUTH_URL) + CONFIG_DUID_LEN + 8];
    snprintf(url, sizeof(url), "%sduid=%s&", PSN_AUTH_URL, config_device_uid());
    return url;
}

#endif /* CHIAKI_HAVE_LIBCHIAKI */