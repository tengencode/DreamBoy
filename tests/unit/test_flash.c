#include "test.h"
#include "testutil.h"

static gba_t *flash_rom(const char *marker) {
    const char *list[1] = {marker};
    return tu_rom_markers(list, 1, 0x400);
}

static void flash_cmd(gba_t *g, uint8_t cmd) {
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAA, 0x55, false);
    bus_write8(g, 0x0E005555, cmd, false);
}

static void flash_byte(gba_t *g, uint32_t addr, uint8_t value) {
    flash_cmd(g, 0xA0);
    bus_write8(g, addr, value, false);
}

TEST(flash_id_mode_reports_the_chip) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_cmd(g, 0x90);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x32); /* Panasonic */
    CHECK_EQ(bus_read8(g, 0x0E000001, false), 0x1B);
    flash_cmd(g, 0xF0);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    gba_destroy(g);
    g = flash_rom("FLASH1M_V103");
    flash_cmd(g, 0x90);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x62); /* Sanyo */
    CHECK_EQ(bus_read8(g, 0x0E000001, false), 0x13);
    gba_destroy(g);
}

TEST(flash_writes_need_the_command) {
    gba_t *g = flash_rom("FLASH_V126");
    bus_write8(g, 0x0E000100, 0x12, false); /* no command: ignored */
    CHECK_EQ(bus_read8(g, 0x0E000100, false), 0xFF);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    flash_byte(g, 0x0E000100, 0x12);
    CHECK_EQ(bus_read8(g, 0x0E000100, false), 0x12);
    CHECK_EQ(bus_read16(g, 0x0E000100, false), 0x1212);
    CHECK_EQ(bus_read8(g, 0x0E010100, false), 0x12); /* repeats every 64 KiB */
    CHECK_EQ(gba_saveram_dirty(g), 1);
    bus_write8(g, 0x0E000101, 0x34, false); /* one byte per command */
    CHECK_EQ(bus_read8(g, 0x0E000101, false), 0xFF);
    gba_destroy(g);
}

TEST(flash_chip_and_sector_erase) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_byte(g, 0x0E000010, 0x00);
    flash_byte(g, 0x0E001010, 0x00);
    flash_cmd(g, 0x80);
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAA, 0x55, false);
    bus_write8(g, 0x0E001000, 0x30, false); /* erase sector 1 */
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0x00);
    CHECK_EQ(bus_read8(g, 0x0E001010, false), 0xFF);
    flash_cmd(g, 0x80);
    flash_cmd(g, 0x10); /* erase chip */
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0xFF);
    gba_destroy(g);
}

TEST(flash_bank_switch_on_128k) {
    gba_t *g = flash_rom("FLASH1M_V103");
    flash_byte(g, 0x0E000020, 0x01);
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 1, false); /* bank 1 */
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0xFF);
    flash_byte(g, 0x0E000020, 0x02);
    CHECK_EQ(gba_saveram(g)[0x10020], 0x02);
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 0, false);
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0x01);
    gba_destroy(g);
}

TEST(flash_64k_ignores_bank_switch) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 1, false);
    flash_byte(g, 0x0E000020, 0x02);
    CHECK_EQ(gba_saveram(g)[0x20], 0x02);
    gba_destroy(g);
}

TEST(flash_broken_sequence_starts_over) {
    gba_t *g = flash_rom("FLASH_V126");
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAB, 0x55, false); /* wrong address */
    bus_write8(g, 0x0E005555, 0xA0, false);
    bus_write8(g, 0x0E000000, 0x12, false);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    flash_byte(g, 0x0E000000, 0x12); /* a clean sequence still works */
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x12);
    gba_destroy(g);
}

TEST(flash_unknown_becomes_flash_on_the_command_prefix) {
    gba_t *g = tu_rom_markers(NULL, 0, 0x400);
    flash_byte(g, 0x0E000005, 0x66);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    CHECK_EQ(bus_read8(g, 0x0E000005, false), 0x66);
    gba_destroy(g);
}
