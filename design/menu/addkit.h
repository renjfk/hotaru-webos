/* design/menu/addkit.h - the "Add a console" flow the add concepts share: its steps, a mock
 * pairing, and the controls they are made of.
 *
 * A concept owns the frame (a full-screen backdrop, Home behind it, a sheet) and hands the flow a
 * geometry: a main column (title, text, the step's control) and an aside, beside it (wide) or
 * under it (stacked). The flow builds every step into that geometry, slides between them, keeps
 * the focus, and tells the frame when a step starts (so it can follow with its tint and step
 * dots) and when the flow is done or backed out of.
 *
 * Steps. PIN: how -> which console -> its address -> its PIN -> pairing -> ready. PSN: how ->
 * sign in on the phone (QR, then the code with the keyboard) -> signing in -> the consoles found.
 * Mock rules: a PIN starting with 0000 is refused; any code links two consoles.
 */
#ifndef HOTARU_ADDKIT_H
#define HOTARU_ADDKIT_H

#include "kit.h"

typedef enum {
    AK_METHOD,
    AK_KIND,
    AK_ADDR,
    AK_PIN,
    AK_PAIRING,
    AK_DONE,
    AK_PSN,
    AK_LINKING,
    AK_FOUND,
    AK__N
} ak_step_t;
enum { AK_BY_PIN, AK_BY_PSN };

#define AK_TINT_NEUTRAL 0x24324f
#define AK_TINT_PIN     0x1d57b8 /* the local network: Home's blue */
#define AK_TINT_PSN     0x0e6b78 /* PSN: the PSN account panel's teal */

typedef struct {
    lv_coord_t x, y;                               /* the flow in its parent */
    lv_coord_t main_w;                             /* the main column */
    lv_coord_t aside_x, aside_y, aside_w, aside_h; /* the aside, relative to x/y */
    bool stacked;                                  /* aside under the main column */
    bool method_cards;                             /* the two ways in as cards side by side */
    const lv_font_t *title_font;
    lv_coord_t key;              /* number pad key size */
    lv_obj_t *kb_parent;         /* where the keyboard slides up: its parent, */
    lv_coord_t kb_x, kb_w, kb_h; /* x and size; it rises from the parent's bottom */
} ak_layout_t;

typedef struct {
    void (*step)(ak_step_t step, int method); /* a step started */
    void (*close)(bool added);                /* finished (added) or backed out of the first step */
    void (*keyboard)(bool open);              /* the keyboard rose (the flow moved up above it) or went */
} ak_hooks_t;

void ak_create(lv_obj_t *parent, const ak_layout_t *lay, const ak_hooks_t *hooks);
void ak_nav(nav_t key);
ak_step_t ak_step(void);
int ak_method(void);
uint32_t ak_tint(void);                         /* the backdrop for the current step */
int ak_progress(int *count, const char **name); /* step number (1-based) in its path, and the path's length */
const char *ak_console_name(void);              /* the paired console */
bool ak_console_ps5(void);

/* ---------- controls (also used by the dialogs concept) ---------- */

lv_obj_t *ak_button(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *icon, const char *text);
void ak_button_focus(lv_obj_t *b, bool on);
void ak_button_danger_focus(lv_obj_t *b, bool on); /* red when focused: Forget */
void ak_shake(lv_obj_t *o);
void ak_outline(lv_obj_t *o, lv_coord_t w, lv_coord_t pad);
/* LVGL repaints only a parent's own area (plus this margin) when it moves, fades or hides: give it
 * room for what its children draw outside it (a focus ring, a grown button, a glow) */
void ak_margin(lv_obj_t *o, lv_coord_t px);

/* a number pad: 1-9, then "." (or nothing), 0, backspace */
#define AK_NP_KEYS 12
typedef struct {
    lv_obj_t *box, *key[AK_NP_KEYS];
    int sel;
    bool focused, dot;
} ak_numpad_t;
void ak_numpad_create(ak_numpad_t *p, lv_obj_t *parent, lv_coord_t key, bool dot);
void ak_numpad_focus(ak_numpad_t *p, bool on);
bool ak_numpad_move(ak_numpad_t *p, nav_t k); /* false at the edge in that direction */
char ak_numpad_press(ak_numpad_t *p);         /* '0'..'9', '.', '\b' */
lv_coord_t ak_numpad_w(lv_coord_t key);
lv_coord_t ak_numpad_h(lv_coord_t key);

/* digit boxes: a PIN (8, split 4 + 4) or a login passcode (4) */
#define AK_DIGITS_MAX 8
typedef struct {
    lv_obj_t *box, *cell[AK_DIGITS_MAX], *digit[AK_DIGITS_MAX];
    int n;
} ak_digits_t;
void ak_digits_create(ak_digits_t *d, lv_obj_t *parent, int n, lv_coord_t cell_w, lv_coord_t cell_h);
void ak_digits_show(ak_digits_t *d, const char *text, bool active, bool error);

#endif
