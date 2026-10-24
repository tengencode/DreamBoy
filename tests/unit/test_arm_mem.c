#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(arm_word_and_byte_transfers) {
    const uint32_t ops[] = {
        0xE5801000, /* str  r1, [r0] */
        0xE5902001, /* ldr  r2, [r0, #1] (rotated) */
        0xE5D03002, /* ldrb r3, [r0, #2] */
        0xE5C01008, /* strb r1, [r0, #8] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x11223344;
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[2], 0x44112233);
    CHECK_EQ(g->cpu.r[3], 0x22);
    CHECK_EQ(bus_read32(g, IW + 8, false), 0x44);
    gba_destroy(g);
}

TEST(arm_halfword_loads_rotate_when_misaligned) {
    const uint32_t ops[] = {
        0xE5801000, /* str  r1, [r0] */
        0xE1D040B2, /* ldrh r4, [r0, #2] */
        0xE1D050B1, /* ldrh r5, [r0, #1] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x11223344;
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[4], 0x1122);
    CHECK_EQ(g->cpu.r[5], 0x44000033);
    gba_destroy(g);
}

TEST(arm_signed_loads) {
    const uint32_t ops[] = {
        0xE1C011B0, /* strh  r1, [r0, #16] */
        0xE1D061F0, /* ldrsh r6, [r0, #16] */
        0xE1D071F1, /* ldrsh r7, [r0, #17] (misaligned: signed byte) */
        0xE1D081D0, /* ldrsb r8, [r0, #16] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0xFF80;
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFF80u);
    CHECK_EQ(g->cpu.r[7], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFF80u);
    gba_destroy(g);
}

TEST(arm_indexing_and_writeback) {
    const uint32_t ops[] = {
        0xE5A01004, /* str r1, [r0, #4]! */
        0xE4109004, /* ldr r9, [r0], #-4 */
        0xE790B10C, /* ldr r11, [r0, r12, lsl #2] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0xABCD;
    g->cpu.r[12] = 1;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 4);
    CHECK_EQ(bus_read32(g, IW + 4, false), 0xABCD);
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[9], 0xABCD);
    CHECK_EQ(g->cpu.r[0], IW);
    CHECK_EQ(g->cpu.r[11], 0xABCD);
    gba_destroy(g);
}

TEST(arm_store_pc_and_load_pc) {
    const uint32_t ops[] = {
        0xE580F00C, /* str pc, [r0, #12] */
        0xE590F014, /* ldr pc, [r0, #20] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    bus_write32(g, IW + 20, 0x08000100, false);
    tu_steps(g, 2);
    CHECK_EQ(bus_read32(g, IW + 12, false), 0x0800000C);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    gba_destroy(g);
}

TEST(arm_load_writeback_same_register_keeps_loaded_value) {
    const uint32_t ops[] = {0xE5B00004}; /* ldr r0, [r0, #4]! */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    bus_write32(g, IW + 4, 0x55, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x55);
    gba_destroy(g);
}

TEST(arm_swap_word_and_byte) {
    const uint32_t ops[] = {
        0xE100A091, /* swp  r10, r1, [r0] */
        0xE140B092, /* swpb r11, r2, [r0] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x22222222;
    g->cpu.r[2] = 0x33;
    bus_write32(g, IW, 0x11111111, false);
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[10], 0x11111111);
    CHECK_EQ(g->cpu.r[11], 0x22);
    CHECK_EQ(bus_read32(g, IW, false), 0x22222233);
    gba_destroy(g);
}
