/* media/ndl_v1.c - the v1 NDL calls, for a webOS 4 TV (see media/ndl_v1.h). */
#include "ndl_v1.h"

#ifdef HOTARU_HAVE_NDL

#include <dlfcn.h>
#include <string.h>

#define NDL_DIRECTMEDIA_API_VERSION 1
#include <NDL_directmedia_v1.h>

#include "../log.h"

static struct {
    bool tried, ok;
    int (*video_open)(NDL_DIRECTVIDEO_DATA_INFO_T *info);
    int (*video_close)(void);
    int (*video_play)(void *buf, unsigned int size);
    int (*video_area)(int left, int top, int width, int height);
    int (*audio_open)(NDL_DIRECTAUDIO_DATA_INFO_T *info);
    int (*audio_close)(void);
    int (*audio_play)(void *buf, unsigned int size);
} g;

#define RESOLVE(field, name)                                                                                           \
    do {                                                                                                               \
        *(void **)(&g.field) = dlsym(lib, name);                                                                       \
        if (!g.field) {                                                                                                \
            log_msg("ndl v1: the library has no %s", name);                                                            \
            memset(&g, 0, sizeof(g));                                                                                  \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

bool ndl_v1_load(void *lib) {
    if (g.tried) return g.ok;
    g.tried = true;
    RESOLVE(video_open, "NDL_DirectVideoOpen");
    RESOLVE(video_close, "NDL_DirectVideoClose");
    RESOLVE(video_play, "NDL_DirectVideoPlay");
    RESOLVE(video_area, "NDL_DirectVideoSetArea"); /* in NDL_directmedia_common.h: both APIs have it */
    RESOLVE(audio_open, "NDL_DirectAudioOpen");
    RESOLVE(audio_close, "NDL_DirectAudioClose");
    RESOLVE(audio_play, "NDL_DirectAudioPlay");
    g.ok = true;
    return true;
}

bool ndl_v1_ready(void) { return g.ok; }

int ndl_v1_video_open(int width, int height) {
    NDL_DIRECTVIDEO_DATA_INFO_T info = { .width = width, .height = height, .source = NDL_DIRECTVIDEO_SRC_TYPE_H264 };
    return g.video_open(&info);
}

void ndl_v1_video_close(void) { g.video_close(); }

int ndl_v1_video_play(const void *buf, unsigned int size) { return g.video_play((void *)buf, size); }

void ndl_v1_video_area(int left, int top, int width, int height) { g.video_area(left, top, width, height); }

int ndl_v1_audio_open(int channels, int hz) {
    /* ss4s's webOS 4 audio setup: 16-bit PCM straight into the mix channel, with NDL's own small delay window
     * (48 ms above, 16 ms below) instead of the default one. */
    NDL_DIRECTAUDIO_DATA_INFO_T info = { .number_of_channel = (unsigned int)channels,
                                         .bit_per_sample = 16,
                                         .no_delay_mode = NDL_DIRECTAUDIO_NODELAY_MODE_CUSTOM,
                                         .no_delay_upper_time = 48,
                                         .no_delay_lower_time = 16,
                                         .channel = NDL_DIRECTAUDIO_CH_MAIN,
                                         .source = NDL_DIRECTAUDIO_SRC_TYPE_PCM,
                                         .frequency = NDL_DIRECTAUDIO_SAMPLING_FREQ_OF(hz) };
    return g.audio_open(&info);
}

void ndl_v1_audio_close(void) { g.audio_close(); }

int ndl_v1_audio_play(const void *buf, unsigned int size) { return g.audio_play((void *)buf, size); }

#else /* !HOTARU_HAVE_NDL: host tests and the simulator */

bool ndl_v1_load(void *lib) {
    (void)lib;
    return false;
}
bool ndl_v1_ready(void) { return false; }
int ndl_v1_video_open(int width, int height) {
    (void)width;
    (void)height;
    return -1;
}
void ndl_v1_video_close(void) {}
int ndl_v1_video_play(const void *buf, unsigned int size) {
    (void)buf;
    (void)size;
    return -1;
}
void ndl_v1_video_area(int left, int top, int width, int height) {
    (void)left;
    (void)top;
    (void)width;
    (void)height;
}
int ndl_v1_audio_open(int channels, int hz) {
    (void)channels;
    (void)hz;
    return -1;
}
void ndl_v1_audio_close(void) {}
int ndl_v1_audio_play(const void *buf, unsigned int size) {
    (void)buf;
    (void)size;
    return -1;
}

#endif /* HOTARU_HAVE_NDL */
