#ifndef GBA_BUS_INLINE_H
#define GBA_BUS_INLINE_H

#include "gba_internal.h"

/* The bus's fast paths, inline for the CPU loop (bus.c holds the general paths). */

static inline uint32_t bus_load_n(const uint8_t *p, unsigned size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static inline void bus_store32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void bus_store16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline uint32_t bus_access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

/* Where an instruction at addr can be read straight from memory: EWRAM, IWRAM and the ROM itself (not past
 * its end, not the GPIO port at 0x080000C4, not region 0xD where an EEPROM may answer); NULL for the rest. */
static inline const uint8_t *bus_code_ptr(const gba_t *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + 4 <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

/* bus_fetch32 / bus_fetch16 with the common case inline: no DMA due, code in RAM or ROM. */
static inline uint32_t bus_fetch32_inline(gba_t *g, uint32_t addr, bool seq) {
    const uint8_t *p;
    if (!dma_bus_due(&g->dma, g->now) && (p = bus_code_ptr(g, addr & ~3u))) {
        g->bus.in_bios = false;
        g->now += bus_access_cycles(g, addr & ~3u, 4, seq);
        return g->bus.open_bus = bus_load_n(p, 4);
    }
    return bus_fetch32(g, addr, seq);
}

static inline uint32_t bus_fetch16_inline(gba_t *g, uint32_t addr, bool seq) {
    const uint8_t *p;
    if (!dma_bus_due(&g->dma, g->now) && (p = bus_code_ptr(g, addr & ~1u))) {
        g->bus.in_bios = false;
        g->now += bus_access_cycles(g, addr & ~1u, 2, seq);
        uint32_t op = bus_load_n(p, 2);
        g->bus.open_bus = op | op << 16;
        return op;
    }
    return bus_fetch16(g, addr, seq);
}

#endif
