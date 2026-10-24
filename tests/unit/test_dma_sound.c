#include "test.h"
#include "testutil.h"

/* Sound on, FIFO A at 100% on both sides, driven by timer 0; DMA1 reads EWRAM bytes 0, 1, 2, ... */
static gba_t *fifo_a_scene(uint16_t dma_control) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 64; i += 4) bus_write32(g, 0x02000000 + i, i | (i + 1) << 8 | (i + 2) << 16 | (i + 3) << 24, false);
    tu_io16(g, 0x084, 0x0080);
    tu_io16(g, 0x082, 0x0304);
    tu_io32(g, 0x0BC, 0x02000000); /* DMA1 source */
    tu_io32(g, 0x0C0, 0x040000A0); /* DMA1 destination: FIFO A */
    tu_io16(g, 0x0C6, dma_control);
    return g;
}

TEST(dma_sound_refills_four_words_when_the_fifo_runs_low) {
    gba_t *g = fifo_a_scene(0xB640); /* enable, sound timing, 32-bit, repeat, fixed destination */
    apu_on_timer_overflow(g, 0);      /* empty FIFO: 0 left → refill */
    CHECK_EQ(g->apu.fifo[0].count, 16);
    CHECK_EQ(g->dma.ch[1].src, 0x02000010);
    CHECK(bus_read16(g, 0x040000C6, false) & 0x8000); /* repeat keeps it enabled */
    apu_on_timer_overflow(g, 0);                      /* plays byte 0; 15 left → refill */
    CHECK_EQ(g->apu.fifo[0].sample, 0);
    CHECK_EQ(g->apu.fifo[0].count, 31);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 1);
    CHECK_EQ(g->apu.fifo[0].count, 30); /* 30 left: no refill */
    gba_destroy(g);
}

TEST(dma_sound_without_repeat_stops_after_one_refill) {
    gba_t *g = fifo_a_scene(0xB440);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].count, 16);
    CHECK_EQ(bus_read16(g, 0x040000C6, false) & 0x8000, 0);
    gba_destroy(g);
}

TEST(dma_sound_irq_and_destination_match) {
    gba_t *g = fifo_a_scene(0xF640); /* + IRQ */
    apu_on_timer_overflow(g, 0);
    CHECK(g->irq.if_ & IRQ_DMA1);
    gba_destroy(g);

    g = fifo_a_scene(0xB640);
    tu_io16(g, 0x0C6, 0x0000);
    tu_io32(g, 0x0C0, 0x040000A4); /* now pointed at FIFO B */
    tu_io16(g, 0x0C6, 0xB640);
    apu_on_timer_overflow(g, 0);   /* FIFO A asks: DMA1 does not answer */
    CHECK_EQ(g->apu.fifo[0].count, 0);
    gba_destroy(g);
}

TEST(dma_sound_plays_through_timer_0) {
    gba_t *g = fifo_a_scene(0xB640);
    tu_io16(g, 0x100, 0xFF00); /* overflow every 256 cycles */
    tu_io16(g, 0x102, 0x0080);
    tu_advance_to(g, g->timers.unit[0].start + 256 * 5);
    CHECK_EQ(g->apu.fifo[0].sample, 3); /* 1st overflow: empty + refill; then bytes 0, 1, 2, 3 */
    gba_destroy(g);
}
