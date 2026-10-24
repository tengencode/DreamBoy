# Milestone 3c-1 — Right Values — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Bring the mGBA test suite groups Memory, I/O read, Shifter, Carry, Multiply long, BIOS math and SIO register R/W to 100% (SIO timing: moved to 3c-2 by the user after Task 7), and add the keypad interrupt, the link port with no cable, and Stop mode.

**Architecture:** The test runner learns `--keys` and `--suite` (it reads the suite's score from the BG1 tile map) and CTest checks every group against `tests/suite_targets.json`. In the core: `bus_open_bus()` returns the prefetched opcode(s) by ARM/Thumb rules; a read-rules table (`io_rules.c`) decides, per I/O halfword, "readable with mask", "reads 0" or "open bus"; the HLE BIOS copies and DMA get their address rules right; ArcTan/ArcTan2 match the BIOS bit for bit; `mul_carry.c` computes the ARM7TDMI multiply carry flag; `sio.c` is the link port with nothing attached; `keypad.c` raises the keypad IRQ; Stop freezes the console and hands out silence.

**Tech Stack:** C11 core, CMake/CTest, Python fetch script.

**Spec:** `docs/superpowers/specs/2026-09-25-milestone-3c1-values-design.md`

---

## Conventions (same as before)

- Git Bash from the repo root; build/test with `bash tools/dev.sh <configure|build|unit [filter]|test|run <args>|view <args>>`.
- Unit tests: `TEST(name)` in `tests/unit/*.c` (new files are picked up automatically; re-run `bash tools/dev.sh configure` once after adding a file if the build does not see it); `CHECK_EQ` compares as `unsigned long long`. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`.
- After every task, update `docs/PROGRESS.md` and tick this plan's checkboxes (project rule in `CLAUDE.md`). The `git add` lines below list the code files; add `docs/PROGRESS.md docs/superpowers/plans/2026-09-25-milestone-3c1-values.md` to each task's commit as well.
- Work on branch `milestone-3c1` (already created; the spec commit is on it).
- A running `gbaview` locks `gbaview.exe`: close viewer windows before building. The user runs PowerShell (plain `bash` there is WSL); start `gbaview` for them with the Bash tool in the background.
- **Suite facts used below** (from the suite's source, MIT, read in the scratchpad while planning):
  - Menu order (DOWN presses before A): 0 memory, 1 io, 2 timing, 3 timer-countup, 4 timer-irq, 5 shifter, 6 carry, 7 multiply-long, 8 bios-math, 9 dma, 10 sio-regs, 11 sio-timing, 12 misc (13 = manual video tests, no score).
  - Text is drawn as BG1 tiles, screen block 1 (VRAM 0x06000800), 32 entries per row, tile number = character − 32. The score `"%4u/%-4u"` sits on row 1, columns 21–29 once a group has finished.
  - Every failing check is also written as a text line into cartridge SRAM, e.g. `BG0CNT: Got 0xFFFF vs 0xDFFF: FAIL`. To see them: `bash tools/dev.sh run tests/roms/mgba/suite.gba --suite io --save /tmp/io.sav` then `tr -d '\000' < /tmp/io.sav | head -50`.
  - Baseline scores (before this plan): memory 1459/1552, io 12/130, timing 362/2020, timer-countup 342/936, timer-irq 0/90, shifter 140/140, carry 93/93, multiply-long 52/72, bios-math 325/615, dma 1184/1244, sio-regs 7/90, sio-timing 0/4, misc 3/12.
- **Targets:** after each task, raise that task's groups in `tests/suite_targets.json` to the score the runner prints (the full count when the group is done). Never lower a number.

## File map

```
tools/fetch_test_roms.py         + mGBA suite (zip SHA-256 pinned) → tests/roms/mgba/suite.gba
tools/testrunner/main.c          + --keys, --suite, --suite-min
tests/suite_targets.json         minimum score per suite group (new)
CMakeLists.txt                   + one CTest entry per suite group
core/src/mem/bus.h / bus.c       bus_open_bus(): prefetched opcode(s); unmapped reads use it
core/src/mem/io.h / io.c         read rules applied in io_read; unmapped I/O → open bus; SIO/KEYCNT/HALTCNT routing
core/src/mem/io_rules.c          per-halfword read rules (new)
core/src/gba.c                   io_init_tables(); EV_SIO; keypad check in gba_set_keys; Stop in gba_run_frame
core/src/bios/hle_memory.c       CpuSet/CpuFastSet keep addresses; LDRH rotation for 16-bit CpuSet
core/src/sys/dma.c               DMA aligns addresses to its unit size (also in the save area)
core/src/bios/hle_math.c / hle.c / hle_internal.h   ArcTan/ArcTan2 in 32-bit wrap-around + r1/r3
core/src/cpu/mul_carry.h / .c    multiply carry flag (new)
core/src/cpu/arm.c, thumb.c      use mul_carry for flag-setting multiplies
core/src/sys/sio.h / sio.c       link port, no cable (new); core/src/sys/sched.h + EV_SIO
core/src/sys/keypad.h / keypad.c KEYCNT interrupt (new)
core/src/sys/irq.h / irq.c       Stop state; core/src/apu/apu.h / apu.c silence while stopped
tests/unit/testutil.h / .c       + tu_raw16()
tests/unit/test_open_bus.c, test_io_read.c, test_mul_carry.c, test_sio.c, test_keypad.c, test_stop.c (new)
```

---

### Task 1: Suite tooling

**Files:**
- Modify: `tools/fetch_test_roms.py`, `tools/testrunner/main.c`, `CMakeLists.txt`
- Create: `tests/suite_targets.json`

- [x] **Step 1: Fetch the suite — `tools/fetch_test_roms.py`**

In the module docstring, after the Tonc line, add:

```python
- mGBA test suite: suite-latest.zip from mGBA's storage, SHA-256 pinned; suite.gba is kept.
```

After the `TONC_DEMOS = [...]` list, add:

```python
MGBA_SUITE_URL = "https://s3.amazonaws.com/mgba/suite-latest.zip"
MGBA_SUITE_SIZE = 88220
MGBA_SUITE_SHA256 = "b802bb1a08566456f1820d09f8c5362ede30c838297fd05acdf862f1b1e790a5"  # downloaded 2026-09-25
MGBA_SUITE_ROM_SIZE = 524288
```

Before `def main():`, add:

```python
def fetch_mgba_suite():
    out = ROOT / "mgba" / "suite.gba"
    if out.exists() and out.stat().st_size == MGBA_SUITE_ROM_SIZE:
        return
    data = fetch(MGBA_SUITE_URL)
    if len(data) != MGBA_SUITE_SIZE or hashlib.sha256(data).hexdigest() != MGBA_SUITE_SHA256:
        sys.exit("suite-latest.zip: size or SHA-256 differs from the pinned file (mGBA replaced it?); not using it")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        rom = z.read("suite.gba")
    if len(rom) != MGBA_SUITE_ROM_SIZE:
        sys.exit(f"suite.gba: expected {MGBA_SUITE_ROM_SIZE} bytes, got {len(rom)}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(rom)
    print(f"mgba/suite.gba: {len(rom)} bytes")
```

In `main()`, call it right after `fetch_jsmolka()`:

```python
    fetch_jsmolka()
    fetch_mgba_suite()
```

- [x] **Step 2: Run the fetch script**

The suite was already downloaded during planning (with the user's approval) together with the zip; remove the zip so only the script's output remains, then run the script:

```bash
rm -f tests/roms/mgba/suite-latest.zip
python tools/fetch_test_roms.py
ls -l tests/roms/mgba
```

Expected: `test ROMs ready`; `tests/roms/mgba/suite.gba` is 524,288 bytes (already present, so nothing is downloaded). To prove the download path works, move the ROM away and run again — this downloads 88 KB from the pinned URL, which the user approved for this file:

```bash
mv tests/roms/mgba/suite.gba /tmp/suite.gba.bak && python tools/fetch_test_roms.py && cmp /tmp/suite.gba.bak tests/roms/mgba/suite.gba && echo same
```

Expected: `mgba/suite.gba: 524288 bytes`, `test ROMs ready`, `same`.

- [x] **Step 3: Key scripts — `tools/testrunner/main.c`**

Replace the header comment's usage lines (the block comment at the top) with:

```c
/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 *              [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]]
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
 * Exit codes: 0 pass/done, 1 fail, 2 timeout, 3 usage/load error, 77 ROM missing (CTest skip).
 */
```

After `enum { EXIT_PASS = 0, ... };` add:

```c
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
    } names[] = {{"A", GBA_KEY_A},         {"B", GBA_KEY_B},       {"SELECT", GBA_KEY_SELECT},
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
```

- [x] **Step 4: Suite mode — `tools/testrunner/main.c`**

After `keys_at`, add:

```c
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
```

After `run_frame` (it must come after it), add:

```c
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
```

In `usage()`, add a fifth line to the message (keep the other four):

```c
                    "                  [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]]\n"
```

- [x] **Step 5: Wire the options into `main()`**

Add to the variable declarations at the top of `main()`:

```c
    const char *keys_arg = NULL, *suite_arg = NULL;
    long suite_min = -1;
```

Add to the option loop, before the `else if (argv[i][0] != '-' && !rom_path)` line:

```c
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite") && i + 1 < argc) suite_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite-min") && i + 1 < argc) suite_min = strtol(argv[++i], NULL, 0);
```

After the line `if (hardware && !parse_hardware(hardware, &hw_flags)) return usage();` add:

```c
    key_script_t keys = {.count = 0};
    if (keys_arg && !parse_keys(keys_arg, &keys)) return usage();
    int suite_group = -1;
    if (suite_arg && (suite_group = suite_group_index(suite_arg)) < 0) return usage();
```

Replace this block:

```c
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
```

with:

```c
    bool exact = frames_given && result_reg < 0;
    long frame = 0;
    if (suite_group >= 0) {
        code = run_suite(g, suite_group, suite_min, &audio);
    } else {
        if (exact) {
            for (; frame < frames; frame++) {
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
```

- [x] **Step 6: Build and check the runner**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite memory; echo "exit $?"
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite memory --suite-min 1460; echo "exit $?"
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite nosuch; echo "exit $?"
bash tools/dev.sh run tests/roms/mgba/suite.gba --keys 10:DOWN,12:-,14:A,16:- --frames 60 --screenshot /tmp/io.png; echo "exit $?"
bash tools/dev.sh run tests/roms/mgba/suite.gba --keys 10:A,5:- --frames 5; echo "exit $?"
```

Expected, in order: `SUITE memory 1459/1552` exit 0; `SUITE memory 1459/1552` + `FAIL suite memory: below the target of 1460` exit 1; usage, exit 3; exit 0 and `/tmp/io.png` shows "I/O read tests 12/130" (look at it); usage, exit 3 (frames go backwards). Builds without warnings.

- [x] **Step 7: Targets file — `tests/suite_targets.json`**

```json
{
  "groups": {
    "memory": 1459,
    "io": 12,
    "timing": 362,
    "timer-countup": 342,
    "timer-irq": 0,
    "shifter": 140,
    "carry": 93,
    "multiply-long": 52,
    "bios-math": 325,
    "dma": 1184,
    "sio-regs": 7,
    "sio-timing": 0,
    "misc": 3
  },
  "totals": {
    "memory": 1552,
    "io": 130,
    "timing": 2020,
    "timer-countup": 936,
    "timer-irq": 90,
    "shifter": 140,
    "carry": 93,
    "multiply-long": 72,
    "bios-math": 615,
    "dma": 1244,
    "sio-regs": 90,
    "sio-timing": 4,
    "misc": 12
  }
}
```

`groups` = the minimum each CTest entry demands (only ever raised); `totals` = the full count, for reference.

- [x] **Step 8: CTest entries — `CMakeLists.txt`**

Inside `if(NOT ANDROID)`, after the frame-fingerprint block's `endif()`, add:

```cmake
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
```

- [x] **Step 9: Run everything**

```bash
bash tools/dev.sh configure && bash tools/dev.sh test
```

Expected: 56/56 pass (43 before + 13 suite groups). Each `mgba_suite_*` log shows its baseline score.

- [x] **Step 10: Commit**

```bash
git add tools/fetch_test_roms.py tools/testrunner/main.c tests/suite_targets.json CMakeLists.txt
git commit -m "feat(tools): run mGBA test suite groups from the test runner and CTest" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(Before committing: `docs/PROGRESS.md` gets the 3c-1 task table with row 1 ✅, a Completed entry and Next = Task 2 — see Conventions.)

---

### Task 2: Open bus from the prefetched opcode

**Files:**
- Modify: `core/src/mem/bus.h`, `core/src/mem/bus.c`, `tests/unit/test_bus.c`
- Create: `tests/unit/test_open_bus.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_open_bus.c`**

```c
#include "test.h"
#include "testutil.h"

/* Unmapped reads return what the CPU has already prefetched: in ARM state the word two instructions
 * ahead ($+8); in Thumb state it depends on the region the code runs from (GBATEK). */

TEST(open_bus_arm_is_the_word_two_ahead) {
    const uint32_t ops[] = {
        0xE5910000, /* ldr r0, [r1] */
        0xE1A00000, /* nop */
        0x12345678, /* data at $+8 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x01000000; /* unmapped */
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x12345678);
    gba_destroy(g);
}

TEST(open_bus_bios_out_of_range_byte_lane) {
    const uint32_t ops[] = {
        0xE5D10000, /* ldrb r0, [r1] */
        0xE1A00000, /* nop */
        0x12345678,
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x00010003; /* past the 16 KiB BIOS */
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x12);
    gba_destroy(g);
}

TEST(open_bus_thumb_rom_repeats_the_halfword_at_4) {
    const uint16_t ops[] = {0x6808 /* ldr r0, [r1] */, 0x1111, 0x2222};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[1] = 0x01000000;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x22222222);
    gba_destroy(g);
}

/* Thumb code copied to `mem` (offset 0 = the ldr) and run from `pc`. */
static uint32_t thumb_open_bus_at(uint8_t *(*mem)(gba_t *), uint32_t pc, const uint16_t *halves, size_t n) {
    gba_t *g = gba_create(NULL);
    uint8_t *m = mem(g);
    for (size_t i = 0; i < n; i++) {
        m[i * 2] = (uint8_t)halves[i];
        m[i * 2 + 1] = (uint8_t)(halves[i] >> 8);
    }
    g->cpu.cpsr |= CPSR_T;
    g->cpu.r[15] = pc;
    g->cpu.r[1] = 0x01000000;
    tu_steps(g, 1);
    uint32_t r0 = g->cpu.r[0];
    gba_destroy(g);
    return r0;
}

static uint8_t *iwram(gba_t *g) {
    return g->bus.iwram;
}

static uint8_t *oam(gba_t *g) {
    return g->bus.oam;
}

TEST(open_bus_thumb_iwram_aligned_and_unaligned) {
    const uint16_t aligned[] = {0x6808, 0x1111, 0x2222};          /* $ = +0: low [$+4], high [$+2] */
    CHECK_EQ(thumb_open_bus_at(iwram, 0x03000000, aligned, TU_LEN(aligned)), 0x11112222);
    const uint16_t unaligned[] = {0x0000, 0x6808, 0x2222, 0x3333}; /* $ = +2: low [$+2], high [$+4] */
    CHECK_EQ(thumb_open_bus_at(iwram, 0x03000002, unaligned, TU_LEN(unaligned)), 0x33332222);
}

TEST(open_bus_thumb_oam_uses_the_next_two_halfwords) {
    const uint16_t aligned[] = {0x6808, 0x1111, 0x2222, 0x3333}; /* $ = +0: low [$+4], high [$+6] */
    CHECK_EQ(thumb_open_bus_at(oam, 0x07000000, aligned, TU_LEN(aligned)), 0x33332222);
}

TEST(open_bus_fetch_from_unmapped_space_does_not_recurse) {
    gba_t *g = gba_create(NULL);
    g->bus.open_bus = 0xE1A00000; /* the last opcode fetched: nop */
    g->cpu.r[15] = 0x01000000;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[15], 0x01000004);
    gba_destroy(g);
}
```

In `tests/unit/test_bus.c`, delete the whole `TEST(bus_unmapped_reads_return_open_bus) { ... }` (it tested the old "current opcode" rule; the file above replaces it).

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit open_bus
```

Expected: the first five FAIL (values from the current opcode); the recursion test passes.

- [x] **Step 3: `core/src/mem/bus.h`**

After `uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size);` add:

```c
/* What unmapped reads return: the opcode(s) the CPU has prefetched (ARM: the word at the current
 * instruction + 8; Thumb: by the region the code runs from, see GBATEK "Unpredictable Things"). */
uint32_t bus_open_bus(gba_t *g);
```

- [x] **Step 4: `core/src/mem/bus.c`**

Add after `save_area()`:

```c
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
```

In `mem_read`, change the two open-bus returns:

```c
        return lane(b->open_bus, addr, size);
```

(after the `if (addr < BIOS_SIZE) { ... }` block in `case 0x0`) and

```c
    default: return lane(b->open_bus, addr, size);
```

to

```c
        return lane(bus_open_bus(g), addr, size);
```

and

```c
    default: return lane(bus_open_bus(g), addr, size);
```

`bus_fetch16/32` keep setting `b->open_bus` (now the fallback value). Update the struct comment in `bus.h`: `uint32_t open_bus; /* last opcode fetched: open-bus fallback when the pipeline is unreadable */`.

- [x] **Step 5: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all unit tests pass (the open-bus ones included).

- [x] **Step 6: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite memory
bash tools/dev.sh test
```

Expected: memory at least 1479/1552 (the 20 BIOS/unmapped out-of-bounds checks now pass). Put the printed score into `tests/suite_targets.json` → `groups.memory`. CTest all pass.

- [x] **Step 7: Commit**

```bash
git add core/src/mem/bus.h core/src/mem/bus.c tests/unit/test_open_bus.c tests/unit/test_bus.c tests/suite_targets.json
git commit -m "feat(core): open bus returns the prefetched opcode (ARM/Thumb region rules)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: I/O read rules

**Files:**
- Create: `core/src/mem/io_rules.c`, `tests/unit/test_io_read.c`
- Modify: `core/src/mem/io.h`, `core/src/mem/io.c`, `core/src/gba.c`, `tests/unit/testutil.h`, `tests/unit/testutil.c`, `tests/unit/test_io.c`, `tests/unit/test_ppu_affine.c`, `tests/unit/test_psg.c`

- [x] **Step 1: Raw-register helper — `tests/unit/testutil.h` / `testutil.c`**

In `testutil.h`, after `tu_io32`:

```c
uint16_t tu_raw16(const gba_t *g, uint32_t off);      /* stored I/O halfword, ignoring read rules */
```

In `testutil.c`, after `tu_io32`:

```c
uint16_t tu_raw16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}
```

- [x] **Step 2: Write the failing tests — `tests/unit/test_io_read.c`**

```c
#include "test.h"
#include "testutil.h"

/* A system about to run "ldrh r0, [r1]" (ARM) with r1 = addr. The word two instructions ahead is
 * 0xDEADDEAD, so an open-bus halfword read gives 0xDEAD (the suite's own trick). */
static gba_t *ldrh_rig(uint32_t addr) {
    static const uint32_t ops[] = {
        0xE1D100B0, /* ldrh r0, [r1] */
        0xE1A00000, /* nop */
        0xDEADDEAD, /* prefetched while the ldrh runs */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[1] = addr;
    return g;
}

TEST(io_write_only_registers_read_open_bus) {
    static const uint32_t offs[] = {0x010, 0x01E, 0x020, 0x03E, 0x040, 0x04C, 0x04E, 0x054,
                                    0x05E, 0x08C, 0x0A0, 0x0A8, 0x0B0, 0x0D6, 0x0E0, 0x110};
    for (size_t i = 0; i < TU_LEN(offs); i++) {
        gba_t *g = ldrh_rig(0x04000000 + offs[i]);
        tu_io16(g, offs[i], 0xFFFF);
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0xDEAD);
        gba_destroy(g);
    }
}

TEST(io_reads_above_the_register_area_are_open_bus) {
    gba_t *g = ldrh_rig(0x0400100C);
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0xDEAD);
    gba_destroy(g);
}

TEST(io_readable_registers_hide_unused_bits) {
    static const struct {
        uint32_t off;
        uint16_t expected;
    } cases[] = {{0x008, 0xDFFF}, {0x00A, 0xDFFF}, {0x00C, 0xFFFF}, {0x048, 0x3F3F}, {0x04A, 0x3F3F},
                 {0x050, 0x3FFF}, {0x052, 0x1F1F}, {0x0BA, 0xF7E0}, {0x0DE, 0xFFE0}, {0x102, 0x00C7},
                 {0x132, 0xC3FF}, {0x200, 0x3FFF}, {0x204, 0x5FFF}, {0x208, 0x0001}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, cases[i].off, 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + cases[i].off, false), cases[i].expected);
        gba_destroy(g);
    }
}

TEST(io_sound_registers_read_back_only_their_readable_bits) {
    static const struct {
        uint32_t off;
        uint16_t expected;
    } cases[] = {{0x060, 0x007F}, {0x062, 0xFFC0}, {0x064, 0x4000}, {0x068, 0xFFC0}, {0x06C, 0x4000},
                 {0x070, 0x00E0}, {0x072, 0xE000}, {0x074, 0x4000}, {0x078, 0xFF00}, {0x07C, 0x40FF},
                 {0x080, 0xFF77}, {0x082, 0x770F}};
    for (size_t i = 0; i < TU_LEN(cases); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, 0x084, 0x0080); /* sound on, or the PSG registers stay locked */
        tu_io16(g, cases[i].off, 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + cases[i].off, false), cases[i].expected);
        gba_destroy(g);
    }
}

TEST(io_unused_slots_read_zero) {
    static const uint32_t offs[] = {0x066, 0x06A, 0x06E, 0x076, 0x07A, 0x07E, 0x086, 0x08A,
                                    0x0B8, 0x0C4, 0x0D0, 0x0DC, 0x136, 0x142, 0x15A, 0x206,
                                    0x20A, 0x302};
    for (size_t i = 0; i < TU_LEN(offs); i++) {
        gba_t *g = gba_create(NULL);
        tu_io16(g, offs[i], 0xFFFF);
        CHECK_EQ(bus_read16(g, 0x04000000 + offs[i], false), 0);
        gba_destroy(g);
    }
}

TEST(io_wide_and_byte_reads_follow_each_half) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x048, 0xFFFF);
    tu_io16(g, 0x04A, 0xFFFF);
    CHECK_EQ(bus_read32(g, 0x04000048, false), 0x3F3F3F3F);
    tu_io16(g, 0x008, 0xFFFF);
    CHECK_EQ(bus_read8(g, 0x04000009, false), 0xDF);
    gba_destroy(g);
}

TEST(io_byte_writes_keep_hidden_bits) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x008, 0xFFFF);
    bus_write8(g, 0x04000008, 0x00, false);
    CHECK_EQ(tu_raw16(g, 0x008), 0xFF00);
    CHECK_EQ(bus_read16(g, 0x04000008, false), 0xDF00);
    gba_destroy(g);
}
```

- [x] **Step 3: Update the tests that read write-only registers**

`tests/unit/test_ppu_affine.c`, in `affine_matrices_default_to_identity`, replace the four `bus_read16` checks with:

```c
    CHECK_EQ(tu_raw16(g, 0x020), 0x100); /* BG2PA (write-only: checked in the register store) */
    CHECK_EQ(tu_raw16(g, 0x026), 0x100); /* BG2PD */
    CHECK_EQ(tu_raw16(g, 0x030), 0x100); /* BG3PA */
    CHECK_EQ(tu_raw16(g, 0x036), 0x100); /* BG3PD */
```

(add `#include "testutil.h"` at the top if the file does not include it yet).

`tests/unit/test_psg.c`, in `psg_sweep_raises_frequency_until_overflow`, replace

```c
    CHECK_EQ(bus_read16(g, 0x04000064, false) & 0x7FF, 1250);
```

with

```c
    CHECK_EQ(tu_raw16(g, 0x064) & 0x7FF, 1250); /* the frequency is write-only; the store follows the sweep */
```

`tests/unit/test_io.c`, replace `io_unhandled_registers_are_stored` with (BG0CNT hides bit 13 now, BG2CNT does not):

```c
TEST(io_unhandled_registers_are_stored) {
    gba_t *g = gba_create(NULL);
    bus_write16(g, 0x0400000C, 0x1234, false);
    CHECK_EQ(bus_read16(g, 0x0400000C, false), 0x1234);
    bus_write8(g, 0x0400000D, 0xAB, false);
    CHECK_EQ(bus_read16(g, 0x0400000C, false), 0xAB34);
    gba_destroy(g);
}
```

- [x] **Step 4: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit io_
```

Expected: the new `io_*` read tests FAIL (0xFFFF read back); the edited older tests pass.

- [x] **Step 5: `core/src/mem/io.h`**

Before `void io_reset(gba_t *g);` add:

```c
/* How the CPU sees an I/O halfword when it reads it. */
typedef enum { IO_OPEN = 0, IO_READ, IO_ZERO } io_kind_t;

typedef struct {
    uint8_t kind;  /* io_kind_t */
    uint16_t mask; /* IO_READ: the readable bits */
} io_rule_t;

void io_init_tables(void);       /* builds the read-rule table; call once before any system runs */
io_rule_t io_rule(uint32_t off); /* rule for the halfword at I/O offset off (0x000-0x3FE) */
```

- [x] **Step 6: `core/src/mem/io_rules.c`**

```c
#include "mem/io.h"

#include <stddef.h>

/* Readable I/O halfwords and their readable bits, and the unused slots that read 0. Everything not
 * listed reads open bus (write-only registers and holes). Sources: GBATEK's I/O map, mGBA's register
 * tables and the expectations of the mGBA suite's "I/O read" and "SIO register R/W" groups. */
typedef struct {
    uint16_t first, last;
    uint8_t kind;
    uint16_t mask;
} io_range_t;

#define READ(first, last, mask) {first, last, IO_READ, mask}
#define ZERO(first, last) {first, last, IO_ZERO, 0}

static const io_range_t RANGES[] = {
    READ(0x000, 0x000, 0xFFF7), /* DISPCNT (bit 3: CGB mode, only the BIOS sets it) */
    READ(0x002, 0x002, 0x0001), /* green swap */
    READ(0x004, 0x004, 0xFF3F), /* DISPSTAT */
    READ(0x006, 0x006, 0x00FF), /* VCOUNT */
    READ(0x008, 0x00A, 0xDFFF), /* BG0CNT, BG1CNT: bit 13 (wrap) only exists for BG2/BG3 */
    READ(0x00C, 0x00E, 0xFFFF), /* BG2CNT, BG3CNT */
    READ(0x048, 0x04A, 0x3F3F), /* WININ, WINOUT */
    READ(0x050, 0x050, 0x3FFF), /* BLDCNT */
    READ(0x052, 0x052, 0x1F1F), /* BLDALPHA */
    READ(0x060, 0x060, 0x007F), /* SOUND1CNT_L */
    READ(0x062, 0x062, 0xFFC0), /* SOUND1CNT_H: the length is write-only */
    READ(0x064, 0x064, 0x4000), /* SOUND1CNT_X: only the length flag */
    ZERO(0x066, 0x066),
    READ(0x068, 0x068, 0xFFC0), /* SOUND2CNT_L */
    ZERO(0x06A, 0x06A),
    READ(0x06C, 0x06C, 0x4000), /* SOUND2CNT_H */
    ZERO(0x06E, 0x06E),
    READ(0x070, 0x070, 0x00E0), /* SOUND3CNT_L */
    READ(0x072, 0x072, 0xE000), /* SOUND3CNT_H */
    READ(0x074, 0x074, 0x4000), /* SOUND3CNT_X */
    ZERO(0x076, 0x076),
    READ(0x078, 0x078, 0xFF00), /* SOUND4CNT_L */
    ZERO(0x07A, 0x07A),
    READ(0x07C, 0x07C, 0x40FF), /* SOUND4CNT_H */
    ZERO(0x07E, 0x07E),
    READ(0x080, 0x080, 0xFF77), /* SOUNDCNT_L */
    READ(0x082, 0x082, 0x770F), /* SOUNDCNT_H */
    READ(0x084, 0x084, 0x008F), /* SOUNDCNT_X: master enable + channel status */
    ZERO(0x086, 0x086),
    READ(0x088, 0x088, 0xC3FE), /* SOUNDBIAS */
    ZERO(0x08A, 0x08A),
    READ(0x090, 0x09E, 0xFFFF), /* wave RAM */
    ZERO(0x0B8, 0x0B8),         /* DMA0CNT_L */
    READ(0x0BA, 0x0BA, 0xF7E0), /* DMA0CNT_H (bit 11, game pak DRQ, is DMA3 only) */
    ZERO(0x0C4, 0x0C4),
    READ(0x0C6, 0x0C6, 0xF7E0),
    ZERO(0x0D0, 0x0D0),
    READ(0x0D2, 0x0D2, 0xF7E0),
    ZERO(0x0DC, 0x0DC),
    READ(0x0DE, 0x0DE, 0xFFE0), /* DMA3CNT_H */
    READ(0x100, 0x100, 0xFFFF), /* TM0CNT_L: the counter */
    READ(0x102, 0x102, 0x00C7), /* TM0CNT_H */
    READ(0x104, 0x104, 0xFFFF),
    READ(0x106, 0x106, 0x00C7),
    READ(0x108, 0x108, 0xFFFF),
    READ(0x10A, 0x10A, 0x00C7),
    READ(0x10C, 0x10C, 0xFFFF),
    READ(0x10E, 0x10E, 0x00C7),
    READ(0x120, 0x12A, 0xFFFF), /* link port data and SIOCNT (the link port applies its own rules) */
    READ(0x130, 0x130, 0x03FF), /* KEYINPUT */
    READ(0x132, 0x132, 0xC3FF), /* KEYCNT */
    READ(0x134, 0x134, 0xFFFF), /* RCNT */
    ZERO(0x136, 0x136),
    READ(0x140, 0x140, 0xFFFF), /* JOYCNT */
    ZERO(0x142, 0x142),
    READ(0x150, 0x158, 0xFFFF), /* JOY_RECV, JOY_TRANS, JOYSTAT */
    ZERO(0x15A, 0x15A),
    READ(0x200, 0x202, 0x3FFF), /* IE, IF */
    READ(0x204, 0x204, 0x5FFF), /* WAITCNT (bit 15, cartridge type, reads 0 on a GBA cartridge) */
    ZERO(0x206, 0x206),
    READ(0x208, 0x208, 0x0001), /* IME */
    ZERO(0x20A, 0x20A),
    READ(0x300, 0x300, 0x0001), /* POSTFLG; HALTCNT (high byte) is write-only and reads 0 */
    ZERO(0x302, 0x302),
};

static io_rule_t table[IO_SIZE / 2];

void io_init_tables(void) {
    for (size_t r = 0; r < sizeof RANGES / sizeof RANGES[0]; r++)
        for (uint32_t off = RANGES[r].first; off <= RANGES[r].last; off += 2)
            table[off >> 1] = (io_rule_t){RANGES[r].kind, RANGES[r].mask};
}

io_rule_t io_rule(uint32_t off) {
    return table[(off & (IO_SIZE - 1)) >> 1];
}
```

(Entries not listed stay `{IO_OPEN, 0}` from zero-initialisation. The table is rebuilt identically on every call, so calling `io_init_tables` more than once is harmless.)

- [x] **Step 7: Apply the rules — `core/src/mem/io.c`**

Replace the whole `io_read` function with:

```c
/* One halfword as the CPU sees it: the register's readable bits, 0, or open bus. read16 (the stored
 * value) stays the source for byte-write merging, so hidden bits survive byte writes. */
static uint16_t read_visible16(gba_t *g, uint32_t off) {
    io_rule_t rule = io_rule(off);
    if (rule.kind == IO_ZERO) return 0;
    if (rule.kind == IO_OPEN) return (uint16_t)(bus_open_bus(g) >> ((off & 2) * 8));
    return read16(g, off) & rule.mask;
}

uint32_t io_read(gba_t *g, uint32_t addr, unsigned size) {
    uint32_t off = addr & 0x00FFFFFF;
    if (off >= IO_SIZE) { /* nothing above the registers: open bus */
        uint32_t open = bus_open_bus(g);
        if (size == 4) return open;
        if (size == 2) return (open >> ((off & 2) * 8)) & 0xFFFF;
        return (open >> ((off & 3) * 8)) & 0xFF;
    }
    switch (size) {
    case 1: return (read_visible16(g, off & ~1u) >> ((off & 1) * 8)) & 0xFF;
    case 2: return read_visible16(g, off);
    default: return read_visible16(g, off) | (uint32_t)read_visible16(g, off + 2) << 16;
    }
}
```

- [x] **Step 8: Build the table at start-up — `core/src/gba.c`**

In `gba_create`, after `cpu_init_tables();` add:

```c
    io_init_tables();
```

- [x] **Step 9: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all unit tests pass.

- [x] **Step 10: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite io
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite sio-regs
bash tools/dev.sh test
```

Expected: `SUITE io 130/130` (if not, look at the failing lines with `--save /tmp/io.sav` and fix the table entry). sio-regs improves (the three zero slots). Set `groups.io` to 130 and `groups.sio-regs` to its printed score. CTest all pass — including all 26 approved frames and the sound fingerprints (reads the games do of DISPSTAT, VCOUNT etc. are unchanged). If a frame fingerprint changes, stop: find which register read changed before going on.

- [x] **Step 11: Commit**

```bash
git add core/src/mem/io.h core/src/mem/io.c core/src/mem/io_rules.c core/src/gba.c tests/unit/test_io_read.c tests/unit/testutil.h tests/unit/testutil.c tests/unit/test_io.c tests/unit/test_ppu_affine.c tests/unit/test_psg.c tests/suite_targets.json
git commit -m "feat(core): I/O read rules - readable masks, write-only registers read open bus, unused slots read 0" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done — checkpoint with the user.**

---

### Task 4: Memory group — BIOS copies and DMA addresses

**Files:**
- Modify: `core/src/bios/hle_memory.c`, `core/src/sys/dma.c`, `tests/unit/test_hle_memory.c`, `tests/unit/test_dma.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_hle_memory.c`**

Add `#include "testutil.h"` after `#include "test.h"`, then add at the end:

```c
TEST(hle_cpu_set_halfwords_from_an_odd_address_rotate) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC + 1; /* the BIOS uses LDRH: a misaligned read rotates the halfword */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 2;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00DE00BE);
    gba_destroy(g);
}

TEST(hle_cpu_set_words_from_an_odd_address_read_the_aligned_word) {
    gba_t *g = gba_create(NULL);
    bus_write32(g, SRC, 0xDEADBEEF, false);
    g->cpu.r[0] = SRC + 1; /* LDMIA: the bus ignores the low bits */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0xDEADBEEF);
    gba_destroy(g);
}

TEST(hle_copies_keep_exact_save_area_addresses) {
    const char *const markers[] = {"SRAM_V"};
    gba_t *g = tu_rom_markers(markers, 1, 0x400);
    const char text[] = "Game";
    for (uint32_t i = 0; i < 4; i++) bus_write8(g, 0x0E000000 + i, (uint8_t)text[i], false);

    g->cpu.r[0] = 0x0E000001; /* 32-bit CpuSet: the byte at +1, repeated */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x61616161);

    g->cpu.r[0] = 0x0E000001; /* 16-bit CpuSet: 'a' and 'e', each rotated into the low byte */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 2;
    hle_cpu_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x00650061);

    g->cpu.r[0] = 0x0E000003; /* CpuFastSet */
    g->cpu.r[1] = DST;
    g->cpu.r[2] = 1;
    hle_cpu_fast_set(g);
    CHECK_EQ(bus_read32(g, DST, false), 0x65656565);

    bus_write32(g, SRC, 0x11223344, false); /* 32-bit CpuSet into SRAM + 0x11: one byte, by address */
    g->cpu.r[0] = SRC;
    g->cpu.r[1] = 0x0E000011;
    g->cpu.r[2] = 1 | (1u << 26);
    hle_cpu_set(g);
    CHECK_EQ(bus_read8(g, 0x0E000011, false), 0x33);
    gba_destroy(g);
}
```

In `tests/unit/test_dma.c`, add at the end:

```c
TEST(dma_aligns_save_area_addresses) {
    const char *const markers[] = {"SRAM_V"};
    gba_t *g = tu_rom_markers(markers, 1, 0x400);
    const char text[] = "Game";
    for (uint32_t i = 0; i < 4; i++) bus_write8(g, 0x0E000000 + i, (uint8_t)text[i], false);

    dma_setup(g, 3, 0x0E000001, 0x02000000, 1, 0x8000); /* 16-bit from SRAM + 1: reads SRAM + 0 */
    run_pending(g);
    CHECK_EQ(bus_read16(g, 0x02000000, false), 0x4747);

    bus_write32(g, 0x02000010, 0xD8D8D8D8, false);
    dma_setup(g, 3, 0x02000010, 0x0E000022, 1, 0x8400); /* 32-bit into SRAM + 0x22: writes SRAM + 0x20 */
    run_pending(g);
    CHECK_EQ(bus_read8(g, 0x0E000020, false), 0xD8);
    CHECK_EQ(bus_read8(g, 0x0E000022, false), 0xFF);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit hle_c
bash tools/dev.sh unit dma_aligns
```

Expected: the four new tests FAIL.

- [x] **Step 3: `core/src/bios/hle_memory.c`**

Add `#include "cpu/cpu_mem.h"` after the existing includes. Replace `hle_cpu_set` and `hle_cpu_fast_set` with:

```c
/* r0 = source, r1 = destination, r2 = count (bits 0-20) | fill (bit 24) | 32-bit units (bit 26).
 * Like the BIOS: 32-bit units move with LDMIA/STMIA (the bus aligns, the save area keeps exact
 * addresses), 16-bit units with LDRH/STRH (a misaligned read rotates the halfword). */
void hle_cpu_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], ctrl = c->r[2];
    uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1, words = (ctrl >> 26) & 1;
    if (!source_allowed(src)) return;
    uint32_t unit = words ? 4 : 2;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * unit;
        uint32_t to = dst + i * unit;
        if (words) bus_write32(g, to, bus_read32(g, from, i > 0), i > 0);
        else bus_write16(g, to, (uint16_t)cpu_load_half(g, from), i > 0);
    }
}

/* Like CpuSet with 32-bit units (LDMIA/STMIA), but the count is rounded up to a multiple of 8 words. */
void hle_cpu_fast_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], ctrl = c->r[2];
    uint32_t count = ((ctrl & 0x1FFFFF) + 7) & ~7u;
    bool fill = (ctrl >> 24) & 1;
    if (!source_allowed(src)) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * 4;
        bus_write32(g, dst + i * 4, bus_read32(g, from, i > 0), i > 0);
    }
}
```

- [x] **Step 4: `core/src/sys/dma.c` — align in `copy()`**

Replace `copy()` with:

```c
/* Copy `count` units. The CPU is stalled for the whole transfer: bus cycles simply advance the clock.
 * DMA addresses are aligned to the unit size, even in the save area (where the CPU's are not). */
static void copy(gba_t *g, dma_channel_t *c, uint32_t count, bool word, int32_t src_step, int32_t dst_step) {
    uint32_t align = word ? ~3u : ~1u;
    for (uint32_t i = 0; i < count; i++) {
        bool seq = i > 0;
        uint32_t src = c->src & align, dst = c->dst & align;
        uint32_t value;
        if (src >= 0x02000000) {
            value = word ? bus_read32(g, src, seq) : bus_read16(g, src, seq) * 0x00010001u;
            g->dma.latch = value;
        } else {
            bus_idle(g, 1);
            value = g->dma.latch; /* the BIOS and unmapped space are not readable by DMA */
        }
        if (word) bus_write32(g, dst, value, seq);
        else bus_write16(g, dst, (uint16_t)(value >> ((c->dst & 2) * 8)), seq);
        c->src += (uint32_t)src_step;
        c->dst += (uint32_t)dst_step;
    }
    bus_idle(g, 2);
}
```

- [x] **Step 5: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all pass.

- [x] **Step 6: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite memory
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite dma
bash tools/dev.sh test
```

Expected: `SUITE memory 1552/1552`. If not, read the remaining failures with `--save /tmp/mem.sav` (`tr -d '\000' < /tmp/mem.sav`), write a unit test for the cause and fix it. The dma group must not drop below its target (it may rise). Set `groups.memory` to 1552 (and raise `dma` if it rose). CTest all pass.

- [x] **Step 7: Commit**

```bash
git add core/src/bios/hle_memory.c core/src/sys/dma.c tests/unit/test_hle_memory.c tests/unit/test_dma.c tests/suite_targets.json
git commit -m "fix(core): BIOS copies keep odd addresses like LDRH/LDMIA; DMA aligns save-area addresses" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: BIOS maths — ArcTan and ArcTan2 bit for bit

**Files:**
- Modify: `core/src/bios/hle_math.c`, `core/src/bios/hle_internal.h`, `core/src/bios/hle.c`, `tests/unit/test_hle_math.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_hle_math.c`**

Replace `hle_arctan_matches_bios_polynomial` and `hle_arctan2_quadrants` with:

```c
TEST(hle_arctan_matches_bios_polynomial) {
    CHECK_EQ(hle_arctan(0, NULL, NULL), 0);
    CHECK_EQ(hle_arctan(0x1000, NULL, NULL), 2555);
    CHECK_EQ(hle_arctan(0x2000, NULL, NULL), 4836);
    CHECK_EQ(hle_arctan(0x4000, NULL, NULL), 0x2000);
    CHECK_EQ(hle_arctan(-0x4000, NULL, NULL), -0x2000);
}

/* Rows of the mGBA suite's BIOS math table (measured on hardware): r0, r1 and r3 after SWI 0x09. */
TEST(hle_arctan_wraps_like_the_bios_and_leaves_r1_r3) {
    static const struct {
        int32_t in;
        uint32_t r0, r1, r3;
    } rows[] = {
        {0x0000, 0x0000, 0x00000000, 0xA2F9},     {0x3FFF, 0x1FFF, 0xFFFFC002, 0x8001},
        {0x7FFF, 0x16D8, 0xFFFF0004, 0x22DB6},    {0x8000, 0x16A2, 0xFFFF0000, 0x22D45},
        {0xC000, 0xFFFFC360, 0x0001C000, 0x10480}, {0xFFFF, 0xFFFFA2FE, 0x00000008, 0xA2FF},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        int32_t r1, r3;
        CHECK_EQ((uint32_t)hle_arctan(rows[i].in, &r1, &r3), rows[i].r0);
        CHECK_EQ((uint32_t)r1, rows[i].r1);
        CHECK_EQ((uint32_t)r3, rows[i].r3);
    }
}

TEST(hle_arctan2_quadrants) {
    int32_t r1 = 0;
    CHECK_EQ(hle_arctan2(0x100, 0x100, &r1), 0x2000);
    CHECK_EQ(hle_arctan2(-0x100, 0x100, &r1), 0x6000);
    CHECK_EQ(hle_arctan2(-0x100, -0x100, &r1), 0xA000);
    CHECK_EQ(hle_arctan2(0x100, -0x100, &r1), 0xE000);
    CHECK_EQ(hle_arctan2(5, 0, &r1), 0);
    CHECK_EQ(hle_arctan2(-5, 0, &r1), 0x8000);
    CHECK_EQ(hle_arctan2(0, 5, &r1), 0x4000);
    CHECK_EQ(hle_arctan2(0, -5, &r1), 0xC000);
    CHECK_EQ(hle_arctan2(0x1000, 0x800, &r1), 0x12E4);
}

/* Suite rows for SWI 0x0A (x, y → r0, r1; r3 is always 0x170): r1 is the inner ArcTan's `a`, or
 * y unchanged when x or y is 0. */
TEST(hle_arctan2_swi_leaves_r1_r3_like_the_bios) {
    static const struct {
        uint32_t x, y, r0, r1;
    } rows[] = {
        {0x00004000, 0x00000000, 0x0000, 0x00000000}, {0xFFFFFFFF, 0x00000001, 0x6000, 0xFFFFC000},
        {0x00000000, 0x00004000, 0x4000, 0x00004000}, {0xFFFF8000, 0x00004000, 0x6D1C, 0xFFFFF000},
        {0x00004000, 0xFFFF0000, 0xC9FC, 0xFFFFFC00}, {0xFFFFC000, 0xFFFF0000, 0xB605, 0xFFFFFC00},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        gba_t *g = gba_create(NULL);
        g->cpu.r[0] = rows[i].x;
        g->cpu.r[1] = rows[i].y;
        hle_swi(g, 0x0A);
        CHECK_EQ(g->cpu.r[0], rows[i].r0);
        CHECK_EQ(g->cpu.r[1], rows[i].r1);
        CHECK_EQ(g->cpu.r[3], 0x170);
        gba_destroy(g);
    }
}
```

Check the top of the file includes `bios/hle.h` (for `hle_swi`); add `#include "bios/hle.h"` if missing.

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh unit hle_arctan
```

Expected: build error (the functions take fewer arguments) — that is the failing state.

- [x] **Step 3: `core/src/bios/hle_internal.h`**

Replace the two declarations with:

```c
/* tan in 1.14 fixed point (full 32-bit input, 32-bit wrap-around like the BIOS); result -0x4000..0x4000
 * as a sign-extended 16-bit value. r1/r3 (may be NULL) receive what the BIOS leaves in those registers. */
int32_t hle_arctan(int32_t tan, int32_t *r1, int32_t *r3);
/* Result 0..0xFFFF for a full circle; *r1 receives the inner ArcTan's r1 (untouched when x or y is 0). */
uint32_t hle_arctan2(int32_t x, int32_t y, int32_t *r1);
```

- [x] **Step 4: `core/src/bios/hle_math.c`**

Replace `hle_arctan` and `hle_arctan2` with:

```c
/* A 32-bit multiply that wraps like the ARM's MUL (no signed-overflow undefined behaviour). */
static int32_t mul32(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

/* The BIOS's polynomial approximation of atan, in its 32-bit arithmetic. */
int32_t hle_arctan(int32_t tan, int32_t *r1, int32_t *r3) {
    int32_t a = -(mul32(tan, tan) >> 14);
    int32_t b = (mul32(0xA9, a) >> 14) + 0x390;
    b = (mul32(b, a) >> 14) + 0x91C;
    b = (mul32(b, a) >> 14) + 0xFB6;
    b = (mul32(b, a) >> 14) + 0x16AA;
    b = (mul32(b, a) >> 14) + 0x2081;
    b = (mul32(b, a) >> 14) + 0x3651;
    b = (mul32(b, a) >> 14) + 0xA2F9;
    if (r1) *r1 = a;
    if (r3) *r3 = b;
    return (int16_t)(mul32(tan, b) >> 16);
}

/* (n << 14) / d in 32 bits, as the BIOS computes the ratio. */
static int32_t ratio(int32_t n, int32_t d) {
    int32_t shifted = (int32_t)((uint32_t)n << 14);
    if (d == -1) return (int32_t)(0u - (uint32_t)shifted); /* INT32_MIN / -1 wraps */
    return shifted / d;
}

/* Reduce to an octant where |ratio| <= 1, then add that octant's base angle. */
uint32_t hle_arctan2(int32_t x, int32_t y, int32_t *r1) {
    if (y == 0) return x >= 0 ? 0 : 0x8000;
    if (x == 0) return y >= 0 ? 0x4000 : 0xC000;
    int64_t xs = x, ys = y; /* -x cannot overflow in 64 bits */
    int32_t r;
    if (ys >= 0) {
        if (xs >= 0 && xs >= ys) r = hle_arctan(ratio(y, x), r1, NULL);
        else if (xs < 0 && -xs >= ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x8000;
        else r = 0x4000 - hle_arctan(ratio(x, y), r1, NULL);
    } else {
        if (xs <= 0 && -xs > -ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x8000;
        else if (xs > 0 && xs >= -ys) r = hle_arctan(ratio(y, x), r1, NULL) + 0x10000;
        else r = 0xC000 - hle_arctan(ratio(x, y), r1, NULL);
    }
    return (uint32_t)r & 0xFFFF;
}
```

- [x] **Step 5: `core/src/bios/hle.c` — the SWI cases**

Replace

```c
    case 0x09: c->r[0] = (uint32_t)hle_arctan((int16_t)c->r[0]); break;
    case 0x0A: c->r[0] = hle_arctan2((int16_t)c->r[0], (int16_t)c->r[1]); break;
```

with

```c
    case 0x09: {
        int32_t r1, r3;
        c->r[0] = (uint32_t)hle_arctan((int32_t)c->r[0], &r1, &r3);
        c->r[1] = (uint32_t)r1;
        c->r[3] = (uint32_t)r3;
        break;
    }
    case 0x0A: {
        int32_t r1 = (int32_t)c->r[1];
        c->r[0] = hle_arctan2((int32_t)c->r[0], (int32_t)c->r[1], &r1);
        c->r[1] = (uint32_t)r1;
        c->r[3] = 0x170;
        break;
    }
```

- [x] **Step 6: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all pass (grep the tree for other callers of `hle_arctan`/`hle_arctan2` first: `git grep -n "hle_arctan"` — only `hle_math.c`, `hle.c`, `hle_internal.h` and the test should appear).

- [x] **Step 7: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite bios-math
bash tools/dev.sh test
```

Expected: `SUITE bios-math 615/615`; set `groups.bios-math` to 615. CTest all pass — **including `frame_tonc_swi_demo`** (its ArcTan2 inputs are small, so the picture should not change). If `frame_tonc_swi_demo` fails: render it (`--frames 60 --screenshot /tmp/swi.png`), compare with the Tonc guide's picture, and show the user before re-approving it (spec §7).

- [x] **Step 8: Commit**

```bash
git add core/src/bios/hle_math.c core/src/bios/hle_internal.h core/src/bios/hle.c tests/unit/test_hle_math.c tests/suite_targets.json
git commit -m "fix(core): ArcTan/ArcTan2 in the BIOS's 32-bit arithmetic, with its r1/r3 values" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Multiply carry flag

**Files:**
- Create: `core/src/cpu/mul_carry.h`, `core/src/cpu/mul_carry.c`, `tests/unit/test_mul_carry.c`
- Modify: `core/src/cpu/arm.c`, `core/src/cpu/thumb.c`, `tests/unit/test_arm_mul.c`

Background: on ARM7TDMI a flag-setting multiply sets C to a value produced inside its Booth multiplier (V is unchanged). The rule was worked out in 2023 (zaydlang, https://bmchtech.github.io/post/multiply/, code under the zlib licence). While planning, the reference code gave exact products for 2,000,000 random inputs and exact flags for all 72 suite checks, and the carry-only version below agreed with it on 3,000,000 random inputs. The expected carries in the tests come from that reference.

- [x] **Step 1: Write the failing tests — `tests/unit/test_mul_carry.c`**

```c
#include "cpu/mul_carry.h"
#include "test.h"
#include "testutil.h"

TEST(mul_carry_reference_values) {
    CHECK_EQ(mul_carry(MUL_SHORT, 0xFFFFFFFF, 6, 0), 0);
    CHECK_EQ(mul_carry(MUL_SHORT, 3, 0x40000000, 0), 0);
    CHECK_EQ(mul_carry(MUL_SHORT, 3, 0x80000000, 0), 1);
    CHECK_EQ(mul_carry(MUL_SHORT, 0x12345678, 0x9ABCDEF0, 0x0F0F0F0F), 1);  /* MLA */
    CHECK_EQ(mul_carry(MUL_LONG_SIGNED, 0xFFFFFFFF, 0x7FFFFFFF, 0), 1);       /* suite: -1 * $7F */
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0xFFFFFFFF, 0xFFFFFFFF, 0), 1);     /* suite: -1 * -1 */
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0, 0, 0), 0);
    CHECK_EQ(mul_carry(MUL_LONG_UNSIGNED, 0xDEADBEEF, 0x80000001, 0x00000001FFFFFFFFull), 1); /* UMLAL */
    CHECK_EQ(mul_carry(MUL_LONG_SIGNED, 0x80000000, 0x80000000, 0xFFFFFFFF00000000ull), 0);  /* SMLAL */
}

TEST(arm_long_multiplies_set_the_booth_carry) {
    const uint32_t ops[] = {
        0xE328F000, /* msr   cpsr_f, #0 */
        0xE0D87994, /* smulls r7, r8, r4, r9 */
        0xE328F000, /* msr   cpsr_f, #0 */
        0xE0965994, /* umulls r5, r6, r4, r9 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[4] = 0xFFFFFFFF;
    g->cpu.r[9] = 0x7FFFFFFF;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[8], 0xFFFFFFFFu);
    CHECK_EQ(g->cpu.r[7], 0x80000001u);
    CHECK(g->cpu.cpsr & CPSR_C);
    g->cpu.r[9] = 0xFFFFFFFF;
    tu_steps(g, 2);
    CHECK_EQ(g->cpu.r[6], 0xFFFFFFFEu);
    CHECK_EQ(g->cpu.r[5], 0x00000001u);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(arm_mla_carry_uses_the_accumulator_before_the_write) {
    const uint32_t ops[] = {
        0xE0302190, /* mlas r0, r0, r1, r2 (rd = rm: operands are read first) */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x12345678;
    g->cpu.r[1] = 0x9ABCDEF0;
    g->cpu.r[2] = 0x0F0F0F0F;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x333C2F8Fu);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}

TEST(thumb_mul_sets_the_booth_carry) {
    const uint16_t ops[] = {0x4348 /* muls r0, r1 */};
    gba_t *g = tu_thumb(ops, TU_LEN(ops));
    g->cpu.r[0] = 0x80000000; /* Rd: the multiplier */
    g->cpu.r[1] = 3;
    tu_steps(g, 1);
    CHECK_EQ(g->cpu.r[0], 0x80000000u);
    CHECK(g->cpu.cpsr & CPSR_C);
    gba_destroy(g);
}
```

In `tests/unit/test_arm_mul.c`, replace `arm_multiply_flags_keep_carry_and_overflow` with:

```c
TEST(arm_multiply_flags_keep_overflow_and_set_the_booth_carry) {
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
    CHECK(!(g->cpu.cpsr & CPSR_C)); /* the Booth carry for -1 * 6 is 0 */
    CHECK(g->cpu.cpsr & CPSR_V);    /* V is never touched */
    gba_destroy(g);
}
```

(`0xE0302190` decodes as MLAS Rd=r0, Rn=r2, Rs=r1, Rm=r0.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit mul
```

Expected: build error (`cpu/mul_carry.h` missing).

- [x] **Step 3: `core/src/cpu/mul_carry.h`**

```c
#ifndef GBA_MUL_CARRY_H
#define GBA_MUL_CARRY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { MUL_SHORT, MUL_LONG_SIGNED, MUL_LONG_UNSIGNED } mul_kind_t;

/* The carry flag a flag-setting ARM7TDMI multiply leaves: rm = multiplicand, rs = multiplier (the
 * register the timing depends on), acc = Rn for MLA, RdHi:RdLo for UMLAL/SMLAL, 0 otherwise. */
bool mul_carry(mul_kind_t kind, uint32_t rm, uint32_t rs, uint64_t acc);

#endif
```

- [x] **Step 4: `core/src/cpu/mul_carry.c`**

```c
/* ARM7TDMI multiply carry flag.
 *
 * Adapted from zaydlang's multiplication-algorithm (https://github.com/zaydlang/multiplication-algorithm,
 * zlib licence; write-up: https://bmchtech.github.io/post/multiply/), which models the chip's Booth
 * recoding and carry-save adder array. This version keeps only the part the carry flag comes from. */
#include "cpu/mul_carry.h"

typedef struct {
    uint64_t sum, carry;
} csa_t;

typedef struct {
    uint64_t value;
    bool carry;
} booth_t;

static bool bit(uint64_t x, int n) {
    return (x >> n) & 1;
}

static uint64_t sign_extend(uint64_t v, int from, int to) {
    if (bit(v, from - 1)) v |= ((1ull << (to - from)) - 1) << from;
    return v;
}

/* Arithmetic shift right of a 33-bit value. */
static uint64_t asr33(uint64_t v, int shift) {
    int64_t s = (int64_t)sign_extend(v, 33, 64);
    return (uint64_t)(s >> shift) & 0x1FFFFFFFFull;
}

static void ror128(uint64_t *lo, uint64_t *hi, int s) {
    uint64_t l = *lo, h = *hi;
    *lo = (l >> s) | (h << (64 - s));
    *hi = (h >> s) | (l << (64 - s));
}

/* One radix-4 Booth digit (3 multiplier bits) → the addend and whether it was negated. */
static booth_t booth_recode(uint64_t m, unsigned chunk) {
    booth_t b = {0, false};
    switch (chunk) {
    case 1:
    case 2: b.value = m; break;
    case 3: b.value = 2 * m; break;
    case 4: b = (booth_t){~(2 * m), true}; break;
    case 5:
    case 6: b = (booth_t){~m, true}; break;
    default: break; /* 0 and 7 add nothing */
    }
    b.value &= 0x3FFFFFFFFull;
    return b;
}

/* Four carry-save adders: one multiplier cycle (8 multiplier bits). */
static csa_t csa_array(csa_t in, const booth_t m[4], uint64_t *acc_shift) {
    csa_t cur = in, out = {0, 0};
    for (int i = 0; i < 4; i++) {
        cur.sum &= 0x1FFFFFFFFull;
        cur.carry &= 0x1FFFFFFFFull;
        uint64_t addend = m[i].value & 0x1FFFFFFFFull;
        csa_t r = {cur.sum ^ addend ^ cur.carry, (cur.sum & addend) | (addend & cur.carry) | (cur.carry & cur.sum)};
        r.carry = r.carry << 1 | m[i].carry;
        out.sum |= (r.sum & 3) << (2 * i);
        out.carry |= (r.carry & 3) << (2 * i);
        r.sum >>= 2;
        r.carry >>= 2;
        uint64_t magic = (uint64_t)bit(*acc_shift, 0) + !bit(cur.carry, 32) + !bit(m[i].value, 33);
        r.sum |= magic << 31;
        r.carry |= (uint64_t)!bit(*acc_shift, 1) << 32;
        *acc_shift >>= 2;
        cur = r;
    }
    out.sum |= cur.sum << 8;
    out.carry |= cur.carry << 8;
    return out;
}

bool mul_carry(mul_kind_t kind, uint32_t rm, uint32_t rs, uint64_t acc) {
    bool is_signed = kind != MUL_LONG_UNSIGNED;
    uint64_t multiplier = is_signed ? sign_extend(rs, 32, 34) : rs;
    uint64_t multiplicand = is_signed ? sign_extend(rm, 32, 34) : rm;
    csa_t csa = {acc, (multiplier & 1) ? ~multiplicand : 0};
    uint64_t acc_shift = acc >> 34;
    uint64_t lo = csa.carry & 1, hi = 0; /* the carry half of the partial product, rotated as it grows */
    csa.sum >>= 1;
    csa.carry >>= 1;
    ror128(&lo, &hi, 1);
    int cycles = 0;
    do {
        booth_t m[4];
        for (int i = 0; i < 4; i++) m[i] = booth_recode(multiplicand, (unsigned)(multiplier >> (2 * i)) & 7);
        csa = csa_array(csa, m, &acc_shift);
        lo |= csa.carry & 0xFF;
        csa.sum >>= 8;
        csa.carry >>= 8;
        ror128(&lo, &hi, 8);
        multiplier = asr33(multiplier, 8);
        cycles++;
    } while (multiplier != 0 && !(is_signed && multiplier == 0x1FFFFFFFFull));
    lo |= csa.carry;
    static const int realign[5] = {0, 23, 15, 7, 31};
    ror128(&lo, &hi, realign[cycles]);
    if (kind == MUL_SHORT && cycles == 4) return bit(hi, 31);
    return bit(hi, 63);
}
```

- [x] **Step 5: Use it — `core/src/cpu/arm.c`**

Add `#include "cpu/mul_carry.h"` to the includes. Replace `arm_multiply` and `arm_multiply_long` with:

```c
static void arm_multiply(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    uint32_t m = c->r[rm], s = c->r[rs], acc = BIT(op, 21) ? c->r[rn] : 0; /* read before rd is written */
    uint32_t result = m * s + acc;
    uint32_t cycles = alu_mul_cycles(s, true) + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    c->r[rd] = result;
    if (BIT(op, 20)) {
        cpu_set_nz(c, result);
        cpu_set_c(c, mul_carry(MUL_SHORT, m, s, acc)); /* V is left unchanged */
    }
}

static void arm_multiply_long(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t hi = (op >> 16) & 0xF, lo = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    bool is_signed = BIT(op, 22);
    uint32_t m = c->r[rm], s = c->r[rs];
    uint64_t acc = BIT(op, 21) ? ((uint64_t)c->r[hi] << 32 | c->r[lo]) : 0;
    uint64_t result = (is_signed ? (uint64_t)((int64_t)(int32_t)m * (int64_t)(int32_t)s) : (uint64_t)m * s) + acc;
    uint32_t cycles = alu_mul_cycles(s, is_signed) + 1 + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    c->r[lo] = (uint32_t)result;
    c->r[hi] = (uint32_t)(result >> 32);
    if (BIT(op, 20)) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | ((uint32_t)(result >> 32) & CPSR_N) | (result == 0 ? CPSR_Z : 0);
        cpu_set_c(c, mul_carry(is_signed ? MUL_LONG_SIGNED : MUL_LONG_UNSIGNED, m, s, acc));
    }
}
```

(If `cpu_set_c` is not visible in `arm.c`, it is declared in `cpu/cpu.h` or `cpu/alu.h` — `thumb.c` already uses it; include the same header.)

- [x] **Step 6: Use it — `core/src/cpu/thumb.c`**

Add `#include "cpu/mul_carry.h"`. In `thumb_alu`, replace

```c
    case 0xD: result = a * b; bus_idle(g, alu_mul_cycles(a, true)); break;         /* MUL: C, V kept */
```

with

```c
    case 0xD: /* MUL: Rd = Rs * Rd (Rd is the multiplier); C from the Booth array, V kept */
        result = a * b;
        carry = mul_carry(MUL_SHORT, b, a, 0);
        bus_idle(g, alu_mul_cycles(a, true));
        break;
```

- [x] **Step 7: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all pass.

- [x] **Step 8: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite multiply-long
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite carry
bash tools/dev.sh test
```

Expected: `SUITE multiply-long 72/72`, carry still 93/93; set `groups.multiply-long` to 72. CTest all pass — jsmolka `arm` and `thumb` included.

- [x] **Step 9: Commit**

```bash
git add core/src/cpu/mul_carry.h core/src/cpu/mul_carry.c core/src/cpu/arm.c core/src/cpu/thumb.c tests/unit/test_mul_carry.c tests/unit/test_arm_mul.c tests/suite_targets.json
git commit -m "feat(cpu): multiply carry flag from the ARM7TDMI Booth multiplier" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 4-6) done — checkpoint with the user.**

---

### Task 7: Link port with no cable

**Files:**
- Create: `core/src/sys/sio.h`, `core/src/sys/sio.c`, `tests/unit/test_sio.c`
- Modify: `core/src/sys/sched.h`, `core/src/gba_internal.h`, `core/src/gba.c`, `core/src/mem/io.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_sio.c`**

```c
#include "test.h"
#include "testutil.h"

/* The suite's procedure: set RCNT and SIOCNT (the mode), write a value, read it back. */
static uint16_t write_read(gba_t *g, uint16_t rcnt, uint16_t siocnt, uint32_t off, uint16_t value) {
    tu_io16(g, 0x134, rcnt);
    tu_io16(g, 0x128, siocnt);
    tu_io16(g, off, value);
    uint16_t result = (uint16_t)bus_read16(g, 0x04000000 + off, false);
    if (off != 0x134) tu_io16(g, off, 0);
    tu_io16(g, 0x128, 0);
    tu_io16(g, 0x134, 0x8000);
    return result;
}

TEST(sio_register_rules_per_mode) {
    static const struct {
        uint16_t rcnt, siocnt;
        uint32_t off;
        uint16_t value, expected;
    } rows[] = {
        /* multiplayer */
        {0x0000, 0x2000, 0x120, 0xFFFF, 0x0000}, {0x0000, 0x2000, 0x128, 0xEFFF, 0x6F8F},
        {0x0000, 0x2000, 0x12A, 0xFFFF, 0xFFFF}, {0x0000, 0x2000, 0x134, 0x3FFF, 0x01FF},
        {0x0000, 0x2000, 0x140, 0xFFFF, 0x0040},
        /* normal 8-bit */
        {0x0000, 0x0000, 0x120, 0xFFFF, 0x0000}, {0x0000, 0x0000, 0x128, 0xCFFF, 0x4F8F},
        {0x0000, 0x0000, 0x134, 0x3FFF, 0x01F5},
        /* normal 32-bit */
        {0x0000, 0x1000, 0x120, 0xFFFF, 0xFFFF}, {0x0000, 0x1000, 0x122, 0xFFFF, 0xFFFF},
        {0x0000, 0x1000, 0x124, 0xFFFF, 0x0000}, {0x0000, 0x1000, 0x128, 0xDFFF, 0x5F8F},
        /* UART */
        {0x0000, 0x3000, 0x128, 0xFFFF, 0x7FAF}, {0x0000, 0x3000, 0x12A, 0xFFFF, 0x0000},
        {0x0000, 0x3000, 0x134, 0x3FFF, 0x01FF},
        /* general purpose */
        {0x8000, 0x0000, 0x128, 0xCFFF, 0x4F8F}, {0x8000, 0x0000, 0x134, 0xBFFF, 0x81FF},
        /* JOY BUS */
        {0xC000, 0x0000, 0x134, 0xFFFF, 0xC1FC}, {0xC000, 0x0000, 0x150, 0xFFFF, 0x0000},
        {0xC000, 0x0000, 0x154, 0xFFFF, 0x0000}, {0xC000, 0x0000, 0x15A, 0xFFFF, 0x0000},
    };
    gba_t *g = gba_create(NULL);
    for (size_t i = 0; i < TU_LEN(rows); i++) {
        uint16_t got = write_read(g, rows[i].rcnt, rows[i].siocnt, rows[i].off, rows[i].value);
        if (got != rows[i].expected) fprintf(stderr, "  row %zu (%03X): %04X != %04X\n", i, rows[i].off, got, rows[i].expected);
        CHECK_EQ(got, rows[i].expected);
    }
    gba_destroy(g);
}

TEST(sio_normal8_internal_clock_finishes_after_8_bits) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_SERIAL;
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x4083); /* IRQ, start, 2 MHz, internal clock, normal 8-bit */
    uint64_t start = g->now; /* after the write: it costs a cycle */
    tu_advance_to(g, start + 63);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_advance_to(g, start + 64); /* 8 bits x 8 cycles */
    CHECK(!(bus_read16(g, 0x04000128, false) & 0x80));
    CHECK_EQ(bus_read16(g, 0x0400012A, false) & 0xFF, 0xFF); /* nothing attached: all 1s arrive */
    CHECK(g->irq.if_ & IRQ_SERIAL);
    gba_destroy(g);
}

TEST(sio_normal32_256k_takes_2048_cycles) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x1081); /* start, 256 kHz, internal clock, normal 32-bit, no IRQ */
    uint64_t start = g->now; /* after the write: it costs a cycle */
    tu_advance_to(g, start + 2047);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_advance_to(g, start + 2048);
    CHECK(!(bus_read16(g, 0x04000128, false) & 0x80));
    CHECK_EQ(bus_read32(g, 0x04000120, false), 0xFFFFFFFF);
    CHECK(!(g->irq.if_ & IRQ_SERIAL));
    gba_destroy(g);
}

TEST(sio_external_clock_and_multiplayer_never_finish) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x134, 0x0000);
    tu_io16(g, 0x128, 0x0080); /* normal 8-bit, external clock: nobody drives it */
    tu_advance_to(g, g->now + 1000000);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    tu_io16(g, 0x128, 0x2083); /* multiplayer: we are a child, the parent never starts */
    tu_advance_to(g, g->now + 1000000);
    CHECK(bus_read16(g, 0x04000128, false) & 0x80);
    gba_destroy(g);
}

TEST(sio_clearing_start_cancels_a_transfer) {
    gba_t *g = gba_create(NULL);
    g->irq.ie = IRQ_SERIAL;
    tu_io16(g, 0x134, 0x0000);
    uint64_t start = g->now;
    tu_io16(g, 0x128, 0x4081);
    tu_io16(g, 0x128, 0x4001);
    tu_advance_to(g, start + 4096);
    CHECK(!(g->irq.if_ & IRQ_SERIAL));
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit sio_
```

Expected: FAIL (registers are plain storage; nothing finishes).

- [x] **Step 3: `core/src/sys/sio.h`**

```c
#ifndef GBA_SIO_H
#define GBA_SIO_H

#include <stdint.h>

typedef struct gba gba_t;

/* The link port with no cable attached. */
typedef struct {
    uint16_t siocnt, rcnt;  /* as written: SIOCNT & 0x7FFF, RCNT & 0xC1FF */
    uint32_t data32;        /* SIODATA32 (normal 32-bit mode) */
    uint16_t multi[4];      /* SIOMULTI0-3 (multiplayer; nothing ever arrives) */
    uint16_t data8;         /* SIODATA8 / SIOMLT_SEND */
    uint16_t joycnt, joystat;
} sio_t;

void sio_reset(gba_t *g);
uint16_t sio_read16(gba_t *g, uint32_t off); /* I/O offsets 0x120-0x12E and 0x134-0x15E */
void sio_write16(gba_t *g, uint32_t off, uint16_t value);
void sio_on_transfer_done(gba_t *g); /* EV_SIO */

#endif
```

- [x] **Step 4: `core/src/sys/sio.c`**

```c
#include "sys/sio.h"

#include <string.h>

#include "gba_internal.h"

/* Rules checked against all 90 rows of the mGBA suite's "SIO register R/W" group (hardware values). */

enum { MODE_NORMAL8, MODE_NORMAL32, MODE_MULTI, MODE_UART, MODE_GPIO, MODE_JOY };

static int mode(const sio_t *s) {
    if (s->rcnt & 0x8000) return (s->rcnt & 0x4000) ? MODE_JOY : MODE_GPIO;
    return (s->siocnt >> 12) & 3; /* normal 8, normal 32, multiplayer, UART */
}

void sio_reset(gba_t *g) {
    memset(&g->sio, 0, sizeof g->sio);
}

uint16_t sio_read16(gba_t *g, uint32_t off) {
    const sio_t *s = &g->sio;
    int m = mode(s);
    switch (off) {
    case 0x120:
    case 0x122:
        if (m == MODE_NORMAL32) return (uint16_t)(s->data32 >> ((off - 0x120) * 8));
        if (m == MODE_MULTI) return s->multi[(off - 0x120) >> 1];
        return 0;
    case 0x124:
    case 0x126: return m == MODE_MULTI ? s->multi[(off - 0x120) >> 1] : 0;
    case 0x128:
        if (m == MODE_MULTI) return (uint16_t)((s->siocnt & 0x7F83) | 0x000C); /* SI, SD high: a child, "ready" */
        if (m == MODE_UART) return (uint16_t)((s->siocnt & 0x7F8F) | 0x0020);  /* receive FIFO empty */
        return (uint16_t)((s->siocnt & 0x7F8B) | 0x0004);                      /* SI high */
    case 0x12A: return m == MODE_UART ? 0 : s->data8;
    case 0x134: {
        uint16_t pins = m == MODE_GPIO ? (s->rcnt & 0xF) : m == MODE_JOY ? 0xC : m <= MODE_NORMAL32 ? 0x5 : 0xF;
        return (uint16_t)((s->rcnt & 0xC1F0) | pins);
    }
    case 0x140: return s->joycnt & 0x47;
    case 0x158: return s->joystat & 0x3A;
    default: return 0; /* JOY_RECV (nothing arrives), JOY_TRANS (reads 0), unused */
    }
}

static void write_siocnt(gba_t *g, uint16_t value) {
    sio_t *s = &g->sio;
    bool was_busy = (s->siocnt & 0x80) != 0;
    s->siocnt = value & 0x7FFF;
    int m = mode(s);
    if (!(value & 0x80) || (m != MODE_NORMAL8 && m != MODE_NORMAL32)) {
        sched_cancel(&g->sched, EV_SIO); /* stopped, or a mode where nothing ever finishes */
        return;
    }
    if (was_busy || !(value & 1)) return; /* already running, or an external clock nobody drives */
    uint32_t bits = m == MODE_NORMAL32 ? 32 : 8;
    uint32_t cycles_per_bit = (value & 2) ? 8 : 64; /* 2 MHz or 256 kHz */
    sched_add(&g->sched, EV_SIO, g->now + bits * cycles_per_bit);
}

void sio_write16(gba_t *g, uint32_t off, uint16_t value) {
    sio_t *s = &g->sio;
    switch (off) {
    case 0x120:
    case 0x122:
        if (mode(s) == MODE_NORMAL32) {
            uint32_t shift = (off - 0x120) * 8;
            s->data32 = (s->data32 & ~(0xFFFFu << shift)) | (uint32_t)value << shift;
        }
        break;
    case 0x128: write_siocnt(g, value); break;
    case 0x12A: s->data8 = value; break;
    case 0x134: s->rcnt = value & 0xC1FF; break;
    case 0x140: s->joycnt = (uint16_t)((s->joycnt & ~(value & 7) & ~0x40) | (value & 0x40)); break;
    case 0x158: s->joystat = (uint16_t)((s->joystat & ~0x30) | (value & 0x30)); break;
    default: break;
    }
}

/* A normal-mode transfer with the internal clock has finished: the input line stayed high. */
void sio_on_transfer_done(gba_t *g) {
    sio_t *s = &g->sio;
    if (mode(s) == MODE_NORMAL32) s->data32 = 0xFFFFFFFF;
    else s->data8 = (uint16_t)(s->data8 | 0x00FF);
    s->siocnt &= (uint16_t)~0x80;
    if (s->siocnt & 0x4000) irq_raise(g, IRQ_SERIAL);
}
```

- [x] **Step 5: Hook it up**

`core/src/sys/sched.h` — add `EV_SIO` before `EV_COUNT`:

```c
    EV_APU_SAMPLE, /* one stereo output sample (every 512 cycles) */
    EV_SIO,        /* a link-port transfer finishes */
    EV_COUNT
```

`core/src/gba_internal.h` — add `#include "sys/sio.h"` with the other includes, and `sio_t sio;` after `apu_t apu;` in `struct gba`.

`core/src/gba.c` — in `gba_reset`, after `dma_reset(g);` add `sio_reset(g);`; in `gba_run_events`, add before `default:`:

```c
        case EV_SIO: sio_on_transfer_done(g); break;
```

`core/src/mem/io.c` — add after the `enum { REG_SOUND ... };` line:

```c
/* Link-port registers (0x130/0x132 in between are the keypad). */
static bool link_port(uint32_t off) {
    return (off >= 0x120 && off < 0x130) || (off >= 0x134 && off < 0x160);
}
```

At the top of `read16`, add:

```c
    if (link_port(off)) return sio_read16(g, off);
```

At the top of `write16`, add:

```c
    if (link_port(off)) {
        sio_write16(g, off, v);
        return;
    }
```

- [x] **Step 6: Run the tests**

```bash
bash tools/dev.sh unit
```

Expected: all pass.

- [x] **Step 7: Suite and full run**

```bash
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite sio-regs
bash tools/dev.sh run tests/roms/mgba/suite.gba --suite sio-timing --save /tmp/siot.sav; tr -d '\000' < /tmp/siot.sav
bash tools/dev.sh test
```

Expected: `SUITE sio-regs 90/90` → set `groups.sio-regs` to 90. For `sio-timing`: the four normal-mode checks now finish instead of timing out; each compares transfer time + interrupt overhead (expected cycles: Normal8/256k 0x279, Normal8/2M 0xB9, Normal32/256k 0x879, Normal32/2M 0x179). Set `groups.sio-timing` to the printed score. **If it is below 4/4: stop and show the user the measured vs expected cycle counts from the log** — the transfer time is exact, so the difference is CPU/IRQ timing (3c-2); the user decides whether these checks move to 3c-2 (spec §9). CTest all pass.

- [x] **Step 8: Commit**

```bash
git add core/src/sys/sio.h core/src/sys/sio.c core/src/sys/sched.h core/src/gba_internal.h core/src/gba.c core/src/mem/io.c tests/unit/test_sio.c tests/suite_targets.json
git commit -m "feat(core): link port with no cable - register rules per mode, normal-mode transfers finish" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Keypad interrupt

**Files:**
- Create: `core/src/sys/keypad.h`, `core/src/sys/keypad.c`, `tests/unit/test_keypad.c`
- Modify: `core/src/gba.c`, `core/src/mem/io.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_keypad.c`**

```c
#include "test.h"
#include "testutil.h"

TEST(keypad_irq_any_selected_key) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A | GBA_KEY_B); /* IRQ on, OR mode */
    gba_set_keys(g, GBA_KEY_START);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_set_keys(g, GBA_KEY_B);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}

TEST(keypad_irq_all_selected_keys) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, 0xC000 | GBA_KEY_A | GBA_KEY_B); /* IRQ on, AND mode */
    gba_set_keys(g, GBA_KEY_A);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_B);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}

TEST(keypad_irq_needs_its_enable_bit) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, GBA_KEY_A);
    gba_set_keys(g, GBA_KEY_A);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_destroy(g);
}

TEST(keypad_irq_checked_when_keycnt_is_written) {
    gba_t *g = gba_create(NULL);
    gba_set_keys(g, GBA_KEY_A);
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit keypad_
```

Expected: the three "fires" tests FAIL; `keypad_irq_needs_its_enable_bit` passes.

- [x] **Step 3: `core/src/sys/keypad.h` and `keypad.c`**

```c
#ifndef GBA_KEYPAD_H
#define GBA_KEYPAD_H

typedef struct gba gba_t;

/* Checks KEYCNT (0x04000132) against the pressed keys and raises the keypad IRQ when its condition
 * holds. Called when the keys change and when KEYCNT is written. */
void keypad_check(gba_t *g);

#endif
```

```c
#include "sys/keypad.h"

#include "gba_internal.h"

void keypad_check(gba_t *g) {
    uint16_t cnt = (uint16_t)(g->io.raw[0x132] | g->io.raw[0x133] << 8);
    if (!(cnt & 0x4000)) return;
    uint16_t selected = cnt & 0x3FF;
    uint16_t pressed = (uint16_t)(~g->keyinput & 0x3FF);
    bool hit = (cnt & 0x8000) ? (pressed & selected) == selected /* AND: all of them */
                              : (pressed & selected) != 0;       /* OR: any of them */
    if (hit) irq_raise(g, IRQ_KEYPAD);
}
```

- [x] **Step 4: Hook it up**

`core/src/gba.c` — add `#include "sys/keypad.h"`; replace `gba_set_keys` with:

```c
void gba_set_keys(gba_t *g, uint16_t pressed) {
    g->keyinput = (uint16_t)(~pressed & 0x3FF);
    keypad_check(g);
}
```

`core/src/mem/io.c` — add `#include "sys/keypad.h"`; add `REG_KEYCNT = 0x132,` to the register enum after `REG_KEYINPUT`; in `write16`'s switch add before `case REG_IE:`:

```c
    case REG_KEYCNT:
        g->io.raw[off] = (uint8_t)v;
        g->io.raw[off + 1] = (uint8_t)(v >> 8);
        keypad_check(g);
        break;
```

- [x] **Step 5: Run the tests and everything**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: all pass (the test runner calls `gba_set_keys` every frame, so each check also runs once per frame there — harmless).

- [x] **Step 6: Commit**

```bash
git add core/src/sys/keypad.h core/src/sys/keypad.c core/src/gba.c core/src/mem/io.c tests/unit/test_keypad.c
git commit -m "feat(core): keypad interrupt (KEYCNT any/all keys)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Stop mode

**Files:**
- Create: `tests/unit/test_stop.c`
- Modify: `core/src/sys/irq.h`, `core/src/sys/irq.c`, `core/src/mem/io.c`, `core/src/bios/hle.c`, `core/src/apu/apu.h`, `core/src/apu/apu.c`, `core/src/gba.c`

- [x] **Step 1: Write the failing tests — `tests/unit/test_stop.c`**

```c
#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static gba_t *spinning(void) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    return tu_arm(ops, TU_LEN(ops));
}

static void drain(gba_t *g) {
    int16_t buf[4096];
    while (gba_audio_read(g, buf, 2048) > 0) {}
}

TEST(stop_freezes_the_console_and_hands_out_silence) {
    gba_t *g = spinning();
    gba_run_frame(g);
    drain(g);
    bus_write8(g, 0x04000301, 0x80, false); /* HALTCNT bit 7: Stop */
    CHECK(g->irq.stopped);
    uint64_t now = g->now;
    uint16_t vcount = g->ppu.vcount;
    gba_run_frame(g);
    CHECK_EQ(g->now, now);
    CHECK_EQ(g->ppu.vcount, vcount);
    int16_t buf[2 * 1024];
    size_t n = gba_audio_read(g, buf, 1024);
    CHECK_EQ(n, 548); /* 280,896 / 512 = 548 rest 320 */
    for (size_t i = 0; i < n * 2; i++) CHECK_EQ(buf[i], 0);
    gba_run_frame(g);
    CHECK_EQ(gba_audio_read(g, buf, 1024), 549); /* the rest carries over */
    gba_destroy(g);
}

TEST(stop_wakes_on_an_enabled_keypad_irq) {
    gba_t *g = spinning();
    g->irq.ie = IRQ_KEYPAD;
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A);
    bus_write8(g, 0x04000301, 0x80, false);
    gba_run_frame(g);
    CHECK(g->irq.stopped);
    uint64_t now = g->now;
    gba_set_keys(g, GBA_KEY_A);
    gba_run_frame(g);
    CHECK(!g->irq.stopped);
    CHECK(g->now > now);
    gba_destroy(g);
}

TEST(stop_ignores_other_interrupts) {
    gba_t *g = spinning();
    g->irq.ie = IRQ_VBLANK | IRQ_KEYPAD;
    bus_write8(g, 0x04000301, 0x80, false);
    irq_raise(g, IRQ_VBLANK);
    gba_run_frame(g);
    CHECK(g->irq.stopped);
    gba_destroy(g);
}

TEST(stop_from_the_bios_call_and_halt_still_halts) {
    gba_t *g = spinning();
    hle_swi(g, 0x03);
    CHECK(g->irq.stopped);
    gba_destroy(g);
    g = spinning();
    bus_write8(g, 0x04000301, 0x00, false);
    CHECK(g->irq.halted);
    CHECK(!g->irq.stopped);
    gba_destroy(g);
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit stop_
```

Expected: build error (`irq.stopped` missing).

- [x] **Step 3: `core/src/sys/irq.h` / `irq.c`**

In `irq_t`, add `bool stopped;` after `bool halted;`. After `void irq_halt(gba_t *g);` declare:

```c
void irq_stop(gba_t *g); /* Stop: the whole console freezes until a keypad/serial/cartridge IRQ */
/* A stopped console wakes when a keypad, serial or cartridge interrupt is flagged and enabled. */
bool irq_stop_wake(const gba_t *g);
```

In `irq.c`: `irq_reset` also sets `g->irq.stopped = false;`, and add:

```c
void irq_stop(gba_t *g) {
    g->irq.stopped = true;
}

bool irq_stop_wake(const gba_t *g) {
    return (g->irq.ie & g->irq.if_ & (IRQ_KEYPAD | IRQ_SERIAL | IRQ_GAMEPAK)) != 0;
}
```

- [x] **Step 4: Entering Stop — `core/src/mem/io.c` and `core/src/bios/hle.c`**

In `io.c` `write16`, replace

```c
    case REG_POSTFLG:
        g->io.postflg = v & 1;
        irq_halt(g); /* a halfword write here also writes HALTCNT */
        break;
```

with

```c
    case REG_POSTFLG:
        g->io.postflg = v & 1;
        if (v & 0x8000) irq_stop(g); /* a halfword write here also writes HALTCNT */
        else irq_halt(g);
        break;
```

In `write8`, replace

```c
    case REG_HALTCNT: irq_halt(g); return; /* bit 7 selects Stop; treated as Halt until M3 */
```

with

```c
    case REG_HALTCNT: /* bit 7: Stop instead of Halt */
        if (v & 0x80) irq_stop(g);
        else irq_halt(g);
        return;
```

In `hle.c` `hle_swi`, replace

```c
    case 0x02:
    case 0x03: irq_halt(g); break; /* Stop behaves like Halt until milestone 3 */
```

with

```c
    case 0x02: irq_halt(g); break;
    case 0x03: irq_stop(g); break;
```

- [x] **Step 5: Silence while stopped — `core/src/apu/apu.h` / `apu.c`**

In `apu_t`, add after `uint32_t ring_read, ring_count;`:

```c
    uint32_t silence_cycles;            /* cycles of silence owed while the console is stopped */
```

Declare after `apu_read`:

```c
/* One video frame's worth of silent samples (the console is stopped; frontends paced by sound keep going). */
void apu_stopped_frame(gba_t *g);
```

In `apu.c`, after `apu_read`:

```c
void apu_stopped_frame(gba_t *g) {
    apu_t *a = &g->apu;
    a->silence_cycles += PPU_FRAME_CYCLES;
    while (a->silence_cycles >= APU_SAMPLE_CYCLES) {
        ring_push(a, 0, 0);
        a->silence_cycles -= APU_SAMPLE_CYCLES;
    }
}
```

- [x] **Step 6: The frame loop — `core/src/gba.c`**

Replace `gba_run_frame` with:

```c
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
                    g->now = sched_next(&g->sched); /* nothing can happen before the next event */
                    break;
                }
                g->irq.halted = false;
            }
            cpu_step(g);
        }
        gba_run_events(g);
    }
}
```

(When Stop begins mid-frame, the CPU loop ends at once; `gba_run_events` only runs events already due, then the next pass of the outer loop returns with a frame of silence.)

- [x] **Step 7: Run the tests and everything**

```bash
bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: all pass.

- [x] **Step 8: Commit**

```bash
git add core/src/sys/irq.h core/src/sys/irq.c core/src/mem/io.c core/src/bios/hle.c core/src/apu/apu.h core/src/apu/apu.c core/src/gba.c tests/unit/test_stop.c
git commit -m "feat(core): Stop mode - the console freezes until a keypad, serial or cartridge IRQ" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 3 (Tasks 7-9) done — checkpoint with the user.**

---

### Task 10: User check and milestone 3c-1 exit

**Files:**
- Modify: `docs/PROGRESS.md`, `CLAUDE.md`, `docs/superpowers/specs/2026-09-25-milestone-3c1-values-design.md` (status line), `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (milestone table), this plan (checkboxes)

- [x] **Step 1: Full run with sanitizers**

```bash
bash tools/dev.sh test
for g in memory io shifter carry multiply-long bios-math sio-regs sio-timing timing timer-countup timer-irq dma misc; do bash tools/dev.sh run tests/roms/mgba/suite.gba --suite $g; done
```

Expected: CTest all pass (56 entries); no ASan/UBSan reports. The 3c-1 groups print full scores (memory 1552, io 130, shifter 140, carry 93, multiply-long 72, bios-math 615, sio-regs 90; sio-timing was moved to 3c-2 after Task 7 and stays at its minimum); the 3c-2 groups are at or above their baseline. `tests/suite_targets.json` holds the final numbers.

- [x] **Step 2: Speed check (release build)**

```bash
bash tools/dev.sh release
time build/release/tools/testrunner/testrunner tests/roms/tonc/m7_demo.gba --frames 600
```

(Run it through `tools/dev.sh`'s PATH if the exe needs llvm-mingw's DLLs: `bash -c 'source <(sed -n 13,29p tools/dev.sh); build/release/tools/testrunner/testrunner tests/roms/tonc/m7_demo.gba --frames 600'`.) Expected: well above real time (600 frames in under 10 s is 60+ fps; it was ~9x real time before). Note the number in PROGRESS.md.

- [x] **Step 3: User check in `gbaview`**

Close any open viewer, then start it for the user (Bash tool, background):

```bash
bash tools/dev.sh view tests/roms/mgba/suite.gba
```

Tell the user (PowerShell form if they start it themselves: `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh view tests/roms/mgba/suite.gba`): arrows move, **Z = A** opens a group, **X = B** goes back; open Memory, I/O read, Multiply long, BIOS math and SIO register R/W and see full scores with no red lines (SIO timing still shows 4 red lines: 3c-2). Then, **only with the user's OK**, a short run of their game A hack (`~\Downloads\hack-1.gba`) to confirm the menu, controls and saving still work.

- [x] **Step 4: Docs**

- `docs/PROGRESS.md`: milestone 3c-1 ✅ with the final scores, the speed number, the user's check; Next = brainstorm 3c-2.
- Spec status line: `**Status:** Implemented — milestone 3c-1 complete <date> (tag \`milestone-3c1\`)`.
- Parent spec §10 row 3: mark 3c-1 ✅.
- `CLAUDE.md` "Key files": current spec/plan → none yet (3c-2 needs brainstorming); last finished → the 3c-1 spec and this plan.
- Tick every box in this plan.

- [x] **Step 5: Commit, tag, merge (ask the user how to integrate — earlier milestones: fast-forward merge into `main`)**

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 3c-1 complete - right values" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-3c1
```

Then follow superpowers:finishing-a-development-branch (the user has chosen a fast-forward merge into `main` for every earlier milestone; confirm), run `bash tools/dev.sh test` on `main`, delete the branch, and record the merge in `docs/PROGRESS.md`.
