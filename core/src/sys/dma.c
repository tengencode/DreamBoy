#include "sys/dma.h"

#include <string.h>

#include "gba_internal.h"

enum { DMA_BASE = 0x0B0, DMA_STRIDE = 12 };
enum { TIMING_IMMEDIATE, TIMING_VBLANK, TIMING_HBLANK, TIMING_SPECIAL };

static const uint32_t src_mask[4] = {0x07FFFFFF, 0x0FFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF};
static const uint32_t dst_mask[4] = {0x07FFFFFF, 0x07FFFFFF, 0x07FFFFFF, 0x0FFFFFFF};

static uint16_t reg16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static uint32_t reg32(const gba_t *g, uint32_t off) {
    return reg16(g, off) | (uint32_t)reg16(g, off + 2) << 16;
}

static uint32_t channel_base(int n) {
    return DMA_BASE + (uint32_t)n * DMA_STRIDE;
}

static uint16_t control(const gba_t *g, int n) {
    return reg16(g, channel_base(n) + 10);
}

/* A count of 0 means the maximum: 0x4000 units (0x10000 for DMA3). */
static uint32_t latched_count(const gba_t *g, int n) {
    uint32_t count = reg16(g, channel_base(n) + 8) & (n == 3 ? 0xFFFFu : 0x3FFFu);
    return count ? count : (n == 3 ? 0x10000u : 0x4000u);
}

void dma_reset(gba_t *g) {
    memset(&g->dma, 0, sizeof g->dma);
    g->dma.due = UINT64_MAX;
}

void dma_write_control(gba_t *g, int n, uint16_t old, uint16_t value) {
    dma_channel_t *c = &g->dma.ch[n];
    if (!(value & 0x8000)) {
        g->dma.pending &= (uint8_t)~(1u << n);
        return;
    }
    if (old & 0x8000) return; /* already enabled: only the settings changed */
    if (n == 3 && ((value >> 12) & 3) == TIMING_SPECIAL) NOTE(g, NOTE_VIDEO_CAPTURE, 0);
    c->src = reg32(g, channel_base(n)) & src_mask[n];
    c->dst = reg32(g, channel_base(n) + 4) & dst_mask[n];
    c->count = latched_count(g, n);
    if (((value >> 12) & 3) == TIMING_IMMEDIATE) {
        /* Takes the bus 3 cycles after the write; the CPU runs on meanwhile, and any CPU bus access
         * from then on waits for the transfer (bus.c). */
        g->dma.pending |= (uint8_t)(1u << n);
        g->dma.due = g->now + 3;
        sched_add(&g->sched, EV_DMA, g->dma.due);
    }
}

/* Address control: 0 increment, 1 decrement, 2 fixed, 3 increment (+ reload for destinations). */
static int32_t step(uint32_t mode, uint32_t unit) {
    if (mode == 1) return -(int32_t)unit;
    if (mode == 2) return 0;
    return (int32_t)unit;
}

/* Copy `count` units. The CPU is stalled for the whole transfer: bus cycles simply advance the clock.
 * DMA addresses are aligned to the unit size, even in the save area (where the CPU's are not). */
static void copy(gba_t *g, dma_channel_t *c, uint32_t count, bool word, int32_t src_step, int32_t dst_step) {
    uint32_t align = word ? ~3u : ~1u;
    for (uint32_t i = 0; i < count; i++) {
        bool seq = i > 0;
        uint32_t src = c->src & align, dst = c->dst & align;
        uint32_t value;
        if (src >= 0x02000000) {
            value = word ? bus_read32(g, src, seq) : bus_read16(g, src, seq) * 0x00010001u;
            g->dma.latch = value;
        } else {
            bus_idle(g, 1);
            value = g->dma.latch; /* the BIOS and unmapped space are not readable by DMA */
        }
        if (word) bus_write32(g, dst, value, seq);
        else bus_write16(g, dst, (uint16_t)(value >> ((c->dst & 2) * 8)), seq);
        c->src += (uint32_t)src_step;
        c->dst += (uint32_t)dst_step;
    }
    if ((c->src >> 24) < 0x8 || (c->dst >> 24) < 0x8) bus_idle(g, 2); /* 2 more cycles unless ROM to ROM */
}

static void disable(gba_t *g, int n, uint16_t ctl) {
    uint16_t off_ctl = (uint16_t)(ctl & ~0x8000);
    g->io.raw[channel_base(n) + 10] = (uint8_t)off_ctl;
    g->io.raw[channel_base(n) + 11] = (uint8_t)(off_ctl >> 8);
}

static void transfer(gba_t *g, int n) {
    dma_channel_t *c = &g->dma.ch[n];
    uint16_t ctl = control(g, n);
    bool word = (ctl & 0x400) != 0;
    uint32_t unit = word ? 4 : 2;
    if (n == 3) save_eeprom_dma(g, c->dst, c->count); /* the length gives away the EEPROM size */
    /* A cartridge-ROM source always increments, whatever the source control says. */
    bool rom_src = (c->src >> 24) >= 0x8 && (c->src >> 24) < 0xE;
    copy(g, c, c->count, word, rom_src ? (int32_t)unit : step((ctl >> 7) & 3, unit), step((ctl >> 5) & 3, unit));

    if (ctl & 0x4000) irq_raise(g, (uint16_t)(IRQ_DMA0 << n));
    uint32_t timing = (ctl >> 12) & 3;
    if ((ctl & 0x200) && timing != TIMING_IMMEDIATE) {
        c->count = latched_count(g, n);
        if (((ctl >> 5) & 3) == 3) c->dst = reg32(g, channel_base(n) + 4) & dst_mask[n];
    } else {
        disable(g, n, ctl);
    }
}

static void trigger(gba_t *g, uint32_t timing) {
    for (int n = 0; n < 4; n++) {
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == timing) transfer(g, n);
    }
}

void dma_on_vblank(gba_t *g) {
    trigger(g, TIMING_VBLANK);
}

void dma_on_hblank(gba_t *g) {
    trigger(g, TIMING_HBLANK);
}

void dma_run_immediate(gba_t *g) {
    g->dma.due = UINT64_MAX;
    sched_cancel(&g->sched, EV_DMA);
    g->dma.active = true;
    for (int n = 0; n < 4; n++) {
        if (!(g->dma.pending & (1u << n))) continue;
        g->dma.pending &= (uint8_t)~(1u << n);
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == TIMING_IMMEDIATE) transfer(g, n);
    }
    g->dma.active = false;
}

void dma_sound_request(gba_t *g, uint32_t fifo_addr) {
    for (int n = 1; n <= 2; n++) {
        dma_channel_t *c = &g->dma.ch[n];
        uint16_t ctl = control(g, n);
        if (!(ctl & 0x8000) || ((ctl >> 12) & 3u) != TIMING_SPECIAL || c->dst != fifo_addr) continue;
        /* Always 4 words to a fixed destination, whatever CNT_L and the size bit say. */
        copy(g, c, 4, true, step((ctl >> 7) & 3, 4), 0);
        if (ctl & 0x4000) irq_raise(g, (uint16_t)(IRQ_DMA0 << n));
        if (!(ctl & 0x200)) disable(g, n, ctl);
        return;
    }
}
