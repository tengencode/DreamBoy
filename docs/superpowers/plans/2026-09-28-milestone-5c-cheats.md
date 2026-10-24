# Milestone 5c — Cheats — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Cheats: Action Replay v3 / GameShark v1-v2 (scrambled or plain), CodeBreaker (plain or scrambled) and address:value codes, auto-detected, run once per frame by the core; a cheat panel in the in-game menu with an add/edit pop-up; each game's cheats remembered and switched back on when it opens.

**Architecture:** `core/src/cheats/` reads a cheat's text into a list of steps (`decode.c`, with the ciphers in `decrypt.c`) and runs them at the end of every `gba_run_frame` (`apply.c`), writing memory without bus cycles (`bus_poke`); ROM patches are applied while the list is set and undone when it changes. The list lives outside every save-state chunk. On Android the list is set only while the emulation thread is parked; Kotlin keeps a game's cheats in `files/cheats/<game id>.json` (`Cheats`) and drives the panel (`Screens.kt`, `MainActivity`).

**Tech Stack:** C11 (core, PC tests, Android NDK), CMake/CTest, Kotlin + Jetpack Compose, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-28-milestone-5c-cheats-design.md`

---

## Conventions (same as 5b)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its `files/saves` / `files/states` / `files/cheats` (except the test files named in a step) without the user's OK. Starting a closed emulator (`dev.sh emu`) is fine; ask before restarting/cold-booting it (a cold boot: see PROGRESS.md Notes).
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a source or test file. One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-5c` (already created; the spec commits are on it). The tasks run one after another without stopping (`CLAUDE.md`); the only stop is Task 7, the user's check and the 5c exit. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p5c`, branch `proto-5c`), which the user tried with their game A hack on 2026-09-28 ('Everything works'). Formats and keys as mGBA reads them (its source read online); our own code (mGBA is MPL-2.0).

## File map

```
core/include/gba.h, core/src/cheats/cheats.h, decrypt.c, decode.c; tests/unit/test_cheats_cipher.c          Task 1
core/src/cheats/apply.c, core/src/mem/bus.{h,c}, core/src/gba.c, core/src/gba_internal.h;
  tests/unit/test_cheats_run.c                                                                            Task 2
tools/testrunner/main.c (--cheat, --load-state)                                                           Task 3
platform/android/emu_thread.{h,c}, jni_bridge.c; app/.../NativeBridge.kt; app/src/androidTest/.../CheatTest.kt   Task 4
app/.../Cheats.kt; app/src/test/.../CheatsTest.kt                                                          Task 5
app/.../Screens.kt, app/.../MainActivity.kt                                                               Task 6
```

---
### Task 1: Ciphers and reading codes

**Files:**
- Create: `core/src/cheats/cheats.h`, `core/src/cheats/decrypt.c`, `core/src/cheats/decode.c`, `tests/unit/test_cheats_cipher.c`
- Modify: `core/include/gba.h`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_cheats_cipher.c`:

```c
/* Cheats: the ciphers (core/src/cheats/decrypt.c) and reading codes (decode.c). */
#include <stdio.h>
#include <string.h>

#include "cheats/cheats.h"
#include "test.h"

TEST(cheat_tea_matches_its_published_test_value) {
    static const uint32_t zero_key[4] = {0, 0, 0, 0};
    uint32_t a = 0, b = 0;
    cheat_tea_encrypt(&a, &b, zero_key);
    CHECK_EQ(a, 0x41EA3A0Au); /* the TEA reference's value for a zero key and a zero block */
    CHECK_EQ(b, 0x94BAA940u);
    cheat_tea_decrypt(&a, &b, zero_key);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 0);
}

TEST(cheat_tea_round_trips_with_both_device_keys) {
    static const uint32_t plain[][2] = {{0x00300000, 0x00000078}, {0x02300002, 0x00005678}, {0x00000000, 0x001DC0DE},
                                        {0x13000000, 0x0000FFFF}, {0xDEADBEEF, 0x12345678}};
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        for (int k = 0; k < 2; k++) {
            const uint32_t *seeds = k ? CHEAT_AR3_SEEDS : CHEAT_GS_SEEDS;
            uint32_t a = plain[i][0], b = plain[i][1];
            cheat_tea_encrypt(&a, &b, seeds);
            CHECK(a != plain[i][0] || b != plain[i][1]);
            cheat_tea_decrypt(&a, &b, seeds);
            CHECK_EQ(a, plain[i][0]);
            CHECK_EQ(b, plain[i][1]);
        }
    }
}

TEST(cheat_codebreaker_scrambling_round_trips) {
    static const uint32_t seeds[][2] = {{0x9123ABCD, 0x1234}, {0x90000000, 0x0000}, {0x9F00FF00, 0xFFFF}};
    for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
        cb_crypt_t c;
        cheat_cb_reseed(&c, seeds[s][0], (uint16_t)seeds[s][1]);
        for (unsigned i = 0; i < sizeof c.table; i++) CHECK(c.table[i] < sizeof c.table);
        for (uint32_t i = 0; i < 50; i++) {
            uint32_t a = 0x32000000u + i * 0x01010101u, orig_a = a;
            uint16_t b = (uint16_t)(i * 0x1357), orig_b = b;
            cheat_cb_encrypt(&c, &a, &b);
            cheat_cb_decrypt(&c, &a, &b);
            CHECK_EQ(a, orig_a);
            CHECK_EQ(b, orig_b);
        }
    }
}

/* "XXXXXXXX YYYYYYYY" of a plain code scrambled with a device key. */
static void scrambled(char *out, size_t cap, uint32_t a, uint32_t b, const uint32_t seeds[4]) {
    cheat_tea_encrypt(&a, &b, seeds);
    snprintf(out, cap, "%08X %08X", (unsigned)a, (unsigned)b);
}

TEST(cheat_auto_detection_finds_each_format) {
    char line[40];
    int bad = -1;
    scrambled(line, sizeof line, 0x00300000, 0x00000078, CHEAT_AR3_SEEDS); /* AR: byte write to IWRAM */
    CHECK_EQ(gba_cheat_check(line, GBA_CHEAT_AUTO, &bad), GBA_CHEAT_ACTION_REPLAY);
    CHECK_EQ(bad, 0);
    scrambled(line, sizeof line, 0x03000000, 0x00000078, CHEAT_GS_SEEDS); /* GS: byte write to IWRAM */
    CHECK_EQ(gba_cheat_check(line, GBA_CHEAT_AUTO, &bad), GBA_CHEAT_GAMESHARK);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("33000000 0063", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_CODEBREAKER);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("02000000:1234", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_RAW);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("  0300 0000 0063  \n", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_CODEBREAKER); /* spaces anywhere */
    CHECK_EQ(bad, 0);
}

TEST(cheat_check_names_the_first_bad_line_counting_blank_ones) {
    int bad = -1;
    gba_cheat_check("33000000 0063\n\n33000001 0064\nnot a code\n", GBA_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 4);
    gba_cheat_check("33000000 0063\n02000000:12", GBA_CHEAT_AUTO, &bad); /* another format in the same cheat */
    CHECK_EQ(bad, 2);
    gba_cheat_check("33000000 00631", GBA_CHEAT_AUTO, &bad); /* 13 digits */
    CHECK_EQ(bad, 1);
    gba_cheat_check("02000000:123456", GBA_CHEAT_RAW, &bad); /* 3 bytes */
    CHECK_EQ(bad, 1);
    gba_cheat_check("", GBA_CHEAT_AUTO, &bad); /* nothing */
    CHECK_EQ(bad, 1);
    gba_cheat_check("\n\n", GBA_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 2);
    char line[40];
    scrambled(line, sizeof line, 0xDEADFACE, 0x00001234, CHEAT_AR3_SEEDS); /* re-keying: not supported */
    gba_cheat_check(line, GBA_CHEAT_ACTION_REPLAY, &bad);
    CHECK_EQ(bad, 1);
    CHECK_EQ(gba_cheat_check("33000000 0063", GBA_CHEAT_ACTION_REPLAY, &bad), GBA_CHEAT_ACTION_REPLAY);
    CHECK_EQ(bad, 1); /* the picked type decides: a CodeBreaker line is not an Action Replay code */
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: the build fails — `fatal error: 'cheats/cheats.h' file not found`.

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
    GBA_ERR_CHEAT,     /* a cheat's text that is not a code this core can read */
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

/* Cheats: Action Replay v3, GameShark v1/v2 (8 + 8 hex digits, scrambled or plain), CodeBreaker (8 + 4, plain or
 * scrambled), address:value. A cheat is its codes as text, one per line (spaces and blank lines allowed). */
typedef enum {
    GBA_CHEAT_AUTO, /* detected from the first code */
    GBA_CHEAT_ACTION_REPLAY,
    GBA_CHEAT_GAMESHARK,
    GBA_CHEAT_CODEBREAKER,
    GBA_CHEAT_RAW, /* AAAAAAAA:VV, :VVVV or :VVVVVVVV */
} gba_cheat_type_t;
/* Reads a cheat without a machine: the type it is read as (Auto → the detected one) with *bad_line = 0, or
 * *bad_line = the first line that is not a code (1-based, blank lines counted). */
gba_cheat_type_t gba_cheat_check(const char *text, gba_cheat_type_t type, int *bad_line);
/* The switched-on cheats, replacing the previous ones (whose ROM patches are undone first). Call between
 * frames. They run at the end of every gba_run_frame; save states and rewind never contain them.
 * GBA_ERR_CHEAT (a text that does not read) leaves no cheats on. count 0 = none. */
gba_err_t gba_cheats_set(gba_t *g, const char *const *texts, const gba_cheat_type_t *types, size_t count);

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

(`GBA_ERR_CHEAT` is appended: the existing error numbers, used by the Kotlin side, stay the same. `gba_cheats_set` is defined in Task 2.)

`core/src/cheats/cheats.h`:

```c
#ifndef GBA_CHEATS_H
#define GBA_CHEATS_H

/* Cheat codes: text → a list of steps, run in order at the end of every frame.
 *
 * Formats (as mGBA reads them; our own code — mGBA is MPL-2.0): Action Replay v3 and GameShark v1/v2
 * (8 + 8 hex digits, TEA-scrambled or plain), CodeBreaker (8 + 4 hex digits, plain or scrambled after a
 * 9xxxxxxx line), address:value (VBA style). Master / hook / game-id / slowdown lines are skipped; cheats
 * run once per frame instead of at the hook. The DEADFACE re-keying lines are not supported. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"

typedef enum { STEP_WRITE, STEP_WRITE_INDIRECT, STEP_ADD, STEP_AND, STEP_OR, STEP_IF } step_kind_t;
typedef enum { IF_EQ, IF_NE, IF_LT, IF_GT, IF_LE, IF_GE, IF_ULT, IF_UGT, IF_AND, IF_NAND, IF_NEVER, IF_BUTTON } if_kind_t;

typedef struct {
    uint8_t kind;  /* step_kind_t */
    uint8_t cond;  /* if_kind_t, for STEP_IF */
    uint8_t width; /* 1, 2 or 4 bytes */
    uint32_t addr, value;
    uint32_t count;      /* writes: how many times (a fill); 1 normally */
    uint32_t addr_step;  /* added to the address after each write (indirect: added to the pointer read) */
    uint32_t value_step; /* added to the value after each write */
    uint32_t then_n;     /* STEP_IF: the steps after it that run when it holds */
    uint32_t else_n;     /* STEP_IF: the steps after those that run when it does not */
} cheat_step_t;

typedef struct {
    uint32_t addr;  /* in the cartridge ROM (0x08000000-based), halfword-aligned */
    uint16_t value; /* the patched halfword */
    uint16_t old;   /* what the ROM held (valid while applied) */
} cheat_patch_t;

typedef struct cheat_list {
    cheat_step_t *steps;
    size_t nsteps, capsteps;
    cheat_patch_t *patches;
    size_t npatches, cappatches;
    bool applied; /* the patches are in the ROM */
} cheat_list_t;

/* Reads one cheat's text into `out` (appending). 0 on success, else the first bad line (1-based, blank
 * lines counted). *used = the type read (Auto → the one detected). */
int cheat_decode(const char *text, gba_cheat_type_t type, cheat_list_t *out, gba_cheat_type_t *used);

void cheat_list_free(cheat_list_t *l);
bool cheat_push_step(cheat_list_t *l, cheat_step_t s);
bool cheat_push_patch(cheat_list_t *l, uint32_t addr, uint16_t value);

/* End of every frame (nothing without cheats). */
void cheats_apply(gba_t *g);
/* Undo the ROM patches and free the list (before a new ROM, at the end). */
void cheats_drop(gba_t *g);

/* ---- the ciphers (decrypt.c) ---- */
extern const uint32_t CHEAT_GS_SEEDS[4];  /* GameShark v1/v2 */
extern const uint32_t CHEAT_AR3_SEEDS[4]; /* Action Replay v3 */
void cheat_tea_decrypt(uint32_t *op1, uint32_t *op2, const uint32_t seeds[4]);
void cheat_tea_encrypt(uint32_t *op1, uint32_t *op2, const uint32_t seeds[4]); /* for tests */

typedef struct {
    uint32_t master; /* the 9xxxxxxx line's first word; 0 = codes are plain */
    uint32_t seeds[4];
    uint8_t table[0x30];
    uint32_t rng;
} cb_crypt_t;
void cheat_cb_reseed(cb_crypt_t *c, uint32_t op1, uint16_t op2); /* after a 9xxxxxxx yyyy line */
void cheat_cb_decrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2);
void cheat_cb_encrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2); /* for tests */

#endif
```

`core/src/cheats/decrypt.c`:

```c
/* The ciphers of the cheat devices: TEA with each device's key, and CodeBreaker's bit-shuffle scrambling. */
#include "cheats/cheats.h"

const uint32_t CHEAT_GS_SEEDS[4] = {0x09F4FBBD, 0x9681884A, 0x352027E9, 0xF3DEE5A7};
const uint32_t CHEAT_AR3_SEEDS[4] = {0x7AA9648F, 0x7FAE6994, 0xC0EFAAD5, 0x42712C57};

#define TEA_DELTA 0x9E3779B9u
#define TEA_ROUNDS 32

void cheat_tea_decrypt(uint32_t *op1, uint32_t *op2, const uint32_t k[4]) {
    uint32_t a = *op1, b = *op2, sum = TEA_DELTA * TEA_ROUNDS;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        b -= ((a << 4) + k[2]) ^ (a + sum) ^ ((a >> 5) + k[3]);
        a -= ((b << 4) + k[0]) ^ (b + sum) ^ ((b >> 5) + k[1]);
        sum -= TEA_DELTA;
    }
    *op1 = a;
    *op2 = b;
}

void cheat_tea_encrypt(uint32_t *op1, uint32_t *op2, const uint32_t k[4]) {
    uint32_t a = *op1, b = *op2, sum = 0;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        sum += TEA_DELTA;
        a += ((b << 4) + k[0]) ^ (b + sum) ^ ((b >> 5) + k[1]);
        b += ((a << 4) + k[2]) ^ (a + sum) ^ ((a >> 5) + k[3]);
    }
    *op1 = a;
    *op2 = b;
}

/* ---- CodeBreaker ---- */

static uint32_t ror32(uint32_t v, unsigned n) {
    return (v >> n) | (v << (32 - n));
}

/* Three steps of the device's LCG, their bits mixed into one number. */
static uint32_t cb_rand(cb_crypt_t *c) {
    uint32_t r1 = c->rng * 0x41C64E6Du + 0x3039;
    uint32_t r2 = r1 * 0x41C64E6Du + 0x3039;
    uint32_t r3 = r2 * 0x41C64E6Du + 0x3039;
    c->rng = r3;
    return ((r1 << 14) & 0xC0000000u) | ((r2 >> 1) & 0x3FFF8000u) | ((r3 >> 16) & 0x7FFFu);
}

/* A table index from the next random number: the device's own shift-and-subtract remainder by 48, quirks
 * included (it decides which bits get swapped, so it must match exactly). */
static uint32_t cb_swap_index(cb_crypt_t *c) {
    uint32_t roll = cb_rand(c), count = sizeof c->table;
    if (roll == count) roll = 0;
    if (roll < count) return roll;
    uint32_t bit = 1;
    while (count < 0x10000000u && count < roll) {
        count <<= 4;
        bit <<= 4;
    }
    while (count < 0x80000000u && count < roll) {
        count <<= 1;
        bit <<= 1;
    }
    uint32_t mask;
    for (;;) {
        mask = 0;
        if (roll >= count) roll -= count;
        if (roll >= count >> 1) {
            roll -= count >> 1;
            mask |= ror32(bit, 1);
        }
        if (roll >= count >> 2) {
            roll -= count >> 2;
            mask |= ror32(bit, 2);
        }
        if (roll >= count >> 3) {
            roll -= count >> 3;
            mask |= ror32(bit, 3);
        }
        if (!roll || !(bit >> 4)) break;
        bit >>= 4;
        count >>= 4;
    }
    mask &= 0xE0000000u;
    if (mask && (bit & 7)) {
        if (mask & ror32(bit, 3)) roll += count >> 3;
        if (mask & ror32(bit, 2)) roll += count >> 2;
        if (mask & ror32(bit, 1)) roll += count >> 1;
    }
    return roll % sizeof c->table; /* always < 48 already; the remainder only guards the table */
}

void cheat_cb_reseed(cb_crypt_t *c, uint32_t op1, uint16_t op2) {
    c->rng = (op2 & 0xFFu) ^ 0x1111u;
    for (unsigned i = 0; i < sizeof c->table; i++) c->table[i] = (uint8_t)i;
    for (int i = 0; i < 0x50; i++) {
        uint32_t x = cb_swap_index(c), y = cb_swap_index(c);
        uint8_t t = c->table[x];
        c->table[x] = c->table[y];
        c->table[y] = t;
    }
    c->rng = 0x4EFAD1C3u;
    for (uint32_t i = 0; i < ((op1 >> 24) & 0xF); i++) c->rng = cb_rand(c);
    c->seeds[2] = cb_rand(c);
    c->seeds[3] = cb_rand(c);
    c->rng = (uint32_t)(op2 >> 8) ^ 0xF254u;
    for (uint32_t i = 0; i < (uint32_t)(op2 >> 8); i++) c->rng = cb_rand(c);
    c->seeds[0] = cb_rand(c);
    c->seeds[1] = cb_rand(c);
    c->master = op1;
}

static void to_bytes(uint8_t b[6], uint32_t op1, uint16_t op2) {
    b[0] = (uint8_t)(op1 >> 24);
    b[1] = (uint8_t)(op1 >> 16);
    b[2] = (uint8_t)(op1 >> 8);
    b[3] = (uint8_t)op1;
    b[4] = (uint8_t)(op2 >> 8);
    b[5] = (uint8_t)op2;
}

static void from_bytes(const uint8_t b[6], uint32_t *op1, uint16_t *op2) {
    *op1 = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    *op2 = (uint16_t)(b[4] << 8 | b[5]);
}

/* Swap bit i of the 48 with bit table[i]. */
static void swap_bits(uint8_t b[6], unsigned i, unsigned j) {
    unsigned x = (b[i >> 3] >> (i & 7)) & 1, y = (b[j >> 3] >> (j & 7)) & 1;
    b[i >> 3] = (uint8_t)((b[i >> 3] & ~(1u << (i & 7))) | (y << (i & 7)));
    b[j >> 3] = (uint8_t)((b[j >> 3] & ~(1u << (j & 7))) | (x << (j & 7)));
}

void cheat_cb_decrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2) {
    uint8_t b[6];
    to_bytes(b, *op1, *op2);
    for (int i = (int)sizeof c->table - 1; i >= 0; i--) swap_bits(b, (unsigned)i, c->table[i]);
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[0];
    *op2 ^= (uint16_t)c->seeds[1];
    to_bytes(b, *op1, *op2);
    uint8_t k0 = (uint8_t)c->master, k1 = (uint8_t)(c->master >> 8);
    for (int i = 0; i < 5; i++) b[i] ^= k1 ^ b[i + 1];
    b[5] ^= k1;
    for (int i = 5; i > 0; i--) b[i] ^= k0 ^ b[i - 1];
    b[0] ^= k0;
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[2];
    *op2 ^= (uint16_t)c->seeds[3];
}

void cheat_cb_encrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2) {
    uint8_t b[6];
    *op1 ^= c->seeds[2];
    *op2 ^= (uint16_t)c->seeds[3];
    to_bytes(b, *op1, *op2);
    uint8_t k0 = (uint8_t)c->master, k1 = (uint8_t)(c->master >> 8);
    b[0] ^= k0;
    for (int i = 1; i <= 5; i++) b[i] ^= k0 ^ b[i - 1];
    b[5] ^= k1;
    for (int i = 4; i >= 0; i--) b[i] ^= k1 ^ b[i + 1];
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[0];
    *op2 ^= (uint16_t)c->seeds[1];
    to_bytes(b, *op1, *op2);
    for (unsigned i = 0; i < sizeof c->table; i++) swap_bits(b, i, c->table[i]);
    from_bytes(b, op1, op2);
}
```

`core/src/cheats/decode.c`:

```c
/* A cheat's text → steps. The formats' meanings follow mGBA's reading of them (our own code). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"

#define ROM_BASE 0x08000000u
#define IO_BASE 0x04000000u
#define KEYINPUT_ADDR 0x04000130u
#define GAME_ID_MARK 0x001DC0DEu /* second word of the device's game-id line */
#define RESEED_MARK 0xDEADFACEu  /* re-keying line: not supported */

/* ---- lists ---- */

bool cheat_push_step(cheat_list_t *l, cheat_step_t s) {
    if (l->nsteps == l->capsteps) {
        size_t cap = l->capsteps ? l->capsteps * 2 : 16;
        cheat_step_t *p = realloc(l->steps, cap * sizeof *p);
        if (!p) return false;
        l->steps = p;
        l->capsteps = cap;
    }
    l->steps[l->nsteps++] = s;
    return true;
}

bool cheat_push_patch(cheat_list_t *l, uint32_t addr, uint16_t value) {
    if (l->npatches == l->cappatches) {
        size_t cap = l->cappatches ? l->cappatches * 2 : 4;
        cheat_patch_t *p = realloc(l->patches, cap * sizeof *p);
        if (!p) return false;
        l->patches = p;
        l->cappatches = cap;
    }
    l->patches[l->npatches++] = (cheat_patch_t){addr & ~1u, value, 0};
    return true;
}

void cheat_list_free(cheat_list_t *l) {
    free(l->steps);
    free(l->patches);
    memset(l, 0, sizeof *l);
}

static cheat_step_t write_step(uint32_t addr, uint32_t value, unsigned width) {
    return (cheat_step_t){.kind = STEP_WRITE, .width = (uint8_t)width, .addr = addr, .value = value, .count = 1};
}

static cheat_step_t if_step(if_kind_t cond, uint32_t addr, uint32_t value, unsigned width, uint32_t then_n) {
    return (cheat_step_t){.kind = STEP_IF, .cond = (uint8_t)cond, .width = (uint8_t)width, .addr = addr,
                          .value = value, .count = 1, .then_n = then_n};
}

static uint32_t width_mask(unsigned width) {
    return width >= 4 ? 0xFFFFFFFFu : (1u << (8 * width)) - 1;
}

/* ---- how likely an address is real game memory (auto-detection) ---- */

static int address_score(uint32_t addr) {
    uint32_t off = addr & 0x00FFFFFFu;
    switch (addr >> 24) {
    case 0x0: return -0x80; /* BIOS */
    case 0x2: return off > 0x40000 ? -0x40 : 0x20;
    case 0x3: return off > 0x8000 ? -0x40 : 0x20;
    case 0x4: return off > 0x400 ? -0x80 : 0x10;
    case 0x5:
    case 0x7: return off > 0x400 ? -0x80 : -0x8;
    case 0x6: return off > 0x18000 ? -0x80 : -0x8;
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: return -0x8;
    case 0xE: case 0xF: return off > 0x10000 ? -0x80 : -0x8;
    default: return -0xC0;
    }
}

static int gs_score(uint32_t op1, uint32_t op2) {
    if (op2 == GAME_ID_MARK) return 0x100;
    uint32_t addr = op1 & 0x0FFFFFFFu;
    switch (op1 >> 28) {
    case 0x0: return 0x20 - ((op2 & 0xFFFFFF00u) ? 0x10 : 0) + address_score(addr);
    case 0x1: return 0x20 - ((op2 & 0xFFFF0000u) ? 0x10 : 0) + address_score(addr);
    case 0x2: return 0x20 + address_score(addr);
    case 0x6: return 0x20 - ((op2 & 0xCFFF0000u) ? 0x10 : 0);
    case 0x8: return 0x10;
    case 0xD: return 0x20 - ((op2 & 0xFFCF0000u) ? 0x10 : 0) + address_score(addr);
    case 0xE: return 0x20 + address_score(op2) - ((op1 & 0x0F000000u) ? 0x10 : 0);
    case 0xF: return 0x20 - ((op2 & 0xFFFF0000u) ? 0x10 : 0);
    default: return -0x40;
    }
}

/* Action Replay v3 addresses: 0x0T0XXXXX (region in bits 20-23) → 0x0T0XXXXX with the region moved to 24-27. */
static uint32_t ar_addr(uint32_t x) {
    return (x & 0xFFFFFu) | ((x << 4) & 0x0F000000u);
}

static int ar_score(uint32_t op1, uint32_t op2) {
    if (op2 == GAME_ID_MARK) return 0x100;
    if (op1 == RESEED_MARK && !(op2 & 0xFFFF0000u)) return 0x100;
    if (!op1) {
        switch (op2 & 0xFE000000u) {
        case 0x80000000u: case 0x82000000u: case 0x84000000u: return 0x40 + address_score(ar_addr(op2)); /* fills */
        case 0x18000000u: case 0x1A000000u: case 0x1C000000u: case 0x1E000000u: return 0x40;             /* ROM patches */
        case 0x00000000u: case 0x08000000u: case 0x10000000u: case 0x12000000u: case 0x14000000u:
        case 0x40000000u: case 0x60000000u: return 0x40 - ((op2 & 0x01000000u) ? 0x40 : 0);
        default: return 0;
        }
    }
    unsigned bits = (op1 & 0x06000000u) >> 22; /* 0, 8, 16 or 24 (never): mGBA's measure of the width */
    if (op1 & 0x38000000u) { /* a condition */
        if (bits >= 24) return 0;
        return 0x20 - ((op2 & ~((1u << bits) - 1)) ? 0x10 : 0);
    }
    int p = 0x20;
    switch (op1 & 0xC0000000u) {
    case 0x80000000u: /* add */
        if (op2 & ~((1u << bits) - 1)) p -= 0x10;
        /* fall through */
    case 0x00000000u: /* write */
    case 0x40000000u: /* write through a pointer */
        p += address_score(ar_addr(op1));
        if (op1 & 0x01000000u) return 0;
        break;
    default: break; /* I/O */
    }
    return p;
}

/* ---- reading lines ---- */

typedef enum { LINE_RAW, LINE_CB, LINE_GSAR } line_kind_t;

typedef struct {
    line_kind_t kind;
    uint32_t a;    /* first word */
    uint32_t b;    /* second word (CB: 16 bits; raw: the value) */
    unsigned width; /* raw: 1, 2 or 4 */
} code_line_t;

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* One line without spaces: "AAAAAAAA:VV..", 12 hex digits or 16. False if it is none of them. */
static bool read_line(const char *s, size_t n, code_line_t *out) {
    char buf[40];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (isspace((unsigned char)s[i])) continue;
        if (k + 1 >= sizeof buf) return false;
        buf[k++] = s[i];
    }
    buf[k] = '\0';
    const char *colon = strchr(buf, ':');
    size_t digits = colon ? (size_t)(colon - buf) : k;
    uint64_t v = 0;
    for (size_t i = 0; i < digits; i++) {
        int h = hexval(buf[i]);
        if (h < 0) return false;
        v = v << 4 | (uint64_t)h;
    }
    if (colon) {
        size_t vd = strlen(colon + 1);
        if (digits != 8 || (vd != 2 && vd != 4 && vd != 8)) return false;
        uint32_t val = 0;
        for (size_t i = 0; i < vd; i++) {
            int h = hexval(colon[1 + i]);
            if (h < 0) return false;
            val = val << 4 | (uint32_t)h;
        }
        *out = (code_line_t){LINE_RAW, (uint32_t)v, val, (unsigned)vd / 2};
        return true;
    }
    if (digits == 12) {
        *out = (code_line_t){LINE_CB, (uint32_t)(v >> 16), (uint32_t)(v & 0xFFFF), 0};
        return true;
    }
    if (digits == 16) {
        *out = (code_line_t){LINE_GSAR, (uint32_t)(v >> 32), (uint32_t)v, 0};
        return true;
    }
    return false;
}

/* ---- decoding, one code at a time ---- */

typedef struct {
    cheat_list_t *out;
    size_t first;        /* this cheat's first step (blocks and counts stay inside it) */
    bool scrambled;      /* GS/AR: TEA-scrambled */
    /* multi-line codes */
    size_t pending;      /* index of a step whose next line completes it (SIZE_MAX: none) */
    int pending_kind;    /* 0 none, 1 AR fill/button, 2 CB fill, 3 CB fill list, 4 GS address list, 5 AR ROM patch */
    uint32_t remaining;  /* CB fill list halfwords / GS list addresses still to come */
    uint32_t list_value; /* GS address list: the value */
    uint32_t patch_addr; /* AR ROM patch: its address */
    size_t block;        /* AR: the open if-block's step (SIZE_MAX: none) */
    cb_crypt_t cb;
} decoder_t;

#define NONE ((size_t)-1)

static bool push(decoder_t *d, cheat_step_t s) {
    return cheat_push_step(d->out, s);
}

static size_t steps_since(const decoder_t *d, size_t i) {
    return d->out->nsteps - i - 1;
}

/* GameShark v1/v2, from the plain words. */
static bool decode_gs(decoder_t *d, uint32_t op1, uint32_t op2) {
    if (d->pending_kind == 4) { /* the addresses of a 32-bit list write, two per line */
        uint32_t addrs[2] = {op1, op2};
        for (int i = 0; i < 2 && d->remaining; i++, d->remaining--) {
            if (!push(d, write_step(addrs[i] & 0x0FFFFFFFu, d->list_value, 4))) return false;
        }
        if (!d->remaining) d->pending_kind = 0;
        return true;
    }
    uint32_t addr = op1 & 0x0FFFFFFFu;
    switch (op1 >> 28) {
    case 0x0: return push(d, write_step(addr, op2, 1));
    case 0x1: return push(d, write_step(addr, op2, 2));
    case 0x2: return push(d, write_step(addr, op2, 4));
    case 0x3: /* 3000CCCC VVVVVVVV: VVVVVVVV to the CCCC addresses on the following lines */
        d->remaining = op1 & 0xFFFF;
        d->list_value = op2;
        d->pending_kind = d->remaining ? 4 : 0;
        return true;
    case 0x6: return cheat_push_patch(d->out, ROM_BASE | ((op1 & 0xFFFFFFu) << 1), (uint16_t)op2);
    case 0x8: { /* while the cheat device's button is held: never here */
        unsigned width = (op1 & 0x00F00000u) == 0x00100000u ? 1 : (op1 & 0x00F00000u) == 0x00200000u ? 2 : 0;
        if (!width) return false;
        return push(d, if_step(IF_BUTTON, 0, 0, 0, 1)) && push(d, write_step(op1 & 0x0F0FFFFFu, op2, width));
    }
    case 0xD: {
        if (op1 == RESEED_MARK) return false;
        static const if_kind_t conds[4] = {IF_EQ, IF_NE, IF_LE, IF_GE};
        unsigned c = (op2 >> 20) & 0xF;
        if (c > 3) return false;
        return push(d, if_step(conds[c], addr, op2 & 0xFFFF, 2, 1));
    }
    case 0xE: return push(d, if_step(IF_EQ, op2 & 0x0FFFFFFFu, op1 & 0xFFFF, 2, (op1 >> 16) & 0xFF));
    case 0xF: return true; /* the hook (master code) */
    default: return false;
    }
}

/* Action Replay v3: close the open if-block at an "end if" (or when the next block starts). */
static void ar_end_block(decoder_t *d) {
    cheat_step_t *s = &d->out->steps[d->block];
    uint32_t size = (uint32_t)steps_since(d, d->block);
    if (s->then_n) s->else_n = size - s->then_n; /* it had an "else" */
    else s->then_n = size;
    d->block = NONE;
}

static bool decode_ar_special(decoder_t *d, uint32_t op2) {
    switch (op2 & 0xFF000000u) {
    case 0x00000000u: return true; /* end of the list */
    case 0x08000000u: return true; /* slowdown: ignored */
    case 0x10000000u: case 0x12000000u: case 0x14000000u: { /* while the device's button is held: never here */
        unsigned width = 1u << ((op2 >> 25) & 3);
        if (!push(d, if_step(IF_BUTTON, 0, 0, 0, 1)) || !push(d, write_step(ar_addr(op2), 0, width))) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = 1;
        return true;
    }
    case 0x18000000u: case 0x1A000000u: case 0x1C000000u: case 0x1E000000u: /* ROM patch: the value on the next line */
        d->patch_addr = ROM_BASE | ((op2 & 0xFFFFFFu) << 1);
        d->pending_kind = 5;
        return true;
    case 0x40000000u: /* end if */
        if (d->block == NONE) return false;
        ar_end_block(d);
        return true;
    case 0x60000000u: /* else */
        if (d->block == NONE) return false;
        d->out->steps[d->block].then_n = (uint32_t)steps_since(d, d->block);
        return true;
    case 0x80000000u: case 0x82000000u: case 0x84000000u: { /* fill: the value and the counts on the next line */
        unsigned width = 1u << ((op2 >> 25) & 3);
        if (!push(d, write_step(ar_addr(op2), 0, width))) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = 1;
        return true;
    }
    default: return false;
    }
}

static bool decode_ar(decoder_t *d, uint32_t op1, uint32_t op2) {
    if (d->pending_kind == 5) {
        d->pending_kind = 0;
        return cheat_push_patch(d->out, d->patch_addr, (uint16_t)op1);
    }
    if (d->pending_kind == 1) {
        cheat_step_t *s = &d->out->steps[d->pending];
        s->value = op1 & width_mask(s->width);
        bool button = d->pending > d->first && d->out->steps[d->pending - 1].kind == STEP_IF &&
                      d->out->steps[d->pending - 1].cond == IF_BUTTON;
        if (!button) { /* a fill: value step, count, address step (in units of the width) */
            s->value_step = op2 >> 24;
            s->count = (op2 >> 16) & 0xFF;
            s->addr_step = (op2 & 0xFFFF) * s->width;
        }
        d->pending_kind = 0;
        return true;
    }
    if (op2 == GAME_ID_MARK) return true;
    if (op1 == 0) return decode_ar_special(d, op2);
    if (op1 == RESEED_MARK) return false;
    if (op1 >> 24 == 0xC4) return true; /* the hook (master code) */
    unsigned width = 1u << ((op1 >> 25) & 3);
    if (op1 & 0x38000000u) { /* a condition */
        static const if_kind_t conds[8] = {IF_AND, IF_EQ, IF_NE, IF_LT, IF_GT, IF_ULT, IF_UGT, IF_AND};
        unsigned action = op1 >> 30;
        if (action == 3) return false; /* "disable" codes */
        if_kind_t cond = width > 4 ? IF_NEVER : conds[(op1 >> 27) & 7];
        cheat_step_t s = if_step(cond, ar_addr(op1), op2 & width_mask(width > 4 ? 4 : width), width > 4 ? 4 : width,
                                 action == 0 ? 1 : action == 1 ? 2 : 0);
        if (action == 2) { /* a block up to "else" / "end if" */
            if (d->block != NONE) ar_end_block(d);
            if (!push(d, s)) return false;
            d->block = d->out->nsteps - 1;
            return true;
        }
        return push(d, s);
    }
    if ((op1 & 0x01000000u) && (op1 & 0xFE000000u) != 0xC6000000u) return false;
    cheat_step_t s = write_step(ar_addr(op1), 0, width);
    switch (op1 & 0xC0000000u) {
    case 0x00000000u: /* write; bytes and halfwords repeat by the value's upper bits */
        s.addr_step = width;
        if (width < 4) s.count = (op2 >> (width * 8)) + 1;
        break;
    case 0x40000000u: /* write through a pointer */
        s.kind = STEP_WRITE_INDIRECT;
        if (width < 4) s.addr_step = (op2 >> (width * 8)) * width;
        break;
    case 0x80000000u: s.kind = STEP_ADD; break;
    default: /* I/O */
        width = ((op1 >> 24) & 1) + 1;
        s.width = (uint8_t)width;
        s.addr = IO_BASE | (op1 & 0x00FFFFFFu);
        break;
    }
    s.value = op2 & width_mask(width);
    return push(d, s);
}

static uint16_t swap16(uint32_t v) {
    return (uint16_t)(((v >> 8) | (v << 8)) & 0xFFFF);
}

/* CodeBreaker, 32 + 16 bits. */
static bool decode_cb(decoder_t *d, uint32_t op1, uint16_t op2) {
    if (d->cb.master) cheat_cb_decrypt(&d->cb, &op1, &op2);
    if (d->pending_kind == 2) { /* a fill's counts */
        cheat_step_t *s = &d->out->steps[d->pending];
        s->count = op1 & 0xFFFF;
        s->addr_step = op2;
        s->value_step = op1 >> 16;
        d->pending_kind = 0;
        return true;
    }
    if (d->pending_kind == 3) { /* a fill list: up to three byte-swapped halfwords per line */
        uint32_t halves[3] = {op1 >> 16, op1 & 0xFFFF, op2};
        for (int i = 0; i < 3 && d->remaining; i++) {
            cheat_step_t *s = &d->out->steps[d->pending];
            s->value = swap16(halves[i]);
            if (--d->remaining) {
                if (!push(d, write_step(s->addr + 2, 0, 2))) return false;
                d->pending = d->out->nsteps - 1;
            }
        }
        if (!d->remaining) d->pending_kind = 0;
        return true;
    }
    uint32_t addr = op1 & 0x0FFFFFFFu;
    cheat_step_t s = write_step(addr, op2, 2);
    switch (op1 >> 28) {
    case 0x0: return true; /* game id */
    case 0x1: return true; /* the hook (master code) */
    case 0x2: s.kind = STEP_OR; break;
    case 0x3: s.width = 1; break;
    case 0x4: /* fill: the counts on the next line */
    case 0x5: /* fill list: op2 halfwords on the next lines */
        if (!push(d, s)) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = (op1 >> 28) == 0x4 ? 2 : 3;
        d->remaining = op2;
        if (d->pending_kind == 3 && !d->remaining) d->pending_kind = 0;
        return true;
    case 0x6: s.kind = STEP_AND; break;
    case 0x7: s = if_step(IF_EQ, addr, op2, 2, 1); break;
    case 0x8: break;
    case 0x9: cheat_cb_reseed(&d->cb, op1, op2); return true; /* the codes after it are scrambled */
    case 0xA: s = if_step(IF_NE, addr, op2, 2, 1); break;
    case 0xB: s = if_step(IF_GT, addr, op2, 2, 1); break;
    case 0xC: s = if_step(IF_LT, addr, op2, 2, 1); break;
    case 0xD:
        if (addr != 0x20) return false;
        s = if_step(IF_NAND, KEYINPUT_ADDR, op2, 2, 1); /* while these GBA buttons are held */
        break;
    case 0xE: s.kind = STEP_ADD; break;
    case 0xF: s = if_step(IF_AND, addr, op2, 2, 1); break;
    default: return false;
    }
    return push(d, s);
}

static bool decode_raw(decoder_t *d, const code_line_t *c) {
    if (c->a >= ROM_BASE && c->a < 0x0E000000u) { /* the ROM: patches, one halfword at a time */
        if (c->width == 1) return false;
        if (!cheat_push_patch(d->out, c->a, (uint16_t)(c->width == 4 ? c->b >> 16 : c->b))) return false;
        return c->width == 2 || cheat_push_patch(d->out, c->a + 2, (uint16_t)c->b);
    }
    return push(d, write_step(c->a, c->b, c->width));
}

/* ---- the whole text ---- */

/* How an 8 + 8 code reads best: GameShark scrambled or plain, Action Replay scrambled or plain. */
static gba_cheat_type_t detect_gsar(uint32_t op1, uint32_t op2, bool *scrambled) {
    uint32_t a = op1, b = op2;
    int best;
    gba_cheat_type_t type = GBA_CHEAT_GAMESHARK;
    cheat_tea_decrypt(&a, &b, CHEAT_GS_SEEDS);
    best = gs_score(a, b);
    *scrambled = true;
    a = op1;
    b = op2;
    cheat_tea_decrypt(&a, &b, CHEAT_AR3_SEEDS);
    int s = ar_score(a, b);
    if (s > best) {
        best = s;
        type = GBA_CHEAT_ACTION_REPLAY;
    }
    s = gs_score(op1, op2);
    if (s > best) {
        best = s;
        type = GBA_CHEAT_GAMESHARK;
        *scrambled = false;
    }
    s = ar_score(op1, op2);
    if (s > best) {
        type = GBA_CHEAT_ACTION_REPLAY;
        *scrambled = false;
    }
    return type;
}

int cheat_decode(const char *text, gba_cheat_type_t type, cheat_list_t *out, gba_cheat_type_t *used) {
    decoder_t d;
    memset(&d, 0, sizeof d);
    d.out = out;
    d.first = out->nsteps;
    d.pending = NONE;
    d.block = NONE;
    d.scrambled = true;
    bool decided = type != GBA_CHEAT_AUTO;
    int line = 0, codes = 0;
    const char *p = text ? text : "";
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        line++;
        size_t k = 0;
        while (k < n && isspace((unsigned char)p[k])) k++;
        if (k < n) { /* not blank */
            code_line_t c;
            if (!read_line(p, n, &c)) return line;
            if (!decided) {
                type = c.kind == LINE_RAW ? GBA_CHEAT_RAW
                       : c.kind == LINE_CB ? GBA_CHEAT_CODEBREAKER
                                           : detect_gsar(c.a, c.b, &d.scrambled);
                decided = true;
            }
            bool ok;
            switch (type) {
            case GBA_CHEAT_RAW: ok = c.kind == LINE_RAW && decode_raw(&d, &c); break;
            case GBA_CHEAT_CODEBREAKER: ok = c.kind == LINE_CB && decode_cb(&d, c.a, (uint16_t)c.b); break;
            case GBA_CHEAT_GAMESHARK:
            case GBA_CHEAT_ACTION_REPLAY: {
                if (c.kind != LINE_GSAR) {
                    ok = false;
                    break;
                }
                uint32_t a = c.a, b = c.b;
                bool gs = type == GBA_CHEAT_GAMESHARK;
                if (d.scrambled) cheat_tea_decrypt(&a, &b, gs ? CHEAT_GS_SEEDS : CHEAT_AR3_SEEDS);
                ok = gs ? decode_gs(&d, a, b) : decode_ar(&d, a, b);
                break;
            }
            default: ok = false; break;
            }
            if (!ok) return line;
            codes++;
        }
        if (!end) break;
        p = end + 1;
    }
    if (!codes) return line ? line : 1; /* nothing to read */
    if (used) *used = type;
    return 0;
}

gba_cheat_type_t gba_cheat_check(const char *text, gba_cheat_type_t type, int *bad_line) {
    cheat_list_t l;
    memset(&l, 0, sizeof l);
    gba_cheat_type_t used = type;
    int bad = cheat_decode(text, type, &l, &used);
    cheat_list_free(&l);
    if (bad_line) *bad_line = bad;
    return used;
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: `385 tests, 0 failed` (380 + 5); no warnings.

- [x] **Step 5: Commit**

```bash
git add core/include/gba.h core/src/cheats/cheats.h core/src/cheats/decrypt.c core/src/cheats/decode.c tests/unit/test_cheats_cipher.c docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(core): cheat codes read - Action Replay v3, GameShark, CodeBreaker, address:value, auto-detected" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Running cheats every frame

**Files:**
- Create: `core/src/cheats/apply.c`, `tests/unit/test_cheats_run.c`
- Modify: `core/src/mem/bus.h`, `core/src/mem/bus.c`, `core/src/gba.c`, `core/src/gba_internal.h`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_cheats_run.c`:

```c
/* Cheats: what the steps do (core/src/cheats/apply.c). The Action Replay cases are mGBA's tests (same codes, same
 * expected memory) rewritten here, scrambled first so the whole path is used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

static gba_t *machine(void) {
    static const uint32_t spin[] = {0xEAFFFFFE}; /* b . : the program never touches memory */
    return tu_arm(spin, TU_LEN(spin));
}

static uint32_t peek(gba_t *g, uint32_t addr, unsigned width) {
    return bus_peek(g, addr, width);
}

static void set(gba_t *g, const char *text, gba_cheat_type_t type) {
    const char *texts[1] = {text};
    CHECK_EQ(gba_cheats_set(g, texts, &type, 1), GBA_OK);
}

/* Plain Action Replay / GameShark codes → scrambled text, one per line. */
static char *scrambled_text(const uint32_t (*codes)[2], size_t n, const uint32_t seeds[4]) {
    char *text = malloc(n * 18 + 1), *p = text;
    for (size_t i = 0; i < n; i++) {
        uint32_t a = codes[i][0], b = codes[i][1];
        cheat_tea_encrypt(&a, &b, seeds);
        p += sprintf(p, "%08X %08X\n", (unsigned)a, (unsigned)b);
    }
    *p = '\0';
    return text;
}

static void set_ar(gba_t *g, const uint32_t (*codes)[2], size_t n) {
    char *text = scrambled_text(codes, n, CHEAT_AR3_SEEDS);
    set(g, text, GBA_CHEAT_ACTION_REPLAY);
    free(text);
}

#define IW 0x03000000u

TEST(cheat_ar_writes_bytes_halfwords_words) {
    static const uint32_t c[][2] = {{0x00300000, 0x00000078}, {0x02300002, 0x00005678}, {0x04300004, 0x12345678}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    CHECK_EQ(peek(g, IW, 1), 0);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x78);
    CHECK_EQ(peek(g, IW + 2, 2), 0x5678);
    CHECK_EQ(peek(g, IW + 4, 4), 0x12345678);
    gba_destroy(g);
}

TEST(cheat_ar_fills_with_steps) {
    static const uint32_t c1[][2] = {{0x00000000, 0x80300000}, {0x00000001, 0x01020002}};
    static const uint32_t c4[][2] = {{0x00000000, 0x84300000}, {0x00000001, 0x01020002}};
    gba_t *g = machine();
    set_ar(g, c1, TU_LEN(c1));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    CHECK_EQ(peek(g, IW + 1, 1), 0);
    CHECK_EQ(peek(g, IW + 2, 1), 2);
    CHECK_EQ(peek(g, IW + 3, 1), 0);
    gba_reset(g);
    set_ar(g, c4, TU_LEN(c4));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 2), 1);
    CHECK_EQ(peek(g, IW + 4, 2), 0);
    CHECK_EQ(peek(g, IW + 8, 2), 2);
    CHECK_EQ(peek(g, IW + 12, 2), 0);
    gba_destroy(g);
}

TEST(cheat_ar_if_guards_the_next_code) {
    static const uint32_t c[][2] = {
        {0x00300001, 0x00000011}, {0x08300000, 0x00000000}, {0x00300001, 0x00000012}, {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    gba_destroy(g);
}

TEST(cheat_ar_if_blocks_with_and_without_else) {
    static const uint32_t block[][2] = {{0x00300001, 0x00000011}, {0x88300000, 0x00000000}, {0x00300001, 0x00000012},
                                        {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    static const uint32_t with_else[][2] = {{0x00300001, 0x00000011}, {0x00300002, 0x00000021},
                                            {0x88300000, 0x00000000}, {0x00300001, 0x00000012},
                                            {0x00000000, 0x60000000}, {0x00300002, 0x00000022},
                                            {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, block, TU_LEN(block));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);

    gba_reset(g);
    set_ar(g, with_else, TU_LEN(with_else));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    CHECK_EQ(peek(g, IW + 2, 1), 0x21);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    CHECK_EQ(peek(g, IW + 2, 1), 0x22);
    gba_destroy(g);
}

TEST(cheat_ar_nested_conditions_inside_if_and_else) {
    static const uint32_t c[][2] = {
        {0x00300003, 0x00000031}, {0x00300004, 0x00000041}, {0x00300005, 0x00000051}, {0x00300006, 0x00000061},
        {0x00300007, 0x00000071}, {0x00300008, 0x00000081}, {0x88300000, 0x00000000}, {0x00300003, 0x00000032},
        {0x08300001, 0x00000000}, {0x00300004, 0x00000042}, {0x00300005, 0x00000052}, {0x00000000, 0x60000000},
        {0x00300006, 0x00000062}, {0x08300002, 0x00000000}, {0x00300007, 0x00000072}, {0x00300008, 0x00000082},
        {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    /* {IW+0, IW+1, IW+2} before → the bytes at IW+3..IW+8 after (mGBA's expected values) */
    static const struct {
        uint8_t in[3];
        uint8_t out[6];
    } cases[] = {
        {{0, 0, 0}, {0x32, 0x42, 0x52, 0x61, 0x71, 0x81}}, {{1, 0, 0}, {0x31, 0x41, 0x51, 0x62, 0x72, 0x82}},
        {{0, 1, 0}, {0x32, 0x41, 0x52, 0x61, 0x71, 0x81}}, {{0, 0, 1}, {0x32, 0x42, 0x52, 0x61, 0x71, 0x81}},
        {{1, 1, 0}, {0x31, 0x41, 0x51, 0x62, 0x72, 0x82}}, {{1, 0, 1}, {0x31, 0x41, 0x51, 0x62, 0x71, 0x82}},
        {{0, 1, 1}, {0x32, 0x41, 0x52, 0x61, 0x71, 0x81}}, {{1, 1, 1}, {0x31, 0x41, 0x51, 0x62, 0x71, 0x82}},
    };
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    for (size_t k = 0; k < TU_LEN(cases); k++) {
        gba_reset(g);
        for (int i = 0; i < 3; i++) bus_poke(g, IW + (uint32_t)i, cases[k].in[i], 1);
        cheats_apply(g);
        for (int i = 0; i < 6; i++) CHECK_EQ(peek(g, IW + 3 + (uint32_t)i, 1), cases[k].out[i]);
    }
    gba_destroy(g);
}

TEST(cheat_ar_two_code_condition_and_never) {
    static const uint32_t two[][2] = {{0x00300002, 0x00000021}, {0x48300000, 0x00000000}, {0x08300001, 0x00000000},
                                      {0x00300002, 0x00000022}, {0x00300000, 0x00000001}};
    static const uint32_t never1[][2] = {
        {0x00300001, 0x00000011}, {0x0E300000, 0x00000000}, {0x00300001, 0x00000012}, {0x00300000, 0x00000001}};
    static const uint32_t never_block[][2] = {{0x00300001, 0x00000011}, {0x8E300000, 0x00000000},
                                              {0x00300001, 0x00000012}, {0x00000000, 0x40000000},
                                              {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, two, TU_LEN(two));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 2, 1), 0x22);
    gba_reset(g);
    bus_poke(g, IW + 1, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 2, 1), 0x21);
    for (int k = 0; k < 2; k++) {
        gba_reset(g);
        if (k) set_ar(g, never_block, TU_LEN(never_block));
        else set_ar(g, never1, TU_LEN(never1));
        cheats_apply(g);
        CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    }
    gba_destroy(g);
}

TEST(cheat_ar_device_button_codes_never_fire_and_master_lines_are_skipped) {
    static const uint32_t c[][2] = {{0x00000000, 0x001DC0DE}, {0xC4000100, 0x00000000}, /* game id, hook */
                                    {0x00000000, 0x10300000}, {0x00000001, 0x00000000}, /* while the button... */
                                    {0x00300001, 0x00000007}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    CHECK_EQ(peek(g, IW + 1, 1), 7);
    gba_destroy(g);
}

TEST(cheat_gameshark_writes_conditions_and_lists) {
    static const uint32_t c[][2] = {
        {0x03000010, 0x000000AB}, {0x13000012, 0x0000BEEF}, {0x23000014, 0xCAFEF00D}, /* write 1, 2, 4 */
        {0xD3000020, 0x00000005}, {0x03000021, 0x00000055},                           /* if [20] == 5 */
        {0xE0020005, 0x03000030}, {0x03000031, 0x00000031}, {0x03000032, 0x00000032}, /* if [30] == 5: 2 codes */
        {0x30000003, 0x11223344}, {0x03000040, 0x03000044}, {0x03000048, 0x00000000}, /* list: 3 addresses */
        {0xF0000100, 0x00000001}};                                                     /* hook */
    gba_t *g = machine();
    char *text = scrambled_text(c, TU_LEN(c), CHEAT_GS_SEEDS);
    set(g, text, GBA_CHEAT_GAMESHARK);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 0x10, 1), 0xAB);
    CHECK_EQ(peek(g, IW + 0x12, 2), 0xBEEF);
    CHECK_EQ(peek(g, IW + 0x14, 4), 0xCAFEF00D);
    CHECK_EQ(peek(g, IW + 0x21, 1), 0);
    CHECK_EQ(peek(g, IW + 0x31, 1), 0);
    CHECK_EQ(peek(g, IW + 0x40, 4), 0x11223344);
    CHECK_EQ(peek(g, IW + 0x44, 4), 0x11223344);
    CHECK_EQ(peek(g, IW + 0x48, 4), 0x11223344);
    bus_poke(g, IW + 0x20, 5, 2);
    bus_poke(g, IW + 0x30, 5, 2);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 0x21, 1), 0x55);
    CHECK_EQ(peek(g, IW + 0x31, 1), 0x31);
    CHECK_EQ(peek(g, IW + 0x32, 1), 0x32);
    free(text);
    gba_destroy(g);
}

TEST(cheat_codebreaker_kinds) {
    gba_t *g = machine();
    set(g,
        "0000AAAA 0001\n"  /* game id: skipped */
        "13000100 0007\n"  /* hook: skipped */
        "33000000 0063\n"  /* write byte */
        "83000002 1234\n"  /* write halfword */
        "43000010 0101\n"  /* fill: 0x0101 ... */
        "00010003 0002\n"  /* ... value +1, 3 times, address +2 */
        "E3000020 0010\n"  /* add */
        "23000022 00F0\n"  /* or */
        "63000024 0F0F\n"  /* and */
        "73000026 0000\n"  /* if [26] == 0 ... */
        "33000028 0042\n", /* ... this */
        GBA_CHEAT_CODEBREAKER);
    bus_poke(g, IW + 0x20, 0x0005, 2);
    bus_poke(g, IW + 0x22, 0x000F, 2);
    bus_poke(g, IW + 0x24, 0xFFFF, 2);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x63);
    CHECK_EQ(peek(g, IW + 2, 2), 0x1234);
    CHECK_EQ(peek(g, IW + 0x10, 2), 0x0101);
    CHECK_EQ(peek(g, IW + 0x12, 2), 0x0102);
    CHECK_EQ(peek(g, IW + 0x14, 2), 0x0103);
    CHECK_EQ(peek(g, IW + 0x16, 2), 0);
    CHECK_EQ(peek(g, IW + 0x20, 2), 0x0015);
    CHECK_EQ(peek(g, IW + 0x22, 2), 0x00FF);
    CHECK_EQ(peek(g, IW + 0x24, 2), 0x0F0F);
    CHECK_EQ(peek(g, IW + 0x28, 1), 0x42);
    gba_destroy(g);
}

TEST(cheat_codebreaker_while_gba_buttons_are_held) {
    gba_t *g = machine();
    set(g, "D0000020 0005\n33000000 0001\n", GBA_CHEAT_CODEBREAKER); /* A (bit 0) + SELECT (bit 2) held */
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    gba_set_keys(g, GBA_KEY_A);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_SELECT);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    gba_destroy(g);
}

TEST(cheat_codebreaker_scrambled_codes) {
    cb_crypt_t c;
    const uint32_t key1 = 0x9123ABCD;
    const uint16_t key2 = 0x5678;
    cheat_cb_reseed(&c, key1, key2);
    uint32_t a1 = 0x33000000;
    uint16_t b1 = 0x0063;
    uint32_t a2 = 0x83000002;
    uint16_t b2 = 0x1234;
    cheat_cb_encrypt(&c, &a1, &b1);
    cheat_cb_encrypt(&c, &a2, &b2);
    char text[80];
    snprintf(text, sizeof text, "%08X %04X\n%08X %04X\n%08X %04X\n", (unsigned)key1, key2, (unsigned)a1, b1,
             (unsigned)a2, b2);
    gba_t *g = machine();
    set(g, text, GBA_CHEAT_AUTO);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x63);
    CHECK_EQ(peek(g, IW + 2, 2), 0x1234);
    gba_destroy(g);
}

TEST(cheat_rom_patches_are_undone_when_switched_off) {
    gba_t *g = machine();
    uint32_t before = peek(g, 0x08000100, 4);
    set(g, "08000100:BEEF\n08000102:1234\n02000000:12345678", GBA_CHEAT_RAW);
    CHECK_EQ(peek(g, 0x08000100, 2), 0xBEEF);
    CHECK_EQ(peek(g, 0x08000102, 2), 0x1234);
    cheats_apply(g);
    CHECK_EQ(peek(g, 0x02000000, 4), 0x12345678);
    CHECK_EQ(gba_cheats_set(g, NULL, NULL, 0), GBA_OK); /* all off */
    CHECK_EQ(peek(g, 0x08000100, 4), before);
    gba_destroy(g);
}

TEST(cheat_a_bad_text_leaves_no_cheats_and_undoes_the_old_patches) {
    gba_t *g = machine();
    uint32_t before = peek(g, 0x08000100, 2);
    set(g, "08000100:BEEF", GBA_CHEAT_RAW);
    const char *texts[2] = {"02000000:11", "not a code"};
    gba_cheat_type_t types[2] = {GBA_CHEAT_AUTO, GBA_CHEAT_AUTO};
    CHECK_EQ(gba_cheats_set(g, texts, types, 2), GBA_ERR_CHEAT);
    CHECK_EQ(peek(g, 0x08000100, 2), before);
    cheats_apply(g);
    CHECK_EQ(peek(g, 0x02000000, 1), 0);
    gba_destroy(g);
}

TEST(cheat_list_is_in_no_state_and_survives_a_loaded_state) {
    gba_t *a = machine(), *b = machine();
    set(a, "02000000:77", GBA_CHEAT_RAW);
    gba_run_frame(a); /* cheats run at the end of the frame */
    CHECK_EQ(peek(a, 0x02000000, 1), 0x77);
    size_t n = gba_state_size(a);
    uint8_t *st = malloc(n);
    CHECK_EQ(gba_state_save(a, st, n), n);
    CHECK_EQ(gba_state_load(b, st, n), GBA_OK); /* b has no cheats: the state brings none */
    bus_poke(b, 0x02000000, 0, 1);
    gba_run_frame(b);
    CHECK_EQ(peek(b, 0x02000000, 1), 0);
    bus_poke(a, 0x02000000, 0, 1);
    CHECK_EQ(gba_state_load(a, st, n), GBA_OK); /* a keeps its cheats */
    bus_poke(a, 0x02000000, 0, 1);
    gba_run_frame(a);
    CHECK_EQ(peek(a, 0x02000000, 1), 0x77);
    free(st);
    gba_destroy(a);
    gba_destroy(b);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: the build fails — `call to undeclared function 'bus_poke'`.

- [x] **Step 3: Write the code**

Replace `core/src/mem/bus.h`:

```c
#ifndef GBA_BUS_H
#define GBA_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

#define BIOS_SIZE 0x4000u
#define EWRAM_SIZE 0x40000u
#define IWRAM_SIZE 0x8000u
#define PAL_SIZE 0x400u
#define VRAM_SIZE 0x18000u
#define OAM_SIZE 0x400u

typedef struct {
    uint8_t bios[BIOS_SIZE];
    uint8_t ewram[EWRAM_SIZE];
    uint8_t iwram[IWRAM_SIZE];
    uint8_t pal[PAL_SIZE];
    uint8_t vram[VRAM_SIZE];
    uint8_t oam[OAM_SIZE];
    uint16_t waitcnt;
    uint8_t cycles[16][2][2]; /* [region][0: 8/16-bit, 1: 32-bit][0: non-seq, 1: seq] */
    bool in_bios;             /* the executing instruction was fetched from the BIOS */
    uint32_t bios_latch;      /* what BIOS reads return while executing outside the BIOS */
    bool data_access;         /* the current instruction made a data read/write (not a code fetch) */
    bool prefetch_hint;       /* this instruction left the cartridge bus free (RAM/I/O access or multiply) */
    uint32_t off_rom;         /* cycles this instruction spent off the cartridge bus */
    uint32_t prefetch_end;    /* address of the last halfword the prefetch buffer holds (0: empty) */
    uint32_t open_bus;        /* last opcode fetched: open-bus fallback when the pipeline is unreadable */
} bus_t;

void bus_reset(gba_t *g); /* clears RAM (not the BIOS) and restores default timings */
void bus_set_waitcnt(gba_t *g, uint16_t value);

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq);
uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq); /* addr aligned down to 2 */
uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq); /* addr aligned down to 4 */
void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq);
void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq);
void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq);
/* Instruction fetches: also update the BIOS latch and the open-bus value. */
uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq);
uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq);
void bus_idle(gba_t *g, uint32_t cycles);
/* Cycles of one code fetch at addr (no access is made): for the pipeline refill. */
uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq);
/* Prefetch buffer (WAITCNT bit 14), mGBA's estimate: called at the end of an instruction that ran from
 * the cartridge and spent cycles off it. Credits the cycles the buffer saves and makes the next fetch S. */
void bus_prefetch_stall(gba_t *g);
/* Read without cycles or side effects, for debuggers and tests. */
uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size);
/* Write without cycles, for cheats: RAM, I/O (through the register write), palette, VRAM and OAM only
 * (their usual byte-write rules apply); the BIOS, ROM and save chip are left alone. */
void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size);
/* What unmapped reads return: the opcode(s) the CPU has prefetched (ARM: the word at the current
 * instruction + 8; Thumb: by the region the code runs from, see GBATEK "Unpredictable Things"). */
uint32_t bus_open_bus(gba_t *g);

#endif
```

Replace `core/src/mem/bus.c`:

```c
#include "mem/bus.h"

#include <string.h>

#include "gba_internal.h"

static inline uint32_t load_n(const uint8_t *p, unsigned size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static inline void store_n(uint8_t *p, uint32_t v, unsigned size) {
    p[0] = (uint8_t)v;
    if (size >= 2) p[1] = (uint8_t)(v >> 8);
    if (size == 4) {
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
}

/* The size-byte lane of a 32-bit bus value that an address selects. */
static inline uint32_t lane(uint32_t word, uint32_t addr, unsigned size) {
    switch (size) {
    case 1: return (word >> ((addr & 3) * 8)) & 0xFF;
    case 2: return (word >> ((addr & 2) * 8)) & 0xFFFF;
    default: return word;
    }
}

/* 96 KiB of VRAM in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF. */
static inline uint32_t vram_offset(uint32_t addr) {
    uint32_t off = addr & 0x1FFFF;
    return off >= 0x18000 ? off - 0x8000 : off;
}

/* The save area has an 8-bit bus: accesses of every size keep their exact address. */
static inline bool save_area(uint32_t addr) {
    return (addr >> 24) == 0xE || (addr >> 24) == 0xF;
}

/* A halfword of the prefetch pipeline: real memory only, no side effects or cycles. Anything else
 * (unmapped space, I/O, the save area) falls back to the last fetched opcode, so a PC in unmapped
 * space cannot recurse into open bus. */
static uint32_t pipeline16(gba_t *g, uint32_t addr) {
    bus_t *b = &g->bus;
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE && b->in_bios) return load_n(&b->bios[addr], 2);
        break;
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], 2);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], 2);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], 2);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], 2);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], 2);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: return cart_rom_read(&g->cart, addr, 2);
    default: break;
    }
    return lane(b->open_bus, addr, 2);
}

uint32_t bus_open_bus(gba_t *g) {
    const arm_cpu_t *c = &g->cpu;
    uint32_t next = c->r[15]; /* while an instruction runs: its address + 8 (ARM) or + 4 (Thumb) */
    if (!(c->cpsr & CPSR_T)) return pipeline16(g, next) | pipeline16(g, next + 2) << 16;
    uint32_t pc = next - 4; /* $, the Thumb instruction being executed */
    switch (pc >> 24) {
    case 0x0: /* BIOS and OAM: 32-bit bus */
    case 0x7:
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 6) << 16;
    case 0x3: /* IWRAM */
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 2) << 16;
    default: { /* EWRAM, palette, VRAM, ROM: 16-bit bus */
        uint32_t half = pipeline16(g, pc + 4);
        return half | half << 16;
    }
    }
}

static uint32_t mem_read(gba_t *g, uint32_t addr, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE) {
            uint32_t word = b->in_bios ? load_n(&b->bios[addr & 0x3FFC], 4) : b->bios_latch;
            return lane(word, addr, size);
        }
        return lane(bus_open_bus(g), addr, size);
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x4: return io_read(g, addr, size);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], size);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], size);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        if (gpio_mapped(g, addr)) return gpio_read(g, addr, size);
        return cart_rom_read(&g->cart, addr, size);
    case 0xE:
    case 0xF: {
        uint32_t byte = save_read8(g, addr); /* wider reads repeat the byte */
        return size == 1 ? byte : size == 2 ? byte * 0x0101u : byte * 0x01010101u;
    }
    default: return lane(bus_open_bus(g), addr, size);
    }
}

static void mem_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x2: store_n(&b->ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&b->iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    case 0x4: io_write(g, addr, v, size); break;
    case 0x5:
        if (size == 1) store_n(&b->pal[addr & (PAL_SIZE - 2)], (v & 0xFF) * 0x0101u, 2);
        else store_n(&b->pal[addr & (PAL_SIZE - 1)], v, size);
        break;
    case 0x6: {
        uint32_t off = vram_offset(addr);
        if (size == 1) {
            /* Byte writes land as a doubled halfword in BG VRAM and are dropped in OBJ VRAM. */
            uint32_t bg_end = (g->ppu.dispcnt & 7) >= 3 ? 0x14000 : 0x10000;
            if (off < bg_end) store_n(&b->vram[off & ~1u], (v & 0xFF) * 0x0101u, 2);
        } else {
            store_n(&b->vram[off], v, size);
        }
        break;
    }
    case 0x7:
        if (size != 1) store_n(&b->oam[addr & (OAM_SIZE - 1)], v, size); /* byte writes are ignored */
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* bit 0 */
        else if (gpio_mapped(g, addr)) gpio_write(g, addr, v, size);
        break;
    case 0xE:
    case 0xF: save_write8(g, addr, (uint8_t)(v >> (8 * (addr & (size - 1))))); break; /* one byte, by address */
    default: break; /* BIOS, ROM and unmapped space ignore writes */
    }
}

static inline uint32_t access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return mem_read(g, addr, 1);
}

uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return mem_read(g, addr, 2);
}

uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return mem_read(g, addr, 4);
}

void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    mem_write(g, addr, value, 1);
}

void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    mem_write(g, addr, value, 2);
}

void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    mem_write(g, addr, value, 4);
}

uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    uint32_t op = mem_read(g, addr, 4);
    /* The BIOS latch holds the last BIOS word fetched: two instructions ahead in the pipeline. */
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 8) & 0x3FFC], 4);
    b->open_bus = op;
    return op;
}

uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    uint32_t op = mem_read(g, addr, 2);
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 4) & 0x3FFC], 4);
    b->open_bus = op | op << 16;
    return op;
}

void bus_prefetch_stall(gba_t *g) {
    bus_t *b = &g->bus;
    uint32_t pc = g->cpu.r[15];
    uint32_t region = pc >> 24;
    if (!(b->waitcnt & 0x4000) || region < 0x8 || region > 0xD) return;
    int32_t s = (int32_t)b->cycles[region][0][1] - 1; /* the cartridge's sequential wait states (16-bit) */
    int32_t wait = (int32_t)b->off_rom;
    int32_t previous = 0, max_loads = 8;
    uint32_t dist = b->prefetch_end - pc; /* halfwords still buffered from an earlier stall */
    if (dist < 16) {
        previous = (int32_t)(dist >> 1);
        max_loads -= previous;
    }
    int32_t stall = s + 1, loads = 1;
    while (stall < wait && loads < max_loads) {
        stall += s;
        loads++;
    }
    b->prefetch_end = pc + 2u * (uint32_t)(loads + previous - 1);
    int32_t used = stall > wait ? stall : wait; /* the wait cannot be shorter than the prefetch */
    g->now = (uint64_t)((int64_t)g->now + (used - wait) - stall);
    g->cpu.next_fetch_seq = true; /* the next fetch comes out of the buffer: sequential */
}

uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    return access_cycles(g, addr, size, seq);
}

void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
    g->bus.off_rom += cycles;
}

uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size) {
    return mem_read(g, addr & ~(uint32_t)(size - 1), size);
}

void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t region = addr >> 24;
    if (region >= 0x2 && region <= 0x7) mem_write(g, addr & ~(uint32_t)(size - 1), value, size);
}

static void set_region(bus_t *b, int region, uint8_t n16, uint8_t s16, uint8_t n32, uint8_t s32) {
    b->cycles[region][0][0] = n16;
    b->cycles[region][0][1] = s16;
    b->cycles[region][1][0] = n32;
    b->cycles[region][1][1] = s32;
}

void bus_set_waitcnt(gba_t *g, uint16_t value) {
    static const uint8_t nonseq[4] = {4, 3, 2, 8};
    static const uint8_t seq[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    bus_t *b = &g->bus;
    b->waitcnt = value & 0x7FFF;
    for (int ws = 0; ws < 3; ws++) {
        uint8_t n = (uint8_t)(1 + nonseq[(value >> (2 + ws * 3)) & 3]);
        uint8_t s = (uint8_t)(1 + seq[ws][(value >> (4 + ws * 3)) & 1]);
        /* The cartridge bus is 16 bits wide: a 32-bit access is one N plus one S access. */
        set_region(b, 0x8 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
        set_region(b, 0x9 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
    }
    uint8_t sram = (uint8_t)(1 + nonseq[value & 3]);
    set_region(b, 0xE, sram, sram, sram, sram);
    set_region(b, 0xF, sram, sram, sram, sram);
}

void bus_reset(gba_t *g) {
    bus_t *b = &g->bus;
    memset(b->ewram, 0, sizeof b->ewram);
    memset(b->iwram, 0, sizeof b->iwram);
    memset(b->pal, 0, sizeof b->pal);
    memset(b->vram, 0, sizeof b->vram);
    memset(b->oam, 0, sizeof b->oam);
    set_region(b, 0x0, 1, 1, 1, 1); /* BIOS */
    set_region(b, 0x1, 1, 1, 1, 1);
    set_region(b, 0x2, 3, 3, 6, 6); /* EWRAM: 16-bit bus, 2 wait states */
    set_region(b, 0x3, 1, 1, 1, 1); /* IWRAM */
    set_region(b, 0x4, 1, 1, 1, 1); /* I/O */
    set_region(b, 0x5, 1, 1, 2, 2); /* palette: 16-bit bus */
    set_region(b, 0x6, 1, 1, 2, 2); /* VRAM: 16-bit bus */
    set_region(b, 0x7, 1, 1, 1, 1); /* OAM */
    bus_set_waitcnt(g, 0);
    b->in_bios = false;
    b->bios_latch = 0;
    b->open_bus = 0;
}
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
    struct cheat_list *cheats; /* the switched-on cheats (cheats/), NULL without; in no state chunk */
};

/* gba_state_load without forgetting the rewind history (rewind.c loads its own snapshots with it). */
gba_err_t state_load(gba_t *g, const void *buf, size_t len);

/* Dispatch every scheduler event due at g->now, in timestamp order. */
void gba_run_events(gba_t *g);

#endif
```

`core/src/cheats/apply.c`:

```c
/* Running the cheats: the steps at the end of every frame, the ROM patches while the list is set. */
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"
#include "gba_internal.h"

static uint32_t rd(gba_t *g, uint32_t addr, unsigned width) {
    return bus_peek(g, addr, width);
}

static bool holds(gba_t *g, const cheat_step_t *s) {
    if (s->cond == IF_NEVER || s->cond == IF_BUTTON) return false; /* no cheat-device button here */
    uint32_t v = rd(g, s->addr, s->width), x = s->value;
    switch (s->cond) {
    case IF_EQ: return v == x;
    case IF_NE: return v != x;
    case IF_LT: return (int32_t)v < (int32_t)x;
    case IF_GT: return (int32_t)v > (int32_t)x;
    case IF_LE: return (int32_t)v <= (int32_t)x;
    case IF_GE: return (int32_t)v >= (int32_t)x;
    case IF_ULT: return v < x;
    case IF_UGT: return v > x;
    case IF_AND: return (v & x) != 0;
    case IF_NAND: return (v & x) == 0;
    default: return false;
    }
}

static void do_step(gba_t *g, const cheat_step_t *s) {
    uint32_t addr = s->addr, operand = s->value;
    for (uint32_t n = 0; n < s->count; n++) {
        uint32_t target = addr, value;
        switch (s->kind) {
        case STEP_WRITE: value = operand; break;
        case STEP_WRITE_INDIRECT: /* the address holds a pointer; the step is an offset from it */
            value = operand;
            target = rd(g, addr, 4) + s->addr_step;
            break;
        case STEP_ADD: value = rd(g, addr, s->width) + operand; break;
        case STEP_AND: value = rd(g, addr, s->width) & operand; break;
        case STEP_OR: value = rd(g, addr, s->width) | operand; break;
        default: return;
        }
        bus_poke(g, target, value, s->width);
        if (s->kind != STEP_WRITE_INDIRECT) addr += s->addr_step;
        operand += s->value_step;
    }
}

/* Steps [i, end): a condition runs its "then" steps when it holds, else the "else" steps after them. */
static void run(gba_t *g, const cheat_step_t *steps, size_t i, size_t end) {
    while (i < end) {
        const cheat_step_t *s = &steps[i];
        if (s->kind != STEP_IF) {
            do_step(g, s);
            i++;
            continue;
        }
        size_t then_end = i + 1 + s->then_n, else_end;
        if (then_end > end) then_end = end;
        else_end = then_end + s->else_n;
        if (else_end > end) else_end = end;
        if (holds(g, s)) run(g, steps, i + 1, then_end);
        else run(g, steps, then_end, else_end);
        i = else_end;
    }
}

void cheats_apply(gba_t *g) {
    cheat_list_t *l = g->cheats;
    if (l && l->nsteps) run(g, l->steps, 0, l->nsteps);
}

static bool in_rom(const gba_t *g, uint32_t addr) {
    uint32_t off = addr & 0x01FFFFFFu;
    return addr >= 0x08000000u && addr < 0x0E000000u && off + 1 < g->cart.rom_size;
}

static void patch_rom(gba_t *g, cheat_list_t *l) {
    for (size_t i = 0; i < l->npatches; i++) {
        cheat_patch_t *p = &l->patches[i];
        if (!in_rom(g, p->addr)) continue;
        uint8_t *b = g->cart.rom + (p->addr & 0x01FFFFFFu);
        p->old = (uint16_t)(b[0] | b[1] << 8);
        b[0] = (uint8_t)p->value;
        b[1] = (uint8_t)(p->value >> 8);
    }
    l->applied = true;
}

static void unpatch_rom(gba_t *g, cheat_list_t *l) {
    if (!l->applied) return;
    for (size_t i = l->npatches; i-- > 0;) { /* last first: two patches of one place restore the original */
        const cheat_patch_t *p = &l->patches[i];
        if (!in_rom(g, p->addr)) continue;
        uint8_t *b = g->cart.rom + (p->addr & 0x01FFFFFFu);
        b[0] = (uint8_t)p->old;
        b[1] = (uint8_t)(p->old >> 8);
    }
    l->applied = false;
}

void cheats_drop(gba_t *g) {
    if (!g->cheats) return;
    unpatch_rom(g, g->cheats);
    cheat_list_free(g->cheats);
    free(g->cheats);
    g->cheats = NULL;
}

gba_err_t gba_cheats_set(gba_t *g, const char *const *texts, const gba_cheat_type_t *types, size_t count) {
    cheats_drop(g);
    if (!count) return GBA_OK;
    cheat_list_t *l = calloc(1, sizeof *l);
    if (!l) return GBA_ERR_NOMEM;
    for (size_t i = 0; i < count; i++) {
        if (cheat_decode(texts[i], types ? types[i] : GBA_CHEAT_AUTO, l, NULL) != 0) {
            cheat_list_free(l);
            free(l);
            return GBA_ERR_CHEAT;
        }
    }
    patch_rom(g, l);
    g->cheats = l;
    return GBA_OK;
}
```

Replace `core/src/gba.c`:

```c
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "cheats/cheats.h"
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
    cheats_drop(g);
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
    cheats_drop(g); /* their ROM patches and addresses belong to the old ROM */
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
    cheats_apply(g); /* VBlank has just started: before the next frame's code, before the snapshot */
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

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `399 tests, 0 failed` (385 + 14); CTest 120/120 (no cheats: nothing changes); no warnings.

- [x] **Step 5: Commit**

```bash
git add core/src/cheats/apply.c tests/unit/test_cheats_run.c core/src/mem/bus.h core/src/mem/bus.c core/src/gba.c core/src/gba_internal.h docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(core): cheats run at the end of every frame; ROM patches undone when switched off" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Cheats in the test runner

**Files:**
- Modify: `tools/testrunner/main.c`

- [x] **Step 1: The failing check** — a cheat file and a bad one:

```bash
printf 'type: cb\n33000000 0063\n' > "$SP/cb.cht"
printf 'not a code\n' > "$SP/bad.cht"
bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --frames 5 --cheat "$SP/cb.cht"; echo "exit $?"
```

Expected: the usage text and `exit 3` (the runner does not know `--cheat` yet).

- [x] **Step 2: The runner**

Replace `tools/testrunner/main.c`:

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 *              [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]] [--state-at <frame>]
 *              [--rewind-at <frame>:<steps>] [--load-state <file>] [--cheat <file>]...
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
 * --load-state starts from a save state (after the ROM, BIOS and save file). --cheat switches on the cheat in
 * the file (up to 8): its codes, one per line, after an optional first line "type: auto|ar|gs|cb|raw".
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
#define MAX_CHEATS 8

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

/* A cheat file: an optional "type: auto|ar|gs|cb|raw" line, then the codes. */
static bool set_cheats(gba_t *g, const char *const *paths, int count) {
    static const struct {
        const char *name;
        gba_cheat_type_t type;
    } names[] = {{"auto", GBA_CHEAT_AUTO}, {"ar", GBA_CHEAT_ACTION_REPLAY}, {"gs", GBA_CHEAT_GAMESHARK},
                 {"cb", GBA_CHEAT_CODEBREAKER}, {"raw", GBA_CHEAT_RAW}};
    char *texts[MAX_CHEATS];
    const char *codes[MAX_CHEATS];
    gba_cheat_type_t types[MAX_CHEATS];
    bool ok = true;
    int n = 0;
    for (; n < count; n++) {
        size_t size;
        uint8_t *data = read_file(paths[n], &size);
        texts[n] = malloc(size + 1);
        if (!data || !texts[n]) {
            fprintf(stderr, "cannot read the cheat file %s\n", paths[n]);
            free(data);
            n++;
            ok = false;
            break;
        }
        memcpy(texts[n], data, size);
        texts[n][size] = '\0';
        free(data);
        codes[n] = texts[n];
        types[n] = GBA_CHEAT_AUTO;
        if (!strncmp(texts[n], "type:", 5)) {
            const char *t = texts[n] + 5;
            while (*t == ' ') t++;
            for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
                if (!strncmp(t, names[k].name, strlen(names[k].name))) types[n] = names[k].type;
            }
            const char *nl = strchr(texts[n], '\n');
            codes[n] = nl ? nl + 1 : "";
        }
        int bad = 0;
        gba_cheat_type_t used = gba_cheat_check(codes[n], types[n], &bad);
        if (bad) {
            fprintf(stderr, "%s: line %d is not a code this runner can read\n", paths[n], bad);
            n++;
            ok = false;
            break;
        }
        static const char *const type_names[] = {"auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value"};
        printf("CHEAT %s (%s)\n", paths[n], type_names[used]);
    }
    if (ok && gba_cheats_set(g, codes, types, (size_t)count) != GBA_OK) ok = false;
    for (int i = 0; i < n; i++) free(texts[i]);
    return ok;
}

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file>]\n"
                    "                  [--screenshot <file.png>] [--hash] [--expect-hash <hex>]\n"
                    "                  [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <hex>]\n"
                    "                  [--rtc <unix seconds>] [--hardware <rtc,solar,gyro,rumble,tilt|none>]\n"
                    "                  [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]]\n"
                    "                  [--state-at <frame>] [--rewind-at <frame>:<steps>]\n"
                    "                  [--load-state <file>] [--cheat <file>]...\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;
    const char *hardware = NULL;
    const char *load_state = NULL;
    const char *cheat_paths[MAX_CHEATS];
    int cheat_count = 0;
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
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_state = argv[++i];
        else if (!strcmp(argv[i], "--cheat") && i + 1 < argc && cheat_count < MAX_CHEATS) cheat_paths[cheat_count++] = argv[++i];
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
    if (load_state) {
        size_t state_size;
        uint8_t *state = read_file(load_state, &state_size);
        gba_err_t err = state ? gba_state_load(g, state, state_size) : GBA_ERR_STATE;
        free(state);
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load the state %s (another game, another version, or damaged)\n", load_state);
            goto done;
        }
    }
    if (cheat_count && !set_cheats(g, cheat_paths, cheat_count)) goto done;
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

- [x] **Step 3: Run the checks**

```bash
bash tools/dev.sh build 2>&1 | grep -c warning
bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --frames 5 --cheat "$SP/cb.cht"; echo "exit $?"
bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --frames 5 --cheat "$SP/bad.cht"; echo "exit $?"
bash tools/dev.sh test
```

Expected: `0` warnings; `CHEAT …/cb.cht (CodeBreaker)` and `exit 0`; `…/bad.cht: line 1 is not a code this runner can read` and `exit 3`; CTest 120/120.

- [x] **Step 4: Claude's check — published game A codes on the user's game** (the ROM, save file and automatic state copied read-only from the phone emulator into the scratchpad; nothing of the app's changes)

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() { [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }; MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"; }
mkdir -p "$SP/poke"
ADB exec-out run-as com.tengen.gbaemu cat files/saves/e86b3224.sav > "$SP/poke/game.sav"
ADB exec-out run-as com.tengen.gbaemu cat files/states/e86b3224/auto.state > "$SP/poke/auto.state"
ADB pull "/sdcard/Download/hack-1.gba" "$(cygpath -w "$SP/poke/rom.gba")"
printf '%s\n' '7881A409 E2026E0C' '8E883EFF 92E9660D' > "$SP/poke/walls.cht"    # walk through walls (Action Replay v3)
printf '%s\n' 'D8BAE4D9 4864DCE5' 'A86CDBA5 19BA49B3' > "$SP/poke/master.cht"   # its master code
for run in plain walls; do
    cp "$SP/poke/game.sav" "$SP/poke/run.sav"
    extra=(); [ $run = walls ] && extra=(--cheat "$SP/poke/master.cht" --cheat "$SP/poke/walls.cht")
    bash tools/dev.sh run "$SP/poke/rom.gba" --save "$SP/poke/run.sav" --load-state "$SP/poke/auto.state" "${extra[@]}" --keys "0:-,120:LEFT" --frames 300 --screenshot "$SP/poke/left_$run.png" 2>&1 | grep -E "CHEAT|cannot"
done
python -c "from PIL import Image; import sys; print('same' if Image.open(sys.argv[1]).tobytes() == Image.open(sys.argv[2]).tobytes() else 'DIFFERENT')" "$(cygpath -w "$SP/poke/left_plain.png")" "$(cygpath -w "$SP/poke/left_walls.png")"
```

Expected: `CHEAT …/master.cht (Action Replay v3)` and `CHEAT …/walls.cht (Action Replay v3)`, then `DIFFERENT`. Look at `left_plain.png` and `left_walls.png`: 2 s without keys (an automatic state can be caught in a screen fade), then 3 s of Left — without the cheat the player stops at furniture or a wall, with it the player walks through. (Found while replaying: walk into something inside the map — stairs and doors warp in both runs, and the map's edge is blocked by a separate check the code does not touch. If the player's left is free, change `LEFT`.)

- [x] **Step 5: Commit**

```bash
git add tools/testrunner/main.c docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(testrunner): --cheat <file> and --load-state <file>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done** — report briefly and carry on (no stop).

---
### Task 4: Cheats on Android (C and JNI)

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/CheatTest.kt`
- Modify: `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/CheatTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Cheats on the phone: set while paused, they change the game's memory and ROM; off undoes the ROM patch. */
@RunWith(AndroidJUnit4::class)
class CheatTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun aCheatSetBeforeStartWritesEveryFrame() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(0, NativeBridge.setCheats(arrayOf("0203FFF0:12345678"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        assertEquals(0x12345678, NativeBridge.debugPeek32(0x0203FFF0))
    }

    @Test
    fun romPatchesComeAndGoWithTheCheat() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        val before = NativeBridge.debugPeek32(0x08000100)
        assertEquals(0, NativeBridge.setCheats(arrayOf("08000100:BEEF\n08000102:F00D"), intArrayOf(NativeBridge.CHEAT_AUTO)))
        assertEquals(0xF00DBEEF.toInt(), NativeBridge.debugPeek32(0x08000100))
        assertEquals(0, NativeBridge.setCheats(arrayOf(), intArrayOf()))
        assertEquals(before, NativeBridge.debugPeek32(0x08000100))
    }

    @Test
    fun aRunningGameRefusesCheatsAndABadTextSetsNone() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("not a code"), intArrayOf(NativeBridge.CHEAT_AUTO)))
        NativeBridge.start()
        Thread.sleep(200)
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("0203FFF0:11"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.pause()
        assertNotEquals(0x11, NativeBridge.debugPeek32(0x0203FFF0) and 0xFF)
    }

    @Test
    fun checkCheatFindsTheTypeOrTheBadLine() {
        assertArrayEquals(intArrayOf(NativeBridge.CHEAT_CODEBREAKER, 0),
            NativeBridge.checkCheat("820257BC 423F\n820257BE 000F", NativeBridge.CHEAT_AUTO))
        assertArrayEquals(intArrayOf(NativeBridge.CHEAT_ACTION_REPLAY, 0),
            NativeBridge.checkCheat("7881A409 E2026E0C\n8E883EFF 92E9660D", NativeBridge.CHEAT_AUTO))
        assertEquals(3, NativeBridge.checkCheat("820257BC 423F\n\nnope", NativeBridge.CHEAT_AUTO)[1])
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: fails to compile — `Unresolved reference 'setCheats'`, `'CHEAT_RAW'`, `'debugPeek32'`, `'CHEAT_AUTO'`, `'ERR_CHEAT'`, `'checkCheat'`, `'CHEAT_CODEBREAKER'`, `'CHEAT_ACTION_REPLAY'`.

- [x] **Step 3: The emulation thread and JNI**

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
/* The switched-on cheats (gba_cheats_set) — only while paused or not started; GBA_ERR_CHEAT when running or when
 * a text does not read (then no cheats are on). */
gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const gba_cheat_type_t *types, size_t count);
/* The current 240×160 BGR555 picture (for a state's thumbnail); same rule: only while paused. */
bool emu_frame(emu_t *e, uint16_t out[240 * 160]);

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
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: `399 tests, 0 failed`.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```
Expected: `tests="45" failures="0"` three times (41 + 4); `0` C warnings.

- [x] **Step 5: Commit**

```bash
git add platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/CheatTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(android): set cheats while paused, check a cheat's text; on-device cheat tests" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: A game's cheats file (Kotlin)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Cheats.kt`, `app/src/test/java/com/tengen/gbaemu/CheatsTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/CheatsTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CheatsTest {
    private val sample = listOf(
        Cheat("Walk through walls", NativeBridge.CHEAT_AUTO, "7881A409 E2026E0C\n8E883EFF 92E9660D", true),
        Cheat("Money \"999,999\" \\ \t ünï", NativeBridge.CHEAT_CODEBREAKER, "820257BC 423F\r\n820257BE 000F\n", false),
        Cheat("", NativeBridge.CHEAT_RAW, "", false),
    )

    @Test
    fun theListSurvivesJsonExactly() {
        val json = Cheats.toJson(sample)
        assertEquals(sample, Cheats.fromJson(json))
        assertEquals(emptyList<Cheat>(), Cheats.fromJson(Cheats.toJson(emptyList())))
    }

    @Test
    fun aGamesCheatsAreSavedAndReadBack() {
        val dir = Files.createTempDirectory("cheats").toFile()
        val c = Cheats(dir, "e86b3224")
        assertEquals(emptyList<Cheat>(), c.load()) // no file yet
        c.save(sample)
        assertTrue(File(dir, "cheats/e86b3224.json").isFile)
        assertEquals(sample, Cheats(dir, "e86b3224").load())
        assertEquals(emptyList<Cheat>(), Cheats(dir, "00000000").load()) // another game
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndReadsAsNothing() {
        val dir = Files.createTempDirectory("cheats").toFile()
        val file = File(dir, "cheats/e86b3224.json").apply { parentFile!!.mkdirs(); writeText("[{\"name\": \"x\", ") }
        assertNull(Cheats(dir, "e86b3224").load())
        assertTrue(File(dir, "cheats/e86b3224.json.bad").isFile)
        assertTrue(!file.exists())
        dir.deleteRecursively()
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'Cheat'`, `'Cheats'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/Cheats.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/** One cheat: its name, the type picked (NativeBridge.CHEAT_*), its codes as typed, and whether it is switched on. */
data class Cheat(val name: String, val type: Int, val codes: String, val on: Boolean)

/**
 * A game's cheats in `files/cheats/<game id>.json`: a list of {name, type, codes, on}. Written safely (a temporary
 * file, then swapped). JSON by hand, so it is tested on the PC without Android's org.json.
 */
class Cheats(filesDir: File, gameId: String) {
    private val file = File(filesDir, "cheats/$gameId.json")

    /** The cheats; empty without a file; null if the file is damaged (it is then kept as `.json.bad`). */
    fun load(): List<Cheat>? {
        if (!file.isFile) return emptyList()
        return try {
            fromJson(file.readText())
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            null
        }
    }

    fun save(cheats: List<Cheat>): Boolean = try {
        file.parentFile?.mkdirs()
        val tmp = File(file.path + ".tmp")
        tmp.writeText(toJson(cheats))
        if (!tmp.renameTo(file)) {
            file.delete()
            tmp.renameTo(file)
        } else true
    } catch (e: Exception) {
        false
    }

    companion object {
        fun toJson(cheats: List<Cheat>): String = cheats.joinToString(",\n", "[\n", "\n]\n") {
            "  {\"name\": ${str(it.name)}, \"type\": ${it.type}, \"codes\": ${str(it.codes)}, \"on\": ${it.on}}"
        }

        private fun str(s: String): String = buildString {
            append('"')
            for (c in s) when {
                c == '"' -> append("\\\"")
                c == '\\' -> append("\\\\")
                c == '\n' -> append("\\n")
                c == '\r' -> append("\\r")
                c == '\t' -> append("\\t")
                c < ' ' -> append("\\u%04x".format(c.code))
                else -> append(c)
            }
            append('"')
        }

        /** Reads what [toJson] writes (and any JSON of that shape); throws on anything else. */
        fun fromJson(json: String): List<Cheat> = Reader(json).run {
            val list = mutableListOf<Cheat>()
            expect('[')
            if (peek() != ']') {
                do {
                    var name = ""
                    var type = 0
                    var codes = ""
                    var on = false
                    expect('{')
                    do {
                        when (val key = string()) {
                            "name" -> { expect(':'); name = string() }
                            "type" -> { expect(':'); type = number() }
                            "codes" -> { expect(':'); codes = string() }
                            "on" -> { expect(':'); on = bool() }
                            else -> error("unknown key $key")
                        }
                    } while (take(',', '}') == ',')
                    list += Cheat(name, type, codes, on)
                } while (take(',', ']') == ',')
            } else expect(']')
            if (peek() != null) error("text after the list")
            list
        }
    }

    private class Reader(private val s: String) {
        private var i = 0

        fun peek(): Char? {
            while (i < s.length && s[i].isWhitespace()) i++
            return if (i < s.length) s[i] else null
        }

        fun expect(c: Char) {
            if (peek() != c) error("expected $c at $i")
            i++
        }

        fun take(a: Char, b: Char): Char {
            val c = peek()
            if (c != a && c != b) error("expected $a or $b at $i")
            i++
            return c!!
        }

        fun string(): String {
            expect('"')
            val out = StringBuilder()
            while (true) {
                if (i >= s.length) error("unfinished string")
                val c = s[i++]
                when (c) {
                    '"' -> return out.toString()
                    '\\' -> {
                        if (i >= s.length) error("unfinished escape")
                        when (val e = s[i++]) {
                            '"', '\\', '/' -> out.append(e)
                            'n' -> out.append('\n')
                            'r' -> out.append('\r')
                            't' -> out.append('\t')
                            'b' -> out.append('\b')
                            'f' -> out.append('\u000C')
                            'u' -> {
                                if (i + 4 > s.length) error("short \\u")
                                out.append(s.substring(i, i + 4).toInt(16).toChar())
                                i += 4
                            }
                            else -> error("bad escape \\$e")
                        }
                    }
                    else -> out.append(c)
                }
            }
        }

        fun number(): Int {
            peek()
            val start = i
            if (i < s.length && s[i] == '-') i++
            while (i < s.length && s[i].isDigit()) i++
            return s.substring(start, i).toIntOrNull() ?: error("expected a number at $start")
        }

        fun bool(): Boolean = when {
            peek() == 't' && s.startsWith("true", i) -> { i += 4; true }
            peek() == 'f' && s.startsWith("false", i) -> { i += 5; false }
            else -> error("expected true or false at $i")
        }
    }
}
```

(Our own small JSON reader and writer: Android's org.json is not available to the PC-side tests, and a library would be a download.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: `CheatsTest` 3 plus the earlier 35 — **38 tests**, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Cheats.kt app/src/test/java/com/tengen/gbaemu/CheatsTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(android): a game's cheats in files/cheats/<id>.json" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 4-5) done** — report briefly and carry on (no stop).

---
### Task 6: The cheat panel, the add/edit pop-up, the wiring

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

(No new automated test: the parts are covered by Tasks 1-5; the whole is checked by Claude in Step 3 and by the user in Task 7.)

- [x] **Step 1: Write the code**

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
             onLoad: () -> Unit, onCheats: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad, "Cheats…" to onCheats,
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
    private var cheatStore: Cheats? = null // the running game's cheats file
    private var cheats by mutableStateOf(listOf<Cheat>())
    private var editCheat by mutableStateOf<Int?>(null) // -1 = a new one
    private var deleteCheat by mutableStateOf<Int?>(null)
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
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
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
        cheatStore = Cheats(filesDir, gameId).also { store ->
            cheats = store.load() ?: emptyList<Cheat>().also {
                toast("This game's cheats file couldn't be read; it was kept aside and the game starts without cheats.")
            }
        }
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

Expected: Kotlin 38/38, `399 tests, 0 failed`, CTest 120/120, on-device `tests="45" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)** — with the Mode 7 demo and the cheat `05000000:001F` (palette colour 0 → red: the demo's black sky turns red; pixel (1000, 30) of an upright screenshot is that sky). (Opening the demo makes the app's Continue point at it; the user opens their game with Open ROM afterwards.)

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
```

Then (as a script, `bash <file>`: it stops if the demo does not open):

```bash
M7=$(python -c "import zlib; print('%08x' % zlib.crc32(open('tests/roms/tonc/m7_demo.gba','rb').read()))")
bash tools/dev.sh push tests/roms/tonc/m7_demo.gba
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
ADB shell run-as com.tengen.gbaemu rm -rf files/states/$M7 files/cheats/$M7.json   # the demo's own test files only
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3
tap_on text "Open ROM" && sleep 2 && pick_download m7_demo.gba && sleep 3 && shot fresh ||
    { echo "STOP: m7_demo did not open - nothing below may run on another screen"; exit 1; }
colour_at fresh 1000 30                                                   # the sky: black
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; texts | head -9
tap_on text "Cheats…"; sleep 1.5; texts | head -4
tap_on text "+ Add cheat"; sleep 1.5
tap_on text "Name"; sleep 0.5; ADB shell input text 'Red%sbackground'
tap_on text "Codes (one per line)"; sleep 0.5; ADB shell input text 'hello'
tap_on text "Save"; sleep 1; texts | grep -E "Line|Found"                  # refused
for i in 1 2 3 4 5; do ADB shell input keyevent KEYCODE_DEL; done
ADB shell input text '05000000:001F'; sleep 1; texts | grep -E "Line|Found"
tap_on text "Save"; sleep 1.5; texts | head -4
ADB shell run-as com.tengen.gbaemu cat files/cheats/$M7.json
tap_on text "Back"; sleep 1; tap_on text "▶  Resume"; sleep 1.5; shot red; colour_at red 1000 30
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Restart game"; sleep 1; tap_on text "Restart"; sleep 3
shot restarted; colour_at restarted 1000 30                              # a fresh start from the ROM: still red
ADB logcat -d -s AndroidRuntime:E | tail -3
ADB shell run-as com.tengen.gbaemu rm -f files/cheats/$M7.json          # the demo's test cheat
```

Expected:
- `fresh`: `(0, 0, 0)`. The menu shows '▶  Resume', 'Fast-forward: …', 'Save state…', 'Load state…', 'Cheats…', 'Restart game', 'Quit'; the panel 'Cheats — m7_demo', 'No cheats yet.', '+ Add cheat', 'Back'.
- 'hello' → "Line 1 isn't a code this app can read." and the pop-up stays; `05000000:001F` → 'Found: address:value'; after Save the panel lists 'Red background' and the file holds `{"name": "Red background", "type": 0, "codes": "05000000:001F", "on": true}`.
- `red` and `restarted`: `(255, 0, 0)` — the cheat runs, and after Restart game (a fresh start from the ROM) it was sent again before the first frame. No `AndroidRuntime` crash.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md
git commit -m "feat(android): the cheat panel in the in-game menu, adding/editing/deleting cheats, sent at game start" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 7: User check and milestone 5c exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 120/120 (sanitizers on, no reports), 399 unit tests, Kotlin 38, on-device 45 (emulator only).

- [x] **Step 2: The user's check** (the app installed on the emulator; their game A hack opened with Open ROM or Continue)

game A: Back → Cheats… → + Add cheat: real codes from a site — walk-through-walls (Action Replay, 'Found: Action Replay v3') works; a CodeBreaker code works; a switch off stops a cheat; quit and reopen → the switched-on cheats are on again.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (5c ✅ with the numbers and the user's check — **milestone 5 complete**; Next = milestone 6 brainstorming — the Kotlin app shell: library, settings, skins, controllers); spec status → `Implemented — milestone 5c complete <date> (tag milestone-5c)`; parent spec §10 row 5 → 5c ✅, milestone 5 ✅; `CLAUDE.md` key files → milestone 6 needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-5c replay-5c`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 5c complete - cheats (milestone 5 complete)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-5c
```
