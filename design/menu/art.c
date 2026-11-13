/* design/menu/art.c - device illustrations from art/<name>.png (see art.h). */
#include "art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/extra/libs/png/lodepng.h"

#ifndef ART_DIR
#define ART_DIR "art"
#endif

#define CACHE_MAX 48

static const char *const g_names[ART__N] = { "ps5", "ps4", "ds4", "dualsense" };

/* one source per drawing, decoded once */
static struct {
    bool tried;
    unsigned char *rgba; /* straight alpha */
    unsigned w, h;
} g_src[ART__N];
static struct {
    art_id_t id;
    lv_coord_t h;
    lv_img_dsc_t dsc;
} g_cache[CACHE_MAX];
static int g_cached;

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!buf) {
        fclose(f);
        return NULL;
    }
    *len = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[*len] = 0;
    return buf;
}

/* CHIAKI_GALLERY_ART_DIR points the gallery at another folder of drawings */
static const char *art_dir(void) {
    const char *d = getenv("CHIAKI_GALLERY_ART_DIR");
    return d && *d ? d : ART_DIR;
}

static bool load(art_id_t id) {
    if (g_src[id].tried) return g_src[id].rgba != NULL;
    g_src[id].tried = true;
    char path[512];
    size_t len;
    snprintf(path, sizeof(path), "%s/%s.png", art_dir(), g_names[id]);
    unsigned char *buf = read_file(path, &len);
    if (buf) {
        unsigned err = lodepng_decode32(&g_src[id].rgba, &g_src[id].w, &g_src[id].h, buf, len);
        free(buf);
        if (!err) return true;
        fprintf(stderr, "art: %s: %s\n", path, lodepng_error_text(err));
        g_src[id].rgba = NULL;
        return false;
    }
    fprintf(stderr, "art: no %s\n", path);
    return false;
}

bool art_exists(art_id_t id) { return load(id); }

float art_aspect(art_id_t id) {
    if (!load(id)) return 1.0f;
    return (float)g_src[id].w / (float)g_src[id].h;
}

lv_coord_t art_width(art_id_t id, lv_coord_t h) { return (lv_coord_t)ceilf(art_aspect(id) * (float)h); }

/* Area-average resample (each output pixel = the coverage-weighted mean of the source pixels under
 * it), on premultiplied colour so transparent pixels do not darken the edges. Separable: rows,
 * then columns. Good for the large downscales a high-res PNG needs; LVGL's own zoom is not. */
static float *resample(const unsigned char *rgba, int sw, int sh, int dw, int dh) {
    /* rows: sw -> dw */
    float *tmp = calloc((size_t)dw * (size_t)sh * 4, sizeof(float));
    float sx = (float)sw / (float)dw;
    for (int y = 0; y < sh; y++) {
        const unsigned char *row = &rgba[(size_t)y * (size_t)sw * 4];
        for (int x = 0; x < dw; x++) {
            float x0 = x * sx, x1 = x0 + sx, acc[4] = { 0 }, wsum = 0;
            for (int i = (int)x0; i < (int)ceilf(x1) && i < sw; i++) {
                float w = fminf(x1, (float)(i + 1)) - fmaxf(x0, (float)i);
                if (w <= 0) continue;
                const unsigned char *p = &row[i * 4];
                float a = p[3] / 255.0f;
                acc[0] += p[0] * a * w;
                acc[1] += p[1] * a * w;
                acc[2] += p[2] * a * w;
                acc[3] += a * w;
                wsum += w;
            }
            float *o = &tmp[((size_t)y * (size_t)dw + (size_t)x) * 4];
            for (int c = 0; c < 4; c++) o[c] = wsum > 0 ? acc[c] / wsum : 0;
        }
    }
    /* columns: sh -> dh */
    float *out = calloc((size_t)dw * (size_t)dh * 4, sizeof(float));
    float sy = (float)sh / (float)dh;
    for (int y = 0; y < dh; y++) {
        float y0 = y * sy, y1 = y0 + sy;
        for (int i = (int)y0; i < (int)ceilf(y1) && i < sh; i++) {
            float w = fminf(y1, (float)(i + 1)) - fmaxf(y0, (float)i);
            if (w <= 0) continue;
            for (int x = 0; x < dw; x++) {
                const float *p = &tmp[((size_t)i * (size_t)dw + (size_t)x) * 4];
                float *o = &out[((size_t)y * (size_t)dw + (size_t)x) * 4];
                for (int c = 0; c < 4; c++) o[c] += p[c] * w / sy;
            }
        }
    }
    free(tmp);
    return out; /* premultiplied RGB (0..255) and alpha (0..1) */
}

static lv_color_t *raster_png(art_id_t id, int w, int h) {
    float *f = resample(g_src[id].rgba, (int)g_src[id].w, (int)g_src[id].h, w, h);
    lv_color_t *px = malloc((size_t)w * (size_t)h * sizeof(lv_color_t));
    for (int i = 0; i < w * h; i++) {
        const float *p = &f[i * 4];
        float a = p[3];
        int r = a > 0 ? (int)lroundf(p[0] / a) : 0, g = a > 0 ? (int)lroundf(p[1] / a) : 0,
            b = a > 0 ? (int)lroundf(p[2] / a) : 0;
        px[i] = lv_color_make((uint8_t)LV_CLAMP(0, r, 255), (uint8_t)LV_CLAMP(0, g, 255), (uint8_t)LV_CLAMP(0, b, 255));
        px[i].ch.alpha = (uint8_t)LV_CLAMP(0, (int)lroundf(a * 255.0f), 255);
    }
    free(f);
    return px;
}

const lv_img_dsc_t *art_img(art_id_t id, lv_coord_t h) {
    for (int i = 0; i < g_cached; i++)
        if (g_cache[i].id == id && g_cache[i].h == h) return &g_cache[i].dsc;
    if (!load(id) || g_cached >= CACHE_MAX || h <= 0) return NULL;
    int w = art_width(id, h);
    lv_color_t *px = raster_png(id, w, h);
    lv_img_dsc_t *d = &g_cache[g_cached].dsc;
    memset(d, 0, sizeof(*d));
    d->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->data_size = (uint32_t)(w * h) * sizeof(lv_color_t);
    d->data = (const uint8_t *)px;
    g_cache[g_cached].id = id;
    g_cache[g_cached].h = h;
    g_cached++;
    return d;
}

lv_obj_t *art_create(lv_obj_t *parent, art_id_t id, lv_coord_t h) {
    lv_obj_t *img = lv_img_create(parent);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    const lv_img_dsc_t *d = art_img(id, h);
    if (!d) return img;
    lv_img_set_src(img, d);
    lv_img_set_pivot(img, (lv_coord_t)(d->header.w / 2), (lv_coord_t)(d->header.h / 2));
    lv_img_set_antialias(img, true);
    return img;
}

uint16_t art_zoom_fit(const lv_obj_t *img, lv_coord_t w, lv_coord_t h) {
    const lv_img_dsc_t *d = lv_img_get_src((lv_obj_t *)img);
    if (!d || !d->header.w || !d->header.h) return 256;
    int32_t zw = (int32_t)w * 256 / (int32_t)d->header.w, zh = (int32_t)h * 256 / (int32_t)d->header.h;
    return (uint16_t)(zw < zh ? zw : zh);
}
