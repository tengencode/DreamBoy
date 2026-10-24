#ifndef GBAEMU_AUDIO_RING_H
#define GBAEMU_AUDIO_RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RING_FRAMES 8192u /* stereo frames; a power of two */

/* Stereo int16 frames from one producer (the emulation thread) to one consumer (AAudio's callback),
 * without locks. The counters only grow (wrapping at 2^32); their difference is the fill. */
typedef struct {
    int16_t data[AUDIO_RING_FRAMES * 2]; /* interleaved left, right */
    atomic_uint write;                   /* frames ever pushed */
    atomic_uint read;                    /* frames ever popped */
} audio_ring_t;

void audio_ring_init(audio_ring_t *r);
/* Producer: stores up to n frames; returns how many fitted (the rest is dropped, nothing is overwritten). */
size_t audio_ring_push(audio_ring_t *r, const int16_t *frames, size_t n);
/* Consumer: copies up to n frames out; returns how many there were. */
size_t audio_ring_pop(audio_ring_t *r, int16_t *out, size_t n);
size_t audio_ring_queued(audio_ring_t *r);
/* Empties the ring. Only while the consumer is stopped (the stream is paused or closed). */
void audio_ring_clear(audio_ring_t *r);

#endif
