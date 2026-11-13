/* The screen's timing: 154 lines of 456 dots. A visible line is OAM scan (mode 2, 80 dots), drawing (mode 3, 172
 * dots or more) and HBlank (mode 0, the rest); lines 144-153 are VBlank (mode 1). The picture itself is made a
 * whole line at a time when its drawing period ends (ppu_draw.c). Also here: the STAT interrupt line, and OAM DMA. */
#include <string.h>

#include "gb_internal.h"

#define LINE_DOTS 456

/* What the CPU can see of a line does not all change at once. Measured on hardware (the mooneye test suite's
 * lcdon_timing, intr_2_* and hblank_ly_scx_timing), in dots from the line's start, without sprites or scroll:
 *     0  LY is the new line; OAM is busy; the OAM (mode 2) interrupt source rises; STAT still shows mode 0
 *     4  STAT shows mode 2; LY is compared with LYC
 *    80  video RAM is busy
 *    84  STAT shows mode 3
 *   252  the line is drawn; the HBlank (mode 0) interrupt source rises
 *   256  STAT shows mode 0; OAM and video RAM are free
 * Each phase below is named for what happens when its dot is reached. */
enum { PH_SHOW_2, PH_DRAW_START, PH_SHOW_3, PH_DRAW_END, PH_SHOW_0, PH_LINE_END, PH_VBLANK_4, PH_VBLANK_12 };

/* The STAT interrupt is asked for when its line goes from low to high; while one source holds it high, another
 * cannot raise it again. With the screen off the line keeps its level. */
static void stat_update(gb_t *g) {
    gb_ppu_t *p = &g->ppu;
    if (!(p->lcdc & 0x80)) return;
    bool line = ((p->stat & 0x40) && p->coincidence) || ((p->stat & 0x08) && p->irq_mode == 0) ||
                ((p->stat & 0x10) && p->irq_mode == 1) || ((p->stat & 0x20) && p->irq_mode == 2);
    if (line && !p->stat_line) g->mem.iflag |= 0x02;
    p->stat_line = line;
}

/* How long this line's drawing takes: longer with a fine scroll, the window and each sprite (Pan Docs' estimate). */
static int mode3_length(const gb_t *g) {
    const gb_ppu_t *p = &g->ppu;
    int len = 172 + (p->scx & 7);
    if ((p->lcdc & 0x20) && p->window_hit && p->wx <= 166) len += 6;
    if (p->lcdc & 0x02) {
        uint8_t sprites[10];
        int n = gb_ppu_line_sprites(g, p->line, sprites);
        for (int i = 0; i < n; i++) {
            int fine = (p->oam[sprites[i] * 4 + 1] + p->scx) & 7;
            len += 11 - (fine < 5 ? fine : 5);
        }
    }
    return len < 289 ? len : 289;
}

static void start_line(gb_t *g) {
    gb_ppu_t *p = &g->ppu;
    if (p->ly != p->line) { /* (line 0: LY has read 0 since early in line 153) */
        p->ly = p->line;
        p->coincidence = 0;
    }
    if (p->line < GB_HEIGHT) {
        p->irq_mode = 2;
        p->oam_busy = 1;
        if (p->wy == p->line) p->window_hit = 1; /* from this line on the window may show */
        p->phase = PH_SHOW_2;
    } else {
        if (p->line == GB_HEIGHT) { /* VBlank starts: the finished picture is shown */
            p->irq_mode = 1;
            g->mem.iflag |= 0x01;
            memcpy(g->frame, g->back, sizeof g->frame);
            if ((p->stat & 0x20) && !p->stat_line) g->mem.iflag |= 0x02; /* the OAM source fires here too */
        }
        p->phase = PH_VBLANK_4;
    }
    p->next = 4;
}

static void event(gb_t *g) {
    gb_ppu_t *p = &g->ppu;
    switch (p->phase) {
    case PH_SHOW_2:
        p->mode = 2;
        p->coincidence = p->ly == p->lyc;
        p->phase = PH_DRAW_START;
        p->next = 80;
        break;
    case PH_DRAW_START:
        p->vram_busy = 1;
        p->irq_mode = 3;
        p->phase = PH_SHOW_3;
        p->next = 84;
        break;
    case PH_SHOW_3: /* (the first line after switching on starts here) */
        p->mode = p->irq_mode = 3;
        p->oam_busy = p->vram_busy = 1;
        p->phase = PH_DRAW_END;
        p->next = (uint16_t)(80 + mode3_length(g));
        break;
    case PH_DRAW_END:
        if (p->line < GB_HEIGHT) gb_ppu_draw_line(g, p->line); /* (a loaded state may say anything) */
        p->irq_mode = 0;
        p->phase = PH_SHOW_0;
        p->next += 4;
        break;
    case PH_SHOW_0:
        p->mode = 0;
        p->oam_busy = p->vram_busy = 0;
        gb_cgb_hblank(g);
        p->phase = PH_LINE_END;
        p->next = LINE_DOTS;
        break;
    case PH_VBLANK_4:
        p->mode = 1;
        p->coincidence = p->ly == p->lyc;
        p->phase = PH_LINE_END;
        p->next = LINE_DOTS;
        if (p->line == 153) { /* the last line shows LY 153 for 4 dots only, then already 0 */
            p->ly = 0;
            p->phase = PH_VBLANK_12;
            p->next = 12;
        }
        break;
    case PH_VBLANK_12:
        p->coincidence = p->lyc == 0;
        p->phase = PH_LINE_END;
        p->next = LINE_DOTS;
        break;
    default: /* the line is over */
        p->dot -= LINE_DOTS;
        if (++p->line == 154) {
            p->line = 0;
            p->window_line = 0;
            p->window_hit = 0;
        }
        start_line(g);
        break;
    }
    stat_update(g);
}

void gb_ppu_tick(gb_t *g, int dots) {
    gb_ppu_t *p = &g->ppu;
    if (!(p->lcdc & 0x80)) return;
    p->dot = (uint16_t)(p->dot + dots);
    while (p->dot >= p->next) event(g);
}

/* OAM DMA: 160 bytes, one a cycle, starting two cycles after the write; a new write restarts it the same way.
 * While a byte is being copied the CPU cannot reach OAM. */
void gb_dma_tick(gb_t *g) {
    gb_ppu_t *p = &g->ppu;
    if (p->dma_delay && --p->dma_delay == 0) {
        p->dma_active = 1;
        p->dma_index = 0;
    }
    p->dma_blocking = 0;
    if (!p->dma_active) return;
    uint16_t src = (uint16_t)(p->dma << 8 | p->dma_index);
    if (src >= 0xE000) src -= 0x2000; /* the top of memory is not reachable: work RAM shows there */
    p->oam[p->dma_index] = gb_bus_peek(g, src);
    p->dma_blocking = 1;
    if (++p->dma_index == 0xA0) p->dma_active = 0;
}

bool gb_ppu_vram_blocked(const gb_t *g) {
    return (g->ppu.lcdc & 0x80) && g->ppu.vram_busy;
}

bool gb_ppu_oam_blocked(const gb_t *g) {
    return g->ppu.dma_blocking || ((g->ppu.lcdc & 0x80) && g->ppu.oam_busy);
}

uint8_t gb_ppu_read(const gb_t *g, uint16_t addr) {
    const gb_ppu_t *p = &g->ppu;
    switch (addr) {
    case 0xFF40: return p->lcdc;
    case 0xFF41: return (uint8_t)(0x80 | p->stat | p->coincidence << 2 | p->mode);
    case 0xFF42: return p->scy;
    case 0xFF43: return p->scx;
    case 0xFF44: return p->ly;
    case 0xFF45: return p->lyc;
    case 0xFF46: return p->dma;
    case 0xFF47: return p->bgp;
    case 0xFF48: return p->obp0;
    case 0xFF49: return p->obp1;
    case 0xFF4A: return p->wy;
    default: return p->wx;
    }
}

/* A blank screen: white (0x7FFF, never a set top bit). */
static void blank(gb_t *g) {
    for (int i = 0; i < GB_WIDTH * GB_HEIGHT; i++) g->back[i] = g->frame[i] = 0x7FFF;
}

static void lcd_switch(gb_t *g, bool on) {
    gb_ppu_t *p = &g->ppu;
    p->line = 0;
    p->ly = 0;
    p->mode = p->irq_mode = 0;
    p->oam_busy = p->vram_busy = 0;
    p->window_line = 0;
    p->window_hit = 0;
    if (on) { /* the first line is 4 dots short and has no OAM scan to see: mode 0, then straight to drawing */
        p->dot = 4;
        p->phase = PH_SHOW_3;
        p->next = 84;
        p->coincidence = p->lyc == 0;
    } else { /* off: a blank (white) screen; the LY = LYC flag and the interrupt line keep their values */
        blank(g);
    }
}

void gb_ppu_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_ppu_t *p = &g->ppu;
    switch (addr) {
    case 0xFF40: {
        bool was_on = p->lcdc & 0x80;
        p->lcdc = value;
        if (was_on != ((value & 0x80) != 0)) lcd_switch(g, !was_on);
        break;
    }
    case 0xFF41:
        /* On the original Game Boy a write to STAT acts for a moment as if every source were enabled. */
        if (!g->info.color && (p->lcdc & 0x80) && !p->stat_line && (p->irq_mode < 2 || p->coincidence))
            g->mem.iflag |= 0x02;
        p->stat = value & 0x78;
        break;
    case 0xFF42: p->scy = value; return;
    case 0xFF43: p->scx = value; return;
    case 0xFF44: return;
    case 0xFF45:
        p->lyc = value;
        if (p->lcdc & 0x80) p->coincidence = p->ly == value;
        break;
    case 0xFF46:
        p->dma = value;
        p->dma_delay = 2;
        return;
    case 0xFF47: p->bgp = value; return;
    case 0xFF48: p->obp0 = value; return;
    case 0xFF49: p->obp1 = value; return;
    case 0xFF4A: p->wy = value; return;
    default: p->wx = value; return;
    }
    stat_update(g);
}

/* The registers as the boot ROM leaves them: the screen on, in the last line of VBlank. */
void gb_ppu_reset(gb_t *g) {
    gb_ppu_t *p = &g->ppu;
    memset(p, 0, sizeof *p);
    p->lcdc = 0x91;
    p->bgp = 0xFC;
    p->obp0 = p->obp1 = 0xFF;
    p->dma = 0xFF;
    p->line = 153;
    p->dot = 400;
    p->mode = p->irq_mode = 1;
    p->phase = PH_LINE_END;
    p->next = LINE_DOTS;
    p->coincidence = 1;
    blank(g);
}
