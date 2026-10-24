#ifndef GBA_PPU_NEON_H
#define GBA_PPU_NEON_H

/* NEON drawing for arm64 (milestone 7b); the plain-C twins are in core/src/ppu. */
#include <arm_neon.h>

#include "ppu/ppu_internal.h"

/* A whole 4bpp tile row into o[0..7]: the opaque pixels (index != 0) get bank colour, prio and PX_OPAQUE; the rest of
 * o is kept. row holds the 8 indices (pixel i in bits 4i-4i+3), pal_bank the bank's 16 colours. As ppu_bg.c's loop. */
static inline void ppu_tile_row4_neon(layer_px_t *o, uint32_t row, bool hflip, const uint8_t *pal_bank, uint8_t prio) {
    uint8x8_t b = vcreate_u8(row);
    uint8x8_t idx = vzip1_u8(vand_u8(b, vdup_n_u8(0x0F)), vshr_n_u8(b, 4)); /* pixels 0-7 */
    if (hflip) idx = vrev64_u8(idx);
    uint8x16x2_t tbl = {{vld1q_u8(pal_bank), vld1q_u8(pal_bank + 16)}};
    uint8x8_t i2 = vshl_n_u8(idx, 1);
    uint8x8_t lo = vqtbl2_u8(tbl, i2);
    uint8x8_t hi = vand_u8(vqtbl2_u8(tbl, vadd_u8(i2, vdup_n_u8(1))), vdup_n_u8(0x7F));
    uint8x8_t opaque = vtst_u8(idx, idx);
    uint8x8x4_t px = vld4_u8((uint8_t *)o);
    px.val[0] = vbsl_u8(opaque, lo, px.val[0]);
    px.val[1] = vbsl_u8(opaque, hi, px.val[1]);
    px.val[2] = vbsl_u8(opaque, vdup_n_u8(prio), px.val[2]);
    px.val[3] = vbsl_u8(opaque, vdup_n_u8(PX_OPAQUE), px.val[3]);
    vst4_u8((uint8_t *)o, px);
}

/* ppu_compose with NEON (ppu_neon.c). */
void ppu_compose_neon(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);

#endif
