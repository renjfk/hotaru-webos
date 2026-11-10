/* ui/widgets.h - reusable, themed UI components.
 *
 * Build screens from these instead of raw LVGL calls so spacing, focus rings
 * and clipping stay consistent. Containers made here never clip their
 * children (ui_no_clip); cards are the clipping boundary and are padded so a
 * focus ring or button shadow on an edge control is fully visible.
 */
#ifndef CHIAKI_WIDGETS_H
#define CHIAKI_WIDGETS_H

#include <stdbool.h>
#include <lvgl.h>

#include "theme.h"

typedef enum { BTN_PRIMARY, BTN_SECONDARY, BTN_DANGER, BTN_GHOST } btn_kind_t;

/* status chip: glowing LED + label */
typedef struct { lv_obj_t *chip, *led, *label; } ui_status_t;

/* layout */
void ui_no_clip(lv_obj_t *o);
lv_obj_t *ui_box(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap);   /* transparent flex box */
lv_obj_t *ui_spacer(lv_obj_t *parent);                                     /* flex_grow filler */
lv_obj_t *ui_card(lv_obj_t *parent, lv_style_t *variant);                  /* padded panel, column flex */
lv_obj_t *ui_card_header(lv_obj_t *card, const char *icon, const char *title, const char *caption);
lv_obj_t *ui_setting_row(lv_obj_t *parent, const char *title, const char *caption);  /* add the control after */
void ui_last_row(lv_obj_t *row);                                           /* drop the divider */

/* content */
lv_obj_t *ui_text(lv_obj_t *parent, txt_kind_t kind, const char *text);
lv_obj_t *ui_tile(lv_obj_t *parent, lv_style_t *style, const char *text, lv_coord_t size, const lv_font_t *font);
lv_obj_t *ui_btn(lv_obj_t *parent, btn_kind_t kind, const char *icon, const char *text);
lv_obj_t *ui_led(lv_obj_t *parent, uint32_t col, lv_coord_t size);
lv_obj_t *ui_chip(lv_obj_t *parent, const char *icon, const char *text, lv_obj_t **label_out);
void ui_status_init(ui_status_t *s, lv_obj_t *parent, uint32_t col, const char *text);
void ui_status_set(ui_status_t *s, uint32_t col, const char *text, bool pulse);
void ui_hint(lv_obj_t *parent, const char *glyph, uint32_t glyph_col, const char *text);   /* controller hint */
lv_obj_t *ui_ps_buttons(lv_obj_t *parent);   /* decorative face-button diamond, ignores layout */

/* controls (CTRL_W wide, for setting rows) */
lv_obj_t *ui_dropdown(lv_obj_t *parent, const char *options, uint16_t selected, lv_event_cb_t changed_cb);
lv_obj_t *ui_slider_control(lv_obj_t *row, int32_t min, int32_t max, int32_t val, int32_t key_step,
                            lv_event_cb_t changed_cb, lv_obj_t **value_label);

/* stick-cursor sprite on the top layer (hidden; input/pointer moves it) */
lv_obj_t *ui_cursor_create(void);

#endif
