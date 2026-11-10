/* ui/page_hosts.c - Hosts tab: registered consoles (or an empty state) + selected-console hero card. */
#include <stdint.h>
#include <stdio.h>

#include "../app.h"
#include "../config/config.h"
#include "pages.h"
#include "register.h"
#include "widgets.h"

#define MAX_HOSTS CONFIG_MAX_HOSTS

static const uint32_t host_state_col[] = { COL_OK, COL_WARN, COL_DIM, COL_INFO };
static const char *const host_state_txt[] = { "Ready", "Standby", "Offline", "Unknown" };

static lv_obj_t *g_host_btns[MAX_HOSTS];
static lv_obj_t *g_list, *g_empty, *g_caption;
static struct {
    lv_obj_t *name, *model, *addr, *fw, *quality, *paired, *play, *wake, *forget;
    ui_status_t status;
} g_hero;

static void set_enabled(lv_obj_t *o, bool on) {
    if (on) lv_obj_clear_state(o, LV_STATE_DISABLED);
    else lv_obj_add_state(o, LV_STATE_DISABLED);
}

void page_hosts_refresh(void) {
    int sel = app_selected();
    for (int j = 0; j < app_host_count() && j < MAX_HOSTS; j++) {
        if (j == sel) lv_obj_add_state(g_host_btns[j], LV_STATE_CHECKED);
        else lv_obj_clear_state(g_host_btns[j], LV_STATE_CHECKED);
    }
    const app_host_t *h = app_host(sel);
    if (!h) {   /* nothing registered: keep the card's shape, nothing to act on */
        lv_label_set_text(g_hero.name, "No console yet");
        lv_label_set_text(g_hero.model, "-");
        lv_label_set_text(g_hero.addr, "-");
        lv_label_set_text(g_hero.fw, "-");
        lv_label_set_text(g_hero.quality, "-");
        lv_label_set_text(g_hero.paired, "Not paired");
        ui_status_set(&g_hero.status, COL_DIM, "Add a console", false);
        set_enabled(g_hero.play, false);
        set_enabled(g_hero.wake, false);
        set_enabled(g_hero.forget, false);
        return;
    }
    lv_label_set_text(g_hero.name, h->name);
    lv_label_set_text(g_hero.model, h->model);
    lv_label_set_text(g_hero.addr, h->addr);
    lv_label_set_text(g_hero.fw, h->firmware);
    lv_label_set_text_fmt(g_hero.quality, "%dp" BULLET "%d fps", h->max_height, h->max_fps);
    lv_label_set_text(g_hero.paired, "Registered");
    ui_status_set(&g_hero.status, host_state_col[h->state], host_state_txt[h->state], false);
    set_enabled(g_hero.wake, h->state == HOST_STANDBY || h->state == HOST_UNKNOWN);
    set_enabled(g_hero.play, h->state != HOST_OFFLINE);
    set_enabled(g_hero.forget, true);
}

static void host_click_cb(lv_event_t *e) { app_select((int)(intptr_t)lv_event_get_user_data(e)); }
static void play_cb(lv_event_t *e) { (void)e; app_connect(); }
static void wake_cb(lv_event_t *e) { (void)e; app_wake(); }
static void forget_cb(lv_event_t *e) { (void)e; app_forget(); }
static void add_cb(lv_event_t *e) { (void)e; ui_register_open(); }

static void fill_list(void) {
    lv_obj_clean(g_list);
    int n = app_host_count();
    for (int i = 0; i < MAX_HOSTS; i++) g_host_btns[i] = NULL;
    for (int i = 0; i < n && i < MAX_HOSTS; i++) {
        const app_host_t *h = app_host(i);
        lv_obj_t *b = lv_list_add_btn(g_list, NULL, NULL);
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
    lv_label_set_text_fmt(g_caption, "%d registered with this TV", n);
    if (n) {
        lv_obj_clear_flag(g_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
    }
}

void page_hosts_reload(void) {
    fill_list();
    page_hosts_refresh();
}

static void build_list(lv_obj_t *page) {
    lv_obj_t *left = ui_card(page, NULL);
    lv_obj_set_size(left, LV_PCT(40), LV_PCT(100));
    lv_obj_t *hdr = ui_card_header(left, LV_SYMBOL_HOME, "Consoles", "");
    g_caption = lv_obj_get_child(lv_obj_get_child(hdr, 1), 1);   /* ui_card_header: tile, (title, caption) */

    /* five consoles fit; past that the list scrolls (its padding keeps the item focus rings inside) */
    g_list = lv_list_create(left);
    lv_obj_set_width(g_list, LV_PCT(100));
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_pad_all(g_list, FOCUS_EXT + 2, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);

    g_empty = ui_box(left, LV_FLEX_FLOW_COLUMN, SP_M);
    lv_obj_set_width(g_empty, LV_PCT(100));
    lv_obj_set_flex_grow(g_empty, 1);
    lv_obj_set_flex_align(g_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_tile(g_empty, &st_badge_soft, LV_SYMBOL_PLUS, TILE_MD, NULL);
    ui_text(g_empty, TXT_HEADING, "No consoles yet");
    lv_obj_t *hint = ui_text(g_empty, TXT_CAPTION, "Choose Add host below to pair a PS5 or PS4 with its PIN or "
                                                   "your PSN account");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *add = ui_btn(left, BTN_GHOST, LV_SYMBOL_PLUS, "Add host");
    lv_obj_set_width(add, LV_PCT(100));
    lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, NULL);
    fill_list();
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
    lv_obj_t **stat_vals[] = { &g_hero.fw, &g_hero.quality, &g_hero.paired };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *t = ui_box(stats, LV_FLEX_FLOW_COLUMN, SP_XS);
        lv_obj_add_style(t, &st_tile, 0);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_grow(t, 1);
        ui_text(t, TXT_CAPTION, stat_names[i]);
        *stat_vals[i] = ui_text(t, TXT_HEADING, "");
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
    g_hero.forget = ui_btn(actions, BTN_DANGER, LV_SYMBOL_TRASH, "Forget");
    lv_obj_add_event_cb(g_hero.forget, forget_cb, LV_EVENT_CLICKED, NULL);
}

void page_hosts_build(lv_obj_t *page) {
    build_list(page);
    build_hero(page);
    page_hosts_refresh();
}
