#include "cart/cart.h"

#include <stdlib.h>
#include <string.h>

/* CRC-32 with the zlib polynomial (reflected 0xEDB88320), table-driven: a 32 MiB ROM takes a moment. */
static uint32_t crc32_of(const uint8_t *data, size_t size) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size) {
    if (size < CART_HEADER_SIZE || size > CART_MAX_ROM) return GBA_ERR_ROM_SIZE;
    size_t padded = (size + 3) & ~(size_t)3;
    uint8_t *rom = calloc(1, padded);
    if (!rom) return GBA_ERR_NOMEM;
    memcpy(rom, data, size);
    cart_unload(c);
    c->rom = rom;
    c->rom_size = (uint32_t)size;
    c->crc32 = crc32_of(data, size);
    memcpy(c->title, rom + 0xA0, 12);
    c->title[12] = '\0';
    memcpy(c->code, rom + 0xAC, 4);
    c->code[4] = '\0';
    save_setup(&c->save, rom, size);
    return GBA_OK;
}

void cart_unload(cart_t *c) {
    free(c->rom);
    memset(c, 0, sizeof(*c));
}

/* Past the end of the ROM, the cartridge bus returns the halfword address as data. */
static uint32_t oob_half(uint32_t offset) {
    return (offset >> 1) & 0xFFFF;
}

uint32_t cart_rom_read(const cart_t *c, uint32_t addr, unsigned size) {
    uint32_t off = addr & 0x01FFFFFFu;
    if (off < c->rom_size) {
        const uint8_t *p = c->rom + off;
        switch (size) {
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
        default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
    }
    switch (size) {
    case 1: return (oob_half(off & ~1u) >> ((off & 1) * 8)) & 0xFF;
    case 2: return oob_half(off);
    default: return oob_half(off) | oob_half(off + 2) << 16;
    }
}
