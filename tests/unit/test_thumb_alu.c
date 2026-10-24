#include "test.h"
#include "testutil.h"

TEST(thumb_shift_and_add_sub_immediates) {
    const uint16_t ops[] = {
        0x2005, /* movs r0, #5 */
        0x0081, /* lsls r1, r0, #2 */
        0x0842, /* lsrs r2, r0, #1 */
        0x1003, /* asrs r3, r0, #32 */
        0x1844, /* adds r4, r0, r1 */
        0x1F85, /* subs r5, r0, #6 */
        0x2805, /* cmp  r0, #5 */
        0x30FA, /* adds r0, #250 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 3);
    CHECK_EQ(c->r[1], 20);
    CHECK_EQ(c->r[2], 2);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[3], 0);
    CHECK(!(c->cpsr & CPSR_C));
    CHECK(c->cpsr & CPSR_Z);
    tu_steps(g, 2);
    CHECK_EQ(c->r[4], 25);
    CHECK_EQ(c->r[5], 0xFFFFFFFFu);
    CHECK(c->cpsr & CPSR_N);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 1);
    CHECK(c->cpsr & CPSR_Z);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[0], 255);
    CHECK_EQ(c->r[15], 0x08000010);
    gba_destroy(g);
}

TEST(thumb_alu_logical_and_multiply) {
    const uint16_t ops[] = {
        0x200C, /* movs r0, #12 */
        0x210A, /* movs r1, #10 */
        0x1C02, /* adds r2, r0, #0 */
        0x400A, /* ands r2, r1 */
        0x1C03, /* adds r3, r0, #0 */
        0x404B, /* eors r3, r1 */
        0x1C04, /* adds r4, r0, #0 */
        0x430C, /* orrs r4, r1 */
        0x1C05, /* adds r5, r0, #0 */
        0x438D, /* bics r5, r1 */
        0x43CE, /* mvns r6, r1 */
        0x424F, /* negs r7, r1 */
        0x4348, /* muls r0, r1 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 13);
    CHECK_EQ(c->r[2], 8);
    CHECK_EQ(c->r[3], 6);
    CHECK_EQ(c->r[4], 14);
    CHECK_EQ(c->r[5], 4);
    CHECK_EQ(c->r[6], 0xFFFFFFF5u);
    CHECK_EQ(c->r[7], 0xFFFFFFF6u);
    CHECK_EQ(c->r[0], 120);
    gba_destroy(g);
}

TEST(thumb_register_shifts) {
    const uint16_t ops[] = {
        0x2001, /* movs r0, #1 */
        0x2121, /* movs r1, #33 */
        0x4088, /* lsls r0, r1 */
        0x2280, /* movs r2, #0x80 */
        0x2304, /* movs r3, #4 */
        0x41DA, /* rors r2, r3 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 3);
    CHECK_EQ(c->r[0], 0);
    CHECK(c->cpsr & CPSR_Z);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 3);
    CHECK_EQ(c->r[2], 0x08);
    CHECK(!(c->cpsr & CPSR_C));
    gba_destroy(g);
}

TEST(thumb_adc_sbc_cmn_tst) {
    const uint16_t ops[] = {
        0x2000, /* movs r0, #0 */
        0x3801, /* subs r0, #1 */
        0x2105, /* movs r1, #5 */
        0x4141, /* adcs r1, r0 */
        0x4181, /* sbcs r1, r0 */
        0x42C0, /* cmn  r0, r0 */
        0x4208, /* tst  r0, r1 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 4);
    CHECK_EQ(c->r[1], 4);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[1], 5);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 1);
    CHECK(c->cpsr & CPSR_N);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK(!(c->cpsr & CPSR_N));
    CHECK(!(c->cpsr & CPSR_Z));
    CHECK(c->cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(thumb_high_register_ops) {
    const uint16_t ops[] = {
        0x4680, /* mov r8, r0 */
        0x4488, /* add r8, r1 */
        0x4580, /* cmp r8, r0 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 3;
    g->cpu.r[1] = 4;
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[8], 7);
    CHECK(g->cpu.cpsr & CPSR_C);
    CHECK(!(g->cpu.cpsr & CPSR_Z));
    gba_destroy(g);
}

TEST(thumb_mov_pc_stays_in_thumb) {
    const uint16_t ops[] = {0x4687}; /* mov pc, r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000021;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    CHECK(g->cpu.cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_add_pc_aligns) {
    const uint16_t ops[] = {0x4487}; /* add pc, r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    gba_destroy(g);
}

TEST(thumb_bx_to_arm) {
    const uint16_t ops[] = {0x4700}; /* bx r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000102;
    tu_steps(g, 1);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    gba_destroy(g);
}
