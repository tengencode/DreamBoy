#ifndef GBAEMU_FRAME_PACER_H
#define GBAEMU_FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

/* One GBA frame = 280,896 cycles at 16,777,216 Hz = 16,742,706 ns + 5,013,504 / 2^24 ns. */
#define PACER_PERIOD_NS 16742706
#define PACER_FRAC_STEP 5013504u
#define PACER_FRAC_ONE (1u << 24)
#define PACER_MAX_LATE 3 /* frames behind before the pacer stops trying to catch up */

typedef struct {
    int64_t next_ns; /* the deadline handed out last */
    uint32_t frac;   /* sub-nanosecond remainder, in 1/2^24 ns */
    bool started;
} frame_pacer_t;

/* The next pacer_next call starts again from its 'now' (first frame, or after a pause). */
void pacer_reset(frame_pacer_t *p);
/* Called after each frame: the absolute time (ns) to sleep until before the next frame. */
int64_t pacer_next(frame_pacer_t *p, int64_t now_ns);

#endif
