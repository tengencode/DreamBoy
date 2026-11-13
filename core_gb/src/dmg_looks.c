/* Looks for grey games (17c): which colours a plain Game Boy's four shades are shown in. A look is three sets of
 * four colours — the background's, and one for each of the two sprite palettes — as on a Game Boy Color, which
 * colours a grey game this way: it adds up the cartridge header's title, looks the sum up in a table of the games
 * it knows and takes that game's combination.
 *
 * The tables below (the sums, the fourth letters that tell games with the same sum apart, the combination of each
 * sum, the combinations and the colours) are the Game Boy Color's own; their values were taken from SameBoy's boot
 * ROM source (BootROMs/cgb_boot.asm, Copyright (c) 2015-2026 Lior Halphon, Expat/MIT licence — third_party/README.md).
 * The lookup code is ours. */
#include <string.h>

#include "gb_internal.h"

static const uint8_t SUMS[94] = {
    0x00, 0x88, 0x16, 0x36, 0xD1, 0xDB, 0xF2, 0x3C, 0x8C, 0x92, 0x3D, 0x5C, 0x58, 0xC9, 0x3E, 0x70,
    0x1D, 0x59, 0x69, 0x19, 0x35, 0xA8, 0x14, 0xAA, 0x75, 0x95, 0x99, 0x34, 0x6F, 0x15, 0xFF, 0x97,
    0x4B, 0x90, 0x17, 0x10, 0x39, 0xF7, 0xF6, 0xA2, 0x49, 0x4E, 0x43, 0x68, 0xE0, 0x8B, 0xF0, 0xCE,
    0x0C, 0x29, 0xE8, 0xB7, 0x86, 0x9A, 0x52, 0x01, 0x9D, 0x71, 0x9C, 0xBD, 0x5D, 0x6D, 0x67, 0x3F,
    0x6B, 0xB3, 0x46, 0x28, 0xA5, 0xC6, 0xD3, 0x27, 0x61, 0x18, 0x66, 0x6A, 0xBF, 0x0D, 0xF4, 0xB3,
    0x46, 0x28, 0xA5, 0xC6, 0xD3, 0x27, 0x61, 0x18, 0x66, 0x6A, 0xBF, 0x0D, 0xF4, 0xB3,
};

#define FIRST_WITH_LETTER 65
static const char LETTERS[] = "BEFAARBEKEK R-URAR INAILICE R";

static const uint8_t COMBO_OF_SUM[94] = {
    0, 4, 5, 35, 34, 3, 31, 15, 10, 5, 19, 36, 7, 37, 30, 44, 21, 32, 31, 20,
    5, 33, 13, 14, 5, 29, 5, 18, 9, 3, 2, 26, 25, 25, 41, 42, 26, 45, 42, 45,
    36, 38, 26, 42, 30, 41, 34, 34, 5, 42, 6, 5, 33, 25, 42, 42, 40, 2, 16, 25,
    42, 42, 5, 0, 39, 36, 22, 25, 6, 32, 12, 36, 11, 39, 18, 39, 24, 31, 50, 17,
    46, 6, 27, 0, 47, 41, 41, 0, 0, 19, 34, 23, 18, 29,
};

static const uint8_t COMBOS[51][3] = { /* the first colour of sprites 0, sprites 1, the background */
    {16, 16, 116},
    {72, 72, 72},
    {80, 80, 80},
    {96, 96, 96},
    {36, 36, 36},
    {0, 0, 0},
    {108, 108, 108},
    {20, 20, 20},
    {48, 48, 48},
    {104, 104, 104},
    {64, 32, 32},
    {16, 112, 112},
    {16, 8, 8},
    {12, 16, 16},
    {16, 116, 116},
    {112, 16, 112},
    {8, 68, 8},
    {64, 64, 32},
    {16, 16, 28},
    {16, 16, 72},
    {16, 16, 80},
    {76, 76, 36},
    {15, 15, 44},
    {68, 68, 8},
    {16, 16, 8},
    {16, 16, 12},
    {112, 112, 0},
    {12, 12, 0},
    {0, 0, 4},
    {72, 88, 72},
    {80, 88, 80},
    {96, 88, 96},
    {64, 88, 32},
    {68, 16, 52},
    {111, 0, 56},
    {111, 16, 60},
    {76, 91, 36},
    {64, 112, 40},
    {16, 92, 112},
    {68, 88, 8},
    {16, 0, 8},
    {16, 112, 12},
    {112, 12, 0},
    {12, 112, 16},
    {84, 112, 16},
    {12, 112, 0},
    {100, 12, 112},
    {0, 112, 32},
    {16, 12, 112},
    {112, 12, 24},
    {16, 112, 116},
};

static const uint16_t COLOURS[120] = {
    0x7FFF, 0x32BF, 0x00D0, 0x0000, 0x639F, 0x4279, 0x15B0, 0x04CB,
    0x7FFF, 0x6E31, 0x454A, 0x0000, 0x7FFF, 0x1BEF, 0x0200, 0x0000,
    0x7FFF, 0x421F, 0x1CF2, 0x0000, 0x7FFF, 0x5294, 0x294A, 0x0000,
    0x7FFF, 0x03FF, 0x012F, 0x0000, 0x7FFF, 0x03EF, 0x01D6, 0x0000,
    0x7FFF, 0x42B5, 0x3DC8, 0x0000, 0x7E74, 0x03FF, 0x0180, 0x0000,
    0x67FF, 0x77AC, 0x1A13, 0x2D6B, 0x7ED6, 0x4BFF, 0x2175, 0x0000,
    0x53FF, 0x4A5F, 0x7E52, 0x0000, 0x4FFF, 0x7ED2, 0x3A4C, 0x1CE0,
    0x03ED, 0x7FFF, 0x255F, 0x0000, 0x036A, 0x021F, 0x03FF, 0x7FFF,
    0x7FFF, 0x01DF, 0x0112, 0x0000, 0x231F, 0x035F, 0x00F2, 0x0009,
    0x7FFF, 0x03EA, 0x011F, 0x0000, 0x299F, 0x001A, 0x000C, 0x0000,
    0x7FFF, 0x027F, 0x001F, 0x0000, 0x7FFF, 0x03E0, 0x0206, 0x0120,
    0x7FFF, 0x7EEB, 0x001F, 0x7C00, 0x7FFF, 0x3FFF, 0x7E00, 0x001F,
    0x7FFF, 0x03FF, 0x001F, 0x0000, 0x03FF, 0x001F, 0x000C, 0x0000,
    0x7FFF, 0x033F, 0x0193, 0x0000, 0x0000, 0x4200, 0x037F, 0x7FFF,
    0x7FFF, 0x7E8C, 0x7C00, 0x0000, 0x7FFF, 0x1BEF, 0x6180, 0x0000,
};

#define RGB(r, g, b) (uint16_t)(((r) >> 3) | ((g) >> 3) << 5 | ((b) >> 3) << 10)
/* The looks that are ours: the four greys (ppu_draw's default), the first Game Boy's green screen, the Pocket's. */
static const uint16_t OWN[3][4] = {
    {0x7FFF, 0x56B5, 0x294A, 0x0000},
    {RGB(200, 220, 90), RGB(140, 170, 50), RGB(60, 100, 50), RGB(15, 50, 25)},
    {RGB(196, 207, 161), RGB(139, 149, 109), RGB(77, 83, 60), RGB(31, 31, 31)},
};
/* The Game Boy Color's own looks, as its owners chose them with the buttons while it started: brown (Up), red
 * (Up + A), dark brown (Up + B), pastel mix (Down), orange (Down + A), yellow (Down + B), blue (Left), dark blue
 * (Left + A), green (Right), dark green (Right + A), inverted (Right + B). Its grey (Left + B) is left out. */
static const uint8_t PICKED[] = {5, 43, 28, 8, 3, 49, 48, 40, 1, 0, 6};
#define N_OWN 3
#define N_PICKED ((int)sizeof PICKED)
#define N_COMBOS ((int)(sizeof COMBOS / sizeof COMBOS[0]))

int gb_look_count(void) {
    return N_OWN + N_PICKED;
}

void gb_look_colours(int look, uint16_t out[12]) {
    int combo = -1;
    if (look >= N_OWN && look < N_OWN + N_PICKED) combo = PICKED[look - N_OWN];
    else if (look >= N_OWN + N_PICKED && look < N_OWN + N_PICKED + N_COMBOS) combo = look - N_OWN - N_PICKED;
    for (int p = 0; p < 3; p++) { /* out: the background, sprites 0, sprites 1; a combination: sprites 0, 1, background */
        const uint16_t *from = combo < 0 ? OWN[look > 0 && look < N_OWN ? look : 0] : COLOURS + COMBOS[combo][(p + 2) % 3];
        for (int i = 0; i < 4; i++) out[p * 4 + i] = from[i];
    }
}

int gb_look_auto(const uint8_t *rom, size_t size) {
    if (size < 0x150) return GB_LOOK_GREY;
    bool nintendo = rom[0x14B] == 0x33 ? rom[0x144] == '0' && rom[0x145] == '1' : rom[0x14B] == 0x01;
    if (!nintendo) return GB_LOOK_GREY;
    uint8_t sum = 0;
    for (int i = 0x134; i <= 0x143; i++) sum = (uint8_t)(sum + rom[i]);
    for (int i = 1; i < (int)sizeof SUMS; i++) { /* (entry 0 is the console's look for a game it does not know) */
        if (SUMS[i] != sum) continue;
        if (i >= FIRST_WITH_LETTER && LETTERS[i - FIRST_WITH_LETTER] != (char)rom[0x137]) continue;
        return N_OWN + N_PICKED + COMBO_OF_SUM[i];
    }
    return GB_LOOK_GREY;
}

static void apply(gb_t *g) {
    int look = g->look_asked == GB_LOOK_AUTO ? (g->info.rom ? gb_look_auto(g->info.rom, g->info.rom_size) : GB_LOOK_GREY)
                                             : g->look_asked;
    uint16_t c[12];
    gb_look_colours(look, c);
    memcpy(g->look, c, sizeof g->look);
}

void gb_set_look(gb_t *g, int look) {
    g->look_asked = look;
    apply(g);
}

void gb_look_refresh(gb_t *g) {
    apply(g);
}
