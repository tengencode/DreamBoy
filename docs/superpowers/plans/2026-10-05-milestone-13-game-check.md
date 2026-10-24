# Milestone 13 — The game check — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task, **inline in this session** (CLAUDE.md: never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play many real games automatically, find what the emulator gets wrong, fix the real problems, and lock every game in as a test.

**Architecture:** A warning log in the core (compiled only into PC test builds with `GBA_NOTES`) records anything a game uses that the emulator skips. The test runner gains long key scripts, a picture every N frames and the log. A Python tool (`tools/game_check.py`, run by `tools/dev.sh games`) plays every game in `tests/games/games.txt`, flags freezes / blank screens / silence / crashes, builds contact sheets and an HTML report, and with `--check` / `--lock` compares or locks each game's hashes and log.

**Tech Stack:** C11 core (clang / llvm-mingw, CMake + Ninja via `tools/dev.sh`), Python 3.14 (stdlib + Pillow 12, already installed), the existing test runner and CTest.

**Spec:** `docs/superpowers/specs/2026-10-05-milestone-13-game-check-design.md`

---

## Conventions (every task)

- Branch `milestone-13`. One commit per task; messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Commands run in Git Bash from the repo root through `bash tools/dev.sh …` (it puts clang, CMake and llvm-mingw's DLLs on PATH; running the `.exe` files directly fails).
- **The checks** (run at the end of every task that changes C code; docs-only steps skip them):
  1. `bash tools/dev.sh unit` → `… tests, 0 failed`
  2. `bash tools/dev.sh test` → `100% tests passed` (121 tests from Task 1 on)
  3. `bash tools/dev.sh bench --check` → every scene `ok`, and each scene's fps ≥ 97 % of the Task 1 baseline (in PROGRESS.md Notes)
  4. When the core (`core/`) changed: `bash tools/dev.sh jvmtest` and `bash tools/dev.sh androidtest` (known flakes on a busy emulator: `SessionTest.runsAtGbaSpeed`, `AudioTest.aStuckStreamIsReopenedByTheWatchdog` — rerun once; if they keep failing, ask the user before restarting the emulator)
  5. From Task 5 on: `bash tools/dev.sh games --check` → no `DIFFERENT`
  6. No C compiler warnings in the build output.
- **Docs after every task** (CLAUDE.md): `docs/PROGRESS.md` (Last updated, the task's Completed entry with its commit hash, the milestone 13 task table, Next with the exact next step and first command), tick this plan's checkboxes, the spec if a decision changed; then re-read PROGRESS's Last updated / Goal / Project status / Next / Notes, this plan's task list, the spec's status line and CLAUDE.md's Key files and fix anything stale.
- ROMs, saves and states of the user's games never go into git. Reports and pictures stay in `build/` (gitignored).

## File map

| File | What it is |
|---|---|
| `core/src/notes.h`, `core/src/notes.c` (new) | The warning log: kinds, storage in `gba_t`, the `NOTE…` macros (empty without `GBA_NOTES`) |
| `core/src/gba_internal.h` | `notes_t notes` in `struct gba` (only with `GBA_NOTES`) |
| `core/src/gba.c`, `bios/hle.c`, `mem/io.c`, `mem/bus.c`, `sys/dma.c`, `cpu/arm.c`, `cpu/thumb.c` | One-line hooks |
| `core/include/gba_debug.h`, `core/src/gba_debug.c` | `gba_note_t`, `gba_debug_notes()`, `gba_debug_notes_enabled()` |
| `core/CMakeLists.txt` | `option(GBA_NOTES …)` |
| `tests/unit/test_notes.c` (new) | Unit tests of the log |
| `tools/testrunner/main.c` | `--write-state`, `--keys @file` and any length, `--shots-every/--shots-dir`, `--notes` |
| `CMakeLists.txt`, `tests/states/snd_mix_95.state` (new), `.gitignore` | The old-state test |
| `tools/dev.sh` | `configure` with `GBA_NOTES=ON`; `build/check`; the `games` command |
| `tools/game_check.py`, `tools/test_game_check.py` (new) | The game check and its tests |
| `tests/games/games.txt` (new) | The game list |
| `tools/fetch_test_roms.py` | The homebrew downloads (Task 6) |

---

### Task 1: The old-state guard, `--write-state`, and the speed baseline

Before any core change: a save state written by today's build is committed, and a CTest test proves it keeps loading and continuing exactly (spec §6). Also the PC speed baseline for the 97 % rule.

**Files:**
- Modify: `tools/testrunner/main.c`
- Modify: `CMakeLists.txt` (after the "Our own test ROMs" block)
- Modify: `.gitignore`
- Create: `tests/states/snd_mix_95.state` (generated)

- [x] **Step 1: Write the failing test** — in `CMakeLists.txt`, after the `foreach(name save_eeprom512 save_eeprom8k) … endforeach()` block, add:

```cmake
    # A save state written by the build of 2026-10-05, before milestone 13's fixes: it must keep loading and
    # continue exactly (the second half of own_snd_mix), so players' saved states survive core changes.
    add_test(NAME oldstate_snd_mix
             COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/snd_mix.gba
                     --load-state ${PROJECT_SOURCE_DIR}/tests/states/snd_mix_95.state --frames 95
                     --expect-hash 80a69197c1fb9325 --expect-audio-hash 0000000000000000)
```

(`80a69197c1fb9325` is own_snd_mix's approved picture after 190 frames; the audio value is set in Step 6.)

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh configure && bash tools/dev.sh test -R oldstate`
Expected: `oldstate_snd_mix` fails — "cannot load the state …" (the file does not exist yet).

- [x] **Step 3: Add `--write-state` to the test runner** — in `tools/testrunner/main.c`:

Header comment, after the `--load-state` sentence add: ` --write-state <file> saves a state of the machine after the run.`

In `usage()`, change the last line to:

```c
                    "                  [--load-state <file>] [--write-state <file>] [--cheat <file>]...\n");
```

In `main`, next to `const char *load_state = NULL;` add `const char *write_state = NULL;`, and in the argument loop after the `--load-state` line add:

```c
        else if (!strcmp(argv[i], "--write-state") && i + 1 < argc) write_state = argv[++i];
```

After the `--save` write-back block (just before `done:`) add:

```c
    if (write_state && code == EXIT_PASS) {
        size_t size = gba_state_size(g);
        uint8_t *state = malloc(size);
        if (!state || gba_state_save(g, state, size) != size || write_file(write_state, state, size) != 0) {
            fprintf(stderr, "cannot write the state %s\n", write_state);
            code = EXIT_USAGE;
        }
        free(state);
    }
```

- [x] **Step 4: Let the state file into git** — in `.gitignore`, after the `*.state` line add:

```
!tests/states/*.state
```

- [x] **Step 5: Write the state with today's core**

Run: `mkdir -p tests/states && bash tools/dev.sh run tests/own_roms/snd_mix.gba --frames 95 --write-state tests/states/snd_mix_95.state`
Expected: exit 0, the file exists (`ls -l tests/states/`).

- [x] **Step 6: Record the second half's sound**

Run: `bash tools/dev.sh run tests/own_roms/snd_mix.gba --load-state tests/states/snd_mix_95.state --frames 95 --hash --audio-hash`
Expected: `HASH 80a69197c1fb9325` (the approved picture — proves the state continues exactly) and `AUDIO <16 hex digits>`. Put that AUDIO value in place of `0000000000000000` in `CMakeLists.txt`.

- [x] **Step 7: Run the test to see it pass**

Run: `bash tools/dev.sh test -R oldstate`
Expected: `1/1 Test #…: oldstate_snd_mix … Passed`

- [x] **Step 8: The speed baseline**

Run: `bash tools/dev.sh bench --runs 3`
Expected: 6 scenes, each `ok`. Copy each scene's fps into PROGRESS.md Notes as "M13 PC speed baseline (2026-10-05, `bench --runs 3`)", and the 97 % floor of each.

- [x] **Step 9: The checks** (Conventions 1, 2, 6; CTest now 121).

- [x] **Step 10: Docs, then commit**

```bash
git add CMakeLists.txt .gitignore tools/testrunner/main.c tests/states/snd_mix_95.state docs/
git commit -m "test: an old save state must keep loading (--write-state); M13 speed baseline

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The warning log in the core

**Files:**
- Create: `core/src/notes.h`, `core/src/notes.c`, `tests/unit/test_notes.c`
- Modify: `core/CMakeLists.txt`, `tools/dev.sh` (`configure`), `core/src/gba_internal.h`, `core/src/gba.c`, `core/src/bios/hle.c`, `core/src/mem/io.c`, `core/src/mem/bus.c`, `core/src/sys/dma.c`, `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`, `core/include/gba_debug.h`, `core/src/gba_debug.c`

- [x] **Step 1: The build option** — append to `core/CMakeLists.txt`:

```cmake

# The warning log (milestone 13): a record of anything a game uses that the emulator skips. Only in the PC builds
# tools/dev.sh makes for tests and the game check (build/host, build/check) - never in the app or build/release.
option(GBA_NOTES "Build the core with the warning log" OFF)
if(GBA_NOTES)
    target_compile_definitions(gbacore PUBLIC GBA_NOTES=1) # PUBLIC: the unit tests see the same struct gba
endif()
```

In `tools/dev.sh`, the `configure)` line becomes:

```bash
    configure)
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE="${GBA_SANITIZE:-ON}" -DGBA_NOTES=ON ;;
```

and its header comment line: `#   tools/dev.sh configure        configure build/host (Debug; sanitizers unless GBA_SANITIZE=OFF; the warning log)`.

- [x] **Step 2: The public reading API** — in `core/include/gba_debug.h`, before the closing `#ifdef __cplusplus`, add:

```c
/* The warning log (milestone 13): what the game used that the emulator skips. Only builds with GBA_NOTES keep
 * it (tools/dev.sh's build/host and build/check); elsewhere it is always empty. */
#define GBA_DEBUG_NOTES_MAX 64
typedef struct {
    const char *kind;     /* "bios-call", "midline-io", "midline-palette", "green-swap", "video-capture",
                             "io-unused", "exec-empty", "undefined", "notes-dropped" */
    uint32_t detail;      /* the BIOS call number, register offset or address; 0 when the kind has none */
    uint32_t first_frame; /* frames finished before it first happened */
    uint32_t count;
} gba_note_t;
int gba_debug_notes_enabled(void);                                  /* 1 in builds with the warning log */
size_t gba_debug_notes(const gba_t *g, gba_note_t *out, size_t max); /* entries kept (copies at most max) */
```

and add `#include <stddef.h>` after `#include <stdint.h>`.

- [x] **Step 3: Write the failing tests** — create `tests/unit/test_notes.c`:

```c
/* The warning log (milestone 13). Only in builds with GBA_NOTES (build/host); the phone's unit tests skip it. */
#ifdef GBA_NOTES
#include <string.h>

#include "bios/hle.h"
#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

static gba_t *spinning(void) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    return tu_arm(ops, TU_LEN(ops));
}

/* The entry of that kind (and detail, unless detail is ANY), or NULL. */
#define ANY 0xFFFFFFFFu
static const gba_note_t *find(const gba_t *g, const char *kind, uint32_t detail) {
    static gba_note_t notes[GBA_DEBUG_NOTES_MAX];
    size_t n = gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(notes[i].kind, kind) && (detail == ANY || notes[i].detail == detail)) return &notes[i];
    return NULL;
}

static size_t note_count(const gba_t *g) {
    gba_note_t notes[GBA_DEBUG_NOTES_MAX];
    return gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
}

TEST(notes_are_on_in_this_build_and_empty_after_power_on) {
    CHECK_EQ(gba_debug_notes_enabled(), 1);
    gba_t *g = spinning();
    gba_run_frame(g);
    gba_run_frame(g);
    CHECK_EQ(note_count(g), 0); /* a plain program: nothing */
    gba_destroy(g);
}

TEST(notes_keep_each_bios_call_once_with_its_first_frame_and_count) {
    gba_t *g = spinning();
    gba_run_frame(g);
    gba_run_frame(g);
    hle_swi(g, 0x1F); /* MidiKey2Freq: skipped by the built-in BIOS */
    hle_swi(g, 0x1F);
    hle_swi(g, 0x20);
    const gba_note_t *n = find(g, "bios-call", 0x1F);
    CHECK(n != NULL);
    if (n) {
        CHECK_EQ(n->count, 2);
        CHECK_EQ(n->first_frame, 2);
    }
    CHECK(find(g, "bios-call", 0x20) != NULL);
    hle_swi(g, 0x0D); /* GetBiosChecksum is done by the built-in BIOS: no note */
    CHECK_EQ(note_count(g), 2);
    gba_destroy(g);
}

TEST(notes_see_picture_writes_only_while_a_visible_line_is_drawn) {
    gba_t *g = spinning();
    g->ppu.vcount = 10;
    g->ppu.dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    tu_io16(g, 0x010, 5); /* BG0HOFS while drawing */
    tu_pal(g, 1, 0x7FFF);
    CHECK(find(g, "midline-io", 0x010) != NULL);
    CHECK(find(g, "midline-palette", ANY) != NULL);
    tu_io16(g, 0x004, 0); /* DISPSTAT is not a picture register */
    CHECK(find(g, "midline-io", 0x004) == NULL);
    g->ppu.dispstat |= DISPSTAT_HBLANK; /* H-blank: fine */
    tu_io16(g, 0x012, 5);
    g->ppu.dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    g->ppu.vcount = 200; /* V-blank: fine */
    tu_io16(g, 0x014, 5);
    g->ppu.vcount = 10;
    g->ppu.dispcnt = 0x0080; /* forced blank: nothing is drawn */
    tu_io16(g, 0x016, 5);
    CHECK(find(g, "midline-io", 0x012) == NULL);
    CHECK(find(g, "midline-io", 0x014) == NULL);
    CHECK(find(g, "midline-io", 0x016) == NULL);
    gba_destroy(g);
}

TEST(notes_see_green_swap_and_video_capture) {
    gba_t *g = spinning();
    g->ppu.vcount = 200;
    tu_io16(g, 0x002, 0); /* off: fine */
    CHECK(find(g, "green-swap", ANY) == NULL);
    tu_io16(g, 0x002, 1);
    CHECK(find(g, "green-swap", ANY) != NULL);
    tu_io16(g, 0x0DE, 0xB000); /* DMA3 on, timing 3 (special) = video capture */
    CHECK(find(g, "video-capture", ANY) != NULL);
    gba_destroy(g);
}

TEST(notes_see_writes_to_io_addresses_without_a_register) {
    gba_t *g = spinning();
    g->ppu.vcount = 200;
    tu_io32(g, 0x064, 0); /* SOUND1CNT_X + the unused 0x066 in one word: fine */
    tu_io16(g, 0x050, 0); /* BLDCNT: fine */
    CHECK_EQ(note_count(g), 0);
    tu_io16(g, 0x0E0, 0);
    const gba_note_t *n = find(g, "io-unused", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x0E0);
    bus_write32(g, 0x04000800, 0x0D000020, false); /* memory control: ignored by the emulator */
    n = find(g, "io-unused", ANY);
    if (n) CHECK_EQ(n->count, 2); /* one entry for the kind; the first offset kept */
    gba_destroy(g);
}

TEST(notes_see_code_running_from_empty_memory) {
    const uint32_t ops[] = {
        0xE3A00201, /* mov r0, #0x10000000 */
        0xE12FFF10, /* bx r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    const gba_note_t *n = find(g, "exec-empty", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x10000000);
    gba_destroy(g);
}

TEST(notes_see_undefined_instructions) {
    const uint32_t ops[] = {0xE7F000F0}; /* undefined (the HLE trap's code, but not in the BIOS) */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    const gba_note_t *n = find(g, "undefined", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x08000000);
    gba_destroy(g);
    const uint16_t tops[] = {0xDE00}; /* Thumb undefined */
    g = tu_thumb(tops, TU_LEN(tops));
    tu_steps(g, 1);
    n = find(g, "undefined", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x08000000);
    gba_destroy(g);
}

TEST(notes_a_full_log_counts_what_it_leaves_out_and_a_reset_clears_it) {
    gba_t *g = spinning();
    for (uint32_t k = 0; k < GBA_DEBUG_NOTES_MAX + 5; k++) hle_swi(g, 0x40 + k); /* unknown calls */
    CHECK_EQ(note_count(g), GBA_DEBUG_NOTES_MAX);
    const gba_note_t *n = find(g, "notes-dropped", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->count, 6); /* 63 kept, 6 left out */
    gba_reset(g);
    CHECK_EQ(note_count(g), 0);
    gba_destroy(g);
}
#endif
```

- [x] **Step 4: Run them to see them fail**

Run: `bash tools/dev.sh configure && bash tools/dev.sh unit notes`
Expected: the build fails — `undefined reference to gba_debug_notes_enabled` / `gba_debug_notes` (or "implicit declaration").

- [x] **Step 5: The log itself** — create `core/src/notes.h`:

```c
#ifndef GBA_NOTES_H
#define GBA_NOTES_H

/* The warning log (milestone 13): what a game uses that the emulator does not do, so a game check finds gaps
 * the picture does not show. Kept only with GBA_NOTES (tools/dev.sh's build/host and build/check); everywhere
 * else the NOTE... macros are empty and cost nothing. Writes made while the CPU is in the BIOS are not noted. */
#include <stdint.h>

typedef struct gba gba_t;

typedef enum {
    NOTE_BIOS_CALL,       /* the built-in BIOS skipped a call; detail: its number */
    NOTE_MIDLINE_IO,      /* a picture register written while a visible line is drawn; detail: its offset */
    NOTE_MIDLINE_PALETTE, /* the palette written while a visible line is drawn */
    NOTE_GREEN_SWAP,      /* green swap switched on (not drawn) */
    NOTE_VIDEO_CAPTURE,   /* a DMA3 video-capture transfer switched on (never runs) */
    NOTE_IO_UNUSED,       /* a write to an I/O address with no register; detail: the first such offset */
    NOTE_EXEC_EMPTY,      /* code fetched from empty memory; detail: the first such address */
    NOTE_UNDEFINED,       /* an undefined instruction; detail: the first one's address */
    NOTE_DROPPED,         /* the log was full: how many notes were left out */
    NOTE_KIND_COUNT
} note_kind_t;

#define NOTES_MAX 64 /* = GBA_DEBUG_NOTES_MAX */

typedef struct {
    uint8_t kind; /* note_kind_t */
    uint32_t detail, first_frame, count;
} note_t;

typedef struct {
    note_t list[NOTES_MAX];
    uint32_t count; /* entries used */
    uint32_t frame; /* frames finished since the last reset */
} notes_t;

#ifdef GBA_NOTES
void notes_add(gba_t *g, note_kind_t kind, uint32_t detail);
void notes_drawing(gba_t *g, note_kind_t kind, uint32_t detail); /* noted only while a visible line is drawn */
void notes_io_write(gba_t *g, uint32_t off, unsigned size);      /* noted when no register is written */
void notes_fetch(gba_t *g, uint32_t addr);                       /* noted when addr is empty memory */
void notes_clear(gba_t *g);
#define NOTE(g, kind, detail) notes_add((g), (kind), (detail))
#define NOTE_DRAWING(g, kind, detail) notes_drawing((g), (kind), (detail))
#define NOTE_IO_WRITE(g, off, size) notes_io_write((g), (off), (size))
#define NOTE_FETCH(g, addr) notes_fetch((g), (addr))
#define NOTES_CLEAR(g) notes_clear(g)
#define NOTES_FRAME(g) ((g)->notes.frame++)
#else
#define NOTE(g, kind, detail) ((void)0)
#define NOTE_DRAWING(g, kind, detail) ((void)0)
#define NOTE_IO_WRITE(g, off, size) ((void)0)
#define NOTE_FETCH(g, addr) ((void)0)
#define NOTES_CLEAR(g) ((void)0)
#define NOTES_FRAME(g) ((void)0)
#endif

#endif
```

Create `core/src/notes.c`:

```c
#include "notes.h"

#ifdef GBA_NOTES
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gba_internal.h"

/* Kinds whose detail tells entries apart (one per BIOS call, one per register); the others keep the first. */
static bool keyed(note_kind_t kind) {
    return kind == NOTE_BIOS_CALL || kind == NOTE_MIDLINE_IO;
}

void notes_add(gba_t *g, note_kind_t kind, uint32_t detail) {
    notes_t *n = &g->notes;
    for (uint32_t i = 0; i < n->count; i++) {
        note_t *e = &n->list[i];
        if (e->kind == kind && (!keyed(kind) || e->detail == detail)) {
            e->count++;
            return;
        }
    }
    if (n->count >= NOTES_MAX - 1 && kind != NOTE_DROPPED) { /* the last entry is kept for the dropped count */
        notes_add(g, NOTE_DROPPED, 0);
        return;
    }
    n->list[n->count++] = (note_t){(uint8_t)kind, detail, n->frame, 1};
}

void notes_drawing(gba_t *g, note_kind_t kind, uint32_t detail) {
    const ppu_t *p = &g->ppu;
    if (g->bus.in_bios || p->vcount >= PPU_HEIGHT || (p->dispstat & DISPSTAT_HBLANK) || (p->dispcnt & 0x0080)) return;
    notes_add(g, kind, detail);
}

/* I/O halfwords with a register behind them (GBATEK's I/O map); the rest of 0x000-0x3FF is unused. */
static const uint16_t REGISTERS[][2] = {
    {0x000, 0x04C}, {0x050, 0x054}, {0x060, 0x064}, {0x068, 0x068}, {0x06C, 0x06C}, {0x070, 0x074},
    {0x078, 0x078}, {0x07C, 0x07C}, {0x080, 0x084}, {0x088, 0x088}, {0x090, 0x0A6}, {0x0B0, 0x0DE},
    {0x100, 0x10E}, {0x120, 0x12A}, {0x130, 0x134}, {0x140, 0x140}, {0x150, 0x158}, {0x200, 0x204},
    {0x208, 0x208}, {0x300, 0x300},
};

static bool has_register(uint32_t off) {
    off &= ~1u;
    for (size_t i = 0; i < sizeof REGISTERS / sizeof REGISTERS[0]; i++)
        if (off >= REGISTERS[i][0] && off <= REGISTERS[i][1]) return true;
    return false;
}

void notes_io_write(gba_t *g, uint32_t off, unsigned size) {
    if (g->bus.in_bios) return;
    bool used = off < IO_SIZE && (has_register(off) || (size == 4 && has_register(off + 2)));
    if (!used) notes_add(g, NOTE_IO_UNUSED, off);
}

void notes_fetch(gba_t *g, uint32_t addr) {
    bool empty;
    switch (addr >> 24) {
    case 0x0: empty = addr >= BIOS_SIZE; break;
    case 0x2:
    case 0x3:
    case 0x5:
    case 0x6:
    case 0x7: empty = false; break; /* RAM, palette, VRAM, OAM: odd places for code, but real memory */
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: empty = (addr & 0x01FFFFFFu) >= g->cart.rom_size; break; /* past the ROM's end */
    default: empty = true; break; /* 0x1, I/O, the save area and everything above */
    }
    if (empty) notes_add(g, NOTE_EXEC_EMPTY, addr);
}

void notes_clear(gba_t *g) {
    memset(&g->notes, 0, sizeof g->notes);
}
#endif
```

- [x] **Step 6: The storage and the hooks**

`core/src/gba_internal.h`: add `#include "notes.h"` after `#include "mem/io.h"`, and in `struct gba` after the `cheats` line:

```c
#ifdef GBA_NOTES
    notes_t notes; /* the warning log (notes.h); in no state chunk */
#endif
```

`core/src/gba.c`: at the end of `gba_reset` (after `if (g->hle_bios) hle_boot(g);`) add `NOTES_CLEAR(g); /* the built-in BIOS's own start-up is not the game's */`; in `gba_run_frame` after `run_frame(g);` add `NOTES_FRAME(g);`.

`core/src/bios/hle.c`, the last case of the second switch in `hle_swi`:

```c
    default: NOTE(g, NOTE_BIOS_CALL, number); break; /* 0x04/0x05 run in the stub; sound-driver and multiboot SWIs are no-ops */
```

`core/src/mem/io.c`, first lines of `write16`:

```c
static void write16(gba_t *g, uint32_t off, uint16_t v) {
    if (off < 0x056 && off != REG_DISPSTAT && off != REG_VCOUNT) NOTE_DRAWING(g, NOTE_MIDLINE_IO, off);
    if (off == 0x002 && (v & 1)) NOTE(g, NOTE_GREEN_SWAP, 0);
```

and the first lines of `io_write`:

```c
void io_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    NOTE_IO_WRITE(g, off, size);
    if (off >= IO_SIZE) return;
```

`core/src/mem/bus.c`, `mem_write`'s palette case:

```c
    case 0x5:
        NOTE_DRAWING(g, NOTE_MIDLINE_PALETTE, 0);
        if (size == 1) store_n(&b->pal[addr & (PAL_SIZE - 2)], (v & 0xFF) * 0x0101u, 2);
```

and in both `bus_fetch32` and `bus_fetch16`, right after `b->in_bios = addr < BIOS_SIZE;` in the slow path (after the `if (p) { … }` block):

```c
    NOTE_FETCH(g, addr);
```

`core/src/sys/dma.c`, `dma_write_control`, after `if (old & 0x8000) return; /* … */`:

```c
    if (n == 3 && ((value >> 12) & 3) == TIMING_SPECIAL) NOTE(g, NOTE_VIDEO_CAPTURE, 0);
```

`core/src/cpu/arm.c`, `arm_undefined`, before `cpu_exception(…)`:

```c
    NOTE(g, NOTE_UNDEFINED, g->cpu.r[15] - 8);
```

`core/src/cpu/thumb.c`, `thumb_undefined`:

```c
static void thumb_undefined(gba_t *g, uint16_t op) {
    (void)op;
    NOTE(g, NOTE_UNDEFINED, g->cpu.r[15] - 4);
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 2);
}
```

`core/src/gba_debug.c`, append:

```c
int gba_debug_notes_enabled(void) {
#ifdef GBA_NOTES
    return 1;
#else
    return 0;
#endif
}

size_t gba_debug_notes(const gba_t *g, gba_note_t *out, size_t max) {
#ifdef GBA_NOTES
    static const char *const names[NOTE_KIND_COUNT] = {
        "bios-call", "midline-io", "midline-palette", "green-swap", "video-capture",
        "io-unused", "exec-empty", "undefined",       "notes-dropped"};
    size_t n = g->notes.count;
    for (size_t i = 0; i < n && i < max; i++) {
        const note_t *e = &g->notes.list[i];
        out[i] = (gba_note_t){names[e->kind], e->detail, e->first_frame, e->count};
    }
    return n;
#else
    (void)g;
    (void)out;
    (void)max;
    return 0;
#endif
}
```

- [x] **Step 7: Run the tests to see them pass**

Run: `bash tools/dev.sh unit notes`
Expected: 8 notes tests PASS, `8 tests, 0 failed`. If `notes_see_code_running_from_empty_memory` fails, check that the branch's pipeline refill goes through `bus_fetch32` (cpu.c) and put `NOTE_FETCH` on the path it takes — the fetch, not the test, is what must change.

- [x] **Step 8: The release build has no log** — `bash tools/dev.sh release` then `grep -c GBA_NOTES build/release/CMakeCache.txt` shows `GBA_NOTES:BOOL=OFF` (one line); the app's Gradle CMake never sets it.

- [x] **Step 9: The checks** (all of Conventions 1–4, 6).

- [x] **Step 10: Docs, then commit**

```bash
git add core/ tests/unit/test_notes.c tools/dev.sh docs/
git commit -m "feat(core): the warning log (GBA_NOTES) for the game check

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The test runner — key scripts of any length, pictures along the way, the log

**Files:**
- Modify: `tools/testrunner/main.c`

- [x] **Step 1: Key scripts of any length** — replace the `MAX_KEY_EVENTS` define and the two key typedefs with:

```c
typedef struct {
    long frame;
    uint16_t keys; /* GBA_KEY_* held from this frame on */
} key_event_t;

typedef struct {
    key_event_t *ev; /* grown by add_key_event; free() */
    int count, cap;
} key_script_t;

/* Appends one event; false when out of memory. */
static bool add_key_event(key_script_t *ks, long frame, uint16_t keys) {
    if (ks->count == ks->cap) {
        int cap = ks->cap ? ks->cap * 2 : 64;
        key_event_t *ev = realloc(ks->ev, sizeof *ev * (size_t)cap);
        if (!ev) return false;
        ks->ev = ev;
        ks->cap = cap;
    }
    ks->ev[ks->count++] = (key_event_t){frame, keys};
    return true;
}
```

In `parse_keys`: change `if (end == p || *end != ':' || frame < 0 || ks->count == MAX_KEY_EVENTS) return false;` to `if (end == p || *end != ':' || frame < 0) return false;`, and `ks->ev[ks->count++] = (key_event_t){frame, keys};` to `if (!add_key_event(ks, frame, keys)) return false;`.

In `run_suite`: replace `key_script_t ks = {.count = 0};` with

```c
    key_event_t ev[2 * SUITE_GROUP_COUNT];
    key_script_t ks = {ev, 0, 2 * SUITE_GROUP_COUNT};
```

In `main`: `key_script_t keys = {.count = 0};` becomes `key_script_t keys = {NULL, 0, 0};`, and after `done:` add `free(keys.ev);` next to `free(rom);`.

- [x] **Step 2: `--keys @file`** — in `main`, add `char *keys_text = NULL;` with the other declarations, and replace `if (keys_arg && !parse_keys(keys_arg, &keys)) return usage();` with:

```c
    if (keys_arg && keys_arg[0] == '@') { /* a long script from a file (the game check's) */
        size_t n;
        uint8_t *data = read_file(keys_arg + 1, &n);
        if (!data || !(keys_text = malloc(n + 1))) {
            fprintf(stderr, "cannot read the key script %s\n", keys_arg + 1);
            free(data);
            return EXIT_USAGE;
        }
        memcpy(keys_text, data, n);
        while (n && (keys_text[n - 1] == '\n' || keys_text[n - 1] == '\r' || keys_text[n - 1] == ' ')) n--;
        keys_text[n] = '\0';
        free(data);
        keys_arg = keys_text;
    }
    if (keys_arg && !parse_keys(keys_arg, &keys)) {
        free(keys_text);
        return usage();
    }
```

and after `done:` add `free(keys_text);`.

- [x] **Step 3: Pictures along the way** — add the options `const char *shots_dir = NULL; long shots_every = 0;` and parse:

```c
        else if (!strcmp(argv[i], "--shots-every") && i + 1 < argc) shots_every = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shots-dir") && i + 1 < argc) shots_dir = argv[++i];
```

with the other usage checks:

```c
    if (shots_every < 0 || (shots_every > 0 && (!shots_dir || !frames_given || result_reg >= 0 || rewind_at >= 0)))
        return usage();
```

Above `main`, add:

```c
/* A PNG of the current picture as <dir>/<frame, 6 digits>.png. */
static bool write_shot(gba_t *g, const char *dir, long frame) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%06ld.png", dir, frame);
    if (png_write_bgr555(path, gba_framebuffer(g), 240, 160) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    return true;
}
```

In the exact-frames loop, right after `run_frame(g, &audio);`:

```c
                if (shots_every > 0 && (frame + 1) % shots_every == 0 && !write_shot(g, shots_dir, frame + 1)) {
                    free(hash_before);
                    code = EXIT_USAGE;
                    goto done;
                }
```

- [x] **Step 4: `--notes`** — add `bool print_notes = false;`, parse `else if (!strcmp(argv[i], "--notes")) print_notes = true;`, and after the usage checks:

```c
    if (print_notes && !gba_debug_notes_enabled()) {
        fprintf(stderr, "--notes: this testrunner was built without the warning log (GBA_NOTES)\n");
        return EXIT_USAGE;
    }
```

After `if (print_audio) printf(…);` add:

```c
    if (print_notes) {
        gba_note_t notes[GBA_DEBUG_NOTES_MAX];
        size_t n = gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
        for (size_t i = 0; i < n && i < GBA_DEBUG_NOTES_MAX; i++)
            printf("NOTE %s %08" PRIx32 " %" PRIu32 " %" PRIu32 "\n", notes[i].kind, notes[i].detail,
                   notes[i].first_frame, notes[i].count);
    }
```

- [x] **Step 5: Header comment and usage** — the header's option list gains `[--write-state <file>] [--shots-every <n> --shots-dir <dir>] [--notes]`, plus these sentences: `--keys @file reads the script from a file (any length). --shots-every n (with --frames) writes a PNG every n frames into --shots-dir as <frame>.png (6 digits). --notes prints the warning log as "NOTE <kind> <detail hex> <first frame> <count>" lines (builds with GBA_NOTES only).` `usage()` gets the line `"                  [--shots-every <n> --shots-dir <dir>] [--notes] [--keys @<file>]\n"`.

- [x] **Step 6: Check it on a Tonc demo**

Run:
```bash
mkdir -p build/shots/m7 && printf '60:UP,400:UP+LEFT,520:-\n' > build/shots/m7_keys.txt
bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --frames 180 --keys @build/shots/m7_keys.txt --shots-every 60 --shots-dir build/shots/m7 --hash --notes
bash tools/dev.sh run tests/roms/tonc/m7_demo.gba --frames 180 --keys 60:UP,400:UP+LEFT,520:- --hash
ls build/shots/m7
```
Expected: both runs print the same `HASH`; the folder holds exactly `000060.png 000120.png 000180.png`; `NOTE` lines (if any) are well-formed. Look at `000180.png` (Read tool): it must equal the picture `--screenshot` gives for 180 frames.

- [x] **Step 7: The checks** (Conventions 1, 2, 3, 6).

- [x] **Step 8: Docs, then commit**

```bash
git add tools/testrunner/main.c docs/
git commit -m "feat(testrunner): key scripts from a file, pictures every n frames, the warning log

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The game check tool

**Files:**
- Create: `tools/game_check.py`, `tools/test_game_check.py`, `tests/games/games.txt`
- Modify: `tools/dev.sh`

- [x] **Step 1: Write the failing tests** — create `tools/test_game_check.py`:

```python
"""Tests of tools/game_check.py's pure parts: python tools/test_game_check.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_check as gc  # noqa: E402

LINE = "m7 | tonc/m7_demo.gba | test | - | - | 60:UP | 1800 | - | - | -"


class ParseList(unittest.TestCase):
    def test_reads_games_and_skips_comments(self):
        games = gc.parse_list(f"# header\n\n{LINE}\n")
        self.assertEqual(len(games), 1)
        self.assertEqual(games[0]["name"], "m7")
        self.assertEqual(games[0]["frames"], 1800)
        self.assertEqual(games[0]["line"], 3)

    def test_refuses_bad_lines(self):
        for bad in ["m7 | x.gba | test", LINE.replace("1800", "0"), LINE.replace("m7 ", "M 7 "),
                    f"{LINE}\n{LINE}"]:
            with self.assertRaises(ValueError):
                gc.parse_list(bad)


class Keys(unittest.TestCase):
    def test_holds_last_until_the_next_hold(self):
        masks = gc.expand_keys("10:A,20:-", 30)
        self.assertEqual(masks[9], 0)
        self.assertEqual(masks[10], 1)
        self.assertEqual(masks[19], 1)
        self.assertEqual(masks[20], 0)

    def test_taps_repeat_on_top_of_holds(self):
        masks = gc.expand_keys("0:B,0-20/10:START", 30)
        start, b = 1 << 3, 1 << 1
        self.assertEqual(masks[0], b | start)
        self.assertEqual(masks[3], b | start)
        self.assertEqual(masks[4], b)
        self.assertEqual(masks[10], b | start)
        self.assertEqual(masks[20], b)

    def test_refuses_unknown_buttons_and_bad_ranges(self):
        for bad in ["10:X", "20-10/5:A", "0-10/0:A", "10"]:
            with self.assertRaises(ValueError):
                gc.expand_keys(bad, 30)

    def test_events_mark_each_change(self):
        self.assertEqual(gc.key_events([0, 0, 1, 1 | 64, 0]), "2:A,3:A+UP,4:-")
        self.assertEqual(gc.key_events([0, 0]), "0:-")


def shots(digests, every=60):
    """(frame, digest, one colour) pictures every `every` frames; digest 'k' means one colour."""
    return [((i + 1) * every, d, d == "k") for i, d in enumerate(digests)]


class Flags(unittest.TestCase):
    def test_a_long_unchanged_picture_while_buttons_change_is_a_freeze(self):
        pics = shots(["a"] * 20 + ["f"] * 15 + ["b"] * 5)  # f from frame 1260 to 2100
        masks = [0] * 2400
        masks[1500] = 1
        self.assertEqual(gc.find_freezes(pics, masks), [(1260, 2100)])

    def test_no_freeze_without_button_changes_or_while_settling_or_when_short(self):
        self.assertEqual(gc.find_freezes(shots(["a"] * 20 + ["f"] * 15 + ["b"] * 5), [0] * 2400), [])
        masks = [0] * 2400
        masks[500] = 1
        self.assertEqual(gc.find_freezes(shots(["f"] * 15 + ["b"] * 25), masks), [])
        masks[1300] = 1
        self.assertEqual(gc.find_freezes(shots(["a"] * 20 + ["f"] * 5 + ["b"] * 15), masks), [])

    def test_one_colour_for_ten_seconds_is_a_blank_screen(self):
        self.assertEqual(gc.find_blank(shots(["a"] * 3 + ["k"] * 12 + ["b"])), [(240, 900)])
        self.assertEqual(gc.find_blank(shots(["a"] * 3 + ["k"] * 5 + ["b"])), [])


class Output(unittest.TestCase):
    def test_reads_hashes_and_notes(self):
        out = gc.parse_output("HASH 0123456789abcdef\nAUDIO fedcba9876543210\n"
                              "NOTE bios-call 0000001f 12 3\nNOTE io-unused 000000e0 0 1\n")
        self.assertEqual(out["picture"], "0123456789abcdef")
        self.assertEqual(out["sound"], "fedcba9876543210")
        self.assertEqual(out["notes"][0], {"kind": "bios-call", "detail": "0000001f", "first": 12, "count": 3})
        self.assertEqual(gc.notes_summary(out["notes"]), "bios-call:0000001f io-unused:000000e0")
        self.assertEqual(gc.notes_summary([]), "-")


if __name__ == "__main__":
    unittest.main()
```

- [x] **Step 2: Run them to see them fail**

Run: `python tools/test_game_check.py`
Expected: `ModuleNotFoundError: No module named 'game_check'`.

- [x] **Step 3: The tool** — create `tools/game_check.py`:

```python
#!/usr/bin/env python3
"""The game check (milestone 13): plays every game in tests/games/games.txt through the test runner.

  game_check.py --runner <testrunner> --games <folder> [--only <name>] [--check | --lock <name|all>]

Without --check / --lock: each game (or --only's) is played with its button script; the run keeps a picture every
second, the sound and the warning log under build/check/work/<name>/, prints a summary, and writes the report
build/check/report/index.html: per game its contact sheet, flags (freeze, blank screen, silence, crash) and log.
--check plays every listed game without pictures and compares its picture hash, sound hash and log with the ones
locked in the list ("-" = not locked yet); exit 1 when any differs.
--lock plays the named game (or all) the same way and writes its SHA-256, hashes and log into the list.
Games come from --games (source "user"), tests/roms/ (source "test") or tests/roms/homebrew/ (a download URL); a
.zip is unpacked (its one .gba). A save file is copied into build/check/work/ first, so the user's file is never
written. Run it through tools/dev.sh games (which builds build/check and passes --runner and --games).
"""
import argparse
import array
import hashlib
import html
import re
import shutil
import subprocess
import sys
import wave
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIST = ROOT / "tests" / "games" / "games.txt"
TEST_ROMS = ROOT / "tests" / "roms"
HOMEBREW = TEST_ROMS / "homebrew"
WORK = ROOT / "build" / "check" / "work"
REPORT = ROOT / "build" / "check" / "report"

FIELDS = ["name", "file", "source", "sha256", "save", "keys", "frames", "picture", "sound", "notes"]
KEY_NAMES = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"]  # GBA_KEY_* bit order
CLOCK = 1767268800  # the cartridge clock, 2026-01-01 12:00 UTC (as gbabench), so runs are identical
SHOT_EVERY = 60     # frames between pictures: one second of game time
TAP = 4             # frames a tapped button stays down
SETTLE = 1200       # the first 20 s are not checked for freezes (logos, intros)
FREEZE = 600        # 10 s without a picture change while the buttons change
BLANK = 600         # 10 s of one colour
QUIET = 64          # sound whose peak-to-peak stays below this is silence
CRASH_KINDS = ("exec-empty", "undefined")


def parse_list(text):
    """The games of the list: one dict per game line (FIELDS + 'line'); blank and # lines skipped."""
    games = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != len(FIELDS):
            raise ValueError(f"line {number}: {len(parts)} fields, expected {len(FIELDS)}")
        game = dict(zip(FIELDS, parts))
        if not re.fullmatch(r"[a-z0-9_]+", game["name"]):
            raise ValueError(f"line {number}: a name may only have a-z, 0-9 and _")
        if not game["frames"].isdigit() or int(game["frames"]) <= 0:
            raise ValueError(f"line {number}: frames must be a positive number")
        if any(g["name"] == game["name"] for g in games):
            raise ValueError(f"line {number}: the name {game['name']} is used twice")
        game["frames"] = int(game["frames"])
        game["line"] = number
        games.append(game)
    return games


def key_mask(keys):
    """'A+B' → GBA_KEY_* bits; '-' → 0."""
    if keys == "-":
        return 0
    mask = 0
    for name in keys.split("+"):
        if name not in KEY_NAMES:
            raise ValueError(f"unknown button {name}")
        mask |= 1 << KEY_NAMES.index(name)
    return mask


def expand_keys(script, frames):
    """The buttons down in each frame. Items, comma-separated: 'F:KEYS' holds KEYS from frame F until the next
    such item ('-' = none); 'A-B/N:KEYS' taps KEYS (down TAP frames) every N frames from A up to B, on top."""
    held, taps, holds = [0] * frames, [0] * frames, []
    if script.strip() != "-":
        for item in (i.strip() for i in script.split(",")):
            when, _, keys = item.partition(":")
            if not keys or not when:
                raise ValueError(f"'{item}': expected FRAME:KEYS or A-B/N:KEYS")
            mask = key_mask(keys)
            if "-" in when:
                span, _, every = when.partition("/")
                start, _, end = span.partition("-")
                start, end, every = int(start), int(end), int(every or 0)
                if every <= 0 or end <= start:
                    raise ValueError(f"'{item}': expected A-B/N:KEYS with A < B and N > 0")
                for f in range(start, min(end, frames), every):
                    for t in range(f, min(f + TAP, frames)):
                        taps[t] |= mask
            else:
                holds.append((int(when), mask))
    holds.sort(key=lambda h: h[0])
    for i, (start, mask) in enumerate(holds):
        end = holds[i + 1][0] if i + 1 < len(holds) else frames
        for f in range(start, min(end, frames)):
            held[f] = mask
    return [h | t for h, t in zip(held, taps)]


def key_events(masks):
    """Per-frame masks → the test runner's --keys text: one event wherever the buttons change."""
    events, last = [], 0
    for frame, mask in enumerate(masks):
        if mask != last:
            names = [n for i, n in enumerate(KEY_NAMES) if mask >> i & 1]
            events.append(f"{frame}:{'+'.join(names) if names else '-'}")
            last = mask
    return ",".join(events) or "0:-"


def same_runs(shots, same):
    """Runs of consecutive pictures that same(a, b) joins, as (first frame, last frame)."""
    runs, i = [], 0
    while i < len(shots):
        j = i
        while j + 1 < len(shots) and same(shots[i], shots[j + 1]):
            j += 1
        runs.append((shots[i][0], shots[j][0]))
        i = j + 1
    return runs


def find_freezes(shots, masks):
    """Unchanged pictures for FREEZE frames or more, after SETTLE, while the buttons changed in between.
    shots: (frame, digest, one colour) in frame order; masks: the buttons per frame."""
    return [(a, b) for a, b in same_runs(shots, lambda x, y: x[1] == y[1])
            if a >= SETTLE and b - a >= FREEZE and len(set(masks[a:b])) > 1]


def find_blank(shots):
    """One-colour pictures (any colour) for BLANK frames or more."""
    return [(a, b) for a, b in same_runs(shots, lambda x, y: x[2] and y[2]) if b - a >= BLANK]


def parse_output(text):
    """The test runner's HASH, AUDIO and NOTE lines."""
    out = {"picture": None, "sound": None, "notes": []}
    for words in (line.split() for line in text.splitlines()):
        if len(words) == 2 and words[0] == "HASH":
            out["picture"] = words[1]
        elif len(words) == 2 and words[0] == "AUDIO":
            out["sound"] = words[1]
        elif len(words) == 5 and words[0] == "NOTE":
            out["notes"].append({"kind": words[1], "detail": words[2], "first": int(words[3]), "count": int(words[4])})
    return out


def notes_summary(notes):
    """The log as the list locks it: sorted kind:detail words, '-' for none."""
    return " ".join(sorted({f"{n['kind']}:{n['detail']}" for n in notes})) or "-"


def folder(game, games_dir):
    if game["source"] == "user":
        return Path(games_dir)
    if game["source"] == "test":
        return TEST_ROMS
    return HOMEBREW


def rom_bytes(game, games_dir):
    """The game's .gba bytes, or None when the file is missing."""
    path = folder(game, games_dir) / game["file"]
    if not path.exists():
        return None
    if path.suffix.lower() != ".zip":
        return path.read_bytes()
    with zipfile.ZipFile(path) as z:
        members = [n for n in z.namelist() if n.lower().endswith(".gba") and not n.startswith("__MACOSX/")]
        if len(members) != 1:
            raise ValueError(f"{path.name}: {len(members)} .gba files inside, expected 1")
        return z.read(members[0])


def run_game(game, args, pictures):
    """Plays one game; a result dict, or None when its file is missing."""
    rom = rom_bytes(game, args.games)
    if rom is None:
        return None
    work = WORK / game["name"]
    if work.exists():
        shutil.rmtree(work)
    (work / "shots").mkdir(parents=True)
    (work / "game.gba").write_bytes(rom)
    masks = expand_keys(game["keys"], game["frames"])
    (work / "keys.txt").write_text(key_events(masks))
    cmd = [args.runner, str(work / "game.gba"), "--frames", str(game["frames"]), "--rtc", str(CLOCK),
           "--keys", "@" + str(work / "keys.txt"), "--hash", "--audio-hash", "--notes"]
    if game["save"] != "-":
        save = folder(game, args.games) / game["save"]
        if not save.exists():
            raise ValueError(f"{game['name']}: the save file {save} is missing")
        shutil.copyfile(save, work / "game.sav")
        cmd += ["--save", str(work / "game.sav")]
    if pictures:
        cmd += ["--shots-every", str(SHOT_EVERY), "--shots-dir", str(work / "shots"), "--wav", str(work / "sound.wav")]
    done = subprocess.run(cmd, capture_output=True, text=True)
    (work / "output.txt").write_text(done.stdout + done.stderr)
    if done.returncode != 0:
        raise RuntimeError(f"{game['name']}: the test runner stopped ({done.returncode}):\n{done.stdout}{done.stderr}")
    result = parse_output(done.stdout)
    result.update(name=game["name"], sha256=hashlib.sha256(rom).hexdigest(), masks=masks, work=work)
    return result


def peak_to_peak(wav_path):
    with wave.open(str(wav_path)) as w:
        samples = array.array("h", w.readframes(w.getnframes()))
    return max(samples) - min(samples) if samples else 0


def analyse(result):
    """Adds the pictures' digests and the flags."""
    from PIL import Image
    shots = []
    for path in sorted((result["work"] / "shots").glob("*.png")):
        with Image.open(path) as im:
            im = im.convert("RGB")
            one_colour = all(lo == hi for lo, hi in im.getextrema())
            shots.append((int(path.stem), hashlib.sha1(im.tobytes()).hexdigest(), one_colour))
    flags = [f"freeze: no change from frame {a} to {b} while buttons changed" for a, b in find_freezes(shots, result["masks"])]
    flags += [f"blank screen: one colour from frame {a} to {b}" for a, b in find_blank(shots)]
    if peak_to_peak(result["work"] / "sound.wav") < QUIET:
        flags.append("silence: no sound in the whole run")
    flags += [f"crash: {n['kind']} at {n['detail']} (frame {n['first']})" for n in result["notes"] if n["kind"] in CRASH_KINDS]
    result["flags"] = flags


def contact_sheet(result, out_path):
    """All pictures at half size, 10 per row, each with its frame number."""
    from PIL import Image, ImageDraw
    paths = sorted((result["work"] / "shots").glob("*.png"))
    cols, w, h, label = 10, 120, 80, 12
    rows = max(1, (len(paths) + cols - 1) // cols)
    sheet = Image.new("RGB", (cols * w, rows * (h + label)), "white")
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(paths):
        x, y = (i % cols) * w, (i // cols) * (h + label)
        with Image.open(path) as im:
            sheet.paste(im.convert("RGB").resize((w, h)), (x, y))
        draw.text((x + 2, y + h), str(int(path.stem)), fill="black")
    sheet.save(out_path)


def write_report(results, missing):
    REPORT.mkdir(parents=True, exist_ok=True)
    parts = ["<!doctype html><meta charset='utf-8'><title>Game check</title>",
             "<style>body{font-family:sans-serif;margin:16px;max-width:1240px}img{max-width:100%}"
             ".flag{color:#b00020}table{border-collapse:collapse}td,th{border:1px solid #999;padding:2px 6px}</style>",
             "<h1>Game check</h1>"]
    for r in results:
        contact_sheet(r, REPORT / f"{r['name']}.png")
        parts.append(f"<h2>{html.escape(r['name'])}</h2>")
        parts += [f"<p class='flag'>{html.escape(f)}</p>" for f in r["flags"]] or ["<p>No flags.</p>"]
        if r["notes"]:
            rows = "".join(f"<tr><td>{html.escape(n['kind'])}</td><td>{n['detail']}</td><td>{n['first']}</td>"
                           f"<td>{n['count']}</td></tr>" for n in r["notes"])
            parts.append(f"<table><tr><th>Warning</th><th>Detail</th><th>First frame</th><th>Count</th></tr>{rows}</table>")
        else:
            parts.append("<p>Warning log: empty.</p>")
        parts.append(f"<p>picture {r['picture']}, sound {r['sound']}</p>"
                     f"<img src='{r['name']}.png' alt='{html.escape(r['name'])}: a picture every second'>")
    parts += [f"<h2>{html.escape(name)}</h2><p>skipped (missing)</p>" for name in missing]
    (REPORT / "index.html").write_text("\n".join(parts), encoding="utf-8")


def check(games, args):
    differ = 0
    for game in games:
        r = run_game(game, args, pictures=False)
        if r is None:
            print(f"{game['name']:<16} skipped (missing)")
            continue
        if game["picture"] == "-":
            print(f"{game['name']:<16} not locked yet")
            continue
        wrong = []
        if r["sha256"] != game["sha256"]:
            wrong.append("the game file differs from the listed one")
        if r["picture"] != game["picture"]:
            wrong.append(f"picture {r['picture']}, locked {game['picture']}")
        if r["sound"] != game["sound"]:
            wrong.append(f"sound {r['sound']}, locked {game['sound']}")
        if notes_summary(r["notes"]) != game["notes"]:
            wrong.append(f"warnings '{notes_summary(r['notes'])}', locked '{game['notes']}'")
        print(f"{game['name']:<16} " + ("ok" if not wrong else "DIFFERENT: " + "; ".join(wrong)))
        differ += bool(wrong)
    return 1 if differ else 0


def lock(games, args, text):
    lines = text.splitlines()
    for game in games:
        r = run_game(game, args, pictures=False)
        if r is None:
            print(f"{game['name']}: skipped (missing), not locked")
            continue
        game.update(sha256=r["sha256"], picture=r["picture"], sound=r["sound"], notes=notes_summary(r["notes"]))
        lines[game["line"] - 1] = " | ".join(str(game[f]) for f in FIELDS)
        print(f"{game['name']}: locked picture {r['picture']}, sound {r['sound']}, warnings {game['notes']}")
    LIST.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    p = argparse.ArgumentParser(description="The game check (milestone 13).")
    p.add_argument("--runner", required=True, help="the test runner built with GBA_NOTES (build/check)")
    p.add_argument("--games", required=True, help="the folder with the user's games")
    p.add_argument("--only", help="one game by name")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="compare with the locked hashes and logs")
    mode.add_argument("--lock", help="lock a game's hashes and log (a name, or all)")
    args = p.parse_args()
    text = LIST.read_text(encoding="utf-8")
    games = parse_list(text)
    wanted = args.lock if args.lock and args.lock != "all" else args.only
    if wanted:
        games = [g for g in games if g["name"] == wanted]
        if not games:
            sys.exit(f"no game named {wanted} in {LIST}")
    if args.check:
        sys.exit(check(games, args))
    if args.lock:
        lock(games, args, text)
        return
    results, missing = [], []
    for game in games:
        r = run_game(game, args, pictures=True)
        if r is None:
            missing.append(game["name"])
            print(f"{game['name']}: skipped (missing)")
            continue
        analyse(r)
        results.append(r)
        print(f"{game['name']}: {len(r['flags'])} flags, {len(r['notes'])} warnings")
        for f in r["flags"]:
            print(f"  {f}")
        for n in r["notes"]:
            print(f"  NOTE {n['kind']} {n['detail']} first frame {n['first']} count {n['count']}")
    write_report(results, missing)
    print(f"report: {REPORT / 'index.html'}")


if __name__ == "__main__":
    main()
```

- [x] **Step 4: Run the tests to see them pass**

Run: `python tools/test_game_check.py`
Expected: `Ran 10 tests … OK`.

- [x] **Step 5: `build/check` and `dev.sh games`** — in `tools/dev.sh`, after `build_release() { … }` add:

```bash
CHECK="$ROOT/build/check"
# build/check: like build/release plus the warning log (GBA_NOTES), for the game check only.
build_check() {
    if [ ! -f "$CHECK/build.ninja" ]; then
        cmake -S "$ROOT" -B "$CHECK" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
            -DGBA_SANITIZE=OFF -DGBA_NOTES=ON
    fi
    cmake --build "$CHECK" --target testrunner
}
```

In the `case`, after `bench)`:

```bash
    games) build_check && (cd "$ROOT" && python tools/game_check.py \
        --runner "$(cygpath -w "$CHECK/tools/testrunner/testrunner.exe")" --games "$(cygpath -w "$GAMES")" "$@") ;;
```

Header comment, after the `bench` lines:

```bash
#   tools/dev.sh games <args>     build/check (release + the warning log), then tools/game_check.py on the games in
#                                 tests/games/games.txt (the user's from $GBA_GAMES, default ~/Downloads): a report
#                                 in build/check/report/; games --check compares with the locked hashes and logs,
#                                 games --lock <name|all> locks them, games --only <name> plays one game
```

and add `games <args>|` to the usage line after `bench <args>|`.

- [x] **Step 6: The list with one always-present game** — create `tests/games/games.txt`:

```
# The game check's games (milestone 13; tools/game_check.py, run with tools/dev.sh games). No game data here.
# name | file | source | sha256 | save | keys | frames | picture | sound | notes
#   name: a-z, 0-9 and _ only. file: the .gba, or a .zip holding one .gba. source: user (the --games folder,
#   default ~/Downloads), test (tests/roms/), or the homebrew download URL (the file is in tests/roms/homebrew/,
#   fetched by tools/fetch_test_roms.py). sha256: of the .gba itself; - until locked. save: a save file next to the
#   game, copied before every run, or -. keys: comma-separated items: F:KEYS holds KEYS from frame F on (- = none);
#   A-B/N:KEYS taps KEYS every N frames from frame A up to B. Buttons: A B SELECT START RIGHT LEFT UP DOWN R L,
#   joined with +. frames: the run's length (60 = one second). picture, sound, notes: set by
#   tools/dev.sh games --lock <name>; - until then.
m7_demo | tonc/m7_demo.gba | test | - | - | 60:UP,400:UP+LEFT,520:UP,900:UP+RIGHT,1000:UP,1300:LEFT,1400:DOWN,1700:DOWN+RIGHT,1800:- | 1800 | - | - | -
```

- [x] **Step 7: Try it end to end**

Run: `bash tools/dev.sh games --only m7_demo`
Expected: `m7_demo: N flags, M warnings` (m7_demo has no sound: a `silence` flag is expected and fine — note it), `report: …\build\check\report\index.html`. Look at `build/check/report/m7_demo.png` (Read tool): 30 pictures of the Mode 7 demo, frame numbers under each.

Run: `bash tools/dev.sh games --lock m7_demo && bash tools/dev.sh games --check`
Expected: `m7_demo: locked picture <16 hex>, sound <16 hex>, warnings …` and then `m7_demo          ok`. `git diff tests/games/games.txt` shows the filled line.

- [x] **Step 8: The checks** (Conventions 1, 2, 5, 6).

- [x] **Step 9: Docs, then commit**

```bash
git add tools/game_check.py tools/test_game_check.py tools/dev.sh tests/games/games.txt docs/
git commit -m "feat(tools): the game check (dev.sh games): runs, flags, contact sheets, report, --check, --lock

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The user's games in the list

**Files:**
- Modify: `tests/games/games.txt`

- [x] **Step 1: Add the user's 11 games** (file names checked 2026-10-05 in `~/Downloads`). Append:

```
# The user's games (source user). The game A hacks share one engine; new games from power-on unless a save is given.
hack_1_save | hack-1.gba | user | - | hack-1.sav | 400:START,404:-,1000-3200/60:A,3300:UP,3500:-,3600:RIGHT,3800:-,3900:DOWN,4100:-,4200:LEFT,4400:-,4500-10800/90:A,4600:UP,5000:RIGHT,5400:DOWN,5800:LEFT,6200:-,6300-10800/600:START,6340-10800/600:B | 10800 | - | - | -
hack_1_new | hack-1.gba | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_2 | hack-2.gba | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_3 | hack-3.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_2b | hack-2b.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_4 | hack-4.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_5 | hack-5.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_6 | hack-6.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
hack_7 | hack-7.zip | user | - | - | 200-10800/40:A,900-10800/700:START | 10800 | - | - | -
game_b | game-b.gba | user | - | - | 950:START,956:-,1010-1200/60:A,1200:UP,1500:UP+RIGHT,1560:UP,1800:UP+LEFT,1880:UP,2100:RIGHT,2160:UP,2400:UP+LEFT,2440:UP,2700:LEFT,2760:UP,3000-10800/30:A,4000:UP+RIGHT,4400:UP,5000:LEFT,5200:UP,6000:DOWN,6400:UP+LEFT,7000:UP,8000:RIGHT,8300:UP | 10800 | - | - | -
game_c | game-c.gba | user | - | - | 700-10800/50:A,4200:DOWN,4500:-,4600:RIGHT,4900:-,5000:UP,5300:-,5400:LEFT,5700:-,6000-10800/900:START,6040-10800/900:B | 10800 | - | - | -
game_d | game-d.gba | user | - | - | 300:A,304:-,900-4600/60:START,930-4600/60:A,4600-10800/20:A | 10800 | - | - | -
```

- [x] **Step 2: Run them and look at every sheet**

Run: `bash tools/dev.sh games`
Expected: one summary per game, none `skipped (missing)`. Read every `build/check/report/<name>.png`.

- [x] **Step 3: Tune each script until the game reaches real play** — for each game whose sheet does not reach play (past the title and intro, into a level, a menu that moves, or walking in the world): change its `keys` (e.g. a different tap rate, START instead of A at a menu, longer holds to walk) and rerun just that game with `bash tools/dev.sh games --only <name>`, until the sheet shows play. A script may stay imperfect when the game itself is the obstacle (e.g. game D's turning games without the rotation sensor) — write that in the game's Completed note. **Do not count a game problem as fixed by changing a script**: anything that looks like an emulator fault goes into spec §10 as found (Task 7 handles it).

- [x] **Done 2026-10-05 beyond these steps** (recorded in PROGRESS and spec §3/§5): the game's own debug messages in the log (`PRINT`), `CW`/`CCW` and `A-B/N/D:KEYS` in the scripts, `hack_2b` dropped (identical file).
- [x] **Step 4: Docs, then commit** (PROGRESS notes: which games reached play, anything odd already seen)

```bash
git add tests/games/games.txt docs/
git commit -m "test(games): the user's 11 games in the game check

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The homebrew games (with the user's OK)

**Files:**
- Modify: `tools/fetch_test_roms.py`, `tests/games/games.txt`

- [x] **Step 1: Pick 20–30 games and demos** (WebSearch / WebFetch). Only files with a **stable direct download URL** (GitHub release assets or raw files, archive.org, the author's own site — not itch.io, whose download links expire) whose author offers them free. Cover, between them (spec §2): every picture mode 0–5, rotation/scaling (BG and sprites), mosaic, blending, windows, many sprites, H-blank effects; Direct Sound with different sound engines (e.g. Maxmod, Apex Audio System, the m4a/MP2K-style "Sappy" engine, GSM playback) and the PSG channels; SRAM, Flash 64/128 KB and EEPROM saves; and anything that calls the BIOS's sound functions (SWI 0x1A–0x2A). Prefer finished games over tech demos; favour the gbadev.net game-jam entries and open-source GBA games (e.g. those built with Butano, libtonc, libgba).

- [x] **Step 2: Ask the user** (AskUserQuestion or a short message): the list with, per game, name, what it covers, source URL and download size, and the total size. **Stop until they answer.** Drop anything they decline.

- [x] **Step 3: Pin the approved files** — download each once to read its size and SHA-256, e.g. `python -c "import hashlib,urllib.request as u;d=u.urlopen(u.Request('<URL>',headers={'User-Agent':'gba-emulator-fetch/1.0'})).read();print(len(d),hashlib.sha256(d).hexdigest())"`, then download it a second time the same way and check that both match (as was done for Tonc). Fill `HOMEBREW` (Step 4) with the values.

- [x] **Step 4: The downloads in `tools/fetch_test_roms.py`** — after `MGBA_SUITE_ROM_SIZE` add:

```python
# Homebrew games for the game check (milestone 13), chosen with the user's OK on 2026-10-05.
# local file name (in tests/roms/homebrew/): (download URL, size in bytes, SHA-256 of the download)
HOMEBREW = {
    # one entry per approved game, e.g.
    # "example.gba": ("https://github.com/<author>/<game>/releases/download/v1.0/example.gba", 1048576, "<64 hex>"),
}
```

(the comment line `# "example.gba"…` is the format; the real entries come from Step 3), and the function:

```python
def fetch_homebrew():
    out_dir = ROOT / "homebrew"
    out_dir.mkdir(parents=True, exist_ok=True)
    failed = []
    for name, (url, size, sha256) in HOMEBREW.items():
        out = out_dir / name
        if out.exists() and out.stat().st_size == size:
            continue
        try:
            data = fetch(url)
        except OSError as e:
            failed.append(f"{name} ({e})")
            continue
        if len(data) != size or hashlib.sha256(data).hexdigest() != sha256:
            failed.append(f"{name} (size or SHA-256 differs from the pinned file)")
            continue
        out.write_bytes(data)
        print(f"homebrew/{name}: {len(data)} bytes")
    if failed:
        sys.exit("homebrew downloads failed: " + ", ".join(failed))
```

In `main()`, after `fetch_mgba_suite()` add `fetch_homebrew()`. Add to the module docstring: `- Homebrew games for the game check: pinned URLs, sizes and SHA-256s (HOMEBREW).`

- [x] **Step 5: Download**

Run: `python tools/fetch_test_roms.py`
Expected: one `homebrew/<name>: N bytes` line per game, then `test ROMs ready`.

- [x] **Step 6: Add them to the list** — one line per game, `source` = its URL, `file` = its name in `tests/roms/homebrew/`, a first script (`200-10800/40:A,900-10800/700:START` is a fair start for menu-driven games; an action game needs holds of the D-pad), `frames` 10800. Under a comment line `# Homebrew (downloaded by tools/fetch_test_roms.py with the user's OK, 2026-10-05).`

- [x] **Step 7: Run, look, tune** — as Task 5 Steps 2–3, with `bash tools/dev.sh games --only <name>` per game.

- [x] **Step 8: Docs, then commit**

```bash
git add tools/fetch_test_roms.py tests/games/games.txt docs/
git commit -m "test(games): homebrew games for the game check (downloaded with the user's OK)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The first full check and the problems list

**Files:**
- Modify: `docs/superpowers/specs/2026-10-05-milestone-13-game-check-design.md` (§10), this plan (the fix tasks)

- [x] **Step 1: Run everything**

Run: `bash tools/dev.sh games`
Expected: a summary per game and the report.

- [x] **Step 2: Judge every game** — read every contact sheet and the summary. For each game decide: **ok** (starts, plays, looks right, no flags or every flag explained — e.g. a game with no sound in its first minutes) or **a problem**. For each warning-log entry decide: **matters** (e.g. a skipped BIOS sound call in a game whose music is missing; a mid-line write in a game whose picture tears or shows a wrong band; code from empty memory) or **harmless** (e.g. a game clearing unused I/O addresses at start-up), with the reason. If a picture looks doubtful and the hardware documentation (GBATEK) does not settle it, ask the user whether to download mGBA for that one comparison.

- [x] **Step 3: Write §10** — one entry per problem: the game(s), what is wrong, the frame, the cause once found (read the code, step the CPU with the test runner, compare with GBATEK), the size of the fix (small / big = more than about a day or a risk to speed), and the harmless log entries with their reasons.

- [x] **Step 4: The fix tasks** — for each small problem, add a task to this plan **before** the exit task (numbered 8, 9, …), in this form:

```
### Task N: Fix — <the problem in a few words>
**Files:** <exact files>
- [x] Step 1: Write the failing test (a unit test in tests/unit/ that shows the wrong behaviour, or a test-ROM
      fingerprint) — <the test's code>
- [x] Step 2: Run it to see it fail: `bash tools/dev.sh unit <filter>` — expected: <the failure>
- [x] Step 3: The fix — <the code>
- [x] Step 4: Run it to see it pass; the game's sheet now shows <what>: `bash tools/dev.sh games --only <name>`
- [x] Step 5: Fingerprints: if any approved frame/audio fingerprint, bench scene hash or locked game changed,
      compare old and new (spec §6) and re-approve or explain
- [x] Step 6: The checks (Conventions 1–6; phone B when the CPU loop, drawing or core/arch/arm64 changed —
      ask the user to plug in the phone)
- [x] Step 7: Docs, then commit ("fix(core): …")
```

with the real tests and code written out (no placeholders). **A big problem** gets no task yet: tell the user what it is, what it would fix and roughly how big, and ask now / later / never; **stop until they answer**, then add a task (now), a line in PROGRESS Next for a later milestone (later), or a §10 note (never).

- [x] **Step 5: Tell the user** in a short message how many problems were found and how many fix tasks were added (not a stop, unless Step 4 needs their decision).

- [x] **Done 2026-10-05:** 30 games judged; 3 fixes added as Tasks 8–10 (all small, none needed the user: MidiKey2Freq; BitUnPack + affine sets via the new BIOS twin tests; the tool's fast-path palette notes); hack 2 and Healz traced to bugs in the games themselves; the rest explained in spec §10. No big problem, so no question for the user.
- [x] **Step 6: Docs, then commit**

```bash
git add docs/
git commit -m "docs: milestone 13 first full check - the problems list and the fix tasks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Fix — the built-in BIOS's MidiKey2Freq (SWI 0x1F) (added 2026-10-05 from the first check)

game D calls SWI 0x1F ~10,000 times in 3 minutes; the built-in BIOS skipped it, so the game's sound driver got the WaveData pointer back as a "frequency" (its music measured ~4 octaves low).

**Files:** `core/src/bios/hle.c` (`midi_key_to_freq`, case 0x1F), `tests/unit/test_hle_sound.c` (new), `tests/unit/CMakeLists.txt` (`GBA_TEST_BIOS`), `tests/unit/test_notes.c` (its example of a skipped call: 0x1A), `tests/bench/scenes.txt` (game_d re-approved)

- [x] Step 1: Failing tests — octaves (key 168 = freq/2, 156 = /4, 60 = >>10, the fine adjust between keys) and a twin test against the user's real BIOS when `tests/bios/gba_bios.bin` exists (1,700 combinations of frequency, key 0–198, fine adjust), run in a machine whose BIOS area holds the real BIOS after the built-in start-up.
- [x] Step 2: Both failed (r0 unchanged; the real BIOS gives e.g. 0x1A2 for key 0).
- [x] Step 3: The fix — whole numbers like the BIOS (no floats, so the phone and the PC agree): a key's factor = round(2^(31 + note/12)) >> (14 − octave), a straight line to the next key's by the fine adjust (fine << 24, high 32 bits of the product), keys > 178 → 178 with fine 255; r0 = high 32 bits of freq × factor.
- [x] Step 4: Both pass — **exact on all 1,700 combinations**. game D's pitches over its first 38 s: the real BIOS's most common pitch +23 semitones (53 windows) — fixed built-in +23 (50 windows, 76 % overlap); old built-in −29/−28 (35 % overlap).
- [x] Step 5: Fingerprints — CTest 121/121 unchanged; bench `game_d` re-approved (picture `b9cd62085af128f1` → `34e400ee6e6fefcf`, sound `dc8d71c5cc96d585` → `e46f1f2a973772d2`): not a drawing change — the game waits for its jingles, which now play at the right speed, so at the scene's last frame it is on the stopwatch screen between microgames instead of mid-game.
- [x] Step 6: The checks — unit 423/423, CTest 121/121, bench ok and above the floors, on-device 78/78, no warnings. (Not the CPU loop, drawing or arm64: no phone B run needed.)
- [x] Step 7: Docs, then commit.

### Task 9: Fix — BitUnPack, ObjAffineSet and BgAffineSet exact; the BIOS twin tests (added 2026-10-05)

While judging hack 2 (Task 7), the twin idea of Task 8 was turned on every built-in BIOS routine: `tests/unit/test_hle_twins.c` runs each call with random inputs in two machines — the built-in BIOS, and the user's real BIOS (skipped without `tests/bios/gba_bios.bin`) — and compares EWRAM, IWRAM (below the BIOS's top 0x200 bytes), VRAM, OAM and the result registers, and prints both machines' cycles.

**Files:** `tests/unit/test_hle_twins.c` (new: CpuSet, CpuFastSet, ObjAffineSet, BgAffineSet, LZ77 W/V, RL W/V, Diff8 W/V, Diff16, BitUnPack, Div, DivArm, Sqrt, ArcTan, ArcTan2, RegisterRamReset), `core/src/bios/hle_memory.c` (BitUnPack), `core/src/bios/hle_math.c` (the sine table, BgAffineSet's arithmetic)

- [x] Step 1: The twin tests — first run: **identical results** for CpuSet, CpuFastSet, both LZ77s, both RLs, the three Diff filters, Div, DivArm, Sqrt, ArcTan, ArcTan2, RegisterRamReset; **different** for BitUnPack (36/80), ObjAffineSet (105/120), BgAffineSet (52/60); UBSan: a signed overflow in BgAffineSet with extreme inputs.
- [x] Step 2: BitUnPack — the BIOS does not cut a unit to the output width after adding the offset (an overflowing unit spills into the next one); the `& dst_mask` removed → 0/80 differ.
- [x] Step 3: The affine sets — the BIOS's sine table is `trunc(16384 × sin(2πi/256))` (ours was rounded: 804 vs 803, …; all 256 entries checked against the user's BIOS); the table regenerated with that formula → 0/120 and 0/60 differ. BgAffineSet's start-point arithmetic in 32-bit wrap-around (no UBSan report).
- [x] Step 4: Fingerprints — CTest 121/121, bench scenes all unchanged (none of them uses these routines in a way that changed).
- [x] Step 5: The checks — unit 433/433, CTest 121/121, bench ok (snd1_demo measured 5,656–6,000 fps across runs: ~6 % noise on that short scene), on-device 78/78, no warnings.
- [x] Step 6: Docs, then commit.

**Timing found by the twins (not fixed here — Task 10):** cycles per call, built-in vs BIOS: CpuSet 2,590 vs 4,086; CpuFastSet 2,999 vs 2,337; LZ77 W 3,068 vs 12,985, V 1,146 vs 19,888; RL W 5,215 vs 13,905, V 2,401 vs 20,315; Diff8 W 6,339 vs 17,778, V 3,739 vs 22,446; Diff16 3,220 vs 8,939; BitUnPack 511 vs 3,905; RegisterRamReset 97 vs 257,192; ObjAffineSet 131 vs 233; BgAffineSet 165 vs 312; ArcTan2 133 vs 366 (Div/Sqrt/ArcTan close: tuned to the mGBA suite in 3c-2).

### Task 10: Fix (the tool) — palette writes from the fast CPU loop in the warning log (added 2026-10-05)

The warning log's midline-palette entry only saw palette writes that went through the slow bus path (`mem_write`); the threaded CPU loop's word/byte stores (`fast_data_ptr`) wrote the palette directly, so the counts were too low and a game that writes the palette only that way showed nothing.

**Files:** `core/src/cpu/cpu_fast.h` (`fast_data_ptr`: `NOTE_DRAWING` for palette writes — empty without `GBA_NOTES`), `tests/unit/test_notes.c` (+1)

- [x] Step 1: Failing test — `str r1, [r0]` to the palette in a loop past the cartridge header (the fast loop leaves the first 0x100 bytes to `cpu_step`), display on, one frame → a midline-palette note with count > 3,000. Without the fix: no note at all.
- [x] Step 2: The fix → 5,373 stores noted. Block stores (STM) only take the fast path for EWRAM/IWRAM, so palette ones already went the slow way.
- [x] Step 3: The checks — unit 434/434, CTest 121/121, bench ok and above the floors, on-device 78/78, no warnings. The app and `build/release` are unchanged (the macro is empty there).
- [x] Step 4: Docs, then commit.

### Task 11: Lock in every game, milestone 13 exit

**Files:**
- Modify: `tests/games/games.txt`, docs

- [x] **Step 1: The final full check** — `bash tools/dev.sh games`; read every sheet once more. Every game reaches play with no crash or freeze; every flag and warning is fixed or explained in spec §10.

- [x] **Step 2: Lock everything**

Run: `bash tools/dev.sh games --lock all && bash tools/dev.sh games --check`
Expected: every game `locked …`, then every game `ok` (none `not locked yet`, none `skipped`).

- [x] **Step 3: The checks** — all of Conventions 1–6; the mGBA suite groups are inside CTest (no score below `tests/suite_targets.json`); compare the bench fps with the Task 1 baseline (≥ 97 %) and write the table into PROGRESS.md.

- [x] **Step 4: Docs** — spec status → implemented / milestone 13 complete; parent spec §10 row 13 done; PROGRESS (Goal, status table row 13 ✅, the 13 task table, Completed, **Next → milestone 14 RetroAchievements: brainstorm its spec**); CLAUDE.md Key files (current milestone spec / plan → none, last finished = 13). Check that nothing is stale.

- [x] **Step 5: Commit and tag**

```bash
git add tests/games/games.txt docs/ CLAUDE.md
git commit -m "docs: milestone 13 complete - every game locked in

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-13
```

- [x] **Done 2026-10-05:** 31 entries locked (the m7 demo, the user's 10 game files as 11 entries — hack 1 from its save and new — and 19 homebrew); `games --check` 31/31 ok. Speed: the PC had slowed down during the final checks (the phone emulator busy in the background: every scene ~40 % slower, old code too), so the 97 % rule was checked A/B — the pre-milestone code (`d9b1c24`) built separately and both benched alternately, 2 rounds × 3 runs: new vs old game_a 727 vs 713, game_b 591 vs 583, game_c 1,087 vs 1,107 (98 %), game_d 603 vs 587, m7 1,396 vs 1,385, snd1 3,549 vs 3,516 fps.
- [x] **Step 6: Report to the user** (no user check — their decision): games checked, problems found and fixed, anything left for later; ask whether to merge `milestone-13` into `main` (superpowers:finishing-a-development-branch).
