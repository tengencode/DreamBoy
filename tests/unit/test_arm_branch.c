#include "test.h"
#include "testutil.h"

TEST(arm_branch_forward) {
    const uint32_t ops[] = {0xEA000002}; /* b .+0x10 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000010);
    gba_destroy(g);
}

TEST(arm_branch_with_link) {
    const uint32_t ops[] = {0xEB00003E}; /* bl .+0x100 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    CHECK_EQ(g->cpu.r[14], 0x08000004);
    gba_destroy(g);
}

TEST(arm_branch_backward) {
    const uint32_t ops[] = {
        0xE1A00000, /* mov r0, r0 */
        0xEAFFFFFD, /* b .-4 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(arm_bx_switches_to_thumb) {
    const uint32_t ops[] = {0xE12FFF10}; /* bx r0 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000021;
    tu_steps(g, 1);
    CHECK(g->cpu.cpsr & CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    gba_destroy(g);
}

TEST(arm_bx_to_arm_aligns_target) {
    const uint32_t ops[] = {0xE12FFF10}; /* bx r0 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000032;
    tu_steps(g, 1);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[15], 0x08000030);
    gba_destroy(g);
}

TEST(arm_swi_enters_supervisor_mode) {
    const uint32_t ops[] = {0xEF010000}; /* swi 0x10000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SVC);
    CHECK(g->cpu.cpsr & CPSR_I);
    CHECK_EQ(g->cpu.r[14], 0x08000004);
    CHECK_EQ(g->cpu.spsr[BANK_SVC], MODE_SYS);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    gba_destroy(g);
}
