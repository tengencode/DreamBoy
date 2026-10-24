# Milestone 3c-2 — Right Timing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Make the emulator spend the hardware's cycles where the mGBA suite measures them — CPU instruction timing, the prefetch buffer, timer/IRQ delays, DMA, the H-blank flag — reaching Timing ≥ 1708/2020, DMA 1244/1244, Timer IRQ ≥ 70, and as much of Timer count-up / Misc / SIO timing as a time-boxed tuning task gets.

**Architecture:** Per-instruction timing like mGBA. `cpu_step` charges a pipeline refill after branches/exceptions and makes the next fetch non-sequential after data accesses and multiplies; the bus records which accesses an instruction made and, with WAITCNT bit 14 set, applies mGBA's prefetch-buffer estimate once per instruction. Timers start 2 cycles after the write; IRQ requests reach the CPU 5 cycles later (halt wake too), counted from the event time; immediate DMA takes the bus 3 cycles after the write while the CPU runs on; H-blank moves to cycle 1008. Every rule and number below was measured in a prototype of this plan (spec §8) before it was written.

**Tech Stack:** C11 core, CMake/CTest, Python helper scripts.

**Spec:** `docs/superpowers/specs/2026-09-25-milestone-3c2-timing-design.md` (finish line revised in §1; prototype results in §8)

---

## Conventions (same as 3c-1)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>|release>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-3c2` (already created; the spec commits are on it).
- Long Python edit scripts inside a Bash heredoc sometimes fail with "unexpected EOF": write the script to the scratchpad with the Write tool and run it.
- **Suite scores:** `bash tools/dev.sh run tests/roms/mgba/suite.gba --suite <group>`; failure log: add `--save /tmp/x.sav` after `rm -f /tmp/x.sav`, then `tr -d '\000\377' < /tmp/x.sav`. (The testrunner loads an existing `--save` file first — always delete it before a run.) **The misc group's log prints "Got <hardware> vs <ours>"** (the suite swaps the arguments there).
- **Targets:** after each task raise `tests/suite_targets.json` → `groups` to the printed scores (never lower a number). The expected score after each task is given; if a score is lower, stop and compare with the prototype rule in spec §8 before going on.
- **Cartridge costs used in tests** (WAITCNT = 0, WS0): 16-bit N = 5, S = 3; 32-bit N = 8, S = 6. IWRAM, BIOS, OAM: 1 cycle.

## File map

```
core/src/mem/bus.h / bus.c     data-access/off-cartridge bookkeeping, fetch path, bus_code_cycles, prefetch estimate, DMA takeover
core/src/cpu/cpu.c             refill after branches/IRQ entry, N fetch after data access/multiply, prefetch hook
core/src/cpu/arm.c, thumb.c    multiplies mark the bus; LDM/STM region crossing is non-sequential
core/src/sys/timer.c           2-cycle start delay; IRQ at the overflow time
core/src/bios/hle.c, hle_math.c, hle_internal.h, hle_memory.c   BIOS routine times
core/src/sys/dma.h / dma.c     3-cycle start, CPU runs on, bus takeover, end cycles, ROM source increments
core/src/sys/irq.h / irq.c     request delay (pending / visible_at), irq_raise_at, irq_changed
core/src/mem/io.c, core/src/bios/hle.c   call irq_changed on IE/IF/IME writes
core/src/gba.c                 halted CPU wakes when the request arrives
core/src/ppu/ppu.h / ppu.c     H-blank at cycle 1008, its IRQ 6 cycles earlier, event-time IRQs
tests/unit/test_cycles.c       new: every timing rule
tests/unit/test_dma.c, test_eeprom.c, test_cpu.c, test_ppu.c   old tests updated for the new timing
tests/expected_frames.json     2 audio fingerprints re-approved (Task 9)
```

---

### Task 1: CPU cycle counts

**Files:**
- Create: `tests/unit/test_cycles.c`
- Modify: `core/src/mem/bus.h`, `core/src/mem/bus.c`, `core/src/cpu/cpu.c`, `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_cycles.c`**

```c
#include "bios/hle.h"
#include "bios/hle_internal.h"
#include "test.h"
#include "testutil.h"

/* Cycles taken by the next n instructions. With WAITCNT = 0 the cartridge (WS0) costs 5 cycles for a
 * first (N) 16-bit read and 3 for a next (S) one; a 32-bit read is two of them (N+S = 8, S+S = 6).
 * IWRAM costs 1 cycle for everything. */
static uint64_t cycles_of(gba_t *g, int n) {
    uint64_t start = g->now;
    tu_steps(g, n);
    return g->now - start;
}

TEST(cycles_arm_rom_straight_line_is_sequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000, 0xE1A00000}; /* nop x3 */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);               /* the first fetch after boot */
    CHECK_EQ(cycles_of(g, 1), 6); /* S (32-bit): 3 + 3 */
    CHECK_EQ(cycles_of(g, 1), 6);
    gba_destroy(g);
}

TEST(cycles_arm_load_makes_the_next_fetch_nonsequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE5910000, 0xE1A00000, 0xE1A00000}; /* nop; ldr r0, [r1]; nop; nop */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x03000000; /* IWRAM data: 1 cycle */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1 + 1); /* S fetch + N data + I */
    CHECK_EQ(cycles_of(g, 1), 8);         /* N fetch: 5 + 3 */
    CHECK_EQ(cycles_of(g, 1), 6);
    gba_destroy(g);
}

TEST(cycles_arm_multiply_makes_the_next_fetch_nonsequential) {
    const uint32_t ops[] = {0xE1A00000, 0xE0000190, 0xE1A00000}; /* nop; mul r0, r0, r1; nop */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0xFF; /* multiplier: 1 internal cycle */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1);
    CHECK_EQ(cycles_of(g, 1), 8);
    gba_destroy(g);
}

TEST(cycles_arm_branch_refills_the_pipeline) {
    const uint32_t ops[] = {0xE1A00000, 0xEA000000, 0xE1A00000, 0xE1A00000, 0xE1A00000}; /* nop; b +8 (to [3]); ... */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 8 + 6); /* S (the branch) + N and S at the target */
    CHECK_EQ(g->cpu.r[15], 0x0800000C);
    CHECK_EQ(cycles_of(g, 1), 6);         /* the target's own fetch is sequential */
    gba_destroy(g);
}

TEST(cycles_thumb_branch_refills_the_pipeline) {
    const uint16_t ops[] = {0x46C0, 0xE000, 0x46C0, 0x46C0}; /* nop; b +4 (to [3]); nop; nop */
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 3 + 5 + 3);
    CHECK_EQ(g->cpu.r[15], 0x08000006);
    CHECK_EQ(cycles_of(g, 1), 3);
    gba_destroy(g);
}

TEST(cycles_ldm_crossing_into_the_cartridge_is_nonsequential) {
    const uint32_t prog[] = {0xE1A00000, 0xE8B10018, 0xE1A00000}; /* nop; ldmia r1!, {r3, r4}; nop */
    gba_t *g = tu_arm(prog, TU_LEN(prog));
    g->cpu.r[1] = 0x07FFFFFC; /* last OAM word, then the first cartridge word */
    tu_steps(g, 1);
    CHECK_EQ(cycles_of(g, 1), 6 + 1 + 8 + 1); /* S fetch + OAM N + ROM N (not S) + I */
    gba_destroy(g);
}

TEST(cycles_irq_entry_refills_the_pipeline) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    g->irq.ie = IRQ_VBLANK;
    g->irq.ime = 1;
    irq_raise(g, IRQ_VBLANK);
    tu_advance_to(g, g->now + 16); /* past any IRQ delay */
    uint64_t start = g->now;
    tu_steps(g, 1); /* IRQ entry, then the BIOS instruction at 0x18 (b irq_vector) */
    CHECK_EQ(g->cpu.cpsr & CPSR_MODE, MODE_IRQ);
    CHECK_EQ(g->now - start, 2 + 1 + 2); /* refill (N+S in the BIOS) + the vector's own fetch + its branch's refill */
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit cycles_
```

Expected: `cycles_arm_rom_straight_line_is_sequential` passes (straight-line code is already right); the load, multiply, branch (ARM and Thumb), LDM-crossing and IRQ-entry tests FAIL.

- [x] **Step 3: `core/src/mem/bus.h`**

In `bus_t`, after `uint32_t bios_latch; ...` add:

```c
    bool data_access;         /* the current instruction made a data read/write (not a code fetch) */
```

After `void bus_idle(gba_t *g, uint32_t cycles);` add:

```c
/* Cycles of one code fetch at addr (no access is made): for the pipeline refill. */
uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq);
```

- [x] **Step 4: `core/src/mem/bus.c`**

In each of `bus_read8`, `bus_read16`, `bus_read32`, `bus_write8`, `bus_write16`, `bus_write32`, add as the first line of the body:

```c
    g->bus.data_access = true;
```

Code fetches must not count as data accesses. In `bus_fetch32` replace

```c
    uint32_t op = bus_read32(g, addr, seq);
```

with

```c
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    uint32_t op = mem_read(g, addr, 4);
```

and in `bus_fetch16` replace

```c
    uint32_t op = bus_read16(g, addr, seq);
```

with

```c
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    uint32_t op = mem_read(g, addr, 2);
```

Before `void bus_idle(gba_t *g, uint32_t cycles) {` add:

```c
uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    return access_cycles(g, addr, size, seq);
}
```

- [x] **Step 5: `core/src/cpu/cpu.c` — `cpu_step`**

Replace

```c
    if (irq_line(g) && !(c->cpsr & CPSR_I)) cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);

    uint32_t pc = c->r[15];
    c->branched = false;
```

with

```c
    if (irq_line(g) && !(c->cpsr & CPSR_I)) {
        cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);
        g->now += bus_code_cycles(g, 0x18, 4, false) + bus_code_cycles(g, 0x1C, 4, true); /* pipeline refill */
        c->next_fetch_seq = true;
    }

    uint32_t pc = c->r[15];
    c->branched = false;
    g->bus.data_access = false;
```

and at the end of `cpu_step`, after the `if (c->cpsr & CPSR_T) { ... } else { ... }` block, add:

```c
    if (c->branched) { /* pipeline refill: N at the target, S after it; the next fetch is then sequential */
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_code_cycles(g, c->r[15], size, false) + bus_code_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false; /* the bus was used for data (or the multiplier): the next fetch is non-sequential */
    }
```

- [x] **Step 6: Multiplies — `core/src/cpu/arm.c` and `core/src/cpu/thumb.c`**

In `arm_multiply` and `arm_multiply_long`, after `bus_idle(g, cycles);` add:

```c
    g->bus.data_access = true; /* the next fetch after a multiply is non-sequential, as after a data access */
```

In `thumb_alu`, `case 0xD` (MUL), after `bus_idle(g, alu_mul_cycles(a, true));` add:

```c
        g->bus.data_access = true;
```

- [x] **Step 7: LDM/STM crossing a region — `core/src/cpu/arm.c` and `core/src/cpu/thumb.c`**

In `arm_block_transfer`, `thumb_push_pop` and `thumb_block`, every block-transfer access currently passes `seq`. Make an access that starts a new 16 MiB region non-sequential (addresses rise by 4, so a crossing means the low 24 bits are 0):

```bash
sed -i 's/bus_read32(g, addr, seq)/bus_read32(g, addr, seq \&\& (addr \& 0x00FFFFFF) != 0)/g; s/bus_write32(g, addr, \(.*\), seq)/bus_write32(g, addr, \1, seq \&\& (addr \& 0x00FFFFFF) != 0)/g' core/src/cpu/arm.c core/src/cpu/thumb.c
grep -c "0x00FFFFFF) != 0" core/src/cpu/arm.c core/src/cpu/thumb.c
```

Expected: `arm.c:3`, `thumb.c:6` (ARM: two loads + one store; Thumb: PUSH/POP three + LDMIA/STMIA two + the extra PUSH LR store). Check with `git diff core/src/cpu` that only block-transfer lines changed.

- [x] **Step 8: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all pass (the 7 new ones included).

- [x] **Step 9: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timing
bash tools/dev.sh test
```

Expected: `SUITE timing` about 1048/2020 (the prototype measured 1048 before it had the IRQ-entry refill, which this task already includes — a few points either way is fine; a large drop is not); timer-countup about 367. Raise `groups.timing` and `groups.timer-countup` to the printed values. CTest: all pass except possibly the two sound fingerprints `frame_own_snd_psg` and `frame_tonc_snd1_demo` (their audio hashes move with timing — handled in Task 9; note any other failure and stop). To keep CTest green until Task 9, run `bash tools/dev.sh test -E "snd_psg|snd1_demo"` for the full-run check in Tasks 1–8.

- [x] **Step 10: Commit**

```bash
git add tests/unit/test_cycles.c core/src/mem/bus.h core/src/mem/bus.c core/src/cpu/cpu.c core/src/cpu/arm.c core/src/cpu/thumb.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(cpu): pipeline refill after branches, N fetch after data access and multiplies" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Timer start delay

**Files:**
- Modify: `core/src/sys/timer.c`, `tests/unit/test_cycles.c`

- [x] **Step 1: Write the failing test — append to `tests/unit/test_cycles.c`**

```c
/* ---- timers ---- */

TEST(timer_starts_counting_two_cycles_after_the_write) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x100, 0x0000);
    tu_io16(g, 0x102, 0x0080); /* start, prescaler 1 */
    uint64_t t = g->now;
    g->now = t + 2;
    CHECK_EQ(timer_read_counter(g, 0), 0);
    g->now = t + 3;
    CHECK_EQ(timer_read_counter(g, 0), 1);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh unit timer_starts
```

Expected: FAIL (reads 2 at t + 2).

- [x] **Step 3: `core/src/sys/timer.c`**

In `timer_read_counter` replace

```c
    if (!free_running(t, n)) return t->counter;
```

with

```c
    if (!free_running(t, n) || g->now < t->start) return t->counter; /* not counting yet */
```

In `timer_write_control` replace

```c
    t->start = g->now;
```

with

```c
    t->start = g->now + (starting ? 2 : 0); /* a started timer counts from 2 cycles after the write */
```

- [x] **Step 4: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timing
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timer-countup
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timer-irq
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all unit tests pass; timing 1060, timer-countup 410, timer-irq 44 (prototype). Raise the targets. CTest passes.

- [x] **Step 5: Commit**

```bash
git add core/src/sys/timer.c tests/unit/test_cycles.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(core): timers start counting 2 cycles after the enabling write" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: BIOS routine times

**Files:**
- Modify: `core/src/bios/hle_internal.h`, `core/src/bios/hle_math.c`, `core/src/bios/hle.c`, `core/src/bios/hle_memory.c`, `tests/unit/test_cycles.c`

- [x] **Step 1: Write the failing tests — append to `tests/unit/test_cycles.c`**

```c
/* ---- BIOS routine times ---- */

TEST(bios_routine_times_follow_mgbas_formulas) {
    CHECK_EQ(hle_div_cycles(0x12345678, 0xFF), 284);
    CHECK_EQ(hle_div_cycles(0xFF, 0x12345678), 24);
    CHECK_EQ(hle_sqrt_cycles(0), 53);
    CHECK_EQ(hle_sqrt_cycles(0xFF), 168);
    CHECK_EQ(hle_sqrt_cycles(0x12345678), 1084);
    CHECK_EQ(hle_arctan_cycles(0xFF), 53);
    CHECK_EQ(hle_arctan2_cycles(5, 0), 11);
}

TEST(bios_division_charges_its_routine_time) {
    gba_t *g = gba_create(NULL);
    g->cpu.r[0] = 0x12345678;
    g->cpu.r[1] = 0xFF;
    uint64_t t = g->now;
    hle_swi(g, 0x06);
    CHECK_EQ(g->now - t, 284 - 8);
    gba_destroy(g);
}
```

(The suite's BIOS timing rows showed that our HLE call path already spends 8 of mGBA's measured routine cycles: need = formula − 8 for all 6 rows — spec §8.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit bios_
```

Expected: build error (the `hle_*_cycles` functions do not exist).

- [x] **Step 3: `core/src/bios/hle_internal.h`**

After the `hle_arctan` declaration add:

```c
/* Cycles the BIOS routines take (mGBA's measured formulas; the HLE call path adds the rest). */
uint32_t hle_div_cycles(int32_t num, int32_t den);
uint32_t hle_sqrt_cycles(uint32_t value);
uint32_t hle_arctan_cycles(int32_t tan);
uint32_t hle_arctan2_cycles(int32_t x, int32_t y);
```

- [x] **Step 4: `core/src/bios/hle_math.c`**

Before `static void affine_params(` add:

```c
/* ---- how long the BIOS routines take (mGBA's measured formulas) ---- */

/* The multiplier's early-termination cycles for one MUL with this multiplier value. */
static uint32_t mul_wait(int32_t r) {
    uint32_t u = (uint32_t)r;
    if ((u & 0xFFFFFF00u) == 0xFFFFFF00u || !(u & 0xFFFFFF00u)) return 1;
    if ((u & 0xFFFF0000u) == 0xFFFF0000u || !(u & 0xFFFF0000u)) return 2;
    if ((u & 0xFF000000u) == 0xFF000000u || !(u & 0xFF000000u)) return 3;
    return 4;
}

static int clz32(uint32_t v) {
    return v ? __builtin_clz(v) : 32;
}

uint32_t hle_div_cycles(int32_t num, int32_t den) {
    int loops = clz32((uint32_t)den) - clz32((uint32_t)num);
    if (loops < 1) loops = 1;
    return 4 + 13 * (uint32_t)loops + 7;
}

uint32_t hle_sqrt_cycles(uint32_t x) {
    if (!x) return 53;
    uint32_t cycles = 15, upper = x, bound = 1;
    while (bound < upper) {
        upper >>= 1;
        bound <<= 1;
        cycles += 6;
    }
    for (;;) {
        cycles += 6;
        upper = x;
        uint32_t accum = 0, lower = bound;
        for (;;) {
            cycles += 5;
            uint32_t old = lower;
            if (lower <= upper >> 1) lower <<= 1;
            if (old >= upper >> 1) break;
        }
        for (;;) {
            cycles += 8;
            accum <<= 1;
            if (upper >= lower) {
                accum++;
                upper -= lower;
            }
            if (lower == bound) break;
            lower >>= 1;
        }
        uint32_t old_bound = bound;
        bound = (bound + accum) >> 1;
        if (bound >= old_bound) break;
    }
    return cycles;
}

uint32_t hle_arctan_cycles(int32_t tan) {
    uint32_t cycles = 37 + mul_wait(mul32(tan, tan));
    int32_t a = -(mul32(tan, tan) >> 14);
    cycles += mul_wait(mul32(0xA9, a));
    int32_t b = (mul32(0xA9, a) >> 14) + 0x390;
    static const int32_t terms[6] = {0x91C, 0xFB6, 0x16AA, 0x2081, 0x3651, 0xA2F9};
    for (int i = 0; i < 6; i++) {
        cycles += mul_wait(mul32(b, a));
        b = (mul32(b, a) >> 14) + terms[i];
    }
    return cycles;
}

uint32_t hle_arctan2_cycles(int32_t x, int32_t y) {
    if (x == 0 || y == 0) return 11;
    int64_t xs = x, ys = y;
    bool y_over_x = (ys >= 0) ? ((xs >= 0 && xs >= ys) || (xs < 0 && -xs >= ys))
                              : ((xs <= 0 && -xs > -ys) || (xs > 0 && xs >= -ys));
    return hle_arctan_cycles(y_over_x ? ratio(y, x) : ratio(x, y));
}
```

(`mul32` and `ratio` already exist in this file from milestone 3c-1; add `#include <stdbool.h>` at the top if the build complains about `bool`.)

- [x] **Step 5: `core/src/bios/hle.c` — charge them**

In `hle_swi` replace

```c
    bus_idle(g, 20); /* approximate call overhead; memory SWIs also pay per-access cycles */
```

with

```c
    /* Routine time: mGBA's formulas minus the 8 cycles our call path already spends; others ~20. */
    uint32_t cost = 20;
    switch (number) {
    case 0x06: cost = hle_div_cycles((int32_t)c->r[0], (int32_t)c->r[1]) - 8; break;
    case 0x07: cost = hle_div_cycles((int32_t)c->r[1], (int32_t)c->r[0]) - 8; break;
    case 0x08: cost = hle_sqrt_cycles(c->r[0]) - 8; break;
    case 0x09: cost = hle_arctan_cycles((int32_t)c->r[0]) - 8; break;
    case 0x0A: cost = hle_arctan2_cycles((int32_t)c->r[0], (int32_t)c->r[1]) - 8; break;
    case 0x0B:
    case 0x0C: cost = 8; break; /* plus one internal cycle per unit, charged by the copy */
    default: break;
    }
    bus_idle(g, cost);
```

- [x] **Step 6: `core/src/bios/hle_memory.c` — the copy loops' internal cycle**

In `hle_cpu_set`, after the line `else bus_write16(g, to, (uint16_t)cpu_load_half(g, from), i > 0);` add:

```c
        bus_idle(g, 1); /* the BIOS loop's load has an internal cycle */
```

In `hle_cpu_fast_set`, after `bus_write32(g, dst + i * 4, bus_read32(g, from, i > 0), i > 0);` add:

```c
        bus_idle(g, 1);
```

- [x] **Step 7: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timing
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all pass; timing 1200 (prototype). Raise the target.

- [x] **Step 8: Commit**

```bash
git add core/src/bios/hle_internal.h core/src/bios/hle_math.c core/src/bios/hle.c core/src/bios/hle_memory.c tests/unit/test_cycles.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(bios): BIOS routines take mGBA's measured times" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done — checkpoint with the user.**

---

### Task 4: DMA timing and the cartridge source

**Files:**
- Modify: `core/src/sys/dma.h`, `core/src/sys/dma.c`, `core/src/mem/bus.c`, `tests/unit/test_cycles.c`, `tests/unit/test_dma.c`, `tests/unit/test_eeprom.c`

- [x] **Step 1: Write the failing tests — append to `tests/unit/test_cycles.c`**

```c
/* ---- DMA ---- */

TEST(dma_immediate_takes_the_bus_three_cycles_after_the_write) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, 0x03000000, 0xCAFEBABE, false);
    tu_io32(g, 0x0D4, 0x03000000);
    tu_io32(g, 0x0D8, 0x03000100);
    tu_io16(g, 0x0DC, 1);
    tu_io16(g, 0x0DE, 0x8400); /* enable, 32-bit, immediate */
    uint64_t t = g->now;
    CHECK_EQ(bus_peek(g, 0x03000100, 4), 0); /* the CPU may still run for 3 cycles */
    g->now = t + 3;
    CHECK_EQ(bus_read32(g, 0x03000100, false), 0xCAFEBABE); /* this access waited for the DMA */
    CHECK_EQ(g->now, t + 3 + 1 + 1 + 2 + 1); /* DMA read + write + 2, then the CPU's own read */
    gba_destroy(g);
}

TEST(dma_from_the_cartridge_always_increments) {
    const uint32_t words[] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    gba_t *g = tu_arm(words, TU_LEN(words));
    tu_io32(g, 0x0D4, 0x08000000);
    tu_io32(g, 0x0D8, 0x03000000);
    tu_io16(g, 0x0DC, 4);
    tu_io16(g, 0x0DE, 0x8500); /* enable, 32-bit, immediate, source fixed (ignored for ROM) */
    tu_advance_to(g, g->now + 3);
    CHECK_EQ(bus_peek(g, 0x0300000C, 4), 0x44444444);
    gba_destroy(g);
}
```

Update the old DMA tests for the 3-cycle start. In `tests/unit/test_dma.c` replace

```c
/* Immediate transfers start 2 cycles after the enabling write. */
static void run_pending(gba_t *g) {
    tu_advance_to(g, g->now + 2);
```

with

```c
/* Immediate transfers take the bus 3 cycles after the enabling write. */
static void run_pending(gba_t *g) {
    tu_advance_to(g, g->now + 3);
```

In `tests/unit/test_eeprom.c`, in `dma3`, replace

```c
    tu_io16(g, 0x0DE, 0x8000);
    tu_advance_to(g, g->now);
```

with

```c
    tu_io16(g, 0x0DE, 0x8000);
    tu_advance_to(g, g->now + 3); /* immediate DMA takes the bus 3 cycles after the write */
```

(`dma_immediate_runs_before_the_cpu_moves_on` stays as it is and must keep passing: its code runs from the cartridge, so the next instruction's fetch takes long enough for the DMA to take the bus before tonclib's clearing write.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit dma_
```

Expected: `dma_immediate_takes_the_bus_three_cycles_after_the_write` and `dma_from_the_cartridge_always_increments` FAIL; the updated old DMA/EEPROM tests pass (they only wait longer).

- [x] **Step 3: `core/src/sys/dma.h`**

Add `#include <stdbool.h>` above `#include <stdint.h>`. In `dma_t`, after `uint8_t pending; ...` add:

```c
    bool active;     /* a transfer is running (its own bus accesses must not start another) */
    uint64_t due;    /* when the pending immediate transfers take the bus (UINT64_MAX: none) */
```

After `void dma_run_immediate(gba_t *g);` add:

```c
/* Called before every CPU bus access: a due immediate DMA takes the bus first. */
static inline bool dma_bus_due(const dma_t *d, uint64_t now) {
    return now >= d->due && !d->active;
}
```

- [x] **Step 4: `core/src/sys/dma.c`**

`dma_reset`:

```c
void dma_reset(gba_t *g) {
    memset(&g->dma, 0, sizeof g->dma);
    g->dma.due = UINT64_MAX;
}
```

In `dma_write_control` replace

```c
        /* Runs as soon as the enabling instruction finishes: the CPU cannot get further before
         * the DMA takes the bus. The 2-cycle start delay is charged in dma_run_immediate. */
        g->dma.pending |= (uint8_t)(1u << n);
        sched_add(&g->sched, EV_DMA, g->now);
```

with

```c
        /* Takes the bus 3 cycles after the write; the CPU runs on meanwhile, and any CPU bus access
         * from then on waits for the transfer (bus.c). */
        g->dma.pending |= (uint8_t)(1u << n);
        g->dma.due = g->now + 3;
        sched_add(&g->sched, EV_DMA, g->dma.due);
```

At the end of `copy()` replace

```c
    bus_idle(g, 2);
}
```

with

```c
    if ((c->src >> 24) < 0x8 || (c->dst >> 24) < 0x8) bus_idle(g, 2); /* 2 more cycles unless ROM to ROM */
}
```

In `transfer()` replace

```c
    copy(g, c, c->count, word, step((ctl >> 7) & 3, unit), step((ctl >> 5) & 3, unit));
```

with

```c
    /* A cartridge-ROM source always increments, whatever the source control says. */
    bool rom_src = (c->src >> 24) >= 0x8 && (c->src >> 24) < 0xE;
    copy(g, c, c->count, word, rom_src ? (int32_t)unit : step((ctl >> 7) & 3, unit), step((ctl >> 5) & 3, unit));
```

Replace `dma_run_immediate` with:

```c
void dma_run_immediate(gba_t *g) {
    g->dma.due = UINT64_MAX;
    sched_cancel(&g->sched, EV_DMA);
    g->dma.active = true;
    for (int n = 0; n < 4; n++) {
        if (!(g->dma.pending & (1u << n))) continue;
        g->dma.pending &= (uint8_t)~(1u << n);
        uint16_t ctl = control(g, n);
        if ((ctl & 0x8000) && ((ctl >> 12) & 3u) == TIMING_IMMEDIATE) transfer(g, n);
    }
    g->dma.active = false;
}
```

- [x] **Step 5: `core/src/mem/bus.c` — the bus takeover**

In each of `bus_read8`, `bus_read16`, `bus_read32`, `bus_write8`, `bus_write16`, `bus_write32`, `bus_fetch32`, `bus_fetch16`, add as the very first line of the body:

```c
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
```

- [x] **Step 6: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timing
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite dma
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all unit tests pass; timing 1376, dma **1244/1244** (prototype). Raise both targets. CTest passes (all frames, the save-chip ROMs and jsmolka unchanged).

- [x] **Step 7: Commit**

```bash
git add core/src/sys/dma.h core/src/sys/dma.c core/src/mem/bus.c tests/unit/test_cycles.c tests/unit/test_dma.c tests/unit/test_eeprom.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(core): DMA takes the bus 3 cycles after the write; ROM sources always increment" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Prefetch buffer

**Files:**
- Modify: `core/src/mem/bus.h`, `core/src/mem/bus.c`, `core/src/cpu/cpu.c`, `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`, `tests/unit/test_cycles.c`

- [x] **Step 1: Write the failing test — append to `tests/unit/test_cycles.c`**

```c
/* ---- prefetch buffer ---- */

/* nop; ldr r0, [r1] (IWRAM); nop; nop from the cartridge, with WAITCNT bit 14 on or off. */
static void prefetch_run(uint16_t waitcnt, uint64_t out[3]) {
    const uint32_t ops[] = {0xE1A00000, 0xE5910000, 0xE1A00000, 0xE1A00000};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_io16(g, 0x204, waitcnt);
    g->cpu.r[1] = 0x03000000;
    tu_steps(g, 1);
    for (int i = 0; i < 3; i++) out[i] = cycles_of(g, 1);
    gba_destroy(g);
}

TEST(prefetch_buffer_hides_cartridge_waits_after_a_ram_load) {
    uint64_t off[3], on[3];
    prefetch_run(0x0000, off);
    prefetch_run(0x4000, on);
    CHECK_EQ(off[0], 8);
    CHECK_EQ(off[1], 8);
    CHECK_EQ(off[2], 6);
    CHECK_EQ(on[0], 6); /* the load's off-cartridge time lets the buffer fetch ahead: 2 cycles credited */
    CHECK_EQ(on[1], 6); /* the next fetch comes from the buffer: sequential */
    CHECK_EQ(on[2], 6);
}
```

(With the buffer on: off-cartridge time = 1 (IWRAM N) + 1 (I) = 2; S wait s = 2; one load fits (stall = s + 1 = 3); the instruction is charged max(3, 2) − 2 − 3 = −2, and the next fetch is S — mGBA's `GBAMemoryStall`.)

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh unit prefetch_buffer
```

Expected: FAIL (`on` equals `off`).

- [x] **Step 3: `core/src/mem/bus.h`**

In `bus_t`, after `bool data_access;` add:

```c
    bool prefetch_hint;       /* this instruction left the cartridge bus free (RAM/I/O access or multiply) */
    uint32_t off_rom;         /* cycles this instruction spent off the cartridge bus */
    uint32_t prefetch_end;    /* address of the last halfword the prefetch buffer holds (0: empty) */
```

After the `bus_code_cycles` declaration add:

```c
/* Prefetch buffer (WAITCNT bit 14), mGBA's estimate: called at the end of an instruction that ran from
 * the cartridge and spent cycles off it. Credits the cycles the buffer saves and makes the next fetch S. */
void bus_prefetch_stall(gba_t *g);
```

- [x] **Step 4: `core/src/mem/bus.c`**

In each data access function (`bus_read8/16/32`, `bus_write8/16/32`) replace the cycle line

```c
    g->now += access_cycles(g, addr, <size>, seq);
```

(`<size>` is 1, 2 or 4) with

```c
    uint32_t cyc = access_cycles(g, addr, <size>, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
```

Change `bus_idle` to also count internal cycles as off-cartridge time:

```c
void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
    g->bus.off_rom += cycles;
}
```

Before `uint32_t bus_code_cycles(` add:

```c
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
```

- [x] **Step 5: `core/src/cpu/cpu.c`**

After `g->bus.data_access = false;` (added in Task 1) add:

```c
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
```

Replace the end-of-step block from Task 1 with:

```c
    if (c->branched) { /* pipeline refill: N at the target, S after it; the next fetch is then sequential */
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_code_cycles(g, c->r[15], size, false) + bus_code_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0; /* a branch empties the prefetch buffer */
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false; /* the bus was used for data (or the multiplier): the next fetch is non-sequential */
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
```

- [x] **Step 6: Multiplies leave the cartridge bus free — `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`**

After each `g->bus.data_access = true;` added in Task 1 Step 6 (two in `arm.c`, one in `thumb.c`) add:

```c
    g->bus.prefetch_hint = true;
```

(with the indentation of the surrounding line).

- [x] **Step 7: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite timing
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all pass; **timing 1796/2020** (prototype; the finish line is 1708). Raise the target.

- [x] **Step 8: Commit**

```bash
git add core/src/mem/bus.h core/src/mem/bus.c core/src/cpu/cpu.c core/src/cpu/arm.c core/src/cpu/thumb.c tests/unit/test_cycles.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(core): cartridge prefetch buffer (mGBA's estimate)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Interrupt delay

**Files:**
- Modify: `core/src/sys/irq.h`, `core/src/sys/irq.c`, `core/src/mem/io.c`, `core/src/bios/hle.c`, `core/src/gba.c`, `core/src/sys/timer.c`, `core/src/ppu/ppu.c`, `tests/unit/test_cycles.c`, `tests/unit/test_cpu.c`

- [x] **Step 1: Write the failing tests — append to `tests/unit/test_cycles.c`**

```c
/* ---- interrupt delay ---- */

TEST(irq_reaches_the_cpu_five_cycles_after_the_request) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_VBLANK;
    g->irq.ime = 1;
    irq_changed(g);
    uint64_t t = g->now;
    irq_raise(g, IRQ_VBLANK);
    g->now = t + 4;
    CHECK(!irq_line(g));
    CHECK(!irq_wake(g));
    g->now = t + 5;
    CHECK(irq_line(g));
    CHECK(irq_wake(g));
    gba_destroy(g);
}

TEST(irq_from_an_event_counts_from_the_event_time) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_TIMER0;
    g->irq.ime = 1;
    irq_changed(g);
    irq_raise_at(g, IRQ_TIMER0, 1000);
    CHECK_EQ(g->irq.visible_at, 1005);
    gba_destroy(g);
}

TEST(halted_cpu_wakes_when_the_request_arrives) {
    const uint32_t ops[] = {0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->irq.ie = IRQ_VBLANK;
    irq_changed(g);
    irq_halt(g);
    irq_raise(g, IRQ_VBLANK); /* IME off: wakes, no interrupt */
    uint64_t t = g->now;
    gba_run_frame(g);
    CHECK(!g->irq.halted);
    CHECK(g->irq.visible_at == t + 5);
    gba_destroy(g);
}
```

In `tests/unit/test_cpu.c`, `cpu_takes_enabled_irq_before_next_instruction`, replace

```c
    g->irq.ime = 1;
    tu_steps(g, 1); /* enters IRQ mode, then executes the instruction at the IRQ vector */
```

with

```c
    g->irq.ime = 1;
    irq_changed(g);
    g->now += 5;    /* the request reaches the CPU 5 cycles later */
    tu_steps(g, 1); /* enters IRQ mode, then executes the instruction at the IRQ vector */
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit irq_
bash tools/dev.sh unit halted_cpu
```

Expected: build error (`irq_changed`, `irq_raise_at`, `visible_at` do not exist).

- [x] **Step 3: `core/src/sys/irq.h`**

In `irq_t`, after `bool stopped;` add:

```c
    bool pending;        /* IE & IF */
    bool ime_on;         /* IME as last seen by irq_changed */
    uint64_t visible_at; /* the CPU sees a new request from this cycle on */
```

After `void irq_raise(gba_t *g, uint16_t sources);` add:

```c
/* Like irq_raise, for event handlers: the request counts from the event's own time. */
void irq_raise_at(gba_t *g, uint16_t sources, uint64_t when);
/* IE, IF or IME changed: a new request reaches the CPU a few cycles later. */
void irq_changed(gba_t *g);
```

- [x] **Step 4: `core/src/sys/irq.c`**

Add to `irq_reset` (before its closing brace):

```c
    g->irq.pending = false;
    g->irq.ime_on = false;
    g->irq.visible_at = 0;
```

Replace `irq_raise` with:

```c
enum { IRQ_DELAY = 5 }; /* cycles from a request to the CPU seeing it (tuned on the mGBA suite, spec §8) */

/* A request is new when IE & IF becomes non-zero, or when IME switches on while one is pending. */
static void changed_at(gba_t *g, uint64_t when) {
    irq_t *q = &g->irq;
    bool pending = (q->ie & q->if_) != 0, ime_on = (q->ime & 1) != 0;
    if (pending && (!q->pending || (ime_on && !q->ime_on))) q->visible_at = when + IRQ_DELAY;
    q->pending = pending;
    q->ime_on = ime_on;
}

void irq_changed(gba_t *g) {
    changed_at(g, g->now);
}

void irq_raise(gba_t *g, uint16_t sources) {
    g->irq.if_ |= sources;
    irq_changed(g);
}

void irq_raise_at(gba_t *g, uint16_t sources, uint64_t when) {
    g->irq.if_ |= sources;
    changed_at(g, when);
}
```

Replace `irq_line` and `irq_wake` with:

```c
bool irq_line(const gba_t *g) {
    return (g->irq.ime & 1) && g->irq.pending && g->now >= g->irq.visible_at;
}

bool irq_wake(const gba_t *g) {
    return g->irq.pending && g->now >= g->irq.visible_at;
}
```

- [x] **Step 5: Keep the request state in step — `core/src/mem/io.c`, `core/src/bios/hle.c`**

`io.c`, in `write16` replace

```c
    case REG_IE: g->irq.ie = v & 0x3FFF; break;
    case REG_IF: g->irq.if_ &= (uint16_t)~v; break;
```

with

```c
    case REG_IE: g->irq.ie = v & 0x3FFF; irq_changed(g); break;
    case REG_IF: g->irq.if_ &= (uint16_t)~v; irq_changed(g); break;
```

and

```c
    case REG_IME: g->irq.ime = v & 1; break;
```

with

```c
    case REG_IME: g->irq.ime = v & 1; irq_changed(g); break;
```

In `write8`, after `g->irq.if_ &= (uint16_t)~(v << ((off & 1) * 8));` add `irq_changed(g);`.

`hle.c`, in `register_ram_reset`, after the three lines that set `g->irq.ie`, `g->irq.if_` and `g->irq.ime` to 0, add:

```c
        irq_changed(g);
```

- [x] **Step 6: Events flag at their own time — `core/src/sys/timer.c`, `core/src/ppu/ppu.c`**

`timer.c`, in `overflow()` replace

```c
    if (t->control & 0x40) irq_raise(g, (uint16_t)(IRQ_TIMER0 << n));
```

with

```c
    if (t->control & 0x40) irq_raise_at(g, (uint16_t)(IRQ_TIMER0 << n), when);
```

`ppu.c`: in `ppu_on_hblank` delete the line `(void)when;` and replace `irq_raise(g, IRQ_HBLANK);` with `irq_raise_at(g, IRQ_HBLANK, when);`; in `ppu_on_line_end` replace `irq_raise(g, IRQ_VBLANK);` with `irq_raise_at(g, IRQ_VBLANK, when);` and `irq_raise(g, IRQ_VCOUNT);` with `irq_raise_at(g, IRQ_VCOUNT, when);`.

- [x] **Step 7: A halted CPU wakes when the request arrives — `core/src/gba.c`**

In `gba_run_frame` replace

```c
                if (!irq_wake(g)) {
                    g->now = sched_next(&g->sched); /* nothing can happen before the next event */
                    break;
                }
```

with

```c
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && g->irq.visible_at < next) {
                        g->now = g->irq.visible_at; /* the request reaches the CPU: wake then */
                        continue;
                    }
                    g->now = next; /* nothing can happen before the next event */
                    break;
                }
```

(Waking earlier than the request made the count-up group hang: the woken CPU ran a few BIOS instructions, took the IRQ just before its Halt write and then halted with nothing left to wake it — spec §8.)

- [x] **Step 8: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
for g in timing timer-countup timer-irq sio-timing; do bash tools/dev.sh run tests/roms/mgba/suite.gba --suite $g; done
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all unit tests pass; timing 1796, **timer-countup 513, timer-irq 71** (prototype; mGBA: 70), sio-timing 0. No group prints TIMEOUT. Raise the targets. CTest passes.

- [x] **Step 9: Commit**

```bash
git add core/src/sys/irq.h core/src/sys/irq.c core/src/mem/io.c core/src/bios/hle.c core/src/gba.c core/src/sys/timer.c core/src/ppu/ppu.c tests/unit/test_cycles.c tests/unit/test_cpu.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(core): interrupt requests reach the CPU 5 cycles later; halt wakes with them" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 4-6) done — checkpoint with the user.**

---

### Task 7: H-blank at cycle 1008

**Files:**
- Modify: `core/src/ppu/ppu.h`, `core/src/ppu/ppu.c`, `tests/unit/test_cycles.c`, `tests/unit/test_ppu.c`

- [x] **Step 1: Write the failing test — append to `tests/unit/test_cycles.c`**

```c
/* ---- H-blank ---- */

TEST(hblank_flag_at_1008_and_its_irq_six_cycles_earlier) {
    gba_t *g = gba_create(NULL);
    g->ppu.dispstat = DISPSTAT_HBLANK_IRQ;
    g->irq.ie = IRQ_HBLANK;
    g->irq.ime = 1;
    irq_changed(g);
    tu_advance_to(g, 1007);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    tu_advance_to(g, 1008);
    CHECK(g->ppu.dispstat & DISPSTAT_HBLANK);
    CHECK(g->irq.if_ & IRQ_HBLANK);
    CHECK_EQ(g->irq.visible_at, 1002 + 5);
    gba_destroy(g);
}
```

In `tests/unit/test_ppu.c`, `ppu_hblank_flag_and_irq`, replace

```c
    advance_to(g, 959);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    advance_to(g, 960);
```

with

```c
    advance_to(g, 1007);
    CHECK(!(g->ppu.dispstat & DISPSTAT_HBLANK));
    advance_to(g, 1008);
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit hblank
bash tools/dev.sh unit ppu_hblank
```

Expected: both FAIL (the flag is set at 960).

- [x] **Step 3: `core/src/ppu/ppu.h` / `ppu.c`**

`ppu.h`: replace `#define PPU_HDRAW_CYCLES 960u` with

```c
#define PPU_HDRAW_CYCLES 1008u /* the H-blank flag, IRQ and DMA start here (mGBA/GBATEK), not at pixel 240's 960 */
```

`ppu.c`, in `ppu_on_hblank` replace the line

```c
    if (p->dispstat & DISPSTAT_HBLANK_IRQ) irq_raise_at(g, IRQ_HBLANK, when);
```

with

```c
    if (p->dispstat & DISPSTAT_HBLANK_IRQ) irq_raise_at(g, IRQ_HBLANK, when - 6); /* mGBA: 6 cycles before the flag */
```

- [x] **Step 4: Run the tests, suite, full run**

```bash
bash tools/dev.sh unit
for g in timing timer-countup timer-irq misc dma; do bash tools/dev.sh run tests/roms/mgba/suite.gba --suite $g; done
bash tools/dev.sh test -E "snd_psg|snd1_demo"
```

Expected: all unit tests pass; **misc 6/12** (prototype), the others unchanged. Raise the target. CTest passes — all 26 frame fingerprints unchanged (the picture is still drawn once per line; only the flag/IRQ/DMA moment moved). If a frame fingerprint changes, stop and look at the picture before going on.

- [x] **Step 5: Commit**

```bash
git add core/src/ppu/ppu.h core/src/ppu/ppu.c tests/unit/test_cycles.c tests/unit/test_ppu.c tests/suite_targets.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "feat(ppu): H-blank flag and DMA at cycle 1008, its IRQ 6 cycles earlier" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Tuning (time-boxed): Timer count-up, Misc, SIO timing

This task cannot list final code: it is a bounded search. Rules: work from the failure logs; one hypothesis at a time; keep a change only if it raises a group **without lowering any other group** (all 13 groups, CTest, unit tests); write a unit test for every kept rule; stop after the list below (or earlier when out of ideas) and report to the user with the numbers.

- [x] **Step 1: Record the starting point**

```bash
for g in memory io shifter carry multiply-long bios-math sio-regs timing timer-countup timer-irq dma misc sio-timing; do bash tools/dev.sh run tests/roms/mgba/suite.gba --suite $g; done
```

- [x] **Step 2: Try, in this order (each one: change → rebuild release → score all groups → keep or revert):**

1. **Prescaled timers tick on a shared clock.** mGBA aligns a prescaled timer's start to the prescaler grid (`currentTime & ~tickMask` in `GBATimerUpdateRegister` / `GBATimerWriteTMCNT_HI`). Try: `t->start` rounded down to a multiple of the prescaler period when starting and on overflow reload.
2. **The timer stop write** (`TMxCNT_H = 0` inside the IRQ handler) freezes the counter at the write; try freezing it 1 cycle earlier or later (`timer_write_control`'s `timer_read_counter` call) — the count-up log shows the frozen values 1 tick off (FFFE vs FFFF).
3. **IRQ delay when the CPU is running vs waking from Halt:** try delay 5 for a running CPU and a separate value for Halt wake (the SIO timing checks wait in `IntrWait`/Halt and want ~24 more cycles).
4. **H-blank flips** (misc log, hardware value first): the flag's falling edge — try clearing DISPSTAT's H-blank bit 1–2 cycles after the line end instead of at it.
5. **SIO timing:** with 1–3 in place, re-check; the 4 checks are each 24 cycles short at delay 5.

- [x] **Step 3: Lock and report**

Raise `tests/suite_targets.json` to the final scores. Record in `docs/PROGRESS.md` what was tried, what was kept (with its unit test), the final numbers against mGBA's (count-up 610–744, IRQ 70) and the list of checks still failing (the "possible later cycle-exact pass" list — spec §1). Tell the user the result before Task 9.

- [x] **Step 4: Commit** (kept changes + tests + targets + docs)

```bash
git add -A core tests docs
git commit -m "feat(core): timing tuning on the mGBA suite (<what was kept>)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Changed sound fingerprints and the speed guard

**Files:**
- Modify: `tests/expected_frames.json` (via `tools/approve_frame.py`)

- [x] **Step 1: See what changed**

```bash
bash tools/dev.sh test -R "snd_psg|snd1_demo" --output-on-failure
```

Expected: both fail on **audio** only (the frame hashes match): sound register writes land a few cycles later, which shifts sample boundaries.

- [x] **Step 2: Compare old and new sound (policy: re-approve only if it sounds the same)**

```bash
REPO=$PWD
git worktree add /tmp/old-3c2 main
(cd /tmp/old-3c2 && cp -r "$REPO/tests/roms" tests/ && bash tools/dev.sh configure >/dev/null && bash tools/dev.sh run tests/own_roms/snd_psg.gba --frames 320 --wav /tmp/psg_old.wav && bash tools/dev.sh run tests/roms/tonc/snd1_demo.gba --frames 150 --wav /tmp/snd1_old.wav)
bash tools/dev.sh run tests/own_roms/snd_psg.gba --frames 320 --wav /tmp/psg_new.wav
bash tools/dev.sh run tests/roms/tonc/snd1_demo.gba --frames 150 --wav /tmp/snd1_new.wav
for f in psg_old psg_new snd1_old snd1_new; do echo "== $f"; python tools/wav_probe.py /tmp/$f.wav | head -40; done
git worktree remove --force /tmp/old-3c2
```

Accept when every window's peak frequency matches within 1 Hz and its RMS within 2 % between old and new (the notes, their order and loudness are the same; only a few samples moved). If anything differs more, stop and play both to the user in `gbaview` before deciding.

- [x] **Step 3: Re-approve**

```bash
bash tools/dev.sh run tests/own_roms/snd_psg.gba --frames 320 --hash --audio-hash
bash tools/dev.sh run tests/roms/tonc/snd1_demo.gba --frames 150 --hash --audio-hash
python tools/approve_frame.py own_snd_psg ../own_roms/snd_psg.gba 320 <HASH> <AUDIO>
python tools/approve_frame.py tonc_snd1_demo tonc/snd1_demo.gba 150 <HASH> <AUDIO>
bash tools/dev.sh configure && bash tools/dev.sh test
```

(`<HASH>`/`<AUDIO>` = the printed values; check `approve_frame.py`'s usage line for the argument order and that it replaces the existing entries.) Expected: CTest 56/56. List both re-approvals (old → new audio hash, the probe comparison) in PROGRESS.md.

- [x] **Step 4: Speed guard**

```bash
bash tools/dev.sh release
bash -c 'source <(sed -n 13,29p tools/dev.sh); ROOT=$PWD; for r in tonc/m7_demo mgba/suite; do s=$(date +%s.%N); build/release/tools/testrunner/testrunner.exe tests/roms/$r.gba --frames 1200 >/dev/null; e=$(date +%s.%N); python -c "print(\"$r\", round(1200/($e-$s)), \"fps\")"; done'
```

Expected: ≥ 600 fps (10× real time); the prototype measured ~790 / ~850.

- [x] **Step 5: Commit**

```bash
git add tests/expected_frames.json docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md
git commit -m "test: re-approve 2 sound fingerprints moved by the timing changes" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 3 (Tasks 7-9) done — checkpoint with the user.**

---

### Task 10: User check and milestone 3c-2 exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test
for g in memory io shifter carry multiply-long bios-math sio-regs timing timer-countup timer-irq dma misc sio-timing; do bash tools/dev.sh run tests/roms/mgba/suite.gba --suite $g; done
```

Expected: CTest all pass (sanitizers on, no reports); the 3c-1 groups full; timing ≥ 1708, dma 1244, timer-irq ≥ 70, the rest at their Task 8 values; `tests/suite_targets.json` holds them.

- [x] **Step 2: User check in `gbaview`**

Close any viewer; start `bash tools/dev.sh view tests/roms/mgba/suite.gba` for the user (PowerShell: `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh view tests/roms/mgba/suite.gba`); Z opens a group, X goes back. They look at Timing, DMA, Timer IRQ, Timer count-up. Then, **only with the user's OK**, their game A hack (`~\Downloads\hack-1.gba`): menu, controls, sound, a battle or two (timing changes touch interrupts and DMA — sound and animation are the things to watch), save, reopen.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (3c-2 ✅ with final scores vs mGBA, what's left for a possible cycle-exact pass, the user's checks; milestone 3 complete → Next = milestone 4 brainstorming); spec status line → `Implemented — milestone 3c-2 complete <date> (tag milestone-3c2)`; parent spec §10 row 3 → 3c-2 ✅ / milestone 3 ✅; `CLAUDE.md` key files → next milestone (4, Android layer) needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (every earlier milestone: fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` on `main`, delete the branch, record the merge in PROGRESS.md.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 3c-2 complete - right timing" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-3c2
```
