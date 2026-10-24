# Milestone 13 — The game check — Design

**Date:** 2026-10-05
**Status:** Implemented — milestone 13 complete 2026-10-05 (tag `milestone-13`): design approved by the user in brainstorming 2026-10-05, the written spec approved ("Spec approved"), plan `docs/superpowers/plans/2026-10-05-milestone-13-game-check.md` (11 tasks, 3 of them fixes added after the first check); 30 games checked and locked in (`tests/games/games.txt`, 31 entries); 3 emulator fixes (§10); no user check (user decision).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones)
**Next milestone:** 14 RetroAchievements (own spec after 13).

---

## 1. Goal and scope

Find and fix real problems in real games before the first public version, instead of guessing which missing hardware features matter.

**User decisions (2026-10-05):**
- The user asked to "finish emulation features" and add RetroAchievements. They are **two milestones**: **13 the game check** (first: achievements watch the game's memory, so more exact emulation means fewer achievements that fail to unlock), **14 RetroAchievements**.
- Of the known gaps (mid-line drawing, the built-in BIOS's sound calls, green swap / DMA3 video capture, the last mGBA timing tests), Claude's assessment was that none is clearly worth it for v1 on its own. The user chose **a game check + fixes** instead: test many games, fix the real problems; a known gap is built only if a game needs it.
- **Games:** the user's own GBA games **and** free homebrew games (Claude asks once, with the list and sizes, before downloading).
- **Automatic only — no user check.** Claude judges every game.
- **Judging:** pictures + a warning log (§3, §4). If a picture looks doubtful and Claude can't tell, Claude asks to download mGBA just for that case.
- **Small fixes** are made directly. **A big one** (e.g. mid-line drawing) is shown to the user first, who decides: now, later or never.
- RetroAchievements is the one exception to the "no third-party sign-ins" rule (2026-10-04) — milestone 14, not here.

**Out of scope:** the link cable; the app (Kotlin) — unless a game problem turns out to be in the app's side of the core (sound pacing, sensors); RetroAchievements (14).

## 2. The games

- **The user's games** (read from `$GBA_GAMES`, default `~/Downloads`, as `dev.sh bench` does; never copied into the repo): `hack-1.gba`, `hack-2.gba`, `game-b.gba`, `game-c.gba`, `game-d.gba`, and the `.gba` inside `hack-3.zip`, `hack-2b.zip`, `hack-4.zip`, `hack-5.zip`, `hack-6.zip`, `hack-7.zip` (the game A hacks share one engine; they count as one kind). The test runner reads only `.gba`, so the game check unpacks a zipped game into `build/check/games/` (gitignored). Any GBA game the user adds to Downloads later joins the list.
- **About 20–30 free homebrew games and demos**, picked by Claude to cover what the user's games don't: every picture mode (0–5), rotation/scaling, mosaic, blending and windows, many sprites, H-blank effects, each sound kind (Direct Sound with different sound engines, the PSG channels), each save chip (SRAM, Flash 64/128 KB, EEPROM), and anything using the BIOS's sound calls. Sources: free, legal downloads (e.g. the gbadev.net game-jam entries). Claude shows the whole list with sizes and asks once before downloading.
- Homebrew files go in `tests/roms/homebrew/` (gitignored, like all ROMs), fetched by `tools/fetch_test_roms.py` with a pinned SHA-256 each, so the same file is always tested.

## 3. The warning log (core)

A record of everything a game uses that the emulator doesn't fully support, so hidden gaps show up even when the picture looks fine.

- **Only in a new PC build, `build/check`** (Release + `GBA_NOTES` defined). The phone app, the `build/host` debug build and `build/release` (the viewer and the speed check) are compiled without it — **no speed cost and no change to the app**.
- **What it records** (each kind once per game with the first frame it happened and a count):
  - a BIOS call the built-in BIOS doesn't do (the sound-driver and multiboot calls, 0x1A–0x2A and any unknown number) — only when the built-in BIOS is used;
  - a picture register or palette written during the visible part of a line (a mid-line change);
  - green swap switched on; a DMA3 video-capture transfer started;
  - a read or write to an I/O address with no register;
  - the CPU running from empty memory or an undefined instruction;
  - anything else found while building the tool that the emulator skips on purpose (listed in this section when added).
  - **Added in Task 5 (2026-10-05): the game's own messages.** Decomp hacks and homebrew report failed internal checks through mGBA's debug-print registers (text at `0x04FFF600`, `level | 0x100` written to `0x04FFF700` sends it; `0x04FFF780` is the "is this mGBA?" probe; real hardware ignores all of them). The log keeps up to 16 messages (once per text and level, with first frame and count; `gba_debug_prints()`), the test runner prints them as `PRINT <level> <first frame> <count> <text>`, and the game check flags levels 0–1 (fatal, error) as "game error". The probe is not answered, so games behave as on hardware. These addresses no longer count as io-unused.
- The log is printed by the test runner (`--notes`) as plain lines, and the game check (§4) keeps it per game.

## 4. The game check tool

- **`bash tools/dev.sh games`** builds `build/check` and runs `tools/game_check.py`, which plays every game in the list (§5) through the test runner with its button script, for about 3 minutes of game time each (at ~500 fps that is ~20 s per game).
- **Per game it keeps:** a picture every few seconds (test runner `--shots-every <frames> --shots-dir <dir>`), the sound, the warning log, and these flags:
  - **freeze:** the picture doesn't change for 10 s of game time after the first 20 s while the script is pressing buttons;
  - **black screen:** the whole picture one colour for 10 s or more;
  - **silence:** no sound in the whole run (a hint to look, not a failure by itself);
  - **crash:** the CPU ran from empty memory or hit an undefined instruction (from the log).
- **The report:** one contact sheet per game (a PNG grid of its pictures, made with Pillow, already installed) and one page `build/check/report/index.html` with the sheets, flags and logs. Claude looks over every sheet; the user can open the page too.
- Reports show the user's games, so they stay in `build/` (gitignored).

## 5. The game list and the lock-in check

- **`tests/games/games.txt`** (in git, no game data), one game per line, like `tests/bench/scenes.txt`: name | file | where it came from (`user` or the homebrew URL) | SHA-256 | save file or `-` | button script | frames | picture hash | sound hash. Hashes are `-` until the game passes; then they are locked.
- Button scripts are written by Claude from power-on, reaching real play: past the title, into a level or a menu that moves. The list's script language (expanded by `game_check.py` into the test runner's `--keys`): `F:KEYS` holds from frame F; `A-B/N:KEYS` taps every N frames from A to B; `A-B/N/D:KEYS` holds each tap D frames (added in Task 5); `CW` / `CCW` turn the rotation sensor while held, as in `gbabench` (added in Task 5 for game D).
- **`bash tools/dev.sh games --check`** replays every listed game it finds and compares the hashes and the warning log with the locked ones. A missing file is reported as `skipped (missing)`, never silently. From the first locked game on, it is part of every task's checks (like `bench --check`).

## 6. Fixing, and the safety rules

- **Every problem found** goes into §10 of this spec: the game, what's wrong, the cause, and what was done. Each fix is test-first: a small unit test (or a test-ROM fingerprint) that shows the bug fails, then the fix.
- **Nothing that works now may break:**
  - CTest stays 120/120 (plus any new tests), all unit tests pass, Kotlin and on-device tests pass;
  - the mGBA suite scores in `tests/suite_targets.json` don't go down;
  - **speed:** `dev.sh bench --check` on the PC stays at ≥ 97 % of today's frames per second in every scene. If a fix touches the fast parts (the CPU loop, drawing, `core/arch/arm64`), phone B is measured too (`phone-bench`), and the fast and `GBA_FORCE_GENERIC` builds must still match — Claude asks the user to plug in the phone at that point;
  - **save states keep loading** — the user's Continue state and slots. If a fix needs new saved data, the loader learns to read the old chunk version and fills the new fields with their power-on values. Before any fix, a state from today's build of one of our own test ROMs (`tests/own_roms/`) is committed as a test file (`tests/states/`, a gitignore exception) and a test proves it still loads.
- **Approved fingerprints** (`tests/expected_frames.json`, audio fingerprints, bench scene hashes): if a fix changes one, Claude compares old and new. Same to the eye or ear → re-approved and written down in PROGRESS.md. Visibly different → Claude checks which matches the hardware documentation (GBATEK) and writes down why; if Claude can't tell, the user is asked.
- **A big fix** (more than about a day of work, or a risk to speed, e.g. mid-line drawing, the BIOS sound driver) is shown to the user first with what it would fix; they decide now / later / never.

## 7. Order of work

1. The warning log and `build/check`.
2. The test runner's pictures-along-the-way, and the game check tool and report.
3. The game list with the user's games; the saved-state test file.
4. The homebrew list → the user's OK → the downloads.
5. The first full check; the problems list (§10). Claude adds one plan task per problem and tells the user how many.
6. The fixes, one per task.
7. Lock in every game; milestone 13 exit.

The plan covers 1–5 and 7; the fix tasks (6) are added to the plan after step 5.

## 8. Finish line

Milestone 13 is done when:
- every game on the list starts, gets into play and runs its button script with no crash or freeze;
- the pictures look right to Claude;
- everything left in the warning log is fixed or written down in §10 with why it's harmless;
- each game is locked in (`games --check` passes);
- all the old tests pass, the mGBA suite scores haven't gone down, and the PC speed is ≥ 97 % of today's;
- no user check (user decision).

## 9. Testing

- New unit tests for the warning log (each kind recorded once, with its frame and count; nothing recorded by a normal ROM such as the jsmolka tests) and for each fix.
- The test runner's new options checked on a Tonc demo (pictures written at the right frames).
- The saved-state test file (§6).
- `games --check`, `bench --check`, CTest, unit, Kotlin and on-device runs at every task, as in earlier milestones.

## 10. Problems found

The first full check (plan Task 7, 2026-10-05): 30 games (the m7 demo, the user's 10, 19 homebrew), 3 minutes each, every contact sheet, flag and warning judged by Claude. Three problems in the emulator were found and fixed; the rest are explained below.

### Fixed

1. **The built-in BIOS skipped MidiKey2Freq (SWI 0x1F)** — game D calls it ~10,000 times in 3 minutes; without a BIOS file its sound driver got the WaveData pointer back as a "frequency" and its music played ~4 octaves low (most common pitch −29/−28 semitones vs the real BIOS's +23). Now answered in whole numbers like the BIOS, **exact against the user's real BIOS on 1,700 inputs**; the music's pitches now match the real BIOS's. The bench `game_d` scene was re-approved: the game waits for its jingles, so its timing changed too (plan Task 8).
2. **BitUnPack, ObjAffineSet and BgAffineSet gave different results from the real BIOS** — found by the BIOS twin tests (plan Task 9: every built-in BIOS routine against the user's real BIOS on random inputs). BitUnPack cut each unit to the output width (the BIOS lets an overflowing unit spill into the next); the affine sets used a rounded sine table (the BIOS's is cut toward zero — all 256 entries checked), ±1–2 in the matrix. All exact now; no fingerprint changed. Every other routine already matched exactly: CpuSet, CpuFastSet, LZ77 and RL (WRAM and VRAM), the three Diff filters, Div, DivArm, Sqrt, ArcTan, ArcTan2, RegisterRamReset.
3. **The tool:** palette writes from the fast CPU loop were missing from the warning log (plan Task 10); a game's own messages with non-UTF-8 bytes stopped the game check (Task 6); the freeze rule skipped long unchanged stretches that began in the first 20 s (Task 4).

### Explained — not emulator problems

- **hack 2 — the game's own heap check fails** (`ASSERTION FAILED src/malloc.c` 119/120/143, then its deliberate `0xEFFF` breakpoint). Traced: the game frees a list node at frame 7223 while it is still linked, then frees it again at 7229 (a double free in the hack). With the built-in BIOS it failed in 8/8 runs with the script shifted 0–49 frames, with the real BIOS in 0/16 — **but** the real BIOS's boot animation starts the game 272 frames later, so every button landed elsewhere; with the script delayed by those 272 frames, **the real BIOS fails the same way in 4/4 runs** (frame 7501 = 7229 + 272). A bug in the hack, triggered by that button sequence. (With the real BIOS the breakpoint then runs into empty memory — the game would crash on hardware; our built-in BIOS steps over it and the game carries on.)
- **Healz — "panicked … Ran out of video RAM for tiles"** (the game's own crash screen, ~frame 3600; also with the real BIOS): a known leak in its engine, agb 0.20, when screens are switched again and again (agbrs/agb discussion #730) — which the random script does.
- **Freeze flags:** a scripted pause in the game A hacks' legendary cutscene (`hack_1_new`, `hack_4`, 9120–9720); BlindJump's link-cable screen waiting for a partner (7500–8700, its message "no valid handshake"); Advancetris's game-over board; Solar Guard's "Press Start" title until the script's next START; earlier ones were script faults, fixed in the scripts (Celeste paused by START, `hack_1_save` not walking out of the truck, hack 2 / hack 3 settings screens needing R / DOWN + A, Dungeon Master's menu confirming with SELECT — read from its source).
- **Silence:** m7_demo (no sound) and Voxelburg (its jam version links Maxmod but never starts it — checked in its source).
- **BPCore Engine** (dropped from the list): an empty engine that shows nothing until a game is appended (its README).
- **The game's own messages:** Skyland and hack 3 print info lines (more than 16 → `notes-dropped`); Skyland even checks the BIOS and prints "BIOS matches GAMEBOY Advance" with the built-in one; BlindJump's "existing tile is a glyph, but has no mapping table entry" comes from the script wandering into its Russian menu (a font gap in the game).
- **The game A hacks' writes to `0x04FFF780`:** their "is this mGBA?" probe (mGBA's debug-print registers); real hardware ignores them, so do we (now part of the game's-own-messages capture).
- **Mid-line writes:** every game writes picture registers or the palette while a line is drawn, mostly at start-up and in fades (palette); the most is Skyland, ~17 BG1 scroll writes per frame (consistent with a few screen splits for its parallax sky). On hardware such a write changes the rest of that one line; our line-at-a-time drawing applies it to the whole line — at most one partly wrong line per write, nothing visible on the contact sheets. **Mid-line drawing stays out of v1.**

### Known differences, not fixed (no game showed a problem)

- **The built-in BIOS's speed** (measured by the twin tests, cycles per call, built-in vs BIOS): CpuSet 2,590 vs 4,086; CpuFastSet 2,999 vs 2,337; LZ77 to WRAM 3,068 vs 12,985 and to VRAM 1,146 vs 19,888; RL 5,215 vs 13,905 and 2,401 vs 20,315; Diff8 6,339 vs 17,778 and 3,739 vs 22,446; Diff16 3,220 vs 8,939; BitUnPack 511 vs 3,905; RegisterRamReset 97 vs 257,192; ObjAffineSet 131 vs 233; BgAffineSet 165 vs 312; ArcTan2 133 vs 366 (Div, Sqrt and ArcTan were tuned to the mGBA suite in 3c-2). Mostly faster than the real BIOS: loading is quicker and games get a little more CPU time per frame. A possible later task: match them with the twin tests.
- **Not twin-tested:** HuffUnComp (no random tree generator; rare in games); the sound-driver SWIs other than MidiKey2Freq (0x1A–0x1E, 0x20–0x2A) are still skipped — none of the 30 games called them (the warning log would show it).
- **The built-in BIOS steps over undefined instructions;** the real BIOS's handler does not return the same way (hack 2's breakpoint sends it into empty memory). Kept: being forgiving loses nothing for players.
