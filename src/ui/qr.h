/* ui/qr.h - a scannable QR code widget.
 *
 * ui_qr() encodes `text` with the vendored qrcodegen (vendor/qrcodegen.h,
 * byte mode, lowest error correction that fits, boosted when there is room)
 * and draws the module matrix on an lv_canvas: black modules on white with
 * the 4-module quiet zone. Modules are a whole number of pixels so a phone
 * camera sees crisp edges; any slack goes to the white margin. The canvas
 * buffer is freed with the widget. If the text does not fit a QR code or the
 * memory is not there, a placeholder tile with a short message is returned.
 */
#ifndef CHIAKI_WEBOS_QR_H
#define CHIAKI_WEBOS_QR_H

#include <stdbool.h>
#include <lvgl.h>

/* returns a white square tile, `size` px; positions/sizes in the parent's flex */
lv_obj_t *ui_qr(lv_obj_t *parent, const char *text, lv_coord_t size);
bool ui_qr_ok(const lv_obj_t *qr); /* false for the placeholder */

#endif
