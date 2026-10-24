#ifndef GBA_PPU_INTERNAL_H
#define GBA_PPU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba_internal.h"

enum { LAYER_BG0, LAYER_BG1, LAYER_BG2, LAYER_BG3, LAYER_OBJ, LAYER_BD };

enum {
    PX_OPAQUE = 1 << 0, /* the layer drew a visible pixel here */
    PX_SEMI = 1 << 1,   /* semi-transparent sprite pixel */
};

typedef struct {
    uint16_t color; /* BGR555 */
    uint8_t prio;   /* 0 (front) .. 3 */
    uint8_t flags;  /* PX_* */
} layer_px_t;

typedef struct {
    layer_px_t bg[4][PPU_WIDTH];
    layer_px_t obj[PPU_WIDTH];
    bool objwin[PPU_WIDTH]; /* covered by an OBJ-window sprite */
} line_buffers_t;

/* Little-endian reads from the I/O backing store, VRAM, OAM and palette RAM. */
static inline uint16_t ppu_reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static inline uint32_t ppu_reg32(const gba_t *g, uint32_t off) {
    return ppu_reg(g, off) | (uint32_t)ppu_reg(g, off + 2) << 16;
}

static inline uint16_t vram16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.vram[off] | g->bus.vram[off + 1] << 8);
}

static inline uint16_t oam16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.oam[off] | g->bus.oam[off + 1] << 8);
}

static inline uint16_t pal_color(const gba_t *g, uint32_t index) {
    return (uint16_t)((g->bus.pal[index * 2] | g->bus.pal[index * 2 + 1] << 8) & 0x7FFF);
}

static inline void put_px(layer_px_t *px, uint16_t color, uint8_t prio, uint8_t flags) {
    px->color = color;
    px->prio = prio;
    px->flags = flags;
}

/* ppu_bg.c */
void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out);
/* ppu_obj.c */
void ppu_render_obj(gba_t *g, int y, layer_px_t *out, bool *objwin);
/* ppu_compose.c: on arm64 the NEON compositor (core/arch/arm64/ppu_neon.c), elsewhere its plain-C twin */
void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);
void ppu_compose_generic(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);

#endif
