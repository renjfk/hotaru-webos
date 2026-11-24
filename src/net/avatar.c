/* net/avatar.c - the PSN accounts' avatar pictures (see avatar.h). */
#include "avatar.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../config/config.h"
#include "../log.h"
#include "src/extra/libs/png/lodepng.h"

#ifdef HOTARU_HAVE_LIBCHIAKI
#include <curl/curl.h>
#include <json-c/json.h>
#endif

#define AVATAR_SLOTS     (CONFIG_MAX_ACCOUNTS * 2)
#define AVATAR_SIDE_MAX  1024 /* a bigger picture is not an avatar: not decoded */
#define AVATAR_BODY_MAX  (2 * 1024 * 1024)
#define AVATAR_TIMEOUT_S 20L
/* the Remote Play sign-in only has the psn:clientapp scope: the newer profile API refuses it (403),
 * the older community one (the base URL the PS4 wake-up in chiaki's holepunch.c asks for too) is
 * tried first */
#define PROFILE_BASE_URL "https://asm.np.community.playstation.net/asm/v1/apps/me/baseUrls/userProfile"
#define PROFILE2_FMT     "%s/v1/users/me/profile2?fields=onlineId,avatarUrls&avatarSizes=m,l,xl"
#define PROFILE_ME_URL   "https://m.np.playstation.com/api/userProfile/v1/internal/users/me/profiles"
#define PROFILE_ID_URL   "https://m.np.playstation.com/api/userProfile/v1/internal/users/%llu/profiles"

typedef struct {
    bool used, loaded;
    uint8_t id[8];
    lv_img_dsc_t dsc;
    uint8_t *pixels, *old; /* old: the previous picture, freed one reload later (an image may still show it) */
} slot_t;

static slot_t g_slots[AVATAR_SLOTS];
static SDL_mutex *g_lock;
static uint8_t g_ready[AVATAR_SLOTS][8]; /* under g_lock: accounts whose file was just written */
static int g_ready_n;

/* ---------- the kept file ---------- */

static void file_path(const uint8_t id[8], char *out, size_t cap) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", config_path());
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof(dir), ".");
    snprintf(out, cap, "%s/avatar-%02x%02x%02x%02x%02x%02x%02x%02x.png", dir, id[0], id[1], id[2], id[3], id[4], id[5],
             id[6], id[7]);
}

static slot_t *slot_of(const uint8_t id[8], bool make) {
    for (int i = 0; i < AVATAR_SLOTS; i++)
        if (g_slots[i].used && !memcmp(g_slots[i].id, id, 8)) return &g_slots[i];
    if (!make) return NULL;
    for (int i = 0; i < AVATAR_SLOTS; i++)
        if (!g_slots[i].used) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].used = true;
            memcpy(g_slots[i].id, id, 8);
            return &g_slots[i];
        }
    return NULL;
}

/* decode the kept PNG once into LVGL's 32-bit colour (B, G, R, A in memory) */
static void slot_load(slot_t *s) {
    s->loaded = true;
    char path[600];
    file_path(s->id, path, sizeof(path));
    if (access(path, R_OK) != 0) return;
    /* read with stdio: LVGL's lodepng opens files through lv_fs, which has no drive in this app */
    FILE *f = fopen(path, "rb");
    if (!f) return;
    unsigned char *png = NULL;
    long len = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (len > 0 && len <= AVATAR_BODY_MAX && fseek(f, 0, SEEK_SET) == 0 && (png = malloc((size_t)len)) &&
        fread(png, 1, (size_t)len, f) != (size_t)len) {
        free(png);
        png = NULL;
    }
    fclose(f);
    if (!png) return;
    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&rgba, &w, &h, png, (size_t)len);
    free(png);
    if (err || !rgba || !w || !h || w > AVATAR_SIDE_MAX || h > AVATAR_SIDE_MAX) {
        if (err) log_msg("avatar: a kept picture was not decoded: %s", lodepng_error_text(err));
        free(rgba);
        return;
    }
    for (size_t i = 0; i < (size_t)w * h; i++) { /* RGBA -> BGRA */
        unsigned char r = rgba[i * 4];
        rgba[i * 4] = rgba[i * 4 + 2];
        rgba[i * 4 + 2] = r;
    }
    free(s->old);
    s->old = s->pixels;
    s->pixels = rgba;
    memset(&s->dsc, 0, sizeof(s->dsc));
    s->dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    s->dsc.header.w = w;
    s->dsc.header.h = h;
    s->dsc.data_size = (uint32_t)(w * h * 4);
    s->dsc.data = s->pixels;
}

const lv_img_dsc_t *avatar_image(const uint8_t account_id[8]) {
    slot_t *s = slot_of(account_id, true);
    if (!s) return NULL;
    if (!s->loaded) slot_load(s);
    return s->pixels ? &s->dsc : NULL;
}

void avatar_forget(const uint8_t account_id[8]) {
    char path[600];
    file_path(account_id, path, sizeof(path));
    unlink(path);
    slot_t *s = slot_of(account_id, false);
    if (s) { /* the slot is free again; its picture may still be on screen until the UI refreshes, so
              * it is parked and freed at the next sign-out */
        static uint8_t *parked;
        free(parked);
        parked = s->pixels;
        free(s->old);
        s->pixels = s->old = NULL;
        s->used = s->loaded = false; /* dsc stays as it is until slot_of() reuses the slot */
    }
}

bool avatar_poll(void) {
    if (!g_lock) return false;
    uint8_t ready[AVATAR_SLOTS][8];
    SDL_LockMutex(g_lock);
    int n = g_ready_n;
    memcpy(ready, g_ready, sizeof(ready));
    g_ready_n = 0;
    SDL_UnlockMutex(g_lock);
    int got = 0;
    for (int i = 0; i < n; i++) {
        if (config_psn_find(ready[i]) < 0) { /* signed out while it was fetched: no slot, no file */
            char path[600];
            file_path(ready[i], path, sizeof(path));
            unlink(path);
            continue;
        }
        slot_t *s = slot_of(ready[i], true);
        if (s) slot_load(s);
        got++;
    }
    return got > 0;
}

/* ---------- fetching ---------- */

#ifdef HOTARU_HAVE_LIBCHIAKI

typedef struct {
    uint8_t id[8];
    char token[256];
} job_t;

struct body {
    unsigned char *data;
    size_t len;
};

static size_t body_cb(char *ptr, size_t size, size_t nmemb, void *user) {
    struct body *b = user;
    size_t n = size * nmemb;
    if (b->len + n > AVATAR_BODY_MAX) return 0;
    unsigned char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/* GET url into b; the HTTP status, or -1 when there was no answer */
static long http_get(const char *url, const char *token, struct body *b) {
    CURL *h = curl_easy_init();
    if (!h) return -1;
    struct curl_slist *hdrs = NULL;
    char auth[300];
    if (token) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);
        hdrs = curl_slist_append(hdrs, auth);
        hdrs = curl_slist_append(hdrs, "Accept: application/json");
    }
    curl_easy_setopt(h, CURLOPT_URL, url);
    if (hdrs) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "hotaru/" HOTARU_VERSION);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, AVATAR_TIMEOUT_S);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, b);
    CURLcode rc = curl_easy_perform(h);
    long status = -1;
    if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    else log_msg("avatar: %s not reached: %s", url, curl_easy_strerror(rc));
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    return status;
}

/* the profile's avatar of the preferred size: "avatars": [{"size", "url"}] (or the older
 * "avatarUrls": [{"size", "avatarUrl"}]) */
static bool pick_url(const char *json, char *out, size_t cap) {
    static const char *pref[] = { "l", "m", "xl", "s" };
    json_object *j = json_tokener_parse(json), *arr = NULL, *prof = NULL;
    bool ok = false;
    const char *url_key = "url";
    json_object *o = j && json_object_object_get_ex(j, "profile", &prof) ? prof : j; /* profile2 wraps it */
    if (o && !json_object_object_get_ex(o, "avatars", &arr) && json_object_object_get_ex(o, "avatarUrls", &arr))
        url_key = "avatarUrl";
    if (arr && json_object_is_type(arr, json_type_array)) {
        for (size_t p = 0; p < sizeof(pref) / sizeof(pref[0]) && !ok; p++)
            for (size_t i = 0; i < json_object_array_length(arr) && !ok; i++) {
                json_object *e = json_object_array_get_idx(arr, i), *sz = NULL, *u = NULL;
                if (!json_object_object_get_ex(e, "size", &sz) || !json_object_object_get_ex(e, url_key, &u)) continue;
                /* a key can be there with a null value: then there is no string to read */
                if (!json_object_is_type(sz, json_type_string) || !json_object_is_type(u, json_type_string)) continue;
                if (strcmp(json_object_get_string(sz), pref[p]) != 0) continue;
                snprintf(out, cap, "%s", json_object_get_string(u));
                ok = out[0] != '\0';
            }
    }
    if (j) json_object_put(j);
    return ok;
}

static unsigned long long id_number(const uint8_t id[8]) { /* chiaki keeps the account id little-endian */
    unsigned long long v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | id[i];
    return v;
}

static bool save_png(const uint8_t id[8], const struct body *b) {
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (b->len < sizeof(sig) || memcmp(b->data, sig, sizeof(sig)) != 0) {
        log_msg("avatar: the picture is not a PNG, not kept");
        return false;
    }
    char path[600], tmp[620];
    file_path(id, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path); /* one per fetch, 0600 from the start */
    int fd = mkstemp(tmp);
    if (fd < 0) return false;
    bool ok = true;
    for (size_t done = 0; ok && done < b->len;) {
        ssize_t w = write(fd, b->data + done, b->len - done);
        ok = w > 0;
        if (ok) done += (size_t)w;
    }
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

/* the older community profile: its base URL first, then profile2 of "me" */
static long legacy_profile(const char *token, struct body *out) {
    struct body base = { 0 };
    long st = http_get(PROFILE_BASE_URL, token, &base);
    if (st != 200) {
        free(base.data);
        return st;
    }
    json_object *j = base.data ? json_tokener_parse((const char *)base.data) : NULL, *u = NULL;
    char url[512] = "";
    if (j && json_object_object_get_ex(j, "url", &u) && json_object_is_type(u, json_type_string))
        snprintf(url, sizeof(url), PROFILE2_FMT, json_object_get_string(u));
    if (j) json_object_put(j);
    free(base.data);
    if (!url[0]) return -1;
    return http_get(url, token, out);
}

static int fetch_thread(void *data) {
    job_t *job = data;
    struct body prof = { 0 }, pic = { 0 };
    char url[1024] = "";
    long st = legacy_profile(job->token, &prof);
    if (st != 200 && st != 401) {
        if (st > 0) log_msg("avatar: the community profile answered HTTP %ld, trying the newer one", st);
        free(prof.data);
        memset(&prof, 0, sizeof(prof));
        st = http_get(PROFILE_ME_URL, job->token, &prof);
    }
    if (st >= 400 && st != 401) { /* "me" not taken: the account by its number */
        free(prof.data);
        memset(&prof, 0, sizeof(prof));
        char by_id[200];
        snprintf(by_id, sizeof(by_id), PROFILE_ID_URL, id_number(job->id));
        st = http_get(by_id, job->token, &prof);
    }
    if (st != 200) {
        if (st > 0) log_msg("avatar: PSN profile answered HTTP %ld%s", st, st == 401 ? " (token expired)" : "");
    } else if (!prof.data || !pick_url((const char *)prof.data, url, sizeof(url))) {
        log_msg("avatar: the PSN profile names no avatar");
    } else if ((st = http_get(url, NULL, &pic)) != 200) {
        if (st > 0) log_msg("avatar: the picture answered HTTP %ld", st);
    } else if (save_png(job->id, &pic)) {
        log_msg("avatar: kept (%zu bytes)", pic.len);
        SDL_LockMutex(g_lock);
        if (g_ready_n < AVATAR_SLOTS) memcpy(g_ready[g_ready_n++], job->id, 8);
        SDL_UnlockMutex(g_lock);
    }
    free(prof.data);
    free(pic.data);
    memset(job->token, 0, sizeof(job->token));
    free(job);
    return 0;
}

void avatar_fetch(const uint8_t account_id[8], const char *access_token) {
    if (!access_token || !access_token[0]) return;
    if (!g_lock) g_lock = SDL_CreateMutex();
    curl_global_init(CURL_GLOBAL_DEFAULT); /* the main thread; counted, so calling it again is fine */
    job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    memcpy(job->id, account_id, 8);
    snprintf(job->token, sizeof(job->token), "%s", access_token);
    SDL_Thread *t = SDL_CreateThread(fetch_thread, "avatar", job);
    if (!t) {
        log_msg("avatar: no thread: %s", SDL_GetError());
        free(job);
        return;
    }
    SDL_DetachThread(t);
}

#else

/* nothing to fetch with: the kept file is shown again, as a fetched one would be */
void avatar_fetch(const uint8_t account_id[8], const char *access_token) {
    (void)access_token;
    if (!g_lock) g_lock = SDL_CreateMutex();
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    if (g_ready_n < AVATAR_SLOTS) memcpy(g_ready[g_ready_n++], account_id, 8);
    SDL_UnlockMutex(g_lock);
}

#endif
