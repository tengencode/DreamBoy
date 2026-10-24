#include "test.h"
#include "testutil.h"

/* Unmapped reads return what the CPU has already prefetched: in ARM state the word two instructions
 * ahead ($+8); in Thumb state it depends on the region the code runs from (GBATEK). */

TEST(open_bus_arm_is_the_word_two_ahead) {
    const uint32_t ops[] = {
        0xE5910000, /* ldr r0, [r1] */
        0xE1A00000, /* nop */
        0x12345678, /* data at $+8 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x01000000; /* unmapped */
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x12345678);
    gba_destroy(g);
}

TEST(open_bus_bios_out_of_range_byte_lane) {
    const uint32_t ops[] = {
        0xE5D10000, /* ldrb r0, [r1] */
        0xE1A00000, /* nop */
        0x12345678,
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x00010003; /* past the 16 KiB BIOS */
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x12);
    gba_destroy(g);
}

TEST(open_bus_thumb_rom_repeats_the_halfword_at_4) {
    const uint16_t ops[] = {0x6808 /* ldr r0, [r1] */, 0x1111, 0x2222};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x01000000;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x22222222);
    gba_destroy(g);
}

/* Thumb code copied to `mem` (offset 0 = first halfword) and run from `pc`. */
static uint32_t thumb_open_bus_at(uint8_t *(*mem)(gba_t *), uint32_t pc, const uint16_t *halves, size_t n) {
    gba_t *g = gba_create(NULL);
    uint8_t *m = mem(g);
    for (size_t i = 0; i < n; i++) {
        m[i * 2] = (uint8_t)halves[i];
        m[i * 2 + 1] = (uint8_t)(halves[i] >> 8);
    }
    g->cpu.cpsr |= CPSR_T;
    g->cpu.r[15] = pc;
    g->cpu.r[1] = 0x01000000;
    tu_steps(g, 1);
    uint32_t r0 = g->cpu.r[0];
    gba_destroy(g);
    return r0;
}

static uint8_t *iwram(gba_t *g) {
    return g->bus.iwram;
}

static uint8_t *oam(gba_t *g) {
    return g->bus.oam;
}

TEST(open_bus_thumb_iwram_aligned_and_unaligned) {
    const uint16_t aligned[] = {0x6808, 0x1111, 0x2222}; /* $ = +0: low [$+4], high [$+2] */
    CHECK_EQ(thumb_open_bus_at(iwram, 0x03000000, aligned, TU_LEN(aligned)), 0x11112222);
    const uint16_t unaligned[] = {0x0000, 0x6808, 0x2222, 0x3333}; /* $ = +2: low [$+2], high [$+4] */
    CHECK_EQ(thumb_open_bus_at(iwram, 0x03000002, unaligned, TU_LEN(unaligned)), 0x33332222);
}

TEST(open_bus_thumb_oam_uses_the_next_two_halfwords) {
    const uint16_t aligned[] = {0x6808, 0x1111, 0x2222, 0x3333}; /* $ = +0: low [$+4], high [$+6] */
    CHECK_EQ(thumb_open_bus_at(oam, 0x07000000, aligned, TU_LEN(aligned)), 0x33332222);
}

TEST(open_bus_fetch_from_unmapped_space_does_not_recurse) {
    gba_t *g = gba_create(NULL);
    g->bus.open_bus = 0xE1A00000; /* the last opcode fetched: nop */
    g->cpu.r[15] = 0x01000000;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x01000004);
    gba_destroy(g);
}
