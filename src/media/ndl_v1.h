/* media/ndl_v1.h - the v1 half of LG's NDL DirectMedia, for webOS 4 and older.
 *
 * The v1 API differs in shape, not only in names: video and audio are opened separately, NDL_DirectVideoPlay()
 * and NDL_DirectAudioPlay() take no presentation timestamp, and there is no render-queue query. It has no H.265
 * and no HDR: a v1 TV plays H.264 SDR (moonlight-tv streams webOS 4 through the same calls in ss4s's webos4
 * module, which this follows).
 *
 * It lives in a translation unit of its own because the SDK's two NDL headers cannot be included together: each
 * insists on its own NDL_DIRECTMEDIA_API_VERSION. That also keeps the v1 structs out of media/ndl.c, whose type
 * checks would otherwise be against the wrong layout.
 */
#ifndef HOTARU_NDL_V1_H
#define HOTARU_NDL_V1_H

#include <stdbool.h>

/* resolve the v1 entry points in the already-dlopened library; false when any of them is missing */
bool ndl_v1_load(void *lib);
bool ndl_v1_ready(void);

int ndl_v1_video_open(int width, int height);
void ndl_v1_video_close(void);
int ndl_v1_video_play(const void *buf, unsigned int size);
void ndl_v1_video_area(int left, int top, int width, int height);

int ndl_v1_audio_open(int channels, int hz);
void ndl_v1_audio_close(void);
int ndl_v1_audio_play(const void *buf, unsigned int size);

#endif
