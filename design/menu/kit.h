/* design/menu/kit.h - shared pieces for the main menu concepts: mock data, procedural console
 * art, PlayStation glyphs, animation helpers and a few big TV controls.
 *
 * Gallery only: nothing here talks to app/config/session. Every concept is driven by
 * directional keys (nav_t) instead of a pointer, and keeps its own focus state; LVGL groups are
 * not used. Colours, spacing and fonts come from ui/theme.h so the concepts match the app.
 */
#ifndef HOTARU_KIT_H
#define HOTARU_KIT_H

#include <stdbool.h>
#include <lvgl.h>

#include "ui/theme.h"

#define SCR_W     1920
#define SCR_H     1080
#define SAFE_X    96 /* TV title-safe margins */
#define SAFE_Y    54
#define FONT_HERO (&lv_font_montserrat_48)

/* the keys every concept understands: D-pad / remote arrows, Cross / OK, Circle / Back, and
 * Share + Options (the stream menu chord) */
typedef enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_MENU } nav_t;

/* ---------- mock data ---------- */

typedef enum { HS_READY, HS_STANDBY, HS_OFFLINE, HS_REMOTE } host_state_t;
typedef struct {
    const char *name, *addr, *last;
    bool ps5;
    host_state_t state;
    uint32_t tint; /* key colour for the backgrounds behind this console */
} mock_host_t;
#define MOCK_HOSTS 4
extern const mock_host_t mock_hosts[MOCK_HOSTS];
const char *mock_state_text(host_state_t s);
uint32_t mock_state_col(host_state_t s);

typedef struct {
    int res, fps, codec, smooth; /* indexes into the option lists */
    int bitrate;                 /* Mbps */
    bool hdr, rumble, touchpad, motion;
} mock_settings_t;
extern mock_settings_t mock_set;
void mock_reset(void); /* back to the defaults: every concept starts from the same settings */
extern const char *const mock_res_opts[2];
extern const char *const mock_fps_opts[2];
extern const char *const mock_codec_opts[3];
extern const char *const mock_smooth_opts[4];
#define MOCK_BITRATE_MIN 2
#define MOCK_BITRATE_MAX 50
const char *mock_quality_text(void); /* "1080p · 60 fps" */
const char *mock_summary_text(void); /* "H.265 · SDR · 15 Mbps" */
const char *mock_clock_text(void);   /* the session time, "00:42:13", ticks with lv_tick */
/* the connected pad: a DualShock 4, or a DualSense with HOTARU_GALLERY_PAD=dualsense (when its
 * drawing exists) */
bool mock_pad_dualsense(void);
const char *mock_pad_name(void);

/* the gallery runner: start (true) or end (false) the stand-in stream */
extern void (*kit_stream_hook)(bool on);

/* ---------- building blocks ---------- */

lv_obj_t *kit_obj(lv_obj_t *parent); /* unstyled, not scrollable, not clickable, overflow visible */
lv_obj_t *kit_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius, uint32_t col, lv_opa_t opa);
void kit_grad(lv_obj_t *o, uint32_t top, uint32_t bottom);
lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text);
lv_obj_t *kit_dot(lv_obj_t *parent, lv_coord_t d, uint32_t col); /* status light with a soft glow */
lv_obj_t *kit_app_mark(lv_obj_t *parent, lv_coord_t size);       /* stand-in for ui_logo(): the shapes on a blue tile */

/* device art from the drawings in art/ (art.h): a PS5, a PS4, the pads. kit_console_art()
 * makes a large one to zoom down (KIT_ART_H, kit_art_zoom() fits it to a box); the _h variants
 * rasterize at the height they are shown at, which is the crisp choice for a fixed size */
#define KIT_ART_H 520
lv_obj_t *kit_console_art(lv_obj_t *parent, bool ps5);
lv_obj_t *kit_console_art_h(lv_obj_t *parent, bool ps5, lv_coord_t h);
lv_obj_t *kit_console_art_box(lv_obj_t *parent, bool ps5, lv_coord_t w, lv_coord_t h); /* largest that fits */
lv_obj_t *kit_pad_art(lv_obj_t *parent, lv_coord_t h);
void kit_art_fit(lv_obj_t *img, lv_coord_t w, lv_coord_t h); /* zoom any art image to fit the box */
uint16_t kit_art_zoom(bool ps5, lv_coord_t w, lv_coord_t h); /* for kit_console_art() images */
/* a white radial glow (recolour it with img_recolor); diameter KIT_GLOW_D */
#define KIT_GLOW_D 760
lv_obj_t *kit_glow(lv_obj_t *parent, uint32_t col, lv_opa_t opa);

typedef enum { PS_TRIANGLE, PS_CIRCLE, PS_CROSS, PS_SQUARE } ps_glyph_t;
extern const uint32_t kit_ps_col[4];
lv_obj_t *kit_ps_glyph(lv_obj_t *parent, ps_glyph_t g, lv_coord_t size, uint32_t col, lv_coord_t stroke);
lv_obj_t *kit_ps_diamond(lv_obj_t *parent, lv_coord_t size, bool colour); /* the four face buttons */

/* controller hints: a row (flex) of "glyph + text" pairs */
lv_obj_t *kit_hints(lv_obj_t *parent);
void kit_hint(lv_obj_t *row, ps_glyph_t g, const char *text);
void kit_hint_key(lv_obj_t *row, const char *key, const char *text); /* a text key: "OPTIONS", arrows */

/* ---------- animation ---------- */

typedef enum {
    KA_X,
    KA_Y,
    KA_W,
    KA_H,
    KA_TX,
    KA_TY,
    KA_OPA,
    KA_BG_OPA,
    KA_TEXT_OPA,
    KA_IMG_OPA,
    KA_ZOOM,
    KA_GROW,
    KA_BORDER_OPA,
    KA_OUTLINE_OPA,
    KA_ARC,
    KA_BAR,
    KA_SHADOW_OPA
} kit_prop_t;

int32_t kit_prop_get(lv_obj_t *o, kit_prop_t p);
void kit_prop_set(lv_obj_t *o, kit_prop_t p, int32_t v);
/* animate from the current value; ms 0 sets it at once (and stops a running animation) */
void kit_anim(lv_obj_t *o, kit_prop_t p, int32_t to, uint32_t ms, uint32_t delay, lv_anim_path_cb_t path);
void kit_anim_from(lv_obj_t *o, kit_prop_t p, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                   lv_anim_path_cb_t path);
/* like kit_anim, then calls ready (with the object) when it lands */
void kit_anim_then(lv_obj_t *o, kit_prop_t p, int32_t to, uint32_t ms, lv_anim_path_cb_t path,
                   void (*ready)(lv_obj_t *o));
int32_t kit_path_spring(const lv_anim_t *a);                    /* damped spring: ~8% overshoot, settles softly */
void kit_enter(lv_obj_t *o, uint32_t delay, lv_coord_t dy);     /* fade + slide in */
void kit_roll_text(lv_obj_t *label, const char *text, int dir); /* old text out, new in */
void kit_count_to(lv_obj_t *label, int from, int to, const char *fmt, uint32_t ms); /* numbers tick */
void kit_mix_color(lv_obj_t *o, lv_style_prop_t prop, uint32_t to, uint32_t ms);    /* colour fade */

/* ---------- controls ---------- */

/* segmented choice with a gliding pill: LEFT/RIGHT change it directly, no dropdown */
#define KIT_SEG_MAX 5
typedef struct {
    lv_obj_t *box, *pill, *lbl[KIT_SEG_MAX];
    int n, sel;
    lv_coord_t seg_w;
    bool focused;
} kit_seg_t;
void kit_seg_create(kit_seg_t *s, lv_obj_t *parent, const char *const *opts, int n, int sel, lv_coord_t seg_w);
void kit_seg_select(kit_seg_t *s, int sel);
void kit_seg_focus(kit_seg_t *s, bool on);

/* a fat level bar with its value: bitrate */
typedef struct {
    lv_obj_t *box, *track, *fill, *value;
    int min, max, val;
    lv_coord_t w;
} kit_meter_t;
void kit_meter_create(kit_meter_t *m, lv_obj_t *parent, lv_coord_t w, int min, int max, int val);
void kit_meter_set(kit_meter_t *m, int val);
void kit_meter_focus(kit_meter_t *m, bool on);

/* a big on/off toggle */
typedef struct {
    lv_obj_t *track, *on, *knob;
    bool val;
} kit_toggle_t;
void kit_toggle_create(kit_toggle_t *t, lv_obj_t *parent, bool val);
void kit_toggle_set(kit_toggle_t *t, bool val);

/* one focus frame that glides between targets (same parent space as the frame) */
lv_obj_t *kit_focus_frame(lv_obj_t *parent, lv_coord_t radius);
void kit_glide(lv_obj_t *frame, lv_obj_t *target, lv_coord_t pad, uint32_t ms);

/* the shared "connecting" interstitial: covers the screen, then hands over to the stream */
void kit_connect_screen(const mock_host_t *h);

void kit_init(void);

#endif
