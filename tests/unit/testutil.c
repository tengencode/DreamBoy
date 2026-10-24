#include "testutil.h"

#include <stdlib.h>
#include <string.h>

static gba_t *boot(const uint8_t *code, size_t len) {
    size_t size = len < 0x200 ? 0x200 : (len + 3) & ~(size_t)3;
    uint8_t *rom = calloc(1, size);
    memcpy(rom, code, len);
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, size);
    free(rom);
    return g;
}

gba_t *tu_arm(const uint32_t *ops, size_t n) {
    uint8_t *code = malloc(n * 4);
    for (size_t i = 0; i < n; i++) {
        code[i * 4 + 0] = (uint8_t)ops[i];
        code[i * 4 + 1] = (uint8_t)(ops[i] >> 8);
        code[i * 4 + 2] = (uint8_t)(ops[i] >> 16);
        code[i * 4 + 3] = (uint8_t)(ops[i] >> 24);
    }
    gba_t *g = boot(code, n * 4);
    free(code);
    return g;
}

gba_t *tu_thumb(const uint16_t *ops, size_t n) {
    uint8_t *code = malloc(n * 2);
    for (size_t i = 0; i < n; i++) {
        code[i * 2 + 0] = (uint8_t)ops[i];
        code[i * 2 + 1] = (uint8_t)(ops[i] >> 8);
    }
    gba_t *g = boot(code, n * 2);
    free(code);
    g->cpu.cpsr |= CPSR_T;
    return g;
}

void tu_steps(gba_t *g, int n) {
    for (int i = 0; i < n; i++) cpu_step(g);
}

void tu_io16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x04000000 + off, value, false);
}

void tu_io32(gba_t *g, uint32_t off, uint32_t value) {
    bus_write32(g, 0x04000000 + off, value, false);
}

uint16_t tu_raw16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

void tu_pal(gba_t *g, uint32_t index, uint16_t color) {
    bus_write16(g, 0x05000000 + index * 2, color, false);
}

void tu_vram16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x06000000 + off, value, false);
}

void tu_oam16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x07000000 + off, value, false);
}

void tu_hide_sprites(gba_t *g) {
    for (uint32_t i = 0; i < 128; i++) tu_oam16(g, i * 8, 0x0200);
}

uint16_t tu_px(const gba_t *g, int x, int y) {
    return g->ppu.framebuffer[y * PPU_WIDTH + x];
}

void tu_advance_to(gba_t *g, uint64_t t) {
    g->now = t;
    gba_run_events(g);
}

gba_t *tu_rom_markers(const char *const *markers, size_t count, size_t size) {
    uint8_t *rom = calloc(1, size);
    rom[0] = 0xFE; /* b . */
    rom[1] = 0xFF;
    rom[2] = 0xFF;
    rom[3] = 0xEA;
    for (size_t i = 0; i < count; i++) memcpy(rom + 0x100 + i * 0x40, markers[i], strlen(markers[i]));
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, size);
    free(rom);
    return g;
}

gba_t *tu_rom_code(const char *code, const char *marker) {
    uint8_t *rom = calloc(1, 0x400);
    rom[0] = 0xFE; /* b . */
    rom[1] = 0xFF;
    rom[2] = 0xFF;
    rom[3] = 0xEA;
    memcpy(rom + 0xAC, code, 4);
    memset(rom + 0xC4, 0xEE, 6);
    if (marker) memcpy(rom + 0x100, marker, strlen(marker));
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, 0x400);
    free(rom);
    return g;
}
