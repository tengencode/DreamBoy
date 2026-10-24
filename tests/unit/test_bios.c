#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static uint32_t bios_word(gba_t *g, uint32_t addr) {
    const uint8_t *p = &g->bus.bios[addr];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void run_until_pc(gba_t *g, uint32_t pc, int max_steps) {
    for (int i = 0; i < max_steps && g->cpu.r[15] != pc; i++) cpu_step(g);
}

TEST(bios_stub_matches_real_bios_at_observable_addresses) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bios_word(g, 0x0E4), 0xE129F000); /* msr cpsr_fc, r0 */
    CHECK_EQ(bios_word(g, 0x128), 0xE92D500F); /* stmfd sp!, {r0-r3, r12, lr} */
    CHECK_EQ(bios_word(g, 0x134), 0xE510F004); /* ldr pc, [r0, #-4] */
    CHECK_EQ(bios_word(g, 0x13C), 0xE25EF004); /* subs pc, lr, #4 */
    CHECK_EQ(bios_word(g, 0x144), 0xE55EC002); /* ldrb r12, [lr, #-2] */
    CHECK_EQ(bios_word(g, 0x188), 0xE1B0F00E); /* movs pc, lr */
    CHECK_EQ(bios_word(g, 0x190), 0xE3A02004); /* mov r2, #4 */
    gba_destroy(g);
}

TEST(bios_read_after_boot_returns_boot_latch) {
    const uint32_t ops[] = {
        0xE3A00000, /* mov r0, #0 */
        0xE5901000, /* ldr r1, [r0] */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0xE129F000);
    gba_destroy(g);
}

TEST(bios_arm_swi_div_round_trip) {
    const uint32_t ops[] = {
        0xE3A00064, /* mov r0, #100 */
        0xE3A01007, /* mov r1, #7 */
        0xEF060000, /* swi 0x60000 (Div) */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    run_until_pc(g, 0x0800000C, 500);
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK_EQ(g->cpu.r[1], 2);
    CHECK_EQ(g->cpu.r[3], 14);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->bus.bios_latch, 0xE3A02004); /* last BIOS fetch: 0x188 + 8 */
    gba_destroy(g);
}

TEST(bios_thumb_swi_round_trip) {
    const uint16_t ops[] = {
        0x2064, /* movs r0, #100 */
        0x2107, /* movs r1, #7 */
        0xDF06, /* swi 6 (Div) */
        0xE7FE, /* b . */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    run_until_pc(g, 0x08000006, 500);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK(g->cpu.cpsr & CPSR_T);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    gba_destroy(g);
}

TEST(bios_vblank_intr_wait_runs_the_game_isr) {
    const uint32_t ops[] = {
        0xE3A00301, /* mov  r0, #0x04000000 */
        0xE3A01008, /* mov  r1, #8 */
        0xE1C010B4, /* strh r1, [r0, #4]      DISPSTAT: VBlank IRQ enable */
        0xE3A01001, /* mov  r1, #1 */
        0xE2802C02, /* add  r2, r0, #0x200 */
        0xE1C210B0, /* strh r1, [r2]          IE = VBlank */
        0xE5821008, /* str  r1, [r2, #8]      IME = 1 */
        0xE28F3010, /* adr  r3, isr */
        0xE2404004, /* sub  r4, r0, #4        0x03FFFFFC mirrors 0x03007FFC */
        0xE5843000, /* str  r3, [r4]          IRQ handler pointer */
        0xEF050000, /* swi  0x50000           VBlankIntrWait */
        0xE3A05001, /* mov  r5, #1 */
        0xEAFFFFFE, /* b    . */
        /* isr: */
        0xE3A00301, /* mov  r0, #0x04000000 */
        0xE2800C02, /* add  r0, r0, #0x200 */
        0xE1D010B2, /* ldrh r1, [r0, #2]      IF */
        0xE1C010B2, /* strh r1, [r0, #2]      acknowledge */
        0xE2402F82, /* sub  r2, r0, #0x208    0x03FFFFF8 mirrors BIOS_IF */
        0xE1D230B0, /* ldrh r3, [r2] */
        0xE1833001, /* orr  r3, r3, r1 */
        0xE1C230B0, /* strh r3, [r2] */
        0xE12FFF1E, /* bx   lr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    gba_run_frame(g); /* the program halts in VBlankIntrWait; VBlank is flagged at frame end */
    gba_run_frame(g); /* wake, run the ISR, return from the SWI */
    CHECK_EQ(g->cpu.r[5], 1);
    CHECK_EQ(g->cpu.r[15], 0x08000030);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    /* Not checking IF: the VBlank that ends frame 2 has just been flagged again. */
    CHECK_EQ(g->bus.iwram[0x7FF8], 0); /* IntrWait clears the flag it waited for */
    gba_destroy(g);
}

TEST(bios_soft_reset) {
    const uint32_t ops[] = {0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->bus.iwram[0x7E00] = 0x55;
    g->bus.iwram[0x7FFA] = 0; /* return to ROM */
    g->cpu.r[0] = 9;
    cpu_write_cpsr(&g->cpu, MODE_SVC);
    hle_swi(g, 0x00);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS);
    CHECK_EQ(g->cpu.r[0], 0);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->cpu.bank_r13[BANK_IRQ], 0x03007FA0);
    CHECK_EQ(g->bus.iwram[0x7E00], 0);

    g->bus.iwram[0x7FFA] = 1; /* return to EWRAM */
    hle_swi(g, 0x00);
    CHECK_EQ(g->cpu.r[15], 0x02000000);
    gba_destroy(g);
}

TEST(bios_register_ram_reset) {
    gba_t *g = gba_create(NULL);
    memset(g->bus.ewram, 0xFF, EWRAM_SIZE);
    memset(g->bus.iwram, 0xFF, IWRAM_SIZE);
    g->cpu.r[0] = 0x03; /* EWRAM and IWRAM */
    hle_swi(g, 0x01);
    CHECK_EQ(g->bus.ewram[0], 0);
    CHECK_EQ(g->bus.ewram[EWRAM_SIZE - 1], 0);
    CHECK_EQ(g->bus.iwram[0x7DFF], 0);
    CHECK_EQ(g->bus.iwram[0x7E00], 0xFF); /* the BIOS work area survives */
    CHECK_EQ(g->ppu.dispcnt, 0x0080);
    gba_destroy(g);
}

TEST(bios_halt_and_checksum) {
    gba_t *g = gba_create(NULL);
    hle_swi(g, 0x02);
    CHECK(g->irq.halted);
    hle_swi(g, 0x0D);
    CHECK_EQ(g->cpu.r[0], 0xBAAE187F);
    gba_destroy(g);
}

TEST(bios_trap_outside_the_bios_is_undefined) {
    const uint32_t ops[] = {0xE7F000F0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_UND);
    gba_destroy(g);
}
