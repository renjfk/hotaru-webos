/* chiaki/session_real.c - the real libchiaki backend (see session.h).
 *
 * Compiled only when HOTARU_HAVE_LIBCHIAKI is set (libchiaki is linked, see
 * CHIAKI_LIB_STAGING in CMakeLists.txt); the simulated backend is session.c.
 *
 * Threading: libchiaki runs the session and the registration on its own
 * threads, so every callback only queues a copy of the event; session_poll()
 * and session_reg_poll() drain the queues on the main thread. No LVGL or app
 * state is ever touched from a libchiaki thread.
 *
 * Media: libchiaki delivers the video frames and the Opus packets on its network
 * receive thread, which must never block or the stream socket overflows. Both go
 * through media/feeder.h: one worker thread feeds NDL the video, another decodes
 * the Opus and plays the PCM. While streaming, session_poll() logs a stats line
 * every STATS_MS (frame rate, bitrate, losses, socket drops, queue depth). PSN runs on detached SDL
 * threads (the OAuth token exchange with libcurl/json-c, and the device list via
 * chiaki_holepunch_list_devices); every result carries an operation id and stale
 * ones are dropped. The PSN connection (holepunch connect, with the regist the
 * library runs inside its session thread) and access-token refresh are
 * implemented. Discovery runs chiaki_discovery_service; its callback copies the
 * host list under the lock for session_discovery_list().
 */
#include "session.h"

#ifdef HOTARU_HAVE_LIBCHIAKI

#include <SDL.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <curl/curl.h>
#include <json-c/json.h>

#include <chiaki/audioreceiver.h>
#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <chiaki/discovery.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include "../log.h"
#include "../media/feeder.h"
#include "../media/ndl.h"
#include "../media/pacer.h"

#define EV_QUEUE      32
#define VIDEO_SLOTS   48                /* 0.8 s of 60 fps video: the smoothing delay waits here */
#define AUDIO_SLOTS   64                /* 0.64 s of 10 ms Opus packets */
#define STREAM_RCVBUF (4 * 1024 * 1024) /* Linux caps it at net.core.rmem_max (512 KB on the TV) */
#define STATS_MS      10000

/* Sony's OAuth endpoint, exactly as chiaki-ng gui/include/psnaccountid.h builds it */
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
#define PSN_TOKEN_URL     "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token"
#define PSN_CLIENT_SECRET "mvaiZkRsAsI1IBkY" /* chiaki-ng gui/include/psnaccountid.h */
#define PSN_SCOPE_PLAIN                                                                                                \
    "psn:clientapp referenceDataService:countryConfig.read "                                                           \
    "pushNotification:webSocket.desktop.connect sessionManager:remotePlaySession.system.update"

static SDL_mutex *g_lock;
static bool g_lib_init;
static ChiakiLog g_log;

static ChiakiSession g_session;
static bool g_session_active;
static config_host_t g_host;
static session_pad_t g_last_pad; /* last pad state, for tests/diagnostics */
static session_event_t g_sessq[EV_QUEUE];
static unsigned g_sess_head, g_sess_tail;

static ChiakiRegist g_regist;
static bool g_regist_active;
static unsigned g_reg_gen;    /* bumped per operation; stale queued results are dropped */
static unsigned g_regist_gen; /* generation of the running chiaki_regist */
static bool g_psn_fetching;
static unsigned g_fetch_gen;      /* generation of the in-flight device fetch */
static bool g_psn_connect_active; /* a PSN holepunch connect is being set up on a worker */
static bool g_psn_cancel;         /* the user disconnected during that setup */

static bool psn_ensure_token(void); /* refresh the access token if it has expired (defined below) */
static char g_regist_addr[64];
static session_reg_event_t g_regq[EV_QUEUE];
static unsigned g_reg_head, g_reg_tail;

static void lock_init(void) {
    if (!g_lock) g_lock = SDL_CreateMutex();
}

/* a chiaki_log_hexdump() line: its header, or "%6zx " offset + hex byte pairs */
static bool is_hexdump_line(const char *m) {
    if (!strncmp(m, "offset 0  1  2", 14)) return true;
    if (strlen(m) < 10 || m[6] != ' ' || m[9] != ' ') return false;
    for (int i = 0; i < 6; i++)
        if (m[i] != ' ' && !isxdigit((unsigned char)m[i])) return false;
    return isxdigit((unsigned char)m[5]) && isxdigit((unsigned char)m[7]) && isxdigit((unsigned char)m[8]);
}

/* libchiaki logs every ctrl message as an INFO hexdump, the frequent heartbeat included, which
 * buries everything else; those stay out of the log (warning and error dumps are kept) */
static bool log_is_noise(ChiakiLogLevel level, const char *msg) {
    if (level != CHIAKI_LOG_INFO) return false;
    return !strcmp(msg, "CTRL RECEIVED") || !strcmp(msg, "Ctrl received Heartbeat, sending reply") ||
           is_hexdump_line(msg);
}

/* libchiaki logs from its own threads; forward it into our single logger. Its debug level only
 * in probe mode. */
static void log_cb(ChiakiLogLevel level, const char *msg, void *user) {
    (void)user;
    if (log_is_noise(level, msg) || (level == CHIAKI_LOG_DEBUG && !log_probe_on())) return;
    log_msg("chiaki: [%c] %s", chiaki_log_level_char(level), msg);
}

static void lib_init(void) {
    lock_init();
    if (g_lib_init) return;
    chiaki_lib_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    chiaki_log_init(&g_log, CHIAKI_LOG_DEBUG | CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR, log_cb, NULL);
    g_lib_init = true;
}

static void sess_push(const session_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_sess_tail - g_sess_head < EV_QUEUE) g_sessq[g_sess_tail++ % EV_QUEUE] = *ev;
    else if (ev->type == SESSION_EV_QUIT) g_sessq[(g_sess_tail - 1) % EV_QUEUE] = *ev; /* full: a quit wins */
    if (g_lock) SDL_UnlockMutex(g_lock);
}

static void reg_push(const session_reg_event_t *ev) {
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_reg_tail - g_reg_head < EV_QUEUE) g_regq[g_reg_tail++ % EV_QUEUE] = *ev;
    else if (ev->type != REG_EV_PROGRESS) g_regq[(g_reg_tail - 1) % EV_QUEUE] = *ev; /* full: a result wins */
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* ---------- stream stats (counted on libchiaki's threads, logged by session_poll) ---------- */

static struct {
    bool connected; /* counting starts at CHIAKI_EVENT_CONNECTED */
    uint32_t since_ms;
    unsigned frames, frames_lost, recovered, fec_failures, idr_requests;
    uint64_t bytes;
    uint32_t last_frame_ms;    /* arrival of the previous frame */
    unsigned gap_max_ms;       /* the longest wait between two frames arriving */
    unsigned push_max_ms;      /* the longest NDL_DirectVideoPlay call */
    int vbuf_max, abuf_max_ms; /* NDL's render queue (frames) and unplayed audio, -1 unknown */
    unsigned pace_restarts;    /* smoothing: stalls longer than the delay (pacer_t.restarts) */
} g_stats;

/* ---------- media: off libchiaki's receive thread (media/feeder.h) ---------- */

enum { AUDIO_HEADER, AUDIO_FRAME };

static feeder_t *g_video_feed, *g_audio_feed;
static ChiakiOpusDecoder g_opus;
static ChiakiAudioSink g_opus_sink; /* the decoder's own sink, driven from the audio feeder thread */
static uint32_t g_audio_channels = 2;

/* Smooth playback (stream.smooth_ms, read live): the video feeder holds every frame until its
 * slot on an even cadence (media/pacer.h), and the audio feeder keeps the same delay queued so
 * picture and sound stay together and a stall drains the queue instead of NDL running dry. */
static pacer_t g_vpace; /* video feeder thread only */
static double g_frame_ms = 1000.0 / 60;
static SDL_atomic_t g_av_delay_ms; /* the video delay the pacer adds right now, for the audio side */
static bool g_audio_refill = true; /* audio feeder thread: build the queue up before playing */

/* video feeder thread: the still-encoded NAL units go to the TV's hardware decoder */
static void video_feed_cb(int tag, uint8_t *data, size_t len, uint32_t pushed_ms, void *user) {
    (void)tag;
    (void)user;
    int smooth = config_get()->stream.smooth_ms;
    if (smooth > 0) {
        double due = pacer_schedule(&g_vpace, pushed_ms, g_frame_ms, smooth);
        SDL_AtomicSet(&g_av_delay_ms, (int)g_vpace.delay_ms);
        int32_t wait = (int32_t)(due - (double)SDL_GetTicks());
        if (wait > 0) SDL_Delay((Uint32)wait);
    } else if (g_vpace.next_ms > 0) {
        pacer_reset(&g_vpace);
        SDL_AtomicSet(&g_av_delay_ms, 0);
    }
    uint32_t t0 = SDL_GetTicks();
    ndl_push_video(data, len, 0);
    unsigned took = SDL_GetTicks() - t0;
    int queued = ndl_video_buffered();
    if (g_lock) SDL_LockMutex(g_lock);
    if (took > g_stats.push_max_ms) g_stats.push_max_ms = took;
    if (queued > g_stats.vbuf_max) g_stats.vbuf_max = queued;
    g_stats.pace_restarts = g_vpace.restarts;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* audio feeder thread, smoothing on: keep the packets the video delay asks for queued (NDL
 * itself holds NDL_AUDIO_HELD_MS and blocks the feeder when full, which paces it). After NDL ran
 * dry, wait for the queue to refill first; drop a packet when far more than that piled up.
 * false = skip this packet */
#define NDL_AUDIO_HELD_MS   80
#define AUDIO_PACKET_MS     10
#define AUDIO_REFILL_MAX_MS 400
static bool audio_pace(void) {
    int delay = SDL_AtomicGet(&g_av_delay_ms) - NDL_AUDIO_HELD_MS;
    unsigned want = delay > 0 ? (unsigned)(delay / AUDIO_PACKET_MS) : 0;
    if (config_get()->stream.smooth_ms <= 0 || !want) {
        g_audio_refill = true;
        return true;
    }
    int held = ndl_audio_buffered_ms();
    if (held >= 0 && held < AUDIO_PACKET_MS) g_audio_refill = true; /* NDL ran dry: a stall emptied the queue */
    if (g_audio_refill) {
        for (int waited = 0; feeder_depth(g_audio_feed) < want && waited < AUDIO_REFILL_MAX_MS; waited += 2)
            SDL_Delay(2);
        g_audio_refill = false;
        return true;
    }
    return feeder_depth(g_audio_feed) <= want + 6; /* 60 ms over: catch up by one packet */
}

/* audio feeder thread: Opus -> PCM (opus_frame_cb), in packet order */
static void audio_feed_cb(int tag, uint8_t *data, size_t len, uint32_t pushed_ms, void *user) {
    (void)pushed_ms;
    (void)user;
    if (tag == AUDIO_HEADER && len == sizeof(ChiakiAudioHeader)) {
        ChiakiAudioHeader h;
        memcpy(&h, data, sizeof(h));
        g_opus_sink.header_cb(&h, g_opus_sink.user);
    } else if (tag == AUDIO_FRAME) {
        if (!audio_pace()) return;
        g_opus_sink.frame_cb(data, len, g_opus_sink.user);
        int ms = ndl_audio_buffered_ms();
        if (g_lock) SDL_LockMutex(g_lock);
        if (ms > g_stats.abuf_max_ms) g_stats.abuf_max_ms = ms;
        if (g_lock) SDL_UnlockMutex(g_lock);
    }
}

static void opus_settings_cb(uint32_t channels, uint32_t rate, void *user) {
    (void)user;
    g_audio_channels = channels ? channels : 2;
    log_msg("audio: %u channel(s) @ %u Hz", channels, rate);
}

static void opus_frame_cb(int16_t *buf, size_t samples_count, void *user) {
    (void)user;
    ndl_push_audio((const uint8_t *)buf, samples_count * g_audio_channels * sizeof(int16_t), 0);
}

/* the feeders, the decoder and NDL for one session; the libchiaki callbacks below fall back to
 * running the work inline if a feeder could not be started */
static void media_begin(const stream_settings_t *stream) {
    ndl_open(stream); /* best effort: real on the TV, a stub otherwise */
    chiaki_opus_decoder_init(&g_opus, &g_log);
    chiaki_opus_decoder_set_cb(&g_opus, opus_settings_cb, opus_frame_cb, NULL);
    chiaki_opus_decoder_get_sink(&g_opus, &g_opus_sink);
    pacer_reset(&g_vpace);
    g_frame_ms = 1000.0 / (stream->fps > 0 ? stream->fps : 60);
    SDL_AtomicSet(&g_av_delay_ms, 0);
    g_audio_refill = true;
    g_video_feed = feeder_start("chiaki-video", VIDEO_SLOTS, video_feed_cb, NULL);
    g_audio_feed = feeder_start("chiaki-audio", AUDIO_SLOTS, audio_feed_cb, NULL);
    if (!g_video_feed || !g_audio_feed) log_msg("media: feeder thread failed, decoding on the receive thread");
    if (g_lock) SDL_LockMutex(g_lock);
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.vbuf_max = g_stats.abuf_max_ms = -1;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* only once libchiaki's threads are joined: nothing may push any more */
static void media_end(void) {
    feeder_stop(g_video_feed);
    feeder_stop(g_audio_feed);
    g_video_feed = g_audio_feed = NULL;
    ndl_close();
    chiaki_opus_decoder_fini(&g_opus);
    if (g_lock) SDL_LockMutex(g_lock);
    g_stats.connected = false;
    if (g_lock) SDL_UnlockMutex(g_lock);
}

/* libchiaki sizes the stream socket's receive buffer to its 100 KB takion window: a large frame
 * arriving while the receive thread is busy overflows it and the kernel drops the packets */
static void stream_socket_tune(void) {
    chiaki_socket_t s = g_session.stream_connection.takion.sock;
    int before = 0, after = 0, want = STREAM_RCVBUF;
    socklen_t len = sizeof(before);
    getsockopt(s, SOL_SOCKET, SO_RCVBUF, &before, &len);
    if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &want, sizeof(want)) != 0) {
        log_msg("stream: could not enlarge the socket receive buffer (%d bytes)", before);
        return;
    }
    len = sizeof(after);
    getsockopt(s, SOL_SOCKET, SO_RCVBUF, &after, &len);
    log_msg("stream: socket receive buffer %d -> %d bytes", before, after);
}

/* ---------- libchiaki callbacks (their threads: queue only) ---------- */

static bool video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
    (void)user;
    uint32_t now = SDL_GetTicks();
    if (g_lock) SDL_LockMutex(g_lock);
    if (g_stats.last_frame_ms && now - g_stats.last_frame_ms > g_stats.gap_max_ms)
        g_stats.gap_max_ms = now - g_stats.last_frame_ms;
    g_stats.last_frame_ms = now;
    g_stats.frames++;
    g_stats.bytes += buf_size;
    if (frames_lost > 0) g_stats.frames_lost += (unsigned)frames_lost;
    if (frame_recovered) g_stats.recovered++;
    if (g_lock) SDL_UnlockMutex(g_lock);
    if (!g_video_feed) {
        ndl_push_video(buf, buf_size, 0);
        return true;
    }
    /* a full queue refuses the frame; returning false keeps libchiaki from using it as a reference */
    return feeder_push(g_video_feed, 0, buf, buf_size);
}

static void audio_header_cb(ChiakiAudioHeader *header, void *user) {
    (void)user;
    if (g_audio_feed) feeder_push(g_audio_feed, AUDIO_HEADER, header, sizeof(*header));
    else g_opus_sink.header_cb(header, g_opus_sink.user);
}

static void audio_frame_cb(uint8_t *buf, size_t buf_size, void *user) {
    (void)user;
    if (g_audio_feed) feeder_push(g_audio_feed, AUDIO_FRAME, buf, buf_size); /* full: a 10 ms gap */
    else g_opus_sink.frame_cb(buf, buf_size, g_opus_sink.user);
}

static void session_event_cb(ChiakiEvent *event, void *user) {
    (void)user;
    session_event_t ev = { 0 };
    switch (event->type) {
        case CHIAKI_EVENT_CONNECTED:
            stream_socket_tune(); /* the session thread, before the video starts */
            if (g_lock) SDL_LockMutex(g_lock);
            g_stats.connected = true;
            if (g_lock) SDL_UnlockMutex(g_lock);
            ev.type = SESSION_EV_CONNECTED;
            ev.progress = 100;
            break;
        case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
            if (g_lock) SDL_LockMutex(g_lock);
            g_stats.fec_failures++;
            if (event->video_fec_failure.idr_request_sent) g_stats.idr_requests++;
            if (g_lock) SDL_UnlockMutex(g_lock);
            return;
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST: ev.type = SESSION_EV_LOGIN_PIN; break;
        case CHIAKI_EVENT_RUMBLE:
            ev.type = SESSION_EV_RUMBLE;
            ev.rumble_left = event->rumble.left;
            ev.rumble_right = event->rumble.right;
            break;
        case CHIAKI_EVENT_QUIT:
            ev.type = SESSION_EV_QUIT;
            snprintf(ev.reason, sizeof(ev.reason), "%s",
                     event->quit.reason_str ? event->quit.reason_str : chiaki_quit_reason_string(event->quit.reason));
            /* the session request is the first contact: these two mean it never got an answer
             * (connect refused, timed out, or no reply), not that the console said no */
            ev.unreachable = !g_session.holepunch_session &&
                             (event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED ||
                              event->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN);
            break;
        default: return; /* keyboard/dualsense/etc. events are not used by the UI yet */
    }
    sess_push(&ev);
}

static void regist_cb(ChiakiRegistEvent *event, void *user) {
    (void)user;
    session_reg_event_t ev = { .op = g_regist_gen, .from_regist = true };
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
        case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED: ev.type = REG_EV_CANCELED; break;
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

/* the video profile both the LAN and the PSN path build */
static void fill_video_profile(ChiakiConnectInfo *info, const config_host_t *host, const stream_settings_t *stream) {
    ChiakiVideoResolutionPreset res =
        stream->width <= 1280 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p : CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
    chiaki_connect_video_profile_preset(&info->video_profile, res,
                                        stream->fps >= 60 ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30);
    info->video_profile.bitrate = (unsigned int)stream->bitrate_kbps;
    /* a PS4 does not stream H.265 (chiaki-ng always decodes PS4 as H.264); only a PS5 takes the setting.
     * HDR is negotiated when the HDR switch is on or the codec is H.265 HDR (both mean HDR10 on a PS5). */
    ChiakiCodec codec;
    if (!host->is_ps5) codec = CHIAKI_CODEC_H264;
    else if (stream->hdr || stream->codec == CODEC_H265_HDR) codec = CHIAKI_CODEC_H265_HDR;
    else codec = (ChiakiCodec)stream->codec;
    info->video_profile.codec = codec;
}

/* libchiaki startup shared by both paths: media, callbacks + chiaki_session_start */
static bool session_begin(const stream_settings_t *stream) {
    media_begin(stream);
    ChiakiAudioSink sink = { .user = NULL, .header_cb = audio_header_cb, .frame_cb = audio_frame_cb };
    chiaki_session_set_audio_sink(&g_session, &sink);
    chiaki_session_set_event_cb(&g_session, session_event_cb, NULL);
    chiaki_session_set_video_sample_cb(&g_session, video_sample_cb, NULL);
    ChiakiErrorCode err = chiaki_session_start(&g_session);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "NET", "chiaki_session_start failed: %s", chiaki_error_string(err));
        chiaki_session_fini(&g_session);
        media_end();
        return false;
    }
    return true;
}

static bool session_start_lan(const config_host_t *host, const stream_settings_t *stream) {
    memset(&g_session, 0, sizeof(g_session));

    ChiakiConnectInfo info = { 0 };
    info.ps5 = host->is_ps5;
    info.host = host->addr;
    memcpy(info.regist_key, host->rp_regist_key, sizeof(info.regist_key)); /* 16, \0-padded */
    memcpy(info.morning, host->rp_key, sizeof(info.morning));              /* the console's rp_key, as chiaki-ng does */
    info.video_profile_auto_downgrade = true;
    info.enable_idr_on_fec_failure = true;
    /* TODO(input): enable_dualsense needs a haptics sink (chiaki_session_set_haptics_sink) to be wired first */
    fill_video_profile(&info, host, stream);
    /* Wi-Fi only: let congestion control trim the bitrate rather than never seeing loss (chiaki-ng default) */
    info.packet_loss_max = 0.05;
    if (config_psn_has_account_id()) memcpy(info.psn_account_id, config_psn()->account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);

    ChiakiErrorCode err = chiaki_session_init(&g_session, &info, &g_log);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "NET", "chiaki_session_init failed: %s", chiaki_error_string(err));
        return false;
    }
    if (!session_begin(stream)) return false;

    g_host = *host;
    g_host.nickname[sizeof(g_host.nickname) - 1] = '\0';
    g_host.addr[sizeof(g_host.addr) - 1] = '\0';
    g_session_active = true;
    log_msg("session: started with %s (%s)", g_host.nickname, g_host.addr);
    return true;
}

/* ---------- PSN connect (holepunch) ---------- */

static void conn_progress(int p) {
    session_event_t ev = { .type = SESSION_EV_PROGRESS, .progress = p };
    sess_push(&ev);
}

static void conn_failf(const char *fmt, ...) {
    session_event_t ev = { .type = SESSION_EV_QUIT };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev.reason, sizeof(ev.reason), fmt, ap);
    va_end(ap);
    sess_push(&ev);
}

static bool psn_cancelled(void) {
    bool c = false;
    if (g_lock) SDL_LockMutex(g_lock);
    c = g_psn_cancel;
    if (g_lock) SDL_UnlockMutex(g_lock);
    return c;
}

typedef struct {
    config_host_t host;
    stream_settings_t stream;
    uint8_t psn_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];
    char token[256];
    char nickname[64];
} psn_conn_job;

/* The holepunch handshake is the blocking part (UPnP, PSN session create/start, STUN), so it
 * runs here on its own thread; libchiaki then runs the PSN regist and the data holepunch on
 * its session thread as usual. Mirror of chiaki-ng StreamSession::Initiate/ConnectPsnConnection. */
static int psn_conn_thread(void *data) {
    psn_conn_job *job = data;
    ChiakiHolepunchSession hp = NULL;
    ChiakiErrorCode err;
    bool announced = false;

    conn_progress(5);
    hp = chiaki_holepunch_session_init(job->token, &g_log);
    if (!hp) {
        conn_failf("Could not create the PSN session");
        goto done;
    }
    chiaki_holepunch_session_force_port_guessing(hp, true);
    if (psn_cancelled()) goto cleanup;
    if (chiaki_holepunch_upnp_discover(hp) != CHIAKI_ERR_SUCCESS) log_msg("psn: UPnP discovery failed, continuing");
    if (psn_cancelled()) goto cleanup;

    conn_progress(20);
    err = chiaki_holepunch_session_create(hp);
    if (err != CHIAKI_ERR_SUCCESS) {
        conn_failf("PSN could not create the session (%s)", chiaki_error_string(err));
        goto cleanup;
    }
    err = holepunch_session_create_offer(hp);
    if (err != CHIAKI_ERR_SUCCESS) {
        conn_failf("PSN could not create the connection offer");
        goto cleanup;
    }
    const ChiakiHolepunchConsoleType type =
        job->host.is_ps5 ? CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 : CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4;
    err = chiaki_holepunch_session_start(hp, job->host.psn_uid, type);
    if (err != CHIAKI_ERR_SUCCESS) {
        conn_failf("PSN could not start the session for this console (%s)", chiaki_error_string(err));
        goto cleanup;
    }
    conn_progress(40);
    err = chiaki_holepunch_session_punch_hole(hp, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);
    if (err != CHIAKI_ERR_SUCCESS) {
        conn_failf("Could not reach the console over PSN (%s)", chiaki_error_string(err));
        goto cleanup;
    }
    if (psn_cancelled()) goto cleanup;
    conn_progress(60);

    ChiakiConnectInfo info = { 0 };
    info.ps5 = job->host.is_ps5;
    info.holepunch_session = hp;
    info.video_profile_auto_downgrade = true;
    info.enable_idr_on_fec_failure = true;
    fill_video_profile(&info, &job->host, &job->stream);
    info.packet_loss_max = 0.05;
    memcpy(info.psn_account_id, job->psn_id, sizeof(info.psn_account_id));

    memset(&g_session, 0, sizeof(g_session));
    err = chiaki_session_init(&g_session, &info, &g_log);
    hp = NULL; /* the session owns it now; chiaki_session_fini() frees it (init's error path too) */
    if (err != CHIAKI_ERR_SUCCESS) {
        conn_failf("Could not initialise the session (%s)", chiaki_error_string(err));
        goto done;
    }
    if (!session_begin(&job->stream)) {
        conn_failf("The console did not start the session");
        goto done;
    }

    g_host = job->host;
    g_host.nickname[sizeof(g_host.nickname) - 1] = '\0';
    g_host.addr[sizeof(g_host.addr) - 1] = '\0';
    if (g_lock) SDL_LockMutex(g_lock);
    g_session_active = true;
    g_psn_connect_active = false;
    bool cancelled = g_psn_cancel;
    if (g_lock) SDL_UnlockMutex(g_lock);
    announced = true;
    if (cancelled) {
        chiaki_session_stop(&g_session);
        chiaki_session_join(&g_session);
        chiaki_session_fini(&g_session);
        media_end();
        g_session_active = false;
        log_msg("session: PSN connect cancelled");
    } else {
        log_msg("session: PSN session started with %s", g_host.nickname);
    }
    goto done;

cleanup:
    if (hp) chiaki_holepunch_session_fini(hp);
done:
    if (!announced) {
        if (g_lock) SDL_LockMutex(g_lock);
        g_psn_connect_active = false;
        if (g_lock) SDL_UnlockMutex(g_lock);
    }
    free(job);
    return 0;
}

static bool session_start_psn(const config_host_t *host, const stream_settings_t *stream) {
    if (!config_psn_has_account_id()) {
        log_event(LOG_ERR, "NET", "Sign in to PSN before connecting to %s", host->nickname);
        return false;
    }
    psn_ensure_token(); /* refresh an expired access token before the holepunch handshake */
    const config_psn_t *psn = config_psn();
    if (!psn->access_token[0]) {
        log_event(LOG_ERR, "NET", "Sign in to PSN again to connect to %s", host->nickname);
        return false;
    }
    psn_conn_job *job = malloc(sizeof(*job));
    if (!job) return false;
    memset(job, 0, sizeof(*job));
    job->host = *host;
    job->stream = *stream;
    memcpy(job->psn_id, psn->account_id, sizeof(job->psn_id));
    snprintf(job->token, sizeof(job->token), "%s", psn->access_token);
    snprintf(job->nickname, sizeof(job->nickname), "%s", host->nickname);

    g_psn_cancel = false;
    g_psn_connect_active = true;
    SDL_Thread *t = SDL_CreateThread(psn_conn_thread, "psn-connect", job);
    if (!t) {
        g_psn_connect_active = false;
        free(job);
        return false;
    }
    SDL_DetachThread(t);
    log_msg("session: PSN connect started for %s", host->nickname);
    return true;
}

bool session_start(const config_host_t *host, const stream_settings_t *stream) {
    lib_init();
    if (g_session_active || g_psn_connect_active || !host || !stream) return false;
    if (host->via_psn && !host->addr[0]) return session_start_psn(host, stream);
    return session_start_lan(host, stream);
}

void session_stop(void) {
    if (g_psn_connect_active) { /* the worker cleans itself up and stops the session if it got one */
        g_psn_cancel = true;
        return;
    }
    if (!g_session_active) return;
    chiaki_session_stop(&g_session);
    chiaki_session_join(&g_session);
    chiaki_session_fini(&g_session);
    media_end();
    log_msg("session: stopped with %s", g_host.nickname);
    g_session_active = false;
}

/* the kernel's drop counter for the stream socket (the last column of /proc/net/udp[6]): packets
 * that arrived while its receive buffer was full. -1 where there is no such file (macOS) */
static long stream_socket_drops(void) {
    struct sockaddr_storage a;
    socklen_t len = sizeof(a);
    if (getsockname(g_session.stream_connection.takion.sock, (struct sockaddr *)&a, &len) != 0) return -1;
    unsigned port = a.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&a)->sin6_port)
                                            : ntohs(((struct sockaddr_in *)&a)->sin_port);
    FILE *f = fopen(a.ss_family == AF_INET6 ? "/proc/net/udp6" : "/proc/net/udp", "r");
    if (!f) return -1;
    char line[512];
    long drops = -1;
    while (drops < 0 && fgets(line, sizeof(line), f)) {
        unsigned lport = 0;
        if (sscanf(line, " %*d: %*[0-9A-Fa-f]:%x", &lport) != 1 || lport != port) continue;
        size_t n = strlen(line);
        while (n && isspace((unsigned char)line[n - 1])) line[--n] = '\0';
        const char *last = strrchr(line, ' ');
        if (last) drops = strtol(last + 1, NULL, 10);
    }
    fclose(f);
    return drops;
}

/* main thread: one line every STATS_MS while streaming */
static void stats_tick(void) {
    static long sock_drops_prev = -1; /* the socket's drop counter at the previous line */
    if (!g_session_active || !g_lock) return;
    uint32_t now = SDL_GetTicks();
    SDL_LockMutex(g_lock);
    if (!g_stats.connected || !g_stats.since_ms) {
        bool start = g_stats.connected;
        if (start) g_stats.since_ms = now ? now : 1;
        SDL_UnlockMutex(g_lock);
        if (start) sock_drops_prev = stream_socket_drops();
        return;
    }
    uint32_t dt = now - g_stats.since_ms;
    if (dt < STATS_MS) {
        SDL_UnlockMutex(g_lock);
        return;
    }
    unsigned frames = g_stats.frames, lost = g_stats.frames_lost, rec = g_stats.recovered;
    unsigned fec = g_stats.fec_failures, idr = g_stats.idr_requests;
    uint64_t bytes = g_stats.bytes;
    unsigned gap = g_stats.gap_max_ms, push = g_stats.push_max_ms;
    int vbuf = g_stats.vbuf_max, abuf = g_stats.abuf_max_ms;
    unsigned restarts = g_stats.pace_restarts;
    g_stats.frames = g_stats.frames_lost = g_stats.recovered = g_stats.fec_failures = g_stats.idr_requests = 0;
    g_stats.bytes = 0;
    g_stats.gap_max_ms = g_stats.push_max_ms = 0;
    g_stats.vbuf_max = g_stats.abuf_max_ms = -1;
    g_stats.since_ms = now;
    SDL_UnlockMutex(g_lock);

    long drops = stream_socket_drops();
    char sock[64] = "socket drops n/a";
    if (drops >= 0 && sock_drops_prev >= 0)
        snprintf(sock, sizeof(sock), "socket drops %ld (total %ld)", drops - sock_drops_prev, drops);
    else if (drops >= 0) snprintf(sock, sizeof(sock), "socket drops total %ld", drops);
    sock_drops_prev = drops;
    feeder_stats_t vs, as;
    feeder_stats(g_video_feed, &vs, true);
    feeder_stats(g_audio_feed, &as, true);
    double secs = dt / 1000.0;
    log_msg("stats: %.1f s, %.1f fps, %.1f Mbps, %u frame(s) lost, %u FEC failure(s), %u IDR request(s), "
            "%u recovered, %s",
            secs, frames / secs, (double)bytes * 8.0 / secs / 1e6, lost, fec, idr, rec, sock);
    log_msg("stats: frame gap max %u ms, NDL video push max %u ms, NDL render queue max %d, NDL audio max %d ms, "
            "queue max video %u/%u audio %u/%u, queue drops %u/%u",
            gap, push, vbuf, abuf, vs.depth_max, feeder_slots(g_video_feed), as.depth_max, feeder_slots(g_audio_feed),
            vs.dropped, as.dropped);
    int smooth = config_get()->stream.smooth_ms;
    if (smooth > 0)
        log_msg("stats: smoothing up to %d ms, adding %d ms now, %u restart(s) so far (a stall longer than the delay)",
                smooth, SDL_AtomicGet(&g_av_delay_ms), restarts);
}

bool session_poll(session_event_t *out) {
    lock_init();
    stats_tick();
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

/* the session layer speaks SESSION_PAD_BTN_*; libchiaki wants ChiakiControllerButton */
static uint32_t chiaki_buttons(uint32_t b) {
    uint32_t out = 0;
    if (b & SESSION_PAD_BTN_CROSS) out |= CHIAKI_CONTROLLER_BUTTON_CROSS;
    if (b & SESSION_PAD_BTN_CIRCLE) out |= CHIAKI_CONTROLLER_BUTTON_MOON;
    if (b & SESSION_PAD_BTN_SQUARE) out |= CHIAKI_CONTROLLER_BUTTON_BOX;
    if (b & SESSION_PAD_BTN_TRIANGLE) out |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
    if (b & SESSION_PAD_BTN_DPAD_LEFT) out |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
    if (b & SESSION_PAD_BTN_DPAD_RIGHT) out |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
    if (b & SESSION_PAD_BTN_DPAD_UP) out |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
    if (b & SESSION_PAD_BTN_DPAD_DOWN) out |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
    if (b & SESSION_PAD_BTN_L1) out |= CHIAKI_CONTROLLER_BUTTON_L1;
    if (b & SESSION_PAD_BTN_R1) out |= CHIAKI_CONTROLLER_BUTTON_R1;
    if (b & SESSION_PAD_BTN_L3) out |= CHIAKI_CONTROLLER_BUTTON_L3;
    if (b & SESSION_PAD_BTN_R3) out |= CHIAKI_CONTROLLER_BUTTON_R3;
    if (b & SESSION_PAD_BTN_OPTIONS) out |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
    if (b & SESSION_PAD_BTN_SHARE) out |= CHIAKI_CONTROLLER_BUTTON_SHARE;
    if (b & SESSION_PAD_BTN_TOUCHPAD) out |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
    if (b & SESSION_PAD_BTN_PS) out |= CHIAKI_CONTROLLER_BUTTON_PS;
    return out;
}

void session_set_pad(const session_pad_t *pad) {
    if (!pad) return;
    g_last_pad = *pad;
    if (!g_session_active) return;
    ChiakiControllerState st;
    chiaki_controller_state_set_idle(&st);
    st.buttons = chiaki_buttons(pad->buttons);
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

const session_pad_t *session_last_pad(void) { return &g_last_pad; }

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
    ChiakiErrorCode err =
        chiaki_discovery_wakeup(&g_log, NULL, host->addr, regist_credential(host->rp_regist_key), host->is_ps5);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_WARN, "HOST", "Wake-up failed: %s", chiaki_error_string(err));
        return false;
    }
    log_msg("chiaki: wake-up packet sent to %s", host->addr);
    return true;
}

/* ---------- discovery ---------- */

#define DISC_PING_MS   500 /* chiaki-ng gui/src/discoverymanager.cpp: PING_MS, HOSTS_MAX, DROP_PINGS */
#define DISC_DROP      3
#define DISC_BCAST_MAX 8

static ChiakiDiscoveryService g_disc;
static bool g_disc_on;
static session_found_t g_found[SESSION_FOUND_MAX];
static int g_found_n;
static unsigned g_found_gen;

static void copy_str(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src ? src : ""); }

/* libchiaki's discovery thread: the whole list, whenever a console appears, changes or goes */
static void disc_cb(ChiakiDiscoveryHost *hosts, size_t count, void *user) {
    (void)user;
    session_found_t list[SESSION_FOUND_MAX];
    int n = 0;
    for (size_t i = 0; i < count && n < SESSION_FOUND_MAX; i++) {
        ChiakiDiscoveryHost *h = &hosts[i];
        if (h->state == CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN || !h->host_addr) continue;
        session_found_t *f = &list[n++];
        copy_str(f->addr, sizeof(f->addr), h->host_addr);
        copy_str(f->host_id, sizeof(f->host_id), h->host_id);
        copy_str(f->name, sizeof(f->name), h->host_name);
        copy_str(f->system_version, sizeof(f->system_version), h->system_version);
        copy_str(f->app_name, sizeof(f->app_name), h->running_app_name);
        f->ps5 = chiaki_discovery_host_is_ps5(h);
        f->ready = h->state == CHIAKI_DISCOVERY_HOST_STATE_READY;
        log_probe("discovery: %s %s at %s, id %s, %s, firmware %s, protocol %s, port %u, app \"%s\" (%s)",
                  f->ps5 ? "PS5" : "PS4", f->name, f->addr, f->host_id, f->ready ? "ready" : "standby",
                  f->system_version, h->device_discovery_protocol_version ? h->device_discovery_protocol_version : "",
                  (unsigned)h->host_request_port, f->app_name, h->running_app_titleid ? h->running_app_titleid : "");
    }
    SDL_LockMutex(g_lock);
    memcpy(g_found, list, sizeof(list[0]) * (size_t)n);
    g_found_n = n;
    g_found_gen++;
    SDL_UnlockMutex(g_lock);
}

/* every IPv4 interface's broadcast address: a console answers 255.255.255.255 only on some setups */
static int broadcast_addrs(struct sockaddr_storage *out, int max) {
    struct ifaddrs *list;
    int n = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (struct ifaddrs *it = list; it && n < max; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || (it->ifa_flags & IFF_LOOPBACK) ||
            !(it->ifa_flags & IFF_BROADCAST) || !it->ifa_broadaddr)
            continue;
        struct sockaddr_in b;
        memcpy(&b, it->ifa_broadaddr, sizeof(b));
        if (b.sin_addr.s_addr == 0xffffffff || !b.sin_addr.s_addr) continue;
        memset(&out[n], 0, sizeof(out[n]));
        memcpy(&out[n], &b, sizeof(b));
        char a[INET_ADDRSTRLEN];
        log_probe("discovery: broadcast on %s to %s", it->ifa_name, inet_ntop(AF_INET, &b.sin_addr, a, sizeof(a)));
        n++;
    }
    freeifaddrs(list);
    return n;
}

bool session_discovery_start(void) {
    lib_init();
    if (g_disc_on) return true;
    struct sockaddr_storage send = { 0 };
    struct sockaddr_in all = { 0 };
    all.sin_family = AF_INET;
    all.sin_addr.s_addr = 0xffffffff; /* 255.255.255.255, then the interfaces' own broadcast */
    memcpy(&send, &all, sizeof(all));
    struct sockaddr_storage bcast[DISC_BCAST_MAX];
    ChiakiDiscoveryServiceOptions o = { 0 };
    o.ping_ms = DISC_PING_MS;
    o.ping_initial_ms = DISC_PING_MS;
    o.hosts_max = SESSION_FOUND_MAX;
    o.host_drop_pings = DISC_DROP;
    o.send_addr = &send;
    o.send_addr_size = sizeof(all);
    o.broadcast_num = (size_t)broadcast_addrs(bcast, DISC_BCAST_MAX);
    o.broadcast_addrs = o.broadcast_num ? bcast : NULL;
    o.cb = disc_cb;
    ChiakiErrorCode err = chiaki_discovery_service_init(&g_disc, &o, &g_log);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_msg("chiaki: discovery could not start: %s", chiaki_error_string(err));
        return false;
    }
    g_disc_on = true;
    log_msg("chiaki: discovery started (%d broadcast address%s)", (int)o.broadcast_num,
            o.broadcast_num == 1 ? "" : "es");
    return true;
}

void session_discovery_stop(void) {
    if (!g_disc_on) return;
    chiaki_discovery_service_fini(&g_disc); /* joins its thread: no callback after this */
    g_disc_on = false;
    SDL_LockMutex(g_lock);
    g_found_n = 0;
    g_found_gen++;
    SDL_UnlockMutex(g_lock);
    log_msg("chiaki: discovery stopped");
}

int session_discovery_list(session_found_t *out, int max, unsigned *gen) {
    lock_init();
    SDL_LockMutex(g_lock);
    int n = g_found_n < max ? g_found_n : max;
    if (n > 0) memcpy(out, g_found, sizeof(out[0]) * (size_t)n);
    if (gen) *gen = g_found_gen;
    SDL_UnlockMutex(g_lock);
    return n;
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
    if (config_psn_has_account_id()) memcpy(info.psn_account_id, config_psn()->account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);

    g_regist_gen = ++g_reg_gen;
    ChiakiErrorCode err = chiaki_regist_start(&g_regist, &g_log, &info, regist_cb, NULL);
    if (err != CHIAKI_ERR_SUCCESS) {
        log_event(LOG_ERR, "REG", "chiaki_regist_start failed: %s", chiaki_error_string(err));
        return false;
    }
    g_regist_active = true;
    log_msg("reg: registering with %s (PIN)", g_regist_addr);
    return true;
}

/* ---------- PSN account: OAuth token exchange + the attached-device list ---------- */

struct http_buf {
    char *data;
    size_t len;
};

static size_t http_write_cb(char *ptr, size_t size, size_t nmemb, void *user) {
    struct http_buf *b = user;
    size_t n = size * nmemb;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

static void http_buf_free(struct http_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
}

/* POST a form body to the PSN token endpoint (Basic auth with the app's client id/secret).
 * Returns the HTTP status, or -1 on a transfer error; both are logged (never the body/tokens). */
static long psn_post_form(const char *body, struct http_buf *out) {
    CURL *h = curl_easy_init();
    if (!h) return -1;
    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(h, CURLOPT_URL, PSN_TOKEN_URL);
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_USERPWD, PSN_CLIENT_ID ":" PSN_CLIENT_SECRET);
    curl_easy_setopt(h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        log_msg("psn: token request failed: %s", curl_easy_strerror(rc));
        return -1;
    }
    log_msg("psn: token request HTTP %ld", status);
    return status;
}

/* GET the account info (user_id + online_id); the access token is part of the URL. -1 on transfer error */
static long psn_get_account(const char *access_token, struct http_buf *out) {
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", PSN_TOKEN_URL, access_token);
    CURL *h = curl_easy_init();
    if (!h) return -1;
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERPWD, PSN_CLIENT_ID ":" PSN_CLIENT_SECRET);
    curl_easy_setopt(h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        log_msg("psn: account request failed: %s", curl_easy_strerror(rc));
        return -1;
    }
    log_msg("psn: account request HTTP %ld", status);
    return status;
}

static bool json_string(json_object *o, const char *key, char *out, size_t cap) {
    json_object *v = NULL;
    if (!o || !json_object_object_get_ex(o, key, &v)) return false;
    const char *s = json_object_get_string(v);
    if (!s) return false;
    snprintf(out, cap, "%s", s);
    return true;
}

static int json_int(json_object *o, const char *key, int dflt) {
    json_object *v = NULL;
    if (!o || !json_object_object_get_ex(o, key, &v)) return dflt;
    return json_object_get_int(v);
}

/* ---------- PSN access token refresh (grant_type=refresh_token) ---------- */

/* Exchange the refresh token for a fresh access token and store it (chiaki-ng PSNToken::RefreshPsnToken). */
static bool psn_token_refresh(void) {
    const config_psn_t *p = config_psn();
    char refresh[256];
    snprintf(refresh, sizeof(refresh), "%s", p->refresh_token);
    uint8_t account_id[8];
    memcpy(account_id, p->account_id, sizeof(account_id));
    char online_id[32];
    snprintf(online_id, sizeof(online_id), "%s", p->online_id);
    if (!refresh[0]) return false;

    char *rt = curl_easy_escape(NULL, refresh, 0);
    char *sc = curl_easy_escape(NULL, PSN_SCOPE_PLAIN, 0);
    char body[1024];
    snprintf(body, sizeof(body), "grant_type=refresh_token&refresh_token=%s&scope=%s&redirect_uri=%s&", rt ? rt : "",
             sc ? sc : "", PSN_REDIRECT);
    curl_free(rt);
    curl_free(sc);

    struct http_buf tok = { 0 };
    long st = psn_post_form(body, &tok);
    bool ok = false;
    if (st == 200) {
        json_object *j = tok.data ? json_tokener_parse(tok.data) : NULL;
        char access[256] = "", new_refresh[256] = "";
        if (j && json_string(j, "access_token", access, sizeof(access))) {
            json_string(j, "refresh_token", new_refresh, sizeof(new_refresh));
            int expires = json_int(j, "expires_in", 3600);
            config_psn_set(account_id, online_id, access, new_refresh[0] ? new_refresh : refresh);
            config_psn_set_expiry((long long)time(NULL) + (expires > 0 ? expires : 3600));
            config_save();
            ok = true;
            log_msg("psn: access token refreshed");
        }
        if (j) json_object_put(j);
    } else {
        log_msg("psn: token refresh failed (HTTP %ld)", st);
    }
    http_buf_free(&tok);
    return ok;
}

/* Make sure the stored access token is usable, refreshing when it has expired or its age is
 * unknown. Returns false only when there is no token at all. */
static bool psn_ensure_token(void) {
    const config_psn_t *p = config_psn();
    if (!p->access_token[0]) return false;
    if (p->expires_at && (long long)time(NULL) + 60 < p->expires_at) return true; /* still valid */
    if (psn_token_refresh()) return true;
    return p->access_token[0] != '\0'; /* refresh failed: fall back to the stored token */
}

static void reg_push_failf(unsigned op, const char *fmt, ...) {
    session_reg_event_t ev = { .type = REG_EV_FAILED, .op = op };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev.reason, sizeof(ev.reason), fmt, ap);
    va_end(ap);
    reg_push(&ev);
}

typedef struct {
    unsigned op;
    char code[SESSION_PSN_CODE_MAX];
} psn_code_job;

/* network, so it runs on its own thread: exchange the code, read the account, queue the result */
static int psn_code_thread(void *data) {
    psn_code_job *job = data;
    unsigned op = job->op;
    struct http_buf tok = { 0 }, acc = { 0 };
    char *code_esc = curl_easy_escape(NULL, job->code, 0);
    char *scope_esc = curl_easy_escape(NULL, PSN_SCOPE_PLAIN, 0);
    char body[SESSION_PSN_CODE_MAX * 3 + 512];
    snprintf(body, sizeof(body), "grant_type=authorization_code&code=%s&scope=%s&redirect_uri=%s&",
             code_esc ? code_esc : "", scope_esc ? scope_esc : "", PSN_REDIRECT);
    curl_free(code_esc);
    curl_free(scope_esc);

    long st = psn_post_form(body, &tok);
    if (st != 200) {
        if (st < 0) reg_push_failf(op, "Could not reach the PSN sign-in service");
        else if (st == 400 || st == 401)
            reg_push_failf(op, "PSN did not accept the code (it expires quickly; sign in again)");
        else reg_push_failf(op, "PSN sign-in failed (HTTP %ld)", st);
        goto out;
    }
    json_object *jt = tok.data ? json_tokener_parse(tok.data) : NULL;
    char access[256] = "", refresh[256] = "";
    if (!jt || !json_string(jt, "access_token", access, sizeof(access))) {
        if (jt) json_object_put(jt);
        reg_push_failf(op, "PSN did not return a token");
        goto out;
    }
    json_string(jt, "refresh_token", refresh, sizeof(refresh));
    int expires_in = json_int(jt, "expires_in", 3600);
    json_object_put(jt);

    st = psn_get_account(access, &acc);
    if (st != 200) {
        if (st < 0) reg_push_failf(op, "Could not reach the PSN account service");
        else reg_push_failf(op, "PSN account lookup failed (HTTP %ld)", st);
        goto out;
    }
    json_object *ja = acc.data ? json_tokener_parse(acc.data) : NULL;
    char user_id[32] = "", online_id[32] = "";
    bool have_id = ja && json_string(ja, "user_id", user_id, sizeof(user_id));
    if (have_id) json_string(ja, "online_id", online_id, sizeof(online_id));
    if (ja) json_object_put(ja);
    char *end = NULL;
    unsigned long long uid = have_id ? strtoull(user_id, &end, 10) : 0;
    if (!have_id || !user_id[0] || (end && *end) || uid == 0) {
        reg_push_failf(op, "PSN did not return the account id");
        goto out;
    }
    /* user_id is a decimal number; the account id is its 8 little-endian bytes (chiaki-ng psnaccountid.cpp) */
    uint8_t id[8];
    for (int i = 0; i < 8; i++) id[i] = (uint8_t)(uid >> (8 * i));

    session_reg_event_t ev = { .type = REG_EV_PSN_LINKED, .op = op };
    memcpy(ev.psn.account_id, id, sizeof(id));
    ev.psn.linked = true;
    snprintf(ev.psn.online_id, sizeof(ev.psn.online_id), "%s", online_id);
    snprintf(ev.psn.access_token, sizeof(ev.psn.access_token), "%s", access);
    snprintf(ev.psn.refresh_token, sizeof(ev.psn.refresh_token), "%s", refresh);
    ev.psn.expires_at = (long long)time(NULL) + (expires_in > 0 ? expires_in : 3600);
    reg_push(&ev);

out:
    http_buf_free(&tok);
    http_buf_free(&acc);
    free(job);
    return 0;
}

bool session_reg_submit_code(const char *code) {
    lib_init();
    if (!code || !*code) return false;
    psn_code_job *job = malloc(sizeof(*job));
    if (!job) return false;
    job->op = ++g_reg_gen;
    snprintf(job->code, sizeof(job->code), "%s", code);
    SDL_Thread *t = SDL_CreateThread(psn_code_thread, "psn-code", job);
    if (!t) {
        free(job);
        return false;
    }
    SDL_DetachThread(t);
    return true;
}

/* the consoles attached to the PSN account (chiaki_holepunch_list_devices); network, so on a thread */
typedef struct {
    unsigned op;
    char token[256];
} psn_dev_job;

static int psn_devices_thread(void *data) {
    psn_dev_job *job = data;
    unsigned op = job->op;
    session_reg_event_t ev = { .type = REG_EV_PSN_DEVICES, .op = op };
    const ChiakiHolepunchConsoleType types[2] = { CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5,
                                                  CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4 };
    int n = 0;
    bool failed = false;
    for (int ti = 0; ti < 2 && n < SESSION_PSN_DEVICES_MAX; ti++) {
        ChiakiHolepunchDeviceInfo *devs = NULL;
        size_t count = 0;
        ChiakiErrorCode err = chiaki_holepunch_list_devices(job->token, types[ti], &devs, &count, &g_log);
        if (err != CHIAKI_ERR_SUCCESS) {
            log_msg("psn: list_devices(%d) failed: %s", (int)types[ti], chiaki_error_string(err));
            failed = true;
            continue;
        }
        for (size_t i = 0; i < count && n < SESSION_PSN_DEVICES_MAX; i++) {
            if (!devs[i].remoteplay_enabled) continue;
            snprintf(ev.devices[n].name, sizeof(ev.devices[n].name), "%s", devs[i].device_name);
            memcpy(ev.devices[n].uid, devs[i].device_uid, sizeof(ev.devices[n].uid));
            ev.devices[n].ps5 = types[ti] == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5;
            n++;
        }
        if (devs) chiaki_holepunch_free_device_list(&devs);
    }
    free(job);
    ev.device_count = n;
    if (n == 0) {
        if (failed) reg_push_failf(op, "Could not list your consoles (sign in again?)");
        else reg_push_failf(op, "No consoles with Remote Play on this PSN account");
        return 0;
    }
    reg_push(&ev);
    return 0;
}

bool session_psn_fetch_devices(void) {
    lib_init();
    if (g_psn_fetching || !config_psn_has_account_id() || !config_psn()->access_token[0]) return false;
    psn_ensure_token(); /* refresh an expired access token before listing */
    psn_dev_job *job = malloc(sizeof(*job));
    if (!job) return false;
    job->op = ++g_reg_gen;
    snprintf(job->token, sizeof(job->token), "%s", config_psn()->access_token);
    SDL_Thread *t = SDL_CreateThread(psn_devices_thread, "psn-devices", job);
    if (!t) {
        free(job);
        return false;
    }
    g_psn_fetching = true;
    g_fetch_gen = job->op;
    SDL_DetachThread(t);
    return true;
}

bool session_reg_begin_psn(const uint8_t account_id[8], bool ps5) {
    (void)account_id;
    (void)ps5;
    /* PSN consoles are registered automatically inside a PSN connect (the library runs
     * chiaki_regist over the holepunch control socket and then streams); a separate
     * registration step is not needed. Listing devices is session_psn_fetch_devices(). */
    log_event(LOG_ERR, "REG", "PSN consoles register during the PSN connection");
    return false;
}

void session_reg_stop(void) {
    g_reg_gen++; /* invalidate any in-flight PSN worker's queued result */
    g_psn_fetching = false;
    if (!g_regist_active) return;
    chiaki_regist_stop(&g_regist);
    chiaki_regist_fini(&g_regist);
    g_regist_active = false;
}

bool session_reg_poll(session_reg_event_t *out) {
    lock_init();
    bool got = false, reap = false;
    session_reg_event_t ev = { 0 };
    if (g_lock) SDL_LockMutex(g_lock);
    while (g_reg_head != g_reg_tail) {
        ev = g_regq[g_reg_head++ % EV_QUEUE];
        /* a terminal chiaki_regist event means its thread is done: reap it even if the result is stale */
        if (ev.from_regist && ev.op == g_regist_gen &&
            (ev.type == REG_EV_SUCCESS || ev.type == REG_EV_FAILED || ev.type == REG_EV_CANCELED))
            reap = true;
        if (ev.op == g_reg_gen) { /* deliver only the current operation's events */
            *out = ev;
            got = true;
            break;
        }
        if (ev.op == g_fetch_gen) g_psn_fetching = false;
    }
    if (g_lock) SDL_UnlockMutex(g_lock);
    if (reap && g_regist_active) {
        chiaki_regist_fini(&g_regist);
        g_regist_active = false;
    }
    if (got && ev.op == g_fetch_gen) g_psn_fetching = false;
    return got;
}

const char *session_reg_psn_url(void) {
    static char url[sizeof(PSN_AUTH_URL) + CONFIG_DUID_LEN + 8];
    snprintf(url, sizeof(url), "%sduid=%s&", PSN_AUTH_URL, config_device_uid());
    return url;
}

#endif /* HOTARU_HAVE_LIBCHIAKI */
