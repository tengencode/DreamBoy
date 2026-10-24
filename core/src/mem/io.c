#include "mem/io.h"

#include <string.h>

#include "gba_internal.h"
#include "sys/keypad.h"

enum {
    REG_DISPCNT = 0x000,
    REG_DISPSTAT = 0x004,
    REG_VCOUNT = 0x006,
    REG_KEYINPUT = 0x130,
    REG_KEYCNT = 0x132,
    REG_IE = 0x200,
    REG_IF = 0x202,
    REG_WAITCNT = 0x204,
    REG_IME = 0x208,
    REG_POSTFLG = 0x300,
    REG_HALTCNT = 0x301,
};

enum { REG_TIMERS = 0x100, REG_TIMERS_END = 0x110 };
enum { REG_DMA = 0x0B0, REG_DMA_END = 0x0E0 };
enum { REG_SOUND = 0x060, REG_SOUND_END = 0x0A8, REG_FIFO = 0x0A0 };

/* Link-port registers (0x130/0x132 in between are the keypad). */
static bool link_port(uint32_t off) {
    return (off >= 0x120 && off < 0x130) || (off >= 0x134 && off < 0x160);
}

void io_reset(gba_t *g) {
    memset(&g->io, 0, sizeof g->io);
}

static uint16_t read16(gba_t *g, uint32_t off) {
    if (link_port(off)) return sio_read16(g, off);
    if (off >= REG_SOUND && off < REG_SOUND_END) return apu_read16(g, off);
    if (off >= REG_TIMERS && off < REG_TIMERS_END && !(off & 2)) return timer_read_counter(g, (int)((off - REG_TIMERS) >> 2));
    switch (off) {
    case REG_DISPCNT: return g->ppu.dispcnt;
    case REG_DISPSTAT: return g->ppu.dispstat;
    case REG_VCOUNT: return g->ppu.vcount;
    case REG_KEYINPUT: return g->keyinput;
    case REG_IE: return g->irq.ie;
    case REG_IF: return g->irq.if_;
    case REG_WAITCNT: return g->bus.waitcnt;
    case REG_IME: return g->irq.ime;
    case REG_POSTFLG: return g->io.postflg; /* HALTCNT (high byte) is write-only */
    default: return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
    }
}

static void write16(gba_t *g, uint32_t off, uint16_t v) {
    if (off < 0x056 && off != REG_DISPSTAT && off != REG_VCOUNT) NOTE_DRAWING(g, NOTE_MIDLINE_IO, off);
    if (off == 0x002 && (v & 1)) NOTE(g, NOTE_GREEN_SWAP, 0);
    if (link_port(off)) {
        sio_write16(g, off, v);
        return;
    }
    if (off >= REG_SOUND && off < REG_SOUND_END) {
        apu_write16(g, off, v);
        return;
    }
    if (off >= REG_DMA && off < REG_DMA_END) {
        uint16_t old = (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if ((off - REG_DMA) % 12 == 10) dma_write_control(g, (int)((off - REG_DMA) / 12), old, v);
        return;
    }
    if (off >= REG_TIMERS && off < REG_TIMERS_END) {
        int n = (int)((off - REG_TIMERS) >> 2);
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if (off & 2) timer_write_control(g, n, v);
        else timer_write_reload(g, n, v);
        return;
    }
    switch (off) {
    case REG_DISPCNT: g->ppu.dispcnt = v; break;
    case REG_DISPSTAT: g->ppu.dispstat = (uint16_t)((g->ppu.dispstat & 0x0007) | (v & 0xFF38)); break;
    case REG_VCOUNT:
    case REG_KEYINPUT: break;
    case REG_KEYCNT:
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        keypad_check(g);
        break;
    case REG_IE: g->irq.ie = v & 0x3FFF; irq_changed(g); break;
    case REG_IF: g->irq.if_ &= (uint16_t)~v; irq_changed(g); break;
    case REG_WAITCNT: bus_set_waitcnt(g, v); break;
    case REG_IME: g->irq.ime = v & 1; irq_changed(g); break;
    case REG_POSTFLG:
        g->io.postflg = v & 1;
        if (v & 0x8000) irq_stop(g); /* a halfword write here also writes HALTCNT */
        else irq_halt(g);
        break;
    default:
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if ((off >= 0x028 && off < 0x030) || (off >= 0x038 && off < 0x040)) ppu_affine_ref_written(g, off);
        break;
    }
}

static void write8(gba_t *g, uint32_t off, uint8_t v) {
    switch (off) {
    case REG_IF:
    case REG_IF + 1:
        g->irq.if_ &= (uint16_t)~(v << ((off & 1) * 8));
        irq_changed(g);
        return;
    case REG_POSTFLG: g->io.postflg = v & 1; return;
    case REG_HALTCNT: /* bit 7: Stop instead of Halt */
        if (v & 0x80) irq_stop(g);
        else irq_halt(g);
        return;
    case REG_FIFO:
    case REG_FIFO + 1:
    case REG_FIFO + 2:
    case REG_FIFO + 3:
    case REG_FIFO + 4:
    case REG_FIFO + 5:
    case REG_FIFO + 6:
    case REG_FIFO + 7: apu_write_fifo8(g, (int)((off - REG_FIFO) >> 2), v); return; /* one byte queued */
    default: {
        uint32_t base = off & ~1u;
        bool reload = base >= REG_TIMERS && base < REG_TIMERS_END && !(base & 2); /* reads give the counter, not the reload */
        uint16_t cur = reload ? g->timers.unit[(base - REG_TIMERS) >> 2].reload : read16(g, base);
        uint16_t merged = (off & 1) ? (uint16_t)((cur & 0x00FF) | v << 8) : (uint16_t)((cur & 0xFF00) | v);
        write16(g, base, merged);
    }
    }
}

/* One halfword as the CPU sees it: the register's readable bits, 0, or open bus. read16 (the stored
 * value) stays the source for byte-write merging, so hidden bits survive byte writes. */
static uint16_t read_visible16(gba_t *g, uint32_t off) {
    io_rule_t rule = io_rule(off);
    if (rule.kind == IO_ZERO) return 0;
    if (rule.kind == IO_OPEN) return (uint16_t)(bus_open_bus(g) >> ((off & 2) * 8));
    return read16(g, off) & rule.mask;
}

uint32_t io_read(gba_t *g, uint32_t addr, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    if (off >= IO_SIZE) { /* nothing above the registers: open bus */
        uint32_t open = bus_open_bus(g);
        if (size == 4) return open;
        if (size == 2) return (open >> ((off & 2) * 8)) & 0xFFFF;
        return (open >> ((off & 3) * 8)) & 0xFF;
    }
    switch (size) {
    case 1: return (read_visible16(g, off & ~1u) >> ((off & 1) * 8)) & 0xFF;
    case 2: return read_visible16(g, off);
    default: return read_visible16(g, off) | (uint32_t)read_visible16(g, off + 2) << 16;
    }
}

void io_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    NOTE_IO_WRITE(g, off, value, size);
    if (off >= IO_SIZE) return;
    switch (size) {
    case 1: write8(g, off, (uint8_t)value); break;
    case 2: write16(g, off, (uint16_t)value); break;
    default:
        write16(g, off, (uint16_t)value);
        write16(g, off + 2, (uint16_t)(value >> 16));
        break;
    }
}
