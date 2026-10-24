#include "test.h"
#include "testutil.h"

TEST(frame_halted_cpu_sleeps_until_vblank) {
    const uint32_t ops[] = {0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.halted = true;
    gba_run_frame(g);
    CHECK_EQ(g->now, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 160);
    CHECK(g->irq.halted);
    gba_run_frame(g);
    CHECK_EQ(g->now, 160 * PPU_LINE_CYCLES + PPU_FRAME_CYCLES);
    gba_destroy(g);
}

TEST(frame_runs_cpu_between_events) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    gba_run_frame(g);
    CHECK(g->now >= 160 * PPU_LINE_CYCLES);
    CHECK(g->now < 160 * PPU_LINE_CYCLES + 16);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(frame_enabled_irq_wakes_halted_cpu) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.halted = true;
    g->irq.ie = IRQ_VBLANK;
    g->ppu.dispstat = DISPSTAT_VBLANK_IRQ;
    gba_run_frame(g); /* VBlank is flagged by the frame's last event */
    CHECK(g->irq.halted);
    gba_run_frame(g);
    CHECK(!g->irq.halted);
    gba_destroy(g);
}

TEST(gba_load_bios_selects_real_bios_boot) {
    static uint8_t bios[0x4000];
    static uint8_t rom[0x200];
    gba_t *g = gba_create(NULL);
    CHECK_EQ(gba_load_bios(g, bios, 100), GBA_ERR_BIOS_SIZE);
    bios[0] = 0x2A; /* mov r0, #42 */
    bios[1] = 0x00;
    bios[2] = 0xA0;
    bios[3] = 0xE3;
    CHECK_EQ(gba_load_bios(g, bios, sizeof bios), GBA_OK);
    CHECK_EQ(gba_load_rom(g, rom, sizeof rom), GBA_OK);
    CHECK_EQ(g->cpu.r[15], 0);
    CHECK_EQ(g->cpu.cpsr, MODE_SVC | CPSR_I | CPSR_F);
    CHECK_EQ(g->bus.bios[0], 0x2A);
    gba_destroy(g);
}
