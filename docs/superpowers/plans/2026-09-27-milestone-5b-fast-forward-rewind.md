# Milestone 5b — Fast-forward and rewind — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Fast-forward (tap or hold; 2×/4×/8×/Max from the in-game menu; sped-up sound, Max silent) and rewind (hold; ~3 minutes; silent), exact by construction: all 32 fingerprints still match after a rewind, on the PC and the phone emulator.

**Architecture:** The core keeps a snapshot every 4 frames (`gba_state_save`), each stored as its XOR difference to the next, packed by runs of unchanged bytes (`delta_pack`), in a 64 MiB ring (`rewind.c`); stepping back unpacks the newest difference into the full "current" snapshot and loads it. The Android emulation thread runs N frames per step with the resampler ratio ÷ N (sped-up sound, sound pacing unchanged), or no pacing at Max, or one step back per 1/60 s while rewinding. Kotlin turns the ⏩/⟲ touch buttons, F/R and R2/L2 into those two controls (`FastForwardSwitch`, `KeyRepeatFilter`).

**Tech Stack:** C11 (core, PC tests, Android NDK), CMake/CTest, Kotlin + Jetpack Compose, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-27-milestone-5b-fast-forward-rewind-design.md`

---

## Conventions (same as 5a)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its `files/saves` / `files/states` (except the test states named in a step) without the user's OK. Starting a closed emulator (`dev.sh emu`) is fine; ask before restarting/cold-booting it.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file or CMake tests. One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-5b` (already created; the spec commits are on it). Checkpoints with the user after Tasks 3 and 5; Task 7 is the user's check and the 5b exit.
- This plan's code is the prototype's (worktree `<scratchpad>/p5b`, branch `proto-5b`), which the user tried with their game A hack on 2026-09-27 ('Everything works now').

## File map

```
core/src/delta_pack.{h,c}; tests/unit/test_delta_pack.c                                                   Task 1
core/src/rewind.{h,c}, core/include/gba.h, core/include/gba_debug.h, core/src/gba.c, core/src/gba_debug.c,
  core/src/gba_internal.h, core/src/state.c; tests/unit/test_rewind.c                                       Task 2
tools/testrunner/main.c, CMakeLists.txt (rewind_<name> tests)                                               Task 3
platform/android/emu_thread.{h,c}, jni_bridge.c; app/.../NativeBridge.kt; app/src/androidTest/.../RewindTest.kt   Task 4
app/.../FastForwardSwitch.kt, KeyRepeatFilter.kt, KeyMap.kt, TouchLayout.kt; app/src/test/.../FastForwardSwitchTest.kt,
  KeyRepeatFilterTest.kt, KeyMapTest.kt, TouchLayoutTest.kt                                                  Task 5
app/.../TouchControlsView.kt, Screens.kt, MainActivity.kt                                                   Task 6
```

---
### Task 1: The packer

**Files:**
- Create: `core/src/delta_pack.h`, `core/src/delta_pack.c`, `tests/unit/test_delta_pack.c`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_delta_pack.c`:

```c
/* The rewind packer (core/src/delta_pack.c): unpacking gives back exactly the other snapshot. */
#include <stdlib.h>
#include <string.h>

#include "delta_pack.h"
#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng = 12345;
static uint32_t next_random(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* Packs a→b, checks the size bound, unpacks into a copy of a and expects b; returns the packed size. */
static size_t round_trip(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t *packed = malloc(delta_pack_bound(n));
    uint8_t *buf = malloc(n ? n : 1);
    size_t len = delta_pack(a, b, n, packed);
    CHECK(len <= delta_pack_bound(n));
    memcpy(buf, a, n);
    CHECK(delta_unpack(packed, len, buf, n));
    CHECK(memcmp(buf, b, n) == 0);
    CHECK(delta_unpack(packed, len, buf, n)); /* and back again */
    CHECK(memcmp(buf, a, n) == 0);
    free(packed);
    free(buf);
    return len;
}

TEST(delta_pack_unchanged_bytes_take_almost_nothing) {
    static uint8_t a[100000];
    for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)(i * 13);
    CHECK(round_trip(a, a, sizeof a) <= 4);
}

TEST(delta_pack_every_byte_changed_stays_within_the_bound) {
    enum { N = 5000 };
    uint8_t *a = malloc(N), *b = malloc(N);
    for (size_t i = 0; i < N; i++) {
        a[i] = (uint8_t)i;
        b[i] = (uint8_t)~i;
    }
    size_t len = round_trip(a, b, N);
    CHECK(len <= N + 8);
    free(a);
    free(b);
}

TEST(delta_pack_random_changes_and_odd_sizes) {
    for (size_t n = 0; n < 200; n += 7) {
        for (int pattern = 0; pattern < 4; pattern++) {
            uint8_t *a = malloc(n + 1), *b = malloc(n + 1);
            for (size_t i = 0; i < n; i++) a[i] = b[i] = (uint8_t)next_random();
            for (size_t i = 0; i < n; i++) {
                int change = pattern == 0 ? next_random() % 50 == 0   /* rare single bytes */
                             : pattern == 1 ? (i / 5) % 3 == 1         /* short runs, short gaps */
                             : pattern == 2 ? (i / 9) % 2 == 0         /* runs and gaps around the cut-off */
                                            : next_random() % 2 == 0;  /* half of everything */
                if (change) b[i] ^= (uint8_t)(1 + next_random() % 255);
            }
            round_trip(a, b, n);
            free(a);
            free(b);
        }
    }
}

TEST(delta_pack_two_real_snapshots_pack_small) {
    static const uint32_t ops[] = {0xE3A01402, 0xE2800001, 0xE5810000, 0xEAFFFFFC}; /* count into EWRAM */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    size_t n = gba_state_size(g);
    uint8_t *a = malloc(n), *b = malloc(n);
    gba_run_frame(g);
    gba_state_save(g, a, n);
    for (int i = 0; i < 4; i++) gba_run_frame(g);
    gba_state_save(g, b, n);
    CHECK(round_trip(a, b, n) < n / 20);
    free(a);
    free(b);
    gba_destroy(g);
}

TEST(delta_pack_broken_input_is_refused) {
    enum { N = 64 };
    uint8_t a[N], b[N], packed[2 * N + 2], buf[N + 1];
    for (size_t i = 0; i < N; i++) {
        a[i] = (uint8_t)i;
        b[i] = (uint8_t)(i % 3 ? i : ~i);
    }
    size_t len = delta_pack(a, b, N, packed);
    memcpy(buf, a, N);
    CHECK(!delta_unpack(packed, len - 1, buf, N)); /* cut short */
    memcpy(buf, a, N);
    CHECK(!delta_unpack(packed, len, buf, N - 1)); /* describes more bytes than there are */
    memcpy(buf, a, N);
    buf[N] = 0;
    CHECK(!delta_unpack(packed, len, buf, N + 1)); /* describes fewer */
    memcpy(buf, a, N);
    CHECK(delta_unpack(packed, len, buf, N)); /* the right size is fine */
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: the build fails — `fatal error: 'delta_pack.h' file not found`.

- [x] **Step 3: Write the code**

`core/src/delta_pack.h`:

```c
#ifndef GBA_DELTA_PACK_H
#define GBA_DELTA_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The difference of two equal-sized snapshots, packed: the XOR of the two as records of
 * (unchanged-byte count, changed-byte count, the changed bytes XORed), counts as variable-length
 * integers. Snapshots a moment apart are mostly unchanged, so the records are few and short. */

/* The most bytes delta_pack can write for n-byte snapshots. */
size_t delta_pack_bound(size_t n);
/* Packs a XOR b (n bytes each) into out (delta_pack_bound(n) bytes); returns the bytes written. */
size_t delta_pack(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out);
/* XORs a packed difference into buf (n bytes): buf = a turns into b and back. False, and buf
 * possibly half-changed, if the packed bytes do not describe exactly n bytes. */
bool delta_unpack(const uint8_t *in, size_t len, uint8_t *buf, size_t n);

#endif
```

`core/src/delta_pack.c`:

```c
#include "delta_pack.h"

#include <string.h>

/* A run of fewer unchanged bytes than this stays inside the changed bytes: a new record would cost more. */
#define MIN_ZERO_RUN 8

/* Every record is at most 2 counts + its changed bytes; a count takes at most as many bytes as it
 * counts (1 byte per 7 bits), so all records together take at most 2n, plus 2 for an empty first
 * run and an empty last run. */
size_t delta_pack_bound(size_t n) {
    return 2 * n + 2;
}

static uint8_t *put_count(uint8_t *p, size_t v) {
    while (v >= 0x80) {
        *p++ = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    *p++ = (uint8_t)v;
    return p;
}

static bool get_count(const uint8_t **p, const uint8_t *end, size_t *v) {
    size_t r = 0;
    for (unsigned shift = 0; *p < end && shift < 8 * sizeof(size_t); shift += 7) {
        uint8_t b = *(*p)++;
        r |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *v = r;
            return true;
        }
    }
    return false;
}

/* The unchanged bytes from i on. Word by word while it can: 650 KB go through here 15 times a second. */
static size_t same_from(const uint8_t *a, const uint8_t *b, size_t i, size_t n) {
    size_t j = i;
    while (j + 8 <= n) {
        uint64_t x, y;
        memcpy(&x, a + j, 8);
        memcpy(&y, b + j, 8);
        if (x != y) break;
        j += 8;
    }
    while (j < n && a[j] == b[j]) j++;
    return j - i;
}

size_t delta_pack(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out) {
    uint8_t *p = out;
    size_t i = 0;
    while (i < n) {
        size_t zeros = same_from(a, b, i, n);
        size_t start = i + zeros;
        size_t end = start; /* the changed bytes run until MIN_ZERO_RUN unchanged ones in a row, or the end */
        while (end < n) {
            if (a[end] != b[end]) {
                end++;
                continue;
            }
            size_t same = same_from(a, b, end, n);
            if (same >= MIN_ZERO_RUN || end + same == n) break;
            end += same;
        }
        p = put_count(p, zeros);
        p = put_count(p, end - start);
        for (size_t k = start; k < end; k++) *p++ = a[k] ^ b[k];
        i = end;
    }
    return (size_t)(p - out);
}

bool delta_unpack(const uint8_t *in, size_t len, uint8_t *buf, size_t n) {
    const uint8_t *p = in, *end = in + len;
    size_t i = 0;
    while (p < end) {
        size_t zeros, lits;
        if (!get_count(&p, end, &zeros) || !get_count(&p, end, &lits)) return false;
        if (zeros > n - i || lits > n - i - zeros || lits > (size_t)(end - p)) return false;
        i += zeros;
        for (size_t k = 0; k < lits; k++) buf[i + k] ^= p[k];
        i += lits;
        p += lits;
    }
    return i == n;
}
```

(Why the bound holds: every record is at most two counts plus its changed bytes; a count takes at most as many bytes as its value, and every record but the first has ≥ 8 unchanged bytes and every record but the last ≥ 1 changed byte — so the whole is ≤ 2n + 2.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: `374 tests, 0 failed` (369 + 5); no warnings.

- [x] **Step 5: Commit**

```bash
git add core/src/delta_pack.h core/src/delta_pack.c tests/unit/test_delta_pack.c docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "feat(core): delta_pack - the difference of two snapshots as runs of unchanged and changed bytes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Rewind in the core

**Files:**
- Create: `core/src/rewind.h`, `core/src/rewind.c`, `tests/unit/test_rewind.c`
- Modify: `core/include/gba.h`, `core/include/gba_debug.h`, `core/src/gba.c`, `core/src/gba_debug.c`, `core/src/gba_internal.h`, `core/src/state.c`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_rewind.c`:

```c
/* Rewind (core/src/rewind.c): stepping back gives exactly the machine of an earlier snapshot. */
#include <stdlib.h>
#include <string.h>

#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

/* Keeps changing the machine: counts in r0, stores it to EWRAM, loops. */
static const uint32_t counter_ops[] = {0xE3A01402, 0xE2800001, 0xE5810000, 0xEAFFFFFC};

#define BUDGET (4u * 1024 * 1024)
#define EVERY 4
#define MAX_SNAPS 16

static uint8_t *state_of(const gba_t *g) {
    size_t n = gba_state_size(g);
    uint8_t *buf = malloc(n);
    CHECK_EQ(gba_state_save(g, buf, n), n);
    return buf;
}

static int same_as(const gba_t *g, const uint8_t *want) {
    uint8_t *now = state_of(g);
    int same = memcmp(now, want, gba_state_size(g)) == 0;
    free(now);
    return same;
}

/* Runs `snaps` × EVERY frames, keeping a copy of the machine at each snapshot (after frames 4, 8, ...). */
static void run_keeping(gba_t *g, int snaps, uint8_t **kept) {
    for (int s = 0; s < snaps; s++) {
        for (int f = 0; f < EVERY; f++) gba_run_frame(g);
        kept[s] = state_of(g);
    }
}

static void free_all(uint8_t **kept, int n) {
    for (int i = 0; i < n; i++) free(kept[i]);
}

TEST(rewind_steps_back_through_every_snapshot_exactly) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 6, kept);
    size_t snaps, bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 6);
    for (int s = 4; s >= 0; s--) { /* at the newest: the first step goes to the one before */
        CHECK_EQ(gba_rewind_step_back(g), 1);
        CHECK(same_as(g, kept[s]));
    }
    uint8_t *oldest = state_of(g);
    CHECK_EQ(gba_rewind_step_back(g), 0); /* nothing older: the machine stays */
    CHECK(same_as(g, oldest));
    free(oldest);
    free_all(kept, 6);
    gba_destroy(g);
}

TEST(rewind_between_snapshots_goes_to_the_newest_first) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 3, kept);
    gba_run_frame(g);
    gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[2]));
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[1]));
    free_all(kept, 3);
    gba_destroy(g);
}

TEST(rewind_playing_on_after_a_step_back_keeps_snapshots_every_4_frames) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 4, kept);
    CHECK_EQ(gba_rewind_step_back(g), 1); /* back to the 3rd snapshot */
    uint8_t *again[MAX_SNAPS];
    run_keeping(g, 2, again); /* two new snapshots after it */
    CHECK(memcmp(again[0], kept[3], gba_state_size(g)) == 0); /* the same machine as the first time */
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, again[0]));
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[2]));
    free_all(kept, 4);
    free_all(again, 2);
    gba_destroy(g);
}

TEST(rewind_drops_the_oldest_when_the_budget_is_full) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 2, kept);
    size_t snaps, one;
    gba_debug_rewind_stats(g, &snaps, &one); /* one packed difference */
    free_all(kept, 2);
    CHECK_EQ(gba_rewind_configure(g, 3 * one, EVERY), GBA_OK); /* room for about two */
    run_keeping(g, 8, kept);
    size_t bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK(snaps >= 2 && snaps <= 4);
    CHECK(bytes <= 3 * one);
    for (size_t s = 1; s < snaps; s++) {
        CHECK_EQ(gba_rewind_step_back(g), 1);
        CHECK(same_as(g, kept[7 - s]));
    }
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 8);
    gba_destroy(g);
}

TEST(rewind_history_is_forgotten_by_a_loaded_state_a_reset_and_configuring) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 3, kept);
    size_t snaps, bytes;
    CHECK_EQ(gba_state_load(g, kept[0], gba_state_size(g)), GBA_OK);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 0);
    free_all(kept, 3);

    run_keeping(g, 3, kept);
    gba_reset(g);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 3);

    run_keeping(g, 3, kept);
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 3);
    gba_destroy(g);
}

TEST(rewind_off_without_a_budget) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    for (int f = 0; f < 8; f++) gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 0); /* never configured */
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    CHECK_EQ(gba_rewind_configure(g, 0, EVERY), GBA_OK);
    for (int f = 0; f < 8; f++) gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    size_t snaps, bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 0);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: the build fails — `call to undeclared function 'gba_rewind_configure'` (and `gba_rewind_step_back`, `gba_debug_rewind_stats`).

- [x] **Step 3: Write the code**

Replace `core/include/gba.h`:

```c
#ifndef GBA_H
#define GBA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gba gba_t;

typedef struct {
    uint32_t flags; /* reserved; pass 0 */
} gba_config_t;

typedef enum {
    GBA_OK = 0,
    GBA_ERR_ROM_SIZE,
    GBA_ERR_BIOS_SIZE,
    GBA_ERR_NOMEM,
    GBA_ERR_SAVE_SIZE, /* a save file whose size does not fit the game's save chip */
    GBA_ERR_STATE,     /* a save state of another game, another format or version, or damaged */
} gba_err_t;

enum {
    GBA_KEY_A = 1 << 0,
    GBA_KEY_B = 1 << 1,
    GBA_KEY_SELECT = 1 << 2,
    GBA_KEY_START = 1 << 3,
    GBA_KEY_RIGHT = 1 << 4,
    GBA_KEY_LEFT = 1 << 5,
    GBA_KEY_UP = 1 << 6,
    GBA_KEY_DOWN = 1 << 7,
    GBA_KEY_R = 1 << 8,
    GBA_KEY_L = 1 << 9,
};

gba_t *gba_create(const gba_config_t *cfg); /* cfg may be NULL */
void gba_destroy(gba_t *g);

/* Optional 16 KiB BIOS dump. Call before gba_load_rom; without it the built-in HLE BIOS is used. */
gba_err_t gba_load_bios(gba_t *g, const uint8_t *bios, size_t size);
/* Copies the ROM and resets the system. */
gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size);
void gba_reset(gba_t *g);

void gba_run_frame(gba_t *g);                    /* runs until the next VBlank starts */
void gba_set_keys(gba_t *g, uint16_t pressed);   /* GBA_KEY_* mask of held buttons */
/* The 240x160 picture in BGR555 (bit 0-4 red, 5-9 green, 10-14 blue), row-major. Lines are
 * drawn as the frame runs, so read it after gba_run_frame returns. */
const uint16_t *gba_framebuffer(const gba_t *g);
#define GBA_AUDIO_RATE 32768 /* stereo samples per second from gba_audio_read */
/* Copies up to max_frames stereo samples (interleaved int16 left, right) made since the last call
 * and returns how many. The core keeps about 3.7 frames of sound; anything older is dropped. */
size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames);

const char *gba_rom_title(const gba_t *g);
const char *gba_rom_code(const gba_t *g);
uint32_t gba_rom_crc32(const gba_t *g); /* CRC-32 (as zlib) of the ROM bytes given to gba_load_rom */

/* Save states: the whole machine between two gba_run_frame calls (save RAM, clock and sensors
 * included). A state loads only into the same ROM and only a state of this build's format; anything
 * else is refused with GBA_ERR_STATE and changes nothing. */
size_t gba_state_size(const gba_t *g);
size_t gba_state_save(const gba_t *g, void *buf, size_t cap); /* bytes written; 0 if cap is too small */
gba_err_t gba_state_load(gba_t *g, const void *buf, size_t len); /* also forgets the rewind history */

/* Rewind: with a budget, gba_run_frame keeps a snapshot every interval_frames frames, each packed as its
 * difference to the next; the oldest are dropped when the budget is full. This reserves all the memory
 * (GBA_ERR_NOMEM: rewind stays off); a budget of 0 turns rewind off. The history is forgotten by
 * gba_reset, gba_load_rom, gba_state_load and configuring again. */
gba_err_t gba_rewind_configure(gba_t *g, size_t budget_bytes, int interval_frames);
/* The machine back to the previous snapshot (the newest one first, if frames ran since it): 1, or 0
 * with the machine unchanged when there is none. The picture is the snapshot's at once. */
int gba_rewind_step_back(gba_t *g);

typedef enum {
    GBA_SAVE_NONE,      /* no save chip */
    GBA_SAVE_UNKNOWN,   /* no marker string: decided on first use */
    GBA_SAVE_SRAM,      /* 32 KiB */
    GBA_SAVE_FLASH64,   /* 64 KiB */
    GBA_SAVE_FLASH128,  /* 128 KiB, two banks */
    GBA_SAVE_EEPROM,    /* EEPROM, size not known yet */
    GBA_SAVE_EEPROM512, /* 512 B */
    GBA_SAVE_EEPROM8K,  /* 8 KiB */
} gba_save_type_t;

gba_save_type_t gba_save_type(const gba_t *g);
uint8_t *gba_saveram(gba_t *g);          /* the save bytes, valid for gba_saveram_size bytes */
size_t gba_saveram_size(const gba_t *g); /* 0 while there is no chip or its size is unknown */
/* Puts a save file back (after gba_load_rom). The size must fit the chip; for an unknown type or
 * an EEPROM of unknown size, the size picks the chip. GBA_ERR_SAVE_SIZE changes nothing. */
gba_err_t gba_load_saveram(gba_t *g, const uint8_t *data, size_t size);
int gba_saveram_dirty(gba_t *g); /* 1 if the game saved since the last call (then clears it) */
void gba_set_save_type(gba_t *g, gba_save_type_t type); /* manual override; blanks the save */

enum {
    GBA_HW_RTC = 1 << 0,    /* real-time clock */
    GBA_HW_SOLAR = 1 << 1,  /* solar (light) sensor */
    GBA_HW_GYRO = 1 << 2,   /* rotation sensor */
    GBA_HW_RUMBLE = 1 << 3, /* rumble motor */
    GBA_HW_TILT = 1 << 4,   /* two-axis tilt sensor */
};

uint32_t gba_cart_hardware(const gba_t *g);           /* GBA_HW_* extras of the loaded cartridge */
void gba_set_cart_hardware(gba_t *g, uint32_t flags); /* manual override (resets the extras' state) */
/* The clock shows this time now and keeps running with emulated time (call after gba_load_rom;
 * without it the clock starts at 2000-01-01 00:00:00 when the ROM is loaded). */
void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s);
void gba_set_light(gba_t *g, uint8_t level);       /* solar sensor: 0 = dark .. 255 = full sun */
void gba_set_gyro(gba_t *g, int16_t z_rate);       /* rotation speed: > 0 clockwise, ±32767 = very fast */
void gba_set_tilt(gba_t *g, int16_t x, int16_t y); /* ±32767 = fully tilted, 0 = level */
int gba_rumble_active(const gba_t *g);             /* 1 while the rumble motor runs */

#ifdef __cplusplus
}
#endif

#endif
```

Replace `core/include/gba_debug.h`:

```c
#ifndef GBA_DEBUG_H
#define GBA_DEBUG_H

#include <stdint.h>

#include "gba.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Introspection for tools and tests; not part of the frontend API. */
uint32_t gba_debug_reg(const gba_t *g, unsigned n);  /* r0-r15 of the current mode; r15 = next instruction */
uint32_t gba_debug_cpsr(const gba_t *g);
uint32_t gba_debug_peek32(gba_t *g, uint32_t addr);  /* no cycles or side effects; addr aligned down */
void gba_debug_step(gba_t *g);                       /* execute exactly one instruction */
uint64_t gba_debug_cycles(const gba_t *g);
/* Rewind history: snapshots kept (the newest full one included) and packed bytes used. */
void gba_debug_rewind_stats(const gba_t *g, size_t *snapshots, size_t *bytes);

#ifdef __cplusplus
}
#endif

#endif
```

Replace `core/src/gba_internal.h`:

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "apu/apu.h"
#include "cart/cart.h"
#include "sys/dma.h"
#include "cpu/cpu.h"
#include "mem/bus.h"
#include "mem/io.h"
#include "ppu/ppu.h"
#include "sys/irq.h"
#include "sys/sched.h"
#include "sys/sio.h"
#include "sys/timer.h"

struct gba {
    arm_cpu_t cpu;
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    timers_t timers;
    dma_t dma;
    apu_t apu;
    sio_t sio;
    sched_t sched;
    cart_t cart;
    uint64_t now;      /* master clock: 16.78 MHz cycles since reset */
    bool frame_done;   /* set when VBlank starts; ends gba_run_frame */
    bool hle_bios;     /* true: built-in BIOS stub + C SWIs; false: user BIOS */
    uint16_t keyinput; /* KEYINPUT register: bit clear = pressed */
    struct rewind *rewind; /* rewind history (rewind.c), NULL while off; in no state chunk */
};

/* gba_state_load without forgetting the rewind history (rewind.c loads its own snapshots with it). */
gba_err_t state_load(gba_t *g, const void *buf, size_t len);

/* Dispatch every scheduler event due at g->now, in timestamp order. */
void gba_run_events(gba_t *g);

#endif
```

(The ring pointer lives in `struct gba` outside every state chunk: saving a state never includes the history, loading one never overwrites the pointer.)

`core/src/rewind.h`:

```c
#ifndef GBA_REWIND_H
#define GBA_REWIND_H

#include <stddef.h>

typedef struct gba gba_t;
typedef struct rewind rewind_t;

/* End of every gba_run_frame: a snapshot every interval_frames frames (nothing when rewind is off). */
void rewind_after_frame(gba_t *g);
/* Forget the history (reset, a new ROM, a loaded state); the memory stays reserved. */
void rewind_clear(rewind_t *r);
void rewind_free(rewind_t *r);
/* For measuring: snapshots kept (the current one included) and ring bytes used. */
void rewind_stats(const rewind_t *r, size_t *snapshots, size_t *bytes);

#endif
```

`core/src/rewind.c`:

```c
/* Rewind: a snapshot every few frames, each kept as its difference to the one after it.
 *
 * `current` is the newest snapshot in full. The ring holds packed differences, newest last; unpacking
 * the newest into `current` turns it into the snapshot before. Each record is its length, the packed
 * bytes, and its length again, so the newest can be popped from the end and the oldest dropped from
 * the start. All memory is reserved by gba_rewind_configure; nothing is allocated while frames run. */
#include <stdlib.h>
#include <string.h>

#include "delta_pack.h"
#include "gba_internal.h"
#include "rewind.h"

#define LEN_SIZE 4u /* a record's length before and after it */

struct rewind {
    size_t state_size;
    uint8_t *current; /* the newest snapshot (valid when have_current) */
    uint8_t *fresh;   /* a new snapshot, before it becomes current */
    uint8_t *packed;  /* delta_pack_bound(state_size) bytes */
    uint8_t *ring;
    size_t cap;   /* ring bytes */
    size_t head;  /* where the oldest record starts */
    size_t used;  /* bytes of records */
    size_t count; /* records */
    bool have_current;
    int interval; /* frames between snapshots */
    int since;    /* frames run since the last snapshot or step back */
};

static void ring_put(rewind_t *r, size_t at, const uint8_t *src, size_t n) {
    at %= r->cap;
    size_t first = n < r->cap - at ? n : r->cap - at;
    memcpy(r->ring + at, src, first);
    memcpy(r->ring, src + first, n - first);
}

static void ring_get(const rewind_t *r, size_t at, uint8_t *dst, size_t n) {
    at %= r->cap;
    size_t first = n < r->cap - at ? n : r->cap - at;
    memcpy(dst, r->ring + at, first);
    memcpy(dst + first, r->ring, n - first);
}

static uint32_t ring_len(const rewind_t *r, size_t at) {
    uint8_t b[LEN_SIZE];
    ring_get(r, at, b, LEN_SIZE);
    uint32_t v;
    memcpy(&v, b, LEN_SIZE);
    return v;
}

static void drop_oldest(rewind_t *r) {
    size_t rec = LEN_SIZE + ring_len(r, r->head) + LEN_SIZE;
    r->head = (r->head + rec) % r->cap;
    r->used -= rec;
    r->count--;
}

void rewind_clear(rewind_t *r) {
    if (!r) return;
    r->head = r->used = r->count = 0;
    r->have_current = false;
    r->since = 0;
}

void rewind_free(rewind_t *r) {
    if (!r) return;
    free(r->current);
    free(r->fresh);
    free(r->packed);
    free(r->ring);
    free(r);
}

void rewind_stats(const rewind_t *r, size_t *snapshots, size_t *bytes) {
    *snapshots = r ? r->count + (r->have_current ? 1 : 0) : 0;
    *bytes = r ? r->used : 0;
}

gba_err_t gba_rewind_configure(gba_t *g, size_t budget_bytes, int interval_frames) {
    rewind_free(g->rewind);
    g->rewind = NULL;
    if (budget_bytes == 0 || interval_frames < 1) return GBA_OK;
    rewind_t *r = calloc(1, sizeof *r);
    if (!r) return GBA_ERR_NOMEM;
    r->state_size = gba_state_size(g);
    r->current = malloc(r->state_size);
    r->fresh = malloc(r->state_size);
    r->packed = malloc(delta_pack_bound(r->state_size));
    r->ring = malloc(budget_bytes);
    if (!r->current || !r->fresh || !r->packed || !r->ring) {
        rewind_free(r);
        return GBA_ERR_NOMEM;
    }
    r->cap = budget_bytes;
    r->interval = interval_frames;
    g->rewind = r;
    return GBA_OK;
}

void rewind_after_frame(gba_t *g) {
    rewind_t *r = g->rewind;
    if (!r || ++r->since < r->interval) return;
    r->since = 0;
    gba_state_save(g, r->fresh, r->state_size);
    if (r->have_current) {
        size_t n = delta_pack(r->fresh, r->current, r->state_size, r->packed);
        size_t rec = LEN_SIZE + n + LEN_SIZE;
        if (rec > r->cap) {
            rewind_clear(r); /* a difference bigger than the whole budget: start the history again */
        } else {
            while (r->cap - r->used < rec) drop_oldest(r);
            uint32_t len = (uint32_t)n;
            size_t at = r->head + r->used;
            ring_put(r, at, (const uint8_t *)&len, LEN_SIZE);
            ring_put(r, at + LEN_SIZE, r->packed, n);
            ring_put(r, at + LEN_SIZE + n, (const uint8_t *)&len, LEN_SIZE);
            r->used += rec;
            r->count++;
        }
    }
    uint8_t *t = r->current;
    r->current = r->fresh;
    r->fresh = t;
    r->have_current = true;
}

int gba_rewind_step_back(gba_t *g) {
    rewind_t *r = g->rewind;
    if (!r || !r->have_current) return 0;
    if (r->since == 0) { /* at the newest snapshot: go to the one before */
        if (r->count == 0) return 0;
        size_t end = r->head + r->used;
        uint32_t n = ring_len(r, end - LEN_SIZE);
        ring_get(r, end - LEN_SIZE - n, r->packed, n);
        if (!delta_unpack(r->packed, n, r->current, r->state_size)) {
            rewind_clear(r); /* cannot happen with our own records; never load a broken snapshot */
            return 0;
        }
        r->used -= LEN_SIZE + n + LEN_SIZE;
        r->count--;
    }
    if (state_load(g, r->current, r->state_size) != GBA_OK) {
        rewind_clear(r);
        return 0;
    }
    r->since = 0;
    return 1;
}
```

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

(`gba_state_load` = `state_load` + forget the history; rewind itself loads its snapshots with `state_load`, all of 5a's checks included.)

Replace `core/src/gba.c`:

```c
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "gba_internal.h"
#include "rewind.h"
#include "sys/keypad.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    cpu_init_tables();
    io_init_tables();
    gba_t *g = calloc(1, sizeof(gba_t));
    if (!g) return NULL;
    g->hle_bios = true;
    gba_reset(g);
    return g;
}

void gba_destroy(gba_t *g) {
    if (!g) return;
    cart_unload(&g->cart);
    rewind_free(g->rewind);
    free(g);
}

gba_err_t gba_load_bios(gba_t *g, const uint8_t *bios, size_t size) {
    if (size != BIOS_SIZE) return GBA_ERR_BIOS_SIZE;
    memcpy(g->bus.bios, bios, BIOS_SIZE);
    g->hle_bios = false;
    return GBA_OK;
}

gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    gba_err_t err = cart_load(&g->cart, rom, size);
    if (err == GBA_OK) {
        hardware_setup(g);
        gba_reset(g);
    }
    return err;
}

void gba_reset(gba_t *g) {
    rewind_clear(g->rewind);
    hardware_reset(g); /* before the cycle count restarts: it re-bases the clock */
    g->now = 0;
    g->frame_done = false;
    g->keyinput = 0x3FF;
    sched_init(&g->sched);
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
    save_reset(&g->cart.save);
    timer_reset(g);
    dma_reset(g);
    sio_reset(g);
    apu_reset(g);
    ppu_reset(g);
    cpu_reset(g);
    if (g->hle_bios) hle_boot(g);
}

void gba_run_events(gba_t *g) {
    uint64_t when;
    int id;
    while ((id = sched_pop_due(&g->sched, g->now, &when)) >= 0) {
        switch (id) {
        case EV_HBLANK: ppu_on_hblank(g, when); break;
        case EV_LINE_END: ppu_on_line_end(g, when); break;
        case EV_TIMER0:
        case EV_TIMER1:
        case EV_TIMER2:
        case EV_TIMER3: timer_on_overflow(g, id - EV_TIMER0, when); break;
        case EV_DMA: dma_run_immediate(g); break;
        case EV_APU_FRAME: apu_on_frame_seq(g, when); break;
        case EV_APU_SAMPLE: apu_on_sample(g, when); break;
        case EV_SIO: sio_on_transfer_done(g); break;
        default: break;
        }
    }
}

static void run_frame(gba_t *g) {
    g->frame_done = false;
    while (!g->frame_done) {
        if (g->irq.stopped) {
            if (!irq_stop_wake(g)) {
                apu_stopped_frame(g); /* nothing inside moves; the frontend still gets its sound */
                return;
            }
            g->irq.stopped = false;
        }
        /* Re-read the next event after every instruction: an I/O write can schedule one sooner
         * (an immediate DMA must run before the CPU continues). */
        while (g->now < sched_next(&g->sched) && !g->irq.stopped) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && irq_wake_at(g) < next) {
                        g->now = irq_wake_at(g); /* the request reaches the halted CPU: wake then */
                        continue;
                    }
                    g->now = next; /* nothing can happen before the next event */
                    break;
                }
                g->irq.halted = false;
            }
            cpu_step(g);
        }
        gba_run_events(g);
    }
}

void gba_run_frame(gba_t *g) {
    run_frame(g);
    rewind_after_frame(g);
}

void gba_set_keys(gba_t *g, uint16_t pressed) {
    g->keyinput = (uint16_t)(~pressed & 0x3FF);
    keypad_check(g);
}

const uint16_t *gba_framebuffer(const gba_t *g) {
    return g->ppu.framebuffer;
}

size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames) {
    return apu_read(g, out, max_frames);
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

uint32_t gba_rom_crc32(const gba_t *g) {
    return g->cart.crc32;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
```

Replace `core/src/gba_debug.c`:

```c
#include "gba_debug.h"

#include "gba_internal.h"
#include "rewind.h"

uint32_t gba_debug_reg(const gba_t *g, unsigned n) {
    return n < 16 ? g->cpu.r[n] : 0;
}

uint32_t gba_debug_cpsr(const gba_t *g) {
    return g->cpu.cpsr;
}

uint32_t gba_debug_peek32(gba_t *g, uint32_t addr) {
    return bus_peek(g, addr, 4);
}

void gba_debug_step(gba_t *g) {
    cpu_step(g);
}

uint64_t gba_debug_cycles(const gba_t *g) {
    return g->now;
}

void gba_debug_rewind_stats(const gba_t *g, size_t *snapshots, size_t *bytes) {
    rewind_stats(g->rewind, snapshots, bytes);
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `380 tests, 0 failed` (374 + 6); CTest 88/88 (rewind is off unless configured); no warnings.

- [x] **Step 5: Commit**

```bash
git add core/src/rewind.h core/src/rewind.c tests/unit/test_rewind.c core/include/gba.h core/include/gba_debug.h core/src/gba.c core/src/gba_debug.c core/src/gba_internal.h core/src/state.c docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "feat(core): rewind - a packed snapshot every few frames in a fixed budget, step back exactly" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Exact replays through rewind (PC)

**Files:**
- Modify: `CMakeLists.txt`, `tools/testrunner/main.c`

- [x] **Step 1: The failing tests — a `rewind_<name>` twin for every approved fingerprint**

Replace `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(gba_emulator LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(GBA_SANITIZE "Build host binaries with AddressSanitizer and UBSan" OFF)
if(GBA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tools/common)
    add_subdirectory(tools/testrunner)
    if(WIN32)
        add_subdirectory(tools/viewer)
    endif()

    # jsmolka/gba-tests ROMs, fetched by tools/fetch_test_roms.py; skipped when missing.
    # Each entry is name:result-register (the Thumb ROM keeps its result in r7).
    set(JSMOLKA_DIR ${PROJECT_SOURCE_DIR}/tests/roms/jsmolka)
    foreach(entry arm:12 thumb:7 memory:12 bios:12 sram:12 flash64:12 flash128:12 none:12)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 reg)
        add_test(NAME jsmolka_${name} COMMAND testrunner ${JSMOLKA_DIR}/${name}.gba --result-reg ${reg})
        set_tests_properties(jsmolka_${name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    # Our own test ROMs that report through r12 (0 = pass).
    foreach(name save_eeprom512 save_eeprom8k)
        add_test(NAME own_${name} COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/${name}.gba --result-reg 12)
    endforeach()

    # Approved frame fingerprints; each runs a ROM for N frames and compares the picture hash.
    set(FRAMES_FILE ${PROJECT_SOURCE_DIR}/tests/expected_frames.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FRAMES_FILE})
    file(READ ${FRAMES_FILE} FRAMES_JSON)
    string(JSON frame_count LENGTH "${FRAMES_JSON}" frames)
    if(frame_count GREATER 0)
        math(EXPR last_frame "${frame_count} - 1")
        foreach(i RANGE ${last_frame})
            string(JSON f_name GET "${FRAMES_JSON}" frames ${i} name)
            string(JSON f_rom GET "${FRAMES_JSON}" frames ${i} rom)
            string(JSON f_frames GET "${FRAMES_JSON}" frames ${i} frames)
            string(JSON f_hash GET "${FRAMES_JSON}" frames ${i} hash)
            string(JSON f_audio ERROR_VARIABLE audio_missing GET "${FRAMES_JSON}" frames ${i} audio)
            set(audio_args)
            if(NOT audio_missing)
                set(audio_args --expect-audio-hash ${f_audio})
            endif()
            add_test(NAME frame_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(frame_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # The same run through a save state: half-way it continues in a brand-new machine.
            math(EXPR f_half "${f_frames} / 2")
            if(f_half LESS 1)
                set(f_half 1)
            endif()
            add_test(NAME state_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --state-at ${f_half} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(state_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # And through rewind (a snapshot every 4 frames): at two thirds (short runs: the last frame but one),
            # step back half of the snapshots so far, at least 2 - so every run unpacks at least one difference.
            math(EXPR f_rewind_at "${f_frames} * 2 / 3")
            if(f_rewind_at LESS 9)
                math(EXPR f_rewind_at "${f_frames} - 1")
            endif()
            math(EXPR f_rewind_steps "${f_rewind_at} / 4 / 2")
            if(f_rewind_steps LESS 2)
                set(f_rewind_steps 2)
            endif()
            add_test(NAME rewind_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --rewind-at ${f_rewind_at}:${f_rewind_steps} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(rewind_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()

    # mGBA test suite (tests/roms/mgba/suite.gba): each group must reach its minimum score.
    set(SUITE_FILE ${PROJECT_SOURCE_DIR}/tests/suite_targets.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SUITE_FILE})
    file(READ ${SUITE_FILE} SUITE_JSON)
    string(JSON suite_count LENGTH "${SUITE_JSON}" groups)
    math(EXPR last_group "${suite_count} - 1")
    foreach(i RANGE ${last_group})
        string(JSON s_group MEMBER "${SUITE_JSON}" groups ${i})
        string(JSON s_min GET "${SUITE_JSON}" groups ${s_group})
        add_test(NAME mgba_suite_${s_group}
                 COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/mgba/suite.gba --suite ${s_group} --suite-min ${s_min})
        set_tests_properties(mgba_suite_${s_group} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
```

(At two thirds of the run — short runs: the last frame but one — step back half of the snapshots so far, at least 2, so every run unpacks at least one difference.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh test -R "^rewind_"
```

Expected: `0% tests passed, 32 tests failed out of 32` (the runner does not know `--rewind-at` yet: usage, exit 3).

- [x] **Step 3: The runner**

Replace `tools/testrunner/main.c`:

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 *              [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]] [--state-at <frame>]
 *              [--rewind-at <frame>:<steps>]
 * With --result-reg: run until the CPU spins on a branch-to-self (or <n> frames, default 600);
 * register n == 0 means PASS (jsmolka gba-tests keep the first failing test number there).
 * Otherwise with --frames: run exactly <n> frames. Otherwise: run until idle (at most 600 frames).
 * The sound of every frame is collected: --wav saves it, --audio-hash fingerprints it.
 * --save loads the file into the save chip first (when it exists) and writes the chip back after.
 * --rtc <unix seconds> sets the cartridge clock; --hardware <rtc,solar,gyro,rumble,tilt|none> forces
 * the cartridge extras.
 * --keys holds buttons from the given frames on: "10:DOWN,12:-,14:A+B" (- = none). Key names: A B
 * SELECT START RIGHT LEFT UP DOWN R L.
 * --suite runs one group of the mGBA test suite (suite.gba): memory io timing timer-countup timer-irq
 * shifter carry multiply-long bios-math dma sio-regs sio-timing misc. It prints "SUITE <group> p/t";
 * with --suite-min it fails when p is below n.
 * --state-at k (with --frames): after k frames, save a state, load it into a brand-new machine (same BIOS
 * and ROM) and run the rest there — the hashes then prove that a loaded state continues exactly.
 * --rewind-at f:s (with --frames): rewind on (64 MiB, a snapshot every 4 frames, as the app); after f frames,
 * step back s snapshots and run on from there to the end with the same keys - the hashes then prove that
 * rewinding is exact. The sound of the frames undone is taken out of the audio hash again.
 * Exit codes: 0 pass/done, 1 fail, 2 timeout, 3 usage/load error, 77 ROM missing (CTest skip).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "frame_hash.h"
#include "gba.h"
#include "gba_debug.h"
#include "png.h"
#include "wav.h"

#define REWIND_BUDGET (64u * 1024 * 1024)
#define REWIND_EVERY 4

enum { EXIT_PASS = 0, EXIT_FAILED = 1, EXIT_TIMEOUT = 2, EXIT_USAGE = 3, EXIT_SKIP = 77 };

#define MAX_KEY_EVENTS 64

typedef struct {
    long frame;
    uint16_t keys; /* GBA_KEY_* held from this frame on */
} key_event_t;

typedef struct {
    key_event_t ev[MAX_KEY_EVENTS];
    int count;
} key_script_t;

/* "10:DOWN,12:-,14:A+B" → events in frame order; false for a bad entry or frames that do not increase. */
static bool parse_keys(const char *list, key_script_t *ks) {
    static const struct {
        const char *name;
        uint16_t key;
    } names[] = {{"A", GBA_KEY_A},         {"B", GBA_KEY_B},         {"SELECT", GBA_KEY_SELECT},
                 {"START", GBA_KEY_START}, {"RIGHT", GBA_KEY_RIGHT}, {"LEFT", GBA_KEY_LEFT},
                 {"UP", GBA_KEY_UP},       {"DOWN", GBA_KEY_DOWN},   {"R", GBA_KEY_R},
                 {"L", GBA_KEY_L}};
    const size_t name_count = sizeof names / sizeof names[0];
    const char *p = list;
    ks->count = 0;
    while (*p) {
        char *end;
        long frame = strtol(p, &end, 10);
        if (end == p || *end != ':' || frame < 0 || ks->count == MAX_KEY_EVENTS) return false;
        if (ks->count > 0 && frame <= ks->ev[ks->count - 1].frame) return false;
        p = end + 1;
        uint16_t keys = 0;
        if (*p == '-') {
            p++;
        } else {
            for (;;) {
                size_t len = strcspn(p, "+,");
                size_t k = 0;
                while (k < name_count && !(strlen(names[k].name) == len && !strncmp(p, names[k].name, len))) k++;
                if (k == name_count) return false;
                keys |= names[k].key;
                p += len;
                if (*p != '+') break;
                p++;
            }
        }
        ks->ev[ks->count++] = (key_event_t){frame, keys};
        if (*p == ',') p++;
        else if (*p) return false;
    }
    return true;
}

/* The keys held during a frame: those of the last event at or before it. */
static uint16_t keys_at(const key_script_t *ks, long frame) {
    uint16_t keys = 0;
    for (int i = 0; i < ks->count && ks->ev[i].frame <= frame; i++) keys = ks->ev[i].keys;
    return keys;
}

static const char *const SUITE_GROUPS[] = {"memory", "io", "timing", "timer-countup", "timer-irq",
                                           "shifter", "carry", "multiply-long", "bios-math", "dma",
                                           "sio-regs", "sio-timing", "misc"};
enum { SUITE_GROUP_COUNT = sizeof SUITE_GROUPS / sizeof SUITE_GROUPS[0], SUITE_MAX_FRAMES = 3600 };

static int suite_group_index(const char *name) {
    for (int i = 0; i < SUITE_GROUP_COUNT; i++)
        if (!strcmp(name, SUITE_GROUPS[i])) return i;
    return -1;
}

/* The suite draws its text as BG1 tiles (screen block 1 at 0x06000800, 32 per row) numbered
 * character - ' '. A finished group shows "%4u/%-4u" on row 1, columns 21-29. */
static bool read_suite_score(gba_t *g, unsigned *passed, unsigned *total) {
    char text[10];
    for (int col = 0; col < 9; col++) {
        uint32_t addr = 0x06000800u + (uint32_t)(32 + 21 + col) * 2;
        uint32_t word = gba_debug_peek32(g, addr);
        uint16_t entry = (addr & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        text[col] = (char)((entry & 0x3FF) + ' ');
    }
    text[9] = '\0';
    return sscanf(text, "%u/%u", passed, total) == 2;
}

typedef struct {
    uint64_t hash;
    wav_writer_t wav;
    bool writing;
} audio_out_t;

static bool at_idle_loop(gba_t *g) {
    uint32_t pc = gba_debug_reg(g, 15);
    if (gba_debug_cpsr(g) & (1u << 5)) { /* Thumb: b . */
        uint32_t word = gba_debug_peek32(g, pc & ~3u);
        uint16_t op = (pc & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        return op == 0xE7FE;
    }
    return gba_debug_peek32(g, pc) == 0xEAFFFFFE; /* ARM: b . */
}

/* One frame, then collect every sound sample it made. */
static void run_frame(gba_t *g, audio_out_t *audio) {
    int16_t buf[1024 * 2];
    size_t n;
    gba_run_frame(g);
    while ((n = gba_audio_read(g, buf, 1024)) > 0) {
        audio->hash = audio_hash_update(audio->hash, buf, n * 2);
        if (audio->writing) wav_write(&audio->wav, buf, n);
    }
}

/* Opens menu entry `group` (DOWN presses, then A) and runs until the result screen shows a score. */
static int run_suite(gba_t *g, int group, long min_score, audio_out_t *audio) {
    key_script_t ks = {.count = 0};
    for (int i = 0; i <= group; i++) {
        long at = 10 + 4L * i;
        ks.ev[ks.count++] = (key_event_t){at, i < group ? GBA_KEY_DOWN : GBA_KEY_A};
        ks.ev[ks.count++] = (key_event_t){at + 2, 0};
    }
    long pressed_a = 10 + 4L * group;
    unsigned passed = 0, total = 0;
    for (long frame = 0; frame < SUITE_MAX_FRAMES; frame++) {
        gba_set_keys(g, keys_at(&ks, frame));
        run_frame(g, audio);
        if (frame > pressed_a + 2 && read_suite_score(g, &passed, &total)) {
            printf("SUITE %s %u/%u\n", SUITE_GROUPS[group], passed, total);
            if (min_score < 0 || (long)passed >= min_score) return EXIT_PASS;
            printf("FAIL suite %s: below the target of %ld\n", SUITE_GROUPS[group], min_score);
            return EXIT_FAILED;
        }
    }
    printf("TIMEOUT suite %s: no score after %d frames\n", SUITE_GROUPS[group], SUITE_MAX_FRAMES);
    return EXIT_TIMEOUT;
}

/* Prints PASS/FAIL for one fingerprint; true when it matches. */
static bool check_hash(const char *rom, const char *what, uint64_t actual, const char *expected) {
    uint64_t want = strtoull(expected, NULL, 16);
    if (actual == want) {
        printf("PASS %s (%s %016" PRIx64 ")\n", rom, what, actual);
        return true;
    }
    printf("FAIL %s: %s %016" PRIx64 ", expected %s\n", rom, what, actual, expected);
    return false;
}

/* "rtc,gyro" → GBA_HW_RTC | GBA_HW_GYRO; "none" → 0. False for an unknown name. */
static bool parse_hardware(const char *list, uint32_t *flags) {
    static const struct {
        const char *name;
        uint32_t flag;
    } names[] = {{"rtc", GBA_HW_RTC}, {"solar", GBA_HW_SOLAR}, {"gyro", GBA_HW_GYRO},
                 {"rumble", GBA_HW_RUMBLE}, {"tilt", GBA_HW_TILT}};
    char buf[128];
    *flags = 0;
    if (!strcmp(list, "none")) return true;
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        size_t k = 0;
        while (k < sizeof names / sizeof names[0] && strcmp(tok, names[k].name) != 0) k++;
        if (k == sizeof names / sizeof names[0]) return false;
        *flags |= names[k].flag;
    }
    return true;
}

/* Save a state of *g, load it into a new machine with the same BIOS and ROM, and carry on with that one. */
static bool switch_machine(gba_t **g, const uint8_t *bios, size_t bios_size, const uint8_t *rom, size_t rom_size) {
    size_t size = gba_state_size(*g);
    uint8_t *state = malloc(size);
    gba_t *fresh = gba_create(NULL);
    bool ok = state && fresh && gba_state_save(*g, state, size) == size &&
              (!bios || gba_load_bios(fresh, bios, bios_size) == GBA_OK) &&
              gba_load_rom(fresh, rom, rom_size) == GBA_OK && gba_state_load(fresh, state, size) == GBA_OK;
    free(state);
    if (!ok) {
        gba_destroy(fresh);
        return false;
    }
    gba_destroy(*g);
    *g = fresh;
    return true;
}

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file>]\n"
                    "                  [--screenshot <file.png>] [--hash] [--expect-hash <hex>]\n"
                    "                  [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <hex>]\n"
                    "                  [--rtc <unix seconds>] [--hardware <rtc,solar,gyro,rumble,tilt|none>]\n"
                    "                  [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]]\n"
                    "                  [--state-at <frame>] [--rewind-at <frame>:<steps>]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;
    const char *hardware = NULL;
    const char *keys_arg = NULL, *suite_arg = NULL;
    long suite_min = -1;
    long state_at = -1;
    long rewind_at = -1, rewind_steps = 0;
    bool rtc_given = false;
    long long rtc_seconds = 0;
    long frames = 600;
    bool frames_given = false, print_hash = false, print_audio = false;
    int result_reg = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 0);
            frames_given = true;
        } else if (!strcmp(argv[i], "--result-reg") && i + 1 < argc) result_reg = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--hash")) print_hash = true;
        else if (!strcmp(argv[i], "--expect-hash") && i + 1 < argc) expect_hash = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--audio-hash")) print_audio = true;
        else if (!strcmp(argv[i], "--expect-audio-hash") && i + 1 < argc) expect_audio = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else if (!strcmp(argv[i], "--rtc") && i + 1 < argc) {
            rtc_seconds = strtoll(argv[++i], NULL, 0);
            rtc_given = true;
        } else if (!strcmp(argv[i], "--hardware") && i + 1 < argc) hardware = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite") && i + 1 < argc) suite_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite-min") && i + 1 < argc) suite_min = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--state-at") && i + 1 < argc) state_at = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--rewind-at") && i + 1 < argc) {
            char *end;
            rewind_at = strtol(argv[++i], &end, 10);
            if (*end != ':') return usage();
            rewind_steps = strtol(end + 1, NULL, 10);
        }
        else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    uint32_t hw_flags = 0;
    if (!rom_path || result_reg > 15 || frames <= 0) return usage();
    if (state_at >= 0 && (!frames_given || result_reg >= 0 || state_at > frames)) return usage();
    if (hardware && !parse_hardware(hardware, &hw_flags)) return usage();
    /* The snapshot the steps reach: snapshots are taken after frames 4, 8, ...; from between two, the first
     * step goes to the newer one. */
    long rewind_to = rewind_at - rewind_at % REWIND_EVERY -
                     REWIND_EVERY * (rewind_steps - (rewind_at % REWIND_EVERY ? 1 : 0));
    if (rewind_at >= 0 && (!frames_given || result_reg >= 0 || state_at >= 0 || wav_path || rewind_at > frames ||
                           rewind_steps < 1 || rewind_to < REWIND_EVERY)) {
        return usage();
    }
    key_script_t keys = {.count = 0};
    if (keys_arg && !parse_keys(keys_arg, &keys)) return usage();
    int suite_group = -1;
    if (suite_arg && (suite_group = suite_group_index(suite_arg)) < 0) return usage();

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) {
        printf("SKIP %s (not found; run tools/fetch_test_roms.py)\n", rom_path);
        return EXIT_SKIP;
    }

    gba_t *g = gba_create(NULL);
    audio_out_t audio = {AUDIO_HASH_INIT, {NULL, 0, 0}, false};
    int code = EXIT_USAGE;
    uint8_t *bios = NULL; /* kept for --state-at's new machine */
    size_t bios_size = 0;
    if (bios_path) {
        bios = read_file(bios_path, &bios_size);
        gba_err_t err = bios ? gba_load_bios(g, bios, bios_size) : GBA_ERR_BIOS_SIZE;
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load BIOS %s\n", bios_path);
            goto done;
        }
    }
    if (gba_load_rom(g, rom, rom_size) != GBA_OK) {
        fprintf(stderr, "cannot load ROM %s\n", rom_path);
        goto done;
    }
    if (hardware) gba_set_cart_hardware(g, hw_flags);
    if (rtc_given) gba_set_rtc(g, rtc_seconds, 0);
    if (save_path) {
        size_t save_size;
        uint8_t *save = read_file(save_path, &save_size);
        if (save) {
            gba_err_t err = gba_load_saveram(g, save, save_size);
            free(save);
            if (err != GBA_OK) {
                fprintf(stderr, "cannot use %s: wrong size for this game's save chip\n", save_path);
                goto done;
            }
        }
    }
    if (wav_path) {
        if (wav_open(&audio.wav, wav_path, GBA_AUDIO_RATE) != 0) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            goto done;
        }
        audio.writing = true;
    }

    bool exact = frames_given && result_reg < 0;
    long frame = 0;
    if (suite_group >= 0) {
        code = run_suite(g, suite_group, suite_min, &audio);
    } else {
        if (exact) {
            uint64_t *hash_before = NULL; /* the audio hash before each frame's sound was read */
            if (rewind_at >= 0) {
                hash_before = malloc(sizeof(uint64_t) * (size_t)frames);
                if (!hash_before || gba_rewind_configure(g, REWIND_BUDGET, REWIND_EVERY) != GBA_OK) {
                    fprintf(stderr, "no memory for rewind\n");
                    free(hash_before);
                    goto done;
                }
            }
            bool rewound = false;
            for (; frame < frames; frame++) {
                if (frame == state_at && !switch_machine(&g, bios, bios_size, rom, rom_size)) {
                    printf("FAIL %s: the state saved at frame %ld did not load\n", rom_path, frame);
                    code = EXIT_FAILED;
                    goto done;
                }
                if (frame == rewind_at && !rewound) {
                    for (long s = 0; s < rewind_steps; s++) {
                        if (!gba_rewind_step_back(g)) {
                            printf("FAIL %s: rewind step %ld of %ld found no snapshot\n", rom_path, s + 1, rewind_steps);
                            free(hash_before);
                            code = EXIT_FAILED;
                            goto done;
                        }
                    }
                    /* The snapshot came after frame rewind_to - 1 ran, its sound not read yet: that is read next. */
                    frame = rewind_to;
                    audio.hash = hash_before[rewind_to - 1];
                    rewound = true;
                }
                if (hash_before) hash_before[frame] = audio.hash;
                gba_set_keys(g, keys_at(&keys, frame));
                run_frame(g, &audio);
            }
            free(hash_before);
        } else {
            while (frame < frames && !at_idle_loop(g)) {
                gba_set_keys(g, keys_at(&keys, frame));
                run_frame(g, &audio);
                frame++;
            }
        }

        code = EXIT_PASS;
        if (!exact && !at_idle_loop(g)) {
            printf("TIMEOUT %s after %ld frames (pc=%08X)\n", rom_path, frames, (unsigned)gba_debug_reg(g, 15));
            code = EXIT_TIMEOUT;
        } else if (result_reg >= 0) {
            uint32_t result = gba_debug_reg(g, (unsigned)result_reg);
            if (result == 0) {
                printf("PASS %s\n", rom_path);
            } else {
                printf("FAIL %s: test %u\n", rom_path, (unsigned)result);
                code = EXIT_FAILED;
            }
        } else if (!exact) {
            printf("DONE %s after %ld frames\n", rom_path, frame);
        }
    }

    const uint16_t *fb = gba_framebuffer(g);
    uint64_t hash = frame_hash(fb);
    if (print_hash) printf("HASH %016" PRIx64 "\n", hash);
    if (print_audio) printf("AUDIO %016" PRIx64 "\n", audio.hash);
    if (shot_path && png_write_bgr555(shot_path, fb, 240, 160) != 0) {
        fprintf(stderr, "cannot write %s\n", shot_path);
        code = EXIT_USAGE;
    }
    if (expect_hash && code == EXIT_PASS && !check_hash(rom_path, "frame", hash, expect_hash)) code = EXIT_FAILED;
    if (expect_audio && code == EXIT_PASS && !check_hash(rom_path, "audio", audio.hash, expect_audio)) {
        code = EXIT_FAILED;
    }

    size_t save_bytes = gba_saveram_size(g);
    if (save_path && save_bytes && write_file(save_path, gba_saveram(g), save_bytes) != 0) {
        fprintf(stderr, "cannot write %s\n", save_path);
        code = EXIT_USAGE;
    }

done:
    if (audio.writing && wav_close(&audio.wav) != 0) {
        fprintf(stderr, "cannot write %s\n", wav_path);
        code = EXIT_USAGE;
    }
    gba_destroy(g);
    free(bios);
    free(rom);
    return code;
}
```

(The snapshot is taken at the end of `gba_run_frame`, before the runner reads that frame's sound — so after a rewind the audio hash goes back to its value before that frame's sound, which is read again next.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh test -R "^rewind_"
bash tools/dev.sh test
```

Expected: `100% tests passed, 0 tests failed out of 32`, then **CTest 120/120**.

- [x] **Step 5: Prove the replays catch a bad unpack** (temporary sabotage, then restore)

```bash
cp core/src/delta_pack.c "$SP/delta_pack.c.keep"
python - <<'PYEOF'
p = 'core/src/delta_pack.c'
s = open(p, encoding='utf-8').read()
old = "        for (size_t k = 0; k < lits; k++) buf[i + k] ^= p[k];"
new = "        for (size_t k = 0; k < lits; k++) buf[i + k] ^= (k == 0 ? 0 : p[k]);"
assert s.count(old) == 1
open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
PYEOF
bash tools/dev.sh test -R "^rewind_" 2>&1 | grep -E "Failed|tests passed"
cp "$SP/delta_pack.c.keep" core/src/delta_pack.c && bash tools/dev.sh test -R "^rewind_" 2>&1 | grep "tests passed"
git status --short core/src/delta_pack.c
```

Expected: with the first changed byte of every run left out of unpacking, exactly 5 fail — `rewind_own_snd_dma`, `rewind_own_snd_mix`, `rewind_own_snd_psg`, `rewind_tonc_pageflip`, `rewind_tonc_snd1_demo` (`84% tests passed, 5 tests failed out of 32`; still pictures do not show it — the Task 1 unit tests do); restored: `100% tests passed … out of 32`; `git status` shows no change to `delta_pack.c` beyond Task 1's.

- [x] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/testrunner/main.c docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "test: every approved fingerprint also replayed through a rewind (CTest 120)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done — checkpoint with the user.**

---
### Task 4: Fast-forward and rewind on Android (C and JNI)

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt`
- Modify: `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Fast-forward and rewind on the phone: exact replays through a rewind, the speeds, rewinding a running game. */
@RunWith(AndroidJUnit4::class)
class RewindTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun everyApprovedFingerprintSurvivesARewind() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 32, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val frames = e.getInt("frames")
            // As CMake's rewind_<name> tests: at two thirds (short runs: the last frame but one), back half the snapshots, at least 2.
            val at = (frames * 2 / 3).let { if (it < 9) frames - 1 else it }
            val steps = maxOf(2, at / 4 / 2)
            val got = NativeBridge.debugRunFramesViaRewind(TestRoms.rom(e.getString("rom")), frames, at, steps)
            val parts = got?.split(" ") ?: listOf("rewind-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /** Frames the game runs in [ms] at the given fast-forward speed. */
    private fun framesIn(ms: Long, speed: Int): Long {
        NativeBridge.setFastForward(speed)
        Thread.sleep(300) // settle
        val from = NativeBridge.debugFrameCount()
        Thread.sleep(ms)
        return NativeBridge.debugFrameCount() - from
    }

    @Test
    fun fastForwardRunsThatManyTimesFaster() { // by the clock (no sound)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        val four = framesIn(1000, 4)
        val max = framesIn(1000, NativeBridge.FF_MAX)
        val back = framesIn(1000, 0)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertTrue("4x $four vs $normal", four >= normal * 3)
        assertTrue("max $max vs 4x $four", max >= four)
        assertTrue("back to normal $back", back in 50..70)
    }

    @Test
    fun fastForwardWithSoundIsPacedByTheSound() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertEquals("sound still playing", 1L, NativeBridge.debugAudioStats()[0])
    }

    @Test
    fun rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        NativeBridge.setKeys(KeyMap.UP)
        Thread.sleep(1000)
        NativeBridge.setKeys(0)
        val snaps = NativeBridge.debugRewindStats()[0]
        assertTrue("snapshots after 1 s: $snaps", snaps in 10..20)
        NativeBridge.setRewinding(true)
        val framesBefore = NativeBridge.debugFrameCount()
        Thread.sleep(snaps * 1000 / 60 + 700) // one step per 1/60 s, then some more at the oldest
        val stats = NativeBridge.debugRewindStats()
        assertEquals("no frames run while rewinding", framesBefore, NativeBridge.debugFrameCount())
        assertEquals("only the oldest snapshot left", 1L, stats[0])
        assertTrue("steps ${stats[2]} for $snaps snapshots", stats[2] in (snaps - 1)..(snaps + 2)) // a snapshot may come before rewinding starts
        NativeBridge.setRewinding(false)
        Thread.sleep(500)
        assertTrue("plays on", NativeBridge.debugFrameCount() > framesBefore + 20)
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: fails to compile — `Unresolved reference 'debugRunFramesViaRewind'`, `'setFastForward'`, `'FF_MAX'`, `'debugRewindStats'`, `'setRewinding'`.

- [x] **Step 3: The emulation thread**

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

/* Creates the core, loads the ROM (copied), sets the clock from the phone and loads the save file
 * at save_path if there is one (save_path NULL: no save file); NULL + *err on failure. From then on
 * the game's saves are written to save_path about 1 s after the game saves, and on pause and stop. */
emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, gba_err_t *err);
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
 * in-game save). The history is kept from the game's start (or its last loaded state), ~3 minutes. */
void emu_set_rewinding(emu_t *e, bool on);
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started), writes the save, frees everything */

/* Cartridge sensors (any thread; applied at the next frame start): tilt and turn ±32767, light 0..255. */
void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn);
void emu_set_light(emu_t *e, int level);
uint32_t emu_cart_hardware(const emu_t *e); /* GBA_HW_* extras of the loaded cartridge */
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
/* The current 240×160 BGR555 picture (for a state's thumbnail); same rule: only while paused. */
bool emu_frame(emu_t *e, uint16_t out[240 * 160]);

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
void emu_simulate_audio_disconnect(emu_t *e);        /* as if the sound device went away */
void emu_simulate_stuck_stream(emu_t *e);            /* the stream stops taking sound, unannounced */
unsigned emu_keys_seen(emu_t *e);
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
#define REWIND_BUDGET (64u * 1024 * 1024) /* ~3 minutes of snapshots (design: 5b) */
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

    /* Sound. The emulation thread alone touches out/rs/converted; the rest is shared as marked. */
    bool audio_wanted;                 /* set before start (test hook) */
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
    e->out = audio_out_open(&e->ring, &e->stats);
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
    if (e->audio_wanted) sound_open(e);
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

emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, gba_err_t *err) {
    emu_t *e = calloc(1, sizeof *e);
    if (!e || !(e->g = gba_create(NULL))) {
        free(e);
        *err = GBA_ERR_NOMEM;
        return NULL;
    }
    *err = gba_load_rom(e->g, rom, size);
    if (*err != GBA_OK) {
        gba_destroy(e->g);
        free(e);
        return NULL;
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
    e->audio_wanted = true;
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
    if (gba_rewind_configure(e->g, REWIND_BUDGET, REWIND_EVERY) != GBA_OK) LOGW("no memory for rewind: rewind off");
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
    if (!e->started) e->audio_wanted = enabled;
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

(While rewinding the sound ring is not fed: it runs dry and the stream plays silence — the ring is never cleared while the callback reads it.)

- [x] **Step 4: JNI and Kotlin**

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
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, path, &err);
    if (path) (*env)->ReleaseStringUTFChars(env, save_path, path);
    return err;
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
    if (!s_render) s_render = render_create(emu_frames(s_emu));
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
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
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

    const val ERR_STATE = 5

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
}
```

- [x] **Step 5: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: `380 tests, 0 failed`.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```
Expected: `tests="41" failures="0"` three times (37 + 4); `0` C warnings.

- [x] **Step 6: Commit**

```bash
git add platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "feat(android): fast-forward with sped-up sound and rewind in the emulation thread; 32 fingerprints through a rewind on the phone" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: Tap or hold, held keys, the buttons' places (Kotlin)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/FastForwardSwitch.kt`, `app/src/main/java/com/tengen/gbaemu/KeyRepeatFilter.kt`, `app/src/test/java/com/tengen/gbaemu/FastForwardSwitchTest.kt`, `app/src/test/java/com/tengen/gbaemu/KeyRepeatFilterTest.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`, `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`, `app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`, `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/FastForwardSwitchTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FastForwardSwitchTest {
    @Test
    fun aTapSwitchesItOnAndTheNextTapOff() {
        val s = FastForwardSwitch()
        assertFalse(s.fast)
        s.press(1000)
        assertTrue(s.fast) // fast at once, before knowing if it is a tap
        s.release(1150)
        assertTrue(s.fast && s.switchedOn)
        s.press(5000)
        s.release(5100)
        assertFalse(s.fast || s.switchedOn)
    }

    @Test
    fun holdingIsFastOnlyWhileHeld() {
        val s = FastForwardSwitch()
        s.press(1000)
        assertTrue(s.fast && !s.switchedOn)
        s.release(1000 + FastForwardSwitch.TAP_MS)
        assertFalse(s.fast)
    }

    @Test
    fun holdingWhileSwitchedOnEndsOff() {
        val s = FastForwardSwitch()
        s.press(0)
        s.release(100) // on
        s.press(2000)
        assertTrue(s.fast)
        s.release(3000) // a hold: back to normal speed
        assertFalse(s.fast || s.switchedOn)
    }

    @Test
    fun aSecondPressWhileHeldAndAStrayReleaseChangeNothing() {
        val s = FastForwardSwitch()
        s.release(50) // nothing was pressed
        assertFalse(s.fast)
        s.press(100)
        s.press(200) // a keyboard key repeating, or a second finger
        s.release(250) // 150 ms after the first press: a tap
        assertTrue(s.switchedOn)
    }

    @Test
    fun resetForgetsEverything() {
        val s = FastForwardSwitch()
        s.press(0)
        s.release(10)
        s.press(500)
        s.reset()
        assertFalse(s.fast || s.switchedOn || s.held)
    }
}
```

`app/src/test/java/com/tengen/gbaemu/KeyRepeatFilterTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class KeyRepeatFilterTest {
    private val bit = KeyMap.FAST

    @Test
    fun aRealPressAndReleaseEndsSoonAfterTheRelease() {
        val f = KeyRepeatFilter()
        assertTrue(f.down(bit, 1000)) // a new hold
        val wait = f.up(bit, 1400)
        assertEquals(KeyRepeatFilter.GAP_MS, wait)
        assertTrue(f.due(1400 + wait - 1).isEmpty())
        assertEquals(listOf(bit to 1400L), f.due(1400 + wait)) // released at the time of the key-up
        assertFalse(f.held(bit))
    }

    @Test
    fun aSingleEmulatorTapIsAShortHoldThatEndsAtOnce() { // F tapped: the switch flips without waiting
        val f = KeyRepeatFilter()
        f.down(bit, 100)
        assertEquals(KeyRepeatFilter.GAP_MS, f.up(bit, 100))
        assertEquals(listOf(bit to 100L), f.due(100 + KeyRepeatFilter.GAP_MS))
    }

    @Test
    fun theEmulatorsRepeatStreamIsOneHold() { // after the PC's repeat delay: a tap every 33 ms
        val f = KeyRepeatFilter()
        assertTrue(f.down(bit, 500))
        var t = 500L
        while (t < 2000) {
            f.up(bit, t)
            t += 33
            assertTrue(f.due(t).isEmpty())
            assertFalse("the same hold", f.down(bit, t))
        }
        val wait = f.up(bit, t)
        assertEquals(listOf(bit to t), f.due(t + wait))
        assertTrue("the next press starts a new hold", f.down(bit, t + 1000))
    }

    @Test
    fun keysAreSeparateAndAStrayUpIsIgnored() {
        val f = KeyRepeatFilter()
        assertEquals(-1L, f.up(KeyMap.REWIND, 10))
        f.down(KeyMap.FAST, 0)
        f.down(KeyMap.REWIND, 0)
        f.up(KeyMap.REWIND, 1000)
        assertEquals(listOf(KeyMap.REWIND to 1000L), f.due(2000))
        assertTrue(f.held(KeyMap.FAST))
        f.clear()
        assertFalse(f.held(KeyMap.FAST))
    }
}
```

Replace `app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class KeyMapTest {
    @Test
    fun everyMappedKeySetsItsButton() {
        val expected = mapOf(
            KeyEvent.KEYCODE_DPAD_UP to KeyMap.UP,
            KeyEvent.KEYCODE_DPAD_DOWN to KeyMap.DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to KeyMap.LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT to KeyMap.RIGHT,
            KeyEvent.KEYCODE_X to KeyMap.A,
            KeyEvent.KEYCODE_BUTTON_A to KeyMap.A,
            KeyEvent.KEYCODE_Z to KeyMap.B,
            KeyEvent.KEYCODE_BUTTON_B to KeyMap.B,
            KeyEvent.KEYCODE_A to KeyMap.L,
            KeyEvent.KEYCODE_BUTTON_L1 to KeyMap.L,
            KeyEvent.KEYCODE_S to KeyMap.R,
            KeyEvent.KEYCODE_BUTTON_R1 to KeyMap.R,
            KeyEvent.KEYCODE_ENTER to KeyMap.START,
            KeyEvent.KEYCODE_BUTTON_START to KeyMap.START,
            KeyEvent.KEYCODE_DEL to KeyMap.SELECT,
            KeyEvent.KEYCODE_BUTTON_SELECT to KeyMap.SELECT,
        )
        for ((key, bit) in expected) assertEquals("key $key", bit, KeyMap.bitFor(key))
    }

    @Test
    fun buttonBitsMatchTheCore() { /* GBA_KEY_* in core/include/gba.h */
        assertEquals(listOf(1, 2, 4, 8, 16, 32, 64, 128, 256, 512),
            listOf(KeyMap.A, KeyMap.B, KeyMap.SELECT, KeyMap.START, KeyMap.RIGHT, KeyMap.LEFT, KeyMap.UP, KeyMap.DOWN,
                KeyMap.R, KeyMap.L))
    }

    @Test
    fun pressAndReleaseChangeOnlyThatButton() {
        var mask = KeyMap.update(0, KeyEvent.KEYCODE_X, true)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_DPAD_UP, true)
        assertEquals(KeyMap.A or KeyMap.UP, mask)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_X, false)
        assertEquals(KeyMap.UP, mask)
    }

    @Test
    fun unmappedKeysChangeNothing() {
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_Q))
        assertEquals(KeyMap.UP, KeyMap.update(KeyMap.UP, KeyEvent.KEYCODE_Q, true))
    }

    @Test
    fun theControlsTableCoversEveryButtonOnceWithNames() {
        assertEquals(4095, KeyMap.bindings.sumOf { it.bit }) // every bit exactly once: 10 GBA buttons, fast-forward, rewind
        assertEquals(12, KeyMap.bindings.size)
        for (b in KeyMap.bindings) {
            assertTrue(b.button, b.keyboard.isNotEmpty() && b.gamepad.isNotEmpty())
            for (code in b.keyboard.keys + b.gamepad.keys) assertTrue(b.button, KeyMap.bitFor(code) == b.bit)
        }
    }

    @Test
    fun keyboardAndTouchButtonsAddUp() {
        assertEquals(KeyMap.A or KeyMap.UP or KeyMap.RIGHT, KeyMap.merge(KeyMap.A, KeyMap.UP or KeyMap.RIGHT))
        assertEquals(KeyMap.B, KeyMap.merge(KeyMap.B, KeyMap.B))
        assertEquals(0, KeyMap.merge(0, 0))
    }
}
```

(Every bit once: the 10 GBA buttons, fast-forward and rewind — 4095, 12 bindings.)

Replace `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutTest {
    private val density = 2.625f // Pixel 9
    private val upright = TouchLayout(1080f, 1700f, density, sideways = false)
    private val sideways = TouchLayout(2400f, 1080f, density, sideways = true)

    @Test
    fun dpadGivesEightDirections() {
        val d = TouchLayout.Companion
        assertEquals(KeyMap.RIGHT, d.dpadDirection(10f, 0f))
        assertEquals(KeyMap.RIGHT or KeyMap.UP, d.dpadDirection(10f, -10f))
        assertEquals(KeyMap.UP, d.dpadDirection(0f, -10f))
        assertEquals(KeyMap.UP or KeyMap.LEFT, d.dpadDirection(-10f, -10f))
        assertEquals(KeyMap.LEFT, d.dpadDirection(-10f, 0f))
        assertEquals(KeyMap.LEFT or KeyMap.DOWN, d.dpadDirection(-10f, 10f))
        assertEquals(KeyMap.DOWN, d.dpadDirection(0f, 10f))
        assertEquals(KeyMap.DOWN or KeyMap.RIGHT, d.dpadDirection(10f, 10f))
        assertEquals(KeyMap.UP, d.dpadDirection(3f, -10f)) // 17° off up: still up
    }

    @Test
    fun dpadMiddleIsDeadAndItsEdgeWorks() {
        for (l in listOf(upright, sideways)) {
            assertEquals(0, l.buttonsAt(l.dpadX, l.dpadY))
            assertEquals(0, l.buttonsAt(l.dpadX + l.dpadRadius * 0.2f, l.dpadY))
            assertEquals(KeyMap.RIGHT, l.buttonsAt(l.dpadX + l.dpadRadius * 0.9f, l.dpadY))
            assertEquals(KeyMap.UP, l.buttonsAt(l.dpadX, l.dpadY - l.dpadRadius))
        }
    }

    @Test
    fun theMiddleOfEveryButtonPressesIt() {
        for (l in listOf(upright, sideways)) {
            assertEquals(8, l.buttons.size) // 6 GBA buttons, rewind, fast-forward
            for (b in l.buttons) assertEquals("${b.label} sideways=${l.sideways}", b.bit, l.buttonsAt(b.cx, b.cy))
        }
    }

    @Test
    fun theGapBetweenAAndBPressesNothing() {
        for (l in listOf(upright, sideways)) {
            val a = l.buttons.first { it.bit == KeyMap.A }
            val b = l.buttons.first { it.bit == KeyMap.B }
            assertEquals("sideways=${l.sideways}", 0, l.buttonsAt((a.cx + b.cx) / 2, (a.cy + b.cy) / 2))
        }
    }

    @Test
    fun buttonsStayInsideTheArea() {
        for (l in listOf(upright, sideways)) {
            for (b in l.buttons) {
                assertTrue("${b.label}", b.left >= 0f && b.top >= 0f && b.right <= l.width && b.bottom <= l.height)
            }
            assertTrue(l.dpadX - l.dpadRadius >= 0f && l.dpadY + l.dpadRadius <= l.height)
        }
    }

    @Test
    fun farFromEverythingPressesNothing() {
        assertEquals(0, upright.buttonsAt(upright.width / 2, upright.height / 2))
        assertEquals(0, sideways.buttonsAt(sideways.width / 2, sideways.height / 2)) // the middle of the game
    }
}
```

(8 buttons: the 6 GBA ones, ⟲ and ⏩ — each pressed at its middle, inside the area.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'FastForwardSwitch'`, `'KeyRepeatFilter'`, `'FAST'`, `'REWIND'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/FastForwardSwitch.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * The ⏩ button (or its key): fast as soon as it is pressed. Let go within [TAP_MS] and it was a tap, which flips
 * the switch (on if it was off, off if it was on); let go later and it was a hold, which ends at normal speed.
 * Pure (times given in ms), so it is tested on the PC.
 */
class FastForwardSwitch {
    /** Fast mode stays on after the button is let go (the button is drawn lit). */
    var switchedOn = false
        private set
    private var pressedAt = -1L

    val held get() = pressedAt >= 0
    val fast get() = held || switchedOn

    fun press(nowMs: Long) {
        if (pressedAt < 0) pressedAt = nowMs
    }

    fun release(nowMs: Long) {
        if (pressedAt < 0) return
        val tap = nowMs - pressedAt < TAP_MS
        pressedAt = -1
        switchedOn = tap && !switchedOn
    }

    fun reset() {
        pressedAt = -1
        switchedOn = false
    }

    companion object {
        const val TAP_MS = 300L
    }
}
```

`app/src/main/java/com/tengen/gbaemu/KeyRepeatFilter.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * Held keyboard keys for ⏩ and ⟲, as the phone emulator's window sends them (checked with getevent, 2026-09-27):
 * a held letter arrives as a 0.1 ms tap, then — after the PC's key-repeat delay (~500 ms) — a tap every ~33 ms,
 * with no key-up when it is let go; the PC repeats only the last key pressed, so pressing an arrow stops the stream.
 * A key-up therefore ends the hold only if no key-down follows within [GAP_MS], and it ends at the key-up's time:
 * a lone tap of F flips fast mode, R held on its own rewinds smoothly. Real keyboards (key-downs repeat, one key-up)
 * only lose [GAP_MS]. Pure: times in ms.
 */
class KeyRepeatFilter {
    private val holdStart = HashMap<Int, Long>() // bit → when this hold began
    private val pending = HashMap<Int, Pair<Long, Long>>() // bit → (release due at, key-up time)

    /** A key-down; true if it starts a new hold (false: the same hold goes on). */
    fun down(bit: Int, now: Long): Boolean {
        pending.remove(bit)
        return holdStart.putIfAbsent(bit, now) == null
    }

    /** A key-up: how long to wait before calling [due] (-1: the key was not held). */
    fun up(bit: Int, now: Long): Long {
        if (bit !in holdStart) return -1
        pending[bit] = (now + GAP_MS) to now
        return GAP_MS
    }

    /** Holds that have ended by [now]: (bit, key-up time) each. */
    fun due(now: Long): List<Pair<Int, Long>> {
        val ended = pending.filter { it.value.first <= now }.map { it.key to it.value.second }
        for ((bit, _) in ended) {
            pending.remove(bit)
            holdStart.remove(bit)
        }
        return ended
    }

    fun held(bit: Int) = bit in holdStart

    fun clear() {
        holdStart.clear()
        pending.clear()
    }

    companion object {
        const val GAP_MS = 60L // longer than the ~33 ms between repeats
    }
}
```

(Found in the user's trial: the phone emulator's window sends letters as taps, and the PC repeats only the last key pressed — so F held while walking with the arrows cannot be seen; on the laptop F is a tap switch, R held on its own rewinds. Real keyboards and gamepads on a phone hold normally.)

Replace `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent

/**
 * Keyboard and gamepad keys → GBA buttons (the same bits as GBA_KEY_* in core/include/gba.h), plus the app's own
 * fast-forward and rewind buttons above them (never sent to the game: see [GBA_BUTTONS]).
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

    /** The whole map; bitFor and the Controls table both come from it. */
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

    /** A keyboard key (not a gamepad button) for ⏩ or ⟲: those go through KeyRepeatFilter. */
    fun isKeyboardExtra(keyCode: Int): Boolean =
        bindings.any { (it.bit == FAST || it.bit == REWIND) && keyCode in it.keyboard }

    /** The GBA button for an Android key code, or 0 if the key is not mapped. */
    fun bitFor(keyCode: Int): Int =
        bindings.firstOrNull { keyCode in it.keyboard || keyCode in it.gamepad }?.bit ?: 0

    /** The held-buttons mask after a key goes down or up. */
    fun update(mask: Int, keyCode: Int, down: Boolean): Int {
        val bit = bitFor(keyCode)
        return if (down) mask or bit else mask and bit.inv()
    }

    /** What the game gets: keyboard/gamepad buttons and touch buttons together. */
    fun merge(keyboardMask: Int, touchMask: Int): Int = keyboardMask or touchMask
}
```

Replace `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * Where the touch buttons are, in pixels, for a controls area of [width] × [height] pixels.
 * Upright (layout A): the area is the space below the game. Sideways: the whole screen, buttons over the side bars.
 * ⟲ rewind and ⏩ fast-forward (KeyMap.REWIND / FAST, not GBA buttons): upright between L and R; sideways just
 * below L and R, in the side bars (between them would cover the game).
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, density: Float, val sideways: Boolean) {
    /** One button: [round] ones are the circle inside the box, the others a rounded box. */
    data class Button(val bit: Int, val label: String, val round: Boolean,
                      val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2
    }

    private val dp = density
    private val slop = 8 * dp // fingers are imprecise: a button reacts a little outside its drawing

    val dpadX: Float
    val dpadY: Float
    val dpadRadius: Float
    val buttons: List<Button>

    init {
        val m = 16 * dp
        if (!sideways) {
            val shoulderH = 40 * dp
            val smallW = 80 * dp
            val smallH = 32 * dp
            val smallTop = height - m - smallH
            dpadRadius = 70 * dp
            dpadX = m + dpadRadius
            dpadY = (m + shoulderH + smallTop) / 2
            val ar = 36 * dp
            val ax = width - m - ar
            val ay = dpadY - 28 * dp
            val bx = ax - 84 * dp
            val by = ay + 56 * dp
            val extraW = 64 * dp
            val extraTop = m + 4 * dp
            val extraBottom = m + shoulderH - 4 * dp
            buttons = listOf(
                Button(KeyMap.L, "L", false, 0f, m, 96 * dp, m + shoulderH),
                Button(KeyMap.R, "R", false, width - 96 * dp, m, width, m + shoulderH),
                Button(KeyMap.REWIND, "⟲", false, width / 2 - 8 * dp - extraW, extraTop, width / 2 - 8 * dp, extraBottom),
                Button(KeyMap.FAST, "⏩", false, width / 2 + 8 * dp, extraTop, width / 2 + 8 * dp + extraW, extraBottom),
                Button(KeyMap.A, "A", true, ax - ar, ay - ar, ax + ar, ay + ar),
                Button(KeyMap.B, "B", true, bx - ar, by - ar, bx + ar, by + ar),
                Button(KeyMap.SELECT, "SELECT", false, width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH),
                Button(KeyMap.START, "START", false, width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH),
            )
        } else {
            val shoulderH = 40 * dp
            val smallW = 72 * dp
            val smallH = 28 * dp
            val smallTop = height - m - smallH
            dpadRadius = 60 * dp
            dpadX = m + dpadRadius
            dpadY = height * 0.55f
            val ar = 30 * dp
            val ax = width - m - ar
            val ay = height * 0.45f
            val bx = ax - 66 * dp
            val by = ay + 52 * dp
            val extraTop = m + shoulderH + 12 * dp
            val extraBottom = extraTop + 32 * dp
            buttons = listOf(
                Button(KeyMap.L, "L", false, 0f, m, 96 * dp, m + shoulderH),
                Button(KeyMap.R, "R", false, width - 96 * dp, m, width, m + shoulderH),
                Button(KeyMap.REWIND, "⟲", false, 16 * dp, extraTop, 80 * dp, extraBottom),
                Button(KeyMap.FAST, "⏩", false, width - 80 * dp, extraTop, width - 16 * dp, extraBottom),
                Button(KeyMap.A, "A", true, ax - ar, ay - ar, ax + ar, ay + ar),
                Button(KeyMap.B, "B", true, bx - ar, by - ar, bx + ar, by + ar),
                Button(KeyMap.SELECT, "SELECT", false, width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH),
                Button(KeyMap.START, "START", false, width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH),
            )
        }
    }

    /** The GBA buttons a finger at (x, y) presses: D-pad directions, or the nearest button it touches, or 0. */
    fun buttonsAt(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val dist = hypot(dx, dy)
        if (dist <= dpadRadius + slop) return if (dist < dpadRadius * DEAD_ZONE) 0 else dpadDirection(dx, dy)
        var best = 0
        var bestDist = Float.MAX_VALUE
        for (b in buttons) {
            if (!touches(b, x, y)) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDist) {
                best = b.bit
                bestDist = d
            }
        }
        return best
    }

    private fun touches(b: Button, x: Float, y: Float): Boolean =
        if (b.round) hypot(x - b.cx, y - b.cy) <= (b.right - b.left) / 2 + slop
        else x >= b.left - slop && x <= b.right + slop && y >= b.top - slop && y <= b.bottom + slop

    companion object {
        const val DEAD_ZONE = 0.25f // no direction in the middle quarter of the D-pad's radius

        /** 8 sectors of 45°, from the D-pad's centre (screen y grows downwards): diagonals press two directions. */
        fun dpadDirection(dx: Float, dy: Float): Int {
            val angle = Math.toDegrees(atan2(-dy, dx).toDouble()) // 0 = right, 90 = up
            return when (((angle / 45.0).roundToInt() + 8) % 8) {
                0 -> KeyMap.RIGHT
                1 -> KeyMap.RIGHT or KeyMap.UP
                2 -> KeyMap.UP
                3 -> KeyMap.UP or KeyMap.LEFT
                4 -> KeyMap.LEFT
                5 -> KeyMap.LEFT or KeyMap.DOWN
                6 -> KeyMap.DOWN
                else -> KeyMap.DOWN or KeyMap.RIGHT
            }
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: `FastForwardSwitchTest` 5, `KeyRepeatFilterTest` 4, `KeyMapTest` 6, `TouchLayoutTest` 6 — **35 tests**, 0 failures. Then `bash tools/dev.sh apk` → `BUILD SUCCESSFUL` (the app still builds; the new buttons are drawn but not wired until Task 6).

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/FastForwardSwitch.kt app/src/main/java/com/tengen/gbaemu/KeyRepeatFilter.kt app/src/main/java/com/tengen/gbaemu/KeyMap.kt app/src/main/java/com/tengen/gbaemu/TouchLayout.kt app/src/test/java/com/tengen/gbaemu/FastForwardSwitchTest.kt app/src/test/java/com/tengen/gbaemu/KeyRepeatFilterTest.kt app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "feat(android): the fast-forward switch (tap or hold), held F/R keys, the rewind and fast-forward buttons' places" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 4-5) done — checkpoint with the user.**

---
### Task 6: The buttons, the menu row and the wiring

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`, `app/src/main/java/com/tengen/gbaemu/Screens.kt`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

(No new automated test: the parts are covered by Tasks 1-5; the whole is checked by Claude in Step 3 and by the user in Task 7.)

- [x] **Step 1: Write the code**

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
 * change, each newly pressed one with a vibration tick (Android's own touch feedback, so the phone's setting applies).
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
        if (pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val layout = touchLayout ?: return
        if (hidden) return
        // Upright: solid grey on black. Sideways: see-through white over the game's side bars.
        val idle = if (sideways) 0x55FFFFFF else 0xFFB4B4B4.toInt()
        val down = if (sideways) 0x99FFFFFF.toInt() else 0xFF6E6E6E.toInt()
        val armDown = if (sideways) 0x55FFFFFF else down // drawn over the cross
        text.color = if (sideways) 0xDDFFFFFF.toInt() else 0xFF222222.toInt()

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

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.DateFormat
import java.util.Date
import kotlin.math.roundToInt
import androidx.compose.ui.viewinterop.AndroidView

/** Continue (when there is a last game), Open ROM, Controls, and a message when something went wrong. */
@Composable
fun StartScreen(message: String?, lastTitle: String?, onContinue: () -> Unit, onOpenRom: () -> Unit,
                onControls: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(24.dp))
            if (lastTitle != null) {
                Button(onClick = onContinue) { Text("Continue: $lastTitle") }
                Spacer(Modifier.height(12.dp))
            }
            Button(onClick = onOpenRom) { Text("Open ROM") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onControls) { Text("Controls") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error)
            }
        }
    }
}

/** Which keyboard key and gamepad button press each GBA button (from KeyMap, so it is always right). */
@Composable
fun ControlsDialog(onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text("Controls") },
        text = {
            Column {
                Row { for (h in listOf("GBA", "Keyboard", "Gamepad")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold) }
                for (b in KeyMap.bindings) {
                    Row(Modifier.padding(top = 4.dp)) {
                        Text(b.button, Modifier.weight(1f))
                        Text(b.keyboard.values.joinToString(), Modifier.weight(1f))
                        Text(b.gamepad.values.joinToString(), Modifier.weight(1f))
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text("On the phone: the on-screen buttons. They hide while a keyboard or gamepad is used; touch the screen to bring them back.")
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Close") } },
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
enum class MenuPage { MAIN, SAVE, LOAD }

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
             onLoad: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad,
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
               onManualLight: (Int?) -> Unit = {}) {
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

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
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
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private var lastTitle by mutableStateOf<String?>(null)
    private var showControls by mutableStateOf(false)
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var menu by mutableStateOf<MenuPage?>(null) // the in-game menu (the game paused) or null
    private var replaceSlot by mutableStateOf<Int?>(null)
    private var askRestart by mutableStateOf(false)
    private var slotsChanged by mutableStateOf(0) // re-reads the slot pictures
    private var session by mutableStateOf(0) // a new game session gets a new game screen (see startGame)
    private var states: States? = null // the running game's save states
    private var currentUri: Uri? = null
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
    private lateinit var lastGame: LastGame

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        lastGame = LastGame(this)
        lastTitle = lastGame.title
        fastSpeed = getSharedPreferences("settings", MODE_PRIVATE).getInt("fast_speed", 4)
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
                } else {
                    StartScreen(message, lastTitle, onContinue = ::openLast,
                        onOpenRom = { openRom.launch(arrayOf("*/*")) }, onControls = { showControls = true })
                }
                if (showControls) ControlsDialog { showControls = false }
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
                        onLoad = { menu = MenuPage.LOAD }, onRestart = { askRestart = true }, onQuit = ::quitGame)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    null -> {}
                }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                BackHandler(enabled = playing && menu == null) { openMenu() }
                BackHandler(enabled = playing && menu != null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    private fun openFromPicker(uri: Uri) {
        try { /* so Continue can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        open(uri, fromContinue = false)
    }

    private fun openLast() {
        open(lastGame.uri ?: return, fromContinue = true)
    }

    /** Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). */
    private fun open(uri: Uri, fromContinue: Boolean, resumeAuto: Boolean = true) {
        val bytes = readBytes(uri)
        if (bytes == null) {
            if (fromContinue) {
                lastGame.forget()
                lastTitle = null
                message = "The last game's file can't be opened any more."
            } else {
                message = "Could not read that file."
            }
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        val gameId = GameId.of(bytes)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA ROM (size 0 or over 32 MiB)."
            return
        }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val title = NativeBridge.romTitle().trim().ifEmpty { uri.lastPathSegment?.substringAfterLast('/') ?: "last game" }
        lastGame.remember(uri, title)
        lastTitle = title
        message = null
        val st = States(filesDir, gameId)
        states = st
        currentUri = uri
        currentTitle = title
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
    }

    private fun restartGame() {
        askRestart = false
        val uri = currentUri ?: return
        stopGame()
        open(uri, fromContinue = false, resumeAuto = false)
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

    private fun speedLabel(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

    /** The menu's fast-forward row: 2× → 4× → 8× → Max → 2×, remembered for all games. */
    private fun nextFastSpeed() {
        fastSpeed = when (fastSpeed) {
            2 -> 4
            4 -> 8
            8 -> NativeBridge.FF_MAX
            else -> 2
        }
        getSharedPreferences("settings", MODE_PRIVATE).edit().putInt("fast_speed", fastSpeed).apply()
        sendKeys() // a switched-on fast mode uses the new speed
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

Expected: Kotlin 35/35, `380 tests, 0 failed`, CTest 120/120, on-device `tests="41" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)** — with the Mode 7 demo: without keys its picture stays still, so moments compare pixel for pixel. (Opening it makes the app's Continue point at it; the user opens their game with Open ROM afterwards.)

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
tap_on() {  # tap the centre of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            ADB shell input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
pick_download() {  # the picker: its side menu → Downloads, then scroll until $1 shows and tap it
    # (found while replaying 5b: the side menu is only sometimes in the UI dump, so Downloads is tapped by its place
    # on the Pixel 9; then wait until the Downloads page is really shown)
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
colour_at() { python -c "from PIL import Image; import sys; print(Image.open(sys.argv[1]).convert('RGB').getpixel((int(sys.argv[2]), int(sys.argv[3]))))" "$(cygpath -w "$SP/$1.png")" $2 $3; }
same_game_picture() {  # the top 1080x720 (the game, upright) of two screenshots: identical?
    python -c "from PIL import Image; import sys; a=Image.open(sys.argv[1]).crop((0,0,1080,720)); b=Image.open(sys.argv[2]).crop((0,0,1080,720)); print('same' if a.tobytes()==b.tobytes() else 'DIFFERENT')" "$(cygpath -w "$SP/$1.png")" "$(cygpath -w "$SP/$2.png")"
}
hold_up() { ADB shell input swipe 226 1453 226 1453 800; }        # the touch D-pad's Up (Pixel 9, upright)
hold_rewind() { ADB shell input swipe 435 812 435 812 3000; }     # ⟲, 3 s = 180 steps = 12 s of game (Pixel 9, upright)
tap_fast() { ADB shell input tap 644 812; }                       # ⏩ (Pixel 9, upright)
```

Then (as a script, `bash <file>`: it stops if the demo does not open — found while replaying 5b, a failed pick let the later taps open another cartridge from the picker's Recent list):

```bash
M7=$(python -c "import zlib; print('%08x' % zlib.crc32(open('tests/roms/tonc/m7_demo.gba','rb').read()))")
bash tools/dev.sh push tests/roms/tonc/m7_demo.gba
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
ADB shell run-as com.tengen.gbaemu rm -rf files/states/$M7   # the demo's own test states only
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3
tap_on text "Open ROM" && sleep 2 && pick_download m7_demo.gba && sleep 3 && shot fresh ||
    { echo "STOP: m7_demo did not open - nothing below may run on another screen"; exit 1; }
colour_at fresh 590 812; colour_at fresh 380 812                    # ⏩ and ⟲ drawn (their plain part, beside the symbol)
tap_fast; sleep 1; shot fast_on; colour_at fast_on 590 812          # a tap: switched on, lit
tap_fast; sleep 1; shot fast_off; colour_at fast_off 590 812        # a tap: off
# a key hides the touch buttons (4c); a touch on an empty spot brings them back and presses nothing
ADB shell input keyevent KEYCODE_F; sleep 1; ADB shell input tap 540 1700; sleep 1; shot key_on; colour_at key_on 590 812
ADB shell input keyevent KEYCODE_F; sleep 1; ADB shell input tap 540 1700; sleep 1; shot key_off; colour_at key_off 590 812
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; texts | head -8
for s in 4× 8× Max 2×; do tap_on text "Fast-forward: $s"; sleep 1; done; texts | grep -o 'Fast-forward: [^"]*'
tap_on text "▶  Resume"; sleep 1
hold_up; hold_up; sleep 0.5; shot moved
hold_rewind; sleep 0.5; shot rewound
same_game_picture fresh rewound; same_game_picture fresh moved
ADB shell settings put system user_rotation 1; sleep 3; shot sideways; ADB shell settings put system user_rotation 0; sleep 2
ADB logcat -d -s AndroidRuntime:E | tail -3
```

Expected:
- `fresh`: ⏩ and ⟲ grey `(180, 180, 180)` between L and R; after a tap on ⏩ (and after `KEYCODE_F`) ⏩ is `(255, 213, 74)` (lit), after the next one grey again.
- The menu shows '▶  Resume', 'Fast-forward: 4×', 'Save state…', 'Load state…', 'Restart game', 'Quit'; tapping the row goes 4× → 8× → Max → 2× → 4× (it ends on `Fast-forward: 4×`).
- `same_game_picture fresh rewound` → **same** (3 s of ⟲ reach back past the walk to the start — the oldest snapshot is the camera's first place); `fresh moved` → **DIFFERENT**.
- `sideways` (look at it): ⟲ just below L and ⏩ just below R, in the black side bars; the game not covered. No `AndroidRuntime` crash.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md
git commit -m "feat(android): rewind and fast-forward buttons, the menu's fast-forward speed, F/R and R2/L2" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 7: User check and milestone 5b exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 120/120 (sanitizers on, no reports), 380 unit tests, Kotlin 35, on-device 41 (emulator only).

- [x] **Step 2: The user's check** (the app installed on the emulator; their game A hack opened with Open ROM or Continue)

game A: hold ⏩ → fast with higher-pitched sound, back to normal on release; tap ⏩ → stays fast (lit) until the next tap; Back → 'Fast-forward: N×' cycles 2×/4×/8×/Max, Max silent; hold ⟲ → the game runs backwards, letting go plays on; sideways: ⟲/⏩ below L/R; on the laptop keyboard: F taps switch fast mode, R held rewinds.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (5b ✅ with the numbers and the user's check; Next = milestone 5c brainstorming — cheats); spec status → `Implemented — milestone 5b complete <date> (tag milestone-5b)`; parent spec §10 row 5 → 5b ✅; `CLAUDE.md` key files → 5c needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-5b replay-5b`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 5b complete - fast-forward and rewind" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-5b
```
