#include "gbtestutil.h"

#include <stdlib.h>
#include <string.h>

void gbtu_fix_checksum(uint8_t *rom) {
    uint8_t x = 0;
    for (int i = 0x134; i <= 0x14C; i++) x = (uint8_t)(x - rom[i] - 1);
    rom[0x14D] = x;
}

uint8_t *gbtu_rom(size_t size, uint8_t cart_type, uint8_t ram_code, uint8_t cgb_flag) {
    uint8_t *rom = calloc(1, size);
    static const uint8_t entry[] = {0x00, 0xC3, 0x50, 0x01};
    memcpy(rom + 0x100, entry, sizeof entry);
    memcpy(rom + 0x134, "TEST", 4);
    rom[0x143] = cgb_flag;
    rom[0x147] = cart_type;
    uint8_t code = 0;
    while ((0x8000u << code) < size) code++;
    rom[0x148] = code;
    rom[0x149] = ram_code;
    gbtu_fix_checksum(rom);
    return rom;
}

gb_t *gbtu_boot(const uint8_t *code, size_t n) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    memcpy(rom + 0x150, code, n);
    gb_t *g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    return g;
}
