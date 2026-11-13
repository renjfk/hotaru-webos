/* media/feeder.h - a bounded copy queue with its own worker thread.
 *
 * libchiaki hands over video frames and audio packets on its network receive thread. If that
 * thread blocks (the hardware decoder taking its time, Opus decoding, NDL pacing the audio) the
 * stream's UDP socket overflows and the kernel drops the console's packets. A feeder copies each
 * buffer into a slot and returns at once; its thread then calls fn() for every buffer, in push
 * order. A full queue refuses the buffer instead of blocking the caller.
 *
 * feeder_push() may be called from any thread; fn() only ever runs on the feeder's thread.
 */
#ifndef CHIAKI_WEBOS_FEEDER_H
#define CHIAKI_WEBOS_FEEDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct feeder feeder_t;

/* runs on the feeder thread; data is only valid during the call; pushed_ms is the
 * SDL_GetTicks() of the push (the arrival time, for pacing) */
typedef void (*feeder_fn)(int tag, uint8_t *data, size_t len, uint32_t pushed_ms, void *user);

typedef struct {
    unsigned pushed;    /* buffers accepted */
    unsigned dropped;   /* buffers refused because the queue was full */
    unsigned depth_max; /* the most buffers that were waiting at once */
} feeder_stats_t;

/* NULL if the thread could not be started */
feeder_t *feeder_start(const char *name, unsigned slots, feeder_fn fn, void *user);
bool feeder_push(feeder_t *f, int tag, const void *data, size_t len); /* copies; false when full */
void feeder_stats(feeder_t *f, feeder_stats_t *out, bool reset);
unsigned feeder_slots(const feeder_t *f);
unsigned feeder_depth(feeder_t *f); /* buffers waiting, including the one fn() is handling */
void feeder_stop(feeder_t *f);      /* drops what is still queued, waits for fn() to return, frees */

#endif
