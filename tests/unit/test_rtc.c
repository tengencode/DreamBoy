#include "test.h"
#include "testutil.h"

static void port(gba_t *g, uint32_t reg, uint16_t value) {
    bus_write16(g, 0x08000000 + reg, value, false);
}

/* A clock game's routines. Pins: 0 SCK, 1 SIO, 2 CS. */
static void rtc_begin(gba_t *g) {
    port(g, 0xC8, 1);
    port(g, 0xC6, 7);
    port(g, 0xC4, 1); /* SCK high, CS low */
    port(g, 0xC4, 5); /* CS high */
}

static void rtc_end(gba_t *g) {
    port(g, 0xC4, 1);
}

static void rtc_send(gba_t *g, uint8_t value, bool msb_first) {
    port(g, 0xC6, 7);
    for (int i = 0; i < 8; i++) {
        uint16_t bit = (value >> (msb_first ? 7 - i : i)) & 1;
        port(g, 0xC4, (uint16_t)(bit << 1 | 4)); /* SCK low, SIO = bit */
        port(g, 0xC4, (uint16_t)(bit << 1 | 5)); /* SCK high: the clock takes the bit */
    }
}

static uint8_t rtc_receive(gba_t *g) {
    port(g, 0xC6, 5); /* SIO in */
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        port(g, 0xC4, 4); /* SCK low: the clock puts out the next bit */
        port(g, 0xC4, 5);
        v |= (uint8_t)(((bus_read16(g, 0x080000C4, false) >> 1) & 1) << i);
    }
    return v;
}

/* Sends a command (as games do: most significant bit first) and reads n bytes. */
static void rtc_read(gba_t *g, uint8_t command, uint8_t *out, int n) {
    rtc_begin(g);
    rtc_send(g, command, true);
    for (int i = 0; i < n; i++) out[i] = rtc_receive(g);
    rtc_end(g);
}

static void check_bytes(const uint8_t *got, const uint8_t *want, int n) {
    for (int i = 0; i < n; i++) CHECK_EQ(got[i], want[i]);
}

TEST(rtc_reads_date_and_time) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0); /* 2026-09-25 13:45:30, a Friday */
    uint8_t t[7];
    static const uint8_t want[7] = {0x26, 0x09, 0x25, 0x05, 0x13, 0x45, 0x30};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_time_command_and_utc_offset) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 3600); /* one hour ahead of UTC */
    uint8_t t[3];
    static const uint8_t want[3] = {0x14, 0x45, 0x30};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, want, 3);
    gba_destroy(g);
}

TEST(rtc_control_register_and_12_hour_mode) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    uint8_t c, t[7];
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x40); /* 24-hour mode, healthy */
    rtc_begin(g);
    rtc_send(g, 0x62, true);  /* control write */
    rtc_send(g, 0x00, false); /* 12-hour mode */
    rtc_end(g);
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x00);
    rtc_read(g, 0x65, t, 7);
    CHECK_EQ(t[4], 0x01); /* 13:45 → 1 */
    gba_destroy(g);
}

TEST(rtc_reset_command_clears_control) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t c;
    rtc_begin(g);
    rtc_send(g, 0x60, true);
    rtc_end(g);
    rtc_read(g, 0x63, &c, 1);
    CHECK_EQ(c, 0x00);
    gba_destroy(g);
}

TEST(rtc_ignores_bad_commands) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t v;
    rtc_read(g, 0x00, &v, 1); /* not 0110 in the low nibble */
    CHECK_EQ(v, 0xFF);        /* SIO stays high */
    gba_destroy(g);
}

TEST(rtc_chip_select_low_aborts) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    rtc_begin(g);
    port(g, 0xC4, 4); /* half a command ... */
    port(g, 0xC4, 5);
    port(g, 0xC4, 6);
    port(g, 0xC4, 7);
    rtc_end(g); /* ... then CS low */
    uint8_t t[7];
    static const uint8_t want[7] = {0x26, 0x09, 0x25, 0x05, 0x13, 0x45, 0x30};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_keeps_time_with_emulated_seconds) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    gba_set_rtc(g, 1790343930, 0);
    g->now += 61ull * 16777216; /* 61 emulated seconds */
    uint8_t t[3];
    static const uint8_t want[3] = {0x13, 0x46, 0x31};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, want, 3);
    gba_reset(g); /* a reset does not stop the clock */
    g->now += 16777216;
    static const uint8_t after[3] = {0x13, 0x46, 0x32};
    rtc_read(g, 0x67, t, 3);
    check_bytes(t, after, 3);
    gba_destroy(g);
}

TEST(rtc_defaults_to_the_year_2000) {
    gba_t *g = tu_rom_code("AXVE", NULL);
    uint8_t t[7];
    static const uint8_t want[7] = {0x00, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00};
    rtc_read(g, 0x65, t, 7);
    check_bytes(t, want, 7);
    gba_destroy(g);
}

TEST(rtc_calendar_edges) {
    static const struct {
        int64_t t;
        uint8_t want[7];
    } cases[] = {
        {1709164800, {0x24, 0x02, 0x29, 0x04, 0x00, 0x00, 0x00}}, /* leap day 2024 */
        {4102444799, {0x99, 0x12, 0x31, 0x04, 0x23, 0x59, 0x59}}, /* last second of 2099 */
        {1790380800, {0x26, 0x09, 0x26, 0x06, 0x00, 0x00, 0x00}}, /* midnight, Saturday */
    };
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        uint8_t out[7];
        rtc_calendar(cases[i].t, true, out);
        check_bytes(out, cases[i].want, 7);
    }
}
