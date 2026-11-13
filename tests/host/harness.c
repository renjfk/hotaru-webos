/* tests/host/harness.c - host behaviour tests: the real modules render offscreen and a fake
 * evdev FIFO drives the real gamepad code. Checks config persistence (round trip, corrupt
 * file), the feeder and the pacer, Home (layout, focus rings, D-pad / left stick / L1 R1
 * navigation, settings edited in place, quick cards), console grouping (a PIN pairing and the
 * PSN entry of the same console are one host with a route choice), the connecting screen,
 * the session, the stream menu (see-through, adjust, restart, Share + Options, Disconnect),
 * right-stick scrolling, hotplug, every step of Add a console, simulated registrations, the
 * confirm and passcode dialogs and the empty host list. The config lives in an isolated dir
 * (CHIAKI_CONFIG_DIR), wiped on every run. Run with tests/host/run.sh. */
#include <SDL.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>
#include <lvgl.h>

#include "app.h"
#include "chiaki/session.h"
#include "config/config.h"
#include "input/input_internal.h"
#include "log.h"
#include "media/feeder.h"
#include "media/pacer.h"
#include "net/update.h"
#include "ui/dialog.h"
#include "ui/home.h"
#include "ui/qr.h"
#include "ui/register.h"
#include "ui/splash.h"
#include "ui/stream_menu.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#define W      1920
#define H      1080
#define DEV    INPUT_DIR
#define POISON 0xff00ff00u /* what the draw buffer holds before LVGL draws: unpainted pixels show it */
static uint32_t *fb;
static int g_fail;

static void poison(lv_color_t *px, size_t n) {
    for (size_t i = 0; i < n; i++) px[i].full = POISON;
}
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
    int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * W + a->x1], &px[(y - a->y1) * w], (size_t)w * 4);
    poison(px, (size_t)w * (size_t)h); /* the next refresh must paint every pixel it flushes */
    lv_disp_flush_ready(drv);
}
/* pixels on screen that no refresh painted: the poison, or the poison under a fading layer */
static int unpainted(void) {
    int n = 0;
    for (int i = 0; i < W * H; i++) {
        unsigned r = (fb[i] >> 16) & 255, g = (fb[i] >> 8) & 255, b = fb[i] & 255;
        if (g > 170 && r < 90 && b < 90) n++;
    }
    return n;
}
static void pump(int ms) {
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    while (SDL_GetTicks() < end) {
        input_tick();
        app_tick();
        lv_timer_handler();
        SDL_Delay(5);
    }
}
/* the frame with its alpha, for scripts/make-screenshots.sh to lay over a game frame */
static void shot_rgba(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), SHOTS_DIR "/%s.pam", name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        g_fail++;
        return;
    }
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char px[4] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255, fb[i] >> 24 };
        fwrite(px, 1, 4, f);
    }
    fclose(f);
}
/* over_video: blend the UI by its alpha over a bright stand-in for the NDL video plane, as
 * the TV composites the app surface while streaming (and keep the alpha in <name>.pam) */
static void shot_ex(const char *name, bool over_video) {
    pump(50);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    char path[256];
    snprintf(path, sizeof(path), SHOTS_DIR "/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        g_fail++;
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255 };
        if (over_video) {
            unsigned a = fb[i] >> 24, x = (unsigned)(i % W), y = (unsigned)(i / W);
            unsigned v[3] = { 120 + (x * 135) / W, 200 - (y * 120) / H, ((x / 120 + y / 120) & 1) ? 230 : 90 };
            for (int c = 0; c < 3; c++) rgb[c] = (unsigned char)((rgb[c] * a + v[c] * (255 - a)) / 255);
        }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    if (over_video) shot_rgba(name);
}
static void shot(const char *name) { shot_ex(name, false); }
static unsigned fb_alpha(lv_coord_t x, lv_coord_t y) { return fb[y * W + x] >> 24; }
static void expect(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static int g_pad_w = -1, g_pad_r = -1;
static char g_node[256];
static void pad_plug(const char *node) {
    mkdir(DEV, 0755);
    snprintf(g_node, sizeof(g_node), "%s/%s", DEV, node);
    unlink(g_node);
    mkfifo(g_node, 0600);
    g_pad_r = open(g_node, O_RDONLY | O_NONBLOCK);
    g_pad_w = open(g_node, O_WRONLY | O_NONBLOCK);
}
static void pad_unplug(void) {
    close(g_pad_w);
    close(g_pad_r);
    unlink(g_node);
}

/* mkdir -p */
static void mkpath(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, 0755);
        *p = '/';
    }
    mkdir(tmp, 0755);
}

/* What the TV shows for a DualShock 4 on `node`: /sys/class/input/eventN/device is inputN,
 * which sits in the HID device's input/ directory, and hid-sony's power_supply hangs off that
 * HID device. A node with no tree here (event13, the DualSense) reports no battery. */
static void fake_sysfs_pad(const char *node) {
    char uhid[512], ev[520], path[600], link[640];
    snprintf(uhid, sizeof(uhid), "%s/../../../devices/virtual/misc/uhid/pad-%s", SYS_INPUT_DIR, node);
    snprintf(ev, sizeof(ev), "%s/%s", SYS_INPUT_DIR, node);
    mkpath(ev);
    mkpath(uhid);
    snprintf(path, sizeof(path), "%s/input/input%s", uhid, node + 5);
    mkpath(path);
    snprintf(path, sizeof(path), "%s/power_supply/sony_controller_battery_02:aa:bb:cc:dd:ee", uhid);
    mkpath(path);
    snprintf(path, sizeof(path), "%s/device", ev);
    snprintf(link, sizeof(link), "%s/input/input%s", uhid, node + 5);
    unlink(path);
    if (symlink(link, path) != 0) printf("fake sysfs: no device link\n");
    snprintf(path, sizeof(path), "%s/power_supply/sony_controller_battery_02:aa:bb:cc:dd:ee/capacity", uhid);
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("fake sysfs: no capacity file\n");
        return;
    }
    fputs("60\n", f);
    fclose(f);
}
static void fake_sysfs(void) {
    fake_sysfs_pad("event9");  /* the pad the tests start on */
    fake_sysfs_pad("event12"); /* ... and the one they replug later (a DualSense takes event13) */
}
static void pad_ev(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (uint16_t)type;
    ev.code = (uint16_t)code;
    ev.value = value;
    if (write(g_pad_w, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) printf("pad write failed\n");
}
static void pad_btn(int code) {
    pad_ev(EV_KEY, code, 1);
    pump(60);
    pad_ev(EV_KEY, code, 0);
    pump(60);
}
static void dpad(int code, int v) {
    pad_ev(EV_ABS, code, v);
    pump(80);
    pad_ev(EV_ABS, code, 0);
    pump(80);
}
static void center_of(lv_obj_t *o, int *x, int *y) {
    *x = (o->coords.x1 + o->coords.x2) / 2;
    *y = (o->coords.y1 + o->coords.y2) / 2;
}
static lv_obj_t *find_btn(lv_obj_t *o, const char *txt) {
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) return lv_obj_get_parent(o);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        lv_obj_t *r = find_btn(lv_obj_get_child(o, (int32_t)i), txt);
        if (r) return r;
    }
    return NULL;
}
static lv_obj_t *find_textarea(lv_obj_t *o, const char *placeholder) {
    if (lv_obj_check_type(o, &lv_textarea_class)) {
        const char *p = lv_textarea_get_placeholder_text(o);
        if (p && !strcmp(p, placeholder)) return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        lv_obj_t *r = find_textarea(lv_obj_get_child(o, (int32_t)i), placeholder);
        if (r) return r;
    }
    return NULL;
}
static void check_overflow(lv_obj_t *o) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_obj_t *p = lv_obj_get_parent(o);
    /* tile rows run past the screen edge; a tile's art is laid out at the focused size and zoomed */
    bool scrolls_by_design = o == home_row(HOME_CONSOLES) || o == home_row(HOME_SETTINGS) ||
                             (p && (p == home_row(HOME_CONSOLES) || p == home_row(HOME_SETTINGS))) ||
                             o == ui_scroll_fallback() || lv_obj_check_type(o, &lv_dropdownlist_class) ||
                             lv_obj_check_type(o, &lv_textarea_class);
    if (!scrolls_by_design && (lv_obj_get_scroll_bottom(o) > 0 || lv_obj_get_scroll_right(o) > 0)) {
        printf("OVERFLOW at (%d,%d)-(%d,%d), by %d right, %d below\n", o->coords.x1, o->coords.y1, o->coords.x2,
               o->coords.y2, (int)lv_obj_get_scroll_right(o), (int)lv_obj_get_scroll_bottom(o));
        g_fail++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) check_overflow(lv_obj_get_child(o, (int32_t)i));
}
static bool visible(lv_obj_t *o) {
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    return true;
}
static lv_obj_t *find_type(lv_obj_t *o, const lv_obj_class_t *cls) {
    if (lv_obj_check_type(o, cls)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        lv_obj_t *r = find_type(lv_obj_get_child(o, (int32_t)i), cls);
        if (r) return r;
    }
    return NULL;
}
/* Home's focus rings: every outline being shown fits inside whatever clips it */
static int g_rings, g_rings_clipped;
static void rings_fit(lv_obj_t *o) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_get_style_outline_width(o, 0) > 0 && lv_obj_get_style_outline_opa(o, 0) > LV_OPA_MIN) {
        g_rings++;
        lv_obj_refresh_ext_draw_size(o);
        lv_coord_t ext = _lv_obj_get_ext_draw_size(o);
        lv_area_t need = o->coords;
        lv_area_increase(&need, ext, ext);
        lv_obj_t *clip = lv_obj_get_parent(o);
        while (clip && lv_obj_has_flag(clip, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) clip = lv_obj_get_parent(clip);
        if (!clip || !_lv_area_is_in(&need, &clip->coords, 0)) {
            printf("CLIPPED ring at (%d,%d)-(%d,%d)\n", need.x1, need.y1, need.x2, need.y2);
            g_rings_clipped++;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) rings_fit(lv_obj_get_child(o, (int32_t)i));
}
static void home_check(const char *name) {
    pump(450); /* let the focus and layout animations land */
    check_overflow(lv_scr_act());
    rings_fit(lv_scr_act());
    shot(name);
}
static void key(SDL_Keycode k) {
    input_remote_key(k, KMOD_NONE);
    pump(90);
}
/* Typed keys queue up and LVGL drains one key per indev period (30 ms), which a loaded runner's
 * slow frames stretch: wait for the field to hold what was typed instead of guessing the time */
static bool wait_text(const char *want) {
    for (int i = 0; i < 300 && strcmp(ui_register_text(), want) != 0; i++) pump(20);
    return strcmp(ui_register_text(), want) == 0;
}
/* the same for the passcode sheet */
static bool wait_passcode(const char *want) {
    for (int i = 0; i < 300 && strcmp(ui_passcode_text(), want) != 0; i++) pump(20);
    return strcmp(ui_passcode_text(), want) == 0;
}
/* the keyboard coming up or going away: a pad press takes its own frames to land */
static bool wait_typing(bool typing) {
    for (int i = 0; i < 300 && ui_register_typing() != typing; i++) pump(20);
    return ui_register_typing() == typing;
}
/* Add a console's step: it fits 1080p and its focus rings are not clipped */
static void reg_check(const char *name) {
    pump(500);
    check_overflow(ui_register_root());
    rings_fit(ui_register_root());
    shot(name);
}
/* Circle until Home is back on its consoles row (bounded) */
static void home_reset(void) {
    for (int i = 0; i < 6 && (home_zone() != HOME_ROW || home_section() != HOME_CONSOLES || home_menu_open()); i++)
        pad_btn(0x132);
    pump(400);
}
static int find_host(bool lan, bool psn) {
    for (int i = 0; i < app_host_count(); i++)
        if (app_host(i)->lan == lan && app_host(i)->psn == psn) return i;
    return -1;
}
static int count_hosts(bool lan, bool psn) {
    int n = 0;
    for (int i = 0; i < app_host_count(); i++) n += app_host(i)->lan == lan && app_host(i)->psn == psn;
    return n;
}

static int check_clipping(lv_obj_t *o) {
    lv_obj_add_state(o, LV_STATE_FOCUS_KEY);
    lv_obj_refresh_ext_draw_size(o);
    lv_coord_t ext = _lv_obj_get_ext_draw_size(o);
    lv_area_t need = o->coords;
    lv_area_increase(&need, ext, ext);
    lv_obj_t *clip = lv_obj_get_parent(o);
    while (clip && lv_obj_has_flag(clip, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) clip = lv_obj_get_parent(clip);
    lv_obj_clear_state(o, LV_STATE_FOCUS_KEY);
    return clip && _lv_area_is_in(&need, &clip->coords, 0);
}

/* settings, consoles, the PSN account and the device UID survive config_save() + config_load() */
static void test_persistence(void) {
    expect(!config_load() && config_host_count() == 0 && !config_psn()->linked,
           "no config file: defaults, no consoles, no PSN account");
    expect(strncmp(config_path(), CONFIG_DIR "/", strlen(CONFIG_DIR) + 1) == 0,
           "the config file is in CHIAKI_CONFIG_DIR");
    char duid[CONFIG_DUID_LEN + 1];
    snprintf(duid, sizeof(duid), "%s", config_device_uid());
    config_t *c = config_get();
    c->stream.codec = CODEC_H264;
    c->stream.width = 1280;
    c->stream.height = 720;
    c->stream.bitrate_kbps = 23000;
    c->input.rumble = false;
    c->input.touchpad = false;
    config_host_t h = { .nickname = "Round \\ trip\nPS5",
                        .addr = "10.0.0.7",
                        .is_ps5 = true,
                        .server_mac = { 0x02, 1, 2, 3, 4, 5 },
                        .via_psn = true };
    memcpy(h.rp_regist_key, "0123456789abcdef", 16);
    for (int i = 0; i < 16; i++) h.rp_key[i] = (uint8_t)(0xf0 + i);
    for (int i = 0; i < 32; i++) h.psn_uid[i] = (uint8_t)(0x20 + i);
    expect(config_host_add(&h) && !config_host_add(&h), "a console is added once, its duplicate is refused");
    const uint8_t acc[8] = { 1, 2, 3, 4, 5, 6, 7, 0xff };
    config_psn_set(acc, "tester", "access=token", "refresh-token");
    expect(config_save(), "config_save() writes the file");

    c->stream.bitrate_kbps = 5000; /* change memory: what comes back must be the file */
    config_host_remove(0);
    config_psn_clear();
    bool loaded = config_load();
    const config_host_t *r = config_host(0);
    const config_psn_t *p = config_psn();
    expect(loaded && c->stream.codec == CODEC_H264 && c->stream.height == 720 && c->stream.bitrate_kbps == 23000 &&
               !c->input.rumble && !c->input.touchpad && config_host_count() == 1 && r &&
               !strcmp(r->nickname, h.nickname) && !strcmp(r->addr, h.addr) && r->is_ps5 &&
               !memcmp(r->server_mac, h.server_mac, 6) && !memcmp(r->rp_regist_key, h.rp_regist_key, 16) &&
               r->via_psn && !memcmp(r->psn_uid, h.psn_uid, 32) && !memcmp(r->rp_key, h.rp_key, 16) && p->linked &&
               !memcmp(p->account_id, acc, 8) && !strcmp(p->online_id, "tester") &&
               !strcmp(p->access_token, "access=token") && !strcmp(p->refresh_token, "refresh-token") &&
               !strcmp(config_device_uid(), duid),
           "settings, consoles, PSN account and device UID survive save + load");

    /* bad lines, unknown keys and older builds' keys */
    static const char junk[] = "garbage \x01\x02 no equals sign\nstream.bitrate_kbps=lots\nstream.fps=45\n"
                               "host.99.addr=10.0.0.9\nhost.0.server_mac=zz\ninput.cursor_speed=120\n"
                               "input.touchpad=0\ninput.motion=0\n";
    FILE *f = fopen(config_path(), "w");
    fputs(junk, f);
    for (int i = 0; i < 3000; i++) fputc('x', f); /* a line longer than any valid one */
    fputs("\ninput.touch=0\n", f);
    fclose(f);
    expect(config_load() && c->stream.bitrate_kbps == 15000 && c->stream.fps == 60 && config_host_count() == 0 &&
               !c->input.touchpad,
           "a corrupt file: bad lines and unknown keys are skipped, good ones kept, the rest defaults");
    unlink(config_path());
    config_load();
}

/* MAC is the identity when both are known; the address is a fallback (DHCP reuses addresses) */
static void test_host_identity(void) {
    config_host_t a = { .addr = "10.0.0.20", .server_mac = { 0x02, 0, 0, 0, 0, 0x0a } };
    config_host_t b = { .addr = "10.0.0.20", .server_mac = { 0x02, 0, 0, 0, 0, 0x0b } };
    expect(config_host_add(&a) && config_host_add(&b) && config_host_count() == 2,
           "two consoles with different MACs at the same address are both kept");
    config_host_t b2 = b;
    snprintf(b2.addr, sizeof(b2.addr), "%s", "10.0.0.99");
    expect(config_host_update(1, &b2) && config_host_count() == 2 && !strcmp(config_host(1)->addr, "10.0.0.99"),
           "a known MAC updates the console in place when its address changes");
    config_host_remove(1);
    config_host_remove(0);
}

/* a manually entered account id persists without marking the PSN account linked */
static void test_psn_account_id(void) {
    const uint8_t id[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    config_psn_set_account_id(id);
    expect(config_psn_has_account_id() && !config_psn()->linked, "a manual account ID is not a PSN link");
    expect(config_save(), "saving a manual account ID writes the file");
    config_psn_clear();
    expect(config_load() && config_psn_has_account_id() && !config_psn()->linked &&
               !memcmp(config_psn()->account_id, id, 8),
           "a manual account ID survives save + load");
    config_psn_clear();
    config_save();
    unlink(config_path());
    config_load();
}

/* media/feeder.c: buffers reach the worker in order, and a full queue refuses instead of blocking */
static SDL_sem *g_feed_gate;
static SDL_atomic_t g_feed_n;
static int g_feed_seen[8];

static void feed_cb(int tag, uint8_t *data, size_t len, uint32_t pushed_ms, void *user) {
    (void)user;
    (void)pushed_ms;
    SDL_SemWait(g_feed_gate); /* held: the worker stalls like a busy decoder */
    int n = SDL_AtomicGet(&g_feed_n), v = -1;
    if (len == sizeof(v)) memcpy(&v, data, sizeof(v));
    if (n < 8) g_feed_seen[n] = v * 10 + tag;
    SDL_AtomicSet(&g_feed_n, n + 1);
}

static void test_feeder(void) {
    g_feed_gate = SDL_CreateSemaphore(0);
    feeder_t *f = feeder_start("test-feeder", 3, feed_cb, NULL);
    expect(f != NULL, "a feeder starts its worker thread");
    int accepted = 0;
    for (int i = 0; i < 6; i++) accepted += feeder_push(f, i % 2, &i, sizeof(i));
    expect(accepted == 3, "a full feeder refuses buffers instead of blocking the caller");
    for (int i = 0; i < 3; i++) SDL_SemPost(g_feed_gate);
    for (int t = 0; t < 200 && SDL_AtomicGet(&g_feed_n) < 3; t++) SDL_Delay(5);
    expect(SDL_AtomicGet(&g_feed_n) == 3 && g_feed_seen[0] == 0 && g_feed_seen[1] == 11 && g_feed_seen[2] == 20,
           "the feeder delivers copies in push order on its own thread");
    feeder_stats_t st;
    feeder_stats(f, &st, true);
    expect(st.pushed == 3 && st.dropped == 3 && st.depth_max == 3, "feeder stats count pushes, drops and depth");
    feeder_stop(f);
    SDL_DestroySemaphore(g_feed_gate);
}

/* media/pacer.c: 60 fps that stalls 115 ms every 500 ms (what the TV's Wi-Fi did) plays out
 * evenly once the delay covers the stall, and the delay shrinks to about the stall */
static void test_pacer(void) {
    const double T = 1000.0 / 60;
    pacer_t p;
    pacer_reset(&p);
    double prev = 0, worst_step = 0, best_step = 1e9;
    unsigned restarts_settled = 0;
    for (int i = 0; i < 60 * 60; i++) {
        double sent = i * T, arrival = sent;
        double phase = sent - 500.0 * (int)(sent / 500.0);
        if (phase < 115) arrival = sent - phase + 115; /* held back by the stall, then a burst */
        double due = pacer_schedule(&p, arrival, T, 200);
        if (i >= 60 * 40) { /* settled: the last 20 s */
            double step = due - prev;
            if (step > worst_step) worst_step = step;
            if (step < best_step) best_step = step;
            if (i == 60 * 40) restarts_settled = p.restarts;
        }
        prev = due;
    }
    expect(worst_step < T * 1.05 && best_step > T * 0.95 && p.restarts == restarts_settled,
           "smoothing plays a stalling 60 fps stream at an even cadence");
    expect(p.delay_ms > 115 && p.delay_ms < 160, "smoothing shrinks its delay to about the stall");
    unsigned before = p.restarts;
    pacer_schedule(&p, prev + 400, T, 200); /* a stall longer than the delay */
    expect(p.restarts == before + 1, "a stall longer than the delay restarts the cadence");
}

/* net/update.c: the latest release's tag (vMAJOR.MINOR) against the installed version (three numbers) */
static void test_update_versions(void) {
    expect(update_newer("v0.3", "0.2.0") && update_newer("v1.0", "0.9.0") && update_newer("0.2.1", "0.2.0") &&
               update_newer("v0.10", "0.9.0"),
           "a higher release is newer, part by part as numbers (the v is optional, a missing part is 0)");
    expect(!update_newer("v0.2", "0.2.0") && !update_newer("v0.1", "0.2.0"), "the same or a lower release is not");
    expect(!update_newer("v0.2", "0.2.57") && update_newer("v0.3", "0.2.57"),
           "a prerelease build (<base>.<run>) is newer than its base release, not the release after it");
    expect(!update_newer("nightly", "0.2.0") && !update_newer("", "0.2.0") && !update_newer(NULL, "0.2.0"),
           "a tag that is no version is never newer");
}

int main(void) {
    log_init("/dev/stdout");
    SDL_Init(SDL_INIT_TIMER);
    lv_init();
    fb = calloc((size_t)W * H, 4);
    static lv_disp_draw_buf_t db;
    lv_color_t *draw = malloc((size_t)W * H * sizeof(lv_color_t));
    poison(draw, (size_t)W * H);
    lv_disp_draw_buf_init(&db, draw, NULL, W * H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &db;
    drv.flush_cb = flush_cb;
    drv.hor_res = W;
    drv.ver_res = H;
    lv_disp_t *disp = lv_disp_drv_register(&drv);

    if (system("rm -rf " DEV " " CONFIG_DIR) != 0) return 1; /* NOLINT(cert-env33-c): fixed paths, a fresh run */
    setenv("CHIAKI_CONFIG_DIR", CONFIG_DIR, 1);              /* never the developer's own settings */
    pad_plug("event9");
    fake_sysfs();
    theme_init(disp);
    test_persistence();
    test_host_identity();
    test_psn_account_id();
    test_feeder();
    test_pacer();
    test_update_versions();
    /* No demo consoles any more: seed three registered ones (not saved) so the Hosts list, the
     * D-pad focus and the connect test below have something to act on. */
    static const struct {
        const char *name, *addr;
        bool ps5;
    } seed[] = {
        { "Living-room PS5", "192.168.1.40", true },
        { "Bedroom PS5", "192.168.1.41", true },
        { "Study PS4 Pro", "192.168.1.52", false },
    };
    for (int i = 0; i < 3; i++) {
        config_host_t h = { .is_ps5 = seed[i].ps5, .server_mac = { 0x02, 0, 0, 0, 0, (uint8_t)(i + 1) } };
        snprintf(h.nickname, sizeof(h.nickname), "%s", seed[i].name);
        snprintf(h.addr, sizeof(h.addr), "%s", seed[i].addr);
        config_host_add(&h);
    }
    app_init(ui_app_changed);
    ui_create();
    const input_hooks_t hooks = { ui_tab_step, app_quit,          ui_set_pad_state, ui_scroll_fallback,
                                  ui_pad_back, ui_overlay_toggle, ui_pad_shift };
    input_init(ui_group(), &hooks);
    expect(input_pad_state() == PAD_ONLINE, "startup grabs the pad");
    int pct = -1;
    expect(input_pad_battery(&pct) && pct == 60, "the DualShock 4's level comes from its uhid power_supply node");
    expect(strcmp(home_pad_battery(), "Battery 60%") == 0, "Home's controller card shows the battery level");
    pump(900); /* the entrance animations */

    /* Home is focus driven: no pointer device at all, one key sink that takes the arrows itself */
    lv_obj_t *sink = lv_group_get_focused(ui_group());
    bool pointer_indev = false;
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in))
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER) pointer_indev = true;
    expect(sink && lv_obj_has_flag(sink, INPUT_FLAG_ARROWS) && !pointer_indev,
           "Home is focus driven: no pointer, the key sink holds the focus");
    expect(home_section() == HOME_CONSOLES && home_zone() == HOME_ROW && app_host_count() == 3 &&
               home_index() == app_selected(),
           "Home opens on the consoles row, on the selected console");

    /* layout: every zone and every settings panel fits 1080p and its focus rings are not clipped */
    int fails0 = g_fail;
    home_check("home");
    home_show(false, false);
    pump(100);
    home_show(true, true); /* fades in, as after a stream */
    int fading = 0;
    for (int i = 0; i < 6; i++) {
        pump(40);
        fading += unpainted();
    }
    expect(fading == 0, "Home fading in paints what is behind it: no stale buffer shows through");
    pump(400);
    key(SDLK_DOWN);
    expect(home_zone() == HOME_ACTIONS && home_item() == HOME_ACT_PLAY, "DOWN from the row: Start Remote Play");
    home_check("home-actions");
    key(SDLK_DOWN);
    expect(home_zone() == HOME_CARDS && home_item() == 0, "DOWN again: the quick cards");
    home_check("home-cards");
    key(SDLK_UP);
    key(SDLK_UP);
    key(SDLK_UP);
    expect(home_zone() == HOME_TOP, "UP from the row: the Consoles | Settings switch");
    home_check("home-top");
    key(SDLK_RIGHT);
    key(SDLK_DOWN);
    expect(home_section() == HOME_SETTINGS && home_zone() == HOME_ROW && home_index() == 0,
           "RIGHT on the switch shows Settings, DOWN enters its row");
    static const char *const panels[] = { "settings-stream", "settings-controller", "settings-account", "settings-log",
                                          "settings-about" };
    bool enters = true;
    char name[64];
    for (int p = 0; p < 5; p++) {
        if (p) key(SDLK_RIGHT);
        home_check(panels[p]);
        if (p == 4) break; /* About has nothing to focus */
        key(SDLK_DOWN);
        enters = enters && home_zone() == HOME_PANEL && home_item() == 0;
        snprintf(name, sizeof(name), "%s-row", panels[p]);
        home_check(name);
        pad_btn(0x132); /* Circle: back to the row */
    }
    expect(enters && home_zone() == HOME_ROW, "DOWN enters each settings panel, Circle leaves it");
    lv_obj_t *srow = home_row(HOME_SETTINGS);
    lv_area_t peek;
    lv_obj_get_coords(lv_obj_get_child(srow, 3), &peek);
    expect(lv_obj_get_style_opa(lv_obj_get_child(srow, 3), 0) == LV_OPA_50 && peek.x2 > 0 &&
               lv_obj_get_style_opa(lv_obj_get_child(srow, 2), 0) == LV_OPA_TRANSP,
           "the tile before the focus peeks in at the left edge: the row goes on that way");
    expect(g_fail == fails0, "no overflow on Home (consoles zones, every settings panel)");
    printf("  %d focus rings checked\n", g_rings);
    expect(g_rings >= 10 && g_rings_clipped == 0, "no focus ring clipped on Home");
    key(SDLK_LEFT);
    expect(home_index() == 3, "LEFT walks the settings row");

    /* L1/R1 switch the sections, L2/R2 do nothing, Back steps out one level at a time */
    pad_btn(0x134);
    pump(400);
    expect(home_section() == HOME_CONSOLES && home_zone() == HOME_ROW, "L1 goes to Consoles");
    pad_btn(0x135);
    pump(400);
    expect(home_section() == HOME_SETTINGS, "R1 goes to Settings");
    pad_btn(0x136);
    pad_btn(0x137);
    expect(home_section() == HOME_SETTINGS, "L2/R2 clicks do nothing");
    expect(ui_back() && home_section() == HOME_CONSOLES, "Back on the settings row returns to Consoles");
    pump(400);
    expect(!ui_back(), "Back on the consoles row has nothing to go back to (the remote's Back exits)");

    /* the D-pad and the left stick move along the row; a console tile selects that console */
    int sel0 = app_selected();
    dpad(ABS_HAT0X, 1);
    pump(300);
    expect(home_index() == sel0 + 1 && app_selected() == sel0 + 1,
           "D-pad RIGHT moves to the next console and selects it");
    shot("home-next");
    pad_ev(EV_ABS, ABS_X, 0);
    pump(120);
    pad_ev(EV_ABS, ABS_X, 128);
    pump(200);
    expect(home_index() == sel0 && app_selected() == sel0, "the left stick steps like the D-pad");
    pad_ev(EV_ABS, ABS_X, 255);
    pump(800);
    pad_ev(EV_ABS, ABS_X, 128);
    pump(200);
    expect(home_index() == app_host_count() && app_selected() == app_host_count() - 1,
           "holding the stick repeats, up to the Add tile at the end of the row");
    shot("home-add");
    key(SDLK_DOWN);
    expect(home_zone() == HOME_ROW && home_index() == app_host_count(),
           "the Add tile has no buttons or stream cards under it: DOWN stays on the tile");
    for (int i = 0; i < app_host_count(); i++) key(SDLK_LEFT);
    expect(home_index() == 0, "LEFT walks back to the first console");

    /* the touchpad is the console's: in the UI it does nothing */
    int idx0 = home_index();
    home_zone_t zone0 = home_zone();
    pad_ev(EV_ABS, ABS_MT_TRACKING_ID, 0);
    pad_ev(EV_ABS, ABS_MT_POSITION_X, 80);
    pad_ev(EV_ABS, ABS_MT_POSITION_Y, 80);
    pump(40);
    pad_ev(EV_ABS, ABS_MT_POSITION_X, 300);
    pump(40);
    pad_ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
    pump(40);
    expect(home_index() == idx0 && home_zone() == zone0, "on Home the touchpad does nothing");

    /* a quick card jumps to its setting; settings change in place and are saved */
    key(SDLK_DOWN);
    key(SDLK_DOWN);
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(400);
    expect(home_section() == HOME_SETTINGS && home_index() == 0 && home_zone() == HOME_PANEL && home_item() == 4,
           "the Smooth playback card jumps to its row in Stream settings");
    stream_settings_t *st = &config_get()->stream;
    key(SDLK_RIGHT);
    expect(st->smooth_ms == 100, "RIGHT on Smooth playback raises it");
    key(SDLK_UP);
    key(SDLK_UP);
    int codec0 = (int)st->codec;
    key(SDLK_RIGHT);
    bool to_hdr = st->codec == CODEC_H265_HDR && st->hdr;
    key(SDLK_RIGHT);
    bool stays = st->codec == CODEC_H265_HDR;
    key(SDLK_LEFT);
    key(SDLK_LEFT);
    bool to_h264 = st->codec == CODEC_H264 && !st->hdr;
    expect(codec0 == CODEC_H265 && to_hdr && stays && to_h264,
           "the codec row steps H.264 / H.265 / HDR with LEFT/RIGHT (HDR = the H.265 HDR profile)");
    key(SDLK_RIGHT);
    key(SDLK_DOWN);
    key(SDLK_LEFT);
    expect(st->codec == CODEC_H265 && st->bitrate_kbps == 14000, "LEFT on the bitrate bar lowers it by 1 Mbps");
    key(SDLK_RIGHT);
    pump(500);
    for (int i = 0; i < 12; i++) key(SDLK_RIGHT); /* held: the remote repeats faster than 400 ms */
    int held_up = st->bitrate_kbps;
    pump(500);
    for (int i = 0; i < 12; i++) key(SDLK_LEFT);
    expect(held_up == 40000 && st->bitrate_kbps == 15000,
           "a held key speeds the bitrate up (1, then 2, then 5 Mbps a step), both ways");
    key(SDLK_UP);
    key(SDLK_UP);
    key(SDLK_UP);
    key(SDLK_LEFT);
    bool to_720 = st->width == 1280 && st->height == 720;
    pad_btn(0x131); /* Cross cycles a choice */
    expect(home_item() == 0 && to_720 && st->width == 1920 && st->height == 1080 && st->bitrate_kbps == 15000,
           "Resolution: LEFT picks 720p, Cross cycles back to 1080p");
    shot("settings-edited");
    pump(1000); /* the debounced save */
    config_load();
    expect(config_get()->stream.smooth_ms == 100 && config_get()->stream.codec == CODEC_H265,
           "settings edited on Home are saved");
    pad_btn(0x132);
    key(SDLK_RIGHT);
    key(SDLK_DOWN);
    bool r0 = config_get()->input.rumble;
    pad_btn(0x131);
    bool flipped = config_get()->input.rumble == !r0;
    key(SDLK_LEFT);
    bool off = !config_get()->input.rumble;
    key(SDLK_RIGHT);
    expect(home_index() == 1 && flipped && off && config_get()->input.rumble,
           "Controller: Cross flips Rumble, LEFT/RIGHT set it off/on");

    /* the right stick scrolls the log on its panel (enough lines to scroll through first) */
    for (int i = 0; i < 60; i++) log_event(LOG_INFO, "TEST", "line %d of a long log", i);
    pad_btn(0x132);
    key(SDLK_RIGHT);
    key(SDLK_RIGHT);
    pump(300);
    lv_obj_t *log = ui_scroll_fallback();
    expect(home_index() == 3 && log != NULL, "the Log panel's console is what the right stick scrolls");
    if (log) {
        lv_obj_scroll_to_y(log, LV_COORD_MAX, LV_ANIM_OFF);
        pump(50);
        lv_coord_t y0 = lv_obj_get_scroll_y(log);
        pad_ev(EV_ABS, ABS_RZ, 0);
        pump(600);
        pad_ev(EV_ABS, ABS_RZ, 128);
        pump(100);
        expect(lv_obj_get_scroll_y(log) < y0 - 300, "right stick scrolls the log");
        shot("log-scrolled");
    }

    /* About: the Diagnostics switch (the focus skips the info rows) turns probe mode on and off */
    key(SDLK_RIGHT);
    pump(300);
    key(SDLK_DOWN);
    pad_btn(0x131);
    expect(home_index() == 4 && home_item() == 4 && log_probe_on() && config_get()->debug.probe,
           "About: Diagnostics is where the focus enters, Cross turns probe mode on");
    shot("settings-about");
    key(SDLK_UP);
    bool left = home_zone() != HOME_PANEL; /* only info rows above it: UP leaves the panel */
    key(SDLK_DOWN);
    pad_btn(0x131);
    expect(left && home_item() == 4 && !log_probe_on() && !config_get()->debug.probe,
           "UP leaves the panel (only info rows above); back on Diagnostics, Cross turns it off again");
    pad_btn(0x132);
    pad_btn(0x134);
    pump(400);

    pad_unplug();
    pump(100);
    expect(input_pad_state() == PAD_OFFLINE, "unplug -> offline");
    shot("pad-offline");
    pad_plug("event12");
    pump(1800);
    expect(input_pad_state() == PAD_ONLINE, "replug on a new node -> reconnected");
    pad_unplug();
    pump(100);
    setenv("SHIM_PAD_NAME", "DualSense Wireless Controller", 1);
    pad_plug("event13");
    pump(1800);
    bool ds = input_pad_state() == PAD_ONLINE && strstr(input_pad_name(), "DualSense") && !input_pad_rumbles() &&
              !input_pad_touches();
    expect(!input_pad_battery(&pct) && !home_pad_battery()[0],
           "a DualSense reports no battery (hid-generic: no power_supply), so the card hides it");
    pad_unplug();
    pump(100);
    unsetenv("SHIM_PAD_NAME");
    pad_plug("event12");
    pump(1800);
    expect(ds && input_pad_rumbles() && input_pad_touches(),
           "a DualSense on webOS cannot rumble and its touchpad only clicks; a DualShock 4 does both");

    /* Add a console: Cross on the Add tile opens the full-screen flow over Home. It is focus
     * driven (every key comes from the sink, no LVGL group of its own), Home stops drawing under
     * it, and every step fits 1080p with its focus rings unclipped */
    while (home_index() < app_host_count()) key(SDLK_RIGHT);
    pump(300);
    pad_btn(0x131);
    pump(500);
    lv_obj_t *reg = ui_register_root();
    expect(ui_register_active() && reg && lv_obj_get_parent(reg) == lv_layer_top() &&
               ui_register_step() == REG_METHOD && g_in.group == ui_group() &&
               lv_obj_has_flag(home_root(), LV_OBJ_FLAG_HIDDEN),
           "Cross on the Add tile opens Add a console over Home, driven by the key sink");
    fails0 = g_fail;
    g_rings = g_rings_clipped = 0;
    reg_check("add-method");
    key(SDLK_DOWN);
    expect(ui_register_focus() == 1, "DOWN moves to the PSN way in");
    reg_check("add-method-psn");
    key(SDLK_UP);
    lv_obj_t *qr = find_type(reg, &lv_canvas_class);
    const char *url = app_reg_psn_url();
    expect(qr && ui_qr_ok(lv_obj_get_parent(qr)) &&
               !strncmp(url, "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?", 66) &&
               strstr(url, config_device_uid()) && !strchr(url, ' '),
           "the PSN sign-in shows the authorize URL (with the device UID) as a QR code");

    /* the PIN path: the console, the PSN account (no ID stored yet), the address, the PIN */
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_KIND, "Cross on the PIN way in: which console");
    reg_check("add-kind");
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_ACCOUNT && !config_psn_has_account_id(),
           "with no PSN account ID stored, PIN pairing asks for it first");
    reg_check("add-account");
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    expect(ui_register_focus() == 2, "Type the account ID shows its field and moves onto it");
    key(SDLK_RETURN);
    lv_obj_t *kb = find_type(reg, &lv_keyboard_class);
    bool kb_up = wait_typing(true);
    pump(300); /* it slides up from the bottom */
    expect(kb_up && kb && visible(kb) && kb->coords.y2 >= H - 1,
           "Cross on the field brings the keyboard up from the bottom");
    key(SDLK_RETURN);        /* the highlighted key, q */
    input_remote_text("x!"); /* a USB keyboard types on meanwhile */
    bool typed = wait_text("qx!");
    reg_check("add-keyboard");
    expect(typed, "the D-pad types on the keyboard, a USB keyboard straight in");
    pad_btn(0x137); /* R2 */
    key(SDLK_RETURN);
    pad_btn(0x136); /* L2 */
    key(SDLK_RETURN);
    expect(!strcmp(ui_register_text(), "qx!Qq"), "L2 or R2 switches the keyboard between capitals and small letters");
    key(SDLK_BACKSPACE);
    key(SDLK_BACKSPACE);
    bool hinted = strstr(lv_btnmatrix_get_btn_text(kb, 34), "L2/R2") != NULL;
    key(SDLK_DOWN);
    key(SDLK_DOWN);
    key(SDLK_RETURN); /* Cross on the ABC key */
    bool upper = lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_UPPER;
    key(SDLK_RETURN);
    expect(hinted && upper && lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_LOWER &&
               !strcmp(ui_register_text(), "qx!"),
           "with a controller the ABC key also names L2/R2, greyed; Cross on it still switches the capitals");
    key(SDLK_UP);
    key(SDLK_UP);
    pad_btn(0x132);
    bool kb_away = wait_typing(false);
    expect(kb_away && ui_register_active() && ui_register_focus() == 2 && !strcmp(ui_register_text(), "qx!"),
           "Circle puts the keyboard away and types nothing");
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_ACCOUNT && !config_psn_has_account_id(), "an invalid account ID is refused");
    key(SDLK_LEFT);
    for (int i = 0; i < 4; i++) key(SDLK_BACKSPACE);
    input_remote_text("AQIDBAUGBwg=");
    bool id_typed = wait_text("AQIDBAUGBwg=");
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    expect(id_typed && ui_register_step() == REG_ADDR && config_psn_has_account_id() &&
               config_psn()->account_id[0] == 1 && !config_psn()->linked,
           "a valid account ID is stored, and the flow goes on to the address");
    expect(!strcmp(ui_register_text(), "192.168.1.240") && ui_register_focus() == 1,
           "the PS5 discovery found unpaired has its address filled in, Next focused");
    for (int i = 0; i < 12; i++) key(SDLK_BACKSPACE);
    input_remote_text("192.168.1.007");    /* the remote's digits and dot type straight in */
    bool zeros = wait_text("192.168.1.7"); /* a key a press and a release, so octets land one by one */
    expect(zeros, "an octet takes no leading zeros: 0 then 7 is 7");
    for (int i = 0; i < 1; i++) key(SDLK_BACKSPACE);
    input_remote_text("77");
    bool octets = wait_text("192.168.1.77");
    reg_check("add-address");
    expect(octets, "the address takes digits and dots, octet by octet");
    key(SDLK_LEFT);
    key(SDLK_LEFT);
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_PIN, "LEFT off the pad reaches Next, which goes on to the PIN");
    key(SDLK_DOWN);
    key(SDLK_DOWN);
    key(SDLK_RIGHT);
    key(SDLK_RETURN); /* 8, on the pad */
    input_remote_text("7654321");
    bool pin = wait_text("87654321");
    expect(pin && ui_register_focus() == 1,
           "the PIN: the pad and the digits type it, Pair takes the focus once it is complete");
    reg_check("add-pin");
    int hosts1 = app_host_count();
    key(SDLK_RETURN);
    pump(400);
    expect(ui_register_step() == REG_PAIRING && app_reg_state() == APP_REG_RUNNING, "Pair starts the pairing");
    reg_check("add-pairing");
    pump(2600);
    expect(ui_register_step() == REG_DONE && app_host_count() == hosts1 + 1 && app_host(app_selected()) &&
               !strcmp(app_host(app_selected())->addr, "192.168.1.77"),
           "paired: the console is added and selected, the flow says it is ready");
    reg_check("add-done");
    expect(g_fail == fails0, "no overflow on any step of Add a console");
    printf("  %d focus rings checked\n", g_rings);
    expect(g_rings >= 5 && g_rings_clipped == 0, "no focus ring clipped on Add a console");
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(500);
    expect(!ui_register_active() && !lv_obj_has_flag(home_root(), LV_OBJ_FLAG_HIDDEN) && home_index() == app_selected(),
           "Back to Home closes the flow on the new console");

    /* a wrong PIN (00000000 in the simulation): back on the PIN step with the reason */
    ui_register_open();
    pump(400);
    key(SDLK_RETURN);
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_ADDR, "with the account ID stored, the PIN path skips the account step");
    for (int i = 0; i < 12; i++) key(SDLK_BACKSPACE);
    input_remote_text("192.168.1.78");
    bool wrong_addr = wait_text("192.168.1.78");
    key(SDLK_LEFT);
    key(SDLK_LEFT);
    key(SDLK_RETURN);
    input_remote_text("00000000");
    bool wrong_pin = wait_text("00000000");
    key(SDLK_RETURN);
    pump(2800);
    expect(wrong_addr && wrong_pin && ui_register_step() == REG_PIN && app_reg_state() == APP_REG_FAILED &&
               app_reg_error()[0],
           "a refused PIN comes back to the PIN step with the reason");
    shot("add-pin-error");

    /* Circle steps back, and closes from the first step */
    pad_btn(0x132);
    pad_btn(0x132);
    pad_btn(0x132);
    expect(ui_register_step() == REG_METHOD, "Circle steps back to the first step");
    pad_btn(0x132);
    pump(300);
    expect(!ui_register_active() && !lv_obj_has_flag(home_root(), LV_OBJ_FLAG_HIDDEN), "... and closes from there");

    /* the manual PSN account id: bad base64 is rejected, a valid one is stored and used */
    config_psn_clear();
    app_reg_submit_pin("192.168.1.79", "12345678", true, "not base64!");
    expect(app_reg_state() == APP_REG_FAILED && strstr(app_reg_error(), "base64"), "a bad PSN account ID is rejected");
    app_reg_cancel();
    app_reg_submit_pin("192.168.1.79", "12345678", true, "AAAAAAAAAAA=");
    expect(app_reg_state() == APP_REG_FAILED && strstr(app_reg_error(), "base64"),
           "an all-zero PSN account ID is rejected");
    app_reg_cancel();
    app_reg_submit_pin("192.168.1.79", "12345678", true, "AQIDBAUGBwg=");
    expect(app_reg_state() == APP_REG_RUNNING && config_psn_has_account_id() && !config_psn()->linked &&
               config_psn()->account_id[0] == 1 && config_psn()->account_id[7] == 8,
           "a valid base64 PSN account ID is stored (not a PSN link) and used");
    pump(2600);
    expect(app_reg_state() == APP_REG_SUCCESS, "registration with a manual account ID succeeds");
    app_reg_cancel();
    config_psn_clear();
    config_save();

    /* signing in for the account ID: the PSN sign-in, then straight on to the address. Signing in
     * brings the account's consoles too: its PS5 has the name of the PS5 paired above, so the two
     * are one console with two routes */
    ui_register_open();
    pump(400);
    key(SDLK_RETURN);
    key(SDLK_RETURN);
    key(SDLK_RETURN); /* Sign in with your phone */
    expect(ui_register_step() == REG_PSN, "the account step's sign-in opens the PSN sign-in");
    reg_check("add-psn");
    input_remote_text("SIM-CODE-123");
    bool code = wait_text("SIM-CODE-123");
    hosts1 = app_host_count();
    int entries1 = config_host_count();
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(400);
    expect(code && ui_register_step() == REG_LINKING, "Link signs in");
    pump(3600);
    expect(ui_register_step() == REG_ADDR && config_psn()->linked && config_host_count() == entries1 + 2 &&
               app_host_count() == hosts1 + 1,
           "signed in, the flow goes on to the address; the PS5 joins the PIN-paired one, the PS4 is new");
    int via = 0;
    for (int i = 0; i < config_host_count(); i++)
        if (config_host(i)->via_psn) via++;
    expect(via == 2, "the PSN consoles are stored as via PSN");
    ui_register_close();
    pump(300);

    /* the PSN way in: the code (a USB keyboard and the on-screen one), signing in, the consoles found */
    ui_register_open();
    pump(400);
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    expect(ui_register_step() == REG_PSN, "the PSN way in opens the sign-in");
    input_remote_text("xX1ZwP");
    bool psn_typed = wait_text("xX1ZwP");
    key(SDLK_RETURN);
    key(SDLK_RETURN); /* q */
    pad_btn(0x132);
    bool kb_closed = wait_typing(false);
    bool psn_code = wait_text("xX1ZwPq");
    bool psn_ok = psn_typed && kb_closed && psn_code && !ui_register_typing() && ui_register_active();
    if (!psn_ok)
        printf("  [psn code] typed=%d closed=%d code=%d active=%d step=%d text='%s'\n", psn_typed, kb_closed, psn_code,
               ui_register_active(), (int)ui_register_step(), ui_register_text());
    expect(psn_ok, "the code types from a USB keyboard and the on-screen keys; Circle types nothing");
    shot("add-psn-typed");
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(3600);
    expect(ui_register_step() == REG_FOUND && config_psn()->linked, "signed in, the flow lists the consoles found");
    shot("add-found");
    key(SDLK_RETURN);
    pump(300);
    expect(!ui_register_active(), "Done closes it");

    int both = find_host(true, true), psn_only = find_host(false, true);
    expect(both >= 0 && !strcmp(app_host(both)->addr, "192.168.1.77") && app_host(both)->route == ROUTE_LAN &&
               psn_only >= 0 && app_host(psn_only)->route == ROUTE_PSN && !app_host(psn_only)->ps5,
           "the grouped console has both routes and prefers the PIN pairing; the PSN-only PS4 goes over PSN");
    app_select(both);
    app_connect();
    expect(app_conn_state() == CONN_CONNECTING && session_active_host() &&
               !strcmp(session_active_host()->addr, "192.168.1.77"),
           "the grouped console connects on the local network by default");
    app_disconnect();
    pump(100);
    app_select(psn_only);
    app_connect();
    expect(app_conn_state() == CONN_CONNECTING && session_active_host() && session_active_host()->via_psn &&
               !session_active_host()->addr[0],
           "a PSN-only console starts Remote Play over PSN");
    pump(3300);
    expect(app_conn_state() == CONN_STREAMING, "the PSN console session reaches Streaming");
    app_disconnect();
    pump(600);

    /* on Home: the grouped console offers the route next to Start; picking PSN connects over PSN */
    app_select(both);
    pump(400);
    home_reset();
    expect(home_index() == both, "Home follows the selection");
    key(SDLK_DOWN);
    key(SDLK_RIGHT);
    expect(home_zone() == HOME_ACTIONS && home_item() == HOME_ACT_ROUTE, "the grouped console has a route button");
    key(SDLK_RETURN);
    pump(300);
    expect(home_menu_open(), "Cross on it opens the route menu");
    home_check("home-route-menu");
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    pump(300);
    expect(!home_menu_open() && app_host(both)->route == ROUTE_PSN, "the route menu switches the console to PSN");
    shot("home-route-psn");
    int psn_entry = -1;
    for (int i = 0; i < config_host_count(); i++)
        if (config_host(i)->via_psn && config_host(i)->is_ps5) psn_entry = i;
    config_load();
    app_init(ui_app_changed); /* as after a restart */
    expect(psn_entry >= 0 && config_host(psn_entry)->prefer_psn && app_host(both)->route == ROUTE_PSN,
           "the route choice is saved and comes back after a restart");
    config_host_t routeless = { .server_mac = { 0x02, 0xc0, 0xff, 0xee, 0, 0x01 } }; /* a MAC alone */
    int shown = app_host_count();
    bool kept = config_host_add(&routeless);
    app_init(ui_app_changed);
    expect(kept && app_host_count() == shown, "an entry with neither an address nor a PSN route is not shown");
    if (kept) config_host_remove(config_host_find(&routeless));
    app_init(ui_app_changed);
    key(SDLK_LEFT);
    key(SDLK_RETURN);
    pump(400);
    expect(app_conn_state() == CONN_CONNECTING && session_active_host() && session_active_host()->via_psn &&
               !session_active_host()->addr[0] && home_connecting(),
           "Start Remote Play goes over the chosen route, behind the connecting screen");
    lv_obj_t *menu_hint = find_btn(lv_scr_act(), "The menu during the game");
    expect(menu_hint && visible(menu_hint) && find_btn(menu_hint, "Share") && find_btn(menu_hint, "Options"),
           "the connecting screen says Share + Options opens the menu during the game");
    shot("home-connecting");
    pad_btn(0x132);
    pump(500);
    expect(app_conn_state() == CONN_IDLE && !home_connecting(), "Circle cancels connecting");
    app_set_route(ROUTE_LAN);
    config_load();
    app_init(ui_app_changed);
    expect(app_host(both)->route == ROUTE_LAN && !config_host(psn_entry)->prefer_psn,
           "the route switches back to the local network, saved");

    /* a console the local network does not reach (".98" in the simulation) goes on over PSN in the
     * same connect, when it has a PSN route; the saved route stays the local network */
    app_reg_submit_pin("192.168.1.98", "12345678", false, NULL);
    pump(2600);
    app_reg_cancel();
    int ps4g = -1;
    for (int i = 0; i < app_host_count(); i++)
        if (app_host(i)->lan && app_host(i)->psn && !app_host(i)->ps5) ps4g = i;
    expect(ps4g >= 0 && !strcmp(app_host(ps4g)->addr, "192.168.1.98") && app_host(ps4g)->route == ROUTE_LAN,
           "a PIN-paired PS4 joins the PSN PS4 of the same name");
    app_select(ps4g);
    pump(300);
    app_connect();
    bool lan_first = app_conn_state() == CONN_CONNECTING && session_active_host() &&
                     !strcmp(session_active_host()->addr, "192.168.1.98") && !app_conn_over_psn();
    pump(1500);
    expect(lan_first && app_conn_state() == CONN_CONNECTING && session_active_host() &&
               session_active_host()->via_psn && !session_active_host()->addr[0] && app_conn_fallback() &&
               home_connecting(),
           "no answer on the local network: the same connect goes on over PSN");
    shot("home-connecting-fallback");
    pump(3300);
    expect(app_conn_state() == CONN_STREAMING && app_conn_over_psn() && app_host(ps4g)->route == ROUTE_LAN,
           "the fallback session streams; the saved route stays the local network");
    app_disconnect();
    pump(600);

    /* discovery: a paired console that answers is ready, with its firmware; ".98" has not answered
     * for a while, so it is offline and its next connect goes over PSN at once */
    pump(2400);
    int liv = -1;
    for (int i = 0; i < app_host_count(); i++)
        if (!strcmp(app_host(i)->name, "Living-room PS5")) liv = i;
    expect(liv >= 0 && app_host(liv)->state == HOST_READY && !strcmp(app_host(liv)->firmware, "9.40"),
           "discovery: a paired console that answers is ready, with its firmware");
    expect(app_host(ps4g)->state == HOST_OFFLINE, "one that has not answered for a while is offline");
    app_select(ps4g);
    pump(300);
    shot("home-offline");
    app_connect();
    expect(app_conn_state() == CONN_CONNECTING && app_conn_fallback() && app_conn_over_psn() && session_active_host() &&
               session_active_host()->via_psn,
           "an offline console with a PSN route connects over PSN at once");
    app_disconnect();
    pump(600);

    /* a console in rest mode (".97") is woken first; the session starts once it answers ready */
    app_reg_submit_pin("192.168.1.97", "12345678", true, NULL);
    pump(2600);
    app_reg_cancel();
    int rest = app_selected();
    pump(800);
    expect(app_host(rest) && app_host(rest)->state == HOST_STANDBY && !strcmp(app_host(rest)->addr, "192.168.1.97"),
           "a console in rest mode is heard as such");
    shot("home-rest-mode");
    app_connect();
    pump(300);
    expect(app_conn_state() == CONN_CONNECTING && app_conn_waking() && !session_active_host() && home_connecting(),
           "Start Remote Play on it wakes it up first");
    shot("home-waking");
    pump(3000);
    expect(!app_conn_waking() && app_host(rest)->state == HOST_READY && session_active_host() &&
               !strcmp(session_active_host()->addr, "192.168.1.97"),
           "once it answers ready, the session starts");
    pump(3500);
    expect(app_conn_state() == CONN_STREAMING, "... and streams");
    app_disconnect();
    pump(600);

    /* the unpaired PS5 is offered to Add a console; a paired console the router moved is followed
     * by its MAC (and then it is not offered any more) */
    bool offered = app_found_count() == 1 && !strcmp(app_found(0)->name, "PS5-240") &&
                   !strcmp(app_found(0)->addr, "192.168.1.240") && app_found(0)->ps5;
    config_host_t mv = { .is_ps5 = true, .server_mac = { 0x02, 0x00, 0x00, 0x00, 0x02, 0x40 } };
    snprintf(mv.nickname, sizeof(mv.nickname), "Attic PS5");
    snprintf(mv.addr, sizeof(mv.addr), "192.168.1.33");
    config_host_add(&mv);
    app_init(ui_app_changed); /* re-read the list, as after a registration */
    pump(800);
    int attic = -1;
    for (int i = 0; i < app_host_count(); i++)
        if (!strcmp(app_host(i)->name, "Attic PS5")) attic = i;
    expect(offered && attic >= 0 && !strcmp(app_host(attic)->addr, "192.168.1.240") &&
               app_host(attic)->state == HOST_READY && app_found_count() == 0,
           "a console found but not paired is offered; one the router moved is followed by its MAC");
    pump(100);
    config_load();
    bool saved = false;
    for (int i = 0; i < config_host_count(); i++)
        saved =
            saved || (!strcmp(config_host(i)->nickname, "Attic PS5") && !strcmp(config_host(i)->addr, "192.168.1.240"));
    expect(saved, "... and its new address is saved");
    app_init(ui_app_changed);
    for (int i = app_host_count() - 1; i >= 0; i--) /* the list as it was */
        if (!strcmp(app_host(i)->name, "Attic PS5") || !strcmp(app_host(i)->addr, "192.168.1.97")) {
            app_select(i);
            app_forget();
        }
    pump(300);

    /* a simulated PIN registration stores the console, selects it, and it is still there after a reload */
    int hosts0 = app_host_count(), entries0 = config_host_count();
    app_reg_submit_pin(" 192.168.1.60 ", "12345678", true, NULL);
    expect(app_reg_state() == APP_REG_RUNNING, "PIN registration starts");
    pump(2600);
    expect(app_reg_state() == APP_REG_SUCCESS && app_host_count() == hosts0 + 1 && app_selected() == hosts0 &&
               !strcmp(app_host(hosts0)->addr, "192.168.1.60") && home_index() == hosts0,
           "simulated PIN registration adds and selects the console");
    shot("home-registered");
    config_load();
    expect(config_host_count() == entries0 + 1 && !strcmp(config_host(config_host_count() - 1)->addr, "192.168.1.60"),
           "the registered console is in the saved config");
    app_reg_cancel();

    /* a console with one route: RIGHT from Start goes straight to its options */
    home_reset();
    key(SDLK_DOWN);
    key(SDLK_RIGHT);
    expect(home_item() == HOME_ACT_OPTIONS, "a console with one route has no route button");
    key(SDLK_LEFT);

    /* Start Remote Play on the console just registered: the session runs with that console */
    key(SDLK_RETURN);
    expect(app_conn_state() == CONN_CONNECTING && session_active_host() &&
               !strcmp(session_active_host()->addr, "192.168.1.60"),
           "Start Remote Play runs a session with the registered console");
    pump(3300);
    expect(app_conn_state() == CONN_STREAMING && app_conn_progress() == 100, "the session reaches Streaming");
    expect(lv_obj_has_flag(home_root(), LV_OBJ_FLAG_HIDDEN) && !home_connecting(), "Home goes away while streaming");

    /* while streaming the pad drives the console, not the UI: raw state is forwarded */
    int sel_streaming = app_selected();
    pad_ev(EV_KEY, 0x131, 1);      /* Cross */
    pad_ev(EV_KEY, 0x134, 1);      /* L1 */
    pad_ev(EV_ABS, ABS_X, 0);      /* left stick fully left */
    pad_ev(EV_ABS, ABS_RX, 200);   /* L2 pressed */
    pad_ev(EV_ABS, ABS_HAT0Y, -1); /* D-pad up */
    pump(80);
    const session_pad_t *sp = session_last_pad();
    expect(sp && (sp->buttons & SESSION_PAD_BTN_CROSS) && (sp->buttons & SESSION_PAD_BTN_L1) &&
               (sp->buttons & SESSION_PAD_BTN_DPAD_UP) && !(sp->buttons & SESSION_PAD_BTN_OPTIONS) && sp->l2 > 150 &&
               sp->left_x < -1000,
           "while streaming the raw pad state is forwarded to the console");
    pad_ev(EV_ABS, ABS_MT_TRACKING_ID, 1);
    pad_ev(EV_ABS, ABS_MT_POSITION_X, 500);
    pump(80);
    bool touch_on = session_last_pad()->touch_down && session_last_pad()->touch_x == 500;
    config_get()->input.touchpad = false;
    pump(80);
    bool touch_off = !session_last_pad()->touch_down;
    config_get()->input.touchpad = true;
    pad_ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
    pump(80);
    expect(touch_on && touch_off, "touches reach the console unless Settings > Controller > Touchpad is off");
    expect(input_streaming() && !ui_overlay_open() && app_selected() == sel_streaming &&
               app_conn_state() == CONN_STREAMING,
           "streaming: the pad's keys do not reach the UI");
    input_rumble(200, 100); /* forwarded to the pad; a no-op on the host, must not crash */
    pump(40);
    input_rumble(0, 0);
    pad_ev(EV_KEY, 0x131, 0);
    pad_ev(EV_KEY, 0x134, 0);
    pad_ev(EV_ABS, ABS_X, 128);
    pad_ev(EV_ABS, ABS_RX, 0);
    pad_ev(EV_ABS, ABS_HAT0Y, 0);
    pump(60);

    /* Back over the stream raises the stream menu: per-pixel alpha (display_set_video turns on
     * screen_transp on the TV), the video above it, a translucent sheet, opaque focused card */
    lv_disp_get_default()->driver->screen_transp = 1;
    expect(ui_back(), "Back over the stream is handled (it opens the menu)");
    pump(700);
    lv_obj_t *resume = stream_menu_card(0);
    expect(ui_overlay_open() && stream_menu_focus() == 0 && lv_obj_get_style_bg_opa(lv_scr_act(), 0) == LV_OPA_TRANSP,
           "the stream menu opens on Resume, with no screen background");
    expect(strcmp(stream_menu_pad_battery(), "60%") == 0, "its Controller card shows the pad's battery");
    shot_ex("stream-menu", true);
    expect(fb_alpha(W / 2, 200) == 0 && fb_alpha(20, H - 30) > 200 && fb_alpha(20, H - 30) < 255 &&
               fb_alpha((lv_coord_t)(resume->coords.x1 + 16),
                        (lv_coord_t)((resume->coords.y1 + resume->coords.y2) / 2)) == LV_OPA_COVER,
           "the video shows above the menu and through its sheet, not through the focused card");
    fails0 = g_fail;
    check_overflow(lv_scr_act());
    expect(g_fail == fails0, "no overflow on the stream menu");
    ui_overlay_toggle();
    pump(400);
    expect(!ui_overlay_open() && g_in.streaming, "closing the stream menu gives the pad back to the console");
    lv_disp_get_default()->driver->screen_transp = 0;

    /* Share + Options opens the menu and neither half reaches the console (the two are
     * forwarded PAD_CHORD_MS late); a lone Options still does, just late */
    pad_ev(EV_KEY, 0x138, 1); /* Share */
    pump(30);
    bool share_held = !(session_last_pad()->buttons & SESSION_PAD_BTN_SHARE);
    pad_ev(EV_KEY, 0x139, 1); /* Options */
    pump(60);
    expect(share_held && ui_overlay_open() && session_last_pad()->buttons == 0,
           "Share + Options opens the stream menu without either reaching the console");
    pad_ev(EV_KEY, 0x138, 0);
    pad_ev(EV_KEY, 0x139, 0);
    pump(60);
    pad_ev(EV_KEY, 0x132, 1); /* Circle */
    pump(60);
    bool circle_closed = g_in.streaming;
    pump(300);
    expect(circle_closed && !ui_overlay_open() && !(session_last_pad()->buttons & SESSION_PAD_BTN_CIRCLE),
           "Circle closes the stream menu and stays away from the console while held");
    pad_ev(EV_KEY, 0x132, 0);
    pump(40);
    pad_ev(EV_KEY, 0x139, 1);
    pump(40);
    bool options_late = !(session_last_pad()->buttons & SESSION_PAD_BTN_OPTIONS);
    pump(120);
    bool options_sent = session_last_pad()->buttons & SESSION_PAD_BTN_OPTIONS;
    pad_ev(EV_KEY, 0x139, 0);
    pump(160);
    expect(options_late && options_sent && !(session_last_pad()->buttons & SESSION_PAD_BTN_OPTIONS),
           "a lone Options reaches the console, PAD_CHORD_MS late");

    /* the quality card: Cross adjusts, LEFT/RIGHT change it, Circle leaves adjust mode and then
     * closes the menu, which restarts the stream with the new settings */
    ui_overlay_toggle();
    pump(600);
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    key(SDLK_LEFT);
    expect(stream_menu_focus() == 1 && stream_menu_adjusting() && st->height == 1080 && st->fps == 30 &&
               app_stream_settings_pending(),
           "Cross on Stream quality adjusts it with LEFT/RIGHT; the change waits for a restart");
    lv_disp_get_default()->driver->screen_transp = 1;
    shot_ex("stream-menu-adjust", true);
    lv_disp_get_default()->driver->screen_transp = 0;
    pad_btn(0x132);
    expect(!stream_menu_adjusting() && ui_overlay_open(), "Circle leaves adjust mode first");
    pad_btn(0x132);
    pump(300);
    bool restarting =
        app_conn_state() == CONN_CONNECTING && !session_active_host() && !ui_overlay_open() && home_connecting();
    pump(2100);
    bool reconnecting = app_conn_state() == CONN_CONNECTING && session_active_host() &&
                        !strcmp(session_active_host()->addr, "192.168.1.60");
    pump(3300);
    expect(restarting && reconnecting && app_conn_state() == CONN_STREAMING && !app_stream_settings_pending(),
           "closing the menu restarts the stream with the new video settings");
    st->fps = 60;

    /* Disconnect in the stream menu ends the session and Home comes back */
    ui_overlay_toggle();
    pump(600);
    for (int i = 0; i < 4; i++) key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(600);
    expect(app_conn_state() == CONN_IDLE && !session_active_host() && !ui_overlay_open() &&
               !lv_obj_has_flag(home_root(), LV_OBJ_FLAG_HIDDEN) &&
               lv_obj_get_style_bg_opa(lv_scr_act(), 0) == LV_OPA_COVER,
           "Disconnect in the stream menu stops the session and brings Home back");

    /* a console that asks for the login PIN gets the passcode sheet mid-connect: the pad or the
     * remote's digits type it, OK submits it and the handshake carries on */
    int hosts_ln = app_host_count();
    app_reg_submit_pin("192.168.1.99", "12345678", true, NULL);
    pump(2600);
    expect(app_reg_state() == APP_REG_SUCCESS && app_host_count() == hosts_ln + 1, "the login-PIN console registers");
    app_reg_cancel();
    app_connect();
    pump(1600);
    expect(app_conn_state() == CONN_CONNECTING && app_login_pin_pending() && ui_passcode_active() &&
               ui_dialog_focus() == 2,
           "the console's login-PIN request opens the passcode sheet, on its number pad");
    pump(500);
    shot("login-passcode");
    key(SDLK_RETURN);                    /* 1, on the pad */
    input_remote_key(SDLK_2, KMOD_NONE); /* a number key types; the text event it brings is dropped */
    input_remote_text("2");
    input_remote_key(SDLK_KP_3, KMOD_NONE);
    input_remote_text("4"); /* text alone: a USB keyboard */
    bool pin_typed = wait_passcode("1234") && ui_dialog_focus() == 1;
    key(SDLK_RETURN);
    pump(300);
    expect(pin_typed && !ui_passcode_active() && !app_login_pin_pending(),
           "the passcode typed on the pad and the remote's digits; OK takes the focus and submits it");
    pump(1600);
    expect(app_conn_state() == CONN_STREAMING, "the session reaches Streaming after the PIN");
    app_disconnect();
    pump(600);

    /* the options menu: Wake up, and Forget behind a confirmation */
    int n_before = app_host_count();
    app_select(0);
    pump(300);
    home_reset();
    key(SDLK_DOWN);
    key(SDLK_RIGHT);
    key(SDLK_RETURN);
    pump(300);
    expect(home_menu_open(), "Cross on the options button opens the options menu");
    home_check("home-options-menu");
    key(SDLK_RETURN);
    pump(300);
    expect(!home_menu_open() && !ui_confirm_active(), "Wake up runs at once");
    key(SDLK_RETURN);
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    pump(200);
    expect(ui_confirm_active() && app_host_count() == n_before,
           "Forget asks first, and the press that opened the dialog does not answer it");
    expect(ui_dialog_focus() == 0, "Cancel has the focus");
    shot("confirm-forget");
    key(SDLK_RETURN);
    pump(300);
    expect(!ui_confirm_active() && app_host_count() == n_before, "Cancel keeps the console");
    key(SDLK_RETURN);
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    pump(200);
    key(SDLK_RIGHT);
    bool d_right = ui_dialog_focus() == 1;
    key(SDLK_LEFT);
    expect(d_right && ui_dialog_focus() == 0, "LEFT/RIGHT move between Cancel and Forget");
    pad_btn(0x132);
    pump(300);
    expect(!ui_confirm_active() && app_host_count() == n_before, "Circle cancels the confirm dialog");
    key(SDLK_RETURN);
    key(SDLK_DOWN);
    key(SDLK_RETURN);
    pump(200);
    key(SDLK_RIGHT);
    shot("confirm-forget-focused");
    key(SDLK_RETURN);
    pump(400);
    expect(!ui_confirm_active() && app_host_count() == n_before - 1, "confirming the dialog forgets the console");

    /* forgetting a grouped console removes its PIN pairing and its PSN entry */
    both = find_host(true, true);
    int entries_before = config_host_count(), grouped_before = count_hosts(true, true);
    app_select(both);
    app_forget();
    pump(150);
    expect(both >= 0 && config_host_count() == entries_before - 2 && count_hosts(true, true) == grouped_before - 1,
           "forgetting a grouped console removes both of its entries");

    /* forgetting every console leaves the empty state: only the Add tile, nothing selected */
    while (app_host_count()) app_forget();
    pump(600);
    fails0 = g_fail;
    home_check("home-empty");
    app_connect();
    expect(app_selected() == -1 && !app_host(app_selected()) && app_conn_state() == CONN_IDLE &&
               config_host_count() == 0 && home_index() == 0 && g_fail == fails0,
           "empty host list: only the Add tile, no selection, Connect does nothing");
    config_load();
    expect(config_host_count() == 0, "forgetting is saved");
    pad_btn(0x131);
    pump(200);
    expect(ui_register_active(), "Cross on the lone Add tile opens Add a console");
    pad_btn(0x132);
    pump(300);
    expect(!ui_register_active(), "Circle closes it again");

    /* startup splash: shows on the top layer, then dismisses itself */
    splash_show();
    pump(250);
    expect(splash_active(), "splash is shown");
    shot("splash");
    pump(1800);
    expect(!splash_active(), "splash auto-dismisses");

    /* a newer release (as at startup): the notice waits for the splash, then names the version and where it is,
     * in the top bar's right end, above the tile row; it goes after 9 s */
    splash_show();
    update_fake("v99.0");
    pump(600);
    bool waited = splash_active() && !home_update_note()[0] && !strcmp(app_update_version(), "99.0");
    pump(1600);
    lv_obj_t *note = find_btn(home_root(), "github.com/renjfk/chiaki-webos");
    expect(waited && !splash_active() && !strcmp(home_update_note(), "Chiaki webOS 99.0 is available") && note &&
               visible(note),
           "a newer release: once the splash is gone, a notice names the version and where to get it");
    lv_area_t row;
    lv_obj_get_coords(home_row(HOME_CONSOLES), &row);
    expect(note && note->coords.y2 < row.y1 && note->coords.x2 < W && note->coords.y1 >= 0,
           "the notice sits in the top bar, above the tile row");
    expect(find_btn(home_root(), "Version 99.0 is available at github.com/renjfk/chiaki-webos") != NULL,
           "Settings > About names the new version too");
    fails0 = g_fail;
    home_check("home-update");
    expect(g_fail == fails0, "the update notice fits Home");
    pump(9000);
    expect(!home_update_note()[0] && note && !visible(note), "the notice goes after 9 s");
    update_fake("v0.0.1");
    pump(100);
    expect(!app_update_version()[0], "an older release is no update");

    printf("== %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
