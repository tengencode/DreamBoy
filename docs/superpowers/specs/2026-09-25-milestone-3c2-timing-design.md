# Milestone 3c-2 — Right Timing (CPU cycles, prefetch buffer, timers, interrupts, DMA) — Design

**Date:** 2026-09-25
**Status:** Implemented — milestone 3c-2 complete 2026-09-26 (tag `milestone-3c2`); finish line revised 2026-09-25 (§1); tuning results in §9; plan: `docs/superpowers/plans/2026-09-25-milestone-3c2-timing.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3.1 scheduler, §3.2 CPU cycle accounting, §3.3 wait states + prefetch buffer, §4.3 DMA, §4.4 timers/IRQ, §10 milestones)
**Builds on:** milestone 3c-1 (tag `milestone-3c1`): suite tooling (`--suite`, `tests/suite_targets.json`), open bus, I/O read rules, link port.

---

## 1. Goal and scope

Make the emulator spend the same number of cycles as the hardware where the mGBA test suite measures it: CPU instruction timing, the cartridge prefetch buffer, timer and interrupt delays, DMA, and the H-blank flag.

**User decisions (2026-09-25):**
- ~~Finish line: Timer count-up, Timer IRQ, DMA, Misc and SIO timing at 100%; Timing ≥ 1708/2020.~~ **Revised while planning (user decision, 2026-09-25): mGBA-level.** A prototype of this design (§8) showed that 100% on the timer groups needs per-bus-cycle timing: even mGBA scores 610–744/936 on Timer count-up and 70/90 on Timer IRQ (suite author's leaderboard). New finish line: **Timing ≥ 1708/2020 (prototype: 1796), DMA 1244/1244, Timer IRQ ≥ 70 (prototype: 71), Timer count-up as high as a time-boxed tuning task gets it, aiming ≥ 610 (prototype: 513), Misc and SIO timing as high as that task gets them (prototype: 6/12, 0/4)**; the scores reached are locked as targets, and the checks still failing are written down as a possible later cycle-exact pass. The 3c-1 groups stay at 100%.
- **Approach: per-instruction timing like mGBA** (datasheet cycle counts per instruction, mGBA's prefetch-buffer estimate, timer/IRQ delays) — not a per-bus-cycle model. Anything still failing after that gets a targeted fix; a check that proves unreasonably hard is shown to the user, who decides.
- **Changed fingerprints:** when a timing fix changes an approved frame or audio fingerprint, Claude compares the new picture/sound with the old one and the reference; if it is the same to the eye/ear (e.g. a counter one tick different), Claude re-approves it and lists it in PROGRESS.md; anything that looks or sounds different is shown to the user before re-approval.
- **Speed guard:** the optimised (release) build must stay at least 10× real time headless on the PC (it is ~16× at the start of 3c-2: ~970 fps on Tonc `m7_demo` and on the suite).

**Baseline (start of 3c-2, 2026-09-25):**

| Group | Score | Main causes found while brainstorming |
|---|---|---|
| Timing | 362/2020 | even "Calibration" (the empty test) fails: no prefetch buffer (all "P" rows), and the wrong N/S charge after loads/stores (changing the N setting has no effect, changing S has too much) |
| Timer count-up | 342/936 | timer start/read timing (values 1 tick off) |
| Timer IRQ | 0/90 | the IRQ is taken too early (every check reads the same 0x44) and the timer stops 13 cycles earlier than on hardware (0x44 vs 0x51): no IRQ delay, missing pipeline-refill cycles |
| DMA | 1184/1244 | 48 checks: DMA from cartridge ROM must always increment the source; 12 checks: DMA from the BIOS area (`R+0x10`) expects 0xDEADDEAD |
| Misc edge cases | 3/12 | DMA-time open bus ("DMA Prefetch": hardware breaks at 0x10002A94 reading 0xDEAD0000); H-blank flag 3 cycles off (hardware 0x4D0, ours 0x4D3) and its flips. **Note:** this group's log prints "Got <hardware> vs <ours>" (the suite swaps the arguments of `doResult` here) |
| SIO timing | 0/4 | every check 24 cycles short = interrupt/BIOS overhead |

| In scope | Out of scope |
|---|---|
| Per-instruction cycle counts (ARM7TDMI datasheet): pipeline refill after branches/exceptions, N fetch after data accesses | Per-bus-cycle CPU model, exact prefetch-unit simulation (NanoBoyAdvance style) |
| Prefetch buffer (WAITCNT bit 14), mGBA's estimate | VRAM/palette/OAM access stalls during rendering |
| Timer start delay, timer read timing, cascade timing | Mid-scanline register changes (rendering still happens once per line) |
| IRQ delay, Halt wake timing, BIOS function times where a check depends on them | Real-BIOS cycle-exact SWIs beyond what checks need |
| H-blank flag / IRQ timing within the line | |
| DMA: ROM source always increments, DMA open bus, BIOS-area source, transfer cost | DMA3 video capture (parent spec: out of v1) |

---

## 2. CPU cycle counts

- Each instruction costs what the ARM7TDMI datasheet says (mGBA uses the same table): data processing 1S (+1I with a register shift); B/BL/BX and exception entry 2S+1N; LDR 1S+1N+1I; STR 2N; LDM nS+1N+1I; STM (n−1)S+2N; multiplies 1S+mI (+1I MLA, +1I long); SWP 1S+2N+1I; an instruction that writes PC adds the refill (1S+1N).
- **Change 1 — pipeline refill:** after a branch or exception entry the CPU refills its pipeline: the fetch at the target (N) and the next one (S). Today only the first is charged; the second is added.
- **Change 2 — N fetch after a data access:** after any data load/store, the next code fetch is non-sequential (on the cartridge that costs the N wait, not the S wait). Today it is charged as S.
- Per-region wait states, the internal cycles already charged (multiplies, shifts, loads), and the frame loop stay as they are.
- **Tests:** a unit test per instruction family counts cycles exactly — from IWRAM (1-cycle, no waits) and from ROM with different WAITCNT settings, so N/S charges are visible. The suite's Timing "Calibration" rows without "P" are the first real-world check.

## 3. Prefetch buffer

- On hardware, with WAITCNT bit 14 set, while the CPU does something that does not use the cartridge (internal cycles, RAM/I/O data accesses), the prefetch unit reads the next halfwords from the cartridge into an 8-halfword buffer; later code fetches that hit the buffer cost 1 cycle.
- **Model (mGBA's `GBAMemoryStall` estimate):** when an instruction running from ROM spends cycles off the cartridge, compute how many sequential halfwords (at the ROM's S wait) fit into that time, up to 8 minus those already buffered; the following code fetches that hit them are charged as buffer hits, and the next fetch's N becomes an S. Only when bit 14 is set and the code runs from ROM.
- The buffer empties on a branch, on a data access to the cartridge, and on a WAITCNT write.
- **Tests:** unit tests with the buffer on and off (a loop of RAM loads running from ROM, exact cycle totals); the suite's "P" rows.

## 4. Timers, interrupts, H-blank flag

- **Timer start delay:** starting a timer (and changing its settings) takes effect a short, fixed number of cycles after the write (expected 2; pinned from GBATEK/mGBA/the suite while planning).
- **Timer read timing:** a timer read inside an instruction sees the count at the cycle the hardware reads it (mGBA offsets reads by 2 cycles), not at the start of the instruction.
- **Cascade:** a count-up timer ticks when the previous timer overflows, with the same delays; checked by the Timer count-up group.
- **IRQ delay:** after an interrupt is flagged (IF & IE, IME), the CPU notices it only a few cycles later (mGBA: 7). Waking from Halt follows the same rule. IRQ entry and the BIOS IRQ handler then cost their real cycles through §2 — expected to close the Timer IRQ values and the 24-cycle SIO timing gap.
- **BIOS function times:** the HLE SWIs currently charge a flat 20 cycles; where a check depends on one, it charges mGBA's measured time instead (only where needed).
- **H-blank flag:** DISPSTAT's H-blank bit and the H-blank IRQ switch at the cycle within the line the hardware uses (the misc group shows a 3-cycle difference); numbers pinned from GBATEK/mGBA while planning. Rendering is unchanged (still once per line), so pictures do not change because of this.

## 5. DMA and the misc checks

- **ROM source:** a DMA whose source is in cartridge ROM always increments the source, whatever the source address control says.
- **DMA open bus:** unmapped reads during a DMA — and by the first instruction right after one — return the DMA's last transferred value (mGBA's rule), not the CPU prefetch. This is the misc group's "DMA Prefetch" check.
- **BIOS-area source (`R+0x10`):** the expected 0xDEADDEAD is explained while planning (latch/open-bus rule) and implemented.
- **Transfer cost:** checked against GBATEK (2N + 2(n−1)S + xI, ROM-to-RAM, start delay) and the DMA/Timing groups; fixed where different.
- **SIO timing:** expected to follow from §2/§4; otherwise a targeted look.

## 6. Testing

1. Unit tests first for every rule (cycle counts per instruction family, prefetch on/off, timer start/read/cascade, IRQ delay, H-blank flag cycle, DMA ROM source, DMA open bus).
2. `tests/suite_targets.json` raised after each task; minimums never go down; the 3c-1 groups stay at their totals.
3. All earlier CTest entries pass with ASan + UBSan; changed frame/audio fingerprints handled per §1.
4. Release build ≥ 10× real time headless (Tonc `m7_demo`, suite).
5. **User checks:** the suite in `gbaview` (3c-2 groups at their finish-line scores), then, with the user's OK, their game A hack (menu, controls, sound, saving).

## 7. Done when

- Revised finish line (§1): Timing ≥ 1708/2020, DMA 1244/1244, Timer IRQ ≥ 70, Timer count-up / Misc / SIO timing at whatever the time-boxed tuning task reaches (aim: count-up ≥ 610), locked in `tests/suite_targets.json`; the 3c-1 groups still at 100%; the still-failing checks listed for a possible later cycle-exact pass.
- All unit tests and CTest entries pass; sanitizers report nothing; the speed guard holds.
- The user's checks pass.

## 8. Prototype results (measured while planning, 2026-09-25)

The design was prototyped in a throwaway copy of the code (release build, full mGBA suite) before the plan was written. Cumulative scores after each planned task:

| After | Timing | Count-up | Timer IRQ | DMA | Misc | SIO timing |
|---|---|---|---|---|---|---|
| start | 362 | 342 | 0 | 1184 | 3 | 0 |
| CPU cycle counts (§2) | 1048 | 367 | 0 | 1184 | 3 | 0 |
| + timer start delay 2 (§4) | 1060 | 410 | 44 | 1184 | 3 | 0 |
| + BIOS routine times (§4) | 1200 | 410 | 44 | 1184 | 3 | 0 |
| + DMA timing and ROM source (§5) | 1376 | 410 | 44 | 1244 | 3 | 0 |
| + prefetch buffer (§3) | 1796 | 410 | 44 | 1244 | 3 | 0 |
| + IRQ delay 5 (§4) | 1796 | 513 | 71 | 1244 | 3 | 0 |
| + H-blank at cycle 1008 (§4) | 1796 | 513 | 71 | 1244 | 6 | 0 |

Rules pinned by the prototype (they replace the "pinned while planning" placeholders above):
- **CPU:** a branch or exception entry charges a pipeline refill of N at the target plus S after it, and the next fetch is then sequential; after any data access **or a multiply**, the next fetch is non-sequential; in LDM/STM an access that crosses into a new 16 MiB region is non-sequential.
- **Timers:** a started timer counts from 2 cycles after the enabling write; reads sample at the end of the access (no extra offset).
- **BIOS routines:** Div/DivArm/Sqrt/ArcTan/ArcTan2 charge mGBA's measured formulas minus 8 cycles (our call path already spends those); CpuSet/CpuFastSet charge 8 plus one internal cycle per unit; other SWIs keep 20.
- **DMA:** an immediate transfer takes the bus 3 cycles after the enabling write; the CPU keeps running until then, and any CPU bus access from that moment waits for the transfer; a transfer ends with 2 extra cycles unless both source and destination are cartridge ROM; a cartridge-ROM source always increments. The "DMA bus value on open bus" rule gave no score and was left out.
- **Prefetch:** mGBA's `GBAMemoryStall` estimate, applied once per instruction that ran from the cartridge with WAITCNT bit 14 set and spent cycles off the cartridge (RAM/I/O data access or multiply).
- **IRQ:** a request (IE & IF becoming non-zero, or IME switching on while one is pending) reaches the CPU 5 cycles later (sweep 2–8: 5 best; 8 gives SIO timing 4/4 but costs ~110 count-up checks); IRQs flagged by events count from the event's own time; a halted CPU wakes when the request arrives (not earlier — waking early made the count-up group hang in `IntrWait`); IRQ entry charges the pipeline refill.
- **H-blank:** DISPSTAT's H-blank flag and H-blank DMA at cycle 1008 of the line (mGBA), the H-blank IRQ requested 6 cycles earlier.
- Only two approved fingerprints change: the audio of `own_snd_psg` and `tonc_snd1_demo` (sound writes land a few cycles later); all 26 frames and the other audio fingerprints are unchanged. Release speed: ~790 fps on Tonc `m7_demo` (13× real time, guard 10×).
- Still failing in the prototype: Timing's ROM-to-ROM DMA rows (±1 cycle) and some prefetch rows; count-up/IRQ sub-instruction timing; misc "DMA Prefetch" (DMA-time open bus) and H-blank flip cycles (±1–2); SIO timing (24-cycle gap at IRQ delay 5).

## 9. Tuning results (Task 8, 2026-09-25)

The time-boxed tuning task tried the plan's five ideas one at a time, scoring all 13 suite groups after each (release build), and kept a change only if no group went down.

| Idea | Result |
|---|---|
| 1. Prescaled timers tick on a shared grid (mGBA aligns the start to the prescaler) | **Kept** (start = (write + 2) rounded down to the prescaler period): count-up 513 → 603. Rounding the write time itself scored the same; rounding up scored 507. Keeping the tick phase on settings-only rewrites (no restart) changed nothing and was left out. |
| 2. Stop write freezes the count 1 cycle earlier/later | Dropped: count-up 603 → 492, Timer IRQ 71 → 65 (both directions). |
| 3. Longer wake delay for a halted CPU | **Kept: +3 cycles** after a running CPU would see the request (`HALT_WAKE_DELAY`). +1: 611, +2: 650, +3: 690 and SIO timing 4/4, +4: 695 but SIO timing 0. On its own +3 cost one Misc check (Flip 4). |
| 4. H-blank flag edges | A flag computed at the exact read cycle (instead of when the event runs) lowered Misc for every set/clear offset tried. **Kept instead: the H-blank IRQ is requested 7 cycles before the flag** (was 6) — with idea 3 this keeps Misc at 6 (−4: 4, −5: 5, −7/−8: 6). |
| 5. SIO timing re-check | 4/4 with idea 3. |

**Final scores:** Timing 1796/2020 (finish line 1708), DMA 1244/1244, Timer IRQ 71/90 (≥ 70), Timer count-up **690/936** (aim ≥ 610; mGBA 610–744), Misc 6/12, SIO timing **4/4**; the 3c-1 groups still full.

**Still failing (a possible later cycle-exact pass):**
- Timing (224): 208 cartridge rows with the prefetch buffer ("P" rows — the buffer is mGBA's estimate, not a simulation) and 16 cartridge rows without it.
- Timer count-up (246) and Timer IRQ (19): values 1–3 ticks off — sub-instruction timing of the timer reads/writes.
- Misc (6): "DMA Prefetch" (Break/Read — the DMA-time open-bus value), "Hblank" (ours 1235 vs 1232 cycles between two H-blank wakes) and Flips 1–3. Traced: the two wakes are exactly 1232 cycles apart; the second path to the timer read runs three more 1-cycle Thumb instructions (`mov r3,#0; mov r1,#2; mov r0,#2`), which hardware hides — a bus-cycle effect this model does not have.
