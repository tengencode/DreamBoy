#include "test.h"
#include "testutil.h"

TEST(timer_counts_at_each_prescaler) {
    static const unsigned shift[4] = {0, 6, 8, 10};
    for (uint16_t p = 0; p < 4; p++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, 0x100, 0);
        tu_io16(g, 0x102, (uint16_t)(0x80 | p));
        g->now = g->timers.unit[0].start + ((uint64_t)5 << shift[p]);
        CHECK_EQ(timer_read_counter(g, 0), 5);
        gba_destroy(g);
    }
}

TEST(timer_io_read_returns_the_live_counter) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x102, 0x83); /* prescaler 1024 */
    g->now = g->timers.unit[0].start + 3 * 1024;
    CHECK_EQ(bus_read16(g, 0x04000100, false), 3);
    gba_destroy(g);
}

TEST(timer_overflow_reloads_and_raises_irq) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0xFFF0);
    tu_io16(g, 0x102, 0xC0); /* enable + IRQ */
    uint64_t start = g->timers.unit[0].start;
    tu_advance_to(g, start + 15);
    CHECK(!(g->irq.if_ & IRQ_TIMER0));
    CHECK_EQ(timer_read_counter(g, 0), 0xFFFF);
    tu_advance_to(g, start + 16);
    CHECK(g->irq.if_ & IRQ_TIMER0);
    CHECK_EQ(timer_read_counter(g, 0), 0xFFF0);
    tu_advance_to(g, start + 32 + 3);
    CHECK_EQ(timer_read_counter(g, 0), 0xFFF3);
    gba_destroy(g);
}

TEST(timer_reload_write_does_not_touch_a_running_count) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0x10);
    tu_io16(g, 0x102, 0x80);
    uint64_t start = g->timers.unit[0].start;
    tu_io16(g, 0x100, 0x20);
    g->now = start + 4;
    CHECK_EQ(timer_read_counter(g, 0), 0x14);
    gba_destroy(g);
}

TEST(timer_cascade_counts_overflows) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x104, 0);
    tu_io16(g, 0x106, 0x84);    /* timer 1: count-up */
    tu_io16(g, 0x100, 0xFFFF);
    tu_io16(g, 0x102, 0x80);    /* timer 0 overflows every cycle */
    tu_advance_to(g, g->timers.unit[0].start + 5);
    CHECK_EQ(timer_read_counter(g, 1), 5);
    gba_destroy(g);
}

TEST(timer_stop_freezes_the_counter) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x102, 0x80);
    g->now = g->timers.unit[0].start + 7;
    tu_io16(g, 0x102, 0x00); /* the write lands one cycle later */
    g->now += 100;
    CHECK_EQ(timer_read_counter(g, 0), 8);
    CHECK_EQ(g->sched.when[EV_TIMER0], SCHED_NEVER);
    gba_destroy(g);
}

/* A byte write to the reload value keeps the reload's other byte, not the live counter's. */
TEST(timer_reload_byte_writes_keep_the_other_byte) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x102, 0x80); /* running, so the counter moves away from the reload */
    g->now = g->timers.unit[0].start + 0x99;
    bus_write8(g, 0x04000100, 0x34, false);
    bus_write8(g, 0x04000101, 0x12, false);
    CHECK_EQ(g->timers.unit[0].reload, 0x1234);
    gba_destroy(g);
}
