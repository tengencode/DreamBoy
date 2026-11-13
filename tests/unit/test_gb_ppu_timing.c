/* The screen's timing: lines, modes, the STAT interrupt line, memory the CPU cannot reach, OAM DMA, LCD off. */
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

#define LCDC 0xFF40
#define STAT 0xFF41
#define LY 0xFF44
#define LYC 0xFF45
#define DMA 0xFF46

/* A machine whose screen is at the very start of `line` (a full, normal line follows). */
static gb_t *at_line(int line) {
    gb_t *g = gbtu_idle();
    while (!(g->ppu.line == 0 && g->ppu.dot < 4)) gb_tick(g); /* into a normal frame */
    while (!(g->ppu.line == line && g->ppu.dot < 4)) gb_tick(g);
    return g;
}

static int mode(const gb_t *g) {
    return gb_bus_peek(g, STAT) & 3;
}

/* On to dot `dot` of the line the screen is in. */
static void to_dot(gb_t *g, int dot) {
    while (g->ppu.dot < dot) gb_tick(g);
}

TEST(gb_ppu_line_and_frame_length) {
    gb_t *g = at_line(0);
    uint64_t start = gb_debug_ticks(g);
    for (int line = 0; line < 153; line++) {
        CHECK_EQ(gb_bus_peek(g, LY), line);
        uint64_t t = gb_debug_ticks(g);
        while (gb_bus_peek(g, LY) == line) gb_tick(g);
        CHECK_EQ(gb_debug_ticks(g) - t, 456);
    }
    CHECK_EQ(gb_bus_peek(g, LY), 153); /* the last line shows 153 for one cycle only, then 0 */
    gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, LY), 0);
    while (g->ppu.line == 153) gb_tick(g);
    CHECK_EQ(gb_debug_ticks(g) - start, GB_FRAME_TICKS);
    gb_destroy(g);
}

/* A line as the CPU sees it, dot by dot (the numbers measured on hardware by the mooneye test suite). */
TEST(gb_ppu_line_as_the_cpu_sees_it) {
    gb_t *g = at_line(10);
    g->ppu.lcdc = 0x91; /* no sprites, no window */
    g->ppu.vram[0] = 0x11;
    g->ppu.oam[0] = 0x22;
    CHECK_EQ(gb_bus_peek(g, LY), 10); /* dot 0: the new line, OAM busy, STAT still mode 0 */
    CHECK_EQ(mode(g), 0);
    CHECK(gb_ppu_oam_blocked(g));
    CHECK(!gb_ppu_vram_blocked(g));
    to_dot(g, 4);
    CHECK_EQ(mode(g), 2);
    to_dot(g, 76);
    CHECK_EQ(mode(g), 2);
    CHECK(!gb_ppu_vram_blocked(g));
    to_dot(g, 80); /* video RAM is busy one cycle before STAT says "drawing" */
    CHECK_EQ(mode(g), 2);
    CHECK(gb_ppu_vram_blocked(g));
    to_dot(g, 84);
    CHECK_EQ(mode(g), 3);
    to_dot(g, 252);
    CHECK_EQ(mode(g), 3);
    CHECK(gb_ppu_vram_blocked(g) && gb_ppu_oam_blocked(g));
    to_dot(g, 256);
    CHECK_EQ(mode(g), 0);
    CHECK(!gb_ppu_vram_blocked(g) && !gb_ppu_oam_blocked(g));
    to_dot(g, 452);
    CHECK_EQ(mode(g), 0);
    CHECK_EQ(gb_bus_peek(g, LY), 10);
    gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, LY), 11);
    gb_destroy(g);

    g = at_line(144); /* ten lines of VBlank; STAT shows mode 1 from dot 4 */
    CHECK_EQ(mode(g), 0);
    gb_tick(g);
    for (int i = 0; i < 4560 / 4 - 1; i++) {
        CHECK_EQ(mode(g), 1);
        gb_tick(g);
    }
    CHECK_EQ(mode(g), 1); /* dot 0 of line 0 */
    CHECK_EQ(gb_bus_peek(g, LY), 0);
    gb_tick(g);
    CHECK_EQ(mode(g), 2);
    gb_destroy(g);
}

TEST(gb_ppu_drawing_takes_longer) {
    gb_t *g = at_line(10);
    g->ppu.lcdc = 0x91;
    g->ppu.scx = 5; /* a fine scroll of 5: 5 dots more, which the CPU sees as 2 cycles */
    to_dot(g, 260);
    CHECK_EQ(mode(g), 3);
    to_dot(g, 264);
    CHECK_EQ(mode(g), 0);
    gb_destroy(g);

    g = at_line(10);
    g->ppu.lcdc = 0x93; /* sprites on, and ten of them on line 10 */
    for (int i = 0; i < 10; i++) g->ppu.oam[i * 4] = 16 + 10, g->ppu.oam[i * 4 + 1] = (uint8_t)(8 + i * 8);
    to_dot(g, 256 + 10 * 6 - 4);
    CHECK_EQ(mode(g), 3);
    to_dot(g, 376); /* never past dot 80 + 289 (+ the 4 dots STAT lags) */
    CHECK_EQ(mode(g), 0);
    to_dot(g, 452);
    gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, LY), 11); /* the line is still 456 dots */
    CHECK_EQ(g->ppu.dot, 0);
    gb_destroy(g);
}

TEST(gb_ppu_vblank_interrupt) {
    gb_t *g = at_line(143);
    g->mem.iflag = 0;
    while (g->ppu.line == 143) {
        CHECK_EQ(g->mem.iflag & 1, 0);
        gb_tick(g);
    }
    CHECK_EQ(g->mem.iflag & 1, 1);
    g->mem.iflag = 0;
    for (int i = 0; i < (GB_FRAME_TICKS - 456) / 4; i++) gb_tick(g); /* once a frame */
    CHECK_EQ(g->mem.iflag & 1, 0);
    for (int i = 0; i < 456 / 4; i++) gb_tick(g);
    CHECK_EQ(g->mem.iflag & 1, 1);
    gb_destroy(g);
}

TEST(gb_ppu_stat_sources) {
    gb_t *g = at_line(20); /* HBlank source: once a line, at dot 252 (4 dots before STAT shows mode 0) */
    g->ppu.lcdc = 0x91;
    g->ppu.stat = 0x08;
    g->ppu.stat_line = 0;
    g->mem.iflag = 0;
    to_dot(g, 248);
    CHECK_EQ(g->mem.iflag & 2, 0);
    to_dot(g, 252);
    CHECK_EQ(g->mem.iflag & 2, 2);
    gb_destroy(g);

    g = at_line(20); /* OAM source: at each line's very start */
    g->ppu.stat = 0x20;
    to_dot(g, 452);
    g->mem.iflag = 0;
    gb_tick(g);
    CHECK_EQ(g->ppu.dot, 0);
    CHECK_EQ(g->mem.iflag & 2, 2);
    gb_destroy(g);

    g = at_line(20); /* LY = LYC source: 4 dots into the line */
    gb_bus_write(g, LYC, 22);
    gb_bus_write(g, STAT, 0x40);
    g->mem.iflag = 0;
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 0);
    while (g->ppu.line != 22) gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, LY), 22);
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 0);
    CHECK_EQ(g->mem.iflag & 2, 0);
    gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 4);
    CHECK_EQ(g->mem.iflag & 2, 2);
    to_dot(g, 452);
    gb_tick(g); /* line 23: the flag drops at once */
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 0);
    gb_destroy(g);

    g = at_line(143); /* VBlank source */
    g->ppu.stat = 0x10;
    g->mem.iflag = 0;
    while (g->ppu.line == 143) gb_tick(g);
    CHECK_EQ(g->mem.iflag & 2, 2);
    gb_destroy(g);

    g = at_line(143); /* the OAM source also fires when VBlank starts */
    g->ppu.stat = 0x20;
    to_dot(g, 452);
    g->mem.iflag = 0;
    gb_tick(g);
    CHECK_EQ(g->mem.iflag & 3, 3);
    gb_destroy(g);

    g = gbtu_idle(); /* the top bit of STAT reads 1; the low three cannot be written */
    gb_bus_write(g, STAT, 0x07);
    CHECK_EQ(gb_bus_peek(g, STAT) & 0xF8, 0x80);
    gb_destroy(g);
}

/* While one source holds the line high, another cannot ask again. */
TEST(gb_ppu_stat_line_blocks) {
    gb_t *g = at_line(143);
    g->ppu.stat = 0x18; /* HBlank and VBlank: HBlank of line 143 runs straight into VBlank */
    to_dot(g, 300);
    CHECK_EQ(g->ppu.stat_line, 1);
    g->mem.iflag = 0;
    while (g->ppu.line != 145) gb_tick(g);
    CHECK_EQ(g->mem.iflag & 2, 0);
    gb_destroy(g);

    g = at_line(20);
    g->ppu.lcdc = 0x91;
    g->ppu.stat = 0x28; /* HBlank and OAM: HBlank runs into the next line's OAM scan, then the line drops */
    to_dot(g, 300);
    g->mem.iflag = 0;
    to_dot(g, 452);
    gb_tick(g);
    to_dot(g, 76);
    CHECK_EQ(g->mem.iflag & 2, 0);
    to_dot(g, 252); /* low during drawing, so the next HBlank asks again */
    CHECK_EQ(g->mem.iflag & 2, 2);
    gb_destroy(g);
}

TEST(gb_ppu_memory_the_cpu_cannot_reach) {
    gb_t *g = at_line(30);
    g->ppu.lcdc = 0x91;
    g->ppu.vram[0x10] = 0x5A;
    g->ppu.oam[4] = 0x6B;
    /* OAM scan: OAM is busy, video RAM is free */
    CHECK_EQ(gb_bus_read(g, 0xFE04), 0xFF);
    CHECK_EQ(gb_bus_read(g, 0x8010), 0x5A);
    gb_bus_write(g, 0xFE04, 0x11);
    CHECK_EQ(g->ppu.oam[4], 0x6B);
    to_dot(g, 100); /* drawing: both busy */
    CHECK_EQ(gb_bus_read(g, 0xFE04), 0xFF);
    CHECK_EQ(gb_bus_read(g, 0x8010), 0xFF);
    gb_bus_write(g, 0x8010, 0x22);
    CHECK_EQ(g->ppu.vram[0x10], 0x5A);
    CHECK_EQ(gb_bus_peek(g, 0x8010), 0x5A); /* a debug read sees it all the same */
    to_dot(g, 256); /* HBlank: both free */
    CHECK_EQ(gb_bus_read(g, 0xFE04), 0x6B);
    gb_bus_write(g, 0x8010, 0x33);
    gb_bus_write(g, 0xFE04, 0x44);
    CHECK_EQ(g->ppu.vram[0x10], 0x33);
    CHECK_EQ(g->ppu.oam[4], 0x44);
    gb_destroy(g);
}

TEST(gb_ppu_oam_dma) {
    gb_t *g = at_line(145); /* in VBlank, so only the DMA itself can block OAM */
    for (int i = 0; i < 0xA0; i++) g->mem.wram[0x100 + i] = (uint8_t)(i + 1);
    g->ppu.oam[0] = 0x77;
    gb_bus_write(g, DMA, 0xC1);
    CHECK_EQ(gb_bus_peek(g, DMA), 0xC1);
    CHECK_EQ(gb_bus_read(g, 0xFE00), 0x77); /* one cycle of preparation: OAM still as it was, and reachable */
    CHECK_EQ(gb_bus_read(g, 0xFE00), 0xFF); /* copying: OAM cannot be read */
    CHECK_EQ(g->ppu.oam[0], 1);
    for (int i = 0; i < 158; i++) gb_tick(g);
    CHECK_EQ(g->ppu.oam[0x9E], 0x9F);
    CHECK_EQ(g->ppu.oam[0x9F], 0x00);
    CHECK_EQ(gb_bus_read(g, 0xFE00), 0xFF); /* the 160th byte */
    CHECK_EQ(g->ppu.oam[0x9F], 0xA0);
    CHECK_EQ(gb_bus_read(g, 0xFE00), 0x01); /* done */
    gb_destroy(g);

    g = at_line(145); /* from the top of memory, work RAM is copied */
    g->mem.wram[0x1F00 & 0xFFF] = 0;
    g->mem.wram[0x1000 + 0xF00] = 0x99; /* 0xDF00, seen at 0xFF00 by the DMA */
    gb_bus_write(g, DMA, 0xFF);
    for (int i = 0; i < 3; i++) gb_tick(g);
    CHECK_EQ(g->ppu.oam[0], 0x99);
    gb_destroy(g);
}

/* Review focus: the screen off for many frames, then on — frames stay exactly as long, and the picture returns. */
TEST(gb_lcd_off_frames_still_end) {
    gb_t *g = gbtu_idle();
    gbtu_tile_fill(g, 0, 3);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 80, 72), 0x0000); /* BGP 0xFC: colour 3 is black */
    gb_bus_write(g, LCDC, 0x11);
    CHECK_EQ(gbtu_px(g, 80, 72), 0x7FFF); /* off: blank at once */
    g->mem.iflag = 0;
    uint64_t t = gb_debug_ticks(g);
    gbtu_frames(g, 5);
    CHECK(gb_debug_ticks(g) - t >= 5u * GB_FRAME_TICKS - 16 && gb_debug_ticks(g) - t <= 5u * GB_FRAME_TICKS + 16);
    CHECK_EQ(gb_bus_peek(g, LY), 0);
    CHECK_EQ(gb_bus_peek(g, STAT) & 3, 0);
    CHECK_EQ(g->mem.iflag & 3, 0); /* no VBlank or STAT interrupts while off */
    CHECK_EQ(gb_bus_read(g, 0x8000), 0xFF); /* tile 0's first byte: video RAM is reachable */
    gb_bus_write(g, LCDC, 0x91);
    CHECK_EQ(gb_bus_peek(g, LY), 0);
    gbtu_frames(g, 2);
    CHECK_EQ(gbtu_px(g, 80, 72), 0x0000);
    CHECK_EQ(g->mem.iflag & 1, 1);
    gb_destroy(g);
}

/* After switching on: no OAM scan to see on the first line (mode 0, then drawing from dot 84), LY 1 comes 452
 * dots after the write's cycle (measured by the mooneye suite's lcdon_timing). */
TEST(gb_lcd_on_first_line) {
    gb_t *g = gbtu_idle();
    g->ppu.oam[0] = 0x22;
    gb_bus_write(g, LCDC, 0x11);
    gb_bus_write(g, LCDC, 0x91);
    CHECK_EQ(mode(g), 0);
    int cycles = 0;
    for (; mode(g) == 0; cycles++) {
        CHECK(!gb_ppu_oam_blocked(g) && !gb_ppu_vram_blocked(g));
        gb_tick(g);
    }
    CHECK_EQ(cycles, 20);
    CHECK_EQ(mode(g), 3);
    for (; gb_bus_peek(g, LY) == 0; cycles++) gb_tick(g);
    CHECK_EQ(cycles, 113);
    gb_tick(g);
    CHECK_EQ(mode(g), 2);
    gb_destroy(g);
}

TEST(gb_stat_lyc_flag_frozen_while_off) {
    gb_t *g = at_line(5);
    gb_bus_write(g, LYC, 5);
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 4);
    gb_bus_write(g, LCDC, 0x11);
    gb_bus_write(g, LYC, 9); /* no comparison happens while the screen is off */
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 4);
    gb_bus_write(g, LCDC, 0x91); /* on: LY 0 against LYC 9 */
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 0);
    gb_destroy(g);
}

/* With the screen off the interrupt line keeps its level, so a flag that is true before and after asks for nothing. */
TEST(gb_stat_line_kept_while_off) {
    gb_t *g = at_line(144);
    gb_bus_write(g, LYC, 144);
    gb_bus_write(g, STAT, 0x40);
    gb_tick(g);
    CHECK_EQ(g->ppu.stat_line, 1);
    gb_bus_write(g, LCDC, 0x11);
    g->mem.iflag = 0;
    gb_bus_write(g, LYC, 0);
    gb_bus_write(g, LCDC, 0x91); /* LY 0 = LYC 0: still true */
    CHECK_EQ(gb_bus_peek(g, STAT) & 4, 4);
    CHECK_EQ(g->mem.iflag & 2, 0);
    gb_destroy(g);
}
