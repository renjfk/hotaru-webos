/* ui/home_internal.h - what home.c and home_settings.c share; private to them. */
#ifndef HOTARU_HOME_INTERNAL_H
#define HOTARU_HOME_INTERNAL_H

#include <stdbool.h>
#include <lvgl.h>

/* layout (1920x1080): TV title-safe margins, the tile row, the area below it */
#define HOME_SAFE_X   96
#define HOME_SAFE_Y   54
#define HOME_PANEL_Y  468 /* settings panels start here, under the tile row */
#define HOME_PANEL_W  (1920 - 2 * HOME_SAFE_X)
#define HOME_PROW_W   1160 /* a settings row; the summary card fills the rest */
#define HOME_PROW_H   92
#define HOME_PROW_GAP 10

/* where the source of what runs is (AGPL-3.0 section 6), and its releases */
#define HOME_SOURCE_URL "github.com/renjfk/hotaru-webos"

enum { SET_STREAM, SET_PAD, SET_ACCOUNT, SET_LOG, SET_ABOUT, SET__N };

/* home_settings.c: the panels under the Settings row, one per tile */
void home_settings_build(lv_obj_t *parent);
void home_settings_show(int panel, bool anim); /* -1 hides them all */
bool home_settings_enterable(int panel);       /* has rows to focus */
int home_settings_first_row(int panel);        /* the row the focus enters on, -1 if none */
void home_settings_focus(int panel, int row);  /* row -1 = no row focused */
/* a key while a row of `panel` has the focus; *row moves. false = the focus leaves the panel (up) */
bool home_settings_key(int panel, int *row, uint32_t key);
void home_settings_refresh(void);  /* config, PSN account, pad or connection changed */
lv_obj_t *home_settings_log(void); /* the log console (right stick scrolls it) */
const char *home_settings_name(int panel);
const char *home_settings_caption(int panel);
uint32_t home_settings_tint(int panel);

/* home.c: a settings value changed (the quick cards show it too) */
void home_values_changed(void);

#endif
