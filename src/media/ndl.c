/* media/ndl.c - STUB until NDL DirectMedia is wired (see ndl.h). */
#include "ndl.h"

bool ndl_available(void) { return false; }

bool ndl_open(const stream_settings_t *stream) { (void)stream; return false; }
void ndl_close(void) {}

bool ndl_push_video(const uint8_t *nal, size_t len, int64_t pts_us) {
    (void)nal;
    (void)len;
    (void)pts_us;
    return false;
}
bool ndl_push_audio(const uint8_t *opus, size_t len, int64_t pts_us) {
    (void)opus;
    (void)len;
    (void)pts_us;
    return false;
}
