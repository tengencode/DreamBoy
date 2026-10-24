#ifndef GBA_SCHED_H
#define GBA_SCHED_H

#include <stdint.h>

#define SCHED_NEVER UINT64_MAX

/* Event kinds. At equal timestamps, lower IDs fire first. */
typedef enum {
    EV_HBLANK = 0, /* HDraw ends on the current line */
    EV_LINE_END,   /* HBlank ends; VCOUNT advances */
    EV_TIMER0,     /* timer n overflows (EV_TIMER0 + n) */
    EV_TIMER1,
    EV_TIMER2,
    EV_TIMER3,
    EV_DMA,        /* immediate DMA transfers start */
    EV_APU_FRAME,  /* 512 Hz sound frame-sequencer step */
    EV_APU_SAMPLE, /* one stereo output sample (every 512 cycles) */
    EV_SIO,        /* a link-port transfer finishes */
    EV_COUNT
} sched_event_t;

typedef struct {
    uint64_t when[EV_COUNT]; /* SCHED_NEVER = not scheduled */
    uint64_t next;           /* cached minimum of when[] */
} sched_t;

void sched_init(sched_t *s);
void sched_refresh(sched_t *s); /* works `next` out again from `when` (after a state load) */
void sched_add(sched_t *s, sched_event_t id, uint64_t when); /* (re)schedules id */
void sched_cancel(sched_t *s, sched_event_t id);
/* Removes the earliest event due at or before now; returns its ID (timestamp in *when_out), or -1. */
int sched_pop_due(sched_t *s, uint64_t now, uint64_t *when_out);

static inline uint64_t sched_next(const sched_t *s) {
    return s->next;
}

#endif
