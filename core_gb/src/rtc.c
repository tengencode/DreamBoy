/* The MBC3 cartridge clock, and the save file: the cartridge RAM followed, for a clock cartridge, by the 48-byte
 * footer other emulators use (the five running registers, the five latched ones, 4 bytes each, then a 64-bit
 * Unix time, all little-endian; an older 44-byte form has a 32-bit time).
 *
 * The clock's registers belong to the game: it sets them, halts them, reads them through a latch. The frontend
 * only says what time it is now; the registers move on by the real seconds that passed since it last said so. */
#include <string.h>

#include "gb_internal.h"

#define FOOTER 48u

enum { SEC, MIN, HOUR, DAY_LOW, DAY_HIGH }; /* DAY_HIGH: bit 0 day bit 8, bit 6 halt, bit 7 day counter wrapped */
static const uint8_t MASK[5] = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};

static void advance(gb_rtc_t *r, int64_t seconds) {
    if (seconds <= 0 || (r->reg[DAY_HIGH] & 0x40)) return;
    int64_t day = r->reg[DAY_LOW] | (r->reg[DAY_HIGH] & 1) << 8;
    /* (values a game set past 59 or 23 simply count as that many seconds) */
    int64_t total = r->reg[SEC] + r->reg[MIN] * 60 + r->reg[HOUR] * 3600 + seconds % 86400;
    day += seconds / 86400 + total / 86400;
    total %= 86400;
    r->reg[SEC] = (uint8_t)(total % 60);
    r->reg[MIN] = (uint8_t)(total / 60 % 60);
    r->reg[HOUR] = (uint8_t)(total / 3600);
    r->reg[DAY_LOW] = (uint8_t)day;
    r->reg[DAY_HIGH] = (uint8_t)((r->reg[DAY_HIGH] & 0xC0) | (day >> 8 & 1) | (day > 511 ? 0x80 : 0));
}

void gb_set_rtc(gb_t *g, int64_t unix_seconds) {
    gb_rtc_t *r = &g->cart.rtc;
    if (!g->info.clock) return;
    /* A clock that went backwards, or made a jump no clock makes (over 100 years), adds nothing. The difference
     * is taken unsigned: the two times may be any values at all. */
    uint64_t passed = (uint64_t)unix_seconds - (uint64_t)r->last;
    if (r->has_time && unix_seconds > r->last && passed < 100ull * 366 * 86400) advance(r, (int64_t)passed);
    r->last = unix_seconds;
    r->has_time = 1;
}

uint8_t gb_rtc_read(const gb_t *g, uint8_t reg) {
    return g->cart.rtc.latched[reg - 8];
}

void gb_rtc_write(gb_t *g, uint8_t reg, uint8_t value) {
    g->cart.rtc.reg[reg - 8] = value & MASK[reg - 8];
    g->cart.dirty = 1;
}

/* A write to 0x6000-0x7FFF: 0 then 1 copies the running registers to the ones the game reads. */
void gb_rtc_latch(gb_t *g, uint8_t value) {
    gb_rtc_t *r = &g->cart.rtc;
    if (r->latch_armed && value == 1) memcpy(r->latched, r->reg, sizeof r->latched);
    r->latch_armed = value == 0;
}

size_t gb_save_size(const gb_t *g) {
    return g->info.battery ? g->info.ram_size + (g->info.clock ? FOOTER : 0) : 0;
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}

size_t gb_save_write(const gb_t *g, uint8_t *buf, size_t cap) {
    size_t size = gb_save_size(g), ram = g->info.ram_size;
    if (!size || size > cap) return 0;
    if (ram) memcpy(buf, g->cart_ram, ram);
    if (g->info.clock) {
        const gb_rtc_t *r = &g->cart.rtc;
        for (int i = 0; i < 5; i++) {
            put32(buf + ram + i * 4, r->reg[i]);
            put32(buf + ram + 20 + i * 4, r->latched[i]);
        }
        put32(buf + ram + 40, (uint32_t)r->last);
        put32(buf + ram + 44, (uint32_t)((uint64_t)r->last >> 32));
    }
    return size;
}

void gb_save_read(gb_t *g, const uint8_t *data, size_t size) {
    size_t ram = g->info.battery ? g->info.ram_size : 0;
    if (!gb_save_size(g)) return;
    if (!data) size = 0;
    size_t n = size < ram ? size : ram;
    if (ram) memset(g->cart_ram, 0xFF, ram);
    if (n) memcpy(g->cart_ram, data, n);
    if (!g->info.clock || size < ram + 44) return; /* no whole footer */
    gb_rtc_t *r = &g->cart.rtc;
    const uint8_t *f = data + ram;
    for (int i = 0; i < 5; i++) {
        r->reg[i] = f[i * 4] & MASK[i];
        r->latched[i] = f[20 + i * 4] & MASK[i];
    }
    uint64_t stamp = 0;
    for (int i = 0; i < (size >= ram + FOOTER ? 8 : 4); i++) stamp |= (uint64_t)f[40 + i] << (i * 8);
    r->last = (int64_t)stamp; /* the time since then is added by the next gb_set_rtc */
    r->has_time = 1;
}
