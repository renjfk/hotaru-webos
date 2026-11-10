/* tests/host/harness.c - host behaviour tests: the real modules render offscreen and a fake
 * evdev FIFO drives the real gamepad code. Checks layout (no overflow, no clipped focus
 * rings), tabs (no swipe, animated L1/R1/click), D-pad + dropdown, press-cancel, the
 * simulated session, right-stick scrolling and hotplug. Run with tests/host/run.sh. */
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
#include "config/config.h"
#include "input/input_internal.h"
#include "log.h"
#include "ui/pages.h"
#include "ui/splash.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "ui/widgets.h"

#define W 1920
#define H 1080
#define DEV INPUT_DIR
static uint32_t *fb;
static int g_fail;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * W + a->x1], &px[(y - a->y1) * w], (size_t)w * 4);
    lv_disp_flush_ready(drv);
}
static void pump(int ms) {
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    while (SDL_GetTicks() < end) { input_tick(); app_tick(); lv_timer_handler(); SDL_Delay(5); }
}
static void shot(const char *name) {
    pump(50);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    char path[256];
    snprintf(path, sizeof(path), SHOTS_DIR "/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char rgb[3] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255 };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void expect(bool ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) g_fail++; }

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
static void pad_unplug(void) { close(g_pad_w); close(g_pad_r); unlink(g_node); }
static void pad_ev(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (uint16_t)type; ev.code = (uint16_t)code; ev.value = value;
    if (write(g_pad_w, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) printf("pad write failed\n");
}
static void pad_btn(int code) { pad_ev(EV_KEY, code, 1); pump(60); pad_ev(EV_KEY, code, 0); pump(60); }
static void dpad(int code, int v) { pad_ev(EV_ABS, code, v); pump(80); pad_ev(EV_ABS, code, 0); pump(80); }
static void cursor_at(int x, int y) { g_in.cx = (float)x; g_in.cy = (float)y; pointer_show(true); pump(60); }
static void center_of(lv_obj_t *o, int *x, int *y) { *x = (o->coords.x1 + o->coords.x2) / 2; *y = (o->coords.y1 + o->coords.y2) / 2; }
static lv_obj_t *find_btn(lv_obj_t *o, const char *txt) {
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) return lv_obj_get_parent(o);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        lv_obj_t *r = find_btn(lv_obj_get_child(o, (int32_t)i), txt);
        if (r) return r;
    }
    return NULL;
}
static lv_obj_t *first_on_page(int page, const lv_obj_class_t *cls) {
    lv_obj_t **o;
    _LV_LL_READ(&ui_group()->obj_ll, o) if (tabs_page_of(*o) == page && lv_obj_check_type(*o, cls)) return *o;
    return NULL;
}
static void check_overflow(lv_obj_t *o) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_obj_t *p = lv_obj_get_parent(o);
    bool scrolls_by_design = (p && lv_obj_check_type(p, &lv_tabview_class) && !lv_obj_check_type(o, &lv_btnmatrix_class)) ||
                             o == page_log_console() || lv_obj_check_type(o, &lv_dropdownlist_class) ||
                             lv_obj_check_type(o, &lv_textarea_class);
    if (!scrolls_by_design && (lv_obj_get_scroll_bottom(o) > 0 || lv_obj_get_scroll_right(o) > 0)) {
        printf("OVERFLOW at (%d,%d)-(%d,%d)\n", o->coords.x1, o->coords.y1, o->coords.x2, o->coords.y2);
        g_fail++;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) check_overflow(lv_obj_get_child(o, (int32_t)i));
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

int main(void) {
    log_init("/dev/stdout");
    SDL_Init(SDL_INIT_TIMER);
    lv_init();
    fb = calloc((size_t)W * H, 4);
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, malloc((size_t)W * H * sizeof(lv_color_t)), NULL, W * H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &db; drv.flush_cb = flush_cb; drv.hor_res = W; drv.ver_res = H;
    lv_disp_t *disp = lv_disp_drv_register(&drv);

    if (system("rm -rf " DEV) != 0) return 1;
    pad_plug("event9");
    theme_init(disp);
    config_load();
    app_init(ui_app_changed);
    ui_create();
    const input_hooks_t hooks = { ui_tab_step, app_quit, ui_set_pad_state, ui_scroll_fallback };
    input_init(ui_group(), ui_cursor_create(), &hooks);
    expect(input_pad_state() == PAD_ONLINE, "startup grabs the pad");
    pump(300);

    lv_obj_t *content = lv_tabview_get_content(lv_obj_get_parent(tabs_bar()));
    const char *names[] = { "hosts", "video", "input", "log" };
    int clipped = 0, rings = 0;
    for (int t = 0; t < TAB__COUNT; t++) {
        lv_tabview_set_act(lv_obj_get_parent(tabs_bar()), (uint32_t)t, LV_ANIM_OFF);
        pump(150);
        check_overflow(lv_scr_act());
        lv_obj_t **o;
        _LV_LL_READ(&ui_group()->obj_ll, o) if (tabs_page_of(*o) == t || (tabs_page_of(*o) < 0 && t == 0)) { rings++; clipped += !check_clipping(*o); }
        shot(names[t]);
    }
    expect(g_fail == 0, "no overflow on any tab");
    printf("  %d focus rings checked\n", rings);
    expect(clipped == 0, "no focus ring clipped");

    lv_tabview_set_act(lv_obj_get_parent(tabs_bar()), TAB_HOSTS, LV_ANIM_OFF); pump(150);
    cursor_at(1300, 600);
    g_in.click = true; pump(60);
    for (int i = 0; i < 20; i++) { g_in.cx -= 40; pump(20); }
    g_in.click = false; pump(500);
    expect(tabs_active() == TAB_HOSTS && lv_obj_get_scroll_x(content) == 0, "drag does not change tab (no swipe)");

    lv_coord_t step = lv_obj_get_content_width(content);
    pad_ev(EV_KEY, 0x135, 1);
    int mid = 0;
    Uint32 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 600) {
        input_tick(); app_tick(); lv_timer_handler(); SDL_Delay(5);
        lv_coord_t x = lv_obj_get_scroll_x(content);
        if (x > 0 && x < step) { mid++; if (mid == 8) shot("r1-mid"); }
    }
    pad_ev(EV_KEY, 0x135, 0); pump(50);
    lv_obj_t *codec = first_on_page(TAB_VIDEO, &lv_dropdown_class);
    expect(mid > 3 && tabs_active() == TAB_VIDEO && lv_obj_get_scroll_x(content) == step, "R1 slides to Video");
    expect(lv_group_get_focused(ui_group()) == codec, "R1 focuses the first Video control");
    pad_btn(0x134); pump(450); pad_btn(0x134); pump(450);
    expect(tabs_active() == TAB_LOG, "L1 twice wraps Video -> Hosts -> Log");
    pad_btn(0x136); pump(450); pad_btn(0x137); pump(450);
    expect(tabs_active() == TAB_LOG, "L2/R2 clicks do nothing");

    lv_area_t *ba = &((lv_btnmatrix_t *)tabs_bar())->button_areas[TAB_INPUT];
    cursor_at(tabs_bar()->coords.x1 + (ba->x1 + ba->x2) / 2, tabs_bar()->coords.y1 + (ba->y1 + ba->y2) / 2);
    g_in.click = true; pump(60); g_in.click = false;
    mid = 0; t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 600) {
        input_tick(); app_tick(); lv_timer_handler(); SDL_Delay(5);
        lv_coord_t x = lv_obj_get_scroll_x(content);
        if (x != 3 * step && x != 2 * step) mid++;
    }
    expect(mid > 3 && tabs_active() == TAB_INPUT, "tab pill click slides to Input");

    lv_tabview_set_act(lv_obj_get_parent(tabs_bar()), TAB_HOSTS, LV_ANIM_OFF); pump(100);
    lv_group_focus_obj(tabs_bar()); pump(50);
    dpad(ABS_HAT0Y, 1);
    expect(input_keypad_mode() && tabs_page_of(lv_group_get_focused(ui_group())) == TAB_HOSTS, "D-pad DOWN: keypad mode, focus on a host");
    pad_btn(0x135); pump(450);
    int c0 = (int)config_get()->stream.codec;
    pad_btn(0x131); pump(100);
    expect(lv_dropdown_is_open(codec), "Cross (keypad mode) opens the codec dropdown");
    dpad(ABS_HAT0Y, 1); pump(100);
    shot("dpad-dropdown");
    pad_btn(0x131); pump(150);
    expect(!lv_dropdown_is_open(codec) && (int)config_get()->stream.codec == c0 + 1, "D-pad DOWN + Cross picks the next codec (config updated)");

    lv_tabview_set_act(lv_obj_get_parent(tabs_bar()), TAB_HOSTS, LV_ANIM_OFF); pump(150);
    lv_obj_t *connect = find_btn(lv_scr_act(), "Connect");
    int x, y;
    center_of(connect, &x, &y);
    cursor_at(x, y);
    g_in.click = true; pump(100);
    g_in.cy = (float)(y - 200); pump(100);
    expect(!lv_obj_has_state(connect, LV_STATE_PRESSED), "dragged off Connect: unpressed");
    g_in.cy = (float)y; pump(100);
    g_in.click = false; pump(150);
    expect(app_conn_state() == CONN_IDLE, "release after leaving: no click");
    g_in.click = true; pump(100); g_in.click = false; pump(150);
    expect(app_conn_state() == CONN_CONNECTING, "press+release inside: Connect fires");
    pump(3300);
    expect(app_conn_state() == CONN_STREAMING, "simulated session reaches Streaming");
    shot("streaming");
    app_disconnect(); pump(50);

    lv_tabview_set_act(lv_obj_get_parent(tabs_bar()), TAB_LOG, LV_ANIM_OFF); pump(150);
    lv_obj_t *log = page_log_console();
    center_of(log, &x, &y);
    cursor_at(x, y);
    lv_obj_scroll_to_y(log, LV_COORD_MAX, LV_ANIM_OFF); pump(50);
    lv_coord_t y0 = lv_obj_get_scroll_y(log);
    pad_ev(EV_ABS, ABS_RZ, 0); pump(600); pad_ev(EV_ABS, ABS_RZ, 128); pump(100);
    expect(lv_obj_get_scroll_y(log) < y0 - 300, "right stick scrolls the log");
    shot("log-scrolled");

    pad_unplug(); pump(100);
    expect(input_pad_state() == PAD_OFFLINE, "unplug -> offline");
    shot("pad-offline");
    pad_plug("event12"); pump(1800);
    expect(input_pad_state() == PAD_ONLINE, "replug on a new node -> reconnected");

    /* startup splash: shows on the top layer, then dismisses itself */
    splash_show();
    pump(250);
    expect(splash_active(), "splash is shown");
    shot("splash");
    pump(1800);
    expect(!splash_active(), "splash auto-dismisses");

    printf("== %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
