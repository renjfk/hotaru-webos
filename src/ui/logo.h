/* ui/logo.h - the Chiaki webOS mark.
 *
 * ui_logo() builds the app mark: a rounded PlayStation-blue tile with the app
 * icon's glyph (Wi-Fi arcs, circle, cross and a solid gamepad, in white), drawn
 * procedurally on an lv_canvas so it stays crisp at any size. Used by the top
 * bar and the startup splash, so both show the same mark as the launcher icon.
 */
#ifndef CHIAKI_WEBOS_LOGO_H
#define CHIAKI_WEBOS_LOGO_H

#include <lvgl.h>

/* returns the tile (a square, `size` px); positions/sizes in the parent's flex */
lv_obj_t *ui_logo(lv_obj_t *parent, lv_coord_t size);

#endif
