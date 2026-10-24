#include "gba_internal.h"
#include "test.h"

static void advance_to(gba_t *g, uint64_t t) {
    g->now = t;
    gba_run_events(g);
}

TEST(ppu_hblank_flag_and_irq) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_HBLANK_IRQ;
    advance_to(g, 1007);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    advance_to(g, 1008);
    CHECK(g->ppu.dispstat & DISPSTAT_HBLANK);
    CHECK(g->irq.if_ & IRQ_HBLANK);
    advance_to(g, 1232);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    CHECK_EQ(g->ppu.vcount, 1);
    gba_destroy(g);
}

TEST(ppu_vblank_spans_lines_160_to_226) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_VBLANK_IRQ;
    advance_to(g, 160 * PPU_LINE_CYCLES - 1);
    CHECK(!g->frame_done);
    advance_to(g, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 160);
    CHECK(g->ppu.dispstat & DISPSTAT_VBLANK);
    CHECK(g->irq.if_ & IRQ_VBLANK);
    CHECK(g->frame_done);
    advance_to(g, 227 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 227);
    CHECK(!(g->ppu.dispstat & DISPSTAT_VBLANK));
    advance_to(g, 228 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 0);
    gba_destroy(g);
}

TEST(ppu_vcount_match_flag_and_irq) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = (uint16_t)((5 << 8) | DISPSTAT_VCOUNT_IRQ);
    advance_to(g, 5 * PPU_LINE_CYCLES);
    CHECK(g->ppu.dispstat & DISPSTAT_VCOUNT);
    CHECK(g->irq.if_ & IRQ_VCOUNT);
    advance_to(g, 6 * PPU_LINE_CYCLES);
    CHECK(!(g->ppu.dispstat & DISPSTAT_VCOUNT));
    gba_destroy(g);
}
