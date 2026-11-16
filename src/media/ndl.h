/* media/ndl.h - hardware video/audio output through LG's NDL DirectMedia.
 *
 * On the TV (HOTARU_HAVE_NDL, set by CMakeLists.txt when the NDL headers are found in
 * the SDK sysroot; the library itself is opened at runtime) this feeds the stream's H.264/H.265 NAL units and
 * decoded PCM straight into the hardware decoder: NDL_DirectMediaInit/Load,
 * NDL_DirectVideoPlay / NDL_DirectAudioPlay, NDL_DirectMediaUnload. The library is
 * opened at runtime (dlopen) and its v2 entry points resolved by name: a webOS 4 TV
 * has only the v1 API, where a plain call to NDL_DirectMediaLoad() would abort the
 * process. ndl_available() is the runtime answer and ndl_unavailable_detail() the
 * reason. Elsewhere (host tests, simulator) every call is an honest stub and
 * ndl_available() returns false.
 *
 * The session drives this from libchiaki's threads: ndl_open() once before the
 * session starts, ndl_push_video()/ndl_push_audio() per frame, ndl_close() when
 * it ends. NDL draws on a hardware video plane, not into the LVGL framebuffer:
 * how that plane composes with the SDL app surface is unverified on this TV
 * (see docs/STATUS.md).
 */
#ifndef HOTARU_NDL_H
#define HOTARU_NDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"

bool ndl_available(void);                 /* true when the TV's libNDL_directmedia has the v2 API (webOS 5 and newer) */
const char *ndl_unavailable_detail(void); /* why not, for the log: the missing symbol or library */

bool ndl_open(const stream_settings_t *stream); /* init + load: codec, size, PCM 48 kHz stereo */
void ndl_close(void);                           /* unload + quit */

bool ndl_push_video(const uint8_t *nal, size_t len, int64_t pts_us);
bool ndl_push_audio(const uint8_t *pcm, size_t len, int64_t pts_us); /* S16LE stereo */

/* diagnostics, -1 when unknown: decoded frames waiting in NDL's render queue, and the PCM
 * NDL holds that is not played yet (call from the thread that feeds that stream) */
int ndl_video_buffered(void);
int ndl_audio_buffered_ms(void);

#endif
