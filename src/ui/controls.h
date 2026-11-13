/* ui/controls.h - building blocks for the focus-driven screens (Home, the stream menu).
 *
 * None of these join an LVGL group or react to the pointer: the screen that owns them keeps
 * its own focus and calls the focus/select functions as keys arrive, and the controls animate. Values
 * change in place with LEFT/RIGHT (segmented choice, level bar, toggle), so there are no
 * dropdown lists to open. ctl_menu is the one pop-up: a short list anchored to a button.
 */
#ifndef HOTARU_CONTROLS_H
#define HOTARU_CONTROLS_H

#include <stdbool.h>
#include <lvgl.h>

#include "theme.h"

/* plain objects: unstyled, not scrollable, not clickable, children may draw outside */
lv_obj_t *ctl_obj(lv_obj_t *parent);
lv_obj_t *ctl_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius, uint32_t col, lv_opa_t opa);
void ctl_grad(lv_obj_t *o, uint32_t top, uint32_t bottom); /* vertical gradient background */
lv_obj_t *ctl_label(lv_obj_t *parent, const lv_font_t *font, uint32_t col, const char *text);
lv_obj_t *ctl_dot(lv_obj_t *parent, lv_coord_t d, uint32_t col); /* status light with a soft glow */
lv_obj_t *ctl_row(lv_obj_t *parent, lv_coord_t gap);             /* content-sized flex row, centred */
void ctl_outline(lv_obj_t *o, lv_coord_t width, lv_coord_t pad); /* white focus outline, hidden */
/* LVGL repaints only a container's own area (plus this margin) when it hides, fades or moves: room
 * for what its children draw outside it (a focus ring, a grown button), or their edge stays behind */
void ctl_margin(lv_obj_t *o, lv_coord_t px);

/* a soft round glow, white recoloured to `col` (one shared canvas) */
#define CTL_GLOW_D 640
lv_obj_t *ctl_glow(lv_obj_t *parent, uint32_t col, lv_opa_t opa);

/* the PlayStation face-button shapes, drawn with lines and rings */
typedef enum { PS_TRIANGLE, PS_CIRCLE, PS_CROSS, PS_SQUARE } ps_glyph_t;
extern const uint32_t ctl_ps_col[4];
lv_obj_t *ctl_ps_glyph(lv_obj_t *parent, ps_glyph_t g, lv_coord_t size, uint32_t col, lv_coord_t stroke);

/* controller hints: a row of "button + what it does" */
lv_obj_t *ctl_hints(lv_obj_t *parent);
void ctl_hint(lv_obj_t *row, ps_glyph_t g, const char *text);
void ctl_hint_key(lv_obj_t *row, const char *key, const char *text);               /* a text key: arrows, a name */
void ctl_hint_keys(lv_obj_t *row, const char *a, const char *b, const char *text); /* two pressed together */

/* segmented choice with a gliding pill */
#define CTL_SEG_MAX 5
typedef struct {
    lv_obj_t *box, *pill, *lbl[CTL_SEG_MAX];
    int n, sel;
    lv_coord_t seg_w;
    bool focused;
} ctl_seg_t;
void ctl_seg_create(ctl_seg_t *s, lv_obj_t *parent, const char *const *opts, int n, int sel, lv_coord_t seg_w);
void ctl_seg_select(ctl_seg_t *s, int sel); /* animated */
void ctl_seg_focus(ctl_seg_t *s, bool on);

/* a fat level bar with its value as text */
typedef struct {
    lv_obj_t *box, *track, *fill, *value;
    int min, max, val;
    lv_coord_t w;
    const char *fmt; /* printf for the value, one int */
} ctl_meter_t;
void ctl_meter_create(ctl_meter_t *m, lv_obj_t *parent, lv_coord_t w, int min, int max, int val, const char *fmt);
void ctl_meter_set(ctl_meter_t *m, int val); /* clamped, animated */
void ctl_meter_focus(ctl_meter_t *m, bool on);

/* a big on/off toggle */
typedef struct {
    lv_obj_t *track, *on, *knob;
    bool val;
} ctl_toggle_t;
void ctl_toggle_create(ctl_toggle_t *t, lv_obj_t *parent, bool val);
void ctl_toggle_set(ctl_toggle_t *t, bool val); /* animated */

/* a short pop-up list (connection route, console options): UP/DOWN move, the owner acts on ENTER */
#define CTL_MENU_MAX 6
typedef struct {
    const char *icon, *text, *detail; /* detail: a second, smaller line (may be NULL) */
    bool checked, danger;
} ctl_menu_item_t;
typedef struct {
    lv_obj_t *card, *rows[CTL_MENU_MAX];
    int n, sel;
} ctl_menu_t;
/* opens above (up) or below the anchor's area, left-aligned with it, on `parent` */
void ctl_menu_open(ctl_menu_t *m, lv_obj_t *parent, const lv_area_t *anchor, bool up, const ctl_menu_item_t *items,
                   int n, int sel);
void ctl_menu_close(ctl_menu_t *m); /* fades out and deletes itself */
void ctl_menu_move(ctl_menu_t *m, int dir);
bool ctl_menu_is_open(const ctl_menu_t *m);

/* a frosted pill: white with dark text when focused (ctl_button_focus), red for a destructive one */
lv_obj_t *ctl_pill(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
lv_obj_t *ctl_button(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *icon, const char *text);
void ctl_button_focus(lv_obj_t *b, bool on);
void ctl_button_danger_focus(lv_obj_t *b, bool on);
void ctl_shake(lv_obj_t *o); /* "not now": a short sideways wiggle */

/* a number pad: 1-9, then "." (or a gap), 0, backspace. The owner moves it with the arrows and
 * presses on Cross; it takes the remote's digits as they are typed */
#define CTL_NP_KEYS 12
typedef struct {
    lv_obj_t *box, *key[CTL_NP_KEYS];
    int sel;
    bool focused, dot;
} ctl_numpad_t;
void ctl_numpad_create(ctl_numpad_t *p, lv_obj_t *parent, lv_coord_t key, bool dot);
void ctl_numpad_focus(ctl_numpad_t *p, bool on);
void ctl_numpad_select(ctl_numpad_t *p, int index);  /* 0..8 = 1..9, 9 = ".", 10 = 0, 11 = backspace */
bool ctl_numpad_move(ctl_numpad_t *p, uint32_t key); /* LV_KEY_*; false at the edge in that direction */
char ctl_numpad_press(ctl_numpad_t *p);              /* '0'..'9', '.', '\b' */
lv_coord_t ctl_numpad_w(lv_coord_t key);
lv_coord_t ctl_numpad_h(lv_coord_t key);

/* digit boxes: a PIN (8, split 4 + 4) or a passcode (4); the next box is marked while typing */
#define CTL_DIGITS_MAX 8
typedef struct {
    lv_obj_t *box, *cell[CTL_DIGITS_MAX], *digit[CTL_DIGITS_MAX];
    int n;
} ctl_digits_t;
void ctl_digits_create(ctl_digits_t *d, lv_obj_t *parent, int n, lv_coord_t cell_w, lv_coord_t cell_h);
void ctl_digits_show(ctl_digits_t *d, const char *text, bool typing, bool error);

/* a progress ring (0..100) */
lv_obj_t *ctl_ring(lv_obj_t *parent, lv_coord_t d, uint32_t col);
void ctl_ring_set(lv_obj_t *ring, int pct); /* glides there */

/* a one-line text field, a frosted pill; the cursor shows while typing */
lv_obj_t *ctl_field(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *placeholder, uint32_t max_len);
void ctl_field_focus(lv_obj_t *ta, bool focused, bool typing);

/* the on-screen keyboard, restyled and focus driven: lv_keyboard typing into one field, moved with
 * ctl_keyboard_key(LV_KEY_UP/DOWN/LEFT/RIGHT) and pressed with LV_KEY_ENTER; its OK key sends
 * LV_EVENT_READY to the keyboard and the field */
lv_obj_t *ctl_keyboard(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
void ctl_keyboard_focus(lv_obj_t *kb, lv_obj_t *field); /* field NULL: no focus */
void ctl_keyboard_key(lv_obj_t *kb, uint32_t key);
void ctl_keyboard_shift(lv_obj_t *kb);         /* capitals <-> small letters, as its ABC key does (L2/R2) */
void ctl_keyboard_pad(lv_obj_t *kb, bool pad); /* a controller: the ABC key also names L2/R2, greyed */

/* the four PlayStation shapes as a diamond, white (the PSN emblem) */
lv_obj_t *ctl_ps_diamond(lv_obj_t *parent, lv_coord_t size);

#endif
