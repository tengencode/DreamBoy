#include "test.h"
#include "testutil.h"

static void dma_setup(gba_t *g, uint32_t n, uint32_t src, uint32_t dst, uint16_t count, uint16_t control) {
    uint32_t base = 0x0B0 + n * 12;
    tu_io32(g, base, src);
    tu_io32(g, base + 4, dst);
    tu_io16(g, base + 8, count);
    tu_io16(g, base + 10, control);
}

/* Immediate transfers take the bus 3 cycles after the enabling write. */
static void run_pending(gba_t *g) {
    tu_advance_to(g, g->now + 3);
}

TEST(dma_immediate_word_copy_then_disables) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 4; i++) bus_write32(g, 0x02000000 + i * 4, i + 1, false);
    dma_setup(g, 3, 0x02000000, 0x02000100, 4, 0x8400); /* enable, 32-bit, immediate */
    run_pending(g);
    for (uint32_t i = 0; i < 4; i++) CHECK_EQ(bus_read32(g, 0x02000100 + i * 4, false), i + 1);
    CHECK_EQ(bus_read16(g, 0x040000DE, false) & 0x8000, 0);
    gba_destroy(g);
}

TEST(dma_halfword_decrementing_source_fixed_destination) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x1111, false);
    bus_write16(g, 0x02000002, 0x2222, false);
    bus_write16(g, 0x02000004, 0x3333, false);
    dma_setup(g, 3, 0x02000004, 0x02000100, 3, 0x8000 | 0x0080 | 0x0040);
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x1111); /* the last value copied */
    CHECK_EQ(bus_read16(g, 0x02000102, false), 0);
    gba_destroy(g);
}

TEST(dma_count_zero_means_maximum) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x0201FFFE, 0xBEEF, false);
    dma_setup(g, 3, 0x02000000, 0x02020000, 0, 0x8000); /* DMA3: 0x10000 halfwords */
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x0203FFFE, false), 0xBEEF);
    gba_destroy(g);
}

TEST(dma_vblank_repeat_reloads_destination) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0xAAAA5555, false);
    /* VBlank, repeat, reload destination, fixed source (sources are never reloaded), 32-bit */
    dma_setup(g, 1, 0x02000000, 0x02000100, 1, 0x8000 | 0x1000 | 0x0200 | 0x0060 | 0x0100 | 0x0400);
    tu_advance_to(g, 100);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0xAAAA5555);
    bus_write32(g, 0x02000000, 0x12345678, false);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES + PPU_FRAME_CYCLES);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0x12345678);
    CHECK_EQ(bus_read32(g, 0x02000104, false), 0);
    CHECK(bus_read16(g, 0x040000C6, false) & 0x8000); /* still enabled */
    gba_destroy(g);
}

TEST(dma_hblank_runs_only_on_visible_lines) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x1234, false);
    dma_setup(g, 0, 0x02000000, 0x02001000, 1, 0x8000 | 0x2000 | 0x0200 | 0x0100); /* HBlank, repeat, fixed src */
    /* Stop inside VBlank: one big jump runs all transfers back to back, and their stall time
     * would otherwise carry the clock into the next frame's line 0. */
    tu_advance_to(g, 200 * PPU_LINE_CYCLES);
    CHECK_EQ(bus_read16(g, 0x02001000 + 159 * 2, false), 0x1234);
    CHECK_EQ(bus_read16(g, 0x02001000 + 160 * 2, false), 0);
    gba_destroy(g);
}

TEST(dma_end_of_transfer_irq) {
    gba_t *g = gba_create(NULL);
    dma_setup(g, 2, 0x02000000, 0x02000100, 1, 0xC000);
    run_pending(g);
    CHECK(g->irq.if_ & IRQ_DMA2);
    gba_destroy(g);
}

TEST(dma_unreadable_source_returns_the_last_value) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0xCAFEBABE, false);
    dma_setup(g, 3, 0x02000000, 0x02000100, 1, 0x8400);
    run_pending(g);
    dma_setup(g, 3, 0x00000000, 0x02000200, 1, 0x8400); /* BIOS region */
    run_pending(g);
    CHECK_EQ(bus_read32(g, 0x02000200, false), 0xCAFEBABE);
    gba_destroy(g);
}

TEST(dma_immediate_runs_before_the_cpu_moves_on) {
    /* strh r1, [r0] (enable); strh r2, [r0] (clear, as tonclib's next dma_cpy does); b . */
    const uint32_t ops[] = {0xE1C010B0, 0xE1C020B0, 0xEAFFFFFE};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    bus_write32(g, 0x02000000, 0x600DF00D, false);
    tu_io32(g, 0x0D4, 0x02000000);
    tu_io32(g, 0x0D8, 0x02000100);
    tu_io16(g, 0x0DC, 1);
    g->cpu.r[0] = 0x040000DE;
    g->cpu.r[1] = 0x8400; /* enable, 32-bit, immediate */
    g->cpu.r[2] = 0;
    gba_run_frame(g);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0x600DF00D);
    gba_destroy(g);
}

TEST(dma_aligns_save_area_addresses) {
    const char *const markers[] = {"SRAM_V"};
    gba_t *g = tu_rom_markers(markers, 1, 0x400);
    const char text[] = "Game";
    for (uint32_t i = 0; i < 4; i++) bus_write8(g, 0x0E000000 + i, (uint8_t)text[i], false);

    dma_setup(g, 3, 0x0E000001, 0x02000000, 1, 0x8000); /* 16-bit from SRAM + 1: reads SRAM + 0 */
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x02000000, false), 0x4747);

    bus_write32(g, 0x02000010, 0xD8D8D8D8, false);
    dma_setup(g, 3, 0x02000010, 0x0E000022, 1, 0x8400); /* 32-bit into SRAM + 0x22: writes SRAM + 0x20 */
    run_pending(g);
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0xD8);
    CHECK_EQ(bus_read8(g, 0x0E000022, false), 0xFF);
    gba_destroy(g);
}
