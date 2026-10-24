#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

TEST(render_backdrop_fills_the_line) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_RED);
    ppu_render_line(g, 5);
    CHECK_EQ(tu_px(g, 0, 5), C_RED);
    CHECK_EQ(tu_px(g, 239, 5), C_RED);
    gba_destroy(g);
}

TEST(render_forced_blank_is_white) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_RED);
    tu_io16(g, 0x000, 0x0080);
    ppu_render_line(g, 3);
    CHECK_EQ(tu_px(g, 0, 3), C_WHITE);
    CHECK_EQ(tu_px(g, 239, 3), C_WHITE);
    gba_destroy(g);
}

TEST(render_frame_loop_draws_every_visible_line) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_pal(g, 0, C_BLUE);
    gba_run_frame(g);
    CHECK_EQ(tu_px(g, 0, 0), C_BLUE);
    CHECK_EQ(tu_px(g, 239, 159), C_BLUE);
    CHECK(gba_framebuffer(g) == g->ppu.framebuffer);
    gba_destroy(g);
}

TEST(compose_orders_layers_by_priority_then_layer) {
    gba_t *g = gba_create(NULL);
    static line_buffers_t lb;
    uint16_t line[PPU_WIDTH];
    memset(&lb, 0, sizeof lb);
    tu_pal(g, 0, C_MAGENTA);
    put_px(&lb.bg[0][0], C_RED, 2, PX_OPAQUE); /* x0: BG1 (prio 1) is in front of BG0 (prio 2) */
    put_px(&lb.bg[1][0], C_GREEN, 1, PX_OPAQUE);
    put_px(&lb.bg[0][1], C_RED, 1, PX_OPAQUE); /* x1: equal priority, BG0 wins */
    put_px(&lb.bg[1][1], C_GREEN, 1, PX_OPAQUE);
    put_px(&lb.bg[2][2], C_BLUE, 0, PX_OPAQUE); /* x2: equal priority, OBJ wins */
    put_px(&lb.obj[2], C_YELLOW, 0, PX_OPAQUE);
    put_px(&lb.bg[3][3], C_BLUE, 3, PX_OPAQUE); /* x3: OBJ prio 1 in front of BG3 prio 3 */
    put_px(&lb.obj[3], C_YELLOW, 1, PX_OPAQUE);
    ppu_compose(g, 0, &lb, line); /* x4: nothing drawn → backdrop */
    CHECK_EQ(line[0], C_GREEN);
    CHECK_EQ(line[1], C_RED);
    CHECK_EQ(line[2], C_YELLOW);
    CHECK_EQ(line[3], C_YELLOW);
    CHECK_EQ(line[4], C_MAGENTA);
    gba_destroy(g);
}
