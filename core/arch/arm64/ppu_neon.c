/*
 * The compositor with NEON (milestone 7b): ppu_compose's rules (core/src/ppu/ppu_compose.c, its plain-C twin) on
 * 16 pixels at a time without windows or effects, 8 at a time with them. Each layer's line buffer is read with
 * vld4, which splits the 4-byte pixels into colour-low, colour-high, priority and flags lanes.
 */
#include "arm64/ppu_neon.h"

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around: which of the x lanes are inside. */
static inline uint8x8_t in_span8(uint16_t reg, uint8x8_t x) {
    uint8x8_t start = vdup_n_u8((uint8_t)(reg >> 8)), end = vdup_n_u8((uint8_t)reg);
    uint8x8_t ge = vcge_u8(x, start), lt = vclt_u8(x, end);
    return (reg >> 8) <= (reg & 0xFF) ? vand_u8(ge, lt) : vorr_u8(ge, lt);
}

static inline uint16x8_t color8(uint8x8_t lo, uint8x8_t hi) {
    return vorrq_u16(vmovl_u8(lo), vshlq_n_u16(vmovl_u8(hi), 8));
}

static inline uint16x8_t wide8(uint8x8_t m) {
    return vreinterpretq_u16_s16(vmovl_s8(vreinterpret_s8_u8(m))); /* 0xFF -> 0xFFFF */
}

static inline uint16x8_t blend8(uint16x8_t a, uint16x8_t b, uint16x8_t eva, uint16x8_t evb) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t ca = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        uint16x8_t cb = vandq_u16(vshlq_u16(b, vdupq_n_s16((int16_t)-s)), m);
        uint16x8_t c = vshrq_n_u16(vmlaq_u16(vmulq_u16(ca, eva), cb, evb), 4);
        out = vorrq_u16(out, vshlq_u16(vminq_u16(c, m), vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static inline uint16x8_t brighten8(uint16x8_t a, uint16x8_t evy) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t c = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        c = vaddq_u16(c, vshrq_n_u16(vmulq_u16(vsubq_u16(m, c), evy), 4));
        out = vorrq_u16(out, vshlq_u16(c, vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static inline uint16x8_t darken8(uint16x8_t a, uint16x8_t evy) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t c = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        c = vsubq_u16(c, vshrq_n_u16(vmulq_u16(c, evy), 4));
        out = vorrq_u16(out, vshlq_u16(c, vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

/* No windows, no effects, no semi-transparent sprite: the front-most opaque pixel, 16 at a time. Keys as in
 * find_top: priority * 8 + (OBJ 0, BGn n + 1); 64 for nothing. */
static void compose_plain(const line_buffers_t *lb, uint16_t backdrop, uint16_t *dst) {
    for (int x = 0; x < PPU_WIDTH; x += 16) {
        uint8x16x4_t o = vld4q_u8((const uint8_t *)&lb->obj[x]);
        uint8x16_t op = vtstq_u8(o.val[3], vdupq_n_u8(PX_OPAQUE));
        uint8x16_t best = vbslq_u8(op, vshlq_n_u8(o.val[2], 3), vdupq_n_u8(64));
        uint8x16_t lo = vbslq_u8(op, o.val[0], vdupq_n_u8((uint8_t)backdrop));
        uint8x16_t hi = vbslq_u8(op, o.val[1], vdupq_n_u8((uint8_t)(backdrop >> 8)));
        for (int bg = 0; bg < 4; bg++) {
            uint8x16x4_t p = vld4q_u8((const uint8_t *)&lb->bg[bg][x]);
            uint8x16_t key = vaddq_u8(vshlq_n_u8(p.val[2], 3), vdupq_n_u8((uint8_t)(bg + 1)));
            uint8x16_t take = vandq_u8(vtstq_u8(p.val[3], vdupq_n_u8(PX_OPAQUE)), vcltq_u8(key, best));
            best = vbslq_u8(take, key, best);
            lo = vbslq_u8(take, p.val[0], lo);
            hi = vbslq_u8(take, p.val[1], hi);
        }
        uint8x16x2_t out = {{lo, hi}};
        vst2q_u8((uint8_t *)&dst[x], out);
    }
}

void ppu_compose_neon(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    uint16_t win0v = ppu_reg(g, 0x044), win1v = ppu_reg(g, 0x046);
    int sv0 = win0v >> 8, ev0 = win0v & 0xFF, sv1 = win1v >> 8, ev1 = win1v & 0xFF;
    bool win0 = (dispcnt & 0x2000) && (sv0 <= ev0 ? y >= sv0 && y < ev0 : y >= sv0 || y < ev0);
    bool win1 = (dispcnt & 0x4000) && (sv1 <= ev1 ? y >= sv1 && y < ev1 : y >= sv1 || y < ev1);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint16_t backdrop = pal_color(g, 0);

    if (!windowing && mode == 0) {
        uint8x16_t semi = vdupq_n_u8(0);
        for (int x = 0; x < PPU_WIDTH; x += 16) semi = vorrq_u8(semi, vld4q_u8((const uint8_t *)&lb->obj[x]).val[3]);
        if (!(vmaxvq_u8(vandq_u8(semi, vdupq_n_u8(PX_SEMI))))) {
            compose_plain(lb, backdrop, dst);
            return;
        }
    }

    uint16x8_t eva = vdupq_n_u16((uint16_t)cap16(bldalpha & 0x1F)), evb = vdupq_n_u16((uint16_t)cap16((bldalpha >> 8) & 0x1F));
    uint16x8_t evy = vdupq_n_u16((uint16_t)cap16(ppu_reg(g, 0x054) & 0x1F));
    /* Which layers (0-3 BGs, 4 OBJ, 5 backdrop) are first and second targets: 0xFF / 0 by layer number. */
    uint8_t first_tab[16] = {0}, second_tab[16] = {0};
    for (int l = 0; l < 6; l++) {
        first_tab[l] = ((bldcnt >> l) & 1) ? 0xFF : 0;
        second_tab[l] = ((bldcnt >> (8 + l)) & 1) ? 0xFF : 0;
    }
    uint8x16_t first_t = vld1q_u8(first_tab), second_t = vld1q_u8(second_tab);
    static const uint8_t iota[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8x8_t lanes = vld1_u8(iota);
    uint8x8_t m_in0 = vdup_n_u8(winin & 0x3F), m_in1 = vdup_n_u8((winin >> 8) & 0x3F);
    uint8x8_t m_objw = vdup_n_u8((winout >> 8) & 0x3F), m_out = vdup_n_u8(winout & 0x3F);
    uint8x8_t m_all = vdup_n_u8(64), bd = vdup_n_u8(LAYER_BD);
    uint16x8_t back = vdupq_n_u16(backdrop);

    for (int x = 0; x < PPU_WIDTH; x += 8) {
        uint8x8_t mask = vdup_n_u8(0x3F); /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            uint8x8_t xv = vadd_u8(vdup_n_u8((uint8_t)x), lanes);
            uint8x8_t in0 = win0 ? in_span8(win0h, xv) : vdup_n_u8(0);
            uint8x8_t in1 = win1 ? in_span8(win1h, xv) : vdup_n_u8(0);
            uint8x8_t ow = objwin ? vtst_u8(vld1_u8((const uint8_t *)&lb->objwin[x]), vdup_n_u8(0xFF)) : vdup_n_u8(0);
            mask = vbsl_u8(in0, m_in0, vbsl_u8(in1, m_in1, vbsl_u8(ow, m_objw, m_out)));
        }
        /* The two front-most visible pixels (find_top_two): keys, layers and colours. */
        uint8x8x4_t o = vld4_u8((const uint8_t *)&lb->obj[x]);
        uint8x8_t vis = vand_u8(vtst_u8(mask, vdup_n_u8(0x10)), vtst_u8(o.val[3], vdup_n_u8(PX_OPAQUE)));
        uint8x8_t key0 = vbsl_u8(vis, vshl_n_u8(o.val[2], 3), m_all), key1 = m_all;
        uint8x8_t layer0 = vbsl_u8(vis, vdup_n_u8(LAYER_OBJ), bd), layer1 = bd;
        uint16x8_t c0 = vbslq_u16(wide8(vis), color8(o.val[0], o.val[1]), back), c1 = back;
        for (int bg = 0; bg < 4; bg++) {
            uint8x8x4_t p = vld4_u8((const uint8_t *)&lb->bg[bg][x]);
            uint8x8_t v = vand_u8(vtst_u8(mask, vdup_n_u8((uint8_t)(1 << bg))), vtst_u8(p.val[3], vdup_n_u8(PX_OPAQUE)));
            uint8x8_t key = vbsl_u8(v, vadd_u8(vshl_n_u8(p.val[2], 3), vdup_n_u8((uint8_t)(bg + 1))), m_all);
            uint8x8_t t0 = vclt_u8(key, key0), t1 = vbic_u8(vclt_u8(key, key1), t0);
            uint16x8_t col = color8(p.val[0], p.val[1]), w0 = wide8(t0), w1 = wide8(t1);
            key1 = vbsl_u8(t0, key0, vbsl_u8(t1, key, key1));
            layer1 = vbsl_u8(t0, layer0, vbsl_u8(t1, vdup_n_u8((uint8_t)bg), layer1));
            c1 = vbslq_u16(w0, c0, vbslq_u16(w1, col, c1));
            key0 = vbsl_u8(t0, key, key0);
            layer0 = vbsl_u8(t0, vdup_n_u8((uint8_t)bg), layer0);
            c0 = vbslq_u16(w0, col, c0);
        }
        uint8x8_t semi = vand_u8(vceq_u8(layer0, vdup_n_u8(LAYER_OBJ)), vtst_u8(o.val[3], vdup_n_u8(PX_SEMI)));
        /* The colour effect (ppu_compose's order): a semi-transparent sprite blends with a second target; otherwise a
         * first target takes the mode's effect (alpha blending only with a second target). */
        uint8x8_t eff = vtst_u8(mask, vdup_n_u8(0x20));
        uint8x8_t first = vqtbl1_u8(first_t, layer0), second = vqtbl1_u8(second_t, layer1);
        uint8x8_t semi_second = vand_u8(semi, second);
        uint8x8_t by_mode = vbic_u8(vand_u8(eff, first), semi_second);
        uint8x8_t use_blend = vand_u8(eff, vorr_u8(semi_second, mode == 1 ? vand_u8(by_mode, second) : vdup_n_u8(0)));
        uint16x8_t out = c0;
        if (vmaxv_u8(use_blend)) out = vbslq_u16(wide8(use_blend), blend8(c0, c1, eva, evb), out);
        if (mode == 2 && vmaxv_u8(by_mode)) out = vbslq_u16(wide8(by_mode), brighten8(c0, evy), out);
        if (mode == 3 && vmaxv_u8(by_mode)) out = vbslq_u16(wide8(by_mode), darken8(c0, evy), out);
        vst1q_u16(&dst[x], out);
    }
}
