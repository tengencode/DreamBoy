/* The divider and timer. gb_tick is one machine cycle (4 clock ticks); gb_bus_write ticks, then writes. */
#include "gbtestutil.h"
#include "test.h"

#define DIV 0xFF04
#define TIMA 0xFF05
#define TMA 0xFF06
#define TAC 0xFF07

static gb_t *machine(void) {
    static const uint8_t code[] = {0x18, 0xFE}; /* JR -2 */
    gb_t *g = gbtu_boot(code, 2);
    g->timer.div = 0;
    return g;
}

static void ticks(gb_t *g, int n) {
    for (int i = 0; i < n / 4; i++) gb_tick(g);
}

TEST(gb_div_counts) {
    gb_t *g = machine();
    ticks(g, 252);
    CHECK_EQ(gb_bus_peek(g, DIV), 0);
    ticks(g, 4);
    CHECK_EQ(gb_bus_peek(g, DIV), 1);
    ticks(g, 256 * 9);
    CHECK_EQ(gb_bus_peek(g, DIV), 10);
    gb_bus_write(g, DIV, 0x55); /* any write resets the whole counter */
    CHECK_EQ(gb_bus_peek(g, DIV), 0);
    CHECK_EQ(g->timer.div, 0);
    gb_destroy(g);
}

TEST(gb_div_after_boot) {
    static const uint8_t code[] = {0x00};
    gb_t *g = gbtu_boot(code, 1);
    CHECK_EQ(gb_bus_peek(g, DIV), 0xAB);
    gb_destroy(g);
}

TEST(gb_tima_rates) {
    static const int period[4] = {1024, 16, 64, 256};
    for (int rate = 0; rate < 4; rate++) {
        gb_t *g = machine();
        g->timer.tac = (uint8_t)(4 | rate);
        ticks(g, period[rate] - 4);
        CHECK_EQ(gb_bus_peek(g, TIMA), 0);
        ticks(g, 4);
        CHECK_EQ(gb_bus_peek(g, TIMA), 1);
        ticks(g, period[rate] * 5);
        CHECK_EQ(gb_bus_peek(g, TIMA), 6);
        g->timer.tac = (uint8_t)rate; /* off: it stops */
        ticks(g, period[rate] * 3);
        CHECK_EQ(gb_bus_peek(g, TIMA), 6);
        gb_destroy(g);
    }
    gb_t *g = machine();
    CHECK_EQ(gb_bus_peek(g, TAC), 0xF8); /* the unused bits read 1 */
    gb_destroy(g);
}

TEST(gb_tima_overflow_delay) {
    gb_t *g = machine();
    g->timer.tac = 5;
    g->timer.tima = 0xFF;
    g->timer.tma = 0x42;
    ticks(g, 16);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x00); /* overflowed: 0 for one cycle, no interrupt yet */
    CHECK_EQ(g->mem.iflag & 4, 0);
    ticks(g, 4);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x42);
    CHECK_EQ(g->mem.iflag & 4, 4);
    gb_destroy(g);

    g = machine(); /* a write to TIMA in the cycle it shows 0 cancels the reload and the interrupt */
    g->timer.tac = 5;
    g->timer.tima = 0xFF;
    g->timer.tma = 0x42;
    ticks(g, 12);
    gb_bus_write(g, TIMA, 0x10);
    ticks(g, 4);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x10);
    CHECK_EQ(g->mem.iflag & 4, 0);
    gb_destroy(g);

    g = machine(); /* a write in the cycle of the reload is lost; a write to TMA then also lands in TIMA */
    g->timer.tac = 5;
    g->timer.tima = 0xFF;
    g->timer.tma = 0x42;
    ticks(g, 16);
    gb_bus_write(g, TIMA, 0x10);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x42);
    CHECK_EQ(g->mem.iflag & 4, 4);
    gb_destroy(g);
    g = machine();
    g->timer.tac = 5;
    g->timer.tima = 0xFF;
    g->timer.tma = 0x42;
    ticks(g, 16);
    gb_bus_write(g, TMA, 0x77);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x77);
    gb_destroy(g);
}

TEST(gb_tima_div_write_edge) {
    gb_t *g = machine(); /* the timer counts falling edges of a divider bit: resetting DIV while it is 1 is one */
    g->timer.tac = 5;    /* bit 3 */
    ticks(g, 8);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0);
    gb_bus_write(g, DIV, 0);
    CHECK_EQ(gb_bus_peek(g, TIMA), 1);
    gb_bus_write(g, DIV, 0); /* bit 3 is 0 now: a reset does nothing */
    CHECK_EQ(gb_bus_peek(g, TIMA), 1);
    gb_destroy(g);

    g = machine(); /* switching the timer off while the bit is 1 is a falling edge too */
    g->timer.tac = 5;
    ticks(g, 8);
    gb_bus_write(g, TAC, 0x01);
    CHECK_EQ(gb_bus_peek(g, TIMA), 1);
    gb_destroy(g);

    g = machine(); /* and when that count wraps TIMA, TMA is loaded and the interrupt asked for at once */
    g->timer.tac = 5;
    g->timer.tima = 0xFF;
    g->timer.tma = 0x42;
    ticks(g, 8);
    gb_bus_write(g, TAC, 0x01);
    CHECK_EQ(gb_bus_peek(g, TIMA), 0x42);
    CHECK_EQ(g->mem.iflag & 4, 4);
    gb_destroy(g);
}
