/* media/ndl.h - hardware video/audio output through LG's NDL DirectMedia.
 *
 * STUB: nothing is decoded or shown yet. This is the next milestone: feed the
 * stream's H.264/H.265 NAL units and Opus audio into the TV's hardware
 * decoder (NDL_DirectMediaInit/Load, NDL_DirectVideoPlay/AudioPlay,
 * NDL_DirectMediaUnload) and measure latency.
 *
 * Known so far:
 * - The NDL headers and stub lib are NOT in this SDK's sysroot (checked), even
 *   though scripts/chiaki-lg-CMakeLists.txt assumes they are. chiaki-lg drives
 *   NDL through mariotaku/ss4s (dlopen'ed ss4s-ndl-webos4/5 modules); reuse
 *   that or vendor the NDL headers.
 * - NDL draws on a hardware video plane, not into the LVGL framebuffer: the
 *   UI must leave the video area transparent (or be hidden) while streaming.
 *   Unverified on this TV.
 */
#ifndef CHIAKI_NDL_H
#define CHIAKI_NDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/config.h"

bool ndl_available(void);                       /* false until NDL is wired */

bool ndl_open(const stream_settings_t *stream); /* init + load: codec, size, fps, Opus 48 kHz stereo */
void ndl_close(void);                            /* unload + quit */

bool ndl_push_video(const uint8_t *nal, size_t len, int64_t pts_us);
bool ndl_push_audio(const uint8_t *opus, size_t len, int64_t pts_us);

#endif
