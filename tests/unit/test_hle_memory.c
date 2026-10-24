#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

#define SRC 0x02000000u
#define DST 0x02000100u

TEST(hle_cpu_set_copies_halfwords) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 5; i++) bus_write16(g, SRC + i * 2, (uint16_t)(i + 1), false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 4;
    hle_cpu_set(g);
    for (uint32_t i = 0; i < 4; i++) CHECK_EQ(bus_read16(g, DST + i * 2, false), i + 1);
    CHECK_EQ(bus_read16(g, DST + 8, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_set_fills_words) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 3 | (1u << 24) | (1u << 26);
    hle_cpu_set(g);
    for (uint32_t i = 0; i < 3; i++) CHECK_EQ(bus_read32(g, DST + i * 4, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, DST + 12, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_set_refuses_bios_source) {
    gba_t *g = gba_create(NULL);
    g->cpu.r[0] = 0x00000000;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 4;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_fast_set_rounds_up_to_eight_words) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 16; i++) bus_write32(g, SRC + i * 4, i + 1, false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 3;
    hle_cpu_fast_set(g);
    for (uint32_t i = 0; i < 8; i++) CHECK_EQ(bus_read32(g, DST + i * 4, false), i + 1);
    CHECK_EQ(bus_read32(g, DST + 32, false), 0);
    gba_destroy(g);
}

TEST(hle_bit_unpack_nibbles_to_bytes) {
    gba_t *g = gba_create(NULL);
    const uint32_t info = SRC + 0x10;
    bus_write8(g, SRC, 0x21, false);
    bus_write8(g, SRC + 1, 0x03, false);
    bus_write16(g, info, 2, false);         /* source length in bytes */
    bus_write8(g, info + 2, 4, false);      /* source unit width */
    bus_write8(g, info + 3, 8, false);      /* destination unit width */
    bus_write32(g, info + 4, 0x10, false);  /* offset 0x10, zero units stay zero */
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = info;
    hle_bit_unpack(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00131211);
    gba_destroy(g);
}

TEST(hle_cpu_set_halfwords_from_an_odd_address_rotate) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC + 1; /* the BIOS uses LDRH: a misaligned read rotates the halfword */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 2;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00DE00BE);
    gba_destroy(g);
}

TEST(hle_cpu_set_words_from_an_odd_address_read_the_aligned_word) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC + 1; /* LDMIA: the bus ignores the low bits */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0xDEADBEEF);
    gba_destroy(g);
}

TEST(hle_copies_keep_exact_save_area_addresses) {
    const char *const markers[] = {"SRAM_V"};
    gba_t *g = tu_rom_markers(markers, 1, 0x400);
    const char text[] = "Game";
    for (uint32_t i = 0; i < 4; i++) bus_write8(g, 0x0E000000 + i, (uint8_t)text[i], false);

    g->cpu.r[0] = 0x0E000001; /* 32-bit CpuSet: the byte at +1, repeated */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x61616161);

    g->cpu.r[0] = 0x0E000001; /* 16-bit CpuSet: 'a' and 'e', each rotated into the low byte */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 2;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00650061);

    g->cpu.r[0] = 0x0E000003; /* CpuFastSet */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1;
    hle_cpu_fast_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x65656565);

    bus_write32(g, SRC, 0x11223344, false); /* 32-bit CpuSet into SRAM + 0x11: one byte, by address */
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = 0x0E000011;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read8(g, 0x0E000011, false), 0x33);
    gba_destroy(g);
}
