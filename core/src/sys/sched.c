#include "sys/sched.h"

static void recompute(sched_t *s) {
    uint64_t min = SCHED_NEVER;
    for (int i = 0; i < EV_COUNT; i++) {
        if (s->when[i] < min) min = s->when[i];
    }
    s->next = min;
}

void sched_refresh(sched_t *s) {
    recompute(s);
}

void sched_init(sched_t *s) {
    for (int i = 0; i < EV_COUNT; i++) s->when[i] = SCHED_NEVER;
    s->next = SCHED_NEVER;
}

void sched_add(sched_t *s, sched_event_t id, uint64_t when) {
    s->when[id] = when;
    recompute(s);
}

void sched_cancel(sched_t *s, sched_event_t id) {
    s->when[id] = SCHED_NEVER;
    recompute(s);
}

int sched_pop_due(sched_t *s, uint64_t now, uint64_t *when_out) {
    if (s->next > now) return -1;
    int best = -1;
    for (int i = 0; i < EV_COUNT; i++) {
        if (s->when[i] <= now && (best < 0 || s->when[i] < s->when[best])) best = i;
    }
    *when_out = s->when[best];
    s->when[best] = SCHED_NEVER;
    recompute(s);
    return best;
}
