#include <stdint.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* sin(2*pi*i/256) in 1.14 fixed point, cut toward zero as the BIOS's own table has it (found by the twin tests, M13);
 * the affine SWIs use the top 8 bits of the angle. */
static const int16_t sine_table[256] = {
    0, 402, 803, 1205, 1605, 2005, 2404, 2801,
    3196, 3589, 3980, 4369, 4756, 5139, 5519, 5896,
    6269, 6639, 7005, 7366, 7723, 8075, 8423, 8765,
    9102, 9434, 9759, 10079, 10393, 10701, 11002, 11297,
    11585, 11866, 12139, 12406, 12665, 12916, 13159, 13395,
    13622, 13842, 14053, 14255, 14449, 14634, 14810, 14978,
    15136, 15286, 15426, 15557, 15678, 15790, 15892, 15985,
    16069, 16142, 16206, 16260, 16305, 16339, 16364, 16379,
    16384, 16379, 16364, 16339, 16305, 16260, 16206, 16142,
    16069, 15985, 15892, 15790, 15678, 15557, 15426, 15286,
    15136, 14978, 14810, 14634, 14449, 14255, 14053, 13842,
    13622, 13395, 13159, 12916, 12665, 12406, 12139, 11866,
    11585, 11297, 11002, 10701, 10393, 10079, 9759, 9434,
    9102, 8765, 8423, 8075, 7723, 7366, 7005, 6639,
    6269, 5896, 5519, 5139, 4756, 4369, 3980, 3589,
    3196, 2801, 2404, 2005, 1605, 1205, 803, 402,
    0, -402, -803, -1205, -1605, -2005, -2404, -2801,
    -3196, -3589, -3980, -4369, -4756, -5139, -5519, -5896,
    -6269, -6639, -7005, -7366, -7723, -8075, -8423, -8765,
    -9102, -9434, -9759, -10079, -10393, -10701, -11002, -11297,
    -11585, -11866, -12139, -12406, -12665, -12916, -13159, -13395,
    -13622, -13842, -14053, -14255, -14449, -14634, -14810, -14978,
    -15136, -15286, -15426, -15557, -15678, -15790, -15892, -15985,
    -16069, -16142, -16206, -16260, -16305, -16339, -16364, -16379,
    -16384, -16379, -16364, -16339, -16305, -16260, -16206, -16142,
    -16069, -15985, -15892, -15790, -15678, -15557, -15426, -15286,
    -15136, -14978, -14810, -14634, -14449, -14255, -14053, -13842,
    -13622, -13395, -13159, -12916, -12665, -12406, -12139, -11866,
    -11585, -11297, -11002, -10701, -10393, -10079, -9759, -9434,
    -9102, -8765, -8423, -8075, -7723, -7366, -7005, -6639,
    -6269, -5896, -5519, -5139, -4756, -4369, -3980, -3589,
    -3196, -2801, -2404, -2005, -1605, -1205, -803, -402,
};

static int32_t sin_lut(uint32_t index) {
    return sine_table[index & 0xFF];
}

static int32_t cos_lut(uint32_t index) {
    return sine_table[(index + 64) & 0xFF];
}

void hle_div(gba_t *g, int32_t num, int32_t den) {
    arm_cpu_t *c = &g->cpu;
    if (den == 0) {
        /* The real BIOS never returns from a division by zero; return something harmless. */
        c->r[0] = num < 0 ? 0xFFFFFFFFu : 1;
        c->r[1] = (uint32_t)num;
        c->r[3] = 1;
        return;
    }
    int64_t q = (int64_t)num / den;
    int64_t r = (int64_t)num % den;
    c->r[0] = (uint32_t)q;
    c->r[1] = (uint32_t)r;
    c->r[3] = (uint32_t)(q < 0 ? -q : q);
}

uint32_t hle_sqrt(uint32_t value) {
    uint32_t result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* A 32-bit multiply that wraps like the ARM's MUL (no signed-overflow undefined behaviour). */
static int32_t mul32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

/* The BIOS's polynomial approximation of atan, in its 32-bit arithmetic. */
int32_t hle_arctan(int32_t tan, int32_t *r1, int32_t *r3) {
    int32_t a = -(mul32(tan, tan) >> 14);
    int32_t b = (mul32(0xA9, a) >> 14) + 0x390;
    b = (mul32(b, a) >> 14) + 0x91C;
    b = (mul32(b, a) >> 14) + 0xFB6;
    b = (mul32(b, a) >> 14) + 0x16AA;
    b = (mul32(b, a) >> 14) + 0x2081;
    b = (mul32(b, a) >> 14) + 0x3651;
    b = (mul32(b, a) >> 14) + 0xA2F9;
    if (r1) *r1 = a;
    if (r3) *r3 = b;
    return (int16_t)(mul32(tan, b) >> 16);
}

/* (n << 14) / d in 32 bits, as the BIOS computes the ratio. */
static int32_t ratio(int32_t n, int32_t d) {
    int32_t shifted = (int32_t)((uint32_t)n << 14);
    if (d == -1) return (int32_t)(0u - (uint32_t)shifted); /* INT32_MIN / -1 wraps */
    return shifted / d;
}

/* Reduce to an octant where |ratio| <= 1, then add that octant's base angle. */
uint32_t hle_arctan2(int32_t x, int32_t y, int32_t *r1) {
    if (y == 0) return x >= 0 ? 0 : 0x8000;
    if (x == 0) return y >= 0 ? 0x4000 : 0xC000;
    int64_t xs = x, ys = y; /* -x cannot overflow in 64 bits */
    int32_t r;
    if (ys >= 0) {
        if (xs >= 0 && xs >= ys) r = hle_arctan(ratio(y, x), r1, NULL);
        else if (xs < 0 && -xs >= ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x8000;
        else r = 0x4000 - hle_arctan(ratio(x, y), r1, NULL);
    } else {
        if (xs <= 0 && -xs > -ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x8000;
        else if (xs > 0 && xs >= -ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x10000;
        else r = 0xC000 - hle_arctan(ratio(x, y), r1, NULL);
    }
    return (uint32_t)r & 0xFFFF;
}

/* ---- how long the BIOS routines take (mGBA's measured formulas) ---- */

/* The multiplier's early-termination cycles for one MUL with this multiplier value. */
static uint32_t mul_wait(int32_t r) {
    uint32_t u = (uint32_t)r;
    if ((u & 0xFFFFFF00u) == 0xFFFFFF00u || !(u & 0xFFFFFF00u)) return 1;
    if ((u & 0xFFFF0000u) == 0xFFFF0000u || !(u & 0xFFFF0000u)) return 2;
    if ((u & 0xFF000000u) == 0xFF000000u || !(u & 0xFF000000u)) return 3;
    return 4;
}

static int clz32(uint32_t v) {
    return v ? __builtin_clz(v) : 32;
}

uint32_t hle_div_cycles(int32_t num, int32_t den) {
    int loops = clz32((uint32_t)den) - clz32((uint32_t)num);
    if (loops < 1) loops = 1;
    return 4 + 13 * (uint32_t)loops + 7;
}

uint32_t hle_sqrt_cycles(uint32_t x) {
    if (!x) return 53;
    uint32_t cycles = 15, upper = x, bound = 1;
    while (bound < upper) {
        upper >>= 1;
        bound <<= 1;
        cycles += 6;
    }
    for (;;) {
        cycles += 6;
        upper = x;
        uint32_t accum = 0, lower = bound;
        for (;;) {
            cycles += 5;
            uint32_t old = lower;
            if (lower <= upper >> 1) lower <<= 1;
            if (old >= upper >> 1) break;
        }
        for (;;) {
            cycles += 8;
            accum <<= 1;
            if (upper >= lower) {
                accum++;
                upper -= lower;
            }
            if (lower == bound) break;
            lower >>= 1;
        }
        uint32_t old_bound = bound;
        bound = (bound + accum) >> 1;
        if (bound >= old_bound) break;
    }
    return cycles;
}

uint32_t hle_arctan_cycles(int32_t tan) {
    uint32_t cycles = 37 + mul_wait(mul32(tan, tan));
    int32_t a = -(mul32(tan, tan) >> 14);
    cycles += mul_wait(mul32(0xA9, a));
    int32_t b = (mul32(0xA9, a) >> 14) + 0x390;
    static const int32_t terms[6] = {0x91C, 0xFB6, 0x16AA, 0x2081, 0x3651, 0xA2F9};
    for (int i = 0; i < 6; i++) {
        cycles += mul_wait(mul32(b, a));
        b = (mul32(b, a) >> 14) + terms[i];
    }
    return cycles;
}

uint32_t hle_arctan2_cycles(int32_t x, int32_t y) {
    if (x == 0 || y == 0) return 11;
    int64_t xs = x, ys = y;
    bool y_over_x = (ys >= 0) ? ((xs >= 0 && xs >= ys) || (xs < 0 && -xs >= ys))
                              : ((xs <= 0 && -xs > -ys) || (xs > 0 && xs >= -ys));
    return hle_arctan_cycles(y_over_x ? ratio(y, x) : ratio(x, y));
}

static void affine_params(int32_t sx, int32_t sy, uint32_t angle, int32_t out[4]) {
    uint32_t theta = (angle >> 8) & 0xFF;
    out[0] = (sx * cos_lut(theta)) >> 14;    /* pa */
    out[1] = -((sx * sin_lut(theta)) >> 14); /* pb */
    out[2] = (sy * sin_lut(theta)) >> 14;    /* pc */
    out[3] = (sy * cos_lut(theta)) >> 14;    /* pd */
}

/* r0 = source (20-byte entries), r1 = destination (16-byte entries), r2 = count */
void hle_bg_affine_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1];
    for (uint32_t i = 0; i < c->r[2]; i++, src += 20, dst += 16) {
        int32_t cx = (int32_t)bus_read32(g, src, false);
        int32_t cy = (int32_t)bus_read32(g, src + 4, false);
        int32_t dx = (int16_t)bus_read16(g, src + 8, false);
        int32_t dy = (int16_t)bus_read16(g, src + 10, false);
        int32_t sx = (int16_t)bus_read16(g, src + 12, false);
        int32_t sy = (int16_t)bus_read16(g, src + 14, false);
        int32_t p[4];
        affine_params(sx, sy, bus_read16(g, src + 16, false), p);
        for (int k = 0; k < 4; k++) bus_write16(g, dst + (uint32_t)k * 2, (uint16_t)p[k], false);
        /* in 32 bits with wrap-around, as the BIOS's own arithmetic */
        uint32_t ux = (uint32_t)p[0] * (uint32_t)dx + (uint32_t)p[1] * (uint32_t)dy;
        uint32_t uy = (uint32_t)p[2] * (uint32_t)dx + (uint32_t)p[3] * (uint32_t)dy;
        bus_write32(g, dst + 8, (uint32_t)cx - ux, false);
        bus_write32(g, dst + 12, (uint32_t)cy - uy, false);
    }
}

/* r0 = source (8-byte entries), r1 = destination, r2 = count, r3 = byte stride between pa/pb/pc/pd */
void hle_obj_affine_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], stride = c->r[3];
    for (uint32_t i = 0; i < c->r[2]; i++, src += 8, dst += stride * 4) {
        int32_t sx = (int16_t)bus_read16(g, src, false);
        int32_t sy = (int16_t)bus_read16(g, src + 2, false);
        int32_t p[4];
        affine_params(sx, sy, bus_read16(g, src + 4, false), p);
        for (uint32_t k = 0; k < 4; k++) bus_write16(g, dst + k * stride, (uint16_t)p[k], false);
    }
}
