/* ui/art.h - the device illustrations: assets/devices/<name>.png, scaled to each size shown.
 *
 * The PNGs ship next to the binary (CMakeLists.txt installs assets/devices); they are already
 * scaled down to the largest size the UI uses (scripts/make-device-art.sh, from the originals in
 * design/menu/art/). Asking for a height decodes the PNG once and keeps an area-filtered copy at
 * exactly that height, so each image is crisp; an animated size zooms down from the larger one.
 * Location: $HOTARU_ASSETS_DIR/devices, else the directory of the running binary + /assets/devices
 * (the sim and the host tests compile in the repo's assets/). A missing file draws nothing.
 */
#ifndef HOTARU_ART_H
#define HOTARU_ART_H

#include <stdbool.h>
#include <lvgl.h>

/* ART_UNKNOWN: a console yet to be added, a question mark on its lid */
typedef enum { ART_PS5, ART_PS4, ART_DS4, ART_DUALSENSE, ART_UNKNOWN, ART__N } art_id_t;

/* share of each console drawing's height below where it stands on the floor (its shadow) */
#define ART_PS5_FLOOR     (10.0f / 1232.0f)
#define ART_PS4_FLOOR     (20.0f / 724.0f)
#define ART_UNKNOWN_FLOOR (43.0f / 695.0f)

bool art_exists(art_id_t id);
float art_aspect(art_id_t id);                                                /* width / height */
const lv_img_dsc_t *art_img(art_id_t id, lv_coord_t h);                       /* NULL when missing */
lv_obj_t *art_create(lv_obj_t *parent, art_id_t id, lv_coord_t h);            /* an lv_img, pivot in the centre */
lv_obj_t *art_box(lv_obj_t *parent, art_id_t id, lv_coord_t w, lv_coord_t h); /* the largest that fits */
art_id_t art_console(bool ps5);
art_id_t art_pad(const char *pad_name); /* the DualSense drawing for a DualSense, else the DualShock 4 */

#endif
