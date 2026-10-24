#include "sys/timer.h"

#include <string.h>

#include "gba_internal.h"

static const unsigned prescale_shift[4] = {0, 6, 8, 10}; /* 1, 64, 256, 1024 cycles per tick */

/* Count-up (cascade) timers only move when the previous timer overflows. Timer 0 cannot cascade. */
static bool free_running(const timer_unit_t *t, int n) {
    return (t->control & 0x80) && !(n > 0 && (t->control & 0x04));
}

void timer_reset(gba_t *g) {
    memset(&g->timers, 0, sizeof g->timers);
}

uint16_t timer_read_counter(gba_t *g, int n) {
    const timer_unit_t *t = &g->timers.unit[n];
    if (!free_running(t, n) || g->now < t->start) return t->counter; /* not counting yet */
    uint64_t value = t->counter + ((g->now - t->start) >> prescale_shift[t->control & 3]);
    if (value > 0xFFFF) { /* overflowed within the current instruction; the event has not run yet */
        uint64_t period = 0x10000u - t->reload;
        value = t->reload + (value - 0x10000u) % period;
    }
    return (uint16_t)value;
}

static void schedule(gba_t *g, int n) {
    const timer_unit_t *t = &g->timers.unit[n];
    uint64_t ticks = 0x10000u - t->counter;
    sched_add(&g->sched, (sched_event_t)(EV_TIMER0 + n), t->start + (ticks << prescale_shift[t->control & 3]));
}

void timer_write_reload(gba_t *g, int n, uint16_t value) {
    g->timers.unit[n].reload = value; /* takes effect at the next start or overflow */
}

void timer_write_control(gba_t *g, int n, uint16_t value) {
    timer_unit_t *t = &g->timers.unit[n];
    t->counter = timer_read_counter(g, n); /* freeze the count before changing anything */
    bool starting = !(t->control & 0x80) && (value & 0x80);
    t->control = value & 0xC7;
    if (starting) t->counter = t->reload;
    t->start = g->now + (starting ? 2 : 0); /* a started timer counts from 2 cycles after the write */
    /* The prescaler is one free-running divider shared by all timers: a started timer ticks on its grid. */
    if (starting) t->start &= ~(((uint64_t)1 << prescale_shift[t->control & 3]) - 1);
    if (free_running(t, n)) schedule(g, n);
    else sched_cancel(&g->sched, (sched_event_t)(EV_TIMER0 + n));
}

/* Reload, flag the IRQ, and tick a cascaded next timer (which may overflow in turn). */
static void overflow(gba_t *g, int n, uint64_t when) {
    timer_unit_t *t = &g->timers.unit[n];
    t->counter = t->reload;
    t->start = when;
    if (t->control & 0x40) irq_raise_at(g, (uint16_t)(IRQ_TIMER0 << n), when);
    if (n < 2) apu_on_timer_overflow(g, n);
    if (free_running(t, n)) schedule(g, n);
    if (n < 3) {
        timer_unit_t *next = &g->timers.unit[n + 1];
        if ((next->control & 0x84) == 0x84 && ++next->counter == 0) overflow(g, n + 1, when);
    }
}

void timer_on_overflow(gba_t *g, int n, uint64_t when) {
    overflow(g, n, when);
}
