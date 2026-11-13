/* The Game Boy's memory as RetroAchievements addresses it (17c): rcheevos' layout for the Game Boy and the Game Boy
 * Color (third_party/rcheevos/src/rcheevos/consoleinfo.c) — the address space as it is, except that 0xA000 is always
 * cartridge RAM bank 0 and 0xD000 always work RAM bank 1; work RAM banks 2-7 follow at 0x10000 and cartridge RAM
 * banks 1-15 at 0x16000. */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

/* MBC5 with 32 KiB of RAM (4 banks), 64 KiB of ROM whose banks start with their number. */
static gb_t *machine(uint8_t cgb) {
    uint8_t *rom = gbtu_rom(0x10000, 0x1B, 3, cgb);
    for (int bank = 1; bank < 4; bank++) rom[bank * 0x4000] = (uint8_t)(0xB0 + bank);
    gbtu_fix_checksum(rom);
    gb_t *g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, 0x10000), GB_OK);
    free(rom);
    for (int bank = 0; bank < 4; bank++) g->cart_ram[bank * 0x2000 + 5] = (uint8_t)(0xA0 + bank);
    for (int bank = 0; bank < 8; bank++) g->mem.wram[bank * 0x1000 + 9] = (uint8_t)(0xC0 + bank);
    return g;
}

static int peek1(const gb_t *g, uint32_t address) { /* the byte, or -1: nothing there */
    uint8_t b = 0;
    return gb_ra_peek(g, address, &b, 1) == 1 ? b : -1;
}

TEST(gb_ra_rom_follows_the_mapped_bank) {
    gb_t *g = machine(0);
    CHECK_EQ(peek1(g, 0x0100), 0x00); /* the entry point's NOP */
    CHECK_EQ(peek1(g, 0x0147), 0x1B); /* the header's cartridge type */
    CHECK_EQ(peek1(g, 0x4000), 0xB1);
    gb_bus_write(g, 0x2000, 3);
    CHECK_EQ(peek1(g, 0x4000), 0xB3);
    gb_destroy(g);
}

TEST(gb_ra_cart_ram_is_bank0_and_work_ram_d000_is_bank1_whatever_is_mapped) {
    gb_t *g = machine(0x80);
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0x4000, 2);    /* cartridge RAM bank 2 */
    gb_bus_write(g, 0xFF70, 5);    /* work RAM bank 5 */
    CHECK_EQ(gb_bus_peek(g, 0xA005), 0xA2); /* what the game sees */
    CHECK_EQ(gb_bus_peek(g, 0xD009), 0xC5);
    CHECK_EQ(peek1(g, 0xA005), 0xA0); /* what RetroAchievements sees */
    CHECK_EQ(peek1(g, 0xC009), 0xC0);
    CHECK_EQ(peek1(g, 0xD009), 0xC1);
    CHECK_EQ(peek1(g, 0xE009), 0xC0); /* the echo */
    CHECK_EQ(peek1(g, 0xF009), 0xC1);
    gb_destroy(g);
}

TEST(gb_ra_the_extra_banks) {
    gb_t *g = machine(0x80);
    for (int bank = 2; bank < 8; bank++) CHECK_EQ(peek1(g, 0x10000 + (uint32_t)(bank - 2) * 0x1000 + 9), 0xC0 + bank);
    for (int bank = 1; bank < 4; bank++) CHECK_EQ(peek1(g, 0x16000 + (uint32_t)(bank - 1) * 0x2000 + 5), 0xA0 + bank);
    CHECK_EQ(peek1(g, 0x16000 + 3 * 0x2000), -1); /* bank 4: this cartridge has four */
    CHECK_EQ(peek1(g, 0x34000), -1);              /* past the layout */
    gb_destroy(g);
    g = machine(0x00); /* a plain Game Boy has no work RAM banks 2-7; its cartridge RAM banks are where they always are */
    CHECK_EQ(peek1(g, 0x10009), -1);
    CHECK_EQ(peek1(g, 0x15FFF), -1);
    CHECK_EQ(peek1(g, 0x16005), 0xA1);
    gb_destroy(g);
}

TEST(gb_ra_video_sprite_and_high_memory) {
    gb_t *g = machine(0x80);
    g->ppu.vram[0x0123] = 0x11;          /* bank 0 */
    g->ppu.vram[0x2000 + 0x0123] = 0x22; /* bank 1 */
    g->ppu.oam[0x10] = 0x33;
    g->mem.hram[0x05] = 0x44;
    g->mem.ie = 0x15;
    CHECK_EQ(peek1(g, 0x8123), 0x11);
    gb_bus_write(g, 0xFF4F, 1);
    CHECK_EQ(peek1(g, 0x8123), 0x22); /* the bank mapped now */
    CHECK_EQ(peek1(g, 0xFE10), 0x33);
    CHECK_EQ(peek1(g, 0xFEA0), 0x00); /* the unusable gap reads as 0 */
    CHECK_EQ(peek1(g, 0xFEFF), 0x00);
    CHECK_EQ(peek1(g, 0xFF85), 0x44);
    CHECK_EQ(peek1(g, 0xFFFF), 0x15);
    g->ppu.vram_busy = g->ppu.oam_busy = 1; /* while the screen is being drawn the CPU cannot read these: we still can */
    CHECK_EQ(peek1(g, 0x8123), 0x22);
    CHECK_EQ(peek1(g, 0xFE10), 0x33);
    gb_destroy(g);
}

TEST(gb_ra_reading_changes_nothing) {
    gb_t *g = machine(0x80);
    gbtu_frames(g, 3);
    size_t n = gb_state_size(g);
    uint8_t *before = malloc(n), *after = malloc(n), all[0x100];
    CHECK_EQ(gb_state_save(g, before, n), n);
    for (uint32_t a = 0; a < 0x34000; a += 0x100) gb_ra_peek(g, a, all, sizeof all); /* every address, the registers too */
    CHECK_EQ(gb_state_save(g, after, n), n);
    CHECK(memcmp(before, after, n) == 0);
    CHECK_EQ(peek1(g, 0xFF40), gb_bus_peek(g, 0xFF40)); /* a register reads as the game would read it */
    free(before);
    free(after);
    gb_destroy(g);
}

TEST(gb_ra_reads_stop_where_memory_ends) {
    gb_t *g = machine(0x80);
    uint8_t buf[16];
    memset(buf, 0xEE, sizeof buf);
    CHECK_EQ(gb_ra_peek(g, 0xBFFC, buf, 8), 8); /* across cartridge RAM into work RAM */
    CHECK_EQ(gb_ra_peek(g, 0x15FFC, buf, 8), 8); /* work RAM bank 7 into cartridge RAM bank 1 */
    CHECK_EQ(gb_ra_peek(g, 0x16000 + 3 * 0x2000 - 4, buf, 8), 4); /* the cartridge RAM ends */
    CHECK_EQ(gb_ra_peek(g, 0x33FFC, buf, 8), 0);
    gb_destroy(g);
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0x00); /* no chip, no cartridge RAM, a plain Game Boy */
    g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    free(rom);
    CHECK_EQ(gb_ra_peek(g, 0x9FFC, buf, 8), 4);  /* video RAM, then no cartridge RAM */
    CHECK_EQ(peek1(g, 0xA000), -1);
    CHECK_EQ(gb_ra_peek(g, 0xFFFC, buf, 8), 4);  /* up to 0xFFFF, then nothing on a plain Game Boy */
    CHECK_EQ(peek1(g, 0x16000), -1);
    gb_destroy(g);
    g = gb_create(); /* no game at all */
    CHECK_EQ(gb_ra_peek(g, 0x0000, buf, 8), 0);
    CHECK_EQ(gb_ra_peek(g, 0xC000, buf, 8), 8);
    gb_destroy(g);
}
