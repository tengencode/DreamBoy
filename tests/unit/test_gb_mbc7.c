/* The tilt cartridge chip (MBC7): its two switches, the tilt sensor's latch, and its save chip (a 93LC56 serial
 * EEPROM, 128 words of 16 bits, driven pin by pin through register 0xA080). */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

#define LEVEL 0x81D0
#define ONE_G 0x70

static gb_t *tilt_cart(void) {
    uint8_t *rom = gbtu_rom(0x20000, 0x22, 0, 0);
    for (size_t bank = 1; bank < 8; bank++) rom[bank * 0x4000] = (uint8_t)bank;
    gb_t *g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, 0x20000), GB_OK);
    free(rom);
    return g;
}

static void open_registers(gb_t *g) {
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0x4000, 0x40);
}

static unsigned word_at(const gb_t *g, uint16_t lo) {
    return gb_bus_peek(g, lo) | (unsigned)gb_bus_peek(g, (uint16_t)(lo + 0x10)) << 8;
}

static void latch(gb_t *g) {
    gb_bus_write(g, 0xA000, 0x55);
    gb_bus_write(g, 0xA010, 0xAA);
}

/* The save chip's pins: bit 7 chip select, bit 6 clock, bit 1 data in; bit 0 reads data out. */
#define CS 0x80
#define CLK 0x40
#define DI 0x02

static void chip_select(gb_t *g, int on) {
    gb_bus_write(g, 0xA080, on ? CS : 0);
}

static int clock_bit(gb_t *g, int bit) { /* one rising edge with data in = bit; what data out shows after it */
    uint8_t pins = (uint8_t)(CS | (bit ? DI : 0));
    gb_bus_write(g, 0xA080, pins);
    gb_bus_write(g, 0xA080, pins | CLK);
    int out = gb_bus_peek(g, 0xA080) & 1;
    gb_bus_write(g, 0xA080, pins);
    return out;
}

static void send(gb_t *g, unsigned bits, int count) { /* high bit first */
    for (int i = count - 1; i >= 0; i--) clock_bit(g, (bits >> i) & 1);
}

/* Start bit, 2 opcode bits, 8 address bits (the low 7 are the word). */
static void command(gb_t *g, unsigned opcode, unsigned address) {
    chip_select(g, 0);
    chip_select(g, 1);
    send(g, 1, 1);
    send(g, opcode, 2);
    send(g, address, 8);
}

static unsigned receive16(gb_t *g) {
    unsigned v = 0;
    for (int i = 0; i < 16; i++) v = v << 1 | (unsigned)clock_bit(g, 0);
    return v;
}

static void write_enable(gb_t *g) {
    command(g, 0, 0xC0);
    chip_select(g, 0);
}

static void write_word(gb_t *g, unsigned address, unsigned value) {
    command(g, 1, address);
    send(g, value, 16);
    chip_select(g, 0);
}

static unsigned read_word(gb_t *g, unsigned address) {
    command(g, 2, address);
    unsigned v = receive16(g);
    chip_select(g, 0);
    return v;
}

TEST(gb_mbc7_loads_and_other_chips_are_still_refused) {
    gb_t *g = tilt_cart();
    CHECK_EQ(gb_has_tilt(g), 1);
    CHECK_EQ(gb_has_rumble(g), 0);
    CHECK_EQ(gb_save_size(g), 256);
    gb_destroy(g);

    uint8_t *rom = gbtu_rom(0x8000, 0x20, 0, 0); /* MBC6: still not a chip this core has */
    g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_ERR_UNSUPPORTED_CART);
    free(rom);
    rom = gbtu_rom(0x8000, 0x1B, 3, 0); /* and another cartridge has no tilt sensor */
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    CHECK_EQ(gb_has_tilt(g), 0);
    gb_set_tilt(g, 1000, 1000); /* harmless */
    free(rom);
    gb_destroy(g);
}

TEST(gb_mbc7_both_switches_guard_the_registers) {
    gb_t *g = tilt_cart();
    CHECK_EQ(gb_bus_peek(g, 0xA020), 0xFF); /* both off */
    gb_bus_write(g, 0x0000, 0x0A);
    CHECK_EQ(gb_bus_peek(g, 0xA020), 0xFF); /* only the first */
    latch(g);                               /* ignored */
    gb_bus_write(g, 0x4000, 0x40);
    CHECK_EQ(word_at(g, 0xA020), 0x8000); /* open: the reading nothing has latched yet */
    CHECK_EQ(gb_bus_peek(g, 0xB000), 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0xBFFF), 0xFF);
    gb_bus_write(g, 0x0000, 0x00);
    CHECK_EQ(gb_bus_peek(g, 0xA020), 0xFF); /* only the second */
    gb_bus_write(g, 0x0000, 0x0A);
    gb_bus_write(g, 0x4000, 0x41); /* only 0x40 opens it */
    CHECK_EQ(gb_bus_peek(g, 0xA020), 0xFF);
    gb_destroy(g);
}

TEST(gb_mbc7_rom_bank) {
    gb_t *g = tilt_cart();
    CHECK_EQ(gb_bus_peek(g, 0x4000), 1);
    gb_bus_write(g, 0x2000, 5);
    CHECK_EQ(gb_bus_peek(g, 0x4000), 5);
    gb_bus_write(g, 0x3FFF, 0x86); /* 7 bits */
    CHECK_EQ(gb_bus_peek(g, 0x4000), 6);
    gb_bus_write(g, 0x2000, 0x0B); /* wraps to the ROM's 8 banks */
    CHECK_EQ(gb_bus_peek(g, 0x4000), 3);
    gb_destroy(g);
}

TEST(gb_mbc7_latch_needs_its_order) {
    gb_t *g = tilt_cart();
    open_registers(g);
    gb_bus_write(g, 0xA010, 0xAA); /* a latch without the erase before it: nothing */
    CHECK_EQ(word_at(g, 0xA020), 0x8000);
    gb_bus_write(g, 0xA000, 0x55);
    gb_bus_write(g, 0xA010, 0xAA);
    CHECK_EQ(word_at(g, 0xA020), LEVEL);
    CHECK_EQ(word_at(g, 0xA040), LEVEL);
    gb_set_tilt(g, 32767, 0);
    gb_bus_write(g, 0xA010, 0xAA); /* a second latch without an erase changes nothing */
    CHECK_EQ(word_at(g, 0xA020), LEVEL);
    gb_bus_write(g, 0xA000, 0x54); /* only 0x55 erases */
    CHECK_EQ(word_at(g, 0xA020), LEVEL);
    gb_bus_write(g, 0xA000, 0x55);
    CHECK_EQ(word_at(g, 0xA020), 0x8000); /* erased */
    CHECK_EQ(word_at(g, 0xA040), 0x8000);
    gb_bus_write(g, 0xA010, 0xAB); /* only 0xAA latches */
    CHECK_EQ(word_at(g, 0xA020), 0x8000);
    gb_bus_write(g, 0xA015, 0xAA); /* the address's low 4 bits do not matter */
    CHECK_EQ(word_at(g, 0xA020), LEVEL - ONE_G);
    gb_destroy(g);
}

TEST(gb_mbc7_reading_level_and_one_g) {
    static const struct {
        int16_t x, y;
        unsigned rx, ry;
    } cases[] = {
        {0, 0, LEVEL, LEVEL},
        {32767, 0, LEVEL - ONE_G, LEVEL},  /* right edge down: X goes down */
        {-32767, 0, LEVEL + ONE_G, LEVEL},
        {0, 32767, LEVEL, LEVEL - ONE_G},  /* bottom edge down: Y goes down */
        {0, -32767, LEVEL, LEVEL + ONE_G},
        {16384, -16384, LEVEL - ONE_G / 2, LEVEL + ONE_G / 2},
    };
    gb_t *g = tilt_cart();
    open_registers(g);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        gb_set_tilt(g, cases[i].x, cases[i].y);
        latch(g);
        CHECK_EQ(word_at(g, 0xA020), cases[i].rx);
        CHECK_EQ(word_at(g, 0xA040), cases[i].ry);
    }
    gb_destroy(g);
}

TEST(gb_mbc7_fixed_registers) {
    gb_t *g = tilt_cart();
    open_registers(g);
    CHECK_EQ(gb_bus_peek(g, 0xA000), 0xFF); /* the latch registers are write-only */
    CHECK_EQ(gb_bus_peek(g, 0xA010), 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0xA060), 0x00);
    CHECK_EQ(gb_bus_peek(g, 0xA070), 0xFF);
    for (uint16_t r = 0xA090; r <= 0xA0F0; r += 0x10) CHECK_EQ(gb_bus_peek(g, r), 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0xAF6F), 0x00); /* only bits 4-7 of the address pick the register */
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_write_needs_enable) {
    gb_t *g = tilt_cart();
    open_registers(g);
    CHECK_EQ(read_word(g, 5), 0xFFFF); /* a fresh chip is erased */
    write_word(g, 5, 0x1234);          /* not enabled: ignored */
    CHECK_EQ(read_word(g, 5), 0xFFFF);
    CHECK_EQ(gb_save_dirty(g), 0);
    write_enable(g);
    write_word(g, 5, 0x1234);
    CHECK_EQ(read_word(g, 5), 0x1234);
    CHECK_EQ(read_word(g, 4), 0xFFFF);
    CHECK_EQ(read_word(g, 6), 0xFFFF);
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_write_then_read) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 0, 0xA55A);
    write_word(g, 127, 0x0F0E);
    write_word(g, 0x80 | 3, 0xBEEF); /* the address's top bit is not used: word 3 */
    CHECK_EQ(read_word(g, 0), 0xA55A);
    CHECK_EQ(read_word(g, 127), 0x0F0E);
    CHECK_EQ(read_word(g, 3), 0xBEEF);
    command(g, 2, 0); /* a read gives a 0 with its last address bit, then the word, high bit first */
    CHECK_EQ(gb_bus_peek(g, 0xA080) & 1, 0);
    CHECK_EQ(clock_bit(g, 0), 1);
    chip_select(g, 0);
    CHECK_EQ(gb_bus_peek(g, 0xA080) & 1, 1); /* idle: ready */
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_read_runs_on_into_the_next_word) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 126, 0x1111);
    write_word(g, 127, 0x2222);
    write_word(g, 0, 0x3333);
    command(g, 2, 126);
    CHECK_EQ(receive16(g), 0x1111);
    CHECK_EQ(receive16(g), 0x2222);
    CHECK_EQ(receive16(g), 0x3333); /* and round to word 0 */
    chip_select(g, 0);
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_erase) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 9, 0x0000);
    write_word(g, 10, 0x0000);
    command(g, 3, 9);
    chip_select(g, 0);
    CHECK_EQ(read_word(g, 9), 0xFFFF);
    CHECK_EQ(read_word(g, 10), 0x0000);
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_write_all_and_erase_all) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    command(g, 0, 0x40); /* write-all */
    send(g, 0x4321, 16);
    chip_select(g, 0);
    CHECK_EQ(read_word(g, 0), 0x4321);
    CHECK_EQ(read_word(g, 77), 0x4321);
    CHECK_EQ(read_word(g, 127), 0x4321);
    command(g, 0, 0x80); /* erase-all */
    chip_select(g, 0);
    CHECK_EQ(read_word(g, 0), 0xFFFF);
    CHECK_EQ(read_word(g, 127), 0xFFFF);
    gb_destroy(g);
}

TEST(gb_mbc7_eeprom_write_disable) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 1, 0x1111);
    command(g, 0, 0x00); /* write disable */
    chip_select(g, 0);
    write_word(g, 1, 0x2222);
    command(g, 3, 1);
    chip_select(g, 0);
    command(g, 0, 0x40);
    send(g, 0x3333, 16);
    chip_select(g, 0);
    command(g, 0, 0x80);
    chip_select(g, 0);
    CHECK_EQ(read_word(g, 1), 0x1111);
    gb_destroy(g);
}

/* Review focus: pins hammered with the chip not selected, and a command cut short. */
TEST(gb_mbc7_eeprom_ignores_noise) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 2, 0x5555);
    gb_save_dirty(g);
    for (int i = 0; i < 100; i++) { /* clocks with chip select low */
        gb_bus_write(g, 0xA080, (uint8_t)((i & 1) ? CLK | DI : DI));
        gb_bus_write(g, 0xA080, (uint8_t)((i & 2) ? CLK : 0));
    }
    command(g, 1, 2); /* a write cut short after 7 of its 16 bits */
    send(g, 0x00, 7);
    chip_select(g, 0);
    chip_select(g, 1); /* selected, no start bit: zeros are not a command */
    for (int i = 0; i < 40; i++) clock_bit(g, 0);
    chip_select(g, 0);
    CHECK_EQ(read_word(g, 2), 0x5555);
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_bus_write(g, 0x4000, 0x00); /* the registers closed: the pins do not reach the chip */
    write_word(g, 2, 0x0000);
    gb_bus_write(g, 0x4000, 0x40);
    CHECK_EQ(read_word(g, 2), 0x5555);
    gb_destroy(g);
}

TEST(gb_mbc7_save_round_trip_and_dirty) {
    gb_t *g = tilt_cart();
    open_registers(g);
    CHECK_EQ(gb_save_dirty(g), 0);
    write_enable(g);
    write_word(g, 1, 0x1234);
    CHECK_EQ(gb_save_dirty(g), 1);
    CHECK_EQ(gb_save_dirty(g), 0);
    uint8_t file[256];
    CHECK_EQ(gb_save_write(g, file, 255), 0);
    CHECK_EQ(gb_save_write(g, file, sizeof file), 256);
    CHECK_EQ(file[2], 0x34); /* word n is bytes 2n (low) and 2n + 1 (high) */
    CHECK_EQ(file[3], 0x12);
    CHECK_EQ(file[0], 0xFF);
    gb_destroy(g);

    g = tilt_cart();
    gb_save_read(g, file, sizeof file);
    open_registers(g);
    CHECK_EQ(read_word(g, 1), 0x1234);
    CHECK_EQ(read_word(g, 0), 0xFFFF);
    gb_destroy(g);
}

/* Review focus: a save file of the wrong length. */
TEST(gb_mbc7_save_of_any_length_loads) {
    uint8_t big[512];
    memset(big, 0x11, sizeof big);
    gb_t *g = tilt_cart();
    open_registers(g);
    gb_save_read(g, big, sizeof big); /* what fits */
    CHECK_EQ(read_word(g, 127), 0x1111);
    gb_save_read(g, big, 3); /* the rest is 0xFF */
    CHECK_EQ(read_word(g, 0), 0x1111);
    CHECK_EQ(read_word(g, 1), 0xFF11);
    CHECK_EQ(read_word(g, 127), 0xFFFF);
    gb_save_read(g, NULL, 0);
    CHECK_EQ(read_word(g, 0), 0xFFFF);
    gb_destroy(g);
}

/* Review focus: a state taken in the middle of a command. */
TEST(gb_mbc7_state_restores_a_command_in_flight) {
    gb_t *a = tilt_cart(), *b = tilt_cart();
    open_registers(a);
    gb_set_tilt(a, 32767, 0);
    latch(a);
    write_enable(a);
    command(a, 1, 20);
    send(a, 0xAB, 8); /* half of the word */
    size_t size = gb_state_size(a);
    CHECK_EQ(size, gb_state_size(b));
    uint8_t *state = malloc(size), *again = malloc(size);
    CHECK_EQ(gb_state_save(a, state, size), size);
    CHECK_EQ(gb_state_load(b, state, size), GB_OK); /* into a machine that never ran */
    CHECK_EQ(gb_state_save(b, again, size), size);
    CHECK(!memcmp(state, again, size));
    CHECK_EQ(word_at(b, 0xA020), LEVEL - ONE_G); /* the kept reading and the switches came along */
    send(b, 0xCD, 8);
    chip_select(b, 0);
    CHECK_EQ(read_word(b, 20), 0xABCD);
    free(state);
    free(again);
    gb_destroy(a);
    gb_destroy(b);
}

TEST(gb_mbc7_rewind_restores_the_chip) {
    gb_t *g = tilt_cart();
    CHECK_EQ(gb_rewind_configure(g, 4 * 1024 * 1024, 1), GB_OK);
    open_registers(g);
    write_enable(g);
    write_word(g, 3, 0x1111);
    for (int i = 0; i < 3; i++) gb_run_frame(g);
    write_word(g, 3, 0x2222);
    gb_set_tilt(g, 0, 32767);
    latch(g);
    gb_run_frame(g);
    CHECK_EQ(read_word(g, 3), 0x2222);
    CHECK_EQ(gb_rewind_step_back(g), 1); /* to the end of the last frame */
    CHECK_EQ(gb_rewind_step_back(g), 1); /* and the one before it */
    CHECK_EQ(read_word(g, 3), 0x1111);
    CHECK_EQ(word_at(g, 0xA040), 0x8000); /* the reading as it was then: none yet */
    gb_destroy(g);
}

TEST(gb_mbc7_tilt_input_is_not_in_a_state) {
    gb_t *a = tilt_cart(), *b = tilt_cart();
    gb_set_tilt(a, 1234, -4321);
    size_t size = gb_state_size(a);
    uint8_t *sa = malloc(size), *sb = malloc(size);
    gb_state_save(a, sa, size);
    gb_state_save(b, sb, size);
    CHECK(!memcmp(sa, sb, size));
    gb_set_tilt(b, 32767, 0);
    CHECK_EQ(gb_state_load(b, sa, size), GB_OK); /* and loading leaves the phone's tilt alone */
    open_registers(b);
    latch(b);
    CHECK_EQ(word_at(b, 0xA020), LEVEL - ONE_G);
    free(sa);
    free(sb);
    gb_destroy(a);
    gb_destroy(b);
}

TEST(gb_mbc7_ra_view_ignores_the_switches) {
    gb_t *g = tilt_cart();
    open_registers(g);
    gb_set_tilt(g, -32767, 0);
    latch(g);
    gb_bus_write(g, 0x0000, 0x00); /* both closed again */
    gb_bus_write(g, 0x4000, 0x00);
    size_t size = gb_state_size(g);
    uint8_t *before = malloc(size), *after = malloc(size);
    gb_state_save(g, before, size);
    uint8_t v[2];
    CHECK_EQ(gb_ra_peek(g, 0xA020, v, 1), 1);
    CHECK_EQ(v[0], (LEVEL + ONE_G) & 0xFF);
    CHECK_EQ(gb_ra_peek(g, 0xA030, v, 1), 1);
    CHECK_EQ(v[0], (LEVEL + ONE_G) >> 8);
    CHECK_EQ(gb_ra_peek(g, 0xAFFF, v, 2), 2); /* across into 0xB000 */
    CHECK_EQ(v[1], 0xFF);
    CHECK_EQ(gb_ra_peek(g, 0xBFFF, v, 2), 2); /* and on into work RAM */
    CHECK_EQ(gb_ra_peek(g, 0x16000, v, 1), 0); /* no further cartridge RAM banks */
    gb_state_save(g, after, size);
    CHECK(!memcmp(before, after, size));
    free(before);
    free(after);
    gb_destroy(g);
}

TEST(gb_mbc7_gameshark_to_its_registers_is_skipped) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 0, 0x1234);
    gb_save_dirty(g);
    const char *text = "010000A0";
    gb_cheat_type_t type = GB_CHEAT_GAMESHARK;
    CHECK_EQ(gb_cheats_set(g, &text, &type, 1), GB_OK);
    gb_run_frame(g);
    CHECK_EQ(read_word(g, 0), 0x1234);
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_destroy(g);
}

/* A reset is not a new cartridge: the save chip keeps its bytes, the switches close. */
TEST(gb_mbc7_a_reset_keeps_the_save_and_closes_the_switches) {
    gb_t *g = tilt_cart();
    open_registers(g);
    write_enable(g);
    write_word(g, 0, 0x1234);
    latch(g);
    gb_reset(g);
    CHECK_EQ(gb_bus_peek(g, 0xA020), 0xFF);
    open_registers(g);
    CHECK_EQ(word_at(g, 0xA020), 0x8000);
    CHECK_EQ(read_word(g, 0), 0x1234);
    write_word(g, 0, 0x0000); /* and writing is locked again */
    CHECK_EQ(read_word(g, 0), 0x1234);
    gb_destroy(g);
}

/* A state may come from a file someone else made: a word number or bit count outside the chip is refused. */
TEST(gb_mbc7_state_with_wild_values_is_refused) {
    gb_t *g = tilt_cart();
    size_t size = gb_state_size(g);
    uint8_t *state = malloc(size), *before = malloc(size);
    gb_state_save(g, before, size);
    static const struct {
        size_t at;
        uint8_t value;
    } wild[] = {{offsetof(gb_mbc7_t, address), 0x80}, {offsetof(gb_mbc7_t, count), 16},
                {offsetof(gb_mbc7_t, phase), 5},      {offsetof(gb_mbc7_t, dout), 2}};
    for (size_t i = 0; i < sizeof wild / sizeof wild[0]; i++) {
        memcpy(state, before, size);
        state[size - sizeof(gb_mbc7_t) + wild[i].at] = wild[i].value; /* the chip's chunk is the last */
        CHECK_EQ(gb_state_load(g, state, size), GB_ERR_STATE);
    }
    gb_state_save(g, state, size);
    CHECK(!memcmp(state, before, size)); /* and a refused state changed nothing */
    CHECK_EQ(gb_state_load(g, before, size), GB_OK);
    free(state);
    free(before);
    gb_destroy(g);
}
