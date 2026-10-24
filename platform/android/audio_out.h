#ifndef GBAEMU_AUDIO_OUT_H
#define GBAEMU_AUDIO_OUT_H

#include <stdatomic.h>
#include <stdbool.h>

#include "audio_ring.h"

/* Counters the AAudio callback keeps (whole session, across reopens). */
typedef struct {
    atomic_llong played; /* frames handed to AAudio */
    atomic_llong silent; /* of those, filled with silence because the ring ran short */
    atomic_llong loud;   /* of those, frames with any non-zero sample */
} audio_stats_t;

/* One AAudio output stream (shared, low latency, stereo int16, the device's own rate) that plays
 * from `ring`. All calls except the callbacks come from the emulation thread. */
typedef struct audio_out audio_out_t;

/* NULL if AAudio cannot open. bursts: AAudio's own buffer in bursts (2 = the least delay; 4, 8 = fewer crackles). */
audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats, int bursts);
bool audio_out_start(audio_out_t *a);
void audio_out_pause(audio_out_t *a); /* returns once the callback has stopped (or after 200 ms) */
bool audio_out_disconnected(audio_out_t *a); /* the error callback reported a lost device */
int audio_out_rate(const audio_out_t *a);
int audio_out_burst(const audio_out_t *a);
void audio_out_close(audio_out_t *a);

#endif
