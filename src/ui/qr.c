/* ui/qr.c - QR code on an lv_canvas (see qr.h); the encoder is vendor/qrcodegen. */
#include "qr.h"

#include <stdlib.h>

#include "../vendor/qrcodegen.h"
#include "theme.h"

#define QUIET 4   /* white border, in modules, that the QR spec asks for */

static void buf_free_cb(lv_event_t *e) { free(lv_event_get_user_data(e)); }

static lv_obj_t *placeholder(lv_obj_t *tile, const char *why) {
    lv_obj_t *l = lv_label_create(tile);
    lv_obj_add_style(l, &st_txt[TXT_CAPTION], 0);
    lv_obj_set_style_text_color(l, C(COL_BG_BOTTOM), 0);   /* dark on the white tile */
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, why);
    lv_obj_center(l);
    return tile;
}

/* black modules on white, `px` pixels each, after the quiet zone */
static void draw_modules(lv_color_t *buf, lv_coord_t side, const uint8_t *qr, int px) {
    lv_color_t white = lv_color_white(), black = lv_color_black();
    for (size_t i = 0; i < (size_t)side * (size_t)side; i++) buf[i] = white;
    int n = qrcodegen_getSize(qr);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!qrcodegen_getModule(qr, x, y)) continue;
            for (int dy = 0; dy < px; dy++) {
                lv_color_t *row = buf + (size_t)((QUIET + y) * px + dy) * (size_t)side + (size_t)((QUIET + x) * px);
                for (int dx = 0; dx < px; dx++) row[dx] = black;
            }
        }
    }
}

lv_obj_t *ui_qr(lv_obj_t *parent, const char *text, lv_coord_t size) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_radius(tile, R_SM, 0);   /* inside the quiet zone, never cuts a module */
    lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    uint8_t *qr = malloc(qrcodegen_BUFFER_LEN_MAX), *tmp = malloc(qrcodegen_BUFFER_LEN_MAX);
    bool ok = qr && tmp && text &&
              qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);
    free(tmp);
    if (!ok) {
        free(qr);
        return placeholder(tile, "QR code unavailable");
    }
    int px = size / (qrcodegen_getSize(qr) + 2 * QUIET);
    lv_coord_t side = (lv_coord_t)(px * (qrcodegen_getSize(qr) + 2 * QUIET));
    lv_color_t *buf = px > 0 ? malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(side, side)) : NULL;
    if (!buf) {   /* too small to be scannable, or RAM is tight */
        free(qr);
        return placeholder(tile, "QR code unavailable");
    }
    draw_modules(buf, side, qr, px);
    free(qr);

    lv_obj_t *cv = lv_canvas_create(tile);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(cv, buf, side, side, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(cv);
    lv_obj_add_event_cb(cv, buf_free_cb, LV_EVENT_DELETE, buf);
    return tile;
}

bool ui_qr_ok(const lv_obj_t *qr) {
    lv_obj_t *c = qr ? lv_obj_get_child(qr, 0) : NULL;
    return c && lv_obj_check_type(c, &lv_canvas_class);
}
