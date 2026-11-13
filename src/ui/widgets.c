/* ui/widgets.c - the few themed LVGL components left (see widgets.h). */
#include "widgets.h"

/* Layout boxes never clip: a focus ring, slider knob or button shadow may
 * reach past them, and the enclosing card (padded for it) does the clipping. */
void ui_no_clip(lv_obj_t *o) {
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_clip_corner(o, false, 0);
}
lv_obj_t *ui_box(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap) {
    lv_obj_t *o = lv_obj_create(parent);
    ui_no_clip(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(o, gap, 0);
    return o;
}
lv_obj_t *ui_text(lv_obj_t *parent, txt_kind_t kind, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &st_txt[kind], 0);
    lv_label_set_text(l, text);
    return l;
}
