# Milestone 3c-1 — Right Values (I/O reads, open bus, memory, BIOS maths, multiply flag, link port, keypad IRQ, Stop) — Design

**Date:** 2026-09-25
**Status:** Implemented — milestone 3c-1 complete 2026-09-25 (tag `milestone-3c1`)
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3.3 memory bus, §3.4 BIOS, §4.4 timers/IRQ/keypad/SIO, §9.3 test ROMs, §10 milestones)
**Builds on:** milestone 3b-2 (tag `milestone-3b2`).

---

## 1. Goal and scope

Make everything the mGBA test suite checks *that is not about exact timing* match real hardware, and add the three small pieces of hardware from the parent spec that are still missing (keypad interrupt, link port with no cable, Stop mode).

**User decisions (2026-09-25):**
- Milestone 3c is split in two: **3c-1 right values** (this document) → **3c-2 right timing** (timers, prefetch buffer, DMA timing).
- Finish line for the whole of 3c: every mGBA suite group at 100%, except the Timing group, which must reach at least mGBA's own score (1708/2020, about 85%). **Later revised for 3c-2 (user decision, 2026-09-25):** mGBA level for the timing groups — see `docs/superpowers/specs/2026-09-25-milestone-3c2-timing-design.md` §1.
- The keypad interrupt, link port ("no cable") and Stop mode are added in 3c-1.
- The mGBA suite ROM was downloaded with the user's approval (`suite-latest.zip`, 88,220 bytes, SHA-256 `b802bb1a08566456f1820d09f8c5362ede30c838297fd05acdf862f1b1e790a5`, MIT licence, from `https://s3.amazonaws.com/mgba/suite-latest.zip`).
- Suite results are checked by the test runner reading the score off the screen and comparing it with a per-group target.
- If a check in a 3c-1 group turns out to depend on timing (or is much harder than expected), it is shown to the user and moved to 3c-2 only with their OK.

**Baseline (2026-09-25, before any 3c work, release build):**

| # | Group (menu order) | Score | Milestone |
|---|---|---|---|
| 0 | Memory tests | 1459/1552 | 3c-1 |
| 1 | I/O read tests | 12/130 | 3c-1 |
| 2 | Timing tests | 362/2020 | 3c-2 |
| 3 | Timer count-up tests | 342/936 | 3c-2 |
| 4 | Timer IRQ tests | 0/90 | 3c-2 |
| 5 | Shifter tests | 140/140 | 3c-1 (already passes) |
| 6 | Carry tests | 93/93 | 3c-1 (already passes) |
| 7 | Multiply long tests | 52/72 | 3c-1 |
| 8 | BIOS math tests | 325/615 | 3c-1 |
| 9 | DMA tests | 1184/1244 | 3c-2 |
| 10 | SIO register R/W tests | 7/90 | 3c-1 |
| 11 | SIO timing tests | 0/4 | 3c-2 (moved by the user, 2026-09-25 — see §9) |
| 12 | Misc. edge case tests | 3/12 | 3c-2 |
| 13 | Video tests | (manual, no score) | not gated |

| In scope (3c-1) | Out of scope (3c-2 or later) |
|---|---|
| Suite tooling: download, `--keys`, `--suite`, score targets, CTest | Timer start delays, timer read timing, timer IRQ timing |
| I/O register read rules (readable masks, write-only, unused) | ROM prefetch buffer, instruction fetch N/S accuracy |
| Open bus from the prefetched opcode (ARM/Thumb rules) | DMA cycle timing, DMA open-bus timing subtleties |
| Memory group value fixes (loads, ROM out of bounds, DMA/CpuSet values, DMA0 cannot read ROM) | H-blank flag start time, misc. edge cases |
| HLE BIOS maths: exact side-register values (r1/r3 …) | Link cable to another emulator (never in v1) |
| Multiply carry flag (MUL/MLA/UMULL/UMLAL/SMULL/SMLAL, ARM + Thumb) | |
| Link port: registers per mode, "no cable" transfers and timing | |
| Keypad interrupt (KEYCNT) | |
| Stop mode (HALTCNT bit 7) | |

---

## 2. Files

```
tools/fetch_test_roms.py           + mGBA suite zip (pinned SHA-256), unpacked to tests/roms/mgba/suite.gba
tools/testrunner/main.c            + --keys <frame:KEY[+KEY],...>, --suite <group>, --suite-min <n> (score read from the BG1 tile map)
tests/suite_targets.json           minimum score per suite group
CMakeLists.txt                     + one CTest entry per group in suite_targets.json
core/src/mem/io.c / io_rules.c     read rules table for 0x04000000-0x040003FF; unused I/O → open bus
core/src/mem/bus.c                 open bus from the prefetched opcode; memory-group fixes
core/src/sys/dma.c                 value fixes found by the memory group (e.g. DMA0 cannot read ROM)
core/src/bios/hle_math.c           side-register values matching the real BIOS
core/src/cpu/alu.h, arm.c, thumb.c multiply carry flag
core/src/sys/sio.h / sio.c         link port: registers, modes, "no cable" transfers, EV_SIO
core/src/sys/keypad.c (or irq.c)   KEYCNT interrupt
core/src/sys/irq.c, gba.c          Stop mode
tests/unit/test_io_read.c, test_open_bus.c, test_sio.c, test_keypad.c, test_stop.c (+ additions to existing CPU/BIOS tests)
```

File names may be adjusted by the plan to follow the existing layout; each new unit keeps one clear purpose.

---

## 3. Suite tooling

- **Download:** `tools/fetch_test_roms.py` fetches `suite-latest.zip`, checks size 88,220 and the SHA-256 above, and unpacks `suite.gba` (524,288 bytes) into `tests/roms/mgba/` (gitignored). A different file fails loudly.
- **`--keys <list>`** (any ROM): comma-separated `frame:KEYS`, where `KEYS` is `A`, `B`, `SELECT`, `START`, `RIGHT`, `LEFT`, `UP`, `DOWN`, `R`, `L` joined with `+`, or `-` for none. From that frame on, those keys are held. Example: `--keys 10:DOWN,12:-,14:A,16:-`. Unknown key name → usage + exit 3.
- **`--suite <group>`**: group names `memory`, `io`, `timing`, `timer-countup`, `timer-irq`, `shifter`, `carry`, `multiply-long`, `bios-math`, `dma`, `sio-regs`, `sio-timing`, `misc`. The runner presses DOWN (menu index) times and then A, with a few idle frames between presses, then runs until the score appears in the top-right corner of the result screen (cap: 3600 frames; every group finishes within 224 frames today). **Reading the score (decided while planning, replaces glyph matching):** the suite draws its text as BG1 tiles whose tile number is the character code minus 32 (`updateTextGrid`, screen block 1 at VRAM 0x06000800, 32 tiles per row), so the runner reads row 1, columns 21–29 straight from the tile map and parses `passed/total` — exact, and no font bitmaps are needed. Prints `SUITE <group> <passed>/<total>`. No score by the cap → message + exit 2.
- **`--suite-min <n>`**: exit 0 when `passed ≥ n`, else exit 1. Without it the runner only prints.
- **Targets:** `tests/suite_targets.json`, e.g. `{"memory": 1552, "timing": 362, ...}`. 3c-1 groups are raised to their full count as their task finishes; 3c-2 groups start at the baseline scores and only ever go up.
- **CTest:** one test `mgba_suite_<group>` per entry, skipped (exit 77) when the ROM is missing, like the other test ROMs.
- **Debugging:** the suite writes a text line for every failing check (with got/expected values) into cartridge SRAM (`savprintf`), so `--save <file>` gives the full failure list; failing checks are also shown in red on the result screens.

## 4. I/O register reads

- **Read rules table** for every halfword in 0x04000000–0x040003FE, one entry each:
  - `READ(mask)` — the stored value AND mask (unused bits read 0);
  - `ZERO` — write-only, reads 0;
  - `OPEN` — nothing there, reads open bus (§5).
- Registers with their own read handler (sound, timers, DISPSTAT, VCOUNT, KEYINPUT, IE, IF, IME, WAITCNT, POSTFLG, …) keep it; the table's mask is applied on top where the handler returns raw state.
- Sources: GBATEK I/O map and "readable bits", cross-checked against mGBA's register tables (`src/gba/io.c`) and the suite's own expectations (`src/io-read.c`; the suite expects `0xDEAD` — its open-bus pattern — for unreadable registers and exact masks such as `BLDCNT` → 0x3FFF, `SOUND1CNT_LO` → 0x007F, wave RAM readable).
- Addresses 0x04000400 and above read open bus. (Internal memory control at 0x04000800 is added only if a suite check needs it.)
- 32-bit reads combine the rules of both halves; 8-bit reads take the lane of the halfword result.
- Writes are unchanged.

## 5. Open bus

- Today the open-bus value is the instruction being executed. On hardware it is the value the CPU has already prefetched:
  - **ARM state:** the word at `PC + 8` (the instruction two ahead), where PC is the current instruction's address.
  - **Thumb state** (`$` = current instruction's address), by the region the code runs from (GBATEK "Unpredictable Things"):
    - EWRAM, palette, VRAM, cartridge ROM: both halves = `[$+4]`;
    - BIOS, OAM: `$` 4-aligned → low `[$+4]`, high `[$+6]`; otherwise low `[$+2]`, high `[$+4]`;
    - IWRAM: `$` 4-aligned → low `[$+4]`, high `[$+2]`; otherwise low `[$+2]`, high `[$+4]`.
- Computed at read time with a side-effect-free peek of those addresses (no cycles charged). The BIOS read-protection latch and the DMA latch keep their current rules.
- Used by: unmapped regions (0x01, 0x10+), BIOS reads outside 0x0000–0x3FFF, `OPEN` I/O slots.

## 6. Memory group fixes

- The group checks values only (no timing): `ldrb`, `ldrsb`, `ldrh` (aligned and +1), `ldrsh` (aligned and +1), `ldr` (aligned and +1/+2/+3) and stores in every region and mirror; ROM past the end (expects the `(addr >> 1)` pattern, e.g. `0x34573456` at 0x092468AC); BIOS past 0x3FFF and unmapped space (expect 0 in the suite's setup); DMA0–3, CpuSet and CpuFastSet with aligned and unaligned addresses.
- **Causes found while planning** (from the suite's failure log): (1) our HLE CpuSet/CpuFastSet round odd source/destination addresses down — the real BIOS uses LDRH (misaligned reads rotate) for 16-bit CpuSet and LDMIA/STMIA (the bus aligns, the save area keeps exact addresses) for 32-bit, so the HLE must pass addresses through unchanged; (2) DMA must align its addresses to the unit size even in the save area (the CPU keeps exact save-area addresses, DMA does not); (3) the BIOS/unmapped out-of-bounds loads need the open-bus fix (§5). DMA0 not reading cartridge ROM is already done (DMA0's source mask 0x07FFFFFF).

## 7. HLE BIOS maths

- Only ArcTan (24 checks) and ArcTan2 (95 checks) fail; Div and Sqrt already pass. **Rules (checked while planning against all 119 of the suite's ArcTan/ArcTan2 rows):** ArcTan takes the full 32-bit r0 and does its polynomial in wrapping 32-bit arithmetic; r0 = the result as a sign-extended 16-bit value, r1 = `a` (= −(x·x >> 14)), r3 = `b` (the final polynomial value). ArcTan2 takes full 32-bit x (r0) and y (r1) and uses `(n << 14) / d` in 32 bits; r0 = the angle as an unsigned 16-bit value; r1 = the `a` of the ArcTan it called (unchanged when x or y is 0); r3 = 0x170.
- Results in r0 must not change. If the Tonc `swi_demo` frame fingerprint changes anyway, the new picture is compared with the Tonc guide and shown to the user before re-approval.

## 8. Multiply carry flag

- On ARM7TDMI, flag-setting multiplies set C to a value that comes out of the multiplier's internal Booth steps (published research, 2023, used by NanoBoyAdvance); V is unchanged. Today C is kept unchanged.
- Implement that rule for MULS, MLAS, UMULLS, UMLALS, SMULLS, SMLALS (ARM) and MUL (Thumb). N and Z unchanged in meaning (Z/N from the full 64-bit result for long multiplies).
- **Checked while planning:** the reference code gives the exact product for 2,000,000 random inputs and the exact flags for all 72 suite checks; the trimmed carry-only version for our core agrees with it on 3,000,000 random inputs (incl. accumulates and early-termination paths).
- Unit tests with known input → C pairs from the research and the suite's red cases; jsmolka `arm` and `thumb` must still pass.

## 9. Link port with no cable

- **Registers** (0x04000120–0x0400015A): SIODATA32 / SIOMULTI0–3, SIOCNT, SIODATA8 / SIOMLT_SEND, RCNT, JOYCNT, JOY_RECV, JOY_TRANS, JOYSTAT. Read/write rules depend on the mode (RCNT bits 14–15, SIOCNT bits 12–13): normal 8-bit, normal 32-bit, multiplayer, UART, general-purpose, JOY BUS — exact masks from GBATEK, mGBA (`src/gba/sio.c`, `src/gba/io.c`) and the suite's `SIO register R/W` expectations.
- **Normal mode, internal clock:** setting the start bit begins a transfer of 8 or 32 bits at 256 KiHz (64 cycles per bit) or 2 MiHz (8 cycles per bit). When it finishes (new event `EV_SIO`): received data = all 1s, start bit cleared, serial IRQ if SIOCNT bit 14 is set.
- **Normal mode, external clock:** the transfer never finishes (nobody drives the clock), as on hardware with no cable.
- **Multiplayer mode (corrected while planning):** with nothing attached, SI and SD read high, so the GBA sees itself as a child and a transfer **never completes** — the suite's `SIO timing` group expects a timeout for all four multiplayer speeds on hardware (and skips them). The start bit reads back as written.
- **Register read rules (checked while planning against all 90 rows of the suite's `SIO register R/W` table):** mode = RCNT bit 15 set → bit 14 ? JOY BUS : general-purpose; else SIOCNT bits 12–13 (normal 8, normal 32, multiplayer, UART). 0x120/0x122: SIODATA32 in normal-32 (written only there), SIOMULTI0/1 in multiplayer, else 0; 0x124/0x126: SIOMULTI2/3 in multiplayer, else 0; SIOCNT stored as `v & 0x7FFF`, read as `(v & 0x7F8B) | 4` (normal, GP, JOY: SI high), `(v & 0x7F83) | 0xC` (multiplayer: SI, SD high), `(v & 0x7F8F) | 0x20` (UART: receive FIFO empty); 0x12A: SIODATA8/SIOMLT_SEND read back, 0 in UART; RCNT stored as `v & 0xC1FF`, read as `(v & 0xC1F0) | pins` with pins = written bits (GP), 0xC (JOY), 0x5 (normal), 0xF (multiplayer, UART); JOYCNT: bit 6 stored, bits 0–2 cleared by writing 1, read mask 0x47; JOYSTAT: bits 4–5 stored, read mask 0x3A; JOY_RECV/JOY_TRANS read 0; 0x136, 0x142, 0x15A read 0.
- **Timing risk:** the suite's four normal-mode timing checks measure the transfer time plus about 121 cycles of interrupt/BIOS overhead with a timer (e.g. 8 bits at 256 KiHz = 512 + 121 = 0x279). The transfer time itself is exact here; the overhead depends on CPU/IRQ timing (3c-2). If the group is not 4/4 after this work, the measured values are shown to the user, who decides whether these checks move to 3c-2.
- **Outcome (2026-09-25):** after Task 7 every check was exactly 24 cycles short (Normal8/256k 0x261 vs 0x279, Normal8/2M 0xA1 vs 0xB9, Normal32/256k 0x861 vs 0x879, Normal32/2M 0x161 vs 0x179): transfer times exact, constant interrupt/BIOS overhead gap. **User decision: the SIO timing group moves to 3c-2.**
- UART / JOY BUS / general-purpose: registers readable/writable as on hardware; no transfers happen.

## 10. Keypad interrupt

- KEYCNT (0x04000132): bits 0–9 select keys, bit 14 enables the IRQ, bit 15 = condition (0: any selected key pressed, 1: all selected keys pressed).
- The condition is checked whenever the keys change (`gba_set_keys`) and when KEYCNT is written; when true and enabled, IF bit 12 (keypad) is set.

## 11. Stop mode

- A write to HALTCNT with bit 7 set enters Stop (today it is treated as Halt).
- While stopped, nothing inside the console moves: CPU, PPU, sound, timers and DMA are frozen, and the emulated clock does not advance.
- Wake-up: a keypad, serial or GamePak interrupt that is enabled in IE (checked when keys change, which is the only one that can happen with no cable and no cartridge interrupts).
- Frontends keep working: while stopped, each `gba_run_frame` call returns at once and `gba_audio_read` hands out one frame's worth of silence (280,896 / 512 ≈ 549 stereo samples), so audio-paced frontends keep a steady 60 fps. The framebuffer keeps its last picture (games blank the screen before stopping).

## 12. Testing

1. Unit tests first for every fix: I/O read table entries (per register class), open-bus values for ARM and each Thumb region case, memory-group causes, BIOS side registers, multiply carry pairs, SIO register rules / transfer completion times / IRQ / external clock never finishing, KEYCNT OR/AND and IRQ, Stop entering / staying frozen / waking on a key.
2. mGBA suite groups through CTest with `tests/suite_targets.json`: all 3c-1 groups at their full counts; 3c-2 groups at least at their baseline.
3. All earlier tests keep passing (43 CTest entries today); ASan + UBSan clean.
4. **User check:** the user opens `suite.gba` in `gbaview` and sees the 3c-1 groups at full score. With the user's OK, a short run of their game A hack confirms nothing broke.

## 13. Done when

- Memory, I/O read, Shifter, Carry, Multiply long, BIOS math and SIO register R/W all at 100% (SIO timing moved to 3c-2 by the user on 2026-09-25).
- All unit tests and earlier CTest entries pass; sanitizers report nothing.
- The user's `gbaview` check passes.
