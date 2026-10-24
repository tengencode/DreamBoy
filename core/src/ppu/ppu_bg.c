#include "ppu/ppu_internal.h"
#ifdef GBA_ARM64
#include "arm64/ppu_neon.h"
#endif

/* BG mosaic block size (1 = off) from MOSAIC when the BG's mosaic bit is set. */
static void bg_mosaic(const gba_t *g, uint16_t cnt, int *mh, int *mv) {
    uint16_t m = ppu_reg(g, 0x04C);
    bool on = (cnt & 0x40) != 0;
    *mh = on ? (m & 0xF) + 1 : 1;
    *mv = on ? ((m >> 4) & 0xF) + 1 : 1;
}

/* Line y of a text BG with horizontal mosaic: pixel by pixel (each block repeats its first pixel). */
static void text_bg_mosaic(gba_t *g, uint16_t cnt, uint32_t hofs, uint32_t py, int mh, layer_px_t *out) {
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        /* Maps wider or taller than 256 are 32x32-entry screen blocks: left to right, then top to bottom. */
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM */
            index = g->bus.vram[addr];
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4 + tx / 2;
            if (addr >= 0x10000) continue;
            index = (tx & 1) ? g->bus.vram[addr] >> 4 : g->bus.vram[addr] & 0xFu;
            if (index) index |= (uint32_t)(entry >> 12) << 4;
        }
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t height = (cnt & 0x8000) ? 512 : 256;
    int mh, mv;
    bg_mosaic(g, cnt, &mh, &mv);
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);
    if (mh > 1) {
        text_bg_mosaic(g, cnt, hofs, py, mh, out);
        return;
    }

    /* A tile row at a time: its map entry and its row of pixel data are read once. */
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    uint32_t row_entries = screen_base + (py >> 8) * (width >> 8) * 0x800 + ((py >> 3) & 31) * 64;
    for (int x = 0; x < PPU_WIDTH;) {
        uint32_t px = ((uint32_t)x + hofs) & (width - 1);
        uint32_t first = px & 7;
        int n = 8 - (int)first;
        if (n > PPU_WIDTH - x) n = PPU_WIDTH - x;
        uint16_t entry = vram16(g, row_entries + (px >> 8) * 0x800 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        bool hflip = (entry & 0x400) != 0;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        layer_px_t *o = &out[x];
        x += n;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM (a row never straddles it) */
            const uint8_t *row = &g->bus.vram[addr];
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = row[hflip ? 7 - tx : tx];
                if (index) put_px(&o[i], pal_color(g, index), prio, PX_OPAQUE);
            }
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4;
            if (addr >= 0x10000) continue;
            const uint8_t *b = &g->bus.vram[addr];
            uint32_t row = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
            if (!row) continue; /* a transparent tile row */
            uint32_t bank = (uint32_t)(entry >> 12) << 4;
#ifdef GBA_ARM64
            if (n == 8) { /* a whole row: NEON (its twin is the loop below) */
                ppu_tile_row4_neon(o, row, hflip, &g->bus.pal[bank * 2], prio);
                continue;
            }
#endif
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = (row >> ((hflip ? 7 - tx : tx) * 4)) & 0xF;
                if (index) put_px(&o[i], pal_color(g, bank | index), prio, PX_OPAQUE);
            }
        }
    }
}

typedef struct {
    int32_t pa, pc; /* texture step per screen pixel (8.8) */
    int32_t x0, y0; /* texture position of screen pixel 0 (20.8) */
    int mh;         /* horizontal mosaic block */
} affine_line_t;

/* Texture walk for line y of BG2/BG3. Vertical mosaic reuses the first line of the block by
 * stepping the reference back (exact while the parameters stay constant over the block). */
static affine_line_t affine_line(const gba_t *g, int bg, int y, uint16_t cnt) {
    uint32_t params = bg == 2 ? 0x020 : 0x030;
    affine_line_t a;
    int mv;
    bg_mosaic(g, cnt, &a.mh, &mv);
    int back = y % mv;
    a.pa = (int16_t)ppu_reg(g, params);
    a.pc = (int16_t)ppu_reg(g, params + 4);
    a.x0 = g->ppu.ref_x[bg - 2] - back * (int16_t)ppu_reg(g, params + 2);
    a.y0 = g->ppu.ref_y[bg - 2] - back * (int16_t)ppu_reg(g, params + 6);
    return a;
}

void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    int32_t size = 128 << ((cnt >> 14) & 3);
    bool wrap = (cnt & 0x2000) != 0;
    affine_line_t a = affine_line(g, bg, y, cnt);

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            continue;
        }
        uint32_t map = screen_base + (uint32_t)(ty >> 3) * (uint32_t)(size >> 3) + (uint32_t)(tx >> 3);
        uint32_t tile = g->bus.vram[map & 0xFFFF];
        uint32_t addr = char_base + tile * 64 + (uint32_t)(ty & 7) * 8 + (uint32_t)(tx & 7);
        if (addr >= 0x10000) continue;
        uint32_t index = g->bus.vram[addr];
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

/* Modes 3-5 draw BG2 as a bitmap sampled through the BG2 affine transform. */
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    affine_line_t a = affine_line(g, 2, y, cnt);

    if (a.mh == 1 && a.pc == 0) { /* not rotated: the whole line is one row of the bitmap */
        int32_t ty = a.y0 >> 8;
        if (ty < 0 || ty >= h) return;
        for (int x = 0; x < PPU_WIDTH; x++) {
            int32_t tx = (a.x0 + a.pa * x) >> 8;
            if (tx < 0 || tx >= w) continue;
            uint32_t pos = (uint32_t)(ty * w + tx);
            if (mode == 4) {
                uint32_t index = g->bus.vram[page + pos];
                if (index) put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
            } else {
                put_px(&out[x], vram16(g, page + pos * 2) & 0x7FFF, prio, PX_OPAQUE);
            }
        }
        return;
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        uint32_t pos = (uint32_t)(ty * w + tx);
        uint16_t color;
        if (mode == 4) {
            uint32_t index = g->bus.vram[page + pos];
            if (!index) continue;
            color = pal_color(g, index);
        } else {
            color = vram16(g, page + pos * 2) & 0x7FFF;
        }
        put_px(&out[x], color, prio, PX_OPAQUE);
    }
}
