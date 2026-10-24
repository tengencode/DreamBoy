#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

static line_buffers_t lb;
static uint16_t line[PPU_WIDTH];

/* BG0 (priority 0) in front of BG1 (priority 1) across the whole line. */
static void two_layers(uint16_t front, uint16_t back) {
    memset(&lb, 0, sizeof lb);
    for (int x = 0; x < PPU_WIDTH; x++) {
        put_px(&lb.bg[0][x], front, 0, PX_OPAQUE);
        put_px(&lb.bg[1][x], back, 1, PX_OPAQUE);
    }
}

TEST(window0_inside_and_outside) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x2000);         /* WIN0 on */
    tu_io16(g, 0x040, (10 << 8) | 20); /* x 10..19 */
    tu_io16(g, 0x044, 160);            /* y 0..159 */
    tu_io16(g, 0x048, 0x02);           /* inside: BG1 only */
    tu_io16(g, 0x04A, 0x01);           /* outside: BG0 only */
    ppu_compose(g, 5, &lb, line);
    CHECK_EQ(line[9], C_RED);
    CHECK_EQ(line[10], C_GREEN);
    CHECK_EQ(line[19], C_GREEN);
    CHECK_EQ(line[20], C_RED);
    gba_destroy(g);
}

TEST(window_wraps_horizontally_and_limits_vertically) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x2000);
    tu_io16(g, 0x040, (230 << 8) | 10); /* x >= 230 or x < 10 */
    tu_io16(g, 0x044, (50 << 8) | 60);  /* y 50..59 */
    tu_io16(g, 0x048, 0x02);
    tu_io16(g, 0x04A, 0x01);
    ppu_compose(g, 55, &lb, line);
    CHECK_EQ(line[5], C_GREEN);
    CHECK_EQ(line[10], C_RED);
    CHECK_EQ(line[229], C_RED);
    CHECK_EQ(line[230], C_GREEN);
    ppu_compose(g, 49, &lb, line);
    CHECK_EQ(line[5], C_RED);
    ppu_compose(g, 60, &lb, line);
    CHECK_EQ(line[5], C_RED);
    gba_destroy(g);
}

TEST(window0_wins_over_window1) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x6000);
    tu_io16(g, 0x040, 240);    /* WIN0: whole line */
    tu_io16(g, 0x042, 240);    /* WIN1: whole line */
    tu_io16(g, 0x044, 160);
    tu_io16(g, 0x046, 160);
    tu_io16(g, 0x048, 0x0102); /* WIN0: BG1; WIN1: BG0 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_GREEN);
    gba_destroy(g);
}

TEST(obj_window_region) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    for (int x = 100; x < 108; x++) lb.objwin[x] = true;
    tu_io16(g, 0x000, 0x8000);
    tu_io16(g, 0x04A, 0x0201); /* OBJ window: BG1; outside: BG0 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[99], C_RED);
    CHECK_EQ(line[100], C_GREEN);
    CHECK_EQ(line[107], C_GREEN);
    gba_destroy(g);
}

TEST(alpha_blend) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x050, 0x0001 | (1 << 6) | 0x0200); /* BG0 over BG1, alpha */
    tu_io16(g, 0x052, 0x0808);                     /* EVA = EVB = 8/16 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    tu_io16(g, 0x052, 0x1F1F); /* coefficients above 16 act as 16; channels saturate */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x7C1F);
    tu_io16(g, 0x050, 0x0001 | (1 << 6)); /* BG1 is not a second target: no blend */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_RED);
    gba_destroy(g);
}

TEST(brighten_and_darken) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x050, 0x0001 | (2 << 6));
    tu_io16(g, 0x054, 8);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3DFF);
    tu_io16(g, 0x054, 31); /* EVY above 16 acts as 16: white */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_WHITE);
    tu_io16(g, 0x050, 0x0001 | (3 << 6));
    tu_io16(g, 0x054, 8);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x0010);
    gba_destroy(g);
}

TEST(backdrop_can_be_a_first_target) {
    gba_t *g = gba_create(NULL);
    memset(&lb, 0, sizeof lb);
    tu_pal(g, 0, C_RED);
    tu_io16(g, 0x050, 0x0020 | (3 << 6)); /* backdrop, darken */
    tu_io16(g, 0x054, 16);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_BLACK);
    gba_destroy(g);
}

TEST(semi_transparent_obj_always_blends) {
    gba_t *g = gba_create(NULL);
    memset(&lb, 0, sizeof lb);
    put_px(&lb.obj[0], C_RED, 0, PX_OPAQUE | PX_SEMI);
    put_px(&lb.bg[1][0], C_BLUE, 1, PX_OPAQUE);
    tu_io16(g, 0x050, 0x0200); /* only a second target; effect mode "none" */
    tu_io16(g, 0x052, 0x0808);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    gba_destroy(g);
}

TEST(window_can_disable_effects) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x000, 0x2000);
    tu_io16(g, 0x040, 240);
    tu_io16(g, 0x044, 160);
    tu_io16(g, 0x050, 0x0001 | (1 << 6) | 0x0200);
    tu_io16(g, 0x052, 0x0808);
    tu_io16(g, 0x048, 0x03); /* BG0 + BG1 visible, effects off */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_RED);
    tu_io16(g, 0x048, 0x23); /* effects on */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    gba_destroy(g);
}
