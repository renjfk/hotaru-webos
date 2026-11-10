/* config/config.h - user settings and registered consoles.
 *
 * Settings live in memory and the UI edits them directly (config_get()).
 * STUB: config_load()/config_save() do not persist yet. Roadmap: JSON in a
 * writable per-app directory (verify which path survives app updates on the
 * TV; the app's install dir is replaced on every install), plus the hosts
 * list filled by registration (session_register) instead of demo data.
 */
#ifndef CHIAKI_CONFIG_H
#define CHIAKI_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { CODEC_H264, CODEC_H265, CODEC_H265_HDR } video_codec_t;

typedef struct {
    video_codec_t codec;
    int width, height;    /* 1280x720 or 1920x1080 */
    int fps;              /* 30 or 60 */
    int bitrate_kbps;
    bool hdr;
} stream_settings_t;

typedef struct {
    bool rumble, touchpad, motion;
    int cursor_speed;     /* percent of the base stick-cursor speed */
} input_settings_t;

typedef struct {
    stream_settings_t stream;
    input_settings_t input;
} config_t;

/* a console paired via Remote Play registration (keys come from libchiaki) */
typedef struct {
    char nickname[64];
    char addr[64];          /* last known IP or hostname */
    bool is_ps5;
    uint8_t server_mac[6];
    uint8_t rp_regist_key[16];
    uint8_t rp_key[16];
} config_host_t;

config_t *config_get(void);
const char *config_codec_name(video_codec_t codec);

bool config_load(void);   /* STUB: keeps defaults, returns false */
bool config_save(void);   /* STUB: no-op, returns false */

int config_host_count(void);
const config_host_t *config_host(int index);
bool config_host_add(const config_host_t *host);   /* STUB: returns false */
bool config_host_remove(int index);                 /* STUB: returns false */

#endif
