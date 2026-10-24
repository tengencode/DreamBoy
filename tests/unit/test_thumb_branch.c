#include "test.h"
#include "testutil.h"

TEST(thumb_conditional_branch) {
    const uint16_t ops[] = {0xD00E}; /* beq .+0x20 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.cpsr |= CPSR_Z;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    gba_destroy(g);

    g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000002);
    gba_destroy(g);
}

TEST(thumb_unconditional_branch) {
    const uint16_t fwd[] = {0xE01C}; /* b .+0x3C */
    gba_t *g = tu_thumb(fwd, TU_LEN(fwd));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x0800003C);
    gba_destroy(g);

    const uint16_t back[] = {0x46C0, 0xE7FD}; /* nop; b .-2 */
    g = tu_thumb(back, TU_LEN(back));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(thumb_long_branch_with_link) {
    const uint16_t ops[] = {0xF000, 0xFFFB}; /* bl .+0xFFA */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000FFA);
    CHECK_EQ(g->cpu.r[14], 0x08000005);
    CHECK(g->cpu.cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_swi_enters_arm_supervisor_mode) {
    const uint16_t ops[] = {0xDF06}; /* swi 6 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SVC);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[14], 0x08000002);
    CHECK_EQ(g->cpu.spsr[BANK_SVC], MODE_SYS | CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    gba_destroy(g);
}
