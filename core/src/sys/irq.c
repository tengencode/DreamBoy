#include "sys/irq.h"

#include "gba_internal.h"

void irq_reset(gba_t *g) {
    g->irq.ie = 0;
    g->irq.if_ = 0;
    g->irq.ime = 0;
    g->irq.halted = false;
    g->irq.stopped = false;
    g->irq.pending = false;
    g->irq.ime_on = false;
    g->irq.visible_at = 0;
}

enum { IRQ_DELAY = 5 }; /* cycles from a request to the CPU seeing it (tuned on the mGBA suite, spec §8) */
enum { HALT_WAKE_DELAY = 3 }; /* a halted CPU wakes this much later still (tuned: count-up + SIO timing) */

/* A request is new when IE & IF becomes non-zero, or when IME switches on while one is pending. */
static void changed_at(gba_t *g, uint64_t when) {
    irq_t *q = &g->irq;
    bool pending = (q->ie & q->if_) != 0, ime_on = (q->ime & 1) != 0;
    if (pending && (!q->pending || (ime_on && !q->ime_on))) q->visible_at = when + IRQ_DELAY;
    q->pending = pending;
    q->ime_on = ime_on;
}

void irq_changed(gba_t *g) {
    changed_at(g, g->now);
}

void irq_raise(gba_t *g, uint16_t sources) {
    g->irq.if_ |= sources;
    irq_changed(g);
}

void irq_raise_at(gba_t *g, uint16_t sources, uint64_t when) {
    g->irq.if_ |= sources;
    changed_at(g, when);
}

void irq_halt(gba_t *g) {
    g->irq.halted = true;
}

void irq_stop(gba_t *g) {
    g->irq.stopped = true;
}

bool irq_stop_wake(const gba_t *g) {
    return (g->irq.ie & g->irq.if_ & (IRQ_KEYPAD | IRQ_SERIAL | IRQ_GAMEPAK)) != 0;
}

bool irq_line(const gba_t *g) {
    return (g->irq.ime & 1) && g->irq.pending && g->now >= g->irq.visible_at;
}

uint64_t irq_wake_at(const gba_t *g) {
    return g->irq.visible_at + HALT_WAKE_DELAY;
}

bool irq_wake(const gba_t *g) {
    return g->irq.pending && g->now >= irq_wake_at(g);
}
