/* design/menu/art.h - device illustrations: art/<name>.png, high-res, downscaled with an area
 * filter.
 *
 * Each (drawing, height) pair is made once and cached: ask for the height it is shown at and it
 * stays crisp; an animated size can zoom down from the largest one.
 */
#ifndef CHIAKI_WEBOS_ART_H
#define CHIAKI_WEBOS_ART_H

#include <lvgl.h>

typedef enum { ART_PS5, ART_PS4, ART_DS4, ART_DUALSENSE, ART__N } art_id_t;

const lv_img_dsc_t *art_img(art_id_t id, lv_coord_t h); /* NULL if the drawing is missing */
bool art_exists(art_id_t id);
lv_obj_t *art_create(lv_obj_t *parent, art_id_t id, lv_coord_t h);
float art_aspect(art_id_t id); /* width / height of the drawing */
lv_coord_t art_width(art_id_t id, lv_coord_t h);
uint16_t art_zoom_fit(const lv_obj_t *img, lv_coord_t w, lv_coord_t h); /* zoom of an art image to fit a box */

#endif
