/* What only a Game Boy Color has: speed switch, RAM banks, palette RAM, copies into video RAM, colour drawing. */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

/* A machine running `code` as a Game Boy Color game (header byte 0x143 = 0x80), past "NOP; JP 0x150". */
static gb_t *color(const uint8_t *code, size_t n) {
    uint8_t *rom = gbtu_rom(0x8000, 0x00, 0, 0x80);
    memcpy(rom + 0x150, code, n);
    gb_t *g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    gb_debug_step(g);
    gb_debug_step(g);
    return g;
}

static gb_t *color_idle(void) {
    static const uint8_t code[] = {0x18, 0xFE};
    return color(code, 2);
}

static void set_colour(gb_t *g, int obj, int palette, int index, uint16_t bgr555) {
    uint8_t *ram = obj ? g->cgb.obj_palette : g->cgb.bg_palette;
    ram[(palette * 4 + index) * 2] = (uint8_t)bgr555;
    ram[(palette * 4 + index) * 2 + 1] = (uint8_t)(bgr555 >> 8);
}

TEST(gb_cgb_registers_dead_on_a_plain_game_boy) {
    gb_t *g = gbtu_idle();
    static const uint16_t regs[] = {0xFF4D, 0xFF4F, 0xFF55, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF70, 0xFF72};
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        gb_bus_write(g, regs[i], 0x01);
        CHECK_EQ(gb_bus_peek(g, regs[i]), 0xFF);
    }
    CHECK_EQ(g->mem.wram_bank, 1);
    CHECK_EQ(g->cgb.vbk, 0);
    gb_destroy(g);
}

TEST(gb_cgb_stop_switches_speed) {
    static const uint8_t code[] = {0x3E, 0x01, 0xE0, 0x4D, /* LD A,1; LDH (KEY1),A */
                                   0x10, 0x00,             /* STOP */
                                   0x00, 0x00, 0x18, 0xFE};
    gb_t *g = color(code, sizeof code);
    CHECK_EQ(gb_bus_peek(g, 0xFF4D), 0x7E);
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(gb_bus_peek(g, 0xFF4D), 0x7F); /* armed */
    gb_debug_step(g);
    CHECK_EQ(gb_bus_peek(g, 0xFF4D), 0xFE); /* double speed, no longer armed */
    CHECK_EQ(g->cpu.halted, 0);
    CHECK_EQ(g->timer.div, 0);
    uint64_t t = gb_debug_ticks(g);
    gb_debug_step(g); /* a NOP is one machine cycle: 2 clock ticks now */
    CHECK_EQ(gb_debug_ticks(g) - t, 2);
    CHECK_EQ(g->timer.div, 4); /* the divider and timer run with the CPU */
    t = gb_debug_ticks(g);
    gb_run_frame(g); /* (the single steps above came out of this first frame) */
    t = gb_debug_ticks(g);
    for (int f = 0; f < 3; f++) gb_run_frame(g); /* a frame is the same length */
    CHECK(gb_debug_ticks(g) - t >= 3u * GB_FRAME_TICKS - 12 && gb_debug_ticks(g) - t <= 3u * GB_FRAME_TICKS + 12);
    gb_destroy(g);

    static const uint8_t stop[] = {0x10, 0x00, 0x00}; /* STOP without arming: it just idles */
    g = color(stop, sizeof stop);
    gb_debug_step(g);
    CHECK_EQ(g->cpu.halted, 1);
    CHECK_EQ(gb_bus_peek(g, 0xFF4D), 0x7E);
    gb_destroy(g);
}

TEST(gb_cgb_work_ram_banks) {
    gb_t *g = color_idle();
    for (int bank = 0; bank < 8; bank++) {
        gb_bus_write(g, 0xFF70, (uint8_t)bank);
        CHECK_EQ(gb_bus_peek(g, 0xFF70), 0xF8 | bank);
        gb_bus_write(g, 0xD000, (uint8_t)(0x10 + bank));
    }
    gb_bus_write(g, 0xC000, 0x99);
    for (int bank = 2; bank < 8; bank++) {
        gb_bus_write(g, 0xFF70, (uint8_t)bank);
        CHECK_EQ(gb_bus_peek(g, 0xD000), 0x10 + bank);
        CHECK_EQ(gb_bus_peek(g, 0xC000), 0x99); /* the low half never moves */
        CHECK_EQ(gb_wram(g)[bank * 0x1000], 0x10 + bank);
    }
    gb_bus_write(g, 0xFF70, 0); /* 0 picks bank 1, which bank 1's write then overwrote */
    CHECK_EQ(gb_bus_peek(g, 0xD000), 0x11);
    gb_bus_write(g, 0xFF70, 1);
    CHECK_EQ(gb_bus_peek(g, 0xD000), 0x11);
    CHECK_EQ(gb_bus_peek(g, 0xF000), 0x11); /* the echo follows the bank */
    gb_destroy(g);
}

TEST(gb_cgb_video_ram_banks) {
    gb_t *g = color_idle();
    gb_bus_write(g, 0xFF40, 0x11); /* screen off: video RAM is free */
    gb_bus_write(g, 0x8000, 0xAA);
    gb_bus_write(g, 0xFF4F, 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0xFF4F), 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0x8000), 0x00);
    gb_bus_write(g, 0x8000, 0xBB);
    gb_bus_write(g, 0xFF4F, 0xFE);
    CHECK_EQ(gb_bus_peek(g, 0x8000), 0xAA);
    CHECK_EQ(g->ppu.vram[0x2000], 0xBB);
    gb_destroy(g);
}

TEST(gb_cgb_palette_ram) {
    gb_t *g = color_idle();
    gb_bus_write(g, 0xFF40, 0x11);
    gb_bus_write(g, 0xFF68, 0x80 | 6); /* from byte 6, moving on */
    gb_bus_write(g, 0xFF69, 0x1F);
    gb_bus_write(g, 0xFF69, 0x7C);
    CHECK_EQ(gb_bus_peek(g, 0xFF68), 0xC0 | 8);
    CHECK_EQ(g->cgb.bg_palette[6], 0x1F);
    CHECK_EQ(g->cgb.bg_palette[7], 0x7C);
    gb_bus_write(g, 0xFF68, 6); /* without the bit the index stays */
    CHECK_EQ(gb_bus_peek(g, 0xFF69), 0x1F);
    gb_bus_write(g, 0xFF69, 0x11);
    gb_bus_write(g, 0xFF69, 0x12);
    CHECK_EQ(g->cgb.bg_palette[6], 0x12);
    gb_bus_write(g, 0xFF68, 0x80 | 63); /* wraps */
    gb_bus_write(g, 0xFF69, 0x01);
    CHECK_EQ(gb_bus_peek(g, 0xFF68), 0xC0);
    gb_bus_write(g, 0xFF6A, 0x80);
    gb_bus_write(g, 0xFF6B, 0x55);
    CHECK_EQ(g->cgb.obj_palette[0], 0x55);
    CHECK_EQ(gb_bus_peek(g, 0xFF6A), 0xC1);
    /* While a line is drawn the data cannot be reached, but the index still moves. */
    gb_bus_write(g, 0xFF40, 0x91);
    while (!gb_ppu_vram_blocked(g)) gb_tick(g);
    gb_bus_write(g, 0xFF68, 0x80 | 6);
    g->ppu.vram_busy = 1;
    gb_bus_write(g, 0xFF69, 0x77);
    CHECK_EQ(g->cgb.bg_palette[6], 0x12);
    CHECK_EQ(gb_bus_peek(g, 0xFF68) & 0x3F, 7);
    gb_destroy(g);
}

TEST(gb_cgb_copy_all_at_once) {
    gb_t *g = color_idle();
    gb_bus_write(g, 0xFF40, 0x11);
    for (int i = 0; i < 0x40; i++) g->mem.wram[0x100 + i] = (uint8_t)(i + 1);
    gb_bus_write(g, 0xFF51, 0xC1);
    gb_bus_write(g, 0xFF52, 0x0F); /* the low 4 bits are ignored: 0xC100 */
    gb_bus_write(g, 0xFF53, 0xE8); /* always into video RAM: 0x8800 */
    gb_bus_write(g, 0xFF54, 0x07);
    gb_bus_write(g, 0xFF55, 0x02); /* 3 blocks of 16 */
    CHECK_EQ(g->ppu.vram[0x0800], 1);
    CHECK_EQ(g->ppu.vram[0x082F], 0x30);
    CHECK_EQ(g->ppu.vram[0x0830], 0);
    CHECK_EQ(gb_bus_peek(g, 0xFF55), 0xFF); /* done */
    uint64_t t = gb_debug_ticks(g);
    uint16_t pc = gb_debug_reg(g, GB_REG_PC);
    gb_debug_step(g); /* the CPU stood still for 3 x 8 cycles, then ran one instruction (JR: 3 cycles) */
    CHECK_EQ(gb_debug_ticks(g) - t, (24 + 3) * 4);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), pc);
    gb_bus_write(g, 0xFF4F, 1); /* into the selected bank */
    gb_bus_write(g, 0xFF51, 0xC1);
    gb_bus_write(g, 0xFF52, 0x00);
    gb_bus_write(g, 0xFF53, 0x00);
    gb_bus_write(g, 0xFF54, 0x00);
    gb_bus_write(g, 0xFF55, 0x00);
    CHECK_EQ(g->ppu.vram[0x2000], 1);
    CHECK_EQ(g->ppu.vram[0x0000], 0);
    gb_destroy(g);
}

TEST(gb_cgb_copy_a_block_each_line) {
    gb_t *g = color_idle();
    while (!(g->ppu.line == 10 && g->ppu.dot == 0)) gb_tick(g);
    for (int i = 0; i < 0x40; i++) g->mem.wram[0x100 + i] = (uint8_t)(i + 1);
    gb_bus_write(g, 0xFF51, 0xC1);
    gb_bus_write(g, 0xFF52, 0x00);
    gb_bus_write(g, 0xFF53, 0x00);
    gb_bus_write(g, 0xFF54, 0x00);
    gb_bus_write(g, 0xFF55, 0x82); /* 3 blocks, one per HBlank */
    CHECK_EQ(gb_bus_peek(g, 0xFF55), 0x02);
    CHECK_EQ(g->ppu.vram[0], 0);
    while (g->ppu.line == 10) gb_tick(g);
    CHECK_EQ(g->ppu.vram[0x0F], 0x10);
    CHECK_EQ(g->ppu.vram[0x10], 0);
    CHECK_EQ(gb_bus_peek(g, 0xFF55), 0x01);
    while (g->ppu.line == 11) gb_tick(g);
    gb_bus_write(g, 0xFF55, 0x00); /* stopped with one block to go */
    CHECK_EQ(gb_bus_peek(g, 0xFF55), 0x80);
    while (g->ppu.line < 14) gb_tick(g);
    CHECK_EQ(g->ppu.vram[0x1F], 0x20);
    CHECK_EQ(g->ppu.vram[0x20], 0);
    gb_destroy(g);
}

/* Colour drawing: tile attributes (palette, bank, flips, priority), sprite palettes and bank, who is in front. */
TEST(gb_cgb_draw_colours_and_priority) {
    enum { RED = 0x001F, GREEN = 0x03E0, BLUE = 0x7C00, WHITE = 0x7FFF, BLACK = 0 };
    gb_t *g = color_idle();
    static const char *const corner[8] = {"31111111", "11111111", "11111111", "11111111",
                                          "11111111", "11111111", "11111111", "11111112"};
    gbtu_tile(g, 1, corner);
    gbtu_tile_fill(g, 2, 3);
    memset(g->ppu.vram + 0x2000 + 16, 0xFF, 16); /* bank 1's tile 1: all colour 3 */
    set_colour(g, 0, 0, 0, WHITE);
    set_colour(g, 0, 3, 1, RED), set_colour(g, 0, 3, 2, GREEN), set_colour(g, 0, 3, 3, BLUE);
    set_colour(g, 1, 5, 3, GREEN);
    g->ppu.vram[0x1800] = 1, g->ppu.vram[0x2000 + 0x1800] = 0x03;        /* palette 3 */
    g->ppu.vram[0x1801] = 1, g->ppu.vram[0x2000 + 0x1801] = 0x03 | 0x20; /* x flip */
    g->ppu.vram[0x1802] = 1, g->ppu.vram[0x2000 + 0x1802] = 0x03 | 0x40; /* y flip */
    g->ppu.vram[0x1803] = 1, g->ppu.vram[0x2000 + 0x1803] = 0x03 | 0x08; /* from bank 1 */
    g->ppu.vram[0x1820] = 1, g->ppu.vram[0x2000 + 0x1820] = 0x03 | 0x80; /* in front of sprites */
    g->ppu.vram[0x1821] = 1, g->ppu.vram[0x2000 + 0x1821] = 0x03;
    g->ppu.lcdc = 0x93;
    uint8_t *o = g->ppu.oam;
    o[0] = 16 + 8, o[1] = 8 + 0, o[2] = 2, o[3] = 0x05;      /* over the "in front" tile: hidden */
    o[4] = 16 + 8, o[5] = 8 + 8, o[6] = 2, o[7] = 0x05;      /* over a plain tile: shown */
    o[8] = 16 + 16, o[9] = 8 + 0, o[10] = 2, o[11] = 0x05;   /* over background colour 0 */
    o[12] = 16 + 40, o[13] = 8 + 40, o[14] = 1, o[15] = 0x0D; /* its tile from bank 1 */
    o[16] = 16 + 60, o[17] = 8 + 44, o[18] = 2, o[19] = 0x05; /* overlap: OAM order decides, not x */
    o[20] = 16 + 60, o[21] = 8 + 40, o[22] = 2, o[23] = 0x06; /* (palette 6 is all black) */
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), BLUE);
    CHECK_EQ(gbtu_px(g, 1, 0), RED);
    CHECK_EQ(gbtu_px(g, 7, 7), GREEN);
    CHECK_EQ(gbtu_px(g, 15, 0), BLUE);  /* x flip */
    CHECK_EQ(gbtu_px(g, 16, 7), BLUE);  /* y flip */
    CHECK_EQ(gbtu_px(g, 24, 3), BLUE);  /* bank 1's tile */
    CHECK_EQ(gbtu_px(g, 32, 0), WHITE); /* palette 0 colour 0 */
    CHECK_EQ(gbtu_px(g, 1, 8), RED);    /* the tile's priority bit beats the sprite */
    CHECK_EQ(gbtu_px(g, 9, 8), GREEN);  /* the sprite */
    CHECK_EQ(gbtu_px(g, 0, 16), GREEN);
    CHECK_EQ(gbtu_px(g, 40, 40), GREEN);
    CHECK_EQ(gbtu_px(g, 44, 60), GREEN); /* the earlier sprite in OAM wins, though it is further right */
    CHECK_EQ(gbtu_px(g, 40, 60), BLACK);
    g->ppu.lcdc = 0x92; /* LCDC bit 0 clear: the background stays, but sprites are always in front */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 1, 8), GREEN);
    CHECK_EQ(gbtu_px(g, 0, 0), BLUE);
    o[7] = 0x85; /* a sprite that asks to be behind */
    g->ppu.lcdc = 0x93;
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 9, 8), RED);
    g->cgb.opri = 1; /* the plain Game Boy's rule on request: the one further left wins */
    gbtu_frames(g, 1);
    CHECK_EQ(gbtu_px(g, 44, 60), BLACK);
    gb_destroy(g);
}

TEST(gb_cgb_plain_game_still_grey) {
    gb_t *g = gbtu_idle();
    gbtu_tile_fill(g, 0, 3);
    memset(g->cgb.bg_palette, 0x55, sizeof g->cgb.bg_palette);
    g->ppu.vram[0x2000 + 0x1800] = 0x07;
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 0, 0), 0x0000);
    gb_destroy(g);
}
