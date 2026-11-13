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

gb_t *gbtu_idle(void) {
    static const uint8_t code[] = {0x18, 0xFE};
    return gbtu_boot(code, sizeof code);
}

void gbtu_tile(gb_t *g, int index, const char *const rows[8]) {
    for (int y = 0; y < 8; y++) {
        uint8_t lo = 0, hi = 0;
        for (int x = 0; x < 8; x++) {
            int c = rows[y][x] - '0';
            lo = (uint8_t)(lo << 1 | (c & 1));
            hi = (uint8_t)(hi << 1 | (c >> 1));
        }
        g->ppu.vram[index * 16 + y * 2] = lo;
        g->ppu.vram[index * 16 + y * 2 + 1] = hi;
    }
}

void gbtu_tile_fill(gb_t *g, int index, int colour) {
    for (int y = 0; y < 8; y++) {
        g->ppu.vram[index * 16 + y * 2] = (colour & 1) ? 0xFF : 0;
        g->ppu.vram[index * 16 + y * 2 + 1] = (colour & 2) ? 0xFF : 0;
    }
}

uint16_t gbtu_px(const gb_t *g, int x, int y) {
    return gb_framebuffer(g)[y * GB_WIDTH + x];
}

void gbtu_frames(gb_t *g, int n) {
    for (int i = 0; i < n; i++) gb_run_frame(g);
}

gb_t *gbtu_boot(const uint8_t *code, size_t n) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0);
    memcpy(rom + 0x150, code, n);
    gb_t *g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    return g;
}
