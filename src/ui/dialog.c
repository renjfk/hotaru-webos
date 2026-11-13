/* ui/dialog.c - the confirm card and the passcode sheet (see dialog.h). */
#include "dialog.h"

#include <stdlib.h>
#include <string.h>

#include "../log.h"
#include "controls.h"
#include "motion.h"

#define CONFIRM_W   920
#define CONFIRM_PAD 44
#define TEXT_X      230 /* the text, right of the console's picture */
#define BTN_W       240
#define BTN_H       84
#define SHEET_H     480
#define SHEET_X     160
#define PASS_KEY    80
#define PASS_MAX    CTL_DIGITS_MAX

enum { F_CANCEL, F_OK, F_PAD };

static struct {
    lv_obj_t *root, *scrim, *card, *btn[2];
    bool passcode, closing;
    int focus, digits;
    char code[PASS_MAX + 1];
    ctl_numpad_t np;
    ctl_digits_t dg;
    ui_confirm_cb_t ccb;
    ui_passcode_cb_t pcb;
    void *user;
} D;

typedef struct {
    ui_confirm_cb_t ccb;
    ui_passcode_cb_t pcb;
    void *user;
    bool ok;
    char code[PASS_MAX + 1];
} done_t;

/* tear down, then run the callback: it may change app state, which can rebuild the UI */
static void done_async(void *p) {
    done_t *d = p;
    if (D.root) lv_obj_del(D.root);
    memset(&D, 0, sizeof(D));
    if (d->ok && d->ccb) d->ccb(d->user);
    if (d->pcb) d->pcb(d->ok ? d->code : NULL, d->user);
    free(d);
}

static void finish(bool ok) {
    if (!D.root || D.closing) return;
    done_t *d = malloc(sizeof(*d));
    if (!d) return;
    D.closing = true;
    d->ccb = D.ccb;
    d->pcb = D.pcb;
    d->user = D.user;
    d->ok = ok;
    memcpy(d->code, D.code, sizeof(d->code));
    motion_to(D.scrim, MO_BG_OPA, LV_OPA_TRANSP, 160, 0, lv_anim_path_ease_in);
    lv_async_call(done_async, d);
}

static void paint(void) {
    if (D.passcode) {
        ctl_numpad_focus(&D.np, D.focus == F_PAD);
        ctl_button_focus(D.btn[F_CANCEL], D.focus == F_CANCEL);
        ctl_button_focus(D.btn[F_OK], D.focus == F_OK);
        ctl_digits_show(&D.dg, D.code, D.focus == F_PAD, false);
        lv_obj_set_style_opa(D.btn[F_OK], (int)strlen(D.code) == D.digits ? LV_OPA_COVER : LV_OPA_50, 0);
    } else {
        ctl_button_focus(D.btn[F_CANCEL], D.focus == F_CANCEL);
        ctl_button_danger_focus(D.btn[F_OK], D.focus == F_OK);
    }
}

static void open_root(lv_opa_t scrim) {
    D.root = ctl_obj(lv_layer_top());
    lv_obj_set_size(D.root, LV_PCT(100), LV_PCT(100));
    D.scrim = ctl_rect(D.root, LV_PCT(100), LV_PCT(100), 0, COL_SHADE, LV_OPA_TRANSP);
    motion_to(D.scrim, MO_BG_OPA, scrim, 260, 0, NULL);
}

static lv_obj_t *buttons(lv_obj_t *parent, const char *ok_icon, const char *ok) {
    lv_obj_t *row = ctl_obj(parent);
    lv_obj_set_size(row, 2 * BTN_W + 20, BTN_H);
    D.btn[F_CANCEL] = ctl_button(row, BTN_W, BTN_H, NULL, "Cancel");
    D.btn[F_OK] = ctl_button(row, BTN_W, BTN_H, ok_icon, ok);
    lv_obj_set_pos(D.btn[F_OK], BTN_W + 20, 0);
    return row;
}

void ui_confirm(const char *title, const char *message, const char *confirm_text, art_id_t art,
                ui_confirm_cb_t on_confirm, void *user) {
    if (D.root) return;
    memset(&D, 0, sizeof(D));
    D.ccb = on_confirm;
    D.user = user;
    D.focus = F_CANCEL; /* the safe default for a destructive action */
    open_root(160);
    bool pic = art < ART__N;
    lv_coord_t tx = pic ? TEXT_X : CONFIRM_PAD, tw = CONFIRM_W - tx - CONFIRM_PAD;
    lv_obj_t *body_l = ctl_label(D.root, FONT_BODY, COL_SUBTLE, message); /* measured, then moved in */
    lv_obj_set_width(body_l, tw);
    lv_label_set_long_mode(body_l, LV_LABEL_LONG_WRAP);
    lv_obj_update_layout(body_l);
    lv_coord_t body_h = lv_obj_get_height(body_l), h = CONFIRM_PAD + 48 + body_h + 36 + BTN_H + 40;
    if (pic && h < CONFIRM_PAD + 150 + 36 + BTN_H + 40) h = CONFIRM_PAD + 150 + 36 + BTN_H + 40;
    D.card = ctl_rect(D.root, CONFIRM_W, h, 36, COL_SHEET, 250);
    lv_obj_set_style_border_width(D.card, 2, 0);
    lv_obj_set_style_border_color(D.card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(D.card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(D.card, 90, 0);
    lv_obj_set_style_shadow_opa(D.card, LV_OPA_60, 0);
    lv_obj_center(D.card);
    if (pic) {
        lv_obj_t *t = ctl_rect(D.card, 150, 150, 34, 0, LV_OPA_COVER);
        ctl_grad(t, art == ART_PS4 ? 0x3b4fa0 : 0x2d528f, 0x0f1a33);
        lv_obj_set_pos(t, CONFIRM_PAD, CONFIRM_PAD);
        lv_obj_center(art_box(t, art, 128, 120));
    }
    lv_obj_t *ttl = ctl_label(D.card, FONT_TITLE, COL_TEXT, title);
    lv_obj_set_width(ttl, tw);
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(ttl, tx, CONFIRM_PAD + 4);
    lv_obj_set_parent(body_l, D.card);
    lv_obj_set_pos(body_l, tx, CONFIRM_PAD + 56);
    lv_obj_align(buttons(D.card, LV_SYMBOL_TRASH, confirm_text ? confirm_text : "OK"), LV_ALIGN_BOTTOM_RIGHT,
                 -CONFIRM_PAD, -40);
    motion_from(D.card, MO_GROW, -40, 0, 420, 0, motion_spring);
    motion_from(D.card, MO_OPA, 0, 255, 220, 0, NULL);
    paint();
    log_event(LOG_INFO, "UI", "confirm: %s", title);
}

void ui_passcode(const char *title, const char *message, int digits, const char *ok_text, ui_passcode_cb_t on_done,
                 void *user) {
    if (D.root) return;
    memset(&D, 0, sizeof(D));
    D.passcode = true;
    D.pcb = on_done;
    D.user = user;
    D.digits = digits < 1 ? 1 : digits > PASS_MAX ? PASS_MAX : digits;
    D.focus = F_PAD;
    open_root(90);
    /* a sheet from the bottom, its lower corners off-screen: the Connecting screen stays above it */
    D.card = ctl_rect(D.root, LV_PCT(100), SHEET_H + 60, 44, COL_SHEET, LV_OPA_COVER);
    lv_obj_set_style_border_width(D.card, 2, 0);
    lv_obj_set_style_border_side(D.card, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(D.card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(D.card, LV_OPA_20, 0);
    lv_obj_align(D.card, LV_ALIGN_BOTTOM_MID, 0, 60);
    lv_obj_set_pos(ctl_label(D.card, FONT_HERO, COL_TEXT, title), SHEET_X, 48);
    lv_obj_t *body = ctl_label(D.card, FONT_BODY, COL_SUBTLE, message);
    lv_obj_set_width(body, 760);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(body, SHEET_X, 118);
    ctl_digits_create(&D.dg, D.card, D.digits, 104, 124);
    lv_obj_set_pos(D.dg.box, SHEET_X, 176);
    lv_obj_set_pos(buttons(D.card, LV_SYMBOL_OK, ok_text ? ok_text : "OK"), SHEET_X, 340);
    ctl_numpad_create(&D.np, D.card, PASS_KEY, false);
    lv_obj_set_pos(D.np.box, 1920 - SHEET_X - ctl_numpad_w(PASS_KEY), 48);
    motion_from(D.card, MO_TY, SHEET_H + 60, 0, 420, 0, NULL);
    paint();
    log_event(LOG_INFO, "UI", "passcode: %s", title);
}

static void pass_type(char c) {
    size_t n = strlen(D.code);
    if (c == '\b') {
        if (n) D.code[n - 1] = 0;
    } else if (c >= '0' && c <= '9' && (int)n < D.digits) {
        D.code[n] = c;
        D.code[n + 1] = 0;
        if ((int)n + 1 == D.digits) D.focus = F_OK; /* complete: on to OK */
    }
}

void ui_dialog_key(uint32_t k) {
    if (!D.root || D.closing) return;
    if (k == LV_KEY_ESC) {
        finish(false);
        return;
    }
    if (D.passcode) {
        if (k >= '0' && k <= '9') pass_type((char)k);
        else if (k == LV_KEY_BACKSPACE) pass_type('\b');
        else if (D.focus == F_PAD) {
            if (k == LV_KEY_ENTER) pass_type(ctl_numpad_press(&D.np));
            else if (!ctl_numpad_move(&D.np, k) && k == LV_KEY_LEFT) D.focus = F_OK;
        } else if (k == LV_KEY_LEFT && D.focus == F_OK) {
            D.focus = F_CANCEL;
        } else if (k == LV_KEY_RIGHT) {
            D.focus = D.focus == F_CANCEL ? F_OK : F_PAD;
        } else if (k == LV_KEY_UP) {
            D.focus = F_PAD;
        } else if (k == LV_KEY_ENTER) {
            if (D.focus == F_CANCEL) {
                finish(false);
                return;
            }
            if ((int)strlen(D.code) == D.digits) {
                finish(true);
                return;
            }
            ctl_shake(D.btn[F_OK]);
            D.focus = F_PAD;
        }
    } else {
        if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) D.focus = k == LV_KEY_RIGHT ? F_OK : F_CANCEL;
        else if (k == LV_KEY_ENTER) {
            finish(D.focus == F_OK);
            return;
        }
    }
    paint();
}

void ui_dialog_back(void) { finish(false); }

bool ui_dialog_active(void) { return D.root != NULL; }
bool ui_confirm_active(void) { return D.root && !D.passcode; }
bool ui_passcode_active(void) { return D.root && D.passcode; }
int ui_dialog_focus(void) { return D.focus; }
const char *ui_passcode_text(void) { return D.code; }
lv_obj_t *ui_dialog_root(void) { return D.root; }
