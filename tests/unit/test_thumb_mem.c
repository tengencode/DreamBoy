#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(thumb_pc_relative_load_uses_word_aligned_pc) {
    const uint16_t ops[] = {
        0x46C0, /* nop (mov r8, r8) */
        0x4901, /* ldr r1, [pc, #4] */
        0x0000,
        0x0000,
        0x5678, /* literal at 0x08000008 */
        0x1234,
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0x12345678);
    gba_destroy(g);
}

TEST(thumb_register_offset_transfers) {
    const uint16_t ops[] = {
        0x50C2, /* str   r2, [r0, r3] */
        0x58C4, /* ldr   r4, [r0, r3] */
        0x54C2, /* strb  r2, [r0, r3] */
        0x5CC5, /* ldrb  r5, [r0, r3] */
        0x52C2, /* strh  r2, [r0, r3] */
        0x5AC6, /* ldrh  r6, [r0, r3] */
        0x56C7, /* ldrsb r7, [r0, r3] */
        0x5EC4, /* ldrsh r4, [r0, r3] */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW;
    c->r[3] = 4;
    c->r[2] = 0x12348281;
    tu_steps(g, 2);
    CHECK_EQ(c->r[4], 0x12348281);
    tu_steps(g, 5);
    CHECK_EQ(c->r[5], 0x81);
    CHECK_EQ(c->r[6], 0x8281);
    CHECK_EQ(c->r[7], 0xFFFFFF81u);
    tu_steps(g, 1);
    CHECK_EQ(c->r[4], 0xFFFF8281u);
    gba_destroy(g);
}

TEST(thumb_misaligned_word_load_rotates) {
    const uint16_t ops[] = {0x58C4}; /* ldr r4, [r0, r3] */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[3] = 1;
    bus_write32(g, IW, 0x11223344, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[4], 0x44112233);
    gba_destroy(g);
}

TEST(thumb_immediate_offset_transfers) {
    const uint16_t ops[] = {
        0x6041, /* str  r1, [r0, #4] */
        0x6842, /* ldr  r2, [r0, #4] */
        0x7241, /* strb r1, [r0, #9] */
        0x7A43, /* ldrb r3, [r0, #9] */
        0x8181, /* strh r1, [r0, #12] */
        0x8984, /* ldrh r4, [r0, #12] */
        0x89B5, /* ldrh r5, [r6, #12] (r6 odd: rotated) */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW;
    c->r[1] = 0xAABBCCDD;
    c->r[6] = IW + 1;
    tu_steps(g, 7);
    CHECK_EQ(c->r[2], 0xAABBCCDD);
    CHECK_EQ(c->r[3], 0xDD);
    CHECK_EQ(c->r[4], 0xCCDD);
    CHECK_EQ(c->r[5], 0xDD0000CC);
    gba_destroy(g);
}

TEST(thumb_sp_relative_and_address_generation) {
    const uint16_t ops[] = {
        0x9102, /* str r1, [sp, #8] */
        0x9A02, /* ldr r2, [sp, #8] */
        0xA302, /* add r3, pc, #8 */
        0xAC04, /* add r4, sp, #16 */
        0xB082, /* sub sp, #8 */
        0xB002, /* add sp, #8 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[1] = 77;
    tu_steps(g, 4);
    CHECK_EQ(c->r[2], 77);
    CHECK_EQ(c->r[3], 0x08000010);
    CHECK_EQ(c->r[4], 0x03007F10);
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007EF8);
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007F00);
    gba_destroy(g);
}

TEST(thumb_push_and_pop_pc) {
    const uint16_t ops[] = {
        0xB503, /* push {r0, r1, lr} */
        0xBD0C, /* pop  {r2, r3, pc} */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = 1;
    c->r[1] = 2;
    c->r[14] = 0x08000041;
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007EF4);
    CHECK_EQ(bus_read32(g, 0x03007EF4, false), 1);
    CHECK_EQ(bus_read32(g, 0x03007EFC, false), 0x08000041);
    tu_steps(g, 1);
    CHECK_EQ(c->r[2], 1);
    CHECK_EQ(c->r[3], 2);
    CHECK_EQ(c->r[15], 0x08000040);
    CHECK_EQ(c->r[13], 0x03007F00);
    CHECK(c->cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_ldmia_stmia) {
    const uint16_t ops[] = {
        0xC006, /* stmia r0!, {r1, r2} */
        0x3808, /* subs  r0, #8 */
        0xC818, /* ldmia r0!, {r3, r4} */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW + 0x100;
    c->r[1] = 5;
    c->r[2] = 6;
    tu_steps(g, 3);
    CHECK_EQ(c->r[3], 5);
    CHECK_EQ(c->r[4], 6);
    CHECK_EQ(c->r[0], IW + 0x108);
    gba_destroy(g);
}

TEST(thumb_ldmia_base_in_list_keeps_loaded_value) {
    const uint16_t ops[] = {0xC906}; /* ldmia r1!, {r1, r2} */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[1] = IW + 0x200;
    bus_write32(g, IW + 0x200, 0x77, false);
    bus_write32(g, IW + 0x204, 0x88, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[1], 0x77);
    CHECK_EQ(g->cpu.r[2], 0x88);
    gba_destroy(g);
}

TEST(thumb_empty_rlist_quirks) {
    const uint16_t store[] = {0xC000}; /* stmia r0!, {} */
    gba_t *g = tu_thumb(store, TU_LEN(store));
    g->cpu.r[0] = IW + 0x300;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x300, false), 0x08000006);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);

    const uint16_t load[] = {0xC800}; /* ldmia r0!, {} */
    g = tu_thumb(load, TU_LEN(load));
    g->cpu.r[0] = IW + 0x300;
    bus_write32(g, IW + 0x300, 0x08000081, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000080);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}
