#ifndef GBA_PPU_H
#define GBA_PPU_H

#include <stdint.h>

typedef struct gba gba_t;

#define PPU_LINE_CYCLES 1232u
#define PPU_HDRAW_CYCLES 1008u /* the H-blank flag, IRQ and DMA start here (mGBA/GBATEK), not at pixel 240's 960 */
#define PPU_LINES 228u
#define PPU_VISIBLE_LINES 160u
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * PPU_LINES)
#define PPU_WIDTH 240
#define PPU_HEIGHT 160

enum {
    DISPSTAT_VBLANK = 1 << 0,
    DISPSTAT_HBLANK = 1 << 1,
    DISPSTAT_VCOUNT = 1 << 2,
    DISPSTAT_VBLANK_IRQ = 1 << 3,
    DISPSTAT_HBLANK_IRQ = 1 << 4,
    DISPSTAT_VCOUNT_IRQ = 1 << 5,
};

typedef struct {
    uint16_t dispcnt, dispstat, vcount;
    int32_t ref_x[2], ref_y[2];                   /* internal BG2/BG3 affine reference points (20.8) */
    uint16_t framebuffer[PPU_WIDTH * PPU_HEIGHT]; /* BGR555, row-major */
} ppu_t;

void ppu_reset(gba_t *g); /* clears state and schedules line 0's events relative to g->now */
void ppu_on_hblank(gba_t *g, uint64_t when);
void ppu_on_line_end(gba_t *g, uint64_t when);
void ppu_render_line(gba_t *g, int y); /* draw visible line y into the framebuffer */
void ppu_latch_affine(gba_t *g);       /* reload both reference points from BG2X/Y and BG3X/Y */
void ppu_affine_ref_written(gba_t *g, uint32_t off); /* a BGxX/BGxY byte at I/O offset off changed */

#endif
