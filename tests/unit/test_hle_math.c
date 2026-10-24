#include <stdint.h>

#include "bios/hle.h"
#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"

TEST(hle_div_signed_results) {
    gba_t *g = gba_create(NULL);
    hle_div(g, 100, 7);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK_EQ(g->cpu.r[1], 2);
    CHECK_EQ(g->cpu.r[3], 14);
    hle_div(g, -100, 7);
    CHECK_EQ(g->cpu.r[0], 0xFFFFFFF2u);
    CHECK_EQ(g->cpu.r[1], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[3], 14);
    hle_div(g, INT32_MIN, -1);
    CHECK_EQ(g->cpu.r[0], 0x80000000u);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[3], 0x80000000u);
    hle_div(g, 5, 0);
    CHECK_EQ(g->cpu.r[0], 1);
    CHECK_EQ(g->cpu.r[1], 5);
    CHECK_EQ(g->cpu.r[3], 1);
    gba_destroy(g);
}

TEST(hle_sqrt_integer_root) {
    CHECK_EQ(hle_sqrt(0), 0);
    CHECK_EQ(hle_sqrt(2), 1);
    CHECK_EQ(hle_sqrt(0x10000), 0x100);
    CHECK_EQ(hle_sqrt(0xFFFFFFFF), 0xFFFF);
}

TEST(hle_arctan_matches_bios_polynomial) {
    CHECK_EQ(hle_arctan(0, NULL, NULL), 0);
    CHECK_EQ(hle_arctan(0x1000, NULL, NULL), 2555);
    CHECK_EQ(hle_arctan(0x2000, NULL, NULL), 4836);
    CHECK_EQ(hle_arctan(0x4000, NULL, NULL), 0x2000);
    CHECK_EQ(hle_arctan(-0x4000, NULL, NULL), -0x2000);
}

/* Rows of the mGBA suite's BIOS math table (measured on hardware): r0, r1 and r3 after SWI 0x09. */
TEST(hle_arctan_wraps_like_the_bios_and_leaves_r1_r3) {
    static const struct {
        int32_t in;
        uint32_t r0, r1, r3;
    } rows[] = {
        {0x0000, 0x0000, 0x00000000, 0xA2F9},     {0x3FFF, 0x1FFF, 0xFFFFC002, 0x8001},
        {0x7FFF, 0x16D8, 0xFFFF0004, 0x22DB6},    {0x8000, 0x16A2, 0xFFFF0000, 0x22D45},
        {0xC000, 0xFFFFC360, 0x0001C000, 0x10480}, {0xFFFF, 0xFFFFA2FE, 0x00000008, 0xA2FF},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        int32_t r1, r3;
        CHECK_EQ((uint32_t)hle_arctan(rows[i].in, &r1, &r3), rows[i].r0);
        CHECK_EQ((uint32_t)r1, rows[i].r1);
        CHECK_EQ((uint32_t)r3, rows[i].r3);
    }
}

TEST(hle_arctan2_quadrants) {
    int32_t r1 = 0;
    CHECK_EQ(hle_arctan2(0x100, 0x100, &r1), 0x2000);
    CHECK_EQ(hle_arctan2(-0x100, 0x100, &r1), 0x6000);
    CHECK_EQ(hle_arctan2(-0x100, -0x100, &r1), 0xA000);
    CHECK_EQ(hle_arctan2(0x100, -0x100, &r1), 0xE000);
    CHECK_EQ(hle_arctan2(5, 0, &r1), 0);
    CHECK_EQ(hle_arctan2(-5, 0, &r1), 0x8000);
    CHECK_EQ(hle_arctan2(0, 5, &r1), 0x4000);
    CHECK_EQ(hle_arctan2(0, -5, &r1), 0xC000);
    CHECK_EQ(hle_arctan2(0x1000, 0x800, &r1), 0x12E4);
}

/* Suite rows for SWI 0x0A (x, y → r0, r1; r3 is always 0x170): r1 is the inner ArcTan's `a`, or
 * y unchanged when x or y is 0. */
TEST(hle_arctan2_swi_leaves_r1_r3_like_the_bios) {
    static const struct {
        uint32_t x, y, r0, r1;
    } rows[] = {
        {0x00004000, 0x00000000, 0x0000, 0x00000000}, {0xFFFFFFFF, 0x00000001, 0x6000, 0xFFFFC000},
        {0x00000000, 0x00004000, 0x4000, 0x00004000}, {0xFFFF8000, 0x00004000, 0x6D1C, 0xFFFFF000},
        {0x00004000, 0xFFFF0000, 0xC9FC, 0xFFFFFC00}, {0xFFFFC000, 0xFFFF0000, 0xB605, 0xFFFFFC00},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        gba_t *g = gba_create(NULL);
        g->cpu.r[0] = rows[i].x;
        g->cpu.r[1] = rows[i].y;
        hle_swi(g, 0x0A);
        CHECK_EQ(g->cpu.r[0], rows[i].r0);
        CHECK_EQ(g->cpu.r[1], rows[i].r1);
        CHECK_EQ(g->cpu.r[3], 0x170);
        gba_destroy(g);
    }
}

TEST(hle_obj_affine_set_rotates_90_degrees) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x100, false); /* x scale 1.0 */
    bus_write16(g, 0x02000002, 0x200, false); /* y scale 2.0 */
    bus_write16(g, 0x02000004, 0x4000, false); /* 90 degrees */
    g->cpu.r[0] = 0x02000000;
    g->cpu.r[1] = 0x02000100;
    g->cpu.r[2] = 1;
    g->cpu.r[3] = 8; /* OAM-style stride */
    hle_obj_affine_set(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x0000); /* pa */
    CHECK_EQ(bus_read16(g, 0x02000108, false), 0xFF00); /* pb = -1.0 */
    CHECK_EQ(bus_read16(g, 0x02000110, false), 0x0200); /* pc = 2.0 */
    CHECK_EQ(bus_read16(g, 0x02000118, false), 0x0000); /* pd */
    gba_destroy(g);
}

TEST(hle_bg_affine_set_offsets_the_origin) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x1000, false); /* texture center x (19.8) */
    bus_write32(g, 0x02000004, 0x800, false);  /* texture center y */
    bus_write16(g, 0x02000008, 8, false);      /* display center x */
    bus_write16(g, 0x0200000A, 4, false);      /* display center y */
    bus_write16(g, 0x0200000C, 0x100, false);  /* x scale 1.0 */
    bus_write16(g, 0x0200000E, 0x100, false);  /* y scale 1.0 */
    bus_write16(g, 0x02000010, 0, false);      /* angle */
    g->cpu.r[0] = 0x02000000;
    g->cpu.r[1] = 0x02000100;
    g->cpu.r[2] = 1;
    hle_bg_affine_set(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x100);
    CHECK_EQ(bus_read16(g, 0x02000102, false), 0);
    CHECK_EQ(bus_read16(g, 0x02000104, false), 0);
    CHECK_EQ(bus_read16(g, 0x02000106, false), 0x100);
    CHECK_EQ(bus_read32(g, 0x02000108, false), 0x800);
    CHECK_EQ(bus_read32(g, 0x0200010C, false), 0x400);
    gba_destroy(g);
}
