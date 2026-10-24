#include "test.h"
#include "testutil.h"

TEST(arm_mov_add_subs) {
    const uint32_t ops[] = {
        0xE3A00005, /* mov  r0, #5 */
        0xE2801003, /* add  r1, r0, #3 */
        0xE2502005, /* subs r2, r0, #5 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[1], 8);
    CHECK_EQ(g->cpu.r[2], 0);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    gba_destroy(g);
}

TEST(arm_rotated_immediate_sets_carry) {
    const uint32_t ops[] = {0xE3B00102}; /* movs r0, #0x80000000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x80000000);
    CHECK(g->cpu.cpsr & CPSR_N);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_register_shift_reads_pc_plus_12) {
    const uint32_t ops[] = {
        0xE3A00000, /* mov r0, #0 */
        0xE1A0101F, /* mov r1, pc, lsl r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0x08000010);
    gba_destroy(g);
}

TEST(arm_conditional_execution) {
    const uint32_t ops[] = {
        0xE3A00001, /* mov   r0, #1 */
        0xE3500001, /* cmp   r0, #1 */
        0x13A01007, /* movne r1, #7 */
        0x03A02009, /* moveq r2, #9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[2], 9);
    gba_destroy(g);
}

TEST(arm_carry_arithmetic) {
    const uint32_t ops[] = {
        0xE3E00000, /* mvn  r0, #0 */
        0xE2901001, /* adds r1, r0, #1 */
        0xE2A12000, /* adc  r2, r1, #0 */
        0xE262300A, /* rsb  r3, r2, #10 */
        0xE2524002, /* subs r4, r2, #2 */
        0xE2C35000, /* sbc  r5, r3, #0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 6);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[2], 1);
    CHECK_EQ(g->cpu.r[3], 9);
    CHECK_EQ(g->cpu.r[4], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[5], 8);
    gba_destroy(g);
}

TEST(arm_tst_takes_carry_from_rotated_immediate) {
    const uint32_t ops[] = {0xE3100102}; /* tst r0, #0x80000000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_logical_shift_sets_carry) {
    const uint32_t ops[] = {0xE1B000A1}; /* movs r0, r1, lsr #1 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 1;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_subs_pc_restores_cpsr) {
    const uint32_t ops[] = {0xE25EF004}; /* subs pc, lr, #4 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_IRQ | CPSR_I);
    g->cpu.spsr[BANK_IRQ] = MODE_SYS | CPSR_Z;
    g->cpu.r[14] = 0x08000104;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS | CPSR_Z);
    gba_destroy(g);
}

TEST(arm_psr_transfer) {
    const uint32_t ops[] = {
        0xE10F0000, /* mrs r0, cpsr */
        0xE328F20F, /* msr cpsr_f, #0xF0000000 */
        0xE10F1000, /* mrs r1, cpsr */
        0xE321F012, /* msr cpsr_c, #0x12 */
        0xE10F2000, /* mrs r2, cpsr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 5);
    CHECK_EQ(g->cpu.r[0], 0x0000001F);
    CHECK_EQ(g->cpu.r[1], 0xF000001F);
    CHECK_EQ(g->cpu.r[2], 0xF0000012);
    CHECK_EQ(g->cpu.r[13], 0x03007FA0);
    gba_destroy(g);
}

TEST(arm_msr_in_user_mode_only_writes_flags) {
    const uint32_t ops[] = {
        0xE3A0001F, /* mov r0, #0x1F */
        0xE380020F, /* orr r0, r0, #0xF0000000 */
        0xE129F000, /* msr cpsr_fc, r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_USR);
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.cpsr, 0xF0000010);
    gba_destroy(g);
}

TEST(arm_msr_mrs_spsr) {
    const uint32_t ops[] = {
        0xE3A00010, /* mov r0, #0x10 */
        0xE169F000, /* msr spsr_fc, r0 */
        0xE14F1000, /* mrs r1, spsr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_IRQ);
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[1], 0x10);
    CHECK_EQ(g->cpu.spsr[BANK_IRQ], 0x10);
    gba_destroy(g);
}

TEST(arm_compare_with_pc_destination_restores_cpsr_without_branching) {
    const uint32_t ops[] = {
        0xE15FF000, /* cmp pc, r0 with Rd = 15 (ARMv3 "cmpp") */
        0xE3A01001, /* mov r1, #1 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_FIQ);
    g->cpu.spsr[BANK_FIQ] = MODE_SYS;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    CHECK_EQ(g->cpu.r[1], 1);
    CHECK_EQ(g->cpu.r[15], 0x08000008);
    gba_destroy(g);
}
