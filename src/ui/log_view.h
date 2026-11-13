/* ui/log_view.h - scrollable on-screen console fed by log_event(). */
#ifndef CHIAKI_WEBOS_LOG_VIEW_H
#define CHIAKI_WEBOS_LOG_VIEW_H

#include <lvgl.h>

/* creates the console in parent and registers it as the log sink */
lv_obj_t *log_view_create(lv_obj_t *parent);

#endif
