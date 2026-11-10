/* config/config.c - in-memory settings; persistence is a stub (see config.h). */
#include "config.h"

#include <stddef.h>

static config_t g_config = {
    .stream = { .codec = CODEC_H265, .width = 1920, .height = 1080, .fps = 60, .bitrate_kbps = 15000, .hdr = false },
    .input = { .rumble = true, .touchpad = false, .motion = true, .cursor_speed = 100 },
};

config_t *config_get(void) { return &g_config; }

const char *config_codec_name(video_codec_t codec) {
    switch (codec) {
        case CODEC_H264: return "H.264";
        case CODEC_H265: return "H.265";
        case CODEC_H265_HDR: return "H.265 HDR";
    }
    return "?";
}

bool config_load(void) { return false; }
bool config_save(void) { return false; }

int config_host_count(void) { return 0; }
const config_host_t *config_host(int index) { (void)index; return NULL; }
bool config_host_add(const config_host_t *host) { (void)host; return false; }
bool config_host_remove(int index) { (void)index; return false; }
