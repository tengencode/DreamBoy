#include "test.h"
#include "testutil.h"

TEST(arm_multiply_family) {
    const uint32_t ops[] = {
        0xE3A00006, /* mov   r0, #6 */
        0xE3A01007, /* mov   r1, #7 */
        0xE0020190, /* mul   r2, r0, r1 */
        0xE0232190, /* mla   r3, r0, r1, r2 */
        0xE3E04000, /* mvn   r4, #0 */
        0xE0865494, /* umull r5, r6, r4, r4 */
        0xE0C87494, /* smull r7, r8, r4, r4 */
        0xE3A09002, /* mov   r9, #2 */
        0xE0E87994, /* smlal r7, r8, r4, r9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 9);
    CHECK_EQ(g->cpu.r[2], 42);
    CHECK_EQ(g->cpu.r[3], 84);
    CHECK_EQ(g->cpu.r[5], 1);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[7], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFFFFu);
    gba_destroy(g);
}

TEST(arm_multiply_flags_keep_overflow_and_set_the_booth_carry) {
    const uint32_t ops[] = {
        0xE328F203, /* msr  cpsr_f, #0x30000000 (C and V) */
        0xE01A0094, /* muls r10, r4, r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[4] = 0xFFFFFFFF;
    g->cpu.r[0] = 6;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[10], 0xFFFFFFFAu);
    CHECK(g->cpu.cpsr & CPSR_N);
    CHECK(!(g->cpu.cpsr & CPSR_Z));
    CHECK(!(g->cpu.cpsr & CPSR_C)); /* the Booth carry for -1 * 6 is 0 */
    CHECK(g->cpu.cpsr & CPSR_V);    /* V is never touched */
    gba_destroy(g);
}
