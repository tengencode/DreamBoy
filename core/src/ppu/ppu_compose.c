#include "ppu/ppu_internal.h"
#ifdef GBA_ARM64
#include "arm64/ppu_neon.h"
#endif

/*
 * The two front-most visible pixels at x. Front to back: lower priority value first;
 * at equal priority OBJ is in front of BG0, BG0 of BG1, and so on. The backdrop is last.
 * Each layer is looked at once; its place in that order is the key prio * 8 + (OBJ 0, BGn n + 1).
 */
static void find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                         uint16_t color[2], bool *semi) {
    unsigned key0 = 64, key1 = 64;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    const layer_px_t *o = &lb->obj[x];
    bool obj_first = false;
    if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE)) {
        key0 = (unsigned)o->prio * 8;
        layer[0] = LAYER_OBJ;
        color[0] = o->color;
        obj_first = true;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        if (!(visible & (1u << bg)) || !(p->flags & PX_OPAQUE)) continue;
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if (key < key0) {
            key1 = key0;
            layer[1] = layer[0];
            color[1] = color[0];
            key0 = key;
            layer[0] = bg;
            color[0] = p->color;
            obj_first = false;
        } else if (key < key1) {
            key1 = key;
            layer[1] = bg;
            color[1] = p->color;
        }
    }
    *semi = obj_first && (o->flags & PX_SEMI) != 0;
}

/* The front-most pixel at x when nothing blends or windows: every layer is visible. */
static uint16_t find_top(const line_buffers_t *lb, int x, uint16_t backdrop) {
    unsigned best = 64;
    uint16_t color = backdrop;
    const layer_px_t *o = &lb->obj[x];
    if (o->flags & PX_OPAQUE) {
        best = (unsigned)o->prio * 8;
        color = o->color;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if ((p->flags & PX_OPAQUE) && key < best) {
            best = key;
            color = p->color;
        }
    }
    return color;
}

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around. */
static bool in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

void ppu_compose_generic(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = cap16(bldalpha & 0x1F), evb = cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);

    /* No windows and no colour effects: only semi-transparent sprites could still blend. */
    if (!windowing && mode == 0) {
        bool semi_obj = false;
        for (int x = 0; x < PPU_WIDTH && !semi_obj; x++) semi_obj = (lb->obj[x].flags & PX_SEMI) != 0;
        if (!semi_obj) {
            for (int x = 0; x < PPU_WIDTH; x++) dst[x] = find_top(lb, x, backdrop);
            return;
        }
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F; /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            if (win0 && in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }

        int layer[2];
        uint16_t color[2];
        bool semi;
        find_top_two(lb, x, mask, backdrop, layer, color, &semi);

        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = blend(color[0], color[1], eva, evb); /* semi-transparent sprites always blend */
            } else if (first) {
                if (mode == 1 && second) out = blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = brighten(color[0], evy);
                else if (mode == 3) out = darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}

void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
#ifdef GBA_ARM64
    ppu_compose_neon(g, y, lb, dst);
#else
    ppu_compose_generic(g, y, lb, dst);
#endif
}
