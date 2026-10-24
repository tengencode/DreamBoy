#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define SCREEN30 0xF000u /* BG screen block 30 */
#define SCREEN31 0xF800u /* BG screen block 31 */

/* Mode 0 with only BG0 on; backdrop black. */
static gba_t *text_scene(uint16_t bg0cnt) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_BLACK);
    tu_io16(g, 0x000, 0x0100);
    tu_io16(g, 0x008, bg0cnt);
    return g;
}

/* 4bpp tile (char base 0) where every pixel has colour index `index`. */
static void solid_tile4(gba_t *g, uint32_t tile, uint16_t index) {
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, tile * 32 + i, (uint16_t)(index * 0x1111));
}

static void map_entry(gba_t *g, uint32_t screen, int tx, int ty, uint16_t entry) {
    tu_vram16(g, screen + (uint32_t)(ty * 32 + tx) * 2, entry);
}

TEST(text_bg_4bpp_uses_palette_bank) {
    gba_t *g = text_scene(31 << 8);
    tu_vram16(g, 32 + 0, 0x4321); /* tile 1, row 0: indices 1..7 then 0 */
    tu_vram16(g, 32 + 2, 0x0765);
    for (uint32_t i = 1; i <= 7; i++) tu_pal(g, 32 + i, (uint16_t)(0x0100 + i));
    map_entry(g, SCREEN31, 0, 0, (2 << 12) | 1); /* palette bank 2 */
    ppu_render_line(g, 0);
    for (int x = 0; x < 7; x++) CHECK_EQ(tu_px(g, x, 0), 0x0100 + x + 1);
    CHECK_EQ(tu_px(g, 7, 0), C_BLACK);
    gba_destroy(g);
}

TEST(text_bg_8bpp) {
    gba_t *g = text_scene((31 << 8) | 0x80);
    tu_vram16(g, 64 + 0, 0x0201); /* tile 1 (64 bytes), row 0: indices 1..8 */
    tu_vram16(g, 64 + 2, 0x0403);
    tu_vram16(g, 64 + 4, 0x0605);
    tu_vram16(g, 64 + 6, 0x0807);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, i, (uint16_t)(0x0200 + i));
    map_entry(g, SCREEN31, 0, 0, (5 << 12) | 1); /* the palette bank is ignored in 8bpp */
    ppu_render_line(g, 0);
    for (int x = 0; x < 8; x++) CHECK_EQ(tu_px(g, x, 0), 0x0200 + x + 1);
    gba_destroy(g);
}

TEST(text_bg_scroll_wraps_around_the_map) {
    gba_t *g = text_scene(31 << 8);
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1);
    tu_io16(g, 0x010, 250); /* BG0HOFS: screen x 6 is map x 256 = 0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 5, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 6, 0), C_RED);
    CHECK_EQ(tu_px(g, 13, 0), C_RED);
    CHECK_EQ(tu_px(g, 14, 0), C_BLACK);
    tu_io16(g, 0x012, 4); /* BG0VOFS */
    ppu_render_line(g, 4);
    CHECK_EQ(tu_px(g, 6, 4), C_BLACK); /* map y 8 is the next tile row */
    gba_destroy(g);
}

TEST(text_bg_flips) {
    gba_t *g = text_scene(31 << 8);
    tu_vram16(g, 32, 0x0001); /* tile 1: only pixel (0,0) is set */
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1 | 0x400); /* horizontal flip */
    map_entry(g, SCREEN31, 1, 0, 1 | 0x800); /* vertical flip */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    ppu_render_line(g, 7);
    CHECK_EQ(tu_px(g, 8, 7), C_RED);
    gba_destroy(g);
}

TEST(text_bg_512_wide_map_uses_the_next_screen_block) {
    gba_t *g = text_scene((30 << 8) | 0x4000); /* 512x256: blocks 30 (left) and 31 (right) */
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1); /* map x 256 */
    tu_io16(g, 0x010, 256);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    gba_destroy(g);
}

TEST(text_bg_priority_and_layer_order) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0300); /* BG0 and BG1 */
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    tu_pal(g, 17, C_GREEN);
    tu_io16(g, 0x008, (31 << 8) | 1);          /* BG0 priority 1 */
    tu_io16(g, 0x00A, (30 << 8) | 0);          /* BG1 priority 0 */
    map_entry(g, SCREEN31, 0, 0, 1);            /* BG0 red */
    map_entry(g, SCREEN30, 0, 0, (1 << 12) | 1); /* BG1 green */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_GREEN);
    tu_io16(g, 0x00A, (30 << 8) | 1); /* equal priority: BG0 is in front */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(text_bg_mosaic) {
    gba_t *g = text_scene((31 << 8) | 0x40);
    tu_vram16(g, 32 + 0, 0x4321); /* tile 1, row 0: indices 1..8 */
    tu_vram16(g, 32 + 2, 0x8765);
    tu_vram16(g, 32 + 8, 0x0002); /* row 2: pixel 0 = index 2 */
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, i, (uint16_t)(0x0100 + i));
    map_entry(g, SCREEN31, 0, 0, 1);
    tu_io16(g, 0x04C, 0x0003); /* 4-pixel-wide blocks */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), 0x0101);
    CHECK_EQ(tu_px(g, 3, 0), 0x0101);
    CHECK_EQ(tu_px(g, 4, 0), 0x0105);
    CHECK_EQ(tu_px(g, 7, 0), 0x0105);
    tu_io16(g, 0x04C, 0x0030); /* 4-line-tall blocks: line 2 repeats line 0 */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 0, 2), 0x0101);
    tu_io16(g, 0x008, 31 << 8); /* mosaic off: row 2 shows index 2 */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 0, 2), 0x0102);
    gba_destroy(g);
}
