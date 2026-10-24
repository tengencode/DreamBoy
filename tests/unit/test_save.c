#include <string.h>

#include "test.h"
#include "testutil.h"

static gba_t *rom_with(const char *marker) {
    const char *list[1] = {marker};
    return tu_rom_markers(list, marker ? 1 : 0, 0x400);
}

TEST(save_detects_marker_strings) {
    static const struct {
        const char *marker;
        gba_save_type_t type;
    } cases[] = {{"EEPROM_V124", GBA_SAVE_EEPROM}, {"SRAM_V113", GBA_SAVE_SRAM},
                 {"SRAM_F_V100", GBA_SAVE_SRAM},   {"FLASH_V126", GBA_SAVE_FLASH64},
                 {"FLASH512_V131", GBA_SAVE_FLASH64}, {"FLASH1M_V103", GBA_SAVE_FLASH128},
                 {NULL, GBA_SAVE_UNKNOWN}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = rom_with(cases[i].marker);
        CHECK_EQ(gba_save_type(g), cases[i].type);
        gba_destroy(g);
    }
}

TEST(save_first_marker_in_the_rom_wins) {
    const char *flash_first[2] = {"FLASH_V126", "SRAM_V113"};
    const char *sram_first[2] = {"SRAM_V113", "FLASH_V126"};
    gba_t *g = tu_rom_markers(flash_first, 2, 0x400);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    gba_destroy(g);
    g = tu_rom_markers(sram_first, 2, 0x400);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    gba_destroy(g);
}

TEST(save_sram_repeats_bytes_and_mirrors) {
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0xFF); /* blank */
    bus_write8(g, 0x0E000010, 0x5A, false);
    CHECK_EQ(bus_read16(g, 0x0E000010, false), 0x5A5A);
    CHECK_EQ(bus_read32(g, 0x0E000010, false), 0x5A5A5A5A);
    CHECK_EQ(bus_read8(g, 0x0E008010, false), 0x5A); /* repeats every 32 KiB */
    CHECK_EQ(bus_read8(g, 0x0F000010, false), 0x5A); /* 0x0F mirrors 0x0E */
    CHECK_EQ(gba_saveram_size(g), 0x8000);
    CHECK_EQ(gba_saveram(g)[0x10], 0x5A);
    gba_destroy(g);
}

TEST(save_sram_wide_writes_store_one_byte) {
    gba_t *g = rom_with("SRAM_V113");
    const uint8_t *d = gba_saveram(g);
    bus_write16(g, 0x0E000021, 0xAABB, false); /* odd address: the high byte */
    CHECK_EQ(d[0x21], 0xAA);
    CHECK_EQ(d[0x20], 0xFF);
    bus_write16(g, 0x0E000030, 0xAABB, false);
    CHECK_EQ(d[0x30], 0xBB);
    CHECK_EQ(d[0x31], 0xFF);
    bus_write32(g, 0x0E000042, 0xAABBCCDD, false); /* byte 2 of the word */
    CHECK_EQ(d[0x42], 0xBB);
    CHECK_EQ(d[0x40], 0xFF);
    CHECK_EQ(d[0x43], 0xFF);
    gba_destroy(g);
}

TEST(save_none_reads_ff_and_ignores_writes) {
    gba_t *g = rom_with("SRAM_V113");
    gba_set_save_type(g, GBA_SAVE_NONE);
    bus_write8(g, 0x0E000000, 0x12, false);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    CHECK_EQ(gba_saveram_size(g), 0);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_NONE);
    gba_destroy(g);
}

TEST(save_unknown_becomes_sram_on_a_plain_write) {
    gba_t *g = rom_with(NULL);
    CHECK_EQ(bus_read8(g, 0x0E000123, false), 0xFF);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_UNKNOWN); /* reading does not decide */
    bus_write8(g, 0x0E000123, 0x77, false);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    CHECK_EQ(bus_read8(g, 0x0E000123, false), 0x77);
    gba_destroy(g);
}

/* mGBA adds 16 bytes of clock data to the saves of games with a clock (a 128 KiB Flash game: 0x20010 bytes):
 * the save itself loads, the footer is left out (our clock follows the phone's). */
TEST(save_load_saveram_takes_mgbas_saves_with_a_clock_footer) {
    static uint8_t file[0x20010];
    memset(file, 0x42, sizeof file);
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(gba_load_saveram(g, file, 0x8010), GBA_OK);
    CHECK_EQ(gba_saveram_size(g), 0x8000);
    gba_destroy(g);
    g = rom_with(NULL);
    CHECK_EQ(gba_load_saveram(g, file, 0x20010), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH128);
    gba_destroy(g);
}

TEST(save_load_saveram_checks_the_size) {
    static uint8_t file[0x20000];
    memset(file, 0x42, sizeof file);
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_OK);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x42);
    CHECK_EQ(gba_saveram_dirty(g), 0); /* loading is not saving */
    CHECK_EQ(gba_load_saveram(g, file, 0x10000), GBA_ERR_SAVE_SIZE);
    gba_destroy(g);

    g = rom_with(NULL); /* unknown: the file size decides */
    CHECK_EQ(gba_load_saveram(g, file, 0x10000), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    gba_destroy(g);

    g = rom_with("EEPROM_V124"); /* EEPROM of unknown size: only EEPROM sizes fit */
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_ERR_SAVE_SIZE);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM);
    CHECK_EQ(gba_load_saveram(g, file, 512), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    gba_destroy(g);

    g = rom_with("SRAM_V113");
    gba_set_save_type(g, GBA_SAVE_NONE);
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_ERR_SAVE_SIZE);
    gba_destroy(g);
}

TEST(save_dirty_flag_reports_once) {
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(gba_saveram_dirty(g), 0);
    bus_write8(g, 0x0E000000, 1, false);
    CHECK_EQ(gba_saveram_dirty(g), 1);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    gba_destroy(g);
}

TEST(save_set_type_blanks_the_chip) {
    gba_t *g = rom_with("SRAM_V113");
    bus_write8(g, 0x0E000000, 1, false);
    gba_set_save_type(g, GBA_SAVE_FLASH128);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH128);
    CHECK_EQ(gba_saveram_size(g), 0x20000);
    CHECK_EQ(gba_saveram(g)[0], 0xFF);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    gba_destroy(g);
}

TEST(save_survives_a_reset) {
    gba_t *g = rom_with("SRAM_V113");
    bus_write8(g, 0x0E000005, 0x99, false);
    gba_reset(g);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    CHECK_EQ(bus_read8(g, 0x0E000005, false), 0x99);
    gba_destroy(g);
}
