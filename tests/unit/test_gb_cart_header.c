#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

static gb_err_t load(uint8_t *rom, size_t size, gb_t **out) {
    gb_t *g = gb_create();
    gb_err_t err = gb_load_rom(g, rom, size);
    if (out) *out = g;
    else gb_destroy(g);
    return err;
}

TEST(gb_load_accepts_plain_32k) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    gb_t *g;
    CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
    CHECK_EQ(gb_is_color(g), 0);
    CHECK(!strcmp(gb_rom_title(g), "TEST"));
    gb_destroy(g);
    free(rom);
}

TEST(gb_load_color_flag) {
    static const uint8_t flags[] = {0x80, 0xC0, 0x00};
    for (int i = 0; i < 3; i++) {
        uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, flags[i]);
        gb_t *g;
        CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
        CHECK_EQ(gb_is_color(g), flags[i] ? 1 : 0);
        gb_destroy(g);
        free(rom);
    }
}

TEST(gb_load_rejects_sizes) {
    uint8_t *rom = gbtu_rom(8u * 1024 * 1024 + 1, 0x19, 0, 0);
    CHECK_EQ(load(rom, 0, NULL), GB_ERR_ROM_SIZE);
    CHECK_EQ(load(rom, 0x100, NULL), GB_ERR_ROM_SIZE);
    CHECK_EQ(load(rom, 8u * 1024 * 1024 + 1, NULL), GB_ERR_ROM_SIZE);
    CHECK_EQ(load(rom, 8u * 1024 * 1024, NULL), GB_OK);
    CHECK_EQ(load(NULL, 0x8000, NULL), GB_ERR_ROM_SIZE);
    free(rom);
}

TEST(gb_load_rejects_unknown_chip) {
    static const uint8_t bad[] = {0x20, 0x22, 0xFC, 0xFF, 0x0B, 0x04, 0x07, 0xFD, 0xFE};
    static const uint8_t good[] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0F, 0x10,
                                   0x11, 0x12, 0x13, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E};
    for (size_t i = 0; i < sizeof bad; i++) {
        uint8_t *rom = gbtu_rom(0x8000, bad[i], 0, 0);
        CHECK_EQ(load(rom, 0x8000, NULL), GB_ERR_UNSUPPORTED_CART);
        free(rom);
    }
    for (size_t i = 0; i < sizeof good; i++) {
        uint8_t *rom = gbtu_rom(0x8000, good[i], 2, 0);
        CHECK_EQ(load(rom, 0x8000, NULL), GB_OK);
        free(rom);
    }
}

TEST(gb_load_header_lies_about_size) {
    uint8_t *rom = gbtu_rom(0x8000, 0x1B, 4, 0);
    rom[0x148] = 8; /* "8 MiB" */
    gbtu_fix_checksum(rom);
    CHECK_EQ(load(rom, 0x8000, NULL), GB_OK);
    free(rom);
    rom = gbtu_rom(0xC000, 0x01, 0, 0); /* 48 KiB: not a power of two */
    CHECK_EQ(load(rom, 0xC000, NULL), GB_OK);
    free(rom);
}

TEST(gb_load_bad_ram_code) {
    uint8_t *rom = gbtu_rom(0x8000, 0x03, 0x07, 0);
    gb_t *g;
    CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
    CHECK_EQ(gb_save_size(g), 0);
    gb_destroy(g);
    free(rom);
}

TEST(gb_load_bad_checksum_ok) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    rom[0x14D] ^= 0x55;
    CHECK_EQ(load(rom, 0x8000, NULL), GB_OK);
    free(rom);
}

TEST(gb_crc_matches_zlib) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    for (int i = 0x150; i < 0x8000; i++) rom[i] = (uint8_t)(i * 7);
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 0x8000; i++) {
        crc ^= rom[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    gb_t *g;
    CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
    CHECK_EQ(gb_rom_crc32(g), (uint32_t)~crc);
    gb_destroy(g);
    free(rom);
}

TEST(gb_title_printable_only) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    static const uint8_t title[] = {'A', 0x01, 'B', 0x80, 0};
    memcpy(rom + 0x134, title, sizeof title);
    gb_t *g;
    CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
    CHECK(!strcmp(gb_rom_title(g), "AB"));
    gb_destroy(g);
    free(rom);
}

TEST(gb_load_twice_replaces) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0x80);
    gb_t *g;
    CHECK_EQ(load(rom, 0x8000, &g), GB_OK);
    rom[0x143] = 0;
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    CHECK_EQ(gb_is_color(g), 0);
    rom[0x147] = 0xFF; /* a refused ROM leaves the loaded one in place */
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_ERR_UNSUPPORTED_CART);
    CHECK(!strcmp(gb_rom_title(g), "TEST"));
    gb_destroy(g);
    free(rom);
}
