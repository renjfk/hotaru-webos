/* ui/motion.c - animation helpers (see motion.h). */
#include "motion.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_opa_t clamp_opa(int32_t v) { return (lv_opa_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void ex_x(void *o, int32_t v) { lv_obj_set_x(o, (lv_coord_t)v); }
static void ex_y(void *o, int32_t v) { lv_obj_set_y(o, (lv_coord_t)v); }
static void ex_w(void *o, int32_t v) { lv_obj_set_width(o, (lv_coord_t)v); }
static void ex_h(void *o, int32_t v) { lv_obj_set_height(o, (lv_coord_t)v); }
static void ex_tx(void *o, int32_t v) { lv_obj_set_style_translate_x(o, (lv_coord_t)v, 0); }
static void ex_ty(void *o, int32_t v) { lv_obj_set_style_translate_y(o, (lv_coord_t)v, 0); }
static void ex_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, clamp_opa(v), 0); }
static void ex_bg_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa(o, clamp_opa(v), 0); }
static void ex_text_opa(void *o, int32_t v) { lv_obj_set_style_text_opa(o, clamp_opa(v), 0); }
static void ex_img_opa(void *o, int32_t v) { lv_obj_set_style_img_opa(o, clamp_opa(v), 0); }
static void ex_border_opa(void *o, int32_t v) { lv_obj_set_style_border_opa(o, clamp_opa(v), 0); }
static void ex_outline_opa(void *o, int32_t v) { lv_obj_set_style_outline_opa(o, clamp_opa(v), 0); }
static void ex_zoom(void *o, int32_t v) { lv_img_set_zoom(o, (uint16_t)(v < 1 ? 1 : v)); }
static void ex_grow(void *o, int32_t v) {
    lv_obj_set_style_transform_width(o, (lv_coord_t)v, 0);
    lv_obj_set_style_transform_height(o, (lv_coord_t)v, 0);
}

static const lv_anim_exec_xcb_t g_exec[] = {
    [MO_X] = ex_x,
    [MO_Y] = ex_y,
    [MO_W] = ex_w,
    [MO_H] = ex_h,
    [MO_TX] = ex_tx,
    [MO_TY] = ex_ty,
    [MO_OPA] = ex_opa,
    [MO_BG_OPA] = ex_bg_opa,
    [MO_TEXT_OPA] = ex_text_opa,
    [MO_IMG_OPA] = ex_img_opa,
    [MO_BORDER_OPA] = ex_border_opa,
    [MO_OUTLINE_OPA] = ex_outline_opa,
    [MO_ZOOM] = ex_zoom,
    [MO_GROW] = ex_grow,
};

int32_t motion_get(lv_obj_t *o, motion_prop_t p) {
    switch (p) {
        case MO_X: return lv_obj_get_style_x(o, 0);
        case MO_Y: return lv_obj_get_style_y(o, 0);
        case MO_W: return lv_obj_get_style_width(o, 0);
        case MO_H: return lv_obj_get_style_height(o, 0);
        case MO_TX: return lv_obj_get_style_translate_x(o, 0);
        case MO_TY: return lv_obj_get_style_translate_y(o, 0);
        case MO_OPA: return lv_obj_get_style_opa(o, 0);
        case MO_BG_OPA: return lv_obj_get_style_bg_opa(o, 0);
        case MO_TEXT_OPA: return lv_obj_get_style_text_opa(o, 0);
        case MO_IMG_OPA: return lv_obj_get_style_img_opa(o, 0);
        case MO_BORDER_OPA: return lv_obj_get_style_border_opa(o, 0);
        case MO_OUTLINE_OPA: return lv_obj_get_style_outline_opa(o, 0);
        case MO_ZOOM: return lv_img_get_zoom(o);
        case MO_GROW: return lv_obj_get_style_transform_width(o, 0);
    }
    return 0;
}

void motion_set(lv_obj_t *o, motion_prop_t p, int32_t v) {
    lv_anim_del(o, g_exec[p]);
    g_exec[p](o, v);
}

void motion_from(lv_obj_t *o, motion_prop_t p, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                 lv_anim_path_cb_t path) {
    if (!ms && !delay) {
        motion_set(o, p, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, g_exec[p]);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms ? ms : 1);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void motion_to(lv_obj_t *o, motion_prop_t p, int32_t to, uint32_t ms, uint32_t delay, lv_anim_path_cb_t path) {
    int32_t from = motion_get(o, p);
    if (from == to && !lv_anim_get(o, g_exec[p])) return;
    motion_from(o, p, from, to, ms, delay, path);
}

static void then_ready_cb(lv_anim_t *a) {
    void (*ready)(lv_obj_t *) = (void (*)(lv_obj_t *))lv_anim_get_user_data(a);
    if (ready) ready(a->var);
}

void motion_then(lv_obj_t *o, motion_prop_t p, int32_t to, uint32_t ms, lv_anim_path_cb_t path,
                 void (*ready)(lv_obj_t *o)) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, g_exec[p]);
    lv_anim_set_values(&a, motion_get(o, p), to);
    lv_anim_set_time(&a, ms ? ms : 1);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, (void *)ready);
    lv_anim_set_ready_cb(&a, then_ready_cb);
    lv_anim_start(&a);
}

int32_t motion_spring(const lv_anim_t *a) {
    if (a->act_time >= (int32_t)a->time) return a->end_value;
    float t = (float)a->act_time / (float)a->time;
    float p = 1.0f - expf(-6.5f * t) * cosf(8.5f * t);
    return a->start_value + (int32_t)lroundf(p * (float)(a->end_value - a->start_value));
}

int32_t motion_shake(const lv_anim_t *a) {
    if (a->act_time >= (int32_t)a->time) return a->end_value;
    float t = (float)a->act_time / (float)a->time;
    return (int32_t)(lv_trigo_sin((int16_t)(t * 1080)) * (1 - t) * (float)a->start_value / LV_TRIGO_SIN_MAX);
}

void motion_enter(lv_obj_t *o, uint32_t delay, lv_coord_t dy) {
    motion_from(o, MO_TY, dy, 0, 340, delay, lv_anim_path_ease_out);
    motion_from(o, MO_OPA, 0, 255, 260, delay, lv_anim_path_ease_out);
}

/* ---------- rolling text: one exec for both halves, v in -256..256 (0 = in place) ---------- */

#define ROLL_DY    26
#define ROLL_SLOTS 24
static struct {
    lv_obj_t *label;
    char text[96];
    int dir;
    lv_opa_t opa;
} g_roll[ROLL_SLOTS]; /* opa: at rest */

static int roll_slot(lv_obj_t *l) {
    int free_i = -1;
    for (int i = 0; i < ROLL_SLOTS; i++) {
        if (g_roll[i].label == l) return i;
        if (!g_roll[i].label && free_i < 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;
    g_roll[free_i].label = l;
    g_roll[free_i].opa = LV_OPA_COVER;
    return free_i;
}

static void roll_forget(lv_obj_t *l) {
    for (int i = 0; i < ROLL_SLOTS; i++)
        if (g_roll[i].label == l) g_roll[i].label = NULL;
}

static void roll_exec(void *o, int32_t v) {
    int i = roll_slot(o);
    lv_obj_set_style_translate_y(o, (lv_coord_t)(v * ROLL_DY / 256), 0);
    lv_obj_set_style_text_opa(o, clamp_opa(g_roll[i].opa * (256 - LV_ABS(v)) / 256), 0);
}

static void roll_done(lv_anim_t *a) { roll_forget(a->var); }

static void roll_swap(lv_anim_t *a) {
    lv_obj_t *l = a->var;
    int i = roll_slot(l);
    lv_label_set_text(l, g_roll[i].text);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, l);
    lv_anim_set_exec_cb(&b, roll_exec);
    lv_anim_set_values(&b, g_roll[i].dir * 256, 0);
    lv_anim_set_time(&b, 200);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&b, roll_done);
    lv_anim_start(&b);
}

static void roll_deleted_cb(lv_event_t *e) { roll_forget(lv_event_get_target(e)); }

void motion_roll_text(lv_obj_t *label, const char *text, int dir) {
    bool rolling = lv_anim_get(label, roll_exec) != NULL;
    if (!strcmp(lv_label_get_text(label), text) && !rolling) return;
    int i = roll_slot(label);
    if (!rolling) g_roll[i].opa = lv_obj_get_style_text_opa(label, 0);
    snprintf(g_roll[i].text, sizeof(g_roll[i].text), "%s", text);
    g_roll[i].dir = dir >= 0 ? 1 : -1;
    lv_obj_remove_event_cb(label, roll_deleted_cb);
    lv_obj_add_event_cb(label, roll_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, roll_exec);
    lv_anim_set_values(&a, lv_obj_get_style_translate_y(label, 0) * 256 / ROLL_DY, -g_roll[i].dir * 256);
    lv_anim_set_time(&a, 110);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, roll_swap);
    lv_anim_start(&a);
}

/* ---------- colour fades ---------- */

#define MIX_SLOTS 24
static struct {
    lv_obj_t *obj;
    lv_style_prop_t prop;
    lv_color_t from, to;
} g_mix[MIX_SLOTS];

static void mix_apply(void *o, int32_t v, lv_style_prop_t prop) {
    for (int i = 0; i < MIX_SLOTS; i++) {
        if (g_mix[i].obj != o || g_mix[i].prop != prop) continue;
        lv_obj_set_local_style_prop(
            o, prop, (lv_style_value_t){ .color = lv_color_mix(g_mix[i].to, g_mix[i].from, (uint8_t)v) }, 0);
        return;
    }
}
static void ex_mix_bg(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BG_COLOR); }
static void ex_mix_grad(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BG_GRAD_COLOR); }
static void ex_mix_text(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_TEXT_COLOR); }
static void ex_mix_recolor(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_IMG_RECOLOR); }
static void ex_mix_border(void *o, int32_t v) { mix_apply(o, v, LV_STYLE_BORDER_COLOR); }

static void mix_deleted_cb(lv_event_t *e) {
    lv_obj_t *o = lv_event_get_target(e);
    for (int i = 0; i < MIX_SLOTS; i++)
        if (g_mix[i].obj == o) g_mix[i].obj = NULL;
}

void motion_color(lv_obj_t *o, lv_style_prop_t prop, uint32_t to, uint32_t ms) {
    lv_anim_exec_xcb_t ex = prop == LV_STYLE_BG_COLOR        ? ex_mix_bg
                            : prop == LV_STYLE_BG_GRAD_COLOR ? ex_mix_grad
                            : prop == LV_STYLE_TEXT_COLOR    ? ex_mix_text
                            : prop == LV_STYLE_IMG_RECOLOR   ? ex_mix_recolor
                                                             : ex_mix_border;
    lv_color_t cur = lv_obj_get_style_prop(o, 0, prop).color, target = lv_color_hex(to);
    if (cur.full == target.full && !lv_anim_get(o, ex)) return;
    int slot = -1;
    for (int i = 0; i < MIX_SLOTS && slot < 0; i++)
        if (g_mix[i].obj == o && g_mix[i].prop == prop) slot = i;
    for (int i = 0; i < MIX_SLOTS && slot < 0; i++)
        if (!g_mix[i].obj) slot = i;
    if (slot < 0) slot = 0;
    lv_obj_remove_event_cb(o, mix_deleted_cb);
    lv_obj_add_event_cb(o, mix_deleted_cb, LV_EVENT_DELETE, NULL);
    g_mix[slot].obj = o;
    g_mix[slot].prop = prop;
    g_mix[slot].from = cur;
    g_mix[slot].to = target;
    if (!ms) {
        lv_anim_del(o, ex);
        ex(o, 255);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, ex);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
