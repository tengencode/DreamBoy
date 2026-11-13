/* The serial port (no cable) and the joypad. */
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

#define P1 0xFF00
#define SB 0xFF01
#define SC 0xFF02

static gb_t *machine(void) {
    static const uint8_t code[] = {0x18, 0xFE};
    gb_t *g = gbtu_boot(code, 2);
    g->timer.div = 0;
    return g;
}

TEST(gb_serial_no_cable) {
    gb_t *g = machine();
    g->timer.div = 0xFFFC; /* the write's own cycle brings the divider to 0 */
    gb_bus_write(g, SB, 'H');
    g->timer.div = 0xFFFC;
    gb_bus_write(g, SC, 0x81);
    CHECK_EQ(gb_bus_peek(g, SC), 0xFF);
    for (int i = 0; i < 8 * 512 / 4 - 1; i++) gb_tick(g);
    CHECK_EQ(g->mem.iflag & 8, 0);
    CHECK_EQ(gb_bus_peek(g, SC) & 0x80, 0x80);
    gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, SB), 0xFF); /* nobody on the other end: all 1s come in */
    CHECK_EQ(gb_bus_peek(g, SC), 0x7F);
    CHECK_EQ(g->mem.iflag & 8, 8);
    char out[8];
    CHECK_EQ(gb_debug_serial(g, out, sizeof out), 1);
    CHECK(!strcmp(out, "H"));
    gb_destroy(g);

    g = machine(); /* the other side's clock never comes */
    gb_bus_write(g, SB, 'x');
    gb_bus_write(g, SC, 0x80);
    for (int i = 0; i < 100000; i++) gb_tick(g);
    CHECK_EQ(gb_bus_peek(g, SB), 'x');
    CHECK_EQ(g->mem.iflag & 8, 0);
    gb_destroy(g);
}

TEST(gb_serial_debug_buffer_is_bounded) {
    gb_t *g = machine();
    for (int i = 0; i < 5000; i++) {
        gb_bus_write(g, SB, 'a');
        gb_bus_write(g, SC, 0x81);
    }
    static char out[8192];
    CHECK_EQ(gb_debug_serial(g, out, sizeof out), 4096);
    char small[4];
    CHECK_EQ(gb_debug_serial(g, small, sizeof small), 3);
    CHECK(!strcmp(small, "aaa"));
    gb_destroy(g);
}

TEST(gb_joypad_matrix) {
    gb_t *g = machine();
    gb_bus_write(g, P1, 0x30); /* nothing selected */
    gb_set_keys(g, 0xFF);
    CHECK_EQ(gb_bus_peek(g, P1), 0xFF);
    gb_set_keys(g, GB_KEY_A | GB_KEY_START | GB_KEY_LEFT);
    gb_bus_write(g, P1, 0x10); /* bit 5 low: the buttons */
    CHECK_EQ(gb_bus_peek(g, P1), 0xC0 | 0x10 | 0x06);
    gb_bus_write(g, P1, 0x20); /* bit 4 low: the directions */
    CHECK_EQ(gb_bus_peek(g, P1), 0xC0 | 0x20 | 0x0D);
    gb_bus_write(g, P1, 0x00); /* both */
    CHECK_EQ(gb_bus_peek(g, P1), 0xC0 | 0x04);
    gb_destroy(g);
}

TEST(gb_joypad_interrupt) {
    gb_t *g = machine();
    gb_bus_write(g, P1, 0x10);
    gb_set_keys(g, GB_KEY_UP); /* not a selected line */
    CHECK_EQ(g->mem.iflag & 0x10, 0);
    gb_set_keys(g, GB_KEY_UP | GB_KEY_B);
    CHECK_EQ(g->mem.iflag & 0x10, 0x10);
    g->mem.iflag = 0;
    gb_set_keys(g, GB_KEY_UP); /* a release raises nothing */
    CHECK_EQ(g->mem.iflag & 0x10, 0);
    gb_bus_write(g, P1, 0x20); /* selecting a line with a held key pulls it low */
    CHECK_EQ(g->mem.iflag & 0x10, 0x10);
    gb_destroy(g);
}
