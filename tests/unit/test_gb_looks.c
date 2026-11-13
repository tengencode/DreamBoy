/* Looks for grey games (17c): the background and the two sprite palettes each get their own four colours; the
 * automatic look is the one a Game Boy Color picks from the cartridge header. Headers here are made up. */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

enum { WHITE = 0x7FFF, LIGHT = 0x56B5, DARK = 0x294A, BLACK = 0x0000 };

/* One background tile of shade 1 at the top left, one sprite of shade 1 from each sprite palette. */
static gb_t *scene(void) {
    gb_t *g = gbtu_idle();
    g->ppu.bgp = g->ppu.obp0 = g->ppu.obp1 = 0xE4;
    gbtu_tile_fill(g, 1, 1);
    g->ppu.vram[0x1800] = 1;
    uint8_t *o = g->ppu.oam;
    o[0] = 40 + 16, o[1] = 40 + 8, o[2] = 1, o[3] = 0x00;
    o[4] = 40 + 16, o[5] = 60 + 8, o[6] = 1, o[7] = 0x10;
    g->ppu.lcdc |= 0x02;
    return g;
}

/* A 32 KiB ROM whose 16 title bytes are `title` (zero-padded) with one more byte at 0x143-1 chosen so that they add
 * up to `sum`; licensee bytes as given. */
static uint8_t *header(const char *title, uint8_t sum, uint8_t old_licensee, const char *new_licensee) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0x00);
    memset(rom + 0x134, 0, 16);
    memcpy(rom + 0x134, title, strlen(title));
    uint8_t now = 0;
    for (int i = 0; i < 16; i++) now = (uint8_t)(now + rom[0x134 + i]);
    rom[0x142] = (uint8_t)(sum - now); /* (0x143, the colour flag, stays 0: a grey game) */
    rom[0x14B] = old_licensee;
    memcpy(rom + 0x144, new_licensee, 2);
    gbtu_fix_checksum(rom);
    return rom;
}

static uint16_t bg0(int look) {
    uint16_t c[12];
    gb_look_colours(look, c);
    return c[0];
}

TEST(gb_looks_default_is_grey) {
    gb_t *g = scene();
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), LIGHT);
    CHECK_EQ(gbtu_px(g, 40, 40), LIGHT);
    CHECK_EQ(gbtu_px(g, 100, 100), WHITE);
    gb_set_look(g, GB_LOOK_GREY);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), LIGHT);
    uint16_t c[12];
    gb_look_colours(GB_LOOK_GREY, c);
    CHECK_EQ(c[0], WHITE);
    CHECK_EQ(c[5], LIGHT);
    CHECK_EQ(c[10], DARK);
    CHECK_EQ(c[11], BLACK);
    gb_destroy(g);
}

TEST(gb_looks_colour_background_and_sprites_apart) {
    /* Find a fixed look whose background and two sprite palettes all differ in shade 1. */
    int look = -1;
    uint16_t c[12];
    for (int i = 0; i < gb_look_count() && look < 0; i++) {
        gb_look_colours(i, c);
        if (c[1] != c[5] && c[5] != c[9] && c[1] != c[9]) look = i;
    }
    CHECK(look > 0);
    gb_look_colours(look, c);
    gb_t *g = scene();
    gb_set_look(g, look);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), c[1]);
    CHECK_EQ(gbtu_px(g, 40, 40), c[5]);
    CHECK_EQ(gbtu_px(g, 60, 40), c[9]);
    CHECK_EQ(gbtu_px(g, 100, 100), c[0]);
    g->ppu.lcdc &= (uint8_t)~0x01; /* background off: the background's lightest colour */
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), c[0]);
    gb_destroy(g);
}

TEST(gb_looks_every_fixed_look_is_readable) {
    CHECK(gb_look_count() >= 14);
    for (int i = 0; i < gb_look_count(); i++) {
        uint16_t c[12];
        gb_look_colours(i, c);
        for (int p = 0; p < 3; p++) CHECK(c[p * 4] != c[p * 4 + 3]); /* lightest and darkest are never the same */
    }
}

TEST(gb_looks_auto_known_sum) {
    uint8_t *rom = header("MADEUP", 0x88, 0x01, "\0\0");
    int look = gb_look_auto(rom, 0x8000);
    CHECK(look >= gb_look_count() || look > 0);
    CHECK_EQ(bg0(look), 0x7E74);
    free(rom);
    rom = header("MADEUP", 0x88, 0x33, "01"); /* the new licensee code says Nintendo too */
    CHECK_EQ(bg0(gb_look_auto(rom, 0x8000)), 0x7E74);
    free(rom);
}

TEST(gb_looks_auto_needs_the_fourth_letter) {
    uint16_t c[12];
    uint8_t *rom = header("XYZU", 0xB3, 0x01, "\0\0");
    gb_look_colours(gb_look_auto(rom, 0x8000), c);
    CHECK_EQ(c[1], 0x42B5); /* the background */
    CHECK_EQ(c[5], 0x01DF); /* sprites */
    free(rom);
    rom = header("XYZR", 0xB3, 0x01, "\0\0");
    gb_look_colours(gb_look_auto(rom, 0x8000), c);
    CHECK_EQ(c[1], 0x03EA);
    free(rom);
    rom = header("XYZQ", 0xB3, 0x01, "\0\0"); /* the sum is known, the letter is not */
    CHECK_EQ(gb_look_auto(rom, 0x8000), GB_LOOK_GREY);
    free(rom);
}

TEST(gb_looks_auto_unknown_or_other_licensee_is_grey) {
    uint8_t *rom = header("MADEUP", 0x02, 0x01, "\0\0"); /* a sum the table does not have */
    CHECK_EQ(gb_look_auto(rom, 0x8000), GB_LOOK_GREY);
    free(rom);
    rom = header("MADEUP", 0x00, 0x01, "\0\0"); /* the table's own "default" entry */
    CHECK_EQ(gb_look_auto(rom, 0x8000), GB_LOOK_GREY);
    free(rom);
    rom = header("MADEUP", 0x88, 0x02, "\0\0");
    CHECK_EQ(gb_look_auto(rom, 0x8000), GB_LOOK_GREY);
    free(rom);
    rom = header("MADEUP", 0x88, 0x33, "02");
    CHECK_EQ(gb_look_auto(rom, 0x8000), GB_LOOK_GREY);
    CHECK_EQ(gb_look_auto(rom, 0x100), GB_LOOK_GREY); /* too short to have a header */
    free(rom);
}

TEST(gb_looks_out_of_range_is_grey) {
    CHECK_EQ(bg0(-7), WHITE);
    CHECK_EQ(bg0(100000), WHITE);
    uint16_t c[12];
    gb_look_colours(100000, c);
    CHECK_EQ(c[5], LIGHT);
    gb_t *g = scene();
    gb_set_look(g, 100000);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), LIGHT);
    gb_destroy(g);
}

TEST(gb_looks_auto_is_set_from_the_loaded_game_and_kept) {
    uint8_t *rom = header("MADEUP", 0x88, 0x01, "\0\0");
    gb_t *g = gb_create();
    gb_set_look(g, GB_LOOK_AUTO); /* before any game: grey for now, resolved when one is loaded */
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), 0x7E74);
    gb_reset(g);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), 0x7E74);
    gb_destroy(g);
    free(rom);
}

TEST(gb_looks_are_not_in_states) {
    gb_t *a = scene(), *b = scene();
    gb_set_look(b, 1);
    size_t n = gb_state_size(a);
    uint8_t *sa = malloc(n), *sb = malloc(n);
    CHECK_EQ(gb_state_save(a, sa, n), n); /* before any frame: the stored picture is the same too */
    CHECK_EQ(gb_state_save(b, sb, n), n);
    CHECK(memcmp(sa, sb, n) == 0);
    CHECK_EQ(gb_state_load(b, sa, n), GB_OK); /* a state from the grey machine: the look stays */
    gbtu_frames(b, 2);
    uint16_t c[12];
    gb_look_colours(1, c);
    CHECK_EQ(gbtu_px(b, 0, 0), c[1]);
    free(sa);
    free(sb);
    gb_destroy(a);
    gb_destroy(b);
}
