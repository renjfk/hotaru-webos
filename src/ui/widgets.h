/* ui/widgets.h - the few themed LVGL components left from before the focus-driven screens.
 *
 * Home, Add a console, the dialogs and the stream menu are built from ui/controls.h. What is
 * here: a non-clipping flex box and a themed label, for the splash.
 */
#ifndef CHIAKI_WEBOS_WIDGETS_H
#define CHIAKI_WEBOS_WIDGETS_H

#include <stdbool.h>
#include <lvgl.h>

#include "theme.h"

void ui_no_clip(lv_obj_t *o);
lv_obj_t *ui_box(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap); /* transparent flex box */
lv_obj_t *ui_text(lv_obj_t *parent, txt_kind_t kind, const char *text);

#endif
