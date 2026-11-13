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
    memset(&g->cart.rtc, 0, sizeof g->cart.rtc); /* a new cartridge: its clock starts at day 0, 00:00:00 */
    return GB_OK;
}

/* Works out where the visible banks start, wrapped to the real sizes. */
static void map(gb_t *g) {
    gb_cart_t *c = &g->cart;
    uint32_t bank0 = 0, bank1 = 1, ram = 0;
    switch (g->info.mbc) {
    case MBC_NONE: break;
    case MBC1:
        bank1 = (uint32_t)(c->bank_hi << 5 | (c->bank_lo ? c->bank_lo : 1));
        if (c->mode) bank0 = (uint32_t)c->bank_hi << 5, ram = c->bank_hi;
        break;
    case MBC2:
    case MBC3:
        bank1 = c->bank_lo ? c->bank_lo : 1;
        ram = c->ram_bank & 7;
        break;
    case MBC5:
        bank1 = (uint32_t)(c->bank_hi << 8 | c->bank_lo);
        ram = c->ram_bank & (g->info.rumble ? 0x07 : 0x0F);
        break;
    }
    c->rom0 = (bank0 * 0x4000) & (g->info.rom_size - 1);
    c->rom1 = (bank1 * 0x4000) & (g->info.rom_size - 1);
    c->ram_at = g->info.ram_size ? (ram * 0x2000) & (g->info.ram_size - 1) : 0;
}

/* A reset is not a new cartridge: the clock keeps running through it. */
void gb_cart_remap(gb_t *g) {
    map(g);
}

void gb_cart_reset(gb_t *g) {
    gb_rtc_t rtc;
    memcpy(&rtc, &g->cart.rtc, sizeof rtc); /* (memcpy: the padding too, so save states stay byte-exact) */
    memset(&g->cart, 0, sizeof g->cart);
    memcpy(&g->cart.rtc, &rtc, sizeof rtc);
    g->cart.bank_lo = 1;
    g->cart.ram_on = g->info.mbc == MBC_NONE; /* without a chip there is no switch */
    map(g);
}

/* Where addr (0xA000-0xBFFF) lands in the cartridge RAM. */
static uint32_t ram_index(const gb_t *g, uint16_t addr) {
    return (g->cart.ram_at + (addr & 0x1FFF)) & (g->info.ram_size - 1);
}

uint8_t gb_cart_read(const gb_t *g, uint16_t addr) {
    const gb_cart_t *c = &g->cart;
    if (addr < 0x4000) return g->info.rom[c->rom0 + addr];
    if (addr < 0x8000) return g->info.rom[c->rom1 + (addr & 0x3FFF)];
    if (!c->ram_on) return 0xFF;
    if (g->info.mbc == MBC3 && c->ram_bank >= 8)
        return g->info.clock && c->ram_bank <= 0x0C ? gb_rtc_read(g, c->ram_bank) : 0xFF;
    if (!g->info.ram_size) return 0xFF;
    uint8_t v = g->cart_ram[ram_index(g, addr)];
    return g->info.mbc == MBC2 ? v | 0xF0 : v;
}

void gb_cart_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_cart_t *c = &g->cart;
    if (addr >= 0x8000) {
        if (!c->ram_on) return;
        if (g->info.mbc == MBC3 && c->ram_bank >= 8) {
            if (g->info.clock && c->ram_bank <= 0x0C) gb_rtc_write(g, c->ram_bank, value);
            return;
        }
        if (!g->info.ram_size) return;
        g->cart_ram[ram_index(g, addr)] = value;
        c->dirty = 1;
        return;
    }
    switch (g->info.mbc) {
    case MBC_NONE: return;
    case MBC1:
        if (addr < 0x2000) c->ram_on = (value & 0x0F) == 0x0A;
        else if (addr < 0x4000) c->bank_lo = value & 0x1F;
        else if (addr < 0x6000) c->bank_hi = value & 0x03;
        else c->mode = value & 1;
        break;
    case MBC2:
        if (addr >= 0x4000) return;
        if (addr & 0x100) c->bank_lo = value & 0x0F;
        else c->ram_on = (value & 0x0F) == 0x0A;
        break;
    case MBC3:
        if (addr < 0x2000) c->ram_on = (value & 0x0F) == 0x0A;
        else if (addr < 0x4000) c->bank_lo = value & (g->info.rom_size > 0x200000 ? 0xFF : 0x7F);
        else if (addr < 0x6000) c->ram_bank = value & 0x0F;
        else if (g->info.clock) gb_rtc_latch(g, value);
        break;
    case MBC5:
        if (addr < 0x2000) c->ram_on = value == 0x0A;
        else if (addr < 0x3000) c->bank_lo = value;
        else if (addr < 0x4000) c->bank_hi = value & 1;
        else if (addr < 0x6000) c->ram_bank = value & 0x0F;
        break;
    }
    map(g);
}

int gb_has_rumble(const gb_t *g) {
    return g->info.rumble != 0;
}

int gb_rumble_active(const gb_t *g) { /* the motor is bit 3 of what the game last wrote as its RAM bank */
    return g->info.rumble && (g->cart.ram_bank & 0x08) != 0;
}

void gb_cart_poke(gb_t *g, int bank, uint16_t addr, uint8_t value) {
    if (!g->info.ram_size) return;
    uint32_t at;
    if (bank < 0) {
        if (g->info.mbc == MBC3 && g->cart.ram_bank >= 8) return; /* a clock register is mapped there now */
        at = ram_index(g, addr);
    } else {
        at = (uint32_t)bank * 0x2000 + (addr & 0x1FFF);
        if (at >= g->info.ram_size) return;
    }
    g->cart_ram[at] = value;
    g->cart.dirty = 1;
}
