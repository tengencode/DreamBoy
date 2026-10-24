#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define MAP16 0x8000u /* BG screen block 16 */

/* Mode 1 with BG2 on: 128x128 affine map at screen block 16, tile 1 solid red at map (0,0). */
static gba_t *affine_scene(uint16_t extra_cnt) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0401);
    tu_io16(g, 0x00C, (uint16_t)(extra_cnt | (16 << 8)));
    for (uint32_t i = 0; i < 64; i += 2) tu_vram16(g, 64 + i, 0x0101); /* 8bpp tile 1: index 1 */
    tu_pal(g, 1, C_RED);
    tu_vram16(g, MAP16, 0x0001); /* map bytes: (0,0) = tile 1, (1,0) = tile 0 */
    return g;
}

TEST(affine_matrices_default_to_identity) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(tu_raw16(g, 0x020), 0x100); /* BG2PA (write-only: checked in the register store) */
    CHECK_EQ(tu_raw16(g, 0x026), 0x100); /* BG2PD */
    CHECK_EQ(tu_raw16(g, 0x030), 0x100); /* BG3PA */
    CHECK_EQ(tu_raw16(g, 0x036), 0x100); /* BG3PD */
    gba_destroy(g);
}

TEST(affine_bg_identity) {
    gba_t *g = affine_scene(0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    gba_destroy(g);
}

TEST(affine_bg_scaling) {
    gba_t *g = affine_scene(0);
    tu_io16(g, 0x020, 0x80); /* PA = 0.5: twice as wide */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 15, 0), C_RED);
    CHECK_EQ(tu_px(g, 16, 0), C_BLACK);
    gba_destroy(g);
}

TEST(affine_bg_wraparound_flag) {
    gba_t *g = affine_scene(0);
    tu_io32(g, 0x028, 128 << 8); /* BG2X = 128: past the right edge of a 128-pixel map */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    tu_io16(g, 0x00C, (16 << 8) | 0x2000); /* wraparound on */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(affine_reference_reloads_on_write_with_sign_extension) {
    gba_t *g = gba_create(NULL);
    tu_io32(g, 0x028, 0x0FFFFF00); /* 28-bit -256 */
    CHECK_EQ(g->ppu.ref_x[0], -256);
    tu_io32(g, 0x03C, 0x00000500); /* BG3Y */
    CHECK_EQ(g->ppu.ref_y[1], 0x500);
    CHECK_EQ(g->ppu.ref_x[0], -256); /* writing Y leaves X alone */
    gba_destroy(g);
}

TEST(affine_reference_steps_per_line_and_reloads_at_vblank) {
    gba_t *g = affine_scene(0);
    tu_io16(g, 0x026, 0x200); /* PD = 2.0 */
    tu_advance_to(g, PPU_HDRAW_CYCLES); /* line 0 drawn */
    CHECK_EQ(g->ppu.ref_y[0], 0x200);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES); /* VBlank reloads from BG2Y (0) */
    CHECK_EQ(g->ppu.ref_y[0], 0);
    gba_destroy(g);
}

TEST(affine_bg3_in_mode_2) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0802); /* mode 2, BG3 */
    tu_io16(g, 0x00E, 16 << 8);
    for (uint32_t i = 0; i < 64; i += 2) tu_vram16(g, 64 + i, 0x0101);
    tu_pal(g, 1, C_GREEN);
    tu_vram16(g, MAP16, 0x0001);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_GREEN);
    gba_destroy(g);
}
