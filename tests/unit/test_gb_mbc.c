/* The cartridge chips. Each 16 KiB ROM bank starts with its own number (low byte, high byte). */
#include <stdlib.h>

#include "gbtestutil.h"
#include "test.h"

static gb_t *cart(size_t size, uint8_t type, uint8_t ram_code) {
    uint8_t *rom = gbtu_rom(size, type, ram_code, 0);
    for (size_t bank = 1; bank < size / 0x4000; bank++) {
        rom[bank * 0x4000] = (uint8_t)bank;
        rom[bank * 0x4000 + 1] = (uint8_t)(bank >> 8);
    }
    gb_t *g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, size), GB_OK);
    free(rom);
    return g;
}

static unsigned bank_at(const gb_t *g, uint16_t addr) {
    return gb_bus_peek(g, addr) | (unsigned)gb_bus_peek(g, (uint16_t)(addr + 1)) << 8;
}

TEST(gb_mbc1_banking) {
    gb_t *g = cart(2 * 1024 * 1024, 0x03, 3);
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 0x00); /* bank 0 is never at 0x4000 */
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 0x1F);
    CHECK_EQ(bank_at(g, 0x4000), 0x1F);
    gb_bus_write(g, 0x3FFF, 0xE5); /* only 5 bits */
    CHECK_EQ(bank_at(g, 0x4000), 0x05);
    gb_bus_write(g, 0x4000, 0x01); /* the two high bits */
    CHECK_EQ(bank_at(g, 0x4000), 0x25);
    gb_bus_write(g, 0x2000, 0x00); /* 0x20 cannot be chosen: the low 5 bits being 0 gives 0x21 */
    CHECK_EQ(bank_at(g, 0x4000), 0x21);
    CHECK_EQ(gb_bus_peek(g, 0x0000), 0x00);
    gb_bus_write(g, 0x6000, 0x01); /* mode 1: the high bits also move the 0x0000 area and pick the RAM bank */
    CHECK_EQ(bank_at(g, 0x0000), 0x20);
    gb_bus_write(g, 0x6000, 0x00);
    CHECK_EQ(gb_bus_peek(g, 0x0000), 0x00);

    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF); /* RAM is off until 0x0A is written */
    gb_bus_write(g, 0xA000, 0x55);
    gb_bus_write(g, 0x0000, 0x0A);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF); /* the write while off was ignored (fresh RAM is 0xFF) */
    gb_bus_write(g, 0xA000, 0x55);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x55);
    gb_bus_write(g, 0x6000, 0x01); /* RAM bank 1 */
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF);
    gb_bus_write(g, 0xBFFF, 0x66);
    gb_bus_write(g, 0x6000, 0x00);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x55);
    CHECK_EQ(gb_bus_peek(g, 0xBFFF), 0xFF);
    gb_bus_write(g, 0x1FFF, 0x1A); /* any value whose low half is 0xA switches RAM on */
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x55);
    gb_bus_write(g, 0x0000, 0x0B);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF);
    gb_destroy(g);

    g = cart(0x10000, 0x01, 0); /* 4 banks: numbers wrap to the ROM's size */
    gb_bus_write(g, 0x2000, 0x05);
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 0x13);
    CHECK_EQ(bank_at(g, 0x4000), 3);
    gb_destroy(g);
}

TEST(gb_mbc2_ram) {
    gb_t *g = cart(0x40000, 0x06, 0);
    CHECK_EQ(gb_save_size(g), 512);
    gb_bus_write(g, 0x0000, 0x0A); /* address bit 8 clear: RAM on/off */
    gb_bus_write(g, 0xA000, 0xAB);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFB); /* half bytes: the top half reads 1s */
    CHECK_EQ(gb_bus_peek(g, 0xA200), 0xFB); /* 512 of them, repeated through the whole area */
    CHECK_EQ(gb_bus_peek(g, 0xBE00), 0xFB);
    gb_bus_write(g, 0xA1FF, 0x04);
    CHECK_EQ(gb_bus_peek(g, 0xBFFF), 0xF4);
    gb_bus_write(g, 0x0100, 0x03); /* address bit 8 set: the ROM bank */
    CHECK_EQ(bank_at(g, 0x4000), 3);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFB);
    gb_bus_write(g, 0x2100, 0x00);
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x3EFF, 0x00); /* bit 8 clear, not 0x0A: RAM off */
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF);
    gb_destroy(g);
}

TEST(gb_mbc3_banking) {
    gb_t *g = cart(2 * 1024 * 1024, 0x13, 3);
    gb_bus_write(g, 0x2000, 0x00);
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 0x7F);
    CHECK_EQ(bank_at(g, 0x4000), 0x7F);
    gb_bus_write(g, 0x2000, 0x80); /* 7 bits */
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x0000, 0x0A);
    for (int bank = 0; bank < 4; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        gb_bus_write(g, 0xA123, (uint8_t)(0x40 + bank));
    }
    for (int bank = 0; bank < 4; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        CHECK_EQ(gb_bus_peek(g, 0xA123), 0x40 + bank);
    }
    gb_destroy(g);

    g = cart(4 * 1024 * 1024, 0x13, 5); /* the bigger variant: 8 ROM-bank bits, 8 RAM banks */
    gb_bus_write(g, 0x2000, 0xFF);
    CHECK_EQ(bank_at(g, 0x4000), 0xFF);
    gb_bus_write(g, 0x0000, 0x0A);
    for (int bank = 0; bank < 8; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        gb_bus_write(g, 0xA000, (uint8_t)(0x40 + bank));
    }
    for (int bank = 0; bank < 8; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        CHECK_EQ(gb_bus_peek(g, 0xA000), 0x40 + bank);
    }
    gb_destroy(g);
}

TEST(gb_mbc5_banking) {
    gb_t *g = cart(8 * 1024 * 1024, 0x1B, 4);
    CHECK_EQ(bank_at(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 0x00); /* here bank 0 really is bank 0 */
    CHECK_EQ(bank_at(g, 0x4000), 0);
    gb_bus_write(g, 0x2FFF, 0xFF);
    CHECK_EQ(bank_at(g, 0x4000), 0xFF);
    gb_bus_write(g, 0x3000, 0x01); /* bit 8 */
    CHECK_EQ(bank_at(g, 0x4000), 0x1FF);
    gb_bus_write(g, 0x2000, 0x23);
    CHECK_EQ(bank_at(g, 0x4000), 0x123);
    gb_bus_write(g, 0x0000, 0x0A);
    for (int bank = 0; bank < 16; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        gb_bus_write(g, 0xB000, (uint8_t)(0x40 + bank));
    }
    for (int bank = 0; bank < 16; bank++) {
        gb_bus_write(g, 0x4000, (uint8_t)bank);
        CHECK_EQ(gb_bus_peek(g, 0xB000), 0x40 + bank);
    }
    gb_bus_write(g, 0x0000, 0x1A); /* this chip wants exactly 0x0A */
    CHECK_EQ(gb_bus_peek(g, 0xB000), 0xFF);
    gb_destroy(g);

    g = cart(0x10000, 0x1E, 3); /* with a rumble motor: RAM-bank bit 3 drives the motor, not the bank */
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0x4000, 0x00);
    gb_bus_write(g, 0xA000, 0x77);
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x77);
    gb_destroy(g);
}

TEST(gb_nochip_ram) {
    gb_t *g = cart(0x8000, 0x09, 2);
    CHECK_EQ(gb_save_size(g), 0x2000);
    gb_bus_write(g, 0xA000, 0x12); /* no switch: always on */
    gb_bus_write(g, 0xBFFF, 0x34);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x12);
    CHECK_EQ(gb_bus_peek(g, 0xBFFF), 0x34);
    gb_destroy(g);
}

TEST(gb_cart_no_ram_reads_ff) {
    gb_t *g = cart(0x8000, 0x01, 0);
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0xA000, 0x12);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF);
    CHECK_EQ(gb_save_size(g), 0);
    gb_destroy(g);
    g = cart(0x8000, 0x02, 2); /* RAM without a battery: nothing to save */
    CHECK_EQ(gb_save_size(g), 0);
    gb_destroy(g);
}

TEST(gb_cart_dirty_flag) {
    gb_t *g = cart(0x8000, 0x03, 2);
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_bus_write(g, 0xA000, 0x12); /* RAM off: nothing was written */
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0xA000, 0x12);
    CHECK_EQ(gb_save_dirty(g), 1);
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_destroy(g);
}

/* Review focus: a header that lies must not send any access outside the ROM or RAM. Run under the sanitizer. */
TEST(gb_cart_lying_header_safe) {
    static const uint8_t types[] = {0x00, 0x03, 0x06, 0x10, 0x13, 0x1B, 0x1E, 0x09};
    for (size_t t = 0; t < sizeof types; t++) {
        for (int ram_code = 0; ram_code <= 5; ram_code++) {
            uint8_t *rom = gbtu_rom(0x8000, types[t], (uint8_t)ram_code, 0);
            rom[0x148] = 8; /* "8 MiB" in a 32 KiB file */
            gbtu_fix_checksum(rom);
            gb_t *g = gb_create();
            CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
            free(rom);
            unsigned sum = 0;
            for (int reg = 0; reg < 0x8000; reg += 0x800) {
                for (int v = 0; v < 256; v += 5) {
                    gb_bus_write(g, (uint16_t)reg, (uint8_t)v);
                    gb_bus_write(g, 0x0000, 0x0A);
                    gb_bus_write(g, 0xA000, 1), gb_bus_write(g, 0xBFFF, 2);
                    sum += gb_bus_peek(g, 0x0000) + gb_bus_peek(g, 0x3FFF) + gb_bus_peek(g, 0x4000) +
                           gb_bus_peek(g, 0x7FFF) + gb_bus_peek(g, 0xA000) + gb_bus_peek(g, 0xBFFF);
                }
            }
            CHECK(sum > 0);
            gb_destroy(g);
        }
    }
}

TEST(gb_mbc5_rumble_follows_the_motor_bit) { /* 17c */
    gb_t *g = cart(0x10000, 0x1C, 0); /* MBC5 + rumble */
    CHECK_EQ(gb_has_rumble(g), 1);
    CHECK_EQ(gb_rumble_active(g), 0);
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_rumble_active(g), 1);
    size_t n = gb_state_size(g);
    void *state = malloc(n);
    CHECK_EQ(gb_state_save(g, state, n), n);
    gb_bus_write(g, 0x4000, 0x00);
    CHECK_EQ(gb_rumble_active(g), 0);
    CHECK_EQ(gb_state_load(g, state, n), GB_OK); /* the motor is as the state left it */
    CHECK_EQ(gb_rumble_active(g), 1);
    gb_reset(g);
    CHECK_EQ(gb_rumble_active(g), 0);
    free(state);
    gb_destroy(g);
    g = cart(0x10000, 0x1B, 2); /* a plain MBC5: bit 3 is part of the RAM bank, no motor */
    CHECK_EQ(gb_has_rumble(g), 0);
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_rumble_active(g), 0);
    gb_destroy(g);
}
