/* ui/logo.c - the Chiaki webOS mark, drawn procedurally (see logo.h).
 *
 * Glyph geometry is the app icon's, on a 512-unit grid that spans the whole tile,
 * scaled to the requested size so every instance is crisp. The rounded gradient
 * tile cannot be drawn on the canvas, so it is an lv_obj behind a transparent
 * canvas holding the glyph.
 */
#include "logo.h"

#include <stdbool.h>
#include <stdlib.h>

#include "theme.h"

#define STROKE 34.0f /* outline width of the arcs, the circle and the cross */

/* The solid gamepad (bottom-right) on its own grid (x +-106, y -62..86), scaled by PAD_F and
 * placed at (366, 366) lifted by 10 units. Right half only, the left is mirrored. LVGL fills
 * convex polygons only, so the body and each grip are filled separately (the grips reach up
 * into the body so no anti-aliased seam shows), then the outline is stroked PAD_STROKE wide
 * to round the silhouette off like the icon. */
#define PAD_F      (101.2f / 106.0f)
#define PAD_CX     366.0f
#define PAD_CY     (366.0f - 10.0f * PAD_F)
#define PAD_STROKE 14.0f
/* segments per Bezier curve: the fills stay coarse because LVGL masks each sloped polygon edge
 * and holds only 16 masks (_LV_MASK_MAX_NUM); the finer outline stroke covers the facets */
#define PAD_FILL_STEPS 4
#define PAD_LINE_STEPS 8

static const float PAD_SHOULDER[4][2] = { { 50, -62 }, { 78, -62 }, { 90, -46 }, { 94, -24 } };
static const float PAD_GRIP[4][2] = { { 104, 52 }, { 108, 80 }, { 84, 92 }, { 68, 74 } };
static const float PAD_CROTCH[2] = { 39.4f, 35 }; /* inner grip edge meets the bottom edge */
static const float PAD_SIDE[2] = { 101.8f, 35 };  /* outer grip edge at the bottom edge */
static const float PAD_SEAM[2] = { 100.45f, 25 }; /* outer grip edge inside the body */

typedef struct {
    lv_point_t p[48];
    int n;
    float k;
} pen_t;

static void pen_to(pen_t *pen, float x, float y) {
    if (pen->n >= (int)(sizeof(pen->p) / sizeof(pen->p[0]))) return;
    pen->p[pen->n].x = (lv_coord_t)((PAD_CX + PAD_F * x) * pen->k + 0.5f);
    pen->p[pen->n].y = (lv_coord_t)((PAD_CY + PAD_F * y) * pen->k + 0.5f);
    pen->n++;
}

/* the curve's points after its start (c[0], or c[3] when walked in reverse); side mirrors x */
static void pen_curve(pen_t *pen, const float c[4][2], float side, bool reverse, int steps) {
    for (int i = 1; i <= steps; i++) {
        float t = (float)(reverse ? steps - i : i) / (float)steps, u = 1.0f - t;
        float a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
        pen_to(pen, side * (a * c[0][0] + b * c[1][0] + d * c[2][0] + e * c[3][0]),
               a * c[0][1] + b * c[1][1] + d * c[2][1] + e * c[3][1]);
    }
}

static void draw_pad(lv_obj_t *cv, float k) {
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_white();
    fill.bg_opa = LV_OPA_COVER;
    pen_t pen = { .n = 0, .k = k };

    pen_to(&pen, PAD_SHOULDER[0][0], PAD_SHOULDER[0][1]);
    pen_curve(&pen, PAD_SHOULDER, 1, false, PAD_FILL_STEPS);
    pen_to(&pen, PAD_SIDE[0], PAD_SIDE[1]);
    pen_to(&pen, -PAD_SIDE[0], PAD_SIDE[1]);
    pen_to(&pen, -PAD_SHOULDER[3][0], PAD_SHOULDER[3][1]);
    pen_curve(&pen, PAD_SHOULDER, -1, true, PAD_FILL_STEPS);
    lv_canvas_draw_polygon(cv, pen.p, (uint32_t)pen.n, &fill);

    for (int side = -1; side <= 1; side += 2) {
        pen.n = 0;
        pen_to(&pen, side * PAD_CROTCH[0], PAD_CROTCH[1]);
        pen_to(&pen, side * PAD_GRIP[3][0], PAD_GRIP[3][1]);
        pen_curve(&pen, PAD_GRIP, (float)side, true, PAD_FILL_STEPS);
        pen_to(&pen, side * PAD_SEAM[0], PAD_SEAM[1]);
        lv_canvas_draw_polygon(cv, pen.p, (uint32_t)pen.n, &fill);
    }

    pen.n = 0;
    pen_to(&pen, PAD_SHOULDER[0][0], PAD_SHOULDER[0][1]);
    pen_curve(&pen, PAD_SHOULDER, 1, false, PAD_LINE_STEPS);
    pen_to(&pen, PAD_GRIP[0][0], PAD_GRIP[0][1]);
    pen_curve(&pen, PAD_GRIP, 1, false, PAD_LINE_STEPS);
    pen_to(&pen, PAD_CROTCH[0], PAD_CROTCH[1]);
    pen_to(&pen, -PAD_CROTCH[0], PAD_CROTCH[1]);
    pen_to(&pen, -PAD_GRIP[3][0], PAD_GRIP[3][1]);
    pen_curve(&pen, PAD_GRIP, -1, true, PAD_LINE_STEPS);
    pen_to(&pen, -PAD_SHOULDER[3][0], PAD_SHOULDER[3][1]);
    pen_curve(&pen, PAD_SHOULDER, -1, true, PAD_LINE_STEPS);
    pen_to(&pen, PAD_SHOULDER[0][0], PAD_SHOULDER[0][1]);
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_white();
    ld.width = (lv_coord_t)(PAD_STROKE * k + 0.5f);
    if (ld.width < 1) ld.width = 1;
    ld.round_start = 1;
    ld.round_end = 1;
    ld.opa = LV_OPA_COVER;
    lv_canvas_draw_line(cv, pen.p, (uint32_t)pen.n, &ld);
}

/* 2x2 grid: Wi-Fi arcs, circle / cross, solid gamepad. Strokes are centred on the outline;
 * LVGL arcs grow inward from their radius, so they are drawn half a stroke larger. */
static void draw_glyph(lv_obj_t *cv, lv_coord_t size) {
    float k = (float)size / 512.0f;
#define S(v) ((lv_coord_t)((v) * k + 0.5f))
    lv_coord_t w = S(STROKE);
    if (w < 2) w = 2;

    /* Wi-Fi: a dot under two round-capped arcs (LVGL angles: 0 deg = right, clockwise) */
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_white();
    ad.width = w;
    ad.rounded = 1;
    ad.opa = LV_OPA_COVER;
    lv_canvas_draw_arc(cv, S(146), S(208.4f), S(65.6f + STROKE / 2), 224, 316, &ad);
    lv_canvas_draw_arc(cv, S(146), S(208.4f), S(126.4f + STROKE / 2), 224, 316, &ad);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_white();
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, S(146 - 19.2f), S(208.4f - 19.2f), S(38.4f), S(38.4f), &rd);

    ad.rounded = 0;
    lv_canvas_draw_arc(cv, S(366), S(146), S(80 + STROKE / 2), 0, 360, &ad);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_white();
    ld.width = w;
    ld.round_start = 1;
    ld.round_end = 1;
    ld.opa = LV_OPA_COVER;
    lv_point_t cross_a[] = { { S(78), S(298) }, { S(214), S(434) } };
    lv_point_t cross_b[] = { { S(214), S(298) }, { S(78), S(434) } };
    lv_canvas_draw_line(cv, cross_a, 2, &ld);
    lv_canvas_draw_line(cv, cross_b, 2, &ld);

    draw_pad(cv, k);
#undef S
}

static void buf_free_cb(lv_event_t *e) { free(lv_event_get_user_data(e)); }

lv_obj_t *ui_logo(lv_obj_t *parent, lv_coord_t size) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_radius(tile, (lv_coord_t)(size * 0.22f + 0.5f), 0); /* the icon's corner */
    lv_obj_set_style_bg_color(tile, C(0x2a86ea), 0);
    lv_obj_set_style_bg_grad_color(tile, C(0x0a3c8f), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    lv_color_t *buf = malloc((size_t)size * (size_t)size * sizeof(lv_color_t));
    if (!buf) return tile; /* degrade to the plain tile if RAM is tight */

    lv_obj_t *cv = lv_canvas_create(tile);
    lv_obj_clear_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_set_buffer(cv, buf, size, size, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_center(cv);
    draw_glyph(cv, size);
    lv_obj_add_event_cb(cv, buf_free_cb, LV_EVENT_DELETE, buf);
    return tile;
}
