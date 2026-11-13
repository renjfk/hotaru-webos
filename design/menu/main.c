/* design/menu/main.c - the concept gallery: runs a study's concepts live in a window, or
 * records their scripts into clips, screenshots and an HTML page. Two studies: the main menu
 * (the default) and "Add a console" (--study add).
 *
 * The UI is composed over a stand-in game frame exactly like the TV does it: while "streaming"
 * LVGL renders with screen_transp on a transparent screen and the result is alpha-blended over
 * the video (the NDL plane on the TV). Recordings run on a fixed 30 fps clock (LVGL's refresh
 * period on the TV) and report how much of the screen each frame repaints.
 */
#include <SDL.h>
#include <errno.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "art.h"
#include "concept.h"
#include "kit.h"

#define W   SCR_W
#define H   SCR_H
#define FPS 30

typedef struct {
    const char *id, *title, *intro, *sheet_caption;
    const concept_t *const *concepts;
    int n;
    const char *sheet[3]; /* the shots each concept shows on the contact sheet */
    bool art;             /* the device-art section */
} study_t;

static const concept_t *const g_menu[] = { &concept_home, &concept_orbit, &concept_rail };
static const concept_t *const g_add[] = { &concept_add_setup, &concept_add_inline, &concept_add_sheet,
                                          &concept_add_dialogs };
static const study_t g_studies[] = {
    { "menu",
      "Chiaki webOS: main menu concepts",
      "Three directions for a cursor-free, D-pad and remote driven main menu, each with its in-stream menu. "
      "Built with LVGL 8.3 on the app's theme tokens and rendered at 1920x1080 exactly as the TV would "
      "(the stream menus are alpha-composed over a stand-in game frame, like the NDL video plane). Clips run on "
      "LVGL's 30 fps refresh clock. Home was picked and is the app's main menu now. Live: "
      "<code>design/menu/run.sh</code>.",
      "home   /   settings   /   menu over the stream",
      g_menu,
      3,
      { "home", "settings", "stream-menu" },
      true },
    { "add",
      "Chiaki webOS: Add a console",
      "The pairing flow redone in Home's language: tinted backdrops, frosted pills, a white focus, big targets, "
      "nothing to point at. Three places for it to live (a full-screen setup, inline in Home's hero, a sheet over "
      "Home) share the same steps and controls: the console kind as tiles, the address as four octets that start "
      "on this TV's own network, the PIN as eight boxes, a number pad the remote can drive, the PSN sign-in as a "
      "QR card and the restyled keyboard. The fourth entry brings the dialogs and the keyboard in line. Mock data, "
      "nothing wired. Live: <code>design/menu/run.sh --study add</code>.",
      "choose how   /   the PIN   /   PSN sign-in",
      g_add,
      4,
      { "method", "pin", "psn-keyboard" },
      false },
};
static const study_t *g_study = &g_studies[0];
static const concept_t *const *g_concepts = g_menu;
static int N_CONCEPTS = 3;

static lv_disp_t *g_disp;
static uint32_t *g_fb;     /* the UI frame, ARGB (alpha matters while streaming) */
static uint32_t *g_video;  /* the stand-in game frame */
static uint32_t *g_out;    /* composed frame */
static uint64_t g_flushed; /* pixels flushed since the last frame */
static bool g_stream;
static int g_cur = -1;
static lv_obj_t *g_root;

/* ---------- display ---------- */

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++) memcpy(&g_fb[y * W + a->x1], &px[(y - a->y1) * w], (size_t)w * 4);
    g_flushed += (uint64_t)w * (uint64_t)(a->y2 - a->y1 + 1);
    lv_disp_flush_ready(drv);
}

static void clear_cb(lv_disp_drv_t *drv, uint8_t *buf, uint32_t size) {
    const lv_area_t *a = drv->draw_ctx ? drv->draw_ctx->buf_area : NULL;
    uint32_t n = a ? lv_area_get_size(a) : size;
    memset(buf, 0, (size_t)(n < size ? n : size) * sizeof(lv_color_t));
}

static void display_init(void) {
    g_fb = calloc((size_t)W * H, 4);
    g_video = calloc((size_t)W * H, 4);
    g_out = calloc((size_t)W * H, 4);
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, malloc((size_t)W * H * sizeof(lv_color_t)), NULL, W * H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &db;
    drv.flush_cb = flush_cb;
    drv.clear_cb = clear_cb;
    drv.hor_res = W;
    drv.ver_res = H;
    g_disp = lv_disp_drv_register(&drv);
}

/* ---------- the stand-in game frame: dusk over three mountain ridges, with a HUD ---------- */

static uint32_t channel(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v; }
static uint32_t rgb(float r, float g, float b) { return 0xff000000u | channel(r) << 16 | channel(g) << 8 | channel(b); }

static void fill(int x, int y, int w, int h, uint32_t c, float a) {
    for (int j = y; j < y + h && j < H; j++)
        for (int i = x; i < x + w && i < W; i++) {
            uint32_t d = g_video[j * W + i];
            float r = ((d >> 16) & 255) * (1 - a) + ((c >> 16) & 255) * a;
            float g = ((d >> 8) & 255) * (1 - a) + ((c >> 8) & 255) * a;
            float b = (d & 255) * (1 - a) + (c & 255) * a;
            g_video[j * W + i] = rgb(r, g, b);
        }
}

static void video_render(float t) {
    static float ridge[3][W];
    static const float base[3] = { 610, 700, 800 }, amp[3] = { 70, 60, 46 }, speed[3] = { 8, 22, 60 };
    static const float col[3][3] = { { 122, 64, 120 }, { 70, 36, 92 }, { 30, 16, 48 } };
    for (int l = 0; l < 3; l++)
        for (int x = 0; x < W; x++) {
            float xs = (float)x + t * speed[l];
            ridge[l][x] = base[l] + amp[l] * sinf(xs * 0.0042f + l * 1.3f) + amp[l] * 0.45f * sinf(xs * 0.0131f + l) +
                          amp[l] * 0.18f * sinf(xs * 0.041f + l * 2.1f);
        }
    float sx = 1380, sy = 470 + 6 * sinf(t * 0.5f);
    for (int y = 0; y < H; y++) {
        float k = (float)y / 700.0f;
        float sr, sg, sb;
        if (k < 0.55f) {
            float u = k / 0.55f;
            sr = 26 + u * 96;
            sg = 18 + u * 40;
            sb = 64 + u * 58;
        } else {
            float u = fminf((k - 0.55f) / 0.45f, 1);
            sr = 122 + u * 133;
            sg = 58 + u * 96;
            sb = 122 - u * 32;
        }
        for (int x = 0; x < W; x++) {
            uint32_t *p = &g_video[y * W + x];
            int l = -1;
            for (int i = 2; i >= 0; i--)
                if ((float)y >= ridge[i][x]) {
                    l = i;
                    break;
                }
            if (y > 880) {
                float u = (float)(y - 880) / 200.0f;
                *p = rgb(22 - u * 10, 12 - u * 6, 36 - u * 14);
                continue;
            }
            if (l >= 0) {
                float shade = 1.0f - fminf(((float)y - ridge[l][x]) / 400.0f, 0.5f);
                *p = rgb(col[l][0] * shade, col[l][1] * shade, col[l][2] * shade);
                continue;
            }
            float dx = x - sx, dy = y - sy, d = sqrtf(dx * dx + dy * dy);
            if (d < 110) {
                *p = rgb(255, 226, 160);
                continue;
            }
            float gl = fmaxf(0, 1 - d / 520);
            gl = gl * gl * 0.7f;
            *p = rgb(sr + 255 * gl, sg + 160 * gl, sb + 80 * gl);
        }
    }
    /* HUD: health + stamina, minimap, crosshair, ammo */
    fill(64, 56, 440, 26, 0x000000, 0.45f);
    fill(68, 60, 300, 18, 0x4ade80, 0.95f);
    fill(64, 92, 320, 12, 0x000000, 0.45f);
    fill(66, 94, 230, 8, 0x60a5fa, 0.95f);
    for (int y = 40; y < 260; y++)
        for (int x = 1680; x < 1900; x++) {
            float d = hypotf((float)(x - 1790), (float)(y - 150));
            if (d < 100) fill(x, y, 1, 1, d > 94 ? 0xffffff : 0x0b0b18, d > 94 ? 0.7f : 0.45f);
        }
    fill(1786, 146, 8, 8, 0xff5468, 1);
    fill(950, 539, 20, 2, 0xffffff, 0.8f);
    fill(959, 530, 2, 20, 0xffffff, 0.8f);
    for (int i = 0; i < 12; i++) fill(1580 + i * 22, 1000, 14, 30, 0xffe08a, i < 9 ? 0.9f : 0.25f);
}

static void compose(void) {
    if (!g_stream) {
        for (int i = 0; i < W * H; i++) g_out[i] = g_fb[i] | 0xff000000u;
        return;
    }
    for (int i = 0; i < W * H; i++) {
        uint32_t f = g_fb[i], v = g_video[i], a = f >> 24;
        if (a == 255) {
            g_out[i] = f;
            continue;
        }
        if (a == 0) {
            g_out[i] = v;
            continue;
        }
        uint32_t r = (((f >> 16) & 255) * a + ((v >> 16) & 255) * (255 - a)) / 255;
        uint32_t g = (((f >> 8) & 255) * a + ((v >> 8) & 255) * (255 - a)) / 255;
        uint32_t b = ((f & 255) * a + (v & 255) * (255 - a)) / 255;
        g_out[i] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
}

static double ui_coverage(void) {
    uint64_t n = 0;
    for (int i = 0; i < W * H; i++) n += (g_fb[i] >> 24) != 0;
    return (double)n / (double)(W * H);
}

/* ---------- concepts ---------- */

static void set_stream(bool on) {
    if (g_stream == on) return;
    g_stream = on;
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_opa(scr, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    g_disp->driver->screen_transp = on ? 1 : 0;
    if (g_concepts[g_cur]->set_stream) g_concepts[g_cur]->set_stream(on);
    lv_obj_invalidate(scr);
}

static void load_concept(int i) {
    lv_obj_clean(lv_layer_top());
    if (g_stream) {
        g_stream = false;
        lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
        g_disp->driver->screen_transp = 0;
    }
    if (g_root) lv_obj_del(g_root);
    lv_anim_del_all();
    mock_reset();
    g_cur = i;
    g_root = kit_obj(lv_scr_act());
    lv_obj_set_size(g_root, W, H);
    g_concepts[i]->build(g_root);
    lv_obj_invalidate(lv_scr_act());
}

static void screen_init(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_remove_style_all(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, C(COL_BG_BOTTOM), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    kit_stream_hook = set_stream;
}

/* ---------- live window ---------- */

static int run_window(int first) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow(g_study->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_Texture *ui = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_Texture *vid = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    SDL_SetTextureBlendMode(ui, SDL_BLENDMODE_BLEND);
    load_concept(first);
    printf("keys: 1-%d concept, arrows D-pad, Enter Cross, Backspace/Esc Circle, M Share+Options (stream menu),\n"
           "      V start/stop the stand-in stream, Q quit\n",
           N_CONCEPTS);
    Uint32 last = SDL_GetTicks(), start = last;
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type != SDL_KEYDOWN) continue;
            SDL_Keycode k = e.key.keysym.sym;
            const concept_t *c = g_concepts[g_cur];
            switch (k) {
                case SDLK_UP: c->nav(NAV_UP); break;
                case SDLK_DOWN: c->nav(NAV_DOWN); break;
                case SDLK_LEFT: c->nav(NAV_LEFT); break;
                case SDLK_RIGHT: c->nav(NAV_RIGHT); break;
                case SDLK_RETURN:
                case SDLK_SPACE: c->nav(NAV_OK); break;
                case SDLK_BACKSPACE:
                case SDLK_ESCAPE: c->nav(NAV_BACK); break;
                case SDLK_m:
                case SDLK_TAB: c->nav(NAV_MENU); break;
                case SDLK_v: set_stream(!g_stream); break;
                case SDLK_q: quit = true; break;
                default:
                    if (k >= SDLK_1 && k < SDLK_1 + N_CONCEPTS) load_concept(k - SDLK_1);
                    break;
            }
        }
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last);
        last = now;
        g_flushed = 0;
        lv_timer_handler();
        if (g_flushed) SDL_UpdateTexture(ui, NULL, g_fb, W * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (g_stream) {
            video_render((float)(now - start) / 1000.0f);
            SDL_UpdateTexture(vid, NULL, g_video, W * 4);
            SDL_RenderCopy(ren, vid, NULL, NULL);
        }
        SDL_RenderCopy(ren, ui, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}

/* ---------- recorder ---------- */

static uint64_t g_frame;
static uint32_t g_last_ms;
static FILE *g_rec;
static char g_rec_meta[512];
static struct {
    int frames;
    double flush_sum, flush_max, ms_sum, ms_max, cov_sum;
    int cov_n;
} g_m;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void step_frame(void) {
    g_frame++;
    uint32_t ms = (uint32_t)(g_frame * 1000 / FPS);
    lv_tick_inc(ms - g_last_ms);
    g_last_ms = ms;
    g_flushed = 0;
    double t0 = now_ms();
    lv_timer_handler();
    lv_refr_now(g_disp);
    double dt = now_ms() - t0;
    if (g_stream) video_render((float)ms / 1000.0f);
    if (!g_rec) return;
    compose();
    fwrite(g_out, 4, (size_t)W * H, g_rec);
    double f = (double)g_flushed / (double)(W * H);
    g_m.frames++; /* averaged over every frame of the clip, idle ones included: the load over time */
    g_m.flush_sum += f;
    g_m.ms_sum += dt;
    if (f > g_m.flush_max) g_m.flush_max = f;
    if (dt > g_m.ms_max) g_m.ms_max = dt;
    if (g_stream) {
        double c = ui_coverage();
        if (c > 0.01) {
            g_m.cov_sum += c;
            g_m.cov_n++;
        }
    }
}

static void write_png(const char *path) {
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "magick ppm:- '%s'", path);
    FILE *p = popen(cmd, "w"); /* NOLINT(cert-env33-c): the recorder's own command line */
    if (!p) {
        fprintf(stderr, "magick: %s\n", strerror(errno));
        return;
    }
    fprintf(p, "P6\n%d %d\n255\n", W, H);
    unsigned char *row = malloc((size_t)W * 3);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t c = g_out[y * W + x];
            row[x * 3] = (unsigned char)(c >> 16);
            row[x * 3 + 1] = (unsigned char)(c >> 8);
            row[x * 3 + 2] = (unsigned char)c;
        }
        fwrite(row, 1, (size_t)W * 3, p);
    }
    free(row);
    pclose(p);
}

static void rec_start(const char *path) {
    char cmd[1400];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - "
             "-c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -movflags +faststart '%s'",
             W, H, FPS, path);
    g_rec = popen(cmd, "w"); /* NOLINT(cert-env33-c): the recorder's own command line */
    memset(&g_m, 0, sizeof(g_m));
}

static void rec_end(const char *meta_path) {
    if (!g_rec) return;
    pclose(g_rec);
    g_rec = NULL;
    int n = g_m.frames ? g_m.frames : 1;
    int len =
        snprintf(g_rec_meta, sizeof(g_rec_meta),
                 "%.1f s; repainted per frame: avg %.0f%%, peak %.0f%% of the screen; "
                 "Mac render avg %.1f ms, peak %.1f ms",
                 g_m.frames / (double)FPS, 100 * g_m.flush_sum / n, 100 * g_m.flush_max, g_m.ms_sum / n, g_m.ms_max);
    if (g_m.cov_n && len > 0 && len < (int)sizeof(g_rec_meta))
        snprintf(g_rec_meta + len, sizeof(g_rec_meta) - (size_t)len, "; menu covers %.0f%% of the video",
                 100 * g_m.cov_sum / g_m.cov_n);
    FILE *f = fopen(meta_path, "w");
    if (f) {
        fprintf(f, "%s\n", g_rec_meta);
        fclose(f);
    }
    printf("    %s\n", g_rec_meta);
}

static void run_script(const concept_t *c, const char *dir) {
    char path[1024], meta[1024];
    for (const step_t *s = c->script; s->kind != ST_END; s++) {
        switch (s->kind) {
            case ST_KEY: c->nav((nav_t)s->arg); break;
            case ST_WAIT:
                for (int i = 0; i < s->arg * FPS / 1000; i++) step_frame();
                break;
            case ST_SHOT:
                step_frame();
                compose();
                snprintf(path, sizeof(path), "%s/%s.png", dir, s->name);
                write_png(path);
                printf("  shot %s\n", path);
                break;
            case ST_REC:
                snprintf(path, sizeof(path), "%s/%s.mp4", dir, s->name);
                snprintf(meta, sizeof(meta), "%s/%s.txt", dir, s->name);
                printf("  clip %s\n", path);
                rec_start(path);
                break;
            case ST_REC_END: rec_end(meta); break;
            case ST_STREAM: set_stream(s->arg != 0); break;
            case ST_END: break;
        }
    }
    rec_end(meta);
}

static void html_media(FILE *f, const concept_t *c, const char *out) {
    for (const step_t *s = c->script; s->kind != ST_END; s++) {
        if (s->kind == ST_REC) {
            char meta[1024], line[512] = "";
            snprintf(meta, sizeof(meta), "%s/%s/%s.txt", out, c->id, s->name);
            FILE *m = fopen(meta, "r");
            if (m) {
                if (!fgets(line, sizeof(line), m)) line[0] = 0;
                fclose(m);
                line[strcspn(line, "\n")] = 0;
            }
            fprintf(f,
                    "<figure class=\"clip\"><video src=\"%s/%s.mp4\" autoplay loop muted playsinline controls></video>"
                    "<figcaption><b>%s</b><span>%s</span></figcaption></figure>\n",
                    c->id, s->name, s->caption, line);
        } else if (s->kind == ST_SHOT) {
            fprintf(f,
                    "<figure class=\"shot\"><a href=\"%s/%s.png\"><img src=\"%s/%s.png\" loading=\"lazy\"></a>"
                    "<figcaption><b>%s</b></figcaption></figure>\n",
                    c->id, s->name, c->id, s->name, s->caption);
        }
    }
}

static void write_html(const char *out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/index.html", out);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(
        f,
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>%s</title>\n"
        "<style>\n"
        "body{margin:0;background:#07090f;color:#eef2f8;font:16px/1.5 -apple-system,Helvetica,Arial,sans-serif}\n"
        "header{padding:40px 56px 8px}h1{margin:0 0 6px;font-size:34px}header p{color:#8f9bb4;max-width:1100px}\n"
        "nav a{color:#3592ff;margin-right:18px;text-decoration:none;font-weight:600}\n"
        "section{padding:28px 56px 40px;border-top:1px solid #1c2640}\n"
        "h2{margin:0;font-size:28px}h2 small{color:#3592ff;font-size:15px;letter-spacing:2px;margin-right:12px}\n"
        ".tag{color:#c3cbe0;font-size:19px;margin:6px 0 14px}.notes{color:#aab4c8;max-width:1200px}\n"
        ".notes b{color:#eef2f8}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(560px,1fr));gap:22px;"
        "margin-top:18px}\n"
        "figure{margin:0;background:#0e1424;border:1px solid #1c2640;border-radius:14px;overflow:hidden}\n"
        "video,img{display:block;width:100%%;aspect-ratio:16/9;background:#000}\n"
        "figcaption{padding:10px 14px 12px}figcaption b{display:block}figcaption span{color:#8f9bb4;font-size:13px}\n"
        "</style></head><body>\n"
        "<header><h1>%s</h1>\n<p>%s</p>\n<nav>",
        g_study->title, g_study->title, g_study->intro);
    for (int i = 0; i < N_CONCEPTS; i++) fprintf(f, "<a href=\"#%s\">%s</a>", g_concepts[i]->id, g_concepts[i]->name);
    if (g_study->art) fprintf(f, "<a href=\"#art\">Device art</a>");
    fprintf(f, "<a href=\"contact-sheet.png\">Contact sheet</a></nav></header>\n");
    for (int i = 0; i < N_CONCEPTS; i++) {
        const concept_t *c = g_concepts[i];
        fprintf(f, "<section id=\"%s\"><h2><small>%.2s</small>%s</h2><div class=\"tag\">%s</div>\n", c->id, c->id,
                c->name, c->tagline);
        fprintf(f, "<div class=\"notes\">%s</div><div class=\"grid\">\n", c->notes);
        html_media(f, c, out);
        fprintf(f, "</div></section>\n");
    }
    if (g_study->art)
        fprintf(f,
                "<section id=\"art\"><h2>Device art</h2><div class=\"tag\">Illustrations in design/menu/art/, "
                "downscaled once to each size they are shown at</div><div class=\"notes\"><p>The PS5, the PS4, "
                "the DualShock 4 and the DualSense: large, on the Home tiles (176 and 256 px) and icon sized. The "
                "two controllers share one canvas, so either can be the connected pad.</p></div>"
                "<div class=\"grid\"><figure class=\"shot\" "
                "style=\"grid-column:1/-1\"><a href=\"art.png\"><img src=\"art.png\"></a></figure></div></section>\n");
    fprintf(f, "</body></html>\n");
    fclose(f);
    printf("gallery: %s\n", path);
    snprintf(path, sizeof(path), "%s/concepts.tsv", out); /* for capture.sh's contact sheet labels */
    f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < N_CONCEPTS; i++) {
        const concept_t *c = g_concepts[i];
        const char *const *sh = c->sheet[0] ? c->sheet : g_study->sheet;
        fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\n", c->id, c->name, c->tagline, sh[0], sh[1], sh[2]);
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/study.tsv", out); /* the sheet's title and caption */
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\t%s\n", g_study->title, g_study->sheet_caption);
    fclose(f);
}

static int run_capture(const char *out, const char *only) {
    mkdir(out, 0755);
    /* one render per frame, after the animations of that frame: step_frame() calls lv_refr_now().
     * Pausing does not stick (every invalidation resumes the timer), so push its period out instead. */
    lv_timer_set_period(_lv_disp_get_refr_timer(g_disp), 0x7fffffff);
    for (int i = 0; i < N_CONCEPTS; i++) {
        const concept_t *c = g_concepts[i];
        if (only && !strstr(c->id, only)) continue;
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/%s", out, c->id);
        mkdir(dir, 0755);
        printf("%s\n", c->id);
        g_frame = 0;
        g_last_ms = 0;
        load_concept(i);
        for (int k = 0; k < FPS / 2; k++) step_frame(); /* settle the entrance */
        run_script(c, dir);
    }
    write_html(out);
    return 0;
}

/* a sheet of every device drawing: large, on the Home tiles (176 and 256 px) and icon sized;
 * a drawing whose SVG is missing leaves its place empty */
static int run_art(const char *path) {
    lv_obj_t *root = kit_obj(lv_scr_act());
    lv_obj_set_size(root, W, H);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    kit_grad(root, 0x1d57b8, COL_BG_BOTTOM);
    static const lv_coord_t big[ART__N] = { 520, 250, 210, 210 };
    lv_coord_t x = 60;
    for (int id = 0; id < ART__N; id++) {
        lv_coord_t w = art_exists((art_id_t)id) ? art_width((art_id_t)id, big[id]) : 300;
        if (art_exists((art_id_t)id))
            lv_obj_set_pos(art_create(root, (art_id_t)id, big[id]), x, id == ART_PS5 ? 30 : 170);
        x += w + 60;
    }
    static const lv_coord_t tiles[2] = { 176, 256 };
    for (int t = 0; t < 2; t++) {
        lv_coord_t s = tiles[t];
        x = 60;
        for (int id = 0; id < ART__N; id++) {
            lv_obj_t *tile = kit_rect(root, s, s, 30, 0x2d528f, LV_OPA_COVER);
            kit_grad(tile, id == ART_PS4 ? 0x3b4fa0 : 0x2d528f, 0x0f1a33);
            lv_obj_set_pos(tile, x, t ? 800 : 600);
            if (art_exists((art_id_t)id)) {
                lv_coord_t ah = id == ART_PS5 ? s * 80 / 100 : (lv_coord_t)(s * 0.88f / art_aspect((art_id_t)id));
                lv_obj_center(art_create(tile, (art_id_t)id, ah));
            }
            x += s + 24;
        }
    }
    for (int id = 0; id < ART__N; id++)
        for (int k = 0; k < 2; k++) {
            if (!art_exists((art_id_t)id)) continue;
            lv_obj_t *a = art_create(root, (art_id_t)id, k ? 72 : 40);
            lv_obj_set_pos(a, 1440 + k * 150, 600 + id * 110);
        }
    lv_refr_now(g_disp);
    compose();
    write_png(path);
    printf("art sheet: %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    const char *capture = NULL, *only = NULL, *art = NULL;
    int first = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--capture") && i + 1 < argc) capture = argv[++i];
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--art") && i + 1 < argc) art = argv[++i];
        else if (!strcmp(argv[i], "--study") && i + 1 < argc) {
            const char *id = argv[++i];
            for (size_t s = 0; s < sizeof(g_studies) / sizeof(g_studies[0]); s++)
                if (!strcmp(g_studies[s].id, id)) g_study = &g_studies[s];
            g_concepts = g_study->concepts;
            N_CONCEPTS = g_study->n;
        } else if (argv[i][0] >= '1' && argv[i][0] <= '9') first = atoi(argv[i]) - 1;
    }
    if (first < 0 || first >= N_CONCEPTS) first = 0;
    lv_init();
    display_init();
    theme_init(g_disp);
    screen_init();
    kit_init();
    if (art) return run_art(art);
    return capture ? run_capture(capture, only) : run_window(first);
}
