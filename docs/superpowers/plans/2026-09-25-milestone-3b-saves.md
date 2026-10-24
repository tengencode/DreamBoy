# Milestone 3b — Save Chips (SRAM, Flash, EEPROM) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Games keep their progress: SRAM, Flash (64K/128K) and EEPROM (512 B/8 KiB) save chips, save-type detection, a public save API, and save files in the test runner and `gbaview`.

**Architecture:** A new `save_t` lives inside `cart_t` (a fixed 128 KiB buffer, no allocation). `core/src/cart/save.c` detects the type at load, owns the public API and routes the save area (0x0E/0x0F: SRAM or Flash) and the EEPROM window (0x0D); `flash.c` and `eeprom.c` hold the two chips' protocols. The bus keeps exact addresses for the 8-bit save area; DMA3 tells the save code its transfer length so the EEPROM size can be detected.

**Tech Stack:** C11 core, Python tools, ARM assembly test ROMs built with the Android NDK's clang (as in milestone 3a).

**Spec:** `docs/superpowers/specs/2026-09-25-milestone-3b-saves-design.md`

---

## Conventions (same as before)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; `CHECK_EQ` compares as `unsigned long long`. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- After every task, update `docs/PROGRESS.md` and tick this plan's checkboxes (project rule in `CLAUDE.md`).
- Work on branch `milestone-3b`.
- The user runs PowerShell, where plain `bash` starts WSL: give them `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh <cmd>`, or start `gbaview` for them with the Bash tool in the background.
- **Test facts:** `tu_io16`/bus accesses cost cycles but run no events; an immediate DMA runs when the clock reaches the cycle of its enabling write, so `tu_advance_to(g, g->now)` right after the write runs it.

## File map

```
core/include/gba.h            + GBA_ERR_SAVE_SIZE, gba_save_type_t and the save API
core/src/cart/save.h          save_t, flash_t, eeprom_t, internal functions (new)
core/src/cart/save.c          detection, API, save-area routing, SRAM, EEPROM window (new)
core/src/cart/flash.c         Flash command state machine (new)
core/src/cart/eeprom.c        EEPROM serial protocol (new)
core/src/cart/cart.h / cart.c + save_t member; save_setup() at load
core/src/mem/bus.c            0x0E/0x0F through save.c with exact addresses; 0x0D EEPROM window
core/src/sys/dma.c            DMA3 reports its length to save.c
core/src/gba.c                gba_reset → save_reset (protocol state only)
tests/unit/testutil.{h,c}     + tu_rom_markers()
tests/unit/test_save.c, test_flash.c, test_eeprom.c   (new)
tools/common/file_util.{h,c}  + write_file()
tools/testrunner/main.c       + --save <file>
tools/viewer/main.c           + <rom>.sav next to the ROM
tools/fetch_test_roms.py      + jsmolka save ROMs
tests/own_roms/src/eeprom.inc, save_eeprom512.S, save_eeprom8k.S   (new; + generated .gba)
CMakeLists.txt                + jsmolka save tests, own_save_* tests
```

---

### Task 1: Save API, detection, SRAM and the save-area bus rules

**Files:**
- Create: `core/src/cart/save.h`, `core/src/cart/save.c`, `tests/unit/test_save.c`
- Modify: `core/include/gba.h`, `core/src/cart/cart.h`, `core/src/cart/cart.c`, `core/src/mem/bus.c`, `core/src/gba.c`, `tests/unit/testutil.h`, `tests/unit/testutil.c`

- [x] **Step 1: Create the branch**

```bash
git checkout -b milestone-3b
```

- [x] **Step 2: Add a ROM-with-markers helper to `tests/unit/testutil.h`**

Add before `#endif`:

```c
/* A booted system whose ROM (size bytes, at least 0x200) starts with "b ." and holds each marker
 * string at 0x100 + 0x40 * i. */
gba_t *tu_rom_markers(const char *const *markers, size_t count, size_t size);
```

- [x] **Step 3: Append it to `tests/unit/testutil.c`**

```c
gba_t *tu_rom_markers(const char *const *markers, size_t count, size_t size) {
    uint8_t *rom = calloc(1, size);
    rom[0] = 0xFE; /* b . */
    rom[1] = 0xFF;
    rom[2] = 0xFF;
    rom[3] = 0xEA;
    for (size_t i = 0; i < count; i++) memcpy(rom + 0x100 + i * 0x40, markers[i], strlen(markers[i]));
    gba_t *g = gba_create(NULL);
    gba_load_rom(g, rom, size);
    free(rom);
    return g;
}
```

- [x] **Step 4: Write the failing tests `tests/unit/test_save.c`**

```c
#include <string.h>

#include "test.h"
#include "testutil.h"

static gba_t *rom_with(const char *marker) {
    const char *list[1] = {marker};
    return tu_rom_markers(list, marker ? 1 : 0, 0x400);
}

TEST(save_detects_marker_strings) {
    static const struct {
        const char *marker;
        gba_save_type_t type;
    } cases[] = {{"EEPROM_V124", GBA_SAVE_EEPROM}, {"SRAM_V113", GBA_SAVE_SRAM},
                 {"SRAM_F_V100", GBA_SAVE_SRAM},   {"FLASH_V126", GBA_SAVE_FLASH64},
                 {"FLASH512_V131", GBA_SAVE_FLASH64}, {"FLASH1M_V103", GBA_SAVE_FLASH128},
                 {NULL, GBA_SAVE_UNKNOWN}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = rom_with(cases[i].marker);
        CHECK_EQ(gba_save_type(g), cases[i].type);
        gba_destroy(g);
    }
}

TEST(save_first_marker_in_the_rom_wins) {
    const char *flash_first[2] = {"FLASH_V126", "SRAM_V113"};
    const char *sram_first[2] = {"SRAM_V113", "FLASH_V126"};
    gba_t *g = tu_rom_markers(flash_first, 2, 0x400);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    gba_destroy(g);
    g = tu_rom_markers(sram_first, 2, 0x400);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    gba_destroy(g);
}

TEST(save_sram_repeats_bytes_and_mirrors) {
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0xFF); /* blank */
    bus_write8(g, 0x0E000010, 0x5A, false);
    CHECK_EQ(bus_read16(g, 0x0E000010, false), 0x5A5A);
    CHECK_EQ(bus_read32(g, 0x0E000010, false), 0x5A5A5A5A);
    CHECK_EQ(bus_read8(g, 0x0E008010, false), 0x5A); /* repeats every 32 KiB */
    CHECK_EQ(bus_read8(g, 0x0F000010, false), 0x5A); /* 0x0F mirrors 0x0E */
    CHECK_EQ(gba_saveram_size(g), 0x8000);
    CHECK_EQ(gba_saveram(g)[0x10], 0x5A);
    gba_destroy(g);
}

TEST(save_sram_wide_writes_store_one_byte) {
    gba_t *g = rom_with("SRAM_V113");
    const uint8_t *d = gba_saveram(g);
    bus_write16(g, 0x0E000021, 0xAABB, false); /* odd address: the high byte */
    CHECK_EQ(d[0x21], 0xAA);
    CHECK_EQ(d[0x20], 0xFF);
    bus_write16(g, 0x0E000030, 0xAABB, false);
    CHECK_EQ(d[0x30], 0xBB);
    CHECK_EQ(d[0x31], 0xFF);
    bus_write32(g, 0x0E000042, 0xAABBCCDD, false); /* byte 2 of the word */
    CHECK_EQ(d[0x42], 0xBB);
    CHECK_EQ(d[0x40], 0xFF);
    CHECK_EQ(d[0x43], 0xFF);
    gba_destroy(g);
}

TEST(save_none_reads_ff_and_ignores_writes) {
    gba_t *g = rom_with("SRAM_V113");
    gba_set_save_type(g, GBA_SAVE_NONE);
    bus_write8(g, 0x0E000000, 0x12, false);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    CHECK_EQ(gba_saveram_size(g), 0);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_NONE);
    gba_destroy(g);
}

TEST(save_unknown_becomes_sram_on_a_plain_write) {
    gba_t *g = rom_with(NULL);
    CHECK_EQ(bus_read8(g, 0x0E000123, false), 0xFF);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_UNKNOWN); /* reading does not decide */
    bus_write8(g, 0x0E000123, 0x77, false);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    CHECK_EQ(bus_read8(g, 0x0E000123, false), 0x77);
    gba_destroy(g);
}

TEST(save_load_saveram_checks_the_size) {
    static uint8_t file[0x20000];
    memset(file, 0x42, sizeof file);
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_OK);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x42);
    CHECK_EQ(gba_saveram_dirty(g), 0); /* loading is not saving */
    CHECK_EQ(gba_load_saveram(g, file, 0x10000), GBA_ERR_SAVE_SIZE);
    gba_destroy(g);

    g = rom_with(NULL); /* unknown: the file size decides */
    CHECK_EQ(gba_load_saveram(g, file, 0x10000), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    gba_destroy(g);

    g = rom_with("EEPROM_V124"); /* EEPROM of unknown size: only EEPROM sizes fit */
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_ERR_SAVE_SIZE);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM);
    CHECK_EQ(gba_load_saveram(g, file, 512), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    gba_destroy(g);

    g = rom_with("SRAM_V113");
    gba_set_save_type(g, GBA_SAVE_NONE);
    CHECK_EQ(gba_load_saveram(g, file, 0x8000), GBA_ERR_SAVE_SIZE);
    gba_destroy(g);
}

TEST(save_dirty_flag_reports_once) {
    gba_t *g = rom_with("SRAM_V113");
    CHECK_EQ(gba_saveram_dirty(g), 0);
    bus_write8(g, 0x0E000000, 1, false);
    CHECK_EQ(gba_saveram_dirty(g), 1);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    gba_destroy(g);
}

TEST(save_set_type_blanks_the_chip) {
    gba_t *g = rom_with("SRAM_V113");
    bus_write8(g, 0x0E000000, 1, false);
    gba_set_save_type(g, GBA_SAVE_FLASH128);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH128);
    CHECK_EQ(gba_saveram_size(g), 0x20000);
    CHECK_EQ(gba_saveram(g)[0], 0xFF);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    gba_destroy(g);
}

TEST(save_survives_a_reset) {
    gba_t *g = rom_with("SRAM_V113");
    bus_write8(g, 0x0E000005, 0x99, false);
    gba_reset(g);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_SRAM);
    CHECK_EQ(bus_read8(g, 0x0E000005, false), 0x99);
    gba_destroy(g);
}
```

- [x] **Step 5: Run to verify failure**

Run: `bash tools/dev.sh unit save_`
Expected: build fails (`gba_save_type` undeclared, `GBA_SAVE_EEPROM` unknown).

- [x] **Step 6: Public API — `core/include/gba.h`**

Replace the `gba_err_t` enum with:

```c
typedef enum {
    GBA_OK = 0,
    GBA_ERR_ROM_SIZE,
    GBA_ERR_BIOS_SIZE,
    GBA_ERR_NOMEM,
    GBA_ERR_SAVE_SIZE, /* a save file whose size does not fit the game's save chip */
} gba_err_t;
```

Before `#ifdef __cplusplus` at the end (after `gba_rom_code`), add:

```c
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
```

- [x] **Step 7: Create `core/src/cart/save.h`**

```c
#ifndef GBA_SAVE_H
#define GBA_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"

#define SAVE_MAX 0x20000u /* the largest chip: 128 KiB Flash */

typedef struct {
    uint8_t state; /* position in a command sequence (FLASH_* in flash.c) */
    bool id_mode;  /* reads of offsets 0 and 1 return the chip ID */
    uint8_t bank;  /* 64 KiB bank of a 128 KiB chip */
} flash_t;

typedef struct {
    uint8_t phase;     /* EE_* in eeprom.c */
    bool writing;      /* the command being received is a write */
    uint8_t count;     /* bits received in the current phase */
    uint8_t reply_pos; /* bits of a read reply already sent (0..67) */
    uint16_t addr;     /* block address */
    uint64_t data;     /* 8 data bytes, first byte in the top bits */
} eeprom_t;

typedef struct {
    gba_save_type_t type;
    bool dirty;
    flash_t flash;
    eeprom_t eeprom;
    uint8_t data[SAVE_MAX]; /* blank = 0xFF */
} save_t;

/* Blank save of the type the ROM's marker strings name (UNKNOWN when there is none). */
void save_setup(save_t *s, const uint8_t *rom, size_t size);
void save_reset(save_t *s); /* forget half-received commands; keeps type and data */
size_t save_type_size(gba_save_type_t type);
/* The save area 0x0E000000-0x0FFFFFFF, one byte at the exact address. */
uint8_t save_read8(gba_t *g, uint32_t addr);
void save_write8(gba_t *g, uint32_t addr, uint8_t value);

#endif
```

- [x] **Step 8: Create `core/src/cart/save.c`**

```c
#include "cart/save.h"

#include <string.h>

#include "gba_internal.h"

static const struct {
    const char *text;
    gba_save_type_t type;
} markers[] = {
    {"EEPROM_V", GBA_SAVE_EEPROM},     {"SRAM_V", GBA_SAVE_SRAM},        {"SRAM_F_V", GBA_SAVE_SRAM},
    {"FLASH1M_V", GBA_SAVE_FLASH128}, {"FLASH_V", GBA_SAVE_FLASH64}, {"FLASH512_V", GBA_SAVE_FLASH64},
};

/* The save libraries leave their name in the ROM, word aligned; the first one found wins. */
static gba_save_type_t detect(const uint8_t *rom, size_t size) {
    for (size_t off = 0; off + 8 <= size; off += 4) {
        uint8_t c = rom[off];
        if (c != 'E' && c != 'S' && c != 'F') continue;
        for (size_t k = 0; k < sizeof markers / sizeof markers[0]; k++) {
            size_t n = strlen(markers[k].text);
            if (off + n <= size && memcmp(rom + off, markers[k].text, n) == 0) return markers[k].type;
        }
    }
    return GBA_SAVE_UNKNOWN;
}

size_t save_type_size(gba_save_type_t type) {
    switch (type) {
    case GBA_SAVE_SRAM: return 0x8000;
    case GBA_SAVE_FLASH64: return 0x10000;
    case GBA_SAVE_FLASH128: return 0x20000;
    case GBA_SAVE_EEPROM512: return 512;
    case GBA_SAVE_EEPROM8K: return 8192;
    default: return 0;
    }
}

static void blank(save_t *s, gba_save_type_t type) {
    s->type = type;
    s->dirty = false;
    memset(s->data, 0xFF, sizeof s->data);
    save_reset(s);
}

void save_setup(save_t *s, const uint8_t *rom, size_t size) {
    blank(s, detect(rom, size));
}

void save_reset(save_t *s) {
    memset(&s->flash, 0, sizeof s->flash);
    memset(&s->eeprom, 0, sizeof s->eeprom);
}

uint8_t save_read8(gba_t *g, uint32_t addr) {
    save_t *s = &g->cart.save;
    if (s->type == GBA_SAVE_SRAM) return s->data[addr & 0x7FFF]; /* 32 KiB, repeated */
    return 0xFF; /* no chip here (yet) */
}

void save_write8(gba_t *g, uint32_t addr, uint8_t value) {
    save_t *s = &g->cart.save;
    if (s->type == GBA_SAVE_UNKNOWN) s->type = GBA_SAVE_SRAM; /* the first write decides */
    if (s->type == GBA_SAVE_SRAM) {
        s->data[addr & 0x7FFF] = value;
        s->dirty = true;
    }
}

/* ---- public API ---- */

gba_save_type_t gba_save_type(const gba_t *g) {
    return g->cart.save.type;
}

uint8_t *gba_saveram(gba_t *g) {
    return g->cart.save.data;
}

size_t gba_saveram_size(const gba_t *g) {
    return save_type_size(g->cart.save.type);
}

/* The type a save file of `size` bytes implies; NONE when it fits nothing. */
static gba_save_type_t type_for_size(size_t size, bool eeprom_only) {
    if (size == 512) return GBA_SAVE_EEPROM512;
    if (size == 8192) return GBA_SAVE_EEPROM8K;
    if (eeprom_only) return GBA_SAVE_NONE;
    if (size == 0x8000) return GBA_SAVE_SRAM;
    if (size == 0x10000) return GBA_SAVE_FLASH64;
    if (size == 0x20000) return GBA_SAVE_FLASH128;
    return GBA_SAVE_NONE;
}

gba_err_t gba_load_saveram(gba_t *g, const uint8_t *data, size_t size) {
    save_t *s = &g->cart.save;
    gba_save_type_t type = s->type;
    if (type == GBA_SAVE_UNKNOWN || type == GBA_SAVE_EEPROM) type = type_for_size(size, type == GBA_SAVE_EEPROM);
    if (size == 0 || size != save_type_size(type)) return GBA_ERR_SAVE_SIZE;
    s->type = type;
    memcpy(s->data, data, size);
    return GBA_OK;
}

int gba_saveram_dirty(gba_t *g) {
    int dirty = g->cart.save.dirty;
    g->cart.save.dirty = false;
    return dirty;
}

void gba_set_save_type(gba_t *g, gba_save_type_t type) {
    blank(&g->cart.save, type);
}
```

- [x] **Step 9: The save lives in the cartridge — `core/src/cart/cart.h` and `cart.c`**

In `cart.h`, add `#include "cart/save.h"` after `#include "gba.h"`, and add the member `save_t save;` after `char code[5];`.

In `cart.c`, in `cart_load`, add before `return GBA_OK;`:

```c
    save_setup(&c->save, rom, size);
```

- [x] **Step 10: Keep the save across resets — `core/src/gba.c`**

In `gba_reset`, add after `irq_reset(g);`:

```c
    save_reset(&g->cart.save);
```

- [x] **Step 11: The save area on the bus — `core/src/mem/bus.c`**

After the `vram_offset` function add:

```c
/* The save area has an 8-bit bus: accesses of every size keep their exact address. */
static inline bool save_area(uint32_t addr) {
    return (addr >> 24) == 0xE || (addr >> 24) == 0xF;
}
```

In `mem_read`, replace

```c
    case 0xE:
    case 0xF: return size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFF; /* save chips arrive in M3 */
```

with

```c
    case 0xE:
    case 0xF: {
        uint32_t byte = save_read8(g, addr); /* wider reads repeat the byte */
        return size == 1 ? byte : size == 2 ? byte * 0x0101u : byte * 0x01010101u;
    }
```

In `mem_write`, replace

```c
    default: break; /* BIOS, ROM, SRAM (M3) and unmapped space ignore writes */
```

with

```c
    case 0xE:
    case 0xF: save_write8(g, addr, (uint8_t)(v >> (8 * (addr & (size - 1))))); break; /* one byte, by address */
    default: break; /* BIOS, ROM and unmapped space ignore writes */
```

In `bus_read16`, `bus_read32`, `bus_write16` and `bus_write32`, replace the alignment line (`addr &= ~1u;` or `addr &= ~3u;`) with the same line guarded:

```c
    if (!save_area(addr)) addr &= ~1u; /* (~3u in the 32-bit functions) */
```

- [x] **Step 12: Run tests to verify they pass**

Run: `bash tools/dev.sh configure` then `bash tools/dev.sh unit` and `bash tools/dev.sh test`
Expected: every unit test passes (223 earlier + 10 `save_*`), no warnings; CTest `100% tests passed` (35).

- [x] **Step 13: Commit**

```bash
git add core tests/unit
git commit -m "feat(save): add save API, marker detection, SRAM and save-area bus rules" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Flash

**Files:**
- Create: `core/src/cart/flash.c`, `tests/unit/test_flash.c`
- Modify: `core/src/cart/save.h`, `core/src/cart/save.c` (`save_read8`, `save_write8`)

- [x] **Step 1: Write the failing tests `tests/unit/test_flash.c`**

```c
#include "test.h"
#include "testutil.h"

static gba_t *flash_rom(const char *marker) {
    const char *list[1] = {marker};
    return tu_rom_markers(list, 1, 0x400);
}

static void flash_cmd(gba_t *g, uint8_t cmd) {
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAA, 0x55, false);
    bus_write8(g, 0x0E005555, cmd, false);
}

static void flash_byte(gba_t *g, uint32_t addr, uint8_t value) {
    flash_cmd(g, 0xA0);
    bus_write8(g, addr, value, false);
}

TEST(flash_id_mode_reports_the_chip) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_cmd(g, 0x90);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x32); /* Panasonic */
    CHECK_EQ(bus_read8(g, 0x0E000001, false), 0x1B);
    flash_cmd(g, 0xF0);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    gba_destroy(g);
    g = flash_rom("FLASH1M_V103");
    flash_cmd(g, 0x90);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x62); /* Sanyo */
    CHECK_EQ(bus_read8(g, 0x0E000001, false), 0x13);
    gba_destroy(g);
}

TEST(flash_writes_need_the_command) {
    gba_t *g = flash_rom("FLASH_V126");
    bus_write8(g, 0x0E000100, 0x12, false); /* no command: ignored */
    CHECK_EQ(bus_read8(g, 0x0E000100, false), 0xFF);
    CHECK_EQ(gba_saveram_dirty(g), 0);
    flash_byte(g, 0x0E000100, 0x12);
    CHECK_EQ(bus_read8(g, 0x0E000100, false), 0x12);
    CHECK_EQ(bus_read16(g, 0x0E000100, false), 0x1212);
    CHECK_EQ(bus_read8(g, 0x0E010100, false), 0x12); /* repeats every 64 KiB */
    CHECK_EQ(gba_saveram_dirty(g), 1);
    bus_write8(g, 0x0E000101, 0x34, false); /* one byte per command */
    CHECK_EQ(bus_read8(g, 0x0E000101, false), 0xFF);
    gba_destroy(g);
}

TEST(flash_chip_and_sector_erase) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_byte(g, 0x0E000010, 0x00);
    flash_byte(g, 0x0E001010, 0x00);
    flash_cmd(g, 0x80);
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAA, 0x55, false);
    bus_write8(g, 0x0E001000, 0x30, false); /* erase sector 1 */
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0x00);
    CHECK_EQ(bus_read8(g, 0x0E001010, false), 0xFF);
    flash_cmd(g, 0x80);
    flash_cmd(g, 0x10); /* erase chip */
    CHECK_EQ(bus_read8(g, 0x0E000010, false), 0xFF);
    gba_destroy(g);
}

TEST(flash_bank_switch_on_128k) {
    gba_t *g = flash_rom("FLASH1M_V103");
    flash_byte(g, 0x0E000020, 0x01);
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 1, false); /* bank 1 */
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0xFF);
    flash_byte(g, 0x0E000020, 0x02);
    CHECK_EQ(gba_saveram(g)[0x10020], 0x02);
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 0, false);
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0x01);
    gba_destroy(g);
}

TEST(flash_64k_ignores_bank_switch) {
    gba_t *g = flash_rom("FLASH_V126");
    flash_cmd(g, 0xB0);
    bus_write8(g, 0x0E000000, 1, false);
    flash_byte(g, 0x0E000020, 0x02);
    CHECK_EQ(gba_saveram(g)[0x20], 0x02);
    gba_destroy(g);
}

TEST(flash_broken_sequence_starts_over) {
    gba_t *g = flash_rom("FLASH_V126");
    bus_write8(g, 0x0E005555, 0xAA, false);
    bus_write8(g, 0x0E002AAB, 0x55, false); /* wrong address */
    bus_write8(g, 0x0E005555, 0xA0, false);
    bus_write8(g, 0x0E000000, 0x12, false);
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0xFF);
    flash_byte(g, 0x0E000000, 0x12); /* a clean sequence still works */
    CHECK_EQ(bus_read8(g, 0x0E000000, false), 0x12);
    gba_destroy(g);
}

TEST(flash_unknown_becomes_flash_on_the_command_prefix) {
    gba_t *g = tu_rom_markers(NULL, 0, 0x400);
    flash_byte(g, 0x0E000005, 0x66);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_FLASH64);
    CHECK_EQ(bus_read8(g, 0x0E000005, false), 0x66);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit flash_`
Expected: link fails or the 7 `flash_*` tests FAIL (Flash reads 0xFF, writes are ignored).

- [x] **Step 3: Declare the chip — `core/src/cart/save.h`**

Add before `#endif`:

```c
/* flash.c: off is the offset within the 64 KiB window (address & 0xFFFF). */
uint8_t flash_read(const save_t *s, uint32_t off);
void flash_write(save_t *s, uint32_t off, uint8_t value);
```

- [x] **Step 4: Create `core/src/cart/flash.c`**

```c
#include <string.h>

#include "cart/save.h"

/* Where a command sequence has got to: prefix = 0xAA at 0x5555, 0x55 at 0x2AAA, command at 0x5555. */
enum {
    FLASH_READY,
    FLASH_AA,
    FLASH_55,
    FLASH_ERASE,    /* got 0x80: waiting for a second prefix */
    FLASH_ERASE_AA,
    FLASH_ERASE_55, /* next: 0x10 at 0x5555 (chip) or 0x30 at a sector */
    FLASH_WRITE,    /* got 0xA0: the next write stores one byte */
    FLASH_BANK,     /* got 0xB0: the next write to offset 0 picks the bank */
};

static uint32_t bank_base(const save_t *s) {
    return (uint32_t)s->flash.bank * 0x10000u;
}

uint8_t flash_read(const save_t *s, uint32_t off) {
    if (s->flash.id_mode && off < 2) {
        bool big = s->type == GBA_SAVE_FLASH128;
        if (off == 0) return big ? 0x62 : 0x32; /* Sanyo : Panasonic */
        return big ? 0x13 : 0x1B;
    }
    return s->data[bank_base(s) + off];
}

static void command(save_t *s, uint8_t value) {
    flash_t *f = &s->flash;
    switch (value) {
    case 0x90: f->id_mode = true; break;
    case 0xF0: f->id_mode = false; break;
    case 0x80: f->state = FLASH_ERASE; break;
    case 0xA0: f->state = FLASH_WRITE; break;
    case 0xB0:
        if (s->type == GBA_SAVE_FLASH128) f->state = FLASH_BANK;
        break;
    default: break;
    }
}

void flash_write(save_t *s, uint32_t off, uint8_t value) {
    flash_t *f = &s->flash;
    uint8_t state = f->state;
    f->state = FLASH_READY; /* anything unexpected starts over */
    switch (state) {
    case FLASH_READY:
        if (off == 0x5555 && value == 0xAA) f->state = FLASH_AA;
        break;
    case FLASH_AA:
        if (off == 0x2AAA && value == 0x55) f->state = FLASH_55;
        break;
    case FLASH_55:
        if (off == 0x5555) command(s, value);
        break;
    case FLASH_ERASE:
        if (off == 0x5555 && value == 0xAA) f->state = FLASH_ERASE_AA;
        break;
    case FLASH_ERASE_AA:
        if (off == 0x2AAA && value == 0x55) f->state = FLASH_ERASE_55;
        break;
    case FLASH_ERASE_55:
        if (off == 0x5555 && value == 0x10) {
            memset(s->data, 0xFF, save_type_size(s->type));
            s->dirty = true;
        } else if (value == 0x30) {
            memset(s->data + bank_base(s) + (off & 0xF000), 0xFF, 0x1000);
            s->dirty = true;
        }
        break;
    case FLASH_WRITE:
        s->data[bank_base(s) + off] = value;
        s->dirty = true;
        break;
    case FLASH_BANK:
        if (off == 0) f->bank = value & 1;
        break;
    default: break;
    }
}
```

- [x] **Step 5: Route Flash — replace `save_read8` and `save_write8` in `core/src/cart/save.c`**

```c
uint8_t save_read8(gba_t *g, uint32_t addr) {
    save_t *s = &g->cart.save;
    switch (s->type) {
    case GBA_SAVE_SRAM: return s->data[addr & 0x7FFF]; /* 32 KiB, repeated */
    case GBA_SAVE_FLASH64:
    case GBA_SAVE_FLASH128: return flash_read(s, addr & 0xFFFF); /* 64 KiB window, repeated */
    default: return 0xFF; /* no chip here (yet) */
    }
}

void save_write8(gba_t *g, uint32_t addr, uint8_t value) {
    save_t *s = &g->cart.save;
    if (s->type == GBA_SAVE_UNKNOWN) { /* the first write decides: a Flash command prefix, or SRAM */
        s->type = ((addr & 0xFFFF) == 0x5555 && value == 0xAA) ? GBA_SAVE_FLASH64 : GBA_SAVE_SRAM;
    }
    switch (s->type) {
    case GBA_SAVE_SRAM:
        s->data[addr & 0x7FFF] = value;
        s->dirty = true;
        break;
    case GBA_SAVE_FLASH64:
    case GBA_SAVE_FLASH128: flash_write(s, addr & 0xFFFF, value); break;
    default: break;
    }
}
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes, including the 7 `flash_*` tests.

- [x] **Step 7: Commit**

```bash
git add core/src/cart tests/unit/test_flash.c
git commit -m "feat(save): add Flash 64K/128K with ID, erase, write and bank commands" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: EEPROM

**Files:**
- Create: `core/src/cart/eeprom.c`, `tests/unit/test_eeprom.c`
- Modify: `core/src/cart/save.h`, `core/src/cart/save.c`, `core/src/mem/bus.c`, `core/src/sys/dma.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_eeprom.c`**

```c
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "testutil.h"

static gba_t *eeprom_rom(size_t size) {
    const char *list[1] = {"EEPROM_V124"};
    return tu_rom_markers(list, 1, size);
}

/* DMA3, halfwords, immediate; runs it. */
static void dma3(gba_t *g, uint32_t src, uint32_t dst, uint32_t count) {
    tu_io32(g, 0x0D4, src);
    tu_io32(g, 0x0D8, dst);
    tu_io16(g, 0x0DC, (uint16_t)count);
    tu_io16(g, 0x0DE, 0x8000);
    tu_advance_to(g, g->now);
}

/* Sends a read (write = false) or write command, most significant bit first, with DMA3. */
static void ee_command(gba_t *g, bool write, unsigned addr_bits, uint32_t addr, uint64_t data) {
    uint16_t bits[2 + 14 + 64 + 1];
    uint32_t n = 0;
    bits[n++] = 1;
    bits[n++] = write ? 0 : 1;
    for (int i = (int)addr_bits - 1; i >= 0; i--) bits[n++] = (uint16_t)((addr >> i) & 1);
    if (write) {
        for (int i = 63; i >= 0; i--) bits[n++] = (uint16_t)((data >> i) & 1);
    }
    bits[n++] = 0;
    for (uint32_t i = 0; i < n; i++) bus_write16(g, 0x02000000 + i * 2, bits[i], false);
    dma3(g, 0x02000000, 0x0D000000, n);
}

/* Fetches a 68-bit reply with DMA3: 4 zero bits, then the 64 data bits. */
static uint64_t ee_reply(gba_t *g) {
    dma3(g, 0x0D000000, 0x02001000, 68);
    uint64_t v = 0;
    for (uint32_t i = 0; i < 68; i++) {
        uint16_t bit = bus_read16(g, 0x02001000 + i * 2, false) & 1;
        if (i < 4) CHECK_EQ(bit, 0);
        else v = v << 1 | bit;
    }
    return v;
}

TEST(eeprom_512_write_then_read) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, true, 6, 1, 0x0123456789ABCDEFull); /* 73 bits: a 512-byte chip */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    CHECK_EQ(gba_saveram(g)[8], 0x01);
    CHECK_EQ(gba_saveram(g)[15], 0xEF);
    CHECK_EQ(gba_saveram_dirty(g), 1);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 1); /* ready */
    ee_command(g, false, 6, 1, 0);
    CHECK_EQ(ee_reply(g), 0x0123456789ABCDEFull);
    gba_destroy(g);
}

TEST(eeprom_8k_uses_14_bit_addresses) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, true, 14, 1023, 0xA5A5A5A55A5A5A5Aull); /* 81 bits: an 8 KiB chip */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    CHECK_EQ(gba_saveram(g)[1023 * 8], 0xA5);
    ee_command(g, false, 14, 1023, 0);
    CHECK_EQ(ee_reply(g), 0xA5A5A5A55A5A5A5Aull);
    ee_command(g, false, 14, 5, 0);
    CHECK_EQ(ee_reply(g), 0xFFFFFFFFFFFFFFFFull); /* never written */
    gba_destroy(g);
}

TEST(eeprom_size_from_the_first_read_request) {
    gba_t *g = eeprom_rom(0x400);
    ee_command(g, false, 14, 0, 0); /* 17 bits */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    gba_destroy(g);
    g = eeprom_rom(0x400);
    ee_command(g, false, 6, 0, 0); /* 9 bits */
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    gba_destroy(g);
}

TEST(eeprom_cpu_first_assumes_8k) {
    gba_t *g = eeprom_rom(0x400);
    bus_write16(g, 0x0D000000, 1, false);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    gba_destroy(g);
}

TEST(eeprom_window_on_32_mib_roms) {
    gba_t *g = eeprom_rom(0x400);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 1); /* small ROM: all of 0x0D is EEPROM */
    gba_destroy(g);
    g = eeprom_rom(0x2000000);
    CHECK_EQ(bus_read16(g, 0x0D000000, false), 0x0000); /* ROM data (zeros) */
    CHECK_EQ(bus_read16(g, 0x0DFFFF00, false), 1);      /* the top 256 bytes are EEPROM */
    gba_destroy(g);
}

TEST(eeprom_unknown_becomes_eeprom_on_dma) {
    gba_t *g = tu_rom_markers(NULL, 0, 0x400);
    ee_command(g, true, 6, 2, 0x1122334455667788ull);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM512);
    CHECK_EQ(gba_saveram(g)[16], 0x11);
    gba_destroy(g);
}

TEST(eeprom_loaded_file_sets_the_size) {
    static uint8_t file[8192];
    memset(file, 0, sizeof file);
    file[8] = 0xC3;
    gba_t *g = eeprom_rom(0x400);
    CHECK_EQ(gba_load_saveram(g, file, sizeof file), GBA_OK);
    CHECK_EQ(gba_save_type(g), GBA_SAVE_EEPROM8K);
    ee_command(g, false, 14, 1, 0);
    CHECK_EQ(ee_reply(g) >> 56, 0xC3);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit eeprom_`
Expected: the 7 `eeprom_*` tests FAIL (0x0D is still plain ROM).

- [x] **Step 3: Declare the chip — `core/src/cart/save.h`**

Add before `#endif`:

```c
/* eeprom.c: one bit per access. */
uint16_t eeprom_read_bit(save_t *s);
void eeprom_write_bit(save_t *s, unsigned bit);
/* save.c: the EEPROM window on 0x0D. */
bool save_eeprom_mapped(const gba_t *g, uint32_t addr);
uint16_t save_eeprom_read(gba_t *g);
void save_eeprom_write(gba_t *g, uint32_t value);
void save_eeprom_dma(gba_t *g, uint32_t dst, uint32_t count); /* a DMA3 to dst of count units starts */
```

- [x] **Step 4: Create `core/src/cart/eeprom.c`**

```c
#include "cart/save.h"

/* Command: 1 1 (read) or 1 0 (write), the block address, 64 data bits (writes only), a stop bit. */
enum { EE_IDLE, EE_CMD, EE_ADDR, EE_DATA, EE_STOP, EE_REPLY };

static unsigned addr_bits(const save_t *s) {
    return s->type == GBA_SAVE_EEPROM512 ? 6 : 14;
}

static uint8_t *block(save_t *s) {
    uint32_t mask = s->type == GBA_SAVE_EEPROM512 ? 0x3Fu : 0x3FFu; /* 64 or 1024 blocks of 8 bytes */
    return s->data + (s->eeprom.addr & mask) * 8u;
}

uint16_t eeprom_read_bit(save_t *s) {
    eeprom_t *e = &s->eeprom;
    if (e->phase != EE_REPLY) return 1; /* ready: writes finish at once */
    uint16_t bit = e->reply_pos < 4 ? 0 : (uint16_t)((e->data >> (67 - e->reply_pos)) & 1);
    if (++e->reply_pos == 68) e->phase = EE_IDLE;
    return bit;
}

void eeprom_write_bit(save_t *s, unsigned bit) {
    eeprom_t *e = &s->eeprom;
    if (s->type == GBA_SAVE_EEPROM) s->type = GBA_SAVE_EEPROM8K; /* no DMA told us the size */
    bit &= 1;
    switch (e->phase) {
    case EE_IDLE:
    case EE_REPLY: /* a new command abandons an unread reply */
        e->phase = bit ? EE_CMD : EE_IDLE;
        break;
    case EE_CMD:
        e->writing = bit == 0;
        e->phase = EE_ADDR;
        e->count = 0;
        e->addr = 0;
        break;
    case EE_ADDR:
        e->addr = (uint16_t)(e->addr << 1 | bit);
        if (++e->count == addr_bits(s)) {
            e->phase = e->writing ? EE_DATA : EE_STOP;
            e->count = 0;
            e->data = 0;
        }
        break;
    case EE_DATA:
        e->data = e->data << 1 | bit;
        if (++e->count == 64) e->phase = EE_STOP;
        break;
    case EE_STOP: {
        uint8_t *b = block(s);
        if (e->writing) {
            for (int i = 0; i < 8; i++) b[i] = (uint8_t)(e->data >> (56 - 8 * i));
            s->dirty = true;
            e->phase = EE_IDLE;
        } else {
            e->data = 0;
            for (int i = 0; i < 8; i++) e->data = e->data << 8 | b[i];
            e->reply_pos = 0;
            e->phase = EE_REPLY;
        }
        break;
    }
    default: e->phase = EE_IDLE; break;
    }
}
```

- [x] **Step 5: The EEPROM window — append to `core/src/cart/save.c`**

```c
/* ---- EEPROM window (0x0D) ---- */

static bool is_eeprom(gba_save_type_t type) {
    return type == GBA_SAVE_EEPROM || type == GBA_SAVE_EEPROM512 || type == GBA_SAVE_EEPROM8K;
}

/* All of 0x0D for ROMs up to 16 MiB; only its last 256 bytes for bigger ROMs. */
static bool eeprom_window(const gba_t *g, uint32_t addr) {
    if ((addr >> 24) != 0xD) return false;
    return g->cart.rom_size <= 0x1000000u || (addr & 0x00FFFFFF) >= 0x00FFFF00;
}

bool save_eeprom_mapped(const gba_t *g, uint32_t addr) {
    return is_eeprom(g->cart.save.type) && eeprom_window(g, addr);
}

uint16_t save_eeprom_read(gba_t *g) {
    return eeprom_read_bit(&g->cart.save);
}

void save_eeprom_write(gba_t *g, uint32_t value) {
    eeprom_write_bit(&g->cart.save, value & 1);
}

void save_eeprom_dma(gba_t *g, uint32_t dst, uint32_t count) {
    save_t *s = &g->cart.save;
    if (!eeprom_window(g, dst)) return;
    if (s->type == GBA_SAVE_UNKNOWN) s->type = GBA_SAVE_EEPROM;
    if (s->type != GBA_SAVE_EEPROM) return;
    /* Read requests are 2 + address + 1 bits, writes 2 + address + 64 + 1: 6-bit or 14-bit addresses. */
    if (count == 9 || count == 73) s->type = GBA_SAVE_EEPROM512;
    else if (count == 17 || count == 81) s->type = GBA_SAVE_EEPROM8K;
}
```

- [x] **Step 6: EEPROM on the bus — `core/src/mem/bus.c`**

In `mem_read`, replace

```c
    case 0xC:
    case 0xD: return cart_rom_read(&g->cart, addr, size);
```

with

```c
    case 0xC: return cart_rom_read(&g->cart, addr, size);
    case 0xD:
        if (save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        return cart_rom_read(&g->cart, addr, size);
```

In `mem_write`, add before `case 0xE:`:

```c
    case 0xD:
        if (save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* the bit is in bit 0 */
        break;
```

- [x] **Step 7: DMA3 reports its length — `core/src/sys/dma.c`**

In `transfer()`, add after the line `uint32_t unit = word ? 4 : 2;`:

```c
    if (n == 3) save_eeprom_dma(g, c->dst, c->count); /* the length gives away the EEPROM size */
```

- [x] **Step 8: Run tests to verify they pass**

Run: `bash tools/dev.sh unit` then `bash tools/dev.sh test`
Expected: every unit test passes (including the 7 `eeprom_*`), CTest `100% tests passed` (35).

- [x] **Step 9: Commit**

```bash
git add core tests/unit/test_eeprom.c
git commit -m "feat(save): add EEPROM 512B/8K with size detection from DMA length" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: jsmolka save ROMs

**Files:**
- Modify: `tools/fetch_test_roms.py`, `CMakeLists.txt`

- [x] **Step 1: Add the ROMs to the fetch list — `tools/fetch_test_roms.py`**

In `JSMOLKA`, add after the `"stripes.gba"` entry:

```python
    "sram.gba": ("save/sram.gba", 2084),
    "flash64.gba": ("save/flash64.gba", 3708),
    "flash128.gba": ("save/flash128.gba", 4096),
    "none.gba": ("save/none.gba", 1628),
```

- [x] **Step 2: Run them in CTest — root `CMakeLists.txt`**

Change the jsmolka `foreach` list from

```cmake
    foreach(entry arm:12 thumb:7 memory:12 bios:12)
```

to

```cmake
    foreach(entry arm:12 thumb:7 memory:12 bios:12 sram:12 flash64:12 flash128:12 none:12)
```

- [x] **Step 3: Download (approved by the user on 2026-09-25) and run**

```bash
python tools/fetch_test_roms.py
bash tools/dev.sh configure
for r in sram flash64 flash128 none; do bash tools/dev.sh run tests/roms/jsmolka/$r.gba --result-reg 12; done
bash tools/dev.sh test
```

Expected: four `jsmolka/<name>.gba: <size> bytes` lines and `test ROMs ready`; then `PASS` for all four; CTest `100% tests passed` (39). If one fails with `FAIL ...: test N`, look up check `tN` in that ROM's source (`save/<name>.asm` / `save/flash.asm` in jsmolka/gba-tests at the pinned commit) and fix it with superpowers:systematic-debugging, reproducing it in a unit test first.

- [x] **Step 4: Commit**

```bash
git add tools/fetch_test_roms.py CMakeLists.txt
git commit -m "test: run jsmolka sram, flash64, flash128 and none save tests" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Our own EEPROM test ROMs

**Files:**
- Create: `tests/own_roms/src/eeprom.inc`, `tests/own_roms/src/save_eeprom512.S`, `tests/own_roms/src/save_eeprom8k.S`, `tests/own_roms/save_eeprom512.gba`, `tests/own_roms/save_eeprom8k.gba` (generated)
- Modify: `CMakeLists.txt`

- [x] **Step 1: Create `tests/own_roms/src/eeprom.inc`**

```asm
@ EEPROM test shared by save_eeprom512.S and save_eeprom8k.S. Define ADDR_BITS (6 or 14) and
@ LAST_BLOCK (63 or 1023) before including this file (after common.inc).
@ r12 = the first failing check (0 = pass); r12 is not the I/O base here, so io16/io32 are not used.
@   1-3 ready bit after writing blocks 1, 2, LAST_BLOCK   4-6 those blocks read back
@   7 block 3 (never written) reads all 0xFF              8 ready bit after writing the run marker
@ Screen (backdrop): green = pass on a blank chip, blue = pass and block 0 already held the run
@ marker (a save from an earlier run was loaded), red = a check failed.

    .equ BUF, 0x02000000   @ outgoing bits, one halfword each
    .equ REPLY, 0x02000400 @ incoming bits
    .equ MARK_HI, 0x52554E21
    .equ MARK_LO, 0x0BADF00D

    .macro expect_ready n
    ldr r0, =0x0D000000
    ldrh r0, [r0]
    tst r0, #1
    moveq r12, #\n
    beq report
    .endm

    .macro write_block block, hi, lo, n
    ldr r8, =\block
    ldr r9, =\hi
    ldr r10, =\lo
    bl ee_write
    expect_ready \n
    .endm

    .macro expect_block block, hi, lo, n
    ldr r8, =\block
    bl ee_read
    ldr r0, =\hi
    cmp r9, r0
    ldreq r0, =\lo
    cmpeq r10, r0
    movne r12, #\n
    bne report
    .endm

    rom_start
    mov r12, #0
    mov r11, #0 @ becomes 1 when the run marker is found
    mov r8, #0
    bl ee_read  @ block 0: the run marker?
    ldr r0, =MARK_HI
    cmp r9, r0
    ldreq r0, =MARK_LO
    cmpeq r10, r0
    moveq r11, #1

    write_block 1, 0x01234567, 0x89ABCDEF, 1
    write_block 2, 0xDEADBEEF, 0x00FF00FF, 2
    write_block LAST_BLOCK, 0xA5A5A5A5, 0x5A5A5A5A, 3
    expect_block 1, 0x01234567, 0x89ABCDEF, 4
    expect_block 2, 0xDEADBEEF, 0x00FF00FF, 5
    expect_block LAST_BLOCK, 0xA5A5A5A5, 0x5A5A5A5A, 6
    expect_block 3, 0xFFFFFFFF, 0xFFFFFFFF, 7
    write_block 0, MARK_HI, MARK_LO, 8

report:
    ldr r1, =0x03E0     @ green
    cmp r11, #1
    ldreq r1, =0x7C00   @ blue: an earlier run's save was loaded
    cmp r12, #0
    ldrne r1, =0x001F   @ red: a check failed
    ldr r0, =0x05000000
    strh r1, [r0]       @ backdrop colour
    frames 2            @ let whole frames show it
done:
    b done

@ put_bits: append the low r1 bits of r0, most significant first, as halfwords at r4 (r4 advances).
put_bits:
    subs r1, r1, #1
    bxmi lr
1:  mov r2, r0, lsr r1
    and r2, r2, #1
    strh r2, [r4], #2
    subs r1, r1, #1
    bpl 1b
    bx lr

@ dma3: copy r7 halfwords from r5 to r6; it runs before the next instruction.
dma3:
    ldr r2, =0x040000D4
    str r5, [r2]
    str r6, [r2, #4]
    orr r3, r7, #0x80000000 @ enable, 16-bit, immediate, both addresses increment
    str r3, [r2, #8]
    bx lr

@ ee_write: write r9:r10 (8 bytes, r9 first) to block r8.
ee_write:
    stmfd sp!, {lr}
    ldr r4, =BUF
    mov r0, #2          @ "1 0": write
    mov r1, #2
    bl put_bits
    mov r0, r8
    mov r1, #ADDR_BITS
    bl put_bits
    mov r0, r9
    mov r1, #32
    bl put_bits
    mov r0, r10
    mov r1, #32
    bl put_bits
    mov r0, #0          @ stop bit
    mov r1, #1
    bl put_bits
    ldr r5, =BUF
    ldr r6, =0x0D000000
    mov r7, #(ADDR_BITS + 67)
    bl dma3
    ldmfd sp!, {pc}

@ ee_read: read block r8 into r9:r10.
ee_read:
    stmfd sp!, {lr}
    ldr r4, =BUF
    mov r0, #3          @ "1 1": read
    mov r1, #2
    bl put_bits
    mov r0, r8
    mov r1, #ADDR_BITS
    bl put_bits
    mov r0, #0
    mov r1, #1
    bl put_bits
    ldr r5, =BUF
    ldr r6, =0x0D000000
    mov r7, #(ADDR_BITS + 3)
    bl dma3
    ldr r5, =0x0D000000
    ldr r6, =REPLY
    mov r7, #68
    bl dma3
    ldr r4, =REPLY + 8  @ skip the 4 dummy bits
    mov r9, #0
    mov r10, #0
    mov r1, #32
1:  ldrh r2, [r4], #2
    and r2, r2, #1
    orr r9, r2, r9, lsl #1
    subs r1, r1, #1
    bne 1b
    mov r1, #32
2:  ldrh r2, [r4], #2
    and r2, r2, #1
    orr r10, r2, r10, lsl #1
    subs r1, r1, #1
    bne 2b
    ldmfd sp!, {pc}

    .ltorg
    .align 2
    .ascii "EEPROM_V124"      @ the save library's marker: this cartridge has an EEPROM
    .byte 0
```

- [x] **Step 2: Create `tests/own_roms/src/save_eeprom512.S`**

```asm
@ save_eeprom512: EEPROM test with a 512-byte chip (6-bit block addresses). See eeprom.inc.
    .equ ADDR_BITS, 6
    .equ LAST_BLOCK, 63
    .include "common.inc"
    .include "eeprom.inc"
```

- [x] **Step 3: Create `tests/own_roms/src/save_eeprom8k.S`**

```asm
@ save_eeprom8k: EEPROM test with an 8 KiB chip (14-bit block addresses). See eeprom.inc.
    .equ ADDR_BITS, 14
    .equ LAST_BLOCK, 1023
    .include "common.inc"
    .include "eeprom.inc"
```

- [x] **Step 4: Run them in CTest — root `CMakeLists.txt`**

After the jsmolka `foreach(...) ... endforeach()` add:

```cmake
    # Our own test ROMs that report through r12 (0 = pass).
    foreach(name save_eeprom512 save_eeprom8k)
        add_test(NAME own_${name} COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/${name}.gba --result-reg 12)
    endforeach()
```

- [x] **Step 5: Build, run and look**

```bash
python tools/build_test_roms.py
bash tools/dev.sh configure
for r in save_eeprom512 save_eeprom8k; do
  bash tools/dev.sh run tests/own_roms/$r.gba --result-reg 12 --screenshot build/shots/$r.png
  python tools/png_probe.py build/shots/$r.png 0 0 239 159
done
bash tools/dev.sh test
```

Expected: `wrote tests/own_roms/save_eeprom512.gba`, `save_eeprom8k.gba` (plus the three sound ROMs, byte-identical to before — `git status` shows them unchanged); `PASS` for both; probes `#00FF00` (green) at both corners; CTest `100% tests passed` (41).

- [x] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/own_roms
git commit -m "test: add our own EEPROM 512B/8K test ROMs" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Save files in the test runner

**Files:**
- Modify: `tools/common/file_util.h`, `tools/common/file_util.c`, `tools/testrunner/main.c`

- [x] **Step 1: `write_file` — `tools/common/file_util.h`**

Add before `#endif`:

```c
/* Write size bytes to path (replacing it). Returns 0 on success. */
int write_file(const char *path, const uint8_t *data, size_t size);
```

- [x] **Step 2: Append to `tools/common/file_util.c`**

```c
int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}
```

- [x] **Step 3: `--save` in `tools/testrunner/main.c`**

In the top comment, change the usage lines to:

```c
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
```

and add after the line about sound:

```c
 * --save loads the file into the save chip first (when it exists) and writes the chip back after.
```

In `usage()`, change the first line of the message to:

```c
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file>]\n"
```

In `main`, change `const char *wav_path = NULL, *expect_audio = NULL;` to:

```c
    const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;
```

and add to the option chain, before the `else if (argv[i][0] != '-' && !rom_path)` line:

```c
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
```

After the `gba_load_rom` check (before `if (wav_path) {`), add:

```c
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
```

After the `expect_audio` check (just before `done:`), add:

```c
    size_t save_bytes = gba_saveram_size(g);
    if (save_path && save_bytes && write_file(save_path, gba_saveram(g), save_bytes) != 0) {
        fprintf(stderr, "cannot write %s\n", save_path);
        code = EXIT_USAGE;
    }
```

- [x] **Step 4: Check that a save survives between runs**

```bash
rm -f build/shots/ee8k.sav
bash tools/dev.sh run tests/own_roms/save_eeprom8k.gba --result-reg 12 --save build/shots/ee8k.sav --screenshot build/shots/ee8k_1.png
bash tools/dev.sh run tests/own_roms/save_eeprom8k.gba --result-reg 12 --save build/shots/ee8k.sav --screenshot build/shots/ee8k_2.png
python tools/png_probe.py build/shots/ee8k_1.png 120 80
python tools/png_probe.py build/shots/ee8k_2.png 120 80
ls -l build/shots/ee8k.sav
head -c 4 /dev/zero > build/shots/bad.sav
bash tools/dev.sh run tests/own_roms/save_eeprom8k.gba --result-reg 12 --save build/shots/bad.sav; echo "exit=$?"
bash tools/dev.sh test
```

Expected: both runs print `PASS`; the first screenshot is `#00FF00` (green, blank chip), the second `#0000FF` (blue: the saved marker was loaded); `ee8k.sav` is 8192 bytes; the 4-byte file is refused with `cannot use ... wrong size` and `exit=3`; CTest `100% tests passed` (41).

- [x] **Step 5: Commit**

```bash
git add tools/common/file_util.h tools/common/file_util.c tools/testrunner/main.c
git commit -m "feat(tools): load and write save files in the test runner" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Save files in `gbaview`

**Files:**
- Modify: `tools/viewer/main.c`

- [x] **Step 1: Save-file helpers**

In the top comment, add after the "Sound plays through..." lines:

```c
 * The save file sits next to the ROM (game.gba → game.sav): loaded at start, written about one
 * second after the game last saved and when the window closes.
```

After the `static int wave_next;` line add:

```c
static char save_path[1024];
static int save_countdown; /* frames until a pending save is written; 0 = nothing pending */
```

Before the `/* ---- sound ---- */` line add:

```c
/* ---- save file ---- */

/* game.gba → game.sav in the same folder. */
static void set_save_path(const char *rom_path) {
    snprintf(save_path, sizeof save_path, "%s", rom_path);
    char *name = save_path;
    for (char *p = save_path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    strncat(save_path, ".sav", sizeof save_path - strlen(save_path) - 1);
}

static void load_save(void) {
    size_t size;
    uint8_t *data = read_file(save_path, &size);
    if (!data) return;
    if (gba_load_saveram(gba, data, size) == GBA_OK) printf("loaded %s\n", save_path);
    else fprintf(stderr, "ignoring %s: wrong size for this game's save chip\n", save_path);
    free(data);
}

/* Written to a temporary file first, then swapped in, so a crash never leaves half a save. */
static void write_save(void) {
    size_t size = gba_saveram_size(gba);
    if (!size) return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", save_path);
    if (write_file(tmp, gba_saveram(gba), size) == 0 && MoveFileExA(tmp, save_path, MOVEFILE_REPLACE_EXISTING)) {
        printf("saved %s\n", save_path);
    } else {
        fprintf(stderr, "cannot write %s\n", save_path);
    }
}
```

- [x] **Step 2: Use them in `main`**

After the `gba_load_rom` check (the block that prints `cannot load ROM`), add:

```c
    set_save_path(rom_path);
    load_save();
```

After `gba_run_frame(gba);` in the main loop add:

```c
        if (gba_saveram_dirty(gba)) save_countdown = 60; /* write once the game has stopped saving */
        else if (save_countdown > 0 && --save_countdown == 0) write_save();
```

After the loop (before `if (sound) audio_close();`) add:

```c
    if (save_countdown > 0) write_save();
```

- [x] **Step 3: Self-test**

```bash
rm -f tests/own_roms/save_eeprom512.sav
bash tools/dev.sh view tests/own_roms/save_eeprom512.gba --mute --autotest 30 build/shots/view_ee_1.png
bash tools/dev.sh view tests/own_roms/save_eeprom512.gba --mute --autotest 30 build/shots/view_ee_2.png
python tools/png_probe.py build/shots/view_ee_1.png 120 80
python tools/png_probe.py build/shots/view_ee_2.png 120 80
ls -l tests/own_roms/save_eeprom512.sav
rm -f tests/own_roms/save_eeprom512.sav
```

Expected: the first run prints `saved tests/own_roms/save_eeprom512.sav` (on exit: 30 frames is less than the 60-frame delay) and shows green `#00FF00`; the second prints `loaded ...` and `saved ...` and shows blue `#0000FF`; the file is 512 bytes (`*.sav` is gitignored).

- [x] **Step 4: Commit**

```bash
git add tools/viewer/main.c
git commit -m "feat(tools): keep game.sav next to the ROM in gbaview" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: User check and milestone 3b exit

**Files:**
- Modify: `tests/expected_frames.json`, `docs/PROGRESS.md`, this plan's checkboxes, the 3b spec's status line, `CLAUDE.md`

- [x] **Step 1: The user sees a save survive**

Delete any old save, then start the ROM for the user in the background (Bash tool, `run_in_background`):

```bash
rm -f tests/own_roms/save_eeprom8k.sav
bash tools/dev.sh view tests/own_roms/save_eeprom8k.gba
```

Ask the user: the window should be **green**; close it with Esc; confirm `tests/own_roms/save_eeprom8k.sav` exists (8,192 bytes). Start it again: now it should be **blue**. Record the answer in `docs/PROGRESS.md`. Then `rm -f tests/own_roms/save_eeprom8k.sav`.

- [x] **Step 2: Approve the green frames as fingerprints**

```bash
bash tools/dev.sh run tests/own_roms/save_eeprom512.gba --frames 10 --hash --screenshot build/shots/ee512_10.png
bash tools/dev.sh run tests/own_roms/save_eeprom8k.gba --frames 10 --hash --screenshot build/shots/ee8k_10.png
python tools/png_probe.py build/shots/ee512_10.png 0 0 239 159
python tools/png_probe.py build/shots/ee8k_10.png 0 0 239 159
python tools/approve_frame.py own_save_eeprom512 ../own_roms/save_eeprom512.gba 10 <HASH>
python tools/approve_frame.py own_save_eeprom8k ../own_roms/save_eeprom8k.gba 10 <HASH>
```

Expected: both probes `#00FF00` at both corners (whole screen green) before approving.

- [x] **Step 3: Full suite with sanitizers**

```bash
bash tools/dev.sh configure
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh unit 2>&1 | grep -E "runtime error|AddressSanitizer" || echo "sanitizers clean"
```

Expected: `100% tests passed, 0 tests failed out of 43` and `sanitizers clean`.

- [x] **Step 4: Update the docs and tag**

Update `docs/PROGRESS.md` (3b done; next: 3b-2 cartridge extras — clock, sensors, rumble, tilt, game database — starting with brainstorming), set the 3b spec's status to implemented, point `CLAUDE.md`'s "current" lines at "none yet — 3b-2 needs brainstorming", then:

```bash
git add docs CLAUDE.md tests/expected_frames.json
git commit -m "docs: milestone 3b complete - save chips" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-3b
```

- [x] **Step 5: Finish the branch**

Use superpowers:finishing-a-development-branch (the user chooses; the repo has no remote). Delete the branch with `git branch -d milestone-3b` (the plain name works for `git branch` even though the tag shares it).

---

## Self-review notes

- **Spec coverage:** §1 scope → header and Task 8; §2 files → file map; §3 API and save state → Task 1; §4 detection incl. detect-on-first-use → Tasks 1 (markers, SRAM), 2 (Flash prefix), 3 (EEPROM by DMA); §5 bus rules → Task 1; §6 Flash → Task 2; §7 EEPROM incl. window, protocol, ready, size detection → Task 3; §8 PC tools → Tasks 4–7; §9 testing → unit tests in Tasks 1–3, CTest ROMs in Tasks 4–5, persistence in Tasks 6–8; §10 done → Task 8.
- **Decisions made while planning:** the save buffer is a fixed 128 KiB array inside `cart_t` (no allocation at all); Flash writes store the byte directly (as mGBA does) rather than AND-ing bits; `bus_peek` of the EEPROM window consumes a reply bit (debug reads never target 0x0D in practice); only DMA3 reports lengths (only DMA3 can reach the EEPROM); the own EEPROM ROMs wait 2 frames after setting the colour so screenshots are fully coloured; r12 holds the result in those ROMs, so they do not use the `io16` macros after start-up.
- **Checked while writing:** jsmolka save ROM sizes from the GitHub API at the pinned commit (sram 2084, flash64 3708, flash128 4096, none 1628); their results are in r12; the jsmolka `memory` test never touches the save area, so detect-on-first-use cannot change it; EEPROM command lengths 9/17 (read) and 73/81 (write).
