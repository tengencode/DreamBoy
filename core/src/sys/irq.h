#ifndef GBA_IRQ_H
#define GBA_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

enum {
    IRQ_VBLANK = 1 << 0,
    IRQ_HBLANK = 1 << 1,
    IRQ_VCOUNT = 1 << 2,
    IRQ_TIMER0 = 1 << 3,
    IRQ_TIMER1 = 1 << 4,
    IRQ_TIMER2 = 1 << 5,
    IRQ_TIMER3 = 1 << 6,
    IRQ_SERIAL = 1 << 7,
    IRQ_DMA0 = 1 << 8,
    IRQ_DMA1 = 1 << 9,
    IRQ_DMA2 = 1 << 10,
    IRQ_DMA3 = 1 << 11,
    IRQ_KEYPAD = 1 << 12,
    IRQ_GAMEPAK = 1 << 13,
};

typedef struct {
    uint16_t ie, if_, ime;
    bool halted;
    bool stopped;
    bool pending;        /* IE & IF */
    bool ime_on;         /* IME as last seen by irq_changed */
    uint64_t visible_at; /* the CPU sees a new request from this cycle on */
} irq_t;

void irq_reset(gba_t *g);
void irq_raise(gba_t *g, uint16_t sources);
/* Like irq_raise, for event handlers: the request counts from the event's own time. */
void irq_raise_at(gba_t *g, uint16_t sources, uint64_t when);
/* IE, IF or IME changed: a new request reaches the CPU a few cycles later. */
void irq_changed(gba_t *g);
void irq_halt(gba_t *g);
void irq_stop(gba_t *g); /* Stop: the whole console freezes until a keypad/serial/cartridge IRQ */
/* A stopped console wakes when a keypad, serial or cartridge interrupt is flagged and enabled. */
bool irq_stop_wake(const gba_t *g);
/* The CPU should take an IRQ (before considering the CPSR I bit). */
bool irq_line(const gba_t *g);
/* A halted CPU wakes when any enabled source is flagged, even with IME off, a few cycles after a running
 * CPU would see the request. */
bool irq_wake(const gba_t *g);
/* When a halted CPU wakes for the current request (valid while IE & IF is non-zero). */
uint64_t irq_wake_at(const gba_t *g);

#endif
