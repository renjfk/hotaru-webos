/* ui/log_view.h - scrollable on-screen console fed by log_event(). */
#ifndef HOTARU_LOG_VIEW_H
#define HOTARU_LOG_VIEW_H

#include <lvgl.h>

/* creates the console in parent and registers it as the log sink */
lv_obj_t *log_view_create(lv_obj_t *parent);

#endif
