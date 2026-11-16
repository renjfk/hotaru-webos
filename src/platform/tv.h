/* platform/tv.h - the TV's identity, as webOS publishes it.
 *
 * A log that names the TV is what turns "it does not work" into a version to check: webOS 4 and older have only
 * the v1 media API (media/ndl.h), and the model decides which firmware line a report belongs to. The values come
 * from the two files webOS keeps for this, /var/run/nyx/device_info.json (product_id, board_type) and
 * /var/run/nyx/os_info.json (webos_name, webos_release); both are world readable. Missing files or keys leave
 * the fields empty: this never fails, it only describes.
 */
#ifndef HOTARU_TV_H
#define HOTARU_TV_H

#include <stddef.h>

typedef struct {
    char model[40];   /* the product code, e.g. "50UP75003LF" */
    char board[40];   /* the board, e.g. "LM21A_DVB" */
    char os[40];      /* e.g. "webOS TV Lite" */
    char release[16]; /* e.g. "6.5.3" */
} tv_identity_t;

/* read `dir`'s device_info.json and os_info.json into out (empty fields when absent) */
void tv_identity_read(const char *dir, tv_identity_t *out);

/* one log line naming the TV: "tv: 50UP75003LF (LM21A_DVB), webOS TV Lite 6.5.3" */
void tv_identity_log(void);

#endif
