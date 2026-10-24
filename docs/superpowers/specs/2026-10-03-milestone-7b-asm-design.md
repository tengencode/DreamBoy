# Milestone 7b — Speed: ARM64 Assembly + NEON — Design

**Date:** 2026-10-03
**Status:** Implemented — milestone 7b complete 2026-10-03 (tag `milestone-7b`; the user's check on phone B: 'Everything works'). Approved design (brainstormed with the user 2026-10-03; each section approved: 'Yes, looks right'); the written spec approved by the user 2026-10-03 ('Approved'); prototyped (branch `proto-7b`) and tried by the user on phone B ('everything works', 2026-10-03); finish line agreed 2026-10-03 (§1: 'Lock in 95%'); plan: `docs/superpowers/plans/2026-10-03-milestone-7b-asm.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§2 layout and `GBA_FORCE_GENERIC`, §3.2 CPU cached interpreter and its ARM64 dispatch loop, §4.1 NEON kernels, §9.5 asm ↔ C equivalence, §9.6 performance, §10 milestone 7)
**Builds on:** milestone 7a (tag `milestone-7a`, spec `docs/superpowers/specs/2026-10-02-milestone-7a-speed-design.md`): `gbabench`, the 6 speed scenes `tests/bench/scenes.txt`, `dev.sh` `bench` / `phone-bench` / `phone-profile` / `phone-androidtest` / `phone-clean`, `tests/unit/test_speedups.c`, Show speed.

---

## 1. Goal and scope

**The goal (user decision, option A of 4):** more speed on cheap and old phones from ARM64 assembly and NEON where it pays — each piece tried and measured on phone B's small core, kept only if it is a win. Not chosen: a JIT to reach 4× everywhere (B — far more work and risk; it would be its own milestone), drawing only (C), skipping 7b (D).

**Why not 4× everywhere:** after 7a the per-instruction CPU loop is 40-65 % of the time and drawing 30-35 % in game A / game C. Claude's estimate given to the user: an assembly loop makes the CPU part ~1.3-1.5× faster and NEON the drawing part ~2-3× — game A ~3×, game C ~4.5×, game B ~1.6× (game B and game D are mostly the CPU loop). Every game already runs at full speed on phone B's small core; more speed buys faster fast-forward, battery and cheaper phones.

**Where we start (after 7a, phone B's small core cpu3, × real time):** game A 2.04, game B 1.22, game C 3.24, game D 1.63, `m7_demo` 4.48, `snd1_demo` 12.28. PC (optimised build): 17.87 / 10.95 / 26.75 / 13.36 / 41.20 / 94.80.

**Only on arm64:** the assembly and NEON. The PC (Windows x86_64) and the x86_64 phone emulator run the plain C — which the prototype also made faster (the threaded C loop, §3, is the assembly's twin; the user was told: 'the PC keeps the plain C, unchanged' no longer holds). Every assembly or NEON piece has a plain-C twin; the build flag `GBA_FORCE_GENERIC` builds the arm64 version with only the twins (parent spec §2.1).

**7b finish line (agreed 2026-10-03, after the prototype's measurements — user decision: 'Lock in 95%'):** on the **phone B's small core**, every speed scene runs at least 95 % of the prototype's speed: game A ≥ 4.42×, game C ≥ 6.08×, game B ≥ 3.34×, game D ≥ 3.19×, `m7_demo` ≥ 5.60×, `snd1_demo` ≥ 23.1× real time (prototype: 4.65 / 6.40 / 3.52 / 3.36 / 5.89 / 24.33; after 7a: 2.04 / 3.24 / 1.22 / 1.63 / 4.48 / 12.28; the C twin alone: 2.68 / 4.06 / 1.98 / 2.19 / 5.04 / 14.43). The ≥ 4× aim is met by game A, game C and the demos; game B and game D stay under it (left: sprites, the affine and bitmap BGs, I/O accesses — a few % each; a JIT was not chosen). Always: **every picture and sound fingerprint is unchanged** — on the PC, the x86_64 emulator, and phone B with the fast pieces on and with `GBA_FORCE_GENERIC` — and the user's check passes (§7).

**Not in 7b:** a JIT/dynarec, 32-bit ARM phones, battery measurements, changes the user can see in the app.

## 2. What the user sees

Nothing new in the app: games run faster underneath; 7a's Show speed label shows it (most visibly at Max fast-forward). Games look, sound and behave exactly as before.

## 3. The pieces (candidates — the prototype tries each, user decision: option A, all four)

The approach (user decision, option 1 of 3): **the CPU loop in hand-written assembly; drawing and sound with NEON through C intrinsics** (the same speed in practice, much less hand-written code). Not chosen: everything in hand-written assembly (2), no assembly at all — a threaded C loop + intrinsics (3). The prototype also tries option 3's threaded C loop side by side; if it measures as fast as the assembly loop, the simpler C is kept and the user is told. **Outcome:** the threaded C loop became the plain-C twin (`cpu_run_generic`: `musttail` handlers, specialised ARM data processing and Thumb copies, fast RAM/ROM/VRAM/palette/OAM accesses, a generic in-loop handler); the assembly beat it ×1.5-1.8 on the games, so both are kept.

1. **The CPU loop — `core/arch/arm64/cpu_run.S`.** On arm64 it replaces `run_frame`'s one-`cpu_step`-at-a-time loop (`core/src/gba.c`): runs guest instructions back to back until the next scheduler event (or a halt / stop / IRQ), with the core pointer, guest PC, the cycle count and the next event's time in callee-saved registers, and the guest flags in the host's NZCV where that pays (data processing with `ADDS/SUBS/ADCS/SBCS/ANDS…`, materialised to `cpsr` when leaving the loop). The common instructions (data processing, loads/stores to RAM and ROM, branches, the common Thumb forms) run in the loop; anything unusual — I/O and save-chip accesses, exceptions, SWI, mode and CPSR changes, halt, the odd memory regions, multiply-long, LDM/STM edge cases — calls today's C handler, so the hard parts stay written once. The wait-state sums and the prefetch-buffer model give the same cycles as the C path (per instruction, as today).
2. **Remembered instructions (the decoded cache, parent spec §3.2; 7a §5 candidate 1).** For ROM, IWRAM and EWRAM, each instruction is decoded once and its handler (and opcode / pre-extracted fields) kept; later runs skip the read and the decode. Forgetting: a write to an IWRAM/EWRAM page that holds remembered code forgets that page; a cheat's ROM patch forgets its page; loading a state, a reset, loading a ROM forget everything. Never saved in a state, never in rewind. Plain C data used by both the C path and the assembly loop. **Outcome: tried, left out** — a cache in the ARM dispatch (opcode + handler per address, checked against the opcode so it is never stale; no forgetting needed) made it slower (game B 3.52 → 3.40×, the rest 0.5-2 % slower): in the threaded loop the decode is already one table lookup by opcode bits.
3. **NEON drawing (`core/src/ppu`, an arm64 file + today's code as the twin).** Text BG tile rows (4bpp/8bpp, flips) and the compositor's pixel mixing (top-two selection, windows, alpha / brighten / darken), 8 pixels per step. **Outcome: kept** — `core/arch/arm64/ppu_neon.{h,c}`: the compositor (16 pixels a step without windows or effects, 8 with them) and whole 4bpp tile rows (the palette by table lookup; 8bpp rows stay C): game A 2.99 → 4.01×, game C 4.65 → 5.99×.
4. **NEON sound.** The core's sound mixing and/or the app's cubic resampler (`platform/android`) — only if a timing test shows it is worth at least 5 % of a frame's time on the small core; otherwise left out and recorded. **Outcome: left out** — the core's sound is 1-4 % of the time, the resampler 0.30 % of a small core.

Each piece is measured alone on phone B (`phone-bench --runs 3`); a piece that does not clearly help, or that cannot be made exact, is left out and recorded with its numbers (as 7a did).

## 4. The switch and the tools

- **`GBA_FORCE_GENERIC`** (CMake option, default OFF): on arm64, builds the core with only the plain-C twins. On x86_64 and the PC the twins are always used.
- **`dev.sh`:** `phone-bench` and `phone-profile` gain a way to build and run the generic version (`GBA_FORCE_GENERIC=ON`) next to the fast one; `phone-androidtest` likewise runs the on-device tests with either native build (a Gradle property passed to CMake). As built: `GENERIC=1` before `phone-bench`, `phone-profile`, `phone-unit`, `phone-androidtest`, `phone-install` (its own build folder `build/phone-arm64-generic`; Gradle `-PgbaForceGeneric=ON`), and `phone-unit [filter]` runs the unit tests on the phone. A failed phone build now stops the command (it used to run the old program).
- **The phone (as 7a §4):** phone B (its serial), only through `dev.sh`'s `phone-*` commands; `gbabench`, the test programs and the games in `/data/local/tmp/gbabench/`, deleted at the end; the app and its test package may be installed (the test package removed at the end). The app and the user's `Download/GBA` stay.

## 5. Testing

- **Every task:** CTest 120/120 (sanitizers on), the unit tests, Kotlin, on-device 53/53 on the x86_64 emulator, `dev.sh bench --check` on the PC (6 scenes ok), 0 C warnings.
- **On phone B, both builds** (fast and `GBA_FORCE_GENERIC`): the on-device tests (53, including the 32 fingerprints, through a save state and a rewind) and `phone-bench --check` — the 6 scenes' hashes identical to the PC's.
- **New tests that run on the phone** (the emulator is x86_64, so the asm and NEON paths only run there; through a `phone-*` command): each NEON routine against the pre-7a code on random VRAM, registers and line buffers (7a's `tests/unit/test_speedups.c`, which on the phone runs the NEON paths); the assembly CPU loop against the C loop on random ARM and Thumb instruction streams — registers, flags, CPSR, memory and cycle counts after every block of instructions (`tests/unit/test_cpu_twins.c`; it also compares the C loop with `cpu_step` on the PC). **Found on the real phone:** `MotionTest.sensorValuesReachTheCoreWithinAFrame` was too tight (100 ms after start; failed alone 8-9 of 10 on phone B, also with `main`'s code); it now waits up to 2 s for the first frame.
- **Remembered instructions** (PC and phone): code rewritten in EWRAM/IWRAM (and a mirror) runs the new code; a cheat's ROM patch and its undo run the patched / original code; a state loaded mid-game, a reset and a new ROM forget everything; the cache never reaches a save state.
- **Planted bugs:** for each piece a deliberate bug (e.g. a wrong flag, a missed forget, a flip error) is planted once to prove a test fails, then removed (as 7a).

## 6. How the work goes

As in 7a: **prototype → the user tries it → plan → build.** The prototype (a throwaway worktree) builds the switch and the tests first, then tries the pieces one at a time on phone B, profiling between them; Claude shows the user the before/after table and agrees the finish line with them; the user tries the prototype app on phone B; the plan is generated from the prototype's kept pieces and replayed before it is executed.

## 7. Done when

- The agreed finish-line speeds are met on phone B's small core by every scene (`phone-bench`), and the before/after table is in `docs/PROGRESS.md`.
- Every fingerprint unchanged: CTest 120/120, the unit tests, Kotlin, on-device 53/53 on the emulator and on phone B (fast and generic builds), `bench --check` on the PC and `phone-bench --check` on phone B (both builds), the new phone-only equivalence tests.
- **The user's check:** on phone B, plays game A, game C and game B with sound — they look and sound as before, no glitches — and Max fast-forward is faster than after 7a (Show speed on).
- `/data/local/tmp/gbabench/` and the test package removed from the phone; the app and `Download/GBA` stay.
