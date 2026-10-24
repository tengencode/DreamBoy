# Milestone 2 — Graphics (PPU), DMA, Timers, PC Viewer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw a correct 240×160 picture every frame, add the DMA copier and timers, and give the PC a screenshot-capable test runner and a live viewer — verified by pixel-exact unit tests and approved screenshots of jsmolka and Tonc demo ROMs.

**Architecture:** At each visible line's HBlank the PPU renders each enabled layer into its own 240-entry line buffer (colour, priority, flags), then a compositor applies windows, priority and blending into the framebuffer. Settings are read straight from the I/O backing store (`io.raw`); only the BG2/BG3 affine reference points keep internal state. DMA and timers are scheduler-driven (`EV_DMA`, `EV_TIMER0..3`); timers count lazily. PC tools share a small `gbatools` library (PNG writer, file loading, frame hash).

**Tech Stack:** C11 core (unchanged toolchain from milestone 1), Win32 GDI + winmm for the viewer, Python 3 for tool scripts.

**Spec:** `docs/superpowers/specs/2026-09-24-milestone-2-ppu-dma-timers-design.md`

---

## Conventions (same as milestone 1)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; `CHECK_EQ` compares as `unsigned long long` — write register/colour constants as `uint32_t`/`uint16_t` values.
- Sources are globbed; re-run `bash tools/dev.sh configure` after adding a new directory with a `CMakeLists.txt`.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- After every task, update `docs/PROGRESS.md` and tick this plan's checkboxes (project rule in `CLAUDE.md`).
- Colour constants used in tests (BGR555): black `0x0000`, red `0x001F`, green `0x03E0`, blue `0x7C00`, white `0x7FFF`, yellow `0x03FF`, magenta `0x7C1F`, cyan `0x7FE0`.

## File map

```
core/include/gba.h            + gba_framebuffer()
core/src/gba_internal.h       + timers, dma members
core/src/gba.c                + gba_framebuffer, timer/DMA reset + event dispatch
core/src/ppu/ppu.h            + framebuffer, affine refs, render/latch functions
core/src/ppu/ppu.c            line rendering, affine latch/step, DMA triggers
core/src/ppu/ppu_internal.h   line-buffer types + memory accessors (new)
core/src/ppu/ppu_bg.c         text / affine / bitmap BG renderers (new)
core/src/ppu/ppu_obj.c        sprite renderer (new)
core/src/ppu/ppu_compose.c    windows, priority, blending (new)
core/src/sys/sched.h          + EV_TIMER0..3, EV_DMA
core/src/sys/timer.{h,c}      timers (new)
core/src/sys/dma.{h,c}        DMA (new)
core/src/mem/io.c             + affine-ref, timer and DMA register hooks
core/src/bios/hle.c           + affine identity defaults
tools/common/                 gbatools lib: png.{h,c}, file_util.{h,c}, frame_hash.h (new)
tools/testrunner/main.c       + --frames/--screenshot/--hash/--expect-hash
tools/viewer/                 gbaview.exe (new)
tools/fetch_test_roms.py      + jsmolka ppu ROMs, Tonc demos
tools/approve_frame.py        records approved fingerprints (new)
tools/dev.sh                  + view command
tests/expected_frames.json    approved fingerprints (new)
tests/unit/testutil.{h,c}     + PPU/IO helpers
tests/unit/test_ppu_*.c, test_timer.c, test_dma.c (new)
```

---

### Task 1: Framebuffer, line pipeline and priority compositor

**Files:**
- Create: `core/src/ppu/ppu_internal.h`, `core/src/ppu/ppu_compose.c`, `tests/unit/test_ppu_render.c`
- Modify: `core/include/gba.h`, `core/src/ppu/ppu.h`, `core/src/ppu/ppu.c`, `core/src/gba.c`, `tests/unit/testutil.h`, `tests/unit/testutil.c`

- [x] **Step 1: Create the milestone branch**

```bash
git checkout -b milestone-2
```

- [x] **Step 2: Add PPU/I/O helpers to `tests/unit/testutil.h`**

Replace the file with:

```c
#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <stddef.h>
#include <stdint.h>

#include "gba_internal.h"

#define TU_LEN(a) (sizeof(a) / sizeof((a)[0]))

enum {
    C_BLACK = 0x0000,
    C_RED = 0x001F,
    C_GREEN = 0x03E0,
    C_BLUE = 0x7C00,
    C_WHITE = 0x7FFF,
    C_YELLOW = 0x03FF,
    C_MAGENTA = 0x7C1F,
    C_CYAN = 0x7FE0,
};

/* A system booted (HLE) into a ROM whose code is ops, starting at 0x08000000. */
gba_t *tu_arm(const uint32_t *ops, size_t n);
/* Same, but the CPU starts in Thumb state. */
gba_t *tu_thumb(const uint16_t *ops, size_t n);
/* Execute exactly n instructions (pending IRQs are taken as in cpu_step). */
void tu_steps(gba_t *g, int n);

/* Register and memory pokes through the bus (so I/O side effects happen). */
void tu_io16(gba_t *g, uint32_t off, uint16_t value); /* I/O register at 0x04000000 + off */
void tu_io32(gba_t *g, uint32_t off, uint32_t value);
void tu_pal(gba_t *g, uint32_t index, uint16_t color);   /* palette entry (256+ = OBJ palette) */
void tu_vram16(gba_t *g, uint32_t off, uint16_t value);  /* VRAM at 0x06000000 + off */
void tu_oam16(gba_t *g, uint32_t off, uint16_t value);   /* OAM at 0x07000000 + off */
void tu_hide_sprites(gba_t *g);                          /* disable all 128 sprites */
uint16_t tu_px(const gba_t *g, int x, int y);            /* framebuffer pixel */
void tu_advance_to(gba_t *g, uint64_t t);                /* set the clock, run due events */

#endif
```

- [x] **Step 3: Append the helper implementations to `tests/unit/testutil.c`**

```c
void tu_io16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x04000000 + off, value, false);
}

void tu_io32(gba_t *g, uint32_t off, uint32_t value) {
    bus_write32(g, 0x04000000 + off, value, false);
}

void tu_pal(gba_t *g, uint32_t index, uint16_t color) {
    bus_write16(g, 0x05000000 + index * 2, color, false);
}

void tu_vram16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x06000000 + off, value, false);
}

void tu_oam16(gba_t *g, uint32_t off, uint16_t value) {
    bus_write16(g, 0x07000000 + off, value, false);
}

void tu_hide_sprites(gba_t *g) {
    for (uint32_t i = 0; i < 128; i++) tu_oam16(g, i * 8, 0x0200);
}

uint16_t tu_px(const gba_t *g, int x, int y) {
    return g->ppu.framebuffer[y * PPU_WIDTH + x];
}

void tu_advance_to(gba_t *g, uint64_t t) {
    g->now = t;
    gba_run_events(g);
}
```

- [x] **Step 4: Write the failing tests `tests/unit/test_ppu_render.c`**

```c
#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

TEST(render_backdrop_fills_the_line) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_RED);
    ppu_render_line(g, 5);
    CHECK_EQ(tu_px(g, 0, 5), C_RED);
    CHECK_EQ(tu_px(g, 239, 5), C_RED);
    gba_destroy(g);
}

TEST(render_forced_blank_is_white) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_RED);
    tu_io16(g, 0x000, 0x0080);
    ppu_render_line(g, 3);
    CHECK_EQ(tu_px(g, 0, 3), C_WHITE);
    CHECK_EQ(tu_px(g, 239, 3), C_WHITE);
    gba_destroy(g);
}

TEST(render_frame_loop_draws_every_visible_line) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_pal(g, 0, C_BLUE);
    gba_run_frame(g);
    CHECK_EQ(tu_px(g, 0, 0), C_BLUE);
    CHECK_EQ(tu_px(g, 239, 159), C_BLUE);
    CHECK(gba_framebuffer(g) == g->ppu.framebuffer);
    gba_destroy(g);
}

TEST(compose_orders_layers_by_priority_then_layer) {
    gba_t *g = gba_create(NULL);
    static line_buffers_t lb;
    uint16_t line[PPU_WIDTH];
    memset(&lb, 0, sizeof lb);
    tu_pal(g, 0, C_MAGENTA);
    put_px(&lb.bg[0][0], C_RED, 2, PX_OPAQUE); /* x0: BG1 (prio 1) is in front of BG0 (prio 2) */
    put_px(&lb.bg[1][0], C_GREEN, 1, PX_OPAQUE);
    put_px(&lb.bg[0][1], C_RED, 1, PX_OPAQUE); /* x1: equal priority, BG0 wins */
    put_px(&lb.bg[1][1], C_GREEN, 1, PX_OPAQUE);
    put_px(&lb.bg[2][2], C_BLUE, 0, PX_OPAQUE); /* x2: equal priority, OBJ wins */
    put_px(&lb.obj[2], C_YELLOW, 0, PX_OPAQUE);
    put_px(&lb.bg[3][3], C_BLUE, 3, PX_OPAQUE); /* x3: OBJ prio 1 in front of BG3 prio 3 */
    put_px(&lb.obj[3], C_YELLOW, 1, PX_OPAQUE);
    ppu_compose(g, 0, &lb, line); /* x4: nothing drawn → backdrop */
    CHECK_EQ(line[0], C_GREEN);
    CHECK_EQ(line[1], C_RED);
    CHECK_EQ(line[2], C_YELLOW);
    CHECK_EQ(line[3], C_YELLOW);
    CHECK_EQ(line[4], C_MAGENTA);
    gba_destroy(g);
}
```

- [x] **Step 5: Run to verify failure**

Run: `bash tools/dev.sh unit render`
Expected: build fails: `'ppu/ppu_internal.h' file not found`.

- [x] **Step 6: Replace `core/src/ppu/ppu.h`**

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
#define PPU_WIDTH 240
#define PPU_HEIGHT 160

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
    uint16_t framebuffer[PPU_WIDTH * PPU_HEIGHT]; /* BGR555, row-major */
} ppu_t;

void ppu_reset(gba_t *g); /* clears state and schedules line 0's events relative to g->now */
void ppu_on_hblank(gba_t *g, uint64_t when);
void ppu_on_line_end(gba_t *g, uint64_t when);
void ppu_render_line(gba_t *g, int y); /* draw visible line y into the framebuffer */

#endif
```

- [x] **Step 7: Create `core/src/ppu/ppu_internal.h`**

```c
#ifndef GBA_PPU_INTERNAL_H
#define GBA_PPU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba_internal.h"

enum { LAYER_BG0, LAYER_BG1, LAYER_BG2, LAYER_BG3, LAYER_OBJ, LAYER_BD };

enum {
    PX_OPAQUE = 1 << 0, /* the layer drew a visible pixel here */
    PX_SEMI = 1 << 1,   /* semi-transparent sprite pixel */
};

typedef struct {
    uint16_t color; /* BGR555 */
    uint8_t prio;   /* 0 (front) .. 3 */
    uint8_t flags;  /* PX_* */
} layer_px_t;

typedef struct {
    layer_px_t bg[4][PPU_WIDTH];
    layer_px_t obj[PPU_WIDTH];
    bool objwin[PPU_WIDTH]; /* covered by an OBJ-window sprite */
} line_buffers_t;

/* Little-endian reads from the I/O backing store, VRAM, OAM and palette RAM. */
static inline uint16_t ppu_reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static inline uint32_t ppu_reg32(const gba_t *g, uint32_t off) {
    return ppu_reg(g, off) | (uint32_t)ppu_reg(g, off + 2) << 16;
}

static inline uint16_t vram16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.vram[off] | g->bus.vram[off + 1] << 8);
}

static inline uint16_t oam16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.oam[off] | g->bus.oam[off + 1] << 8);
}

static inline uint16_t pal_color(const gba_t *g, uint32_t index) {
    return (uint16_t)((g->bus.pal[index * 2] | g->bus.pal[index * 2 + 1] << 8) & 0x7FFF);
}

static inline void put_px(layer_px_t *px, uint16_t color, uint8_t prio, uint8_t flags) {
    px->color = color;
    px->prio = prio;
    px->flags = flags;
}

/* ppu_bg.c */
void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out);
/* ppu_obj.c */
void ppu_render_obj(gba_t *g, int y, layer_px_t *out, bool *objwin);
/* ppu_compose.c */
void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);

#endif
```

- [x] **Step 8: Create `core/src/ppu/ppu_compose.c`** (priority only; windows and effects arrive in Task 6)

```c
#include "ppu/ppu_internal.h"

/*
 * The two front-most visible pixels at x. Front to back: lower priority value first;
 * at equal priority OBJ is in front of BG0, BG0 of BG1, and so on. The backdrop is last.
 */
static void find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                         uint16_t color[2], bool *semi) {
    int found = 0;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    *semi = false;
    for (uint8_t prio = 0; prio < 4 && found < 2; prio++) {
        const layer_px_t *o = &lb->obj[x];
        if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE) && o->prio == prio) {
            if (found == 0) *semi = (o->flags & PX_SEMI) != 0;
            layer[found] = LAYER_OBJ;
            color[found++] = o->color;
        }
        for (int bg = 0; bg < 4 && found < 2; bg++) {
            const layer_px_t *p = &lb->bg[bg][x];
            if ((visible & (1u << bg)) && (p->flags & PX_OPAQUE) && p->prio == prio) {
                layer[found] = bg;
                color[found++] = p->color;
            }
        }
    }
}

void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    (void)y;
    uint16_t backdrop = pal_color(g, 0);
    for (int x = 0; x < PPU_WIDTH; x++) {
        int layer[2];
        uint16_t color[2];
        bool semi;
        find_top_two(lb, x, 0x1F, backdrop, layer, color, &semi);
        dst[x] = color[0];
    }
}
```

- [x] **Step 9: Replace `core/src/ppu/ppu.c`**

```c
#include "ppu/ppu.h"

#include <string.h>

#include "gba_internal.h"
#include "ppu/ppu_internal.h"

void ppu_reset(gba_t *g) {
    memset(&g->ppu, 0, sizeof g->ppu);
    sched_add(&g->sched, EV_HBLANK, g->now + PPU_HDRAW_CYCLES);
    sched_add(&g->sched, EV_LINE_END, g->now + PPU_LINE_CYCLES);
}

void ppu_render_line(gba_t *g, int y) {
    uint16_t *dst = &g->ppu.framebuffer[y * PPU_WIDTH];
    if (g->ppu.dispcnt & 0x0080) { /* forced blank */
        for (int x = 0; x < PPU_WIDTH; x++) dst[x] = 0x7FFF;
        return;
    }
    line_buffers_t lb;
    memset(&lb, 0, sizeof lb);
    ppu_compose(g, y, &lb, dst);
}

void ppu_on_hblank(gba_t *g, uint64_t when) {
    (void)when;
    ppu_t *p = &g->ppu;
    if (p->vcount < PPU_VISIBLE_LINES) ppu_render_line(g, p->vcount);
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

- [x] **Step 10: Expose the framebuffer**

In `core/include/gba.h`, after the `gba_set_keys` declaration add:

```c
/* The 240x160 picture in BGR555 (bit 0-4 red, 5-9 green, 10-14 blue), row-major. Lines are
 * drawn as the frame runs, so read it after gba_run_frame returns. */
const uint16_t *gba_framebuffer(const gba_t *g);
```

In `core/src/gba.c`, after `gba_set_keys` add:

```c
const uint16_t *gba_framebuffer(const gba_t *g) {
    return g->ppu.framebuffer;
}
```

- [x] **Step 11: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes, including the 4 new `render_*`/`compose_*` tests.

- [x] **Step 12: Commit**

```bash
git add core tests/unit
git commit -m "feat(ppu): add framebuffer, per-line pipeline and priority compositor" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Tiled (text) backgrounds

**Files:**
- Create: `core/src/ppu/ppu_bg.c`, `tests/unit/test_ppu_text.c`
- Modify: `core/src/ppu/ppu.c` (`ppu_render_line`)

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu_text.c`**

```c
#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define SCREEN30 0xF000u /* BG screen block 30 */
#define SCREEN31 0xF800u /* BG screen block 31 */

/* Mode 0 with only BG0 on; backdrop black. */
static gba_t *text_scene(uint16_t bg0cnt) {
    gba_t *g = gba_create(NULL);
    tu_pal(g, 0, C_BLACK);
    tu_io16(g, 0x000, 0x0100);
    tu_io16(g, 0x008, bg0cnt);
    return g;
}

/* 4bpp tile (char base 0) where every pixel has colour index `index`. */
static void solid_tile4(gba_t *g, uint32_t tile, uint16_t index) {
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, tile * 32 + i, (uint16_t)(index * 0x1111));
}

static void map_entry(gba_t *g, uint32_t screen, int tx, int ty, uint16_t entry) {
    tu_vram16(g, screen + (uint32_t)(ty * 32 + tx) * 2, entry);
}

TEST(text_bg_4bpp_uses_palette_bank) {
    gba_t *g = text_scene(31 << 8);
    tu_vram16(g, 32 + 0, 0x4321); /* tile 1, row 0: indices 1..7 then 0 */
    tu_vram16(g, 32 + 2, 0x0765);
    for (uint32_t i = 1; i <= 7; i++) tu_pal(g, 32 + i, (uint16_t)(0x0100 + i));
    map_entry(g, SCREEN31, 0, 0, (2 << 12) | 1); /* palette bank 2 */
    ppu_render_line(g, 0);
    for (int x = 0; x < 7; x++) CHECK_EQ(tu_px(g, x, 0), 0x0100 + x + 1);
    CHECK_EQ(tu_px(g, 7, 0), C_BLACK);
    gba_destroy(g);
}

TEST(text_bg_8bpp) {
    gba_t *g = text_scene((31 << 8) | 0x80);
    tu_vram16(g, 64 + 0, 0x0201); /* tile 1 (64 bytes), row 0: indices 1..8 */
    tu_vram16(g, 64 + 2, 0x0403);
    tu_vram16(g, 64 + 4, 0x0605);
    tu_vram16(g, 64 + 6, 0x0807);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, i, (uint16_t)(0x0200 + i));
    map_entry(g, SCREEN31, 0, 0, (5 << 12) | 1); /* the palette bank is ignored in 8bpp */
    ppu_render_line(g, 0);
    for (int x = 0; x < 8; x++) CHECK_EQ(tu_px(g, x, 0), 0x0200 + x + 1);
    gba_destroy(g);
}

TEST(text_bg_scroll_wraps_around_the_map) {
    gba_t *g = text_scene(31 << 8);
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1);
    tu_io16(g, 0x010, 250); /* BG0HOFS: screen x 6 is map x 256 = 0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 5, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 6, 0), C_RED);
    CHECK_EQ(tu_px(g, 13, 0), C_RED);
    CHECK_EQ(tu_px(g, 14, 0), C_BLACK);
    tu_io16(g, 0x012, 4); /* BG0VOFS */
    ppu_render_line(g, 4);
    CHECK_EQ(tu_px(g, 6, 4), C_BLACK); /* map y 8 is the next tile row */
    gba_destroy(g);
}

TEST(text_bg_flips) {
    gba_t *g = text_scene(31 << 8);
    tu_vram16(g, 32, 0x0001); /* tile 1: only pixel (0,0) is set */
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1 | 0x400); /* horizontal flip */
    map_entry(g, SCREEN31, 1, 0, 1 | 0x800); /* vertical flip */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    ppu_render_line(g, 7);
    CHECK_EQ(tu_px(g, 8, 7), C_RED);
    gba_destroy(g);
}

TEST(text_bg_512_wide_map_uses_the_next_screen_block) {
    gba_t *g = text_scene((30 << 8) | 0x4000); /* 512x256: blocks 30 (left) and 31 (right) */
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    map_entry(g, SCREEN31, 0, 0, 1); /* map x 256 */
    tu_io16(g, 0x010, 256);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    gba_destroy(g);
}

TEST(text_bg_priority_and_layer_order) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0300); /* BG0 and BG1 */
    solid_tile4(g, 1, 1);
    tu_pal(g, 1, C_RED);
    tu_pal(g, 17, C_GREEN);
    tu_io16(g, 0x008, (31 << 8) | 1);          /* BG0 priority 1 */
    tu_io16(g, 0x00A, (30 << 8) | 0);          /* BG1 priority 0 */
    map_entry(g, SCREEN31, 0, 0, 1);            /* BG0 red */
    map_entry(g, SCREEN30, 0, 0, (1 << 12) | 1); /* BG1 green */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_GREEN);
    tu_io16(g, 0x00A, (30 << 8) | 1); /* equal priority: BG0 is in front */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(text_bg_mosaic) {
    gba_t *g = text_scene((31 << 8) | 0x40);
    tu_vram16(g, 32 + 0, 0x4321); /* tile 1, row 0: indices 1..8 */
    tu_vram16(g, 32 + 2, 0x8765);
    tu_vram16(g, 32 + 8, 0x0002); /* row 2: pixel 0 = index 2 */
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, i, (uint16_t)(0x0100 + i));
    map_entry(g, SCREEN31, 0, 0, 1);
    tu_io16(g, 0x04C, 0x0003); /* 4-pixel-wide blocks */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), 0x0101);
    CHECK_EQ(tu_px(g, 3, 0), 0x0101);
    CHECK_EQ(tu_px(g, 4, 0), 0x0105);
    CHECK_EQ(tu_px(g, 7, 0), 0x0105);
    tu_io16(g, 0x04C, 0x0030); /* 4-line-tall blocks: line 2 repeats line 0 */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 0, 2), 0x0101);
    tu_io16(g, 0x008, 31 << 8); /* mosaic off: row 2 shows index 2 */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 0, 2), 0x0102);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit text_bg`
Expected: all 7 tests FAIL (only the backdrop is drawn).

- [x] **Step 3: Create `core/src/ppu/ppu_bg.c`**

```c
#include "ppu/ppu_internal.h"

/* BG mosaic block size (1 = off) from MOSAIC when the BG's mosaic bit is set. */
static void bg_mosaic(const gba_t *g, uint16_t cnt, int *mh, int *mv) {
    uint16_t m = ppu_reg(g, 0x04C);
    bool on = (cnt & 0x40) != 0;
    *mh = on ? (m & 0xF) + 1 : 1;
    *mv = on ? ((m >> 4) & 0xF) + 1 : 1;
}

void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256, height = (cnt & 0x8000) ? 512 : 256;
    int mh, mv;
    bg_mosaic(g, cnt, &mh, &mv);
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);

    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        /* Maps wider or taller than 256 are 32x32-entry screen blocks: left to right, then top to bottom. */
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM */
            index = g->bus.vram[addr];
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4 + tx / 2;
            if (addr >= 0x10000) continue;
            index = (tx & 1) ? g->bus.vram[addr] >> 4 : g->bus.vram[addr] & 0xFu;
            if (index) index |= (uint32_t)(entry >> 12) << 4;
        }
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}
```

- [x] **Step 4: Replace `ppu_render_line` in `core/src/ppu/ppu.c`**

```c
void ppu_render_line(gba_t *g, int y) {
    uint16_t *dst = &g->ppu.framebuffer[y * PPU_WIDTH];
    uint16_t dispcnt = g->ppu.dispcnt;
    if (dispcnt & 0x0080) { /* forced blank */
        for (int x = 0; x < PPU_WIDTH; x++) dst[x] = 0x7FFF;
        return;
    }
    line_buffers_t lb;
    memset(&lb, 0, sizeof lb);
    switch (dispcnt & 7) {
    case 0:
        for (int bg = 0; bg < 4; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    case 1:
        for (int bg = 0; bg < 2; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    default: break;
    }
    ppu_compose(g, y, &lb, dst);
}
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 7 `text_bg_*` tests.

- [x] **Step 6: Commit**

```bash
git add core/src/ppu tests/unit/test_ppu_text.c
git commit -m "feat(ppu): render tiled backgrounds with scrolling, flips and mosaic" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Rotating/scaling (affine) backgrounds

**Files:**
- Modify: `core/src/ppu/ppu.h`, `core/src/ppu/ppu.c`, `core/src/ppu/ppu_bg.c`, `core/src/mem/io.c`, `core/src/bios/hle.c`
- Test: `tests/unit/test_ppu_affine.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu_affine.c`**

```c
#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define MAP16 0x8000u /* BG screen block 16 */

/* Mode 1 with BG2 on: 128x128 affine map at screen block 16, tile 1 solid red at map (0,0). */
static gba_t *affine_scene(uint16_t extra_cnt) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0401);
    tu_io16(g, 0x00C, (uint16_t)(extra_cnt | (16 << 8)));
    for (uint32_t i = 0; i < 64; i += 2) tu_vram16(g, 64 + i, 0x0101); /* 8bpp tile 1: index 1 */
    tu_pal(g, 1, C_RED);
    tu_vram16(g, MAP16, 0x0001); /* map bytes: (0,0) = tile 1, (1,0) = tile 0 */
    return g;
}

TEST(affine_matrices_default_to_identity) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read16(g, 0x04000020, false), 0x100); /* BG2PA */
    CHECK_EQ(bus_read16(g, 0x04000026, false), 0x100); /* BG2PD */
    CHECK_EQ(bus_read16(g, 0x04000030, false), 0x100); /* BG3PA */
    CHECK_EQ(bus_read16(g, 0x04000036, false), 0x100); /* BG3PD */
    gba_destroy(g);
}

TEST(affine_bg_identity) {
    gba_t *g = affine_scene(0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    CHECK_EQ(tu_px(g, 8, 0), C_BLACK);
    gba_destroy(g);
}

TEST(affine_bg_scaling) {
    gba_t *g = affine_scene(0);
    tu_io16(g, 0x020, 0x80); /* PA = 0.5: twice as wide */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 15, 0), C_RED);
    CHECK_EQ(tu_px(g, 16, 0), C_BLACK);
    gba_destroy(g);
}

TEST(affine_bg_wraparound_flag) {
    gba_t *g = affine_scene(0);
    tu_io32(g, 0x028, 128 << 8); /* BG2X = 128: past the right edge of a 128-pixel map */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    tu_io16(g, 0x00C, (16 << 8) | 0x2000); /* wraparound on */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(affine_reference_reloads_on_write_with_sign_extension) {
    gba_t *g = gba_create(NULL);
    tu_io32(g, 0x028, 0x0FFFFF00); /* 28-bit -256 */
    CHECK_EQ(g->ppu.ref_x[0], -256);
    tu_io32(g, 0x03C, 0x00000500); /* BG3Y */
    CHECK_EQ(g->ppu.ref_y[1], 0x500);
    CHECK_EQ(g->ppu.ref_x[0], -256); /* writing Y leaves X alone */
    gba_destroy(g);
}

TEST(affine_reference_steps_per_line_and_reloads_at_vblank) {
    gba_t *g = affine_scene(0);
    tu_io16(g, 0x026, 0x200); /* PD = 2.0 */
    tu_advance_to(g, PPU_HDRAW_CYCLES); /* line 0 drawn */
    CHECK_EQ(g->ppu.ref_y[0], 0x200);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES); /* VBlank reloads from BG2Y (0) */
    CHECK_EQ(g->ppu.ref_y[0], 0);
    gba_destroy(g);
}

TEST(affine_bg3_in_mode_2) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0802); /* mode 2, BG3 */
    tu_io16(g, 0x00E, 16 << 8);
    for (uint32_t i = 0; i < 64; i += 2) tu_vram16(g, 64 + i, 0x0101);
    tu_pal(g, 1, C_GREEN);
    tu_vram16(g, MAP16, 0x0001);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_GREEN);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit affine`
Expected: build fails: no member `ref_x` in `ppu_t`.

- [x] **Step 3: Update `core/src/ppu/ppu.h`**

Replace the `ppu_t` struct and the function list with:

```c
typedef struct {
    uint16_t dispcnt, dispstat, vcount;
    int32_t ref_x[2], ref_y[2];                   /* internal BG2/BG3 affine reference points (20.8) */
    uint16_t framebuffer[PPU_WIDTH * PPU_HEIGHT]; /* BGR555, row-major */
} ppu_t;

void ppu_reset(gba_t *g); /* clears state and schedules line 0's events relative to g->now */
void ppu_on_hblank(gba_t *g, uint64_t when);
void ppu_on_line_end(gba_t *g, uint64_t when);
void ppu_render_line(gba_t *g, int y); /* draw visible line y into the framebuffer */
void ppu_latch_affine(gba_t *g);       /* reload both reference points from BG2X/Y and BG3X/Y */
void ppu_affine_ref_written(gba_t *g, uint32_t off); /* a BGxX/BGxY byte at I/O offset off changed */
```

- [x] **Step 4: Add the affine renderer to the end of `core/src/ppu/ppu_bg.c`**

```c
typedef struct {
    int32_t pa, pc; /* texture step per screen pixel (8.8) */
    int32_t x0, y0; /* texture position of screen pixel 0 (20.8) */
    int mh;         /* horizontal mosaic block */
} affine_line_t;

/* Texture walk for line y of BG2/BG3. Vertical mosaic reuses the first line of the block by
 * stepping the reference back (exact while the parameters stay constant over the block). */
static affine_line_t affine_line(const gba_t *g, int bg, int y, uint16_t cnt) {
    uint32_t params = bg == 2 ? 0x020 : 0x030;
    affine_line_t a;
    int mv;
    bg_mosaic(g, cnt, &a.mh, &mv);
    int back = y % mv;
    a.pa = (int16_t)ppu_reg(g, params);
    a.pc = (int16_t)ppu_reg(g, params + 4);
    a.x0 = g->ppu.ref_x[bg - 2] - back * (int16_t)ppu_reg(g, params + 2);
    a.y0 = g->ppu.ref_y[bg - 2] - back * (int16_t)ppu_reg(g, params + 6);
    return a;
}

void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    int32_t size = 128 << ((cnt >> 14) & 3);
    bool wrap = (cnt & 0x2000) != 0;
    affine_line_t a = affine_line(g, bg, y, cnt);

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            continue;
        }
        uint32_t map = screen_base + (uint32_t)(ty >> 3) * (uint32_t)(size >> 3) + (uint32_t)(tx >> 3);
        uint32_t tile = g->bus.vram[map & 0xFFFF];
        uint32_t addr = char_base + tile * 64 + (uint32_t)(ty & 7) * 8 + (uint32_t)(tx & 7);
        if (addr >= 0x10000) continue;
        uint32_t index = g->bus.vram[addr];
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}
```

- [x] **Step 5: Update `core/src/ppu/ppu.c`**

Add these functions after `ppu_reset`:

```c
static int32_t sign_extend28(uint32_t v) {
    return (int32_t)(v << 4) >> 4;
}

void ppu_latch_affine(gba_t *g) {
    for (int i = 0; i < 2; i++) {
        uint32_t base = i == 0 ? 0x028 : 0x038;
        g->ppu.ref_x[i] = sign_extend28(ppu_reg32(g, base));
        g->ppu.ref_y[i] = sign_extend28(ppu_reg32(g, base + 4));
    }
}

void ppu_affine_ref_written(gba_t *g, uint32_t off) {
    int i = off < 0x030 ? 0 : 1;
    uint32_t base = i == 0 ? 0x028 : 0x038;
    if (off - base < 4) g->ppu.ref_x[i] = sign_extend28(ppu_reg32(g, base));
    else g->ppu.ref_y[i] = sign_extend28(ppu_reg32(g, base + 4));
}

/* After each drawn line the references move by (PB, PD). */
static void affine_step(gba_t *g) {
    for (int i = 0; i < 2; i++) {
        uint32_t params = i == 0 ? 0x020 : 0x030;
        g->ppu.ref_x[i] += (int16_t)ppu_reg(g, params + 2);
        g->ppu.ref_y[i] += (int16_t)ppu_reg(g, params + 6);
    }
}
```

Replace `ppu_render_line`'s `switch` with:

```c
    switch (dispcnt & 7) {
    case 0:
        for (int bg = 0; bg < 4; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    case 1:
        for (int bg = 0; bg < 2; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_text_bg(g, bg, y, lb.bg[bg]);
        }
        if (dispcnt & 0x400) ppu_render_affine_bg(g, 2, y, lb.bg[2]);
        break;
    case 2:
        for (int bg = 2; bg < 4; bg++) {
            if (dispcnt & (0x100 << bg)) ppu_render_affine_bg(g, bg, y, lb.bg[bg]);
        }
        break;
    default: break;
    }
```

In `ppu_on_hblank`, replace the render line with:

```c
    if (p->vcount < PPU_VISIBLE_LINES) {
        ppu_render_line(g, p->vcount);
        affine_step(g);
    }
```

In `ppu_on_line_end`, inside `if (p->vcount == PPU_VISIBLE_LINES) {`, add as the first statement:

```c
        ppu_latch_affine(g);
```

- [x] **Step 6: Reload references when a game writes them — `core/src/mem/io.c`**

Replace the `default:` branch of `write16` with:

```c
    default:
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if ((off >= 0x028 && off < 0x030) || (off >= 0x038 && off < 0x040)) ppu_affine_ref_written(g, off);
        break;
```

- [x] **Step 7: Identity matrices after boot — `core/src/bios/hle.c`**

Add after `bios_word`:

```c
/* The BIOS leaves the BG2/BG3 affine matrices at identity (PA = PD = 1.0). */
static void set_affine_identity(gba_t *g) {
    static const uint32_t regs[] = {0x020, 0x026, 0x030, 0x036};
    for (int i = 0; i < 4; i++) {
        g->io.raw[regs[i]] = 0x00;
        g->io.raw[regs[i] + 1] = 0x01;
    }
}
```

In `hle_boot`, add after `g->io.postflg = 1;`:

```c
    set_affine_identity(g);
```

In `register_ram_reset`, inside `if (flags & 0x80) {`, add as the last statement:

```c
        set_affine_identity(g);
```

- [x] **Step 8: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 7 `affine_*` tests.

- [x] **Step 9: Commit**

```bash
git add core tests/unit/test_ppu_affine.c
git commit -m "feat(ppu): render affine backgrounds with reference latching" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Bitmap modes 3, 4 and 5

**Files:**
- Modify: `core/src/ppu/ppu_bg.c`, `core/src/ppu/ppu.c`
- Test: `tests/unit/test_ppu_bitmap.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu_bitmap.c`**

```c
#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

TEST(bitmap_mode3_direct_colour) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0403);
    tu_pal(g, 0, C_MAGENTA);
    tu_vram16(g, (2 * 240 + 5) * 2, C_GREEN);
    tu_io32(g, 0x02C, 2 << 8); /* BG2Y: the affine reference only moves as a frame runs */
    ppu_render_line(g, 2);
    CHECK_EQ(tu_px(g, 5, 2), C_GREEN);
    CHECK_EQ(tu_px(g, 6, 2), C_BLACK); /* colour 0 is opaque black in mode 3 */
    gba_destroy(g);
}

TEST(bitmap_mode4_palette_and_page_flip) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0404);
    tu_pal(g, 0, C_MAGENTA);
    tu_pal(g, 2, C_BLUE);
    tu_pal(g, 3, C_RED);
    tu_vram16(g, 240 + 2, 0x0200);          /* page 0, line 1: (2,1) = 0, (3,1) = 2 */
    tu_vram16(g, 0xA000 + 240 + 2, 0x0300); /* page 1: (3,1) = 3 */
    tu_io32(g, 0x02C, 1 << 8);              /* BG2Y = line 1 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 3, 1), C_BLUE);
    CHECK_EQ(tu_px(g, 2, 1), C_MAGENTA); /* index 0 is transparent */
    tu_io16(g, 0x000, 0x0414);           /* show page 1 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 3, 1), C_RED);
    gba_destroy(g);
}

TEST(bitmap_mode5_small_frame) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0415); /* mode 5, page 1, BG2 */
    tu_pal(g, 0, C_MAGENTA);
    tu_vram16(g, 0xA000 + 10 * 2, C_YELLOW);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 10, 0), C_YELLOW);
    CHECK_EQ(tu_px(g, 160, 0), C_MAGENTA); /* the frame is 160 pixels wide */
    gba_destroy(g);
}

TEST(bitmap_uses_affine_scaling) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x000, 0x0403);
    tu_io16(g, 0x020, 0x80); /* PA = 0.5 */
    tu_vram16(g, 5 * 2, C_RED);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 10, 0), C_RED);
    CHECK_EQ(tu_px(g, 11, 0), C_RED);
    CHECK_EQ(tu_px(g, 12, 0), C_BLACK);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit bitmap`
Expected: the 4 tests FAIL.

- [x] **Step 3: Add the bitmap renderer to the end of `core/src/ppu/ppu_bg.c`**

```c
/* Modes 3-5 draw BG2 as a bitmap sampled through the BG2 affine transform. */
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    affine_line_t a = affine_line(g, 2, y, cnt);

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        uint32_t pos = (uint32_t)(ty * w + tx);
        uint16_t color;
        if (mode == 4) {
            uint32_t index = g->bus.vram[page + pos];
            if (!index) continue;
            color = pal_color(g, index);
        } else {
            color = vram16(g, page + pos * 2) & 0x7FFF;
        }
        put_px(&out[x], color, prio, PX_OPAQUE);
    }
}
```

- [x] **Step 4: In `ppu_render_line` (`core/src/ppu/ppu.c`), replace `default: break;` in the switch with**

```c
    case 3:
    case 4:
    case 5:
        if (dispcnt & 0x400) ppu_render_bitmap_bg(g, dispcnt & 7, y, lb.bg[2]);
        break;
    default: break; /* modes 6-7: backdrop only */
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 4 `bitmap_*` tests.

- [x] **Step 6: Commit**

```bash
git add core/src/ppu tests/unit/test_ppu_bitmap.c
git commit -m "feat(ppu): render bitmap modes 3, 4 and 5" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Sprites

**Files:**
- Create: `core/src/ppu/ppu_obj.c`, `tests/unit/test_ppu_obj.c`
- Modify: `core/src/ppu/ppu.c` (`ppu_render_line`)

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu_obj.c`**

```c
#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

#define OBJ_TILES 0x10000u

static gba_t *obj_scene(uint16_t dispcnt) {
    gba_t *g = gba_create(NULL);
    tu_hide_sprites(g);
    tu_io16(g, 0x000, dispcnt);
    tu_pal(g, 0, C_BLACK);
    return g;
}

/* 4bpp sprite tile where every pixel has colour index `index`. */
static void obj_solid_tile4(gba_t *g, uint32_t tile, uint16_t index) {
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, OBJ_TILES + tile * 32 + i, (uint16_t)(index * 0x1111));
}

static void sprite(gba_t *g, uint32_t i, uint16_t a0, uint16_t a1, uint16_t a2) {
    tu_oam16(g, i * 8, a0);
    tu_oam16(g, i * 8 + 2, a1);
    tu_oam16(g, i * 8 + 4, a2);
}

/* Affine parameter set 0 lives in the 4th halfword of OAM entries 0-3. */
static void affine_params0(gba_t *g, uint16_t pa, uint16_t pb, uint16_t pc, uint16_t pd) {
    tu_oam16(g, 6, pa);
    tu_oam16(g, 14, pb);
    tu_oam16(g, 22, pc);
    tu_oam16(g, 30, pd);
}

TEST(obj_regular_4bpp_sprite) {
    gba_t *g = obj_scene(0x1040); /* OBJ on, 1D mapping */
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 256 + 16 + 1, C_RED);
    sprite(g, 0, 20, 10, 1 << 12); /* y 20, x 10, tile 0, palette bank 1 */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 9, 20), C_BLACK);
    CHECK_EQ(tu_px(g, 10, 20), C_RED);
    CHECK_EQ(tu_px(g, 17, 20), C_RED);
    CHECK_EQ(tu_px(g, 18, 20), C_BLACK);
    ppu_render_line(g, 28);
    CHECK_EQ(tu_px(g, 10, 28), C_BLACK);
    gba_destroy(g);
}

TEST(obj_position_wraps) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 250, 508, 0); /* y 250 reaches line 1; x 508 is -4 */
    ppu_render_line(g, 1);
    CHECK_EQ(tu_px(g, 0, 1), C_RED);
    CHECK_EQ(tu_px(g, 3, 1), C_RED);
    CHECK_EQ(tu_px(g, 4, 1), C_BLACK);
    gba_destroy(g);
}

TEST(obj_flips) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES, 0x0001); /* tile 0: only pixel (0,0) */
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0 | 0x1000, 0);  /* horizontal flip */
    sprite(g, 1, 16, 0 | 0x2000, 0); /* vertical flip */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 7, 0), C_RED);
    ppu_render_line(g, 23);
    CHECK_EQ(tu_px(g, 0, 23), C_RED);
    gba_destroy(g);
}

TEST(obj_8bpp_uses_the_whole_obj_palette) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES + 64, 0x0201); /* 8bpp tile at 32-byte unit 2, row 0: 1..8 */
    tu_vram16(g, OBJ_TILES + 66, 0x0403);
    tu_vram16(g, OBJ_TILES + 68, 0x0605);
    tu_vram16(g, OBJ_TILES + 70, 0x0807);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, 256 + i, (uint16_t)(0x0300 + i));
    sprite(g, 0, 0x2000, 0, 2);
    ppu_render_line(g, 0);
    for (int x = 0; x < 8; x++) CHECK_EQ(tu_px(g, x, 0), 0x0300 + x + 1);
    gba_destroy(g);
}

TEST(obj_1d_and_2d_tile_mapping) {
    for (int one_d = 0; one_d < 2; one_d++) {
        gba_t *g = obj_scene(one_d ? 0x1040 : 0x1000);
        obj_solid_tile4(g, 2, 1);  /* 1D: the sprite's second tile row starts at tile 2 */
        obj_solid_tile4(g, 32, 2); /* 2D: ...and at tile 32 */
        tu_pal(g, 257, C_RED);
        tu_pal(g, 258, C_GREEN);
        sprite(g, 0, 0, 1 << 14, 0); /* 16x16 */
        ppu_render_line(g, 8);
        CHECK_EQ(tu_px(g, 0, 8), one_d ? C_RED : C_GREEN);
        gba_destroy(g);
    }
}

TEST(obj_priority_against_backgrounds) {
    gba_t *g = obj_scene(0x1140); /* OBJ + BG0 */
    for (uint32_t i = 0; i < 32; i += 2) tu_vram16(g, 32 + i, 0x1111); /* BG tile 1 solid */
    tu_pal(g, 1, C_BLUE);
    tu_io16(g, 0x008, 31 << 8); /* BG0 priority 0 */
    tu_vram16(g, 0xF800, 1);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0, 1 << 10); /* priority 1: behind BG0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLUE);
    sprite(g, 0, 0, 0, 0); /* priority 0: tie goes to the sprite */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(obj_order_between_sprites) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    tu_pal(g, 273, C_GREEN);
    sprite(g, 0, 0, 100, (1 << 10) | (0 << 12)); /* red, priority 1 */
    sprite(g, 1, 0, 100, (1 << 10) | (1 << 12)); /* green, priority 1 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 100, 0), C_RED); /* lower OAM index wins ties */
    sprite(g, 1, 0, 100, (0 << 10) | (1 << 12)); /* green, priority 0 */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 100, 0), C_GREEN);
    gba_destroy(g);
}

TEST(obj_bitmap_modes_hide_tiles_below_512) {
    gba_t *g = obj_scene(0x1043); /* mode 3, OBJ on */
    obj_solid_tile4(g, 0, 1);
    obj_solid_tile4(g, 512, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 0, 0, 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK);
    sprite(g, 0, 0, 0, 512);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED);
    gba_destroy(g);
}

TEST(obj_affine_identity_and_double_size) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    affine_params0(g, 0x100, 0, 0, 0x100);
    sprite(g, 0, 20 | 0x100, 10, 0); /* affine, parameter set 0 */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 10, 20), C_RED);
    CHECK_EQ(tu_px(g, 17, 20), C_RED);
    sprite(g, 0, 20 | 0x300, 10, 0); /* double-size: 16x16 box, sprite centred */
    ppu_render_line(g, 20);
    CHECK_EQ(tu_px(g, 14, 20), C_BLACK);
    ppu_render_line(g, 24);
    CHECK_EQ(tu_px(g, 13, 24), C_BLACK);
    CHECK_EQ(tu_px(g, 14, 24), C_RED);
    CHECK_EQ(tu_px(g, 21, 24), C_RED);
    CHECK_EQ(tu_px(g, 22, 24), C_BLACK);
    gba_destroy(g);
}

TEST(obj_affine_scaling) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    affine_params0(g, 0x80, 0, 0, 0x80); /* 2x */
    sprite(g, 0, 0 | 0x300, 10, 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 9, 0), C_BLACK);
    CHECK_EQ(tu_px(g, 10, 0), C_RED);
    CHECK_EQ(tu_px(g, 25, 0), C_RED);
    CHECK_EQ(tu_px(g, 26, 0), C_BLACK);
    gba_destroy(g);
}

TEST(obj_line_cycle_budget) {
    gba_t *g = obj_scene(0x1040);
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    /* 64x64 sprites cost 64 cycles on each line they cover; the budget is 1210. */
    for (uint32_t i = 0; i < 17; i++) sprite(g, i, 0, 300 | (3 << 14), 0); /* off-screen at x -212 */
    sprite(g, 17, 0, 0 | (3 << 14), 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_RED); /* 18 x 64 = 1152 fits */
    sprite(g, 17, 0, 300 | (3 << 14), 0);
    sprite(g, 18, 0, 0 | (3 << 14), 0);
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 0, 0), C_BLACK); /* 19 x 64 = 1216 does not */
    gba_destroy(g);
}

TEST(obj_window_and_semi_transparent_flags) {
    gba_t *g = obj_scene(0x9040); /* OBJ + OBJ window */
    obj_solid_tile4(g, 0, 1);
    tu_pal(g, 257, C_RED);
    sprite(g, 0, 20 | (2 << 10), 10, 0);  /* OBJ-window sprite */
    sprite(g, 1, 20 | (1 << 10), 100, 0); /* semi-transparent sprite */
    static layer_px_t obj[PPU_WIDTH];
    static bool win[PPU_WIDTH];
    memset(obj, 0, sizeof obj);
    memset(win, 0, sizeof win);
    ppu_render_obj(g, 20, obj, win);
    CHECK(win[10]);
    CHECK(!win[9]);
    CHECK_EQ(obj[10].flags, 0);
    CHECK_EQ(obj[100].flags, PX_OPAQUE | PX_SEMI);
    gba_destroy(g);
}

TEST(obj_mosaic) {
    gba_t *g = obj_scene(0x1040);
    tu_vram16(g, OBJ_TILES + 0, 0x4321); /* row 0: indices 1..8 */
    tu_vram16(g, OBJ_TILES + 2, 0x8765);
    for (uint32_t i = 1; i <= 8; i++) tu_pal(g, 256 + i, (uint16_t)(0x0100 + i));
    sprite(g, 0, 0x1000, 8, 0); /* mosaic on, x 8 */
    tu_io16(g, 0x04C, 3 << 8);  /* OBJ blocks 4 pixels wide */
    ppu_render_line(g, 0);
    CHECK_EQ(tu_px(g, 8, 0), 0x0101);
    CHECK_EQ(tu_px(g, 11, 0), 0x0101);
    CHECK_EQ(tu_px(g, 12, 0), 0x0105);
    CHECK_EQ(tu_px(g, 15, 0), 0x0105);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit obj_`
Expected: link fails: `undefined symbol: ppu_render_obj`.

- [x] **Step 3: Create `core/src/ppu/ppu_obj.c`**

```c
#include "ppu/ppu_internal.h"

/* [shape][size]: square, wide, tall. */
static const uint8_t obj_width[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
static const uint8_t obj_height[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

/* Colour index at pixel (tx, ty) of a w-pixel-wide sprite; 0 is transparent. */
static uint32_t obj_texel(const gba_t *g, uint32_t tile, bool bpp8, bool one_d, int w, int tx, int ty) {
    uint32_t unit = bpp8 ? 2 : 1; /* tile numbers count 32-byte units */
    uint32_t row_stride = one_d ? (uint32_t)(w / 8) * unit : 32;
    uint32_t t = tile + (uint32_t)(ty / 8) * row_stride + (uint32_t)(tx / 8) * unit;
    uint32_t base = (t & 0x3FF) * 32;
    if (bpp8) return g->bus.vram[0x10000 + ((base + (uint32_t)(ty % 8) * 8 + (uint32_t)(tx % 8)) & 0x7FFF)];
    uint8_t b = g->bus.vram[0x10000 + ((base + (uint32_t)(ty % 8) * 4 + (uint32_t)(tx % 8) / 2) & 0x7FFF)];
    return (tx & 1) ? b >> 4 : b & 0xFu;
}

void ppu_render_obj(gba_t *g, int y, layer_px_t *out, bool *objwin) {
    uint16_t dispcnt = g->ppu.dispcnt;
    bool one_d = (dispcnt & 0x40) != 0;
    bool bitmap_mode = (dispcnt & 7) >= 3;
    uint16_t mosaic = ppu_reg(g, 0x04C);
    int mos_h = ((mosaic >> 8) & 0xF) + 1, mos_v = ((mosaic >> 12) & 0xF) + 1;
    int budget = (dispcnt & 0x20) ? 954 : 1210; /* H-Blank Interval Free shortens the budget */

    for (uint32_t i = 0; i < 128; i++) {
        uint16_t a0 = oam16(g, i * 8), a1 = oam16(g, i * 8 + 2), a2 = oam16(g, i * 8 + 4);
        bool affine = (a0 & 0x100) != 0;
        if (!affine && (a0 & 0x200)) continue; /* disabled */
        int shape = (a0 >> 14) & 3, size = (a1 >> 14) & 3;
        if (shape == 3) continue;
        int w = obj_width[shape][size], h = obj_height[shape][size];
        bool dbl = affine && (a0 & 0x200);
        int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        int row = (y - (a0 & 0xFF)) & 0xFF; /* Y wraps at 256 */
        if (row >= bh) continue;
        budget -= affine ? 2 * bw + 10 : bw;
        if (budget < 0) break;

        int mode = (a0 >> 10) & 3; /* 0 normal, 1 semi-transparent, 2 OBJ window */
        if (mode == 3) continue;
        if (mode == 2 ? !(dispcnt & 0x8000) : !(dispcnt & 0x1000)) continue;
        bool bpp8 = (a0 & 0x2000) != 0, mosaic_on = (a0 & 0x1000) != 0;
        uint32_t tile = a2 & 0x3FF;
        if (bitmap_mode && tile < 512) continue; /* those tiles overlap the bitmap */
        if (bpp8 && !one_d) tile &= ~1u;
        uint8_t prio = (a2 >> 10) & 3;
        uint32_t pal_base = bpp8 ? 256 : 256 + (uint32_t)(a2 >> 12) * 16;
        int ox = a1 & 0x1FF;
        if (ox >= PPU_WIDTH) ox -= 512;
        if (mosaic_on) row -= row % mos_v;

        int32_t pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            uint32_t p = ((a1 >> 9) & 0x1F) * 32;
            pa = (int16_t)oam16(g, p + 6);
            pb = (int16_t)oam16(g, p + 14);
            pc = (int16_t)oam16(g, p + 22);
            pd = (int16_t)oam16(g, p + 30);
        }

        for (int bx = 0; bx < bw; bx++) {
            int sx = ox + bx;
            if (sx < 0 || sx >= PPU_WIDTH) continue;
            int lx = bx;
            if (mosaic_on) {
                int m = sx - sx % mos_h - ox;
                if (m >= 0) lx = m;
            }
            int tx, ty;
            if (affine) {
                int cx = lx - bw / 2, cy = row - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = (a1 & 0x1000) ? w - 1 - lx : lx;
                ty = (a1 & 0x2000) ? h - 1 - row : row;
            }
            uint32_t index = obj_texel(g, tile, bpp8, one_d, w, tx, ty);
            if (!index) continue;
            if (mode == 2) {
                objwin[sx] = true;
                continue;
            }
            layer_px_t *px = &out[sx];
            if ((px->flags & PX_OPAQUE) && px->prio <= prio) continue; /* an earlier sprite wins ties */
            put_px(px, pal_color(g, pal_base + index), prio, (uint8_t)(PX_OPAQUE | (mode == 1 ? PX_SEMI : 0)));
        }
    }
}
```

- [x] **Step 4: In `ppu_render_line` (`core/src/ppu/ppu.c`), add before `ppu_compose(g, y, &lb, dst);`**

```c
    if (dispcnt & 0x9000) ppu_render_obj(g, y, lb.obj, lb.objwin); /* OBJ layer or OBJ window */
```

- [x] **Step 5: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 13 `obj_*` tests.

- [x] **Step 6: Commit**

```bash
git add core/src/ppu tests/unit/test_ppu_obj.c
git commit -m "feat(ppu): render regular and affine sprites with budget, window and mosaic" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Windows and colour effects

**Files:**
- Modify: `core/src/ppu/ppu_compose.c` (full replacement)
- Test: `tests/unit/test_ppu_compose.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_ppu_compose.c`**

```c
#include <string.h>

#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

static line_buffers_t lb;
static uint16_t line[PPU_WIDTH];

/* BG0 (priority 0) in front of BG1 (priority 1) across the whole line. */
static void two_layers(uint16_t front, uint16_t back) {
    memset(&lb, 0, sizeof lb);
    for (int x = 0; x < PPU_WIDTH; x++) {
        put_px(&lb.bg[0][x], front, 0, PX_OPAQUE);
        put_px(&lb.bg[1][x], back, 1, PX_OPAQUE);
    }
}

TEST(window0_inside_and_outside) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x2000);         /* WIN0 on */
    tu_io16(g, 0x040, (10 << 8) | 20); /* x 10..19 */
    tu_io16(g, 0x044, 160);            /* y 0..159 */
    tu_io16(g, 0x048, 0x02);           /* inside: BG1 only */
    tu_io16(g, 0x04A, 0x01);           /* outside: BG0 only */
    ppu_compose(g, 5, &lb, line);
    CHECK_EQ(line[9], C_RED);
    CHECK_EQ(line[10], C_GREEN);
    CHECK_EQ(line[19], C_GREEN);
    CHECK_EQ(line[20], C_RED);
    gba_destroy(g);
}

TEST(window_wraps_horizontally_and_limits_vertically) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x2000);
    tu_io16(g, 0x040, (230 << 8) | 10); /* x >= 230 or x < 10 */
    tu_io16(g, 0x044, (50 << 8) | 60);  /* y 50..59 */
    tu_io16(g, 0x048, 0x02);
    tu_io16(g, 0x04A, 0x01);
    ppu_compose(g, 55, &lb, line);
    CHECK_EQ(line[5], C_GREEN);
    CHECK_EQ(line[10], C_RED);
    CHECK_EQ(line[229], C_RED);
    CHECK_EQ(line[230], C_GREEN);
    ppu_compose(g, 49, &lb, line);
    CHECK_EQ(line[5], C_RED);
    ppu_compose(g, 60, &lb, line);
    CHECK_EQ(line[5], C_RED);
    gba_destroy(g);
}

TEST(window0_wins_over_window1) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    tu_io16(g, 0x000, 0x6000);
    tu_io16(g, 0x040, 240);    /* WIN0: whole line */
    tu_io16(g, 0x042, 240);    /* WIN1: whole line */
    tu_io16(g, 0x044, 160);
    tu_io16(g, 0x046, 160);
    tu_io16(g, 0x048, 0x0102); /* WIN0: BG1; WIN1: BG0 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_GREEN);
    gba_destroy(g);
}

TEST(obj_window_region) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_GREEN);
    for (int x = 100; x < 108; x++) lb.objwin[x] = true;
    tu_io16(g, 0x000, 0x8000);
    tu_io16(g, 0x04A, 0x0201); /* OBJ window: BG1; outside: BG0 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[99], C_RED);
    CHECK_EQ(line[100], C_GREEN);
    CHECK_EQ(line[107], C_GREEN);
    gba_destroy(g);
}

TEST(alpha_blend) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x050, 0x0001 | (1 << 6) | 0x0200); /* BG0 over BG1, alpha */
    tu_io16(g, 0x052, 0x0808);                     /* EVA = EVB = 8/16 */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    tu_io16(g, 0x052, 0x1F1F); /* coefficients above 16 act as 16; channels saturate */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x7C1F);
    tu_io16(g, 0x050, 0x0001 | (1 << 6)); /* BG1 is not a second target: no blend */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_RED);
    gba_destroy(g);
}

TEST(brighten_and_darken) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x050, 0x0001 | (2 << 6));
    tu_io16(g, 0x054, 8);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3DFF);
    tu_io16(g, 0x054, 31); /* EVY above 16 acts as 16: white */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_WHITE);
    tu_io16(g, 0x050, 0x0001 | (3 << 6));
    tu_io16(g, 0x054, 8);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x0010);
    gba_destroy(g);
}

TEST(backdrop_can_be_a_first_target) {
    gba_t *g = gba_create(NULL);
    memset(&lb, 0, sizeof lb);
    tu_pal(g, 0, C_RED);
    tu_io16(g, 0x050, 0x0020 | (3 << 6)); /* backdrop, darken */
    tu_io16(g, 0x054, 16);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_BLACK);
    gba_destroy(g);
}

TEST(semi_transparent_obj_always_blends) {
    gba_t *g = gba_create(NULL);
    memset(&lb, 0, sizeof lb);
    put_px(&lb.obj[0], C_RED, 0, PX_OPAQUE | PX_SEMI);
    put_px(&lb.bg[1][0], C_BLUE, 1, PX_OPAQUE);
    tu_io16(g, 0x050, 0x0200); /* only a second target; effect mode "none" */
    tu_io16(g, 0x052, 0x0808);
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    gba_destroy(g);
}

TEST(window_can_disable_effects) {
    gba_t *g = gba_create(NULL);
    two_layers(C_RED, C_BLUE);
    tu_io16(g, 0x000, 0x2000);
    tu_io16(g, 0x040, 240);
    tu_io16(g, 0x044, 160);
    tu_io16(g, 0x050, 0x0001 | (1 << 6) | 0x0200);
    tu_io16(g, 0x052, 0x0808);
    tu_io16(g, 0x048, 0x03); /* BG0 + BG1 visible, effects off */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], C_RED);
    tu_io16(g, 0x048, 0x23); /* effects on */
    ppu_compose(g, 0, &lb, line);
    CHECK_EQ(line[0], 0x3C0F);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit window`
Expected: the window tests FAIL (the compositor ignores windows).

- [x] **Step 3: Replace `core/src/ppu/ppu_compose.c`**

```c
#include "ppu/ppu_internal.h"

/*
 * The two front-most visible pixels at x. Front to back: lower priority value first;
 * at equal priority OBJ is in front of BG0, BG0 of BG1, and so on. The backdrop is last.
 */
static void find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                         uint16_t color[2], bool *semi) {
    int found = 0;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    *semi = false;
    for (uint8_t prio = 0; prio < 4 && found < 2; prio++) {
        const layer_px_t *o = &lb->obj[x];
        if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE) && o->prio == prio) {
            if (found == 0) *semi = (o->flags & PX_SEMI) != 0;
            layer[found] = LAYER_OBJ;
            color[found++] = o->color;
        }
        for (int bg = 0; bg < 4 && found < 2; bg++) {
            const layer_px_t *p = &lb->bg[bg][x];
            if ((visible & (1u << bg)) && (p->flags & PX_OPAQUE) && p->prio == prio) {
                layer[found] = bg;
                color[found++] = p->color;
            }
        }
    }
}

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around. */
static bool in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = cap16(bldalpha & 0x1F), evb = cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);

    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F; /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            if (win0 && in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }

        int layer[2];
        uint16_t color[2];
        bool semi;
        find_top_two(lb, x, mask, backdrop, layer, color, &semi);

        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = blend(color[0], color[1], eva, evb); /* semi-transparent sprites always blend */
            } else if (first) {
                if (mode == 1 && second) out = blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = brighten(color[0], evy);
                else if (mode == 3) out = darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 9 compositor tests.

- [x] **Step 5: Commit**

```bash
git add core/src/ppu/ppu_compose.c tests/unit/test_ppu_compose.c
git commit -m "feat(ppu): add windows, alpha blending, brighten and darken" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Timers

**Files:**
- Create: `core/src/sys/timer.h`, `core/src/sys/timer.c`, `tests/unit/test_timer.c`
- Modify: `core/src/sys/sched.h`, `core/src/gba_internal.h`, `core/src/gba.c`, `core/src/mem/io.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_timer.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(timer_counts_at_each_prescaler) {
    static const unsigned shift[4] = {0, 6, 8, 10};
    for (uint16_t p = 0; p < 4; p++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, 0x100, 0);
        tu_io16(g, 0x102, (uint16_t)(0x80 | p));
        g->now = g->timers.unit[0].start + ((uint64_t)5 << shift[p]);
        CHECK_EQ(timer_read_counter(g, 0), 5);
        gba_destroy(g);
    }
}

TEST(timer_io_read_returns_the_live_counter) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x102, 0x83); /* prescaler 1024 */
    g->now = g->timers.unit[0].start + 3 * 1024;
    CHECK_EQ(bus_read16(g, 0x04000100, false), 3);
    gba_destroy(g);
}

TEST(timer_overflow_reloads_and_raises_irq) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0xFFF0);
    tu_io16(g, 0x102, 0xC0); /* enable + IRQ */
    uint64_t start = g->timers.unit[0].start;
    tu_advance_to(g, start + 15);
    CHECK(!(g->irq.if_ & IRQ_TIMER0));
    CHECK_EQ(timer_read_counter(g, 0), 0xFFFF);
    tu_advance_to(g, start + 16);
    CHECK(g->irq.if_ & IRQ_TIMER0);
    CHECK_EQ(timer_read_counter(g, 0), 0xFFF0);
    tu_advance_to(g, start + 32 + 3);
    CHECK_EQ(timer_read_counter(g, 0), 0xFFF3);
    gba_destroy(g);
}

TEST(timer_reload_write_does_not_touch_a_running_count) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0x10);
    tu_io16(g, 0x102, 0x80);
    uint64_t start = g->timers.unit[0].start;
    tu_io16(g, 0x100, 0x20);
    g->now = start + 4;
    CHECK_EQ(timer_read_counter(g, 0), 0x14);
    gba_destroy(g);
}

TEST(timer_cascade_counts_overflows) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x104, 0);
    tu_io16(g, 0x106, 0x84);    /* timer 1: count-up */
    tu_io16(g, 0x100, 0xFFFF);
    tu_io16(g, 0x102, 0x80);    /* timer 0 overflows every cycle */
    tu_advance_to(g, g->timers.unit[0].start + 5);
    CHECK_EQ(timer_read_counter(g, 1), 5);
    gba_destroy(g);
}

TEST(timer_stop_freezes_the_counter) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x102, 0x80);
    g->now = g->timers.unit[0].start + 7;
    tu_io16(g, 0x102, 0x00); /* the write lands one cycle later */
    g->now += 100;
    CHECK_EQ(timer_read_counter(g, 0), 8);
    CHECK_EQ(g->sched.when[EV_TIMER0], SCHED_NEVER);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit timer`
Expected: build fails: no member `timers` in `struct gba`.

- [x] **Step 3: Add timer events — replace the enum in `core/src/sys/sched.h`**

```c
/* Event kinds. At equal timestamps, lower IDs fire first. */
typedef enum {
    EV_HBLANK = 0, /* HDraw ends on the current line */
    EV_LINE_END,   /* HBlank ends; VCOUNT advances */
    EV_TIMER0,     /* timer n overflows (EV_TIMER0 + n) */
    EV_TIMER1,
    EV_TIMER2,
    EV_TIMER3,
    EV_COUNT
} sched_event_t;
```

- [x] **Step 4: Create `core/src/sys/timer.h`**

```c
#ifndef GBA_TIMER_H
#define GBA_TIMER_H

#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    uint16_t reload;
    uint16_t counter; /* the count at `start` while running; the frozen count otherwise */
    uint16_t control; /* TMxCNT_H */
    uint64_t start;
} timer_unit_t;

typedef struct {
    timer_unit_t unit[4];
} timers_t;

void timer_reset(gba_t *g);
uint16_t timer_read_counter(gba_t *g, int n);
void timer_write_reload(gba_t *g, int n, uint16_t value);
void timer_write_control(gba_t *g, int n, uint16_t value);
void timer_on_overflow(gba_t *g, int n, uint64_t when);

#endif
```

- [x] **Step 5: Create `core/src/sys/timer.c`**

```c
#include "sys/timer.h"

#include <string.h>

#include "gba_internal.h"

static const unsigned prescale_shift[4] = {0, 6, 8, 10}; /* 1, 64, 256, 1024 cycles per tick */

/* Count-up (cascade) timers only move when the previous timer overflows. Timer 0 cannot cascade. */
static bool free_running(const timer_unit_t *t, int n) {
    return (t->control & 0x80) && !(n > 0 && (t->control & 0x04));
}

void timer_reset(gba_t *g) {
    memset(&g->timers, 0, sizeof g->timers);
}

uint16_t timer_read_counter(gba_t *g, int n) {
    const timer_unit_t *t = &g->timers.unit[n];
    if (!free_running(t, n)) return t->counter;
    uint64_t value = t->counter + ((g->now - t->start) >> prescale_shift[t->control & 3]);
    if (value > 0xFFFF) { /* overflowed within the current instruction; the event has not run yet */
        uint64_t period = 0x10000u - t->reload;
        value = t->reload + (value - 0x10000u) % period;
    }
    return (uint16_t)value;
}

static void schedule(gba_t *g, int n) {
    const timer_unit_t *t = &g->timers.unit[n];
    uint64_t ticks = 0x10000u - t->counter;
    sched_add(&g->sched, (sched_event_t)(EV_TIMER0 + n), t->start + (ticks << prescale_shift[t->control & 3]));
}

void timer_write_reload(gba_t *g, int n, uint16_t value) {
    g->timers.unit[n].reload = value; /* takes effect at the next start or overflow */
}

void timer_write_control(gba_t *g, int n, uint16_t value) {
    timer_unit_t *t = &g->timers.unit[n];
    t->counter = timer_read_counter(g, n); /* freeze the count before changing anything */
    bool starting = !(t->control & 0x80) && (value & 0x80);
    t->control = value & 0xC7;
    if (starting) t->counter = t->reload;
    t->start = g->now;
    if (free_running(t, n)) schedule(g, n);
    else sched_cancel(&g->sched, (sched_event_t)(EV_TIMER0 + n));
}

/* Reload, flag the IRQ, and tick a cascaded next timer (which may overflow in turn). */
static void overflow(gba_t *g, int n, uint64_t when) {
    timer_unit_t *t = &g->timers.unit[n];
    t->counter = t->reload;
    t->start = when;
    if (t->control & 0x40) irq_raise(g, (uint16_t)(IRQ_TIMER0 << n));
    if (free_running(t, n)) schedule(g, n);
    if (n < 3) {
        timer_unit_t *next = &g->timers.unit[n + 1];
        if ((next->control & 0x84) == 0x84 && ++next->counter == 0) overflow(g, n + 1, when);
    }
}

void timer_on_overflow(gba_t *g, int n, uint64_t when) {
    overflow(g, n, when);
}
```

- [x] **Step 6: Replace `core/src/gba_internal.h`**

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
#include "sys/timer.h"

struct gba {
    arm_cpu_t cpu;
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    timers_t timers;
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

- [x] **Step 7: Update `core/src/gba.c`**

In `gba_reset`, add after `irq_reset(g);`:

```c
    timer_reset(g);
```

In `gba_run_events`, add before `default: break;`:

```c
        case EV_TIMER0:
        case EV_TIMER1:
        case EV_TIMER2:
        case EV_TIMER3: timer_on_overflow(g, id - EV_TIMER0, when); break;
```

- [x] **Step 8: Route timer registers — `core/src/mem/io.c`**

Add after the `enum { REG_... }` block:

```c
enum { REG_TIMERS = 0x100, REG_TIMERS_END = 0x110 };
```

In `read16`, add before `switch (off) {`:

```c
    if (off >= REG_TIMERS && off < REG_TIMERS_END && !(off & 2)) return timer_read_counter(g, (int)((off - REG_TIMERS) >> 2));
```

In `write16`, add before `switch (off) {`:

```c
    if (off >= REG_TIMERS && off < REG_TIMERS_END) {
        int n = (int)((off - REG_TIMERS) >> 2);
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if (off & 2) timer_write_control(g, n, v);
        else timer_write_reload(g, n, v);
        return;
    }
```

- [x] **Step 9: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 6 `timer_*` tests.

- [x] **Step 10: Commit**

```bash
git add core/src tests/unit/test_timer.c
git commit -m "feat(core): add timers with prescalers, cascade and overflow IRQs" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: DMA

**Files:**
- Create: `core/src/sys/dma.h`, `core/src/sys/dma.c`, `tests/unit/test_dma.c`
- Modify: `core/src/sys/sched.h`, `core/src/gba_internal.h`, `core/src/gba.c`, `core/src/mem/io.c`, `core/src/ppu/ppu.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_dma.c`**

```c
#include "test.h"
#include "testutil.h"

static void dma_setup(gba_t *g, uint32_t n, uint32_t src, uint32_t dst, uint16_t count, uint16_t control) {
    uint32_t base = 0x0B0 + n * 12;
    tu_io32(g, base, src);
    tu_io32(g, base + 4, dst);
    tu_io16(g, base + 8, count);
    tu_io16(g, base + 10, control);
}

/* Immediate transfers start 2 cycles after the enabling write. */
static void run_pending(gba_t *g) {
    tu_advance_to(g, g->now + 2);
}

TEST(dma_immediate_word_copy_then_disables) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 4; i++) bus_write32(g, 0x02000000 + i * 4, i + 1, false);
    dma_setup(g, 3, 0x02000000, 0x02000100, 4, 0x8400); /* enable, 32-bit, immediate */
    run_pending(g);
    for (uint32_t i = 0; i < 4; i++) CHECK_EQ(bus_read32(g, 0x02000100 + i * 4, false), i + 1);
    CHECK_EQ(bus_read16(g, 0x040000DE, false) & 0x8000, 0);
    gba_destroy(g);
}

TEST(dma_halfword_decrementing_source_fixed_destination) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x1111, false);
    bus_write16(g, 0x02000002, 0x2222, false);
    bus_write16(g, 0x02000004, 0x3333, false);
    dma_setup(g, 3, 0x02000004, 0x02000100, 3, 0x8000 | 0x0080 | 0x0040);
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x02000100, false), 0x1111); /* the last value copied */
    CHECK_EQ(bus_read16(g, 0x02000102, false), 0);
    gba_destroy(g);
}

TEST(dma_count_zero_means_maximum) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x0201FFFE, 0xBEEF, false);
    dma_setup(g, 3, 0x02000000, 0x02020000, 0, 0x8000); /* DMA3: 0x10000 halfwords */
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x0203FFFE, false), 0xBEEF);
    gba_destroy(g);
}

TEST(dma_vblank_repeat_reloads_destination) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0xAAAA5555, false);
    /* VBlank, repeat, reload destination, fixed source (sources are never reloaded), 32-bit */
    dma_setup(g, 1, 0x02000000, 0x02000100, 1, 0x8000 | 0x1000 | 0x0200 | 0x0060 | 0x0100 | 0x0400);
    tu_advance_to(g, 100);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0xAAAA5555);
    bus_write32(g, 0x02000000, 0x12345678, false);
    tu_advance_to(g, 160 * PPU_LINE_CYCLES + PPU_FRAME_CYCLES);
    CHECK_EQ(bus_read32(g, 0x02000100, false), 0x12345678);
    CHECK_EQ(bus_read32(g, 0x02000104, false), 0);
    CHECK(bus_read16(g, 0x040000C6, false) & 0x8000); /* still enabled */
    gba_destroy(g);
}

TEST(dma_hblank_runs_only_on_visible_lines) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x02000000, 0x1234, false);
    dma_setup(g, 0, 0x02000000, 0x02001000, 1, 0x8000 | 0x2000 | 0x0200 | 0x0100); /* HBlank, repeat, fixed src */
    /* Stop inside VBlank: one big jump runs all transfers back to back, and their stall time
     * would otherwise carry the clock into the next frame's line 0. */
    tu_advance_to(g, 200 * PPU_LINE_CYCLES);
    CHECK_EQ(bus_read16(g, 0x02001000 + 159 * 2, false), 0x1234);
    CHECK_EQ(bus_read16(g, 0x02001000 + 160 * 2, false), 0);
    gba_destroy(g);
}

TEST(dma_end_of_transfer_irq) {
    gba_t *g = gba_create(NULL);
    dma_setup(g, 2, 0x02000000, 0x02000100, 1, 0xC000);
    run_pending(g);
    CHECK(g->irq.if_ & IRQ_DMA2);
    gba_destroy(g);
}

TEST(dma_unreadable_source_returns_the_last_value) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x02000000, 0xCAFEBABE, false);
    dma_setup(g, 3, 0x02000000, 0x02000100, 1, 0x8400);
    run_pending(g);
    dma_setup(g, 3, 0x00000000, 0x02000200, 1, 0x8400); /* BIOS region */
    run_pending(g);
    CHECK_EQ(bus_read32(g, 0x02000200, false), 0xCAFEBABE);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit dma`
Expected: the 7 tests FAIL (DMA registers are plain storage).

- [x] **Step 3: Add the DMA event — replace the enum in `core/src/sys/sched.h`**

```c
/* Event kinds. At equal timestamps, lower IDs fire first. */
typedef enum {
    EV_HBLANK = 0, /* HDraw ends on the current line */
    EV_LINE_END,   /* HBlank ends; VCOUNT advances */
    EV_TIMER0,     /* timer n overflows (EV_TIMER0 + n) */
    EV_TIMER1,
    EV_TIMER2,
    EV_TIMER3,
    EV_DMA,        /* immediate DMA transfers start */
    EV_COUNT
} sched_event_t;
```

- [x] **Step 4: Create `core/src/sys/dma.h`**

```c
#ifndef GBA_DMA_H
#define GBA_DMA_H

#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    uint32_t src, dst, count; /* internal registers, latched when the channel is enabled */
} dma_channel_t;

typedef struct {
    dma_channel_t ch[4];
    uint32_t latch;  /* last value transferred; returned for sources DMA cannot read */
    uint8_t pending; /* channels waiting for their immediate start */
} dma_t;

void dma_reset(gba_t *g);
void dma_write_control(gba_t *g, int n, uint16_t old, uint16_t value); /* DMAnCNT_H was written */
void dma_on_vblank(gba_t *g);
void dma_on_hblank(gba_t *g); /* visible lines only */
void dma_run_immediate(gba_t *g);

#endif
```

- [x] **Step 5: Create `core/src/sys/dma.c`**

```c
#include "sys/dma.h"

#include <string.h>

#include "gba_internal.h"

enum { DMA_BASE = 0x0B0, DMA_STRIDE = 12 };
enum { TIMING_IMMEDIATE, TIMING_VBLANK, TIMING_HBLANK, TIMING_SPECIAL };

static const uint32_t src_mask[4] = {0x07FFFFFF, 0x0FFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF};
static const uint32_t dst_mask[4] = {0x07FFFFFF, 0x07FFFFFF, 0x07FFFFFF, 0x0FFFFFFF};

static uint16_t reg16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static uint32_t reg32(const gba_t *g, uint32_t off) {
    return reg16(g, off) | (uint32_t)reg16(g, off + 2) << 16;
}

static uint32_t channel_base(int n) {
    return DMA_BASE + (uint32_t)n * DMA_STRIDE;
}

static uint16_t control(const gba_t *g, int n) {
    return reg16(g, channel_base(n) + 10);
}

/* A count of 0 means the maximum: 0x4000 units (0x10000 for DMA3). */
static uint32_t latched_count(const gba_t *g, int n) {
    uint32_t count = reg16(g, channel_base(n) + 8) & (n == 3 ? 0xFFFFu : 0x3FFFu);
    return count ? count : (n == 3 ? 0x10000u : 0x4000u);
}

void dma_reset(gba_t *g) {
    memset(&g->dma, 0, sizeof g->dma);
}

void dma_write_control(gba_t *g, int n, uint16_t old, uint16_t value) {
    dma_channel_t *c = &g->dma.ch[n];
    if (!(value & 0x8000)) {
        g->dma.pending &= (uint8_t)~(1u << n);
        return;
    }
    if (old & 0x8000) return; /* already enabled: only the settings changed */
    c->src = reg32(g, channel_base(n)) & src_mask[n];
    c->dst = reg32(g, channel_base(n) + 4) & dst_mask[n];
    c->count = latched_count(g, n);
    if (((value >> 12) & 3) == TIMING_IMMEDIATE) {
        g->dma.pending |= (uint8_t)(1u << n);
        sched_add(&g->sched, EV_DMA, g->now + 2);
    }
}

/* Address control: 0 increment, 1 decrement, 2 fixed, 3 increment (+ reload for destinations). */
static int32_t step(uint32_t mode, uint32_t unit) {
    if (mode == 1) return -(int32_t)unit;
    if (mode == 2) return 0;
    return (int32_t)unit;
}

static void transfer(gba_t *g, int n) {
    dma_channel_t *c = &g->dma.ch[n];
    uint16_t ctl = control(g, n);
    bool word = (ctl & 0x400) != 0;
    uint32_t unit = word ? 4 : 2;
    int32_t src_step = step((ctl >> 7) & 3, unit), dst_step = step((ctl >> 5) & 3, unit);

    /* The CPU is stalled for the whole transfer: bus cycles simply advance the clock. */
    for (uint32_t i = 0; i < c->count; i++) {
        bool seq = i > 0;
        uint32_t value;
        if (c->src >= 0x02000000) {
            value = word ? bus_read32(g, c->src, seq) : bus_read16(g, c->src, seq) * 0x00010001u;
            g->dma.latch = value;
        } else {
            bus_idle(g, 1);
            value = g->dma.latch; /* the BIOS and unmapped space are not readable by DMA */
        }
        if (word) bus_write32(g, c->dst, value, seq);
        else bus_write16(g, c->dst, (uint16_t)(value >> ((c->dst & 2) * 8)), seq);
        c->src += (uint32_t)src_step;
        c->dst += (uint32_t)dst_step;
    }
    bus_idle(g, 2);

    if (ctl & 0x4000) irq_raise(g, (uint16_t)(IRQ_DMA0 << n));
    uint32_t timing = (ctl >> 12) & 3;
    if ((ctl & 0x200) && timing != TIMING_IMMEDIATE) {
        c->count = latched_count(g, n);
        if (((ctl >> 5) & 3) == 3) c->dst = reg32(g, channel_base(n) + 4) & dst_mask[n];
    } else {
        uint16_t off_ctl = (uint16_t)(ctl & ~0x8000);
        g->io.raw[channel_base(n) + 10] = (uint8_t)off_ctl;
        g->io.raw[channel_base(n) + 11] = (uint8_t)(off_ctl >> 8);
    }
}

static void trigger(gba_t *g, uint32_t timing) {
    for (int n = 0; n < 4; n++) {
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == timing) transfer(g, n);
    }
}

void dma_on_vblank(gba_t *g) {
    trigger(g, TIMING_VBLANK);
}

void dma_on_hblank(gba_t *g) {
    trigger(g, TIMING_HBLANK);
}

void dma_run_immediate(gba_t *g) {
    for (int n = 0; n < 4; n++) {
        if (!(g->dma.pending & (1u << n))) continue;
        g->dma.pending &= (uint8_t)~(1u << n);
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == TIMING_IMMEDIATE) transfer(g, n);
    }
}
```

- [x] **Step 6: Update `core/src/gba_internal.h`**

Add `#include "sys/dma.h"` after `#include "cart/cart.h"`, and add the member `dma_t dma;` after `timers_t timers;`.

- [x] **Step 7: Update `core/src/gba.c`**

In `gba_reset`, add after `timer_reset(g);`:

```c
    dma_reset(g);
```

In `gba_run_events`, add before `default: break;`:

```c
        case EV_DMA: dma_run_immediate(g); break;
```

- [x] **Step 8: Route DMA registers — `core/src/mem/io.c`**

Add after the `REG_TIMERS` enum:

```c
enum { REG_DMA = 0x0B0, REG_DMA_END = 0x0E0 };
```

In `write16`, add before the timer block:

```c
    if (off >= REG_DMA && off < REG_DMA_END) {
        uint16_t old = (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        if ((off - REG_DMA) % 12 == 10) dma_write_control(g, (int)((off - REG_DMA) / 12), old, v);
        return;
    }
```

- [x] **Step 9: Trigger VBlank/HBlank DMA — `core/src/ppu/ppu.c`**

Replace `ppu_on_hblank` with:

```c
void ppu_on_hblank(gba_t *g, uint64_t when) {
    (void)when;
    ppu_t *p = &g->ppu;
    bool visible = p->vcount < PPU_VISIBLE_LINES;
    if (visible) {
        ppu_render_line(g, p->vcount);
        affine_step(g);
    }
    p->dispstat |= DISPSTAT_HBLANK;
    if (p->dispstat & DISPSTAT_HBLANK_IRQ) irq_raise(g, IRQ_HBLANK);
    if (visible) dma_on_hblank(g);
}
```

In `ppu_on_line_end`, inside `if (p->vcount == PPU_VISIBLE_LINES) {`, add after `g->frame_done = true;`:

```c
        dma_on_vblank(g);
```

- [x] **Step 10: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: all tests pass, including the 7 `dma_*` tests.

- [x] **Step 11: Commit**

```bash
git add core/src tests/unit/test_dma.c
git commit -m "feat(core): add DMA with immediate, VBlank and HBlank transfers" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Shared tool code, PNG screenshots and test-runner options

**Files:**
- Create: `tools/common/CMakeLists.txt`, `tools/common/png.h`, `tools/common/png.c`, `tools/common/file_util.h`, `tools/common/file_util.c`, `tools/common/frame_hash.h`, `tools/png_probe.py`
- Modify: `CMakeLists.txt`, `tools/testrunner/CMakeLists.txt`, `tools/testrunner/main.c` (full replacement)

- [x] **Step 1: Create `tools/common/png.h`**

```c
#ifndef GBA_TOOLS_PNG_H
#define GBA_TOOLS_PNG_H

#include <stdint.h>

/* Write a BGR555 image as a 24-bit RGB PNG. Returns 0 on success. */
int png_write_bgr555(const char *path, const uint16_t *pixels, int width, int height);

#endif
```

- [x] **Step 2: Create `tools/common/png.c`**

```c
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t head[8], tail[4];
    put32be(head, len);
    memcpy(head + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    put32be(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4;
}

static uint8_t expand5(uint32_t c) {
    return (uint8_t)(c << 3 | c >> 2);
}

int png_write_bgr555(const char *path, const uint16_t *pixels, int width, int height) {
    crc_init();
    size_t row = 1 + (size_t)width * 3;
    size_t raw_len = row * (size_t)height;
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < height; y++) {
        uint8_t *out = raw + (size_t)y * row;
        *out++ = 0; /* filter: none */
        for (int x = 0; x < width; x++) {
            uint32_t c = pixels[y * width + x];
            *out++ = expand5(c & 31);
            *out++ = expand5((c >> 5) & 31);
            *out++ = expand5((c >> 10) & 31);
        }
    }

    /* zlib stream made of stored (uncompressed) deflate blocks. */
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    if (!z) {
        free(raw);
        return -1;
    }
    size_t p = 0;
    z[p++] = 0x78;
    z[p++] = 0x01;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        z[p++] = (off + n == raw_len) ? 1 : 0; /* BFINAL on the last block, BTYPE 00 */
        z[p++] = (uint8_t)n;
        z[p++] = (uint8_t)(n >> 8);
        z[p++] = (uint8_t)~n;
        z[p++] = (uint8_t)(~n >> 8);
        memcpy(z + p, raw + off, n);
        p += n;
        off += n;
    }
    uint32_t a = 1, b = 0; /* Adler-32 of the uncompressed data */
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32be(z + p, b << 16 | a);
    p += 4;

    uint8_t ihdr[13];
    put32be(ihdr, (uint32_t)width);
    put32be(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* colour type: RGB */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* interlace */

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(signature, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
             write_chunk(f, "IDAT", z, (uint32_t)p) && write_chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = 0;
    free(raw);
    free(z);
    return ok ? 0 : -1;
}
```

- [x] **Step 3: Create `tools/common/file_util.h` and `tools/common/file_util.c`**

`tools/common/file_util.h`:

```c
#ifndef GBA_TOOLS_FILE_UTIL_H
#define GBA_TOOLS_FILE_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Read a whole file; NULL if it is missing, empty or unreadable. Free the result with free(). */
uint8_t *read_file(const char *path, size_t *size);

#endif
```

`tools/common/file_util.c`:

```c
#include "file_util.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t *read_file(const char *path, size_t *size) {
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
```

- [x] **Step 4: Create `tools/common/frame_hash.h`**

```c
#ifndef GBA_TOOLS_FRAME_HASH_H
#define GBA_TOOLS_FRAME_HASH_H

#include <stdint.h>

/* 64-bit FNV-1a over the 240x160 BGR555 framebuffer, low byte of each pixel first. */
static inline uint64_t frame_hash(const uint16_t *pixels) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < 240 * 160; i++) {
        h = (h ^ (pixels[i] & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (uint32_t)(pixels[i] >> 8)) * 0x100000001B3ull;
    }
    return h;
}

#endif
```

- [x] **Step 5: Create `tools/common/CMakeLists.txt`**

```cmake
add_library(gbatools STATIC png.c file_util.c)
target_include_directories(gbatools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(gbatools PRIVATE -Wall -Wextra)
```

- [x] **Step 6: Wire it up**

In the root `CMakeLists.txt`, replace `add_subdirectory(tools/testrunner)` with:

```cmake
    add_subdirectory(tools/common)
    add_subdirectory(tools/testrunner)
```

Replace `tools/testrunner/CMakeLists.txt` with:

```cmake
add_executable(testrunner main.c)
target_link_libraries(testrunner PRIVATE gbacore gbatools)
target_compile_options(testrunner PRIVATE -Wall -Wextra)
```

- [x] **Step 7: Replace `tools/testrunner/main.c`**

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 * With --result-reg: run until the CPU spins on a branch-to-self (or <n> frames, default 600);
 * register n == 0 means PASS (jsmolka gba-tests keep the first failing test number there).
 * Otherwise with --frames: run exactly <n> frames. Otherwise: run until idle (at most 600 frames).
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

enum { EXIT_PASS = 0, EXIT_FAILED = 1, EXIT_TIMEOUT = 2, EXIT_USAGE = 3, EXIT_SKIP = 77 };

static bool at_idle_loop(gba_t *g) {
    uint32_t pc = gba_debug_reg(g, 15);
    if (gba_debug_cpsr(g) & (1u << 5)) { /* Thumb: b . */
        uint32_t word = gba_debug_peek32(g, pc & ~3u);
        uint16_t op = (pc & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        return op == 0xE7FE;
    }
    return gba_debug_peek32(g, pc) == 0xEAFFFFFE; /* ARM: b . */
}

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]\n"
                    "                  [--screenshot <file.png>] [--hash] [--expect-hash <hex>]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    long frames = 600;
    bool frames_given = false, print_hash = false;
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

    bool exact = frames_given && result_reg < 0;
    long frame = 0;
    if (exact) {
        for (; frame < frames; frame++) gba_run_frame(g);
    } else {
        while (frame < frames && !at_idle_loop(g)) {
            gba_run_frame(g);
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

    const uint16_t *fb = gba_framebuffer(g);
    uint64_t hash = frame_hash(fb);
    if (print_hash) printf("HASH %016" PRIx64 "\n", hash);
    if (shot_path && png_write_bgr555(shot_path, fb, 240, 160) != 0) {
        fprintf(stderr, "cannot write %s\n", shot_path);
        code = EXIT_USAGE;
    }
    if (expect_hash && code == EXIT_PASS) {
        uint64_t want = strtoull(expect_hash, NULL, 16);
        if (hash == want) {
            printf("PASS %s (frame %016" PRIx64 ")\n", rom_path, hash);
        } else {
            printf("FAIL %s: frame %016" PRIx64 ", expected %s\n", rom_path, hash, expect_hash);
            code = EXIT_FAILED;
        }
    }

done:
    gba_destroy(g);
    free(rom);
    return code;
}
```

- [x] **Step 8: Create `tools/png_probe.py`**

```python
#!/usr/bin/env python3
"""Validate a PNG written by tools/common/png.c and print pixel colours.

usage: png_probe.py <file.png> [x y]...
Prints '<width>x<height> ok', then '<x>,<y>: #RRGGBB' for each coordinate pair.
"""
import struct
import sys
import zlib


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("not a PNG")
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(kind + body) != crc:
            sys.exit(f"bad CRC in {kind.decode()}")
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 3
    if len(raw) != stride * height:
        sys.exit("bad image data length")
    return width, height, raw, stride


def main():
    if len(sys.argv) < 2 or len(sys.argv) % 2:
        sys.exit(__doc__)
    width, height, raw, stride = load(sys.argv[1])
    print(f"{width}x{height} ok")
    coords = sys.argv[2:]
    for i in range(0, len(coords), 2):
        x, y = int(coords[i]), int(coords[i + 1])
        p = y * stride + 1 + x * 3
        print(f"{x},{y}: #{raw[p]:02X}{raw[p + 1]:02X}{raw[p + 2]:02X}")


if __name__ == "__main__":
    main()
```

- [x] **Step 9: Build and check a real screenshot**

The jsmolka `arm` ROM (fetched in milestone 1) prints its result as black text on white in mode 4.

```bash
bash tools/dev.sh configure
mkdir -p build/shots
bash tools/dev.sh run tests/roms/jsmolka/arm.gba --frames 10 --screenshot build/shots/arm.png --hash
python tools/png_probe.py build/shots/arm.png 0 0
```

Expected: a `HASH <16 hex digits>` line; the probe prints `240x160 ok` and `0,0: #FFFFFF`. Open `build/shots/arm.png` (Read tool) and confirm it shows black text "All tests passed" near the middle of a white screen.

- [x] **Step 10: Check hash matching**

Run (replace `<hash>` with the value printed in Step 9):

```bash
bash tools/dev.sh run tests/roms/jsmolka/arm.gba --frames 10 --expect-hash <hash>; echo "exit=$?"
bash tools/dev.sh run tests/roms/jsmolka/arm.gba --frames 10 --expect-hash 0000000000000000; echo "exit=$?"
```

Expected: first prints `PASS ... (frame <hash>)` and `exit=0`; second prints `FAIL ...` and `exit=1`. Also run `bash tools/dev.sh test` — expected `100% tests passed`.

- [x] **Step 11: Commit**

```bash
git add CMakeLists.txt tools
git commit -m "feat(tools): add PNG screenshots, frame hashes and exact-frame runs to the test runner" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Test ROM download and frame-fingerprint tests

**Files:**
- Create: `tools/approve_frame.py`, `tests/expected_frames.json`
- Modify: `tools/fetch_test_roms.py` (full replacement), `CMakeLists.txt`

- [x] **Step 1: Replace `tools/fetch_test_roms.py`**

```python
#!/usr/bin/env python3
"""Download the test ROMs used by CTest into tests/roms/ (gitignored).

- jsmolka/gba-tests: pinned commit, sizes checked.
- Tonc demos: tonc-bin.zip from coranac.com; only the demos listed in TONC_DEMOS are kept.
Files that are already present with the right size are not downloaded again.
"""
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "tests/roms"
JSMOLKA_COMMIT = "a7113b67e63f83a9b321696ddd7042ccfad6c881"
JSMOLKA = {  # local name: (path in the repository, size in bytes)
    "arm.gba": ("arm/arm.gba", 8824),
    "thumb.gba": ("thumb/thumb.gba", 3680),
    "memory.gba": ("memory/memory.gba", 2172),
    "bios.gba": ("bios/bios.gba", 1908),
    "hello.gba": ("ppu/hello.gba", 1300),
    "shades.gba": ("ppu/shades.gba", 352),
    "stripes.gba": ("ppu/stripes.gba", 324),
}
TONC_URL = "https://www.coranac.com/files/tonc-bin.zip"
TONC_SIZE = 958346
TONC_DEMOS = [
    "first", "m3_demo", "bm_modes", "pageflip", "obj_demo", "obj_aff", "oacombo", "sbb_reg", "cbb_demo",
    "brin_demo", "sbb_aff", "bld_demo", "win_demo", "mos_demo", "dma_demo", "tmr_demo", "irq_demo",
    "m7_demo", "swi_demo",
]


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def fetch_jsmolka():
    out_dir = ROOT / "jsmolka"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, (path, size) in JSMOLKA.items():
        out = out_dir / name
        if out.exists() and out.stat().st_size == size:
            continue
        data = fetch(f"https://raw.githubusercontent.com/jsmolka/gba-tests/{JSMOLKA_COMMIT}/{path}")
        if len(data) != size:
            sys.exit(f"{name}: expected {size} bytes, got {len(data)}")
        out.write_bytes(data)
        print(f"jsmolka/{name}: {len(data)} bytes")


def fetch_tonc():
    out_dir = ROOT / "tonc"
    out_dir.mkdir(parents=True, exist_ok=True)
    if all((out_dir / f"{d}.gba").exists() for d in TONC_DEMOS):
        return
    data = fetch(TONC_URL)
    if len(data) != TONC_SIZE:
        sys.exit(f"tonc-bin.zip: expected {TONC_SIZE} bytes, got {len(data)}")
    found = {}
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        roms = [n for n in z.namelist() if n.lower().endswith(".gba")]
        for entry in roms:
            stem = entry.rsplit("/", 1)[-1][:-4]
            if stem.endswith(".mb"):
                stem = stem[:-3]
            if stem in TONC_DEMOS:
                found[stem] = z.read(entry)
    missing = [d for d in TONC_DEMOS if d not in found]
    if missing:
        print("ROMs in the archive:", ", ".join(sorted(roms)))
        sys.exit(f"missing Tonc demos: {', '.join(missing)}")
    for stem in TONC_DEMOS:
        (out_dir / f"{stem}.gba").write_bytes(found[stem])
        print(f"tonc/{stem}.gba: {len(found[stem])} bytes")


def main():
    fetch_jsmolka()
    fetch_tonc()
    print("test ROMs ready")


if __name__ == "__main__":
    main()
```

- [x] **Step 2: Create `tests/expected_frames.json`**

```json
{
  "frames": []
}
```

- [x] **Step 3: Create `tools/approve_frame.py`**

```python
#!/usr/bin/env python3
"""Record an approved frame fingerprint in tests/expected_frames.json.

usage: approve_frame.py <name> <rom path under tests/roms> <frames> <hash>
Only run this after looking at the screenshot and confirming it is correct.
"""
import json
import re
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parent.parent / "tests/expected_frames.json"


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    name, rom, frames, digest = sys.argv[1:]
    if not re.fullmatch(r"[a-z0-9_]+", name):
        sys.exit("name must be lowercase letters, digits and _")
    if not re.fullmatch(r"[0-9a-f]{16}", digest):
        sys.exit("hash must be 16 lowercase hex digits")
    data = json.loads(PATH.read_text())
    entries = [e for e in data["frames"] if e["name"] != name]
    entries.append({"name": name, "rom": rom, "frames": int(frames), "hash": digest})
    data["frames"] = sorted(entries, key=lambda e: e["name"])
    PATH.write_text(json.dumps(data, indent=2) + "\n", newline="\n")
    print(f"approved {name}: {digest}")


if __name__ == "__main__":
    main()
```

- [x] **Step 4: Register one CTest per approved frame — root `CMakeLists.txt`**

Inside the `if(NOT ANDROID)` block, after the jsmolka `foreach(...) ... endforeach()`, add:

```cmake
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
            add_test(NAME frame_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames} --expect-hash ${f_hash})
            set_tests_properties(frame_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()
```

- [x] **Step 5: Download the ROMs (needs the user's approval: about 0.9 MB from coranac.com plus 3 small files from GitHub)**

Run: `python tools/fetch_test_roms.py`
Expected: three `jsmolka/...` lines (hello, shades, stripes), 19 `tonc/...` lines, then `test ROMs ready`. If it prints `missing Tonc demos`, read the printed archive list, correct the names in `TONC_DEMOS`, and run again.

- [x] **Step 6: Check the fingerprint wiring**

```bash
bash tools/dev.sh configure
bash tools/dev.sh test
```

Expected: `100% tests passed, 0 tests failed out of 5` (no frame tests yet — the list is empty).

- [x] **Step 7: Commit**

```bash
git add CMakeLists.txt tools/fetch_test_roms.py tools/approve_frame.py tests/expected_frames.json
git commit -m "test: fetch graphics test ROMs and run approved frame fingerprints in CTest" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: `gbaview` live viewer

**Files:**
- Create: `tools/viewer/CMakeLists.txt`, `tools/viewer/main.c`
- Modify: `CMakeLists.txt`, `tools/dev.sh`

- [x] **Step 1: Create `tools/viewer/main.c`**

```c
/*
 * gbaview: play a ROM in a window.
 *   gbaview <rom.gba> [--bios <file>] [--autotest <frames> <out.png>]
 * Keys: arrows = D-pad, Z = A, X = B, A = L, S = R, Enter = Start, Backspace = Select,
 *       F12 = screenshot (gbaview-<n>.png), Esc = quit.
 * --autotest runs <frames> paced frames in the window, saves the last one, prints the
 * measured frame rate and exits.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "gba.h"
#include "png.h"

#define GBA_FPS 59.7275 /* 16777216 cycles/s / 280896 cycles per frame */
#define SCALE 3

static gba_t *gba;
static uint16_t held;
static uint32_t pixels[240 * 160]; /* 0x00RRGGBB, top-down */
static int shot_count;

static uint16_t key_for(WPARAM vk) {
    switch (vk) {
    case VK_UP: return GBA_KEY_UP;
    case VK_DOWN: return GBA_KEY_DOWN;
    case VK_LEFT: return GBA_KEY_LEFT;
    case VK_RIGHT: return GBA_KEY_RIGHT;
    case 'Z': return GBA_KEY_A;
    case 'X': return GBA_KEY_B;
    case 'A': return GBA_KEY_L;
    case 'S': return GBA_KEY_R;
    case VK_RETURN: return GBA_KEY_START;
    case VK_BACK: return GBA_KEY_SELECT;
    default: return 0;
    }
}

static uint32_t expand5(uint32_t c) {
    return c << 3 | c >> 2;
}

static void convert_frame(void) {
    const uint16_t *fb = gba_framebuffer(gba);
    for (int i = 0; i < 240 * 160; i++) {
        uint32_t c = fb[i];
        pixels[i] = expand5(c & 31) << 16 | expand5((c >> 5) & 31) << 8 | expand5((c >> 10) & 31);
    }
}

static void save_screenshot(const char *path) {
    if (png_write_bgr555(path, gba_framebuffer(gba), 240, 160) == 0) printf("saved %s\n", path);
    else fprintf(stderr, "cannot write %s\n", path);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wp == VK_F12) {
            char name[32];
            snprintf(name, sizeof name, "gbaview-%d.png", shot_count++);
            save_screenshot(name);
            return 0;
        }
        held |= key_for(wp);
        gba_set_keys(gba, held);
        return 0;
    case WM_KEYUP:
        held &= (uint16_t)~key_for(wp);
        gba_set_keys(gba, held);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof bmi);
        bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
        bmi.bmiHeader.biWidth = 240;
        bmi.bmiHeader.biHeight = -160; /* negative: rows run top to bottom */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR); /* nearest neighbour: sharp pixels */
        StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, 240, 160, pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static double seconds_now(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}

/* Sleep until `target` seconds, finishing with a short spin for accuracy. */
static void wait_until(double target) {
    for (;;) {
        double left = target - seconds_now();
        if (left <= 0) return;
        if (left > 0.002) Sleep((DWORD)((left - 0.001) * 1000));
    }
}

static int usage(void) {
    fprintf(stderr, "usage: gbaview <rom.gba> [--bios <file>] [--autotest <frames> <out.png>]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *auto_png = NULL;
    long auto_frames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--autotest") && i + 2 < argc) {
            auto_frames = strtol(argv[++i], NULL, 0);
            auto_png = argv[++i];
        } else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    if (!rom_path) return usage();

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 3;
    }
    gba = gba_create(NULL);
    if (bios_path) {
        size_t bios_size;
        uint8_t *bios = read_file(bios_path, &bios_size);
        gba_err_t err = bios ? gba_load_bios(gba, bios, bios_size) : GBA_ERR_BIOS_SIZE;
        free(bios);
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load BIOS %s\n", bios_path);
            return 3;
        }
    }
    if (gba_load_rom(gba, rom, rom_size) != GBA_OK) {
        fprintf(stderr, "cannot load ROM %s\n", rom_path);
        return 3;
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "gbaview";
    RegisterClassA(&wc);
    RECT r = {0, 0, 240 * SCALE, 160 * SCALE};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    char title[64];
    snprintf(title, sizeof title, "gbaview - %s", gba_rom_title(gba));
    HWND hwnd = CreateWindowA("gbaview", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        fprintf(stderr, "cannot create window\n");
        return 3;
    }

    timeBeginPeriod(1); /* 1 ms Sleep resolution */
    const double frame_time = 1.0 / GBA_FPS;
    double start = seconds_now(), next = start;
    long frames = 0;
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        gba_run_frame(gba);
        convert_frame();
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        frames++;

        if (auto_frames > 0 && frames >= auto_frames) {
            double elapsed = seconds_now() - start;
            printf("%ld frames in %.2f s (%.1f fps)\n", frames, elapsed, (double)frames / elapsed);
            save_screenshot(auto_png);
            DestroyWindow(hwnd);
            break;
        }
        next += frame_time;
        double now = seconds_now();
        if (now - next > 0.25) next = now; /* far behind (e.g. window dragged): resync instead of racing */
        wait_until(next);
    }
    timeEndPeriod(1);
    gba_destroy(gba);
    free(rom);
    return 0;
}
```

- [x] **Step 2: Create `tools/viewer/CMakeLists.txt`**

```cmake
add_executable(gbaview main.c)
target_link_libraries(gbaview PRIVATE gbacore gbatools gdi32 winmm)
target_compile_options(gbaview PRIVATE -Wall -Wextra)
```

- [x] **Step 3: Build it on Windows only — root `CMakeLists.txt`**

Inside `if(NOT ANDROID)`, after `add_subdirectory(tools/testrunner)`, add:

```cmake
    if(WIN32)
        add_subdirectory(tools/viewer)
    endif()
```

- [x] **Step 4: Add a `view` command to `tools/dev.sh`**

Add to the header comment:

```bash
#   tools/dev.sh view <args>      build, then run tools/viewer/gbaview with args (Windows)
```

Add a case before the `*)` line:

```bash
    view)  cmake --build "$BUILD" && "$BUILD/tools/viewer/gbaview" "$@" ;;
```

And change the usage line to:

```bash
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>" >&2; exit 2 ;;
```

- [x] **Step 5: Run the viewer's self-test**

```bash
bash tools/dev.sh configure
bash tools/dev.sh view tests/roms/jsmolka/hello.gba --autotest 120 build/shots/view.png
python tools/png_probe.py build/shots/view.png 0 0
```

Expected: a window appears for about 2 seconds; the output ends with `120 frames in 2.0x s (59.x fps)` (between 58 and 61 fps) and `saved build/shots/view.png`; the probe prints `240x160 ok` and `0,0: #000000`. Open `build/shots/view.png` and confirm it shows white "Hello world!" on black.

- [x] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/viewer tools/dev.sh
git commit -m "feat(tools): add gbaview live viewer with keyboard input and screenshots" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Approve the jsmolka screenshots

**Files:**
- Modify: `tests/expected_frames.json` (through `tools/approve_frame.py`)

- [x] **Step 1: Render all seven ROMs**

```bash
mkdir -p build/shots
for r in hello shades stripes arm thumb memory bios; do
  echo "== $r"; bash tools/dev.sh run tests/roms/jsmolka/$r.gba --frames 10 --screenshot build/shots/$r.png --hash | grep HASH
done
```

Expected: seven `HASH` lines.

- [x] **Step 2: Probe the exact colours**

```bash
python tools/png_probe.py build/shots/stripes.png 4 80 12 80
python tools/png_probe.py build/shots/shades.png 8 80 24 80 232 80
python tools/png_probe.py build/shots/hello.png 0 0
for r in arm thumb memory bios; do python tools/png_probe.py build/shots/$r.png 0 0; done
```

Expected:
- stripes: `4,80: #5A84AD` (backdrop 0x560B) and `12,80: #84A5C6` (colour 0x6290) — 8-pixel stripes alternating.
- shades: `8,80: #000000`, `24,80: #000010`, `232,80: #0000E7` — 16-pixel bands from black to bright blue.
- hello: `0,0: #000000`.
- arm/thumb/memory/bios: `0,0: #FFFFFF`.

- [x] **Step 3: Look at each picture**

Open every PNG in `build/shots/` (Read tool) and confirm:
- `hello.png`: white "Hello world!" on black, starting near x = 72, y = 76.
- `shades.png`: vertical bands, darkest on the left, brightening to blue on the right.
- `stripes.png`: even vertical stripes in two blue-grey shades across the whole screen.
- `arm/thumb/memory/bios.png`: black "All tests passed" on white.

If any picture is wrong, stop and fix it with superpowers:systematic-debugging: reproduce the wrong pixels in a unit test (a hand-built scene in `tests/unit/test_ppu_*.c`), fix, rerun Step 1.

- [x] **Step 4: Record the approved fingerprints**

For each ROM, run (with the hash from Step 1):

```bash
python tools/approve_frame.py jsmolka_<name> jsmolka/<name>.gba 10 <hash>
```

- [x] **Step 5: Verify CTest runs them**

```bash
bash tools/dev.sh configure
bash tools/dev.sh test
```

Expected: `100% tests passed, 0 tests failed out of 12` (unit tests, 4 jsmolka CPU ROMs, 7 frame tests).

- [x] **Step 6: Commit**

```bash
git add tests/expected_frames.json
git commit -m "test: approve jsmolka screenshots" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Approve the Tonc demo screenshots

**Files:**
- Modify: `tests/expected_frames.json`; any core file a failure points to

- [x] **Step 1: Render all 19 demos after one second (60 frames)**

```bash
for d in first m3_demo bm_modes pageflip obj_demo obj_aff oacombo sbb_reg cbb_demo brin_demo sbb_aff bld_demo win_demo mos_demo dma_demo tmr_demo irq_demo m7_demo swi_demo; do
  echo "$d $(bash tools/dev.sh run tests/roms/tonc/$d.gba --frames 60 --screenshot build/shots/$d.png --hash | grep HASH)"
done
```

Expected: 19 lines of `<demo> HASH <hex>`. None may print `TIMEOUT`, crash, or trigger a sanitizer report.

- [x] **Step 2: Compare each screenshot with the Tonc guide**

Open each `build/shots/<demo>.png` (Read tool) and the matching chapter in the browser pane; each chapter shows the demo's screenshot. Where the guide shows two versions (for example `cbb_demo_hw.png` vs `cbb_demo_vba.png`), match the **hardware** one.

| Demo | Chapter | Feature checked |
|---|---|---|
| first | https://gbadev.net/tonc/first.html | mode 3: red, green and blue dots at (120,80), (136,80), (120,96) |
| m3_demo | https://gbadev.net/tonc/bitmaps.html | mode 3 rectangles, frames, lines |
| bm_modes | https://gbadev.net/tonc/bitmaps.html | bitmap modes |
| pageflip | https://gbadev.net/tonc/bitmaps.html | mode 4 page flipping |
| obj_demo | https://gbadev.net/tonc/regobj.html | regular sprites, 1D/2D mapping |
| obj_aff | https://gbadev.net/tonc/affobj.html | affine sprites, double size |
| oacombo | https://gbadev.net/tonc/affobj.html | affine sprite combinations |
| sbb_reg | https://gbadev.net/tonc/regbg.html | large text maps, screen blocks |
| cbb_demo | https://gbadev.net/tonc/regbg.html | char blocks, 4bpp/8bpp (hardware result) |
| brin_demo | https://gbadev.net/tonc/regbg.html | scrolling tiled map |
| sbb_aff | https://gbadev.net/tonc/affbg.html | affine background |
| bld_demo | https://gbadev.net/tonc/gfx.html | alpha blending |
| win_demo | https://gbadev.net/tonc/gfx.html | windows |
| mos_demo | https://gbadev.net/tonc/gfx.html | mosaic |
| dma_demo | https://gbadev.net/tonc/dma.html | HBlank DMA effects |
| tmr_demo | https://gbadev.net/tonc/timers.html | timers (clock display) |
| irq_demo | https://gbadev.net/tonc/interrupts.html | HBlank/VCount interrupts |
| m7_demo | https://gbadev.net/tonc/mode7.html | per-line affine (mode 7) |
| swi_demo | https://gbadev.net/tonc/swi.html | BIOS math calls drawn as graphs |

Animated demos will not match the guide frame-for-frame; judge the content (layers, colours, sprite shapes, effects), not the exact animation phase.

- [x] **Step 3: Fix anything that is wrong**

For each mismatch use superpowers:systematic-debugging:
1. Identify the feature involved (table above) and the exact wrong pixels (`python tools/png_probe.py`).
2. Reproduce it in a unit test in the matching `tests/unit/test_ppu_*.c`, `test_dma.c` or `test_timer.c`; run it and watch it fail.
3. Fix the code, run `bash tools/dev.sh unit`, then re-render that demo.
4. Commit each fix separately: `git commit -m "fix(ppu): <what was wrong>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.

Only when a demo cannot be made to match for a reason outside this milestone's scope (for example it needs sound or the Stop mode), leave it unapproved and record why in `docs/PROGRESS.md`.

- [x] **Step 4: Record the approved fingerprints**

For each demo that matches (re-render first if code changed):

```bash
python tools/approve_frame.py tonc_<demo> tonc/<demo>.gba 60 <hash>
```

- [x] **Step 5: Verify CTest**

```bash
bash tools/dev.sh configure
bash tools/dev.sh test
```

Expected: `100% tests passed`, with one `frame_tonc_*` test per approved demo.

- [x] **Step 6: Commit**

```bash
git add tests/expected_frames.json
git commit -m "test: approve Tonc demo screenshots" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Milestone exit

**Files:**
- Modify: `docs/PROGRESS.md`, this plan's checkboxes

- [x] **Step 1: Full suite with sanitizers**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh test 2>&1 | grep -E "AddressSanitizer|runtime error" || echo "sanitizers clean"
```

Expected: `100% tests passed` (unit tests + 4 jsmolka CPU ROMs + 7 jsmolka frames + every approved Tonc frame) and `sanitizers clean`.

- [x] **Step 2: Live check with the user**

Ask the user to run the viewer and try the controls:

```bash
bash tools/dev.sh view tests/roms/tonc/obj_demo.gba
```

Expected: the game L sprite from `obj_demo` shows at full speed; the arrow keys move it; F12 saves `gbaview-0.png`; Esc closes the window. Record the user's result in `docs/PROGRESS.md`.

- [x] **Step 3: Update the docs and tag**

Update `docs/PROGRESS.md` (milestone 2 done, what's next: milestone 3 — sound, saves, prefetch, timing accuracy), then:

```bash
git add docs
git commit -m "docs: milestone 2 complete - graphics, DMA, timers and PC viewer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-2
```

- [x] **Step 4: Finish the branch**

Use superpowers:finishing-a-development-branch to merge `milestone-2` into `main` (the user chooses).

---

## Self-review notes

- **Spec coverage:** §3.1 registers/affine latch → Tasks 1, 3; §3.2 line timing, forced blank, VBlank latch, HBlank DMA order → Tasks 1, 3, 8; §3.3 buffers → Task 1; §3.4 text/affine/bitmap/mosaic/mode table → Tasks 2–4; §3.5 sprites incl. budget, 1D/2D, bitmap-mode tiles, affine, double size, OBJ window, semi-transparent, mosaic → Task 5; §3.6 windows, priority, effects → Tasks 1, 6; §4 DMA → Task 8; §5 timers → Task 7; §6 PNG/hash/runner/viewer/fetch → Tasks 9–11; §7 tests → every task + Tasks 12–13; §8 done criteria → Task 14.
- **Verified while writing:** Tonc demo names come from the Tonc source tree; each listed chapter URL returns 200 and names its demo's screenshot. Expected probe colours were computed from the jsmolka sources (stripes 0x560B/0x6290, shades palette `i * 0x800`).
