/*
 * Milestone 7a's speed-ups must not change a single result. Each fast path is compared here with the code it
 * replaced (copied below as ref_*), on random VRAM, registers and line buffers; plus the CPU shortcuts.
 */
#include <string.h>

#include "cpu/cpu.h"
#include "gba_internal.h"
#include "mem/bus.h"
#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng_state = 0x12345678u;

static uint32_t rnd(void) { /* xorshift32: the same numbers on every run */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void random_vram(gba_t *g) {
    for (uint32_t i = 0; i < VRAM_SIZE; i++) g->bus.vram[i] = (uint8_t)rnd();
    for (uint32_t i = 0; i < PAL_SIZE; i++) g->bus.pal[i] = (uint8_t)rnd();
}

/* ---- text backgrounds: the pixel-by-pixel code before 7a ---- */

static void ref_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256, height = (cnt & 0x8000) ? 512 : 256;
    uint16_t m = ppu_reg(g, 0x04C);
    int mh = (cnt & 0x40) ? (m & 0xF) + 1 : 1, mv = (cnt & 0x40) ? ((m >> 4) & 0xF) + 1 : 1;
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue;
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

TEST(speedup_text_bg_matches_the_pixel_by_pixel_code) {
    gba_t *g = gba_create(NULL);
    static layer_px_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    for (int trial = 0; trial < 400; trial++) {
        if (trial % 50 == 0) random_vram(g);
        int bg = (int)(rnd() & 3);
        uint16_t cnt = (uint16_t)rnd();
        if (trial % 4) cnt &= (uint16_t)~0x40; /* mostly without mosaic (the tile-row path) */
        tu_io16(g, 0x008 + (uint32_t)bg * 2, cnt);
        tu_io16(g, 0x010 + (uint32_t)bg * 4, (uint16_t)rnd());
        tu_io16(g, 0x012 + (uint32_t)bg * 4, (uint16_t)rnd());
        tu_io16(g, 0x04C, (uint16_t)rnd());
        int y = (int)(rnd() % 160);
        memset(fast, 0, sizeof fast);
        memset(ref, 0, sizeof ref);
        ppu_render_text_bg(g, bg, y, fast);
        ref_text_bg(g, bg, y, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
    }
    gba_destroy(g);
}

/* ---- the compositor: the code before 7a ---- */

static void ref_find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                             uint16_t color[2], bool *semi) {
    int found = 0;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    *semi = false;
    for (uint8_t prio = 0; prio < 4 && found < 2; prio++) {
        const layer_px_t *o = &lb->obj[x];
        if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE) && o->prio == prio) {
            if (found == 0) *semi = (o->flags & PX_SEMI) != 0;
            layer[found] = LAYER_OBJ;
            color[found++] = o->color;
        }
        for (int bg = 0; bg < 4 && found < 2; bg++) {
            const layer_px_t *p = &lb->bg[bg][x];
            if ((visible & (1u << bg)) && (p->flags & PX_OPAQUE) && p->prio == prio) {
                layer[found] = bg;
                color[found++] = p->color;
            }
        }
    }
}

static bool ref_in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t ref_cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t ref_blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t ref_brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t ref_darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

static void ref_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && ref_in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && ref_in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = ref_cap16(bldalpha & 0x1F), evb = ref_cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = ref_cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F;
        if (windowing) {
            if (win0 && ref_in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && ref_in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }
        int layer[2];
        uint16_t color[2];
        bool semi;
        ref_find_top_two(lb, x, mask, backdrop, layer, color, &semi);
        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = ref_blend(color[0], color[1], eva, evb);
            } else if (first) {
                if (mode == 1 && second) out = ref_blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = ref_brighten(color[0], evy);
                else if (mode == 3) out = ref_darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}

static void random_px(layer_px_t *p, bool obj) {
    uint32_t r = rnd();
    uint8_t flags = (r & 3) ? PX_OPAQUE : 0; /* three in four drawn */
    if (obj && flags && (r & 0x30) == 0x30) flags |= PX_SEMI;
    put_px(p, (uint16_t)(r >> 16) & 0x7FFF, (uint8_t)((r >> 8) & 3), flags);
}

TEST(speedup_compose_matches_the_code_before) {
    gba_t *g = gba_create(NULL);
    static line_buffers_t lb;
    uint16_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    for (int trial = 0; trial < 600; trial++) {
        bool plain = trial % 3 == 0; /* no windows, no effects, no semi-transparent sprites: the quick path */
        for (int x = 0; x < PPU_WIDTH; x++) {
            for (int bg = 0; bg < 4; bg++) random_px(&lb.bg[bg][x], false);
            random_px(&lb.obj[x], !plain);
            lb.objwin[x] = (rnd() & 1) != 0;
        }
        g->ppu.dispcnt = plain ? 0 : (uint16_t)(rnd() & 0xE000);
        for (uint32_t off = 0x040; off <= 0x04A; off += 2) tu_io16(g, off, (uint16_t)rnd());
        tu_io16(g, 0x050, plain ? (uint16_t)(rnd() & 0x3F3F) : (uint16_t)rnd());
        tu_io16(g, 0x052, (uint16_t)rnd());
        tu_io16(g, 0x054, (uint16_t)rnd());
        tu_pal(g, 0, (uint16_t)rnd() & 0x7FFF);
        int y = (int)(rnd() % 160);
        ppu_compose(g, y, &lb, fast);
        ref_compose(g, y, &lb, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
#ifdef GBA_ARM64
        ppu_compose_generic(g, y, &lb, ref); /* the NEON compositor against today's plain-C twin too */
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
#endif
    }
    gba_destroy(g);
}

/* ---- bitmap backgrounds (modes 3-5): the code before 7a ---- */

static void ref_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    uint16_t m = ppu_reg(g, 0x04C);
    int mh = (cnt & 0x40) ? (m & 0xF) + 1 : 1, mv = (cnt & 0x40) ? ((m >> 4) & 0xF) + 1 : 1;
    int back = y % mv;
    int32_t pa = (int16_t)ppu_reg(g, 0x020), pc = (int16_t)ppu_reg(g, 0x024);
    int32_t x0 = g->ppu.ref_x[0] - back * (int16_t)ppu_reg(g, 0x022);
    int32_t y0 = g->ppu.ref_y[0] - back * (int16_t)ppu_reg(g, 0x026);
    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % mh;
        int32_t tx = (x0 + pa * sx) >> 8, ty = (y0 + pc * sx) >> 8;
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

TEST(speedup_bitmap_bg_matches_the_code_before) {
    gba_t *g = gba_create(NULL);
    static layer_px_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    random_vram(g);
    for (int trial = 0; trial < 400; trial++) {
        int mode = 3 + (int)(rnd() % 3);
        g->ppu.dispcnt = (uint16_t)(mode | (rnd() & 0x10));
        uint16_t cnt = (uint16_t)(rnd() & 3);
        if (trial % 4 == 0) cnt |= 0x40; /* some with mosaic */
        tu_io16(g, 0x00C, cnt);
        tu_io16(g, 0x04C, (uint16_t)rnd());
        bool unrotated = trial % 2 == 0; /* half the lines take the one-row path */
        tu_io16(g, 0x020, unrotated ? (uint16_t)(64 + (rnd() & 0x1FF)) : (uint16_t)rnd());
        tu_io16(g, 0x022, (uint16_t)rnd());
        tu_io16(g, 0x024, unrotated ? 0 : (uint16_t)rnd());
        tu_io16(g, 0x026, (uint16_t)rnd());
        g->ppu.ref_x[0] = (int32_t)(rnd() % (400u << 8)) - (80 << 8);
        g->ppu.ref_y[0] = (int32_t)(rnd() % (260u << 8)) - (50 << 8);
        int y = (int)(rnd() % 160);
        memset(fast, 0, sizeof fast);
        memset(ref, 0, sizeof ref);
        ppu_render_bitmap_bg(g, mode, y, fast);
        ref_bitmap_bg(g, mode, y, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
    }
    gba_destroy(g);
}

/* ---- the CPU shortcuts ---- */

TEST(speedup_every_condition_with_every_flag_combination) {
    const uint32_t nop = 0xE1A00000; /* mov r0, r0 */
    for (uint32_t cond = 0; cond < 16; cond++) {
        gba_t *g = tu_arm(&nop, 1);
        for (uint32_t nzcv = 0; nzcv < 16; nzcv++) {
            bus_poke(g, 0x03000000, cond << 28 | 0x03A00001, 4); /* mov<cond> r0, #1 */
            g->cpu.r[0] = 0;
            g->cpu.r[15] = 0x03000000;
            g->cpu.cpsr = (g->cpu.cpsr & 0x0FFFFFFFu) | nzcv << 28;
            tu_steps(g, 1);
            CHECK_EQ(g->cpu.r[0], cpu_cond_passed(nzcv << 28, cond) ? 1u : 0u);
        }
        gba_destroy(g);
    }
}

TEST(speedup_code_rewritten_in_ram_runs_the_new_code) {
    const uint32_t nop = 0xE1A00000;
    gba_t *g = tu_arm(&nop, 1);
    const uint32_t places[] = {0x03000100, 0x02000100, 0x03FF8100 /* an IWRAM mirror */};
    for (size_t i = 0; i < TU_LEN(places); i++) {
        bus_poke(g, places[i], 0xE3A00011, 4); /* mov r0, #0x11 */
        g->cpu.r[15] = places[i];
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0x11);
        bus_write32(g, places[i], 0xE3A00022, false); /* the program rewrites it: mov r0, #0x22 */
        g->cpu.r[15] = places[i];
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0x22);
    }
    gba_destroy(g);
}

TEST(speedup_rom_reads_at_the_end_and_past_it) {
    const uint32_t ops[] = {0xE1A00000, 0x11223344};
    gba_t *g = tu_arm(ops, TU_LEN(ops)); /* a 0x200-byte ROM */
    CHECK_EQ(bus_read32(g, 0x08000004, false), 0x11223344);
    CHECK_EQ(bus_read32(g, 0x080001FC, false), 0); /* the last word */
    CHECK_EQ(bus_read16(g, 0x08000200, false), 0x0100); /* past the end: the address pattern */
    CHECK_EQ(bus_read32(g, 0x08000200, false), 0x01010100);
    CHECK_EQ(bus_read8(g, 0x08000201, false), 0x01);
    CHECK_EQ(bus_read32(g, 0x0A000004, false), 0x11223344); /* a mirror */
    gba_destroy(g);
}
