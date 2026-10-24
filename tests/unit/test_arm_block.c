#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(arm_stm_ldm_with_writeback) {
    const uint32_t ops[] = {
        0xE8A0000E, /* stmia r0!, {r1-r3} */
        0xE9300070, /* ldmdb r0!, {r4-r6} */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x100;
    g->cpu.r[1] = 1;
    g->cpu.r[2] = 2;
    g->cpu.r[3] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 0x10C);
    CHECK_EQ(bus_read32(g, IW + 0x108, false), 3);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 0x100);
    CHECK_EQ(g->cpu.r[4], 1);
    CHECK_EQ(g->cpu.r[5], 2);
    CHECK_EQ(g->cpu.r[6], 3);
    gba_destroy(g);
}

TEST(arm_stmfd_ldmfd_stack) {
    const uint32_t ops[] = {
        0xE92D4001, /* stmfd sp!, {r0, lr} */
        0xE8BD0006, /* ldmfd sp!, {r1, r2} */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 7;
    g->cpu.r[14] = 9;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->cpu.r[1], 7);
    CHECK_EQ(g->cpu.r[2], 9);
    gba_destroy(g);
}

TEST(arm_ldm_loads_pc) {
    const uint32_t ops[] = {0xE8908000}; /* ldmia r0, {pc} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x200;
    bus_write32(g, IW + 0x200, 0x08000040, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000040);
    gba_destroy(g);
}

TEST(arm_empty_rlist_store_writes_pc_and_moves_base_by_64) {
    const uint32_t ops[] = {0xE8A00000}; /* stmia r0!, {} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x300;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x300, false), 0x0800000C);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}

TEST(arm_empty_rlist_load_branches) {
    const uint32_t ops[] = {0xE8B00000}; /* ldmia r0!, {} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x300;
    bus_write32(g, IW + 0x300, 0x08000080, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000080);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}

TEST(arm_stm_base_in_list) {
    const uint32_t not_first[] = {0xE8A1000F}; /* stmia r1!, {r0-r3} */
    gba_t *g = tu_arm(not_first, TU_LEN(not_first));
    g->cpu.r[1] = IW + 0x400;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x404, false), IW + 0x410); /* the written-back base */
    gba_destroy(g);

    const uint32_t first[] = {0xE8A00003}; /* stmia r0!, {r0, r1} */
    g = tu_arm(first, TU_LEN(first));
    g->cpu.r[0] = IW + 0x500;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x500, false), IW + 0x500); /* the original base */
    gba_destroy(g);
}

TEST(arm_ldm_base_in_list_keeps_loaded_value) {
    const uint32_t ops[] = {0xE8B10006}; /* ldmia r1!, {r1, r2} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = IW + 0x600;
    bus_write32(g, IW + 0x600, 0xAA, false);
    bus_write32(g, IW + 0x604, 0xBB, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[1], 0xAA);
    CHECK_EQ(g->cpu.r[2], 0xBB);
    gba_destroy(g);
}

TEST(arm_stm_user_bank) {
    const uint32_t ops[] = {0xE8C00100}; /* stmia r0, {r8}^ */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x700;
    g->cpu.r[8] = 0x55;
    cpu_write_cpsr(&g->cpu, MODE_FIQ);
    g->cpu.r[8] = 0x66;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x700, false), 0x55);
    CHECK_EQ(g->cpu.r[8], 0x66);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_FIQ);
    gba_destroy(g);
}

TEST(arm_ldm_pc_with_s_restores_cpsr) {
    const uint32_t ops[] = {0xE8D08000}; /* ldmia r0, {pc}^ */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x800;
    bus_write32(g, IW + 0x800, 0x08000051, false);
    cpu_write_cpsr(&g->cpu, MODE_IRQ);
    g->cpu.spsr[BANK_IRQ] = MODE_SYS | CPSR_T;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS | CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x08000050);
    gba_destroy(g);
}
