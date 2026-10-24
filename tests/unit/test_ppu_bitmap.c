#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

TEST(bitmap_mode3_direct_colour) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0403);
    tu_pal(g, 0, C_MAGENTA);
    tu_vram16(g, (2 * 240 + 5) * 2, C_GREEN);
    tu_io32(g, 0x02C, 2 << 8); /* BG2Y: the affine reference only moves as a frame runs */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 5, 2), C_GREEN);
    CHECK_EQ(tu_px(g, 6, 2), C_BLACK); /* colour 0 is opaque black in mode 3 */
    gba_destroy(g);
}

TEST(bitmap_mode4_palette_and_page_flip) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0404);
    tu_pal(g, 0, C_MAGENTA);
    tu_pal(g, 2, C_BLUE);
    tu_pal(g, 3, C_RED);
    tu_vram16(g, 240 + 2, 0x0200);          /* page 0, line 1: (2,1) = 0, (3,1) = 2 */
    tu_vram16(g, 0xA000 + 240 + 2, 0x0300); /* page 1: (3,1) = 3 */
    tu_io32(g, 0x02C, 1 << 8);              /* BG2Y = line 1 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 3, 1), C_BLUE);
    CHECK_EQ(tu_px(g, 2, 1), C_MAGENTA); /* index 0 is transparent */
    tu_io16(g, 0x000, 0x0414);           /* show page 1 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 3, 1), C_RED);
    gba_destroy(g);
}

TEST(bitmap_mode5_small_frame) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0415); /* mode 5, page 1, BG2 */
    tu_pal(g, 0, C_MAGENTA);
    tu_vram16(g, 0xA000 + 10 * 2, C_YELLOW);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 10, 0), C_YELLOW);
    CHECK_EQ(tu_px(g, 160, 0), C_MAGENTA); /* the frame is 160 pixels wide */
    gba_destroy(g);
}

TEST(bitmap_uses_affine_scaling) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0403);
    tu_io16(g, 0x020, 0x80); /* PA = 0.5 */
    tu_vram16(g, 5 * 2, C_RED);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 10, 0), C_RED);
    CHECK_EQ(tu_px(g, 11, 0), C_RED);
    CHECK_EQ(tu_px(g, 12, 0), C_BLACK);
    gba_destroy(g);
}
