/* The picture, one whole line at a time: background, window, sprites — for both consoles. On a Game Boy Color
 * every background tile has an attribute byte (in video RAM's second bank) and colours come from palette RAM. */
#include <string.h>

#include "gb_internal.h"

/* The four greys of a plain Game Boy, lightest first (BGR555). */
static const uint16_t GREY[4] = {0x7FFF, 0x56B5, 0x294A, 0x0000};

/* Colour `index` of palette `n` in a Game Boy Color palette RAM: 15 bits, red lowest — the framebuffer's format. */
static uint16_t palette_colour(const uint8_t *ram, int n, int index) {
    const uint8_t *c = ram + (n * 4 + index) * 2;
    return (uint16_t)((c[0] | c[1] << 8) & 0x7FFF);
}

/* Up to 10 sprites show on a line: the first ten in OAM order that cover it. Returns how many; their OAM numbers
 * go to out[]. */
int gb_ppu_line_sprites(const gb_t *g, int ly, uint8_t out[10]) {
    const gb_ppu_t *p = &g->ppu;
    int height = (p->lcdc & 0x04) ? 16 : 8, n = 0;
    for (int i = 0; i < 40 && n < 10; i++) {
        int top = p->oam[i * 4] - 16;
        if (ly >= top && ly < top + height) out[n++] = (uint8_t)i;
    }
    return n;
}

/* One row of a tile map into dst[from..to) (colour numbers 0-3) and attr[] (each pixel's tile attributes; 0 on
 * a plain Game Boy): the pixel at `from` is the map's pixel (map_x, map_y); the map wraps at 256. */
static void tile_row(const gb_t *g, uint8_t *dst, uint8_t *attr, int from, int to, unsigned map, unsigned map_x,
                     unsigned map_y) {
    const gb_ppu_t *p = &g->ppu;
    unsigned row = map + (map_y / 8 & 31) * 32;
    for (int x = from; x < to; x++, map_x++) {
        unsigned cell = row + (map_x / 8 & 31);
        uint8_t tile = p->vram[cell], a = g->info.color ? p->vram[0x2000 + cell] : 0;
        unsigned line = (a & 0x40) ? 7 - (map_y & 7) : map_y & 7;
        unsigned addr = ((p->lcdc & 0x10) ? tile * 16u : 0x1000u + (unsigned)((int8_t)tile * 16)) + line * 2 +
                        ((a & 0x08) ? 0x2000u : 0);
        int bit = (a & 0x20) ? (int)(map_x & 7) : 7 - (int)(map_x & 7);
        dst[x] = (uint8_t)((p->vram[addr] >> bit & 1) | (p->vram[addr + 1] >> bit & 1) << 1);
        attr[x] = a;
    }
}

static void sprites(const gb_t *g, int ly, const uint8_t *bg, const uint8_t *bg_attr, uint16_t *out) {
    const gb_ppu_t *p = &g->ppu;
    bool color = g->info.color;
    uint8_t order[10], colour[GB_WIDTH] = {0}, attrs[GB_WIDTH];
    int n = gb_ppu_line_sprites(g, ly, order), height = (p->lcdc & 0x04) ? 16 : 8;
    /* Where sprites overlap: on a plain Game Boy the one further left wins, then the earlier one in OAM; on a
     * Game Boy Color just the earlier one in OAM (unless the game asks for the old rule). Losers are drawn first. */
    if (!color || (g->cgb.opri & 1)) {
        for (int i = 1; i < n; i++) {
            uint8_t s = order[i];
            int j = i;
            for (; j > 0 && p->oam[order[j - 1] * 4 + 1] > p->oam[s * 4 + 1]; j--) order[j] = order[j - 1];
            order[j] = s;
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        const uint8_t *o = p->oam + order[i] * 4;
        int row = ly - (o[0] - 16), left = o[1] - 8;
        uint8_t attr = o[3], tile = height == 16 ? o[2] & 0xFE : o[2];
        if (attr & 0x40) row = height - 1 - row;
        unsigned addr = tile * 16u + (unsigned)row * 2 + ((color && (attr & 0x08)) ? 0x2000u : 0);
        for (int c = 0; c < 8; c++) {
            int x = left + c, bit = (attr & 0x20) ? c : 7 - c;
            if (x < 0 || x >= GB_WIDTH) continue;
            uint8_t px = (uint8_t)((p->vram[addr] >> bit & 1) | (p->vram[addr + 1] >> bit & 1) << 1);
            if (px) colour[x] = px, attrs[x] = attr;
        }
    }
    for (int x = 0; x < GB_WIDTH; x++) {
        if (!colour[x]) continue;
        if (color) {
            /* Background colours 1-3 cover a sprite when the tile or the sprite says so — unless LCDC bit 0
             * takes the background's priority away altogether. */
            if ((p->lcdc & 0x01) && bg[x] && ((bg_attr[x] | attrs[x]) & 0x80)) continue;
            out[x] = palette_colour(g->cgb.obj_palette, attrs[x] & 7, colour[x]);
        } else {
            if ((attrs[x] & 0x80) && bg[x]) continue; /* "behind": only over background colour 0 */
            out[x] = GREY[((attrs[x] & 0x10) ? p->obp1 : p->obp0) >> (colour[x] * 2) & 3];
        }
    }
}

void gb_ppu_draw_line(gb_t *g, int ly) {
    gb_ppu_t *p = &g->ppu;
    bool color = g->info.color;
    uint16_t *out = g->back + ly * GB_WIDTH;
    uint8_t bg[GB_WIDTH] = {0}, bg_attr[GB_WIDTH] = {0}; /* the background's or window's pixel under each one */
    if (color || (p->lcdc & 0x01)) { /* (on a Game Boy Color, LCDC bit 0 does not switch the background off) */
        tile_row(g, bg, bg_attr, 0, GB_WIDTH, (p->lcdc & 0x08) ? 0x1C00 : 0x1800, p->scx, (unsigned)(ly + p->scy));
        if ((p->lcdc & 0x20) && p->window_hit && p->wx <= 166) {
            int left = p->wx - 7; /* the window keeps its own line count: it only moves on where it showed */
            tile_row(g, bg, bg_attr, left < 0 ? 0 : left, GB_WIDTH, (p->lcdc & 0x40) ? 0x1C00 : 0x1800,
                     left < 0 ? (unsigned)-left : 0, p->window_line++);
        }
        for (int x = 0; x < GB_WIDTH; x++)
            out[x] = color ? palette_colour(g->cgb.bg_palette, bg_attr[x] & 7, bg[x]) : GREY[p->bgp >> (bg[x] * 2) & 3];
    } else { /* background off: white, and sprites over all of it */
        for (int x = 0; x < GB_WIDTH; x++) out[x] = GREY[0];
    }
    if (p->lcdc & 0x02) sprites(g, ly, bg, bg_attr, out);
}
