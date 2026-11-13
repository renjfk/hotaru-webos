/* ui/motion.h - small animation helpers: tween a style property from where it is now.
 *
 * Every helper animates one property of one object; starting another animation of the same
 * property replaces the running one, so a quick second key press turns an animation around
 * mid-way instead of stacking. LVGL deletes an object's animations with the object.
 */
#ifndef CHIAKI_WEBOS_MOTION_H
#define CHIAKI_WEBOS_MOTION_H

#include <lvgl.h>

typedef enum {
    /* position and size (style values) */
    MO_X,
    MO_Y,
    MO_W,
    MO_H,
    /* translate */
    MO_TX,
    MO_TY,
    /* opacity: the whole object, or one of its parts */
    MO_OPA,
    MO_BG_OPA,
    MO_TEXT_OPA,
    MO_IMG_OPA,
    MO_BORDER_OPA,
    MO_OUTLINE_OPA,
    MO_ZOOM, /* lv_img zoom, 256 = 1:1 */
    MO_GROW, /* transform_width and transform_height together */
} motion_prop_t;

int32_t motion_get(lv_obj_t *o, motion_prop_t p);
void motion_set(lv_obj_t *o, motion_prop_t p, int32_t v); /* now, stopping a running animation */
/* from the current value; ms 0 sets it at once; path NULL = ease out */
void motion_to(lv_obj_t *o, motion_prop_t p, int32_t to, uint32_t ms, uint32_t delay, lv_anim_path_cb_t path);
void motion_from(lv_obj_t *o, motion_prop_t p, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                 lv_anim_path_cb_t path);
/* like motion_to, then ready(o) when it lands */
void motion_then(lv_obj_t *o, motion_prop_t p, int32_t to, uint32_t ms, lv_anim_path_cb_t path,
                 void (*ready)(lv_obj_t *o));
int32_t motion_spring(const lv_anim_t *a); /* damped spring path: ~8% overshoot */
int32_t motion_shake(const lv_anim_t *a);  /* a decaying wiggle around 0; start value = amplitude */

void motion_enter(lv_obj_t *o, uint32_t delay, lv_coord_t dy);     /* fade in while sliding up dy px */
void motion_roll_text(lv_obj_t *label, const char *text, int dir); /* old text rolls out, new in */
/* prop: a BG, GRAD, TEXT, IMG_RECOLOR or BORDER colour */
void motion_color(lv_obj_t *o, lv_style_prop_t prop, uint32_t to, uint32_t ms);

#endif
