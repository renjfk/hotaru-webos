/* net/update.c - the release check against GitHub (see update.h). */
#include "update.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../log.h"

#ifdef HOTARU_HAVE_LIBCHIAKI
#include <curl/curl.h>
#include <json-c/json.h>
#endif

#define UPDATE_URL       "https://api.github.com/repos/" UPDATE_REPO "/releases/latest"
#define UPDATE_TIMEOUT_S 15L
#define UPDATE_TRIES     4            /* a TV just switched on may not be on its network yet */
#define UPDATE_RETRY_MS  15000        /* ... so a transfer that fails is tried again this much later */
#define UPDATE_BODY_MAX  (512 * 1024) /* the release is a few KB of JSON: anything this big is not it */
#define TAG_MAX          64

static SDL_mutex *g_lock;
static bool g_started;
static bool g_answered;        /* under g_lock: an answer waits in g_tag for update_poll() */
static char g_tag[TAG_MAX];    /* under g_lock: the latest release's tag, "" when the check failed */
static char g_latest[TAG_MAX]; /* main thread: the newer version, "" when none */

static void lock_init(void) {
    if (!g_lock) g_lock = SDL_CreateMutex();
}

/* any thread: tag NULL when there is no answer to give */
static void answer(const char *tag) {
    SDL_LockMutex(g_lock);
    snprintf(g_tag, sizeof(g_tag), "%s", tag ? tag : "");
    g_answered = true;
    SDL_UnlockMutex(g_lock);
}

/* "v0.3" or "0.2.57" -> {0, 3, 0} / {0, 2, 57}; false when it does not start with a number */
static bool parse_version(const char *s, long out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (s && (*s == 'v' || *s == 'V')) s++;
    if (!s || !isdigit((unsigned char)*s)) return false;
    for (int i = 0; i < 3 && s && isdigit((unsigned char)*s); i++) {
        char *end;
        out[i] = strtol(s, &end, 10);
        s = *end == '.' ? end + 1 : NULL;
    }
    return true;
}

bool update_newer(const char *tag, const char *installed) {
    long a[3], b[3];
    if (!parse_version(tag, a)) return false;
    parse_version(installed, b);
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

#ifdef HOTARU_HAVE_LIBCHIAKI

enum { FETCH_OK, FETCH_RETRY, FETCH_FAIL };

struct body {
    char *data;
    size_t len;
};

static size_t body_cb(char *ptr, size_t size, size_t nmemb, void *user) {
    struct body *b = user;
    size_t n = size * nmemb;
    if (b->len + n > UPDATE_BODY_MAX) return 0; /* ends the transfer */
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/* the latest release's tag into tag; FETCH_RETRY when GitHub was not reached */
static int fetch_latest(char *tag, size_t cap) {
    CURL *h = curl_easy_init();
    if (!h) return FETCH_FAIL;
    struct body b = { 0 };
    struct curl_slist *hdrs = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    hdrs = curl_slist_append(hdrs, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(h, CURLOPT_URL, UPDATE_URL);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "hotaru/" HOTARU_VERSION); /* GitHub's API wants one */
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);                  /* a renamed repository redirects */
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L); /* a worker thread: no signals for the DNS timeout */
    curl_easy_setopt(h, CURLOPT_TIMEOUT, UPDATE_TIMEOUT_S);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);

    int res = FETCH_FAIL;
    if (rc != CURLE_OK) {
        log_msg("update: GitHub not reached: %s", curl_easy_strerror(rc));
        res = FETCH_RETRY;
    } else if (status != 200) {
        log_msg("update: GitHub answered HTTP %ld%s", status,
                status == 404 ? " (no release yet, or the repository is private)" : "");
    } else {
        json_object *j = b.data ? json_tokener_parse(b.data) : NULL, *v = NULL;
        const char *s = j && json_object_object_get_ex(j, "tag_name", &v) ? json_object_get_string(v) : NULL;
        if (s && *s) {
            snprintf(tag, cap, "%s", s);
            res = FETCH_OK;
        } else {
            log_msg("update: GitHub's answer has no tag_name");
        }
        if (j) json_object_put(j);
    }
    free(b.data);
    return res;
}

static int check_thread(void *data) {
    (void)data;
    char tag[TAG_MAX] = "";
    int res = FETCH_RETRY;
    for (int i = 0; i < UPDATE_TRIES && res == FETCH_RETRY; i++) {
        if (i) SDL_Delay(UPDATE_RETRY_MS);
        res = fetch_latest(tag, sizeof(tag));
    }
    answer(res == FETCH_OK ? tag : NULL);
    return 0;
}

static void check(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT); /* here on the main thread: it is not thread-safe everywhere */
    SDL_Thread *t = SDL_CreateThread(check_thread, "update", NULL);
    if (!t) {
        log_msg("update: no thread for the check: %s", SDL_GetError());
        return;
    }
    SDL_DetachThread(t);
}

#else

static void check(void) { log_msg("update: not checked (no libcurl in this build)"); }

#endif

void update_start(void) {
    if (g_started) return;
    g_started = true;
    lock_init();
    const char *fake = getenv("HOTARU_UPDATE_LATEST");
    if (fake && *fake) update_fake(fake);
    else check();
}

void update_fake(const char *tag) {
    lock_init();
    answer(tag);
}

bool update_poll(void) {
    char tag[TAG_MAX];
    if (!g_lock) return false;
    SDL_LockMutex(g_lock);
    bool got = g_answered;
    g_answered = false;
    snprintf(tag, sizeof(tag), "%s", g_tag);
    SDL_UnlockMutex(g_lock);
    if (!got) return false;
    if (update_newer(tag, HOTARU_VERSION)) {
        snprintf(g_latest, sizeof(g_latest), "%s", tag[0] == 'v' || tag[0] == 'V' ? tag + 1 : tag);
        log_event(LOG_INFO, "UPDATE", "Hotaru %s is out (this is %s)", g_latest, HOTARU_VERSION);
    } else {
        g_latest[0] = '\0';
        if (tag[0]) log_msg("update: the latest release is %s, this is %s", tag, HOTARU_VERSION);
    }
    return true;
}

const char *update_latest(void) { return g_latest; }
