/* config/config.c - in-memory settings + the key=value file behind them (see config.h). */
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../log.h"

#define FILE_VERSION 1
#define FILE_NAME    "hotaru.conf"
#define FALLBACK_DIR "/tmp/hotaru"      /* writable everywhere, cleared on boot */
#define DUID_PREFIX  "0000000700410080" /* chiaki-ng lib/include/chiaki/remote/holepunch.h */
#define LINE_MAX_LEN 1024               /* longest valid line: an escaped 255-char token + key */

#define DEFAULT_CONFIG                                                                                                 \
    .stream = { .codec = CODEC_H265, .width = 1920, .height = 1080, .fps = 60, .bitrate_kbps = 15000, .hdr = false },  \
    .input = { .rumble = true, .touchpad = true }, .session = { .sleep_on_disconnect = false }

static const config_t g_defaults = { DEFAULT_CONFIG };
static config_t g_config = { DEFAULT_CONFIG };
static config_host_t g_hosts[CONFIG_MAX_HOSTS];
static int g_host_count;
static config_psn_t g_psn[CONFIG_MAX_ACCOUNTS];
static int g_psn_count;
static char g_duid[CONFIG_DUID_LEN + 1];
static char g_dir[512], g_path[600];

config_t *config_get(void) { return &g_config; }

const char *config_codec_name(video_codec_t codec) {
    switch (codec) {
        case CODEC_H264: return "H.264";
        case CODEC_H265: return "H.265";
        case CODEC_H265_HDR: return "H.265 HDR";
    }
    return "?";
}

/* ---------- location ---------- */

static bool mkdir_p(const char *dir) {
    char tmp[sizeof(g_dir)];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(tmp, 0700); /* EEXIST and friends: the final stat() decides */
        *p = '/';
    }
    mkdir(tmp, 0700);
    struct stat st;
    return stat(tmp, &st) == 0 && S_ISDIR(st.st_mode) && access(tmp, W_OK) == 0;
}

static void resolve_path(void) {
    if (g_path[0]) return;
    const char *env = getenv("HOTARU_CONFIG_DIR"), *home = getenv("HOME");
    if (env && *env) snprintf(g_dir, sizeof(g_dir), "%s", env);
#ifdef __linux__
    else if (home && *home) snprintf(g_dir, sizeof(g_dir), "%s/conf", home); /* the TV: HOME = app dir */
#else
    else if (home && *home) snprintf(g_dir, sizeof(g_dir), "%s/.hotaru", home);
#endif
    if (!g_dir[0] || !mkdir_p(g_dir)) {
        if (g_dir[0]) log_event(LOG_WARN, "CONFIG", "%s is not writable, settings last until reboot", g_dir);
        snprintf(g_dir, sizeof(g_dir), "%s", FALLBACK_DIR);
        mkdir_p(g_dir);
    }
    snprintf(g_path, sizeof(g_path), "%s/" FILE_NAME, g_dir);
}

const char *config_path(void) {
    resolve_path();
    return g_path;
}

/* ---------- writing ---------- */

static void put_str(FILE *f, const char *key, const char *v) {
    fprintf(f, "%s=", key);
    for (; *v; v++) {
        if (*v == '\\') fputs("\\\\", f);
        else if (*v == '\n') fputs("\\n", f);
        else if (*v == '\r') fputs("\\r", f);
        else fputc(*v, f);
    }
    fputc('\n', f);
}

static void put_hex(FILE *f, const char *key, const uint8_t *b, size_t n) {
    fprintf(f, "%s=", key);
    for (size_t i = 0; i < n; i++) fprintf(f, "%02x", b[i]);
    fputc('\n', f);
}

static void put_int(FILE *f, const char *key, int v) { fprintf(f, "%s=%d\n", key, v); }

static void write_all(FILE *f) {
    const config_t *c = &g_config;
    fputs("# Hotaru settings, written by the app (format: see src/config/config.h)\n", f);
    put_int(f, "version", FILE_VERSION);
    if (g_duid[0]) put_str(f, "device_uid", g_duid);
    put_int(f, "stream.codec", (int)c->stream.codec);
    put_int(f, "stream.width", c->stream.width);
    put_int(f, "stream.height", c->stream.height);
    put_int(f, "stream.fps", c->stream.fps);
    put_int(f, "stream.bitrate_kbps", c->stream.bitrate_kbps);
    put_int(f, "stream.hdr", c->stream.hdr);
    put_int(f, "stream.custom", c->stream.custom);
    put_int(f, "stream.smooth_ms", c->stream.smooth_ms);
    put_int(f, "input.rumble", c->input.rumble);
    put_int(f, "input.touch", c->input.touchpad);
    put_int(f, "session.sleep_on_disconnect", c->session.sleep_on_disconnect);
    put_int(f, "debug.probe", c->debug.probe);
    for (int i = 0; i < g_psn_count; i++) {
        const config_psn_t *p = &g_psn[i];
        char key[48];
#define PKEY(field) (snprintf(key, sizeof(key), "psn.%d." field, i), key)
        put_int(f, PKEY("linked"), p->linked ? 1 : 0);
        put_hex(f, PKEY("account_id"), p->account_id, sizeof(p->account_id));
        put_str(f, PKEY("online_id"), p->online_id);
        put_str(f, PKEY("access_token"), p->access_token);
        put_str(f, PKEY("refresh_token"), p->refresh_token);
        if (p->expires_at) fprintf(f, "%s=%lld\n", PKEY("expires_at"), p->expires_at);
#undef PKEY
    }
    for (int i = 0; i < g_host_count; i++) {
        const config_host_t *h = &g_hosts[i];
        char key[48];
#define KEY(field) (snprintf(key, sizeof(key), "host.%d." field, i), key)
        put_str(f, KEY("nickname"), h->nickname);
        put_str(f, KEY("addr"), h->addr);
        put_int(f, KEY("is_ps5"), h->is_ps5);
        put_hex(f, KEY("server_mac"), h->server_mac, sizeof(h->server_mac));
        put_hex(f, KEY("rp_regist_key"), h->rp_regist_key, sizeof(h->rp_regist_key));
        put_hex(f, KEY("rp_key"), h->rp_key, sizeof(h->rp_key));
        put_int(f, KEY("via_psn"), h->via_psn);
        put_hex(f, KEY("psn_uid"), h->psn_uid, sizeof(h->psn_uid));
        put_hex(f, KEY("psn_account_id"), h->psn_account_id, sizeof(h->psn_account_id));
        if (h->prefer_psn) put_int(f, KEY("prefer_psn"), 1);
#undef KEY
    }
}

bool config_save(void) {
    resolve_path();
    char tmp[sizeof(g_path) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600); /* keys and tokens: owner only */
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!f) {
        log_event(LOG_ERR, "CONFIG", "cannot write %s: %s", tmp, strerror(errno));
        if (fd >= 0) close(fd);
        return false;
    }
    write_all(f);
    bool ok = !ferror(f) && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(tmp, g_path) != 0) {
        log_event(LOG_ERR, "CONFIG", "saving %s failed: %s", g_path, strerror(errno));
        unlink(tmp);
        return false;
    }
    return true;
}

/* ---------- reading ---------- */

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool get_hex(const char *v, uint8_t *out, size_t n) {
    if (strlen(v) != n * 2) return false;
    uint8_t tmp[32];
    if (n > sizeof(tmp)) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(v[2 * i]), lo = hex_val(v[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        tmp[i] = (uint8_t)(hi << 4 | lo);
    }
    memcpy(out, tmp, n);
    return true;
}

static bool get_int(const char *v, int min, int max, int *out) {
    char *end;
    errno = 0;
    long n = strtol(v, &end, 10);
    if (errno || end == v || *end || n < min || n > max) return false;
    *out = (int)n;
    return true;
}

static bool get_bool(const char *v, bool *out) {
    int n;
    if (!get_int(v, 0, 1, &n)) return false;
    *out = n != 0;
    return true;
}

static bool get_str(const char *v, char *out, size_t cap) {
    char tmp[LINE_MAX_LEN];
    size_t n = 0;
    for (; *v; v++) {
        char c = *v;
        if (c == '\\') {
            v++;
            if (*v == '\\') c = '\\';
            else if (*v == 'n') c = '\n';
            else if (*v == 'r') c = '\r';
            else return false;
        }
        if (n + 1 >= cap || n + 1 >= sizeof(tmp)) return false;
        tmp[n++] = c;
    }
    tmp[n] = '\0';
    memcpy(out, tmp, n + 1);
    return true;
}

static void apply_host(const char *k, const char *v, config_host_t *hosts, bool *seen) {
    char *end;
    long i = strtol(k, &end, 10);
    if (end == k || *end != '.' || i < 0 || i >= CONFIG_MAX_HOSTS) return;
    const char *field = end + 1;
    config_host_t *h = &hosts[i];
    bool ok = false;
    if (!strcmp(field, "nickname")) ok = get_str(v, h->nickname, sizeof(h->nickname));
    else if (!strcmp(field, "addr")) ok = get_str(v, h->addr, sizeof(h->addr));
    else if (!strcmp(field, "is_ps5")) ok = get_bool(v, &h->is_ps5);
    else if (!strcmp(field, "server_mac")) ok = get_hex(v, h->server_mac, sizeof(h->server_mac));
    else if (!strcmp(field, "rp_regist_key")) ok = get_hex(v, h->rp_regist_key, sizeof(h->rp_regist_key));
    else if (!strcmp(field, "rp_key")) ok = get_hex(v, h->rp_key, sizeof(h->rp_key));
    else if (!strcmp(field, "via_psn")) ok = get_bool(v, &h->via_psn);
    else if (!strcmp(field, "psn_uid")) ok = get_hex(v, h->psn_uid, sizeof(h->psn_uid));
    else if (!strcmp(field, "psn_account_id")) ok = get_hex(v, h->psn_account_id, sizeof(h->psn_account_id));
    else if (!strcmp(field, "prefer_psn")) ok = get_bool(v, &h->prefer_psn);
    if (ok) seen[i] = true;
}

/* invalid values keep what is there (the defaults); unknown keys are ignored */
/* accounts read while loading, and the one-key-per-field psn block of an older file */
static config_psn_t g_rd_accounts[CONFIG_MAX_ACCOUNTS];
static bool g_rd_seen[CONFIG_MAX_ACCOUNTS];
static config_psn_t g_rd_legacy;
static bool g_rd_legacy_seen;

static void apply_psn(const char *k, const char *v) {
    /* psn.<i>.<field> now; a file from before multiple accounts has psn.<field> */
    const char *field = k;
    int i = -1;
    char *end;
    long n = strtol(k, &end, 10);
    if (end != k && *end == '.' && n >= 0 && n < CONFIG_MAX_ACCOUNTS) {
        i = (int)n;
        field = end + 1;
    }
    config_psn_t *p = i >= 0 ? &g_rd_accounts[i] : &g_rd_legacy;
    bool *read = i >= 0 ? &g_rd_seen[i] : &g_rd_legacy_seen;
    bool ok = false;
    if (!strcmp(field, "linked")) ok = get_bool(v, &p->linked);
    else if (!strcmp(field, "account_id")) ok = get_hex(v, p->account_id, sizeof(p->account_id));
    else if (!strcmp(field, "online_id")) ok = get_str(v, p->online_id, sizeof(p->online_id));
    else if (!strcmp(field, "access_token")) ok = get_str(v, p->access_token, sizeof(p->access_token));
    else if (!strcmp(field, "refresh_token")) ok = get_str(v, p->refresh_token, sizeof(p->refresh_token));
    else if (!strcmp(field, "expires_at")) {
        char *e;
        errno = 0;
        long long t = strtoll(v, &e, 10);
        if (!errno && e != v && !*e && t >= 0) {
            p->expires_at = t;
            ok = true;
        }
    }
    if (ok && read) *read = true;
}

static void apply(const char *k, const char *v, config_host_t *hosts, bool *seen) {
    stream_settings_t *s = &g_config.stream;
    input_settings_t *in = &g_config.input;
    int n;
    if (!strncmp(k, "host.", 5)) apply_host(k + 5, v, hosts, seen);
    else if (!strcmp(k, "version")) {
        if (get_int(v, 0, 1000, &n) && n != FILE_VERSION) log_msg("config: file version %d, reading what we know", n);
    } else if (!strcmp(k, "device_uid")) {
        if (strlen(v) == CONFIG_DUID_LEN && strspn(v, "0123456789abcdefABCDEF") == CONFIG_DUID_LEN)
            memcpy(g_duid, v, CONFIG_DUID_LEN + 1);
    } else if (!strcmp(k, "stream.codec")) {
        if (get_int(v, CODEC_H264, CODEC_H265_HDR, &n)) s->codec = (video_codec_t)n;
    } else if (!strcmp(k, "stream.width")) get_int(v, 1, 7680, &s->width);
    else if (!strcmp(k, "stream.height")) get_int(v, 1, 4320, &s->height);
    else if (!strcmp(k, "stream.fps")) get_int(v, 30, 60, &s->fps);
    else if (!strcmp(k, "stream.bitrate_kbps")) get_int(v, 2000, 50000, &s->bitrate_kbps);
    else if (!strcmp(k, "stream.hdr")) get_bool(v, &s->hdr);
    else if (!strcmp(k, "stream.custom")) get_bool(v, &s->custom);
    else if (!strcmp(k, "stream.smooth_ms")) get_int(v, 0, 300, &s->smooth_ms);
    else if (!strcmp(k, "input.rumble")) get_bool(v, &in->rumble);
    else if (!strcmp(k, "input.touch")) get_bool(v, &in->touchpad);
    else if (!strcmp(k, "session.sleep_on_disconnect")) get_bool(v, &g_config.session.sleep_on_disconnect);
    else if (!strcmp(k, "debug.probe")) get_bool(v, &g_config.debug.probe);
    else if (!strncmp(k, "psn.", 4)) apply_psn(k + 4, v);
}

/* only the combinations Settings > Stream offers */
static void validate(void) {
    stream_settings_t *s = &g_config.stream;
    bool res_ok = (s->width == 1280 && s->height == 720) || (s->width == 1920 && s->height == 1080);
    if (!res_ok) {
        s->width = g_defaults.stream.width;
        s->height = g_defaults.stream.height;
    }
    if (s->fps != 30 && s->fps != 60) s->fps = g_defaults.stream.fps;
}

static void reset(void) {
    g_config = g_defaults;
    memset(g_hosts, 0, sizeof(g_hosts));
    g_host_count = 0;
    memset(g_psn, 0, sizeof(g_psn));
    g_psn_count = 0;
    g_duid[0] = '\0';
}

bool config_load(void) {
    reset();
    resolve_path();
    FILE *f = fopen(g_path, "r");
    if (!f) {
        log_msg("config: no file at %s, using defaults", g_path);
        return false;
    }
    static config_host_t hosts[CONFIG_MAX_HOSTS];
    bool seen[CONFIG_MAX_HOSTS] = { false };
    memset(hosts, 0, sizeof(hosts));
    memset(g_rd_accounts, 0, sizeof(g_rd_accounts));
    memset(g_rd_seen, 0, sizeof(g_rd_seen));
    memset(&g_rd_legacy, 0, sizeof(g_rd_legacy));
    g_rd_legacy_seen = false;
    char line[LINE_MAX_LEN];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') { /* too long: skip the rest, ignore it */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = '\0';
        apply(line, eq + 1, hosts, seen);
    }
    fclose(f);
    validate();
    /* accounts: the indexed block, else the single account an older file stored */
    for (int i = 0; i < CONFIG_MAX_ACCOUNTS; i++) {
        config_psn_t *p = &g_rd_accounts[i];
        bool id = false;
        for (int b = 0; b < 8; b++)
            if (p->account_id[b]) id = true;
        if (!g_rd_seen[i] || !id) continue; /* an account needs its id: it names the pairings */
        p->linked = p->linked || p->access_token[0] || p->refresh_token[0];
        g_psn[g_psn_count++] = *p;
    }
    bool legacy = false;
    for (int b = 0; b < 8; b++)
        if (g_rd_legacy.account_id[b]) legacy = true;
    if (!g_psn_count && g_rd_legacy_seen && legacy) {
        g_rd_legacy.linked = g_rd_legacy.linked || g_rd_legacy.access_token[0] || g_rd_legacy.refresh_token[0];
        g_psn[g_psn_count++] = g_rd_legacy;
        for (int i = 0; i < CONFIG_MAX_HOSTS; i++) { /* what was paired before belonged to that one account */
            bool paired = false;
            for (int b = 0; b < 8; b++)
                if (hosts[i].psn_account_id[b]) paired = true;
            if (seen[i] && !paired)
                memcpy(hosts[i].psn_account_id, g_rd_legacy.account_id, sizeof(hosts[i].psn_account_id));
        }
    }
    for (int i = 0; i < CONFIG_MAX_HOSTS; i++)
        if (seen[i]) config_host_add(&hosts[i]); /* drops entries without an identity and duplicates */
    log_msg("config: loaded %s (%d console(s), %d PSN account(s))", g_path, g_host_count, g_psn_count);
    return true;
}

/* ---------- consoles ---------- */

static bool mac_set(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++)
        if (mac[i]) return true;
    return false;
}

static bool uid_set(const uint8_t uid[32]) {
    for (int i = 0; i < 32; i++)
        if (uid[i]) return true;
    return false;
}

static bool same_console(const config_host_t *a, const config_host_t *b) {
    /* a pairing belongs to a PSN account: the same console under two accounts is two consoles */
    if (memcmp(a->psn_account_id, b->psn_account_id, sizeof(a->psn_account_id)) != 0) return false;
    /* PSN uid, then MAC when both are known; the address is only a fallback (DHCP reassigns it,
     * and a PSN console has no address at all) */
    if (uid_set(a->psn_uid) && uid_set(b->psn_uid)) return !memcmp(a->psn_uid, b->psn_uid, sizeof(a->psn_uid));
    if (mac_set(a->server_mac) && mac_set(b->server_mac))
        return !memcmp(a->server_mac, b->server_mac, sizeof(a->server_mac));
    return a->addr[0] && !strcmp(a->addr, b->addr);
}

int config_host_count(void) { return g_host_count; }
const config_host_t *config_host(int index) { return index >= 0 && index < g_host_count ? &g_hosts[index] : NULL; }

int config_host_find(const config_host_t *host) {
    for (int i = 0; host && i < g_host_count; i++)
        if (same_console(host, &g_hosts[i])) return i;
    return -1;
}

bool config_host_add(const config_host_t *host) {
    if (!host || g_host_count >= CONFIG_MAX_HOSTS) return false;
    if (!host->addr[0] && !mac_set(host->server_mac) && !uid_set(host->psn_uid)) return false;
    if (config_host_find(host) >= 0) return false;
    g_hosts[g_host_count] = *host;
    g_hosts[g_host_count].nickname[sizeof(host->nickname) - 1] = '\0';
    g_hosts[g_host_count].addr[sizeof(host->addr) - 1] = '\0';
    g_host_count++;
    return true;
}

bool config_host_update(int index, const config_host_t *host) {
    if (!host || !config_host(index)) return false;
    for (int i = 0; i < g_host_count; i++)
        if (i != index && same_console(host, &g_hosts[i])) return false;
    g_hosts[index] = *host;
    g_hosts[index].nickname[sizeof(host->nickname) - 1] = '\0';
    g_hosts[index].addr[sizeof(host->addr) - 1] = '\0';
    return true;
}

bool config_host_remove(int index) {
    if (!config_host(index)) return false;
    memmove(&g_hosts[index], &g_hosts[index + 1], sizeof(g_hosts[0]) * (size_t)(g_host_count - index - 1));
    g_host_count--;
    memset(&g_hosts[g_host_count], 0, sizeof(g_hosts[0]));
    return true;
}

/* ---------- PSN accounts ---------- */

int config_psn_count(void) { return g_psn_count; }

const config_psn_t *config_psn_at(int index) { return index >= 0 && index < g_psn_count ? &g_psn[index] : NULL; }

config_psn_t *config_psn_edit(int index) { return index >= 0 && index < g_psn_count ? &g_psn[index] : NULL; }

static bool id_set(const uint8_t id[8]) {
    for (int i = 0; i < 8; i++)
        if (id[i]) return true;
    return false;
}

int config_psn_find(const uint8_t account_id[8]) {
    for (int i = 0; account_id && i < g_psn_count; i++)
        if (!memcmp(g_psn[i].account_id, account_id, sizeof(g_psn[i].account_id))) return i;
    return -1;
}

int config_psn_add(const config_psn_t *account) {
    if (!account || !id_set(account->account_id)) return -1;
    int i = config_psn_find(account->account_id);
    if (i < 0) {
        if (g_psn_count >= CONFIG_MAX_ACCOUNTS) return -1;
        i = g_psn_count++;
        memset(&g_psn[i], 0, sizeof(g_psn[i]));
        memcpy(g_psn[i].account_id, account->account_id, sizeof(g_psn[i].account_id));
    }
    config_psn_t *p = &g_psn[i];
    p->linked = account->linked || account->access_token[0] || account->refresh_token[0];
    if (account->online_id[0]) snprintf(p->online_id, sizeof(p->online_id), "%s", account->online_id);
    if (account->access_token[0]) snprintf(p->access_token, sizeof(p->access_token), "%s", account->access_token);
    if (account->refresh_token[0]) snprintf(p->refresh_token, sizeof(p->refresh_token), "%s", account->refresh_token);
    if (account->expires_at) p->expires_at = account->expires_at;
    return i;
}

int config_psn_add_id(const uint8_t account_id[8]) {
    if (!id_set(account_id)) return -1;
    int i = config_psn_find(account_id);
    if (i >= 0) return i; /* the id alone never replaces a signed-in account's tokens */
    config_psn_t acc = { 0 };
    memcpy(acc.account_id, account_id, sizeof(acc.account_id));
    snprintf(acc.online_id, sizeof(acc.online_id), "PSN %02X%02X", account_id[0], account_id[1]); /* a label */
    return config_psn_add(&acc);
}

void config_psn_set_tokens(int index, const char *access_token, const char *refresh_token, long long expires_at) {
    config_psn_t *p = config_psn_edit(index);
    if (!p) return;
    if (access_token) snprintf(p->access_token, sizeof(p->access_token), "%s", access_token);
    if (refresh_token) snprintf(p->refresh_token, sizeof(p->refresh_token), "%s", refresh_token);
    p->expires_at = expires_at;
    p->linked = p->access_token[0] || p->refresh_token[0];
}

void config_psn_remove(int index) {
    const config_psn_t *p = config_psn_at(index);
    if (!p) return;
    uint8_t id[8];
    memcpy(id, p->account_id, sizeof(id));
    memmove(&g_psn[index], &g_psn[index + 1], sizeof(g_psn[0]) * (size_t)(g_psn_count - index - 1));
    g_psn_count--;
    memset(&g_psn[g_psn_count], 0, sizeof(g_psn[0]));
    /* its PSN-only consoles leave with it; a PIN pairing keeps its keys and loses the PSN route */
    for (int i = g_host_count - 1; i >= 0; i--) {
        config_host_t *h = &g_hosts[i];
        if (memcmp(h->psn_account_id, id, sizeof(id)) != 0) continue;
        if (h->via_psn && !h->addr[0]) {
            config_host_remove(i);
            continue;
        }
        /* the pairing keeps its account stamp: signing in again rejoins it, and a re-pair updates it */
        h->via_psn = false;
        h->prefer_psn = false;
    }
}

/* ---------- device UID ---------- */

static void random_bytes(uint8_t *b, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f ? fread(b, 1, n, f) : 0;
    if (f) fclose(f);
    if (got == n) return;
    /* no urandom: not cryptographic, but the UID only has to be unique per TV */
    uint32_t s = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16) ^ (uint32_t)clock();
    for (size_t i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        b[i] = (uint8_t)(s >> 16);
    }
}

/* the format of chiaki_holepunch_generate_client_device_uid(): DUID_PREFIX + 16 random bytes as hex */
const char *config_device_uid(void) {
    if (g_duid[0]) return g_duid;
    uint8_t r[16];
    random_bytes(r, sizeof(r));
    char *p = g_duid + snprintf(g_duid, sizeof(g_duid), "%s", DUID_PREFIX);
    for (size_t i = 0; i < sizeof(r); i++) p += snprintf(p, 3, "%02x", r[i]);
    log_msg("config: generated a device uid");
    config_save();
    return g_duid;
}
