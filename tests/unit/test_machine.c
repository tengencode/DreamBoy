/* The layer over both cores. */
#include <stdlib.h>
#include <string.h>

#include "gba.h"
#include "gbtestutil.h"
#include "machine.h"
#include "test.h"

/* A 0x200-byte ROM with a valid Game Boy Advance header and "b ." at its start. */
static uint8_t *gba_rom(void) {
    uint8_t *rom = calloc(1, 0x200);
    static const uint8_t loop[] = {0xFE, 0xFF, 0xFF, 0xEA};
    memcpy(rom, loop, 4);
    memcpy(rom + 0xA0, "TESTGAME", 8);
    rom[0xB2] = 0x96;
    uint8_t sum = 0;
    for (int i = 0xA0; i <= 0xBC; i++) sum = (uint8_t)(sum - rom[i]);
    rom[0xBD] = (uint8_t)(sum - 0x19);
    return rom;
}

TEST(machine_detect_by_content) {
    uint8_t *rom = gba_rom();
    CHECK_EQ(machine_detect(rom, 0x200, "game.gb"), MACHINE_GBA);
    CHECK_EQ(machine_detect(rom, 0x200, NULL), MACHINE_GBA);
    free(rom);
    rom = gbtu_rom(0x8000, 0, 0, 0);
    CHECK_EQ(machine_detect(rom, 0x8000, "game.gba"), MACHINE_GB);
    CHECK_EQ(machine_detect(rom, 0x8000, "game.zip"), MACHINE_GB);
    free(rom);
    rom = gbtu_rom(0x10000, 0x19, 0, 0xC0);
    CHECK_EQ(machine_detect(rom, 0x10000, "game.gba"), MACHINE_GBC);
    free(rom);
}

TEST(machine_detect_by_name) {
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0);
    rom[0x14D] ^= 1; /* a wrong header checksum (homebrew, hacks): the name decides */
    CHECK_EQ(machine_detect(rom, 0x8000, "x.GB"), MACHINE_GB);
    CHECK_EQ(machine_detect(rom, 0x8000, "x.gbc"), MACHINE_GB); /* the colour flag still comes from the header */
    rom[0x143] = 0x80;
    CHECK_EQ(machine_detect(rom, 0x8000, "x.gb"), MACHINE_GBC);
    CHECK_EQ(machine_detect(rom, 0x8000, "x.gba"), MACHINE_GBA);
    CHECK_EQ(machine_detect(rom, 0x8000, "x.bin"), MACHINE_NONE);
    CHECK_EQ(machine_detect(rom, 0x8000, "gb"), MACHINE_NONE);
    CHECK_EQ(machine_detect(rom, 0x8000, NULL), MACHINE_NONE);
    CHECK_EQ(machine_detect(rom, 0, "x.gb"), MACHINE_GB); /* too short to be read: the core will refuse it */
    CHECK_EQ(machine_detect(NULL, 0, "x.gb"), MACHINE_NONE);
    free(rom);
    rom = gbtu_rom(0x8000, 0, 0, 0); /* a right checksum but a size byte that does not fit the file: the name */
    rom[0x148] = 3;
    gbtu_fix_checksum(rom);
    CHECK_EQ(machine_detect(rom, 0x8000, "x.gba"), MACHINE_GBA);
    free(rom);
}

TEST(machine_gb_runs) {
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0);
    machine_t *m = machine_create(MACHINE_GB);
    CHECK_EQ(machine_load_rom(m, rom, 0x8000), MACHINE_OK);
    CHECK_EQ(machine_kind(m), MACHINE_GB);
    CHECK_EQ(machine_width(m), 160);
    CHECK_EQ(machine_height(m), 144);
    CHECK(machine_gb(m) != NULL);
    CHECK(machine_gba(m) == NULL);
    machine_run_frame(m);
    CHECK(machine_framebuffer(m) != NULL);
    CHECK(!strcmp(machine_title(m), "TEST"));
    CHECK_EQ(machine_crc32(m), gb_rom_crc32(machine_gb(m)));
    rom[0x143] = 0x80;
    CHECK_EQ(machine_load_rom(m, rom, 0x8000), MACHINE_OK);
    CHECK_EQ(machine_kind(m), MACHINE_GBC);
    rom[0x147] = 0xFF;
    CHECK_EQ(machine_load_rom(m, rom, 0x8000), MACHINE_ERR_UNSUPPORTED);
    CHECK_EQ(machine_load_rom(m, rom, 0x10), MACHINE_ERR_ROM);
    machine_destroy(m);
    free(rom);
    CHECK(machine_create(MACHINE_NONE) == NULL);
    machine_destroy(NULL);
}

TEST(machine_gba_runs) {
    uint8_t *rom = gba_rom();
    machine_t *m = machine_create(MACHINE_GBA);
    CHECK_EQ(machine_load_rom(m, rom, 0x200), MACHINE_OK);
    CHECK_EQ(machine_kind(m), MACHINE_GBA);
    CHECK_EQ(machine_width(m), 240);
    CHECK_EQ(machine_height(m), 160);
    CHECK(machine_gba(m) != NULL);
    CHECK(machine_gb(m) == NULL);
    machine_run_frame(m);
    machine_run_frame(m);
    int16_t sound[2048 * 2];
    CHECK(machine_audio_read(m, sound, 2048) > 0);
    CHECK_EQ(machine_crc32(m), gba_rom_crc32((gba_t *)machine_gba(m)));
    size_t size = machine_state_size(m);
    uint8_t *state = malloc(size);
    CHECK_EQ(machine_state_save(m, state, size), size);
    CHECK_EQ(machine_state_load(m, state, size), MACHINE_OK);
    state[0] ^= 1;
    CHECK_EQ(machine_state_load(m, state, size), MACHINE_ERR_STATE);
    free(state);
    CHECK_EQ(machine_load_rom(m, rom, 0), MACHINE_ERR_ROM);
    machine_destroy(m);
    free(rom);
}

TEST(machine_keys_drop_lr) {
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0);
    machine_t *m = machine_create(MACHINE_GB);
    machine_load_rom(m, rom, 0x8000);
    free(rom);
    machine_set_keys(m, GBA_KEY_L | GBA_KEY_R | GBA_KEY_A | GBA_KEY_DOWN);
    gb_t *g = (gb_t *)machine_gb(m);
    CHECK_EQ(g->joypad.keys, GB_KEY_A | GB_KEY_DOWN);
    machine_destroy(m);
}

TEST(machine_gb_save) {
    uint8_t *rom = gbtu_rom(0x8000, 0x03, 2, 0);
    machine_t *m = machine_create(MACHINE_GB);
    machine_load_rom(m, rom, 0x8000);
    free(rom);
    CHECK_EQ(machine_save_size(m), 0x2000);
    static uint8_t save[0x2000], back[0x2000];
    for (int i = 0; i < 0x2000; i++) save[i] = (uint8_t)(i * 3);
    CHECK_EQ(machine_save_read(m, save, sizeof save), MACHINE_OK);
    CHECK_EQ(machine_save_write(m, back, sizeof back - 1), 0);
    CHECK_EQ(machine_save_write(m, back, sizeof back), 0x2000);
    CHECK(!memcmp(save, back, sizeof save));
    CHECK_EQ(machine_save_dirty(m), 0);
    machine_destroy(m);
}
