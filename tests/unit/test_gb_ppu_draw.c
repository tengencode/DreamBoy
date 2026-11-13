/* The picture: background, window and sprites, to the pixel. Scenes are set up by hand in video RAM and OAM, then
 * two frames run (the picture shown is the last finished one). */
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

enum { WHITE = 0x7FFF, LIGHT = 0x56B5, DARK = 0x294A, BLACK = 0x0000 };

/* Tile 1: colour 1 with a colour-3 dot at its top-left and a colour-2 dot at its bottom-right. */
static const char *const MARK[8] = {"31111111", "11111111", "11111111", "11111111",
                                    "11111111", "11111111", "11111111", "11111112"};

/* A machine with the plain palette (colour n = shade n), tile 0 blank, tile 1 = MARK, tile 2 full colour 2,
 * tile 3 full colour 3; map 0 (0x9800) all tile 0. */
static gb_t *scene(void) {
    gb_t *g = gbtu_idle();
    g->ppu.bgp = g->ppu.obp0 = 0xE4;
    g->ppu.obp1 = 0x1B; /* the reverse */
    gbtu_tile(g, 1, MARK);
    gbtu_tile_fill(g, 2, 2);
    gbtu_tile_fill(g, 3, 3);
    return g;
}

static void map_set(gb_t *g, int map, int col, int row, uint8_t tile) {
    g->ppu.vram[(map ? 0x1C00 : 0x1800) + row * 32 + col] = tile;
}

static void sprite(gb_t *g, int i, int x, int y, uint8_t tile, uint8_t attr) {
    uint8_t *o = g->ppu.oam + i * 4;
    o[0] = (uint8_t)(y + 16), o[1] = (uint8_t)(x + 8), o[2] = tile, o[3] = attr;
}

TEST(gb_draw_background_and_palette) {
    gb_t *g = scene();
    map_set(g, 0, 0, 0, 1);
    map_set(g, 0, 19, 17, 1);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), BLACK);
    CHECK_EQ(gbtu_px(g, 1, 0), LIGHT);
    CHECK_EQ(gbtu_px(g, 7, 7), DARK);
    CHECK_EQ(gbtu_px(g, 8, 0), WHITE);
    CHECK_EQ(gbtu_px(g, 152, 136), BLACK);
    CHECK_EQ(gbtu_px(g, 159, 143), DARK);
    g->ppu.bgp = 0x1B; /* 3 2 1 0: every shade swapped */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 0, 0), WHITE);
    CHECK_EQ(gbtu_px(g, 1, 0), DARK);
    CHECK_EQ(gbtu_px(g, 8, 0), BLACK);
    gb_destroy(g);
}

TEST(gb_draw_scroll_and_wrap) {
    gb_t *g = scene();
    map_set(g, 0, 31, 31, 1); /* the map's last tile */
    g->ppu.scx = 250;
    g->ppu.scy = 251;
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), LIGHT); /* map pixel (250, 251): inside the tile */
    CHECK_EQ(gbtu_px(g, 5, 4), DARK);  /* (255, 255): its bottom-right dot */
    CHECK_EQ(gbtu_px(g, 6, 5), WHITE); /* (0, 0): wrapped to the map's first tile */
    g->ppu.scx = 248 - 3;              /* fine scroll: the tile starts at x = 3 */
    g->ppu.scy = 248;
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 2, 0), WHITE);
    CHECK_EQ(gbtu_px(g, 3, 0), BLACK);
    gb_destroy(g);
}

TEST(gb_draw_tile_data_and_map_select) {
    gb_t *g = scene();
    /* With LCDC bit 4 clear, tiles 0-127 come from 0x9000 and 128-255 from 0x8800. */
    memset(g->ppu.vram + 0x1000 + 5 * 16, 0xFF, 16); /* 0x9000 tile 5: colour 3 */
    for (int i = 0; i < 16; i += 2) g->ppu.vram[0x0800 + 5 * 16 + i] = 0xFF; /* 0x8800 tile 128+5: colour 1 */
    map_set(g, 0, 0, 0, 5);
    map_set(g, 0, 1, 0, 133);
    map_set(g, 1, 0, 0, 2);
    g->ppu.lcdc = 0x81;
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), BLACK);
    CHECK_EQ(gbtu_px(g, 8, 0), LIGHT);
    g->ppu.lcdc = 0x99; /* map 1, 0x8000 tile data */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 0, 0), DARK);
    CHECK_EQ(gbtu_px(g, 8, 0), WHITE);
    gb_destroy(g);
}

TEST(gb_draw_window) {
    gb_t *g = scene();
    for (int i = 0; i < 32 * 32; i++) g->ppu.vram[0x1C00 + i] = 2; /* the window's map: all dark */
    map_set(g, 1, 0, 0, 1);
    g->ppu.lcdc = 0xF1; /* window on, on map 1 */
    g->ppu.wx = 7 + 40;
    g->ppu.wy = 50;
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 39, 50), WHITE); /* left of it and above it: background */
    CHECK_EQ(gbtu_px(g, 40, 49), WHITE);
    CHECK_EQ(gbtu_px(g, 40, 50), BLACK); /* the window's own top-left pixel */
    CHECK_EQ(gbtu_px(g, 41, 50), LIGHT);
    CHECK_EQ(gbtu_px(g, 159, 143), DARK);
    g->ppu.wx = 3; /* left of the screen's edge: the window's first 4 columns are cut off */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 0, 50), LIGHT);
    CHECK_EQ(gbtu_px(g, 3, 57), DARK);
    CHECK_EQ(gbtu_px(g, 4, 50), DARK);
    g->ppu.wx = 167; /* off the right edge: not shown */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 159, 100), WHITE);
    g->ppu.wx = 7;
    g->ppu.wy = 144; /* never reached */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 0, 143), WHITE);
    gb_destroy(g);
}

/* The window counts its own lines: hidden for a while mid-frame, it carries on where it stopped. */
TEST(gb_draw_window_line_counter) {
    static const uint8_t code[] = {
        0xF0, 0x44,       /* 150 wait: LDH A,(LY) */
        0xFE, 0x0A,       /*     CP 10 */
        0x20, 0xFA,       /*     JR NZ,wait */
        0x3E, 0xD1,       /*     LD A,0xD1: window off */
        0xE0, 0x40,       /*     LDH (LCDC),A */
        0xF0, 0x44,       /* 15A wait2: LDH A,(LY) */
        0xFE, 0x14,       /*     CP 20 */
        0x20, 0xFA,       /*     JR NZ,wait2 */
        0x3E, 0xF1,       /*     LD A,0xF1: window on again */
        0xE0, 0x40,       /*     LDH (LCDC),A */
        0xF0, 0x44,       /* 164 wait3: LDH A,(LY) */
        0xFE, 0x90,       /*     CP 144 */
        0x20, 0xFA,       /*     JR NZ,wait3 */
        0x18, 0xE4,       /*     JR wait */
    };
    gb_t *g = gbtu_boot(code, sizeof code);
    g->ppu.bgp = 0xE4;
    gbtu_tile_fill(g, 2, 2);
    gbtu_tile_fill(g, 3, 3);
    for (int i = 0; i < 32; i++) g->ppu.vram[0x1C00 + i] = 2;      /* window rows 0-7: dark */
    for (int i = 0; i < 32; i++) g->ppu.vram[0x1C00 + 32 + i] = 3; /* window rows 8-15: black */
    g->ppu.lcdc = 0xF1;
    g->ppu.wx = 7;
    g->ppu.wy = 4;
    gbtu_frames(g, 3);
    CHECK_EQ(gbtu_px(g, 80, 3), WHITE);
    CHECK_EQ(gbtu_px(g, 80, 4), DARK);   /* window lines 0-5 on screen lines 4-9 */
    CHECK_EQ(gbtu_px(g, 80, 9), DARK);
    CHECK_EQ(gbtu_px(g, 80, 12), WHITE); /* hidden on lines 10-19 */
    CHECK_EQ(gbtu_px(g, 80, 20), DARK);  /* back: window lines 6, 7 */
    CHECK_EQ(gbtu_px(g, 80, 21), DARK);
    CHECK_EQ(gbtu_px(g, 80, 22), BLACK); /* window line 8 */
    gb_destroy(g);
}

TEST(gb_draw_sprites) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x93;
    sprite(g, 0, 20, 30, 1, 0x00);
    sprite(g, 1, 40, 30, 1, 0x20);  /* flipped left-right */
    sprite(g, 2, 60, 30, 1, 0x40);  /* flipped top-bottom */
    sprite(g, 3, 80, 30, 1, 0x10);  /* the second palette */
    sprite(g, 4, -4, 30, 3, 0x00);  /* half off the left edge */
    sprite(g, 5, 156, 140, 3, 0x00); /* off the right and bottom edges */
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 20, 30), BLACK);
    CHECK_EQ(gbtu_px(g, 27, 37), DARK);
    CHECK_EQ(gbtu_px(g, 28, 30), WHITE);
    CHECK_EQ(gbtu_px(g, 47, 30), BLACK); /* x flip */
    CHECK_EQ(gbtu_px(g, 40, 37), DARK);
    CHECK_EQ(gbtu_px(g, 60, 37), BLACK); /* y flip */
    CHECK_EQ(gbtu_px(g, 67, 30), DARK);
    CHECK_EQ(gbtu_px(g, 80, 30), WHITE); /* OBP1 0x1B: colour 3 is white, colour 1 dark */
    CHECK_EQ(gbtu_px(g, 81, 30), DARK);
    CHECK_EQ(gbtu_px(g, 0, 30), BLACK);
    CHECK_EQ(gbtu_px(g, 3, 37), BLACK);
    CHECK_EQ(gbtu_px(g, 4, 30), WHITE);
    CHECK_EQ(gbtu_px(g, 159, 143), BLACK);
    g->ppu.lcdc = 0x91; /* sprites off */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 20, 30), WHITE);
    gb_destroy(g);
}

TEST(gb_draw_sprite_colour_0_is_clear) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x93;
    static const char *const ring[8] = {"33333333", "30000003", "30000003", "30000003",
                                        "30000003", "30000003", "30000003", "33333333"};
    gbtu_tile(g, 4, ring);
    map_set(g, 0, 2, 2, 2); /* dark background under the sprite */
    sprite(g, 0, 16, 16, 4, 0);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 16, 16), BLACK);
    CHECK_EQ(gbtu_px(g, 18, 18), DARK);
    gb_destroy(g);
}

TEST(gb_draw_tall_sprites) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x97; /* 8x16: the tile number's low bit is ignored */
    sprite(g, 0, 20, 30, 3, 0x00);  /* tiles 2 (dark) over 3 (black) */
    sprite(g, 1, 40, 30, 2, 0x40);  /* upside down: 3 over 2 */
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 20, 30), DARK);
    CHECK_EQ(gbtu_px(g, 20, 37), DARK);
    CHECK_EQ(gbtu_px(g, 20, 38), BLACK);
    CHECK_EQ(gbtu_px(g, 20, 45), BLACK);
    CHECK_EQ(gbtu_px(g, 20, 46), WHITE);
    CHECK_EQ(gbtu_px(g, 40, 30), BLACK);
    CHECK_EQ(gbtu_px(g, 40, 38), DARK);
    gb_destroy(g);
}

TEST(gb_draw_sprite_priority) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x93;
    /* Overlapping: the one further left wins, whatever its place in OAM. */
    sprite(g, 0, 24, 30, 3, 0);
    sprite(g, 1, 20, 30, 2, 0);
    /* The same x: the earlier one in OAM wins. */
    sprite(g, 2, 60, 30, 2, 0);
    sprite(g, 3, 60, 30, 3, 0);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 20, 30), DARK);
    CHECK_EQ(gbtu_px(g, 25, 30), DARK);
    CHECK_EQ(gbtu_px(g, 28, 30), BLACK);
    CHECK_EQ(gbtu_px(g, 60, 30), DARK);
    gb_destroy(g);
}

TEST(gb_draw_sprite_behind_background) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x93;
    map_set(g, 0, 2, 2, 1); /* colour 1, with dots */
    sprite(g, 0, 12, 16, 3, 0x80); /* behind: shows only where the background is colour 0 */
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 12, 16), BLACK); /* over the blank tile */
    CHECK_EQ(gbtu_px(g, 17, 17), LIGHT); /* hidden by the background */
    /* A winning sprite that is "behind" still hides the sprite it beat. */
    sprite(g, 1, 13, 16, 2, 0x00);
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 17, 17), LIGHT);
    CHECK_EQ(gbtu_px(g, 20, 17), DARK); /* past sprite 0's right edge, sprite 1 shows */
    gb_destroy(g);
}

TEST(gb_draw_ten_sprites_a_line) {
    gb_t *g = scene();
    g->ppu.lcdc = 0x93;
    for (int i = 0; i < 12; i++) sprite(g, i, i * 10, 50, 3, 0);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 90, 50), BLACK);  /* the 10th */
    CHECK_EQ(gbtu_px(g, 100, 50), WHITE); /* the 11th and 12th are not drawn */
    CHECK_EQ(gbtu_px(g, 110, 50), WHITE);
    sprite(g, 0, 0, 100, 3, 0); /* one of the ten moves away: the 11th gets its turn */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 100, 50), BLACK);
    gb_destroy(g);
}

TEST(gb_draw_background_off) {
    gb_t *g = scene();
    g->ppu.bgp = 0xFF; /* even with an all-black palette, "off" is white */
    g->ppu.lcdc = 0x92;
    map_set(g, 0, 0, 0, 3);
    sprite(g, 0, 40, 40, 3, 0x80);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), WHITE);
    CHECK_EQ(gbtu_px(g, 40, 40), BLACK); /* a "behind" sprite shows everywhere */
    gb_destroy(g);
}
