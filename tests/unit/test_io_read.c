#include "test.h"
#include "testutil.h"

/* A system about to run "ldrh r0, [r1]" (ARM) with r1 = addr. The word two instructions ahead is
 * 0xDEADDEAD, so an open-bus halfword read gives 0xDEAD (the suite's own trick). */
static gba_t *ldrh_rig(uint32_t addr) {
    static const uint32_t ops[] = {
        0xE1D100B0, /* ldrh r0, [r1] */
        0xE1A00000, /* nop */
        0xDEADDEAD, /* prefetched while the ldrh runs */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = addr;
    return g;
}

TEST(io_write_only_registers_read_open_bus) {
    static const uint32_t offs[] = {0x010, 0x01E, 0x020, 0x03E, 0x040, 0x04C, 0x04E, 0x054,
                                    0x05E, 0x08C, 0x0A0, 0x0A8, 0x0B0, 0x0D6, 0x0E0, 0x110};
    for (size_t i = 0; i < TU_LEN(offs); i++) {
        gba_t *g = ldrh_rig(0x04000000 + offs[i]);
        tu_io16(g, offs[i], 0xFFFF);
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0xDEAD);
        gba_destroy(g);
    }
}

TEST(io_reads_above_the_register_area_are_open_bus) {
    gba_t *g = ldrh_rig(0x0400100C);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0xDEAD);
    gba_destroy(g);
}

TEST(io_readable_registers_hide_unused_bits) {
    static const struct {
        uint32_t off;
        uint16_t expected;
    } cases[] = {{0x008, 0xDFFF}, {0x00A, 0xDFFF}, {0x00C, 0xFFFF}, {0x048, 0x3F3F}, {0x04A, 0x3F3F},
                 {0x050, 0x3FFF}, {0x052, 0x1F1F}, {0x0BA, 0xF7E0}, {0x0DE, 0xFFE0}, {0x102, 0x00C7},
                 {0x132, 0xC3FF}, {0x200, 0x3FFF}, {0x204, 0x5FFF}, {0x208, 0x0001}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, cases[i].off, 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + cases[i].off, false), cases[i].expected);
        gba_destroy(g);
    }
}

TEST(io_sound_registers_read_back_only_their_readable_bits) {
    static const struct {
        uint32_t off;
        uint16_t expected;
    } cases[] = {{0x060, 0x007F}, {0x062, 0xFFC0}, {0x064, 0x4000}, {0x068, 0xFFC0}, {0x06C, 0x4000},
                 {0x070, 0x00E0}, {0x072, 0xE000}, {0x074, 0x4000}, {0x078, 0xFF00}, {0x07C, 0x40FF},
                 {0x080, 0xFF77}, {0x082, 0x770F}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, 0x084, 0x0080); /* sound on, or the PSG registers stay locked */
        tu_io16(g, cases[i].off, 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + cases[i].off, false), cases[i].expected);
        gba_destroy(g);
    }
}

TEST(io_unused_slots_read_zero) {
    static const uint32_t offs[] = {0x066, 0x06A, 0x06E, 0x076, 0x07A, 0x07E, 0x086, 0x08A,
                                    0x0B8, 0x0C4, 0x0D0, 0x0DC, 0x136, 0x142, 0x15A, 0x206,
                                    0x20A, 0x302};
    for (size_t i = 0; i < TU_LEN(offs); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, offs[i], 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + offs[i], false), 0);
        gba_destroy(g);
    }
}

TEST(io_wide_and_byte_reads_follow_each_half) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x048, 0xFFFF);
    tu_io16(g, 0x04A, 0xFFFF);
    CHECK_EQ(bus_read32(g, 0x04000048, false), 0x3F3F3F3F);
    tu_io16(g, 0x008, 0xFFFF);
    CHECK_EQ(bus_read8(g, 0x04000009, false), 0xDF);
    gba_destroy(g);
}

TEST(io_byte_writes_keep_hidden_bits) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x008, 0xFFFF);
    bus_write8(g, 0x04000008, 0x00, false);
    CHECK_EQ(tu_raw16(g, 0x008), 0xFF00);
    CHECK_EQ(bus_read16(g, 0x04000008, false), 0xDF00);
    gba_destroy(g);
}
