/* ui/art.c - device illustrations from PNG (see art.h). */
#include "art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log.h"
#include "src/extra/libs/png/lodepng.h"

#define CACHE_MAX 32

static const char *const g_names[ART__N] = { "ps5", "ps4", "ds4", "dualsense", "unknown" };
static struct {
    bool tried;
    unsigned char *rgba;
    unsigned w, h;
} g_src[ART__N]; /* straight alpha */
static struct {
    art_id_t id;
    lv_coord_t h;
    lv_img_dsc_t dsc;
} g_cache[CACHE_MAX];
static int g_cached;

static const char *assets_dir(void) {
    static char dir[512];
    if (dir[0]) return dir;
    const char *env = getenv("CHIAKI_ASSETS_DIR");
#ifdef CHIAKI_ASSETS_DIR
    if (!env || !*env) env = CHIAKI_ASSETS_DIR;
#endif
    if (env && *env) {
        snprintf(dir, sizeof(dir), "%s", env);
        return dir;
    }
    char exe[400];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1); /* the TV: next to the binary */
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) *slash = '\0';
        snprintf(dir, sizeof(dir), "%s/assets", exe);
    } else {
        snprintf(dir, sizeof(dir), "assets");
    }
    return dir;
}

static bool load(art_id_t id) {
    if (g_src[id].tried) return g_src[id].rgba != NULL;
    g_src[id].tried = true;
    char path[600];
    snprintf(path, sizeof(path), "%s/devices/%s.png", assets_dir(), g_names[id]);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_msg("art: %s missing", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? malloc((size_t)n) : NULL;
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    unsigned err = buf ? lodepng_decode32(&g_src[id].rgba, &g_src[id].w, &g_src[id].h, buf, got) : 1;
    free(buf);
    if (err) {
        log_msg("art: %s: %s", path, lodepng_error_text(err));
        g_src[id].rgba = NULL;
        return false;
    }
    return true;
}

bool art_exists(art_id_t id) { return load(id); }

float art_aspect(art_id_t id) { return load(id) ? (float)g_src[id].w / (float)g_src[id].h : 1.0f; }

/* Area resample: each output pixel is the coverage-weighted mean of the source pixels under it,
 * on premultiplied colour so transparent pixels do not darken the edges. Rows, then columns. */
static lv_color_t *resample(const unsigned char *rgba, int sw, int sh, int dw, int dh) {
    float *tmp = calloc((size_t)dw * (size_t)sh * 4, sizeof(float));
    float *out = calloc((size_t)dw * (size_t)dh * 4, sizeof(float));
    lv_color_t *px = malloc((size_t)dw * (size_t)dh * sizeof(lv_color_t));
    if (!tmp || !out || !px) {
        free(tmp);
        free(out);
        free(px);
        return NULL;
    }
    float sx = (float)sw / (float)dw, sy = (float)sh / (float)dh;
    for (int y = 0; y < sh; y++) {
        const unsigned char *row = &rgba[(size_t)y * (size_t)sw * 4];
        for (int x = 0; x < dw; x++) {
            float x0 = (float)x * sx, x1 = x0 + sx, acc[4] = { 0 }, wsum = 0;
            for (int i = (int)x0; i < (int)ceilf(x1) && i < sw; i++) {
                float w = fminf(x1, (float)(i + 1)) - fmaxf(x0, (float)i);
                if (w <= 0) continue;
                const unsigned char *p = &row[i * 4];
                float a = (float)p[3] / 255.0f * w;
                acc[0] += p[0] * a;
                acc[1] += p[1] * a;
                acc[2] += p[2] * a;
                acc[3] += a;
                wsum += w;
            }
            float *o = &tmp[((size_t)y * (size_t)dw + (size_t)x) * 4];
            for (int c = 0; c < 4; c++) o[c] = wsum > 0 ? acc[c] / wsum : 0;
        }
    }
    for (int y = 0; y < dh; y++) {
        float y0 = (float)y * sy, y1 = y0 + sy;
        float *o = &out[(size_t)y * (size_t)dw * 4];
        for (int i = (int)y0; i < (int)ceilf(y1) && i < sh; i++) {
            float w = (fminf(y1, (float)(i + 1)) - fmaxf(y0, (float)i)) / sy;
            if (w <= 0) continue;
            const float *p = &tmp[(size_t)i * (size_t)dw * 4];
            for (int x = 0; x < dw * 4; x++) o[x] += p[x] * w;
        }
    }
    for (int i = 0; i < dw * dh; i++) {
        const float *p = &out[(size_t)i * 4];
        float a = p[3];
        int r = a > 0 ? (int)lroundf(p[0] / a) : 0, g = a > 0 ? (int)lroundf(p[1] / a) : 0,
            b = a > 0 ? (int)lroundf(p[2] / a) : 0;
        px[i] = lv_color_make((uint8_t)LV_CLAMP(0, r, 255), (uint8_t)LV_CLAMP(0, g, 255), (uint8_t)LV_CLAMP(0, b, 255));
        px[i].ch.alpha = (uint8_t)LV_CLAMP(0, (int)lroundf(a * 255.0f), 255);
    }
    free(tmp);
    free(out);
    return px;
}

const lv_img_dsc_t *art_img(art_id_t id, lv_coord_t h) {
    for (int i = 0; i < g_cached; i++)
        if (g_cache[i].id == id && g_cache[i].h == h) return &g_cache[i].dsc;
    if (h <= 0 || g_cached >= CACHE_MAX || !load(id)) return NULL;
    int w = (int)lroundf(art_aspect(id) * (float)h);
    if (w < 1) w = 1;
    lv_color_t *px = resample(g_src[id].rgba, (int)g_src[id].w, (int)g_src[id].h, w, h);
    if (!px) return NULL;
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

lv_obj_t *art_box(lv_obj_t *parent, art_id_t id, lv_coord_t w, lv_coord_t h) {
    lv_coord_t fit = (lv_coord_t)((float)w / art_aspect(id));
    return art_create(parent, id, fit < h ? fit : h);
}

art_id_t art_console(bool ps5) { return ps5 ? ART_PS5 : ART_PS4; }

art_id_t art_pad(const char *pad_name) { return pad_name && strstr(pad_name, "DualSense") ? ART_DUALSENSE : ART_DS4; }
