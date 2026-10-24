#include "ppu/ppu_internal.h"

/* [shape][size]: square, wide, tall. */
static const uint8_t obj_width[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
static const uint8_t obj_height[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

/* Colour index at pixel (tx, ty) of a w-pixel-wide sprite; 0 is transparent. */
static uint32_t obj_texel(const gba_t *g, uint32_t tile, bool bpp8, bool one_d, int w, int tx, int ty) {
    uint32_t unit = bpp8 ? 2 : 1; /* tile numbers count 32-byte units */
    uint32_t row_stride = one_d ? (uint32_t)(w / 8) * unit : 32;
    uint32_t t = tile + (uint32_t)(ty / 8) * row_stride + (uint32_t)(tx / 8) * unit;
    uint32_t base = (t & 0x3FF) * 32;
    if (bpp8) return g->bus.vram[0x10000 + ((base + (uint32_t)(ty % 8) * 8 + (uint32_t)(tx % 8)) & 0x7FFF)];
    uint8_t b = g->bus.vram[0x10000 + ((base + (uint32_t)(ty % 8) * 4 + (uint32_t)(tx % 8) / 2) & 0x7FFF)];
    return (tx & 1) ? b >> 4 : b & 0xFu;
}

void ppu_render_obj(gba_t *g, int y, layer_px_t *out, bool *objwin) {
    uint16_t dispcnt = g->ppu.dispcnt;
    bool one_d = (dispcnt & 0x40) != 0;
    bool bitmap_mode = (dispcnt & 7) >= 3;
    uint16_t mosaic = ppu_reg(g, 0x04C);
    int mos_h = ((mosaic >> 8) & 0xF) + 1, mos_v = ((mosaic >> 12) & 0xF) + 1;
    int budget = (dispcnt & 0x20) ? 954 : 1210; /* H-Blank Interval Free shortens the budget */

    for (uint32_t i = 0; i < 128; i++) {
        uint16_t a0 = oam16(g, i * 8), a1 = oam16(g, i * 8 + 2), a2 = oam16(g, i * 8 + 4);
        bool affine = (a0 & 0x100) != 0;
        if (!affine && (a0 & 0x200)) continue; /* disabled */
        int shape = (a0 >> 14) & 3, size = (a1 >> 14) & 3;
        if (shape == 3) continue;
        int w = obj_width[shape][size], h = obj_height[shape][size];
        bool dbl = affine && (a0 & 0x200);
        int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        int row = (y - (a0 & 0xFF)) & 0xFF; /* Y wraps at 256 */
        if (row >= bh) continue;
        budget -= affine ? 2 * bw + 10 : bw;
        if (budget < 0) break;

        int mode = (a0 >> 10) & 3; /* 0 normal, 1 semi-transparent, 2 OBJ window */
        if (mode == 3) continue;
        if (mode == 2 ? !(dispcnt & 0x8000) : !(dispcnt & 0x1000)) continue;
        bool bpp8 = (a0 & 0x2000) != 0, mosaic_on = (a0 & 0x1000) != 0;
        uint32_t tile = a2 & 0x3FF;
        if (bitmap_mode && tile < 512) continue; /* those tiles overlap the bitmap */
        if (bpp8 && !one_d) tile &= ~1u;
        uint8_t prio = (a2 >> 10) & 3;
        uint32_t pal_base = bpp8 ? 256 : 256 + (uint32_t)(a2 >> 12) * 16;
        int ox = a1 & 0x1FF;
        if (ox >= PPU_WIDTH) ox -= 512;
        if (mosaic_on) row -= row % mos_v;

        int32_t pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            uint32_t p = ((a1 >> 9) & 0x1F) * 32;
            pa = (int16_t)oam16(g, p + 6);
            pb = (int16_t)oam16(g, p + 14);
            pc = (int16_t)oam16(g, p + 22);
            pd = (int16_t)oam16(g, p + 30);
        }

        for (int bx = 0; bx < bw; bx++) {
            int sx = ox + bx;
            if (sx < 0 || sx >= PPU_WIDTH) continue;
            int lx = bx;
            if (mosaic_on) {
                int m = sx - sx % mos_h - ox;
                if (m >= 0) lx = m;
            }
            int tx, ty;
            if (affine) {
                int cx = lx - bw / 2, cy = row - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = (a1 & 0x1000) ? w - 1 - lx : lx;
                ty = (a1 & 0x2000) ? h - 1 - row : row;
            }
            uint32_t index = obj_texel(g, tile, bpp8, one_d, w, tx, ty);
            if (!index) continue;
            if (mode == 2) {
                objwin[sx] = true;
                continue;
            }
            layer_px_t *px = &out[sx];
            if ((px->flags & PX_OPAQUE) && px->prio <= prio) continue; /* an earlier sprite wins ties */
            put_px(px, pal_color(g, pal_base + index), prio, (uint8_t)(PX_OPAQUE | (mode == 1 ? PX_SEMI : 0)));
        }
    }
}
