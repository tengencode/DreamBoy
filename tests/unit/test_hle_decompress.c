#include <stddef.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"

#define SRC 0x02000000u
#define DST 0x02001000u

static gba_t *with_source(const uint8_t *bytes, size_t n, uint32_t dst) {
    gba_t *g = gba_create(NULL);
    for (size_t i = 0; i < n; i++) bus_write8(g, SRC + (uint32_t)i, bytes[i], false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = dst;
    return g;
}

static bool memory_equals(gba_t *g, uint32_t addr, const uint8_t *want, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (bus_read8(g, addr + (uint32_t)i, false) != want[i]) return false;
    }
    return true;
}

/* header 0x10 | size 9; flags 0b00010000; 'A' 'B' 'C'; block (len 6, disp 3) */
static const uint8_t lz77_src[] = {0x10, 0x09, 0x00, 0x00, 0x10, 'A', 'B', 'C', 0x30, 0x02};
static const uint8_t lz77_out[] = {'A', 'B', 'C', 'A', 'B', 'C', 'A', 'B', 'C'};

TEST(hle_lz77_wram) {
    gba_t *g = with_source(lz77_src, sizeof lz77_src, DST);
    hle_lz77(g, false);
    CHECK(memory_equals(g, DST, lz77_out, sizeof lz77_out));
    gba_destroy(g);
}

TEST(hle_lz77_vram_writes_halfwords) {
    gba_t *g = with_source(lz77_src, sizeof lz77_src, 0x06000000);
    hle_lz77(g, true);
    CHECK(memory_equals(g, 0x06000000, lz77_out, sizeof lz77_out));
    CHECK_EQ(bus_read8(g, 0x06000009, false), 0); /* odd tail byte padded with zero */
    gba_destroy(g);
}

/* header 0x28 (8-bit data) | size 4; tree size byte 1; root 0xC0 (both children are data): 'A', 'B';
 * bitstream word 0x60000000 = bits 0,1,1,0 → "ABBA" */
TEST(hle_huffman_8bit) {
    static const uint8_t src[] = {0x28, 0x04, 0x00, 0x00, 0x01, 0xC0, 'A', 'B', 0x00, 0x00, 0x00, 0x60};
    static const uint8_t want[] = {'A', 'B', 'B', 'A'};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_huffman(g);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x30 | size 7; run of 4 x 0xAA (flag 0x81); 3 literal bytes (flag 0x02) */
TEST(hle_run_length_wram) {
    static const uint8_t src[] = {0x30, 0x07, 0x00, 0x00, 0x81, 0xAA, 0x02, 0x01, 0x02, 0x03};
    static const uint8_t want[] = {0xAA, 0xAA, 0xAA, 0xAA, 0x01, 0x02, 0x03};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_rl(g, false);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x81 (8-bit deltas) | size 4 */
TEST(hle_diff8_wram) {
    static const uint8_t src[] = {0x81, 0x04, 0x00, 0x00, 0x10, 0x01, 0x01, 0xFF};
    static const uint8_t want[] = {0x10, 0x11, 0x12, 0x11};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_diff8(g, false);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x82 (16-bit deltas) | size 6 bytes */
TEST(hle_diff16_halfwords) {
    static const uint8_t src[] = {0x82, 0x06, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0xFF, 0xFF};
    static const uint8_t want[] = {0x00, 0x10, 0x01, 0x10, 0x00, 0x10};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_diff16(g);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}
