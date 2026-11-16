/* platform/tv.c - the TV's identity, as webOS publishes it (see platform/tv.h). */
#include "tv.h"

#include <stdio.h>
#include <string.h>

#include "../log.h"

#define TV_INFO_DIR "/var/run/nyx"

/* the value of "key": "value" in one of webOS's small flat JSON files. A hand-rolled reader rather than a JSON
 * library: the app links none for this, and the two files hold one flat object each. */
static void json_string(const char *path, const char *key, char *out, size_t cap) {
    out[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return;
    p = strchr(p + strlen(pat), ':');
    if (!p) return;
    for (p++; *p == ' ' || *p == '\t'; p++) {}
    if (*p != '"') return;
    size_t o = 0;
    for (p++; *p && *p != '"' && *p != '\\'; p++)
        if (o + 1 < cap) out[o++] = *p;
    out[o] = '\0';
}

void tv_identity_read(const char *dir, tv_identity_t *out) {
    memset(out, 0, sizeof(*out));
    char path[256];
    snprintf(path, sizeof(path), "%s/device_info.json", dir);
    json_string(path, "product_id", out->model, sizeof(out->model));
    json_string(path, "board_type", out->board, sizeof(out->board));
    snprintf(path, sizeof(path), "%s/os_info.json", dir);
    json_string(path, "webos_name", out->os, sizeof(out->os));
    json_string(path, "webos_release", out->release, sizeof(out->release));
}

void tv_identity_log(void) {
    tv_identity_t id;
    tv_identity_read(TV_INFO_DIR, &id);
    if (!id.model[0] && !id.os[0] && !id.release[0]) {
        log_msg("tv: no identity in " TV_INFO_DIR " (model and webOS version unknown)");
        return;
    }
    char model[96], os[96];
    if (id.model[0] && id.board[0]) snprintf(model, sizeof(model), "%s (%s)", id.model, id.board);
    else snprintf(model, sizeof(model), "%s", id.model[0] ? id.model : "unknown model");
    if (id.os[0] && id.release[0]) snprintf(os, sizeof(os), "%s %s", id.os, id.release);
    else snprintf(os, sizeof(os), "%s", id.os[0] ? id.os : id.release[0] ? id.release : "unknown webOS");
    log_msg("tv: %s, %s", model, os);
}
