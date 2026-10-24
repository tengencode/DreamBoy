#ifndef GBA_TIMER_H
#define GBA_TIMER_H

#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    uint16_t reload;
    uint16_t counter; /* the count at `start` while running; the frozen count otherwise */
    uint16_t control; /* TMxCNT_H */
    uint64_t start;
} timer_unit_t;

typedef struct {
    timer_unit_t unit[4];
} timers_t;

void timer_reset(gba_t *g);
uint16_t timer_read_counter(gba_t *g, int n);
void timer_write_reload(gba_t *g, int n, uint16_t value);
void timer_write_control(gba_t *g, int n, uint16_t value);
void timer_on_overflow(gba_t *g, int n, uint64_t when);

#endif
