# Milestone 7a — Speed: Measuring + C Speed-ups — Design

**Date:** 2026-10-02
**Status:** Implemented — milestone 7a complete 2026-10-03 (tag `milestone-7a`). Approved design (brainstormed with the user 2026-10-02; the written spec approved 2026-10-03: 'Everything is perfect, begin'); prototyped (branch `proto-7a`) and tried by the user on phone B ('Everything works', 2026-10-03); finish line agreed 2026-10-03 (§1); plan: `docs/superpowers/plans/2026-10-03-milestone-7a-speed.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3.2 CPU cached interpreter, §9.5 asm ↔ C equivalence, §9.6 performance, §10 milestone 7)
**Builds on:** everything up to milestone 6c (tag `milestone-6c`); the testrunner (`--keys`, `--load-state`, `--hash`, `--audio-hash`), the 32 fingerprint ROMs, `dev.sh`'s `phone-*` commands (4d).

---

## 1. Goal and scope

Milestone 7 (ARM64 assembly speedups) is split in two (user decision 2026-10-02): **7a measuring + C speed-ups** → **7b ARM64 assembly** where 7a's measurements show it still pays.

**The goal (user decision, option A of 4):** run well on cheap and old phones — measure first on the user's real phone, speed up what matters in C (helps every device, the x86_64 emulator and the PC too), and keep assembly for 7b. Not chosen: building the original spec's asm paths regardless of need (B), measuring battery instead of speed (C), only the one or two biggest wins (D).

**Where we start (PC, optimised build, 2026-10-02, no keys pressed — intros and title screens):** game A hack 550 fps (9× real time), Mode 7 demo 868 fps (14×), game C 635 fps (10.5×), game D 455 fps (7.5×), game B 1361 fps (22×, waiting on its title screen). The first measurements were on the user's phone A (Snapdragon 8 Gen 2; small Cortex-A510 cores). **From 2026-10-03 the measuring phone is the user's phone B** (Snapdragon 845, Android 11; user decision when it was plugged in instead): its small cores (cpu0-3, Cortex-A55 at 1.77 GHz) are slower than the A510 and than the parent spec's "Cortex-A73-class" core — a pass there is a pass on cheap and old phones.

**7a finish line (agreed 2026-10-03, after the prototype's measurements — user decision: 'Lock in today's gains'):** on the **phone B's small core** (see the phone below), every speed scene runs at least 95 % of the prototype's speed: game A ≥ 1.93×, game C ≥ 3.05×, game D ≥ 1.55×, game B ≥ 1.15× (it could not keep up before: 0.84×), `m7_demo` ≥ 4.25×, `snd1_demo` ≥ 11.8× real time (prototype: 2.03 / 3.21 / 1.63 / 1.22 / 4.50 / 12.5; before 7a: 1.23 / 1.60 / 1.13 / 0.84 / 2.30 / 3.71). The ≥ 4× aim moves to 7b: what is left is the per-instruction CPU loop (40-65 % of the time) and mixing pixels (30-35 % in game A and game C) — the work assembly and NEON are for. Always: **every picture and sound fingerprint is unchanged** (CTest 120/120, the 404 unit tests, the on-device tests, the 6 speed scenes' hashes) on the PC, the x86_64 emulator and the user's phone, and the user's check passes (§7).

**Not in 7a:** assembly and NEON (7b); a JIT/dynarec (parent spec: "possible later behind the same interface"); the parent spec's on-device frame-time graph (a plain speed readout instead, §2); battery measurements.

## 2. What the user sees

- **Settings → Picture → Show speed** (off by default): a small label in a corner of the game screen, e.g. `60 fps · 1.0×`, during fast-forward e.g. `240 fps · 4.0×` (frames the core ran in the last second; × = that ÷ 59.73). Saved like the other settings.
- Games look, sound and behave exactly as before — only faster underneath (Max fast-forward is the visible difference).
- Nothing else in the app changes.

## 3. The speed tool and the scenes

- **`gbabench`** (new, `tools/bench/`): a command-line program built for the PC (optimised `build/release`) and for arm64 Android (a plain executable built with the NDK, run from `/data/local/tmp`). It reads a scene list, plays each scene, and prints per scene: frames per second and × real time for the timed part (best of N runs, default 3), and the scene's picture + sound fingerprint (the same hashes as the testrunner's `--hash` / `--audio-hash`) with `ok` / `DIFFERENT` against the expected one. `--check` exits non-zero on any `DIFFERENT`; `--only <scene>` runs one scene; `--runs <n>`; `--roms <dir>` is where the games are.
- **Scene list** `tests/bench/scenes.txt` (in git; no ROM data — names, key scripts, hashes): one scene per line — name, ROM file name, key script (the testrunner's `--keys` syntax), warm-up frames (not timed), timed frames, expected picture and sound hashes. The expected hashes are recorded once from the unchanged code (`main` at the 6c merge) and never change in 7a.
- **The 6 scenes** (user decisions: the user's games plus the heaviest demos, option B; scripted by Claude from power-on, option A): game A (the user's hack, from their `.sav`: Continue, Littleroot Town, Mom talking, the new house); game B in its first level; game C in its opening village in the storm (rain over the town, people talking — as built; a battle comes only much later in the game, so the user was not asked for a save state); game D a run of micro-games (its rotation sensor turned by the key script's `CW` / `CCW`); Tonc `m7_demo`; Tonc `snd1_demo`. Claude checks each script reaches its scene with screenshots before recording its hashes.
- **The games stay the user's:** read from `~\Downloads\` (game A `hack-1.gba`, `game-b.gba`, `game-c.gba`, `game-d.gba`); never in the repo or git; on the phone only in `/data/local/tmp/gbabench/`, deleted at the end of 7a.

## 4. The phone

- **What Claude may do on the user's phones — phone A and (2026-10-03) phone B (user decision, option B of 3; for phone B also: the user's 4 games and game A `.sav` copied to `Download/GBA` for their trial, user decision 2026-10-03):** copy and run `gbabench`, `simpleperf` and the games in `/data/local/tmp`; install the app (and, for the on-device tests, its test package, removed again afterwards). Only through `dev.sh`'s phone commands, which refuse unless exactly one USB phone is plugged in (`phone_serial`); the other commands keep using only the x86_64 emulator.
- **New `dev.sh` commands:** `phone-bench [gbabench args]` (builds `gbabench` for arm64, pushes it and the games when missing or changed, runs it pinned to the slowest core), `phone-profile <scene>` (the same under `simpleperf record`, then a report of the hottest functions on the PC), `phone-androidtest` (the on-device tests on the phone), `phone-clean` (deletes `/data/local/tmp/gbabench/`), and `bench [args]` for the PC.
- **The small core:** the last of those with the lowest `/sys/devices/system/cpu/cpu*/cpu_capacity` (or, on phones without it such as phone B, the lowest `cpufreq/cpuinfo_max_freq`), chosen at run time; `gbabench --cpu n` plays the warm-up on any core and moves to that core for the timed runs (the warm-up runs once; each timed run starts from its save state). Best of 3 runs, so a busy phone or a warm chip doesn't spoil a number.
- **Profiling:** Android's `simpleperf` (in the NDK) is refused on the user's phones (`perf_event_open`: Permission denied for programs run over USB; no setting is changed to allow it), so `gbabench --profile` samples itself (SIGPROF, 4000 per second of CPU time) and `tools/bench/profile_report.py` looks the addresses up with the NDK's `llvm-symbolizer`.

## 5. The C speed-ups (candidates)

Today every instruction goes through: an interrupt check, an event check, a DMA check, the wait-state sum (with the prefetch buffer model), a read through the general memory reader, the decode table, then the handler. The candidates, most promising first. **The prototype measured each one on the small core; the outcome (2026-10-03) is marked on each — kept ones are in the plan:**

1. **Remembered instructions (code cache)** — *not built*: the direct instruction fetch (kept, in 4) removed the memory read it would save, and the decode table costs ~3 % in the profiles. For ROM, IWRAM and EWRAM, each instruction is decoded once and its handler (and opcode) kept; later fetches skip the memory read and the table. The wait-state sum and the prefetch model still run per fetch, so timing is exact. Forgetting: a write to an IWRAM/EWRAM page that holds remembered code forgets that page's entries; a cheat's ROM patch forgets its page; loading a state, a reset and loading a ROM forget everything. The cache is never saved in a state (parent spec §6).
2. **Fewer checks per instruction** — *kept in part*: the IRQ check calls out only when a request is pending; the rest stays (the profiles show no single fat check). The interrupt / event / DMA checks run only when something could have changed them — an I/O write, an interrupt raised, a CPSR write, an event scheduled sooner, DMA becoming due — otherwise the CPU runs straight to the next event.
3. **Skipping idle waiting** — *left out*: the games sleep (halt) while they wait; spinning is 8.5 % of game B's time and under 2 % elsewhere — not worth the risk to exactness. A short loop that only reads memory and compares (no writes, no I/O side effects, the same cycle cost every turn — e.g. polling VCOUNT or a flag an interrupt sets) is skipped forward by whole turns up to the next event, adding exactly the cycles those turns take. Anything not proven safe runs normally.
4. **Faster memory reads and writes** — *kept*, with instruction fetches too (ARM conditions from a table as well). A direct path for RAM and ROM loads/stores instead of the general reader (I/O, save chips and the odd regions keep the general path).
5. **Faster drawing** — *kept* (it was 55-65 % of game A's and game C's time): text BGs a tile row at a time, the compositor looking at each layer once, unrotated bitmap lines as one row. As first planned: skip layers that are off, draw 8 pixels of a tile row at a time.
6. **Compiler settings** — *kept on Android*: link-time optimisation (+15-24 % on the games on a small core; `-O3` gave ~3 %; no clear gain on the PC, which stays as it is). As first planned: `-O3`, link-time optimisation, tuning for newer ARM cores — kept only if it helps and changes no fingerprint.

## 6. Testing

- **Every task:** CTest 120/120 (sanitizers on), the 404 unit tests (more with the new ones), Kotlin, on-device 53/53 on the x86_64 emulator, `dev.sh bench --check` on the PC (6 scenes ok), 0 C warnings.
- **New unit tests (`tests/unit/test_speedups.c`):** each fast path compared with a copy of the code it replaced, on random VRAM, registers and line buffers (text BGs, the compositor, bitmap BGs); every ARM condition with every flag combination; code rewritten in EWRAM/IWRAM (and a mirror) runs the new code; ROM reads at the end of the ROM and past it. They pass on today's code; a planted horizontal-flip bug and a planted OBJ-priority bug each made one fail.
- **On the user's phone:** `phone-androidtest` — the on-device tests including the 32 fingerprints, proving the arm64 build matches the PC exactly (until now they ran only on the x86_64 emulator; a test that turns out to depend on the emulator itself is reported to the user, not silently skipped); `phone-bench --check` — the 6 scenes' hashes match the PC's, and the speeds.
- The 'Show speed' label: a JVM test of the fps / × sum (`SpeedMeterTest`); checked by Claude on the emulator.
- **Found on the real phone:** `RewindTest.rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn` was too tight (a frame running when rewinding is asked for still finishes and counts; it failed on phone B with `main`'s code too); it now counts frames from the first rewind step.

## 7. Done when

- The finish-line speeds (§1) are met on phone B's small core by every scene (`phone-bench`), and the before/after table is recorded in `docs/PROGRESS.md`.
- Every fingerprint unchanged: CTest 120/120, the unit tests, Kotlin, on-device 53/53 on the emulator and on the user's phone, `bench --check` on the PC and the phone.
- **The user's check:** on their phone, plays game A, game C and game B with sound — they look and sound as before, no glitches — and Max fast-forward is clearly faster than before (Show speed on).
- `/data/local/tmp/gbabench/` deleted from the phone; the app stays unless the user says otherwise.

## 8. How the work goes

As in 6b/6c: **prototype → the user tries it → plan → build.** The prototype (a throwaway worktree) builds `gbabench`, the scenes and the phone commands, measures today's speed, profiles, and tries the speed-ups one at a time; Claude then shows the user the table and agrees the finish line with them. The plan is generated from the prototype's kept speed-ups and replayed before it is executed.

**7b (its own design later):** ARM64 assembly / NEON for what 7a's profiles still show as hot, each routine with a C twin (`GBA_FORCE_GENERIC`, parent spec §2) and the phone's fingerprint tests proving them identical.
