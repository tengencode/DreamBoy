#include "mem/bus.h"

#include <string.h>

#include "gba_internal.h"
#include "mem/bus_inline.h"

static inline void store_n(uint8_t *p, uint32_t v, unsigned size) {
    p[0] = (uint8_t)v;
    if (size >= 2) p[1] = (uint8_t)(v >> 8);
    if (size == 4) {
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
}

/* The size-byte lane of a 32-bit bus value that an address selects. */
static inline uint32_t lane(uint32_t word, uint32_t addr, unsigned size) {
    switch (size) {
    case 1: return (word >> ((addr & 3) * 8)) & 0xFF;
    case 2: return (word >> ((addr & 2) * 8)) & 0xFFFF;
    default: return word;
    }
}

/* 96 KiB of VRAM in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF. */
static inline uint32_t vram_offset(uint32_t addr) {
    uint32_t off = addr & 0x1FFFF;
    return off >= 0x18000 ? off - 0x8000 : off;
}

/* The save area has an 8-bit bus: accesses of every size keep their exact address. */
static inline bool save_area(uint32_t addr) {
    return (addr >> 24) == 0xE || (addr >> 24) == 0xF;
}

/* A halfword of the prefetch pipeline: real memory only, no side effects or cycles. Anything else
 * (unmapped space, I/O, the save area) falls back to the last fetched opcode, so a PC in unmapped
 * space cannot recurse into open bus. */
static uint32_t pipeline16(gba_t *g, uint32_t addr) {
    bus_t *b = &g->bus;
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE && b->in_bios) return bus_load_n(&b->bios[addr], 2);
        break;
    case 0x2: return bus_load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], 2);
    case 0x3: return bus_load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], 2);
    case 0x5: return bus_load_n(&b->pal[addr & (PAL_SIZE - 1)], 2);
    case 0x6: return bus_load_n(&b->vram[vram_offset(addr)], 2);
    case 0x7: return bus_load_n(&b->oam[addr & (OAM_SIZE - 1)], 2);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: return cart_rom_read(&g->cart, addr, 2);
    default: break;
    }
    return lane(b->open_bus, addr, 2);
}

uint32_t bus_open_bus(gba_t *g) {
    const arm_cpu_t *c = &g->cpu;
    uint32_t next = c->r[15]; /* while an instruction runs: its address + 8 (ARM) or + 4 (Thumb) */
    if (!(c->cpsr & CPSR_T)) return pipeline16(g, next) | pipeline16(g, next + 2) << 16;
    uint32_t pc = next - 4; /* $, the Thumb instruction being executed */
    switch (pc >> 24) {
    case 0x0: /* BIOS and OAM: 32-bit bus */
    case 0x7:
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 6) << 16;
    case 0x3: /* IWRAM */
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 2) << 16;
    default: { /* EWRAM, palette, VRAM, ROM: 16-bit bus */
        uint32_t half = pipeline16(g, pc + 4);
        return half | half << 16;
    }
    }
}

static uint32_t mem_read(gba_t *g, uint32_t addr, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE) {
            uint32_t word = b->in_bios ? bus_load_n(&b->bios[addr & 0x3FFC], 4) : b->bios_latch;
            return lane(word, addr, size);
        }
        return lane(bus_open_bus(g), addr, size);
    case 0x2: return bus_load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return bus_load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x4: return io_read(g, addr, size);
    case 0x5: return bus_load_n(&b->pal[addr & (PAL_SIZE - 1)], size);
    case 0x6: return bus_load_n(&b->vram[vram_offset(addr)], size);
    case 0x7: return bus_load_n(&b->oam[addr & (OAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        if (gpio_mapped(g, addr)) return gpio_read(g, addr, size);
        return cart_rom_read(&g->cart, addr, size);
    case 0xE:
    case 0xF: {
        uint32_t byte = save_read8(g, addr); /* wider reads repeat the byte */
        return size == 1 ? byte : size == 2 ? byte * 0x0101u : byte * 0x01010101u;
    }
    default: return lane(bus_open_bus(g), addr, size);
    }
}

static void mem_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x2: store_n(&b->ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&b->iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    case 0x4: io_write(g, addr, v, size); break;
    case 0x5:
        NOTE_DRAWING(g, NOTE_MIDLINE_PALETTE, 0);
        if (size == 1) store_n(&b->pal[addr & (PAL_SIZE - 2)], (v & 0xFF) * 0x0101u, 2);
        else store_n(&b->pal[addr & (PAL_SIZE - 1)], v, size);
        break;
    case 0x6: {
        uint32_t off = vram_offset(addr);
        if (size == 1) {
            /* Byte writes land as a doubled halfword in BG VRAM and are dropped in OBJ VRAM. */
            uint32_t bg_end = (g->ppu.dispcnt & 7) >= 3 ? 0x14000 : 0x10000;
            if (off < bg_end) store_n(&b->vram[off & ~1u], (v & 0xFF) * 0x0101u, 2);
        } else {
            store_n(&b->vram[off], v, size);
        }
        break;
    }
    case 0x7:
        if (size != 1) store_n(&b->oam[addr & (OAM_SIZE - 1)], v, size); /* byte writes are ignored */
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* bit 0 */
        else if (gpio_mapped(g, addr)) gpio_write(g, addr, v, size);
        break;
    case 0xE:
    case 0xF: save_write8(g, addr, (uint8_t)(v >> (8 * (addr & (size - 1))))); break; /* one byte, by address */
    default: break; /* BIOS, ROM and unmapped space ignore writes */
    }
}

/* Data reads: EWRAM, IWRAM and plain ROM straight from memory, the rest through mem_read. */
static inline uint32_t data_read(gba_t *g, uint32_t addr, unsigned size) {
    switch (addr >> 24) {
    case 0x2: return bus_load_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return bus_load_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        if (off >= 0x100 && off + size <= g->cart.rom_size) return bus_load_n(g->cart.rom + off, size);
        return mem_read(g, addr, size);
    }
    default: return mem_read(g, addr, size);
    }
}

/* Data writes: EWRAM and IWRAM straight to memory, the rest through mem_write. */
static inline void data_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    switch (addr >> 24) {
    case 0x2: store_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    default: mem_write(g, addr, v, size); break;
    }
}

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = bus_access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 1);
}

uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = bus_access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 2);
}

uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = bus_access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 4);
}

void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = bus_access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 1);
}

void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = bus_access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 2);
}

void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = bus_access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 4);
}

uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = bus_code_ptr(g, addr & ~3u);
    if (p) {
        b->in_bios = false;
        g->now += bus_access_cycles(g, addr & ~3u, 4, seq);
        return b->open_bus = bus_load_n(p, 4);
    }
    b->in_bios = addr < BIOS_SIZE;
    NOTE_FETCH(g, addr);
    addr &= ~3u;
    g->now += bus_access_cycles(g, addr, 4, seq);
    uint32_t op = mem_read(g, addr, 4);
    /* The BIOS latch holds the last BIOS word fetched: two instructions ahead in the pipeline. */
    if (b->in_bios) b->bios_latch = bus_load_n(&b->bios[(addr + 8) & 0x3FFC], 4);
    b->open_bus = op;
    return op;
}

uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = bus_code_ptr(g, addr & ~1u);
    if (p) {
        b->in_bios = false;
        g->now += bus_access_cycles(g, addr & ~1u, 2, seq);
        uint32_t op = bus_load_n(p, 2);
        b->open_bus = op | op << 16;
        return op;
    }
    b->in_bios = addr < BIOS_SIZE;
    NOTE_FETCH(g, addr);
    addr &= ~1u;
    g->now += bus_access_cycles(g, addr, 2, seq);
    uint32_t op = mem_read(g, addr, 2);
    if (b->in_bios) b->bios_latch = bus_load_n(&b->bios[(addr + 4) & 0x3FFC], 4);
    b->open_bus = op | op << 16;
    return op;
}

void bus_prefetch_stall(gba_t *g) {
    bus_t *b = &g->bus;
    uint32_t pc = g->cpu.r[15];
    uint32_t region = pc >> 24;
    if (!(b->waitcnt & 0x4000) || region < 0x8 || region > 0xD) return;
    int32_t s = (int32_t)b->cycles[region][0][1] - 1; /* the cartridge's sequential wait states (16-bit) */
    int32_t wait = (int32_t)b->off_rom;
    int32_t previous = 0, max_loads = 8;
    uint32_t dist = b->prefetch_end - pc; /* halfwords still buffered from an earlier stall */
    if (dist < 16) {
        previous = (int32_t)(dist >> 1);
        max_loads -= previous;
    }
    int32_t stall = s + 1, loads = 1;
    while (stall < wait && loads < max_loads) {
        stall += s;
        loads++;
    }
    b->prefetch_end = pc + 2u * (uint32_t)(loads + previous - 1);
    int32_t used = stall > wait ? stall : wait; /* the wait cannot be shorter than the prefetch */
    g->now = (uint64_t)((int64_t)g->now + (used - wait) - stall);
    g->cpu.next_fetch_seq = true; /* the next fetch comes out of the buffer: sequential */
}

uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    return bus_access_cycles(g, addr, size, seq);
}

void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
    g->bus.off_rom += cycles;
}

uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size) {
    return mem_read(g, addr & ~(uint32_t)(size - 1), size);
}

void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t region = addr >> 24;
    if (region >= 0x2 && region <= 0x7) mem_write(g, addr & ~(uint32_t)(size - 1), value, size);
}

static void set_region(bus_t *b, int region, uint8_t n16, uint8_t s16, uint8_t n32, uint8_t s32) {
    b->cycles[region][0][0] = n16;
    b->cycles[region][0][1] = s16;
    b->cycles[region][1][0] = n32;
    b->cycles[region][1][1] = s32;
}

void bus_set_waitcnt(gba_t *g, uint16_t value) {
    static const uint8_t nonseq[4] = {4, 3, 2, 8};
    static const uint8_t seq[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    bus_t *b = &g->bus;
    b->waitcnt = value & 0x7FFF;
    for (int ws = 0; ws < 3; ws++) {
        uint8_t n = (uint8_t)(1 + nonseq[(value >> (2 + ws * 3)) & 3]);
        uint8_t s = (uint8_t)(1 + seq[ws][(value >> (4 + ws * 3)) & 1]);
        /* The cartridge bus is 16 bits wide: a 32-bit access is one N plus one S access. */
        set_region(b, 0x8 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
        set_region(b, 0x9 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
    }
    uint8_t sram = (uint8_t)(1 + nonseq[value & 3]);
    set_region(b, 0xE, sram, sram, sram, sram);
    set_region(b, 0xF, sram, sram, sram, sram);
}

void bus_rebuild_timing(gba_t *g) {
    bus_t *b = &g->bus;
    set_region(b, 0x0, 1, 1, 1, 1); /* BIOS */
    set_region(b, 0x1, 1, 1, 1, 1);
    set_region(b, 0x2, 3, 3, 6, 6); /* EWRAM: 16-bit bus, 2 wait states */
    set_region(b, 0x3, 1, 1, 1, 1); /* IWRAM */
    set_region(b, 0x4, 1, 1, 1, 1); /* I/O */
    set_region(b, 0x5, 1, 1, 2, 2); /* palette: 16-bit bus */
    set_region(b, 0x6, 1, 1, 2, 2); /* VRAM: 16-bit bus */
    set_region(b, 0x7, 1, 1, 1, 1); /* OAM */
    bus_set_waitcnt(g, b->waitcnt);
}

void bus_reset(gba_t *g) {
    bus_t *b = &g->bus;
    memset(b->ewram, 0, sizeof b->ewram);
    memset(b->iwram, 0, sizeof b->iwram);
    memset(b->pal, 0, sizeof b->pal);
    memset(b->vram, 0, sizeof b->vram);
    memset(b->oam, 0, sizeof b->oam);
    b->waitcnt = 0;
    bus_rebuild_timing(g);
    b->in_bios = false;
    b->bios_latch = 0;
    b->open_bus = 0;
}
