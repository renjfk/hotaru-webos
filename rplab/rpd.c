/* rplab/rpd.c - the Remote Play lab daemon: a headless console session for experiments (not part of the app).
 *
 * Holds one libchiaki session to a LAN console from a Hotaru config and takes one-line commands on a
 * Unix socket; rplab/rp is the client. A reply is "ok ..." or "err ...", followed by the console
 * messages that arrived while the command ran (plus a short settle). The video is decoded (FFmpeg) only
 * so `shot` can grab the console's screen. Built against a libchiaki with the chiaki/probe.h hooks
 * (rplab/chiaki-probe.patch, applied by rplab/build.sh to a copy of chiaki-ng).
 *
 * Lua (LuaJIT): one state lives as long as the daemon, so scripts keep their globals between runs.
 * `lua <code>` runs a chunk, `run <file> [args]` a file, `reload` starts a fresh state. The native core
 * is the rp table (rp.cmd runs any command above and returns its reply); <lua dir>/prelude.lua builds
 * the helpers on it and is read again on reload, so helpers change without a rebuild.
 *
 *   rpd --state DIR --lua DIR [--host N] [--res 720|1080] [--no-connect]
 * --state holds the socket, the logs and the screenshots; --lua the prelude and scripts/. HOTARU_CONFIG_DIR
 * picks the config (rplab/run.sh passes all of them).
 */
#include <chiaki/audioreceiver.h>
#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <chiaki/discovery.h>
#include <chiaki/log.h>
#include <chiaki/probe.h>
#include <chiaki/session.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "config/config.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define RING_LINES    4096
#define LINE_LEN      320
#define PEND_LINES    64
#define PEND_FLUSH_MS 150
#define SETTLE_MS     400
#define CMD_LEN       1024
#define ARGS_MAX      24
#define SHOT_WAIT_MS  3000

enum { ST_IDLE, ST_CONNECTING, ST_CONNECTED, ST_QUIT };
static const char *k_state_name[] = { "idle", "connecting", "connected", "quit" };

static char g_dir[PATH_MAX];     /* state: socket, logs, shots */
static char g_lua_dir[PATH_MAX]; /* prelude.lua and scripts/ */
static uint64_t g_t0;
static ChiakiLog g_log;
static ChiakiSession g_session;
static bool g_live; /* g_session initialized: stop/join/fini owed */
static const config_host_t *g_host;
static int g_res = 720;
static _Atomic int g_state = ST_IDLE;
static _Atomic int g_player_index = -1;
static _Atomic unsigned g_led;
static char g_quit_reason[200];
static volatile sig_atomic_t g_stop;

static ChiakiControllerState g_pad0;
static ChiakiProbeFeedback g_fb;
static ChiakiProbeSetup g_setup; /* applied when a session connects */

/* notable log: a ring the replies read from, fed by libchiaki's log minus the per-frame noise */
static pthread_mutex_t g_ring_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_ring[RING_LINES][LINE_LEN];
static unsigned long g_ring_seq;
static FILE *g_full, *g_notable;
static char g_pend[PEND_LINES][LINE_LEN]; /* a header line and its hexdump, decided as one */
static int g_pend_n;
static bool g_pend_drop, g_last_drop;
static uint64_t g_pend_ms;

static AVCodecContext *g_dec;
static AVPacket *g_pkt;
static AVFrame *g_tmp, *g_last;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned long g_frames_in, g_frames_out;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static double now_s(void) { return (double)(now_ms() - g_t0) / 1000.0; }

/* ---------- notable log ---------- */

static bool ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static bool is_hexdump(const char *m) {
    if (!strncmp(m, "offset ", 7)) return true;
    int sp = 0;
    while (m[sp] == ' ') sp++;
    return sp >= 2 && isxdigit((unsigned char)m[sp]);
}

static bool is_noise(const char *m) {
    static const char *drop[] = { "Added reference",
                                  "Congestion Control Packet",
                                  "Takion received data ack",
                                  "Acked seq num",
                                  "Pushed seq num",
                                  "sent heartbeat",
                                  "connection quality",
                                  "measured bitrate",
                                  "Ctrl received Heartbeat",
                                  "Senkusha",
                                  "message of type 0xfe",
                                  "message type 1fe",
                                  NULL };
    for (int i = 0; drop[i]; i++)
        if (strstr(m, drop[i])) return true;
    return ends_with(m, "msg.type == 3") || ends_with(m, "msg.type == 16");
}

static void ring_add_locked(const char *line) {
    snprintf(g_ring[g_ring_seq % RING_LINES], LINE_LEN, "%s", line);
    g_ring_seq++;
    if (g_notable) {
        fprintf(g_notable, "%s\n", line);
        fflush(g_notable);
    }
}

static void pend_settle_locked(bool drop_now) {
    if (!g_pend_n) return;
    bool drop = g_pend_drop || drop_now;
    if (!drop)
        for (int i = 0; i < g_pend_n; i++) ring_add_locked(g_pend[i]);
    g_last_drop = drop;
    g_pend_n = 0;
}

static void note_line_locked(const char *msg, const char *line) {
    if (is_hexdump(msg)) {
        if (g_pend_n > 0) {
            if (g_pend_n < PEND_LINES) snprintf(g_pend[g_pend_n++], LINE_LEN, "%s", line);
        } else if (!g_last_drop) {
            ring_add_locked(line);
        }
        return;
    }
    /* a heartbeat request's CTRL RECEIVED dump is followed by one of these: drop both */
    bool heartbeat = strstr(msg, "Ctrl received Heartbeat") != NULL || strstr(msg, "message of type 0xfe") != NULL;
    pend_settle_locked(heartbeat && g_pend_n && strstr(g_pend[0], "CTRL RECEIVED") != NULL);
    snprintf(g_pend[0], LINE_LEN, "%s", line);
    g_pend_n = 1;
    g_pend_drop = is_noise(msg);
    g_pend_ms = now_ms();
}

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...) {
    char msg[LINE_LEN - 16], line[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(line, sizeof(line), "[%8.3f] * %s", now_s(), msg);
    pthread_mutex_lock(&g_ring_lock);
    if (g_full) fprintf(g_full, "%s\n", line);
    pend_settle_locked(false);
    ring_add_locked(line);
    g_last_drop = false;
    pthread_mutex_unlock(&g_ring_lock);
}

static void chiaki_log_cb(ChiakiLogLevel level, const char *msg, void *user) {
    (void)user;
    char clean[LINE_LEN - 16], line[LINE_LEN];
    snprintf(clean, sizeof(clean), "%s", msg);
    size_t n = strlen(clean);
    while (n && (clean[n - 1] == '\n' || clean[n - 1] == '\r')) clean[--n] = '\0';
    snprintf(line, sizeof(line), "[%8.3f] %c %s", now_s(), chiaki_log_level_char(level), clean);
    pthread_mutex_lock(&g_ring_lock);
    if (g_full) fprintf(g_full, "%s\n", line);
    note_line_locked(clean, line);
    pthread_mutex_unlock(&g_ring_lock);
}

static void pend_tick(void) {
    pthread_mutex_lock(&g_ring_lock);
    if (g_pend_n && now_ms() - g_pend_ms > PEND_FLUSH_MS) pend_settle_locked(false);
    if (g_full) fflush(g_full);
    pthread_mutex_unlock(&g_ring_lock);
}

static unsigned long ring_seq(void) {
    pthread_mutex_lock(&g_ring_lock);
    unsigned long s = g_ring_seq;
    pthread_mutex_unlock(&g_ring_lock);
    return s;
}

static void ring_print(FILE *out, unsigned long from) {
    pthread_mutex_lock(&g_ring_lock);
    unsigned long end = g_ring_seq;
    if (end - from > RING_LINES) {
        fprintf(out, "(%lu older lines dropped)\n", end - from - RING_LINES);
        from = end - RING_LINES;
    }
    for (unsigned long s = from; s < end; s++) fprintf(out, "%s\n", g_ring[s % RING_LINES]);
    pthread_mutex_unlock(&g_ring_lock);
}

/* ---------- session ---------- */

static const char *event_name(ChiakiEventType t) {
    switch (t) {
        case CHIAKI_EVENT_CONNECTED: return "connected";
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST: return "login_pin_request";
        case CHIAKI_EVENT_HOLEPUNCH: return "holepunch";
        case CHIAKI_EVENT_REGIST: return "regist";
        case CHIAKI_EVENT_NICKNAME_RECEIVED: return "nickname_received";
        case CHIAKI_EVENT_KEYBOARD_OPEN: return "keyboard_open";
        case CHIAKI_EVENT_KEYBOARD_TEXT_CHANGE: return "keyboard_text_change";
        case CHIAKI_EVENT_KEYBOARD_REMOTE_CLOSE: return "keyboard_remote_close";
        case CHIAKI_EVENT_RUMBLE: return "rumble";
        case CHIAKI_EVENT_QUIT: return "quit";
        case CHIAKI_EVENT_TRIGGER_EFFECTS: return "trigger_effects";
        case CHIAKI_EVENT_MOTION_RESET: return "motion_reset";
        case CHIAKI_EVENT_LED_COLOR: return "led_color";
        case CHIAKI_EVENT_PLAYER_INDEX: return "player_index";
        case CHIAKI_EVENT_HAPTIC_INTENSITY: return "haptic_intensity";
        case CHIAKI_EVENT_TRIGGER_INTENSITY: return "trigger_intensity";
        case CHIAKI_EVENT_VIDEO_FEC_FAILURE: return "video_fec_failure";
    }
    return "?";
}

static void event_cb(ChiakiEvent *e, void *user) {
    (void)user;
    switch (e->type) {
        case CHIAKI_EVENT_CONNECTED:
            g_state = ST_CONNECTED;
            note("EVENT connected");
            break;
        case CHIAKI_EVENT_QUIT:
            snprintf(g_quit_reason, sizeof(g_quit_reason), "%s%s%s", chiaki_quit_reason_string(e->quit.reason),
                     e->quit.reason_str ? ": " : "", e->quit.reason_str ? e->quit.reason_str : "");
            g_state = ST_QUIT;
            note("EVENT quit (%s)", g_quit_reason);
            break;
        case CHIAKI_EVENT_PLAYER_INDEX:
            g_player_index = e->player_index;
            note("EVENT player_index %u", e->player_index);
            break;
        case CHIAKI_EVENT_LED_COLOR:
            g_led = (unsigned)e->led_state[0] << 16 | (unsigned)e->led_state[1] << 8 | e->led_state[2];
            note("EVENT led %02x%02x%02x", e->led_state[0], e->led_state[1], e->led_state[2]);
            break;
        case CHIAKI_EVENT_RUMBLE: note("EVENT rumble left %u right %u", e->rumble.left, e->rumble.right); break;
        case CHIAKI_EVENT_HAPTIC_INTENSITY:
        case CHIAKI_EVENT_TRIGGER_INTENSITY: note("EVENT %s %d", event_name(e->type), (int)e->intensity); break;
        case CHIAKI_EVENT_TRIGGER_EFFECTS:
            note("EVENT trigger_effects left 0x%02x right 0x%02x", e->trigger_effects.type_left,
                 e->trigger_effects.type_right);
            break;
        case CHIAKI_EVENT_NICKNAME_RECEIVED: note("EVENT nickname %s", e->server_nickname); break;
        case CHIAKI_EVENT_VIDEO_FEC_FAILURE: break;
        default: note("EVENT %s", event_name(e->type)); break;
    }
}

static void decoder_close(void) { avcodec_free_context(&g_dec); }

static bool decoder_open(void) {
    decoder_close();
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) return false;
    g_dec = avcodec_alloc_context3(codec);
    if (!g_dec || avcodec_open2(g_dec, codec, NULL) < 0) {
        decoder_close();
        return false;
    }
    return true;
}

static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user) {
    (void)frames_lost;
    (void)frame_recovered;
    (void)user;
    g_frames_in++;
    if (!g_dec) return true;
    g_pkt->data = buf;
    g_pkt->size = (int)buf_size;
    int r = avcodec_send_packet(g_dec, g_pkt);
    if (r < 0 && r != AVERROR(EAGAIN)) return false;
    while (avcodec_receive_frame(g_dec, g_tmp) == 0) {
        pthread_mutex_lock(&g_frame_lock);
        av_frame_unref(g_last);
        av_frame_move_ref(g_last, g_tmp);
        g_frames_out++;
        pthread_mutex_unlock(&g_frame_lock);
    }
    return true;
}

static void audio_header_cb(ChiakiAudioHeader *header, void *user) {
    (void)header;
    (void)user;
}

static void audio_frame_cb(uint8_t *buf, size_t buf_size, void *user) {
    (void)buf;
    (void)buf_size;
    (void)user;
}

static bool session_connect(void) {
    if (g_live) return false;
    ChiakiConnectInfo info;
    memset(&info, 0, sizeof(info));
    info.ps5 = g_host->is_ps5;
    info.host = g_host->addr;
    memcpy(info.regist_key, g_host->rp_regist_key, sizeof(info.regist_key));
    memcpy(info.morning, g_host->rp_key, sizeof(info.morning));
    int ai = config_psn_find(g_host->psn_account_id);
    if (ai >= 0) memcpy(info.psn_account_id, config_psn_at(ai)->account_id, CHIAKI_PSN_ACCOUNT_ID_SIZE);
    info.video_profile_auto_downgrade = true;
    info.enable_idr_on_fec_failure = true;
    info.packet_loss_max = 0.05;
    chiaki_connect_video_profile_preset(
        &info.video_profile, g_res >= 1080 ? CHIAKI_VIDEO_RESOLUTION_PRESET_1080p : CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
        CHIAKI_VIDEO_FPS_PRESET_60);
    info.video_profile.codec = CHIAKI_CODEC_H264;

    if (!decoder_open()) {
        note("H.264 decoder failed to open");
        return false;
    }
    pthread_mutex_lock(&g_frame_lock);
    av_frame_unref(g_last);
    pthread_mutex_unlock(&g_frame_lock);
    g_frames_in = g_frames_out = 0;

    memset(&g_session, 0, sizeof(g_session));
    ChiakiErrorCode err = chiaki_session_init(&g_session, &info, &g_log);
    if (err != CHIAKI_ERR_SUCCESS) {
        note("chiaki_session_init failed: %s", chiaki_error_string(err));
        decoder_close();
        return false;
    }
    chiaki_session_set_event_cb(&g_session, event_cb, NULL);
    chiaki_session_set_video_sample_cb(&g_session, video_cb, NULL);
    ChiakiAudioSink sink = { .user = NULL, .header_cb = audio_header_cb, .frame_cb = audio_frame_cb };
    chiaki_session_set_audio_sink(&g_session, &sink);
    g_state = ST_CONNECTING;
    g_player_index = -1;
    g_led = 0;
    g_quit_reason[0] = '\0';
    chiaki_controller_state_set_idle(&g_pad0);
    err = chiaki_session_start(&g_session);
    if (err != CHIAKI_ERR_SUCCESS) {
        note("chiaki_session_start failed: %s", chiaki_error_string(err));
        chiaki_session_fini(&g_session);
        decoder_close();
        g_state = ST_IDLE;
        return false;
    }
    g_live = true;
    note("connecting to %s (%s), %dp H.264", g_host->nickname, g_host->addr, g_res);
    return true;
}

static void session_teardown(void) {
    if (!g_live) return;
    chiaki_session_stop(&g_session);
    chiaki_session_join(&g_session);
    chiaki_session_fini(&g_session);
    g_live = false;
    decoder_close();
    if (g_state != ST_QUIT) g_state = ST_IDLE;
    note("session closed");
}

/* the console refuses a new session for ~15 s after the last one ends: retry until g_retry_until */
static uint64_t g_retry_until, g_retry_next;
#define RETRY_WINDOW_MS 60000
#define RETRY_EVERY_MS  4000

static bool console_busy(void) { return g_state == ST_QUIT && strstr(g_quit_reason, "already in use") != NULL; }

static void maintenance(void) {
    pend_tick();
    if (g_live && g_state == ST_QUIT) session_teardown();
    uint64_t now = now_ms();
    if (!g_live && console_busy() && now < g_retry_until && now >= g_retry_next) {
        g_retry_next = now + RETRY_EVERY_MS;
        note("console still holds the last session: retrying");
        session_connect();
    }
}

static void sleep_pump(int ms) {
    uint64_t until = now_ms() + (uint64_t)(ms > 0 ? ms : 0);
    while (now_ms() < until) {
        usleep(10000);
        maintenance();
    }
    maintenance();
}

static bool pad0_apply(void) {
    if (g_state != ST_CONNECTED) return false;
    return chiaki_session_set_controller_state(&g_session, &g_pad0) == CHIAKI_ERR_SUCCESS;
}

/* ---------- command parsing ---------- */

static const char *kv(const char *tok, const char *key) {
    size_t n = strlen(key);
    return !strncmp(tok, key, n) && tok[n] == '=' ? tok + n + 1 : NULL;
}

static bool parse_long(const char *s, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end) return false;
    *out = v;
    return true;
}

/* buttons and the analog triggers (CHIAKI_CONTROLLER_ANALOG_BUTTON_*: pressed fully) */
static uint32_t button_bit(const char *name) {
    static const struct {
        const char *name;
        uint32_t bit;
    } map[] = {
        { "cross", CHIAKI_CONTROLLER_BUTTON_CROSS },
        { "circle", CHIAKI_CONTROLLER_BUTTON_MOON },
        { "moon", CHIAKI_CONTROLLER_BUTTON_MOON },
        { "square", CHIAKI_CONTROLLER_BUTTON_BOX },
        { "box", CHIAKI_CONTROLLER_BUTTON_BOX },
        { "triangle", CHIAKI_CONTROLLER_BUTTON_PYRAMID },
        { "pyramid", CHIAKI_CONTROLLER_BUTTON_PYRAMID },
        { "left", CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT },
        { "right", CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT },
        { "up", CHIAKI_CONTROLLER_BUTTON_DPAD_UP },
        { "down", CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN },
        { "l1", CHIAKI_CONTROLLER_BUTTON_L1 },
        { "r1", CHIAKI_CONTROLLER_BUTTON_R1 },
        { "l3", CHIAKI_CONTROLLER_BUTTON_L3 },
        { "r3", CHIAKI_CONTROLLER_BUTTON_R3 },
        { "options", CHIAKI_CONTROLLER_BUTTON_OPTIONS },
        { "share", CHIAKI_CONTROLLER_BUTTON_SHARE },
        { "create", CHIAKI_CONTROLLER_BUTTON_SHARE },
        { "touchpad", CHIAKI_CONTROLLER_BUTTON_TOUCHPAD },
        { "ps", CHIAKI_CONTROLLER_BUTTON_PS },
        { "l2", CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 },
        { "r2", CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (!strcmp(name, map[i].name)) return map[i].bit;
    return 0;
}

static bool parse_buttons(const char *s, uint32_t *out) {
    *out = 0;
    if (!*s || !strcmp(s, "none")) return true;
    char buf[256], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", s);
    for (char *t = strtok_r(buf, ",+", &save); t; t = strtok_r(NULL, ",+", &save)) {
        uint32_t b = button_bit(t);
        if (!b) return false;
        *out |= b;
    }
    return true;
}

/* buttons with the analog bits split out into the triggers */
static void pad_set_buttons(ChiakiControllerState *st, uint32_t bits) {
    st->buttons = bits & 0xffffu;
    st->l2_state = bits & CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 ? 0xff : 0;
    st->r2_state = bits & CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 ? 0xff : 0;
}

/* hex bytes, optionally separated by ':', ',', '.' or '-'; -1 on bad input */
static int parse_hex(const char *s, uint8_t *out, int max) {
    int n = 0, hi = -1;
    for (; *s; s++) {
        int v;
        if (*s >= '0' && *s <= '9') v = *s - '0';
        else if (*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') v = *s - 'A' + 10;
        else if (strchr(":,.-", *s) && hi < 0) continue;
        else return -1;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= max) return -1;
            out[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return hi >= 0 ? -1 : n;
}

static int controller_type(const char *s) {
    static const char *names[] = { "notset", "ds3", "ds4", "vita", "xinput", "mobile", "dualsense", "vr2sense" };
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (!strcmp(s, names[i])) return i;
    long v;
    return parse_long(s, &v) && v >= 0 ? (int)v : -2;
}

static void feedback_idle(ChiakiFeedbackState *st) {
    memset(st, 0, sizeof(*st));
    st->accel_y = 1.0f;
    st->orient_w = 1.0f;
}

static void fb_print(FILE *out) {
    fprintf(out, "feedback: %d state(s) per packet, byte3 %s", g_fb.count, g_fb.count_byte >= 0 ? "" : "auto");
    if (g_fb.count_byte >= 0) fprintf(out, "0x%02x", g_fb.count_byte);
    fprintf(out, "\n");
    for (int i = 0; i < CHIAKI_PROBE_STATES_MAX; i++) {
        if (g_fb.raw[i]) {
            fprintf(out, "  slot %d raw", i);
            for (int b = 0; b < CHIAKI_FEEDBACK_STATE_BUF_SIZE_MAX; b++) fprintf(out, " %02x", g_fb.raw_bytes[i][b]);
            fprintf(out, "\n");
        } else if (i == 0) {
            fprintf(out, "  slot 0 pad0\n");
        } else {
            const ChiakiFeedbackState *s = &g_fb.extra[i];
            fprintf(out, "  slot %d lx %d ly %d rx %d ry %d\n", i, s->left_x, s->left_y, s->right_x, s->right_y);
        }
    }
}

static void print_help(FILE *out) {
    fputs("ok commands:\n"
          "  status | log [n] | wait <ms> | help | quit\n"
          "  connect | reconnect | disconnect | wake | rest (rest mode)\n"
          "  pad0 [idle] [buttons=cross,l1|none] [lx= ly= rx= ry=] [l2= r2=]   pad 0 (buttons go as history events)\n"
          "  press <btn[,btn]> [ms]                                           momentary press on pad 0\n"
          "  states <1..4> [byte3=<v>|auto]                                   controller states per feedback packet\n"
          "  slot <i> [lx= ly= rx= ry=] | slot <i> raw=<hex> | slot <i> clear  state i of the feedback packet\n"
          "  cc [id=<n>|none] [connected=0|1] [type=ds4|dualsense|<n>]        ControllerConnection message\n"
          "  hist <hex>                                                       raw feedback history event (<= 5 bytes)\n"
          "  shot [name]                                                      screenshot -> shots/<name>.png\n"
          "  lua <code> | run <file.lua> [args] | reload                      Lua (see prelude.lua)\n"
          "buttons: cross circle square triangle up down left right l1 r1 l2 r2 l3 r3 options share touchpad ps\n",
          out);
}

static void print_status(FILE *out) {
    fprintf(out, "ok %s\n", k_state_name[g_state]);
    fprintf(out, "host: %s (%s) %dp\n", g_host->nickname, g_host->addr, g_res);
    if (g_quit_reason[0]) fprintf(out, "quit: %s\n", g_quit_reason);
    fprintf(out, "video: %lu frames in, %lu decoded\n", (unsigned long)g_frames_in, (unsigned long)g_frames_out);
    fprintf(out, "console: player_index %d, led %06x\n", (int)g_player_index, (unsigned)g_led);
    fprintf(out, "pad0: buttons 0x%04x l2 %u r2 %u lx %d ly %d rx %d ry %d\n", g_pad0.buttons, g_pad0.l2_state,
            g_pad0.r2_state, g_pad0.left_x, g_pad0.left_y, g_pad0.right_x, g_pad0.right_y);
    fb_print(out);
    const ChiakiProbeSetup *s = &g_setup;
    fprintf(out, "setup: ControllerType %d ClientType %d ConPath %d PadProcNo %d start_cc 0x%x type %d id0 %d\n",
            s->controller_type, s->client_type, s->con_path, s->pad_proc_no, s->start_cc_mask, s->start_cc_type,
            s->start_cc_with_id0);
    if (s->ctrl_extra[0]) fprintf(out, "  ctrl extra: %s\n", s->ctrl_extra);
    if (s->init_extra[0]) fprintf(out, "  init extra: %s\n", s->init_extra);
    if (s->controllers[0]) fprintf(out, "  connectedControllers: [%s]\n", s->controllers);
    if (s->game_spec_extra[0]) fprintf(out, "  game spec extra: %s\n", s->game_spec_extra);
}

static bool write_png(const char *path, uint8_t *rgb, int w, int h) {
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    AVCodecContext *c = codec ? avcodec_alloc_context3(codec) : NULL;
    AVFrame *f = av_frame_alloc();
    AVPacket *p = av_packet_alloc();
    bool ok = false;
    if (c && f && p) {
        c->width = w;
        c->height = h;
        c->pix_fmt = AV_PIX_FMT_RGB24;
        c->time_base = (AVRational){ 1, 1 };
        if (avcodec_open2(c, codec, NULL) == 0) {
            f->format = AV_PIX_FMT_RGB24;
            f->width = w;
            f->height = h;
            f->data[0] = rgb;
            f->linesize[0] = w * 3;
            if (avcodec_send_frame(c, f) == 0 && avcodec_receive_packet(c, p) == 0) {
                FILE *fp = fopen(path, "wb");
                if (fp) {
                    ok = fwrite(p->data, 1, (size_t)p->size, fp) == (size_t)p->size;
                    ok = fclose(fp) == 0 && ok;
                }
            }
        }
    }
    av_packet_free(&p);
    av_frame_free(&f);
    avcodec_free_context(&c);
    return ok;
}

static bool shot(const char *name, FILE *out) {
    unsigned long start = g_frames_out;
    uint64_t until = now_ms() + SHOT_WAIT_MS;
    while (g_frames_out <= start + 1 && now_ms() < until) sleep_pump(10);
    char dir[PATH_MAX], path[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/shots", g_dir);
    mkdir(dir, 0755);
    if (name) snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    else snprintf(path, sizeof(path), "%s/shot-%.0f.png", dir, now_s() * 1000.0);

    pthread_mutex_lock(&g_frame_lock);
    if (!g_last->data[0]) {
        pthread_mutex_unlock(&g_frame_lock);
        fprintf(out, "err no decoded frame yet\n");
        return false;
    }
    int w = g_last->width, h = g_last->height;
    int ow = w > 1280 ? w / 2 : w, oh = w > 1280 ? h / 2 : h;
    struct SwsContext *sws = sws_getContext(w, h, (enum AVPixelFormat)g_last->format, ow, oh, AV_PIX_FMT_RGB24,
                                            SWS_BILINEAR, NULL, NULL, NULL);
    uint8_t *rgb = malloc((size_t)ow * (size_t)oh * 3);
    if (!sws || !rgb) {
        pthread_mutex_unlock(&g_frame_lock);
        sws_freeContext(sws);
        free(rgb);
        fprintf(out, "err scaler\n");
        return false;
    }
    uint8_t *dst[4] = { rgb, NULL, NULL, NULL };
    int dst_ls[4] = { ow * 3, 0, 0, 0 };
    sws_scale(sws, (const uint8_t *const *)g_last->data, g_last->linesize, 0, h, dst, dst_ls);
    pthread_mutex_unlock(&g_frame_lock);
    sws_freeContext(sws);

    bool ok = write_png(path, rgb, ow, oh);
    free(rgb);
    if (!ok) {
        fprintf(out, "err cannot write %s\n", path);
        return false;
    }
    fprintf(out, "ok shot %s\n", path);
    return true;
}

static bool need_connected(FILE *out) {
    if (g_state == ST_CONNECTED) return true;
    fprintf(out, "err not connected (%s)\n", k_state_name[g_state]);
    return false;
}

static void dispatch(int argc, char **argv, FILE *out, int *settle) {
    const char *cmd = argv[0];
    if (!strcmp(cmd, "help")) {
        print_help(out);
        *settle = 0;
    } else if (!strcmp(cmd, "status")) {
        print_status(out);
        *settle = 0;
    } else if (!strcmp(cmd, "log")) {
        long n = 40;
        if (argc > 1 && (!parse_long(argv[1], &n) || n < 1)) n = 40;
        unsigned long end = ring_seq();
        fprintf(out, "ok last %ld lines\n", n);
        ring_print(out, end > (unsigned long)n ? end - (unsigned long)n : 0);
        *settle = -1;
    } else if (!strcmp(cmd, "wait")) {
        long ms = 1000;
        if (argc > 1 && !parse_long(argv[1], &ms)) {
            fprintf(out, "err wait <ms>\n");
            return;
        }
        fprintf(out, "ok waited %ld ms\n", ms);
        *settle = (int)ms;
    } else if (!strcmp(cmd, "quit")) {
        g_stop = 1;
        fprintf(out, "ok quitting\n");
        *settle = 0;
    } else if (!strcmp(cmd, "connect") || !strcmp(cmd, "reconnect")) {
        if (!strcmp(cmd, "reconnect")) {
            session_teardown();
            g_state = ST_IDLE;
        } else if (g_live) {
            fprintf(out, "err already %s\n", k_state_name[g_state]);
            return;
        }
        g_retry_until = now_ms() + RETRY_WINDOW_MS;
        g_retry_next = now_ms() + RETRY_EVERY_MS;
        if (!session_connect()) {
            fprintf(out, "err connect failed\n");
            return;
        }
        while (g_state != ST_CONNECTED && now_ms() < g_retry_until && (g_live || console_busy())) sleep_pump(20);
        fprintf(out, "%s %s\n", g_state == ST_CONNECTED ? "ok" : "err", k_state_name[g_state]);
    } else if (!strcmp(cmd, "disconnect")) {
        session_teardown();
        g_state = ST_IDLE;
        fprintf(out, "ok idle\n");
    } else if (!strcmp(cmd, "rest")) {
        if (!need_connected(out)) return;
        ChiakiErrorCode err = chiaki_session_goto_bed(&g_session);
        fprintf(out, "%s rest request %s\n", err == CHIAKI_ERR_SUCCESS ? "ok" : "err",
                err == CHIAKI_ERR_SUCCESS ? "sent (the console ends the session; wake before connecting again)"
                                          : chiaki_error_string(err));
        *settle = 2000;
    } else if (!strcmp(cmd, "wake")) {
        char key[17];
        memcpy(key, g_host->rp_regist_key, 16);
        key[16] = '\0';
        ChiakiErrorCode err =
            chiaki_discovery_wakeup(&g_log, NULL, g_host->addr, strtoull(key, NULL, 16), g_host->is_ps5);
        fprintf(out, "%s wake-up %s\n", err == CHIAKI_ERR_SUCCESS ? "ok" : "err",
                err == CHIAKI_ERR_SUCCESS ? "sent" : chiaki_error_string(err));
    } else if (!strcmp(cmd, "pad0")) {
        if (!need_connected(out)) return;
        for (int i = 1; i < argc; i++) {
            const char *v;
            long n;
            uint32_t bits;
            if (!strcmp(argv[i], "idle")) {
                chiaki_controller_state_set_idle(&g_pad0);
            } else if ((v = kv(argv[i], "buttons"))) {
                if (!parse_buttons(v, &bits)) {
                    fprintf(out, "err bad buttons %s\n", v);
                    return;
                }
                pad_set_buttons(&g_pad0, bits);
            } else if ((v = kv(argv[i], "lx")) && parse_long(v, &n)) g_pad0.left_x = (int16_t)n;
            else if ((v = kv(argv[i], "ly")) && parse_long(v, &n)) g_pad0.left_y = (int16_t)n;
            else if ((v = kv(argv[i], "rx")) && parse_long(v, &n)) g_pad0.right_x = (int16_t)n;
            else if ((v = kv(argv[i], "ry")) && parse_long(v, &n)) g_pad0.right_y = (int16_t)n;
            else if ((v = kv(argv[i], "l2")) && parse_long(v, &n)) g_pad0.l2_state = (uint8_t)n;
            else if ((v = kv(argv[i], "r2")) && parse_long(v, &n)) g_pad0.r2_state = (uint8_t)n;
            else {
                fprintf(out, "err bad argument %s\n", argv[i]);
                return;
            }
        }
        fprintf(out, "%s pad0 buttons 0x%04x l2 %u r2 %u lx %d ly %d rx %d ry %d\n", pad0_apply() ? "ok" : "err",
                g_pad0.buttons, g_pad0.l2_state, g_pad0.r2_state, g_pad0.left_x, g_pad0.left_y, g_pad0.right_x,
                g_pad0.right_y);
    } else if (!strcmp(cmd, "press")) {
        uint32_t bits;
        long ms = 150;
        if (argc < 2 || !parse_buttons(argv[1], &bits) || !bits || (argc > 2 && !parse_long(argv[2], &ms))) {
            fprintf(out, "err press <btn[,btn]> [ms]\n");
            return;
        }
        if (!need_connected(out)) return;
        ChiakiControllerState saved = g_pad0;
        ChiakiControllerState pressed = g_pad0;
        pad_set_buttons(&pressed, bits);
        pressed.buttons |= saved.buttons;
        if (saved.l2_state > pressed.l2_state) pressed.l2_state = saved.l2_state;
        if (saved.r2_state > pressed.r2_state) pressed.r2_state = saved.r2_state;
        g_pad0 = pressed;
        pad0_apply();
        sleep_pump((int)ms);
        g_pad0 = saved;
        pad0_apply();
        fprintf(out, "ok pressed %s for %ld ms\n", argv[1], ms);
    } else if (!strcmp(cmd, "states")) {
        long n;
        if (argc < 2 || !parse_long(argv[1], &n) || n < 1 || n > CHIAKI_PROBE_STATES_MAX) {
            fprintf(out, "err states <1..%d> [byte3=<v>|auto]\n", CHIAKI_PROBE_STATES_MAX);
            return;
        }
        g_fb.count = (int)n;
        for (int i = 2; i < argc; i++) {
            const char *v = kv(argv[i], "byte3");
            long b;
            if (v && !strcmp(v, "auto")) g_fb.count_byte = -1;
            else if (v && parse_long(v, &b) && b >= 0 && b <= 255) g_fb.count_byte = (int)b;
            else {
                fprintf(out, "err bad argument %s\n", argv[i]);
                return;
            }
        }
        chiaki_probe_set_feedback(&g_fb);
        fputs("ok ", out);
        fb_print(out);
    } else if (!strcmp(cmd, "slot")) {
        long i;
        if (argc < 3 || !parse_long(argv[1], &i) || i < 0 || i >= CHIAKI_PROBE_STATES_MAX) {
            fprintf(out, "err slot <0..%d> ...\n", CHIAKI_PROBE_STATES_MAX - 1);
            return;
        }
        for (int a = 2; a < argc; a++) {
            const char *v;
            long n;
            if (!strcmp(argv[a], "clear")) {
                g_fb.raw[i] = false;
                feedback_idle(&g_fb.extra[i]);
            } else if ((v = kv(argv[a], "raw"))) {
                memset(g_fb.raw_bytes[i], 0, sizeof(g_fb.raw_bytes[i]));
                if (parse_hex(v, g_fb.raw_bytes[i], CHIAKI_FEEDBACK_STATE_BUF_SIZE_MAX) < 0) {
                    fprintf(out, "err bad hex (max %d bytes)\n", CHIAKI_FEEDBACK_STATE_BUF_SIZE_MAX);
                    return;
                }
                g_fb.raw[i] = true;
            } else if (i == 0) {
                fprintf(out, "err slot 0 is pad0: only raw= or clear\n");
                return;
            } else if ((v = kv(argv[a], "lx")) && parse_long(v, &n)) {
                g_fb.extra[i].left_x = (int16_t)n;
                g_fb.raw[i] = false;
            } else if ((v = kv(argv[a], "ly")) && parse_long(v, &n)) {
                g_fb.extra[i].left_y = (int16_t)n;
                g_fb.raw[i] = false;
            } else if ((v = kv(argv[a], "rx")) && parse_long(v, &n)) {
                g_fb.extra[i].right_x = (int16_t)n;
                g_fb.raw[i] = false;
            } else if ((v = kv(argv[a], "ry")) && parse_long(v, &n)) {
                g_fb.extra[i].right_y = (int16_t)n;
                g_fb.raw[i] = false;
            } else {
                fprintf(out, "err bad argument %s\n", argv[a]);
                return;
            }
        }
        chiaki_probe_set_feedback(&g_fb);
        fputs("ok ", out);
        fb_print(out);
    } else if (!strcmp(cmd, "cc")) {
        bool has_id = false, connected = true;
        long id = 0;
        int type = -1;
        for (int a = 1; a < argc; a++) {
            const char *v;
            long n;
            if ((v = kv(argv[a], "id"))) {
                if (!strcmp(v, "none")) has_id = false;
                else if (parse_long(v, &id)) has_id = true;
                else {
                    fprintf(out, "err bad id %s\n", v);
                    return;
                }
            } else if ((v = kv(argv[a], "connected")) && parse_long(v, &n)) {
                connected = n != 0;
            } else if ((v = kv(argv[a], "type")) && (type = controller_type(v)) >= -1) {
                continue;
            } else {
                fprintf(out, "err bad argument %s\n", argv[a]);
                return;
            }
        }
        if (!need_connected(out)) return;
        ChiakiErrorCode err = chiaki_probe_send_controller_connection(&g_session, has_id, (int32_t)id, connected, type);
        fprintf(out, "%s cc id %s%ld connected %d type %d%s%s\n", err == CHIAKI_ERR_SUCCESS ? "ok" : "err",
                has_id ? "" : "none/", id, connected, type, err == CHIAKI_ERR_SUCCESS ? "" : ": ",
                err == CHIAKI_ERR_SUCCESS ? "" : chiaki_error_string(err));
    } else if (!strcmp(cmd, "hist")) {
        char joined[CMD_LEN] = "";
        for (int a = 1; a < argc; a++) strncat(joined, argv[a], sizeof(joined) - strlen(joined) - 1);
        uint8_t ev[CHIAKI_HISTORY_EVENT_SIZE_MAX];
        int n = parse_hex(joined, ev, (int)sizeof(ev));
        if (n <= 0) {
            fprintf(out, "err hist <hex, 1..%d bytes>\n", CHIAKI_HISTORY_EVENT_SIZE_MAX);
            return;
        }
        if (!need_connected(out)) return;
        ChiakiErrorCode err = chiaki_probe_push_history_event(&g_session, ev, (size_t)n);
        fprintf(out, "%s hist %d bytes%s%s\n", err == CHIAKI_ERR_SUCCESS ? "ok" : "err", n,
                err == CHIAKI_ERR_SUCCESS ? "" : ": ", err == CHIAKI_ERR_SUCCESS ? "" : chiaki_error_string(err));
    } else if (!strcmp(cmd, "shot")) {
        if (!need_connected(out)) return;
        shot(argc > 1 ? argv[1] : NULL, out);
        *settle = 0;
    } else {
        fprintf(out, "err unknown command %s (try help)\n", cmd);
        *settle = 0;
    }
}

/* ---------- Lua ---------- */

static lua_State *g_L;
static FILE *g_lua_out; /* the reply of the running script */

static int tokenize(char *line, char **argv) {
    char *save = NULL;
    int argc = 0;
    for (char *t = strtok_r(line, " \t", &save); t && argc < ARGS_MAX; t = strtok_r(NULL, " \t", &save))
        argv[argc++] = t;
    return argc;
}

/* rp.cmd(line) -> reply: any daemon command but lua/run/reload, without the log tail */
static int l_cmd(lua_State *L) {
    char line[CMD_LEN], *argv[ARGS_MAX];
    snprintf(line, sizeof(line), "%s", luaL_checkstring(L, 1));
    int argc = tokenize(line, argv);
    if (!argc) return luaL_error(L, "rp.cmd: empty command");
    char *buf = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&buf, &len);
    if (!out) return luaL_error(L, "rp.cmd: out of memory");
    int settle = 0;
    dispatch(argc, argv, out, &settle);
    if (!strcmp(argv[0], "wait") && settle > 0) sleep_pump(settle);
    fclose(out);
    while (len && buf[len - 1] == '\n') len--;
    lua_pushlstring(L, buf, len);
    free(buf);
    return 1;
}

static int l_sleep(lua_State *L) {
    sleep_pump((int)luaL_checkinteger(L, 1));
    return 0;
}

static int l_now(lua_State *L) {
    lua_pushnumber(L, (lua_Number)(now_ms() - g_t0));
    return 1;
}

static int l_seq(lua_State *L) {
    lua_pushnumber(L, (lua_Number)ring_seq());
    return 1;
}

/* rp.lines([from]) -> the notable log lines since seq from (default: all the ring holds) */
static int l_lines(lua_State *L) {
    unsigned long from = (unsigned long)luaL_optnumber(L, 1, 0);
    lua_newtable(L);
    pthread_mutex_lock(&g_ring_lock);
    unsigned long end = g_ring_seq;
    if (end - from > RING_LINES || from > end) from = end > RING_LINES ? end - RING_LINES : 0;
    int n = 0;
    for (unsigned long s = from; s < end; s++) {
        lua_pushstring(L, g_ring[s % RING_LINES]);
        lua_rawseti(L, -2, ++n);
    }
    pthread_mutex_unlock(&g_ring_lock);
    return 1;
}

/* rp.fresh([ms]) -> true once a frame newer than the call is decoded (default wait 1000 ms) */
static int l_fresh(lua_State *L) {
    unsigned long start = g_frames_out;
    uint64_t until = now_ms() + (uint64_t)luaL_optinteger(L, 1, 1000);
    while (g_frames_out <= start && now_ms() < until) sleep_pump(5);
    lua_pushboolean(L, g_frames_out > start);
    return 1;
}

/* rp.luma(x, y, w, h) -> mean luma (0..255) of a region of the latest frame, in screenshot coordinates
 * (the frame scaled to at most 1280 wide, as shot saves it); nil before the first frame */
static int l_luma(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3), h = (int)luaL_checkinteger(L, 4);
    pthread_mutex_lock(&g_frame_lock);
    if (!g_last->data[0] || (g_last->format != AV_PIX_FMT_YUV420P && g_last->format != AV_PIX_FMT_YUVJ420P)) {
        pthread_mutex_unlock(&g_frame_lock);
        lua_pushnil(L);
        return 1;
    }
    int scale = g_last->width > 1280 ? 2 : 1;
    int x0 = x * scale, y0 = y * scale, x1 = (x + w) * scale, y1 = (y + h) * scale;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_last->width) x1 = g_last->width;
    if (y1 > g_last->height) y1 = g_last->height;
    uint64_t sum = 0, n = 0;
    for (int yy = y0; yy < y1; yy++) {
        const uint8_t *row = g_last->data[0] + (size_t)yy * (size_t)g_last->linesize[0];
        for (int xx = x0; xx < x1; xx++) sum += row[xx];
        n += x1 > x0 ? (uint64_t)(x1 - x0) : 0;
    }
    pthread_mutex_unlock(&g_frame_lock);
    lua_pushnumber(L, n ? (lua_Number)sum / (lua_Number)n : 0);
    return 1;
}

static int l_info(lua_State *L) {
    lua_newtable(L);
    lua_pushstring(L, k_state_name[g_state]);
    lua_setfield(L, -2, "state");
    lua_pushinteger(L, g_player_index);
    lua_setfield(L, -2, "player_index");
    lua_pushinteger(L, (lua_Integer)g_led);
    lua_setfield(L, -2, "led");
    lua_pushnumber(L, (lua_Number)g_frames_in);
    lua_setfield(L, -2, "frames_in");
    lua_pushnumber(L, (lua_Number)g_frames_out);
    lua_setfield(L, -2, "frames_out");
    lua_pushstring(L, g_quit_reason);
    lua_setfield(L, -2, "quit_reason");
    lua_pushstring(L, g_host->nickname);
    lua_setfield(L, -2, "host");
    return 1;
}

/* field k of the table at idx as header lines: a string or a list of strings, each line ending in \r\n */
static void setup_lines(lua_State *L, int idx, const char *k, char *dst, size_t cap) {
    lua_getfield(L, idx, k);
    if (lua_isstring(L, -1)) {
        snprintf(dst, cap, "%s\r\n", lua_tostring(L, -1));
    } else if (lua_istable(L, -1)) {
        for (int i = 1;; i++) {
            lua_rawgeti(L, -1, i);
            if (!lua_isstring(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            size_t n = strlen(dst);
            snprintf(dst + n, cap - n, "%s\r\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static int setup_int(lua_State *L, int idx, const char *k, int dflt) {
    lua_getfield(L, idx, k);
    int v = dflt;
    if (lua_isnumber(L, -1)) v = (int)lua_tointeger(L, -1);
    else if (lua_isstring(L, -1) && !strcmp(lua_tostring(L, -1), "none")) v = -2;
    lua_pop(L, 1);
    return v;
}

/* rp.setup{controller_type=, client_type=, con_path=, pad_proc_no= (n|"none"), ctrl={lines}, init={lines},
 * controllers={"ds4",...}, game_spec=',"k":v', start_cc={1,2}, start_cc_type=n|"ds4", id0=bool}: replaces the
 * whole setup (unset fields are chiaki's defaults); applies on the next connect. rp.setup() resets it. */
static int l_setup(lua_State *L) {
    ChiakiProbeSetup s;
    chiaki_probe_setup_default(&s);
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        s.controller_type = setup_int(L, 1, "controller_type", -1);
        s.client_type = setup_int(L, 1, "client_type", -1);
        s.con_path = setup_int(L, 1, "con_path", -1);
        s.pad_proc_no = setup_int(L, 1, "pad_proc_no", -1);
        setup_lines(L, 1, "ctrl", s.ctrl_extra, sizeof(s.ctrl_extra));
        setup_lines(L, 1, "init", s.init_extra, sizeof(s.init_extra));
        lua_getfield(L, 1, "controllers");
        if (lua_isstring(L, -1)) {
            snprintf(s.controllers, sizeof(s.controllers), "%s", lua_tostring(L, -1));
        } else if (lua_istable(L, -1)) {
            for (int i = 1;; i++) {
                lua_rawgeti(L, -1, i);
                if (!lua_isstring(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                size_t n = strlen(s.controllers);
                snprintf(s.controllers + n, sizeof(s.controllers) - n, "%s\"%s\"", i > 1 ? "," : "",
                         lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "game_spec");
        if (lua_isstring(L, -1)) snprintf(s.game_spec_extra, sizeof(s.game_spec_extra), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "start_cc");
        if (lua_istable(L, -1)) {
            for (int i = 1;; i++) {
                lua_rawgeti(L, -1, i);
                if (!lua_isnumber(L, -1)) {
                    lua_pop(L, 1);
                    break;
                }
                int id = (int)lua_tointeger(L, -1);
                if (id >= 0 && id < 8) s.start_cc_mask |= 1u << id;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "start_cc_type");
        s.start_cc_type = lua_isnumber(L, -1)   ? (int)lua_tointeger(L, -1)
                          : lua_isstring(L, -1) ? controller_type(lua_tostring(L, -1))
                                                : -1;
        lua_pop(L, 1);
        lua_getfield(L, 1, "id0");
        s.start_cc_with_id0 = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    g_setup = s;
    chiaki_probe_set_setup(&s);
    return 0;
}

/* print() goes into the reply */
static int l_print(lua_State *L) {
    int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; i++) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char *s = lua_tostring(L, -1);
        if (g_lua_out) fprintf(g_lua_out, "%s%s", i > 1 ? "\t" : "", s ? s : "");
        lua_pop(L, 1);
    }
    if (g_lua_out) fputc('\n', g_lua_out);
    return 0;
}

static int lua_traceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

/* run the function on the top of the stack with nargs arguments below it; errors go to out */
static bool lua_run(int nargs, FILE *out) {
    int base = lua_gettop(g_L) - nargs;
    lua_pushcfunction(g_L, lua_traceback);
    lua_insert(g_L, base);
    bool ok = lua_pcall(g_L, nargs, 0, base) == 0;
    if (!ok) {
        fprintf(out, "err %s\n", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
    }
    lua_remove(g_L, base);
    return ok;
}

static void lua_close_state(void) {
    if (g_L) lua_close(g_L);
    g_L = NULL;
}

static bool lua_open_state(FILE *out) {
    lua_close_state();
    g_L = luaL_newstate();
    if (!g_L) return false;
    luaL_openlibs(g_L);
    static const luaL_Reg rp[] = { { "cmd", l_cmd },     { "sleep", l_sleep }, { "now", l_now },     { "seq", l_seq },
                                   { "lines", l_lines }, { "info", l_info },   { "fresh", l_fresh }, { "luma", l_luma },
                                   { "setup", l_setup }, { NULL, NULL } };
    luaL_register(g_L, "rp", rp);
    lua_pushstring(g_L, g_dir);
    lua_setfield(g_L, -2, "dir");
    lua_pushstring(g_L, g_lua_dir);
    lua_setfield(g_L, -2, "lua_dir");
    lua_pushboolean(g_L, 1);
    lua_setfield(g_L, -2, "show_log");
    lua_pop(g_L, 1);
    lua_register(g_L, "print", l_print);

    char code[PATH_MAX * 2];
    snprintf(code, sizeof(code), "package.path = '%s/scripts/?.lua;' .. package.path", g_lua_dir);
    luaL_dostring(g_L, code);

    char prelude[PATH_MAX];
    snprintf(prelude, sizeof(prelude), "%s/prelude.lua", g_lua_dir);
    if (access(prelude, R_OK) != 0) return true;
    if (luaL_loadfile(g_L, prelude) != 0) {
        fprintf(out, "err prelude: %s\n", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }
    return lua_run(0, out);
}

static bool lua_show_log(void) {
    lua_getglobal(g_L, "rp");
    lua_getfield(g_L, -1, "show_log");
    bool show = lua_toboolean(g_L, -1);
    lua_pop(g_L, 2);
    return show;
}

/* lua <code>, run <file> [args], reload; false when the line is none of them */
static bool lua_command(char *line, FILE *out, unsigned long start) {
    bool is_lua = !strncmp(line, "lua ", 4), is_run = !strncmp(line, "run ", 4);
    if (!strcmp(line, "reload")) {
        if (lua_open_state(out)) fprintf(out, "ok fresh Lua state\n");
        return true;
    }
    if (!is_lua && !is_run) return false;
    if (!g_L && !lua_open_state(out)) return true;
    g_lua_out = out;
    bool ok;
    if (is_lua) {
        const char *code = line + 4;
        ok = luaL_loadbuffer(g_L, code, strlen(code), "=lua") == 0;
        if (ok) ok = lua_run(0, out);
        else {
            fprintf(out, "err %s\n", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    } else {
        char *argv[ARGS_MAX];
        int argc = tokenize(line + 4, argv);
        ok = argc > 0 && luaL_loadfile(g_L, argv[0]) == 0;
        if (!ok) {
            fprintf(out, "err %s\n", argc > 0 ? lua_tostring(g_L, -1) : "run <file.lua> [args]");
            if (argc > 0) lua_pop(g_L, 1);
        } else {
            lua_newtable(g_L);
            for (int i = 0; i < argc; i++) {
                lua_pushstring(g_L, argv[i]);
                lua_rawseti(g_L, -2, i);
            }
            lua_setglobal(g_L, "arg");
            for (int i = 1; i < argc; i++) lua_pushstring(g_L, argv[i]);
            ok = lua_run(argc - 1, out);
        }
    }
    g_lua_out = NULL;
    if (ok) fprintf(out, "ok\n");
    if (lua_show_log()) {
        sleep_pump(100);
        fprintf(out, "--- log\n");
        ring_print(out, start);
    }
    return true;
}

static void run_command(char *line, FILE *out) {
    char echo[CMD_LEN];
    snprintf(echo, sizeof(echo), "%s", line);
    if (!echo[0]) {
        fprintf(out, "err empty command\n");
        return;
    }
    note("> %s", echo);
    unsigned long start = ring_seq();
    if (lua_command(line, out, start)) return;
    char *argv[ARGS_MAX];
    int argc = tokenize(line, argv);
    if (!argc) {
        fprintf(out, "err empty command\n");
        return;
    }
    int settle = SETTLE_MS;
    dispatch(argc, argv, out, &settle);
    if (settle < 0) return;
    sleep_pump(settle);
    ring_print(out, start);
}

static void handle_client(int fd) {
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char line[CMD_LEN];
    size_t n = 0;
    while (n < sizeof(line) - 1) {
        ssize_t r = read(fd, line + n, 1);
        if (r <= 0 || line[n] == '\n') break;
        n++;
    }
    line[n] = '\0';
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';

    char *buf = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&buf, &len);
    if (!out) {
        close(fd);
        return;
    }
    run_command(line, out);
    fclose(out);
    for (size_t off = 0; off < len;) {
        ssize_t w = write(fd, buf + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    free(buf);
    close(fd);
}

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

int main(int argc, char **argv) {
    int host_index = 0;
    bool autoconnect = true;
    const char *state = NULL, *lua_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host_index = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) g_res = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "--lua") && i + 1 < argc) lua_dir = argv[++i];
        else if (!strcmp(argv[i], "--no-connect")) autoconnect = false;
        else {
            state = NULL;
            break;
        }
    }
    if (!state || !lua_dir) {
        fprintf(stderr, "usage: rpd --state DIR --lua DIR [--host N] [--res 720|1080] [--no-connect]\n");
        return 2;
    }
    g_t0 = now_ms();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    mkdir(state, 0755);
    if (!realpath(state, g_dir) || !realpath(lua_dir, g_lua_dir)) {
        fprintf(stderr, "rpd: no such directory: %s or %s\n", state, lua_dir);
        return 2;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/rpd-full.log", g_dir);
    g_full = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/rpd-notable.log", g_dir);
    g_notable = fopen(path, "w");
    snprintf(path, sizeof(path), "%s/rpd-hotaru.log", g_dir);
    log_init(path);

    config_load();
    g_host = config_host(host_index);
    if (!g_host || !g_host->addr[0]) {
        fprintf(stderr, "rpd: host %d in %s is not a LAN console\n", host_index, config_path());
        return 2;
    }

    av_log_set_level(AV_LOG_FATAL); /* a stream that starts or restarts mid-GOP makes the decoder chatty */
    g_pkt = av_packet_alloc();
    g_tmp = av_frame_alloc();
    g_last = av_frame_alloc();
    if (!g_pkt || !g_tmp || !g_last) return 1;

    chiaki_lib_init();
    chiaki_log_init(&g_log, CHIAKI_LOG_ALL, chiaki_log_cb, NULL);

    memset(&g_fb, 0, sizeof(g_fb));
    g_fb.count = 1;
    g_fb.count_byte = -1;
    for (int i = 0; i < CHIAKI_PROBE_STATES_MAX; i++) feedback_idle(&g_fb.extra[i]);
    chiaki_probe_set_feedback(&g_fb);
    chiaki_probe_setup_default(&g_setup);
    chiaki_probe_set_setup(&g_setup);
    chiaki_controller_state_set_idle(&g_pad0);
    lua_open_state(stdout);

    char sock_path[PATH_MAX];
    snprintf(sock_path, sizeof(sock_path), "%s/rpd.sock", g_dir);
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(sock_path) >= sizeof(sa.sun_path)) {
        fprintf(stderr, "rpd: socket path too long: %s\n", sock_path);
        return 1;
    }
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", sock_path);
    unlink(sock_path);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(lfd, 4) < 0) {
        fprintf(stderr, "rpd: cannot listen on %s: %s\n", sock_path, strerror(errno));
        return 1;
    }
    printf("rpd: host %d %s (%s), %dp, socket %s\n", host_index, g_host->nickname, g_host->addr, g_res, sock_path);
    fflush(stdout);

    if (autoconnect) {
        g_retry_until = now_ms() + RETRY_WINDOW_MS;
        g_retry_next = now_ms() + RETRY_EVERY_MS;
        session_connect();
    }
    while (!g_stop) {
        struct pollfd p = { .fd = lfd, .events = POLLIN };
        int r = poll(&p, 1, 100);
        maintenance();
        if (r > 0 && (p.revents & POLLIN)) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) handle_client(cfd);
        }
    }
    session_teardown();
    lua_close_state();
    close(lfd);
    unlink(sock_path);
    printf("rpd: stopped\n");
    return 0;
}
