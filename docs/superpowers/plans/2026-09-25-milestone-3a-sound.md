# Milestone 3a — Sound (APU, DMA sound, PC audio) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Produce the GBA's sound — four PSG channels, two DMA sound channels, the hardware mixer — as a 32,768 Hz stereo stream, play it in `gbaview`, and lock it in with unit tests, pitch checks and approved audio fingerprints.

**Architecture:** A new `core/src/apu/` module. `EV_APU_SAMPLE` fires every 512 cycles: it advances the PSG channels, mixes exactly as the hardware does, and pushes one stereo frame into a ring buffer that the frontend drains with `gba_audio_read()`. `EV_APU_FRAME` (512 Hz) runs the frame sequencer. Timer 0/1 overflows pop the DMA-sound FIFOs, which ask DMA1/DMA2 (special timing) for 4 more words when 16 bytes or fewer remain. Sound registers keep their values in the I/O backing store (`io.raw`), like the PPU registers.

**Tech Stack:** C11 core, Win32 waveOut (winmm) in the viewer, Python 3 tools (no third-party packages), ARM assembly test ROMs built with the Android NDK's clang.

**Spec:** `docs/superpowers/specs/2026-09-25-milestone-3a-sound-design.md`

---

## Conventions (same as milestones 1–2)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; `CHECK_EQ` compares as `unsigned long long` (signed values on both sides compare correctly). Test names must not equal any function name.
- Sources are globbed; re-run `bash tools/dev.sh configure` after adding a new directory with a `CMakeLists.txt` or new source files in a new folder.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- After every task, update `docs/PROGRESS.md` and tick this plan's checkboxes (project rule in `CLAUDE.md`).
- Work on branch `milestone-3a`.
- **Timing facts used by the tests:** `gba_create()` resets the clock to 0; `EV_APU_SAMPLE` first fires at 512 and `EV_APU_FRAME` at 32768 (then every 512 / 32768 cycles). Every I/O access through the bus costs 1 cycle *before* its effect, so after `tu_io16(...)` the write happened at the new `g->now`. The first frame-sequencer event runs step 0, the 3rd step 2, the 7th step 6, the 8th step 7.
- **User commands:** the user runs PowerShell, where plain `bash` starts WSL. Give them `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh <cmd>`.

## File map

```
core/include/gba.h            + GBA_AUDIO_RATE, gba_audio_read()
core/src/apu/apu.h / apu.c    registers 0x080-0x0A7, FIFOs, mixer, sample/sequencer events, ring buffer (new)
core/src/apu/psg.h / psg.c    square 1/2, wave, noise, frame sequencer, wave RAM (new)
core/src/sys/sched.h          + EV_APU_FRAME, EV_APU_SAMPLE
core/src/sys/timer.c          timer 0/1 overflow → apu_on_timer_overflow()
core/src/sys/dma.h / dma.c    + dma_sound_request() (special timing, 4 words)
core/src/mem/io.c             + 0x060-0x0A7 routed to the APU; byte writes to the FIFOs
core/src/gba_internal.h       + apu_t apu
core/src/gba.c                + apu_reset, event dispatch, gba_audio_read
core/src/bios/hle.c           RegisterRamReset bit 6 → apu_reset
tools/common/wav.{h,c}        WAV writer (new)
tools/common/frame_hash.h     + audio_hash_update()
tools/testrunner/main.c       + --wav/--audio-hash/--expect-audio-hash
tools/viewer/main.c           + waveOut audio, audio pacing, --mute
tools/wav_probe.py            loudness + pitch per window, waveform PNG (new)
tools/build_test_roms.py      assembles tests/own_roms/src/*.S (new)
tools/approve_frame.py        + optional audio fingerprint
tools/fetch_test_roms.py      + Tonc snd1_demo
tests/own_roms/src/common.inc, snd_psg.S, snd_dma.S, snd_mix.S   (new)
tests/own_roms/*.gba          generated, committed
tests/expected_frames.json    + audio fingerprints
tests/unit/test_apu.c, test_psg.c, test_dma_sound.c   (new)
CMakeLists.txt                frame tests pass --expect-audio-hash when present
.gitignore                    !tests/own_roms/*.gba
```

---

### Task 1: APU core — registers, FIFOs, mixer, output buffer

**Files:**
- Create: `core/src/apu/apu.h`, `core/src/apu/apu.c`, `core/src/apu/psg.h`, `core/src/apu/psg.c` (silent stand-in, replaced in Task 2), `tests/unit/test_apu.c`
- Modify: `core/src/sys/sched.h`, `core/src/gba_internal.h`, `core/src/gba.c`, `core/include/gba.h`, `core/src/mem/io.c`, `core/src/sys/timer.c`, `core/src/bios/hle.c`

- [x] **Step 1: Create the branch**

```bash
git checkout -b milestone-3a
```

- [x] **Step 2: Write the failing tests `tests/unit/test_apu.c`**

```c
#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static gba_t *sound_on(void) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x084, 0x0080); /* SOUNDCNT_X: master enable */
    return g;
}

TEST(apu_soundbias_resets_to_0x200) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read16(g, 0x04000088, false), 0x0200);
    gba_destroy(g);
}

TEST(apu_one_sample_every_512_cycles) {
    gba_t *g = gba_create(NULL);
    int16_t buf[200 * 2];
    tu_advance_to(g, 512 * 10);
    CHECK_EQ(gba_audio_read(g, buf, 200), 10);
    CHECK_EQ(buf[0], 0); /* sound off: silence */
    CHECK_EQ(buf[19], 0);
    CHECK_EQ(gba_audio_read(g, buf, 200), 0);
    gba_destroy(g);
}

TEST(apu_ring_buffer_drops_when_nobody_reads) {
    gba_t *g = gba_create(NULL);
    static int16_t buf[4096 * 2];
    tu_advance_to(g, 512 * 3000);
    CHECK_EQ(gba_audio_read(g, buf, 4096), APU_RING_FRAMES);
    gba_destroy(g);
}

TEST(apu_master_off_clears_and_locks_psg_registers) {
    gba_t *g = sound_on();
    tu_io16(g, 0x080, 0xFF77);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0xFF77);
    tu_io16(g, 0x084, 0x0000);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x0000);
    tu_io16(g, 0x080, 0x1234); /* ignored while sound is off */
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x0000);
    tu_io16(g, 0x082, 0x0304); /* SOUNDCNT_H stays writable */
    CHECK_EQ(bus_read16(g, 0x04000082, false), 0x0304);
    tu_io16(g, 0x084, 0x0080);
    tu_io16(g, 0x080, 0x1234);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x1234);
    gba_destroy(g);
}

TEST(apu_mixer_psg_levels) {
    const uint8_t loud[4] = {15, 15, 15, 15};
    const int8_t none[2] = {0, 0};
    /* all four channels, volume 7, PSG 100%: (45 << 3) + (15 << 3) = 480; x8 = 3840; >> 2 = 960 → clamps at 0x3FF */
    CHECK_EQ(apu_mix_side(0xFF77, 0x0002, 0x200, loud, none, true), 32704);
    CHECK_EQ(apu_mix_side(0xFF77, 0x0003, 0x200, loud, none, false), 32704); /* ratio 3 acts as 100% */
    /* channel 1 on the left only, volume 0, PSG 25%: 120 >> 4 = 7 */
    CHECK_EQ(apu_mix_side(0x1000, 0x0000, 0x200, loud, none, true), 7 * 64);
    CHECK_EQ(apu_mix_side(0x1000, 0x0000, 0x200, loud, none, false), 0);
}

TEST(apu_mixer_dma_levels) {
    const uint8_t quiet[4] = {0, 0, 0, 0};
    const int8_t high[2] = {127, 64};
    const int8_t low[2] = {-128, 0};
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x200, quiet, high, true), 508 * 64);  /* A 100% */
    CHECK_EQ(apu_mix_side(0, 0x0300, 0x200, quiet, high, true), 254 * 64);  /* A 50% */
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x200, quiet, low, false), -32768);    /* -128 x 4 = -512 */
    CHECK_EQ(apu_mix_side(0, 0x1008, 0x200, quiet, high, false), 256 * 64); /* B 100%, right only */
    CHECK_EQ(apu_mix_side(0, 0x1008, 0x200, quiet, high, true), 0);
}

TEST(apu_mixer_bias_and_clamps) {
    const uint8_t loud[4] = {15, 15, 15, 15};
    const uint8_t quiet[4] = {0, 0, 0, 0};
    const int8_t low[2] = {-128, 0};
    const int8_t none[2] = {0, 0};
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x000, quiet, low, true), 0);            /* clamped at 0, minus bias 0 */
    CHECK_EQ(apu_mix_side(0xFF77, 0x0002, 0x000, loud, none, true), 32767);   /* 960 x 64 is past int16 */
}

TEST(apu_fifo_order_and_timer_select) {
    gba_t *g = sound_on();
    tu_io16(g, 0x082, 0x4000); /* A on timer 0, B on timer 1 */
    tu_io32(g, 0x0A0, 0x04030201);
    tu_io32(g, 0x0A4, 0x08070605);
    CHECK_EQ(g->apu.fifo[0].count, 4);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 1);
    CHECK_EQ(g->apu.fifo[1].sample, 0);
    apu_on_timer_overflow(g, 1);
    CHECK_EQ(g->apu.fifo[1].sample, 5);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 2);
    gba_destroy(g);
}

TEST(apu_fifo_drops_when_full_and_holds_when_empty) {
    gba_t *g = sound_on();
    for (uint32_t i = 0; i < 9; i++) {
        uint32_t b = i * 4;
        tu_io32(g, 0x0A0, b | (b + 1) << 8 | (b + 2) << 16 | (b + 3) << 24);
    }
    CHECK_EQ(g->apu.fifo[0].count, 32); /* bytes 32-35 were dropped */
    for (int i = 0; i < 32; i++) apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 31);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 31); /* empty: the last sample is held */
    gba_destroy(g);
}

TEST(apu_fifo_reset_bits_and_byte_writes) {
    gba_t *g = sound_on();
    bus_write8(g, 0x040000A0, 0x11, false);
    bus_write8(g, 0x040000A1, 0x22, false);
    CHECK_EQ(g->apu.fifo[0].count, 2);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 0x11);
    tu_io16(g, 0x082, 0x0800); /* reset FIFO A */
    CHECK_EQ(g->apu.fifo[0].count, 0);
    CHECK_EQ(bus_read16(g, 0x04000082, false) & 0x0800, 0);
    gba_destroy(g);
}

TEST(apu_timer_overflow_pops_the_fifo) {
    gba_t *g = sound_on();
    tu_io16(g, 0x082, 0x0304); /* A 100%, both sides, timer 0 */
    tu_io32(g, 0x0A0, 0x7F7F7F7F);
    tu_io16(g, 0x100, 0xFFF0);
    tu_io16(g, 0x102, 0x0080);
    tu_advance_to(g, g->timers.unit[0].start + 16);
    CHECK_EQ(g->apu.fifo[0].sample, 127);
    CHECK_EQ(g->apu.fifo[0].count, 3);
    gba_destroy(g);
}

TEST(apu_dma_sample_reaches_the_output) {
    gba_t *g = sound_on();
    int16_t out[2];
    tu_io16(g, 0x082, 0x0304);
    tu_io32(g, 0x0A0, 0x40404040);
    apu_on_timer_overflow(g, 0); /* sample 64 */
    tu_advance_to(g, 512);
    CHECK_EQ(gba_audio_read(g, out, 1), 1);
    CHECK_EQ(out[0], 16384); /* 64 x 4 x 64 */
    CHECK_EQ(out[1], 16384);
    gba_destroy(g);
}

TEST(apu_wave_ram_cpu_sees_the_other_bank) {
    gba_t *g = sound_on();
    tu_io16(g, 0x070, 0x0000); /* bank 0 playing */
    tu_io16(g, 0x090, 0xBEEF);
    CHECK_EQ(g->apu.psg.wave[1][0], 0xEF);
    CHECK_EQ(g->apu.psg.wave[1][1], 0xBE);
    CHECK_EQ(g->apu.psg.wave[0][0], 0x00);
    CHECK_EQ(bus_read16(g, 0x04000090, false), 0xBEEF);
    tu_io16(g, 0x070, 0x0040); /* bank 1 playing: the CPU now sees bank 0 */
    CHECK_EQ(bus_read16(g, 0x04000090, false), 0x0000);
    gba_destroy(g);
}

TEST(apu_frame_sequencer_steps_at_512_hz) {
    gba_t *g = sound_on();
    CHECK_EQ(g->apu.psg.step, 0);
    tu_advance_to(g, 32768);
    CHECK_EQ(g->apu.psg.step, 1);
    tu_advance_to(g, 9 * 32768);
    CHECK_EQ(g->apu.psg.step, 1);
    gba_destroy(g);
}

TEST(apu_bios_register_ram_reset_clears_sound) {
    gba_t *g = sound_on();
    tu_io32(g, 0x0A0, 0x01020304);
    tu_io16(g, 0x088, 0x0100);
    g->cpu.r[0] = 0x40; /* sound registers */
    hle_swi(g, 0x01);
    CHECK_EQ(g->apu.fifo[0].count, 0);
    CHECK_EQ(bus_read16(g, 0x04000088, false), 0x0200);
    CHECK_EQ(bus_read16(g, 0x04000084, false), 0x0000);
    gba_destroy(g);
}
```

- [x] **Step 3: Run to verify failure**

Run: `bash tools/dev.sh unit apu_`
Expected: build fails (`gba_audio_read` undeclared, no member `apu` in `struct gba`).

- [x] **Step 4: Add the APU events — replace the enum in `core/src/sys/sched.h`**

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
    EV_APU_FRAME,  /* 512 Hz sound frame-sequencer step */
    EV_APU_SAMPLE, /* one stereo output sample (every 512 cycles) */
    EV_COUNT
} sched_event_t;
```

- [x] **Step 5: Create `core/src/apu/psg.h`** (final interface; Task 2 fills in the channels)

```c
#ifndef GBA_PSG_H
#define GBA_PSG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    bool on;
    uint8_t volume;      /* envelope volume 0..15 */
    uint8_t env_timer;   /* sequencer envelope clocks left until the next volume step */
    uint16_t length;     /* length clocks left; 0 = expired */
    uint32_t timer;      /* cycles until the next waveform step (1..period) */
    uint8_t pos;         /* duty step 0..7 or wave sample 0..63 */
    uint16_t freq;       /* 11-bit frequency (square 1: updated by the sweep) */
    uint8_t sweep_timer; /* sequencer sweep clocks left */
    bool sweep_on;
    uint16_t lfsr;       /* noise shift register */
} psg_channel_t;

typedef struct {
    psg_channel_t ch[4]; /* square 1, square 2, wave, noise */
    uint8_t wave[2][16]; /* wave RAM banks: two 4-bit samples per byte, high nibble first */
    uint8_t step;        /* next frame-sequencer step 0..7 */
    uint64_t last;       /* cycle the channels were last advanced to */
} psg_t;

void psg_reset(psg_t *p, uint64_t now); /* channels off, sequencer at step 0; wave RAM kept */
void psg_advance(gba_t *g, uint64_t now); /* run the channel timers up to cycle now */
void psg_frame_step(gba_t *g);            /* one 512 Hz sequencer step */
/* A PSG register (0x060-0x07F) was written; io.raw already holds the value. */
void psg_write(gba_t *g, uint32_t off, uint16_t value);
uint8_t psg_output(const gba_t *g, int ch); /* current channel level 0..15 */
uint8_t psg_status(const gba_t *g);        /* SOUNDCNT_X bits 0-3: channels playing */
uint16_t psg_wave_read16(const gba_t *g, uint32_t off); /* 0x090-0x09F: the bank that is not playing */
void psg_wave_write16(gba_t *g, uint32_t off, uint16_t value);

#endif
```

- [x] **Step 6: Create the silent stand-in `core/src/apu/psg.c`** (Task 2 replaces this file)

```c
#include "apu/psg.h"

#include <string.h>

#include "gba_internal.h"

/* Stand-in until Task 2: the channels never play, but the sequencer and wave RAM work. */

static uint16_t reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

void psg_reset(psg_t *p, uint64_t now) {
    memset(p->ch, 0, sizeof p->ch);
    p->step = 0;
    p->last = now;
}

void psg_advance(gba_t *g, uint64_t now) {
    if (now > g->apu.psg.last) g->apu.psg.last = now;
}

void psg_frame_step(gba_t *g) {
    g->apu.psg.step = (uint8_t)((g->apu.psg.step + 1) & 7);
}

void psg_write(gba_t *g, uint32_t off, uint16_t value) {
    (void)g;
    (void)off;
    (void)value;
}

uint8_t psg_output(const gba_t *g, int ch) {
    (void)g;
    (void)ch;
    return 0;
}

uint8_t psg_status(const gba_t *g) {
    (void)g;
    return 0;
}

/* The CPU reads and writes the wave bank that is not selected for playback (SOUND3CNT_L bit 6). */
static uint8_t *cpu_wave_bank(const gba_t *g) {
    uint32_t bank = ((reg(g, 0x070) >> 6) & 1) ^ 1;
    return (uint8_t *)g->apu.psg.wave[bank];
}

uint16_t psg_wave_read16(const gba_t *g, uint32_t off) {
    const uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    return (uint16_t)(b[0] | b[1] << 8);
}

void psg_wave_write16(gba_t *g, uint32_t off, uint16_t value) {
    uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    b[0] = (uint8_t)value;
    b[1] = (uint8_t)(value >> 8);
}
```

- [x] **Step 7: Create `core/src/apu/apu.h`**

```c
#ifndef GBA_APU_H
#define GBA_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apu/psg.h"

typedef struct gba gba_t;

#define APU_SAMPLE_CYCLES 512u      /* 16,777,216 / 512 = 32,768 samples per second */
#define APU_FRAME_SEQ_CYCLES 32768u /* 512 Hz frame sequencer */
#define APU_RING_FRAMES 2048u       /* stereo frames kept for gba_audio_read (about 3.7 video frames) */

typedef struct {
    int8_t data[32];
    uint8_t read;  /* index of the oldest byte */
    uint8_t count; /* bytes queued, 0..32 */
    int8_t sample; /* current output, held until the next pop */
} apu_fifo_t;

typedef struct {
    psg_t psg;
    apu_fifo_t fifo[2];                /* 0 = FIFO A, 1 = FIFO B */
    int16_t ring[APU_RING_FRAMES * 2]; /* interleaved left, right */
    uint32_t ring_read, ring_count;    /* in stereo frames */
} apu_t;

/* Clears all sound state, sets SOUNDBIAS to 0x200 and schedules the sample and sequencer events. */
void apu_reset(gba_t *g);
void apu_on_sample(gba_t *g, uint64_t when);
void apu_on_frame_seq(gba_t *g, uint64_t when);
void apu_on_timer_overflow(gba_t *g, int timer); /* timer 0 or 1 overflowed */
uint16_t apu_read16(gba_t *g, uint32_t off);      /* I/O offsets 0x060-0x0A7 */
void apu_write16(gba_t *g, uint32_t off, uint16_t value);
void apu_write_fifo8(gba_t *g, int fifo, uint8_t value);
size_t apu_read(gba_t *g, int16_t *out, size_t max_frames);
/* One output value for one side: the hardware mixer's arithmetic (exposed for tests). */
int16_t apu_mix_side(uint16_t soundcnt_l, uint16_t soundcnt_h, uint16_t bias, const uint8_t psg[4],
                     const int8_t fifo[2], bool left);

#endif
```

- [x] **Step 8: Create `core/src/apu/apu.c`**

```c
#include "apu/apu.h"

#include <string.h>

#include "gba_internal.h"

enum {
    REG_PSG_FIRST = 0x060,
    REG_SOUNDCNT_L = 0x080,
    REG_SOUNDCNT_H = 0x082,
    REG_SOUNDCNT_X = 0x084,
    REG_SOUNDBIAS = 0x088,
    REG_WAVE_RAM = 0x090,
    REG_FIFO_A = 0x0A0,
};

static uint16_t raw16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static void set_raw16(gba_t *g, uint32_t off, uint16_t v) {
    g->io.raw[off] = (uint8_t)v;
    g->io.raw[off + 1] = (uint8_t)(v >> 8);
}

static bool master_on(const gba_t *g) {
    return (g->io.raw[REG_SOUNDCNT_X] & 0x80) != 0;
}

void apu_reset(gba_t *g) {
    memset(&g->apu, 0, sizeof g->apu);
    psg_reset(&g->apu.psg, g->now);
    set_raw16(g, REG_SOUNDBIAS, 0x0200);
    sched_add(&g->sched, EV_APU_SAMPLE, g->now + APU_SAMPLE_CYCLES);
    sched_add(&g->sched, EV_APU_FRAME, g->now + APU_FRAME_SEQ_CYCLES);
}

/* ---- DMA sound FIFOs ---- */

static void fifo_push(apu_fifo_t *f, uint8_t v) {
    if (f->count == 32) return; /* full: the byte is lost */
    f->data[(f->read + f->count) & 31] = (int8_t)v;
    f->count++;
}

static void fifo_clear(apu_fifo_t *f) {
    f->read = 0;
    f->count = 0;
}

void apu_write_fifo8(gba_t *g, int fifo, uint8_t value) {
    fifo_push(&g->apu.fifo[fifo], value);
}

void apu_on_timer_overflow(gba_t *g, int timer) {
    if (!master_on(g)) return;
    uint16_t cnt_h = raw16(g, REG_SOUNDCNT_H);
    for (int i = 0; i < 2; i++) {
        if ((int)((cnt_h >> (10 + 4 * i)) & 1) != timer) continue;
        apu_fifo_t *f = &g->apu.fifo[i];
        if (f->count) { /* an empty FIFO keeps playing its last sample */
            f->sample = f->data[f->read];
            f->read = (uint8_t)((f->read + 1) & 31);
            f->count--;
        }
    }
}

/* ---- mixer and output ---- */

int16_t apu_mix_side(uint16_t soundcnt_l, uint16_t soundcnt_h, uint16_t bias, const uint8_t psg[4],
                     const int8_t fifo[2], bool left) {
    uint32_t enables = (left ? soundcnt_l >> 12 : soundcnt_l >> 8) & 0xF;
    int volume = left ? (soundcnt_l >> 4) & 7 : soundcnt_l & 7;
    int sum = 0;
    for (int n = 0; n < 3; n++) {
        if (enables & (1u << n)) sum += psg[n];
    }
    sum <<= 3;
    if (enables & 8) sum += psg[3] << 3;
    sum *= 1 + volume;
    int ratio = soundcnt_h & 3;
    sum >>= 4 - (ratio == 3 ? 2 : ratio); /* 25% / 50% / 100% (3 is not allowed; treated as 100%) */
    for (int i = 0; i < 2; i++) {
        uint32_t sides = soundcnt_h >> (8 + 4 * i); /* bit 0 right, bit 1 left */
        if (!(sides & (left ? 2u : 1u))) continue;
        int full = (soundcnt_h >> (2 + i)) & 1;
        sum += (fifo[i] * 4) >> (full ? 0 : 1);
    }
    sum += bias;
    if (sum < 0) sum = 0;
    if (sum > 0x3FF) sum = 0x3FF; /* the 10-bit output */
    int out = (sum - bias) * 64;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

static void ring_push(apu_t *a, int16_t left, int16_t right) {
    if (a->ring_count == APU_RING_FRAMES) return; /* nobody is reading: drop */
    uint32_t w = (a->ring_read + a->ring_count) % APU_RING_FRAMES;
    a->ring[w * 2] = left;
    a->ring[w * 2 + 1] = right;
    a->ring_count++;
}

void apu_on_sample(gba_t *g, uint64_t when) {
    int16_t left = 0, right = 0;
    psg_advance(g, when);
    if (master_on(g)) {
        uint8_t psg[4];
        for (int n = 0; n < 4; n++) psg[n] = psg_output(g, n);
        const int8_t fifo[2] = {g->apu.fifo[0].sample, g->apu.fifo[1].sample};
        uint16_t cnt_l = raw16(g, REG_SOUNDCNT_L), cnt_h = raw16(g, REG_SOUNDCNT_H);
        uint16_t bias = raw16(g, REG_SOUNDBIAS) & 0x3FE;
        left = apu_mix_side(cnt_l, cnt_h, bias, psg, fifo, true);
        right = apu_mix_side(cnt_l, cnt_h, bias, psg, fifo, false);
    }
    ring_push(&g->apu, left, right);
    sched_add(&g->sched, EV_APU_SAMPLE, when + APU_SAMPLE_CYCLES);
}

void apu_on_frame_seq(gba_t *g, uint64_t when) {
    psg_advance(g, when);
    if (master_on(g)) psg_frame_step(g);
    sched_add(&g->sched, EV_APU_FRAME, when + APU_FRAME_SEQ_CYCLES);
}

size_t apu_read(gba_t *g, int16_t *out, size_t max_frames) {
    apu_t *a = &g->apu;
    size_t n = 0;
    while (n < max_frames && a->ring_count) {
        out[n * 2] = a->ring[a->ring_read * 2];
        out[n * 2 + 1] = a->ring[a->ring_read * 2 + 1];
        a->ring_read = (a->ring_read + 1) % APU_RING_FRAMES;
        a->ring_count--;
        n++;
    }
    return n;
}

/* ---- registers ---- */

uint16_t apu_read16(gba_t *g, uint32_t off) {
    if (off == REG_SOUNDCNT_X) return (uint16_t)((raw16(g, off) & 0x80) | psg_status(g));
    if (off >= REG_WAVE_RAM && off < REG_WAVE_RAM + 16) return psg_wave_read16(g, off);
    return raw16(g, off);
}

void apu_write16(gba_t *g, uint32_t off, uint16_t value) {
    psg_advance(g, g->now); /* the change takes effect from this cycle */
    if (off >= REG_FIFO_A) {
        apu_fifo_t *f = &g->apu.fifo[(off - REG_FIFO_A) >> 2];
        fifo_push(f, (uint8_t)value);
        fifo_push(f, (uint8_t)(value >> 8));
        return;
    }
    if (off >= REG_WAVE_RAM && off < REG_WAVE_RAM + 16) {
        psg_wave_write16(g, off, value);
        return;
    }
    if (off < REG_SOUNDCNT_H && !master_on(g)) return; /* 0x060-0x081 are locked while sound is off */
    uint16_t old = raw16(g, off);
    switch (off) {
    case REG_SOUNDCNT_H:
        if (value & 0x0800) fifo_clear(&g->apu.fifo[0]);
        if (value & 0x8000) fifo_clear(&g->apu.fifo[1]);
        set_raw16(g, off, value & 0x770F); /* the reset bits read back as 0 */
        break;
    case REG_SOUNDCNT_X:
        set_raw16(g, off, value & 0x0080);
        if ((old & 0x80) && !(value & 0x80)) { /* sound off: PSG registers cleared, channels stopped */
            memset(&g->io.raw[REG_PSG_FIRST], 0, REG_SOUNDCNT_H - REG_PSG_FIRST);
            psg_reset(&g->apu.psg, g->now);
        } else if (!(old & 0x80) && (value & 0x80)) {
            g->apu.psg.step = 0; /* the sequencer restarts at step 0 */
        }
        break;
    default:
        set_raw16(g, off, value);
        if (off < REG_SOUNDCNT_L) psg_write(g, off, value);
        break;
    }
}
```

- [x] **Step 9: Add `apu_t` to the system — `core/src/gba_internal.h`**

Add `#include "apu/apu.h"` before `#include "cart/cart.h"`, and add the member `apu_t apu;` after `dma_t dma;`.

- [x] **Step 10: Reset, events and the public call — `core/src/gba.c`**

In `gba_reset`, add after `dma_reset(g);`:

```c
    apu_reset(g);
```

In `gba_run_events`, add before `default: break;`:

```c
        case EV_APU_FRAME: apu_on_frame_seq(g, when); break;
        case EV_APU_SAMPLE: apu_on_sample(g, when); break;
```

After `gba_framebuffer` add:

```c
size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames) {
    return apu_read(g, out, max_frames);
}
```

- [x] **Step 11: Declare it — `core/include/gba.h`**

After the `gba_framebuffer` declaration add:

```c
#define GBA_AUDIO_RATE 32768 /* stereo samples per second from gba_audio_read */
/* Copies up to max_frames stereo samples (interleaved int16 left, right) made since the last call
 * and returns how many. The core keeps about 3.7 frames of sound; anything older is dropped. */
size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames);
```

- [x] **Step 12: Route the sound registers — `core/src/mem/io.c`**

After the `REG_DMA` enum add:

```c
enum { REG_SOUND = 0x060, REG_SOUND_END = 0x0A8, REG_FIFO = 0x0A0 };
```

In `read16`, add as the first line:

```c
    if (off >= REG_SOUND && off < REG_SOUND_END) return apu_read16(g, off);
```

In `write16`, add as the first lines:

```c
    if (off >= REG_SOUND && off < REG_SOUND_END) {
        apu_write16(g, off, v);
        return;
    }
```

In `write8`, add before `default: {`:

```c
    case REG_FIFO:
    case REG_FIFO + 1:
    case REG_FIFO + 2:
    case REG_FIFO + 3:
    case REG_FIFO + 4:
    case REG_FIFO + 5:
    case REG_FIFO + 6:
    case REG_FIFO + 7: apu_write_fifo8(g, (int)((off - REG_FIFO) >> 2), v); return; /* one byte queued */
```

- [x] **Step 13: Timers 0 and 1 clock the FIFOs — `core/src/sys/timer.c`**

In `overflow()`, add after the `irq_raise` line:

```c
    if (n < 2) apu_on_timer_overflow(g, n);
```

- [x] **Step 14: RegisterRamReset bit 6 resets sound — `core/src/bios/hle.c`**

In `register_ram_reset`, replace

```c
    if (flags & 0x40) memset(&g->io.raw[0x060], 0, 0x0B0 - 0x060); /* sound */
```

with

```c
    if (flags & 0x40) { /* sound */
        memset(&g->io.raw[0x060], 0, 0x0B0 - 0x060);
        apu_reset(g);
    }
```

- [x] **Step 15: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes (189 from milestones 1–2 plus the 15 new `apu_*` tests), no warnings.

- [x] **Step 16: Commit**

```bash
git add core tests/unit/test_apu.c
git commit -m "feat(apu): add sound registers, DMA-sound FIFOs, mixer and output buffer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: PSG channels and frame sequencer

**Files:**
- Modify: `core/src/apu/psg.c` (full replacement)
- Test: `tests/unit/test_psg.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_psg.c`**

```c
#include "test.h"
#include "testutil.h"

static gba_t *sound_on(void) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x084, 0x0080);
    return g;
}

/* Channel ch's level at cycles t0, t0 + period, t0 + 2 * period, ... */
static void levels(gba_t *g, int ch, uint64_t t0, uint32_t period, int count, uint8_t *out) {
    for (int i = 0; i < count; i++) {
        psg_advance(g, t0 + (uint64_t)i * period);
        out[i] = psg_output(g, ch);
    }
}

static uint16_t status(gba_t *g) {
    return bus_read16(g, 0x04000084, false) & 0x0F;
}

TEST(psg_square_duty_patterns) {
    static const uint8_t expect[4][8] = {{0, 0, 0, 0, 0, 0, 0, 15},
                                         {15, 0, 0, 0, 0, 0, 0, 15},
                                         {15, 0, 0, 0, 0, 15, 15, 15},
                                         {0, 15, 15, 15, 15, 15, 15, 0}};
    for (int d = 0; d < 4; d++) {
        gba_t *g = sound_on();
        tu_io16(g, 0x068, (uint16_t)(0xF000 | d << 6)); /* volume 15, duty d */
        tu_io16(g, 0x06C, 0x8000 | 2047);               /* trigger: 16 cycles per step */
        uint8_t got[8];
        levels(g, 1, g->now, 16, 8, got);
        for (int i = 0; i < 8; i++) CHECK_EQ(got[i], expect[d][i]);
        gba_destroy(g);
    }
}

TEST(psg_square_step_length) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF080);        /* 50% duty */
    tu_io16(g, 0x06C, 0x8000 | 1750); /* 16 x 298 = 4768 cycles per step */
    uint64_t t0 = g->now;
    psg_advance(g, t0 + 4767);
    CHECK_EQ(psg_output(g, 1), 15);
    psg_advance(g, t0 + 4768);
    CHECK_EQ(psg_output(g, 1), 0);
    gba_destroy(g);
}

TEST(psg_register_writes_catch_the_channels_up_first) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF080);
    tu_io16(g, 0x06C, 0x8000 | 2047);
    g->now += 20;                /* one step (16 cycles) passes without a sample event */
    tu_io16(g, 0x06C, 1000);     /* new frequency, no trigger */
    CHECK_EQ(g->apu.psg.ch[1].pos, 1);
    gba_destroy(g);
}

TEST(psg_length_counter_stops_the_channel) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF03F);        /* length 63: one length clock left */
    tu_io16(g, 0x06C, 0xC000 | 1000); /* trigger + length enable */
    CHECK_EQ(status(g), 0x02);
    tu_advance_to(g, 32767);
    CHECK_EQ(status(g), 0x02);
    tu_advance_to(g, 32768); /* step 0 clocks the lengths */
    CHECK_EQ(status(g), 0x00);
    gba_destroy(g);
}

TEST(psg_length_ignored_unless_enabled) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF03F);
    tu_io16(g, 0x06C, 0x8000 | 1000); /* no length enable */
    tu_advance_to(g, 4 * 32768);
    CHECK_EQ(status(g), 0x02);
    gba_destroy(g);
}

TEST(psg_envelope_steps_on_sequencer_step_7) {
    gba_t *g = sound_on();
    tu_io16(g, 0x062, 0x7100); /* volume 7, down, every step */
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_io16(g, 0x068, 0xE900); /* volume 14, up, every step */
    tu_io16(g, 0x06C, 0x8000 | 1000);
    tu_io16(g, 0x078, 0x5000); /* volume 5, frozen */
    tu_io16(g, 0x07C, 0x8000);
    tu_advance_to(g, 8 * 32768 - 1);
    CHECK_EQ(g->apu.psg.ch[0].volume, 7);
    tu_advance_to(g, 8 * 32768); /* the 8th sequencer event is step 7 */
    CHECK_EQ(g->apu.psg.ch[0].volume, 6);
    CHECK_EQ(g->apu.psg.ch[1].volume, 15);
    CHECK_EQ(g->apu.psg.ch[3].volume, 5);
    tu_advance_to(g, 16 * 32768);
    CHECK_EQ(g->apu.psg.ch[0].volume, 5);
    CHECK_EQ(g->apu.psg.ch[1].volume, 15); /* stays at the top */
    gba_destroy(g);
}

TEST(psg_sweep_raises_frequency_until_overflow) {
    gba_t *g = sound_on();
    tu_io16(g, 0x060, 0x0012); /* sweep time 1, up, shift 2 */
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_advance_to(g, 3 * 32768); /* step 2 */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1250);
    CHECK_EQ(bus_read16(g, 0x04000064, false) & 0x7FF, 1250);
    tu_advance_to(g, 7 * 32768); /* step 6 */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1562);
    CHECK_EQ(status(g) & 1, 1);
    tu_advance_to(g, 11 * 32768); /* 1562 → 1952; the next value (2440) would pass 2047: off */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1952);
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

TEST(psg_sweep_down_and_overflow_at_trigger) {
    gba_t *g = sound_on();
    tu_io16(g, 0x060, 0x0019); /* time 1, down, shift 1 */
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_advance_to(g, 3 * 32768);
    CHECK_EQ(g->apu.psg.ch[0].freq, 500);
    tu_io16(g, 0x060, 0x0011);        /* up, shift 1 */
    tu_io16(g, 0x064, 0x8000 | 2000); /* 2000 + 1000 > 2047: off at once */
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

TEST(psg_dac_off_keeps_the_channel_silent) {
    gba_t *g = sound_on();
    tu_io16(g, 0x062, 0x0080); /* volume 0, fading down: DAC off */
    tu_io16(g, 0x064, 0x8000 | 1000);
    CHECK_EQ(status(g) & 1, 0);
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    CHECK_EQ(status(g) & 1, 1);
    tu_io16(g, 0x062, 0x0000); /* turning the DAC off stops it */
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

/* Bank 0 = samples 0, 1, ..., 15, 0, 1, ..., 15; bank 1 = fill. Leaves bank 0 selected for playback. */
static void load_wave(gba_t *g, uint16_t fill) {
    static const uint16_t ramp[4] = {0x2301, 0x6745, 0xAB89, 0xEFCD};
    tu_io16(g, 0x070, 0x0040); /* bank 1 playing: the CPU sees bank 0 */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, ramp[i & 3]);
    tu_io16(g, 0x070, 0x0000); /* bank 0 playing: the CPU sees bank 1 */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, fill);
}

TEST(psg_wave_plays_bank_samples) {
    gba_t *g = sound_on();
    load_wave(g, 0xFFFF);
    tu_io16(g, 0x070, 0x0080);        /* bank 0, DAC on */
    tu_io16(g, 0x072, 0x2000);        /* 100% */
    tu_io16(g, 0x074, 0x8000 | 2047); /* 8 cycles per sample */
    uint8_t got[34];
    levels(g, 2, g->now, 8, 34, got);
    for (int i = 0; i < 34; i++) CHECK_EQ(got[i], (i % 32) & 15);
    gba_destroy(g);
}

TEST(psg_wave_64_sample_mode_plays_both_banks) {
    gba_t *g = sound_on();
    load_wave(g, 0xFFFF);
    tu_io16(g, 0x070, 0x00A0); /* 64 samples, starting at bank 0, DAC on */
    tu_io16(g, 0x072, 0x2000);
    tu_io16(g, 0x074, 0x8000 | 2047);
    uint8_t got[65];
    levels(g, 2, g->now, 8, 65, got);
    CHECK_EQ(got[16], 0);
    CHECK_EQ(got[32], 15); /* bank 1 */
    CHECK_EQ(got[63], 15);
    CHECK_EQ(got[64], 0); /* back to bank 0 */
    gba_destroy(g);
}

TEST(psg_wave_volume_codes) {
    static const struct {
        uint16_t reg;
        uint8_t level;
    } cases[] = {{0x0000, 0}, {0x2000, 13}, {0x4000, 6}, {0x6000, 3}, {0x8000, 9}};
    for (size_t k = 0; k < TU_LEN(cases); k++) {
        gba_t *g = sound_on();
        load_wave(g, 0xFFFF);
        tu_io16(g, 0x070, 0x0080);
        tu_io16(g, 0x072, cases[k].reg);
        tu_io16(g, 0x074, 0x8000 | 2047);
        psg_advance(g, g->now + 13 * 8); /* sample 13 */
        CHECK_EQ(psg_output(g, 2), cases[k].level);
        gba_destroy(g);
    }
}

TEST(psg_noise_lfsr_15_bit) {
    gba_t *g = sound_on();
    tu_io16(g, 0x078, 0xF000); /* volume 15 */
    tu_io16(g, 0x07C, 0x8000); /* r 0, s 0: a step every 32 cycles */
    uint64_t t0 = g->now;
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x7FFF);
    CHECK_EQ(psg_output(g, 3), 0); /* bit 0 set: low */
    psg_advance(g, t0 + 14 * 32);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x0001);
    psg_advance(g, t0 + 15 * 32);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x4000);
    CHECK_EQ(psg_output(g, 3), 15);
    gba_destroy(g);
}

TEST(psg_noise_7_bit_and_period) {
    gba_t *g = sound_on();
    tu_io16(g, 0x078, 0xF000);
    tu_io16(g, 0x07C, 0x8000 | 0x08 | 0x20 | 1); /* 7-bit, s 2, r 1: 64 x 1 << 2 = 256 cycles */
    uint64_t t0 = g->now;
    psg_advance(g, t0 + 255);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x7FFF);
    psg_advance(g, t0 + 256);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x3FBF);
    gba_destroy(g);
}

TEST(psg_levels_reach_the_mixed_output) {
    gba_t *g = sound_on();
    int16_t buf[4096 * 2];
    tu_io16(g, 0x070, 0x0040); /* fill bank 0 with 15s */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, 0xFFFF);
    tu_io16(g, 0x070, 0x0080);
    tu_io16(g, 0x072, 0x2000);
    tu_io16(g, 0x080, 0x4477); /* wave channel on both sides, volume 7 */
    tu_io16(g, 0x082, 0x0002); /* PSG 100% */
    tu_io16(g, 0x074, 0x8000 | 2047);
    gba_audio_read(g, buf, 4096);
    tu_advance_to(g, (g->now / 512 + 1) * 512);
    CHECK_EQ(gba_audio_read(g, buf, 4096), 1);
    CHECK_EQ(buf[0], 15360); /* 15 << 3 = 120; x8 = 960; >> 2 = 240; x64 */
    CHECK_EQ(buf[1], 15360);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit psg_`
Expected: most `psg_*` tests FAIL (the stand-in keeps every channel silent).

- [x] **Step 3: Replace `core/src/apu/psg.c`**

```c
#include "apu/psg.h"

#include <string.h>

#include "gba_internal.h"

enum { SQUARE1, SQUARE2, WAVE, NOISE };

/* 8-step duty patterns, step 0 in the top bit: 12.5%, 25%, 50%, 75%. */
static const uint8_t duty_pattern[4] = {0x01, 0x81, 0x87, 0x7E};
/* Length/envelope/duty register (the wave channel's is its length/volume register). */
static const uint32_t env_reg[4] = {0x062, 0x068, 0x072, 0x078};
/* Control register with the length-enable (bit 14) and trigger (bit 15) bits. */
static const uint32_t ctl_reg[4] = {0x064, 0x06C, 0x074, 0x07C};

static uint16_t reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static void set_reg(gba_t *g, uint32_t off, uint16_t v) {
    g->io.raw[off] = (uint8_t)v;
    g->io.raw[off + 1] = (uint8_t)(v >> 8);
}

void psg_reset(psg_t *p, uint64_t now) {
    memset(p->ch, 0, sizeof p->ch);
    p->step = 0;
    p->last = now;
}

/* Cycles per waveform step. */
static uint32_t period(const gba_t *g, int n) {
    const psg_channel_t *c = &g->apu.psg.ch[n];
    if (n == SQUARE1 || n == SQUARE2) return 16u * (2048u - c->freq);
    if (n == WAVE) return 8u * (2048u - c->freq);
    uint16_t v = reg(g, 0x07C);
    uint32_t r = v & 7, s = (v >> 4) & 0xF;
    return r ? (64u * r) << s : 32u << s;
}

static bool dac_on(const gba_t *g, int n) {
    if (n == WAVE) return (reg(g, 0x070) & 0x80) != 0;
    return (reg(g, env_reg[n]) & 0xF800) != 0; /* initial volume 0 and fading down = DAC off */
}

/* Waveform steps during the next `cycles` cycles; keeps c->timer in 1..per. */
static uint32_t clock_steps(psg_channel_t *c, uint32_t cycles, uint32_t per) {
    if (c->timer > cycles) {
        c->timer -= cycles;
        return 0;
    }
    cycles -= c->timer;
    c->timer = per - cycles % per;
    return 1 + cycles / per;
}

static void noise_step(psg_channel_t *c, bool narrow) {
    uint16_t bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
    c->lfsr = (uint16_t)((c->lfsr >> 1) | bit << 14);
    if (narrow) c->lfsr = (uint16_t)((c->lfsr & ~0x40u) | bit << 6);
}

void psg_advance(gba_t *g, uint64_t now) {
    psg_t *p = &g->apu.psg;
    if (now <= p->last) return;
    uint64_t gap = now - p->last;
    uint32_t elapsed = gap > 0x40000000u ? 0x40000000u : (uint32_t)gap;
    p->last = now;
    for (int n = 0; n < 4; n++) {
        psg_channel_t *c = &p->ch[n];
        if (!c->on) continue;
        uint32_t steps = clock_steps(c, elapsed, period(g, n));
        if (!steps) continue;
        if (n == WAVE) {
            uint32_t samples = (reg(g, 0x070) & 0x20) ? 64 : 32;
            c->pos = (uint8_t)((c->pos + steps) % samples);
        } else if (n == NOISE) {
            bool narrow = (reg(g, 0x07C) & 0x08) != 0;
            for (uint32_t i = 0; i < steps; i++) noise_step(c, narrow);
        } else {
            c->pos = (uint8_t)((c->pos + steps) & 7);
        }
    }
}

/* ---- frame sequencer ---- */

static void clock_length(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    if (!(reg(g, ctl_reg[n]) & 0x4000) || c->length == 0) return;
    if (--c->length == 0) c->on = false;
}

static void clock_envelope(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    uint16_t env = reg(g, env_reg[n]);
    uint8_t step_time = (env >> 8) & 7;
    if (!step_time) return; /* frozen */
    if (c->env_timer > 1) {
        c->env_timer--;
        return;
    }
    c->env_timer = step_time;
    if ((env & 0x800) && c->volume < 15) c->volume++;
    else if (!(env & 0x800) && c->volume > 0) c->volume--;
}

static uint16_t sweep_target(const gba_t *g, uint16_t freq) {
    uint16_t s = reg(g, 0x060);
    uint16_t delta = (uint16_t)(freq >> (s & 7));
    return (s & 0x08) ? (uint16_t)(freq - delta) : (uint16_t)(freq + delta);
}

static void clock_sweep(gba_t *g) {
    psg_channel_t *c = &g->apu.psg.ch[SQUARE1];
    uint16_t s = reg(g, 0x060);
    uint8_t time = (s >> 4) & 7;
    if (!c->sweep_on) return;
    if (c->sweep_timer > 1) {
        c->sweep_timer--;
        return;
    }
    c->sweep_timer = time ? time : 8;
    if (!time) return;
    uint16_t next = sweep_target(g, c->freq);
    if (next > 2047) {
        c->on = false;
        return;
    }
    if (s & 7) {
        c->freq = next;
        set_reg(g, 0x064, (uint16_t)((reg(g, 0x064) & 0xF800) | next));
        if (sweep_target(g, next) > 2047) c->on = false; /* checked again with the new value */
    }
}

void psg_frame_step(gba_t *g) {
    psg_t *p = &g->apu.psg;
    uint8_t s = p->step;
    if (!(s & 1)) {
        for (int n = 0; n < 4; n++) clock_length(g, n);
    }
    if (s == 2 || s == 6) clock_sweep(g);
    if (s == 7) {
        clock_envelope(g, SQUARE1);
        clock_envelope(g, SQUARE2);
        clock_envelope(g, NOISE);
    }
    p->step = (uint8_t)((s + 1) & 7);
}

/* ---- registers ---- */

static void trigger(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    c->on = dac_on(g, n);
    if (c->length == 0) c->length = n == WAVE ? 256 : 64;
    if (n == WAVE) {
        c->pos = 0;
    } else {
        uint16_t env = reg(g, env_reg[n]);
        c->volume = (uint8_t)(env >> 12);
        c->env_timer = (env >> 8) & 7;
    }
    if (n == NOISE) c->lfsr = 0x7FFF;
    c->timer = period(g, n);
    if (n == SQUARE1) {
        uint16_t s = reg(g, 0x060);
        uint8_t time = (s >> 4) & 7;
        c->sweep_timer = time ? time : 8;
        c->sweep_on = time || (s & 7);
        if ((s & 7) && sweep_target(g, c->freq) > 2047) c->on = false;
    }
}

void psg_write(gba_t *g, uint32_t off, uint16_t value) {
    psg_t *p = &g->apu.psg;
    switch (off) {
    case 0x062:
    case 0x068:
    case 0x078: {
        int n = off == 0x062 ? SQUARE1 : off == 0x068 ? SQUARE2 : NOISE;
        p->ch[n].length = (uint16_t)(64 - (value & 0x3F));
        if (!dac_on(g, n)) p->ch[n].on = false;
        break;
    }
    case 0x070:
        if (!(value & 0x80)) p->ch[WAVE].on = false;
        break;
    case 0x072: p->ch[WAVE].length = (uint16_t)(256 - (value & 0xFF)); break;
    case 0x064:
    case 0x06C:
    case 0x074: {
        int n = off == 0x064 ? SQUARE1 : off == 0x06C ? SQUARE2 : WAVE;
        p->ch[n].freq = value & 0x7FF;
        set_reg(g, off, value & 0x7FFF); /* the trigger bit is not kept */
        if (value & 0x8000) trigger(g, n);
        break;
    }
    case 0x07C:
        set_reg(g, off, value & 0x7FFF);
        if (value & 0x8000) trigger(g, NOISE);
        break;
    default: break;
    }
}

/* ---- output ---- */

uint8_t psg_output(const gba_t *g, int n) {
    const psg_t *p = &g->apu.psg;
    const psg_channel_t *c = &p->ch[n];
    if (!c->on) return 0;
    if (n == SQUARE1 || n == SQUARE2) {
        uint8_t duty = (reg(g, env_reg[n]) >> 6) & 3;
        return ((duty_pattern[duty] >> (7 - c->pos)) & 1) ? c->volume : 0;
    }
    if (n == NOISE) return (c->lfsr & 1) ? 0 : c->volume;
    uint16_t l = reg(g, 0x070), h = reg(g, 0x072);
    uint32_t bank = (((l >> 6) & 1) + c->pos / 32u) & 1;
    uint32_t i = c->pos % 32u;
    uint8_t byte = p->wave[bank][i / 2];
    uint8_t s = (i & 1) ? byte & 0xF : byte >> 4;
    if (h & 0x8000) return (uint8_t)((s * 3) >> 2); /* forced 75% */
    switch ((h >> 13) & 3) {
    case 1: return s;
    case 2: return s >> 1;
    case 3: return s >> 2;
    default: return 0;
    }
}

uint8_t psg_status(const gba_t *g) {
    uint8_t s = 0;
    for (int n = 0; n < 4; n++) {
        if (g->apu.psg.ch[n].on) s |= (uint8_t)(1 << n);
    }
    return s;
}

/* The CPU reads and writes the wave bank that is not selected for playback (SOUND3CNT_L bit 6). */
static uint8_t *cpu_wave_bank(const gba_t *g) {
    uint32_t bank = ((reg(g, 0x070) >> 6) & 1) ^ 1;
    return (uint8_t *)g->apu.psg.wave[bank];
}

uint16_t psg_wave_read16(const gba_t *g, uint32_t off) {
    const uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    return (uint16_t)(b[0] | b[1] << 8);
}

void psg_wave_write16(gba_t *g, uint32_t off, uint16_t value) {
    uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    b[0] = (uint8_t)value;
    b[1] = (uint8_t)(value >> 8);
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `bash tools/dev.sh unit`
Expected: every test passes, including the 15 `psg_*` tests; no warnings.

- [x] **Step 5: Commit**

```bash
git add core/src/apu/psg.c tests/unit/test_psg.c
git commit -m "feat(apu): add square, wave and noise channels with the frame sequencer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: DMA sound refill (DMA1/DMA2 special timing)

**Files:**
- Modify: `core/src/sys/dma.h`, `core/src/sys/dma.c` (full replacement), `core/src/apu/apu.c` (`apu_on_timer_overflow`)
- Test: `tests/unit/test_dma_sound.c`

- [x] **Step 1: Write the failing tests `tests/unit/test_dma_sound.c`**

```c
#include "test.h"
#include "testutil.h"

/* Sound on, FIFO A at 100% on both sides, driven by timer 0; DMA1 reads EWRAM bytes 0, 1, 2, ... */
static gba_t *fifo_a_scene(uint16_t dma_control) {
    gba_t *g = gba_create(NULL);
    for (uint32_t i = 0; i < 64; i += 4) bus_write32(g, 0x02000000 + i, i | (i + 1) << 8 | (i + 2) << 16 | (i + 3) << 24, false);
    tu_io16(g, 0x084, 0x0080);
    tu_io16(g, 0x082, 0x0304);
    tu_io32(g, 0x0BC, 0x02000000); /* DMA1 source */
    tu_io32(g, 0x0C0, 0x040000A0); /* DMA1 destination: FIFO A */
    tu_io16(g, 0x0C6, dma_control);
    return g;
}

TEST(dma_sound_refills_four_words_when_the_fifo_runs_low) {
    gba_t *g = fifo_a_scene(0xB640); /* enable, sound timing, 32-bit, repeat, fixed destination */
    apu_on_timer_overflow(g, 0);      /* empty FIFO: 0 left → refill */
    CHECK_EQ(g->apu.fifo[0].count, 16);
    CHECK_EQ(g->dma.ch[1].src, 0x02000010);
    CHECK(bus_read16(g, 0x040000C6, false) & 0x8000); /* repeat keeps it enabled */
    apu_on_timer_overflow(g, 0);                      /* plays byte 0; 15 left → refill */
    CHECK_EQ(g->apu.fifo[0].sample, 0);
    CHECK_EQ(g->apu.fifo[0].count, 31);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 1);
    CHECK_EQ(g->apu.fifo[0].count, 30); /* 30 left: no refill */
    gba_destroy(g);
}

TEST(dma_sound_without_repeat_stops_after_one_refill) {
    gba_t *g = fifo_a_scene(0xB440);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].count, 16);
    CHECK_EQ(bus_read16(g, 0x040000C6, false) & 0x8000, 0);
    gba_destroy(g);
}

TEST(dma_sound_irq_and_destination_match) {
    gba_t *g = fifo_a_scene(0xF640); /* + IRQ */
    apu_on_timer_overflow(g, 0);
    CHECK(g->irq.if_ & IRQ_DMA1);
    gba_destroy(g);

    g = fifo_a_scene(0xB640);
    tu_io16(g, 0x0C6, 0x0000);
    tu_io32(g, 0x0C0, 0x040000A4); /* now pointed at FIFO B */
    tu_io16(g, 0x0C6, 0xB640);
    apu_on_timer_overflow(g, 0);   /* FIFO A asks: DMA1 does not answer */
    CHECK_EQ(g->apu.fifo[0].count, 0);
    gba_destroy(g);
}

TEST(dma_sound_plays_through_timer_0) {
    gba_t *g = fifo_a_scene(0xB640);
    tu_io16(g, 0x100, 0xFF00); /* overflow every 256 cycles */
    tu_io16(g, 0x102, 0x0080);
    tu_advance_to(g, g->timers.unit[0].start + 256 * 5);
    CHECK_EQ(g->apu.fifo[0].sample, 3); /* 1st overflow: empty + refill; then bytes 0, 1, 2, 3 */
    gba_destroy(g);
}
```

- [x] **Step 2: Run to verify failure**

Run: `bash tools/dev.sh unit dma_sound`
Expected: the 4 tests FAIL (FIFO count stays 0: nothing refills it).

- [x] **Step 3: Declare the request — `core/src/sys/dma.h`**

Add after `void dma_run_immediate(gba_t *g);`:

```c
/* A sound FIFO (0x040000A0 or 0x040000A4) has 16 bytes or fewer: DMA1/DMA2 with special timing and
 * that destination copy 4 words to it. */
void dma_sound_request(gba_t *g, uint32_t fifo_addr);
```

- [x] **Step 4: Replace `core/src/sys/dma.c`**

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
        /* Runs as soon as the enabling instruction finishes: the CPU cannot get further before
         * the DMA takes the bus. The 2-cycle start delay is charged in dma_run_immediate. */
        g->dma.pending |= (uint8_t)(1u << n);
        sched_add(&g->sched, EV_DMA, g->now);
    }
}

/* Address control: 0 increment, 1 decrement, 2 fixed, 3 increment (+ reload for destinations). */
static int32_t step(uint32_t mode, uint32_t unit) {
    if (mode == 1) return -(int32_t)unit;
    if (mode == 2) return 0;
    return (int32_t)unit;
}

/* Copy `count` units. The CPU is stalled for the whole transfer: bus cycles simply advance the clock. */
static void copy(gba_t *g, dma_channel_t *c, uint32_t count, bool word, int32_t src_step, int32_t dst_step) {
    for (uint32_t i = 0; i < count; i++) {
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
}

static void disable(gba_t *g, int n, uint16_t ctl) {
    uint16_t off_ctl = (uint16_t)(ctl & ~0x8000);
    g->io.raw[channel_base(n) + 10] = (uint8_t)off_ctl;
    g->io.raw[channel_base(n) + 11] = (uint8_t)(off_ctl >> 8);
}

static void transfer(gba_t *g, int n) {
    dma_channel_t *c = &g->dma.ch[n];
    uint16_t ctl = control(g, n);
    bool word = (ctl & 0x400) != 0;
    uint32_t unit = word ? 4 : 2;
    copy(g, c, c->count, word, step((ctl >> 7) & 3, unit), step((ctl >> 5) & 3, unit));

    if (ctl & 0x4000) irq_raise(g, (uint16_t)(IRQ_DMA0 << n));
    uint32_t timing = (ctl >> 12) & 3;
    if ((ctl & 0x200) && timing != TIMING_IMMEDIATE) {
        c->count = latched_count(g, n);
        if (((ctl >> 5) & 3) == 3) c->dst = reg32(g, channel_base(n) + 4) & dst_mask[n];
    } else {
        disable(g, n, ctl);
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
    if (g->dma.pending) bus_idle(g, 2); /* start delay */
    for (int n = 0; n < 4; n++) {
        if (!(g->dma.pending & (1u << n))) continue;
        g->dma.pending &= (uint8_t)~(1u << n);
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == TIMING_IMMEDIATE) transfer(g, n);
    }
}

void dma_sound_request(gba_t *g, uint32_t fifo_addr) {
    for (int n = 1; n <= 2; n++) {
        dma_channel_t *c = &g->dma.ch[n];
        uint16_t ctl = control(g, n);
        if (!(ctl & 0x8000) || ((ctl >> 12) & 3u) != TIMING_SPECIAL || c->dst != fifo_addr) continue;
        /* Always 4 words to a fixed destination, whatever CNT_L and the size bit say. */
        copy(g, c, 4, true, step((ctl >> 7) & 3, 4), 0);
        if (ctl & 0x4000) irq_raise(g, (uint16_t)(IRQ_DMA0 << n));
        if (!(ctl & 0x200)) disable(g, n, ctl);
        return;
    }
}
```

- [x] **Step 5: Ask for the refill — replace `apu_on_timer_overflow` in `core/src/apu/apu.c`**

```c
void apu_on_timer_overflow(gba_t *g, int timer) {
    if (!master_on(g)) return;
    uint16_t cnt_h = raw16(g, REG_SOUNDCNT_H);
    for (int i = 0; i < 2; i++) {
        if ((int)((cnt_h >> (10 + 4 * i)) & 1) != timer) continue;
        apu_fifo_t *f = &g->apu.fifo[i];
        if (f->count) { /* an empty FIFO keeps playing its last sample */
            f->sample = f->data[f->read];
            f->read = (uint8_t)((f->read + 1) & 31);
            f->count--;
        }
        if (f->count <= 16) dma_sound_request(g, 0x04000000u + REG_FIFO_A + 4u * (uint32_t)i);
    }
}
```

- [x] **Step 6: Run tests to verify they pass**

Run: `bash tools/dev.sh unit` then `bash tools/dev.sh test`
Expected: every unit test passes (including the 4 `dma_sound_*`), and CTest reports `100% tests passed` (the 26 approved frames are unchanged — the refactor keeps DMA timing identical).

- [x] **Step 7: Commit**

```bash
git add core tests/unit/test_dma_sound.c
git commit -m "feat(dma): refill the sound FIFOs with DMA1/DMA2 special timing" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: WAV files, audio fingerprints and test-runner options

**Files:**
- Create: `tools/common/wav.h`, `tools/common/wav.c`
- Modify: `tools/common/CMakeLists.txt`, `tools/common/frame_hash.h`, `tools/testrunner/main.c` (full replacement), `CMakeLists.txt`, `tools/approve_frame.py`

- [x] **Step 1: Create `tools/common/wav.h`**

```c
#ifndef GBA_TOOLS_WAV_H
#define GBA_TOOLS_WAV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint32_t rate, frames;
} wav_writer_t;

/* 16-bit stereo PCM. Each returns 0 on success. */
int wav_open(wav_writer_t *w, const char *path, uint32_t rate);
void wav_write(wav_writer_t *w, const int16_t *stereo, size_t frames); /* interleaved left, right */
int wav_close(wav_writer_t *w); /* writes the final sizes into the header */

#endif
```

- [x] **Step 2: Create `tools/common/wav.c`**

```c
#include "wav.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void header(uint8_t h[44], uint32_t rate, uint32_t frames) {
    uint32_t data = frames * 4;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);       /* fmt chunk size */
    put16(h + 20, 1);        /* PCM */
    put16(h + 22, 2);        /* stereo */
    put32(h + 24, rate);
    put32(h + 28, rate * 4); /* bytes per second */
    put16(h + 32, 4);        /* bytes per frame */
    put16(h + 34, 16);       /* bits per sample */
    memcpy(h + 36, "data", 4);
    put32(h + 40, data);
}

int wav_open(wav_writer_t *w, const char *path, uint32_t rate) {
    uint8_t h[44];
    w->rate = rate;
    w->frames = 0;
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    header(h, rate, 0);
    return fwrite(h, 1, sizeof h, w->f) == sizeof h ? 0 : -1;
}

void wav_write(wav_writer_t *w, const int16_t *stereo, size_t frames) {
    uint8_t buf[4096];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done > 1024 ? 1024 : frames - done;
        for (size_t i = 0; i < n * 2; i++) put16(buf + i * 2, (uint16_t)stereo[done * 2 + i]);
        fwrite(buf, 1, n * 4, w->f);
        done += n;
    }
    w->frames += (uint32_t)frames;
}

int wav_close(wav_writer_t *w) {
    uint8_t h[44];
    int ok = w->f != NULL;
    if (ok) {
        header(h, w->rate, w->frames);
        ok = fseek(w->f, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof h, w->f) == sizeof h;
        if (fclose(w->f) != 0) ok = 0;
    }
    w->f = NULL;
    return ok ? 0 : -1;
}
```

- [x] **Step 3: Build it — `tools/common/CMakeLists.txt`**

Replace the `add_library` line with:

```cmake
add_library(gbatools STATIC png.c file_util.c wav.c)
```

- [x] **Step 4: Audio fingerprint — `tools/common/frame_hash.h`**

Add `#include <stddef.h>` after `#include <stdint.h>`, and before `#endif` add:

```c
#define AUDIO_HASH_INIT 0xCBF29CE484222325ull

/* Continue a 64-bit FNV-1a over int16 samples, low byte of each first. */
static inline uint64_t audio_hash_update(uint64_t h, const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint16_t v = (uint16_t)samples[i];
        h = (h ^ (v & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (uint32_t)(v >> 8)) * 0x100000001B3ull;
    }
    return h;
}
```

- [x] **Step 5: Replace `tools/testrunner/main.c`**

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 * With --result-reg: run until the CPU spins on a branch-to-self (or <n> frames, default 600);
 * register n == 0 means PASS (jsmolka gba-tests keep the first failing test number there).
 * Otherwise with --frames: run exactly <n> frames. Otherwise: run until idle (at most 600 frames).
 * The sound of every frame is collected: --wav saves it, --audio-hash fingerprints it.
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

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>]\n"
                    "                  [--screenshot <file.png>] [--hash] [--expect-hash <hex>]\n"
                    "                  [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <hex>]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    const char *wav_path = NULL, *expect_audio = NULL;
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
    audio_out_t audio = {AUDIO_HASH_INIT, {NULL, 0, 0}, false};
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
    if (wav_path) {
        if (wav_open(&audio.wav, wav_path, GBA_AUDIO_RATE) != 0) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            goto done;
        }
        audio.writing = true;
    }

    bool exact = frames_given && result_reg < 0;
    long frame = 0;
    if (exact) {
        for (; frame < frames; frame++) run_frame(g, &audio);
    } else {
        while (frame < frames && !at_idle_loop(g)) {
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

done:
    if (audio.writing && wav_close(&audio.wav) != 0) {
        fprintf(stderr, "cannot write %s\n", wav_path);
        code = EXIT_USAGE;
    }
    gba_destroy(g);
    free(rom);
    return code;
}
```

- [x] **Step 6: CTest checks audio fingerprints when present — root `CMakeLists.txt`**

Replace the `foreach(i RANGE ${last_frame}) ... endforeach()` block inside the frame-fingerprint section with:

```cmake
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
        endforeach()
```

- [x] **Step 7: Optional audio fingerprint in `tools/approve_frame.py`**

Replace the docstring's usage line and `main()` with:

```python
"""Record an approved frame (and optional audio) fingerprint in tests/expected_frames.json.

usage: approve_frame.py <name> <rom path under tests/roms> <frames> <hash> [<audio hash>]
Only run this after looking at the screenshot (and checking the sound) and confirming it is correct.
"""
```

```python
def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    name, rom, frames, digest = sys.argv[1:5]
    audio = sys.argv[5] if len(sys.argv) == 6 else None
    if not re.fullmatch(r"[a-z0-9_]+", name):
        sys.exit("name must be lowercase letters, digits and _")
    for value in [digest] + ([audio] if audio else []):
        if not re.fullmatch(r"[0-9a-f]{16}", value):
            sys.exit("hashes must be 16 lowercase hex digits")
    data = json.loads(PATH.read_text())
    entries = [e for e in data["frames"] if e["name"] != name]
    entry = {"name": name, "rom": rom, "frames": int(frames), "hash": digest}
    if audio:
        entry["audio"] = audio
    entries.append(entry)
    data["frames"] = sorted(entries, key=lambda e: e["name"])
    PATH.write_text(json.dumps(data, indent=2) + "\n", newline="\n")
    print(f"approved {name}: {digest}" + (f", audio {audio}" if audio else ""))
```

- [x] **Step 8: Build and check**

```bash
bash tools/dev.sh configure
bash tools/dev.sh run tests/roms/tonc/first.gba --frames 60 --wav build/shots/first.wav --audio-hash --hash
python -c "import wave; w = wave.open('build/shots/first.wav'); print(w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes())"
bash tools/dev.sh test
```

Expected: `HASH 348344528f49392b` (unchanged from its approved value) and an `AUDIO <16 hex digits>` line; the WAV prints `2 32768 2 32753` (60 frames = 16,769,984 cycles → 32,753 samples); CTest `100% tests passed` (still 31 tests; the frame tests pass with no audio entries).

- [x] **Step 9: Commit**

```bash
git add CMakeLists.txt tools
git commit -m "feat(tools): save WAV files and audio fingerprints from the test runner" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `wav_probe.py` — loudness, pitch and waveform picture

**Files:**
- Create: `tools/wav_probe.py`

- [x] **Step 1: Create `tools/wav_probe.py`**

```python
#!/usr/bin/env python3
"""Measure a 16-bit stereo WAV: loudness and pitch per time window, plus a waveform picture.

usage: wav_probe.py <file.wav> [--window SECONDS] [--png out.png]
Prints the duration, then one line per window:
  <start>-<end> s  rms L <left> R <right>  peak <hz> Hz
The peak (strongest frequency of left+right, found with an FFT and parabolic interpolation)
is left out for nearly silent windows. --png draws the whole file: left channel on top, right
below, one column per time slice (min..max of the samples in it).
"""
import argparse
import cmath
import math
import struct
import sys
import wave
import zlib


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 2:
            sys.exit("expected 16-bit stereo")
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    data = struct.unpack("<%dh" % (len(raw) // 2), raw)
    return rate, data[0::2], data[1::2]


def fft(values):
    """Iterative radix-2 FFT; len(values) must be a power of two."""
    n = len(values)
    a = [complex(v) for v in values]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        half = size // 2
        twiddles = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                u = a[start + k]
                v = a[start + k + half] * twiddles[k]
                a[start + k] = u + v
                a[start + k + half] = u - v
        size <<= 1
    return a


def peak_hz(samples, rate):
    n = 1
    while n * 2 <= min(len(samples), 16384):
        n *= 2
    if n < 256:
        return None
    mean = sum(samples[:n]) / n
    windowed = [(samples[i] - mean) * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i in range(n)]
    mags = [abs(c) for c in fft(windowed)[: n // 2]]
    low = max(2, int(20 * n / rate))  # ignore everything below 20 Hz
    k = max(range(low, n // 2 - 1), key=lambda i: mags[i])
    a, b, c = mags[k - 1], mags[k], mags[k + 1]
    denom = a - 2 * b + c
    shift = 0.5 * (a - c) / denom if denom else 0.0
    shift = max(-0.5, min(0.5, shift))  # only meaningful at a true local maximum
    return (k + shift) * rate / n


def rms(values):
    return math.sqrt(sum(v * v for v in values) / len(values)) if values else 0.0


def write_png(path, width, height, pixels):
    rows = b"".join(b"\0" + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8-bit greyscale
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def draw(path, left, right):
    width, lane = 800, 100
    height = lane * 2
    pixels = bytearray(width * height)
    for ch, samples in enumerate((left, right)):
        top = ch * lane
        for x in range(width):
            part = samples[len(samples) * x // width: max(len(samples) * (x + 1) // width, len(samples) * x // width + 1)]
            if not part:
                continue
            lo = top + int((32768 - max(part)) * (lane - 1) / 65535)
            hi = top + int((32768 - min(part)) * (lane - 1) / 65535)
            for y in range(lo, hi + 1):
                pixels[y * width + x] = 255
        for x in range(width):
            pixels[(top + lane // 2) * width + x] |= 80  # the zero line
    write_png(path, width, height, pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wav")
    parser.add_argument("--window", type=float, default=0.25, help="window length in seconds (default 0.25)")
    parser.add_argument("--png", help="write a waveform picture of the whole file")
    args = parser.parse_args()

    rate, left, right = load(args.wav)
    print(f"duration {len(left) / rate:.3f} s, {rate} Hz")
    step = max(1, int(args.window * rate))
    for start in range(0, len(left) - step + 1, step):
        l_part, r_part = left[start:start + step], right[start:start + step]
        line = f"{start / rate:6.2f}-{(start + step) / rate:6.2f} s  rms L {rms(l_part):8.1f} R {rms(r_part):8.1f}"
        mono = [(a + b) / 2 for a, b in zip(l_part, r_part)]
        spread = max(mono) - min(mono)
        if spread > 400:
            hz = peak_hz(mono, rate)
            if hz:
                line += f"  peak {hz:7.1f} Hz"
        print(line)
    if args.png:
        draw(args.png, left, right)
        print(f"wrote {args.png}")


if __name__ == "__main__":
    main()
```

- [x] **Step 2: Check it on a known tone**

```bash
mkdir -p build/shots
python - <<'EOF'
import math, struct, wave
with wave.open("build/shots/sine440.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(32768)
    frames = b"".join(struct.pack("<hh", int(10000 * math.sin(2 * math.pi * 440 * i / 32768)), 0) for i in range(32768))
    w.writeframes(frames)
EOF
python tools/wav_probe.py build/shots/sine440.wav --png build/shots/sine440.png
```

Expected: `duration 1.000 s, 32768 Hz`, then 4 window lines each with `rms L   7071.x R      0.0  peak   440.0 Hz` (±0.5 Hz), and `wrote build/shots/sine440.png`. Open the PNG (Read tool): a solid white band in the top half (a dense sine), a flat line in the bottom half.

- [x] **Step 3: Commit**

```bash
git add tools/wav_probe.py
git commit -m "feat(tools): add wav_probe to measure loudness and pitch of recorded sound" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Our own sound test ROMs

**Files:**
- Create: `tools/build_test_roms.py`, `tests/own_roms/src/common.inc`, `tests/own_roms/src/snd_psg.S`, `tests/own_roms/src/snd_dma.S`, `tests/own_roms/src/snd_mix.S`, `tests/own_roms/snd_psg.gba`, `tests/own_roms/snd_dma.gba`, `tests/own_roms/snd_mix.gba` (generated)
- Modify: `.gitignore`

- [x] **Step 1: Create `tools/build_test_roms.py`**

```python
#!/usr/bin/env python3
"""Assemble our own test ROMs: tests/own_roms/src/<name>.S -> tests/own_roms/<name>.gba.

Uses clang, ld.lld and llvm-objcopy from the Android NDK (or from $CLANG_BIN_DIR), like
tools/gen_bios_stub.py. Fills the cartridge header (title, game code, maker, fixed byte and
complement checksum); the Nintendo logo area stays blank, so these ROMs are for HLE mode.
Re-run after editing a source and commit the regenerated .gba files.
"""
import glob
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "tests/own_roms/src"
OUT_DIR = ROOT / "tests/own_roms"


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


def fill_header(rom: bytearray, name: str) -> None:
    rom[0xA0:0xAC] = name.upper().replace("_", " ")[:12].encode("ascii").ljust(12, b"\0")
    rom[0xAC:0xB0] = b"ZOWN"  # game code
    rom[0xB0:0xB2] = b"00"    # maker
    rom[0xB2] = 0x96          # fixed value
    rom[0xBD] = (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF


def build(src: Path, bin_dir: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp) / "rom.elf"
        raw = Path(tmp) / "rom.bin"
        subprocess.run(
            [tool(bin_dir, "clang"), "--target=armv4t-none-eabi", "-nostdlib", "-fuse-ld=lld", "-I", str(SRC_DIR),
             "-Wl,-Ttext=0x08000000", "-Wl,--entry=_start", str(src), "-o", str(elf)],
            check=True)
        subprocess.run(
            [tool(bin_dir, "llvm-objcopy"), "-O", "binary", "--only-section=.text", str(elf), str(raw)],
            check=True)
        rom = bytearray(raw.read_bytes())
    if len(rom) < 0xC0:
        sys.exit(f"{src.name}: too small to hold a header")
    fill_header(rom, src.stem)
    rom += b"\0" * (-len(rom) % 4)
    out = OUT_DIR / (src.stem + ".gba")
    out.write_bytes(rom)
    print(f"wrote {out.relative_to(ROOT).as_posix()} ({len(rom)} bytes)")


def main() -> None:
    bin_dir = find_bin_dir()
    sources = sorted(SRC_DIR.glob("*.S"))
    if not sources:
        sys.exit(f"no sources in {SRC_DIR}")
    for src in sources:
        build(src, bin_dir)


if __name__ == "__main__":
    main()
```

- [x] **Step 2: Create `tests/own_roms/src/common.inc`**

```asm
@ Shared by our own test ROMs. The HLE boot enters at 0x08000000 in ARM state, System mode,
@ with interrupts off. r12 holds 0x04000000 (the I/O registers) after rom_start.

    .syntax unified
    .arm

@ io16 OFF, VALUE: halfword to the I/O register at 0x04000000 + OFF (uses r0, r1)
    .macro io16 off, value
    ldr r0, =\value
    ldr r1, =\off
    strh r0, [r12, r1]
    .endm

@ io32 OFF, VALUE: word to the I/O register at 0x04000000 + OFF (uses r0, r1)
    .macro io32 off, value
    ldr r0, =\value
    ldr r1, =\off
    str r0, [r12, r1]
    .endm

@ frames N: wait until N more VBlanks have started (uses r0-r2, lr)
    .macro frames n
    ldr r0, =\n
    bl wait_frames
    .endm

@ rom_start: entry branch, header space and the wait_frames helper; code continues after it.
    .macro rom_start
    .text
    .global _start
_start:
    b main
    .space 0xC0 - 4 @ cartridge header, filled in by tools/build_test_roms.py
wait_frames:
    ldr r2, =0x04000006 @ VCOUNT
1:  ldrh r1, [r2]
    cmp r1, #160
    beq 1b              @ leave the current VBlank line first
2:  ldrh r1, [r2]
    cmp r1, #160
    bne 2b              @ then wait for the next VBlank to start
    subs r0, r0, #1
    bne 1b
    bx lr
main:
    ldr r12, =0x04000000
    .endm
```

- [x] **Step 3: Create `tests/own_roms/src/snd_psg.S`**

```asm
@ snd_psg: each PSG channel plays for 60 frames (about one second).
@   0-1 s square 1 at 439.8 Hz; 1-2 s square 2 at 879.7 Hz; 2-3 s wave channel sine at 439.8 Hz;
@   3-4 s noise; 4-5 s square 1 with a slow upward sweep from 131 Hz. Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ SOUNDCNT_X: sound on
    io16 0x080, 0xFF77      @ SOUNDCNT_L: all PSG channels left + right, volume 7
    io16 0x082, 0x0002      @ SOUNDCNT_H: PSG 100%

    io16 0x060, 0x0000      @ square 1: no sweep
    io16 0x062, 0xF080      @ volume 15, envelope off, 50% duty
    io16 0x064, 0x86D6      @ trigger, frequency 1750 → 131072 / 298 = 439.8 Hz
    frames 60
    io16 0x062, 0x0000      @ DAC off: silent

    io16 0x068, 0xF080      @ square 2
    io16 0x06C, 0x876B      @ trigger, frequency 1899 → 131072 / 149 = 879.7 Hz
    frames 60
    io16 0x068, 0x0000

    io16 0x070, 0x0040      @ bank 1 selected for playback, so the CPU writes bank 0
    ldr r3, =sine
    ldr r4, =0x04000090
    ldmia r3, {r5-r8}
    stmia r4, {r5-r8}
    io16 0x070, 0x0080      @ play bank 0, DAC on
    io16 0x072, 0x2000      @ volume 100%
    io16 0x074, 0x876B      @ trigger, frequency 1899 → 65536 / 149 = 439.8 Hz
    frames 60
    io16 0x070, 0x0000

    io16 0x078, 0xF000      @ noise: volume 15
    io16 0x07C, 0x8021      @ trigger, 15-bit, r 1, s 2: a step every 256 cycles
    frames 60
    io16 0x078, 0x0000

    io16 0x060, 0x0077      @ sweep time 7, up, shift 7
    io16 0x062, 0xF080
    io16 0x064, 0x83E8      @ trigger, frequency 1000 → 125.1 Hz, rising
    frames 60
    io16 0x062, 0x0000
    io16 0x084, 0x0000      @ sound off
done:
    b done
    .ltorg
    .align 2
sine:                       @ 32 four-bit samples, round(7.5 + 7.5 sin(2 pi i / 32)), high nibble first
    .byte 0x89, 0xAC, 0xDE, 0xEF, 0xFF, 0xEE, 0xDC, 0xA9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56
```

- [x] **Step 4: Create `tests/own_roms/src/snd_dma.S`**

```asm
@ snd_dma: a 512 Hz triangle (32-byte period) at 16,384 samples per second through
@   0-1 s FIFO A (timer 0 + DMA1), then 1-2 s FIFO B (timer 1 + DMA2). Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ sound on
    io16 0x080, 0x0000      @ PSG muted

    io16 0x082, 0x0B04      @ A: 100%, left + right, timer 0, FIFO reset
    io32 0x0BC, triangle    @ DMA1 source
    io32 0x0C0, 0x040000A0  @ DMA1 destination: FIFO A
    io16 0x0C6, 0xB640      @ DMA1: enable, sound FIFO timing, 32-bit, repeat, fixed destination
    io16 0x100, 0xFC00      @ timer 0: overflow every 1024 cycles = 16,384 Hz
    io16 0x102, 0x0080
    frames 60
    io16 0x102, 0x0000
    io16 0x0C6, 0x0000

    io16 0x082, 0xF008      @ B: 100%, left + right, timer 1, FIFO reset (A off)
    io32 0x0C8, triangle    @ DMA2 source
    io32 0x0CC, 0x040000A4  @ DMA2 destination: FIFO B
    io16 0x0D2, 0xB640
    io16 0x104, 0xFC00      @ timer 1
    io16 0x106, 0x0080
    frames 60
    io16 0x106, 0x0000
    io16 0x0D2, 0x0000
    io16 0x084, 0x0000
done:
    b done
    .ltorg
    .align 2
triangle:                   @ 544 periods = 17,408 bytes: a little more than one second
    .rept 544
    .byte 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70
    .byte 0x7F, 0x6F, 0x5F, 0x4F, 0x3F, 0x2F, 0x1F, 0x0F, 0xFF, 0xEF, 0xDF, 0xCF, 0xBF, 0xAF, 0x9F, 0x8F
    .endr
```

- [x] **Step 5: Create `tests/own_roms/src/snd_mix.S`**

```asm
@ snd_mix: mixer settings, 30 frames (about half a second) each:
@   0.0 square 2 (439.8 Hz) left only; 0.5 right only; 1.0 both at PSG 50%; 1.5 both at PSG 25%;
@   2.0 FIFO A triangle (512 Hz) at 50%; 2.5 the same at 100%. Then silence.
    .include "common.inc"
    rom_start
    io16 0x084, 0x0080      @ sound on
    io16 0x082, 0x0002      @ PSG 100%
    io16 0x068, 0xF080      @ square 2: volume 15, 50% duty
    io16 0x080, 0x2077      @ square 2 on the left only, volume 7
    io16 0x06C, 0x86D6      @ trigger, frequency 1750
    frames 30
    io16 0x080, 0x0277      @ right only
    frames 30
    io16 0x080, 0x2277      @ both sides
    io16 0x082, 0x0001      @ PSG 50%
    frames 30
    io16 0x082, 0x0000      @ PSG 25%
    frames 30
    io16 0x068, 0x0000      @ square 2 off

    io16 0x082, 0x0B00      @ A: 50%, left + right, timer 0, FIFO reset
    io32 0x0BC, triangle
    io32 0x0C0, 0x040000A0
    io16 0x0C6, 0xB640
    io16 0x100, 0xFC00
    io16 0x102, 0x0080
    frames 30
    io16 0x082, 0x0304      @ A: 100% (no reset: keeps playing)
    frames 30
    io16 0x102, 0x0000
    io16 0x0C6, 0x0000
    io16 0x084, 0x0000
done:
    b done
    .ltorg
    .align 2
triangle:
    .rept 544
    .byte 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70
    .byte 0x7F, 0x6F, 0x5F, 0x4F, 0x3F, 0x2F, 0x1F, 0x0F, 0xFF, 0xEF, 0xDF, 0xCF, 0xBF, 0xAF, 0x9F, 0x8F
    .endr
```

- [x] **Step 6: Let git keep the generated ROMs — `.gitignore`**

After the `*.gba` line add:

```
!tests/own_roms/*.gba
```

- [x] **Step 7: Build and measure**

```bash
python tools/build_test_roms.py
for r in snd_psg:320 snd_dma:130 snd_mix:190; do
  n=${r%%:*}; f=${r##*:}
  bash tools/dev.sh run tests/own_roms/$n.gba --frames $f --wav build/shots/$n.wav --audio-hash --hash
  python tools/wav_probe.py build/shots/$n.wav --png build/shots/$n.png
done
```

Expected: three `wrote tests/own_roms/<name>.gba` lines (`snd_psg` about 500 bytes, `snd_dma` and `snd_mix` about 18 KB), then for each ROM a HASH/AUDIO pair and a probe table. Check these windows (they sit fully inside a segment):
- `snd_psg`: 0.25–0.75 s peak **439.8 Hz ±4.4**; 1.25–1.75 s **879.7 ±8.8**; 2.25–2.75 s **439.8 ±4.4**; 3.25–3.75 s rms > 3000 on both sides (noise, no clear peak needed); 4.25–4.75 s a peak between 124 and 150 Hz that rises from window to window (frequency 1000 grows by about 1/128 every 7/128 s: 125 Hz → about 144 Hz); the last segment ends at about 5.02 s, so the 5.00–5.25 s window still holds a little sound (rms about 3,000), then silence.
- `snd_dma`: 0.25–0.75 s and 1.25–1.75 s peak **512.0 ±5.1**, rms ≈ 18,979 on both sides; after 2.0 s rms 0.
- `snd_mix` (probe again with `--window 0.125` so each window sits inside one half-second segment): 0.25–0.375 s rms L ≈ 10,861, R 0; 0.75–0.875 s L 0, R ≈ 10,861; 1.25–1.375 s both ≈ 5,431; 1.75–1.875 s both ≈ 2,715; 2.25–2.375 s both ≈ 9,490 with peak ≈ 512; 2.75–2.875 s both ≈ 18,979. Rms values within 3%.

  `python tools/wav_probe.py build/shots/snd_mix.wav --window 0.125`

Open each PNG (Read tool) and confirm the shapes: separated blocks per segment; square waves as solid bands; the triangle as a solid band; the left-only / right-only halves of `snd_mix` visible in the two lanes.

If any check fails, stop and debug with superpowers:systematic-debugging (reproduce in a unit test first).

- [x] **Step 8: Commit**

```bash
git add .gitignore tools/build_test_roms.py tests/own_roms
git commit -m "test: add our own sound test ROMs (PSG, DMA sound, mixer)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Sound in `gbaview`

**Files:**
- Modify: `tools/viewer/main.c` (full replacement)

- [x] **Step 1: Replace `tools/viewer/main.c`**

```c
/*
 * gbaview: play a ROM in a window, with sound.
 *   gbaview <rom.gba> [--bios <file>] [--mute] [--autotest <frames> <out.png>]
 * Keys: arrows = D-pad, Z = A, X = B, A = L, S = R, Enter = Start, Backspace = Select,
 *       F12 = screenshot (gbaview-<n>.png), Esc = quit.
 * Sound plays through waveOut at 32,768 Hz and paces the emulation (about 3 frames queued).
 * With --mute, or when no audio device opens, a 59.7275 fps timer paces it instead.
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
#define AUDIO_BUFFERS 8
#define AUDIO_BUFFER_FRAMES 1024
#define AUDIO_QUEUE_TARGET 3 /* buffers (about one frame each) waiting to play */

static gba_t *gba;
static uint16_t held;
static uint32_t pixels[240 * 160]; /* 0x00RRGGBB, top-down */
static int shot_count;

static HWAVEOUT wave_out;
static WAVEHDR wave_hdr[AUDIO_BUFFERS];
static int16_t wave_data[AUDIO_BUFFERS][AUDIO_BUFFER_FRAMES * 2];
static int wave_next;

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

/* ---- sound ---- */

static bool audio_open(void) {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = GBA_AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = GBA_AUDIO_RATE * 4;
    memset(wave_hdr, 0, sizeof wave_hdr);
    return waveOutOpen(&wave_out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

static bool buffer_busy(const WAVEHDR *h) {
    return (h->dwFlags & WHDR_PREPARED) && !(h->dwFlags & WHDR_DONE);
}

/* Buffers handed to waveOut that have not finished playing. */
static int audio_queued(void) {
    int n = 0;
    for (int i = 0; i < AUDIO_BUFFERS; i++) {
        if (buffer_busy(&wave_hdr[i])) n++;
    }
    return n;
}

/* Queue the samples of the frame just emulated. */
static void audio_submit(void) {
    WAVEHDR *h = &wave_hdr[wave_next];
    while (buffer_busy(h)) Sleep(1);
    if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, h, sizeof *h);
    size_t frames = gba_audio_read(gba, wave_data[wave_next], AUDIO_BUFFER_FRAMES);
    if (frames == 0) return;
    memset(h, 0, sizeof *h);
    h->lpData = (LPSTR)wave_data[wave_next];
    h->dwBufferLength = (DWORD)(frames * 4);
    waveOutPrepareHeader(wave_out, h, sizeof *h);
    waveOutWrite(wave_out, h, sizeof *h);
    wave_next = (wave_next + 1) % AUDIO_BUFFERS;
}

static void audio_close(void) {
    waveOutReset(wave_out);
    for (int i = 0; i < AUDIO_BUFFERS; i++) {
        if (wave_hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, &wave_hdr[i], sizeof wave_hdr[i]);
    }
    waveOutClose(wave_out);
}

/* ---- window ---- */

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
    fprintf(stderr, "usage: gbaview <rom.gba> [--bios <file>] [--mute] [--autotest <frames> <out.png>]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *auto_png = NULL;
    long auto_frames = 0;
    bool mute = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--mute")) mute = true;
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

    bool sound = !mute && audio_open();
    if (!mute && !sound) fprintf(stderr, "no audio device: running muted\n");
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
        if (sound) {
            audio_submit();
            while (audio_queued() >= AUDIO_QUEUE_TARGET) Sleep(1); /* the sound card sets the pace */
        } else {
            static int16_t discard[2048 * 2];
            gba_audio_read(gba, discard, 2048);
            next += frame_time;
            double now = seconds_now();
            if (now - next > 0.25) next = now; /* far behind (e.g. window dragged): resync instead of racing */
            wait_until(next);
        }
    }
    if (sound) audio_close();
    timeEndPeriod(1);
    gba_destroy(gba);
    free(rom);
    return 0;
}
```

- [x] **Step 2: Self-tests**

```bash
bash tools/dev.sh view tests/own_roms/snd_dma.gba --autotest 120 build/shots/view_snd.png
bash tools/dev.sh view tests/roms/jsmolka/hello.gba --mute --autotest 120 build/shots/view.png
```

Expected: each opens a window for about 2 seconds. The first plays the 512 Hz tone; the sound card sets the pace, and opening the device adds a fixed start-up delay of about 0.1 s, so 120 frames print about 55–57 fps — run `--autotest 600` to see the steady rate: 58.5–60 fps (measured 600 frames in 10.12 s = 59.3 fps against 10.05 s of audio). The second is silent and prints 58–61 fps.

- [x] **Step 3: Commit**

```bash
git add tools/viewer/main.c
git commit -m "feat(tools): play sound in gbaview, paced by the audio device" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Tonc `snd1_demo`

**Files:**
- Modify: `tools/fetch_test_roms.py`

- [x] **Step 1: Add the demo to the list**

In `tools/fetch_test_roms.py`, add `"snd1_demo"` as the last entry of `TONC_DEMOS` (after `"swi_demo"`).

- [x] **Step 2: Download (the same SHA-256-pinned zip the user already approved)**

Run: `python tools/fetch_test_roms.py`
Expected: 20 `tonc/...` lines including `tonc/snd1_demo.gba`, then `test ROMs ready`. If coranac.com resets the connection, wait and rerun (it did this for a while on 2026-09-25).

- [x] **Step 3: Measure it**

```bash
bash tools/dev.sh run tests/roms/tonc/snd1_demo.gba --frames 150 --wav build/shots/snd1_demo.wav --audio-hash --hash --screenshot build/shots/snd1_demo.png
python tools/wav_probe.py build/shots/snd1_demo.wav --window 0.125 --png build/shots/snd1_demo_wave.png
```

Expected: the demo plays "SOS" at start-up with square 1 — notes D4, F4, D5 lasting 8, 8 and 32 frames, twice (about 1.6 s in all). The long D5 notes (about 0.53 s each) give windows fully inside them with a peak of **587.8 Hz ±5.9**. The short notes (0.13 s) rarely fill a window; rerun the probe with `--window 0.0625` and expect peaks near 294 Hz (D4) and 350 Hz (F4) before each D5 — close is enough there, the listening check in Task 9 confirms them. The loudness decays within each note (envelope). The waveform PNG shows 6 note blocks, the third and sixth long. The screenshot shows the demo's note-name text.

- [x] **Step 4: Commit**

```bash
git add tools/fetch_test_roms.py
git commit -m "test: fetch Tonc snd1_demo" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Listen, then approve the audio fingerprints

**Files:**
- Modify: `tests/expected_frames.json` (through `tools/approve_frame.py`)

- [x] **Step 1: The user listens**

Start each ROM for the user (run in the background with the Bash tool, one at a time) and ask what they hear:

```bash
bash tools/dev.sh view tests/own_roms/snd_psg.gba
bash tools/dev.sh view tests/own_roms/snd_dma.gba
bash tools/dev.sh view tests/own_roms/snd_mix.gba
bash tools/dev.sh view tests/roms/tonc/snd1_demo.gba
```

Expected answers: `snd_psg` — a beep, a higher beep, a softer (sine) beep, a hiss, a slowly rising tone; `snd_dma` — two identical buzzy tones; `snd_mix` — a beep in the left ear, then the right, then both quieter and quieter, then two buzzes (the second louder); `snd1_demo` — "di-di-daaah" twice (and arrow keys / Z play more notes). No crackles or stutter. If the user hears a problem, stop and debug it (superpowers:systematic-debugging) before approving anything.

- [x] **Step 2: Record the fingerprints**

For each ROM, rerun and copy both hashes:

```bash
bash tools/dev.sh run tests/own_roms/snd_psg.gba --frames 320 --hash --audio-hash
bash tools/dev.sh run tests/own_roms/snd_dma.gba --frames 130 --hash --audio-hash
bash tools/dev.sh run tests/own_roms/snd_mix.gba --frames 190 --hash --audio-hash
bash tools/dev.sh run tests/roms/tonc/snd1_demo.gba --frames 150 --hash --audio-hash
python tools/approve_frame.py own_snd_psg ../own_roms/snd_psg.gba 320 <HASH> <AUDIO>
python tools/approve_frame.py own_snd_dma ../own_roms/snd_dma.gba 130 <HASH> <AUDIO>
python tools/approve_frame.py own_snd_mix ../own_roms/snd_mix.gba 190 <HASH> <AUDIO>
python tools/approve_frame.py tonc_snd1_demo tonc/snd1_demo.gba 150 <HASH> <AUDIO>
```

(The own ROMs live in `tests/own_roms/`; their paths are written relative to `tests/roms/`, where CTest looks.)

- [x] **Step 3: Verify CTest**

```bash
bash tools/dev.sh configure
bash tools/dev.sh test
```

Expected: `100% tests passed, 0 tests failed out of 35` (unit tests, 4 jsmolka CPU ROMs, 26 approved frames, 4 new frame+audio tests).

- [x] **Step 4: Commit**

```bash
git add tests/expected_frames.json
git commit -m "test: approve sound fingerprints for our sound ROMs and Tonc snd1_demo" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Milestone 3a exit

**Files:**
- Modify: `docs/PROGRESS.md`, this plan's checkboxes, `docs/superpowers/specs/2026-09-25-milestone-3a-sound-design.md` (status line), `CLAUDE.md` (current spec/plan lines)

- [x] **Step 1: Full suite with sanitizers**

```bash
bash tools/dev.sh test 2>&1 | tail -3
bash tools/dev.sh unit 2>&1 | grep -E "runtime error|AddressSanitizer" || echo "sanitizers clean"
```

Expected: `100% tests passed` and `sanitizers clean`.

- [x] **Step 2: Update the docs and tag**

Update `docs/PROGRESS.md` (3a done; next: milestone 3b — saves + cartridge extras — starting with brainstorming), set the 3a spec's status to implemented, point `CLAUDE.md`'s "current" lines at "none yet — 3b needs brainstorming", then:

```bash
git add docs CLAUDE.md
git commit -m "docs: milestone 3a complete - sound" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-3a
```

- [x] **Step 3: Finish the branch**

Use superpowers:finishing-a-development-branch (the user chooses how to merge; the repo has no remote).

---

## Self-review notes

- **Spec coverage:** §1 split + scope → header, Task 10; §2 files → file map; §3 sampling, catch-up, sequencer event, ring buffer → Tasks 1–2; §4 PSG (squares, duty, sweep, wave banks/64-sample/volumes, noise LFSR/period, sequencer steps, trigger, DAC, SOUNDCNT_X) → Tasks 1–2; §5 DMA sound (SOUNDCNT_H, FIFOs, timer link, special DMA 4 words) → Tasks 1, 3; §6 mixer → Task 1 (`apu_mix_side`); §7 PC tools (WAV, fingerprint, runner, viewer, wav_probe, own ROMs, fetch) → Tasks 4–8; §8 testing → Tasks 1–3 unit tests, Task 6 pitch checks, Task 9 approvals; §9 done → Task 10.
- **Decisions made while planning:** square duty position is not reset on trigger (hardware behaviour; the wave position is); sweep overflow checks follow the Game Boy rules (checked at trigger and again after each update); RegisterRamReset bit 6 calls `apu_reset` (SOUNDBIAS back to 0x200); FIFOs pop on timer overflow only while sound is on, whatever the left/right enables; the own-ROM fingerprints use paths `../own_roms/<name>.gba` so the existing CMake loop needs no special case.
- **Expected values were computed while writing:** pitches 131072/(2048−f) for squares and 65536/(2048−f) for a 32-sample wave; Tonc SND_RATE for D4/F4/D5 = 1602/1673/1825; RMS figures from the mixer formula (square 15 → 15,360 peak; triangle ×256); 60 frames = 16,769,984 cycles → 32,753 samples.
