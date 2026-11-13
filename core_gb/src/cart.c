/* The cartridge: its header, and (cart_read / cart_write) the banking chips MBC1, MBC2, MBC3 and MBC5. */
#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"

#define MIN_ROM 0x150u /* the header ends at 0x14F */
#define MAX_ROM (8u * 1024 * 1024)

static uint32_t crc32_of(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

/* The header's cartridge type byte (0x147). False for a chip this core does not have. */
static bool read_type(uint8_t type, cart_info_t *c, bool *has_ram) {
    static const struct {
        uint8_t type, mbc, ram, battery, clock, rumble;
    } types[] = {
        {0x00, MBC_NONE, 0, 0, 0, 0}, {0x08, MBC_NONE, 1, 0, 0, 0}, {0x09, MBC_NONE, 1, 1, 0, 0},
        {0x01, MBC1, 0, 0, 0, 0},     {0x02, MBC1, 1, 0, 0, 0},     {0x03, MBC1, 1, 1, 0, 0},
        {0x05, MBC2, 1, 0, 0, 0},     {0x06, MBC2, 1, 1, 0, 0},     {0x0F, MBC3, 0, 1, 1, 0},
        {0x10, MBC3, 1, 1, 1, 0},     {0x11, MBC3, 0, 0, 0, 0},     {0x12, MBC3, 1, 0, 0, 0},
        {0x13, MBC3, 1, 1, 0, 0},     {0x19, MBC5, 0, 0, 0, 0},     {0x1A, MBC5, 1, 0, 0, 0},
        {0x1B, MBC5, 1, 1, 0, 0},     {0x1C, MBC5, 0, 0, 0, 1},     {0x1D, MBC5, 1, 0, 0, 1},
        {0x1E, MBC5, 1, 1, 0, 1},
    };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        if (types[i].type != type) continue;
        c->mbc = (mbc_t)types[i].mbc;
        *has_ram = types[i].ram;
        c->battery = types[i].battery;
        c->clock = types[i].clock;
        c->rumble = types[i].rumble;
        return true;
    }
    return false;
}

void gb_cart_free(gb_t *g) {
    free(g->info.rom);
    free(g->cart_ram);
    g->info.rom = NULL;
    g->cart_ram = NULL;
}

gb_err_t gb_cart_load(gb_t *g, const uint8_t *rom, size_t size) {
    static const uint32_t ram_sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000}; /* by header byte 0x149 */
    if (!rom || size < MIN_ROM || size > MAX_ROM) return GB_ERR_ROM_SIZE;
    cart_info_t c = {0};
    bool has_ram;
    if (!read_type(rom[0x147], &c, &has_ram)) return GB_ERR_UNSUPPORTED_CART;
    /* The file's own length decides the ROM's size (headers lie); banks wrap to it. */
    c.rom_size = 0x8000;
    while (c.rom_size < size) c.rom_size <<= 1;
    if (c.mbc == MBC2) c.ram_size = 512;
    else if (has_ram && rom[0x149] < sizeof ram_sizes / sizeof ram_sizes[0]) c.ram_size = ram_sizes[rom[0x149]];
    c.rom = malloc(c.rom_size);
    uint8_t *ram = c.ram_size ? malloc(c.ram_size) : NULL;
    if (!c.rom || (c.ram_size && !ram)) {
        free(c.rom);
        free(ram);
        return GB_ERR_NOMEM;
    }
    memcpy(c.rom, rom, size);
    memset(c.rom + size, 0xFF, c.rom_size - size);
    if (ram) memset(ram, 0xFF, c.ram_size);
    c.crc32 = crc32_of(rom, size);
    c.color = (rom[0x143] & 0x80) != 0;
    /* The title: up to 16 bytes, 15 when the last one is the colour flag; only printable ASCII is kept. */
    size_t n = 0;
    for (size_t i = 0x134; i < (c.color ? 0x143u : 0x144u) && rom[i]; i++)
        if (rom[i] >= 0x20 && rom[i] < 0x7F) c.title[n++] = (char)rom[i];
    gb_cart_free(g);
    g->info = c;
    g->cart_ram = ram;
    return GB_OK;
}
