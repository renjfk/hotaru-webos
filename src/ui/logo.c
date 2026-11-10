/* ui/logo.c - the Chiaki webOS mark, drawn procedurally (see logo.h).
 *
 * Glyph geometry is the app icon's, on a 512-unit grid, scaled to the requested
 * size so every instance is crisp. The rounded gradient tile cannot be drawn on
 * the canvas, so it is an lv_obj behind a transparent canvas holding the glyph.
 */
#include "logo.h"

#include <stdlib.h>

#include "theme.h"

/* white Play triangle + two round-capped signal arcs, on the icon's 512 grid */
static void draw_glyph(lv_obj_t *cv, lv_coord_t size) {
    float k = (float)size / 512.0f;
#define S(v) ((lv_coord_t)((v) * k + 0.5f))
    lv_point_t tri[] = { { S(132), S(190) }, { S(132), S(322) }, { S(236), S(256) } };
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_white();
    rd.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_polygon(cv, tri, 3, &rd);

    lv_coord_t w = S(28);
    if (w < 2) w = 2;
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_white();
    ad.width = w;
    ad.rounded = 1;
    ad.opa = LV_OPA_COVER;
    /* LVGL arc angles: 0 deg = right, clockwise; 270->90 is the right half */
    lv_canvas_draw_arc(cv, S(284), S(256), S(88), 270, 90, &ad);
    lv_canvas_draw_arc(cv, S(330), S(256), S(144), 270, 90, &ad);
#undef S
}

static void buf_free_cb(lv_event_t *e) { free(lv_event_get_user_data(e)); }

lv_obj_t *ui_logo(lv_obj_t *parent, lv_coord_t size) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_radius(tile, (lv_coord_t)(size * 0.22f + 0.5f), 0);   /* the icon's corner */
    lv_obj_set_style_bg_color(tile, C(0x2a86ea), 0);
    lv_obj_set_style_bg_grad_color(tile, C(0x0a3c8f), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    lv_color_t *buf = malloc((size_t)size * (size_t)size * sizeof(lv_color_t));
    if (!buf) return tile;   /* degrade to the plain tile if RAM is tight */

    lv_obj_t *cv = lv_canvas_create(tile);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(cv, buf, size, size, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_center(cv);
    draw_glyph(cv, size);
    lv_obj_add_event_cb(cv, buf_free_cb, LV_EVENT_DELETE, buf);
    return tile;
}