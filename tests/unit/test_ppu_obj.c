#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define OBJ_TILES 0x10000u

static gba_t *obj_scene(uint16_t dispcnt) {
    gba_t *g = gba_create(NULL);
    tu_hide_sprites(g);
    tu_io16(g, 0x000, dispcnt);
    tu_pal(g, 0, C_BLACK);
    return g;
}

/* 4bpp sprite tile where every pixel has colour index `index`. */
static void obj_solid_tile4(gba_t *g, uint32_t tile, uint16_t index) {
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, OBJ_TILES + tile * 32 + i, (uint16_t)(index * 0x1111));
}

static void sprite(gba_t *g, uint32_t i, uint16_t a0, uint16_t a1, uint16_t a2) {
    tu_oam16(g, i * 8, a0);
    tu_oam16(g, i * 8 + 2, a1);
    tu_oam16(g, i * 8 + 4, a2);
}

/* Affine parameter set 0 lives in the 4th halfword of OAM entries 0-3. */
static void affine_params0(gba_t *g, uint16_t pa, uint16_t pb, uint16_t pc, uint16_t pd) {
    tu_oam16(g, 6, pa);
    tu_oam16(g, 14, pb);
    tu_oam16(g, 22, pc);
    tu_oam16(g, 30, pd);
}

TEST(obj_regular_4bpp_sprite) {
    gba_t *g = obj_scene(0x1040); /* OBJ on, 1D mapping */
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 256 + 16 + 1, C_RED);
    sprite(g, 0, 20, 10, 1 << 12); /* y 20, x 10, tile 0, palette bank 1 */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 9, 20), C_BLACK);
    CHECK_EQ(tu_px(g, 10, 20), C_RED);
    CHECK_EQ(tu_px(g, 17, 20), C_RED);
    CHECK_EQ(tu_px(g, 18, 20), C_BLACK);
    ppu_render_line(g, 28);
    CHECK_EQ(tu_px(g, 10, 28), C_BLACK);
    gba_destroy(g);
}

TEST(obj_position_wraps) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 250, 508, 0); /* y 250 reaches line 1; x 508 is -4 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 0, 1), C_RED);
    CHECK_EQ(tu_px(g, 3, 1), C_RED);
    CHECK_EQ(tu_px(g, 4, 1), C_BLACK);
    gba_destroy(g);
}

TEST(obj_flips) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES, 0x0001); /* tile 0: only pixel (0,0) */
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0 | 0x1000, 0);  /* horizontal flip */
    sprite(g, 1, 16, 0 | 0x2000, 0); /* vertical flip */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    ppu_render_line(g, 23);
    CHECK_EQ(tu_px(g, 0, 23), C_RED);
    gba_destroy(g);
}

TEST(obj_8bpp_uses_the_whole_obj_palette) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES + 64, 0x0201); /* 8bpp tile at 32-byte unit 2, row 0: 1..8 */
    tu_vram16(g, OBJ_TILES + 66, 0x0403);
    tu_vram16(g, OBJ_TILES + 68, 0x0605);
    tu_vram16(g, OBJ_TILES + 70, 0x0807);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, 256 + i, (uint16_t)(0x0300 + i));
    sprite(g, 0, 0x2000, 0, 2);
    ppu_render_line(g, 0);
    for (int x = 0; x < 8; x++) CHECK_EQ(tu_px(g, x, 0), 0x0300 + x + 1);
    gba_destroy(g);
}

TEST(obj_1d_and_2d_tile_mapping) {
    for (int one_d = 0; one_d < 2; one_d++) {
        gba_t *g = obj_scene(one_d ? 0x1040 : 0x1000);
        obj_solid_tile4(g, 2, 1);  /* 1D: the sprite's second tile row starts at tile 2 */
        obj_solid_tile4(g, 32, 2); /* 2D: ...and at tile 32 */
        tu_pal(g, 257, C_RED);
        tu_pal(g, 258, C_GREEN);
        sprite(g, 0, 0, 1 << 14, 0); /* 16x16 */
        ppu_render_line(g, 8);
        CHECK_EQ(tu_px(g, 0, 8), one_d ? C_RED : C_GREEN);
        gba_destroy(g);
    }
}

TEST(obj_priority_against_backgrounds) {
    gba_t *g = obj_scene(0x1140); /* OBJ + BG0 */
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, 32 + i, 0x1111); /* BG tile 1 solid */
    tu_pal(g, 1, C_BLUE);
    tu_io16(g, 0x008, 31 << 8); /* BG0 priority 0 */
    tu_vram16(g, 0xF800, 1);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0, 1 << 10); /* priority 1: behind BG0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLUE);
    sprite(g, 0, 0, 0, 0); /* priority 0: tie goes to the sprite */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(obj_order_between_sprites) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    tu_pal(g, 273, C_GREEN);
    sprite(g, 0, 0, 100, (1 << 10) | (0 << 12)); /* red, priority 1 */
    sprite(g, 1, 0, 100, (1 << 10) | (1 << 12)); /* green, priority 1 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 100, 0), C_RED); /* lower OAM index wins ties */
    sprite(g, 1, 0, 100, (0 << 10) | (1 << 12)); /* green, priority 0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 100, 0), C_GREEN);
    gba_destroy(g);
}

TEST(obj_bitmap_modes_hide_tiles_below_512) {
    gba_t *g = obj_scene(0x1043); /* mode 3, OBJ on */
    obj_solid_tile4(g, 0, 1);
    obj_solid_tile4(g, 512, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0, 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    sprite(g, 0, 0, 0, 512);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(obj_affine_identity_and_double_size) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    affine_params0(g, 0x100, 0, 0, 0x100);
    sprite(g, 0, 20 | 0x100, 10, 0); /* affine, parameter set 0 */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 10, 20), C_RED);
    CHECK_EQ(tu_px(g, 17, 20), C_RED);
    sprite(g, 0, 20 | 0x300, 10, 0); /* double-size: 16x16 box, sprite centred */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 14, 20), C_BLACK);
    ppu_render_line(g, 24);
    CHECK_EQ(tu_px(g, 13, 24), C_BLACK);
    CHECK_EQ(tu_px(g, 14, 24), C_RED);
    CHECK_EQ(tu_px(g, 21, 24), C_RED);
    CHECK_EQ(tu_px(g, 22, 24), C_BLACK);
    gba_destroy(g);
}

TEST(obj_affine_scaling) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    affine_params0(g, 0x80, 0, 0, 0x80); /* 2x */
    sprite(g, 0, 0 | 0x300, 10, 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 9, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 10, 0), C_RED);
    CHECK_EQ(tu_px(g, 25, 0), C_RED);
    CHECK_EQ(tu_px(g, 26, 0), C_BLACK);
    gba_destroy(g);
}

TEST(obj_line_cycle_budget) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    /* 64x64 sprites cost 64 cycles on each line they cover; the budget is 1210. */
    for (uint32_t i = 0; i < 17; i++) sprite(g, i, 0, 300 | (3 << 14), 0); /* off-screen at x -212 */
    sprite(g, 17, 0, 0 | (3 << 14), 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED); /* 18 x 64 = 1152 fits */
    sprite(g, 17, 0, 300 | (3 << 14), 0);
    sprite(g, 18, 0, 0 | (3 << 14), 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK); /* 19 x 64 = 1216 does not */
    gba_destroy(g);
}

TEST(obj_window_and_semi_transparent_flags) {
    gba_t *g = obj_scene(0x9040); /* OBJ + OBJ window */
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 20 | (2 << 10), 10, 0);  /* OBJ-window sprite */
    sprite(g, 1, 20 | (1 << 10), 100, 0); /* semi-transparent sprite */
    static layer_px_t obj[PPU_WIDTH];
    static bool win[PPU_WIDTH];
    memset(obj, 0, sizeof obj);
    memset(win, 0, sizeof win);
    ppu_render_obj(g, 20, obj, win);
    CHECK(win[10]);
    CHECK(!win[9]);
    CHECK_EQ(obj[10].flags, 0);
    CHECK_EQ(obj[100].flags, PX_OPAQUE | PX_SEMI);
    gba_destroy(g);
}

TEST(obj_mosaic) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES + 0, 0x4321); /* row 0: indices 1..8 */
    tu_vram16(g, OBJ_TILES + 2, 0x8765);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, 256 + i, (uint16_t)(0x0100 + i));
    sprite(g, 0, 0x1000, 8, 0); /* mosaic on, x 8 */
    tu_io16(g, 0x04C, 3 << 8);  /* OBJ blocks 4 pixels wide */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 8, 0), 0x0101);
    CHECK_EQ(tu_px(g, 11, 0), 0x0101);
    CHECK_EQ(tu_px(g, 12, 0), 0x0105);
    CHECK_EQ(tu_px(g, 15, 0), 0x0105);
    gba_destroy(g);
}
