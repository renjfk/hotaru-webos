/* design/menu/lv_conf.h - the app's LVGL config (which has the 48 px font and the PNG decoder
 * the concepts use) plus a manual tick, so recordings run on a fixed clock instead of
 * SDL_GetTicks(). */
#ifndef CHIAKI_WEBOS_GALLERY_LV_CONF_H
#define CHIAKI_WEBOS_GALLERY_LV_CONF_H

#include "../../src/lv_conf.h"

#undef LV_TICK_CUSTOM
#define LV_TICK_CUSTOM 0

#endif
