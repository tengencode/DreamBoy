/* The divider (a 16-bit counter of clock ticks; DIV is its high byte) and the timer, which counts the falling
 * edges of one divider bit — so resetting the divider or switching the timer off can count once too. */
#include "gb_internal.h"

static bool timer_bit(const gb_timer_t *t) {
    static const uint8_t bit[4] = {9, 3, 5, 7}; /* 1024, 16, 64, 256 ticks per count */
    return (t->tac & 4) && (t->div >> bit[t->tac & 3] & 1);
}

static void count(gb_timer_t *t) {
    if (++t->tima == 0) t->overflow = 1; /* TIMA shows 0 for one cycle before TMA is loaded */
}

/* The divider goes from `old` to its new value: the timer and the serial clock watch its bits fall. */
static void divider_changed(gb_t *g, uint16_t old, bool timer_was_high) {
    if (timer_was_high && !timer_bit(&g->timer)) count(&g->timer);
    /* The serial clock (8192 Hz) is one cycle ahead of the divider's bit 8 falling, as seen from the CPU (the
     * mooneye suite's boot_sclk_align test). */
    if ((uint16_t)(old + 4) & ~(uint16_t)(g->timer.div + 4) & 0x100) gb_serial_clock(g);
    if (old & ~g->timer.div & (g->cgb.double_speed ? 0x2000 : 0x1000)) gb_apu_sequencer(g);
}

void gb_timer_tick(gb_t *g) {
    gb_timer_t *t = &g->timer;
    if (!(t->tac & 4) && !t->overflow && !t->reloading && (t->div & 0xFC) != 0xF8 && (t->div & 0xFC) != 0xFC) {
        t->div += 4; /* the timer is off and no divider bit from 8 up changes: nothing else can happen */
        return;
    }
    t->reloading = 0;
    if (t->overflow) {
        t->overflow = 0;
        t->tima = t->tma;
        t->reloading = 1;
        g->mem.iflag |= 0x04;
    }
    uint16_t old = t->div;
    bool high = timer_bit(t);
    t->div += 4;
    divider_changed(g, old, high);
}

uint8_t gb_timer_read(const gb_t *g, uint16_t addr) {
    const gb_timer_t *t = &g->timer;
    switch (addr) {
    case 0xFF04: return (uint8_t)(t->div >> 8);
    case 0xFF05: return t->tima;
    case 0xFF06: return t->tma;
    default: return t->tac | 0xF8;
    }
}

void gb_timer_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_timer_t *t = &g->timer;
    uint16_t old = t->div;
    bool high = timer_bit(t);
    switch (addr) {
    case 0xFF04:
        t->div = 0;
        divider_changed(g, old, high);
        break;
    case 0xFF05:
        if (!t->reloading) { /* in the cycle TMA is loaded, the write is lost */
            t->tima = value;
            t->overflow = 0;
        }
        break;
    case 0xFF06:
        t->tma = value;
        if (t->reloading) t->tima = value;
        break;
    default:
        t->tac = value & 7;
        divider_changed(g, old, high);
        if (t->overflow) { /* a wrap caused by this write loads TMA and asks for the interrupt in this same
                              cycle, not the next (what the mooneye suite's rapid_toggle test measures) */
            t->overflow = 0;
            t->tima = t->tma;
            g->mem.iflag |= 0x04;
        }
        break;
    }
}
