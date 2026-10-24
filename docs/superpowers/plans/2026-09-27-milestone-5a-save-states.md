# Milestone 5a — Save states and the in-game menu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Save states: the core saves and loads the whole machine (atomically, same ROM only), a loaded state continues exactly (all 32 fingerprints through a state, PC and phone), and the app gets an in-game menu on Back with 10 picture slots, an automatic state that resumes the exact moment, and Restart game.

**Architecture:** `core/src/state.c` writes one chunk per component (the component's struct as bytes, tag + version + length) after a header with the ROM's CRC-32; loading checks every chunk before copying anything. `testrunner --state-at k` switches to a brand-new machine through a state half-way, and CMake runs that for every approved fingerprint. On Android the emulation thread allows saving/loading only while parked (paused); Kotlin keeps a game's states in `States` and drives the menu, the slot grid and the automatic state.

**Tech Stack:** C11 (core, PC tests, Android NDK), CMake/CTest, Kotlin + Jetpack Compose, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-27-milestone-5a-save-states-design.md`

---

## Conventions (same as 4a–4d)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its `files/saves` / `files/states` (except the test states named in a step) without the user's OK. Ask before restarting/cold-booting the emulator.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file or CMake tests. One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-5` (already created; the spec commits are on it). Checkpoints with the user after Tasks 2 and 4; Task 6 is the user's check and the 5a exit.
- The app has no home-screen icon on the emulator (adb installs): open it with `bash tools/dev.sh launch` or from the app list.
- This plan's code is the prototype's (worktree `<scratchpad>/p5a`, branch `proto-5a`), which the user tried with their game A hack on 2026-09-27.

## File map

```
core/include/gba.h, core/src/cart/cart.{h,c}, core/src/gba.c, core/src/state.c; tests/unit/test_state.c   Task 1
tools/testrunner/main.c, CMakeLists.txt (state_<name> tests)                                            Task 2
platform/android/save_file.{h,c}, emu_thread.{h,c}, jni_bridge.c; tests/unit/test_saves.c;
  app/.../NativeBridge.kt; app/src/androidTest/.../StateTest.kt                                        Task 3
app/.../States.kt; app/src/test/.../StatesTest.kt                                                       Task 4
app/.../Screens.kt, app/.../MainActivity.kt                                                             Task 5
```

---
### Task 1: Save states in the core

**Files:**
- Create: `core/src/state.c`, `tests/unit/test_state.c`
- Modify: `core/include/gba.h`, `core/src/cart/cart.h`, `core/src/cart/cart.c`, `core/src/gba.c`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_state.c`:

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
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: the build fails — `call to undeclared function 'gba_state_size'` (and `gba_state_save`, `gba_state_load`, `gba_rom_crc32`; `use of undeclared identifier 'GBA_ERR_STATE'`).

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
gba_err_t gba_state_load(gba_t *g, const void *buf, size_t len);

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

(`GBA_ERR_STATE` is appended, so the existing error numbers — used by the Kotlin side — stay the same.)

Replace `core/src/cart/cart.h`:

```c
#ifndef GBA_CART_H
#define GBA_CART_H

#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "cart/save.h"
#include "cart/hardware.h"

#define CART_MAX_ROM (32u * 1024 * 1024)
#define CART_HEADER_SIZE 0xC0u

typedef struct {
    uint8_t *rom;      /* rom_size bytes, zero-padded to a multiple of 4 */
    uint32_t rom_size;
    uint32_t crc32;    /* of the rom_size bytes as given (not the padding) */
    char title[13];    /* NUL-terminated header title (offset 0xA0) */
    char code[5];      /* NUL-terminated game code (offset 0xAC) */
    save_t save;
    hardware_t hw;
} cart_t;

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size);
void cart_unload(cart_t *c);
/* addr: 0x08000000-0x0DFFFFFF aligned to size; size is 1, 2 or 4. */
uint32_t cart_rom_read(const cart_t *c, uint32_t addr, unsigned size);

#endif
```

Replace `core/src/cart/cart.c`:

```c
#include "cart/cart.h"

#include <stdlib.h>
#include <string.h>

/* CRC-32 with the zlib polynomial (reflected 0xEDB88320), table-driven: a 32 MiB ROM takes a moment. */
static uint32_t crc32_of(const uint8_t *data, size_t size) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size) {
    if (size < CART_HEADER_SIZE || size > CART_MAX_ROM) return GBA_ERR_ROM_SIZE;
    size_t padded = (size + 3) & ~(size_t)3;
    uint8_t *rom = calloc(1, padded);
    if (!rom) return GBA_ERR_NOMEM;
    memcpy(rom, data, size);
    cart_unload(c);
    c->rom = rom;
    c->rom_size = (uint32_t)size;
    c->crc32 = crc32_of(data, size);
    memcpy(c->title, rom + 0xA0, 12);
    c->title[12] = '\0';
    memcpy(c->code, rom + 0xAC, 4);
    c->code[4] = '\0';
    save_setup(&c->save, rom, size);
    return GBA_OK;
}

void cart_unload(cart_t *c) {
    free(c->rom);
    memset(c, 0, sizeof(*c));
}

/* Past the end of the ROM, the cartridge bus returns the halfword address as data. */
static uint32_t oob_half(uint32_t offset) {
    return (offset >> 1) & 0xFFFF;
}

uint32_t cart_rom_read(const cart_t *c, uint32_t addr, unsigned size) {
    uint32_t off = addr & 0x01FFFFFFu;
    if (off < c->rom_size) {
        const uint8_t *p = c->rom + off;
        switch (size) {
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
        default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
    }
    switch (size) {
    case 1: return (oob_half(off & ~1u) >> ((off & 1) * 8)) & 0xFF;
    case 2: return oob_half(off);
    default: return oob_half(off) | oob_half(off + 2) << 16;
    }
}
```

Replace `core/src/gba.c`:

```c
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "gba_internal.h"
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

void gba_run_frame(gba_t *g) {
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

`core/src/state.c`:

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

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `368 tests, 0 failed` (363 + 5); CTest 56/56; no warnings.

- [x] **Step 5: Commit**

```bash
git add core/include/gba.h core/src/cart/cart.h core/src/cart/cart.c core/src/gba.c core/src/state.c tests/unit/test_state.c docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md
git commit -m "feat(core): save states - one chunk per component, same ROM only, validated before anything changes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Exact replays through a state (PC)

**Files:**
- Modify: `CMakeLists.txt`, `tools/testrunner/main.c`

- [x] **Step 1: The failing tests — a `state_<name>` twin for every approved fingerprint**

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

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh test -R "^state_"
```

Expected: `0% tests passed, 32 tests failed out of 32` (the runner does not know `--state-at` yet: usage, exit 3).

- [x] **Step 3: The runner**

Replace `tools/testrunner/main.c`:

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 *              [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]] [--state-at <frame>]
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
                    "                  [--state-at <frame>]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;
    const char *hardware = NULL;
    const char *keys_arg = NULL, *suite_arg = NULL;
    long suite_min = -1;
    long state_at = -1;
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
        else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    uint32_t hw_flags = 0;
    if (!rom_path || result_reg > 15 || frames <= 0) return usage();
    if (state_at >= 0 && (!frames_given || result_reg >= 0 || state_at > frames)) return usage();
    if (hardware && !parse_hardware(hardware, &hw_flags)) return usage();
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
            for (; frame < frames; frame++) {
                if (frame == state_at && !switch_machine(&g, bios, bios_size, rom, rom_size)) {
                    printf("FAIL %s: the state saved at frame %ld did not load\n", rom_path, frame);
                    code = EXIT_FAILED;
                    goto done;
                }
                gba_set_keys(g, keys_at(&keys, frame));
                run_frame(g, &audio);
            }
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

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh test -R "^state_"
bash tools/dev.sh test
```

Expected: `100% tests passed, 0 tests failed out of 32`, then **CTest 88/88**.

- [x] **Step 5: Prove the replays catch a bad load** (temporary sabotage, then restore)

```bash
cp core/src/state.c "$SP/state.c.keep"
python - <<'PYEOF'
p = 'core/src/state.c'
s = open(p, encoding='utf-8').read()
old = "        } else {\n            memcpy((uint8_t *)g + c->offset, q, c->size);"
new = "        } else if (c->offset != offsetof(struct gba, apu)) {\n            memcpy((uint8_t *)g + c->offset, q, c->size);"
assert s.count(old) == 1
open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
PYEOF
bash tools/dev.sh test -R "^state_" 2>&1 | grep -E "Failed|tests passed"
cp "$SP/state.c.keep" core/src/state.c && bash tools/dev.sh test -R "^state_" 2>&1 | grep "tests passed"
git status --short core/src/state.c
```

Expected: with the sound left out of loading, exactly the 4 sound entries fail — `state_own_snd_dma`, `state_own_snd_mix`, `state_own_snd_psg`, `state_tonc_snd1_demo` (`88% tests passed, 4 tests failed out of 32`); restored: `100% tests passed … out of 32`; `git status` shows no change to `state.c` beyond Task 1's.

- [x] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/testrunner/main.c docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md
git commit -m "test: every approved fingerprint also replayed through a save state into a new machine (CTest 88)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-2) done — checkpoint with the user.**

---
### Task 3: Save states on Android (C and JNI)

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/StateTest.kt`
- Modify: `tests/unit/test_saves.c`, `platform/android/save_file.h`, `platform/android/save_file.c`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

Replace `tests/unit/test_saves.c`:

```c
/* The Android save files' plain-C pieces (platform/android): writing safely, and when to write. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p, 0755)
#define remove_dir(p) rmdir(p)
#endif

#include "save_file.h"
#include "save_timer.h"
#include "test.h"

#define SAV "test_saves_game.sav"

static void clean(void) {
    remove(SAV);
    remove(SAV ".bak");
    remove(SAV ".tmp");
    remove_dir(SAV ".tmp");
}

static int same(const char *path, const uint8_t *want, size_t n) {
    size_t size = 0;
    uint8_t *got = save_file_load(path, &size);
    int ok = got && size == n && memcmp(got, want, n) == 0;
    free(got);
    return ok;
}

/* ---- save_file ---- */

TEST(save_file_write_then_load_gives_the_same_bytes) {
    clean();
    uint8_t data[32768];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7 + 3);
    CHECK(save_file_write(SAV, data, sizeof data));
    CHECK(same(SAV, data, sizeof data));
    clean();
}

TEST(save_file_second_write_keeps_the_first_as_bak) {
    clean();
    const uint8_t first[4] = {1, 2, 3, 4}, second[4] = {9, 8, 7, 6};
    CHECK(save_file_write(SAV, first, 4));
    CHECK(save_file_write(SAV, second, 4));
    CHECK(same(SAV, second, 4));
    CHECK(same(SAV ".bak", first, 4));
    clean();
}

TEST(save_file_failed_write_leaves_the_save_as_it_was) {
    clean();
    const uint8_t old[4] = {1, 2, 3, 4}, fresh[4] = {5, 6, 7, 8};
    CHECK(save_file_write(SAV, old, 4));
    CHECK(make_dir(SAV ".tmp") == 0); /* a folder where the temporary file must go: the write fails */
    CHECK(!save_file_write(SAV, fresh, 4));
    CHECK(same(SAV, old, 4));
    clean();
}

TEST(save_file_replace_swaps_in_the_new_bytes_and_keeps_no_backup) {
    clean();
    const uint8_t first[4] = {1, 2, 3, 4}, second[4] = {9, 8, 7, 6};
    CHECK(save_file_replace(SAV, first, 4));
    CHECK(save_file_replace(SAV, second, 4));
    CHECK(same(SAV, second, 4));
    size_t size = 0;
    CHECK(save_file_load(SAV ".bak", &size) == NULL);
    CHECK(save_file_load(SAV ".tmp", &size) == NULL);
    clean();
}

TEST(save_file_write_into_a_missing_folder_fails) {
    const uint8_t data[4] = {1, 2, 3, 4};
    CHECK(!save_file_write("test_saves_no_such_folder/game.sav", data, 4));
}

TEST(save_file_load_of_a_missing_file_is_null) {
    clean();
    size_t size = 123;
    CHECK(save_file_load(SAV, &size) == NULL);
}

/* ---- save_timer ---- */

TEST(save_timer_writes_sixty_quiet_frames_after_a_save) {
    save_timer_t t;
    save_timer_init(&t);
    CHECK(!save_timer_frame(&t, true));
    for (int i = 1; i < SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(save_timer_frame(&t, false)); /* the 60th quiet frame */
    CHECK(!save_timer_frame(&t, false)); /* once */
}

TEST(save_timer_saving_again_pushes_the_write_back) {
    save_timer_t t;
    save_timer_init(&t);
    save_timer_frame(&t, true);
    for (int i = 0; i < 30; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(!save_timer_frame(&t, true));
    for (int i = 1; i < SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(save_timer_frame(&t, false));
}

TEST(save_timer_flush_writes_now_when_something_is_pending) {
    save_timer_t t;
    save_timer_init(&t);
    save_timer_frame(&t, true);
    CHECK(save_timer_flush(&t));
    for (int i = 0; i < 2 * SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false)); /* nothing left */
}

TEST(save_timer_flush_with_nothing_pending_does_nothing) {
    save_timer_t t;
    save_timer_init(&t);
    CHECK(!save_timer_flush(&t));
    for (int i = 0; i < 2 * SAVE_TIMER_FRAMES; i++) save_timer_frame(&t, false);
    CHECK(!save_timer_flush(&t));
}
```

`app/src/androidTest/java/com/tengen/gbaemu/StateTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Save states on the phone: exact replays through a state, saving/loading a running game, refusals. */
@RunWith(AndroidJUnit4::class)
class StateTest {
    private lateinit var dir: File

    @Before
    fun freshFolder() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_states")
        dir.deleteRecursively()
        dir.mkdirs()
    }

    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun everyApprovedFingerprintSurvivesAStateHalfWay() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 32, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val frames = e.getInt("frames")
            val got = NativeBridge.debugRunFramesViaState(TestRoms.rom(e.getString("rom")), frames, maxOf(1, frames / 2))
            val parts = got?.split(" ") ?: listOf("state-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    @Test
    fun aPausedGameSavesAndLoadsItsMoment() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        NativeBridge.setKeys(KeyMap.UP) // the camera moves, so moments differ
        Thread.sleep(500)
        NativeBridge.pause()
        val state = File(dir, "slot1.state")
        assertTrue("save", NativeBridge.saveState(state.path))
        val saved = NativeBridge.frameBgr555()
        assertNotNull(saved)
        NativeBridge.resume()
        Thread.sleep(500) // play on
        NativeBridge.pause()
        assertFalse("the picture moved on", saved!!.contentEquals(NativeBridge.frameBgr555()!!))
        assertEquals(0, NativeBridge.loadState(state.path))
        assertArrayEquals("back at the saved moment", saved, NativeBridge.frameBgr555())
    }

    @Test
    fun aRunningGameRefusesStates() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(200)
        assertFalse(NativeBridge.saveState(File(dir, "x.state").path))
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(File(dir, "x.state").path))
    }

    @Test
    fun aStateLoadsIntoANewSessionOfTheSameGameButNotAnother() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        val state = File(dir, "auto.state")
        assertTrue(NativeBridge.saveState(state.path))
        val saved = NativeBridge.frameBgr555()
        NativeBridge.stop()

        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba"))) // not started: allowed
        assertEquals(0, NativeBridge.loadState(state.path))
        assertArrayEquals(saved, NativeBridge.frameBgr555())
        NativeBridge.stop()

        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/obj_demo.gba")))
        assertEquals("another game's state", NativeBridge.ERR_STATE, NativeBridge.loadState(state.path))
        assertEquals("a missing file", NativeBridge.ERR_STATE, NativeBridge.loadState(File(dir, "none.state").path))
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit
bash tools/dev.sh androidtest
```

Expected: the PC build fails — `call to undeclared function 'save_file_replace'`; `androidtest` fails to compile — `Unresolved reference 'debugRunFramesViaState'`, `'saveState'`, `'frameBgr555'`, `'loadState'`, `'ERR_STATE'`.

- [x] **Step 3: A state file without a backup copy**

Replace `platform/android/save_file.h`:

```c
#ifndef GBAEMU_SAVE_FILE_H
#define GBAEMU_SAVE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A game's save file (plain C: the PC tests build it too). */

/* The whole file: malloc'd bytes and *size, or NULL if it does not exist or cannot be read. */
uint8_t *save_file_load(const char *path, size_t *size);

/* Writes safely: a copy of the current file goes to <path>.bak, the new bytes go to <path>.tmp
 * (flushed to the disk), then <path>.tmp replaces <path> in one step — so <path> is always either
 * the old or the new save, never half of one. On any failure <path> is left as it was: false. */
bool save_file_write(const char *path, const uint8_t *data, size_t size);

/* The same without the .bak copy (save states: the slots themselves are the history). */
bool save_file_replace(const char *path, const uint8_t *data, size_t size);

#endif
```

Replace `platform/android/save_file.c`:

```c
#include "save_file.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PATH_LEN 1024

uint8_t *save_file_load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc(n ? (size_t)n : 1)) != NULL &&
        fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t)n;
    return data;
}

/* The whole buffer into a new file, flushed to the disk (not just to the system's cache). */
static bool write_synced(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size && fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    return fclose(f) == 0 && ok;
}

/* Puts `from` in place of `to` in one step (replacing it). */
static bool replace_file(const char *from, const char *to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

bool save_file_write(const char *path, const uint8_t *data, size_t size) {
    char tmp[PATH_LEN], bak[PATH_LEN];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    if (snprintf(bak, sizeof bak, "%s.bak", path) >= (int)sizeof bak) return false;
    size_t old_size = 0;
    uint8_t *old = save_file_load(path, &old_size);
    if (old) { /* keep the previous save */
        bool kept = write_synced(tmp, old, old_size) && replace_file(tmp, bak);
        free(old);
        if (!kept) {
            remove(tmp);
            return false;
        }
    }
    if (!write_synced(tmp, data, size) || !replace_file(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}

bool save_file_replace(const char *path, const uint8_t *data, size_t size) {
    char tmp[PATH_LEN];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    if (!write_synced(tmp, data, size) || !replace_file(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}
```

- [x] **Step 4: The emulation thread, JNI and Kotlin**

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

/* Every sample of the frame: into the resampler, then into the ring (or dropped without sound). */
static void feed_sound(emu_t *e) {
    int16_t in[1024 * 2];
    size_t n;
    while ((n = gba_audio_read(e->g, in, 1024)) > 0) {
        if (!e->out) continue;
        double fill = (double)audio_ring_queued(&e->ring) / (2.0 * (double)target_frames(e));
        double ratio = rate_control_ratio(audio_out_rate(e->out), fill);
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

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
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

        unsigned keys = key_latch_take(&e->keys);
        atomic_fetch_or(&e->keys_seen, keys);
        gba_set_keys(e->g, (uint16_t)keys);
        apply_sensors(e);
        gba_run_frame(e->g);
        atomic_store(&e->rumble, gba_rumble_active(e->g) != 0);
        memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
        tb_publish(&e->tb);
        feed_sound(e);
        if (save_timer_frame(&e->save_timer, gba_saveram_dirty(e->g))) write_save(e);
        atomic_fetch_add(&e->frames, 1);

        if (e->out && atomic_exchange(&e->fake_stuck, false)) audio_out_pause(e->out); /* stops draining, unannounced */
        if (e->out && (audio_out_disconnected(e->out) || atomic_exchange(&e->fake_disconnect, false))) {
            LOGW("sound device changed: reopening the stream");
            sound_reopen(e);
        }
        if (e->out) {
            sound_pace(e);
            pacer_reset(&pacer); /* if sound stops, the clock takes over from now */
        } else {
            int64_t due = pacer_next(&pacer, now_ns());
            struct timespec ts = {(time_t)(due / 1000000000), (long)(due % 1000000000)};
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
            }
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

/* Headless run on the calling thread, exactly like `testrunner --frames n [--state-at k]` (state_at < 0:
 * none): "<frame hash> <audio hash>", or NULL when the ROM or the state did not load. */
static jstring run_headless(JNIEnv *env, jobject rom, jint frames, jint state_at) {
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    for (jint f = 0; f < frames; f++) {
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
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    return run_headless(env, rom, frames, -1);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaState)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                         jint state_at) {
    (void)self;
    return run_headless(env, rom, frames, state_at);
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

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 5: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: `369 tests, 0 failed`.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="37" failures="0"` three times (33 + 4); `0` C warnings.

- [x] **Step 6: Commit**

```bash
git add tests/unit/test_saves.c platform/android/save_file.h platform/android/save_file.c platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/StateTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md
git commit -m "feat(android): save and load states while paused; the 32 fingerprints replayed through a state on the phone" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: A game's states and their pictures (Kotlin)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/States.kt`, `app/src/test/java/com/tengen/gbaemu/StatesTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/StatesTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class StatesTest {
    @Test
    fun filesPerGameAndSlot() {
        val s = States(File("files"), "1a2b3c4d")
        assertEquals(File("files/states/1a2b3c4d/slot3.state"), s.state(3))
        assertEquals(File("files/states/1a2b3c4d/slot10.png"), s.picture(10))
        assertEquals(File("files/states/1a2b3c4d/auto.state"), s.state(States.AUTO))
        assertEquals(File("files/states/1a2b3c4d/auto.png"), s.picture(States.AUTO))
    }

    @Test
    fun picturesUseTheSameColoursAsTheScreen() {
        val px = shortArrayOf(0x0000, 0x7FFF, 0x001F, 0x03E0, 0x7C00, 0x0010)
        assertArrayEquals(
            intArrayOf(
                0xFF000000.toInt(), 0xFFFFFFFF.toInt(), // black, white
                0xFFFF0000.toInt(), 0xFF00FF00.toInt(), 0xFF0000FF.toInt(), // red, green, blue
                0xFF840000.toInt(), // red 16 of 31: (16 << 3) | (16 >> 2) = 0x84
            ),
            States.argbOf(px),
        )
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'States'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/States.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File

/**
 * One game's save states in `<files>/states/<game id>/`: slots 1-10 and the automatic one ([AUTO]), each a
 * `.state` file with a `.png` picture of that moment beside it. Saving and loading need the game paused.
 */
class States(filesDir: File, gameId: String) {
    val dir = File(filesDir, "states/$gameId")

    fun state(slot: Int): File = File(dir, name(slot) + ".state")
    fun picture(slot: Int): File = File(dir, name(slot) + ".png")
    fun used(slot: Int): Boolean = state(slot).exists()
    fun savedAt(slot: Int): Long = state(slot).lastModified()

    /** The paused game into [slot], with its picture. False: nothing saved (an older state there stays). */
    fun save(slot: Int): Boolean {
        dir.mkdirs()
        val frame = NativeBridge.frameBgr555() ?: return false
        if (!NativeBridge.saveState(state(slot).path)) return false
        try {
            picture(slot).outputStream().use { bitmapOf(frame).compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Exception) { // the state is what matters; a missing picture shows as a plain tile
        }
        return true
    }

    /** False: refused (another game, another version, damaged) — the game is unchanged. */
    fun load(slot: Int): Boolean = NativeBridge.loadState(state(slot).path) == 0

    fun loadPicture(slot: Int): Bitmap? = picture(slot).takeIf { it.exists() }?.let { BitmapFactory.decodeFile(it.path) }

    companion object {
        const val AUTO = 0
        const val SLOTS = 10

        fun name(slot: Int): String = if (slot == AUTO) "auto" else "slot$slot"

        /** A BGR555 picture as ARGB pixels (5 bits → 8 by repeating the top bits, like the OpenGL shader). */
        fun argbOf(bgr555: ShortArray): IntArray = IntArray(bgr555.size) { i ->
            val c = bgr555[i].toInt()
            fun eight(v: Int) = (v shl 3) or (v shr 2)
            (0xFF shl 24) or (eight(c and 31) shl 16) or (eight((c shr 5) and 31) shl 8) or eight((c shr 10) and 31)
        }

        fun bitmapOf(bgr555: ShortArray): Bitmap =
            Bitmap.createBitmap(argbOf(bgr555), 240, 160, Bitmap.Config.ARGB_8888)
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```

Expected: `StatesTest` 2 plus the earlier 24 — 26 tests, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/States.kt app/src/test/java/com/tengen/gbaemu/StatesTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md
git commit -m "feat(android): a game's save-state slots and their pictures" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 3-4) done — checkpoint with the user.**

---
### Task 5: The in-game menu, the slot grid, the automatic state, Restart game

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`

(No new automated test: the parts are covered by Tasks 1-4; the whole is checked by Claude in Step 3 and by the user in Task 6.)

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
fun GameMenu(title: String, onResume: () -> Unit, onSave: () -> Unit, onLoad: () -> Unit, onRestart: () -> Unit,
             onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Save state…" to onSave, "Load state…" to onLoad,
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
fun GameScreen(controlsHidden: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
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
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) key(session) {
                    GameScreen(
                        controlsHidden = controlsHidden,
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
                    MenuPage.MAIN -> GameMenu(currentTitle, onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
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

    private fun sendKeys() = NativeBridge.setKeys(KeyMap.merge(keyboardMask, touchMask))

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

(`key(session)` gives every game session a fresh `SurfaceView` — found while prototyping: Restart stops and starts within one frame, the old view stayed and the new render thread never got a window, so the picture froze while the restarted game ran.)

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit && bash tools/dev.sh test
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: Kotlin 26/26, `369 tests, 0 failed`, CTest 88/88, on-device `tests="37" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)** — with the Mode 7 demo: without keys its picture stays still, so moments compare pixel for pixel.

Use a scratchpad folder `$SP`. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
shot() { ADB exec-out screencap -p > "$SP/$1.png"; }
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
pick_download() {  # in the open file picker: show Downloads and pick file $1 (the side menu animates: wait for it)
    for try in 1 2 3; do
        if ADB shell uiautomator dump /sdcard/ui.xml >/dev/null && ADB shell cat /sdcard/ui.xml | grep -qF "text=\"$1\""; then
            tap_on text "$1"
            return
        fi
        tap_on content-desc "Show roots" && sleep 3 && tap_on text "Downloads" && sleep 3
    done
    echo "could not pick $1 from Downloads" >&2
    return 1
}
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
slot_text() { texts | grep -oE "text=\"$1 · [^\"]+\"" | head -1 | sed 's/^text="//; s/"$//'; }  # e.g. "1 · 9/27/26 3:04 PM"
hold_up() { ADB shell input swipe 226 1453 226 1453 800; }  # the touch D-pad's Up (Pixel 9, upright)
same_game_picture() {  # the top 1080x720 (the game) of two screenshots: identical?
    python -c "from PIL import Image; import sys; a=Image.open(sys.argv[1]).crop((0,0,1080,720)); b=Image.open(sys.argv[2]).crop((0,0,1080,720)); print('same' if a.tobytes()==b.tobytes() else 'DIFFERENT')" "$SP/$1.png" "$SP/$2.png"
}
```

Then:

```bash
M7=$(python -c "import zlib; print('%08x' % zlib.crc32(open('tests/roms/tonc/m7_demo.gba','rb').read()))")
bash tools/dev.sh push tests/roms/tonc/m7_demo.gba
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
ADB shell run-as com.tengen.gbaemu rm -rf files/states/$M7   # the demo's own test states only
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3
tap_on text "Open ROM" && sleep 2 && pick_download m7_demo.gba && sleep 3 && shot fresh
hold_up; ADB shell input keyevent KEYCODE_BACK; sleep 1.5; shot saved; texts | head -6
tap_on text "Save state…"; sleep 1.5; shot grid; tap_on text "1 · empty"; sleep 2
ADB shell run-as com.tengen.gbaemu ls files/states/$M7
hold_up; ADB shell input keyevent KEYCODE_BACK; sleep 1.5; shot moved
tap_on text "Save state…"; sleep 1.5; tap_on text "$(slot_text 1)"; sleep 1; texts | grep -E "Replace|Cancel"; tap_on text "Cancel"; sleep 1
tap_on text "Back"; sleep 1; tap_on text "Load state…"; sleep 1.5; tap_on text "$(slot_text 1)"; sleep 1.5
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; shot loaded
same_game_picture saved loaded; same_game_picture saved moved
tap_on text "▶  Resume"; sleep 1; ADB shell input keyevent KEYCODE_HOME; sleep 2
ADB shell run-as com.tengen.gbaemu ls files/states/$M7; ADB shell am force-stop com.tengen.gbaemu
bash tools/dev.sh launch; sleep 2; tap_on text "Continue: m7_demo"; sleep 2
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; shot continued; same_game_picture saved continued
tap_on text "Restart game"; sleep 1; texts | grep -E "Restart|lost"; tap_on text "Restart"; sleep 2; shot restarted
same_game_picture fresh restarted
ADB logcat -d -s AndroidRuntime:E | tail -3
```

Expected:
- `saved` shows the menu over the paused game with 'Paused — m7_demo', '▶  Resume', 'Save state…', 'Load state…', 'Restart game', 'Quit'; `grid` shows 10 tiles '1 · empty' … '10 · empty' and 'Back'.
- After saving: `slot1.png slot1.state`. Saving into slot 1 again asks 'Replace slot 1?' (Replace / Cancel).
- `same_game_picture saved loaded` → **same** (loading slot 1 went back to the saved moment); `saved moved` → **DIFFERENT** (the check can tell moments apart).
- After Home: `auto.png auto.state slot1.png slot1.state`; after force-stop and Continue: `same_game_picture saved continued` → **same** (the automatic state).
- Restart asks 'Restart from the title screen?' / '…lost unless it's saved in a slot.'; `same_game_picture fresh restarted` → **same** (the camera back at its start, and the picture moving again — not frozen). No `AndroidRuntime` crash.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md
git commit -m "feat(android): in-game menu on Back, 10 picture slots, automatic state resumed on open, restart game" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 5a exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 88/88 (sanitizers on, no reports), 369 unit tests, Kotlin 26, on-device 37 (emulator only).

- [x] **Step 2: The user's check** (the app installed and open on the emulator; their game A hack is in the Download folder)

game A: Back → menu; Save state… → a slot; play on; Load state… → that moment; saving into a used slot asks to replace; Home, then (from the app list) Continue → the exact moment; also after the app is closed; Restart game → the title, the in-game save kept.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (5a ✅ with the numbers and the user's check; Next = milestone 5b brainstorming — fast-forward + rewind); spec status → `Implemented — milestone 5a complete <date> (tag milestone-5a)`; parent spec §10 row 5 → 5a ✅; `CLAUDE.md` key files → 5b needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-5a replay-5a`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 5a complete - save states and the in-game menu" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-5a
```
