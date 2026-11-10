/* ui/page_hosts.c - Hosts tab: console list + selected-console hero card. */
#include <stdint.h>
#include <stdio.h>

#include "../app.h"
#include "../log.h"
#include "pages.h"
#include "widgets.h"

#define MAX_HOSTS 8

static const uint32_t host_state_col[] = { COL_OK, COL_WARN, COL_DIM };
static const char *const host_state_txt[] = { "Ready", "Standby", "Offline" };

static lv_obj_t *g_host_btns[MAX_HOSTS];
static struct {
    lv_obj_t *name, *model, *addr, *fw, *quality, *play, *wake;
    ui_status_t status;
} g_hero;

void page_hosts_refresh(void) {
    int sel = app_selected();
    for (int j = 0; j < app_host_count() && j < MAX_HOSTS; j++) {
        if (j == sel) lv_obj_add_state(g_host_btns[j], LV_STATE_CHECKED);
        else lv_obj_clear_state(g_host_btns[j], LV_STATE_CHECKED);
    }
    const app_host_t *h = app_host(sel);
    lv_label_set_text(g_hero.name, h->name);
    lv_label_set_text(g_hero.model, h->model);
    lv_label_set_text(g_hero.addr, h->addr);
    lv_label_set_text(g_hero.fw, h->firmware);
    lv_label_set_text_fmt(g_hero.quality, "%dp" BULLET "%d fps", h->max_height, h->max_fps);
    ui_status_set(&g_hero.status, host_state_col[h->state], host_state_txt[h->state], false);
    if (h->state == HOST_STANDBY) lv_obj_clear_state(g_hero.wake, LV_STATE_DISABLED);
    else lv_obj_add_state(g_hero.wake, LV_STATE_DISABLED);
    if (h->state == HOST_OFFLINE) lv_obj_add_state(g_hero.play, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_hero.play, LV_STATE_DISABLED);
}

static void host_click_cb(lv_event_t *e) { app_select((int)(intptr_t)lv_event_get_user_data(e)); }
static void play_cb(lv_event_t *e) { (void)e; app_connect(); }
static void wake_cb(lv_event_t *e) { (void)e; app_wake(); }
static void forget_cb(lv_event_t *e) { (void)e; app_forget(); }
static void add_cb(lv_event_t *e) {
    (void)e;
    log_event(LOG_INFO, "HOST", "Add host: enter the console IP on the Input tab");
}

static void build_list(lv_obj_t *page) {
    lv_obj_t *left = ui_card(page, NULL);
    lv_obj_set_size(left, LV_PCT(40), LV_PCT(100));
    char caption[48];
    snprintf(caption, sizeof(caption), "%d found on your network", app_host_count());
    ui_card_header(left, LV_SYMBOL_HOME, "Consoles", caption);
    lv_obj_t *list = lv_list_create(left);
    ui_no_clip(list);   /* the hosts always fit; let the item focus rings breathe */
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, FOCUS_EXT + 2, 0);
    for (int i = 0; i < app_host_count() && i < MAX_HOSTS; i++) {
        const app_host_t *h = app_host(i);
        lv_obj_t *b = lv_list_add_btn(list, NULL, NULL);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_tile(b, h->state == HOST_OFFLINE ? &st_badge_soft : &st_badge, h->model, TILE_MD, FONT_CAPTION);
        lv_obj_t *col = ui_box(b, LV_FLEX_FLOW_COLUMN, 2);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        ui_text(col, TXT_BODY, h->name);
        ui_text(col, TXT_CAPTION, h->addr);
        lv_obj_t *st = ui_box(b, LV_FLEX_FLOW_ROW, SP_S);
        ui_led(st, host_state_col[h->state], 12);
        ui_text(st, TXT_CAPTION, host_state_txt[h->state]);
        lv_obj_add_event_cb(b, host_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_host_btns[i] = b;
    }
    lv_obj_t *add = ui_btn(left, BTN_GHOST, LV_SYMBOL_PLUS, "Add host");
    lv_obj_set_width(add, LV_PCT(100));
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, NULL);
}

static void build_hero(lv_obj_t *page) {
    lv_obj_t *hero = ui_card(page, &st_card_hero);
    lv_obj_set_height(hero, LV_PCT(100));
    lv_obj_set_flex_grow(hero, 1);
    lv_obj_align(ui_ps_buttons(hero), LV_ALIGN_TOP_RIGHT, -SP_L, SP_S);
    ui_text(hero, TXT_OVERLINE, "SELECTED CONSOLE");
    g_hero.name = ui_text(hero, TXT_DISPLAY, "");
    lv_obj_t *chips = ui_box(hero, LV_FLEX_FLOW_ROW, SP_M);
    ui_status_init(&g_hero.status, chips, COL_OK, "");
    ui_chip(chips, LV_SYMBOL_VIDEO, "", &g_hero.model);
    ui_chip(chips, LV_SYMBOL_WIFI, "", &g_hero.addr);
    ui_spacer(hero);

    lv_obj_t *stats = ui_box(hero, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(stats, LV_PCT(100));
    const char *stat_names[] = { "Firmware", "Best quality", "Remote Play" };
    lv_obj_t **stat_vals[] = { &g_hero.fw, &g_hero.quality, NULL };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *t = ui_box(stats, LV_FLEX_FLOW_COLUMN, SP_XS);
        lv_obj_add_style(t, &st_tile, 0);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_grow(t, 1);
        ui_text(t, TXT_CAPTION, stat_names[i]);
        lv_obj_t *v = ui_text(t, TXT_HEADING, i == 2 ? "Registered" : "");
        if (stat_vals[i]) *stat_vals[i] = v;
    }

    lv_obj_t *actions = ui_box(hero, LV_FLEX_FLOW_ROW, SP_L);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_style_pad_top(actions, SP_S, 0);
    g_hero.play = ui_btn(actions, BTN_PRIMARY, LV_SYMBOL_PLAY, "Start Remote Play");
    lv_obj_set_style_pad_hor(g_hero.play, SP_3XL, 0);
    lv_obj_add_event_cb(g_hero.play, play_cb, LV_EVENT_CLICKED, NULL);
    g_hero.wake = ui_btn(actions, BTN_SECONDARY, LV_SYMBOL_POWER, "Wake");
    lv_obj_add_event_cb(g_hero.wake, wake_cb, LV_EVENT_CLICKED, NULL);
    ui_spacer(actions);
    lv_obj_t *forget = ui_btn(actions, BTN_DANGER, LV_SYMBOL_TRASH, "Forget");
    lv_obj_add_event_cb(forget, forget_cb, LV_EVENT_CLICKED, NULL);
}

void page_hosts_build(lv_obj_t *page) {
    build_list(page);
    build_hero(page);
    page_hosts_refresh();
}
