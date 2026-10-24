#include <string.h>

#include "gba_internal.h"
#include "test.h"

TEST(bus_ewram_and_iwram_mirror) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x11223344, false);
    CHECK_EQ(bus_read32(g, 0x02040000, false), 0x11223344);
    bus_write32(g, 0x03000010, 0xAABBCCDD, false);
    CHECK_EQ(bus_read32(g, 0x03008010, false), 0xAABBCCDD);
    CHECK_EQ(bus_read32(g, 0x03FF8010, false), 0xAABBCCDD);
    gba_destroy(g);
}

TEST(bus_accesses_are_little_endian_and_force_aligned) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x11223344, false);
    CHECK_EQ(bus_read8(g, 0x02000001, false), 0x33);
    CHECK_EQ(bus_read16(g, 0x02000002, false), 0x1122);
    CHECK_EQ(bus_read16(g, 0x02000003, false), 0x1122);
    CHECK_EQ(bus_read32(g, 0x02000002, false), 0x11223344);
    gba_destroy(g);
}

TEST(bus_palette_byte_write_is_doubled) {
    gba_t *g = gba_create(NULL);
    bus_write8(g, 0x05000021, 0x7F, false);
    CHECK_EQ(bus_read16(g, 0x05000020, false), 0x7F7F);
    CHECK_EQ(bus_read16(g, 0x05000420, false), 0x7F7F);
    gba_destroy(g);
}

TEST(bus_oam_ignores_byte_writes) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x07000010, 0x12345678, false);
    bus_write8(g, 0x07000010, 0xFF, false);
    CHECK_EQ(bus_read32(g, 0x07000010, false), 0x12345678);
    CHECK_EQ(bus_read32(g, 0x07000410, false), 0x12345678);
    gba_destroy(g);
}

TEST(bus_vram_mirrors) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x06010000, 0xCAFE, false);
    CHECK_EQ(bus_read32(g, 0x06018000, false), 0xCAFE);
    bus_write32(g, 0x06000000, 7, false);
    CHECK_EQ(bus_read32(g, 0x06020000, false), 7);
    gba_destroy(g);
}

TEST(bus_vram_byte_writes_depend_on_display_mode) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispcnt = 0; /* tiled: BG VRAM ends at 0x10000 */
    bus_write8(g, 0x0600FFFF, 0x42, false);
    CHECK_EQ(bus_read16(g, 0x0600FFFE, false), 0x4242);
    bus_write8(g, 0x06010010, 0x42, false);
    CHECK_EQ(bus_read16(g, 0x06010010, false), 0);
    g->ppu.dispcnt = 3; /* bitmap: BG VRAM ends at 0x14000 */
    bus_write8(g, 0x06013FF0, 0x11, false);
    CHECK_EQ(bus_read16(g, 0x06013FF0, false), 0x1111);
    bus_write8(g, 0x06014010, 0x11, false);
    CHECK_EQ(bus_read16(g, 0x06014010, false), 0);
    gba_destroy(g);
}

TEST(bus_rom_is_mirrored_in_all_waitstate_regions) {
    static uint8_t rom[0x200];
    memset(rom, 0, sizeof rom);
    rom[0] = 0xEF;
    rom[1] = 0xBE;
    rom[2] = 0xAD;
    rom[3] = 0xDE;
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, sizeof rom);
    CHECK_EQ(bus_read32(g, 0x08000000, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, 0x0A000000, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, 0x0C000000, false), 0xDEADBEEF);
    gba_destroy(g);
}

TEST(bus_bios_is_read_protected_outside_bios) {
    gba_t *g = gba_create(NULL);
    g->bus.bios[0] = 0x78;
    g->bus.bios[1] = 0x56;
    g->bus.bios[2] = 0x34;
    g->bus.bios[3] = 0x12;
    g->bus.bios_latch = 0xE129F000;
    g->bus.in_bios = false;
    CHECK_EQ(bus_read32(g, 0x00000000, false), 0xE129F000);
    CHECK_EQ(bus_read8(g, 0x00000001, false), 0xF0);
    g->bus.in_bios = true;
    CHECK_EQ(bus_read32(g, 0x00000000, false), 0x12345678);
    gba_destroy(g);
}

TEST(bus_sram_space_reads_ff_without_a_save_chip) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    gba_destroy(g);
}

TEST(bus_charges_region_wait_states) {
    gba_t *g = gba_create(NULL);
    uint64_t t = g->now;
    bus_read32(g, 0x02000000, false);
    CHECK_EQ(g->now - t, 6);
    t = g->now;
    bus_read16(g, 0x03000000, false);
    CHECK_EQ(g->now - t, 1);
    t = g->now; /* WAITCNT = 0: WS0 N = 4+1, S = 2+1 */
    bus_read16(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 5);
    t = g->now;
    bus_read16(g, 0x08000002, true);
    CHECK_EQ(g->now - t, 3);
    t = g->now;
    bus_read32(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 8);
    bus_set_waitcnt(g, 0x4317); /* SRAM 8, WS0 N 3 / S 1 */
    t = g->now;
    bus_read16(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 4);
    t = g->now;
    bus_read32(g, 0x08000004, true);
    CHECK_EQ(g->now - t, 4);
    t = g->now;
    bus_read8(g, 0x0E000000, false);
    CHECK_EQ(g->now - t, 9);
    gba_destroy(g);
}
