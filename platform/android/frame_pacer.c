#include "frame_pacer.h"

void pacer_reset(frame_pacer_t *p) {
    p->started = false;
}

int64_t pacer_next(frame_pacer_t *p, int64_t now_ns) {
    if (!p->started || now_ns - p->next_ns > (int64_t)PACER_MAX_LATE * PACER_PERIOD_NS) {
        p->next_ns = now_ns + PACER_PERIOD_NS; /* start over: no burst of catch-up frames */
        p->frac = PACER_FRAC_STEP; /* this first period's sub-nanosecond part */
        p->started = true;
        return p->next_ns;
    }
    p->next_ns += PACER_PERIOD_NS;
    p->frac += PACER_FRAC_STEP;
    if (p->frac >= PACER_FRAC_ONE) {
        p->frac -= PACER_FRAC_ONE;
        p->next_ns += 1;
    }
    return p->next_ns;
}
