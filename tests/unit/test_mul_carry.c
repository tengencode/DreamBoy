#include "cpu/mul_carry.h"
#include "test.h"
#include "testutil.h"

TEST(mul_carry_reference_values) {
    CHECK_EQ(mul_carry(MUL_SHORT, 0xFFFFFFFF, 6, 0), 0);
    CHECK_EQ(mul_carry(MUL_SHORT, 3, 0x40000000, 0), 0);
    CHECK_EQ(mul_carry(MUL_SHORT, 3, 0x80000000, 0), 1);
    CHECK_EQ(mul_carry(MUL_SHORT, 0x12345678, 0x9ABCDEF0, 0x0F0F0F0F), 1);  /* MLA */
    CHECK_EQ(mul_carry(MUL_LONG_SIGNED, 0xFFFFFFFF, 0x7FFFFFFF, 0), 1);       /* suite: -1 * $7F */
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0xFFFFFFFF, 0xFFFFFFFF, 0), 1);     /* suite: -1 * -1 */
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0, 0, 0), 0);
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0xDEADBEEF, 0x80000001, 0x00000001FFFFFFFFull), 1); /* UMLAL */
    CHECK_EQ(mul_carry(MUL_LONG_SIGNED, 0x80000000, 0x80000000, 0xFFFFFFFF00000000ull), 0);  /* SMLAL */
}

TEST(arm_long_multiplies_set_the_booth_carry) {
    const uint32_t ops[] = {
        0xE328F000, /* msr   cpsr_f, #0 */
        0xE0D87994, /* smulls r7, r8, r4, r9 */
        0xE328F000, /* msr   cpsr_f, #0 */
        0xE0965994, /* umulls r5, r6, r4, r9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[4] = 0xFFFFFFFF;
    g->cpu.r[9] = 0x7FFFFFFF;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[7], 0x80000001u);
    CHECK(g->cpu.cpsr & CPSR_C);
    g->cpu.r[9] = 0xFFFFFFFF;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[5], 0x00000001u);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_mla_carry_uses_the_accumulator_before_the_write) {
    const uint32_t ops[] = {
        0xE0302190, /* mlas r0, r0, r1, r2 (rd = rm: operands are read first) */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x12345678;
    g->cpu.r[1] = 0x9ABCDEF0;
    g->cpu.r[2] = 0x0F0F0F0F;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x333C2F8Fu);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(thumb_mul_sets_the_booth_carry) {
    const uint16_t ops[] = {0x4348 /* muls r0, r1 */};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x80000000; /* Rd: the multiplier */
    g->cpu.r[1] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x80000000u);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}
