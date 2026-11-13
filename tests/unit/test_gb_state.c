/* Game Boy save states and rewind. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gba.h"
#include "gbtestutil.h"
#include "machine.h"
#include "test.h"

/* A program that keeps the whole machine busy: it scrolls, plays a note, counts in cartridge RAM and work RAM. */
static const uint8_t BUSY[] = {
    0x3E, 0x0A, 0xEA, 0x00, 0x00, /* LD A,0x0A; LD (0x0000),A: cartridge RAM on */
    0x3E, 0x80, 0xE0, 0x26,       /* sound on */
    0x3E, 0xFF, 0xE0, 0x25, 0xE0, 0x24,
    0x3E, 0xF3, 0xE0, 0x17,       /* NR22 */
    0x3E, 0x87, 0xE0, 0x19,       /* NR24: trigger */
    0x3E, 0x05, 0xE0, 0x07,       /* timer on */
    0x21, 0x00, 0xC0,             /* LD HL,0xC000 */
    0x34,                         /* loop: INC (HL) */
    0x7E, 0xE0, 0x43,             /* LD A,(HL); LDH (SCX),A */
    0xEA, 0x00, 0xA0,             /* LD (0xA000),A */
    0xE0, 0x18,                   /* LDH (NR23),A */
    0x2C,                         /* INC L */
    0x18, 0xF4,                   /* JR loop */
};

static gb_t *busy(uint8_t cgb_flag) {
    uint8_t *rom = gbtu_rom(0x8000, 0x13, 2, cgb_flag); /* MBC3 + RAM + battery */
    memcpy(rom + 0x150, BUSY, sizeof BUSY);
    for (int i = 0x1000; i < 0x4000; i++) rom[i] = (uint8_t)(i * 31 >> 3); /* something to show as tiles */
    gb_t *g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    for (int i = 0; i < 0x1800; i++) g->ppu.vram[i] = (uint8_t)(i * 7);
    for (int i = 0; i < 0x400; i++) g->ppu.vram[0x1800 + i] = (uint8_t)i;
    return g;
}

static uint64_t hash(uint64_t h, const void *data, size_t n) {
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* Runs n frames; a fingerprint of every picture and every sample. */
static uint64_t run(gb_t *g, int n) {
    uint64_t h = 0xCBF29CE484222325ull;
    int16_t sound[1024 * 2];
    for (int f = 0; f < n; f++) {
        gb_run_frame(g);
        h = hash(h, gb_framebuffer(g), GB_WIDTH * GB_HEIGHT * 2);
        size_t got;
        while ((got = gb_audio_read(g, sound, 1024)) > 0) h = hash(h, sound, got * 4);
    }
    return h;
}

TEST(gb_state_round_trip) {
    for (int color = 0; color < 2; color++) {
        gb_t *a = busy(color ? 0x80 : 0), *b = busy(color ? 0x80 : 0);
        run(a, 37);
        size_t size = gb_state_size(a);
        CHECK(size > 0x8000 && size == gb_state_size(b));
        uint8_t *state = malloc(size), *again = malloc(size);
        CHECK_EQ(gb_state_save(a, state, size - 1), 0);
        CHECK_EQ(gb_state_save(a, state, size), size);
        CHECK_EQ(gb_state_load(b, state, size), GB_OK); /* into a machine that never ran */
        CHECK_EQ(gb_state_save(b, again, size), size);
        CHECK(!memcmp(state, again, size));
        CHECK_EQ(run(a, 30), run(b, 30)); /* and both go on exactly alike */
        CHECK_EQ(gb_bus_peek(a, 0xA000), gb_bus_peek(b, 0xA000));
        free(state);
        free(again);
        gb_destroy(a);
        gb_destroy(b);
    }
}

/* Review focus: anything that is not this build's state of this ROM is refused and changes nothing. */
TEST(gb_state_refuses) {
    gb_t *a = busy(0), *twin = busy(0);
    run(a, 20);
    run(twin, 20);
    size_t size = gb_state_size(a);
    uint8_t *state = malloc(size), *bad = malloc(size + 16);
    gb_state_save(a, state, size);
    run(a, 5);
    run(twin, 5);

    memcpy(bad, state, size);
    bad[0] ^= 1; /* the magic */
    CHECK_EQ(gb_state_load(a, bad, size), GB_ERR_STATE);
    memcpy(bad, state, size);
    bad[8] ^= 1; /* another ROM's CRC */
    CHECK_EQ(gb_state_load(a, bad, size), GB_ERR_STATE);
    memcpy(bad, state, size);
    bad[4] ^= 1; /* another format version */
    CHECK_EQ(gb_state_load(a, bad, size), GB_ERR_STATE);
    memcpy(bad, state, size);
    bad[16] ^= 1; /* the first chunk's tag */
    CHECK_EQ(gb_state_load(a, bad, size), GB_ERR_STATE);
    memcpy(bad, state, size);
    bad[24] ^= 1; /* the first chunk's length */
    CHECK_EQ(gb_state_load(a, bad, size), GB_ERR_STATE);
    CHECK_EQ(gb_state_load(a, state, size - 1), GB_ERR_STATE);
    CHECK_EQ(gb_state_load(a, bad, size + 16), GB_ERR_STATE);
    CHECK_EQ(gb_state_load(a, state, 0), GB_ERR_STATE);
    CHECK_EQ(gb_state_load(a, NULL, size), GB_ERR_STATE);
    for (size_t cut = 0; cut < size; cut += 997) CHECK_EQ(gb_state_load(a, state, cut), GB_ERR_STATE);
    CHECK_EQ(run(a, 10), run(twin, 10)); /* untouched by all of that */

    gb_t *other = busy(0x80); /* the same program as a Game Boy Color game: another ROM */
    CHECK_EQ(gb_state_load(other, state, size), GB_ERR_STATE);
    gb_destroy(other);
    free(state);
    free(bad);
    gb_destroy(a);
    gb_destroy(twin);
}

/* A state file may come from anywhere: values that pick a place in memory or keep a loop going are checked.
 * Every byte of the state is set to 0xFF, a chunk at a time; whatever loads must then run clean (the sanitizer
 * watches) and still finish its frames. */
TEST(gb_state_bad_values_safe) {
    gb_t *g = busy(0x80);
    run(g, 20);
    size_t size = gb_state_size(g);
    uint8_t *state = malloc(size), *bad = malloc(size);
    gb_state_save(g, state, size);
    size_t at = 16;
    int loaded = 0, chunks = 0;
    while (at < size) {
        uint32_t len;
        memcpy(&len, state + at + 8, 4);
        for (int fill = 0; fill < 2; fill++) {
            memcpy(bad, state, size);
            memset(bad + at + 12, fill ? 0xFF : 0x00, len);
            if (gb_state_load(g, bad, size) == GB_OK) {
                loaded++;
                uint64_t t = gb_debug_ticks(g);
                run(g, 3);
                CHECK(gb_debug_ticks(g) - t <= 3u * GB_FRAME_TICKS + 2000);
            }
            CHECK_EQ(gb_state_load(g, state, size), GB_OK);
        }
        at += 12 + len;
        chunks++;
    }
    CHECK_EQ(at, size);
    CHECK(chunks >= 10);
    CHECK(loaded > 0);
    /* The sound channels' timers and the mixer's sums are signed numbers that are added to: the extremes of an
     * int32 must be refused (they would overflow, or keep a catch-up loop busy for seconds). */
    size_t apu_at = 16;
    for (;;) {
        uint32_t len;
        memcpy(&len, state + apu_at + 8, 4);
        if (!memcmp(state + apu_at, "APU ", 4)) break;
        apu_at += 12 + len;
    }
    static const size_t fields[] = {offsetof(gb_apu_t, ch[0].timer), offsetof(gb_apu_t, ch[2].timer),
                                    offsetof(gb_apu_t, ch[3].timer), offsetof(gb_apu_t, sum_left),
                                    offsetof(gb_apu_t, sum_right), offsetof(gb_apu_t, cap_left),
                                    offsetof(gb_apu_t, cap_right)};
    static const int32_t extremes[] = {INT32_MIN, INT32_MAX, -(1 << 30), 1 << 30};
    for (size_t f = 0; f < sizeof fields / sizeof fields[0]; f++) {
        for (size_t e = 0; e < 4; e++) {
            memcpy(bad, state, size);
            memcpy(bad + apu_at + 12 + fields[f], &extremes[e], 4);
            CHECK_EQ(gb_state_load(g, bad, size), GB_ERR_STATE);
        }
    }
    CHECK_EQ(gb_state_load(g, state, size), GB_OK);
    run(g, 2);
    free(state);
    free(bad);
    gb_destroy(g);
}

TEST(gb_rewind_steps_back) {
    gb_t *g = busy(0), *twin = busy(0);
    CHECK_EQ(gb_rewind_step_back(g), 0); /* off */
    CHECK_EQ(gb_rewind_configure(g, 4u * 1024 * 1024, 4), GB_OK);
    uint64_t at8 = 0;
    run(g, 8);
    run(twin, 8);
    size_t size = gb_state_size(twin);
    uint8_t *want = malloc(size), *got = malloc(size);
    gb_state_save(twin, want, size); /* the machine after 8 frames */
    at8 = run(twin, 6);
    run(g, 10); /* snapshots after 4, 8, 12, 16; 2 more frames on top */
    CHECK_EQ(gb_rewind_step_back(g), 1); /* to 16 */
    CHECK_EQ(gb_rewind_step_back(g), 1); /* 12 */
    CHECK_EQ(gb_rewind_step_back(g), 1); /* 8 */
    gb_state_save(g, got, size);
    CHECK(!memcmp(want, got, size));
    CHECK_EQ(run(g, 6), at8); /* and on from there exactly as the first time */
    gb_rewind_configure(g, 4u * 1024 * 1024, 4);
    CHECK_EQ(gb_rewind_step_back(g), 0); /* configuring again forgets the history */
    run(g, 9);
    gb_reset(g); /* so does a reset */
    CHECK_EQ(gb_rewind_step_back(g), 0);
    run(g, 9);
    gb_state_load(g, want, size); /* and a loaded state */
    CHECK_EQ(gb_rewind_step_back(g), 0);
    CHECK_EQ(gb_rewind_configure(g, 0, 4), GB_OK); /* off again */
    run(g, 9);
    CHECK_EQ(gb_rewind_step_back(g), 0);
    free(want);
    free(got);
    gb_destroy(g);
    gb_destroy(twin);
}

/* Loading another ROM changes the state's size: rewind keeps working with the new one. */
TEST(gb_rewind_survives_a_new_rom) {
    gb_t *g = busy(0);
    gb_rewind_configure(g, 4u * 1024 * 1024, 2);
    run(g, 6);
    uint8_t *rom = gbtu_rom(0x8000, 0x1B, 4, 0); /* 128 KiB of cartridge RAM: a much bigger state */
    memcpy(rom + 0x150, BUSY, sizeof BUSY);
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    free(rom);
    CHECK_EQ(gb_rewind_step_back(g), 0);
    run(g, 6);
    CHECK_EQ(gb_rewind_step_back(g), 1);
    gb_destroy(g);
}

TEST(machine_state_kind_mismatch) {
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0);
    machine_t *gb = machine_create(MACHINE_GB), *gba = machine_create(MACHINE_GBA);
    machine_load_rom(gb, rom, 0x8000);
    static uint8_t gba_rom[0x200];
    machine_load_rom(gba, gba_rom, sizeof gba_rom);
    free(rom);
    size_t gb_size = machine_state_size(gb), gba_size = machine_state_size(gba);
    CHECK(gb_size > 0 && gba_size > 0);
    uint8_t *a = malloc(gb_size), *b = malloc(gba_size);
    CHECK_EQ(machine_state_save(gb, a, gb_size), gb_size);
    CHECK_EQ(machine_state_save(gba, b, gba_size), gba_size);
    CHECK_EQ(machine_state_load(gb, b, gba_size), MACHINE_ERR_STATE);
    CHECK_EQ(machine_state_load(gba, a, gb_size), MACHINE_ERR_STATE);
    CHECK_EQ(machine_state_load(gb, a, gb_size), MACHINE_OK);
    CHECK_EQ(machine_rewind_configure(gb, 1024 * 1024, 1), MACHINE_OK);
    machine_run_frame(gb);
    machine_run_frame(gb);
    CHECK_EQ(machine_rewind_step_back(gb), 1);
    free(a);
    free(b);
    machine_destroy(gb);
    machine_destroy(gba);
}
