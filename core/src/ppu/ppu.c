#include "ppu/ppu.h"

#include <string.h>

#include "gba_internal.h"
#include "ppu/ppu_internal.h"

void ppu_reset(gba_t *g) {
    memset(&g->ppu, 0, sizeof g->ppu);
    sched_add(&g->sched, EV_HBLANK, g->now + PPU_HDRAW_CYCLES);
    sched_add(&g->sched, EV_LINE_END, g->now + PPU_LINE_CYCLES);
}

static int32_t sign_extend28(uint32_t v) {
    return (int32_t)(v << 4) >> 4;
}

void ppu_latch_affine(gba_t *g) {
    for (int i = 0; i < 2; i++) {
        uint32_t base = i == 0 ? 0x028 : 0x038;
        g->ppu.ref_x[i] = sign_extend28(ppu_reg32(g, base));
        g->ppu.ref_y[i] = sign_extend28(ppu_reg32(g, base + 4));
    }
}

void ppu_affine_ref_written(gba_t *g, uint32_t off) {
    int i = off < 0x030 ? 0 : 1;
    uint32_t base = i == 0 ? 0x028 : 0x038;
    if (off - base < 4) g->ppu.ref_x[i] = sign_extend28(ppu_reg32(g, base));
    else g->ppu.ref_y[i] = sign_extend28(ppu_reg32(g, base + 4));
}

/* After each drawn line the references move by (PB, PD). */
static void affine_step(gba_t *g) {
    for (int i = 0; i < 2; i++) {
        uint32_t params = i == 0 ? 0x020 : 0x030;
        g->ppu.ref_x[i] += (int16_t)ppu_reg(g, params + 2);
        g->ppu.ref_y[i] += (int16_t)ppu_reg(g, params + 6);
    }
}

void ppu_render_line(gba_t *g, int y) {
    uint16_t *dst = &g->ppu.framebuffer[y * PPU_WIDTH];
    uint16_t dispcnt = g->ppu.dispcnt;
    if (dispcnt & 0x0080) { /* forced blank */
        for (int x = 0; x < PPU_WIDTH; x++) dst[x] = 0x7FFF;
        return;
    }
    line_buffers_t lb;
    memset(&lb, 0, sizeof lb);
    switch (dispcnt & 7) {
    case 0:
        for (int bg = 0; bg < 4; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    case 1:
        for (int bg = 0; bg < 2; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        if (dispcnt & 0x400) ppu_render_affine_bg(g, 2, y, lb.bg[2]);
        break;
    case 2:
        for (int bg = 2; bg < 4; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_affine_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    case 3:
    case 4:
    case 5:
        if (dispcnt & 0x400) ppu_render_bitmap_bg(g, dispcnt & 7, y, lb.bg[2]);
        break;
    default: break; /* modes 6-7: backdrop only */
    }
    if (dispcnt & 0x9000) ppu_render_obj(g, y, lb.obj, lb.objwin); /* OBJ layer or OBJ window */
    ppu_compose(g, y, &lb, dst);
}

void ppu_on_hblank(gba_t *g, uint64_t when) {
    ppu_t *p = &g->ppu;
    bool visible = p->vcount < PPU_VISIBLE_LINES;
    if (visible) {
        ppu_render_line(g, p->vcount);
        affine_step(g);
    }
    p->dispstat |= DISPSTAT_HBLANK;
    if (p->dispstat & DISPSTAT_HBLANK_IRQ) irq_raise_at(g, IRQ_HBLANK, when - 7); /* before the flag (tuned on the mGBA suite with the halt wake delay) */
    if (visible) dma_on_hblank(g);
}

void ppu_on_line_end(gba_t *g, uint64_t when) {
    ppu_t *p = &g->ppu;
    p->dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    p->vcount = (uint16_t)((p->vcount + 1) % PPU_LINES);

    if (p->vcount == PPU_VISIBLE_LINES) {
        ppu_latch_affine(g);
        p->dispstat |= DISPSTAT_VBLANK;
        if (p->dispstat & DISPSTAT_VBLANK_IRQ) irq_raise_at(g, IRQ_VBLANK, when);
        g->frame_done = true;
        dma_on_vblank(g);
    } else if (p->vcount == PPU_LINES - 1) {
        p->dispstat &= (uint16_t)~DISPSTAT_VBLANK; /* the flag is already clear on the last line */
    }

    if (p->vcount == (p->dispstat >> 8)) {
        p->dispstat |= DISPSTAT_VCOUNT;
        if (p->dispstat & DISPSTAT_VCOUNT_IRQ) irq_raise_at(g, IRQ_VCOUNT, when);
    } else {
        p->dispstat &= (uint16_t)~DISPSTAT_VCOUNT;
    }

    sched_add(&g->sched, EV_HBLANK, when + PPU_HDRAW_CYCLES);
    sched_add(&g->sched, EV_LINE_END, when + PPU_LINE_CYCLES);
}
