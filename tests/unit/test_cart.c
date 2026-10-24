#include <string.h>

#include "gba_internal.h"
#include "test.h"

static uint8_t rom[0x200];

static void make_rom(void) {
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0xA0, "GBA TESTS\0\0\0", 12);
    memcpy(rom + 0xAC, "1337", 4);
    rom[0x10] = 0x11;
    rom[0x11] = 0x22;
    rom[0x12] = 0x33;
    rom[0x13] = 0x44;
}

TEST(cart_rejects_bad_sizes) {
    cart_t c = {0};
    make_rom();
    CHECK_EQ(cart_load(&c, rom, 0), GBA_ERR_ROM_SIZE);
    CHECK_EQ(cart_load(&c, rom, 0xBF), GBA_ERR_ROM_SIZE);
    CHECK_EQ(cart_load(&c, rom, CART_MAX_ROM + 1), GBA_ERR_ROM_SIZE);
    CHECK(c.rom == NULL);
}

TEST(cart_parses_header_title_and_code) {
    cart_t c = {0};
    make_rom();
    CHECK_EQ(cart_load(&c, rom, sizeof rom), GBA_OK);
    CHECK(strcmp(c.title, "GBA TESTS") == 0);
    CHECK(strcmp(c.code, "1337") == 0);
    cart_unload(&c);
}

TEST(cart_reads_little_endian_in_every_mirror) {
    cart_t c = {0};
    make_rom();
    cart_load(&c, rom, sizeof rom);
    CHECK_EQ(cart_rom_read(&c, 0x08000010, 4), 0x44332211);
    CHECK_EQ(cart_rom_read(&c, 0x08000012, 2), 0x4433);
    CHECK_EQ(cart_rom_read(&c, 0x08000011, 1), 0x22);
    CHECK_EQ(cart_rom_read(&c, 0x0A000010, 4), 0x44332211);
    CHECK_EQ(cart_rom_read(&c, 0x0C000010, 4), 0x44332211);
    cart_unload(&c);
}

TEST(cart_out_of_bounds_returns_address_pattern) {
    cart_t c = {0};
    make_rom();
    cart_load(&c, rom, sizeof rom);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 2), 0x0100);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 4), 0x01010100);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 1), 0x00);
    CHECK_EQ(cart_rom_read(&c, 0x08000201, 1), 0x01);
    cart_unload(&c);
}

TEST(gba_load_rom_exposes_header) {
    gba_t *g = gba_create(NULL);
    make_rom();
    CHECK_EQ(gba_load_rom(g, rom, sizeof rom), GBA_OK);
    CHECK(strcmp(gba_rom_title(g), "GBA TESTS") == 0);
    CHECK(strcmp(gba_rom_code(g), "1337") == 0);
    gba_destroy(g);
}
