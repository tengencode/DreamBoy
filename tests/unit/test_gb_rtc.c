/* The MBC3 cartridge clock and the save file (cartridge RAM + the clock's 48-byte footer). */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

enum { T0 = 1700000000 }; /* some Unix time */

static gb_t *cart(uint8_t type, uint8_t ram_code) {
    uint8_t *rom = gbtu_rom(0x8000, type, ram_code, 0);
    gb_t *g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    gb_bus_write(g, 0x0000, 0x0A); /* RAM and clock on */
    return g;
}

static void latch(gb_t *g) {
    gb_bus_write(g, 0x6000, 0);
    gb_bus_write(g, 0x6000, 1);
}

/* A clock register (0x08 seconds .. 0x0C day high / flags), freshly latched. */
static uint8_t clock_reg(gb_t *g, uint8_t reg) {
    latch(g);
    gb_bus_write(g, 0x4000, reg);
    return gb_bus_peek(g, 0xA000);
}

static void set_reg(gb_t *g, uint8_t reg, uint8_t value) {
    gb_bus_write(g, 0x4000, reg);
    gb_bus_write(g, 0xA000, value);
}

TEST(gb_rtc_counts_real_seconds) {
    gb_t *g = cart(0x10, 2);
    gb_set_rtc(g, T0);
    CHECK_EQ(clock_reg(g, 0x08), 0);
    gb_set_rtc(g, T0 + 61);
    CHECK_EQ(clock_reg(g, 0x08), 1);
    CHECK_EQ(clock_reg(g, 0x09), 1);
    gb_set_rtc(g, T0 + 61 + 3600 * 5 + 86400 * 3);
    CHECK_EQ(clock_reg(g, 0x0A), 5);
    CHECK_EQ(clock_reg(g, 0x0B), 3);
    CHECK_EQ(clock_reg(g, 0x0C), 0);
    gb_set_rtc(g, T0); /* the phone's clock set back: nothing is added, nothing breaks */
    CHECK_EQ(clock_reg(g, 0x0B), 3);
    gb_set_rtc(g, T0 + 10);
    CHECK_EQ(clock_reg(g, 0x08), 11);
    gb_destroy(g);
}

TEST(gb_rtc_latch_freezes_the_reading) {
    gb_t *g = cart(0x10, 2);
    gb_set_rtc(g, T0);
    latch(g);
    gb_set_rtc(g, T0 + 30);
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0); /* still the latched value */
    gb_bus_write(g, 0x6000, 1);          /* 1 again without a 0 before: no new latch */
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0);
    latch(g);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 30);
    gb_destroy(g);
}

TEST(gb_rtc_game_sets_and_stops_it) {
    gb_t *g = cart(0x10, 2);
    gb_set_rtc(g, T0);
    set_reg(g, 0x08, 58);
    set_reg(g, 0x09, 59);
    set_reg(g, 0x0A, 23);
    set_reg(g, 0x0B, 0xFF);
    set_reg(g, 0x0C, 0x01); /* day 511 */
    gb_set_rtc(g, T0 + 2);  /* over the top: day 0, and the carry bit stays set */
    CHECK_EQ(clock_reg(g, 0x08), 0);
    CHECK_EQ(clock_reg(g, 0x09), 0);
    CHECK_EQ(clock_reg(g, 0x0A), 0);
    CHECK_EQ(clock_reg(g, 0x0B), 0);
    CHECK_EQ(clock_reg(g, 0x0C), 0x80);
    gb_set_rtc(g, T0 + 86400 + 2);
    CHECK_EQ(clock_reg(g, 0x0B), 1);
    CHECK_EQ(clock_reg(g, 0x0C), 0x80);
    set_reg(g, 0x0C, 0x40); /* the game halts the clock (and clears the carry) */
    gb_set_rtc(g, T0 + 86400 * 5);
    CHECK_EQ(clock_reg(g, 0x0B), 1);
    CHECK_EQ(clock_reg(g, 0x0C), 0x40);
    set_reg(g, 0x0C, 0x00); /* running again: from now, not from when it was halted */
    gb_set_rtc(g, T0 + 86400 * 5 + 5);
    CHECK_EQ(clock_reg(g, 0x0B), 1);
    CHECK_EQ(clock_reg(g, 0x08), 5);
    CHECK_EQ(gb_save_dirty(g), 1); /* setting the clock is something to save */
    gb_destroy(g);
}

TEST(gb_rtc_big_jumps_and_odd_values) {
    gb_t *g = cart(0x10, 2);
    gb_set_rtc(g, T0);
    gb_set_rtc(g, T0 + (int64_t)86400 * 365 * 10 + 7); /* ten years on: worked out, not counted second by second */
    CHECK_EQ(clock_reg(g, 0x08), 7);
    CHECK_EQ(clock_reg(g, 0x0B) | (clock_reg(g, 0x0C) & 1) << 8, 3650 % 512);
    CHECK_EQ(clock_reg(g, 0x0C) & 0x80, 0x80);
    gb_set_rtc(g, INT64_MAX); /* nonsense from outside must not overflow or hang */
    gb_set_rtc(g, INT64_MIN);
    gb_set_rtc(g, 0);
    set_reg(g, 0x08, 0xFF); /* only the bits the chip has are kept */
    CHECK_EQ(clock_reg(g, 0x08), 0x3F);
    set_reg(g, 0x0A, 0xFF);
    CHECK_EQ(clock_reg(g, 0x0A), 0x1F);
    set_reg(g, 0x0C, 0xFF);
    CHECK_EQ(clock_reg(g, 0x0C), 0xC1);
    gb_destroy(g);

    g = cart(0x13, 2); /* the same chip without a clock: those selections read 0xFF */
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF);
    gb_set_rtc(g, T0);
    gb_destroy(g);
}

TEST(gb_save_sizes) {
    gb_t *g = cart(0x10, 2);
    CHECK_EQ(gb_save_size(g), 0x2000 + 48);
    gb_destroy(g);
    g = cart(0x0F, 0); /* a clock and a battery but no RAM */
    CHECK_EQ(gb_save_size(g), 48);
    gb_destroy(g);
    g = cart(0x13, 3);
    CHECK_EQ(gb_save_size(g), 0x8000);
    gb_destroy(g);
    g = cart(0x12, 3); /* no battery */
    CHECK_EQ(gb_save_size(g), 0);
    uint8_t buf[4];
    CHECK_EQ(gb_save_write(g, buf, sizeof buf), 0);
    gb_destroy(g);
}

TEST(gb_save_round_trip_with_clock) {
    gb_t *g = cart(0x10, 2);
    gb_set_rtc(g, T0);
    gb_bus_write(g, 0x4000, 0);
    gb_bus_write(g, 0xA000, 0x12);
    gb_bus_write(g, 0xBFFF, 0x34);
    gb_set_rtc(g, T0 + 3 * 86400 + 4 * 3600 + 5 * 60 + 6);
    latch(g);
    gb_set_rtc(g, T0 + 3 * 86400 + 4 * 3600 + 5 * 60 + 9);
    static uint8_t save[0x2000 + 48];
    CHECK_EQ(gb_save_write(g, save, sizeof save - 1), 0);
    CHECK_EQ(gb_save_write(g, save, sizeof save), sizeof save);
    CHECK_EQ(save[0], 0x12);
    CHECK_EQ(save[0x1FFF], 0x34);
    /* The footer other emulators use: the running registers, the latched ones (4 bytes each), a 64-bit time. */
    static const uint8_t want[40] = {9, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0,
                                     6, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!memcmp(save + 0x2000, want, 40));
    int64_t stamp;
    memcpy(&stamp, save + 0x2000 + 40, 8);
    CHECK_EQ(stamp, T0 + 3 * 86400 + 4 * 3600 + 5 * 60 + 9);
    gb_destroy(g);

    g = cart(0x10, 2); /* into a fresh machine, a day later: the day that passed is added */
    gb_save_read(g, save, sizeof save);
    CHECK_EQ(gb_save_dirty(g), 0);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0x12);
    gb_bus_write(g, 0x4000, 0x08);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 6); /* the latched reading came back too */
    gb_set_rtc(g, stamp + 86400 + 1);
    CHECK_EQ(clock_reg(g, 0x08), 10);
    CHECK_EQ(clock_reg(g, 0x0B), 4);
    static uint8_t again[0x2000 + 48];
    gb_save_write(g, again, sizeof again);
    CHECK(!memcmp(save, again, 0x2000));
    gb_destroy(g);

    g = cart(0x10, 2); /* the older 44-byte footer (a 32-bit time) is read as well */
    gb_save_read(g, save, 0x2000 + 44);
    gb_set_rtc(g, stamp + 2);
    CHECK_EQ(clock_reg(g, 0x08), 11);
    CHECK_EQ(clock_reg(g, 0x0B), 3);
    gb_destroy(g);
}

/* Review focus: a save file of any length loads without reading or writing outside its buffers (run under the
 * sanitizer); what fits is copied, the rest of the RAM is 0xFF, a cut-off footer is ignored. */
TEST(gb_save_read_any_length) {
    static const size_t lengths[] = {0, 1, 0x1FFF, 0x2001, 0x2000 + 20, 0x2000 + 43, 0x2000 + 47, 0x6000};
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
        size_t n = lengths[i];
        uint8_t *data = malloc(n ? n : 1); /* exactly n bytes, so reading past the end is caught */
        memset(data, 0x5A, n);
        gb_t *g = cart(0x10, 2);
        gb_set_rtc(g, T0);
        gb_save_read(g, n ? data : NULL, n);
        CHECK_EQ(gb_bus_peek(g, 0xA000), n > 0 ? 0x5A : 0xFF);
        CHECK_EQ(gb_bus_peek(g, 0xBFFF), n >= 0x2000 ? 0x5A : 0xFF);
        if (n < 0x2000 + 44) { /* no whole footer: the clock is untouched */
            gb_set_rtc(g, T0 + 5);
            CHECK_EQ(clock_reg(g, 0x08), 5);
        }
        gb_run_frame(g);
        gb_destroy(g);
        free(data);
    }
    gb_t *g = cart(0x0F, 0); /* no RAM at all */
    uint8_t footer[48] = {30};
    gb_save_read(g, footer, 10);
    gb_save_read(g, footer, sizeof footer);
    CHECK_EQ(clock_reg(g, 0x08), 30);
    gb_destroy(g);
    g = cart(0x00, 0); /* nothing to save into */
    gb_save_read(g, footer, sizeof footer);
    gb_destroy(g);
}
