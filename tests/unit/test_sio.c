#include "test.h"
#include "testutil.h"

/* The suite's procedure: set RCNT and SIOCNT (the mode), write a value, read it back. */
static uint16_t write_read(gba_t *g, uint16_t rcnt, uint16_t siocnt, uint32_t off, uint16_t value) {
    tu_io16(g, 0x134, rcnt);
    tu_io16(g, 0x128, siocnt);
    tu_io16(g, off, value);
    uint16_t result = (uint16_t)bus_read16(g, 0x04000000 + off, false);
    if (off != 0x134) tu_io16(g, off, 0);
    tu_io16(g, 0x128, 0);
    tu_io16(g, 0x134, 0x8000);
    return result;
}

TEST(sio_register_rules_per_mode) {
    static const struct {
        uint16_t rcnt, siocnt;
        uint32_t off;
        uint16_t value, expected;
    } rows[] = {
        /* multiplayer */
        {0x0000, 0x2000, 0x120, 0xFFFF, 0x0000}, {0x0000, 0x2000, 0x128, 0xEFFF, 0x6F8F},
        {0x0000, 0x2000, 0x12A, 0xFFFF, 0xFFFF}, {0x0000, 0x2000, 0x134, 0x3FFF, 0x01FF},
        {0x0000, 0x2000, 0x140, 0xFFFF, 0x0040},
        /* normal 8-bit */
        {0x0000, 0x0000, 0x120, 0xFFFF, 0x0000}, {0x0000, 0x0000, 0x128, 0xCFFF, 0x4F8F},
        {0x0000, 0x0000, 0x134, 0x3FFF, 0x01F5},
        /* normal 32-bit */
        {0x0000, 0x1000, 0x120, 0xFFFF, 0xFFFF}, {0x0000, 0x1000, 0x122, 0xFFFF, 0xFFFF},
        {0x0000, 0x1000, 0x124, 0xFFFF, 0x0000}, {0x0000, 0x1000, 0x128, 0xDFFF, 0x5F8F},
        /* UART */
        {0x0000, 0x3000, 0x128, 0xFFFF, 0x7FAF}, {0x0000, 0x3000, 0x12A, 0xFFFF, 0x0000},
        {0x0000, 0x3000, 0x134, 0x3FFF, 0x01FF},
        /* general purpose */
        {0x8000, 0x0000, 0x128, 0xCFFF, 0x4F8F}, {0x8000, 0x0000, 0x134, 0xBFFF, 0x81FF},
        /* JOY BUS */
        {0xC000, 0x0000, 0x134, 0xFFFF, 0xC1FC}, {0xC000, 0x0000, 0x150, 0xFFFF, 0x0000},
        {0xC000, 0x0000, 0x154, 0xFFFF, 0x0000}, {0xC000, 0x0000, 0x15A, 0xFFFF, 0x0000},
    };
    gba_t *g = gba_create(NULL);
    for (size_t i = 0; i < TU_LEN(rows); i++) {
        uint16_t got = write_read(g, rows[i].rcnt, rows[i].siocnt, rows[i].off, rows[i].value);
        if (got != rows[i].expected) fprintf(stderr, "  row %zu (%03X): %04X != %04X\n", i, (unsigned)rows[i].off, got, rows[i].expected);
        CHECK_EQ(got, rows[i].expected);
    }
    gba_destroy(g);
}

TEST(sio_normal8_internal_clock_finishes_after_8_bits) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_SERIAL;
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x4083); /* IRQ, start, 2 MHz, internal clock, normal 8-bit */
    uint64_t start = g->now; /* after the write: it costs a cycle */
    tu_advance_to(g, start + 63);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_advance_to(g, start + 64); /* 8 bits x 8 cycles */
    CHECK(!(bus_read16(g, 0x04000128, false) & 0x80));
    CHECK_EQ(bus_read16(g, 0x0400012A, false) & 0xFF, 0xFF); /* nothing attached: all 1s arrive */
    CHECK(g->irq.if_ & IRQ_SERIAL);
    gba_destroy(g);
}

TEST(sio_normal32_256k_takes_2048_cycles) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x1081); /* start, 256 kHz, internal clock, normal 32-bit, no IRQ */
    uint64_t start = g->now; /* after the write: it costs a cycle */
    tu_advance_to(g, start + 2047);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_advance_to(g, start + 2048);
    CHECK(!(bus_read16(g, 0x04000128, false) & 0x80));
    CHECK_EQ(bus_read32(g, 0x04000120, false), 0xFFFFFFFF);
    CHECK(!(g->irq.if_ & IRQ_SERIAL));
    gba_destroy(g);
}

TEST(sio_external_clock_and_multiplayer_never_finish) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x0080); /* normal 8-bit, external clock: nobody drives it */
    tu_advance_to(g, g->now + 1000000);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_io16(g, 0x128, 0x2083); /* multiplayer: we are a child, the parent never starts */
    tu_advance_to(g, g->now + 1000000);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    gba_destroy(g);
}

TEST(sio_clearing_start_cancels_a_transfer) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_SERIAL;
    tu_io16(g, 0x134, 0x0000);
    uint64_t start = g->now;
    tu_io16(g, 0x128, 0x4081);
    tu_io16(g, 0x128, 0x4001);
    tu_advance_to(g, start + 4096);
    CHECK(!(g->irq.if_ & IRQ_SERIAL));
    gba_destroy(g);
}
