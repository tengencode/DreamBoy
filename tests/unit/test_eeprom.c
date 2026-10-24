#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "testutil.h"

static gba_t *eeprom_rom(size_t size) {
    const char *list[1] = {"EEPROM_V124"};
    return tu_rom_markers(list, 1, size);
}

/* DMA3, halfwords, immediate; runs it. */
static void dma3(gba_t *g, uint32_t src, uint32_t dst, uint32_t count) {
    tu_io32(g, 0x0D4, src);
    tu_io32(g, 0x0D8, dst);
    tu_io16(g, 0x0DC, (uint16_t)count);
    tu_io16(g, 0x0DE, 0x8000);
    tu_advance_to(g, g->now + 3); /* immediate DMA takes the bus 3 cycles after the write */
}

/* Sends a read (write = false) or write command, most significant bit first, with DMA3. */
static void ee_command(gba_t *g, bool write, unsigned addr_bits, uint32_t addr, uint64_t data) {
    uint16_t bits[2 + 14 + 64 + 1];
    uint32_t n = 0;
    bits[n++] = 1;
    bits[n++] = write ? 0 : 1;
    for (int i = (int)addr_bits - 1; i >= 0; i--) bits[n++] = (uint16_t)((addr >> i) & 1);
    if (write) {
        for (int i = 63; i >= 0; i--) bits[n++] = (uint16_t)((data >> i) & 1);
    }
    bits[n++] = 0;
    for (uint32_t i = 0; i < n; i++) bus_write16(g, 0x02000000 + i * 2, bits[i], false);
    dma3(g, 0x02000000, 0x0D000000, n);
}

/* Fetches a 68-bit reply with DMA3: 4 zero bits, then the 64 data bits. */
static uint64_t ee_reply(gba_t *g) {
    dma3(g, 0x0D000000, 0x02001000, 68);
    uint64_t v = 0;
    for (uint32_t i = 0; i < 68; i++) {
        uint16_t bit = bus_read16(g, 0x02001000 + i * 2, false) & 1;
        if (i < 4) CHECK_EQ(bit, 0);
        else v = v << 1 | bit;
    }
    return v;
}

TEST(eeprom_512_write_then_read) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, true, 6, 1, 0x0123456789ABCDEFull); /* 73 bits: a 512-byte chip */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    CHECK_EQ(gba_saveram(g)[8], 0x01);
    CHECK_EQ(gba_saveram(g)[15], 0xEF);
    CHECK_EQ(gba_saveram_dirty(g), 1);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 1); /* ready */
    ee_command(g, false, 6, 1, 0);
    CHECK_EQ(ee_reply(g), 0x0123456789ABCDEFull);
    gba_destroy(g);
}

TEST(eeprom_8k_uses_14_bit_addresses) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, true, 14, 1023, 0xA5A5A5A55A5A5A5Aull); /* 81 bits: an 8 KiB chip */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    CHECK_EQ(gba_saveram(g)[1023 * 8], 0xA5);
    ee_command(g, false, 14, 1023, 0);
    CHECK_EQ(ee_reply(g), 0xA5A5A5A55A5A5A5Aull);
    ee_command(g, false, 14, 5, 0);
    CHECK_EQ(ee_reply(g), 0xFFFFFFFFFFFFFFFFull); /* never written */
    gba_destroy(g);
}

TEST(eeprom_size_from_the_first_read_request) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, false, 14, 0, 0); /* 17 bits */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    gba_destroy(g);
    g = eeprom_rom(0x400);
    ee_command(g, false, 6, 0, 0); /* 9 bits */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    gba_destroy(g);
}

TEST(eeprom_cpu_first_assumes_8k) {
    gba_t *g = eeprom_rom(0x400);
    bus_write16(g, 0x0D000000, 1, false);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    gba_destroy(g);
}

TEST(eeprom_window_on_32_mib_roms) {
    gba_t *g = eeprom_rom(0x400);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 1); /* small ROM: all of 0x0D is EEPROM */
    gba_destroy(g);
    g = eeprom_rom(0x2000000);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 0x0000); /* ROM data (zeros) */
    CHECK_EQ(bus_read16(g, 0x0DFFFF00, false), 1);      /* the top 256 bytes are EEPROM */
    gba_destroy(g);
}

TEST(eeprom_unknown_becomes_eeprom_on_dma) {
    gba_t *g = tu_rom_markers(NULL, 0, 0x400);
    ee_command(g, true, 6, 2, 0x1122334455667788ull);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    CHECK_EQ(gba_saveram(g)[16], 0x11);
    gba_destroy(g);
}

TEST(eeprom_loaded_file_sets_the_size) {
    static uint8_t file[8192];
    memset(file, 0, sizeof file);
    file[8] = 0xC3;
    gba_t *g = eeprom_rom(0x400);
    CHECK_EQ(gba_load_saveram(g, file, sizeof file), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    ee_command(g, false, 14, 1, 0);
    CHECK_EQ(ee_reply(g) >> 56, 0xC3);
    gba_destroy(g);
}
