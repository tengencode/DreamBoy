/* Save states (core/src/state.c): a loaded state is the same machine; a bad state changes nothing. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gba_debug.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

/* A small program that keeps changing the machine: counts in r0, stores it to EWRAM, loops. */
static const uint32_t counter_ops[] = {
    0xE3A01402, /* mov r1, #0x02000000 */
    0xE2800001, /* loop: add r0, r0, #1 */
    0xE5810000, /* str r0, [r1] */
    0xEAFFFFFC, /* b loop */
};

static void run_frames(gba_t *g, int n) {
    for (int i = 0; i < n; i++) gba_run_frame(g);
}

static uint8_t *state_of(const gba_t *g, size_t *size) {
    *size = gba_state_size(g);
    uint8_t *buf = malloc(*size);
    CHECK_EQ(gba_state_save(g, buf, *size), *size);
    return buf;
}

TEST(state_size_is_what_save_writes_and_a_small_buffer_gets_nothing) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    size_t size = gba_state_size(g);
    CHECK(size > 400 * 1024); /* the memories alone are ~400 KiB */
    uint8_t *buf = malloc(size);
    CHECK_EQ(gba_state_save(g, buf, size - 1), 0);
    CHECK_EQ(gba_state_save(g, buf, size), size);
    CHECK(memcmp(buf, "GBAS", 4) == 0);
    free(buf);
    gba_destroy(g);
}

TEST(state_loaded_into_a_new_machine_is_the_same_and_runs_on_the_same) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(a, 3);
    size_t size;
    uint8_t *sa = state_of(a, &size);
    gba_t *b = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_state_load(b, sa, size), GBA_OK);
    size_t size_b;
    uint8_t *sb = state_of(b, &size_b);
    CHECK_EQ(size_b, size);
    CHECK(memcmp(sa, sb, size) == 0);
    run_frames(a, 5);
    run_frames(b, 5);
    free(sa);
    free(sb);
    sa = state_of(a, &size);
    sb = state_of(b, &size_b);
    CHECK(memcmp(sa, sb, size) == 0); /* still the same machine five frames later */
    CHECK(gba_debug_reg(b, 0) > 0);
    free(sa);
    free(sb);
    gba_destroy(a);
    gba_destroy(b);
}

/* Loading `bad` must fail and leave b exactly as it was. */
static void check_refused(gba_t *b, const uint8_t *bad, size_t len) {
    size_t size;
    uint8_t *before = state_of(b, &size);
    CHECK_EQ(gba_state_load(b, bad, len), GBA_ERR_STATE);
    uint8_t *after = state_of(b, &size);
    CHECK(memcmp(before, after, size) == 0);
    free(before);
    free(after);
}

TEST(state_bad_states_are_refused_and_change_nothing) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(a, 2);
    size_t size;
    uint8_t *good = state_of(a, &size);
    uint8_t *bad = malloc(size);
    gba_t *b = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(b, 7); /* a different moment, so a wrong load would show */

    memcpy(bad, good, size);
    bad[0] = 'X'; /* magic */
    check_refused(b, bad, size);
    memcpy(bad, good, size);
    bad[4] ^= 1; /* format version */
    check_refused(b, bad, size);
    memcpy(bad, good, size);
    bad[8] ^= 1; /* the ROM's CRC-32 */
    check_refused(b, bad, size);
    memcpy(bad, good, size);
    bad[16 + 4] ^= 1; /* the first chunk's version */
    check_refused(b, bad, size);
    memcpy(bad, good, size);
    bad[16 + 8] ^= 1; /* the first chunk's length */
    check_refused(b, bad, size);
    memcpy(bad, good, size);
    bad[16] = 'Z'; /* an unknown tag */
    check_refused(b, bad, size);
    check_refused(b, good, size - 1); /* cut short */
    CHECK_EQ(gba_state_load(b, NULL, size), GBA_ERR_STATE);

    CHECK_EQ(gba_state_load(b, good, size), GBA_OK); /* and the good one still loads */
    free(good);
    free(bad);
    gba_destroy(a);
    gba_destroy(b);
}

TEST(state_of_another_rom_is_refused) {
    static const uint32_t other_ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    gba_t *b = tu_arm(other_ops, TU_LEN(other_ops));
    size_t size;
    uint8_t *sa = state_of(a, &size);
    check_refused(b, sa, size);
    free(sa);
    gba_destroy(a);
    gba_destroy(b);
}

TEST(state_rom_crc32_is_zlibs_over_the_bytes_given) {
    uint8_t rom[193]; /* not a multiple of 4: the core pads, the CRC must not include the padding */
    for (size_t i = 0; i < sizeof rom; i++) rom[i] = (uint8_t)(i * 7 + 3);
    gba_t *g = gba_create(NULL);
    CHECK_EQ(gba_load_rom(g, rom, sizeof rom), GBA_OK);
    CHECK_EQ(gba_rom_crc32(g), 0x71AD99BAu); /* python: zlib.crc32(bytes((i*7+3)&255 for i in range(193))) */
    gba_destroy(g);
}

/* The same ROM with the built-in BIOS, or with a BIOS file (here 16 KiB of "b ." loops). */
static gba_t *machine(bool bios_file) {
    static const uint8_t loop[4] = {0xFE, 0xFF, 0xFF, 0xEA}; /* b . */
    uint8_t *rom = calloc(1, 0x400);
    memcpy(rom, loop, 4);
    gba_t *g = gba_create(NULL);
    if (bios_file) {
        uint8_t *bios = malloc(16384);
        for (size_t i = 0; i < 16384; i += 4) memcpy(bios + i, loop, 4);
        CHECK_EQ(gba_load_bios(g, bios, 16384), GBA_OK);
        free(bios);
    }
    CHECK_EQ(gba_load_rom(g, rom, 0x400), GBA_OK);
    free(rom);
    return g;
}

TEST(state_of_the_other_bios_kind_is_refused) {
    gba_t *builtin = machine(false), *file = machine(true);
    run_frames(builtin, 2);
    run_frames(file, 2);
    size_t size;
    uint8_t *s_builtin = state_of(builtin, &size);
    uint8_t *s_file = state_of(file, &size);
    check_refused(file, s_builtin, size); /* SWIs would run the wrong BIOS */
    check_refused(builtin, s_file, size);
    CHECK_EQ(gba_state_load(builtin, s_builtin, size), GBA_OK); /* its own kind still loads */
    CHECK_EQ(gba_state_load(file, s_file, size), GBA_OK);
    free(s_builtin);
    free(s_file);
    gba_destroy(builtin);
    gba_destroy(file);
}

/* A chunk's bytes inside a state (found by its tag), for the tests that damage one field. */
static uint8_t *chunk_of(uint8_t *state, const char tag[4]) {
    uint8_t *q = state + 16;
    for (uint32_t i = 0, n = (uint32_t)state[12]; i < n; i++) {
        uint32_t len;
        memcpy(&len, q + 8, 4);
        if (memcmp(q, tag, 4) == 0) return q + 12;
        q += 12 + len;
    }
    return NULL;
}

/* Loading the good state with one field set to `value` must be refused and change nothing. */
#define POKE(tag, st, member, value)                                                    do {                                                                                    memcpy(bad, good, size);                                                            __typeof__(((st *)0)->member) v_ = (value);                                         memcpy(chunk_of(bad, tag) + offsetof(st, member), &v_, sizeof v_);                  check_refused(b, bad, size);                                                    } while (0)

/* A state from a shared file or someone else's backup: values that point outside the machine's memory,
 * or that would stop the frame from ever ending, are refused (and change nothing). */
TEST(state_with_values_out_of_range_is_refused) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(a, 2);
    size_t size;
    uint8_t *good = state_of(a, &size);
    uint8_t *bad = malloc(size);
    gba_t *b = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(b, 7);
    uint64_t now = a->now;

    POKE("CART", cart_t, save.flash.bank, 2); /* 64 KiB banks: 0 or 1 */
    POKE("CART", cart_t, save.type, GBA_SAVE_EEPROM8K + 1);
    POKE("CART", cart_t, save.eeprom.reply_pos, 69); /* 0..68 */
    POKE("CART", cart_t, hw.rtc.bit_pos, 8);           /* 0..7 */
    POKE("APU ", apu_t, psg.ch[0].pos, 8);             /* a square wave's step: 0..7 */
    POKE("APU ", apu_t, psg.ch[1].pos, 8);
    POKE("APU ", apu_t, psg.ch[2].pos, 64);            /* the wave channel's sample: 0..63 */
    POKE("APU ", apu_t, fifo[0].read, 32);
    POKE("APU ", apu_t, fifo[1].count, 33);
    POKE("APU ", apu_t, ring_read, APU_RING_FRAMES);
    POKE("APU ", apu_t, ring_count, APU_RING_FRAMES + 1);
    POKE("SCHD", sched_t, when[EV_LINE_END], SCHED_NEVER); /* the frame would never end */
    POKE("SCHD", sched_t, when[EV_LINE_END], now + 2 * PPU_FRAME_CYCLES);
    POKE("SCHD", sched_t, when[EV_TIMER0], 1); /* far in the past: endless catching up */

    CHECK_EQ(gba_state_load(b, good, size), GBA_OK);
    free(good);
    free(bad);
    gba_destroy(a);
    gba_destroy(b);
}

/* The ROM in the machine is the truth: a state can't make it bigger than it is. */
TEST(state_keeps_the_loaded_roms_size) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    size_t size;
    uint8_t *s = state_of(a, &size);
    uint32_t rom_size = a->cart.rom_size, huge = 0x02000000u;
    memcpy(chunk_of(s, "CART") + offsetof(cart_t, rom_size), &huge, 4);
    CHECK_EQ(gba_state_load(a, s, size), GBA_OK);
    CHECK_EQ(a->cart.rom_size, rom_size);
    free(s);
    gba_destroy(a);
}

/* A state whose scheduler cache disagrees with its events still runs (the cache is rebuilt). */
TEST(state_rebuilds_the_schedulers_next_event) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    run_frames(a, 1);
    size_t size;
    uint8_t *s = state_of(a, &size);
    uint64_t zero = 0;
    memcpy(chunk_of(s, "SCHD") + offsetof(sched_t, next), &zero, 8);
    CHECK_EQ(gba_state_load(a, s, size), GBA_OK);
    uint64_t min = SCHED_NEVER;
    for (int i = 0; i < EV_COUNT; i++)
        if (a->sched.when[i] < min) min = a->sched.when[i];
    CHECK_EQ(a->sched.next, min);
    free(s);
    gba_destroy(a);
}

/* The memory timing table follows WAITCNT: a state can't set its own (all zeros: time would stand still). */
TEST(state_rebuilds_the_memory_timing_from_waitcnt) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    size_t size;
    uint8_t *s = state_of(a, &size);
    memset(chunk_of(s, "BUS ") + offsetof(bus_t, cycles), 0, sizeof a->bus.cycles);
    uint8_t cycles[sizeof a->bus.cycles];
    memcpy(cycles, a->bus.cycles, sizeof cycles);
    CHECK_EQ(gba_state_load(a, s, size), GBA_OK);
    CHECK(memcmp(a->bus.cycles, cycles, sizeof cycles) == 0);
    free(s);
    gba_destroy(a);
}

/* The ROM's title and game code are the loaded ROM's, always ending in a NUL. */
TEST(state_keeps_the_loaded_roms_title_and_code) {
    gba_t *a = tu_arm(counter_ops, TU_LEN(counter_ops));
    size_t size;
    uint8_t *s = state_of(a, &size);
    memset(chunk_of(s, "CART") + offsetof(cart_t, title), 'A', sizeof a->cart.title + sizeof a->cart.code);
    char title[sizeof a->cart.title], code[sizeof a->cart.code];
    memcpy(title, a->cart.title, sizeof title);
    memcpy(code, a->cart.code, sizeof code);
    CHECK_EQ(gba_state_load(a, s, size), GBA_OK);
    CHECK(memcmp(a->cart.title, title, sizeof title) == 0);
    CHECK(memcmp(a->cart.code, code, sizeof code) == 0);
    free(s);
    gba_destroy(a);
}
