# Milestone 1 — CPU, Memory Bus, HLE BIOS, Scheduler, Test Runner — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A portable C11 GBA core that boots cartridges without a BIOS dump and passes the jsmolka `arm`, `thumb`, `memory` and `bios` test ROMs from a host command-line test runner.

**Architecture:** `core/` is a static library with no OS dependencies. A cycle counter (`g->now`) is advanced by every bus access; a small event scheduler fires LCD line events; the ARM7TDMI interpreter decodes through 4096-entry (ARM) and 1024-entry (Thumb) handler tables. The HLE BIOS is a ~600-byte ARM stub (assembled once, committed as a C include) whose layout matches the real BIOS at every address games can observe; SWIs trap from the stub into C. A host `testrunner` runs ROMs headless and reads the jsmolka result register.

**Tech Stack:** C11, CMake ≥ 3.22 + Ninja (from the Android SDK), llvm-mingw clang on Windows (host), Android NDK clang (only to assemble the BIOS stub), Python 3 (tool scripts), Git Bash.

**Spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§2, §3, §4.4 IRQ/keypad, §8, §9.1–9.3, §10 milestone 1).

---

## Scope and deliberate deviations for this milestone

| Spec item | Milestone 1 handling | Why |
|---|---|---|
| Scheduler "binary min-heap" (§3.1) | Fixed array of per-event timestamps with a cached minimum | < 16 event kinds; a scan is simpler and faster; same interface. |
| Decoded-block cache (§3.2) | **Deferred to milestone 7** (built with the ARM64 dispatch loop). M1 decodes through the handler tables every instruction. | Pure optimization; needs replay/equivalence tests first. |
| Prefetch buffer (§3.3) | **Deferred to milestone 3** (verified by mGBA timing suite). Wait states are modeled. | Not observable by M1 exit tests. |
| ARM64 assembly | **Milestone 7.** M1 is generic C only. | Per spec §10. |
| Timers, DMA, APU, PPU rendering, save chips | Later milestones. SRAM space reads `0xFF`. Unmapped I/O reads return 0. | Per spec §10. |
| HLE `Stop` (SWI 3) | Treated as `Halt`. | Stop needs keypad/cart wake logic (M3/M6). |
| HLE `ArcTan` side effects in r1/r3 | Not reproduced; r0 result only. | No known game depends on them. |

## Conventions (read once)

- **Shell:** all commands are Git Bash, run from the repo root `~\Documents\gba_emulator`.
- **Build/test entry point:** `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>>` (created in Task 1).
- **Unit tests** are C files in `tests/unit/` registered with `TEST(name)`; `bash tools/dev.sh unit <substring>` runs matching tests. `CHECK_EQ` compares as `unsigned long long`, so write expected register values as `uint32_t` (e.g. `0xFFFFFFF2u`, never `-14`).
- **Sources are globbed** (`CONFIGURE_DEPENDS`): adding a `.c` file needs no CMake edit, but re-run `bash tools/dev.sh configure` if Ninja does not pick it up.
- **Commits:** one per task, message ends with the trailer shown in each commit step.
- **Register conventions used everywhere:** between instructions `cpu.r[15]` is the address of the *next* instruction; while an instruction executes, `r[15]` = its address + 8 (ARM) / + 4 (Thumb).

## File map

```
CMakeLists.txt                     root: options, subdirs, CTest ROM tests
tools/dev.sh                       host build helper (PATH setup for cmake/ninja/clang)
tools/gen_bios_stub.py             assembles bios_stub.S → bios_stub.inc (NDK clang)
tools/fetch_test_roms.py           downloads jsmolka ROMs to tests/roms/jsmolka/
tools/testrunner/{CMakeLists.txt,main.c}
core/CMakeLists.txt
core/include/gba.h                 public API
core/include/gba_debug.h           introspection API for tools/tests
core/src/gba_internal.h            struct gba (aggregates all subsystems)
core/src/gba.c                     lifecycle, reset, frame loop, event dispatch
core/src/gba_debug.c
core/src/sys/sched.{h,c}           event scheduler
core/src/sys/irq.{h,c}             IE/IF/IME, halt
core/src/cart/cart.{h,c}           ROM storage, header, ROM bus reads
core/src/mem/bus.{h,c}             memory map, mirrors, wait states, BIOS latch, open bus
core/src/mem/io.{h,c}              I/O register dispatch
core/src/ppu/ppu.{h,c}             LCD timing (DISPSTAT/VCOUNT); rendering comes in M2
core/src/cpu/cpu.{h,c}             state, banking, exceptions, step
core/src/cpu/alu.h                 adder/shifter/multiplier-timing helpers
core/src/cpu/cpu_mem.h             ARM7TDMI load semantics (rotation, sign extension)
core/src/cpu/arm.c                 ARM decode table + handlers
core/src/cpu/thumb.c               Thumb decode table + handlers
core/src/bios/hle.{h,c}            boot state, stub install, SWI dispatch, resets
core/src/bios/hle_internal.h
core/src/bios/hle_math.c           Div, Sqrt, ArcTan(2), affine set-up
core/src/bios/hle_memory.c         CpuSet, CpuFastSet, BitUnPack
core/src/bios/hle_decompress.c     LZ77, Huffman, RL, Diff filters
core/src/bios/bios_stub.S          stub source
core/src/bios/bios_stub.inc        generated, committed
tests/unit/{CMakeLists.txt,test.h,test_main.c,testutil.h,testutil.c,test_*.c}
```

---

### Task 1: Toolchain and build skeleton

**Files:**
- Create: `tools/dev.sh`, `CMakeLists.txt`, `core/CMakeLists.txt`, `core/include/gba.h`, `core/src/gba_internal.h`, `core/src/gba.c`, `tests/unit/CMakeLists.txt`, `tests/unit/test.h`, `tests/unit/test_main.c`, `tests/unit/test_smoke.c`

- [x] **Step 1: Install llvm-mingw (needs the user's approval — it installs software)**

Ask the user to approve, then run:

```bash
winget install --id MartinStorsjo.LLVM-MinGW.UCRT -e
```

Expected: `Successfully installed`. The package lands under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_*\llvm-mingw-*\bin`.

- [x] **Step 2: Create `tools/dev.sh`**

```bash
#!/usr/bin/env bash
# Host build helper (Git Bash on Windows).
#   tools/dev.sh configure        configure build/host (Debug; sanitizers unless GBA_SANITIZE=OFF)
#   tools/dev.sh build            build everything
#   tools/dev.sh unit [filter]    build, then run unit tests whose name contains filter
#   tools/dev.sh test             build, then run every CTest test
#   tools/dev.sh run <args>       build, then run tools/testrunner with args
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAD="$(cygpath -u "${LOCALAPPDATA}")"
SDK="${ANDROID_HOME:+$(cygpath -u "$ANDROID_HOME")}"
SDK="${SDK:-$LAD/Android/Sdk}"
export PATH="$SDK/cmake/3.31.6/bin:$PATH"

if [ -z "${LLVM_MINGW_BIN:-}" ]; then
    for d in "$LAD"/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT*/llvm-mingw-*/bin; do
        [ -x "$d/clang.exe" ] && LLVM_MINGW_BIN="$d"
    done
fi
if [ -n "${LLVM_MINGW_BIN:-}" ]; then
    export PATH="$LLVM_MINGW_BIN:$PATH"
fi
command -v clang >/dev/null || {
    echo "clang not found: install llvm-mingw (winget install MartinStorsjo.LLVM-MinGW.UCRT) or set LLVM_MINGW_BIN" >&2
    exit 1
}

BUILD="$ROOT/build/host"
cmd="${1:-}"
shift || true
case "$cmd" in
    configure)
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE="${GBA_SANITIZE:-ON}" ;;
    build) cmake --build "$BUILD" ;;
    unit)  cmake --build "$BUILD" && "$BUILD/tests/unit/unit_tests" "$@" ;;
    test)  cmake --build "$BUILD" && ctest --test-dir "$BUILD" --output-on-failure "$@" ;;
    run)   cmake --build "$BUILD" && "$BUILD/tools/testrunner/testrunner" "$@" ;;
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>" >&2; exit 2 ;;
esac
```

- [x] **Step 3: Create the root `CMakeLists.txt`**

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
endif()
```

- [x] **Step 4: Create `core/CMakeLists.txt`**

```cmake
file(GLOB_RECURSE GBACORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(gbacore STATIC ${GBACORE_SOURCES})
target_include_directories(gbacore PUBLIC include PRIVATE src)
target_compile_options(gbacore PRIVATE -Wall -Wextra -Wno-unused-parameter)
```

- [x] **Step 5: Create the test harness `tests/unit/test.h`**

```c
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

typedef void (*test_fn)(void);

void test_register(const char *name, test_fn fn);
void test_fail(void);

#define TEST(name)                                                   \
    static void name(void);                                          \
    __attribute__((constructor)) static void name##_register(void) { \
        test_register(#name, name);                                  \
    }                                                                \
    static void name(void)

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "  %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            test_fail();                                                        \
        }                                                                       \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        unsigned long long a_ = (unsigned long long)(actual);                    \
        unsigned long long e_ = (unsigned long long)(expected);                  \
        if (a_ != e_) {                                                          \
            fprintf(stderr, "  %s:%d: CHECK_EQ(%s, %s): 0x%llx != 0x%llx\n",     \
                    __FILE__, __LINE__, #actual, #expected, a_, e_);             \
            test_fail();                                                         \
        }                                                                        \
    } while (0)

#endif
```

- [x] **Step 6: Create `tests/unit/test_main.c`**

```c
#include <stdio.h>
#include <string.h>

#include "test.h"

#define MAX_TESTS 1024

static struct {
    const char *name;
    test_fn fn;
} tests[MAX_TESTS];
static int test_count;
static int current_failed;

void test_register(const char *name, test_fn fn) {
    if (test_count < MAX_TESTS) {
        tests[test_count].name = name;
        tests[test_count].fn = fn;
        test_count++;
    }
}

void test_fail(void) {
    current_failed = 1;
}

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0, failed = 0;
    for (int i = 0; i < test_count; i++) {
        if (filter && !strstr(tests[i].name, filter)) continue;
        current_failed = 0;
        tests[i].fn();
        run++;
        if (current_failed) {
            failed++;
            printf("[ FAIL ] %s\n", tests[i].name);
        } else {
            printf("[ PASS ] %s\n", tests[i].name);
        }
    }
    printf("%d tests, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
```

- [x] **Step 7: Create `tests/unit/CMakeLists.txt`**

```cmake
file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_executable(unit_tests ${UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE gbacore)
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME unit_tests COMMAND unit_tests)
```

- [x] **Step 8: Write the failing smoke test `tests/unit/test_smoke.c`**

```c
#include "gba.h"
#include "test.h"

TEST(gba_create_and_destroy) {
    gba_t *g = gba_create(NULL);
    CHECK(g != NULL);
    gba_destroy(g);
}
```

- [x] **Step 9: Create the public header `core/include/gba.h`**

This is the full milestone-1 API; functions are implemented across Tasks 1–6.

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

const char *gba_rom_title(const gba_t *g);
const char *gba_rom_code(const gba_t *g);

#ifdef __cplusplus
}
#endif

#endif
```

- [x] **Step 10: Create `core/src/gba_internal.h` and an empty `core/src/gba.c`**

CMake refuses a library with no sources, so `gba.c` exists before configuring; it gains its functions in Step 12.

`core/src/gba_internal.h`:

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"

struct gba {
    uint64_t now; /* master clock: 16.78 MHz cycles since reset */
};

#endif
```

`core/src/gba.c`:

```c
#include "gba_internal.h"
```

- [x] **Step 11: Configure and verify the test fails to link**

Run: `bash tools/dev.sh configure && bash tools/dev.sh unit`
Expected: configure succeeds; the build fails with `undefined reference to 'gba_create'` (or the equivalent `undefined symbol` message).

If configure or link fails with a sanitizer-related error (for example `cannot find -lclang_rt.asan`), reconfigure without sanitizers and mention it in your task report:

```bash
rm -rf build/host && GBA_SANITIZE=OFF bash tools/dev.sh configure
```

- [x] **Step 12: Replace `core/src/gba.c`**

```c
#include <stdlib.h>

#include "gba_internal.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    return calloc(1, sizeof(gba_t));
}

void gba_destroy(gba_t *g) {
    free(g);
}
```

- [x] **Step 13: Run the test to verify it passes**

Run: `bash tools/dev.sh unit`
Expected: `[ PASS ] gba_create_and_destroy` and `1 tests, 0 failed`.

- [x] **Step 14: Commit**

```bash
git add tools/dev.sh CMakeLists.txt core tests
git commit -m "build: add CMake skeleton, host dev script and C unit-test harness" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Event scheduler

**Files:**
- Create: `core/src/sys/sched.h`, `core/src/sys/sched.c`
- Test: `tests/unit/test_sched.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_sched.c`**

```c
#include "sys/sched.h"
#include "test.h"

TEST(sched_empty_has_no_events) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    CHECK_EQ(sched_next(&s), SCHED_NEVER);
    CHECK_EQ(sched_pop_due(&s, 1000, &when), -1);
}

TEST(sched_pops_in_time_order) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    sched_add(&s, EV_LINE_END, 100);
    sched_add(&s, EV_HBLANK, 50);
    CHECK_EQ(sched_next(&s), 50);
    CHECK_EQ(sched_pop_due(&s, 40, &when), -1);
    CHECK_EQ(sched_pop_due(&s, 200, &when), EV_HBLANK);
    CHECK_EQ(when, 50);
    CHECK_EQ(sched_pop_due(&s, 200, &when), EV_LINE_END);
    CHECK_EQ(when, 100);
    CHECK_EQ(sched_pop_due(&s, 200, &when), -1);
}

TEST(sched_ties_fire_lower_id_first) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    sched_add(&s, EV_LINE_END, 10);
    sched_add(&s, EV_HBLANK, 10);
    CHECK_EQ(sched_pop_due(&s, 10, &when), EV_HBLANK);
    CHECK_EQ(sched_pop_due(&s, 10, &when), EV_LINE_END);
}

TEST(sched_add_replaces_and_cancel_removes) {
    sched_t s;
    sched_init(&s);
    sched_add(&s, EV_HBLANK, 10);
    sched_add(&s, EV_HBLANK, 30);
    CHECK_EQ(sched_next(&s), 30);
    sched_cancel(&s, EV_HBLANK);
    CHECK_EQ(sched_next(&s), SCHED_NEVER);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit sched`
Expected: build fails: `'sys/sched.h' file not found`.

- [x] **Step 3: Create `core/src/sys/sched.h`**

```c
#ifndef GBA_SCHED_H
#define GBA_SCHED_H

#include <stdint.h>

#define SCHED_NEVER UINT64_MAX

/* Event kinds. At equal timestamps, lower IDs fire first. Later milestones add timers, DMA, APU. */
typedef enum {
    EV_HBLANK = 0, /* HDraw ends on the current line */
    EV_LINE_END,   /* HBlank ends; VCOUNT advances */
    EV_COUNT
} sched_event_t;

typedef struct {
    uint64_t when[EV_COUNT]; /* SCHED_NEVER = not scheduled */
    uint64_t next;           /* cached minimum of when[] */
} sched_t;

void sched_init(sched_t *s);
void sched_add(sched_t *s, sched_event_t id, uint64_t when); /* (re)schedules id */
void sched_cancel(sched_t *s, sched_event_t id);
/* Removes the earliest event due at or before now; returns its ID (timestamp in *when_out), or -1. */
int sched_pop_due(sched_t *s, uint64_t now, uint64_t *when_out);

static inline uint64_t sched_next(const sched_t *s) {
    return s->next;
}

#endif
```

- [x] **Step 4: Create `core/src/sys/sched.c`**

```c
#include "sys/sched.h"

static void recompute(sched_t *s) {
    uint64_t min = SCHED_NEVER;
    for (int i = 0; i < EV_COUNT; i++) {
        if (s->when[i] < min) min = s->when[i];
    }
    s->next = min;
}

void sched_init(sched_t *s) {
    for (int i = 0; i < EV_COUNT; i++) s->when[i] = SCHED_NEVER;
    s->next = SCHED_NEVER;
}

void sched_add(sched_t *s, sched_event_t id, uint64_t when) {
    s->when[id] = when;
    recompute(s);
}

void sched_cancel(sched_t *s, sched_event_t id) {
    s->when[id] = SCHED_NEVER;
    recompute(s);
}

int sched_pop_due(sched_t *s, uint64_t now, uint64_t *when_out) {
    if (s->next > now) return -1;
    int best = -1;
    for (int i = 0; i < EV_COUNT; i++) {
        if (s->when[i] <= now && (best < 0 || s->when[i] < s->when[best])) best = i;
    }
    *when_out = s->when[best];
    s->when[best] = SCHED_NEVER;
    recompute(s);
    return best;
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit sched`
Expected: 4 `[ PASS ]` lines, `4 tests, 0 failed`.

- [x] **Step 6: Commit**

```bash
git add core/src/sys tests/unit/test_sched.c
git commit -m "feat(core): add event scheduler" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Cartridge ROM

**Files:**
- Create: `core/src/cart/cart.h`, `core/src/cart/cart.c`
- Modify: `core/src/gba_internal.h`, `core/src/gba.c`
- Test: `tests/unit/test_cart.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_cart.c`**

```c
#include <string.h>

#include "gba_internal.h"
#include "test.h"

static uint8_t rom[0x200];

static void make_rom(void) {
    memset(rom, 0, sizeof rom);
    memcpy(rom + 0xA0, "GBA TESTS\0\0\0", 12);
    memcpy(rom + 0xAC, "1337", 4);
    rom[0x10] = 0x11;
    rom[0x11] = 0x22;
    rom[0x12] = 0x33;
    rom[0x13] = 0x44;
}

TEST(cart_rejects_bad_sizes) {
    cart_t c = {0};
    make_rom();
    CHECK_EQ(cart_load(&c, rom, 0), GBA_ERR_ROM_SIZE);
    CHECK_EQ(cart_load(&c, rom, 0xBF), GBA_ERR_ROM_SIZE);
    CHECK_EQ(cart_load(&c, rom, CART_MAX_ROM + 1), GBA_ERR_ROM_SIZE);
    CHECK(c.rom == NULL);
}

TEST(cart_parses_header_title_and_code) {
    cart_t c = {0};
    make_rom();
    CHECK_EQ(cart_load(&c, rom, sizeof rom), GBA_OK);
    CHECK(strcmp(c.title, "GBA TESTS") == 0);
    CHECK(strcmp(c.code, "1337") == 0);
    cart_unload(&c);
}

TEST(cart_reads_little_endian_in_every_mirror) {
    cart_t c = {0};
    make_rom();
    cart_load(&c, rom, sizeof rom);
    CHECK_EQ(cart_rom_read(&c, 0x08000010, 4), 0x44332211);
    CHECK_EQ(cart_rom_read(&c, 0x08000012, 2), 0x4433);
    CHECK_EQ(cart_rom_read(&c, 0x08000011, 1), 0x22);
    CHECK_EQ(cart_rom_read(&c, 0x0A000010, 4), 0x44332211);
    CHECK_EQ(cart_rom_read(&c, 0x0C000010, 4), 0x44332211);
    cart_unload(&c);
}

TEST(cart_out_of_bounds_returns_address_pattern) {
    cart_t c = {0};
    make_rom();
    cart_load(&c, rom, sizeof rom);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 2), 0x0100);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 4), 0x01010100);
    CHECK_EQ(cart_rom_read(&c, 0x08000200, 1), 0x00);
    CHECK_EQ(cart_rom_read(&c, 0x08000201, 1), 0x01);
    cart_unload(&c);
}

TEST(gba_load_rom_exposes_header) {
    gba_t *g = gba_create(NULL);
    make_rom();
    CHECK_EQ(gba_load_rom(g, rom, sizeof rom), GBA_OK);
    CHECK(strcmp(gba_rom_title(g), "GBA TESTS") == 0);
    CHECK(strcmp(gba_rom_code(g), "1337") == 0);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit cart`
Expected: build fails: unknown type `cart_t`.

- [x] **Step 3: Create `core/src/cart/cart.h`**

```c
#ifndef GBA_CART_H
#define GBA_CART_H

#include <stddef.h>
#include <stdint.h>

#include "gba.h"

#define CART_MAX_ROM (32u * 1024 * 1024)
#define CART_HEADER_SIZE 0xC0u

typedef struct {
    uint8_t *rom;      /* rom_size bytes, zero-padded to a multiple of 4 */
    uint32_t rom_size;
    char title[13];    /* NUL-terminated header title (offset 0xA0) */
    char code[5];      /* NUL-terminated game code (offset 0xAC) */
} cart_t;

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size);
void cart_unload(cart_t *c);
/* addr: 0x08000000-0x0DFFFFFF aligned to size; size is 1, 2 or 4. */
uint32_t cart_rom_read(const cart_t *c, uint32_t addr, unsigned size);

#endif
```

- [x] **Step 4: Create `core/src/cart/cart.c`**

```c
#include "cart/cart.h"

#include <stdlib.h>
#include <string.h>

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size) {
    if (size < CART_HEADER_SIZE || size > CART_MAX_ROM) return GBA_ERR_ROM_SIZE;
    size_t padded = (size + 3) & ~(size_t)3;
    uint8_t *rom = calloc(1, padded);
    if (!rom) return GBA_ERR_NOMEM;
    memcpy(rom, data, size);
    cart_unload(c);
    c->rom = rom;
    c->rom_size = (uint32_t)size;
    memcpy(c->title, rom + 0xA0, 12);
    c->title[12] = '\0';
    memcpy(c->code, rom + 0xAC, 4);
    c->code[4] = '\0';
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

- [x] **Step 5: Replace `core/src/gba_internal.h`**

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "cart/cart.h"

struct gba {
    cart_t cart;
    uint64_t now; /* master clock: 16.78 MHz cycles since reset */
};

#endif
```

- [x] **Step 6: Replace `core/src/gba.c`**

```c
#include <stdlib.h>

#include "gba_internal.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    return calloc(1, sizeof(gba_t));
}

void gba_destroy(gba_t *g) {
    if (!g) return;
    cart_unload(&g->cart);
    free(g);
}

gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    return cart_load(&g->cart, rom, size);
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
```

- [x] **Step 7: Run tests to verify they pass**

Run: `bash tools/dev.sh unit cart`
Expected: 5 `[ PASS ]` lines (4 `cart_*` + `gba_load_rom_exposes_header`), `0 failed`.

- [x] **Step 8: Commit**

```bash
git add core/src/cart core/src/gba_internal.h core/src/gba.c tests/unit/test_cart.c
git commit -m "feat(core): add cartridge ROM loading and bus reads" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Memory bus, I/O registers and interrupt controller

**Files:**
- Create: `core/src/sys/irq.h`, `core/src/sys/irq.c`, `core/src/ppu/ppu.h`, `core/src/mem/io.h`, `core/src/mem/io.c`, `core/src/mem/bus.h`, `core/src/mem/bus.c`
- Modify: `core/src/gba_internal.h`, `core/src/gba.c`
- Test: `tests/unit/test_bus.c`, `tests/unit/test_io.c`

- [x] **Step 1: Write the failing bus tests `tests/unit/test_bus.c`**

```c
#include <string.h>

#include "gba_internal.h"
#include "test.h"

TEST(bus_ewram_and_iwram_mirror) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x11223344, false);
    CHECK_EQ(bus_read32(g, 0x02040000, false), 0x11223344);
    bus_write32(g, 0x03000010, 0xAABBCCDD, false);
    CHECK_EQ(bus_read32(g, 0x03008010, false), 0xAABBCCDD);
    CHECK_EQ(bus_read32(g, 0x03FF8010, false), 0xAABBCCDD);
    gba_destroy(g);
}

TEST(bus_accesses_are_little_endian_and_force_aligned) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x11223344, false);
    CHECK_EQ(bus_read8(g, 0x02000001, false), 0x33);
    CHECK_EQ(bus_read16(g, 0x02000002, false), 0x1122);
    CHECK_EQ(bus_read16(g, 0x02000003, false), 0x1122);
    CHECK_EQ(bus_read32(g, 0x02000002, false), 0x11223344);
    gba_destroy(g);
}

TEST(bus_palette_byte_write_is_doubled) {
    gba_t *g = gba_create(NULL);
    bus_write8(g, 0x05000021, 0x7F, false);
    CHECK_EQ(bus_read16(g, 0x05000020, false), 0x7F7F);
    CHECK_EQ(bus_read16(g, 0x05000420, false), 0x7F7F);
    gba_destroy(g);
}

TEST(bus_oam_ignores_byte_writes) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x07000010, 0x12345678, false);
    bus_write8(g, 0x07000010, 0xFF, false);
    CHECK_EQ(bus_read32(g, 0x07000010, false), 0x12345678);
    CHECK_EQ(bus_read32(g, 0x07000410, false), 0x12345678);
    gba_destroy(g);
}

TEST(bus_vram_mirrors) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x06010000, 0xCAFE, false);
    CHECK_EQ(bus_read32(g, 0x06018000, false), 0xCAFE);
    bus_write32(g, 0x06000000, 7, false);
    CHECK_EQ(bus_read32(g, 0x06020000, false), 7);
    gba_destroy(g);
}

TEST(bus_vram_byte_writes_depend_on_display_mode) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispcnt = 0; /* tiled: BG VRAM ends at 0x10000 */
    bus_write8(g, 0x0600FFFF, 0x42, false);
    CHECK_EQ(bus_read16(g, 0x0600FFFE, false), 0x4242);
    bus_write8(g, 0x06010010, 0x42, false);
    CHECK_EQ(bus_read16(g, 0x06010010, false), 0);
    g->ppu.dispcnt = 3; /* bitmap: BG VRAM ends at 0x14000 */
    bus_write8(g, 0x06013FF0, 0x11, false);
    CHECK_EQ(bus_read16(g, 0x06013FF0, false), 0x1111);
    bus_write8(g, 0x06014010, 0x11, false);
    CHECK_EQ(bus_read16(g, 0x06014010, false), 0);
    gba_destroy(g);
}

TEST(bus_rom_is_mirrored_in_all_waitstate_regions) {
    static uint8_t rom[0x200];
    memset(rom, 0, sizeof rom);
    rom[0] = 0xEF;
    rom[1] = 0xBE;
    rom[2] = 0xAD;
    rom[3] = 0xDE;
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, sizeof rom);
    CHECK_EQ(bus_read32(g, 0x08000000, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, 0x0A000000, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, 0x0C000000, false), 0xDEADBEEF);
    gba_destroy(g);
}

TEST(bus_bios_is_read_protected_outside_bios) {
    gba_t *g = gba_create(NULL);
    g->bus.bios[0] = 0x78;
    g->bus.bios[1] = 0x56;
    g->bus.bios[2] = 0x34;
    g->bus.bios[3] = 0x12;
    g->bus.bios_latch = 0xE129F000;
    g->bus.in_bios = false;
    CHECK_EQ(bus_read32(g, 0x00000000, false), 0xE129F000);
    CHECK_EQ(bus_read8(g, 0x00000001, false), 0xF0);
    g->bus.in_bios = true;
    CHECK_EQ(bus_read32(g, 0x00000000, false), 0x12345678);
    gba_destroy(g);
}

TEST(bus_unmapped_reads_return_open_bus) {
    gba_t *g = gba_create(NULL);
    g->bus.open_bus = 0xAABBCCDD;
    CHECK_EQ(bus_read32(g, 0x10000000, false), 0xAABBCCDD);
    CHECK_EQ(bus_read16(g, 0x01000002, false), 0xAABB);
    CHECK_EQ(bus_read8(g, 0x00004001, false), 0xCC);
    gba_destroy(g);
}

TEST(bus_sram_space_reads_ff_without_a_save_chip) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    gba_destroy(g);
}

TEST(bus_charges_region_wait_states) {
    gba_t *g = gba_create(NULL);
    uint64_t t = g->now;
    bus_read32(g, 0x02000000, false);
    CHECK_EQ(g->now - t, 6);
    t = g->now;
    bus_read16(g, 0x03000000, false);
    CHECK_EQ(g->now - t, 1);
    t = g->now; /* WAITCNT = 0: WS0 N = 4+1, S = 2+1 */
    bus_read16(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 5);
    t = g->now;
    bus_read16(g, 0x08000002, true);
    CHECK_EQ(g->now - t, 3);
    t = g->now;
    bus_read32(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 8);
    bus_set_waitcnt(g, 0x4317); /* SRAM 8, WS0 N 3 / S 1 */
    t = g->now;
    bus_read16(g, 0x08000000, false);
    CHECK_EQ(g->now - t, 4);
    t = g->now;
    bus_read32(g, 0x08000004, true);
    CHECK_EQ(g->now - t, 4);
    t = g->now;
    bus_read8(g, 0x0E000000, false);
    CHECK_EQ(g->now - t, 9);
    gba_destroy(g);
}
```

- [x] **Step 2: Write the failing I/O tests `tests/unit/test_io.c`**

```c
#include "gba_internal.h"
#include "test.h"

TEST(io_dispstat_status_bits_are_read_only) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = 0x0003;
    bus_write16(g, 0x04000004, 0xFFFF, false);
    CHECK_EQ(g->ppu.dispstat, 0xFF3B);
    gba_destroy(g);
}

TEST(io_if_is_write_one_to_clear) {
    gba_t *g = gba_create(NULL);
    g->irq.if_ = 0x0005;
    bus_write16(g, 0x04000202, 0x0001, false);
    CHECK_EQ(g->irq.if_, 0x0004);
    g->irq.if_ = 0x0101;
    bus_write8(g, 0x04000203, 0x01, false);
    CHECK_EQ(g->irq.if_, 0x0001);
    gba_destroy(g);
}

TEST(io_ie_ime_waitcnt_round_trip) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x04000200, 0xFFFF, false);
    CHECK_EQ(bus_read16(g, 0x04000200, false), 0x3FFF);
    bus_write16(g, 0x04000208, 0xFFFF, false);
    CHECK_EQ(bus_read16(g, 0x04000208, false), 1);
    bus_write16(g, 0x04000204, 0x4317, false);
    CHECK_EQ(bus_read16(g, 0x04000204, false), 0x4317);
    CHECK_EQ(g->bus.waitcnt, 0x4317);
    gba_destroy(g);
}

TEST(io_32bit_write_splits_into_halves) {
    gba_t *g = gba_create(NULL);
    g->irq.if_ = 0x0007;
    bus_write32(g, 0x04000200, 0x00050003, false);
    CHECK_EQ(g->irq.ie, 0x0003);
    CHECK_EQ(g->irq.if_, 0x0002);
    gba_destroy(g);
}

TEST(io_keyinput_reflects_pressed_keys) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read16(g, 0x04000130, false), 0x03FF);
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_START);
    CHECK_EQ(bus_read16(g, 0x04000130, false), 0x03F6);
    gba_destroy(g);
}

TEST(io_haltcnt_write_halts_cpu) {
    gba_t *g = gba_create(NULL);
    bus_write8(g, 0x04000301, 0, false);
    CHECK(g->irq.halted);
    gba_destroy(g);
}

TEST(io_unhandled_registers_are_stored) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x04000008, 0x1234, false);
    CHECK_EQ(bus_read16(g, 0x04000008, false), 0x1234);
    bus_write8(g, 0x04000009, 0xAB, false);
    CHECK_EQ(bus_read16(g, 0x04000008, false), 0xAB34);
    gba_destroy(g);
}
```

- [x] **Step 3: Run to verify failure**

Run: `bash tools/dev.sh unit bus`
Expected: build fails: `no member named 'ppu' in 'struct gba'` / undeclared `bus_write32`.

- [x] **Step 4: Create `core/src/sys/irq.h`**

```c
#ifndef GBA_IRQ_H
#define GBA_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

enum {
    IRQ_VBLANK = 1 << 0,
    IRQ_HBLANK = 1 << 1,
    IRQ_VCOUNT = 1 << 2,
    IRQ_TIMER0 = 1 << 3,
    IRQ_TIMER1 = 1 << 4,
    IRQ_TIMER2 = 1 << 5,
    IRQ_TIMER3 = 1 << 6,
    IRQ_SERIAL = 1 << 7,
    IRQ_DMA0 = 1 << 8,
    IRQ_DMA1 = 1 << 9,
    IRQ_DMA2 = 1 << 10,
    IRQ_DMA3 = 1 << 11,
    IRQ_KEYPAD = 1 << 12,
    IRQ_GAMEPAK = 1 << 13,
};

typedef struct {
    uint16_t ie, if_, ime;
    bool halted;
} irq_t;

void irq_reset(gba_t *g);
void irq_raise(gba_t *g, uint16_t sources);
void irq_halt(gba_t *g);
/* The CPU should take an IRQ (before considering the CPSR I bit). */
bool irq_line(const gba_t *g);
/* A halted CPU wakes when any enabled source is flagged, even with IME off. */
bool irq_wake(const gba_t *g);

#endif
```

- [x] **Step 5: Create `core/src/sys/irq.c`**

```c
#include "sys/irq.h"

#include "gba_internal.h"

void irq_reset(gba_t *g) {
    g->irq.ie = 0;
    g->irq.if_ = 0;
    g->irq.ime = 0;
    g->irq.halted = false;
}

void irq_raise(gba_t *g, uint16_t sources) {
    g->irq.if_ |= sources;
}

void irq_halt(gba_t *g) {
    g->irq.halted = true;
}

bool irq_line(const gba_t *g) {
    return (g->irq.ime & 1) && (g->irq.ie & g->irq.if_);
}

bool irq_wake(const gba_t *g) {
    return (g->irq.ie & g->irq.if_) != 0;
}
```

- [x] **Step 6: Create `core/src/ppu/ppu.h`** (the timing functions are implemented in Task 5)

```c
#ifndef GBA_PPU_H
#define GBA_PPU_H

#include <stdint.h>

typedef struct gba gba_t;

#define PPU_LINE_CYCLES 1232u
#define PPU_HDRAW_CYCLES 960u
#define PPU_LINES 228u
#define PPU_VISIBLE_LINES 160u
#define PPU_FRAME_CYCLES (PPU_LINE_CYCLES * PPU_LINES)

enum {
    DISPSTAT_VBLANK = 1 << 0,
    DISPSTAT_HBLANK = 1 << 1,
    DISPSTAT_VCOUNT = 1 << 2,
    DISPSTAT_VBLANK_IRQ = 1 << 3,
    DISPSTAT_HBLANK_IRQ = 1 << 4,
    DISPSTAT_VCOUNT_IRQ = 1 << 5,
};

typedef struct {
    uint16_t dispcnt, dispstat, vcount;
} ppu_t;

void ppu_reset(gba_t *g); /* schedules line 0's events relative to g->now */
void ppu_on_hblank(gba_t *g, uint64_t when);
void ppu_on_line_end(gba_t *g, uint64_t when);

#endif
```

- [x] **Step 7: Create `core/src/mem/io.h`**

```c
#ifndef GBA_IO_H
#define GBA_IO_H

#include <stdint.h>

typedef struct gba gba_t;

#define IO_SIZE 0x400u

typedef struct {
    uint8_t raw[IO_SIZE]; /* backing store for registers without dedicated handling */
    uint8_t postflg;
} io_t;

void io_reset(gba_t *g);
/* addr in 0x04000000-0x04FFFFFF, aligned to size; size is 1, 2 or 4. */
uint32_t io_read(gba_t *g, uint32_t addr, unsigned size);
void io_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size);

#endif
```

- [x] **Step 8: Create `core/src/mem/io.c`**

```c
#include "mem/io.h"

#include <string.h>

#include "gba_internal.h"

enum {
    REG_DISPCNT = 0x000,
    REG_DISPSTAT = 0x004,
    REG_VCOUNT = 0x006,
    REG_KEYINPUT = 0x130,
    REG_IE = 0x200,
    REG_IF = 0x202,
    REG_WAITCNT = 0x204,
    REG_IME = 0x208,
    REG_POSTFLG = 0x300,
    REG_HALTCNT = 0x301,
};

void io_reset(gba_t *g) {
    memset(&g->io, 0, sizeof g->io);
}

static uint16_t read16(gba_t *g, uint32_t off) {
    switch (off) {
    case REG_DISPCNT: return g->ppu.dispcnt;
    case REG_DISPSTAT: return g->ppu.dispstat;
    case REG_VCOUNT: return g->ppu.vcount;
    case REG_KEYINPUT: return g->keyinput;
    case REG_IE: return g->irq.ie;
    case REG_IF: return g->irq.if_;
    case REG_WAITCNT: return g->bus.waitcnt;
    case REG_IME: return g->irq.ime;
    case REG_POSTFLG: return g->io.postflg; /* HALTCNT (high byte) is write-only */
    default: return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
    }
}

static void write16(gba_t *g, uint32_t off, uint16_t v) {
    switch (off) {
    case REG_DISPCNT: g->ppu.dispcnt = v; break;
    case REG_DISPSTAT: g->ppu.dispstat = (uint16_t)((g->ppu.dispstat & 0x0007) | (v & 0xFF38)); break;
    case REG_VCOUNT:
    case REG_KEYINPUT: break;
    case REG_IE: g->irq.ie = v & 0x3FFF; break;
    case REG_IF: g->irq.if_ &= (uint16_t)~v; break;
    case REG_WAITCNT: bus_set_waitcnt(g, v); break;
    case REG_IME: g->irq.ime = v & 1; break;
    case REG_POSTFLG:
        g->io.postflg = v & 1;
        irq_halt(g); /* a halfword write here also writes HALTCNT */
        break;
    default:
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        break;
    }
}

static void write8(gba_t *g, uint32_t off, uint8_t v) {
    switch (off) {
    case REG_IF:
    case REG_IF + 1:
        g->irq.if_ &= (uint16_t)~(v << ((off & 1) * 8));
        return;
    case REG_POSTFLG: g->io.postflg = v & 1; return;
    case REG_HALTCNT: irq_halt(g); return; /* bit 7 selects Stop; treated as Halt until M3 */
    default: {
        uint32_t base = off & ~1u;
        uint16_t cur = read16(g, base);
        uint16_t merged = (off & 1) ? (uint16_t)((cur & 0x00FF) | v << 8) : (uint16_t)((cur & 0xFF00) | v);
        write16(g, base, merged);
    }
    }
}

uint32_t io_read(gba_t *g, uint32_t addr, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    if (off >= IO_SIZE) return 0; /* hardware returns open bus; refined in M3 */
    switch (size) {
    case 1: return (read16(g, off & ~1u) >> ((off & 1) * 8)) & 0xFF;
    case 2: return read16(g, off);
    default: return read16(g, off) | (uint32_t)read16(g, off + 2) << 16;
    }
}

void io_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    if (off >= IO_SIZE) return;
    switch (size) {
    case 1: write8(g, off, (uint8_t)value); break;
    case 2: write16(g, off, (uint16_t)value); break;
    default:
        write16(g, off, (uint16_t)value);
        write16(g, off + 2, (uint16_t)(value >> 16));
        break;
    }
}
```

- [x] **Step 9: Create `core/src/mem/bus.h`**

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
    uint32_t open_bus;        /* what unmapped reads return */
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
/* Read without cycles or side effects, for debuggers and tests. */
uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size);

#endif
```

- [x] **Step 10: Create `core/src/mem/bus.c`**

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

static uint32_t mem_read(gba_t *g, uint32_t addr, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE) {
            uint32_t word = b->in_bios ? load_n(&b->bios[addr & 0x3FFC], 4) : b->bios_latch;
            return lane(word, addr, size);
        }
        return lane(b->open_bus, addr, size);
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
    case 0xD: return cart_rom_read(&g->cart, addr, size);
    case 0xE:
    case 0xF: return size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFF; /* save chips arrive in M3 */
    default: return lane(b->open_bus, addr, size);
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
    default: break; /* BIOS, ROM, SRAM (M3) and unmapped space ignore writes */
    }
}

static inline uint32_t access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq) {
    g->now += access_cycles(g, addr, 1, seq);
    return mem_read(g, addr, 1);
}

uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq) {
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    return mem_read(g, addr, 2);
}

uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq) {
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    return mem_read(g, addr, 4);
}

void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq) {
    g->now += access_cycles(g, addr, 1, seq);
    mem_write(g, addr, value, 1);
}

void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq) {
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    mem_write(g, addr, value, 2);
}

void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq) {
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    mem_write(g, addr, value, 4);
}

uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq) {
    bus_t *b = &g->bus;
    b->in_bios = addr < BIOS_SIZE;
    uint32_t op = bus_read32(g, addr, seq);
    /* The BIOS latch holds the last BIOS word fetched: two instructions ahead in the pipeline. */
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 8) & 0x3FFC], 4);
    b->open_bus = op;
    return op;
}

uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq) {
    bus_t *b = &g->bus;
    b->in_bios = addr < BIOS_SIZE;
    uint32_t op = bus_read16(g, addr, seq);
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 4) & 0x3FFC], 4);
    b->open_bus = op | op << 16;
    return op;
}

void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
}

uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size) {
    return mem_read(g, addr & ~(uint32_t)(size - 1), size);
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

- [x] **Step 11: Replace `core/src/gba_internal.h`**

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "cart/cart.h"
#include "mem/bus.h"
#include "mem/io.h"
#include "ppu/ppu.h"
#include "sys/irq.h"

struct gba {
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    cart_t cart;
    uint64_t now;      /* master clock: 16.78 MHz cycles since reset */
    uint16_t keyinput; /* KEYINPUT register: bit clear = pressed */
};

#endif
```

- [x] **Step 12: Replace `core/src/gba.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "gba_internal.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    gba_t *g = calloc(1, sizeof(gba_t));
    if (!g) return NULL;
    gba_reset(g);
    return g;
}

void gba_destroy(gba_t *g) {
    if (!g) return;
    cart_unload(&g->cart);
    free(g);
}

gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    gba_err_t err = cart_load(&g->cart, rom, size);
    if (err == GBA_OK) gba_reset(g);
    return err;
}

void gba_reset(gba_t *g) {
    g->now = 0;
    g->keyinput = 0x3FF;
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
    memset(&g->ppu, 0, sizeof g->ppu);
}

void gba_set_keys(gba_t *g, uint16_t pressed) {
    g->keyinput = (uint16_t)(~pressed & 0x3FF);
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
```

- [x] **Step 13: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes, including 11 `bus_*` and 7 `io_*` tests; `0 failed`.

- [x] **Step 14: Commit**

```bash
git add core/src tests/unit/test_bus.c tests/unit/test_io.c
git commit -m "feat(core): add memory bus, I/O registers and interrupt controller" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: LCD timing (DISPSTAT, VCOUNT, line events)

**Files:**
- Create: `core/src/ppu/ppu.c`
- Modify: `core/src/gba_internal.h`, `core/src/gba.c`
- Test: `tests/unit/test_ppu.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu.c`**

```c
#include "gba_internal.h"
#include "test.h"

static void advance_to(gba_t *g, uint64_t t) {
    g->now = t;
    gba_run_events(g);
}

TEST(ppu_hblank_flag_and_irq) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_HBLANK_IRQ;
    advance_to(g, 959);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    advance_to(g, 960);
    CHECK(g->ppu.dispstat & DISPSTAT_HBLANK);
    CHECK(g->irq.if_ & IRQ_HBLANK);
    advance_to(g, 1232);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    CHECK_EQ(g->ppu.vcount, 1);
    gba_destroy(g);
}

TEST(ppu_vblank_spans_lines_160_to_226) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_VBLANK_IRQ;
    advance_to(g, 160 * PPU_LINE_CYCLES - 1);
    CHECK(!g->frame_done);
    advance_to(g, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 160);
    CHECK(g->ppu.dispstat & DISPSTAT_VBLANK);
    CHECK(g->irq.if_ & IRQ_VBLANK);
    CHECK(g->frame_done);
    advance_to(g, 227 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 227);
    CHECK(!(g->ppu.dispstat & DISPSTAT_VBLANK));
    advance_to(g, 228 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 0);
    gba_destroy(g);
}

TEST(ppu_vcount_match_flag_and_irq) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = (uint16_t)((5 << 8) | DISPSTAT_VCOUNT_IRQ);
    advance_to(g, 5 * PPU_LINE_CYCLES);
    CHECK(g->ppu.dispstat & DISPSTAT_VCOUNT);
    CHECK(g->irq.if_ & IRQ_VCOUNT);
    advance_to(g, 6 * PPU_LINE_CYCLES);
    CHECK(!(g->ppu.dispstat & DISPSTAT_VCOUNT));
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit ppu`
Expected: build fails: implicit declaration of `gba_run_events` / no member `frame_done`.

- [x] **Step 3: Create `core/src/ppu/ppu.c`**

```c
#include "ppu/ppu.h"

#include "gba_internal.h"

void ppu_reset(gba_t *g) {
    g->ppu.dispcnt = 0;
    g->ppu.dispstat = 0;
    g->ppu.vcount = 0;
    sched_add(&g->sched, EV_HBLANK, g->now + PPU_HDRAW_CYCLES);
    sched_add(&g->sched, EV_LINE_END, g->now + PPU_LINE_CYCLES);
}

void ppu_on_hblank(gba_t *g, uint64_t when) {
    (void)when;
    ppu_t *p = &g->ppu;
    p->dispstat |= DISPSTAT_HBLANK;
    if (p->dispstat & DISPSTAT_HBLANK_IRQ) irq_raise(g, IRQ_HBLANK);
}

void ppu_on_line_end(gba_t *g, uint64_t when) {
    ppu_t *p = &g->ppu;
    p->dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    p->vcount = (uint16_t)((p->vcount + 1) % PPU_LINES);

    if (p->vcount == PPU_VISIBLE_LINES) {
        p->dispstat |= DISPSTAT_VBLANK;
        if (p->dispstat & DISPSTAT_VBLANK_IRQ) irq_raise(g, IRQ_VBLANK);
        g->frame_done = true;
    } else if (p->vcount == PPU_LINES - 1) {
        p->dispstat &= (uint16_t)~DISPSTAT_VBLANK; /* the flag is already clear on the last line */
    }

    if (p->vcount == (p->dispstat >> 8)) {
        p->dispstat |= DISPSTAT_VCOUNT;
        if (p->dispstat & DISPSTAT_VCOUNT_IRQ) irq_raise(g, IRQ_VCOUNT);
    } else {
        p->dispstat &= (uint16_t)~DISPSTAT_VCOUNT;
    }

    sched_add(&g->sched, EV_HBLANK, when + PPU_HDRAW_CYCLES);
    sched_add(&g->sched, EV_LINE_END, when + PPU_LINE_CYCLES);
}
```

- [x] **Step 4: Replace `core/src/gba_internal.h`**

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "cart/cart.h"
#include "mem/bus.h"
#include "mem/io.h"
#include "ppu/ppu.h"
#include "sys/irq.h"
#include "sys/sched.h"

struct gba {
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    sched_t sched;
    cart_t cart;
    uint64_t now;      /* master clock: 16.78 MHz cycles since reset */
    bool frame_done;   /* set when VBlank starts; ends gba_run_frame */
    uint16_t keyinput; /* KEYINPUT register: bit clear = pressed */
};

/* Dispatch every scheduler event due at g->now, in timestamp order. */
void gba_run_events(gba_t *g);

#endif
```

- [x] **Step 5: In `core/src/gba.c`, replace `gba_reset` and add `gba_run_events`**

Replace the whole `gba_reset` function with:

```c
void gba_reset(gba_t *g) {
    g->now = 0;
    g->frame_done = false;
    g->keyinput = 0x3FF;
    sched_init(&g->sched);
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
    ppu_reset(g);
}

void gba_run_events(gba_t *g) {
    uint64_t when;
    int id;
    while ((id = sched_pop_due(&g->sched, g->now, &when)) >= 0) {
        switch (id) {
        case EV_HBLANK: ppu_on_hblank(g, when); break;
        case EV_LINE_END: ppu_on_line_end(g, when); break;
        default: break;
        }
    }
}
```

Also delete the now-unused `#include <string.h>` line from `gba.c`.

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 3 `ppu_*` tests.

- [x] **Step 7: Commit**

```bash
git add core/src tests/unit/test_ppu.c
git commit -m "feat(core): add LCD line timing, DISPSTAT and VCOUNT" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: CPU core — state, banking, exceptions, step loop, frame loop, HLE boot state

**Files:**
- Create: `core/src/cpu/cpu.h`, `core/src/cpu/cpu.c`, `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`, `core/src/bios/hle.h`, `core/src/bios/hle.c`, `tests/unit/testutil.h`, `tests/unit/testutil.c`
- Modify: `core/src/gba_internal.h`, `core/src/gba.c`
- Test: `tests/unit/test_cpu.c`, `tests/unit/test_frame.c`

- [x] **Step 1: Create the test helpers `tests/unit/testutil.h`**

```c
#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <stddef.h>
#include <stdint.h>

#include "gba_internal.h"

#define TU_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* A system booted (HLE) into a ROM whose code is ops, starting at 0x08000000. */
gba_t *tu_arm(const uint32_t *ops, size_t n);
/* Same, but the CPU starts in Thumb state. */
gba_t *tu_thumb(const uint16_t *ops, size_t n);
/* Execute exactly n instructions (pending IRQs are taken as in cpu_step). */
void tu_steps(gba_t *g, int n);

#endif
```

- [x] **Step 2: Create `tests/unit/testutil.c`**

```c
#include "testutil.h"

#include <stdlib.h>
#include <string.h>

static gba_t *boot(const uint8_t *code, size_t len) {
    size_t size = len < 0x200 ? 0x200 : (len + 3) & ~(size_t)3;
    uint8_t *rom = calloc(1, size);
    memcpy(rom, code, len);
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, size);
    free(rom);
    return g;
}

gba_t *tu_arm(const uint32_t *ops, size_t n) {
    uint8_t *code = malloc(n * 4);
    for (size_t i = 0; i < n; i++) {
        code[i * 4 + 0] = (uint8_t)ops[i];
        code[i * 4 + 1] = (uint8_t)(ops[i] >> 8);
        code[i * 4 + 2] = (uint8_t)(ops[i] >> 16);
        code[i * 4 + 3] = (uint8_t)(ops[i] >> 24);
    }
    gba_t *g = boot(code, n * 4);
    free(code);
    return g;
}

gba_t *tu_thumb(const uint16_t *ops, size_t n) {
    uint8_t *code = malloc(n * 2);
    for (size_t i = 0; i < n; i++) {
        code[i * 2 + 0] = (uint8_t)ops[i];
        code[i * 2 + 1] = (uint8_t)(ops[i] >> 8);
    }
    gba_t *g = boot(code, n * 2);
    free(code);
    g->cpu.cpsr |= CPSR_T;
    return g;
}

void tu_steps(gba_t *g, int n) {
    for (int i = 0; i < n; i++) cpu_step(g);
}
```

- [x] **Step 3: Write the failing CPU tests `tests/unit/test_cpu.c`**

```c
#include "test.h"
#include "testutil.h"

static const uint32_t zero_rom[] = {0};

TEST(cpu_boot_state_skips_the_bios) {
    gba_t *g = tu_arm(zero_rom, TU_LEN(zero_rom));
    arm_cpu_t *c = &g->cpu;
    CHECK_EQ(c->r[15], 0x08000000);
    CHECK_EQ(c->cpsr, MODE_SYS);
    CHECK_EQ(c->r[13], 0x03007F00);
    CHECK_EQ(c->bank_r13[BANK_IRQ], 0x03007FA0);
    CHECK_EQ(c->bank_r13[BANK_SVC], 0x03007FE0);
    CHECK_EQ(g->io.postflg, 1);
    gba_destroy(g);
}

TEST(cpu_mode_switch_banks_registers) {
    gba_t *g = tu_arm(zero_rom, TU_LEN(zero_rom));
    arm_cpu_t *c = &g->cpu;
    c->r[8] = 8;
    c->r[13] = 13;
    c->r[14] = 14;
    cpu_write_cpsr(c, MODE_FIQ);
    CHECK_EQ(c->r[8], 0);
    CHECK_EQ(c->r[13], 0);
    c->r[8] = 88;
    cpu_write_cpsr(c, MODE_IRQ);
    CHECK_EQ(c->r[8], 8);
    CHECK_EQ(c->r[13], 0x03007FA0);
    cpu_write_cpsr(c, MODE_SYS);
    CHECK_EQ(c->r[13], 13);
    CHECK_EQ(c->r[14], 14);
    cpu_write_cpsr(c, MODE_FIQ);
    CHECK_EQ(c->r[8], 88);
    gba_destroy(g);
}

TEST(cpu_condition_codes) {
    CHECK(cpu_cond_passed(CPSR_Z, 0x0));
    CHECK(!cpu_cond_passed(0, 0x0));
    CHECK(cpu_cond_passed(CPSR_C, 0x8));
    CHECK(!cpu_cond_passed(CPSR_C | CPSR_Z, 0x8));
    CHECK(cpu_cond_passed(CPSR_N | CPSR_V, 0xA));
    CHECK(!cpu_cond_passed(CPSR_N, 0xA));
    CHECK(cpu_cond_passed(0, 0xC));
    CHECK(!cpu_cond_passed(CPSR_Z, 0xC));
    CHECK(cpu_cond_passed(0, 0xE));
    CHECK(!cpu_cond_passed(0xF0000000, 0xF));
}

TEST(cpu_undefined_instruction_enters_und_mode) {
    const uint32_t ops[] = {0xE7F000F1}; /* permanently undefined */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 1);
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_UND);
    CHECK(c->cpsr & CPSR_I);
    CHECK_EQ(c->r[14], 0x08000004);
    CHECK_EQ(c->spsr[BANK_UND], MODE_SYS);
    CHECK_EQ(c->r[15], 0x00000004);
    gba_destroy(g);
}

TEST(cpu_failed_condition_skips_instruction) {
    const uint32_t ops[] = {0x07F000F1}; /* undefined, but only if EQ; Z is clear */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000004);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    gba_destroy(g);
}

TEST(cpu_thumb_undefined_instruction) {
    const uint16_t ops[] = {0xDE00};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 1);
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_UND);
    CHECK(!(c->cpsr & CPSR_T));
    CHECK_EQ(c->r[14], 0x08000002);
    CHECK_EQ(c->spsr[BANK_UND], MODE_SYS | CPSR_T);
    CHECK_EQ(c->r[15], 0x00000004);
    gba_destroy(g);
}

TEST(cpu_takes_enabled_irq_before_next_instruction) {
    const uint32_t ops[] = {0x07F000F1};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    g->irq.ie = IRQ_VBLANK;
    g->irq.if_ = IRQ_VBLANK;
    g->irq.ime = 1;
    tu_steps(g, 1); /* enters IRQ mode, then executes the instruction at the IRQ vector */
    CHECK_EQ(c->cpsr & CPSR_MODE, MODE_IRQ);
    CHECK(c->cpsr & CPSR_I);
    CHECK_EQ(c->r[14], 0x08000004);
    CHECK_EQ(c->spsr[BANK_IRQ], MODE_SYS);
    gba_destroy(g);
}

TEST(cpu_ignores_irq_when_ime_clear_or_cpsr_i_set) {
    const uint32_t ops[] = {0x07F000F1, 0x07F000F1};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.ie = IRQ_VBLANK;
    g->irq.if_ = IRQ_VBLANK;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    g->irq.ime = 1;
    g->cpu.cpsr |= CPSR_I;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    CHECK_EQ(g->cpu.r[15], 0x08000008);
    gba_destroy(g);
}
```

- [x] **Step 4: Write the failing frame tests `tests/unit/test_frame.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(frame_halted_cpu_sleeps_until_vblank) {
    const uint32_t ops[] = {0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.halted = true;
    gba_run_frame(g);
    CHECK_EQ(g->now, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(g->ppu.vcount, 160);
    CHECK(g->irq.halted);
    gba_run_frame(g);
    CHECK_EQ(g->now, 160 * PPU_LINE_CYCLES + PPU_FRAME_CYCLES);
    gba_destroy(g);
}

TEST(gba_load_bios_selects_real_bios_boot) {
    static uint8_t bios[0x4000];
    static uint8_t rom[0x200];
    gba_t *g = gba_create(NULL);
    CHECK_EQ(gba_load_bios(g, bios, 100), GBA_ERR_BIOS_SIZE);
    bios[0] = 0x2A; /* mov r0, #42 */
    bios[1] = 0x00;
    bios[2] = 0xA0;
    bios[3] = 0xE3;
    CHECK_EQ(gba_load_bios(g, bios, sizeof bios), GBA_OK);
    CHECK_EQ(gba_load_rom(g, rom, sizeof rom), GBA_OK);
    CHECK_EQ(g->cpu.r[15], 0);
    CHECK_EQ(g->cpu.cpsr, MODE_SVC | CPSR_I | CPSR_F);
    CHECK_EQ(g->bus.bios[0], 0x2A);
    gba_destroy(g);
}
```

- [x] **Step 5: Run to verify failure**

Run: `bash tools/dev.sh unit cpu`
Expected: build fails: unknown type `arm_cpu_t` / undeclared `cpu_step`.

- [x] **Step 6: Create `core/src/cpu/cpu.h`**

```c
#ifndef GBA_CPU_H
#define GBA_CPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

enum {
    MODE_USR = 0x10,
    MODE_FIQ = 0x11,
    MODE_IRQ = 0x12,
    MODE_SVC = 0x13,
    MODE_ABT = 0x17,
    MODE_UND = 0x1B,
    MODE_SYS = 0x1F,
};

#define CPSR_N (1u << 31)
#define CPSR_Z (1u << 30)
#define CPSR_C (1u << 29)
#define CPSR_V (1u << 28)
#define CPSR_I (1u << 7)
#define CPSR_F (1u << 6)
#define CPSR_T (1u << 5)
#define CPSR_MODE 0x1Fu

enum { BANK_USR, BANK_FIQ, BANK_IRQ, BANK_SVC, BANK_ABT, BANK_UND, BANK_COUNT };

/*
 * r[15]: between instructions, the address of the next instruction. While an
 * instruction executes, its address + 8 (ARM) or + 4 (Thumb) — the value the
 * program reads as PC.
 */
typedef struct {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t spsr[BANK_COUNT]; /* spsr[BANK_USR] is unused */
    uint32_t bank_r13[BANK_COUNT];
    uint32_t bank_r14[BANK_COUNT];
    uint32_t bank_usr_r8_12[5]; /* r8-r12 of every mode except FIQ */
    uint32_t bank_fiq_r8_12[5];
    bool branched;       /* the executing instruction wrote PC */
    bool next_fetch_seq; /* the next fetch continues sequentially */
} arm_cpu_t;

void cpu_init_tables(void); /* builds the ARM and Thumb decode tables; idempotent */
void cpu_reset(gba_t *g);   /* registers zero, SVC mode, IRQ+FIQ masked, ARM state, PC = 0 */
void cpu_step(gba_t *g);    /* take a pending enabled IRQ, then execute one instruction */

int cpu_mode_bank(uint32_t mode);
bool cpu_has_spsr(const arm_cpu_t *c);
void cpu_write_cpsr(arm_cpu_t *c, uint32_t value); /* swaps banked registers on mode change */
void cpu_branch(arm_cpu_t *c, uint32_t target);
void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr);
bool cpu_cond_passed(uint32_t cpsr, uint32_t cond);

static inline void cpu_set_nz(arm_cpu_t *c, uint32_t result) {
    c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | (result & CPSR_N) | (result == 0 ? CPSR_Z : 0);
}

static inline void cpu_set_c(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_C : c->cpsr & ~CPSR_C;
}

static inline void cpu_set_v(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_V : c->cpsr & ~CPSR_V;
}

static inline bool cpu_carry(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_C) != 0;
}

static inline bool cpu_overflow(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_V) != 0;
}

/* arm.c */
void arm_init_table(void);
void arm_execute(gba_t *g, uint32_t op);
/* thumb.c */
void thumb_init_table(void);
void thumb_execute(gba_t *g, uint16_t op);

#endif
```

- [x] **Step 7: Create `core/src/cpu/cpu.c`**

```c
#include "cpu/cpu.h"

#include <string.h>

#include "gba_internal.h"

int cpu_mode_bank(uint32_t mode) {
    switch (mode & CPSR_MODE) {
    case MODE_FIQ: return BANK_FIQ;
    case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SVC;
    case MODE_ABT: return BANK_ABT;
    case MODE_UND: return BANK_UND;
    default: return BANK_USR; /* USR, SYS and invalid mode values */
    }
}

bool cpu_has_spsr(const arm_cpu_t *c) {
    return cpu_mode_bank(c->cpsr) != BANK_USR;
}

static void switch_bank(arm_cpu_t *c, int from, int to) {
    if (from == to) return;
    if (from == BANK_FIQ || to == BANK_FIQ) {
        memcpy(from == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, &c->r[8], sizeof c->bank_usr_r8_12);
        memcpy(&c->r[8], to == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, sizeof c->bank_usr_r8_12);
    }
    c->bank_r13[from] = c->r[13];
    c->bank_r14[from] = c->r[14];
    c->r[13] = c->bank_r13[to];
    c->r[14] = c->bank_r14[to];
}

void cpu_write_cpsr(arm_cpu_t *c, uint32_t value) {
    switch_bank(c, cpu_mode_bank(c->cpsr), cpu_mode_bank(value));
    c->cpsr = value;
}

void cpu_branch(arm_cpu_t *c, uint32_t target) {
    c->r[15] = target;
    c->branched = true;
    c->next_fetch_seq = false;
}

void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr) {
    arm_cpu_t *c = &g->cpu;
    uint32_t old = c->cpsr;
    cpu_write_cpsr(c, (old & ~(CPSR_MODE | CPSR_T)) | mode | CPSR_I);
    c->spsr[cpu_mode_bank(mode)] = old;
    c->r[14] = return_addr;
    cpu_branch(c, vector);
}

bool cpu_cond_passed(uint32_t cpsr, uint32_t cond) {
    bool n = (cpsr & CPSR_N) != 0, z = (cpsr & CPSR_Z) != 0;
    bool cf = (cpsr & CPSR_C) != 0, v = (cpsr & CPSR_V) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return cf;
    case 0x3: return !cf;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return cf && !z;
    case 0x9: return !cf || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    case 0xE: return true;
    default: return false; /* NV: never executes on ARMv4 */
    }
}

void cpu_init_tables(void) {
    arm_init_table();
    thumb_init_table();
}

void cpu_reset(gba_t *g) {
    memset(&g->cpu, 0, sizeof g->cpu);
    g->cpu.cpsr = MODE_SVC | CPSR_I | CPSR_F;
}

void cpu_step(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    /* LR_irq = next instruction + 4, so the handler's "subs pc, lr, #4" resumes it. */
    if (irq_line(g) && !(c->cpsr & CPSR_I)) cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);

    uint32_t pc = c->r[15];
    c->branched = false;
    if (c->cpsr & CPSR_T) {
        uint16_t op = (uint16_t)bus_fetch16(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 4;
        thumb_execute(g, op);
        if (!c->branched) c->r[15] = pc + 2;
    } else {
        uint32_t op = bus_fetch32(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 8;
        if (cpu_cond_passed(c->cpsr, op >> 28)) arm_execute(g, op);
        if (!c->branched) c->r[15] = pc + 4;
    }
}
```

- [x] **Step 8: Create the ARM decoder skeleton `core/src/cpu/arm.c`**

Every instruction is "undefined" for now; Tasks 8–12 fill in the classes.

```c
#include "cpu/cpu.h"
#include "gba_internal.h"

typedef void (*arm_handler_t)(gba_t *g, uint32_t op);

static arm_handler_t arm_table[4096];

static void arm_undefined(gba_t *g, uint32_t op) {
    (void)op;
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 4);
}

/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    (void)hi;
    (void)lo;
    return arm_undefined;
}

void arm_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) arm_table[i] = arm_classify(i >> 4, i & 0xF);
}

void arm_execute(gba_t *g, uint32_t op) {
    arm_table[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)](g, op);
}
```

- [x] **Step 9: Create the Thumb decoder skeleton `core/src/cpu/thumb.c`**

```c
#include "cpu/cpu.h"
#include "gba_internal.h"

typedef void (*thumb_handler_t)(gba_t *g, uint16_t op);

static thumb_handler_t thumb_table[1024];

static void thumb_undefined(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 2);
}

/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    (void)idx;
    return thumb_undefined;
}

void thumb_init_table(void) {
    for (uint32_t i = 0; i < 1024; i++) thumb_table[i] = thumb_classify(i);
}

void thumb_execute(gba_t *g, uint16_t op) {
    thumb_table[op >> 6](g, op);
}
```

- [x] **Step 10: Create `core/src/bios/hle.h`**

```c
#ifndef GBA_HLE_H
#define GBA_HLE_H

typedef struct gba gba_t;

/* Put the system in the state the real BIOS leaves when it jumps to the cartridge. */
void hle_boot(gba_t *g);

#endif
```

- [x] **Step 11: Create `core/src/bios/hle.c`**

```c
#include "bios/hle.h"

#include "gba_internal.h"

/* Stack pointers and modes the real BIOS sets up before entering the cartridge. */
static void set_post_bios_registers(arm_cpu_t *c) {
    cpu_write_cpsr(c, MODE_SVC);
    c->r[13] = 0x03007FE0;
    c->r[14] = 0;
    c->spsr[BANK_SVC] = 0;
    cpu_write_cpsr(c, MODE_IRQ);
    c->r[13] = 0x03007FA0;
    c->r[14] = 0;
    c->spsr[BANK_IRQ] = 0;
    cpu_write_cpsr(c, MODE_SYS);
    c->r[13] = 0x03007F00;
    c->r[14] = 0;
    for (int i = 0; i <= 12; i++) c->r[i] = 0;
}

void hle_boot(gba_t *g) {
    set_post_bios_registers(&g->cpu);
    g->cpu.r[15] = 0x08000000;
    g->io.postflg = 1;
}
```

- [x] **Step 12: Replace `core/src/gba_internal.h`** (final form for this milestone)

```c
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "cart/cart.h"
#include "cpu/cpu.h"
#include "mem/bus.h"
#include "mem/io.h"
#include "ppu/ppu.h"
#include "sys/irq.h"
#include "sys/sched.h"

struct gba {
    arm_cpu_t cpu;
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    sched_t sched;
    cart_t cart;
    uint64_t now;      /* master clock: 16.78 MHz cycles since reset */
    bool frame_done;   /* set when VBlank starts; ends gba_run_frame */
    bool hle_bios;     /* true: built-in BIOS stub + C SWIs; false: user BIOS */
    uint16_t keyinput; /* KEYINPUT register: bit clear = pressed */
};

/* Dispatch every scheduler event due at g->now, in timestamp order. */
void gba_run_events(gba_t *g);

#endif
```

- [x] **Step 13: Replace `core/src/gba.c`** (final form for this milestone)

```c
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "gba_internal.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    cpu_init_tables();
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
    if (err == GBA_OK) gba_reset(g);
    return err;
}

void gba_reset(gba_t *g) {
    g->now = 0;
    g->frame_done = false;
    g->keyinput = 0x3FF;
    sched_init(&g->sched);
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
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
        default: break;
        }
    }
}

void gba_run_frame(gba_t *g) {
    g->frame_done = false;
    while (!g->frame_done) {
        uint64_t next = sched_next(&g->sched);
        while (g->now < next) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
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
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
```

- [x] **Step 14: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including 8 `cpu_*`, `frame_halted_cpu_sleeps_until_vblank` and `gba_load_bios_selects_real_bios_boot`.

- [x] **Step 15: Commit**

```bash
git add core/src tests/unit
git commit -m "feat(cpu): add ARM7TDMI state, banking, exceptions, step and frame loops" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: ALU helpers (adder, barrel shifter, multiplier timing)

**Files:**
- Create: `core/src/cpu/alu.h`
- Test: `tests/unit/test_alu.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_alu.c`**

```c
#include "cpu/alu.h"
#include "test.h"

TEST(alu_add_carry_and_overflow) {
    alu_result_t r = alu_add(0xFFFFFFFF, 1, 0);
    CHECK_EQ(r.value, 0);
    CHECK(r.carry);
    CHECK(!r.overflow);
    r = alu_add(0x7FFFFFFF, 1, 0);
    CHECK_EQ(r.value, 0x80000000);
    CHECK(!r.carry);
    CHECK(r.overflow);
    r = alu_add(5, ~5u, 1); /* 5 - 5 */
    CHECK_EQ(r.value, 0);
    CHECK(r.carry); /* no borrow */
    r = alu_add(3, ~5u, 1); /* 3 - 5 */
    CHECK_EQ(r.value, 0xFFFFFFFEu);
    CHECK(!r.carry);
    r = alu_add(0x80000000, ~1u, 1); /* INT_MIN - 1 */
    CHECK(r.overflow);
}

TEST(alu_shift_by_immediate_special_encodings) {
    bool c = true;
    CHECK_EQ(shift_by_imm(0, 0x12345678, 0, &c), 0x12345678); /* LSL #0: unchanged */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(0, 0x80000001, 1, &c), 2);
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(1, 0x80000000, 0, &c), 0); /* LSR #0 means #32 */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_imm(2, 0x80000000, 0, &c), 0xFFFFFFFF); /* ASR #0 means #32 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_imm(3, 0x00000002, 0, &c), 0x80000001); /* ROR #0 means RRX */
    CHECK(!c);
    c = true;
    CHECK_EQ(shift_by_imm(3, 0x000000F1, 4, &c), 0x1000000F);
    CHECK(!c);
}

TEST(alu_shift_by_register_large_amounts) {
    bool c = true;
    CHECK_EQ(shift_by_reg(0, 0x1234, 0, &c), 0x1234); /* amount 0: unchanged, C kept */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(0, 1, 32, &c), 0); /* LSL 32: C = bit 0 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_reg(0, 1, 33, &c), 0);
    CHECK(!c);
    c = false;
    CHECK_EQ(shift_by_reg(1, 0x80000000, 32, &c), 0); /* LSR 32: C = bit 31 */
    CHECK(c);
    c = true;
    CHECK_EQ(shift_by_reg(1, 0x80000000, 33, &c), 0);
    CHECK(!c);
    c = false;
    CHECK_EQ(shift_by_reg(2, 0x80000000, 40, &c), 0xFFFFFFFF);
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(3, 0x80000001, 32, &c), 0x80000001); /* ROR 32: C = bit 31 */
    CHECK(c);
    c = false;
    CHECK_EQ(shift_by_reg(3, 0x00000003, 33, &c), 0x80000001); /* ROR 33 = ROR 1 */
    CHECK(c);
}

TEST(alu_multiplier_cycles) {
    CHECK_EQ(alu_mul_cycles(0x000000FF, false), 1);
    CHECK_EQ(alu_mul_cycles(0x00001234, false), 2);
    CHECK_EQ(alu_mul_cycles(0x00123456, false), 3);
    CHECK_EQ(alu_mul_cycles(0x12345678, false), 4);
    CHECK_EQ(alu_mul_cycles(0xFFFFFF00, true), 1);
    CHECK_EQ(alu_mul_cycles(0xFFFFFF00, false), 4);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit alu`
Expected: build fails: `'cpu/alu.h' file not found`.

- [x] **Step 3: Create `core/src/cpu/alu.h`**

```c
#ifndef GBA_ALU_H
#define GBA_ALU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t value;
    bool carry, overflow;
} alu_result_t;

static inline uint32_t ror32(uint32_t v, uint32_t n) {
    n &= 31;
    return n ? (v >> n) | (v << (32 - n)) : v;
}

/* a + b + carry_in with ARM carry-out and signed overflow. Subtraction a - b is alu_add(a, ~b, 1). */
static inline alu_result_t alu_add(uint32_t a, uint32_t b, uint32_t carry_in) {
    uint64_t wide = (uint64_t)a + b + carry_in;
    alu_result_t r;
    r.value = (uint32_t)wide;
    r.carry = (wide >> 32) != 0;
    r.overflow = ((~(a ^ b) & (a ^ r.value)) >> 31) != 0;
    return r;
}

/*
 * Barrel shifter with an immediate amount (0-31). type: 0 LSL, 1 LSR, 2 ASR, 3 ROR.
 * Amount 0 encodes: LSL #0 = no shift, LSR/ASR #0 = #32, ROR #0 = RRX.
 * *carry holds C on entry and the shifter carry-out on return.
 */
static inline uint32_t shift_by_imm(uint32_t type, uint32_t v, uint32_t amount, bool *carry) {
    switch (type) {
    case 0:
        if (amount == 0) return v;
        *carry = (v >> (32 - amount)) & 1;
        return v << amount;
    case 1:
        if (amount == 0) {
            *carry = v >> 31;
            return 0;
        }
        *carry = (v >> (amount - 1)) & 1;
        return v >> amount;
    case 2:
        if (amount == 0) {
            *carry = v >> 31;
            return (uint32_t)((int32_t)v >> 31);
        }
        *carry = (v >> (amount - 1)) & 1;
        return (uint32_t)((int32_t)v >> amount);
    default:
        if (amount == 0) {
            bool old = *carry;
            *carry = v & 1;
            return (v >> 1) | ((uint32_t)old << 31);
        }
        *carry = (v >> (amount - 1)) & 1;
        return ror32(v, amount);
    }
}

/* Barrel shifter with a register amount (bottom byte of Rs, 0-255). Amount 0 leaves v and C alone. */
static inline uint32_t shift_by_reg(uint32_t type, uint32_t v, uint32_t amount, bool *carry) {
    if (amount == 0) return v;
    switch (type) {
    case 0:
        if (amount < 32) {
            *carry = (v >> (32 - amount)) & 1;
            return v << amount;
        }
        *carry = amount == 32 ? (v & 1) : 0;
        return 0;
    case 1:
        if (amount < 32) {
            *carry = (v >> (amount - 1)) & 1;
            return v >> amount;
        }
        *carry = amount == 32 ? (v >> 31) : 0;
        return 0;
    case 2:
        if (amount < 32) {
            *carry = (v >> (amount - 1)) & 1;
            return (uint32_t)((int32_t)v >> amount);
        }
        *carry = v >> 31;
        return (uint32_t)((int32_t)v >> 31);
    default:
        amount &= 31;
        if (amount == 0) {
            *carry = v >> 31;
            return v;
        }
        *carry = (v >> (amount - 1)) & 1;
        return ror32(v, amount);
    }
}

/* Multiplier array cycles (1-4): the multiplier stops early when the top bytes of rs are all 0s (or all 1s when signed). */
static inline uint32_t alu_mul_cycles(uint32_t rs, bool allow_ones) {
    if ((rs >> 8) == 0 || (allow_ones && (rs >> 8) == 0x00FFFFFF)) return 1;
    if ((rs >> 16) == 0 || (allow_ones && (rs >> 16) == 0xFFFF)) return 2;
    if ((rs >> 24) == 0 || (allow_ones && (rs >> 24) == 0xFF)) return 3;
    return 4;
}

#endif
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit alu`
Expected: 4 `alu_*` tests pass.

- [x] **Step 5: Commit**

```bash
git add core/src/cpu/alu.h tests/unit/test_alu.c
git commit -m "feat(cpu): add adder, barrel shifter and multiplier timing helpers" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: ARM data processing and PSR transfer

**Files:**
- Modify: `core/src/cpu/arm.c` (full replacement)
- Test: `tests/unit/test_arm_dp.c`

Instruction encodings in tests were produced with the NDK assembler (`clang --target=armv4t-none-eabi`); the comment shows the source.

- [x] **Step 1: Write the failing tests `tests/unit/test_arm_dp.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(arm_mov_add_subs) {
    const uint32_t ops[] = {
        0xE3A00005, /* mov  r0, #5 */
        0xE2801003, /* add  r1, r0, #3 */
        0xE2502005, /* subs r2, r0, #5 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[1], 8);
    CHECK_EQ(g->cpu.r[2], 0);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    gba_destroy(g);
}

TEST(arm_rotated_immediate_sets_carry) {
    const uint32_t ops[] = {0xE3B00102}; /* movs r0, #0x80000000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x80000000);
    CHECK(g->cpu.cpsr & CPSR_N);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_register_shift_reads_pc_plus_12) {
    const uint32_t ops[] = {
        0xE3A00000, /* mov r0, #0 */
        0xE1A0101F, /* mov r1, pc, lsl r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0x08000010);
    gba_destroy(g);
}

TEST(arm_conditional_execution) {
    const uint32_t ops[] = {
        0xE3A00001, /* mov   r0, #1 */
        0xE3500001, /* cmp   r0, #1 */
        0x13A01007, /* movne r1, #7 */
        0x03A02009, /* moveq r2, #9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[2], 9);
    gba_destroy(g);
}

TEST(arm_carry_arithmetic) {
    const uint32_t ops[] = {
        0xE3E00000, /* mvn  r0, #0 */
        0xE2901001, /* adds r1, r0, #1 */
        0xE2A12000, /* adc  r2, r1, #0 */
        0xE262300A, /* rsb  r3, r2, #10 */
        0xE2524002, /* subs r4, r2, #2 */
        0xE2C35000, /* sbc  r5, r3, #0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 6);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[2], 1);
    CHECK_EQ(g->cpu.r[3], 9);
    CHECK_EQ(g->cpu.r[4], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[5], 8);
    gba_destroy(g);
}

TEST(arm_tst_takes_carry_from_rotated_immediate) {
    const uint32_t ops[] = {0xE3100102}; /* tst r0, #0x80000000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_logical_shift_sets_carry) {
    const uint32_t ops[] = {0xE1B000A1}; /* movs r0, r1, lsr #1 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 1;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0);
    CHECK(g->cpu.cpsr & CPSR_Z);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_subs_pc_restores_cpsr) {
    const uint32_t ops[] = {0xE25EF004}; /* subs pc, lr, #4 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_IRQ | CPSR_I);
    g->cpu.spsr[BANK_IRQ] = MODE_SYS | CPSR_Z;
    g->cpu.r[14] = 0x08000104;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS | CPSR_Z);
    gba_destroy(g);
}

TEST(arm_psr_transfer) {
    const uint32_t ops[] = {
        0xE10F0000, /* mrs r0, cpsr */
        0xE328F20F, /* msr cpsr_f, #0xF0000000 */
        0xE10F1000, /* mrs r1, cpsr */
        0xE321F012, /* msr cpsr_c, #0x12 */
        0xE10F2000, /* mrs r2, cpsr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 5);
    CHECK_EQ(g->cpu.r[0], 0x0000001F);
    CHECK_EQ(g->cpu.r[1], 0xF000001F);
    CHECK_EQ(g->cpu.r[2], 0xF0000012);
    CHECK_EQ(g->cpu.r[13], 0x03007FA0);
    gba_destroy(g);
}

TEST(arm_msr_in_user_mode_only_writes_flags) {
    const uint32_t ops[] = {
        0xE3A0001F, /* mov r0, #0x1F */
        0xE380020F, /* orr r0, r0, #0xF0000000 */
        0xE129F000, /* msr cpsr_fc, r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_USR);
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.cpsr, 0xF0000010);
    gba_destroy(g);
}

TEST(arm_msr_mrs_spsr) {
    const uint32_t ops[] = {
        0xE3A00010, /* mov r0, #0x10 */
        0xE169F000, /* msr spsr_fc, r0 */
        0xE14F1000, /* mrs r1, spsr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_IRQ);
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[1], 0x10);
    CHECK_EQ(g->cpu.spsr[BANK_IRQ], 0x10);
    gba_destroy(g);
}

TEST(arm_compare_with_pc_destination_restores_cpsr_without_branching) {
    const uint32_t ops[] = {
        0xE15FF000, /* cmp pc, r0 with Rd = 15 (ARMv3 "cmpp") */
        0xE3A01001, /* mov r1, #1 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    cpu_write_cpsr(&g->cpu, MODE_FIQ);
    g->cpu.spsr[BANK_FIQ] = MODE_SYS;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    CHECK_EQ(g->cpu.r[1], 1);
    CHECK_EQ(g->cpu.r[15], 0x08000008);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit arm_`
Expected: tests FAIL (every instruction is still "undefined": wrong register values, UND mode).

- [x] **Step 3: Replace `core/src/cpu/arm.c`**

```c
#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "gba_internal.h"

typedef void (*arm_handler_t)(gba_t *g, uint32_t op);

static arm_handler_t arm_table[4096];

#define BIT(op, n) (((op) >> (n)) & 1u)

static void arm_undefined(gba_t *g, uint32_t op) {
    (void)op;
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 4);
}

static void arm_data_processing(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t opcode = (op >> 21) & 0xF;
    bool s = BIT(op, 20);
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t a = c->r[rn];
    bool shift_c = cpu_carry(c);
    uint32_t b;

    if (BIT(op, 25)) {
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        b = ror32(op & 0xFF, rot);
        if (rot) shift_c = (b >> 31) != 0;
    } else if (BIT(op, 4)) {
        /* Register-specified shift: one extra cycle, and PC reads 4 bytes further ahead. */
        uint32_t rm = op & 0xF;
        uint32_t value = c->r[rm] + (rm == 15 ? 4 : 0);
        if (rn == 15) a += 4;
        b = shift_by_reg((op >> 5) & 3, value, c->r[(op >> 8) & 0xF] & 0xFF, &shift_c);
        bus_idle(g, 1);
    } else {
        b = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &shift_c);
    }

    alu_result_t r = {0, shift_c, cpu_overflow(c)}; /* logical ops: C from shifter, V kept */
    switch (opcode) {
    case 0x0: case 0x8: r.value = a & b; break;        /* AND, TST */
    case 0x1: case 0x9: r.value = a ^ b; break;        /* EOR, TEQ */
    case 0x2: case 0xA: r = alu_add(a, ~b, 1); break;  /* SUB, CMP */
    case 0x3: r = alu_add(b, ~a, 1); break;            /* RSB */
    case 0x4: case 0xB: r = alu_add(a, b, 0); break;   /* ADD, CMN */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); break;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); break; /* SBC */
    case 0x7: r = alu_add(b, ~a, cpu_carry(c)); break; /* RSC */
    case 0xC: r.value = a | b; break;                  /* ORR */
    case 0xD: r.value = b; break;                      /* MOV */
    case 0xE: r.value = a & ~b; break;                 /* BIC */
    default: r.value = ~b; break;                      /* MVN */
    }
    bool is_test = opcode >= 0x8 && opcode <= 0xB;

    if (s && rd == 15) {
        /* S with PC as destination copies SPSR to CPSR (exception return); compares do not branch. */
        if (cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
    } else if (s) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V)) | (r.value & CPSR_N) |
                  (r.value == 0 ? CPSR_Z : 0) | (r.carry ? CPSR_C : 0) | (r.overflow ? CPSR_V : 0);
    }
    if (!is_test) {
        if (rd == 15) cpu_branch(c, r.value & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        else c->r[rd] = r.value;
    }
}

static void arm_mrs(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 12) & 0xF;
    if (BIT(op, 22) && cpu_has_spsr(c)) c->r[rd] = c->spsr[cpu_mode_bank(c->cpsr)];
    else c->r[rd] = c->cpsr;
}

static void arm_msr(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t value = BIT(op, 25) ? ror32(op & 0xFF, ((op >> 8) & 0xF) * 2) : c->r[op & 0xF];
    uint32_t mask = (BIT(op, 19) ? 0xFF000000u : 0) | (BIT(op, 16) ? 0x000000FFu : 0);
    if (BIT(op, 22)) {
        if (cpu_has_spsr(c)) {
            uint32_t *spsr = &c->spsr[cpu_mode_bank(c->cpsr)];
            *spsr = (*spsr & ~mask) | (value & mask);
        }
        return;
    }
    if ((c->cpsr & CPSR_MODE) == MODE_USR) mask &= 0xFF000000u; /* user mode may only write flags */
    uint32_t next = (c->cpsr & ~mask) | (value & mask);
    next = (next & ~CPSR_T) | (c->cpsr & CPSR_T); /* MSR cannot change the Thumb bit */
    cpu_write_cpsr(c, next);
}

/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_undefined; /* multiply/extra loads: Tasks 10-11 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return arm_data_processing;
    return arm_undefined;
}

void arm_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) arm_table[i] = arm_classify(i >> 4, i & 0xF);
}

void arm_execute(gba_t *g, uint32_t op) {
    arm_table[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)](g, op);
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including 12 `arm_*` tests.

- [x] **Step 5: Commit**

```bash
git add core/src/cpu/arm.c tests/unit/test_arm_dp.c
git commit -m "feat(cpu): implement ARM data processing and PSR transfer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: ARM branches and SWI; CPU-driven frame loop

**Files:**
- Modify: `core/src/cpu/arm.c`
- Test: `tests/unit/test_arm_branch.c`, `tests/unit/test_frame.c` (append)

- [x] **Step 1: Write the failing tests `tests/unit/test_arm_branch.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(arm_branch_forward) {
    const uint32_t ops[] = {0xEA000002}; /* b .+0x10 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000010);
    gba_destroy(g);
}

TEST(arm_branch_with_link) {
    const uint32_t ops[] = {0xEB00003E}; /* bl .+0x100 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    CHECK_EQ(g->cpu.r[14], 0x08000004);
    gba_destroy(g);
}

TEST(arm_branch_backward) {
    const uint32_t ops[] = {
        0xE1A00000, /* mov r0, r0 */
        0xEAFFFFFD, /* b .-4 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(arm_bx_switches_to_thumb) {
    const uint32_t ops[] = {0xE12FFF10}; /* bx r0 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000021;
    tu_steps(g, 1);
    CHECK(g->cpu.cpsr & CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    gba_destroy(g);
}

TEST(arm_bx_to_arm_aligns_target) {
    const uint32_t ops[] = {0xE12FFF10}; /* bx r0 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000032;
    tu_steps(g, 1);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[15], 0x08000030);
    gba_destroy(g);
}

TEST(arm_swi_enters_supervisor_mode) {
    const uint32_t ops[] = {0xEF010000}; /* swi 0x10000 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SVC);
    CHECK(g->cpu.cpsr & CPSR_I);
    CHECK_EQ(g->cpu.r[14], 0x08000004);
    CHECK_EQ(g->cpu.spsr[BANK_SVC], MODE_SYS);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    gba_destroy(g);
}
```

- [x] **Step 2: Append frame-loop tests to `tests/unit/test_frame.c`**

```c
TEST(frame_runs_cpu_between_events) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    gba_run_frame(g);
    CHECK(g->now >= 160 * PPU_LINE_CYCLES);
    CHECK(g->now < 160 * PPU_LINE_CYCLES + 16);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(frame_enabled_irq_wakes_halted_cpu) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.halted = true;
    g->irq.ie = IRQ_VBLANK;
    g->ppu.dispstat = DISPSTAT_VBLANK_IRQ;
    gba_run_frame(g); /* VBlank is flagged by the frame's last event */
    CHECK(g->irq.halted);
    gba_run_frame(g);
    CHECK(!g->irq.halted);
    gba_destroy(g);
}
```

- [x] **Step 3: Run to verify failure**

Run: `bash tools/dev.sh unit arm_b`
Expected: FAIL — branches are still undefined instructions (UND mode, wrong PC).

- [x] **Step 4: Add the handlers to `core/src/cpu/arm.c`**

Insert above `arm_classify`:

```c
static void arm_branch(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (uint32_t)(((int32_t)(op << 8)) >> 6); /* sign-extended 24-bit word offset */
    if (BIT(op, 24)) c->r[14] = c->r[15] - 4;
    cpu_branch(c, c->r[15] + offset);
}

static void arm_bx(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[op & 0xF];
    if (target & 1) {
        c->cpsr |= CPSR_T;
        cpu_branch(c, target & ~1u);
    } else {
        c->cpsr &= ~CPSR_T;
        cpu_branch(c, target & ~3u);
    }
}

static void arm_swi(gba_t *g, uint32_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 4);
}
```

- [x] **Step 5: Replace `arm_classify` in `core/src/cpu/arm.c`**

```c
/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_undefined; /* multiply/extra loads: Tasks 10-11 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return arm_data_processing;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined;
}
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including 6 new `arm_*` branch tests and 2 new `frame_*` tests.

- [x] **Step 7: Commit**

```bash
git add core/src/cpu/arm.c tests/unit/test_arm_branch.c tests/unit/test_frame.c
git commit -m "feat(cpu): implement ARM B/BL/BX and SWI" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: ARM multiply

**Files:**
- Modify: `core/src/cpu/arm.c`
- Test: `tests/unit/test_arm_mul.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_arm_mul.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(arm_multiply_family) {
    const uint32_t ops[] = {
        0xE3A00006, /* mov   r0, #6 */
        0xE3A01007, /* mov   r1, #7 */
        0xE0020190, /* mul   r2, r0, r1 */
        0xE0232190, /* mla   r3, r0, r1, r2 */
        0xE3E04000, /* mvn   r4, #0 */
        0xE0865494, /* umull r5, r6, r4, r4 */
        0xE0C87494, /* smull r7, r8, r4, r4 */
        0xE3A09002, /* mov   r9, #2 */
        0xE0E87994, /* smlal r7, r8, r4, r9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 9);
    CHECK_EQ(g->cpu.r[2], 42);
    CHECK_EQ(g->cpu.r[3], 84);
    CHECK_EQ(g->cpu.r[5], 1);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[7], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFFFFu);
    gba_destroy(g);
}

TEST(arm_multiply_flags_keep_carry_and_overflow) {
    const uint32_t ops[] = {
        0xE328F203, /* msr  cpsr_f, #0x30000000 (C and V) */
        0xE01A0094, /* muls r10, r4, r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[4] = 0xFFFFFFFF;
    g->cpu.r[0] = 6;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[10], 0xFFFFFFFAu);
    CHECK(g->cpu.cpsr & CPSR_N);
    CHECK(!(g->cpu.cpsr & CPSR_Z));
    CHECK(g->cpu.cpsr & CPSR_C);
    CHECK(g->cpu.cpsr & CPSR_V);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit arm_multiply`
Expected: FAIL (multiplies are still undefined).

- [x] **Step 3: Add the handlers to `core/src/cpu/arm.c`** (above `arm_classify`)

```c
static void arm_multiply(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    uint32_t result = c->r[rm] * c->r[rs];
    uint32_t cycles = alu_mul_cycles(c->r[rs], true);
    if (BIT(op, 21)) {
        result += c->r[rn];
        cycles++;
    }
    bus_idle(g, cycles);
    c->r[rd] = result;
    if (BIT(op, 20)) cpu_set_nz(c, result); /* C and V are left unchanged */
}

static void arm_multiply_long(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t hi = (op >> 16) & 0xF, lo = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    bool is_signed = BIT(op, 22);
    uint64_t result = is_signed ? (uint64_t)((int64_t)(int32_t)c->r[rm] * (int64_t)(int32_t)c->r[rs])
                                : (uint64_t)c->r[rm] * c->r[rs];
    uint32_t cycles = alu_mul_cycles(c->r[rs], is_signed) + 1;
    if (BIT(op, 21)) {
        result += (uint64_t)c->r[hi] << 32 | c->r[lo];
        cycles++;
    }
    bus_idle(g, cycles);
    c->r[lo] = (uint32_t)result;
    c->r[hi] = (uint32_t)(result >> 32);
    if (BIT(op, 20)) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | ((uint32_t)(result >> 32) & CPSR_N) | (result == 0 ? CPSR_Z : 0);
    }
}
```

- [x] **Step 4: Replace `arm_classify` in `core/src/cpu/arm.c`**

```c
/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_multiply;
    if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_multiply_long;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_undefined; /* swap/extra loads: Task 11 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return arm_data_processing;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined;
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including both `arm_multiply_*` tests.

- [x] **Step 6: Commit**

```bash
git add core/src/cpu/arm.c tests/unit/test_arm_mul.c
git commit -m "feat(cpu): implement ARM multiply and multiply-long" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: ARM single, halfword/signed and swap transfers

**Files:**
- Create: `core/src/cpu/cpu_mem.h`
- Modify: `core/src/cpu/arm.c`
- Test: `tests/unit/test_arm_mem.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_arm_mem.c`**

```c
#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(arm_word_and_byte_transfers) {
    const uint32_t ops[] = {
        0xE5801000, /* str  r1, [r0] */
        0xE5902001, /* ldr  r2, [r0, #1] (rotated) */
        0xE5D03002, /* ldrb r3, [r0, #2] */
        0xE5C01008, /* strb r1, [r0, #8] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x11223344;
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[2], 0x44112233);
    CHECK_EQ(g->cpu.r[3], 0x22);
    CHECK_EQ(bus_read32(g, IW + 8, false), 0x44);
    gba_destroy(g);
}

TEST(arm_halfword_loads_rotate_when_misaligned) {
    const uint32_t ops[] = {
        0xE5801000, /* str  r1, [r0] */
        0xE1D040B2, /* ldrh r4, [r0, #2] */
        0xE1D050B1, /* ldrh r5, [r0, #1] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x11223344;
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[4], 0x1122);
    CHECK_EQ(g->cpu.r[5], 0x44000033);
    gba_destroy(g);
}

TEST(arm_signed_loads) {
    const uint32_t ops[] = {
        0xE1C011B0, /* strh  r1, [r0, #16] */
        0xE1D061F0, /* ldrsh r6, [r0, #16] */
        0xE1D071F1, /* ldrsh r7, [r0, #17] (misaligned: signed byte) */
        0xE1D081D0, /* ldrsb r8, [r0, #16] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0xFF80;
    tu_steps(g, 4);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFF80u);
    CHECK_EQ(g->cpu.r[7], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFF80u);
    gba_destroy(g);
}

TEST(arm_indexing_and_writeback) {
    const uint32_t ops[] = {
        0xE5A01004, /* str r1, [r0, #4]! */
        0xE4109004, /* ldr r9, [r0], #-4 */
        0xE790B10C, /* ldr r11, [r0, r12, lsl #2] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0xABCD;
    g->cpu.r[12] = 1;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 4);
    CHECK_EQ(bus_read32(g, IW + 4, false), 0xABCD);
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[9], 0xABCD);
    CHECK_EQ(g->cpu.r[0], IW);
    CHECK_EQ(g->cpu.r[11], 0xABCD);
    gba_destroy(g);
}

TEST(arm_store_pc_and_load_pc) {
    const uint32_t ops[] = {
        0xE580F00C, /* str pc, [r0, #12] */
        0xE590F014, /* ldr pc, [r0, #20] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    bus_write32(g, IW + 20, 0x08000100, false);
    tu_steps(g, 2);
    CHECK_EQ(bus_read32(g, IW + 12, false), 0x0800000C);
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    gba_destroy(g);
}

TEST(arm_load_writeback_same_register_keeps_loaded_value) {
    const uint32_t ops[] = {0xE5B00004}; /* ldr r0, [r0, #4]! */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    bus_write32(g, IW + 4, 0x55, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x55);
    gba_destroy(g);
}

TEST(arm_swap_word_and_byte) {
    const uint32_t ops[] = {
        0xE100A091, /* swp  r10, r1, [r0] */
        0xE140B092, /* swpb r11, r2, [r0] */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[1] = 0x22222222;
    g->cpu.r[2] = 0x33;
    bus_write32(g, IW, 0x11111111, false);
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[10], 0x11111111);
    CHECK_EQ(g->cpu.r[11], 0x22);
    CHECK_EQ(bus_read32(g, IW, false), 0x22222233);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit arm_`
Expected: the 7 new tests FAIL (transfers are still undefined).

- [x] **Step 3: Create `core/src/cpu/cpu_mem.h`**

```c
#ifndef GBA_CPU_MEM_H
#define GBA_CPU_MEM_H

#include <stdint.h>

#include "cpu/alu.h"
#include "mem/bus.h"

/* ARM7TDMI load semantics shared by the ARM and Thumb handlers. */

static inline uint32_t cpu_load_word(gba_t *g, uint32_t addr) {
    return ror32(bus_read32(g, addr, false), (addr & 3) * 8); /* misaligned: rotated */
}

static inline uint32_t cpu_load_half(gba_t *g, uint32_t addr) {
    return ror32(bus_read16(g, addr, false), (addr & 1) * 8); /* misaligned: rotated */
}

static inline uint32_t cpu_load_signed_byte(gba_t *g, uint32_t addr) {
    return (uint32_t)(int32_t)(int8_t)bus_read8(g, addr, false);
}

static inline uint32_t cpu_load_signed_half(gba_t *g, uint32_t addr) {
    if (addr & 1) return cpu_load_signed_byte(g, addr); /* misaligned: loads a signed byte */
    return (uint32_t)(int32_t)(int16_t)bus_read16(g, addr, false);
}

#endif
```

- [x] **Step 4: Add `#include "cpu/cpu_mem.h"` to the includes at the top of `core/src/cpu/arm.c`**, then add the handlers above `arm_classify`:

```c
static void arm_single_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset;
    if (BIT(op, 25)) {
        bool unused_carry = cpu_carry(c); /* RRX reads C; the carry-out is discarded */
        offset = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &unused_carry);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);

    if (BIT(op, 20)) {
        uint32_t value = BIT(op, 22) ? bus_read8(g, addr, false) : cpu_load_word(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved; /* a load into the base register wins */
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0); /* STR PC stores address + 12 */
        if (BIT(op, 22)) bus_write8(g, addr, (uint8_t)value, false);
        else bus_write32(g, addr, value, false);
        if (writeback) c->r[rn] = moved;
    }
}

static void arm_halfword_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset = BIT(op, 22) ? (((op >> 4) & 0xF0) | (op & 0xF)) : c->r[op & 0xF];
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);
    uint32_t sh = (op >> 5) & 3;

    if (BIT(op, 20)) {
        uint32_t value = sh == 1   ? cpu_load_half(g, addr)
                         : sh == 2 ? cpu_load_signed_byte(g, addr)
                                   : cpu_load_signed_half(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved;
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else if (sh == 1) {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0);
        bus_write16(g, addr, (uint16_t)value, false);
        if (writeback) c->r[rn] = moved;
    }
    /* sh 2/3 with L = 0 are ARMv5 LDRD/STRD: no effect on the ARM7TDMI. */
}

static void arm_swap(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF, rm = op & 0xF;
    uint32_t addr = c->r[rn], source = c->r[rm], loaded;
    if (BIT(op, 22)) {
        loaded = bus_read8(g, addr, false);
        bus_write8(g, addr, (uint8_t)source, false);
    } else {
        loaded = cpu_load_word(g, addr);
        bus_write32(g, addr, source, false);
    }
    bus_idle(g, 1);
    c->r[rd] = loaded;
}
```

- [x] **Step 5: Replace `arm_classify` in `core/src/cpu/arm.c`**

```c
/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_multiply;
    if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_multiply_long;
    if ((hi & 0xFB) == 0x10 && lo == 0x9) return arm_swap;
    if ((hi & 0xE0) == 0x00 && lo == 0x9) return arm_undefined;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_halfword_transfer; /* lo = 1011, 1101, 1111 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return arm_data_processing;
    if ((hi & 0xE0) == 0x60 && (lo & 0x1)) return arm_undefined;
    if ((hi & 0xC0) == 0x40) return arm_single_transfer;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined;
}
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 7 new transfer tests.

- [x] **Step 7: Commit**

```bash
git add core/src/cpu tests/unit/test_arm_mem.c
git commit -m "feat(cpu): implement ARM LDR/STR, halfword/signed transfers and SWP" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: ARM block transfer (LDM/STM)

**Files:**
- Modify: `core/src/cpu/arm.c`
- Test: `tests/unit/test_arm_block.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_arm_block.c`**

```c
#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(arm_stm_ldm_with_writeback) {
    const uint32_t ops[] = {
        0xE8A0000E, /* stmia r0!, {r1-r3} */
        0xE9300070, /* ldmdb r0!, {r4-r6} */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x100;
    g->cpu.r[1] = 1;
    g->cpu.r[2] = 2;
    g->cpu.r[3] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 0x10C);
    CHECK_EQ(bus_read32(g, IW + 0x108, false), 3);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], IW + 0x100);
    CHECK_EQ(g->cpu.r[4], 1);
    CHECK_EQ(g->cpu.r[5], 2);
    CHECK_EQ(g->cpu.r[6], 3);
    gba_destroy(g);
}

TEST(arm_stmfd_ldmfd_stack) {
    const uint32_t ops[] = {
        0xE92D4001, /* stmfd sp!, {r0, lr} */
        0xE8BD0006, /* ldmfd sp!, {r1, r2} */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 7;
    g->cpu.r[14] = 9;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->cpu.r[1], 7);
    CHECK_EQ(g->cpu.r[2], 9);
    gba_destroy(g);
}

TEST(arm_ldm_loads_pc) {
    const uint32_t ops[] = {0xE8908000}; /* ldmia r0, {pc} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x200;
    bus_write32(g, IW + 0x200, 0x08000040, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000040);
    gba_destroy(g);
}

TEST(arm_empty_rlist_store_writes_pc_and_moves_base_by_64) {
    const uint32_t ops[] = {0xE8A00000}; /* stmia r0!, {} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x300;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x300, false), 0x0800000C);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}

TEST(arm_empty_rlist_load_branches) {
    const uint32_t ops[] = {0xE8B00000}; /* ldmia r0!, {} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x300;
    bus_write32(g, IW + 0x300, 0x08000080, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000080);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}

TEST(arm_stm_base_in_list) {
    const uint32_t not_first[] = {0xE8A1000F}; /* stmia r1!, {r0-r3} */
    gba_t *g = tu_arm(not_first, TU_LEN(not_first));
    g->cpu.r[1] = IW + 0x400;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x404, false), IW + 0x410); /* the written-back base */
    gba_destroy(g);

    const uint32_t first[] = {0xE8A00003}; /* stmia r0!, {r0, r1} */
    g = tu_arm(first, TU_LEN(first));
    g->cpu.r[0] = IW + 0x500;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x500, false), IW + 0x500); /* the original base */
    gba_destroy(g);
}

TEST(arm_ldm_base_in_list_keeps_loaded_value) {
    const uint32_t ops[] = {0xE8B10006}; /* ldmia r1!, {r1, r2} */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = IW + 0x600;
    bus_write32(g, IW + 0x600, 0xAA, false);
    bus_write32(g, IW + 0x604, 0xBB, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[1], 0xAA);
    CHECK_EQ(g->cpu.r[2], 0xBB);
    gba_destroy(g);
}

TEST(arm_stm_user_bank) {
    const uint32_t ops[] = {0xE8C00100}; /* stmia r0, {r8}^ */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x700;
    g->cpu.r[8] = 0x55;
    cpu_write_cpsr(&g->cpu, MODE_FIQ);
    g->cpu.r[8] = 0x66;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x700, false), 0x55);
    CHECK_EQ(g->cpu.r[8], 0x66);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_FIQ);
    gba_destroy(g);
}

TEST(arm_ldm_pc_with_s_restores_cpsr) {
    const uint32_t ops[] = {0xE8D08000}; /* ldmia r0, {pc}^ */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = IW + 0x800;
    bus_write32(g, IW + 0x800, 0x08000051, false);
    cpu_write_cpsr(&g->cpu, MODE_IRQ);
    g->cpu.spsr[BANK_IRQ] = MODE_SYS | CPSR_T;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS | CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x08000050);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit arm_`
Expected: the 9 new tests FAIL (block transfers are still undefined).

- [x] **Step 3: Add the handler to `core/src/cpu/arm.c`** (above `arm_classify`)

```c
static void arm_block_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF;
    uint32_t rlist = op & 0xFFFF;
    bool pre = BIT(op, 24), up = BIT(op, 23), psr = BIT(op, 22), wb = BIT(op, 21), load = BIT(op, 20);

    /* ARMv4 quirk: an empty list transfers only PC but moves the base as if 16 registers were listed. */
    uint32_t count = (uint32_t)__builtin_popcount(rlist);
    if (rlist == 0) {
        rlist = 1u << 15;
        count = 16;
    }

    uint32_t base = c->r[rn];
    uint32_t bytes = count * 4;
    uint32_t new_base = up ? base + bytes : base - bytes;
    uint32_t addr = up ? base + (pre ? 4 : 0) : base - bytes + (pre ? 0 : 4);

    /* The S bit without a PC load transfers the user-mode registers. */
    bool user_bank = psr && !(load && (rlist & 0x8000));
    uint32_t mode = c->cpsr & CPSR_MODE;
    bool seq = false;

    if (load) {
        if (wb) c->r[rn] = new_base; /* a loaded base register overrides the writeback */
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 15; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        bool load_pc = (rlist & 0x8000) != 0;
        uint32_t pc = load_pc ? bus_read32(g, addr, seq) : 0;
        bus_idle(g, 1);
        if (load_pc) {
            if (psr && cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
            cpu_branch(c, pc & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        }
    } else {
        /* A stored base is the original value only when it is the lowest register listed. */
        bool base_first = (rlist & ((1u << rn) - 1)) == 0;
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value;
            if (i == 15) value = c->r[15] + 4; /* STM PC stores address + 12 */
            else if (i == rn) value = (base_first || !wb) ? base : new_base;
            else value = c->r[i];
            bus_write32(g, addr, value, seq);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        if (wb) c->r[rn] = new_base;
    }
}
```

- [x] **Step 4: Replace `arm_classify` in `core/src/cpu/arm.c`** (final form)

```c
/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_multiply;
    if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_multiply_long;
    if ((hi & 0xFB) == 0x10 && lo == 0x9) return arm_swap;
    if ((hi & 0xE0) == 0x00 && lo == 0x9) return arm_undefined;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_halfword_transfer; /* lo = 1011, 1101, 1111 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return arm_data_processing;
    if ((hi & 0xE0) == 0x60 && (lo & 0x1)) return arm_undefined;
    if ((hi & 0xC0) == 0x40) return arm_single_transfer;
    if ((hi & 0xE0) == 0x80) return arm_block_transfer;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined; /* coprocessor space: the GBA has no coprocessors */
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 9 block-transfer tests.

- [x] **Step 6: Commit**

```bash
git add core/src/cpu/arm.c tests/unit/test_arm_block.c
git commit -m "feat(cpu): implement ARM LDM/STM including ARMv4 edge cases" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Thumb formats 1–5 (shifts, add/sub, immediates, ALU, high registers/BX)

**Files:**
- Modify: `core/src/cpu/thumb.c` (full replacement)
- Test: `tests/unit/test_thumb_alu.c`

Format numbers follow the ARM7TDMI data sheet ("THUMB 1" … "THUMB 19"), which the jsmolka tests also use.

- [x] **Step 1: Write the failing tests `tests/unit/test_thumb_alu.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(thumb_shift_and_add_sub_immediates) {
    const uint16_t ops[] = {
        0x2005, /* movs r0, #5 */
        0x0081, /* lsls r1, r0, #2 */
        0x0842, /* lsrs r2, r0, #1 */
        0x1003, /* asrs r3, r0, #32 */
        0x1844, /* adds r4, r0, r1 */
        0x1F85, /* subs r5, r0, #6 */
        0x2805, /* cmp  r0, #5 */
        0x30FA, /* adds r0, #250 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 3);
    CHECK_EQ(c->r[1], 20);
    CHECK_EQ(c->r[2], 2);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[3], 0);
    CHECK(!(c->cpsr & CPSR_C));
    CHECK(c->cpsr & CPSR_Z);
    tu_steps(g, 2);
    CHECK_EQ(c->r[4], 25);
    CHECK_EQ(c->r[5], 0xFFFFFFFFu);
    CHECK(c->cpsr & CPSR_N);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 1);
    CHECK(c->cpsr & CPSR_Z);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[0], 255);
    CHECK_EQ(c->r[15], 0x08000010);
    gba_destroy(g);
}

TEST(thumb_alu_logical_and_multiply) {
    const uint16_t ops[] = {
        0x200C, /* movs r0, #12 */
        0x210A, /* movs r1, #10 */
        0x1C02, /* adds r2, r0, #0 */
        0x400A, /* ands r2, r1 */
        0x1C03, /* adds r3, r0, #0 */
        0x404B, /* eors r3, r1 */
        0x1C04, /* adds r4, r0, #0 */
        0x430C, /* orrs r4, r1 */
        0x1C05, /* adds r5, r0, #0 */
        0x438D, /* bics r5, r1 */
        0x43CE, /* mvns r6, r1 */
        0x424F, /* negs r7, r1 */
        0x4348, /* muls r0, r1 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 13);
    CHECK_EQ(c->r[2], 8);
    CHECK_EQ(c->r[3], 6);
    CHECK_EQ(c->r[4], 14);
    CHECK_EQ(c->r[5], 4);
    CHECK_EQ(c->r[6], 0xFFFFFFF5u);
    CHECK_EQ(c->r[7], 0xFFFFFFF6u);
    CHECK_EQ(c->r[0], 120);
    gba_destroy(g);
}

TEST(thumb_register_shifts) {
    const uint16_t ops[] = {
        0x2001, /* movs r0, #1 */
        0x2121, /* movs r1, #33 */
        0x4088, /* lsls r0, r1 */
        0x2280, /* movs r2, #0x80 */
        0x2304, /* movs r3, #4 */
        0x41DA, /* rors r2, r3 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 3);
    CHECK_EQ(c->r[0], 0);
    CHECK(c->cpsr & CPSR_Z);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 3);
    CHECK_EQ(c->r[2], 0x08);
    CHECK(!(c->cpsr & CPSR_C));
    gba_destroy(g);
}

TEST(thumb_adc_sbc_cmn_tst) {
    const uint16_t ops[] = {
        0x2000, /* movs r0, #0 */
        0x3801, /* subs r0, #1 */
        0x2105, /* movs r1, #5 */
        0x4141, /* adcs r1, r0 */
        0x4181, /* sbcs r1, r0 */
        0x42C0, /* cmn  r0, r0 */
        0x4208, /* tst  r0, r1 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    tu_steps(g, 4);
    CHECK_EQ(c->r[1], 4);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK_EQ(c->r[1], 5);
    CHECK(!(c->cpsr & CPSR_C));
    tu_steps(g, 1);
    CHECK(c->cpsr & CPSR_N);
    CHECK(c->cpsr & CPSR_C);
    tu_steps(g, 1);
    CHECK(!(c->cpsr & CPSR_N));
    CHECK(!(c->cpsr & CPSR_Z));
    CHECK(c->cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(thumb_high_register_ops) {
    const uint16_t ops[] = {
        0x4680, /* mov r8, r0 */
        0x4488, /* add r8, r1 */
        0x4580, /* cmp r8, r0 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 3;
    g->cpu.r[1] = 4;
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[8], 7);
    CHECK(g->cpu.cpsr & CPSR_C);
    CHECK(!(g->cpu.cpsr & CPSR_Z));
    gba_destroy(g);
}

TEST(thumb_mov_pc_stays_in_thumb) {
    const uint16_t ops[] = {0x4687}; /* mov pc, r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000021;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    CHECK(g->cpu.cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_add_pc_aligns) {
    const uint16_t ops[] = {0x4487}; /* add pc, r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    gba_destroy(g);
}

TEST(thumb_bx_to_arm) {
    const uint16_t ops[] = {0x4700}; /* bx r0 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x08000102;
    tu_steps(g, 1);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[15], 0x08000100);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit thumb_`
Expected: the 8 tests FAIL (all Thumb instructions are undefined).

- [x] **Step 3: Replace `core/src/cpu/thumb.c`**

```c
#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "gba_internal.h"

typedef void (*thumb_handler_t)(gba_t *g, uint16_t op);

static thumb_handler_t thumb_table[1024];

static void set_nzcv(arm_cpu_t *c, alu_result_t r) {
    cpu_set_nz(c, r.value);
    cpu_set_c(c, r.carry);
    cpu_set_v(c, r.overflow);
}

static void thumb_undefined(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 2);
}

/* THUMB 1: LSL/LSR/ASR Rd, Rs, #imm5 */
static void thumb_shift_imm(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    bool carry = cpu_carry(c);
    uint32_t result = shift_by_imm((op >> 11) & 3u, c->r[(op >> 3) & 7], (op >> 6) & 0x1Fu, &carry);
    c->r[op & 7] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry);
}

/* THUMB 2: ADD/SUB Rd, Rs, Rn|#imm3 */
static void thumb_add_sub(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t a = c->r[(op >> 3) & 7];
    uint32_t b = (op & (1u << 10)) ? (op >> 6) & 7u : c->r[(op >> 6) & 7];
    alu_result_t r = (op & (1u << 9)) ? alu_add(a, ~b, 1) : alu_add(a, b, 0);
    c->r[op & 7] = r.value;
    set_nzcv(c, r);
}

/* THUMB 3: MOV/CMP/ADD/SUB Rd, #imm8 */
static void thumb_imm(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7, imm = op & 0xFFu;
    alu_result_t r;
    switch ((op >> 11) & 3) {
    case 0:
        c->r[rd] = imm;
        cpu_set_nz(c, imm);
        return;
    case 1: set_nzcv(c, alu_add(c->r[rd], ~imm, 1)); return;
    case 2: r = alu_add(c->r[rd], imm, 0); break;
    default: r = alu_add(c->r[rd], ~imm, 1); break;
    }
    c->r[rd] = r.value;
    set_nzcv(c, r);
}

/* THUMB 4: ALU operations on low registers */
static void thumb_alu(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t a = c->r[rd], b = c->r[(op >> 3) & 7];
    bool carry = cpu_carry(c);
    uint32_t result;
    alu_result_t r;
    switch ((op >> 6) & 0xF) {
    case 0x0: result = a & b; break; /* AND */
    case 0x1: result = a ^ b; break; /* EOR */
    case 0x2: result = shift_by_reg(0, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSL */
    case 0x3: result = shift_by_reg(1, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSR */
    case 0x4: result = shift_by_reg(2, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ASR */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return; /* SBC */
    case 0x7: result = shift_by_reg(3, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ROR */
    case 0x8: cpu_set_nz(c, a & b); return;                                        /* TST */
    case 0x9: r = alu_add(0, ~b, 1); c->r[rd] = r.value; set_nzcv(c, r); return;   /* NEG */
    case 0xA: set_nzcv(c, alu_add(a, ~b, 1)); return;                              /* CMP */
    case 0xB: set_nzcv(c, alu_add(a, b, 0)); return;                               /* CMN */
    case 0xC: result = a | b; break;                                               /* ORR */
    case 0xD: result = a * b; bus_idle(g, alu_mul_cycles(a, true)); break;         /* MUL: C, V kept */
    case 0xE: result = a & ~b; break;                                              /* BIC */
    default: result = ~b; break;                                                   /* MVN */
    }
    c->r[rd] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry); /* unchanged unless a shift produced a carry-out */
}

/* THUMB 5: ADD/CMP/MOV on high registers, and BX */
static void thumb_hi_reg(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op & 7u) | ((op >> 4) & 8u); /* H1 is bit 7 */
    uint32_t value = c->r[(op >> 3) & 0xF];     /* H2 + Rs are bits 6-3 */
    switch ((op >> 8) & 3) {
    case 0: {
        uint32_t result = c->r[rd] + value;
        if (rd == 15) cpu_branch(c, result & ~1u);
        else c->r[rd] = result;
        break;
    }
    case 1: set_nzcv(c, alu_add(c->r[rd], ~value, 1)); break;
    case 2:
        if (rd == 15) cpu_branch(c, value & ~1u);
        else c->r[rd] = value;
        break;
    default:
        if (value & 1) {
            cpu_branch(c, value & ~1u);
        } else {
            c->cpsr &= ~CPSR_T;
            cpu_branch(c, value & ~3u);
        }
        break;
    }
}

/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    uint32_t op = idx << 6;
    if ((op & 0xF800) == 0x1800) return thumb_add_sub;   /* 00011 */
    if ((op & 0xE000) == 0x0000) return thumb_shift_imm; /* 000xx */
    if ((op & 0xE000) == 0x2000) return thumb_imm;       /* 001xx */
    if ((op & 0xFC00) == 0x4000) return thumb_alu;       /* 010000 */
    if ((op & 0xFC00) == 0x4400) return thumb_hi_reg;    /* 010001 */
    return thumb_undefined;
}

void thumb_init_table(void) {
    for (uint32_t i = 0; i < 1024; i++) thumb_table[i] = thumb_classify(i);
}

void thumb_execute(gba_t *g, uint16_t op) {
    thumb_table[op >> 6](g, op);
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 8 `thumb_*` tests.

- [x] **Step 5: Commit**

```bash
git add core/src/cpu/thumb.c tests/unit/test_thumb_alu.c
git commit -m "feat(cpu): implement Thumb formats 1-5" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Thumb formats 6–15 (loads/stores, address generation, push/pop, LDM/STM)

**Files:**
- Modify: `core/src/cpu/thumb.c`
- Test: `tests/unit/test_thumb_mem.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_thumb_mem.c`**

```c
#include "test.h"
#include "testutil.h"

#define IW 0x03000000u

TEST(thumb_pc_relative_load_uses_word_aligned_pc) {
    const uint16_t ops[] = {
        0x46C0, /* nop (mov r8, r8) */
        0x4901, /* ldr r1, [pc, #4] */
        0x0000,
        0x0000,
        0x5678, /* literal at 0x08000008 */
        0x1234,
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0x12345678);
    gba_destroy(g);
}

TEST(thumb_register_offset_transfers) {
    const uint16_t ops[] = {
        0x50C2, /* str   r2, [r0, r3] */
        0x58C4, /* ldr   r4, [r0, r3] */
        0x54C2, /* strb  r2, [r0, r3] */
        0x5CC5, /* ldrb  r5, [r0, r3] */
        0x52C2, /* strh  r2, [r0, r3] */
        0x5AC6, /* ldrh  r6, [r0, r3] */
        0x56C7, /* ldrsb r7, [r0, r3] */
        0x5EC4, /* ldrsh r4, [r0, r3] */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW;
    c->r[3] = 4;
    c->r[2] = 0x12348281;
    tu_steps(g, 2);
    CHECK_EQ(c->r[4], 0x12348281);
    tu_steps(g, 5);
    CHECK_EQ(c->r[5], 0x81);
    CHECK_EQ(c->r[6], 0x8281);
    CHECK_EQ(c->r[7], 0xFFFFFF81u);
    tu_steps(g, 1);
    CHECK_EQ(c->r[4], 0xFFFF8281u);
    gba_destroy(g);
}

TEST(thumb_misaligned_word_load_rotates) {
    const uint16_t ops[] = {0x58C4}; /* ldr r4, [r0, r3] */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = IW;
    g->cpu.r[3] = 1;
    bus_write32(g, IW, 0x11223344, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[4], 0x44112233);
    gba_destroy(g);
}

TEST(thumb_immediate_offset_transfers) {
    const uint16_t ops[] = {
        0x6041, /* str  r1, [r0, #4] */
        0x6842, /* ldr  r2, [r0, #4] */
        0x7241, /* strb r1, [r0, #9] */
        0x7A43, /* ldrb r3, [r0, #9] */
        0x8181, /* strh r1, [r0, #12] */
        0x8984, /* ldrh r4, [r0, #12] */
        0x89B5, /* ldrh r5, [r6, #12] (r6 odd: rotated) */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW;
    c->r[1] = 0xAABBCCDD;
    c->r[6] = IW + 1;
    tu_steps(g, 7);
    CHECK_EQ(c->r[2], 0xAABBCCDD);
    CHECK_EQ(c->r[3], 0xDD);
    CHECK_EQ(c->r[4], 0xCCDD);
    CHECK_EQ(c->r[5], 0xDD0000CC);
    gba_destroy(g);
}

TEST(thumb_sp_relative_and_address_generation) {
    const uint16_t ops[] = {
        0x9102, /* str r1, [sp, #8] */
        0x9A02, /* ldr r2, [sp, #8] */
        0xA302, /* add r3, pc, #8 */
        0xAC04, /* add r4, sp, #16 */
        0xB082, /* sub sp, #8 */
        0xB002, /* add sp, #8 */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[1] = 77;
    tu_steps(g, 4);
    CHECK_EQ(c->r[2], 77);
    CHECK_EQ(c->r[3], 0x08000010);
    CHECK_EQ(c->r[4], 0x03007F10);
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007EF8);
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007F00);
    gba_destroy(g);
}

TEST(thumb_push_and_pop_pc) {
    const uint16_t ops[] = {
        0xB503, /* push {r0, r1, lr} */
        0xBD0C, /* pop  {r2, r3, pc} */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = 1;
    c->r[1] = 2;
    c->r[14] = 0x08000041;
    tu_steps(g, 1);
    CHECK_EQ(c->r[13], 0x03007EF4);
    CHECK_EQ(bus_read32(g, 0x03007EF4, false), 1);
    CHECK_EQ(bus_read32(g, 0x03007EFC, false), 0x08000041);
    tu_steps(g, 1);
    CHECK_EQ(c->r[2], 1);
    CHECK_EQ(c->r[3], 2);
    CHECK_EQ(c->r[15], 0x08000040);
    CHECK_EQ(c->r[13], 0x03007F00);
    CHECK(c->cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_ldmia_stmia) {
    const uint16_t ops[] = {
        0xC006, /* stmia r0!, {r1, r2} */
        0x3808, /* subs  r0, #8 */
        0xC818, /* ldmia r0!, {r3, r4} */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    arm_cpu_t *c = &g->cpu;
    c->r[0] = IW + 0x100;
    c->r[1] = 5;
    c->r[2] = 6;
    tu_steps(g, 3);
    CHECK_EQ(c->r[3], 5);
    CHECK_EQ(c->r[4], 6);
    CHECK_EQ(c->r[0], IW + 0x108);
    gba_destroy(g);
}

TEST(thumb_ldmia_base_in_list_keeps_loaded_value) {
    const uint16_t ops[] = {0xC906}; /* ldmia r1!, {r1, r2} */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[1] = IW + 0x200;
    bus_write32(g, IW + 0x200, 0x77, false);
    bus_write32(g, IW + 0x204, 0x88, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[1], 0x77);
    CHECK_EQ(g->cpu.r[2], 0x88);
    gba_destroy(g);
}

TEST(thumb_empty_rlist_quirks) {
    const uint16_t store[] = {0xC000}; /* stmia r0!, {} */
    gba_t *g = tu_thumb(store, TU_LEN(store));
    g->cpu.r[0] = IW + 0x300;
    tu_steps(g, 1);
    CHECK_EQ(bus_read32(g, IW + 0x300, false), 0x08000006);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);

    const uint16_t load[] = {0xC800}; /* ldmia r0!, {} */
    g = tu_thumb(load, TU_LEN(load));
    g->cpu.r[0] = IW + 0x300;
    bus_write32(g, IW + 0x300, 0x08000081, false);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000080);
    CHECK_EQ(g->cpu.r[0], IW + 0x340);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit thumb_`
Expected: the 9 new tests FAIL.

- [x] **Step 3: Add `#include "cpu/cpu_mem.h"` to the includes of `core/src/cpu/thumb.c`**, then add these handlers above `thumb_classify`:

```c
/* THUMB 6: LDR Rd, [PC, #imm8*4] — PC is word-aligned first */
static void thumb_pc_load(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t addr = (c->r[15] & ~2u) + (op & 0xFFu) * 4;
    c->r[(op >> 8) & 7] = bus_read32(g, addr, false);
    bus_idle(g, 1);
}

/* THUMB 7: STR/STRB/LDR/LDRB Rd, [Rb, Ro] */
static void thumb_reg_offset(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = L, bit 10 = B */
    case 0: bus_write32(g, addr, c->r[rd], false); break;
    case 1: bus_write8(g, addr, (uint8_t)c->r[rd], false); break;
    case 2: c->r[rd] = cpu_load_word(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = bus_read8(g, addr, false); bus_idle(g, 1); break;
    }
}

/* THUMB 8: STRH/LDSB/LDRH/LDSH Rd, [Rb, Ro] */
static void thumb_reg_offset_signed(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = H, bit 10 = S */
    case 0: bus_write16(g, addr, (uint16_t)c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_signed_byte(g, addr); bus_idle(g, 1); break;
    case 2: c->r[rd] = cpu_load_half(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = cpu_load_signed_half(g, addr); bus_idle(g, 1); break;
    }
}

/* THUMB 9: STR/LDR/STRB/LDRB Rd, [Rb, #imm5] */
static void thumb_imm_offset(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7, base = c->r[(op >> 3) & 7], imm = (op >> 6) & 0x1Fu;
    switch ((op >> 11) & 3) { /* bit 12 = B, bit 11 = L */
    case 0: bus_write32(g, base + imm * 4, c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_word(g, base + imm * 4); bus_idle(g, 1); break;
    case 2: bus_write8(g, base + imm, (uint8_t)c->r[rd], false); break;
    default: c->r[rd] = bus_read8(g, base + imm, false); bus_idle(g, 1); break;
    }
}

/* THUMB 10: STRH/LDRH Rd, [Rb, #imm5*2] */
static void thumb_half_imm(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + ((op >> 6) & 0x1Fu) * 2;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_half(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write16(g, addr, (uint16_t)c->r[rd], false);
    }
}

/* THUMB 11: STR/LDR Rd, [SP, #imm8*4] */
static void thumb_sp_relative(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7;
    uint32_t addr = c->r[13] + (op & 0xFFu) * 4;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_word(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write32(g, addr, c->r[rd], false);
    }
}

/* THUMB 12: ADD Rd, PC|SP, #imm8*4 */
static void thumb_load_address(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t base = (op & (1u << 11)) ? c->r[13] : (c->r[15] & ~2u);
    c->r[(op >> 8) & 7] = base + (op & 0xFFu) * 4;
}

/* THUMB 13: ADD SP, #±imm7*4 */
static void thumb_sp_adjust(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (op & 0x7Fu) * 4;
    c->r[13] = (op & 0x80) ? c->r[13] - offset : c->r[13] + offset;
}

/* THUMB 14: PUSH {rlist, LR} / POP {rlist, PC}. SP keeps any misalignment; accesses are aligned. */
static void thumb_push_pop(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rlist = op & 0xFFu;
    bool extra = (op & (1u << 8)) != 0; /* LR for PUSH, PC for POP */
    uint32_t count = (uint32_t)__builtin_popcount(rlist) + (extra ? 1 : 0);
    bool seq = false;

    if (op & (1u << 11)) { /* POP = LDMIA sp! */
        uint32_t addr = c->r[13];
        if (count == 0) { /* ARMv4 empty-list quirk: loads PC, SP moves by 0x40 */
            uint32_t pc = bus_read32(g, addr, false);
            c->r[13] = addr + 0x40;
            cpu_branch(c, pc & ~1u);
            return;
        }
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq);
            seq = true;
            addr += 4;
        }
        if (extra) {
            uint32_t pc = bus_read32(g, addr, seq);
            addr += 4;
            cpu_branch(c, pc & ~1u); /* ARMv4: POP {pc} does not change state */
        }
        c->r[13] = addr;
        bus_idle(g, 1);
    } else { /* PUSH = STMDB sp! */
        if (count == 0) {
            c->r[13] -= 0x40;
            bus_write32(g, c->r[13], c->r[15] + 2, false);
            return;
        }
        uint32_t addr = c->r[13] - count * 4;
        c->r[13] = addr;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            bus_write32(g, addr, c->r[i], seq);
            seq = true;
            addr += 4;
        }
        if (extra) bus_write32(g, addr, c->r[14], seq);
    }
}

/* THUMB 15: STMIA/LDMIA Rb!, {rlist} */
static void thumb_block(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rb = (op >> 8) & 7, rlist = op & 0xFFu;
    uint32_t base = c->r[rb], addr = base;
    bool load = (op & (1u << 11)) != 0;
    bool seq = false;

    if (rlist == 0) { /* ARMv4 quirk: transfers PC, base moves by 0x40 */
        if (load) {
            uint32_t pc = bus_read32(g, addr, false);
            c->r[rb] = base + 0x40;
            cpu_branch(c, pc & ~1u);
        } else {
            bus_write32(g, addr, c->r[15] + 2, false); /* instruction address + 6 */
            c->r[rb] = base + 0x40;
        }
        return;
    }

    uint32_t new_base = base + (uint32_t)__builtin_popcount(rlist) * 4;
    if (load) {
        c->r[rb] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq);
            seq = true;
            addr += 4;
        }
        bus_idle(g, 1);
    } else {
        bool base_first = (rlist & ((1u << rb) - 1)) == 0;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value = (i == rb) ? (base_first ? base : new_base) : c->r[i];
            bus_write32(g, addr, value, seq);
            seq = true;
            addr += 4;
        }
        c->r[rb] = new_base;
    }
}
```

- [x] **Step 4: Replace `thumb_classify` in `core/src/cpu/thumb.c`**

```c
/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    uint32_t op = idx << 6;
    if ((op & 0xF800) == 0x1800) return thumb_add_sub;           /* 00011 */
    if ((op & 0xE000) == 0x0000) return thumb_shift_imm;         /* 000xx */
    if ((op & 0xE000) == 0x2000) return thumb_imm;               /* 001xx */
    if ((op & 0xFC00) == 0x4000) return thumb_alu;               /* 010000 */
    if ((op & 0xFC00) == 0x4400) return thumb_hi_reg;            /* 010001 */
    if ((op & 0xF800) == 0x4800) return thumb_pc_load;           /* 01001 */
    if ((op & 0xF200) == 0x5000) return thumb_reg_offset;        /* 0101xx0 */
    if ((op & 0xF200) == 0x5200) return thumb_reg_offset_signed; /* 0101xx1 */
    if ((op & 0xE000) == 0x6000) return thumb_imm_offset;        /* 011xx */
    if ((op & 0xF000) == 0x8000) return thumb_half_imm;          /* 1000x */
    if ((op & 0xF000) == 0x9000) return thumb_sp_relative;       /* 1001x */
    if ((op & 0xF000) == 0xA000) return thumb_load_address;      /* 1010x */
    if ((op & 0xFF00) == 0xB000) return thumb_sp_adjust;         /* 10110000 */
    if ((op & 0xF600) == 0xB400) return thumb_push_pop;          /* 1011x10x */
    if ((op & 0xF000) == 0xC000) return thumb_block;             /* 1100x */
    return thumb_undefined;
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 9 `thumb_*` memory tests.

- [x] **Step 6: Commit**

```bash
git add core/src/cpu/thumb.c tests/unit/test_thumb_mem.c
git commit -m "feat(cpu): implement Thumb formats 6-15" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Thumb formats 16–19 (conditional branch, SWI, branch, long branch with link)

**Files:**
- Modify: `core/src/cpu/thumb.c`
- Test: `tests/unit/test_thumb_branch.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_thumb_branch.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(thumb_conditional_branch) {
    const uint16_t ops[] = {0xD00E}; /* beq .+0x20 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.cpsr |= CPSR_Z;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000020);
    gba_destroy(g);

    g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x08000002);
    gba_destroy(g);
}

TEST(thumb_unconditional_branch) {
    const uint16_t fwd[] = {0xE01C}; /* b .+0x3C */
    gba_t *g = tu_thumb(fwd, TU_LEN(fwd));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x0800003C);
    gba_destroy(g);

    const uint16_t back[] = {0x46C0, 0xE7FD}; /* nop; b .-2 */
    g = tu_thumb(back, TU_LEN(back));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    gba_destroy(g);
}

TEST(thumb_long_branch_with_link) {
    const uint16_t ops[] = {0xF000, 0xFFFB}; /* bl .+0xFFA */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[15], 0x08000FFA);
    CHECK_EQ(g->cpu.r[14], 0x08000005);
    CHECK(g->cpu.cpsr & CPSR_T);
    gba_destroy(g);
}

TEST(thumb_swi_enters_arm_supervisor_mode) {
    const uint16_t ops[] = {0xDF06}; /* swi 6 */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SVC);
    CHECK(!(g->cpu.cpsr & CPSR_T));
    CHECK_EQ(g->cpu.r[14], 0x08000002);
    CHECK_EQ(g->cpu.spsr[BANK_SVC], MODE_SYS | CPSR_T);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit thumb_`
Expected: the 4 new tests FAIL.

- [x] **Step 3: Add the handlers to `core/src/cpu/thumb.c`** (above `thumb_classify`)

```c
/* THUMB 16: B<cond> label */
static void thumb_cond_branch(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    if (!cpu_cond_passed(c->cpsr, (op >> 8) & 0xFu)) return;
    int32_t offset = (int8_t)(op & 0xFF) * 2;
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 17: SWI */
static void thumb_swi(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 2);
}

/* THUMB 18: B label */
static void thumb_branch(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 20; /* sign-extended offset11 * 2 */
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 19, first half: LR = PC + (offset11 << 12) */
static void thumb_bl_high(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 9;
    c->r[14] = c->r[15] + (uint32_t)offset;
}

/* THUMB 19, second half: branch to LR + (offset11 << 1); LR = next instruction | 1 */
static void thumb_bl_low(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[14] + (op & 0x7FFu) * 2;
    c->r[14] = (c->r[15] - 2) | 1;
    cpu_branch(c, target & ~1u);
}
```

- [x] **Step 4: Replace `thumb_classify` in `core/src/cpu/thumb.c`** (final form)

```c
/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    uint32_t op = idx << 6;
    if ((op & 0xF800) == 0x1800) return thumb_add_sub;           /* 00011 */
    if ((op & 0xE000) == 0x0000) return thumb_shift_imm;         /* 000xx */
    if ((op & 0xE000) == 0x2000) return thumb_imm;               /* 001xx */
    if ((op & 0xFC00) == 0x4000) return thumb_alu;               /* 010000 */
    if ((op & 0xFC00) == 0x4400) return thumb_hi_reg;            /* 010001 */
    if ((op & 0xF800) == 0x4800) return thumb_pc_load;           /* 01001 */
    if ((op & 0xF200) == 0x5000) return thumb_reg_offset;        /* 0101xx0 */
    if ((op & 0xF200) == 0x5200) return thumb_reg_offset_signed; /* 0101xx1 */
    if ((op & 0xE000) == 0x6000) return thumb_imm_offset;        /* 011xx */
    if ((op & 0xF000) == 0x8000) return thumb_half_imm;          /* 1000x */
    if ((op & 0xF000) == 0x9000) return thumb_sp_relative;       /* 1001x */
    if ((op & 0xF000) == 0xA000) return thumb_load_address;      /* 1010x */
    if ((op & 0xFF00) == 0xB000) return thumb_sp_adjust;         /* 10110000 */
    if ((op & 0xF600) == 0xB400) return thumb_push_pop;          /* 1011x10x */
    if ((op & 0xF000) == 0xC000) return thumb_block;             /* 1100x */
    if ((op & 0xFF00) == 0xDF00) return thumb_swi;               /* 11011111 */
    if ((op & 0xFF00) == 0xDE00) return thumb_undefined;         /* 11011110 */
    if ((op & 0xF000) == 0xD000) return thumb_cond_branch;       /* 1101 */
    if ((op & 0xF800) == 0xE000) return thumb_branch;            /* 11100 */
    if ((op & 0xF800) == 0xF000) return thumb_bl_high;           /* 11110 */
    if ((op & 0xF800) == 0xF800) return thumb_bl_low;            /* 11111 */
    return thumb_undefined; /* 11101 (BLX on ARMv5) and unused 1011 encodings */
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 4 Thumb branch tests.

- [x] **Step 6: Commit**

```bash
git add core/src/cpu/thumb.c tests/unit/test_thumb_branch.c
git commit -m "feat(cpu): implement Thumb branches, BL and SWI" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: HLE BIOS arithmetic and affine SWIs

**Files:**
- Create: `core/src/bios/hle_internal.h`, `core/src/bios/hle_math.c`
- Test: `tests/unit/test_hle_math.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_hle_math.c`**

```c
#include <stdint.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"

TEST(hle_div_signed_results) {
    gba_t *g = gba_create(NULL);
    hle_div(g, 100, 7);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK_EQ(g->cpu.r[1], 2);
    CHECK_EQ(g->cpu.r[3], 14);
    hle_div(g, -100, 7);
    CHECK_EQ(g->cpu.r[0], 0xFFFFFFF2u);
    CHECK_EQ(g->cpu.r[1], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[3], 14);
    hle_div(g, INT32_MIN, -1);
    CHECK_EQ(g->cpu.r[0], 0x80000000u);
    CHECK_EQ(g->cpu.r[1], 0);
    CHECK_EQ(g->cpu.r[3], 0x80000000u);
    hle_div(g, 5, 0);
    CHECK_EQ(g->cpu.r[0], 1);
    CHECK_EQ(g->cpu.r[1], 5);
    CHECK_EQ(g->cpu.r[3], 1);
    gba_destroy(g);
}

TEST(hle_sqrt_integer_root) {
    CHECK_EQ(hle_sqrt(0), 0);
    CHECK_EQ(hle_sqrt(2), 1);
    CHECK_EQ(hle_sqrt(0x10000), 0x100);
    CHECK_EQ(hle_sqrt(0xFFFFFFFF), 0xFFFF);
}

TEST(hle_arctan_matches_bios_polynomial) {
    CHECK_EQ(hle_arctan(0), 0);
    CHECK_EQ(hle_arctan(0x1000), 2555);
    CHECK_EQ(hle_arctan(0x2000), 4836);
    CHECK_EQ(hle_arctan(0x4000), 0x2000);
    CHECK_EQ(hle_arctan(-0x4000), -0x2000);
}

TEST(hle_arctan2_quadrants) {
    CHECK_EQ(hle_arctan2(0x100, 0x100), 0x2000);
    CHECK_EQ(hle_arctan2(-0x100, 0x100), 0x6000);
    CHECK_EQ(hle_arctan2(-0x100, -0x100), 0xA000);
    CHECK_EQ(hle_arctan2(0x100, -0x100), 0xE000);
    CHECK_EQ(hle_arctan2(5, 0), 0);
    CHECK_EQ(hle_arctan2(-5, 0), 0x8000);
    CHECK_EQ(hle_arctan2(0, 5), 0x4000);
    CHECK_EQ(hle_arctan2(0, -5), 0xC000);
    CHECK_EQ(hle_arctan2(0x1000, 0x800), 0x12E4);
}

TEST(hle_obj_affine_set_rotates_90_degrees) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x100, false); /* x scale 1.0 */
    bus_write16(g, 0x02000002, 0x200, false); /* y scale 2.0 */
    bus_write16(g, 0x02000004, 0x4000, false); /* 90 degrees */
    g->cpu.r[0] = 0x02000000;
    g->cpu.r[1] = 0x02000100;
    g->cpu.r[2] = 1;
    g->cpu.r[3] = 8; /* OAM-style stride */
    hle_obj_affine_set(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x0000); /* pa */
    CHECK_EQ(bus_read16(g, 0x02000108, false), 0xFF00); /* pb = -1.0 */
    CHECK_EQ(bus_read16(g, 0x02000110, false), 0x0200); /* pc = 2.0 */
    CHECK_EQ(bus_read16(g, 0x02000118, false), 0x0000); /* pd */
    gba_destroy(g);
}

TEST(hle_bg_affine_set_offsets_the_origin) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0x1000, false); /* texture center x (19.8) */
    bus_write32(g, 0x02000004, 0x800, false);  /* texture center y */
    bus_write16(g, 0x02000008, 8, false);      /* display center x */
    bus_write16(g, 0x0200000A, 4, false);      /* display center y */
    bus_write16(g, 0x0200000C, 0x100, false);  /* x scale 1.0 */
    bus_write16(g, 0x0200000E, 0x100, false);  /* y scale 1.0 */
    bus_write16(g, 0x02000010, 0, false);      /* angle */
    g->cpu.r[0] = 0x02000000;
    g->cpu.r[1] = 0x02000100;
    g->cpu.r[2] = 1;
    hle_bg_affine_set(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x100);
    CHECK_EQ(bus_read16(g, 0x02000102, false), 0);
    CHECK_EQ(bus_read16(g, 0x02000104, false), 0);
    CHECK_EQ(bus_read16(g, 0x02000106, false), 0x100);
    CHECK_EQ(bus_read32(g, 0x02000108, false), 0x800);
    CHECK_EQ(bus_read32(g, 0x0200010C, false), 0x400);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit hle_`
Expected: build fails: `'bios/hle_internal.h' file not found`.

- [x] **Step 3: Create `core/src/bios/hle_internal.h`**

```c
#ifndef GBA_HLE_INTERNAL_H
#define GBA_HLE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

/* hle_math.c — register-based SWIs follow GBATEK's argument conventions. */
void hle_div(gba_t *g, int32_t num, int32_t den); /* r0 = quotient, r1 = remainder, r3 = |quotient| */
uint32_t hle_sqrt(uint32_t value);
int32_t hle_arctan(int32_t tan);         /* tan in 1.14 fixed point; result -0x4000..0x4000 */
uint32_t hle_arctan2(int32_t x, int32_t y); /* result 0..0xFFFF for a full circle */
void hle_bg_affine_set(gba_t *g);
void hle_obj_affine_set(gba_t *g);

/* hle_memory.c */
void hle_cpu_set(gba_t *g);
void hle_cpu_fast_set(gba_t *g);
void hle_bit_unpack(gba_t *g);

/* hle_decompress.c — the vram variants write 16-bit units because VRAM ignores byte writes. */
void hle_lz77(gba_t *g, bool vram);
void hle_huffman(gba_t *g);
void hle_rl(gba_t *g, bool vram);
void hle_diff8(gba_t *g, bool vram);
void hle_diff16(gba_t *g);

#endif
```

- [x] **Step 4: Create `core/src/bios/hle_math.c`**

```c
#include <stdint.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* sin(2*pi*i/256) in 1.14 fixed point; the affine SWIs use the top 8 bits of the angle. */
static const int16_t sine_table[256] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801,
    3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765,
    9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395,
    13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
    16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143,
    16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
    15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842,
    13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
    11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434,
    9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
    6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590,
    3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2006, -2404, -2801,
    -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
    -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765,
    -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434,
    -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
    -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590,
    -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
};

static int32_t sin_lut(uint32_t index) {
    return sine_table[index & 0xFF];
}

static int32_t cos_lut(uint32_t index) {
    return sine_table[(index + 64) & 0xFF];
}

void hle_div(gba_t *g, int32_t num, int32_t den) {
    arm_cpu_t *c = &g->cpu;
    if (den == 0) {
        /* The real BIOS never returns from a division by zero; return something harmless. */
        c->r[0] = num < 0 ? 0xFFFFFFFFu : 1;
        c->r[1] = (uint32_t)num;
        c->r[3] = 1;
        return;
    }
    int64_t q = (int64_t)num / den;
    int64_t r = (int64_t)num % den;
    c->r[0] = (uint32_t)q;
    c->r[1] = (uint32_t)r;
    c->r[3] = (uint32_t)(q < 0 ? -q : q);
}

uint32_t hle_sqrt(uint32_t value) {
    uint32_t result = 0, bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* The BIOS's polynomial approximation of atan. */
int32_t hle_arctan(int32_t tan) {
    int64_t t = tan;
    int64_t a = -((t * t) >> 14);
    int64_t b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    return (int32_t)((t * b) >> 16);
}

/* Reduce to an octant where |ratio| <= 1, then add that octant's base angle. */
uint32_t hle_arctan2(int32_t x, int32_t y) {
    if (y == 0) return x >= 0 ? 0 : 0x8000;
    if (x == 0) return y >= 0 ? 0x4000 : 0xC000;
    int32_t r;
    if (y >= 0) {
        if (x >= 0 && x >= y) r = hle_arctan(y * 16384 / x);
        else if (x < 0 && -x >= y) r = hle_arctan(y * 16384 / x) + 0x8000;
        else r = 0x4000 - hle_arctan(x * 16384 / y);
    } else {
        if (x <= 0 && -x > -y) r = hle_arctan(y * 16384 / x) + 0x8000;
        else if (x > 0 && x >= -y) r = hle_arctan(y * 16384 / x) + 0x10000;
        else r = 0xC000 - hle_arctan(x * 16384 / y);
    }
    return (uint32_t)r & 0xFFFF;
}

static void affine_params(int32_t sx, int32_t sy, uint32_t angle, int32_t out[4]) {
    uint32_t theta = (angle >> 8) & 0xFF;
    out[0] = (sx * cos_lut(theta)) >> 14;    /* pa */
    out[1] = -((sx * sin_lut(theta)) >> 14); /* pb */
    out[2] = (sy * sin_lut(theta)) >> 14;    /* pc */
    out[3] = (sy * cos_lut(theta)) >> 14;    /* pd */
}

/* r0 = source (20-byte entries), r1 = destination (16-byte entries), r2 = count */
void hle_bg_affine_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1];
    for (uint32_t i = 0; i < c->r[2]; i++, src += 20, dst += 16) {
        int32_t cx = (int32_t)bus_read32(g, src, false);
        int32_t cy = (int32_t)bus_read32(g, src + 4, false);
        int32_t dx = (int16_t)bus_read16(g, src + 8, false);
        int32_t dy = (int16_t)bus_read16(g, src + 10, false);
        int32_t sx = (int16_t)bus_read16(g, src + 12, false);
        int32_t sy = (int16_t)bus_read16(g, src + 14, false);
        int32_t p[4];
        affine_params(sx, sy, bus_read16(g, src + 16, false), p);
        for (int k = 0; k < 4; k++) bus_write16(g, dst + (uint32_t)k * 2, (uint16_t)p[k], false);
        bus_write32(g, dst + 8, (uint32_t)(cx - (p[0] * dx + p[1] * dy)), false);
        bus_write32(g, dst + 12, (uint32_t)(cy - (p[2] * dx + p[3] * dy)), false);
    }
}

/* r0 = source (8-byte entries), r1 = destination, r2 = count, r3 = byte stride between pa/pb/pc/pd */
void hle_obj_affine_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], stride = c->r[3];
    for (uint32_t i = 0; i < c->r[2]; i++, src += 8, dst += stride * 4) {
        int32_t sx = (int16_t)bus_read16(g, src, false);
        int32_t sy = (int16_t)bus_read16(g, src + 2, false);
        int32_t p[4];
        affine_params(sx, sy, bus_read16(g, src + 4, false), p);
        for (uint32_t k = 0; k < 4; k++) bus_write16(g, dst + k * stride, (uint16_t)p[k], false);
    }
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit hle_`
Expected: 6 `hle_*` tests pass.

- [x] **Step 6: Commit**

```bash
git add core/src/bios tests/unit/test_hle_math.c
git commit -m "feat(bios): add HLE Div, Sqrt, ArcTan, ArcTan2 and affine set-up" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: HLE BIOS memory SWIs (CpuSet, CpuFastSet, BitUnPack)

**Files:**
- Create: `core/src/bios/hle_memory.c`
- Test: `tests/unit/test_hle_memory.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_hle_memory.c`**

```c
#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"

#define SRC 0x02000000u
#define DST 0x02000100u

TEST(hle_cpu_set_copies_halfwords) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 5; i++) bus_write16(g, SRC + i * 2, (uint16_t)(i + 1), false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 4;
    hle_cpu_set(g);
    for (uint32_t i = 0; i < 4; i++) CHECK_EQ(bus_read16(g, DST + i * 2, false), i + 1);
    CHECK_EQ(bus_read16(g, DST + 8, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_set_fills_words) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 3 | (1u << 24) | (1u << 26);
    hle_cpu_set(g);
    for (uint32_t i = 0; i < 3; i++) CHECK_EQ(bus_read32(g, DST + i * 4, false), 0xDEADBEEF);
    CHECK_EQ(bus_read32(g, DST + 12, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_set_refuses_bios_source) {
    gba_t *g = gba_create(NULL);
    g->cpu.r[0] = 0x00000000;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 4;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0);
    gba_destroy(g);
}

TEST(hle_cpu_fast_set_rounds_up_to_eight_words) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 16; i++) bus_write32(g, SRC + i * 4, i + 1, false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 3;
    hle_cpu_fast_set(g);
    for (uint32_t i = 0; i < 8; i++) CHECK_EQ(bus_read32(g, DST + i * 4, false), i + 1);
    CHECK_EQ(bus_read32(g, DST + 32, false), 0);
    gba_destroy(g);
}

TEST(hle_bit_unpack_nibbles_to_bytes) {
    gba_t *g = gba_create(NULL);
    const uint32_t info = SRC + 0x10;
    bus_write8(g, SRC, 0x21, false);
    bus_write8(g, SRC + 1, 0x03, false);
    bus_write16(g, info, 2, false);         /* source length in bytes */
    bus_write8(g, info + 2, 4, false);      /* source unit width */
    bus_write8(g, info + 3, 8, false);      /* destination unit width */
    bus_write32(g, info + 4, 0x10, false);  /* offset 0x10, zero units stay zero */
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = DST;
    g->cpu.r[2] = info;
    hle_bit_unpack(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00131211);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit hle_`
Expected: link fails: `undefined reference to 'hle_cpu_set'`.

- [x] **Step 3: Create `core/src/bios/hle_memory.c`**

```c
#include "bios/hle_internal.h"
#include "gba_internal.h"

/* The BIOS refuses to copy from its own address range. */
static bool source_allowed(uint32_t src) {
    return (src & 0x0E000000u) != 0;
}

/* r0 = source, r1 = destination, r2 = count (bits 0-20) | fill (bit 24) | 32-bit units (bit 26) */
void hle_cpu_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], ctrl = c->r[2];
    uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1, words = (ctrl >> 26) & 1;
    if (!source_allowed(src)) return;
    uint32_t unit = words ? 4 : 2;
    src &= ~(unit - 1);
    dst &= ~(unit - 1);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * unit;
        if (words) bus_write32(g, dst + i * unit, bus_read32(g, from, i > 0), i > 0);
        else bus_write16(g, dst + i * unit, (uint16_t)bus_read16(g, from, i > 0), i > 0);
    }
}

/* Like CpuSet with 32-bit units, but the count is rounded up to a multiple of 8 words. */
void hle_cpu_fast_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0] & ~3u, dst = c->r[1] & ~3u, ctrl = c->r[2];
    uint32_t count = ((ctrl & 0x1FFFFF) + 7) & ~7u;
    bool fill = (ctrl >> 24) & 1;
    if (!source_allowed(src)) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * 4;
        bus_write32(g, dst + i * 4, bus_read32(g, from, i > 0), i > 0);
    }
}

/* r0 = source, r1 = destination (word-aligned), r2 = info: u16 length, u8 src width, u8 dst width, u32 offset|zero flag */
void hle_bit_unpack(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1] & ~3u, info = c->r[2];
    uint32_t len = bus_read16(g, info, false);
    uint32_t src_width = bus_read8(g, info + 2, false);
    uint32_t dst_width = bus_read8(g, info + 3, false);
    uint32_t data = bus_read32(g, info + 4, false);
    uint32_t offset = data & 0x7FFFFFFF;
    bool offset_zero = (data >> 31) != 0;
    bool src_ok = src_width == 1 || src_width == 2 || src_width == 4 || src_width == 8;
    bool dst_ok = dst_width == 1 || dst_width == 2 || dst_width == 4 || dst_width == 8 || dst_width == 16 ||
                  dst_width == 32;
    if (!src_ok || !dst_ok) return;

    uint32_t src_mask = (1u << src_width) - 1;
    uint32_t dst_mask = dst_width == 32 ? 0xFFFFFFFFu : (1u << dst_width) - 1;
    uint32_t out = 0, out_bits = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t byte = bus_read8(g, src + i, false);
        for (uint32_t bit = 0; bit < 8; bit += src_width) {
            uint32_t unit = (byte >> bit) & src_mask;
            if (unit || offset_zero) unit += offset;
            out |= (unit & dst_mask) << out_bits;
            out_bits += dst_width;
            if (out_bits == 32) {
                bus_write32(g, dst, out, false);
                dst += 4;
                out = 0;
                out_bits = 0;
            }
        }
    }
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit hle_`
Expected: 11 `hle_*` tests pass (6 from Task 16 + 5 new).

- [x] **Step 5: Commit**

```bash
git add core/src/bios/hle_memory.c tests/unit/test_hle_memory.c
git commit -m "feat(bios): add HLE CpuSet, CpuFastSet and BitUnPack" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 18: HLE BIOS decompression SWIs (LZ77, Huffman, RL, Diff)

**Files:**
- Create: `core/src/bios/hle_decompress.c`
- Test: `tests/unit/test_hle_decompress.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_hle_decompress.c`**

The compressed inputs are hand-built from GBATEK's formats; each comment gives the layout.

```c
#include <stddef.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"
#include "test.h"

#define SRC 0x02000000u
#define DST 0x02001000u

static gba_t *with_source(const uint8_t *bytes, size_t n, uint32_t dst) {
    gba_t *g = gba_create(NULL);
    for (size_t i = 0; i < n; i++) bus_write8(g, SRC + (uint32_t)i, bytes[i], false);
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = dst;
    return g;
}

static bool memory_equals(gba_t *g, uint32_t addr, const uint8_t *want, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (bus_read8(g, addr + (uint32_t)i, false) != want[i]) return false;
    }
    return true;
}

/* header 0x10 | size 9; flags 0b00010000; 'A' 'B' 'C'; block (len 6, disp 3) */
static const uint8_t lz77_src[] = {0x10, 0x09, 0x00, 0x00, 0x10, 'A', 'B', 'C', 0x30, 0x02};
static const uint8_t lz77_out[] = {'A', 'B', 'C', 'A', 'B', 'C', 'A', 'B', 'C'};

TEST(hle_lz77_wram) {
    gba_t *g = with_source(lz77_src, sizeof lz77_src, DST);
    hle_lz77(g, false);
    CHECK(memory_equals(g, DST, lz77_out, sizeof lz77_out));
    gba_destroy(g);
}

TEST(hle_lz77_vram_writes_halfwords) {
    gba_t *g = with_source(lz77_src, sizeof lz77_src, 0x06000000);
    hle_lz77(g, true);
    CHECK(memory_equals(g, 0x06000000, lz77_out, sizeof lz77_out));
    CHECK_EQ(bus_read8(g, 0x06000009, false), 0); /* odd tail byte padded with zero */
    gba_destroy(g);
}

/* header 0x28 (8-bit data) | size 4; tree size byte 1; root 0xC0 (both children are data): 'A', 'B';
 * bitstream word 0x60000000 = bits 0,1,1,0 → "ABBA" */
TEST(hle_huffman_8bit) {
    static const uint8_t src[] = {0x28, 0x04, 0x00, 0x00, 0x01, 0xC0, 'A', 'B', 0x00, 0x00, 0x00, 0x60};
    static const uint8_t want[] = {'A', 'B', 'B', 'A'};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_huffman(g);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x30 | size 7; run of 4 x 0xAA (flag 0x81); 3 literal bytes (flag 0x02) */
TEST(hle_run_length_wram) {
    static const uint8_t src[] = {0x30, 0x07, 0x00, 0x00, 0x81, 0xAA, 0x02, 0x01, 0x02, 0x03};
    static const uint8_t want[] = {0xAA, 0xAA, 0xAA, 0xAA, 0x01, 0x02, 0x03};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_rl(g, false);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x81 (8-bit deltas) | size 4 */
TEST(hle_diff8_wram) {
    static const uint8_t src[] = {0x81, 0x04, 0x00, 0x00, 0x10, 0x01, 0x01, 0xFF};
    static const uint8_t want[] = {0x10, 0x11, 0x12, 0x11};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_diff8(g, false);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}

/* header 0x82 (16-bit deltas) | size 6 bytes */
TEST(hle_diff16_halfwords) {
    static const uint8_t src[] = {0x82, 0x06, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0xFF, 0xFF};
    static const uint8_t want[] = {0x00, 0x10, 0x01, 0x10, 0x00, 0x10};
    gba_t *g = with_source(src, sizeof src, DST);
    hle_diff16(g);
    CHECK(memory_equals(g, DST, want, sizeof want));
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit hle_`
Expected: link fails: `undefined reference to 'hle_lz77'`.

- [x] **Step 3: Create `core/src/bios/hle_decompress.c`**

```c
#include <string.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* Output sink: byte writes, or (VRAM variants) byte pairs combined into halfword writes. */
typedef struct {
    uint32_t addr;
    bool halfwords;
    bool has_pending;
    uint8_t pending;
} out_writer_t;

static out_writer_t writer_init(uint32_t addr, bool halfwords) {
    out_writer_t w = {addr, halfwords, false, 0};
    return w;
}

static void writer_put(gba_t *g, out_writer_t *w, uint8_t v) {
    if (!w->halfwords) {
        bus_write8(g, w->addr++, v, false);
        return;
    }
    if (!w->has_pending) {
        w->pending = v;
        w->has_pending = true;
        return;
    }
    bus_write16(g, w->addr, (uint16_t)(w->pending | v << 8), false);
    w->addr += 2;
    w->has_pending = false;
}

static void writer_flush(gba_t *g, out_writer_t *w) {
    if (w->halfwords && w->has_pending) bus_write16(g, w->addr, w->pending, false);
    w->has_pending = false;
}

static uint8_t read_byte(gba_t *g, uint32_t *src) {
    return (uint8_t)bus_read8(g, (*src)++, false);
}

/* Every compressed stream starts with a word: type in bits 4-7, decompressed size in bits 8-31. */
static uint32_t read_header(gba_t *g, uint32_t *src) {
    uint32_t header = bus_read32(g, *src, false);
    *src += 4;
    return header;
}

void hle_lz77(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint8_t window[4096]; /* the maximum displacement is 4096 */
    memset(window, 0, sizeof window);
    uint32_t written = 0;
    while (written < size) {
        uint8_t flags = read_byte(g, &src);
        for (int bit = 7; bit >= 0 && written < size; bit--) {
            if (flags & (1u << bit)) {
                uint8_t hi = read_byte(g, &src), lo = read_byte(g, &src);
                uint32_t len = (hi >> 4) + 3u;
                uint32_t disp = (((hi & 0xFu) << 8) | lo) + 1;
                for (uint32_t j = 0; j < len && written < size; j++) {
                    uint8_t v = window[(written - disp) & 0xFFF];
                    window[written & 0xFFF] = v;
                    writer_put(g, &w, v);
                    written++;
                }
            } else {
                uint8_t v = read_byte(g, &src);
                window[written & 0xFFF] = v;
                writer_put(g, &w, v);
                written++;
            }
        }
    }
    writer_flush(g, &w);
}

void hle_huffman(gba_t *g) {
    uint32_t src = g->cpu.r[0] & ~3u, dst = g->cpu.r[1] & ~3u;
    uint32_t header = read_header(g, &src);
    uint32_t bits = header & 0xF; /* 4 or 8 */
    uint32_t remaining = header >> 8;
    uint32_t tree = src;
    uint32_t stream = tree + (bus_read8(g, tree, false) + 1) * 2;
    uint32_t root = tree + 1, node = root;
    uint32_t out = 0, out_bits = 0;
    if (bits != 4 && bits != 8) return;

    while (remaining > 0) {
        uint32_t word = bus_read32(g, stream, false);
        stream += 4;
        for (int i = 31; i >= 0 && remaining > 0; i--) {
            uint32_t bit = (word >> i) & 1;
            uint32_t n = bus_read8(g, node, false);
            uint32_t child = (node & ~1u) + (n & 0x3F) * 2 + 2 + bit;
            bool is_data = (n & (bit ? 0x40u : 0x80u)) != 0;
            if (!is_data) {
                node = child;
                continue;
            }
            out |= (bus_read8(g, child, false) & ((1u << bits) - 1)) << out_bits;
            out_bits += bits;
            node = root;
            if (out_bits == 32) {
                bus_write32(g, dst, out, false);
                dst += 4;
                remaining = remaining > 4 ? remaining - 4 : 0;
                out = 0;
                out_bits = 0;
            }
        }
    }
}

void hle_rl(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint32_t written = 0;
    while (written < size) {
        uint8_t flag = read_byte(g, &src);
        if (flag & 0x80) {
            uint32_t len = (flag & 0x7Fu) + 3;
            uint8_t v = read_byte(g, &src);
            for (uint32_t j = 0; j < len && written < size; j++, written++) writer_put(g, &w, v);
        } else {
            uint32_t len = (flag & 0x7Fu) + 1;
            for (uint32_t j = 0; j < len && written < size; j++, written++) writer_put(g, &w, read_byte(g, &src));
        }
    }
    writer_flush(g, &w);
}

void hle_diff8(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint8_t value = 0;
    for (uint32_t i = 0; i < size; i++) {
        value = (uint8_t)(value + read_byte(g, &src));
        writer_put(g, &w, value);
    }
    writer_flush(g, &w);
}

void hle_diff16(gba_t *g) {
    uint32_t src = g->cpu.r[0] & ~3u, dst = g->cpu.r[1];
    uint32_t size = read_header(g, &src) >> 8;
    uint16_t value = 0;
    for (uint32_t i = 0; i < size; i += 2) {
        value = (uint16_t)(value + bus_read16(g, src + i, false));
        bus_write16(g, dst + i, value, false);
    }
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit hle_`
Expected: 17 `hle_*` tests pass.

- [x] **Step 5: Commit**

```bash
git add core/src/bios/hle_decompress.c tests/unit/test_hle_decompress.c
git commit -m "feat(bios): add HLE LZ77, Huffman, run-length and diff decompression" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: HLE BIOS stub, SWI dispatch, boot latch and system SWIs

The stub is real ARM code placed so that every address a game can observe matches the retail BIOS: the IRQ handler at 0x128–0x13C, the SWI handler ending with `movs pc, lr` at 0x188, and the words at 0x0E4, 0x13C, 0x144 and 0x190 that BIOS read protection exposes (the jsmolka `bios` test checks exactly these). The SWI handler switches to System mode like the real BIOS, so IRQs can fire during `IntrWait`; everything except `IntrWait`/`VBlankIntrWait` traps into C through the reserved undefined instruction `0xE7F000F0`.

**Files:**
- Create: `core/src/bios/bios_stub.S`, `tools/gen_bios_stub.py`, `core/src/bios/bios_stub.inc` (generated)
- Modify: `core/src/bios/hle.h`, `core/src/bios/hle.c` (full replacements), `core/src/cpu/arm.c`
- Test: `tests/unit/test_bios.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_bios.c`**

The integration programs were assembled with the NDK assembler; the comments show the source.

```c
#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static uint32_t bios_word(gba_t *g, uint32_t addr) {
    const uint8_t *p = &g->bus.bios[addr];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void run_until_pc(gba_t *g, uint32_t pc, int max_steps) {
    for (int i = 0; i < max_steps && g->cpu.r[15] != pc; i++) cpu_step(g);
}

TEST(bios_stub_matches_real_bios_at_observable_addresses) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bios_word(g, 0x0E4), 0xE129F000); /* msr cpsr_fc, r0 */
    CHECK_EQ(bios_word(g, 0x128), 0xE92D500F); /* stmfd sp!, {r0-r3, r12, lr} */
    CHECK_EQ(bios_word(g, 0x134), 0xE510F004); /* ldr pc, [r0, #-4] */
    CHECK_EQ(bios_word(g, 0x13C), 0xE25EF004); /* subs pc, lr, #4 */
    CHECK_EQ(bios_word(g, 0x144), 0xE55EC002); /* ldrb r12, [lr, #-2] */
    CHECK_EQ(bios_word(g, 0x188), 0xE1B0F00E); /* movs pc, lr */
    CHECK_EQ(bios_word(g, 0x190), 0xE3A02004); /* mov r2, #4 */
    gba_destroy(g);
}

TEST(bios_read_after_boot_returns_boot_latch) {
    const uint32_t ops[] = {
        0xE3A00000, /* mov r0, #0 */
        0xE5901000, /* ldr r1, [r0] */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[1], 0xE129F000);
    gba_destroy(g);
}

TEST(bios_arm_swi_div_round_trip) {
    const uint32_t ops[] = {
        0xE3A00064, /* mov r0, #100 */
        0xE3A01007, /* mov r1, #7 */
        0xEF060000, /* swi 0x60000 (Div) */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    CHECK_EQ(g->cpu.r[15], 0x00000008);
    run_until_pc(g, 0x0800000C, 500);
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK_EQ(g->cpu.r[1], 2);
    CHECK_EQ(g->cpu.r[3], 14);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->bus.bios_latch, 0xE3A02004); /* last BIOS fetch: 0x188 + 8 */
    gba_destroy(g);
}

TEST(bios_thumb_swi_round_trip) {
    const uint16_t ops[] = {
        0x2064, /* movs r0, #100 */
        0x2107, /* movs r1, #7 */
        0xDF06, /* swi 6 (Div) */
        0xE7FE, /* b . */
    };
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    run_until_pc(g, 0x08000006, 500);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    CHECK_EQ(g->cpu.r[0], 14);
    CHECK(g->cpu.cpsr & CPSR_T);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    gba_destroy(g);
}

TEST(bios_vblank_intr_wait_runs_the_game_isr) {
    const uint32_t ops[] = {
        0xE3A00301, /* mov  r0, #0x04000000 */
        0xE3A01008, /* mov  r1, #8 */
        0xE1C010B4, /* strh r1, [r0, #4]      DISPSTAT: VBlank IRQ enable */
        0xE3A01001, /* mov  r1, #1 */
        0xE2802C02, /* add  r2, r0, #0x200 */
        0xE1C210B0, /* strh r1, [r2]          IE = VBlank */
        0xE5821008, /* str  r1, [r2, #8]      IME = 1 */
        0xE28F3010, /* adr  r3, isr */
        0xE2404004, /* sub  r4, r0, #4        0x03FFFFFC mirrors 0x03007FFC */
        0xE5843000, /* str  r3, [r4]          IRQ handler pointer */
        0xEF050000, /* swi  0x50000           VBlankIntrWait */
        0xE3A05001, /* mov  r5, #1 */
        0xEAFFFFFE, /* b    . */
        /* isr: */
        0xE3A00301, /* mov  r0, #0x04000000 */
        0xE2800C02, /* add  r0, r0, #0x200 */
        0xE1D010B2, /* ldrh r1, [r0, #2]      IF */
        0xE1C010B2, /* strh r1, [r0, #2]      acknowledge */
        0xE2402F82, /* sub  r2, r0, #0x208    0x03FFFFF8 mirrors BIOS_IF */
        0xE1D230B0, /* ldrh r3, [r2] */
        0xE1833001, /* orr  r3, r3, r1 */
        0xE1C230B0, /* strh r3, [r2] */
        0xE12FFF1E, /* bx   lr */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    gba_run_frame(g); /* the program halts in VBlankIntrWait; VBlank is flagged at frame end */
    gba_run_frame(g); /* wake, run the ISR, return from the SWI */
    CHECK_EQ(g->cpu.r[5], 1);
    CHECK_EQ(g->cpu.r[15], 0x08000030);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_SYS);
    /* Not checking IF: the VBlank that ends frame 2 has just been flagged again. */
    CHECK_EQ(g->bus.iwram[0x7FF8], 0); /* IntrWait clears the flag it waited for */
    gba_destroy(g);
}

TEST(bios_soft_reset) {
    const uint32_t ops[] = {0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->bus.iwram[0x7E00] = 0x55;
    g->bus.iwram[0x7FFA] = 0; /* return to ROM */
    g->cpu.r[0] = 9;
    cpu_write_cpsr(&g->cpu, MODE_SVC);
    hle_swi(g, 0x00);
    CHECK_EQ(g->cpu.r[15], 0x08000000);
    CHECK_EQ(g->cpu.cpsr, MODE_SYS);
    CHECK_EQ(g->cpu.r[0], 0);
    CHECK_EQ(g->cpu.r[13], 0x03007F00);
    CHECK_EQ(g->cpu.bank_r13[BANK_IRQ], 0x03007FA0);
    CHECK_EQ(g->bus.iwram[0x7E00], 0);

    g->bus.iwram[0x7FFA] = 1; /* return to EWRAM */
    hle_swi(g, 0x00);
    CHECK_EQ(g->cpu.r[15], 0x02000000);
    gba_destroy(g);
}

TEST(bios_register_ram_reset) {
    gba_t *g = gba_create(NULL);
    memset(g->bus.ewram, 0xFF, EWRAM_SIZE);
    memset(g->bus.iwram, 0xFF, IWRAM_SIZE);
    g->cpu.r[0] = 0x03; /* EWRAM and IWRAM */
    hle_swi(g, 0x01);
    CHECK_EQ(g->bus.ewram[0], 0);
    CHECK_EQ(g->bus.ewram[EWRAM_SIZE - 1], 0);
    CHECK_EQ(g->bus.iwram[0x7DFF], 0);
    CHECK_EQ(g->bus.iwram[0x7E00], 0xFF); /* the BIOS work area survives */
    CHECK_EQ(g->ppu.dispcnt, 0x0080);
    gba_destroy(g);
}

TEST(bios_halt_and_checksum) {
    gba_t *g = gba_create(NULL);
    hle_swi(g, 0x02);
    CHECK(g->irq.halted);
    hle_swi(g, 0x0D);
    CHECK_EQ(g->cpu.r[0], 0xBAAE187F);
    gba_destroy(g);
}

TEST(bios_trap_outside_the_bios_is_undefined) {
    const uint32_t ops[] = {0xE7F000F0};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_UND);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit bios_`
Expected: build fails: implicit declaration of `hle_swi`.

- [x] **Step 3: Create `core/src/bios/bios_stub.S`**

```asm
@ HLE BIOS stub, assembled by tools/gen_bios_stub.py into bios_stub.inc.
@
@ Only what games can observe mirrors the retail BIOS:
@   0x0E4        word the BIOS read latch holds after boot
@   0x128-0x13C  IRQ handler (same instructions and addresses)
@   0x140-0x188  SWI entry/exit (0x144 and 0x188 match), 0x190 latch word after SWIs
@ SWIs other than IntrWait/VBlankIntrWait trap into C via the undefined
@ instruction 0xE7F000F0 with the SWI number in r12.

        .syntax unified
        .arm
        .text
        .global _start
_start:
        b       reset_vector            @ 0x00 Reset
        b       undef_vector            @ 0x04 Undefined instruction
        b       swi_vector              @ 0x08 SWI
        b       abort_vector            @ 0x0C Prefetch abort
        b       abort_vector            @ 0x10 Data abort
        b       abort_vector            @ 0x14 Reserved
        b       irq_vector              @ 0x18 IRQ
        b       abort_vector            @ 0x1C FIQ

reset_vector:
        mov     r12, #0                 @ SoftReset
        .word   0xE7F000F0
undef_vector:
        movs    pc, lr
abort_vector:
        subs    pc, lr, #4

        .org    0x0E4
        .word   0xE129F000

        .org    0x128
irq_vector:
        stmfd   sp!, {r0-r3, r12, lr}
        mov     r0, #0x04000000
        add     lr, pc, #0
        ldr     pc, [r0, #-4]           @ game handler at 0x03FFFFFC (mirror of 0x03007FFC)
        ldmfd   sp!, {r0-r3, r12, lr}
        subs    pc, lr, #4

swi_vector:                             @ 0x140
        stmfd   sp!, {r11, r12, lr}
        ldrb    r12, [lr, #-2]          @ SWI number: comment byte (ARM bits 16-23 / Thumb bits 0-7)
        mrs     r11, spsr
        stmfd   sp!, {r11}
        and     r11, r11, #0x80         @ keep the caller's IRQ-disable bit
        orr     r11, r11, #0x1F         @ run in System mode, like the real BIOS
        msr     cpsr_fc, r11
        stmfd   sp!, {r2, lr}
        bl      swi_dispatch
        ldmfd   sp!, {r2, lr}
        mov     r12, #0xD3
        msr     cpsr_fc, r12
        ldmfd   sp!, {r11}
        msr     spsr_fc, r11
        ldmfd   sp!, {r11, r12, lr}
        .org    0x188                   @ the zero padding above decodes as andeq r0, r0, r0 (a no-op)
        movs    pc, lr
        .org    0x190
        .word   0xE3A02004

        .org    0x200
swi_dispatch:
        cmp     r12, #0x04
        beq     intr_wait
        cmp     r12, #0x05
        beq     vblank_intr_wait
        .word   0xE7F000F0              @ C executes SWI r12
        bx      lr

vblank_intr_wait:
        mov     r0, #1
        mov     r1, #1
intr_wait:                              @ r0: discard old flags, r1: IRQ flags to wait for
        stmfd   sp!, {r2, r3, lr}
        mov     r3, #0x04000000
        mov     r2, #1
        strb    r2, [r3, #0x208]        @ IME = 1
        cmp     r0, #0
        ldrhne  r2, [r3, #-8]           @ BIOS_IF at 0x03FFFFF8 (mirror of 0x03007FF8)
        bicne   r2, r2, r1
        strhne  r2, [r3, #-8]
1:
        ldrh    r2, [r3, #-8]
        ands    r0, r2, r1
        bne     2f
        mov     r2, #0
        strb    r2, [r3, #0x301]        @ HALTCNT: sleep until an interrupt
        b       1b
2:
        bic     r2, r2, r1
        strh    r2, [r3, #-8]
        ldmfd   sp!, {r2, r3, pc}
```

- [x] **Step 4: Create `tools/gen_bios_stub.py`**

```python
#!/usr/bin/env python3
"""Assemble core/src/bios/bios_stub.S into core/src/bios/bios_stub.inc.

Uses clang, ld.lld and llvm-objcopy from the Android NDK (or from $CLANG_BIN_DIR).
Re-run after editing bios_stub.S and commit the regenerated .inc.
"""
import glob
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "core/src/bios/bios_stub.S"
OUT = ROOT / "core/src/bios/bios_stub.inc"


def find_bin_dir() -> Path:
    if os.environ.get("CLANG_BIN_DIR"):
        return Path(os.environ["CLANG_BIN_DIR"])
    sdk = Path(os.environ.get("ANDROID_HOME") or Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk")
    found = sorted(glob.glob(str(sdk / "ndk/*/toolchains/llvm/prebuilt/*/bin")))
    if not found:
        sys.exit("Android NDK not found; set CLANG_BIN_DIR to a directory containing clang")
    return Path(found[-1])


def tool(bin_dir: Path, name: str) -> str:
    exe = bin_dir / (name + ".exe")
    return str(exe if exe.exists() else bin_dir / name)


def main() -> None:
    bin_dir = find_bin_dir()
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp) / "stub.elf"
        raw = Path(tmp) / "stub.bin"
        subprocess.run(
            [tool(bin_dir, "clang"), "--target=armv4t-none-eabi", "-nostdlib", "-fuse-ld=lld",
             "-Wl,-Ttext=0", "-Wl,--entry=_start", str(SRC), "-o", str(elf)],
            check=True)
        subprocess.run(
            [tool(bin_dir, "llvm-objcopy"), "-O", "binary", "--only-section=.text", str(elf), str(raw)],
            check=True)
        data = raw.read_bytes()
    lines = ["/* Generated by tools/gen_bios_stub.py from bios_stub.S. Do not edit. */"]
    for i in range(0, len(data), 16):
        lines.append(" ".join("0x%02X," % b for b in data[i:i + 16]))
    OUT.write_text("\n".join(lines) + "\n", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT).as_posix()} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
```

- [x] **Step 5: Generate the stub include**

Run: `python tools/gen_bios_stub.py`
Expected: `wrote core/src/bios/bios_stub.inc (612 bytes)`

- [x] **Step 6: Replace `core/src/bios/hle.h`**

```c
#ifndef GBA_HLE_H
#define GBA_HLE_H

#include <stdint.h>

typedef struct gba gba_t;

/* Reserved undefined instruction in the BIOS stub meaning "run SWI number r12 in C". */
#define HLE_TRAP_OPCODE 0xE7F000F0u

/* Install the BIOS stub and set the state the real BIOS leaves when it jumps to the cartridge. */
void hle_boot(gba_t *g);
/* Run BIOS function `number` on the current registers. */
void hle_swi(gba_t *g, uint32_t number);

#endif
```

- [x] **Step 7: Replace `core/src/bios/hle.c`**

```c
#include "bios/hle.h"

#include <string.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* Assembled from bios_stub.S by tools/gen_bios_stub.py. */
static const uint8_t bios_stub[] = {
#include "bios_stub.inc"
};

static uint32_t bios_word(const gba_t *g, uint32_t addr) {
    const uint8_t *p = &g->bus.bios[addr];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Stack pointers and modes the real BIOS sets up before entering the cartridge. */
static void set_post_bios_registers(arm_cpu_t *c) {
    cpu_write_cpsr(c, MODE_SVC);
    c->r[13] = 0x03007FE0;
    c->r[14] = 0;
    c->spsr[BANK_SVC] = 0;
    cpu_write_cpsr(c, MODE_IRQ);
    c->r[13] = 0x03007FA0;
    c->r[14] = 0;
    c->spsr[BANK_IRQ] = 0;
    cpu_write_cpsr(c, MODE_SYS);
    c->r[13] = 0x03007F00;
    c->r[14] = 0;
    for (int i = 0; i <= 12; i++) c->r[i] = 0;
}

void hle_boot(gba_t *g) {
    memset(g->bus.bios, 0, BIOS_SIZE);
    memcpy(g->bus.bios, bios_stub, sizeof bios_stub);
    set_post_bios_registers(&g->cpu);
    g->cpu.r[15] = 0x08000000;
    g->io.postflg = 1;
    g->bus.bios_latch = bios_word(g, 0xE4); /* the retail BIOS's last fetch before the jump */
}

/* SWI 0x00: the flag at 0x03007FFA picks EWRAM or ROM as the restart address. */
static void soft_reset(gba_t *g) {
    bool to_ewram = g->bus.iwram[0x7FFA] != 0;
    memset(&g->bus.iwram[IWRAM_SIZE - 0x200], 0, 0x200);
    set_post_bios_registers(&g->cpu);
    cpu_branch(&g->cpu, to_ewram ? 0x02000000 : 0x08000000);
}

/* SWI 0x01: r0 selects which memories and register groups to clear. */
static void register_ram_reset(gba_t *g) {
    uint32_t flags = g->cpu.r[0];
    bus_t *b = &g->bus;
    g->ppu.dispcnt = 0x0080; /* always: forced blank */
    if (flags & 0x01) memset(b->ewram, 0, sizeof b->ewram);
    if (flags & 0x02) memset(b->iwram, 0, IWRAM_SIZE - 0x200); /* the top 0x200 bytes are BIOS state */
    if (flags & 0x04) memset(b->pal, 0, sizeof b->pal);
    if (flags & 0x08) memset(b->vram, 0, sizeof b->vram);
    if (flags & 0x10) memset(b->oam, 0, sizeof b->oam);
    if (flags & 0x20) memset(&g->io.raw[0x120], 0, 0x160 - 0x120); /* serial */
    if (flags & 0x40) memset(&g->io.raw[0x060], 0, 0x0B0 - 0x060); /* sound */
    if (flags & 0x80) {                                             /* all other registers */
        memset(&g->io.raw[0x000], 0, 0x060);
        memset(&g->io.raw[0x0B0], 0, 0x120 - 0x0B0);
        g->ppu.dispstat = 0;
        g->irq.ie = 0;
        g->irq.if_ = 0;
        g->irq.ime = 0;
        bus_set_waitcnt(g, 0);
    }
}

/* SWI 0x19: jump straight to the target bias level instead of ramping. */
static void sound_bias(gba_t *g) {
    uint32_t bias = bus_read16(g, 0x04000088, false);
    bias = (bias & ~0x3FFu) | (g->cpu.r[0] ? 0x200u : 0);
    bus_write16(g, 0x04000088, (uint16_t)bias, false);
}

void hle_swi(gba_t *g, uint32_t number) {
    arm_cpu_t *c = &g->cpu;
    bus_idle(g, 20); /* approximate call overhead; memory SWIs also pay per-access cycles */
    switch (number) {
    case 0x00: soft_reset(g); break;
    case 0x01: register_ram_reset(g); break;
    case 0x02:
    case 0x03: irq_halt(g); break; /* Stop behaves like Halt until milestone 3 */
    case 0x06: hle_div(g, (int32_t)c->r[0], (int32_t)c->r[1]); break;
    case 0x07: hle_div(g, (int32_t)c->r[1], (int32_t)c->r[0]); break; /* DivArm: operands swapped */
    case 0x08: c->r[0] = hle_sqrt(c->r[0]); break;
    case 0x09: c->r[0] = (uint32_t)hle_arctan((int16_t)c->r[0]); break;
    case 0x0A: c->r[0] = hle_arctan2((int16_t)c->r[0], (int16_t)c->r[1]); break;
    case 0x0B: hle_cpu_set(g); break;
    case 0x0C: hle_cpu_fast_set(g); break;
    case 0x0D: c->r[0] = 0xBAAE187F; break; /* GetBiosChecksum: the retail BIOS value */
    case 0x0E: hle_bg_affine_set(g); break;
    case 0x0F: hle_obj_affine_set(g); break;
    case 0x10: hle_bit_unpack(g); break;
    case 0x11: hle_lz77(g, false); break;
    case 0x12: hle_lz77(g, true); break;
    case 0x13: hle_huffman(g); break;
    case 0x14: hle_rl(g, false); break;
    case 0x15: hle_rl(g, true); break;
    case 0x16: hle_diff8(g, false); break;
    case 0x17: hle_diff8(g, true); break;
    case 0x18: hle_diff16(g); break;
    case 0x19: sound_bias(g); break;
    default: break; /* 0x04/0x05 run in the stub; sound-driver and multiboot SWIs are no-ops */
    }
}
```

- [x] **Step 8: Route the trap in `core/src/cpu/arm.c`**

Add `#include "bios/hle.h"` to the includes, and replace `arm_undefined` with:

```c
static void arm_undefined(gba_t *g, uint32_t op) {
    /* The HLE BIOS stub calls into C with a reserved undefined instruction. */
    if (op == HLE_TRAP_OPCODE && g->hle_bios && g->bus.in_bios) {
        hle_swi(g, g->cpu.r[12] & 0xFF);
        return;
    }
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 4);
}
```

- [x] **Step 9: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes, including the 9 `bios_*` tests.

- [x] **Step 10: Commit**

```bash
git add core/src tools/gen_bios_stub.py tests/unit/test_bios.c
git commit -m "feat(bios): add HLE BIOS stub with IRQ/SWI handlers and C SWI dispatch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 20: Debug API, headless test runner, ROM fetch script, CTest wiring

**Files:**
- Create: `core/include/gba_debug.h`, `core/src/gba_debug.c`, `tools/testrunner/CMakeLists.txt`, `tools/testrunner/main.c`, `tools/fetch_test_roms.py`
- Modify: `CMakeLists.txt`
- Test: `tests/unit/test_debug.c`

- [x] **Step 1: Write the failing test `tests/unit/test_debug.c`**

```c
#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

TEST(debug_api_reads_state_and_steps) {
    const uint32_t ops[] = {
        0xE3A00005, /* mov r0, #5 */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    CHECK_EQ(gba_debug_reg(g, 15), 0x08000000);
    CHECK_EQ(gba_debug_peek32(g, 0x08000004), 0xEAFFFFFE);
    uint64_t before = gba_debug_cycles(g);
    gba_debug_step(g);
    CHECK_EQ(gba_debug_reg(g, 0), 5);
    CHECK(gba_debug_cycles(g) > before);
    CHECK_EQ(gba_debug_cpsr(g), MODE_SYS);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit debug`
Expected: build fails: `'gba_debug.h' file not found`.

- [x] **Step 3: Create `core/include/gba_debug.h`**

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

#ifdef __cplusplus
}
#endif

#endif
```

- [x] **Step 4: Create `core/src/gba_debug.c`**

```c
#include "gba_debug.h"

#include "gba_internal.h"

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
```

- [x] **Step 5: Run the test to verify it passes**

Run: `bash tools/dev.sh unit debug`
Expected: `[ PASS ] debug_api_reads_state_and_steps`.

- [x] **Step 6: Create `tools/testrunner/main.c`**

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]
 * Runs until the CPU spins on a branch-to-self (the idle loop test ROMs end in) or
 * <n> frames pass (default 600). With --result-reg, register n == 0 means PASS
 * (jsmolka gba-tests keep the first failing test number there).
 * Exit codes: 0 pass/done, 1 fail, 2 timeout, 3 usage/load error, 77 ROM missing (CTest skip).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba.h"
#include "gba_debug.h"

enum { EXIT_PASS = 0, EXIT_FAILED = 1, EXIT_TIMEOUT = 2, EXIT_USAGE = 3, EXIT_SKIP = 77 };

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = len > 0 ? malloc((size_t)len) : NULL;
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = buf ? (size_t)len : 0;
    return buf;
}

static int at_idle_loop(gba_t *g) {
    uint32_t pc = gba_debug_reg(g, 15);
    if (gba_debug_cpsr(g) & (1u << 5)) { /* Thumb: b . */
        uint32_t word = gba_debug_peek32(g, pc & ~3u);
        uint16_t op = (pc & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        return op == 0xE7FE;
    }
    return gba_debug_peek32(g, pc) == 0xEAFFFFFE; /* ARM: b . */
}

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL;
    long frames = 600;
    int result_reg = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--result-reg") && i + 1 < argc) result_reg = (int)strtol(argv[++i], NULL, 0);
        else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    if (!rom_path || result_reg > 15 || frames <= 0) return usage();

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) {
        printf("SKIP %s (not found; run tools/fetch_test_roms.py)\n", rom_path);
        return EXIT_SKIP;
    }

    gba_t *g = gba_create(NULL);
    int code = EXIT_USAGE;
    if (bios_path) {
        size_t bios_size;
        uint8_t *bios = read_file(bios_path, &bios_size);
        gba_err_t err = bios ? gba_load_bios(g, bios, bios_size) : GBA_ERR_BIOS_SIZE;
        free(bios);
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load BIOS %s\n", bios_path);
            goto done;
        }
    }
    if (gba_load_rom(g, rom, rom_size) != GBA_OK) {
        fprintf(stderr, "cannot load ROM %s\n", rom_path);
        goto done;
    }

    long frame = 0;
    while (frame < frames && !at_idle_loop(g)) {
        gba_run_frame(g);
        frame++;
    }

    if (!at_idle_loop(g)) {
        printf("TIMEOUT %s after %ld frames (pc=%08X)\n", rom_path, frames, (unsigned)gba_debug_reg(g, 15));
        code = EXIT_TIMEOUT;
    } else if (result_reg < 0) {
        printf("DONE %s after %ld frames\n", rom_path, frame);
        code = EXIT_PASS;
    } else {
        uint32_t result = gba_debug_reg(g, (unsigned)result_reg);
        if (result == 0) {
            printf("PASS %s\n", rom_path);
            code = EXIT_PASS;
        } else {
            printf("FAIL %s: test %u\n", rom_path, (unsigned)result);
            code = EXIT_FAILED;
        }
    }

done:
    gba_destroy(g);
    free(rom);
    return code;
}
```

- [x] **Step 7: Create `tools/testrunner/CMakeLists.txt`**

```cmake
add_executable(testrunner main.c)
target_link_libraries(testrunner PRIVATE gbacore)
target_compile_options(testrunner PRIVATE -Wall -Wextra)
```

- [x] **Step 8: Replace the `if(NOT ANDROID)` block in the root `CMakeLists.txt`**

```cmake
if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tools/testrunner)

    # jsmolka/gba-tests ROMs, fetched by tools/fetch_test_roms.py; skipped when missing.
    # Each entry is name:result-register (the Thumb ROM keeps its result in r7).
    set(JSMOLKA_DIR ${PROJECT_SOURCE_DIR}/tests/roms/jsmolka)
    foreach(entry arm:12 thumb:7 memory:12 bios:12)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 reg)
        add_test(NAME jsmolka_${name} COMMAND testrunner ${JSMOLKA_DIR}/${name}.gba --result-reg ${reg})
        set_tests_properties(jsmolka_${name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
```

- [x] **Step 9: Create `tools/fetch_test_roms.py`**

```python
#!/usr/bin/env python3
"""Download the jsmolka/gba-tests ROMs used by CTest into tests/roms/jsmolka/ (gitignored).

Pinned to one upstream commit; file sizes are checked against that commit.
"""
import sys
import urllib.request
from pathlib import Path

COMMIT = "a7113b67e63f83a9b321696ddd7042ccfad6c881"
FILES = {  # local name: (path in repo, expected size in bytes)
    "arm.gba": ("arm/arm.gba", 8824),
    "thumb.gba": ("thumb/thumb.gba", 3680),
    "memory.gba": ("memory/memory.gba", 2172),
    "bios.gba": ("bios/bios.gba", 1908),
}
DEST = Path(__file__).resolve().parent.parent / "tests/roms/jsmolka"


def main() -> None:
    DEST.mkdir(parents=True, exist_ok=True)
    for name, (path, size) in FILES.items():
        url = f"https://raw.githubusercontent.com/jsmolka/gba-tests/{COMMIT}/{path}"
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read()
        if len(data) != size:
            sys.exit(f"{name}: expected {size} bytes, got {len(data)}")
        (DEST / name).write_bytes(data)
        print(f"{name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
```

- [x] **Step 10: Reconfigure and check the runner skips missing ROMs**

Run: `bash tools/dev.sh configure && bash tools/dev.sh run tests/roms/jsmolka/arm.gba --result-reg 12; echo "exit=$?"`
Expected: `SKIP tests/roms/jsmolka/arm.gba (not found; run tools/fetch_test_roms.py)` and `exit=77`.

Run: `bash tools/dev.sh test`
Expected: `unit_tests` passes; the four `jsmolka_*` tests are reported as `Skipped`; `100% tests passed`.

- [x] **Step 11: Commit**

```bash
git add CMakeLists.txt core/include/gba_debug.h core/src/gba_debug.c tools/testrunner tools/fetch_test_roms.py tests/unit/test_debug.c
git commit -m "feat(tools): add debug API, headless test runner and jsmolka ROM fetch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 21: Milestone exit — pass the jsmolka ARM, Thumb, memory and BIOS ROMs

**Files:**
- Modify: whichever core files the failures point to (no planned edits)
- Test: `tests/unit/test_regressions.c` (create only if a failure needs a regression test)

- [x] **Step 1: Fetch the test ROMs (needs the user's approval — downloads 4 files, about 16.6 KB total, from github.com/jsmolka/gba-tests)**

Run: `python tools/fetch_test_roms.py`
Expected:
```
arm.gba: 8824 bytes
thumb.gba: 3680 bytes
memory.gba: 2172 bytes
bios.gba: 1908 bytes
```

- [x] **Step 2: Run the full suite**

Run: `bash tools/dev.sh test`
Expected: 5 tests (`unit_tests`, `jsmolka_arm`, `jsmolka_thumb`, `jsmolka_memory`, `jsmolka_bios`), `100% tests passed, 0 tests failed out of 5`.

Individually, `bash tools/dev.sh run tests/roms/jsmolka/arm.gba --result-reg 12` must print `PASS tests/roms/jsmolka/arm.gba`.

- [x] **Step 3: If any ROM fails, fix it test-first (skip when Step 2 passed)**

Use superpowers:systematic-debugging. The runner prints `FAIL <rom>: test <n>`; locate test `<n>` in the upstream source at commit `a7113b67e63f83a9b321696ddd7042ccfad6c881`:

| ROM | Test number ranges → source file |
|---|---|
| `arm` | 1 conditions, 50 branches, 100 flags, 150 shifts, 200 data_processing, 250 psr_transfer, 300 multiply, 350 single_transfer, 400 halfword_transfer, 450 data_swap, 500 block_transfer (`arm/*.asm`) |
| `thumb` | 1 logical, 50 shifts, 100 arithmetic, 150 branches, 200 memory (`thumb/*.asm`) |
| `memory` | 1 mirrors, 50 video_strb (`memory/*.asm`) |
| `bios` | 1–4 in `bios/bios.asm` (BIOS latch after boot / after SWI / during IRQ / after IRQ) |

For each failure:
1. Copy the failing test's instruction sequence into a new `TEST` in `tests/unit/test_regressions.c`, assembling encodings with the NDK assembler (`clang --target=armv4t-none-eabi -c x.S && llvm-objdump -d x.o`, both in `%LOCALAPPDATA%/Android/Sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin`), and assert the value the jsmolka test compares.
2. Run `bash tools/dev.sh unit <test name>` and confirm it fails the same way.
3. Fix the handler, run `bash tools/dev.sh test` until everything passes.
4. Commit each fix separately: `git commit -m "fix(cpu): <what was wrong>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

If a ROM reports `TIMEOUT`, print the PC it stopped at (the runner does) and check it against the stub layout (addresses below 0x4000 mean the CPU is stuck in the BIOS stub — usually an IRQ/halt issue).

- [x] **Step 4: Confirm a clean sanitizer run**

Run: `bash tools/dev.sh test 2>&1 | grep -E "AddressSanitizer|runtime error" || echo "sanitizers clean"`
Expected: `sanitizers clean` (skip if Task 1 had to disable sanitizers; say so in the report).

- [x] **Step 5: Commit any remaining work and tag the milestone**

```bash
git status --short
git tag milestone-1
```

Expected: `git status --short` prints nothing (all fixes were committed in Step 3).

---

## Self-review notes (for the plan author; kept for reviewers)

- **Spec coverage (milestone 1 scope):** scheduler (§3.1) → Task 2; CPU modes/banking/exceptions/halt (§3.2) → Tasks 6, 8–15; decode tables (§3.2) → Tasks 6/8/13; memory map, mirrors, byte-write quirks, open bus, BIOS protection, ROM OOB, wait states (§3.3, §4.5 ROM) → Tasks 3–4; HLE stub, IRQ handler, SWI list, boot state, real-BIOS path (§3.4) → Tasks 6, 16–19; IRQ/keypad (§4.4) → Task 4; LCD timing needed by the frame loop (§4.1 timing) → Task 5; error codes (§8) → Tasks 3, 6; host toolchain, sanitizers, unit tests, test ROM runner and fetch script (§9.1–9.3) → Tasks 1, 20–21. Deferred items are listed in "Scope and deliberate deviations".
- **Encodings:** every ARM/Thumb opcode in the tests and the BIOS stub was assembled with NDK clang 27.2 (`--target=armv4t-none-eabi`) while writing this plan; the stub's observable words were checked after linking with ld.lld.
- **Math:** the ArcTan polynomial reproduces `atan` exactly at the tested points, and ArcTan2 was checked against `atan2` over 20,000 random inputs (max error 1.6 units of 1/65536 turn).
