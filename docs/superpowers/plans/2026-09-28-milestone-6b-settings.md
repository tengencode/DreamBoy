# Milestone 6b — Settings and Controller Remapping — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** A Settings screen (from the library and the in-game menu) with picture (sharp/smooth, fit / whole-number / stretch), sound (on/off, delay), fast-forward speed + rewind on/off and length, keyboard + controller remapping, the touch buttons' sideways look and buzz, and a BIOS file; per-game save type and clock.

**Architecture:** The core refuses a save state of the other BIOS kind. The C platform layer gets size modes (`fit_rect_mode`), smooth drawing in the shader, the sound buffer size, and `emu_options_t` (BIOS, overrides, rewind budget, sound) for `emu_create`; JNI setters keep them (`setBios`, `setRewindBudget`, `setSound`, `setPicture`, `setGameOverrides` for the next game only, `cartSetup`). Kotlin: `Settings`, `KeyBindings` (which `KeyMap` now reads), `GameSettings`, `Bios` on an `IntStore` (SharedPreferences in the app, a map in the PC-side tests); `SettingsScreens.kt` draws the pages; `MainActivity` applies and saves.

**Tech Stack:** C11 (core, PC tests, Android NDK, GLSL ES 3.0), CMake/CTest, Kotlin + Jetpack Compose, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-28-milestone-6b-settings-design.md`

---

## Conventions (same as 6a)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its files or preferences (except the test files named in a step, and the settings only as the check in Task 5 says, which puts them back) without the user's OK. Starting a closed emulator (`dev.sh emu`) is fine; ask before restarting/cold-booting it.
- **The user's BIOS** `tests/bios/gba_bios.bin` (16 KiB, CRC-32 `81977335`) is for local tests only — gitignored (`*.bin`), never committed; steps that use it skip themselves where it is missing.
- Unit tests: `TEST(name)` in `tests/unit/*.c`. One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-6b` (already created; the spec commits are on it). The tasks run one after another without stopping (`CLAUDE.md`); the only stop is Task 6, the user's check and the 6b exit. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p6b`, branch `proto-6b`), which the user tried on 2026-09-28 ('Everything works').

## File map

```
core/src/state.c; tests/unit/test_state.c                                                         Task 1
platform/android/fit_rect.{h,c}; tests/unit/test_platform.c                                                   Task 2
app/src/main/java/com/tengen/gbaemu/Store.kt, Settings.kt, KeyBindings.kt, KeyMap.kt, GameSettings.kt, Bios.kt;
  app/src/test/java/com/tengen/gbaemu/SettingsTest.kt, KeyBindingsTest.kt, GameSettingsTest.kt, BiosTest.kt              Task 3
platform/android/video_gl.{h,c}, gl_readback.{h,c}, render_thread.{h,c}, audio_out.{h,c}, emu_thread.{h,c}, jni_bridge.c;
  app/src/main/java/com/tengen/gbaemu/NativeBridge.kt; app/src/androidTest/java/com/tengen/gbaemu/SettingsDeviceTest.kt                                    Task 4
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt (new), Screens.kt, TouchControlsView.kt, MainActivity.kt                      Task 5
```

---
### Task 1: A save state of the other BIOS kind is refused

**Files:**
- Modify: `core/src/state.c`, `tests/unit/test_state.c`

- [x] **Step 1: Write the failing test**

Replace `tests/unit/test_state.c`:

```c
/* Save states (core/src/state.c): a loaded state is the same machine; a bad state changes nothing. */
#include <stdlib.h>
#include <string.h>

#include "gba_debug.h"
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
```

(The new test is the last one: `machine()` builds the same 0x400-byte ROM with the built-in BIOS or a BIOS file; a ROM smaller than that is refused by `gba_load_rom`.)

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh unit state_
```

Expected: `[ FAIL ] state_of_the_other_bios_kind_is_refused`, `11 tests, 1 failed`.

- [x] **Step 3: Write the code**

Replace `core/src/state.c`:

```c
/* Save states: the whole machine, one chunk per component holding that component's struct as bytes.
 *
 * Layout (little-endian hosts): header "GBAS", format version, ROM CRC-32, chunk count; then per chunk
 * a 4-letter tag, a version and a length, followed by the bytes. A state loads only into the same ROM
 * and only when every chunk's tag, version and length match this build: everything is checked first,
 * so a refused state leaves the machine untouched. Bump a chunk's version by hand when the meaning of
 * its struct changes without its size changing. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gba_internal.h"
#include "rewind.h"

#define STATE_FORMAT 1u
#define HEADER_SIZE 16u
#define CHUNK_HEAD 12u
#define MISC_CHUNK SIZE_MAX /* not a struct inside gba_t: the fields below */

/* The machine's own fields, in a fixed layout without padding. */
typedef struct {
    uint64_t now;
    uint16_t keyinput;
    uint8_t frame_done;
    uint8_t hle_bios;
    uint32_t reserved;
} misc_t;

typedef struct {
    char tag[4];
    uint32_t version;
    size_t offset; /* in struct gba; MISC_CHUNK for misc_t */
    size_t size;
} chunk_t;

static const chunk_t chunks[] = {
    {{'C', 'P', 'U', ' '}, 1, offsetof(struct gba, cpu), sizeof(arm_cpu_t)},
    {{'B', 'U', 'S', ' '}, 1, offsetof(struct gba, bus), sizeof(bus_t)},
    {{'I', 'O', ' ', ' '}, 1, offsetof(struct gba, io), sizeof(io_t)},
    {{'P', 'P', 'U', ' '}, 1, offsetof(struct gba, ppu), sizeof(ppu_t)},
    {{'I', 'R', 'Q', ' '}, 1, offsetof(struct gba, irq), sizeof(irq_t)},
    {{'T', 'M', 'R', ' '}, 1, offsetof(struct gba, timers), sizeof(timers_t)},
    {{'D', 'M', 'A', ' '}, 1, offsetof(struct gba, dma), sizeof(dma_t)},
    {{'A', 'P', 'U', ' '}, 1, offsetof(struct gba, apu), sizeof(apu_t)},
    {{'S', 'I', 'O', ' '}, 1, offsetof(struct gba, sio), sizeof(sio_t)},
    {{'S', 'C', 'H', 'D'}, 1, offsetof(struct gba, sched), sizeof(sched_t)},
    {{'C', 'A', 'R', 'T'}, 1, offsetof(struct gba, cart), sizeof(cart_t)}, /* the ROM pointer is never kept */
    {{'M', 'I', 'S', 'C'}, 1, MISC_CHUNK, sizeof(misc_t)},
};
#define CHUNK_COUNT (sizeof chunks / sizeof chunks[0])

static void put32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}

static uint32_t get32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

size_t gba_state_size(const gba_t *g) {
    (void)g;
    size_t n = HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) n += CHUNK_HEAD + chunks[i].size;
    return n;
}

size_t gba_state_save(const gba_t *g, void *buf, size_t cap) {
    size_t total = gba_state_size(g);
    if (!buf || cap < total) return 0;
    uint8_t *p = buf;
    memcpy(p, "GBAS", 4);
    put32(p + 4, STATE_FORMAT);
    put32(p + 8, g->cart.crc32);
    put32(p + 12, (uint32_t)CHUNK_COUNT);
    p += HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        const chunk_t *c = &chunks[i];
        memcpy(p, c->tag, 4);
        put32(p + 4, c->version);
        put32(p + 8, (uint32_t)c->size);
        p += CHUNK_HEAD;
        if (c->offset == MISC_CHUNK) {
            misc_t m;
            memset(&m, 0, sizeof m);
            m.now = g->now;
            m.keyinput = g->keyinput;
            m.frame_done = g->frame_done;
            m.hle_bios = g->hle_bios;
            memcpy(p, &m, sizeof m);
        } else {
            memcpy(p, (const uint8_t *)g + c->offset, c->size);
            if (c->offset == offsetof(struct gba, cart)) memset(p + offsetof(cart_t, rom), 0, sizeof(uint8_t *));
        }
        p += c->size;
    }
    return total;
}

gba_err_t gba_state_load(gba_t *g, const void *buf, size_t len) {
    gba_err_t err = state_load(g, buf, len);
    if (err == GBA_OK) rewind_clear(g->rewind);
    return err;
}

gba_err_t state_load(gba_t *g, const void *buf, size_t len) {
    const uint8_t *p = buf;
    if (!buf || len != gba_state_size(g)) return GBA_ERR_STATE;
    if (memcmp(p, "GBAS", 4) != 0 || get32(p + 4) != STATE_FORMAT || get32(p + 8) != g->cart.crc32 ||
        get32(p + 12) != CHUNK_COUNT) {
        return GBA_ERR_STATE;
    }
    const uint8_t *q = p + HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) { /* check everything before changing anything */
        const chunk_t *c = &chunks[i];
        if (memcmp(q, c->tag, 4) != 0 || get32(q + 4) != c->version || get32(q + 8) != c->size) return GBA_ERR_STATE;
        /* made with the other kind of BIOS (built-in vs a BIOS file): its SWIs would run the wrong BIOS */
        if (c->offset == MISC_CHUNK && (q[CHUNK_HEAD + offsetof(misc_t, hle_bios)] != 0) != g->hle_bios) return GBA_ERR_STATE;
        q += CHUNK_HEAD + c->size;
    }
    q = p + HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        const chunk_t *c = &chunks[i];
        q += CHUNK_HEAD;
        if (c->offset == MISC_CHUNK) {
            misc_t m;
            memcpy(&m, q, sizeof m);
            g->now = m.now;
            g->keyinput = m.keyinput;
            g->frame_done = m.frame_done != 0;
            g->hle_bios = m.hle_bios != 0;
        } else if (c->offset == offsetof(struct gba, cart)) {
            uint8_t *rom = g->cart.rom; /* the loaded ROM stays; everything else comes from the state */
            memcpy(&g->cart, q, sizeof(cart_t));
            g->cart.rom = rom;
        } else {
            memcpy((uint8_t *)g + c->offset, q, c->size);
        }
        q += c->size;
    }
    return GBA_OK;
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `400 tests, 0 failed` (399 + 1); CTest 120/120; no warnings.

- [x] **Step 5: Commit**

```bash
git add core/src/state.c tests/unit/test_state.c docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6b-settings.md
git commit -m "feat(core): a save state made with the other kind of BIOS is refused" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Picture size modes

**Files:**
- Modify: `platform/android/fit_rect.h`, `platform/android/fit_rect.c`, `tests/unit/test_platform.c`

- [x] **Step 1: Write the failing tests**

Replace `tests/unit/test_platform.c`:

```c
/* The Android platform layer's plain-C pieces (platform/android), which also build on the PC. */
#include <pthread.h>
#include <stdlib.h>

#include "fit_rect.h"
#include "frame_pacer.h"
#include "test.h"
#include "triple_buffer.h"

/* ---- fit_rect ---- */

TEST(fit_rect_exact_3_to_2_fills_the_surface) {
    fit_rect_t r = fit_rect(480, 320);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
}

TEST(fit_rect_wide_surface_gets_bars_left_and_right) {
    fit_rect_t r = fit_rect(720, 320);
    CHECK_EQ(r.x, 120);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
}

TEST(fit_rect_tall_surface_gets_bars_above_and_below) {
    fit_rect_t r = fit_rect(1080, 2424); /* Pixel 9 portrait */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 852);
    CHECK_EQ(r.w, 1080);
    CHECK_EQ(r.h, 720);
}

TEST(fit_rect_odd_sizes_round_down_and_centre) {
    fit_rect_t r = fit_rect(301, 200);
    CHECK_EQ(r.w, 300);
    CHECK_EQ(r.h, 200);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    r = fit_rect(0, 100);
    CHECK_EQ(r.w, 0);
    CHECK_EQ(r.h, 0);
}

TEST(fit_rect_mode_fit_is_fit_rect) {
    fit_rect_t a = fit_rect(1080, 2424), b = fit_rect_mode(1080, 2424, FIT_ASPECT);
    CHECK_EQ(b.x, a.x);
    CHECK_EQ(b.y, a.y);
    CHECK_EQ(b.w, a.w);
    CHECK_EQ(b.h, a.h);
}

TEST(fit_rect_mode_integer_takes_the_largest_whole_multiple_centred) {
    fit_rect_t r = fit_rect_mode(1080, 720, FIT_INTEGER); /* Pixel 9 upright: 4.5x → 4x */
    CHECK_EQ(r.w, 960);
    CHECK_EQ(r.h, 640);
    CHECK_EQ(r.x, 60);
    CHECK_EQ(r.y, 40);
    r = fit_rect_mode(2424, 1080, FIT_INTEGER); /* sideways: 6x by height */
    CHECK_EQ(r.w, 1440);
    CHECK_EQ(r.h, 960);
    CHECK_EQ(r.x, 492);
    CHECK_EQ(r.y, 60);
    r = fit_rect_mode(480, 320, FIT_INTEGER); /* exactly 2x */
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
}

TEST(fit_rect_mode_integer_smaller_than_1x_falls_back_to_fit) {
    fit_rect_t r = fit_rect_mode(200, 100, FIT_INTEGER);
    fit_rect_t f = fit_rect(200, 100);
    CHECK_EQ(r.w, f.w);
    CHECK_EQ(r.h, f.h);
    CHECK_EQ(r.x, f.x);
    CHECK_EQ(r.y, f.y);
}

TEST(fit_rect_mode_stretch_fills_the_surface) {
    fit_rect_t r = fit_rect_mode(2424, 1080, FIT_STRETCH);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 2424);
    CHECK_EQ(r.h, 1080);
    r = fit_rect_mode(0, 50, FIT_STRETCH);
    CHECK_EQ(r.w, 0);
}

/* ---- frame pacer ---- */

TEST(pacer_first_frame_is_due_one_period_after_now) {
    frame_pacer_t p;
    pacer_reset(&p);
    CHECK_EQ(pacer_next(&p, 1000), 1000 + PACER_PERIOD_NS);
}

TEST(pacer_keeps_exact_time_over_600_frames) {
    frame_pacer_t p;
    pacer_reset(&p);
    int64_t start = 5000, deadline = pacer_next(&p, start);
    for (int i = 1; i < 600; i++) deadline = pacer_next(&p, deadline + 100); /* woke a little late each time */
    /* 600 x 16,742,706.2988 ns = 10,045,623,779.3 ns */
    CHECK_EQ(deadline - start, 10045623779LL);
}

TEST(pacer_restarts_instead_of_catching_up) {
    frame_pacer_t p;
    pacer_reset(&p);
    int64_t d = pacer_next(&p, 0);
    int64_t late = d + 4LL * PACER_PERIOD_NS; /* more than 3 frames behind */
    CHECK_EQ(pacer_next(&p, late), late + PACER_PERIOD_NS);
    int64_t d2 = pacer_next(&p, late + PACER_PERIOD_NS + 10); /* back on time: normal step */
    CHECK_EQ(d2, late + 2LL * PACER_PERIOD_NS);
}

TEST(pacer_reset_starts_again_from_now) {
    frame_pacer_t p;
    pacer_reset(&p);
    pacer_next(&p, 0);
    pacer_reset(&p);
    CHECK_EQ(pacer_next(&p, 7), 7 + PACER_PERIOD_NS);
}

/* ---- triple buffer ---- */

static triple_buffer_t *tb_new(void) {
    triple_buffer_t *tb = malloc(sizeof *tb);
    tb_init(tb);
    return tb;
}

TEST(tb_nothing_published_is_not_fresh) {
    triple_buffer_t *tb = tb_new();
    bool fresh = true;
    tb_take(tb, &fresh);
    CHECK(!fresh);
    free(tb);
}

TEST(tb_consumer_gets_the_published_frame_once) {
    triple_buffer_t *tb = tb_new();
    tb_back(tb)[0] = 0x1234;
    tb_publish(tb);
    bool fresh = false;
    const uint16_t *f = tb_take(tb, &fresh);
    CHECK(fresh);
    CHECK_EQ(f[0], 0x1234);
    f = tb_take(tb, &fresh);
    CHECK(!fresh);
    CHECK_EQ(f[0], 0x1234); /* the same frame again */
    free(tb);
}

TEST(tb_newest_frame_wins) {
    triple_buffer_t *tb = tb_new();
    tb_back(tb)[0] = 1;
    tb_publish(tb);
    tb_back(tb)[0] = 2;
    tb_publish(tb);
    bool fresh;
    CHECK_EQ(tb_take(tb, &fresh)[0], 2);
    CHECK(fresh);
    free(tb);
}

TEST(tb_producer_never_writes_the_consumers_frame) {
    triple_buffer_t *tb = tb_new();
    bool fresh;
    for (int i = 0; i < 20; i++) {
        tb_publish(tb);
        if (i % 3 == 0) tb_take(tb, &fresh);
        CHECK(tb_back(tb) != tb->slot[tb->front]);
    }
    free(tb);
}

/* Two real threads: every frame the consumer sees is whole (all pixels equal) and never older. */
static void *tb_producer(void *arg) {
    triple_buffer_t *tb = arg;
    for (uint16_t n = 1; n <= 20000; n++) {
        uint16_t *px = tb_back(tb);
        for (int i = 0; i < TB_PIXELS; i++) px[i] = n;
        tb_publish(tb);
    }
    return NULL;
}

TEST(tb_threads_never_see_a_torn_or_older_frame) {
    triple_buffer_t *tb = tb_new();
    pthread_t t;
    pthread_create(&t, NULL, tb_producer, tb);
    uint16_t last = 0;
    bool torn = false, older = false;
    while (last < 20000) {
        bool fresh;
        const uint16_t *f = tb_take(tb, &fresh);
        if (!fresh) continue;
        for (int i = 1; i < TB_PIXELS; i++) torn |= f[i] != f[0];
        older |= f[0] < last;
        last = f[0];
    }
    pthread_join(t, NULL);
    CHECK(!torn);
    CHECK(!older);
    free(tb);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit fit_rect
```

Expected: the build fails — `call to undeclared function 'fit_rect_mode'`, `use of undeclared identifier 'FIT_ASPECT'`.

- [x] **Step 3: Write the code**

Replace `platform/android/fit_rect.h`:

```c
#ifndef GBAEMU_FIT_RECT_H
#define GBAEMU_FIT_RECT_H

typedef struct {
    int x, y, w, h;
} fit_rect_t;

/* The largest 3:2 rectangle inside a width x height surface, centred, in whole pixels
 * (x and y are the offsets of the free space on each side; all zero for an empty surface). */
fit_rect_t fit_rect(int width, int height);

/* How the picture fills the surface (Settings → Picture → Size). */
typedef enum {
    FIT_ASPECT = 0,  /* fit_rect: the largest 3:2 rectangle */
    FIT_INTEGER = 1, /* the largest whole multiple of 240x160 that fits, centred (FIT_ASPECT if not even 1x fits) */
    FIT_STRETCH = 2, /* the whole surface */
} fit_mode_t;

fit_rect_t fit_rect_mode(int width, int height, fit_mode_t mode);

#endif
```

Replace `platform/android/fit_rect.c`:

```c
#include "fit_rect.h"

fit_rect_t fit_rect(int width, int height) {
    fit_rect_t r = {0, 0, 0, 0};
    if (width <= 0 || height <= 0) return r;
    if ((long)width * 2 <= (long)height * 3) { /* the width is the limit: bars above and below */
        r.w = width;
        r.h = width * 2 / 3;
    } else { /* the height is the limit: bars left and right */
        r.h = height;
        r.w = height * 3 / 2;
    }
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}

fit_rect_t fit_rect_mode(int width, int height, fit_mode_t mode) {
    if (width <= 0 || height <= 0) return fit_rect(width, height);
    if (mode == FIT_STRETCH) return (fit_rect_t){0, 0, width, height};
    if (mode == FIT_INTEGER) {
        int n = width / 240 < height / 160 ? width / 240 : height / 160;
        if (n >= 1) return (fit_rect_t){(width - 240 * n) / 2, (height - 160 * n) / 2, 240 * n, 160 * n};
    }
    return fit_rect(width, height);
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `404 tests, 0 failed` (400 + 4); CTest 120/120; no warnings.

- [x] **Step 5: Commit**

```bash
git add platform/android/fit_rect.h platform/android/fit_rect.c tests/unit/test_platform.c docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6b-settings.md
git commit -m "feat(platform): picture size modes - fit, whole-number steps, stretch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The settings, key bindings, game settings and BIOS file (Kotlin)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Store.kt`, `app/src/main/java/com/tengen/gbaemu/Settings.kt`, `app/src/main/java/com/tengen/gbaemu/KeyBindings.kt`, `app/src/main/java/com/tengen/gbaemu/GameSettings.kt`, `app/src/main/java/com/tengen/gbaemu/Bios.kt`, `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt`, `app/src/test/java/com/tengen/gbaemu/KeyBindingsTest.kt`, `app/src/test/java/com/tengen/gbaemu/GameSettingsTest.kt`, `app/src/test/java/com/tengen/gbaemu/BiosTest.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/KeyMap.kt` (the keys in use are `keyboard` / `gamepad`, set by `KeyBindings.use`)

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/SettingsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class SettingsTest {
    @Test
    fun nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour() {
        val s = Settings.load(MemoryStore())
        assertEquals(Settings(), s)
        assertEquals(false, s.smooth)
        assertEquals(Settings.SIZE_FIT, s.size)
        assertEquals(true, s.sound)
        assertEquals(2, s.soundBursts)
        assertEquals(4, s.fastSpeed)
        assertEquals(64L * 1024 * 1024, s.rewindBudget)
        assertEquals(Settings.LOOK_NORMAL, s.touchLook)
        assertEquals(true, s.buzz)
        assertEquals(false, s.biosFile)
    }

    @Test
    fun everySettingIsSavedAndReadBack() {
        val store = MemoryStore()
        val s = Settings(smooth = true, size = Settings.SIZE_STRETCH, sound = false, delay = Settings.DELAY_HIGH,
            fastSpeed = NativeBridge.FF_MAX, rewind = false, rewindLength = Settings.LENGTH_LONG,
            touchLook = Settings.LOOK_STRONG, buzz = false, biosFile = true)
        s.save(store)
        assertEquals(s, Settings.load(store))
    }

    @Test
    fun theFastForwardSpeedStoredBefore6bIsKept() {
        val store = MemoryStore()
        store.put("fast_speed", 8) // written by the in-game menu row since 5b
        assertEquals(8, Settings.load(store).fastSpeed)
    }

    @Test
    fun aValueThatDoesNotReadIsItsDefault() {
        val store = MemoryStore()
        for ((key, value) in listOf("size" to 9, "delay" to -2, "fast_speed" to 3, "rewind_length" to 7, "touch_look" to 5,
            "smooth" to 4)) store.put(key, value)
        assertEquals(Settings(), Settings.load(store))
    }

    @Test
    fun theDelayAndTheRewindLengthMeanBurstsAndBytes() {
        assertEquals(listOf(2, 4, 8), listOf(Settings.DELAY_LOW, Settings.DELAY_MEDIUM, Settings.DELAY_HIGH)
            .map { Settings(delay = it).soundBursts })
        assertEquals(listOf(24L, 64L, 128L).map { it * 1024 * 1024 },
            listOf(Settings.LENGTH_SHORT, Settings.LENGTH_NORMAL, Settings.LENGTH_LONG).map { Settings(rewindLength = it).rewindBudget })
        assertEquals(0L, Settings(rewind = false).rewindBudget)
    }
}
```

`app/src/test/java/com/tengen/gbaemu/KeyBindingsTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test

class KeyBindingsTest {
    @After
    fun backToTheDefaults() = KeyBindings(MemoryStore()).use() // KeyMap is shared by every test

    @Test
    fun theDefaultsAreTheKeysOfMilestone4() {
        val k = KeyBindings(MemoryStore())
        for (b in KeyMap.bindings) {
            assertEquals(b.button, b.keyboard.keys.first(), k.key(b.bit, KeyBindings.KEYBOARD))
            assertEquals(b.button, b.gamepad.keys.first(), k.key(b.bit, KeyBindings.CONTROLLER))
        }
    }

    @Test
    fun aKeyAlreadyUsedMovesAndLeavesItsOldButtonEmpty() {
        val k = KeyBindings(MemoryStore())
        k.assign(KeyMap.B, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_X) // X was A's
        assertEquals(KeyEvent.KEYCODE_X, k.key(KeyMap.B, KeyBindings.KEYBOARD))
        assertEquals(KeyBindings.NONE, k.key(KeyMap.A, KeyBindings.KEYBOARD))
        assertEquals(KeyEvent.KEYCODE_BUTTON_A, k.key(KeyMap.A, KeyBindings.CONTROLLER)) // the other column untouched
        k.assign(KeyMap.A, KeyBindings.CONTROLLER, KeyEvent.KEYCODE_BUTTON_Y) // a new key: nothing else changes
        assertEquals(KeyEvent.KEYCODE_BUTTON_Y, k.key(KeyMap.A, KeyBindings.CONTROLLER))
        assertEquals(KeyEvent.KEYCODE_BUTTON_B, k.key(KeyMap.B, KeyBindings.CONTROLLER))
    }

    @Test
    fun theKeysAreSavedResetAndUsedByKeyMap() {
        val store = MemoryStore()
        val k = KeyBindings(store)
        k.assign(KeyMap.START, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_SPACE)
        k.assign(KeyMap.FAST, KeyBindings.CONTROLLER, KeyEvent.KEYCODE_BUTTON_Y)
        val again = KeyBindings(store)
        assertEquals(KeyEvent.KEYCODE_SPACE, again.key(KeyMap.START, KeyBindings.KEYBOARD))
        again.use()
        assertEquals(KeyMap.START, KeyMap.bitFor(KeyEvent.KEYCODE_SPACE))
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_ENTER)) // no longer Start
        assertEquals(KeyMap.FAST, KeyMap.bitFor(KeyEvent.KEYCODE_BUTTON_Y))
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_BUTTON_R2))
        again.reset()
        again.use()
        assertEquals(KeyEvent.KEYCODE_ENTER, KeyBindings(store).key(KeyMap.START, KeyBindings.KEYBOARD))
        assertEquals(KeyMap.START, KeyMap.bitFor(KeyEvent.KEYCODE_ENTER))
    }

    @Test
    fun onlyKeyboardKeysForFastForwardAndRewindGoThroughTheRepeatFilter() {
        val k = KeyBindings(MemoryStore())
        k.use()
        assertEquals(true, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_F))
        k.assign(KeyMap.FAST, KeyBindings.KEYBOARD, KeyEvent.KEYCODE_G)
        k.use()
        assertEquals(false, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_F))
        assertEquals(true, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_G))
        assertEquals(false, KeyMap.isKeyboardExtra(KeyEvent.KEYCODE_BUTTON_R2))
    }
}
```

`app/src/test/java/com/tengen/gbaemu/GameSettingsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class GameSettingsTest {
    @Test
    fun aGameWithNothingStoredIsAuto() {
        val g = GameSettings(MemoryStore()).of("e86b3224")
        assertEquals(GameSettings.AUTO, g.saveType)
        assertEquals(GameSettings.AUTO, g.clock)
    }

    @Test
    fun eachGameKeepsItsOwn() {
        val store = MemoryStore()
        val gs = GameSettings(store)
        gs.set("e86b3224", GameSettings.Game(GameSettings.SAVE_FLASH128, GameSettings.CLOCK_OFF))
        gs.set("04f79685", GameSettings.Game(GameSettings.SAVE_SRAM, GameSettings.AUTO))
        val again = GameSettings(store)
        assertEquals(GameSettings.Game(GameSettings.SAVE_FLASH128, GameSettings.CLOCK_OFF), again.of("e86b3224"))
        assertEquals(GameSettings.Game(GameSettings.SAVE_SRAM, GameSettings.AUTO), again.of("04f79685"))
        assertEquals(GameSettings.Game(), again.of("00000000"))
        again.set("e86b3224", GameSettings.Game()) // back to Auto
        assertEquals(GameSettings.Game(), GameSettings(store).of("e86b3224"))
    }

    @Test
    fun whatTheGameHadWhenDetectedIsRemembered() {
        val store = MemoryStore()
        val gs = GameSettings(store)
        assertEquals(null, gs.detected("e86b3224"))
        gs.remember("e86b3224", GameSettings.SAVE_FLASH128, NativeBridge.HW_RTC or NativeBridge.HW_RUMBLE)
        assertEquals(GameSettings.SAVE_FLASH128 to true, GameSettings(store).detected("e86b3224"))
    }

    @Test
    fun aStoredValueThatDoesNotReadIsAuto() {
        val store = MemoryStore()
        store.put("e86b3224.save", 1) // "unknown" is not a choice
        store.put("e86b3224.clock", 7)
        assertEquals(GameSettings.Game(), GameSettings(store).of("e86b3224"))
    }
}
```

`app/src/test/java/com/tengen/gbaemu/BiosTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BiosTest {
    @Test
    fun onlySixteenKilobytesIsABiosAndTheUsualOneIsKnownByItsCrc() {
        assertEquals(Bios.Check.WRONG_SIZE, Bios.check(ByteArray(16383)))
        assertEquals(Bios.Check.WRONG_SIZE, Bios.check(ByteArray(0)))
        assertEquals(Bios.Check.UNUSUAL, Bios.check(ByteArray(16384)))
        val real = File("../tests/bios/gba_bios.bin") // the user's own dump, where it exists (never committed)
        if (real.isFile) assertEquals(Bios.Check.USUAL, Bios.check(real.readBytes()))
    }

    @Test
    fun aBiosIsCopiedIntoTheAppReadBackAndRemoved() {
        val dir = Files.createTempDirectory("bios").toFile()
        assertNull(Bios.load(dir))
        val bytes = ByteArray(16384) { (it * 13).toByte() }
        assertTrue(Bios.install(dir, bytes))
        assertTrue(File(dir, "bios/gba_bios.bin").isFile)
        assertArrayEquals(bytes, Bios.load(dir))
        Bios.remove(dir)
        assertNull(Bios.load(dir))
        assertFalse(Bios.install(dir, ByteArray(100))) // not a BIOS: nothing written
        assertNull(Bios.load(dir))
        dir.deleteRecursively()
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'Bios'`, `'GameSettings'`, `'KeyBindings'`, `'MemoryStore'`, `'Settings'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/Store.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.SharedPreferences

/** Whole numbers by name: the settings' storage (SharedPreferences in the app, a map in the PC-side tests). */
interface IntStore {
    fun get(key: String): Int?

    /** null removes the value. */
    fun put(key: String, value: Int?)
}

class MemoryStore : IntStore {
    private val map = HashMap<String, Int>()

    override fun get(key: String): Int? = map[key]

    override fun put(key: String, value: Int?) {
        if (value == null) map.remove(key) else map[key] = value
    }
}

/** A value of another kind under the same name reads as nothing. */
class PrefsStore(private val prefs: SharedPreferences) : IntStore {
    override fun get(key: String): Int? = if (!prefs.contains(key)) null else try {
        prefs.getInt(key, 0)
    } catch (e: ClassCastException) {
        null
    }

    override fun put(key: String, value: Int?) {
        val edit = prefs.edit()
        if (value == null) edit.remove(key) else edit.putInt(key, value)
        edit.apply()
    }
}
```

`app/src/main/java/com/tengen/gbaemu/Settings.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * The app-wide settings (Settings screen). The defaults are the app's behaviour before 6b. Stored in an [IntStore]
 * (the "settings" preferences); `fast_speed` is the key the in-game menu row has used since 5b, so it is kept.
 */
data class Settings(
    val smooth: Boolean = false,
    val size: Int = SIZE_FIT,
    val sound: Boolean = true,
    val delay: Int = DELAY_LOW,
    val fastSpeed: Int = 4,
    val rewind: Boolean = true,
    val rewindLength: Int = LENGTH_NORMAL,
    val touchLook: Int = LOOK_NORMAL,
    val buzz: Boolean = true,
    val biosFile: Boolean = false, // a BIOS file was chosen (Bios.file); the next game starts through it
) {
    /** AAudio's own buffer, in bursts. */
    val soundBursts: Int get() = when (delay) {
        DELAY_MEDIUM -> 4
        DELAY_HIGH -> 8
        else -> 2
    }

    /** Bytes for rewind snapshots (0 = rewind off). */
    val rewindBudget: Long get() = if (!rewind) 0 else when (rewindLength) {
        LENGTH_SHORT -> 24L * MIB
        LENGTH_LONG -> 128L * MIB
        else -> 64L * MIB
    }

    fun save(store: IntStore) {
        store.put("smooth", flag(smooth))
        store.put("size", size)
        store.put("sound", flag(sound))
        store.put("delay", delay)
        store.put("fast_speed", fastSpeed)
        store.put("rewind", flag(rewind))
        store.put("rewind_length", rewindLength)
        store.put("touch_look", touchLook)
        store.put("buzz", flag(buzz))
        store.put("bios_file", flag(biosFile))
    }

    companion object {
        private const val MIB = 1024 * 1024

        const val SIZE_FIT = 0 // the numbers of fit_mode_t (platform/android/fit_rect.h)
        const val SIZE_INTEGER = 1
        const val SIZE_STRETCH = 2
        const val DELAY_LOW = 0
        const val DELAY_MEDIUM = 1
        const val DELAY_HIGH = 2
        const val LENGTH_SHORT = 0
        const val LENGTH_NORMAL = 1
        const val LENGTH_LONG = 2
        const val LOOK_NORMAL = 0
        const val LOOK_FAINT = 1
        const val LOOK_STRONG = 2

        /** The choices' names, by value. */
        val SIZE_NAMES = listOf("Fit screen", "Whole-number steps", "Stretch to fill")
        val DELAY_NAMES = listOf("Low", "Medium", "High")
        val LENGTH_NAMES = listOf("About 1 minute", "About 2½ minutes", "About 5 minutes")
        val LOOK_NAMES = listOf("Normal", "Faint", "Strong")
        val SPEEDS = listOf(2, 4, 8, NativeBridge.FF_MAX)

        fun speedName(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

        fun load(store: IntStore): Settings {
            val d = Settings()
            return Settings(
                smooth = bool(store, "smooth", d.smooth),
                size = choice(store, "size", 0..2, d.size),
                sound = bool(store, "sound", d.sound),
                delay = choice(store, "delay", 0..2, d.delay),
                fastSpeed = store.get("fast_speed")?.takeIf { it in SPEEDS } ?: d.fastSpeed,
                rewind = bool(store, "rewind", d.rewind),
                rewindLength = choice(store, "rewind_length", 0..2, d.rewindLength),
                touchLook = choice(store, "touch_look", 0..2, d.touchLook),
                buzz = bool(store, "buzz", d.buzz),
                biosFile = bool(store, "bios_file", d.biosFile),
            )
        }

        private fun flag(b: Boolean) = if (b) 1 else 0

        private fun bool(store: IntStore, key: String, default: Boolean): Boolean = when (store.get(key)) {
            0 -> false
            1 -> true
            else -> default
        }

        private fun choice(store: IntStore, key: String, valid: IntRange, default: Int): Int =
            store.get(key)?.takeIf { it in valid } ?: default
    }
}
```

`app/src/main/java/com/tengen/gbaemu/KeyBindings.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent

/**
 * Which keyboard key and which controller button press each GBA button (and ⏩ ⟲): one of each, stored in an
 * [IntStore] (the "keys" preferences) as `kbd.<bit>` / `pad.<bit>` = the Android key code (0 = none). Nothing
 * stored = the defaults ([KeyMap.bindings]). [use] makes them the ones [KeyMap] reads.
 */
class KeyBindings(private val store: IntStore) {
    /** The key for [bit] on [device] ([KEYBOARD] or [CONTROLLER]); [NONE] if it has none. */
    fun key(bit: Int, device: Int): Int = store.get(name(bit, device)) ?: default(bit, device)

    /** [keyCode] now presses [bit]; if another button had it, that one is left with none. */
    fun assign(bit: Int, device: Int, keyCode: Int) {
        for (b in KeyMap.bindings) {
            if (b.bit != bit && key(b.bit, device) == keyCode) store.put(name(b.bit, device), NONE)
        }
        store.put(name(bit, device), keyCode)
    }

    /** Back to the defaults. */
    fun reset() {
        for (b in KeyMap.bindings) for (device in listOf(KEYBOARD, CONTROLLER)) store.put(name(b.bit, device), null)
    }

    /** From now on key events are read with these keys. */
    fun use() {
        KeyMap.keyboard = map(KEYBOARD)
        KeyMap.gamepad = map(CONTROLLER)
    }

    private fun map(device: Int): Map<Int, Int> =
        KeyMap.bindings.mapNotNull { b -> key(b.bit, device).takeIf { it != NONE }?.let { it to b.bit } }.toMap()

    private fun name(bit: Int, device: Int) = (if (device == KEYBOARD) "kbd." else "pad.") + bit

    private fun default(bit: Int, device: Int): Int {
        val b = KeyMap.bindings.first { it.bit == bit }
        return (if (device == KEYBOARD) b.keyboard else b.gamepad).keys.first()
    }

    companion object {
        const val KEYBOARD = 0
        const val CONTROLLER = 1
        const val NONE = 0 // KeyEvent.KEYCODE_UNKNOWN

        /** A key's name to show: the default table's name (X, Enter, ↑, L1 …) or Android's (Space, Button Y …). */
        fun keyName(keyCode: Int): String {
            if (keyCode == NONE) return "—"
            for (b in KeyMap.bindings) (b.keyboard[keyCode] ?: b.gamepad[keyCode])?.let { return it }
            val raw = KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_")
            return raw.split('_').joinToString(" ") { w -> w.lowercase().replaceFirstChar { it.uppercase() } }
        }
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent

/**
 * Keyboard and gamepad keys → GBA buttons (the same bits as GBA_KEY_* in core/include/gba.h), plus the app's own
 * fast-forward and rewind buttons above them (never sent to the game: see [GBA_BUTTONS]). [bindings] is the default
 * table; the keys in use are [keyboard] and [gamepad], which KeyBindings.use sets (Settings → Keyboard & controller).
 */
object KeyMap {
    const val A = 1 shl 0
    const val B = 1 shl 1
    const val SELECT = 1 shl 2
    const val START = 1 shl 3
    const val RIGHT = 1 shl 4
    const val LEFT = 1 shl 5
    const val UP = 1 shl 6
    const val DOWN = 1 shl 7
    const val R = 1 shl 8
    const val L = 1 shl 9
    const val FAST = 1 shl 10 // ⏩ fast-forward: tap or hold
    const val REWIND = 1 shl 11 // ⟲ rewind: hold
    const val GBA_BUTTONS = 0x3FF

    /** One GBA button and what presses it: keyboard keys and gamepad buttons (key code → name to show). */
    class Binding(val bit: Int, val button: String, val keyboard: Map<Int, String>, val gamepad: Map<Int, String>)

    /** The default map (and the buttons' names). */
    val bindings = listOf(
        Binding(A, "A", mapOf(KeyEvent.KEYCODE_X to "X"), mapOf(KeyEvent.KEYCODE_BUTTON_A to "A")),
        Binding(B, "B", mapOf(KeyEvent.KEYCODE_Z to "Z"), mapOf(KeyEvent.KEYCODE_BUTTON_B to "B")),
        Binding(L, "L", mapOf(KeyEvent.KEYCODE_A to "A"), mapOf(KeyEvent.KEYCODE_BUTTON_L1 to "L1")),
        Binding(R, "R", mapOf(KeyEvent.KEYCODE_S to "S"), mapOf(KeyEvent.KEYCODE_BUTTON_R1 to "R1")),
        Binding(START, "Start", mapOf(KeyEvent.KEYCODE_ENTER to "Enter"), mapOf(KeyEvent.KEYCODE_BUTTON_START to "Start")),
        Binding(SELECT, "Select", mapOf(KeyEvent.KEYCODE_DEL to "Backspace"), mapOf(KeyEvent.KEYCODE_BUTTON_SELECT to "Select")),
        Binding(UP, "Up", mapOf(KeyEvent.KEYCODE_DPAD_UP to "↑"), mapOf(KeyEvent.KEYCODE_DPAD_UP to "D-pad ↑")),
        Binding(DOWN, "Down", mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "↓"), mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "D-pad ↓")),
        Binding(LEFT, "Left", mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "←"), mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "D-pad ←")),
        Binding(RIGHT, "Right", mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "→"), mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "D-pad →")),
        Binding(FAST, "Fast-forward", mapOf(KeyEvent.KEYCODE_F to "F"), mapOf(KeyEvent.KEYCODE_BUTTON_R2 to "R2")),
        Binding(REWIND, "Rewind", mapOf(KeyEvent.KEYCODE_R to "R"), mapOf(KeyEvent.KEYCODE_BUTTON_L2 to "L2")),
    )

    /** The keys in use: key code → button bit (the defaults until KeyBindings.use). */
    @Volatile
    var keyboard: Map<Int, Int> = bindings.associate { it.keyboard.keys.first() to it.bit }

    @Volatile
    var gamepad: Map<Int, Int> = bindings.associate { it.gamepad.keys.first() to it.bit }

    /** A keyboard key (not a gamepad button) for ⏩ or ⟲: those go through KeyRepeatFilter. */
    fun isKeyboardExtra(keyCode: Int): Boolean = keyboard[keyCode].let { it == FAST || it == REWIND }

    /** The GBA button for an Android key code, or 0 if the key is not mapped. */
    fun bitFor(keyCode: Int): Int = keyboard[keyCode] ?: gamepad[keyCode] ?: 0

    /** The held-buttons mask after a key goes down or up. */
    fun update(mask: Int, keyCode: Int, down: Boolean): Int {
        val bit = bitFor(keyCode)
        return if (down) mask or bit else mask and bit.inv()
    }

    /** What the game gets: keyboard/gamepad buttons and touch buttons together. */
    fun merge(keyboardMask: Int, touchMask: Int): Int = keyboardMask or touchMask
}
```

`app/src/main/java/com/tengen/gbaemu/GameSettings.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * Each game's own settings (Game settings… in its hold menu), by game id in an [IntStore] (the "game_settings"
 * preferences): its save type and clock, [AUTO] = what the app detects. Also what was detected the last time the
 * game ran without overrides, to show beside Auto.
 */
class GameSettings(private val store: IntStore) {
    data class Game(val saveType: Int = AUTO, val clock: Int = AUTO)

    fun of(id: String): Game = Game(
        store.get("$id.save")?.takeIf { it in SAVE_CHOICES } ?: AUTO,
        store.get("$id.clock")?.takeIf { it == CLOCK_OFF || it == CLOCK_ON } ?: AUTO,
    )

    fun set(id: String, game: Game) {
        store.put("$id.save", game.saveType.takeIf { it != AUTO })
        store.put("$id.clock", game.clock.takeIf { it != AUTO })
    }

    /** The detected save type and whether a clock was found, or null if the game has not run yet. */
    fun detected(id: String): Pair<Int, Boolean>? {
        val save = store.get("$id.detected_save") ?: return null
        return save to (store.get("$id.detected_clock") == 1)
    }

    /** After a game opened without overrides: what the app found ([hardware] = NativeBridge.HW_* bits). */
    fun remember(id: String, saveType: Int, hardware: Int) {
        store.put("$id.detected_save", saveType)
        store.put("$id.detected_clock", if (hardware and NativeBridge.HW_RTC != 0) 1 else 0)
    }

    companion object {
        const val AUTO = -1
        const val SAVE_NONE = 0 // the numbers of gba_save_type_t (core/include/gba.h)
        const val SAVE_UNKNOWN = 1
        const val SAVE_SRAM = 2
        const val SAVE_FLASH64 = 3
        const val SAVE_FLASH128 = 4
        const val SAVE_EEPROM = 5
        const val SAVE_EEPROM512 = 6
        const val SAVE_EEPROM8K = 7
        const val CLOCK_OFF = 0
        const val CLOCK_ON = 1

        /** The save types the user can pick (not "unknown" or "EEPROM, size not known"). */
        val SAVE_CHOICES = listOf(SAVE_NONE, SAVE_SRAM, SAVE_FLASH64, SAVE_FLASH128, SAVE_EEPROM512, SAVE_EEPROM8K)

        fun saveName(type: Int): String = when (type) {
            SAVE_NONE -> "None"
            SAVE_UNKNOWN -> "Not known yet"
            SAVE_SRAM -> "SRAM"
            SAVE_FLASH64 -> "Flash 64K"
            SAVE_FLASH128 -> "Flash 128K"
            SAVE_EEPROM -> "EEPROM"
            SAVE_EEPROM512 -> "EEPROM 512 B"
            SAVE_EEPROM8K -> "EEPROM 8K"
            else -> "Auto"
        }
    }
}
```

`app/src/main/java/com/tengen/gbaemu/Bios.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.util.zip.CRC32

/** A BIOS file the user chose, copied to `files/bios/gba_bios.bin` (Settings → BIOS). */
object Bios {
    const val SIZE = 16384
    private const val USUAL_CRC = 0x81977335L // the GBA BIOS every console has

    enum class Check { USUAL, UNUSUAL, WRONG_SIZE }

    fun check(bytes: ByteArray): Check = when {
        bytes.size != SIZE -> Check.WRONG_SIZE
        CRC32().apply { update(bytes) }.value == USUAL_CRC -> Check.USUAL
        else -> Check.UNUSUAL
    }

    fun file(filesDir: File) = File(filesDir, "bios/gba_bios.bin")

    /** Copies [bytes] in (safely: a temporary file, then swapped); false (nothing written) if not 16 KiB. */
    fun install(filesDir: File, bytes: ByteArray): Boolean {
        if (bytes.size != SIZE) return false
        return try {
            val f = file(filesDir)
            f.parentFile?.mkdirs()
            val tmp = File(f.path + ".tmp")
            tmp.writeBytes(bytes)
            tmp.renameTo(f) || (f.delete() && tmp.renameTo(f))
        } catch (e: Exception) {
            false
        }
    }

    /** The copied BIOS, or null (none, or not 16 KiB). */
    fun load(filesDir: File): ByteArray? = file(filesDir).takeIf { it.isFile }?.readBytes()?.takeIf { it.size == SIZE }

    fun remove(filesDir: File) {
        file(filesDir).delete()
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **69 tests** (54 + 5 + 4 + 4 + 2), 0 failures (`KeyMapTest` 6 still green: the defaults are in use).

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Store.kt app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/main/java/com/tengen/gbaemu/KeyBindings.kt app/src/main/java/com/tengen/gbaemu/KeyMap.kt app/src/main/java/com/tengen/gbaemu/GameSettings.kt app/src/main/java/com/tengen/gbaemu/Bios.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt app/src/test/java/com/tengen/gbaemu/KeyBindingsTest.kt app/src/test/java/com/tengen/gbaemu/GameSettingsTest.kt app/src/test/java/com/tengen/gbaemu/BiosTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6b-settings.md
git commit -m "feat(android): settings, key bindings, game settings and the BIOS file, stored" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done** — report briefly and carry on (no stop).

---
### Task 4: The settings on the C side (picture, sound, rewind, BIOS, overrides) and JNI

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/SettingsDeviceTest.kt`
- Modify: `platform/android/video_gl.h`, `platform/android/video_gl.c`, `platform/android/gl_readback.h`, `platform/android/gl_readback.c`, `platform/android/render_thread.h`, `platform/android/render_thread.c`, `platform/android/audio_out.h`, `platform/android/audio_out.c`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/SettingsDeviceTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The settings reaching the C side: the picture modes, sound on/off, rewind, a game's overrides, a BIOS file. */
@RunWith(AndroidJUnit4::class)
class SettingsDeviceTest {
    private val dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_settings")

    @After
    fun backToDefaults() {
        NativeBridge.stop()
        NativeBridge.setBios(null)
        NativeBridge.setRewindBudget(64L * 1024 * 1024)
        NativeBridge.setSound(true, 2)
        NativeBridge.setPicture(Settings.SIZE_FIT, false)
        dir.deleteRecursively()
    }

    private val black = 0xFF shl 24
    private val white = -1 // 0xFFFFFFFF

    /** Black and white columns: sharp keeps only those two, smooth mixes them at the edges. */
    private val stripes = ShortArray(240 * 160) { i -> if ((i % 240) % 2 == 0) 0x7FFF else 0 }

    @Test
    fun smoothPixelsMixNeighboursAndSharpOnesDoNot() {
        val sharp = NativeBridge.debugDrawFrameWith(stripes, 720, 480, Settings.SIZE_FIT, false)
        val smooth = NativeBridge.debugDrawFrameWith(stripes, 720, 480, Settings.SIZE_FIT, true)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", sharp)
        assertTrue("sharp: only black and white", sharp!!.all { it == black || it == white })
        assertTrue("smooth: greys between the columns", smooth!!.any { it != black && it != white })
    }

    @Test
    fun theSizeModesPlaceThePicture() {
        val allWhite = ShortArray(240 * 160) { 0x7FFF }
        val whole = NativeBridge.debugDrawFrameWith(allWhite, 1080, 720, Settings.SIZE_INTEGER, false)!!
        assertEquals("left of 4x (960 wide, from x 60)", black, whole[360 * 1080 + 59])
        assertEquals("inside 4x", white, whole[360 * 1080 + 60])
        assertEquals("above 4x (640 high, from y 40)", black, whole[39 * 1080 + 540])
        val stretch = NativeBridge.debugDrawFrameWith(allWhite, 700, 200, Settings.SIZE_STRETCH, false)!!
        assertTrue("stretch fills everything", stretch.all { it == white })
        val fit = NativeBridge.debugDrawFrameWith(allWhite, 700, 200, Settings.SIZE_FIT, false)!!
        assertEquals("fit leaves bars", black, fit[100 * 700 + 10])
    }

    @Test
    fun soundCanBeSwitchedOffAndOnWhileTheGameRuns() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/snd1_demo.gba")))
        NativeBridge.start()
        Thread.sleep(1000)
        assertEquals("sound at start", 1L, NativeBridge.debugAudioStats()[0])
        NativeBridge.setSound(false, 2)
        Thread.sleep(500)
        assertEquals("sound off", 0L, NativeBridge.debugAudioStats()[0])
        val frames = NativeBridge.debugFrameCount()
        Thread.sleep(500)
        assertTrue("the clock paces the game without sound", NativeBridge.debugFrameCount() > frames + 20)
        NativeBridge.setSound(true, 8)
        Thread.sleep(1000)
        assertEquals("sound on again", 1L, NativeBridge.debugAudioStats()[0])
    }

    @Test
    fun rewindOffKeepsNoSnapshots() {
        NativeBridge.setRewindBudget(0)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        assertEquals(0L, NativeBridge.debugRewindStats()[0])
        NativeBridge.setRewindBudget(64L * 1024 * 1024)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        assertTrue("rewind on again for the next game", NativeBridge.debugRewindStats()[0] > 0)
    }

    @Test
    fun aGamesOverridesApplyToTheNextLoadOnly() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val detected = NativeBridge.cartSetup()
        assertEquals("the demo has no clock", 0, detected[1] and NativeBridge.HW_RTC)
        NativeBridge.setGameOverrides(GameSettings.SAVE_SRAM, GameSettings.CLOCK_ON)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val forced = NativeBridge.cartSetup()
        assertEquals(GameSettings.SAVE_SRAM, forced[0])
        assertEquals(NativeBridge.HW_RTC, forced[1] and NativeBridge.HW_RTC)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals("used once", detected.toList(), NativeBridge.cartSetup().toList())
    }

    @Test
    fun aBiosFileIsUsedAndItsStatesDoNotLoadWithTheBuiltInOne() {
        assertEquals(NativeBridge.ERR_BIOS_SIZE, NativeBridge.setBios(ByteArray(100)))
        // 16 KiB of "mov pc, #0x08000000": a BIOS that starts the cartridge at once
        val bios = ByteArray(Bios.SIZE) { i -> byteArrayOf(0x02, 0xF3.toByte(), 0xA0.toByte(), 0xE3.toByte())[i % 4] }
        assertEquals(0, NativeBridge.setBios(bios))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        dir.mkdirs()
        val withFile = File(dir, "file.state").path
        assertTrue(NativeBridge.saveState(withFile))
        NativeBridge.setBios(null)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals("a BIOS-file state into the built-in BIOS", NativeBridge.ERR_STATE, NativeBridge.loadState(withFile))
        val builtIn = File(dir, "builtin.state").path
        assertTrue(NativeBridge.saveState(builtIn))
        assertEquals(0, NativeBridge.loadState(builtIn))
        assertEquals(0, NativeBridge.setBios(bios))
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertNotEquals("a built-in state into a BIOS file", 0, NativeBridge.loadState(builtIn))
        assertEquals(0, NativeBridge.loadState(withFile))
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: fails to compile — `Unresolved reference 'setBios'`, `'setRewindBudget'`, `'setSound'`, `'setPicture'`, `'debugDrawFrameWith'`, `'cartSetup'`, `'setGameOverrides'`, `'ERR_BIOS_SIZE'`.

- [x] **Step 3: Write the code**

Replace `platform/android/video_gl.h`:

```c
#ifndef GBAEMU_VIDEO_GL_H
#define GBAEMU_VIDEO_GL_H

#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdint.h>

/* Draws 240x160 BGR555 frames with OpenGL ES 3.0: one integer texture, one quad, a shader that
 * unpacks the 15-bit colour (sharp: the nearest texel; smooth: the 4 nearest mixed). Every call needs the
 * same current GLES 3 context. */
typedef struct {
    GLuint program, texture, vao;
    GLint smooth_loc;
} video_gl_t;

bool video_gl_init(video_gl_t *v);                          /* false if the shader will not build */
void video_gl_upload(video_gl_t *v, const uint16_t *pixels); /* a new frame */
/* Black surface + the frame, placed by fit_rect_mode(fit_mode: fit_mode_t), sharp or smooth. */
void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth);
void video_gl_destroy(video_gl_t *v);

#endif
```

Replace `platform/android/video_gl.c`:

```c
#include "video_gl.h"

#include <android/log.h>

#include "fit_rect.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

/* A quad from gl_VertexID (triangle strip, no vertex buffer); uv (0,0) is the bottom left. */
static const char *VERTEX_SRC =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Row 0 of the frame at the top; each 5-bit channel c becomes (c << 3) | (c >> 2). Sharp: the nearest texel.
 * Smooth: the 4 texels around the point mixed by distance (bilinear; an integer texture can't filter itself). */
static const char *FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp usampler2D;\n"
    "uniform usampler2D u_frame;\n"
    "uniform int u_smooth;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec3 texel(ivec2 t) {\n"
    "    uint p = texelFetch(u_frame, clamp(t, ivec2(0), ivec2(239, 159)), 0).r;\n"
    "    uvec3 c = uvec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u);\n"
    "    return vec3((c << 3) | (c >> 2)) / 255.0;\n"
    "}\n"
    "void main() {\n"
    "    vec2 s = vec2(v_uv.x * 240.0, (1.0 - v_uv.y) * 160.0);\n"
    "    if (u_smooth == 0) {\n"
    "        o_color = vec4(texel(ivec2(min(s.x, 239.0), min(s.y, 159.0))), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 p = s - 0.5;\n"
    "    ivec2 i = ivec2(floor(p));\n"
    "    vec2 f = p - floor(p);\n"
    "    vec3 top = mix(texel(i), texel(i + ivec2(1, 0)), f.x);\n"
    "    vec3 bottom = mix(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x);\n"
    "    o_color = vec4(mix(top, bottom, f.y), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool video_gl_init(video_gl_t *v) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SRC), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    if (!vs || !fs) return false;
    v->program = glCreateProgram();
    glAttachShader(v->program, vs);
    glAttachShader(v->program, fs);
    glLinkProgram(v->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(v->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("shader program did not link");
        return false;
    }
    glUseProgram(v->program);
    glUniform1i(glGetUniformLocation(v->program, "u_frame"), 0);
    v->smooth_loc = glGetUniformLocation(v->program, "u_smooth");

    glGenTextures(1, &v->texture);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 240, 160);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* integer textures need NEAREST */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &v->vao);
    return glGetError() == GL_NO_ERROR;
}

void video_gl_upload(video_gl_t *v, const uint16_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
}

void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth) {
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fit_rect_t r = fit_rect_mode(width, height, (fit_mode_t)fit_mode);
    if (r.w == 0) return;
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(v->program);
    glUniform1i(v->smooth_loc, smooth ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void video_gl_destroy(video_gl_t *v) {
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->program) glDeleteProgram(v->program);
    v->vao = v->texture = v->program = 0;
}
```

Replace `platform/android/gl_readback.h`:

```c
#ifndef GBAEMU_GL_READBACK_H
#define GBAEMU_GL_READBACK_H

#include <stdbool.h>
#include <stdint.h>

/* Test helper: draws one 240x160 BGR555 frame with the real video_gl code (fit_mode, smooth: as video_gl_draw) into an offscreen
 * width x height surface on the calling thread and reads it back, top row first, one
 * little-endian RGBA pixel per uint32_t (r | g << 8 | b << 16 | a << 24). */
bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t *out);

#endif
```

Replace `platform/android/gl_readback.c`:

```c
#include "gl_readback.h"

#include <string.h>

#include "egl_ctx.h"
#include "video_gl.h"

bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t *out) {
    egl_ctx_t e;
    if (!egl_ctx_init(&e, true)) return false;
    const EGLint pb_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(e.display, e.config, pb_attrs);
    bool ok = s != EGL_NO_SURFACE && eglMakeCurrent(e.display, s, s, e.context);
    video_gl_t v = {0, 0, 0, 0};
    if (ok) ok = video_gl_init(&v);
    if (ok) {
        video_gl_upload(&v, pixels);
        video_gl_draw(&v, width, height, fit_mode, smooth);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
        ok = glGetError() == GL_NO_ERROR;
        for (int y = 0; y < height / 2; y++) { /* GL rows start at the bottom */
            uint32_t *a = out + (size_t)y * width, *b = out + (size_t)(height - 1 - y) * width;
            for (int x = 0; x < width; x++) {
                uint32_t t = a[x];
                a[x] = b[x];
                b[x] = t;
            }
        }
        video_gl_destroy(&v);
    }
    if (s != EGL_NO_SURFACE) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(e.display, s);
    }
    egl_ctx_destroy(&e);
    return ok;
}
```

Replace `platform/android/render_thread.h`:

```c
#ifndef GBAEMU_RENDER_THREAD_H
#define GBAEMU_RENDER_THREAD_H

#include <android/native_window.h>
#include <stdbool.h>

#include "triple_buffer.h"

/* The thread that owns OpenGL: once per display refresh (AChoreographer) it draws the newest
 * frame from the triple buffer into the current window. */
typedef struct render_thread render_thread_t;

render_thread_t *render_create(triple_buffer_t *tb);
/* The window appeared, changed (w) or went away (NULL). Returns only after the render thread has
 * switched over (and let go of the old window); false if OpenGL ES 3 could not draw into w. */
bool render_set_window(render_thread_t *r, ANativeWindow *w);
/* The picture settings (fit_mode: fit_mode_t; smooth pixels): used from the next frame on, from any thread. */
void render_set_picture(render_thread_t *r, int fit_mode, bool smooth);
void render_destroy(render_thread_t *r);

#endif
```

Replace `platform/android/render_thread.c`:

```c
#include "render_thread.h"

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "egl_ctx.h"
#include "video_gl.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

struct render_thread {
    pthread_t thread;
    triple_buffer_t *tb;
    int pipe_fd[2]; /* messages to the thread: 'W' window changed, 'Q' quit */

    pthread_mutex_t lock; /* guards pending/requested/handled/last_ok */
    pthread_cond_t done;
    ANativeWindow *pending;
    atomic_int fit_mode; /* the picture settings, set from any thread */
    atomic_bool smooth;
    unsigned requested, handled;
    bool last_ok;

    /* Only the render thread touches these. */
    AChoreographer *choreographer;
    egl_ctx_t egl;
    bool egl_ready, gl_ready, frame_posted, quit;
    EGLSurface surface;
    ANativeWindow *window;
    video_gl_t gl;
};

static void post_frame(render_thread_t *r);

static void drop_surface(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->window) ANativeWindow_release(r->window);
    r->window = NULL;
}

/* Tear down the whole context (it was lost, or the thread is ending); GL objects go with it. */
static void drop_context(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->egl_ready) egl_ctx_destroy(&r->egl);
    r->egl_ready = r->gl_ready = false;
}

/* Surface for r->window; context and GL objects made on first use. */
static bool make_surface(render_thread_t *r) {
    if (!r->egl_ready) {
        r->egl_ready = egl_ctx_init(&r->egl, false);
        if (!r->egl_ready) return false;
    }
    r->surface = eglCreateWindowSurface(r->egl.display, r->egl.config, r->window, NULL);
    if (r->surface == EGL_NO_SURFACE || !eglMakeCurrent(r->egl.display, r->surface, r->surface, r->egl.context)) {
        LOGE("window surface failed: 0x%x", eglGetError());
        return false;
    }
    if (!r->gl_ready) {
        r->gl_ready = video_gl_init(&r->gl);
        if (!r->gl_ready) return false;
        bool fresh;
        video_gl_upload(&r->gl, tb_take(r->tb, &fresh)); /* show the newest frame straight away */
    }
    post_frame(r);
    return true;
}

static bool apply_window(render_thread_t *r, ANativeWindow *w) {
    if (w && w == r->window) { /* same window, new size: drawing reads the size every frame */
        ANativeWindow_release(w);
        return r->gl_ready && r->surface != EGL_NO_SURFACE;
    }
    drop_surface(r);
    if (!w) return true;
    r->window = w;
    return make_surface(r);
}

static void on_frame(render_thread_t *r) {
    r->frame_posted = false;
    if (r->quit || r->surface == EGL_NO_SURFACE || !r->gl_ready) return;
    bool fresh;
    const uint16_t *frame = tb_take(r->tb, &fresh);
    if (fresh) video_gl_upload(&r->gl, frame);
    EGLint w = 0, h = 0;
    eglQuerySurface(r->egl.display, r->surface, EGL_WIDTH, &w);
    eglQuerySurface(r->egl.display, r->surface, EGL_HEIGHT, &h);
    video_gl_draw(&r->gl, w, h, atomic_load(&r->fit_mode), atomic_load(&r->smooth));
    if (!eglSwapBuffers(r->egl.display, r->surface) && eglGetError() == EGL_CONTEXT_LOST) {
        LOGE("EGL context lost: rebuilding");
        drop_context(r);
        if (!make_surface(r)) return;
    }
    post_frame(r);
}

static void frame_cb64(int64_t frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void frame_cb(long frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void post_frame(render_thread_t *r) {
    if (r->frame_posted) return;
    r->frame_posted = true;
    if (__builtin_available(android 29, *)) {
        AChoreographer_postFrameCallback64(r->choreographer, frame_cb64, r);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        AChoreographer_postFrameCallback(r->choreographer, frame_cb, r);
#pragma clang diagnostic pop
    }
}

static int on_message(int fd, int events, void *data) {
    (void)events;
    render_thread_t *r = data;
    char msg;
    if (read(fd, &msg, 1) != 1) return 1;
    if (msg == 'Q') {
        r->quit = true;
        return 1;
    }
    pthread_mutex_lock(&r->lock);
    ANativeWindow *w = r->pending;
    r->pending = NULL;
    unsigned seq = r->requested;
    pthread_mutex_unlock(&r->lock);
    bool ok = apply_window(r, w);
    pthread_mutex_lock(&r->lock);
    r->handled = seq;
    r->last_ok = ok;
    pthread_cond_broadcast(&r->done);
    pthread_mutex_unlock(&r->lock);
    return 1;
}

static void *render_main(void *arg) {
    render_thread_t *r = arg;
    ALooper *looper = ALooper_prepare(0);
    ALooper_addFd(looper, r->pipe_fd[0], 1, ALOOPER_EVENT_INPUT, on_message, r);
    r->choreographer = AChoreographer_getInstance();
    while (!r->quit) ALooper_pollOnce(-1, NULL, NULL, NULL);
    ALooper_removeFd(looper, r->pipe_fd[0]);
    drop_surface(r);
    drop_context(r);
    return NULL;
}

render_thread_t *render_create(triple_buffer_t *tb) {
    render_thread_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->tb = tb;
    r->surface = EGL_NO_SURFACE;
    if (pipe(r->pipe_fd) != 0) {
        free(r);
        return NULL;
    }
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->done, NULL);
    pthread_create(&r->thread, NULL, render_main, r);
    return r;
}

bool render_set_window(render_thread_t *r, ANativeWindow *w) {
    if (w) ANativeWindow_acquire(w); /* the render thread owns this reference now */
    pthread_mutex_lock(&r->lock);
    r->pending = w;
    unsigned seq = ++r->requested;
    pthread_mutex_unlock(&r->lock);
    char msg = 'W';
    write(r->pipe_fd[1], &msg, 1);
    pthread_mutex_lock(&r->lock);
    while (r->handled != seq) pthread_cond_wait(&r->done, &r->lock);
    bool ok = r->last_ok;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

void render_set_picture(render_thread_t *r, int fit_mode, bool smooth) {
    atomic_store(&r->fit_mode, fit_mode);
    atomic_store(&r->smooth, smooth);
}

void render_destroy(render_thread_t *r) {
    if (!r) return;
    char msg = 'Q';
    write(r->pipe_fd[1], &msg, 1);
    pthread_join(r->thread, NULL);
    close(r->pipe_fd[0]);
    close(r->pipe_fd[1]);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->done);
    free(r);
}
```

Replace `platform/android/audio_out.h`:

```c
#ifndef GBAEMU_AUDIO_OUT_H
#define GBAEMU_AUDIO_OUT_H

#include <stdatomic.h>
#include <stdbool.h>

#include "audio_ring.h"

/* Counters the AAudio callback keeps (whole session, across reopens). */
typedef struct {
    atomic_llong played; /* frames handed to AAudio */
    atomic_llong silent; /* of those, filled with silence because the ring ran short */
    atomic_llong loud;   /* of those, frames with any non-zero sample */
} audio_stats_t;

/* One AAudio output stream (shared, low latency, stereo int16, the device's own rate) that plays
 * from `ring`. All calls except the callbacks come from the emulation thread. */
typedef struct audio_out audio_out_t;

/* NULL if AAudio cannot open. bursts: AAudio's own buffer in bursts (2 = the least delay; 4, 8 = fewer crackles). */
audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats, int bursts);
bool audio_out_start(audio_out_t *a);
void audio_out_pause(audio_out_t *a); /* returns once the callback has stopped (or after 200 ms) */
bool audio_out_disconnected(audio_out_t *a); /* the error callback reported a lost device */
int audio_out_rate(const audio_out_t *a);
int audio_out_burst(const audio_out_t *a);
void audio_out_close(audio_out_t *a);

#endif
```

Replace `platform/android/audio_out.c`:

```c
#include "audio_out.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "gbaemu", __VA_ARGS__)

struct audio_out {
    AAudioStream *stream;
    audio_ring_t *ring;
    audio_stats_t *stats;
    atomic_bool disconnected;
    int rate, burst;
};

/* AAudio's high-priority thread: copy from the ring, silence for what is missing. No locks, no allocation. */
static aaudio_data_callback_result_t on_data(AAudioStream *stream, void *user, void *audio, int32_t frames) {
    (void)stream;
    audio_out_t *a = user;
    int16_t *out = audio;
    size_t got = audio_ring_pop(a->ring, out, (size_t)frames);
    if (got < (size_t)frames) memset(out + got * 2, 0, ((size_t)frames - got) * 4);
    long long loud = 0;
    for (size_t i = 0; i < got; i++) loud += (out[i * 2] | out[i * 2 + 1]) != 0;
    atomic_fetch_add_explicit(&a->stats->played, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->silent, (long long)((size_t)frames - got), memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->loud, loud, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Called on its own thread when the stream breaks (e.g. headphones unplugged). Reopening is not
 * allowed here: only raise the flag; the emulation thread reopens at the next frame. */
static void on_error(AAudioStream *stream, void *user, aaudio_result_t error) {
    (void)stream;
    (void)error;
    audio_out_t *a = user;
    atomic_store(&a->disconnected, true);
}

audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats, int bursts) {
    audio_out_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->ring = ring;
    a->stats = stats;
    atomic_init(&a->disconnected, false);
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t res = AAudio_createStreamBuilder(&b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio builder: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (__builtin_available(android 28, *)) {
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    }
    AAudioStreamBuilder_setDataCallback(b, on_data, a);
    AAudioStreamBuilder_setErrorCallback(b, on_error, a);
    res = AAudioStreamBuilder_openStream(b, &a->stream);
    AAudioStreamBuilder_delete(b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio open: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    a->rate = AAudioStream_getSampleRate(a->stream);
    a->burst = AAudioStream_getFramesPerBurst(a->stream);
    /* Two bursts of AAudio's own buffering is enough (Google's advice; the phone emulator's default was
     * 4,360 frames = 91 ms of extra delay); more (Settings → Sound → Delay) for a phone that crackles. */
    AAudioStream_setBufferSizeInFrames(a->stream, a->burst * (bursts >= 2 ? bursts : 2));
    LOGI("AAudio stream: %d Hz, burst %d frames, buffer %d frames", a->rate, a->burst,
         AAudioStream_getBufferSizeInFrames(a->stream));
    return a;
}

bool audio_out_start(audio_out_t *a) {
    aaudio_result_t res = AAudioStream_requestStart(a->stream);
    if (res != AAUDIO_OK) LOGE("AAudio start: %s", AAudio_convertResultToText(res));
    return res == AAUDIO_OK;
}

void audio_out_pause(audio_out_t *a) {
    if (AAudioStream_requestPause(a->stream) != AAUDIO_OK) return;
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(a->stream, AAUDIO_STREAM_STATE_PAUSING, &next, 200 * 1000000LL);
}

bool audio_out_disconnected(audio_out_t *a) {
    return atomic_load(&a->disconnected);
}

int audio_out_rate(const audio_out_t *a) {
    return a->rate;
}

int audio_out_burst(const audio_out_t *a) {
    return a->burst;
}

void audio_out_close(audio_out_t *a) {
    if (!a) return;
    AAudioStream_requestStop(a->stream);
    AAudioStream_close(a->stream);
    free(a);
}
```

Replace `platform/android/emu_thread.h`:

```c
#ifndef GBAEMU_EMU_THREAD_H
#define GBAEMU_EMU_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "triple_buffer.h"

/* One running game: the core, its frames, its sound, and the thread that runs it — paced by the sound
 * (AAudio), or at 59.7275 frames/s by the clock when there is no sound. Once started, only the
 * emulation thread touches the core and the sound stream. */
typedef struct emu emu_t;

typedef enum {
    EMU_SAVE_NONE,   /* no save chip (or one the game has not shown yet): nothing to offer */
    EMU_SAVE_LOADED, /* the save file was loaded */
    EMU_SAVE_EMPTY,  /* a known save chip and no save file yet: offer an import */
    EMU_SAVE_MISFIT, /* a save file whose size does not fit the chip: not loaded, left untouched */
} emu_save_status_t;

/* How a game is set up (emu_create); emu_options_default() = the built-in BIOS, no overrides, rewind as in 5b,
 * sound on with 2 bursts. */
typedef struct {
    const uint8_t *bios;  /* a 16 KiB BIOS file's bytes (copied), or NULL: the built-in BIOS */
    size_t bios_size;
    int save_type;        /* EMU_AUTO or a gba_save_type_t forced before the save file is read */
    int clock;            /* EMU_AUTO, 0 = no clock, 1 = a clock (the cartridge's other extras stay) */
    size_t rewind_budget; /* bytes for rewind snapshots; 0 = rewind off */
    bool sound;           /* false: no stream, the clock paces the game */
    int sound_bursts;     /* AAudio's buffer in bursts: 2, 4 or 8 */
} emu_options_t;
#define EMU_AUTO (-1)
#define EMU_REWIND_DEFAULT (64u * 1024 * 1024) /* ~2.5 minutes of snapshots in game A (design: 5b) */
emu_options_t emu_options_default(void);

/* Creates the core, loads the BIOS (if any) and the ROM (copied), forces the save type and clock (if asked),
 * sets the clock from the phone and loads the save file at save_path if there is one (save_path NULL: no save
 * file); NULL + *err on failure (GBA_ERR_BIOS_SIZE for a BIOS not 16 KiB). From then on the game's saves are
 * written to save_path about 1 s after the game saves, and on pause and stop. opt NULL = the defaults. */
emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, const emu_options_t *opt, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e); /* returns once the thread has stopped and written the save (at most 1 s) */
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */

#define EMU_FF_MAX (-1) /* fast-forward with no pacing and no sound */
/* Fast-forward (any thread; from the next loop step): 0 or 1 = normal; 2, 4, 8 = that many frames per
 * normal frame, the sound sped up to match (higher pitch); EMU_FF_MAX = as fast as it goes, silent. */
void emu_set_fast_forward(emu_t *e, int speed);
/* Rewind (any thread): while on, the game steps back one snapshot (4 frames) per 1/60 s, silent, keys
 * ignored; it stays on the oldest snapshot when there is none older. It wins over fast-forward. When it
 * goes off, play goes on from there and the save file is rewritten ~1 s later (a rewind may undo an
 * in-game save). The history is kept from the game's start (or its last loaded state), as long as the options'
 * rewind budget allows (~2.5 minutes in game A with the default). */
void emu_set_rewinding(emu_t *e, bool on);
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started), writes the save, frees everything */

/* Cartridge sensors (any thread; applied at the next frame start): tilt and turn ±32767, light 0..255. */
void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn);
void emu_set_light(emu_t *e, int level);
uint32_t emu_cart_hardware(const emu_t *e); /* GBA_HW_* extras of the loaded cartridge */
gba_save_type_t emu_save_type(const emu_t *e); /* the save chip (detected or forced) */
bool emu_rumble_active(emu_t *e);           /* the rumble motor after the last frame */
const char *emu_rom_title(const emu_t *e);
emu_save_status_t emu_save_status(const emu_t *e);
/* Before emu_start only: puts a save file's bytes in and writes them to the save file at once.
 * GBA_ERR_SAVE_SIZE (nothing changes) if they do not fit the chip. */
gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size);
bool emu_save_error(emu_t *e); /* true once after a failed save-file write */

/* Save states — only while the game is paused (emu_pause has returned) or not started yet; otherwise
 * refused. Saving writes the whole machine to `path` (safely: a temporary file, then swapped in). */
bool emu_state_save(emu_t *e, const char *path);
/* GBA_ERR_STATE: not paused, unreadable, or not a state of this game and build (the game is unchanged).
 * After a load the game's save file is rewritten from the loaded save chip about 1 s after resuming. */
gba_err_t emu_state_load(emu_t *e, const char *path);
/* The switched-on cheats (gba_cheats_set) — only while paused or not started; GBA_ERR_CHEAT when running or when
 * a text does not read (then no cheats are on). */
gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const gba_cheat_type_t *types, size_t count);
/* The current 240×160 BGR555 picture (for a state's thumbnail); same rule: only while paused. */
bool emu_frame(emu_t *e, uint16_t out[240 * 160]);

/* Sound on/off and its delay (any thread): the emulation thread opens, closes or reopens the stream between
 * frames; without a stream the clock paces the game. */
void emu_set_sound(emu_t *e, bool on, int bursts);

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
void emu_simulate_audio_disconnect(emu_t *e);        /* as if the sound device went away */
void emu_simulate_stuck_stream(emu_t *e);            /* the stream stops taking sound, unannounced */
unsigned emu_keys_seen(emu_t *e);
/* A word of the machine's memory, only while paused or not started (else 0xDEADDEAD). */
uint32_t emu_debug_peek32(emu_t *e, uint32_t addr);
void emu_motion_seen(emu_t *e, int32_t out[4]); /* the sensor values last applied: tilt x, tilt y, turn, light */
/* The core's save bytes (read them only while paused or before start); returns their size. */
size_t emu_debug_saveram(emu_t *e, const uint8_t **data); /* buttons the game was given in any frame since the last call */
/* active (1/0), sample rate, frames per burst, frames played, frames silence-filled, loud frames, reopens */
void emu_audio_stats(emu_t *e, int64_t out[7]);
/* Rewind: snapshots kept, their packed bytes, steps back taken since start. */
void emu_rewind_stats(emu_t *e, int64_t out[3]);

#endif
```

Replace `platform/android/emu_thread.c`:

```c
#include "emu_thread.h"

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio_out.h"
#include "audio_ring.h"
#include "frame_pacer.h"
#include "gba_debug.h"
#include "key_latch.h"
#include "rate_control.h"
#include "resampler.h"
#include "save_file.h"
#include "save_timer.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "gbaemu", __VA_ARGS__)

#define AUDIO_TARGET_MS 30        /* the sound buffer is kept at about this much */
#define WATCHDOG_NS 250000000LL   /* buffer not draining this long while playing → the stream is stuck */
#define WATCHDOG_GRACE_NS 2000000000LL /* ...but not in the first 2 s after a start/resume: a waking sound
                                        * device (seen: 350 ms on the phone emulator) takes a while */
#define CONVERTED_FRAMES 8192     /* room for 1,024 GBA frames converted at up to ~8x */
#define REWIND_EVERY 4            /* frames between snapshots; rewinding steps back one per 1/60 s */

struct emu {
    gba_t *g;
    triple_buffer_t tb;
    pthread_t thread;
    bool started;
    pthread_mutex_t lock; /* guards paused/quit */
    pthread_cond_t wake;
    bool paused, quit;
    key_latch_t keys;
    atomic_ullong frames;
    atomic_uint keys_seen; /* test hook */
    atomic_int tilt_x, tilt_y, turn, light; /* cartridge sensors, from the UI thread; applied at frame start */
    atomic_int seen[4];                     /* test hook: the sensor values last applied (x, y, turn, light) */
    atomic_bool rumble;                     /* the cartridge's rumble motor after the last frame */
    atomic_int ff_speed;                    /* 0/1 normal, 2/4/8, EMU_FF_MAX (from the UI thread) */
    atomic_bool rewinding;                  /* from the UI thread */
    atomic_llong rw_snaps, rw_bytes, rw_steps; /* rewind stats (test hook), kept by the emulation thread */
    bool parked;           /* the thread is stopped in a pause with the save written (guarded by lock) */

    /* Save file. Before start only the caller touches these; after start, only the emulation thread
     * (save_failed is shared). */
    char *save_path;                /* NULL: no save file */
    save_timer_t save_timer;
    emu_save_status_t save_status;
    atomic_bool save_failed;        /* a write failed since the last emu_save_error */

    /* Sound. The emulation thread alone touches out/rs/converted/sound_applied/bursts_applied; the rest is shared
     * as marked. */
    atomic_bool sound_wanted;          /* Settings: sound on (any thread) */
    atomic_int sound_bursts;           /* Settings: AAudio's buffer in bursts (any thread) */
    bool sound_applied;                /* what the emulation thread last acted on: a stream that gave up stays */
    int bursts_applied;                /* closed until the wish changes */
    audio_out_t *out;                  /* NULL: no sound, the clock paces the game */
    audio_ring_t ring;                 /* emulation thread → AAudio callback */
    resampler_t rs;
    int16_t converted[CONVERTED_FRAMES * 2];
    audio_stats_t stats;               /* kept by the callback */
    atomic_bool sound_on;
    atomic_int rate, burst, reopens;
    atomic_bool fake_disconnect;       /* test hook */
    atomic_bool fake_stuck;            /* test hook */
    bool watchdog_used;
    int64_t sound_started_ns;          /* last start/resume of the stream */
};

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static bool paused_or_quitting(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    bool stop = e->paused || e->quit;
    pthread_mutex_unlock(&e->lock);
    return stop;
}

static void sound_open(emu_t *e) {
    e->out = audio_out_open(&e->ring, &e->stats, e->bursts_applied);
    if (e->out && !audio_out_start(e->out)) {
        audio_out_close(e->out);
        e->out = NULL;
    }
    e->sound_started_ns = now_ns();
    atomic_store(&e->rate, e->out ? audio_out_rate(e->out) : 0);
    atomic_store(&e->burst, e->out ? audio_out_burst(e->out) : 0);
    atomic_store(&e->sound_on, e->out != NULL);
    if (!e->out) LOGW("no sound: the clock paces the game");
}

static void sound_close(emu_t *e) {
    audio_out_close(e->out);
    e->out = NULL;
    atomic_store(&e->sound_on, false);
}

/* Close and open again (device change, stuck stream). The ring is emptied while nothing reads it. */
static void sound_reopen(emu_t *e) {
    sound_close(e);
    audio_ring_clear(&e->ring);
    resampler_init(&e->rs);
    atomic_fetch_add(&e->reopens, 1);
    sound_open(e);
}

/* Settings changed (sound on/off, delay): act once per change. */
static void apply_sound_settings(emu_t *e) {
    bool want = atomic_load(&e->sound_wanted);
    int bursts = atomic_load(&e->sound_bursts);
    if (want == e->sound_applied && bursts == e->bursts_applied) return;
    e->sound_applied = want;
    e->bursts_applied = bursts;
    if (e->out) {
        sound_close(e);
        audio_ring_clear(&e->ring);
        resampler_init(&e->rs);
    }
    if (want) sound_open(e);
}

static size_t target_frames(emu_t *e) {
    return (size_t)audio_out_rate(e->out) * AUDIO_TARGET_MS / 1000;
}

/* Every sample of the frame: into the resampler, then into the ring (or dropped without sound, and at
 * EMU_FF_MAX). At N× speed N times fewer samples come out: the sound plays N times faster and higher. */
static void feed_sound(emu_t *e, int speed) {
    int16_t in[1024 * 2];
    size_t n;
    while ((n = gba_audio_read(e->g, in, 1024)) > 0) {
        if (!e->out || speed == EMU_FF_MAX) continue;
        double fill = (double)audio_ring_queued(&e->ring) / (2.0 * (double)target_frames(e));
        double ratio = rate_control_ratio(audio_out_rate(e->out), fill) / (speed >= 2 ? speed : 1);
        size_t m = resampler_run(&e->rs, in, n, ratio, e->converted, CONVERTED_FRAMES);
        audio_ring_push(&e->ring, e->converted, m);
    }
}

/* Sound pacing: wait (1 ms at a time) while the ring holds more than the target. A ring that stops
 * draining for 250 ms means a stuck stream: reopen once; the second time, give up on sound. */
static void sound_pace(emu_t *e) {
    size_t target = target_frames(e);
    size_t last = audio_ring_queued(&e->ring);
    int64_t since = now_ns();
    for (;;) {
        size_t q = audio_ring_queued(&e->ring);
        if (q <= target || paused_or_quitting(e)) return;
        int64_t now = now_ns();
        if (q < last || now - e->sound_started_ns < WATCHDOG_GRACE_NS) {
            last = q;
            since = now;
        } else if (now - since > WATCHDOG_NS) {
            LOGW("sound stream stuck: %s", e->watchdog_used ? "giving up on sound" : "reopening it");
            if (e->watchdog_used) {
                sound_close(e);
            } else {
                e->watchdog_used = true;
                sound_reopen(e);
            }
            return;
        }
        struct timespec ms = {0, 1000000};
        nanosleep(&ms, NULL);
    }
}

/* The sensor values from the UI thread into the core (cheap: a few stores; harmless for carts without them). */
static void apply_sensors(emu_t *e) {
    int x = atomic_load(&e->tilt_x), y = atomic_load(&e->tilt_y);
    int turn = atomic_load(&e->turn), light = atomic_load(&e->light);
    gba_set_tilt(e->g, (int16_t)x, (int16_t)y);
    gba_set_gyro(e->g, (int16_t)turn);
    gba_set_light(e->g, (uint8_t)light);
    atomic_store(&e->seen[0], x);
    atomic_store(&e->seen[1], y);
    atomic_store(&e->seen[2], turn);
    atomic_store(&e->seen[3], light);
}

/* The core's save bytes into the save file (nothing without a path or a chip of known size). */
static void write_save(emu_t *e) {
    size_t size = gba_saveram_size(e->g);
    if (!e->save_path || !size) return;
    if (!save_file_write(e->save_path, gba_saveram(e->g), size)) {
        LOGW("cannot write the save file %s", e->save_path);
        atomic_store(&e->save_failed, true);
    }
}

/* Pause, stop: write now if the game saved since the last write. */
static void flush_save(emu_t *e) {
    if (gba_saveram_dirty(e->g)) save_timer_frame(&e->save_timer, true);
    if (save_timer_flush(&e->save_timer)) write_save(e);
}

/* Wait for the clock's next frame time. */
static void clock_wait(frame_pacer_t *pacer) {
    int64_t due = pacer_next(pacer, now_ns());
    struct timespec ts = {(time_t)(due / 1000000000), (long)(due % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
    }
}

static void keep_rewind_stats(emu_t *e) {
    size_t snaps, bytes;
    gba_debug_rewind_stats(e->g, &snaps, &bytes);
    atomic_store(&e->rw_snaps, (long long)snaps);
    atomic_store(&e->rw_bytes, (long long)bytes);
}

/* One frame of play: keys, sensors, the frame, its picture and sound, the save timer. */
static void play_frame(emu_t *e, int speed) {
    unsigned keys = key_latch_take(&e->keys);
    atomic_fetch_or(&e->keys_seen, keys);
    gba_set_keys(e->g, (uint16_t)keys);
    apply_sensors(e);
    gba_run_frame(e->g);
    atomic_store(&e->rumble, gba_rumble_active(e->g) != 0);
    memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
    tb_publish(&e->tb);
    feed_sound(e, speed);
    if (save_timer_frame(&e->save_timer, gba_saveram_dirty(e->g))) write_save(e);
    atomic_fetch_add(&e->frames, 1);
}

/* One step back while rewinding: the snapshot's picture at once; its unread sound is dropped (silence). */
static void rewind_step(emu_t *e) {
    if (gba_rewind_step_back(e->g)) atomic_fetch_add(&e->rw_steps, 1);
    int16_t drop[1024 * 2];
    while (gba_audio_read(e->g, drop, 1024) > 0) {
    }
    atomic_store(&e->rumble, false);
    memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
    tb_publish(&e->tb);
}

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    bool was_rewinding = false;
    e->sound_applied = atomic_load(&e->sound_wanted);
    e->bursts_applied = atomic_load(&e->sound_bursts);
    if (e->sound_applied) sound_open(e);
    for (;;) {
        pthread_mutex_lock(&e->lock);
        bool pausing = e->paused && !e->quit;
        pthread_mutex_unlock(&e->lock);
        if (pausing) {
            if (e->out) { /* stop the callback, then drop the stale sound */
                audio_out_pause(e->out);
                audio_ring_clear(&e->ring);
                resampler_init(&e->rs);
            }
            flush_save(e); /* outside the lock, so emu_pause's 1 s limit keeps working */
            pthread_mutex_lock(&e->lock);
            e->parked = true;
            pthread_cond_broadcast(&e->wake);
            while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);
            e->parked = false;
            bool resuming = !e->quit;
            pthread_mutex_unlock(&e->lock);
            if (e->out && resuming) {
                audio_out_start(e->out);
                e->sound_started_ns = now_ns();
            }
            pacer_reset(&pacer);
        }
        pthread_mutex_lock(&e->lock);
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        if (atomic_load(&e->rewinding)) { /* the sound ring simply runs dry: silence */
            rewind_step(e);
            keep_rewind_stats(e);
            was_rewinding = true;
            clock_wait(&pacer);
            continue;
        }
        if (was_rewinding) {
            was_rewinding = false;
            save_timer_frame(&e->save_timer, true); /* the save chip may have gone back: rewrite the file */
            pacer_reset(&pacer);
        }
        apply_sound_settings(e);
        int speed = atomic_load(&e->ff_speed);
        for (int i = 0, n = speed >= 2 ? speed : 1; i < n; i++) play_frame(e, speed);
        keep_rewind_stats(e);

        if (e->out && atomic_exchange(&e->fake_stuck, false)) audio_out_pause(e->out); /* stops draining, unannounced */
        if (e->out && (audio_out_disconnected(e->out) || atomic_exchange(&e->fake_disconnect, false))) {
            LOGW("sound device changed: reopening the stream");
            sound_reopen(e);
        }
        if (speed == EMU_FF_MAX) {
            pacer_reset(&pacer); /* no waiting at all */
        } else if (e->out) {
            sound_pace(e);
            pacer_reset(&pacer); /* if sound stops, the clock takes over from now */
        } else {
            clock_wait(&pacer); /* one wait per loop step: N frames per step at N× */
        }
    }
    flush_save(e);
    sound_close(e);
    return NULL;
}

static bool known_save_chip(gba_save_type_t t) {
    return t != GBA_SAVE_NONE && t != GBA_SAVE_UNKNOWN;
}

/* The save file, if there is one, into the core. */
static void load_save(emu_t *e) {
    size_t size = 0;
    uint8_t *data = e->save_path ? save_file_load(e->save_path, &size) : NULL;
    if (data) {
        e->save_status = gba_load_saveram(e->g, data, size) == GBA_OK ? EMU_SAVE_LOADED : EMU_SAVE_MISFIT;
        free(data);
    } else {
        e->save_status = known_save_chip(gba_save_type(e->g)) ? EMU_SAVE_EMPTY : EMU_SAVE_NONE;
    }
}

emu_options_t emu_options_default(void) {
    emu_options_t o = {NULL, 0, EMU_AUTO, EMU_AUTO, EMU_REWIND_DEFAULT, true, 2};
    return o;
}

emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, const emu_options_t *opt, gba_err_t *err) {
    emu_options_t o = opt ? *opt : emu_options_default();
    emu_t *e = calloc(1, sizeof *e);
    if (!e || !(e->g = gba_create(NULL))) {
        free(e);
        *err = GBA_ERR_NOMEM;
        return NULL;
    }
    *err = o.bios ? gba_load_bios(e->g, o.bios, o.bios_size) : GBA_OK;
    if (*err == GBA_OK) *err = gba_load_rom(e->g, rom, size);
    if (*err != GBA_OK) {
        gba_destroy(e->g);
        free(e);
        return NULL;
    }
    /* Game settings, before the save file is read (a forced save type blanks the save memory). */
    if (o.save_type != EMU_AUTO) gba_set_save_type(e->g, (gba_save_type_t)o.save_type);
    if (o.clock != EMU_AUTO) {
        uint32_t hw = gba_cart_hardware(e->g);
        gba_set_cart_hardware(e->g, o.clock ? hw | GBA_HW_RTC : hw & ~(uint32_t)GBA_HW_RTC);
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    gba_set_rtc(e->g, (int64_t)now, (int32_t)local.tm_gmtoff);
    tb_init(&e->tb);
    pthread_mutex_init(&e->lock, NULL);
    pthread_condattr_t attr; /* emu_pause's time limit counts on the monotonic clock */
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&e->wake, &attr);
    pthread_condattr_destroy(&attr);
    key_latch_init(&e->keys);
    atomic_init(&e->frames, 0ull);
    atomic_init(&e->keys_seen, 0u);
    atomic_init(&e->tilt_x, 0);
    atomic_init(&e->tilt_y, 0);
    atomic_init(&e->turn, 0);
    atomic_init(&e->light, 0);
    for (int i = 0; i < 4; i++) atomic_init(&e->seen[i], 0);
    atomic_init(&e->rumble, false);
    atomic_init(&e->sound_wanted, o.sound);
    atomic_init(&e->sound_bursts, o.sound_bursts);
    audio_ring_init(&e->ring);
    resampler_init(&e->rs);
    atomic_init(&e->stats.played, 0);
    atomic_init(&e->stats.silent, 0);
    atomic_init(&e->stats.loud, 0);
    atomic_init(&e->sound_on, false);
    atomic_init(&e->rate, 0);
    atomic_init(&e->burst, 0);
    atomic_init(&e->reopens, 0);
    atomic_init(&e->fake_disconnect, false);
    atomic_init(&e->fake_stuck, false);
    atomic_init(&e->save_failed, false);
    save_timer_init(&e->save_timer);
    e->save_path = save_path ? strdup(save_path) : NULL;
    load_save(e);
    atomic_init(&e->ff_speed, 0);
    atomic_init(&e->rewinding, false);
    atomic_init(&e->rw_snaps, 0);
    atomic_init(&e->rw_bytes, 0);
    atomic_init(&e->rw_steps, 0);
    if (o.rewind_budget && gba_rewind_configure(e->g, o.rewind_budget, REWIND_EVERY) != GBA_OK) {
        LOGW("no memory for rewind: rewind off");
    }
    return e;
}

void emu_start(emu_t *e) {
    if (e->started) return;
    e->started = pthread_create(&e->thread, NULL, emu_main, e) == 0;
}

static void set_paused(emu_t *e, bool paused) {
    pthread_mutex_lock(&e->lock);
    e->paused = paused;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
}

void emu_pause(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    e->paused = true;
    pthread_cond_broadcast(&e->wake);
    if (e->started) { /* wait until the thread has stopped and written the save: at most 1 s */
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += 1;
        while (!e->parked && !e->quit) {
            if (pthread_cond_timedwait(&e->wake, &e->lock, &until) == ETIMEDOUT) break;
        }
    }
    pthread_mutex_unlock(&e->lock);
}

void emu_resume(emu_t *e) {
    set_paused(e, false);
}

void emu_set_keys(emu_t *e, uint16_t pressed) {
    key_latch_set(&e->keys, pressed);
}

void emu_set_fast_forward(emu_t *e, int speed) {
    atomic_store(&e->ff_speed, speed == EMU_FF_MAX || (speed >= 2 && speed <= 8) ? speed : 0);
}

void emu_set_rewinding(emu_t *e, bool on) {
    atomic_store(&e->rewinding, on);
}

void emu_rewind_stats(emu_t *e, int64_t out[3]) {
    out[0] = atomic_load(&e->rw_snaps);
    out[1] = atomic_load(&e->rw_bytes);
    out[2] = atomic_load(&e->rw_steps);
}

triple_buffer_t *emu_frames(emu_t *e) {
    return &e->tb;
}

uint64_t emu_frame_count(const emu_t *e) {
    return atomic_load(&((emu_t *)e)->frames);
}

void emu_set_audio_enabled(emu_t *e, bool enabled) {
    if (!e->started) atomic_store(&e->sound_wanted, enabled);
}

void emu_set_sound(emu_t *e, bool on, int bursts) {
    atomic_store(&e->sound_bursts, bursts == 4 || bursts == 8 ? bursts : 2);
    atomic_store(&e->sound_wanted, on);
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn) {
    atomic_store(&e->tilt_x, clamp(tilt_x, -32767, 32767));
    atomic_store(&e->tilt_y, clamp(tilt_y, -32767, 32767));
    atomic_store(&e->turn, clamp(turn, -32767, 32767));
}

void emu_set_light(emu_t *e, int level) {
    atomic_store(&e->light, clamp(level, 0, 255));
}

uint32_t emu_cart_hardware(const emu_t *e) {
    return gba_cart_hardware(e->g);
}

gba_save_type_t emu_save_type(const emu_t *e) {
    return gba_save_type(e->g);
}

bool emu_rumble_active(emu_t *e) {
    return atomic_load(&e->rumble);
}

void emu_motion_seen(emu_t *e, int32_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = atomic_load(&e->seen[i]);
}

const char *emu_rom_title(const emu_t *e) {
    return gba_rom_title(e->g);
}

emu_save_status_t emu_save_status(const emu_t *e) {
    return e->save_status;
}

gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size) {
    if (e->started) return GBA_ERR_SAVE_SIZE; /* only before start: the thread owns the core then */
    gba_err_t err = gba_load_saveram(e->g, data, size);
    if (err != GBA_OK) return err;
    e->save_status = EMU_SAVE_LOADED;
    write_save(e);
    return GBA_OK;
}

/* The core may be used from another thread only while the emulation thread is parked or not started. */
static bool core_free(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    bool free_now = !e->started || e->parked;
    pthread_mutex_unlock(&e->lock);
    return free_now;
}

bool emu_state_save(emu_t *e, const char *path) {
    if (!core_free(e)) return false;
    size_t size = gba_state_size(e->g);
    uint8_t *buf = malloc(size);
    bool ok = buf && gba_state_save(e->g, buf, size) == size && save_file_replace(path, buf, size);
    free(buf);
    return ok;
}

gba_err_t emu_state_load(emu_t *e, const char *path) {
    if (!core_free(e)) return GBA_ERR_STATE;
    size_t size = 0;
    uint8_t *buf = save_file_load(path, &size);
    if (!buf) return GBA_ERR_STATE;
    gba_err_t err = gba_state_load(e->g, buf, size);
    free(buf);
    if (err == GBA_OK) save_timer_frame(&e->save_timer, true); /* the loaded save chip → the save file */
    return err;
}

gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const gba_cheat_type_t *types, size_t count) {
    if (!core_free(e)) return GBA_ERR_CHEAT;
    return gba_cheats_set(e->g, texts, types, count);
}

bool emu_frame(emu_t *e, uint16_t out[240 * 160]) {
    if (!core_free(e)) return false;
    memcpy(out, gba_framebuffer(e->g), sizeof(uint16_t) * 240 * 160);
    return true;
}

bool emu_save_error(emu_t *e) {
    return atomic_exchange(&e->save_failed, false);
}

size_t emu_debug_saveram(emu_t *e, const uint8_t **data) {
    *data = gba_saveram(e->g);
    return gba_saveram_size(e->g);
}

uint32_t emu_debug_peek32(emu_t *e, uint32_t addr) {
    return core_free(e) ? gba_debug_peek32(e->g, addr) : 0xDEADDEADu;
}

unsigned emu_keys_seen(emu_t *e) {
    return atomic_exchange(&e->keys_seen, 0u);
}

void emu_simulate_audio_disconnect(emu_t *e) {
    atomic_store(&e->fake_disconnect, true);
}

void emu_simulate_stuck_stream(emu_t *e) {
    atomic_store(&e->fake_stuck, true);
}

void emu_audio_stats(emu_t *e, int64_t out[7]) {
    out[0] = atomic_load(&e->sound_on) ? 1 : 0;
    out[1] = atomic_load(&e->rate);
    out[2] = atomic_load(&e->burst);
    out[3] = atomic_load(&e->stats.played);
    out[4] = atomic_load(&e->stats.silent);
    out[5] = atomic_load(&e->stats.loud);
    out[6] = atomic_load(&e->reopens);
}

void emu_destroy(emu_t *e) {
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->quit = true;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
    if (e->started) pthread_join(e->thread, NULL);
    gba_destroy(e->g);
    pthread_mutex_destroy(&e->lock);
    pthread_cond_destroy(&e->wake);
    free(e->save_path);
    free(e);
}
```

Replace `platform/android/jni_bridge.c`:

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <android/native_window_jni.h>
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu_thread.h"
#include "frame_hash.h"
#include "gl_readback.h"
#include "render_thread.h"

static emu_t *s_emu;
static render_thread_t *s_render;
static ANativeWindow *s_window; /* a window that arrived before start() */

/* Settings kept for every game (set any time; the BIOS and the rewind budget are used by the next loadRom). */
static uint8_t *s_bios;       /* a BIOS file's 16 KiB, or NULL: the built-in BIOS */
static size_t s_rewind_budget = EMU_REWIND_DEFAULT;
static bool s_sound = true;
static int s_sound_bursts = 2;
static int s_fit_mode;        /* fit_mode_t */
static bool s_smooth;
/* The next game's own settings (used by the next loadRom only). */
static int s_next_save_type = EMU_AUTO, s_next_clock = EMU_AUTO;

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

static void stop_session(void) {
    render_destroy(s_render); /* first: it reads the emulator's frames */
    s_render = NULL;
    emu_destroy(s_emu);
    s_emu = NULL;
}

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom, jstring save_path) {
    (void)self;
    stop_session(); /* writes the previous game's save */
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    const char *path = save_path ? (*env)->GetStringUTFChars(env, save_path, NULL) : NULL;
    emu_options_t opt = emu_options_default();
    opt.bios = s_bios;
    opt.bios_size = s_bios ? 16384 : 0;
    opt.save_type = s_next_save_type;
    opt.clock = s_next_clock;
    opt.rewind_budget = s_rewind_budget;
    opt.sound = s_sound;
    opt.sound_bursts = s_sound_bursts;
    s_next_save_type = s_next_clock = EMU_AUTO;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, path, &opt, &err);
    if (path) (*env)->ReleaseStringUTFChars(env, save_path, path);
    return err;
}

/* The BIOS for the next games: 16 KiB, or null for the built-in one. GBA_ERR_BIOS_SIZE (nothing changes) otherwise. */
JNIEXPORT jint JNICALL JNI_FN(setBios)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!bytes) {
        free(s_bios);
        s_bios = NULL;
        return GBA_OK;
    }
    if ((*env)->GetArrayLength(env, bytes) != 16384) return GBA_ERR_BIOS_SIZE;
    uint8_t *copy = malloc(16384);
    if (!copy) return GBA_ERR_NOMEM;
    (*env)->GetByteArrayRegion(env, bytes, 0, 16384, (jbyte *)copy);
    free(s_bios);
    s_bios = copy;
    return GBA_OK;
}

/* Bytes for the next games' rewind snapshots (0 = rewind off). */
JNIEXPORT void JNICALL JNI_FN(setRewindBudget)(JNIEnv *env, jobject self, jlong bytes) {
    (void)env;
    (void)self;
    s_rewind_budget = bytes > 0 ? (size_t)bytes : 0;
}

/* The next loadRom's save type (a gba_save_type_t, or -1 = detect) and clock (-1 = detect, 0 = none, 1 = one). */
JNIEXPORT void JNICALL JNI_FN(setGameOverrides)(JNIEnv *env, jobject self, jint save_type, jint clock) {
    (void)env;
    (void)self;
    s_next_save_type = save_type;
    s_next_clock = clock;
}

/* Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once for a running game, and for the next ones. */
JNIEXPORT void JNICALL JNI_FN(setSound)(JNIEnv *env, jobject self, jboolean on, jint bursts) {
    (void)env;
    (void)self;
    s_sound = on == JNI_TRUE;
    s_sound_bursts = bursts;
    if (s_emu) emu_set_sound(s_emu, s_sound, bursts);
}

/* How the picture fills the screen (fit_mode_t) and whether pixels are smoothed: from the next frame on. */
JNIEXPORT void JNICALL JNI_FN(setPicture)(JNIEnv *env, jobject self, jint fit_mode, jboolean smooth) {
    (void)env;
    (void)self;
    s_fit_mode = fit_mode;
    s_smooth = smooth == JNI_TRUE;
    if (s_render) render_set_picture(s_render, s_fit_mode, s_smooth);
}

JNIEXPORT jstring JNICALL JNI_FN(romTitle)(JNIEnv *env, jobject self) {
    (void)self;
    return (*env)->NewStringUTF(env, s_emu ? emu_rom_title(s_emu) : "");
}

JNIEXPORT jint JNICALL JNI_FN(saveStatus)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_save_status(s_emu) : EMU_SAVE_NONE;
}

JNIEXPORT jint JNICALL JNI_FN(importSave)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!s_emu) return GBA_ERR_SAVE_SIZE;
    jsize n = (*env)->GetArrayLength(env, bytes);
    jbyte *b = (*env)->GetByteArrayElements(env, bytes, NULL);
    gba_err_t err = emu_import_save(s_emu, (const uint8_t *)b, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, bytes, b, JNI_ABORT);
    return err;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return JNI_FALSE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    bool ok = emu_state_save(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL JNI_FN(loadState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return GBA_ERR_STATE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    gba_err_t err = emu_state_load(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return err;
}

/* [the type it reads as, the first bad line (0 = none)]; no game needed. */
JNIEXPORT jintArray JNICALL JNI_FN(checkCheat)(JNIEnv *env, jobject self, jstring text, jint type) {
    (void)self;
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    int bad = 1;
    gba_cheat_type_t used = t ? gba_cheat_check(t, (gba_cheat_type_t)type, &bad) : (gba_cheat_type_t)type;
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
    jint v[2] = {(jint)used, bad};
    jintArray out = (*env)->NewIntArray(env, 2);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}

/* The switched-on cheats (only while paused or before start): 0, or a gba_err_t. */
JNIEXPORT jint JNICALL JNI_FN(setCheats)(JNIEnv *env, jobject self, jobjectArray texts, jintArray types) {
    (void)self;
    if (!s_emu) return GBA_ERR_CHEAT;
    jsize n = (*env)->GetArrayLength(env, texts);
    if ((*env)->GetArrayLength(env, types) != n) return GBA_ERR_CHEAT;
    const char **t = calloc((size_t)n + 1, sizeof *t);
    jstring *js = calloc((size_t)n + 1, sizeof *js);
    gba_cheat_type_t *ty = calloc((size_t)n + 1, sizeof *ty);
    jint *raw = (*env)->GetIntArrayElements(env, types, NULL);
    gba_err_t err = t && js && ty && raw ? GBA_OK : GBA_ERR_NOMEM;
    for (jsize i = 0; err == GBA_OK && i < n; i++) {
        js[i] = (jstring)(*env)->GetObjectArrayElement(env, texts, i);
        t[i] = js[i] ? (*env)->GetStringUTFChars(env, js[i], NULL) : NULL;
        if (!t[i]) err = GBA_ERR_CHEAT;
        ty[i] = (gba_cheat_type_t)raw[i];
    }
    if (err == GBA_OK) err = emu_cheats_set(s_emu, t, ty, (size_t)n);
    for (jsize i = 0; js && i < n; i++) {
        if (js[i] && t && t[i]) (*env)->ReleaseStringUTFChars(env, js[i], t[i]);
        if (js[i]) (*env)->DeleteLocalRef(env, js[i]);
    }
    if (raw) (*env)->ReleaseIntArrayElements(env, types, raw, JNI_ABORT);
    free(t);
    free(js);
    free(ty);
    return (jint)err;
}

JNIEXPORT jshortArray JNICALL JNI_FN(frameBgr555)(JNIEnv *env, jobject self) {
    (void)self;
    static uint16_t px[240 * 160];
    if (!s_emu || !emu_frame(s_emu, px)) return NULL;
    jshortArray out = (*env)->NewShortArray(env, 240 * 160);
    (*env)->SetShortArrayRegion(env, out, 0, 240 * 160, (const jshort *)px);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(setMotion)(JNIEnv *env, jobject self, jint tilt_x, jint tilt_y, jint turn) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_motion(s_emu, tilt_x, tilt_y, turn);
}

JNIEXPORT void JNICALL JNI_FN(setLight)(JNIEnv *env, jobject self, jint level) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_light(s_emu, level);
}

JNIEXPORT jint JNICALL JNI_FN(cartHardware)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_cart_hardware(s_emu) : 0;
}

JNIEXPORT jboolean JNICALL JNI_FN(rumbleActive)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_rumble_active(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveError)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_save_error(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (!s_emu) return;
    if (!s_render) {
        s_render = render_create(emu_frames(s_emu));
        if (s_render) render_set_picture(s_render, s_fit_mode, s_smooth);
    }
    if (s_render && s_window) {
        render_set_window(s_render, s_window);
        ANativeWindow_release(s_window);
        s_window = NULL;
    }
    emu_start(s_emu);
}

JNIEXPORT jboolean JNICALL JNI_FN(setSurface)(JNIEnv *env, jobject self, jobject surface) {
    (void)self;
    ANativeWindow *w = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (!s_render) { /* not started yet: hand it over in start() */
        if (s_window) ANativeWindow_release(s_window);
        s_window = w;
        return JNI_TRUE;
    }
    bool ok = render_set_window(s_render, w);
    if (w) ANativeWindow_release(w);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    stop_session();
    if (s_window) ANativeWindow_release(s_window);
    s_window = NULL;
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_pause(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_resume(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *env, jobject self, jint pressed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_keys(s_emu, (uint16_t)(pressed & 0x3FF));
}

JNIEXPORT void JNICALL JNI_FN(setFastForward)(JNIEnv *env, jobject self, jint speed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_fast_forward(s_emu, speed);
}

JNIEXPORT void JNICALL JNI_FN(setRewinding)(JNIEnv *env, jobject self, jboolean on) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_rewinding(s_emu, on);
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugRewindStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[3] = {0, 0, 0};
    if (s_emu) emu_rewind_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 3);
    if (out) (*env)->SetLongArrayRegion(env, out, 0, 3, (const jlong *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugPeek32)(JNIEnv *env, jobject self, jint addr) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_debug_peek32(s_emu, (uint32_t)addr) : 0;
}

JNIEXPORT jlong JNICALL JNI_FN(debugFrameCount)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jlong)emu_frame_count(s_emu) : 0;
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugAudioStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[7] = {0, 0, 0, 0, 0, 0, 0};
    if (s_emu) emu_audio_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 7);
    (*env)->SetLongArrayRegion(env, out, 0, 7, (const jlong *)v);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(debugSetAudioEnabled)(JNIEnv *env, jobject self, jboolean enabled) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_audio_enabled(s_emu, enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateAudioDisconnect)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_audio_disconnect(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateStuckStream)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_stuck_stream(s_emu);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(debugSaveRam)(JNIEnv *env, jobject self) {
    (void)self;
    if (!s_emu) return NULL;
    const uint8_t *data;
    size_t n = emu_debug_saveram(s_emu, &data);
    jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)data);
    return out;
}

JNIEXPORT jintArray JNICALL JNI_FN(debugMotionSeen)(JNIEnv *env, jobject self) {
    (void)self;
    int32_t v[4] = {0, 0, 0, 0};
    if (s_emu) emu_motion_seen(s_emu, v);
    jintArray out = (*env)->NewIntArray(env, 4);
    (*env)->SetIntArrayRegion(env, out, 0, 4, (const jint *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugKeysSeen)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_keys_seen(s_emu) : 0;
}

#define REWIND_EVERY 4

/* Headless run on the calling thread, exactly like `testrunner --frames n [--state-at k]
 * [--rewind-at f:s]` (state_at / rewind_at < 0: none): "<frame hash> <audio hash>", or NULL when the ROM
 * or the state did not load, or a rewind step found no snapshot. */
static jstring run_headless(JNIEnv *env, jobject rom, jint frames, jint state_at, jint rewind_at, jint rewind_steps) {
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    uint64_t *hash_before = NULL; /* the audio hash before each frame's sound was read */
    if (rewind_at >= 0) {
        hash_before = malloc(sizeof(uint64_t) * (size_t)frames);
        if (!hash_before || gba_rewind_configure(g, 64u * 1024 * 1024, REWIND_EVERY) != GBA_OK) {
            free(hash_before);
            gba_destroy(g);
            return NULL;
        }
    }
    bool rewound = false;
    for (jint f = 0; f < frames; f++) {
        if (f == rewind_at && !rewound) { /* as testrunner --rewind-at */
            for (jint s = 0; s < rewind_steps; s++) {
                if (!gba_rewind_step_back(g)) {
                    free(hash_before);
                    gba_destroy(g);
                    return NULL;
                }
            }
            jint to = rewind_at - rewind_at % REWIND_EVERY - REWIND_EVERY * (rewind_steps - (rewind_at % REWIND_EVERY ? 1 : 0));
            f = to;
            audio_hash = hash_before[to - 1];
            rewound = true;
        }
        if (hash_before) hash_before[f] = audio_hash;
        if (f == state_at) { /* carry on in a brand-new machine loaded from a state */
            size_t n = gba_state_size(g);
            uint8_t *state = malloc(n);
            gba_t *fresh = gba_create(NULL);
            bool ok = state && fresh && gba_state_save(g, state, n) == n &&
                      gba_load_rom(fresh, data, (size_t)size) == GBA_OK && gba_state_load(fresh, state, n) == GBA_OK;
            free(state);
            gba_destroy(g);
            g = fresh;
            if (!ok) {
                gba_destroy(g);
                return NULL;
            }
        }
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    free(hash_before);
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    return run_headless(env, rom, frames, -1, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaState)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                         jint state_at) {
    (void)self;
    return run_headless(env, rom, frames, state_at, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaRewind)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                          jint rewind_at, jint rewind_steps) {
    (void)self;
    return run_headless(env, rom, frames, -1, rewind_at, rewind_steps);
}

/* Draws one frame offscreen with the real OpenGL code; RGBA pixels top row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrameWith)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                       jint height, jint fit_mode, jboolean smooth) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, fit_mode, smooth == JNI_TRUE, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}

/* As debugDrawFrameWith with the picture as in 4a (fit, sharp). */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    return JNI_FN(debugDrawFrameWith)(env, self, pixels, width, height, 0, JNI_FALSE);
}

/* The loaded game's save type (gba_save_type_t) and extras (GBA_HW_*), detected or forced; -1 without a game. */
JNIEXPORT jintArray JNICALL JNI_FN(cartSetup)(JNIEnv *env, jobject self) {
    (void)self;
    jint v[2] = {-1, -1};
    if (s_emu) {
        v[0] = (jint)emu_save_type(s_emu);
        v[1] = (jint)emu_cart_hardware(s_emu);
    }
    jintArray out = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}
```

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.Surface
import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /**
     * Creates the emulator with this ROM (a direct buffer; the core copies it) and loads its save file
     * from savePath if there is one (null: no save file). The game's saves go to that file ~1 s after
     * the game saves, and on pause/stop. 0 = OK, else a gba_err_t. Stops (and saves) a previous game.
     */
    external fun loadRom(rom: ByteBuffer, savePath: String?): Int

    fun loadRom(rom: ByteBuffer): Int = loadRom(rom, null)

    /** The BIOS for the next games: a 16 KiB BIOS file's bytes, or null = the built-in one. 0, or ERR_BIOS_SIZE. */
    external fun setBios(bytes: ByteArray?): Int

    /** Bytes for the next games' rewind snapshots (0 = rewind off). */
    external fun setRewindBudget(bytes: Long)

    /** The next loadRom only: a forced save type (a gba_save_type_t) and clock (0 none, 1 one); -1 = detect. */
    external fun setGameOverrides(saveType: Int, clock: Int)

    /** Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once, and for the next games. */
    external fun setSound(on: Boolean, bursts: Int)

    /** How the picture fills the screen (Settings.SIZE_*) and whether pixels are smoothed: at once. */
    external fun setPicture(fitMode: Int, smooth: Boolean)

    /** After loadRom: [save type (gba_save_type_t), HW_* extras], detected or forced; -1s without a game. */
    external fun cartSetup(): IntArray

    /** The loaded ROM's header title (may be empty). */
    external fun romTitle(): String

    /** After loadRom: one of the SAVE_* values below. */
    external fun saveStatus(): Int

    /** Before start(): puts a .sav file's bytes in and writes the save file. 0 = OK, else a gba_err_t (nothing changed). */
    external fun importSave(bytes: ByteArray): Int

    /** True once after a save-file write failed. */
    external fun saveError(): Boolean

    /** Save states — only while paused (after pause()) or before start(). Writes the whole machine to path. */
    external fun saveState(path: String): Boolean

    /** 0 = loaded; else a gba_err_t (GBA_ERR_STATE = 5: another game, another version, damaged, or not paused). */
    external fun loadState(path: String): Int

    /** The current picture, 240×160 BGR555 (only while paused), or null. */
    external fun frameBgr555(): ShortArray?

    const val ERR_BIOS_SIZE = 2
    const val ERR_STATE = 5
    const val ERR_CHEAT = 6

    /** Reads a cheat's text without a game: [the type it reads as (CHEAT_*), the first bad line (0 = none)]. */
    external fun checkCheat(text: String, type: Int): IntArray

    /** The switched-on cheats (only while paused or before start()): 0, or ERR_CHEAT (then none are on). */
    external fun setCheats(texts: Array<String>, types: IntArray): Int

    const val CHEAT_AUTO = 0
    const val CHEAT_ACTION_REPLAY = 1
    const val CHEAT_GAMESHARK = 2
    const val CHEAT_CODEBREAKER = 3
    const val CHEAT_RAW = 4

    /** Cartridge sensors, applied at the next frame: tilt x/y and turn ±32767 (turn > 0 = clockwise). */
    external fun setMotion(tiltX: Int, tiltY: Int, turn: Int)

    /** The solar sensor's light: 0 = dark .. 255 = full sun. */
    external fun setLight(level: Int)

    /** The loaded cartridge's extras: HW_* bits below. */
    external fun cartHardware(): Int

    /** True while the cartridge's rumble motor runs (as of the last frame). */
    external fun rumbleActive(): Boolean

    const val HW_RTC = 1
    const val HW_SOLAR = 2
    const val HW_GYRO = 4
    const val HW_RUMBLE = 8
    const val HW_TILT = 16

    const val SAVE_NONE = 0 // no save chip: nothing to offer
    const val SAVE_LOADED = 1 // the save file was loaded
    const val SAVE_EMPTY = 2 // a save chip and no save file yet: offer an import
    const val SAVE_MISFIT = 3 // the save file does not fit the chip: not loaded, left untouched

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    /** Returns once the game has stopped and its save is written (at most 1 s). */
    external fun pause()

    external fun resume()

    /** The game's window appeared/changed (or went away: null). False if OpenGL ES 3 cannot draw into it. */
    external fun setSurface(surface: Surface?): Boolean

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    /** Fast-forward: 0 = normal, 2/4/8 = that many times faster (sound sped up), FF_MAX = flat out, silent. */
    external fun setFastForward(speed: Int)

    /** While true the game runs backwards (one snapshot of 4 frames per 1/60 s, silent); false plays on from there. */
    external fun setRewinding(on: Boolean)

    const val FF_MAX = -1

    // ---- test-only ----

    /** A word of the game's memory (only while paused or before start). */
    external fun debugPeek32(addr: Int): Int

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

    /** As if the sound device went away (headphones unplugged): the stream must reopen. */
    external fun debugSimulateAudioDisconnect()

    /** The stream silently stops taking sound: the watchdog must reopen it (after its 2 s start-up grace). */
    external fun debugSimulateStuckStream()

    /** The core's save bytes (call while paused), or null without a game. */
    external fun debugSaveRam(): ByteArray?

    /** The sensor values last applied to the core: [tilt x, tilt y, turn, light]. */
    external fun debugMotionSeen(): IntArray

    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** The same, but at frame stateAt it carries on in a new machine loaded from a save state (null: failed). */
    external fun debugRunFramesViaState(rom: ByteBuffer, frames: Int, stateAt: Int): String?

    /** The same, but at frame rewindAt it steps back rewindSteps snapshots (4 frames apart) and plays on (null: failed). */
    external fun debugRunFramesViaRewind(rom: ByteBuffer, frames: Int, rewindAt: Int, rewindSteps: Int): String?

    /** Rewind since start: [snapshots kept, their packed bytes, steps back taken]. */
    external fun debugRewindStats(): LongArray

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?

    /** As debugDrawFrame, with a picture size (Settings.SIZE_*) and sharp or smooth pixels. */
    external fun debugDrawFrameWith(pixels: ShortArray, width: Int, height: Int, fitMode: Int, smooth: Boolean): IntArray?
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: `404 tests, 0 failed`.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```
Expected: `tests="53" failures="0"` three times (47 + 6); `0` C warnings.

- [x] **Step 5: Commit**

```bash
git add platform/android/video_gl.h platform/android/video_gl.c platform/android/gl_readback.h platform/android/gl_readback.c platform/android/render_thread.h platform/android/render_thread.c platform/android/audio_out.h platform/android/audio_out.c platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/SettingsDeviceTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6b-settings.md
git commit -m "feat(android): smooth pixels, size modes, sound on/off and delay, rewind budget, BIOS file, a game's overrides" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: The Settings screens, remapping and the wiring

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` ("Settings…" in the in-game menu, "Game settings…" in the hold menu, Controls → Settings, the Controls pop-up removed), `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt` (look, buzz), `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

(No new automated test: the parts are covered by Tasks 1-4; the whole is checked by Claude in Step 3 and by the user in Task 6.)

- [x] **Step 1: Write the code**

`app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The Settings screen's pages: the categories, then one page each. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward & rewind"), KEYS("Keyboard & controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"),
}

/** A pick list waiting to be shown: its title, the choices, the one chosen now, and what picking does. */
private class Choice(val title: String, val options: List<String>, val selected: Int, val onPick: (Int) -> Unit)

/**
 * Settings (from the library's ⚙ / ⋮ and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val s = settings
    val onOff = listOf("On", "Off")
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = onBack) { Text("←", fontSize = 22.sp) }
                Text(page.title, style = MaterialTheme.typography.titleLarge)
            }
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        Category("Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        Category("Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        Category("Fast-forward & rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        Category("Keyboard & controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        Category("Touch buttons", "${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        Category("BIOS", biosName) { onPage(SettingsPage.BIOS) }
                    }
                    SettingsPage.PICTURE -> {
                        val pixels = listOf("Sharp", "Smooth")
                        Setting("Pixels", pixels[if (s.smooth) 1 else 0]) {
                            choosing = Choice("Pixels", pixels, if (s.smooth) 1 else 0) { onChange(s.copy(smooth = it == 1)) }
                        }
                        Setting("Size", Settings.SIZE_NAMES[s.size]) {
                            choosing = Choice("Size", Settings.SIZE_NAMES, s.size) { onChange(s.copy(size = it)) }
                        }
                        Note("Whole-number steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                    }
                    SettingsPage.SOUND -> {
                        Setting("Sound", onOff[if (s.sound) 0 else 1]) {
                            choosing = Choice("Sound", onOff, if (s.sound) 0 else 1) { onChange(s.copy(sound = it == 0)) }
                        }
                        Setting("Delay", Settings.DELAY_NAMES[s.delay]) {
                            choosing = Choice("Delay", Settings.DELAY_NAMES, s.delay) { onChange(s.copy(delay = it)) }
                        }
                        Note("Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> {
                        val speeds = Settings.SPEEDS.map(Settings::speedName)
                        Setting("Fast-forward speed", Settings.speedName(s.fastSpeed)) {
                            choosing = Choice("Fast-forward speed", speeds, Settings.SPEEDS.indexOf(s.fastSpeed)) {
                                onChange(s.copy(fastSpeed = Settings.SPEEDS[it]))
                            }
                        }
                        Setting("Rewind", onOff[if (s.rewind) 0 else 1]) {
                            choosing = Choice("Rewind", onOff, if (s.rewind) 0 else 1) { onChange(s.copy(rewind = it == 0)) }
                        }
                        Setting("Rewind length", Settings.LENGTH_NAMES[s.rewindLength]) {
                            choosing = Choice("Rewind length", Settings.LENGTH_NAMES, s.rewindLength) { onChange(s.copy(rewindLength = it)) }
                        }
                        Note("Rewind uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        Setting("Sideways, over the picture", Settings.LOOK_NAMES[s.touchLook]) {
                            choosing = Choice("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook) { onChange(s.copy(touchLook = it)) }
                        }
                        Setting("Buzz", onOff[if (s.buzz) 0 else 1]) {
                            choosing = Choice("Buzz", onOff, if (s.buzz) 0 else 1) { onChange(s.copy(buzz = it == 0)) }
                        }
                        Note("Buzz follows the phone's own touch-vibration setting.")
                    }
                    SettingsPage.BIOS -> {
                        Setting("BIOS", biosName) {}
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(onClick = onChooseBios, modifier = Modifier.fillMaxWidth()) { Text("Choose a BIOS file…") }
                        if (s.biosFile) {
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth()) { Text("Use the built-in BIOS") }
                        }
                        Note("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                }
            }
        }
    }
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}

@Composable
private fun Category(title: String, summary: String, onClick: () -> Unit) {
    Column(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 12.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        Text(summary, fontSize = 13.sp, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Setting(label: String, value: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyLarge)
        Text(value, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Note(text: String) {
    Text(text, fontSize = 13.sp, color = Color.Gray, modifier = Modifier.padding(vertical = 12.dp))
}

/** A pick list: tapping a choice picks it. */
@Composable
fun ChoiceDialog(title: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(title) },
        text = {
            Column {
                options.forEachIndexed { i, label ->
                    Row(Modifier.fillMaxWidth().selectable(selected = i == selected, onClick = { onPick(i) }).padding(vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = i == selected, onClick = null)
                        Text(label, modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Each GBA button (and ⏩ ⟲) with its keyboard key and controller button; a tap waits for the new one. */
@Composable
private fun KeyTable(keys: KeyBindings, keysChanged: Int, onPickKey: (Int, Int) -> Unit, onReset: () -> Unit) {
    val rows = remember(keysChanged) {
        KeyMap.bindings.map { b ->
            Triple(b, KeyBindings.keyName(keys.key(b.bit, KeyBindings.KEYBOARD)), KeyBindings.keyName(keys.key(b.bit, KeyBindings.CONTROLLER)))
        }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        for (h in listOf("GBA", "Keyboard", "Controller")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold)
    }
    for ((b, kbd, pad) in rows) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(b.button, Modifier.weight(1f))
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
        }
        HorizontalDivider()
    }
    Spacer(Modifier.height(12.dp))
    OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth()) { Text("Reset to defaults") }
    Note("Tap a key, then press the new one. A key already used moves here; its old button shows \"—\". The phone's touch buttons stay as they are.")
}

/**
 * Waiting for the key for one GBA button; Back cancels (Back itself can't be a game key). A layer on the same window,
 * not a dialog: a dialog's window would get the key instead of MainActivity.dispatchKeyEvent.
 */
@Composable
fun KeyWaitLayer(button: String, device: Int, onCancel: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(24.dp)) {
            Column(Modifier.padding(20.dp)) {
                Text("$button — ${if (device == KeyBindings.KEYBOARD) "keyboard" else "controller"}",
                    style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(12.dp))
                Text("Press the ${if (device == KeyBindings.KEYBOARD) "key" else "controller button"} for $button…")
                TextButton(onClick = onCancel, modifier = Modifier.align(Alignment.End)) { Text("Cancel") }
            }
        }
    }
}

/** A 16 KB file that isn't the usual GBA BIOS. */
@Composable
fun UnusualBiosDialog(onUse: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Not the usual GBA BIOS") },
        text = { Text("This isn't the usual GBA BIOS; games may not start. Use it anyway?") },
        confirmButton = { TextButton(onClick = onUse) { Text("Use it") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Game settings (hold a game → Game settings…): save type and clock, Auto showing what was detected. */
@Composable
fun GameSettingsDialog(name: String, game: GameSettings.Game, detected: Pair<Int, Boolean>?, onChange: (GameSettings.Game) -> Unit,
                       onDone: () -> Unit) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val autoSave = "Auto" + (detected?.let { " (${GameSettings.saveName(it.first)})" } ?: "")
    val autoClock = "Auto" + (detected?.let { if (it.second) " (has one)" else " (none)" } ?: "")
    val saveOptions = listOf(autoSave) + GameSettings.SAVE_CHOICES.map(GameSettings::saveName)
    val saveIndex = if (game.saveType == GameSettings.AUTO) 0 else GameSettings.SAVE_CHOICES.indexOf(game.saveType) + 1
    val clockOptions = listOf(autoClock, "On", "Off")
    val clockIndex = when (game.clock) {
        GameSettings.CLOCK_ON -> 1
        GameSettings.CLOCK_OFF -> 2
        else -> 0
    }
    AlertDialog(
        onDismissRequest = onDone,
        title = { Text("Game settings — $name") },
        text = {
            Column {
                Setting("Save type", saveOptions[saveIndex]) {
                    choosing = Choice("Save type", saveOptions, saveIndex) {
                        onChange(game.copy(saveType = if (it == 0) GameSettings.AUTO else GameSettings.SAVE_CHOICES[it - 1]))
                    }
                }
                Setting("Clock", clockOptions[clockIndex]) {
                    choosing = Choice("Clock", clockOptions, clockIndex) {
                        onChange(game.copy(clock = when (it) {
                            1 -> GameSettings.CLOCK_ON
                            2 -> GameSettings.CLOCK_OFF
                            else -> GameSettings.AUTO
                        }))
                    }
                }
                Note("Used the next time this game opens. The save file is kept either way; it only loads if it fits the save type.")
            }
        },
        confirmButton = { TextButton(onClick = onDone) { Text("Done") } },
    )
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}
```

(The "press a key" prompt is a layer on the activity's window, not a dialog: a dialog's window would get the key instead of `MainActivity.dispatchKeyEvent` — found while prototyping.)

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import java.util.Calendar
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.layout.systemBarsPadding
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.DateFormat
import java.util.Date
import kotlin.math.roundToInt
import androidx.compose.ui.viewinterop.AndroidView

/** The very first start (no games folder yet) or a folder that can't be opened any more. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().systemBarsPadding().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(
                if (folderLost) "Can't open the games folder any more — choose it again."
                else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                    "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                    "Create new folder) and put your games in it.",
                textAlign = TextAlign.Center,
                color = if (folderLost) MaterialTheme.colorScheme.error else Color.Unspecified,
            )
            Spacer(Modifier.height(24.dp))
            Button(onClick = onChooseFolder) { Text("Choose games folder") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onOpenGame) { Text("Open a single game…") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onSettings) { Text("Settings") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
            }
        }
    }
}

/** When a game was last played, as the list shows it: "Today 14:00", "Yesterday 09:12", a date, or "Never played". */
fun playedText(lastPlayed: Long, now: Long = System.currentTimeMillis()): String {
    if (lastPlayed <= 0) return "Never played"
    val day = { t: Long -> Calendar.getInstance().apply { timeInMillis = t }.let { it.get(Calendar.YEAR) * 1000 + it.get(Calendar.DAY_OF_YEAR) } }
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(lastPlayed))
    val yesterday = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -1) }.timeInMillis
    return when (day(lastPlayed)) {
        day(now) -> "Today $time"
        day(yesterday) -> "Yesterday $time"
        else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(lastPlayed))
    }
}

/**
 * The game list: one row per game (the picture of where it was left, its name, when it was last played). A tap
 * plays it, holding opens [onHold]'s menu. [refresh] changes whenever the pictures may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit,
                  onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Text("Games", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                Box {
                    TextButton(onClick = { menuOpen = true }) { Text("⋮", fontSize = 22.sp) }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open a game…") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder…") }, onClick = { menuOpen = false; onChangeFolder() })
                        DropdownMenuItem(text = { Text("Settings") }, onClick = { menuOpen = false; onSettings() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    if (scanning) Text("Looking for games…")
                    else {
                        Text("No games found in ${folderName ?: "the folder"}.", textAlign = TextAlign.Center)
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = onChangeFolder) { Text("Change folder") }
                    }
                }
            } else {
                LazyColumn(Modifier.fillMaxSize()) {
                    items(rows, key = { it.key }) { game -> GameRow(game, refresh, picture, onPlay, onHold) }
                }
            }
        }
    }
}

@Composable
private fun GameRow(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit,
                    onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    Row(
        Modifier.fillMaxWidth()
            .pointerInput(game) { detectTapGestures(onTap = { onPlay(game) }, onLongPress = { onHold(game) }) }
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val frame = Modifier.width(96.dp).aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
        if (bitmap != null) {
            Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
                contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
        } else {
            Box(frame.background(MaterialTheme.colorScheme.surfaceVariant))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                fontSize = 12.sp, color = Color.Gray)
        }
    }
}

/** Holding a game in the list. [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onGameSettings: () -> Unit, onRemove: (() -> Unit)?, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column {
                val items = listOf("Start fresh (from the title screen)" to onStartFresh, "Import save…" to onImport,
                    "Export save…" to onExport, "Game settings…" to onGameSettings) +
                    listOfNotNull(onRemove?.let { "Remove from list" to it })
                for ((label, action) in items) {
                    TextButton(onClick = action, modifier = Modifier.fillMaxWidth()) {
                        Text(label, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Cancel") } },
    )
}

/** Import over a save that exists: the old one is kept as .sav.bak. */
@Composable
fun ReplaceSaveDialog(name: String, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace this game's save?") },
        text = { Text("$name already has a save. It will be kept as a backup, and the game will start from the title screen with the imported save.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** A game with a save chip and no save yet: start fresh, or bring in a .sav file. Dismissing cancels the opening. */
@Composable
fun ImportDialog(error: String?, onStartFresh: () -> Unit, onImport: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("No save for this game yet") },
        text = {
            Column {
                Text("Start a fresh game, or import a .sav file (for example from a PC emulator)?")
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { TextButton(onClick = onImport) { Text("Import a .sav") } },
        dismissButton = { TextButton(onClick = onStartFresh) { Text("Start fresh") } },
    )
}

/** Which page of the in-game menu is open. */
enum class MenuPage { MAIN, SAVE, LOAD, CHEATS }

/** A dimmed layer over the paused game (it takes every touch) with a card in the middle. */
@Composable
private fun MenuLayer(content: @Composable () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center,
    ) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(16.dp).widthIn(max = 440.dp)) {
            content()
        }
    }
}

/** Back while playing: the game is paused behind this menu. */
@Composable
fun GameMenu(title: String, fastSpeed: String, onFastSpeed: () -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad, "Cheats…" to onCheats, "Settings…" to onSettings,
                "Restart game" to onRestart, "Quit" to onQuit)) {
                FilledTonalButton(onClick = action, modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                    Text(label)
                }
            }
        }
    }
}

/**
 * The 10 slots as a grid of pictures (two columns). Saving: every slot can be picked; loading: only used ones.
 * [refresh] changes whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val pictures = remember(refresh) { (1..States.SLOTS).map { if (states.used(it)) states.loadPicture(it) else null } }
    val used = remember(refresh) { (1..States.SLOTS).map { states.used(it) } }
    val dates = remember(refresh) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        (1..States.SLOTS).map { if (states.used(it)) format.format(Date(states.savedAt(it))) else "empty" }
    }
    MenuLayer {
        Column(Modifier.padding(12.dp).verticalScroll(rememberScrollState())) {
            Text(if (saving) "Save state" else "Load state", style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(4.dp))
            for (row in 0 until States.SLOTS / 2) {
                Row {
                    for (col in 0..1) {
                        val slot = row * 2 + col + 1
                        val enabled = saving || used[slot - 1]
                        Column(
                            Modifier.weight(1f).padding(4.dp).clip(RoundedCornerShape(8.dp))
                                .background(MaterialTheme.colorScheme.surfaceVariant)
                                .clickable(enabled = enabled) { onPick(slot) }.padding(6.dp),
                        ) {
                            val picture = pictures[slot - 1]
                            val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
                            if (picture != null) {
                                Image(picture.asImageBitmap(), contentDescription = "slot $slot", modifier = frame,
                                    contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
                            } else {
                                Box(frame.border(1.dp, Color.Gray, RoundedCornerShape(4.dp)))
                            }
                            Text("$slot · ${dates[slot - 1]}", fontSize = 12.sp,
                                color = if (enabled) Color.Unspecified else Color.Gray,
                                modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/** The type picker's names, by NativeBridge.CHEAT_* value. */
val CHEAT_TYPES = listOf("Auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value")

/** The game's cheats: one row each (name, on/off switch); a tap on the name edits it. */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(300.dp)) {
            Text("Cheats — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Column(Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                if (cheats.isEmpty()) Text("No cheats yet.", Modifier.padding(vertical = 8.dp))
                cheats.forEachIndexed { i, c ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(12.dp))
                        Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 8.dp))
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = onAdd, modifier = Modifier.fillMaxWidth()) { Text("+ Add cheat") }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/**
 * Adding (editing == null) or editing a cheat: name, codes (one per line), type. [check] reads the codes:
 * [the type found, the first bad line (0 = none)]. Save refuses unreadable codes and names the line.
 */
@Composable
fun CheatDialog(editing: Cheat?, check: (String, Int) -> IntArray, onSave: (Cheat) -> Unit, onDelete: (() -> Unit)?,
                onCancel: () -> Unit) {
    var name by remember { mutableStateOf(editing?.name ?: "") }
    var codes by remember { mutableStateOf(editing?.codes ?: "") }
    var type by remember { mutableStateOf(editing?.type ?: NativeBridge.CHEAT_AUTO) }
    var picking by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val found = remember(codes, type) { if (codes.isBlank()) null else check(codes, type) }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(if (editing == null) "Add cheat" else "Edit cheat") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(value = codes, onValueChange = { codes = it; error = null },
                    label = { Text("Codes (one per line)") }, minLines = 4, maxLines = 10,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Box {
                    OutlinedButton(onClick = { picking = true }) { Text("Type: ${CHEAT_TYPES[type]} ▾") }
                    DropdownMenu(expanded = picking, onDismissRequest = { picking = false }) {
                        CHEAT_TYPES.forEachIndexed { i, t ->
                            DropdownMenuItem(text = { Text(t) }, onClick = { type = i; picking = false; error = null })
                        }
                    }
                }
                val hint = when {
                    error != null -> error
                    found == null -> null
                    found[1] != 0 -> "Line ${found[1]} isn't a code this app can read."
                    type == NativeBridge.CHEAT_AUTO -> "Found: ${CHEAT_TYPES[found[0]]}"
                    else -> null
                }
                if (hint != null) {
                    Text(hint, color = if (found != null && found[1] == 0 && error == null) Color.Unspecified
                        else MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 6.dp))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val result = check(codes, type)
                if (codes.isBlank()) error = "Type or paste the codes first."
                else if (result[1] != 0) error = "Line ${result[1]} isn't a code this app can read."
                else {
                    val shown = name.trim().ifEmpty { codes.lineSequence().first { it.isNotBlank() }.trim() }
                    onSave(Cheat(shown, type, codes, editing?.on ?: true))
                }
            }) { Text("Save") }
        },
        dismissButton = {
            Row {
                if (onDelete != null) TextButton(onClick = onDelete) { Text("Delete") }
                TextButton(onClick = onCancel) { Text("Cancel") }
            }
        },
    )
}

@Composable
fun DeleteCheatDialog(name: String, onDelete: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Delete $name?") },
        text = { Text("The cheat and its codes are removed from this game.") },
        confirmButton = { TextButton(onClick = onDelete) { Text("Delete") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ReplaceDialog(slot: Int, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace slot $slot?") },
        text = { Text("The state saved there is overwritten.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun RestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart from the title screen?") },
        text = { Text("The current moment is lost unless it's saved in a slot.") },
        confirmButton = { TextButton(onClick = onRestart) { Text("Restart") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the ☀ button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}, touchLook: Int = Settings.LOOK_NORMAL, buzz: Boolean = true) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val sideways = maxWidth > maxHeight
        val game: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(modifier = modifier, factory = { gameSurface(it, onDrawFailed) })
        }
        val buttons: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(
                modifier = modifier,
                factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
                update = { v ->
                    v.sideways = sideways
                    v.hidden = controlsHidden
                    v.fastOn = fastOn
                    v.look = touchLook
                    v.buzz = buzz
                    v.onMask = onTouchMask
                    v.onShow = onShowControls
                },
            )
        }
        if (sideways) {
            Box(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxSize()) // the game centred, black bars left and right
                buttons(Modifier.fillMaxSize()) // see-through, over the bars
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxWidth().aspectRatio(1.5f)) // 240×160 across the full width, at the top
                buttons(Modifier.fillMaxWidth().weight(1f))
            }
        }
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
    }
}

/** ☀: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = RoundedCornerShape(12.dp), color = Color(0xE0202020)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", color = Color.White, modifier = Modifier.weight(1f))
                        Text("Auto ", color = Color.White)
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        TextButton(onClick = { open = !open }) { Text("☀", fontSize = 28.sp, color = Color(0xFFFFD54F)) }
    }
}

private fun gameSurface(context: Context, onDrawFailed: () -> Unit) = SurfaceView(context).apply {
    holder.addCallback(object : SurfaceHolder.Callback {
        override fun surfaceCreated(holder: SurfaceHolder) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            NativeBridge.setSurface(null)
        }
    })
}
```

Replace `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`:

```kotlin
package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The on-screen GBA buttons (layout A). Every finger is followed; the pressed buttons go to [onMask] whenever they
 * change, each newly pressed one with a vibration tick (Android's own touch feedback, so the phone's setting applies)
 * unless [buzz] is off. Sideways, [look] sets how strongly they show over the picture (Settings.LOOK_*).
 * While [hidden] (a keyboard or gamepad is in use) nothing is drawn; the next touch shows the buttons again.
 */
class TouchControlsView(context: Context) : View(context) {
    /** True: sideways — see-through buttons over the whole screen. False: upright — the area below the game. */
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) setMask(0)
            invalidate()
        }

    /** Fast mode switched on: ⏩ is drawn lit. */
    var fastOn = false
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var buzz = true

    var onMask: (Int) -> Unit = {}
    var onShow: () -> Unit = {}

    var touchLayout: TouchLayout? = null
        private set

    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the buttons presses nothing

    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways) else null
        touchLayout = l
        dpadShape.reset()
        if (l != null) {
            val r = l.dpadRadius
            val a = r * ARM
            val across = Path().apply { addRoundRect(l.dpadX - r, l.dpadY - a, l.dpadX + r, l.dpadY + a, a / 3, a / 3, Path.Direction.CW) }
            val down = Path().apply { addRoundRect(l.dpadX - a, l.dpadY - r, l.dpadX + a, l.dpadY + r, a / 3, a / 3, Path.Direction.CW) }
            dpadShape.op(across, down, Path.Op.UNION)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val layout = touchLayout ?: return false
        val action = event.actionMasked
        if (hidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                hidden = false
                ignoreUntilUp = true
                onShow()
            }
            return true
        }
        if (ignoreUntilUp) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ignoreUntilUp = false
            return true
        }
        var pressed = 0
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) pressed = pressed or layout.buttonsAt(event.getX(i), event.getY(i))
            }
        }
        setMask(pressed)
        return true
    }

    private fun setMask(pressed: Int) {
        if (pressed == mask) return
        if (buzz && pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val layout = touchLayout ?: return
        if (hidden) return
        // Upright: solid grey on black. Sideways: see-through white over the game (Faint / Normal / Strong).
        val (idleA, downA, textA) = when (look) {
            Settings.LOOK_FAINT -> Triple(0x33, 0x66, 0x88)
            Settings.LOOK_STRONG -> Triple(0x99, 0xCC, 0xFF)
            else -> Triple(0x55, 0x99, 0xDD)
        }
        val idle = if (sideways) (idleA shl 24) or 0xFFFFFF else 0xFFB4B4B4.toInt()
        val down = if (sideways) (downA shl 24) or 0xFFFFFF else 0xFF6E6E6E.toInt()
        val armDown = if (sideways) (idleA shl 24) or 0xFFFFFF else down // drawn over the cross
        text.color = if (sideways) (textA shl 24) or 0xFFFFFF else 0xFF222222.toInt()

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker.
        fill.color = idle
        canvas.drawPath(dpadShape, fill)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = armDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            fill.color = when {
                mask and b.bit != 0 -> down
                b.bit == KeyMap.FAST && fastOn -> LIT
                else -> idle
            }
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
            }
            text.textSize = when {
                b.round -> 22 * density
                b.bit == KeyMap.FAST || b.bit == KeyMap.REWIND -> 20 * density // symbols, not words
                else -> 13 * density
            }
            canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
    }

    private companion object {
        const val ARM = 0.36f // the D-pad's arm half-width, as a part of its radius
        const val LIT = 0xFFFFD54A.toInt() // ⏩ while fast mode is switched on
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private lateinit var library: Library
    private var rows by mutableStateOf(listOf<GameEntry>()) // the list as shown
    private var hasFolder by mutableStateOf(false) // library.folder is set (the screen watches this, not the Library)
    private var folderName by mutableStateOf<String?>(null)
    private var folderLost by mutableStateOf(false)
    private var scanning by mutableStateOf(false)
    private var scanRun = 0 // only the newest scan's result is used
    private var libraryRefresh by mutableStateOf(0) // re-reads the list's pictures
    private var holdGame by mutableStateOf<GameEntry?>(null)
    private var replaceSave by mutableStateOf<Pair<GameEntry, ByteArray>?>(null)
    private var saveGame: GameEntry? = null // the game whose save is being exported / imported
    private var exportBytes: ByteArray? = null
    private lateinit var settingsStore: IntStore
    private var settings by mutableStateOf(Settings())
    private var settingsPage by mutableStateOf<SettingsPage?>(null) // the Settings screen, or null
    private lateinit var keys: KeyBindings
    private var keysChanged by mutableStateOf(0) // re-reads the key table
    private var waitingKey by mutableStateOf<Pair<Int, Int>?>(null) // (button bit, KeyBindings device) being remapped
    private var unusualBios by mutableStateOf<ByteArray?>(null) // a 16 KB file that isn't the usual BIOS, to confirm
    private lateinit var gameSettings: GameSettings
    private var editGame by mutableStateOf<Triple<String, String, GameSettings.Game>?>(null) // name, id, its settings
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var menu by mutableStateOf<MenuPage?>(null) // the in-game menu (the game paused) or null
    private var replaceSlot by mutableStateOf<Int?>(null)
    private var askRestart by mutableStateOf(false)
    private var slotsChanged by mutableStateOf(0) // re-reads the slot pictures
    private var session by mutableStateOf(0) // a new game session gets a new game screen (see startGame)
    private var states: States? = null // the running game's save states
    private var cheatStore: Cheats? = null // the running game's cheats file
    private var cheats by mutableStateOf(listOf<Cheat>())
    private var editCheat by mutableStateOf<Int?>(null) // -1 = a new one
    private var deleteCheat by mutableStateOf<Int?>(null)
    private var current: GameEntry? = null // the running game's row
    private var currentTitle = ""
    private var controlsHidden by mutableStateOf(false)
    private var manualLight by mutableStateOf<Int?>(null)
    private var hardware = 0 // the cartridge's extras (NativeBridge.HW_*)
    private var motion: MotionInput? = null
    private var rumble: Rumble? = null
    private var keyboardMask = 0
    private var touchMask = 0
    private val fastSwitch = FastForwardSwitch()
    private val repeatFilter = KeyRepeatFilter() // F / R held in the phone emulator's window
    private val handler = Handler(Looper.getMainLooper())
    private var fastOn by mutableStateOf(false) // ⏩ lit
    private var fastSpeed by mutableStateOf(4) // 2, 4, 8 or NativeBridge.FF_MAX; remembered for all games

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }
    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useFolder(uri)
    }
    private val exportTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) writeExport(uri)
    }
    private val pickBios = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) biosPicked(uri)
    }
    private val importFor = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPicked(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = Library(filesDir)
        if (!library.load()) toast("The game list couldn't be read; it was kept aside and is built again from the folder.")
        carryOverLastGame()
        rows = library.rows()
        hasFolder = library.folder != null
        settingsStore = PrefsStore(getSharedPreferences("settings", MODE_PRIVATE))
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        applySettings(settings)
        keys = KeyBindings(PrefsStore(getSharedPreferences("keys", MODE_PRIVATE)))
        keys.use()
        gameSettings = GameSettings(PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE)))
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) key(session) {
                    GameScreen(
                        controlsHidden = controlsHidden,
                        fastOn = fastOn,
                        onTouchMask = { touchMask = it; sendKeys() },
                        onShowControls = { controlsHidden = false },
                        onDrawFailed = {
                            stopGame()
                            message = "This device cannot show the game: OpenGL ES 3.0 failed."
                        },
                        solar = hardware and NativeBridge.HW_SOLAR != 0,
                        manualLight = manualLight,
                        onManualLight = {
                            manualLight = it
                            motion?.manualLight = it
                        },
                        touchLook = settings.touchLook,
                        buzz = settings.buzz,
                    )
                    LaunchedEffect(Unit) { // a failed save-file write: tell once per game
                        while (true) {
                            delay(1000)
                            if (NativeBridge.saveError()) {
                                toast("Could not write the save file.")
                                break
                            }
                        }
                    }
                } else if (folderLost || !hasFolder) { // no folder yet, or it can't be opened
                    FirstStartScreen(folderLost, message, onChooseFolder = { pickFolder.launch(null) },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onSettings = { settingsPage = SettingsPage.MAIN })
                } else {
                    LibraryScreen(rows, folderName, scanning, message, libraryRefresh,
                        picture = { g -> if (g.id.isEmpty()) null else States(filesDir, g.id).loadPicture(States.AUTO) },
                        onPlay = { open(it) }, onHold = { holdGame = it },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onChangeFolder = { pickFolder.launch(null) },
                        onSettings = { settingsPage = SettingsPage.MAIN })
                }
                holdGame?.let { g ->
                    GameHoldMenu(g,
                        onStartFresh = { holdGame = null; open(g, resumeAuto = false) },
                        onImport = { holdGame = null; saveGame = g; importFor.launch(arrayOf("*/*")) },
                        onExport = { holdGame = null; exportSave(g) },
                        onGameSettings = {
                            holdGame = null
                            idOf(g)?.let { id -> editGame = Triple(g.name, id, gameSettings.of(id)) }
                        },
                        onRemove = if (g.outside) ({ holdGame = null; removeGame(g) }) else null,
                        onClose = { holdGame = null })
                }
                replaceSave?.let { (g, bytes) ->
                    ReplaceSaveDialog(g.name, onReplace = { replaceSave = null; importSave(g, bytes) },
                        onCancel = { replaceSave = null })
                }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                val st = states
                when (menu) {
                    MenuPage.MAIN -> GameMenu(currentTitle, speedLabel(fastSpeed), onFastSpeed = ::nextFastSpeed,
                        onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
                        onSettings = { settingsPage = SettingsPage.MAIN },
                        onRestart = { askRestart = true }, onQuit = ::quitGame)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN })
                    null -> {}
                }
                editCheat?.let { i ->
                    CheatDialog(editing = cheats.getOrNull(i), check = NativeBridge::checkCheat,
                        onSave = { c ->
                            changeCheats(cheats.toMutableList().also { if (i in it.indices) it[i] = c else it += c })
                            editCheat = null
                        },
                        onDelete = if (i in cheats.indices) ({ deleteCheat = i }) else null,
                        onCancel = { editCheat = null })
                }
                deleteCheat?.let { i ->
                    DeleteCheatDialog(cheats[i].name,
                        onDelete = {
                            changeCheats(cheats.toMutableList().also { it.removeAt(i) })
                            deleteCheat = null
                            editCheat = null
                        },
                        onCancel = { deleteCheat = null })
                }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                settingsPage?.let { page ->
                    SettingsScreen(page, settings, keys, keysChanged, if (settings.biosFile) "BIOS file" else "Built-in",
                        onPage = { settingsPage = it },
                        onBack = { settingsPage = if (page == SettingsPage.MAIN) null else SettingsPage.MAIN },
                        onChange = ::changeSettings,
                        onPickKey = { bit, device -> waitingKey = bit to device },
                        onResetKeys = { keys.reset(); keys.use(); keysChanged++ },
                        onChooseBios = { pickBios.launch(arrayOf("*/*")) },
                        onBuiltInBios = {
                            Bios.remove(filesDir)
                            changeSettings(settings.copy(biosFile = false))
                            toast("The built-in BIOS is used from the next game on.")
                        })
                }
                waitingKey?.let { (bit, device) ->
                    KeyWaitLayer(KeyMap.bindings.first { it.bit == bit }.button, device, onCancel = { waitingKey = null })
                }
                unusualBios?.let { bytes ->
                    UnusualBiosDialog(onUse = { unusualBios = null; installBios(bytes) }, onCancel = { unusualBios = null })
                }
                editGame?.let { (name, id, game) ->
                    GameSettingsDialog(name, game, gameSettings.detected(id),
                        onChange = { gameSettings.set(id, it); editGame = Triple(name, id, it) },
                        onDone = { editGame = null })
                }
                BackHandler(enabled = playing && menu == null && settingsPage == null) { openMenu() }
                BackHandler(enabled = playing && menu != null && settingsPage == null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
                }
                BackHandler(enabled = settingsPage != null && waitingKey == null) {
                    settingsPage = if (settingsPage == SettingsPage.MAIN) null else SettingsPage.MAIN
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    /** The file's name as the picker showed it and its size (0 = unknown), or null. */
    private fun fileInfo(uri: Uri): Pair<String, Long>? = try {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) to (if (it.isNull(1)) 0L else it.getLong(1)) else null
        }
    } catch (e: Exception) {
        null
    }

    /** "Open a game…": the game's row if the list has it (e.g. it is in the folder), else a new outside one. */
    private fun openFromPicker(uri: Uri) {
        try { /* so the list can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            uri.toString()
        }
        val info = fileInfo(uri)
        val entry = library.games.firstOrNull { it.key == key }
            ?: GameEntry(key, uri.toString(), Library.nameOf(info?.first ?: uri.lastPathSegment ?: "game"), outside = true,
                size = info?.second ?: 0)
        open(entry)
    }

    /** The games folder picked: its permission kept (the old folder's given back), the list read from it. */
    private fun useFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        library.folder?.let { old ->
            if (old != tree.toString()) try {
                contentResolver.releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: Exception) {
            }
        }
        library.folder = tree.toString()
        library.save()
        hasFolder = true
        folderLost = false
        message = null
        refreshLibrary()
    }

    /**
     * Reads the folder again (off the main thread) and shows the list. Played games whose id is not known yet (the
     * last game carried over from before the list) are read once to learn it, so their picture shows. Outside games
     * of unknown size get it, so [Library.merge] can match them with the folder's copy by name and size.
     */
    private fun refreshLibrary() {
        rows = library.rows()
        val folder = library.folder?.let(Uri::parse)
        val unknown = library.games.filter { it.lastPlayed > 0 && it.id.isEmpty() }
        val unsized = library.games.filter { it.outside && it.size <= 0 }
        if (folder == null && unknown.isEmpty() && unsized.isEmpty()) return
        val run = ++scanRun
        scanning = true
        thread(name = "library-scan") {
            val found = folder?.let { FolderScan.scan(contentResolver, it) }
            val name = folder?.let { FolderScan.folderName(contentResolver, it) }
            val ids = unknown.mapNotNull { g -> readGame(Uri.parse(g.uri))?.let { g.key to GameId.of(it) } }.toMap()
            val sizes = unsized.mapNotNull { g -> fileInfo(Uri.parse(g.uri))?.second?.takeIf { it > 0 }?.let { g.key to it } }.toMap()
            runOnUiThread {
                if (run != scanRun) return@runOnUiThread
                scanning = false
                for (g in library.games) ids[g.key]?.let { library.played(g.copy(id = it)) }
                for (g in library.games) sizes[g.key]?.let { library.played(g.copy(size = it)) }
                if (folder != null) {
                    folderLost = found == null
                    if (found != null) library.merge(found)
                }
                folderName = name
                library.save()
                rows = library.rows()
                libraryRefresh++
            }
        }
    }

    /** Before the list (5c and earlier): the last game opened becomes the list's first game. */
    private fun carryOverLastGame() {
        val old = getSharedPreferences("last_game", MODE_PRIVATE)
        val u = old.getString("uri", null) ?: return
        val uri = Uri.parse(u)
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            u
        }
        if (library.games.none { it.key == key }) {
            val info = fileInfo(uri)
            val name = info?.first?.let(Library::nameOf) ?: old.getString("title", null) ?: "last game"
            library.played(GameEntry(key, u, name, lastPlayed = System.currentTimeMillis(), outside = true, size = info?.second ?: 0))
            library.save()
        }
        old.edit().clear().apply()
    }

    /** The game's bytes (from a .gba or a .zip), or null. */
    private fun readGame(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use(RomFile::read)
    } catch (e: Exception) {
        null
    }

    /** The game's id, reading the game if the list doesn't know it yet (then remembered); null if it can't be read. */
    private fun idOf(g: GameEntry): String? {
        if (g.id.isNotEmpty()) return g.id
        val bytes = readGame(Uri.parse(g.uri)) ?: run { toast("Can't open ${g.name}."); return null }
        val id = GameId.of(bytes)
        library.played(g.copy(id = id))
        library.save()
        rows = library.rows()
        return id
    }

    private fun removeGame(g: GameEntry) {
        library.remove(g.key)
        library.save()
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(g.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: Exception) {
        }
        rows = library.rows()
    }

    private fun exportSave(g: GameEntry) {
        val id = idOf(g) ?: return
        val bytes = SaveFiles.export(filesDir, id) ?: run { toast("No save for this game yet."); return }
        exportBytes = bytes
        exportTo.launch("${g.name}.sav")
    }

    private fun writeExport(uri: Uri) {
        val bytes = exportBytes ?: return
        exportBytes = null
        try {
            contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } ?: error("no stream")
            toast("Save exported.")
        } catch (e: Exception) {
            toast("Couldn't write the save there.")
        }
    }

    private fun importPicked(uri: Uri) {
        val g = saveGame ?: return
        saveGame = null
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        if (!SaveFiles.validSize(bytes.size)) {
            toast("That file isn't a save for this game.")
            return
        }
        val id = idOf(g) ?: return
        if (SaveFiles.file(filesDir, id).isFile) replaceSave = g to bytes else importSave(g, bytes)
    }

    private fun importSave(g: GameEntry, bytes: ByteArray) {
        val id = idOf(g) ?: return
        when (SaveFiles.import(filesDir, id, bytes)) {
            SaveFiles.Import.OK -> toast("Save imported. ${g.name} will start from the title screen.")
            SaveFiles.Import.WRONG_SIZE -> toast("That file isn't a save for this game.")
            SaveFiles.Import.FAILED -> toast("Couldn't write the save.")
        }
        libraryRefresh++ // the picture of where it was left is gone
    }

    /** Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true) {
        val uri = Uri.parse(entry.uri)
        val bytes = try {
            contentResolver.openInputStream(uri)?.use(RomFile::read)
        } catch (e: RomFile.NotAGame) {
            message = "Not a GBA game."
            return
        } catch (e: Exception) {
            null
        }
        if (bytes == null) {
            message = "Can't open ${entry.name}."
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        val gameId = GameId.of(bytes)
        val bios = if (settings.biosFile) {
            Bios.load(filesDir) ?: null.also { toast("The BIOS file is missing; the built-in BIOS is used.") }
        } else null
        NativeBridge.setBios(bios)
        val own = gameSettings.of(gameId)
        NativeBridge.setGameOverrides(own.saveType, own.clock)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA game."
            return
        }
        if (own == GameSettings.Game()) NativeBridge.cartSetup().let { gameSettings.remember(gameId, it[0], it[1]) }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val played = entry.copy(id = gameId, lastPlayed = System.currentTimeMillis())
        library.played(played)
        library.save()
        rows = library.rows()
        message = null
        val st = States(filesDir, gameId)
        states = st
        cheatStore = Cheats(filesDir, gameId).also { store ->
            cheats = store.load() ?: emptyList<Cheat>().also {
                toast("This game's cheats file couldn't be read; it was kept aside and the game starts without cheats.")
            }
        }
        current = played
        currentTitle = played.name
        if (resumeAuto && st.used(States.AUTO)) {
            if (st.load(States.AUTO)) {
                startGame()
                return
            }
            toast("Couldn't resume where you left off; starting the game.")
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    private fun importFrom(uri: Uri) {
        val bytes = readBytes(uri)
        when {
            bytes == null -> importError = "Could not read that file."
            NativeBridge.importSave(bytes) != 0 -> importError = "This .sav doesn't fit this game."
            else -> {
                askImport = false
                startGame()
            }
        }
    }

    private fun startGame() {
        keyboardMask = 0
        touchMask = 0
        fastSwitch.reset()
        fastOn = false
        repeatFilter.clear()
        controlsHidden = false
        if (hardware and (NativeBridge.HW_TILT or NativeBridge.HW_GYRO or NativeBridge.HW_SOLAR) != 0) {
            motion = MotionInput(this, hardware) { toast(it) }
        }
        if (hardware and NativeBridge.HW_RUMBLE != 0) rumble = Rumble(this)
        // A fresh SurfaceView for every session: Restart stops and starts within one frame, and the new render
        // thread only gets a window when the view's surface is created (the old one kept the last frame).
        session++
        sendCheats() // after the ROM, its save file and the automatic state: before the first frame
        NativeBridge.start()
        playing = true
        setFullScreen(true)
        startExtras()
    }

    /** The cartridge's sensors and rumble run only while the game does. */
    private fun startExtras() {
        motion?.start()
        rumble?.start()
    }

    private fun stopExtras() {
        motion?.stop()
        rumble?.stop()
    }

    private fun stopGame() {
        stopExtras()
        motion = null
        rumble = null
        NativeBridge.stop() // writes the save
        playing = false
        menu = null
        setFullScreen(false)
    }

    /** Back: the game pauses (sound, sensors, rumble too; the save file is written) behind the menu. */
    private fun openMenu() {
        stopExtras()
        NativeBridge.pause()
        slotsChanged++
        menu = MenuPage.MAIN
    }

    private fun resumeGame() {
        menu = null
        NativeBridge.resume()
        startExtras()
    }

    private fun saveTo(slot: Int) {
        replaceSlot = null
        if (states?.save(slot) == true) toast("Saved to slot $slot") else toast("Couldn't save the state.")
        slotsChanged++
        resumeGame()
    }

    private fun loadFrom(slot: Int) {
        if (states?.load(slot) == true) resumeGame()
        else toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The moment the player left, for resuming it next time (the game must be paused). */
    private fun saveAuto() {
        if (playing) states?.save(States.AUTO)
    }

    private fun quitGame() {
        saveAuto()
        stopGame()
        refreshLibrary() // it is now first, with the picture of where it was left
    }

    private fun restartGame() {
        askRestart = false
        val g = current ?: return
        stopGame()
        open(g, resumeAuto = false)
    }

    /** Buttons to the game; ⏩ and ⟲ (touch or keys) to the switch and the emulation thread. */
    private fun sendKeys(now: Long = SystemClock.uptimeMillis()) {
        val all = KeyMap.merge(keyboardMask, touchMask)
        NativeBridge.setKeys(all and KeyMap.GBA_BUTTONS)
        val fastDown = all and KeyMap.FAST != 0
        if (fastDown != fastSwitch.held) {
            if (fastDown) fastSwitch.press(now) else fastSwitch.release(now)
        }
        fastOn = fastSwitch.switchedOn
        NativeBridge.setFastForward(if (fastSwitch.fast) fastSpeed else 0)
        NativeBridge.setRewinding(all and KeyMap.REWIND != 0)
    }

    private fun releaseRepeatedKeys() {
        for ((bit, upAt) in repeatFilter.due(SystemClock.uptimeMillis())) {
            keyboardMask = keyboardMask and bit.inv()
            sendKeys(upAt) // the hold ended at the key-up, which decides tap or hold
        }
    }

    /** The panel changed the list (the game is paused behind the menu): keep it and switch the cheats. */
    private fun changeCheats(list: List<Cheat>) {
        cheats = list
        if (cheatStore?.save(list) == false) toast("Couldn't save the cheats.")
        sendCheats()
    }

    /** The switched-on cheats into the emulator; one that no longer reads is switched off (and said so). */
    private fun sendCheats() {
        val bad = cheats.filter { it.on && NativeBridge.checkCheat(it.codes, it.type)[1] != 0 }
        if (bad.isNotEmpty()) {
            for (c in bad) toast("Cheat '${c.name}' can't be read and was switched off.")
            cheats = cheats.map { if (it in bad) it.copy(on = false) else it }
            cheatStore?.save(cheats)
        }
        val on = cheats.filter { it.on }
        if (NativeBridge.setCheats(on.map { it.codes }.toTypedArray(), on.map { it.type }.toIntArray()) != 0 && on.isNotEmpty()) {
            toast("The cheats couldn't be switched on.")
        }
    }

    private fun speedLabel(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

    /** The menu's fast-forward row: 2× → 4× → 8× → Max → 2×, remembered for all games. */
    private fun nextFastSpeed() {
        changeSettings(settings.copy(fastSpeed = when (fastSpeed) {
            2 -> 4
            4 -> 8
            8 -> NativeBridge.FF_MAX
            else -> 2
        }))
    }

    /** A setting changed (Settings screen, menu row): kept, and used at once where it can be. */
    private fun changeSettings(s: Settings) {
        settings = s
        s.save(settingsStore)
        fastSpeed = s.fastSpeed
        applySettings(s)
        if (playing) sendKeys() // a switched-on fast mode uses the new speed
    }

    /** Picture and sound at once; rewind for the next game (the touch buttons read [settings] themselves). */
    private fun applySettings(s: Settings) {
        NativeBridge.setPicture(s.size, s.smooth)
        NativeBridge.setSound(s.sound, s.soundBursts)
        NativeBridge.setRewindBudget(s.rewindBudget)
    }

    private fun biosPicked(uri: Uri) {
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        when (Bios.check(bytes)) {
            Bios.Check.WRONG_SIZE -> toast("That isn't a GBA BIOS (it must be 16 KB).")
            Bios.Check.UNUSUAL -> unusualBios = bytes
            Bios.Check.USUAL -> installBios(bytes)
        }
    }

    private fun installBios(bytes: ByteArray) {
        if (!Bios.install(filesDir, bytes)) {
            toast("Couldn't copy the BIOS file.")
            return
        }
        changeSettings(settings.copy(biosFile = true))
        toast("BIOS file chosen: it is used the next time a game opens.")
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()

    private fun setFullScreen(on: Boolean) {
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (on) {
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        waitingKey?.let { (bit, device) -> // Settings → Keyboard & controller: this key is the button's now
            if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_ESCAPE) {
                if (event.action == KeyEvent.ACTION_UP) waitingKey = null
            } else if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                keys.assign(bit, device, event.keyCode)
                keys.use()
                keysChanged++
                waitingKey = null
            }
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_ESCAPE) { // Esc on a keyboard = Back (the menu)
            if (event.action == KeyEvent.ACTION_UP) onBackPressedDispatcher.onBackPressed()
            return true
        }
        val motionKey = MotionMath.motionKey(event.keyCode)
        val m = motion
        if (playing && motionKey != 0 && m != null) { // I/J/K/L tilt, Q/E turn
            when (event.action) {
                KeyEvent.ACTION_DOWN -> m.setKey(motionKey, true)
                KeyEvent.ACTION_UP -> m.setKey(motionKey, false)
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.isKeyboardExtra(event.keyCode)) { // F / R: see KeyRepeatFilter
            val bit = KeyMap.bitFor(event.keyCode)
            when (event.action) {
                KeyEvent.ACTION_DOWN -> if (repeatFilter.down(bit, event.eventTime)) {
                    keyboardMask = keyboardMask or bit
                    sendKeys(event.eventTime)
                }
                KeyEvent.ACTION_UP -> {
                    val wait = repeatFilter.up(bit, event.eventTime)
                    if (wait >= 0) handler.postDelayed(::releaseRepeatedKeys, wait)
                }
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.bitFor(event.keyCode) != 0) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, true)
                KeyEvent.ACTION_UP -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, false)
            }
            controlsHidden = true // a keyboard or gamepad is in use
            sendKeys()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        stopExtras()
        NativeBridge.pause() // returns once the save is written
        saveAuto() // Home, another app, screen off: keep the moment
    }

    override fun onResume() {
        super.onResume()
        if (playing && menu == null && replaceSlot == null && !askRestart) {
            NativeBridge.resume()
            startExtras()
        }
        if (!playing) refreshLibrary() // games added to the folder meanwhile, back from a picker
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit && bash tools/dev.sh test
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: Kotlin 69/69, `404 tests, 0 failed`, CTest 120/120, on-device `tests="53" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)** — in the user's library (games folder `Download/GBA` with Claude's test file `m7_demo.gba`); the user's settings, keys, game settings and BIOS copy are kept aside first and put back at the end. Real BIOS on the PC (skipped without the user's file):

```bash
[ -f tests/bios/gba_bios.bin ] && for f in 60 400; do bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --bios tests/bios/gba_bios.bin --frames $f --screenshot "$SP/pc_bios_$f.png" >/dev/null; echo "exit $?"; done
```

Expected: `exit 0` twice; `pc_bios_60.png` shows the GAME BOY / Nintendo logo, `pc_bios_400.png` the demo.

Use a scratchpad folder `$SP`. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
shot() { ADB exec-out screencap -p > "$SP/$1.png"; }
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
centre_of() {  # "x y" of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            echo "$((($1 + $3) / 2)) $((($2 + $4) / 2))"
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
tap_on() { local p; p=$(centre_of "$1" "$2") || return 1; ADB shell input tap $p; }
hold_on() {  # press and hold 1 s (a same-point "input swipe" is no long press for Compose)
    local p; p=$(centre_of "$1" "$2") || return 1
    set -- $p
    ADB shell "input motionevent DOWN $1 $2; sleep 1; input motionevent UP $1 $2"
}
pick_download() {  # the file picker: its side menu → Downloads (tapped by its place on the Pixel 9), then $1
    tap_on content-desc "Show roots" || return 1
    sleep 3
    ADB shell input tap 259 657
    for i in 1 2 3 4 5 6 7 8 9 10; do texts | grep -qF 'text="Files in Downloads"' && break; sleep 1; done
    for i in 1 2 3 4 5; do
        if texts | grep -qF "text=\"$1\""; then tap_on text "$1"; return; fi
        ADB shell input swipe 540 1900 540 700 400; sleep 1.5
    done
    echo "could not pick $1 from Downloads" >&2
    return 1
}
same_game() {  # the top 1080x720 (the game, upright) of two screenshots: identical?
    python -c "from PIL import Image; import sys; a=Image.open(sys.argv[1]).crop((0,0,1080,720)); b=Image.open(sys.argv[2]).crop((0,0,1080,720)); print('same' if a.tobytes()==b.tobytes() else 'DIFFERENT')" "$(cygpath -w "$SP/$1.png")" "$(cygpath -w "$SP/$2.png")"
}
brightness() {  # mean brightness of a region x0 y0 x1 y1 of a screenshot
    python -c "from PIL import Image, ImageStat; import sys; print(round(ImageStat.Stat(Image.open(sys.argv[1]).convert('L').crop(tuple(map(int, sys.argv[2:6])))).mean[0], 1))" "$(cygpath -w "$SP/$1.png")" $2 $3 $4 $5
}
colour_at() { python -c "from PIL import Image; import sys; print(Image.open(sys.argv[1]).convert('RGB').getpixel((int(sys.argv[2]), int(sys.argv[3]))))" "$(cygpath -w "$SP/$1.png")" $2 $3; }
```

Then (as a script, `bash <file>`; it stops if Settings is not in the library menu):

```bash
PREFS="settings keys game_settings"
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
bash tools/dev.sh install
# the user's settings, keys, game settings and BIOS copy kept aside (put back at the end); the check runs on the defaults
for f in $PREFS; do
    ADB shell run-as com.tengen.gbaemu cp shared_prefs/$f.xml shared_prefs/$f.xml.keep 2>/dev/null
    ADB shell run-as com.tengen.gbaemu rm -f shared_prefs/$f.xml
done
ADB shell run-as com.tengen.gbaemu cp files/bios/gba_bios.bin files/gba_bios.bin.keep 2>/dev/null
ADB shell run-as com.tengen.gbaemu rm -f files/bios/gba_bios.bin
bash tools/dev.sh launch; sleep 4
texts | head -3                                                                  # the library (the games folder Download/GBA)
tap_on text "⋮" && sleep 1 && tap_on text "Settings" || { echo "STOP: no Settings in the library menu"; exit 1; }
sleep 1.5; texts                                                                 # the six categories
tap_on text "Picture"; sleep 1; tap_on text "Size"; sleep 1; texts | tail -4
tap_on text "Whole-number steps"; sleep 1; tap_on text "Pixels"; sleep 1; tap_on text "Smooth"; sleep 1
ADB shell input keyevent KEYCODE_BACK; sleep 1; texts | sed -n '3,4p'
tap_on text "Keyboard &amp; controller" || tap_on text "Keyboard & controller"; sleep 1
tap_on text "X"; sleep 1; ADB shell input keyevent KEYCODE_Q; sleep 1; texts | sed -n '6,8p'  # A: Q | A
ADB shell run-as com.tengen.gbaemu cat shared_prefs/keys.xml
ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1
tap_on text "m7_demo"; sleep 4; shot k0
ADB shell input keyevent KEYCODE_X; sleep 1.5; shot k1; echo "old key X: $(same_game k0 k1)"
ADB shell input keyevent KEYCODE_Q; sleep 1.5; shot k2; echo "new key Q: $(same_game k1 k2)"
ADB shell settings put system user_rotation 1; sleep 3
ADB shell input tap 1200 540; sleep 1.5; shot side_normal                        # a touch shows the buttons again
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5
ADB shell input swipe 1200 900 1200 300 400; sleep 1                             # sideways the list is longer than the screen
tap_on text "Touch buttons"; sleep 1; tap_on text "Sideways, over the picture"; sleep 1; tap_on text "Strong"; sleep 1
ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1
tap_on text "▶  Resume"; sleep 1.5; ADB shell input tap 1200 540; sleep 1.5; shot side_strong
echo "buttons (left side): normal $(brightness side_normal 0 0 420 1080), strong $(brightness side_strong 0 0 420 1080)"
ADB shell settings put system user_rotation 0; sleep 2
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Quit"; sleep 3
hold_on text "m7_demo"; sleep 1.5; tap_on text "Game settings…"; sleep 1.5; texts | sed -n '1,5p'
tap_on text "Done"; sleep 1
if [ -f tests/bios/gba_bios.bin ]; then                                          # the user's own BIOS (never committed)
    ADB push "$(cygpath -w tests/bios/gba_bios.bin)" /sdcard/Download/gba_bios.bin
    tap_on text "⋮"; sleep 1; tap_on text "Settings"; sleep 1.5; tap_on text "BIOS"; sleep 1
    tap_on text "Choose a BIOS file…"; sleep 3; pick_download gba_bios.bin; sleep 2; texts | sed -n '3,5p'
    ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1
    tap_on text "m7_demo"; sleep 1.5; shot bios_boot; colour_at bios_boot 540 100  # the logo's white background
    sleep 5; ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Quit"; sleep 2
fi
ADB logcat -d -s AndroidRuntime:E | tail -3
ADB shell am force-stop com.tengen.gbaemu
# (found while replaying: "run-as … sh -c '…'" breaks — adb joins and re-splits the words; plain commands, and
# adb's exit status says whether a kept file exists)
for f in $PREFS; do
    if ADB shell run-as com.tengen.gbaemu ls shared_prefs/$f.xml.keep >/dev/null 2>&1; then
        ADB shell run-as com.tengen.gbaemu mv shared_prefs/$f.xml.keep shared_prefs/$f.xml
    else
        ADB shell run-as com.tengen.gbaemu rm -f shared_prefs/$f.xml
    fi
done
if ADB shell run-as com.tengen.gbaemu ls files/gba_bios.bin.keep >/dev/null 2>&1; then
    ADB shell run-as com.tengen.gbaemu mv files/gba_bios.bin.keep files/bios/gba_bios.bin
else
    ADB shell run-as com.tengen.gbaemu rm -f files/bios/gba_bios.bin
fi
bash tools/dev.sh launch; sleep 3; texts | head -3                               # the user's own settings again
```

Expected:
- The library, then Settings with the defaults (the user's own are kept aside and back at the end — found while replaying: a check that assumes the defaults must set them): 'Picture' 'Sharp · Fit screen', 'Sound' 'On · Low delay', 'Fast-forward & rewind' '4× · Rewind about 2½ minutes', 'Keyboard & controller', 'Touch buttons' 'Normal · Buzz on', 'BIOS' 'Built-in'.
- The Size pick list: 'Fit screen', 'Whole-number steps', 'Stretch to fill', 'Cancel'; after the changes 'Smooth · Whole-number steps'.
- The key table: A's keyboard key 'Q'; keys.xml `<int name="kbd.1" value="45" />`. In the demo: `old key X: same`, `new key Q: DIFFERENT`.
- Sideways: the left side's brightness higher with Strong than with Normal.
- Game settings — m7_demo: 'Save type' 'Auto (Not known yet)', 'Clock' 'Auto (none)'.
- With the user's BIOS: 'BIOS file' on the BIOS page; `bios_boot`'s pixel `(255, 255, 255)` (the logo's white) — `bios_boot.png` shows the GAME BOY logo.
- No `AndroidRuntime` crash; at the end the library, and the user's `settings.xml` / `keys.xml` / `game_settings.xml` byte-identical to before (compare their `md5sum` before and after the script).

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-6b-settings.md
git commit -m "feat(android): the Settings screen - picture, sound, speed and rewind, remapping, touch buttons, BIOS; game settings" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 6b exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 120/120 (sanitizers on, no reports), 404 unit tests, Kotlin 69, on-device 53 (emulator only).

- [x] **Step 2: The user's check** (the app installed on the emulator)

In game A, from the in-game menu's Settings…: smooth pixels, a size, a remapped key, the sideways look take effect; game settings and the BIOS choice work (back to Built-in before resuming game A: its automatic state was made with the built-in BIOS).

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (6b ✅ with the numbers and the user's check; Next = milestone 6c brainstorming — skins + layout editor); spec status → `Implemented — milestone 6b complete <date> (tag milestone-6b)`; parent spec §10 row 6 → 6b ✅; `CLAUDE.md` key files → 6c needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-6b replay-6b`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 6b complete - settings and controller remapping" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-6b
```
