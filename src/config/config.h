/* config/config.h - user settings, registered consoles, the PSN account and the device UID.
 *
 * Everything lives in memory and is persisted to one file by config_save().
 * The mutators below only change memory; callers save afterwards (app.c saves
 * after registration/forget and debounced after settings edits). The one
 * exception is config_device_uid(): it saves as soon as it generates the UID,
 * so the id stays stable even if the app is killed before the next save.
 *
 * Location: $CHIAKI_CONFIG_DIR/chiaki.conf when that variable is set (host
 * tests, simulator). Otherwise, on the TV, $HOME/conf/chiaki.conf: the jailer
 * sets HOME to the app's install directory, which is writable on webOS 4+ and
 * survives reboots (moonlight-tv keeps its pairing keys there the same way).
 * TODO(path): confirm on the TV whether an update over the same app id keeps
 * that directory; an uninstall certainly removes it. Elsewhere (macOS) the
 * default is $HOME/.chiaki-webos. If the directory cannot be created the file
 * goes to /tmp/chiaki-webos (cleared on boot) and a warning is logged.
 *
 * Format: UTF-8 text, one `key=value` per line, '#' starts a comment line.
 * The value is everything after the first '='; in strings `\\`, `\n` and `\r`
 * are escaped. Byte arrays are lower-case hex. Hosts are `host.<i>.<field>`
 * with i = 0..CONFIG_MAX_HOSTS-1. Unknown keys and invalid values are skipped,
 * so a missing, partial or corrupt file falls back to the defaults. Saving
 * writes a temp file (mode 0600: it holds keys and tokens) and renames it over
 * the old one, so a crash mid-write never leaves a truncated file.
 *
 *   version=1
 *   device_uid=0000000700410080<32 hex>
 *   stream.codec=1
 *   input.cursor_speed=100
 *   psn.account_id=<16 hex>
 *   host.0.nickname=PS5-123
 *   host.0.rp_key=<32 hex>
 */
#ifndef CHIAKI_CONFIG_H
#define CHIAKI_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_MAX_HOSTS 16
#define CONFIG_DUID_LEN  48   /* chiaki's DUID_PREFIX (16 hex) + 16 random bytes as hex */

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
    char addr[64];          /* last known IP or hostname; empty for a console registered over PSN */
    bool is_ps5;
    uint8_t server_mac[6];  /* identity of the console (all zero = unknown) */
    uint8_t rp_regist_key[16];
    uint8_t rp_key[16];
} config_host_t;

/* the linked PSN account (OAuth via QR + code, see chiaki/session.h) */
typedef struct {
    bool    linked;
    uint8_t account_id[8];        /* PSN account id */
    char    online_id[32];
    char    access_token[256];
    char    refresh_token[256];
} config_psn_t;

config_t *config_get(void);
const char *config_codec_name(video_codec_t codec);

bool config_load(void);          /* defaults, then the file; false if there was no readable file */
bool config_save(void);          /* atomic write; false (and logged) on any I/O error */
const char *config_path(void);   /* the file config_load()/config_save() use */

/* registered consoles, in registration order. A host is a duplicate of another
 * if they share a non-empty addr or a non-zero server_mac. */
int config_host_count(void);
const config_host_t *config_host(int index);              /* NULL if out of range */
int config_host_find(const config_host_t *host);           /* index of its duplicate, or -1 */
bool config_host_add(const config_host_t *host);           /* false when full, empty or a duplicate */
bool config_host_update(int index, const config_host_t *host);
bool config_host_remove(int index);

config_psn_t *config_psn(void);
void config_psn_set(const uint8_t account_id[8], const char *online_id,
                    const char *access_token, const char *refresh_token);
void config_psn_clear(void);

const char *config_device_uid(void);   /* stable device UID; generated (and saved) on first call */

#endif
