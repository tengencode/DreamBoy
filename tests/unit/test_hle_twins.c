/* The built-in BIOS against the real one (milestone 13): the same call, with random inputs, in two machines — one with
 * the built-in BIOS, one whose BIOS area holds the user's own BIOS (tests/bios/gba_bios.bin, never committed; these
 * tests skip without it) — must leave the same memory and registers behind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static uint8_t *real_bios(void) {
    static uint8_t bios[0x4000];
    static int state; /* 0 not read, 1 read, -1 missing */
    if (!state) {
        FILE *f = fopen(GBA_TEST_BIOS, "rb");
        state = f && fread(bios, 1, sizeof bios, f) == sizeof bios ? 1 : -1;
        if (f) fclose(f);
        if (state < 0) printf("  (skipped: no tests/bios/gba_bios.bin)\n");
    }
    return state > 0 ? bios : NULL;
}

static uint32_t rng = 12345;
static uint32_t next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* "swi n; b ." in ROM, EWRAM and IWRAM filled with the same random bytes, r0-r3 given. */
static gba_t *machine(uint32_t swi, const uint32_t regs[4], uint32_t seed) {
    const uint32_t ops[] = {0xEF000000 | swi << 16, 0xEAFFFFFE};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    uint32_t r = seed;
    for (size_t i = 0; i < sizeof g->bus.ewram; i++) g->bus.ewram[i] = (uint8_t)((r = r * 1103515245u + 12345u) >> 16);
    for (size_t i = 0; i < 0x7E00; i++) g->bus.iwram[i] = (uint8_t)((r = r * 1103515245u + 12345u) >> 16);
    for (int i = 0; i < 4; i++) g->cpu.r[i] = regs[i];
    return g;
}

/* Runs to "b ."; the cycles it took. */
static uint64_t run_call(gba_t *g) {
    uint64_t start = g->now;
    for (int i = 0; i < 200000 && g->cpu.r[15] != 0x08000004; i++) cpu_step(g);
    return g->now - start;
}

typedef struct {
    int mismatches;
    double hle_cycles, bios_cycles;
} twin_result_t;

/* Bytes placed at an address in both machines before the call (a compressed stream, an info block). */
static const uint8_t *blob;
static size_t blob_len;
static uint32_t blob_at;

static void put_blob(gba_t *g) {
    for (size_t i = 0; i < blob_len; i++) bus_write8(g, blob_at + (uint32_t)i, blob[i], false);
}

/* One call in both machines; prints the first differences. outputs: how many of r0-r3 are results to compare. */
static void twin(twin_result_t *res, const char *what, uint32_t swi, const uint32_t regs[4], int outputs) {
    uint32_t seed = next_random();
    gba_t *a = machine(swi, regs, seed), *b = machine(swi, regs, seed);
    put_blob(a);
    put_blob(b);
    memcpy(b->bus.bios, real_bios(), 0x4000);
    b->hle_bios = false;
    res->hle_cycles += (double)run_call(a);
    res->bios_cycles += (double)run_call(b);
    const char *diff = NULL;
    size_t at = 0;
    for (int i = 0; i < outputs && !diff; i++)
        if (a->cpu.r[i] != b->cpu.r[i]) diff = "a result register", at = (size_t)i;
    if (!diff && memcmp(a->bus.ewram, b->bus.ewram, sizeof a->bus.ewram)) {
        diff = "EWRAM";
        while (a->bus.ewram[at] == b->bus.ewram[at]) at++;
    }
    if (!diff && memcmp(a->bus.iwram, b->bus.iwram, 0x7E00)) { /* the top 0x200 bytes are BIOS state */
        diff = "IWRAM";
        while (a->bus.iwram[at] == b->bus.iwram[at]) at++;
    }
    if (!diff && memcmp(a->bus.vram, b->bus.vram, sizeof a->bus.vram)) {
        diff = "VRAM";
        while (a->bus.vram[at] == b->bus.vram[at]) at++;
    }
    if (!diff && memcmp(a->bus.oam, b->bus.oam, sizeof a->bus.oam)) {
        diff = "OAM";
        while (a->bus.oam[at] == b->bus.oam[at]) at++;
    }
    if (diff && res->mismatches++ < 6) {
        printf("  %s r0=%08x r1=%08x r2=%08x r3=%08x: %s differs at %#zx\n", what, regs[0], regs[1], regs[2], regs[3],
               diff, at);
        const uint8_t *ma = !strcmp(diff, "EWRAM") ? a->bus.ewram : !strcmp(diff, "IWRAM") ? a->bus.iwram : NULL;
        const uint8_t *mb = !strcmp(diff, "EWRAM") ? b->bus.ewram : !strcmp(diff, "IWRAM") ? b->bus.iwram : NULL;
        if (ma) {
            size_t from = at & ~(size_t)1;
            printf("    built-in:");
            for (size_t k = 0; k < 8; k += 2) printf(" %04x", ma[from + k] | ma[from + k + 1] << 8);
            printf("  BIOS:");
            for (size_t k = 0; k < 8; k += 2) printf(" %04x", mb[from + k] | mb[from + k + 1] << 8);
            uint32_t src = regs[0] & 0x00FFFFFF;
            const uint8_t *ms = (regs[0] >> 24) == 2 ? a->bus.ewram : a->bus.iwram;
            printf("  input sx sy angle: %04x %04x %04x\n", ms[src] | ms[src + 1] << 8, ms[src + 2] | ms[src + 3] << 8,
                   ms[src + 4] | ms[src + 5] << 8);
        }
    }
    gba_destroy(a);
    gba_destroy(b);
}

/* A random address of a unit-aligned buffer in EWRAM, IWRAM or ROM (sources only) with room for bytes. */
static uint32_t buffer(uint32_t bytes, uint32_t align, bool source) {
    uint32_t kind = next_random() % (source ? 3 : 2);
    uint32_t base = kind == 0 ? 0x02000000 : kind == 1 ? 0x03000000 : 0x08000000;
    uint32_t room = kind == 0 ? 0x40000 : kind == 1 ? 0x7E00 : 0x200;
    uint32_t off = room > bytes ? next_random() % (room - bytes) : 0;
    return base + (off & ~(align - 1));
}

static void report(const char *what, const twin_result_t *r, int calls) {
    printf("  %s: %d calls, %d differ; cycles built-in %.0f vs BIOS %.0f per call\n", what, calls, r->mismatches,
           r->hle_cycles / calls, r->bios_cycles / calls);
}

TEST(hle_twin_cpuset_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t r = {0};
    const int calls = 120;
    for (int i = 0; i < calls; i++) {
        bool words = next_random() & 1, fill = next_random() & 1;
        uint32_t count = next_random() % (i < 10 ? 4 : 0x300), unit = words ? 4 : 2;
        uint32_t ctrl = count | (fill ? 1u << 24 : 0) | (words ? 1u << 26 : 0);
        uint32_t regs[4] = {buffer(fill ? unit : count * unit, unit, true), buffer(count * unit, unit, false), ctrl,
                            next_random()};
        twin(&r, "CpuSet", 0x0B, regs, 0);
    }
    report("CpuSet", &r, calls);
    CHECK_EQ(r.mismatches, 0);
}

TEST(hle_twin_cpufastset_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t r = {0};
    const int calls = 120;
    for (int i = 0; i < calls; i++) {
        bool fill = next_random() & 1;
        uint32_t count = next_random() % (i < 10 ? 4 : 0x300);
        uint32_t ctrl = count | (fill ? 1u << 24 : 0);
        uint32_t room = ((count + 7) & ~7u) * 4;
        uint32_t regs[4] = {buffer(fill ? 4 : room, 4, true), buffer(room, 4, false), ctrl, next_random()};
        twin(&r, "CpuFastSet", 0x0C, regs, 0);
    }
    report("CpuFastSet", &r, calls);
    CHECK_EQ(r.mismatches, 0);
}

TEST(hle_twin_objaffineset_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t r = {0};
    const int calls = 120;
    for (int i = 0; i < calls; i++) {
        uint32_t count = 1 + next_random() % 4, stride = (next_random() & 1) ? 2 : 8;
        uint32_t regs[4] = {buffer(count * 8, 2, false), buffer(count * stride * 4, 2, false), count, stride};
        twin(&r, "ObjAffineSet", 0x0F, regs, 0);
    }
    report("ObjAffineSet", &r, calls);
    CHECK_EQ(r.mismatches, 0);
}

TEST(hle_twin_bgaffineset_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t r = {0};
    const int calls = 60;
    for (int i = 0; i < calls; i++) {
        uint32_t count = 1 + next_random() % 3;
        uint32_t regs[4] = {buffer(count * 20, 4, false), buffer(count * 16, 4, false), count, next_random()};
        twin(&r, "BgAffineSet", 0x0E, regs, 0);
    }
    report("BgAffineSet", &r, calls);
    CHECK_EQ(r.mismatches, 0);
}

/* ---- decompression: random valid streams at 0x02030000, output in EWRAM 0x02000000 or VRAM ---- */

static uint8_t stream[0x4000];

static void decompress_twin(twin_result_t *r, const char *what, uint32_t swi, size_t len, bool vram) {
    blob = stream;
    blob_len = len;
    blob_at = 0x02030000;
    uint32_t regs[4] = {0x02030000, vram ? 0x06000000 : 0x02000000, next_random(), next_random()};
    twin(r, what, swi, regs, 0);
    blob_len = 0;
}

/* LZ77: literals and back-references (VRAM: never a distance of 1, which real games avoid there). */
static size_t make_lz77(uint32_t size, bool vram) {
    size_t n = 0;
    stream[n++] = 0x10;
    stream[n++] = (uint8_t)size;
    stream[n++] = (uint8_t)(size >> 8);
    stream[n++] = (uint8_t)(size >> 16);
    uint32_t made = 0;
    while (made < size) {
        size_t flag_at = n++;
        uint8_t flags = 0;
        for (int bit = 7; bit >= 0 && made < size; bit--) {
            uint32_t min_disp = vram ? 2 : 1, left = size - made;
            if (made >= min_disp && left >= 3 && (next_random() & 1)) {
                uint32_t len = 3 + next_random() % 16, disp = min_disp + next_random() % (made - min_disp + 1);
                if (disp > 4096) disp = 4096;
                if (len > left) len = left;
                flags |= (uint8_t)(1 << bit);
                stream[n++] = (uint8_t)((len - 3) << 4 | (disp - 1) >> 8);
                stream[n++] = (uint8_t)(disp - 1);
                made += len;
            } else {
                stream[n++] = (uint8_t)next_random();
                made++;
            }
        }
        stream[flag_at] = flags;
    }
    return n;
}

/* Run-length: runs of 3-130 bytes and literal groups of 1-128, never past size. */
static size_t make_rl(uint32_t size) {
    size_t n = 0;
    stream[n++] = 0x30;
    stream[n++] = (uint8_t)size;
    stream[n++] = (uint8_t)(size >> 8);
    stream[n++] = (uint8_t)(size >> 16);
    uint32_t made = 0;
    while (made < size) {
        uint32_t left = size - made;
        if (left >= 3 && (next_random() & 1)) {
            uint32_t len = 3 + next_random() % 128;
            if (len > left) len = left;
            stream[n++] = (uint8_t)(0x80 | (len - 3));
            stream[n++] = (uint8_t)next_random();
            made += len;
        } else {
            uint32_t len = 1 + next_random() % 128;
            if (len > left) len = left;
            stream[n++] = (uint8_t)(len - 1);
            for (uint32_t i = 0; i < len; i++) stream[n++] = (uint8_t)next_random();
            made += len;
        }
    }
    return n;
}

/* Differences: the header then size bytes of data (any data is valid). */
static size_t make_diff(uint32_t size, bool words) {
    size_t n = 0;
    stream[n++] = words ? 0x82 : 0x81;
    stream[n++] = (uint8_t)size;
    stream[n++] = (uint8_t)(size >> 8);
    stream[n++] = (uint8_t)(size >> 16);
    for (uint32_t i = 0; i < size; i++) stream[n++] = (uint8_t)next_random();
    return n;
}

TEST(hle_twin_lz77_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t w = {0}, v = {0};
    for (int i = 0; i < 60; i++) {
        uint32_t size = 4 + next_random() % 0x800;
        decompress_twin(&w, "LZ77UnCompWram", 0x11, make_lz77(size, false), false);
        decompress_twin(&v, "LZ77UnCompVram", 0x12, make_lz77(size & ~1u, true), true);
    }
    report("LZ77UnCompWram", &w, 60);
    report("LZ77UnCompVram", &v, 60);
    CHECK_EQ(w.mismatches + v.mismatches, 0);
}

TEST(hle_twin_rl_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t w = {0}, v = {0};
    for (int i = 0; i < 60; i++) {
        uint32_t size = 4 + next_random() % 0x800;
        decompress_twin(&w, "RLUnCompWram", 0x14, make_rl(size), false);
        decompress_twin(&v, "RLUnCompVram", 0x15, make_rl(size & ~1u), true);
    }
    report("RLUnCompWram", &w, 60);
    report("RLUnCompVram", &v, 60);
    CHECK_EQ(w.mismatches + v.mismatches, 0);
}

TEST(hle_twin_diff_filters_match_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t w = {0}, v = {0}, h = {0};
    for (int i = 0; i < 40; i++) {
        uint32_t size = 4 + next_random() % 0x800;
        decompress_twin(&w, "Diff8bitUnFilterWram", 0x16, make_diff(size, false), false);
        decompress_twin(&v, "Diff8bitUnFilterVram", 0x17, make_diff(size & ~1u, false), true);
        decompress_twin(&h, "Diff16bitUnFilter", 0x18, make_diff(size & ~1u, true), false);
    }
    report("Diff8bitUnFilterWram", &w, 40);
    report("Diff8bitUnFilterVram", &v, 40);
    report("Diff16bitUnFilter", &h, 40);
    CHECK_EQ(w.mismatches + v.mismatches + h.mismatches, 0);
}

TEST(hle_twin_bitunpack_matches_the_real_bios) {
    if (!real_bios()) return;
    static const uint8_t src_widths[] = {1, 2, 4, 8}, dst_widths[] = {1, 2, 4, 8, 16, 32};
    twin_result_t r = {0};
    const int calls = 80;
    for (int i = 0; i < calls; i++) {
        uint8_t sw = src_widths[next_random() % 4], dw = dst_widths[next_random() % 6];
        if (dw < sw) dw = sw;
        uint32_t len = 1 + next_random() % 64, offset = next_random() & ((next_random() & 1) ? 0x8000000Fu : 0x7Fu);
        /* the info block at 0x02030000, the source bytes after it */
        stream[0] = (uint8_t)len;
        stream[1] = (uint8_t)(len >> 8);
        stream[2] = sw;
        stream[3] = dw;
        for (int k = 0; k < 4; k++) stream[4 + k] = (uint8_t)(offset >> (8 * k));
        for (uint32_t k = 0; k < len; k++) stream[8 + k] = (uint8_t)next_random();
        blob = stream;
        blob_len = 8 + len;
        blob_at = 0x02030000;
        uint32_t regs[4] = {0x02030008, 0x02000000, 0x02030000, next_random()};
        twin(&r, "BitUnPack", 0x10, regs, 0);
        blob_len = 0;
    }
    report("BitUnPack", &r, calls);
    CHECK_EQ(r.mismatches, 0);
}

TEST(hle_twin_math_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t div = {0}, divarm = {0}, sq = {0}, at = {0}, at2 = {0};
    const int calls = 120;
    for (int i = 0; i < calls; i++) {
        int32_t num = (int32_t)next_random() >> (next_random() % 31);
        int32_t den = (int32_t)next_random() >> (next_random() % 31);
        if (den == 0) den = 7;
        uint32_t d[4] = {(uint32_t)num, (uint32_t)den, 0, 0};
        twin(&div, "Div", 0x06, d, 4);
        uint32_t da[4] = {(uint32_t)den, (uint32_t)num, 0, 0};
        twin(&divarm, "DivArm", 0x07, da, 4);
        uint32_t s[4] = {next_random() >> (next_random() % 32), 0, 0, 0};
        twin(&sq, "Sqrt", 0x08, s, 1);
        uint32_t t[4] = {(uint32_t)(int32_t)(int16_t)(next_random() % 0x8001 - 0x4000), 0, 0, 0}; /* |tan| <= 1.0 */
        twin(&at, "ArcTan", 0x09, t, 4);
        uint32_t t2[4] = {(uint32_t)(int32_t)(int16_t)next_random(), (uint32_t)(int32_t)(int16_t)next_random(), 0, 0};
        twin(&at2, "ArcTan2", 0x0A, t2, 2);
    }
    report("Div", &div, calls);
    report("DivArm", &divarm, calls);
    report("Sqrt", &sq, calls);
    report("ArcTan", &at, calls);
    report("ArcTan2", &at2, calls);
    CHECK_EQ(div.mismatches + divarm.mismatches + sq.mismatches + at.mismatches + at2.mismatches, 0);
}

TEST(hle_twin_registerramreset_matches_the_real_bios) {
    if (!real_bios()) return;
    twin_result_t r = {0};
    for (uint32_t flags = 0; flags < 0x100; flags += 0x11) {
        uint32_t regs[4] = {flags, 0, 0, 0};
        twin(&r, "RegisterRamReset", 0x01, regs, 0);
    }
    report("RegisterRamReset", &r, 16);
    CHECK_EQ(r.mismatches, 0);
}
