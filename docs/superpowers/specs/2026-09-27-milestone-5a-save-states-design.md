# Milestone 5a — Save states and the in-game menu — Design

**Date:** 2026-09-27
**Status:** Implemented — milestone 5a complete 2026-09-27 (tag `milestone-5a`). Approved design (brainstormed with the user 2026-09-27); prototyped and tried by the user; plan: `docs/superpowers/plans/2026-09-27-milestone-5a-save-states.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3 core API, §6.1 save states, §5.5 lifecycle, §7 app shell, §8 errors, §10 milestones)
**Builds on:** milestone 4 (tags `milestone-4a`…`-4d`: the Android app, the emulation thread with its waiting pause, save files written safely by `save_file`, Continue, the Back question, `GameId`); the core as of milestone 3 (tag `milestone-3c2`).

---

## 1. Goal and scope

Milestone 5 is split in three (user decision 2026-09-27): **5a save states** → **5b fast-forward + rewind** (rewind is built on 5a's states) → **5c cheats**.

**5a finish line:** on the Pixel 9 emulator the user, in their game A hack, saves a state into a slot, plays on, loads it and the game is back at that moment and plays on normally; after Home or Android closing the app, **Continue** brings them back to the exact moment; **Restart game** starts from the title screen. Automatic tests prove that a loaded state continues **exactly** like the original: for all 32 approved test ROMs, running on from a state loaded into a brand-new core gives the **approved picture and sound fingerprints** — on the PC and on the emulator.

**User decisions (2026-09-27):**
- **10 slots per game**, shown as a **grid of pictures** (two columns; picture, number, date/time or "empty").
- An **automatic state** when the user leaves or quits; opening that game again resumes the exact moment. **Restart game** in the menu starts from the title.
- **Back opens the in-game menu** (Resume, Save state…, Load state…, Restart game, Quit) — it replaces 4c's "Quit the game?" question; Quit no longer asks (the automatic state keeps the moment).
- **Ask before overwriting a used slot** ("Replace slot 3?"); loading happens at once.
- **A state includes the game's save chip**; loading an older state rolls the in-game save back too (like mGBA); the app then rewrites the save file, the previous one kept as `.sav.bak`.
- **Format: one chunk per component, holding that component's struct as bytes** (option 1 of 3): little code and exact by construction (everything a component remembers is in its chunk), each chunk tagged with its size and a version; after a core change to a component, older states of it are refused with a clear message (game save files are never affected). (Rejected: hand-written field-by-field serialisers — much more code, and a forgotten field silently breaks exactness; one copy of the whole `gba_t` — the ROM pointer and layout make it fragile.)

| In scope (5a) | Later |
|---|---|
| Core: `gba_state_size`, `gba_state_save`, `gba_state_load` (atomic), `gba_rom_crc32` | Compression (with rewind, **5b**) |
| Test runner `--state-at <frame>` + 32 CTest `state_*` replays | Fast-forward, rewind (**5b**); cheats (**5c**) |
| 10 slots per game with pictures; "Replace slot N?" | Export/share states, a game library (milestone 6) |
| Automatic state on leave/quit, resumed on open; Restart game | Save states in `gbaview` |
| In-game menu on Back | |

## 2. Files

```
core/include/gba.h                 + gba_state_size / gba_state_save / gba_state_load, gba_rom_crc32, GBA_ERR_STATE
core/src/state.c                   the format: header, chunk table, save, validate-then-load
core/src/cart/cart.c               + CRC32 of the ROM at load
tools/testrunner/main.c            + --state-at <frame>
CMakeLists.txt                     + state_<name> tests (every frame_<name> entry of tests/expected_frames.json)
tests/unit/test_state.c            PC tests of the format
platform/android/emu_thread.{h,c}  + emu_state_save / emu_state_load (only while parked), save chip marked dirty after a load
platform/android/jni_bridge.c      + saveState(path), loadState(path), frameBgr555(); test hooks
app/src/main/java/com/tengen/gbaemu/
  States.kt          the files of a game's states (slots 1-10 + auto, .state + .png), dates, the picture PNG from a frame
  Screens.kt         + GameMenu (Resume / Save state… / Load state… / Restart game / Quit), SlotGrid, the replace and
                       restart questions
  MainActivity.kt    + menu on Back, automatic state on leave/quit and on open, Restart game
  NativeBridge.kt    + the new functions
app/src/androidTest/.../StateTest.kt   on-device: round trip, automatic state, refusals, the 32 replays
```

## 3. The state format (core)

- **Header** (little-endian): `"GBAS"`, `format_version` u32 = 1, `rom_crc32` u32, `chunk_count` u32.
- **Chunks:** `{ tag char[4], version u32, length u32, payload }`, in a fixed order: `CPU ` (`arm_cpu_t`), `BUS ` (`bus_t`), `IO  ` (`io_t`), `PPU ` (`ppu_t`, incl. the framebuffer and affine references), `IRQ ` (`irq_t`), `TMR ` (`timers_t`), `DMA ` (`dma_t`), `APU ` (`apu_t`), `SIO ` (`sio_t`), `SCHD` (`sched_t`), `CART` (`cart_t` with the ROM pointer and size cleared: the save chip, GPIO, RTC, sensors, tilt), `MISC` (`now`, `frame_done`, `hle_bios`, `keyinput`).
- **Version** of a chunk = a per-component constant (bumped by hand when a struct's meaning changes) combined with `sizeof` of the struct; **length** = `sizeof`. A state is loadable only if every chunk's tag, version and length match this build exactly.
- **Save:** `gba_state_size` (~650 KB) and `gba_state_save(g, buf, cap)` (0 if `cap` is too small).
- **Load:** `gba_state_load(g, buf, len)` first validates everything — magic, format version, `rom_crc32` = the loaded ROM's, chunk count, every tag/version/length, the total length — and only then copies the chunks in, restoring the ROM pointer and size in `cart_t`. Any mismatch → `GBA_ERR_STATE` and **the core is unchanged**.
- `gba_rom_crc32`: CRC-32 (the zlib polynomial) of the ROM as loaded, computed once in `cart_load`.

## 4. The app

- **Files:** `<app files>/states/<game id>/slot1.state … slot10.state`, `auto.state`, each with a `.png` beside it (240×160, from the frame at saving time via `frameBgr555()`). Written with `save_file_write` (temporary file → swap).
- **Saving/loading only while paused:** `emu_state_save/load` refuse unless the emulation thread is parked (after `emu_pause`), or not yet started; the menu pauses the game first. After a load the thread marks the save chip dirty, so the `.sav` is rewritten ~1 s later (the old one → `.sav.bak`).
- **In-game menu** (Back, or Esc on a keyboard): the game pauses (sound and extras stop, the save file is flushed) and a sheet shows **Resume**, **Save state…**, **Load state…**, **Restart game**, **Quit**. Back or Resume → the game resumes.
  - **Save state…** → the slot grid (layout A); a used slot → "Replace slot N?"; saving writes `slotN.state` + `slotN.png`, toasts "Saved to slot N" and resumes.
  - **Load state…** → the same grid, empty slots greyed out; a tap loads and resumes; a refused load → "This state can't be loaded (damaged, or made by an older version)." and the menu stays.
  - **Restart game** → "Restart from the title screen? The current moment is lost unless it's saved in a slot." → the ROM is loaded afresh (its save file loads as usual) and starts.
  - **Quit** → the automatic state is written, the game stops (save file written), the start screen.
- **One game screen per session (found while prototyping):** `GameScreen` sits in `key(session)`, `session` counting up in `startGame` — Restart stops and starts within one frame, and without a fresh `SurfaceView` the new render thread never got a window (the picture froze while the restarted game ran).
- **Automatic state:** written in `onPause` after the game has paused (Home, another app, screen off) and on Quit. **Opening a game** (Continue or Open ROM) with an `auto.state`: after `loadRom` (and the save file) the automatic state is loaded, then the game starts; a refused one → a toast "Couldn't resume where you left off; starting the game." and a normal start. The 4c import question is asked only for games without a save file **and** without an automatic state.

## 5. Error handling

| Case | Behaviour |
|---|---|
| A state of another game, an older format, a damaged or cut-short file | `GBA_ERR_STATE`, core unchanged; the message above; the file is kept |
| Writing a state fails (storage full) | "Couldn't save the state." — the slot's old file (if any) is unchanged; the game resumes |
| The automatic state fails to load on open | A toast, then a normal start (from the title / with the save file) |
| Save/load called while the game runs | Refused in C (programming error guard); the app always pauses first |

## 6. Testing

1. **PC unit tests** (`tests/unit/test_state.c`, test-first): save → load into a second core → both cores' states are byte-identical; `gba_state_size` equals the bytes written; a too-small buffer → 0; refused (and the core byte-identical to before) for: wrong magic, other format version, other ROM's CRC, a chunk with a changed version or length, an unknown tag, a cut-short buffer; `gba_rom_crc32` of a small test ROM image equals `zlib.crc32` of the same bytes as given (computed over the size passed to `gba_load_rom`, not the zero padding the core adds).
2. **Exact replays** (the milestone's main proof): `testrunner --state-at <k>` runs k frames, saves a state, loads it into a brand-new core (same ROM, same save file), frees the first, and runs the rest — the frame and audio hashes cover the whole run. CMake adds `state_<name>` for every `frame_<name>` entry (k = half its frame count, at least 1) with the same `--expect-hash` / `--expect-audio-hash`: **32 new CTests, all must pass** (56 → 88).
3. **On-device** (`StateTest`, emulator only): `debugRunFrames`-style replay of all 32 entries through the Android build's core with a state in the middle → the approved hashes; `saveState`/`loadState` round trip on a running session (paused) → the same frames afterwards; an automatic state written on stop and resumed on the next `loadRom`; another game's state refused.
4. **Claude's check** (adb): Back → the menu; Save state → the grid; a used slot → the replace question; Load state → the game jumps back; force-stop → Continue → the same moment (screenshot); Restart game → the title.
5. **User check:** game A — save a state, play on, load it; Home and force-close → Continue at the exact moment; Restart game.

## 7. Done when

- All tests in §6 pass; CTest 88/88 on the PC.
- The user's check (§6.5) passes.
