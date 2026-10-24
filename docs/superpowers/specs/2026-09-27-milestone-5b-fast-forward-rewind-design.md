# Milestone 5b — Fast-forward and rewind — Design

**Date:** 2026-09-27
**Status:** Implemented — milestone 5b complete 2026-09-28 (tag `milestone-5b`). Approved design (brainstormed with the user 2026-09-27); prototyped (branch `proto-5b`) and tried by the user ('Everything works now', 2026-09-27); plan: `docs/superpowers/plans/2026-09-27-milestone-5b-fast-forward-rewind.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3 core API, §5.3 audio, §6.2 rewind, §7 app shell, §10 milestones)
**Builds on:** milestone 5a (tag `milestone-5a`: `gba_state_size/save/load`, the in-game menu, the automatic state); milestone 4 (the emulation thread, sound pacing, the touch layout, keys and gamepads).

---

## 1. Goal and scope

Milestone 5 is split in three (user decision 2026-09-27): 5a save states ✅ → **5b fast-forward + rewind** → 5c cheats.

**5b finish line:** on the Pixel 9 emulator the user, in their game A hack, holds ⏩ and the game runs fast with sped-up (higher-pitched) sound; a tap keeps it fast until the next tap; the speed chosen in the in-game menu (2×/4×/8×/Max) is used, Max is silent; holding ⟲ runs the game backwards smoothly, and letting go plays on normally from there; a gamepad's R2/L2 and two keyboard keys do the same. Automatic tests prove that rewinding is exact: for all 32 approved test ROMs, running to a frame, rewinding some steps and playing on to the end still gives the **approved picture and sound fingerprints** — on the PC and on the emulator.

**User decisions (2026-09-27):**
- **Fast-forward: tap or hold** (option C of 3): a quick tap (< 300 ms) switches it on/off (the button stays lit while on); holding runs fast while held, and releasing returns to normal speed (the switch off).
- **Speed chosen in the in-game menu: 2×, 4×, 8×, Max** (option B); a menu row "Fast-forward: 4×" cycles them; remembered by the app for all games; default 4×.
- **Sound while fast: sped up** (option B — higher pitch, like a tape on fast-forward); at **Max** silent (the speed is not steady enough to follow).
- **Rewind reaches about 3 minutes** (option C): a 64 MiB budget, one snapshot every 4 frames.
- **Buttons at the top, between L and R** (layout A, chosen in the visual companion): ⟲ then ⏩. Sideways (found while prototyping): between L and R is over the game's picture, so there ⟲ sits just below L and ⏩ just below R, in the black side bars.
- **Rewind stored in the core with our own small packer** (option A of 3): each snapshot kept as "what changed since the one before", packed by runs of unchanged bytes. (Rejected: the LZ4 library — a download, and the project has no outside code so far; rewind only in the Android layer — the PC tests could not prove it.)
- **Not changed:** save-state slot files stay unpacked (packing them would refuse the user's existing 5a states; they are ~650 KB).

| In scope (5b) | Later |
|---|---|
| Core: `gba_rewind_configure`, `gba_rewind_step_back`, automatic snapshots in `gba_run_frame`, the packer | Rewind / fast-forward in `gbaview` |
| Test runner `--rewind-at <frame>:<steps>` + 32 CTest `rewind_*` replays | A settings screen (rewind on/off, budget), per-game speeds (milestone 6) |
| Fast-forward 2×/4×/8×/Max with sped-up sound; rewind while held, silent | Rewind sound played backwards |
| ⟲ ⏩ touch buttons, the menu row, gamepad R2/L2, two keyboard keys | Packed save-state files; cheats (**5c**) |

## 2. Files

```
core/include/gba.h                 + gba_rewind_configure / gba_rewind_step_back
core/src/delta_pack.{h,c}          the packer: XOR of two snapshots → runs of zeros and literal bytes
core/src/rewind.c                  the ring of packed snapshots; capture and step back
core/src/gba.c                     capture every interval_frames at the end of gba_run_frame; clear on reset
core/src/state.c                   gba_state_load clears the rewind history (the internal load used by rewind does not)
core/src/gba_internal.h            + the rewind ring pointer in struct gba (outside every state chunk)
tools/testrunner/main.c            + --rewind-at <frame>:<steps>
CMakeLists.txt                     + rewind_<name> tests (every frame_<name> entry of tests/expected_frames.json)
tests/unit/test_delta_pack.c, tests/unit/test_rewind.c
platform/android/emu_thread.{h,c}  + fast-forward speed and rewinding (atomics), the loop for each mode
platform/android/jni_bridge.c      + setFastForward(speed), setRewinding(on); test hooks
app/src/main/java/com/tengen/gbaemu/
  FastForwardSwitch.kt  tap vs hold (pure, JVM-tested)
  TouchLayout.kt        + the ⟲ ⏩ buttons (not GBA keys)
  TouchControlsView.kt  + drawing them (⏩ lit while on) and reporting presses
  KeyMap.kt             + R2 / L2, F / R
  KeyRepeatFilter.kt    held F / R through the emulator window's tap stream (pure, JVM-tested)
  Screens.kt            + the menu row "Fast-forward: N×"
  MainActivity.kt       + wiring, the remembered speed
  NativeBridge.kt       + the new functions
app/src/test/.../FastForwardSwitchTest.kt, app/src/androidTest/.../RewindTest.kt
```

## 3. Rewind in the core

- **API:**
  - `gba_err_t gba_rewind_configure(gba_t *g, size_t budget_bytes, int interval_frames)` — allocates the ring (`budget_bytes`) plus two state-sized buffers and a packing scratch buffer; `GBA_ERR_NOMEM` if that fails (rewind then stays off). A budget of 0 turns rewind off and frees everything. Configuring again clears the history. This is the only place rewind allocates (the parent spec's rule: no allocation inside `gba_run_frame`).
  - `int gba_rewind_step_back(gba_t *g)` — puts the machine back one snapshot; returns 1, or 0 when the history is empty (the machine unchanged).
- **Capture:** at the end of every `interval_frames`-th `gba_run_frame` (a frame counter in the rewind ring), the machine is saved with `gba_state_save` into the "new" buffer; the packer stores `new XOR current` into the ring; `new` becomes `current` (one uncompressed copy is kept). The first capture only fills `current`.
- **Packer** (`delta_pack`): input = the XOR of two equal-sized snapshots (mostly zero bytes). Output = a sequence of `(zero-run length, literal length, literal bytes)`, lengths as variable-length integers. `delta_unpack` XORs the result straight into a buffer, so unpacking and "undoing" are one pass. Worst-case output size is bounded (`delta_pack_bound(n)`), which sizes the scratch buffer.
- **Ring:** packed records one after another in the budget block, each with its length at both ends (so the newest can be popped and the oldest dropped). A new record that does not fit drops the oldest records until it does.
- **Step back:** if frames ran since the last capture, the first step loads `current` (the last snapshot). Otherwise the newest record is unpacked into `current` (which turns it into the snapshot before), popped, and `current` is loaded. Loading uses the internal state load (all of 5a's checks) without clearing the history. The snapshot holds the PPU framebuffer, so the picture is right immediately — no frame is run. The capture counter restarts at the restored moment, so the next capture comes `interval_frames` frames later (snapshots stay `interval_frames` apart, and the test runner can tell which frame it is on).
- **Cleared by:** `gba_reset`, `gba_load_rom`, the public `gba_state_load`, `gba_rewind_configure`.
- **Not in any state:** the ring pointer lives in `struct gba` outside every chunk of `state.c`, so saving a state never includes the history, and loading one never overwrites the pointer.
- **How far back 64 MiB really reaches (measured in the prototype):** in the user's game A hack, walking around from their automatic state (PC, same core), a snapshot packs to ~27 KB on average → **~2 min 40 s** (about 2,400 snapshots). A step back takes ~0.02 ms on the PC.

## 4. Fast-forward and rewind on Android

- **Controls:** `emu_set_fast_forward(e, speed)` (0 = normal, 2, 4, 8, or `EMU_FF_MAX`) and `emu_set_rewinding(e, on)`, atomics read at the start of each loop step. Rewinding wins over fast-forward.
- **Normal:** as today (one frame, sound pacing, or the clock without sound).
- **Fast 2×/4×/8× (N):** N frames per loop step; every frame's picture goes to the triple buffer (the screen shows the newest); every sample goes through the resampler with the ratio divided by N (N times fewer output samples → N times higher pitch and speed); sound pacing works unchanged and holds the game at N× the normal rate. If the phone cannot keep up, it runs as fast as it can; the sound may crackle.
- **Max:** frames run with no pacing; the core's sound is read and dropped, the output ring emptied (silence); keys, sensors, pause and quit are checked every frame as usual.
- **Rewinding:** one `gba_rewind_step_back` per 1/60 s (the clock pacer), the framebuffer published after each step; no sound (the ring emptied); keys are not applied. When the history is empty the picture stays on the oldest moment. On release the save timer is marked, so the `.sav` is rewritten ~1 s later (the previous one → `.sav.bak`), as after a 5a load — rewinding past an in-game save undoes it.
- **Rewind configured** with 64 MiB and 4 frames when a game starts (after its save file and automatic state are loaded); a `GBA_ERR_NOMEM` leaves rewind off (logged; ⟲ does nothing).
- **The save timer** counts frames, so at N× it writes after ~1/N s — harmless.
- **JNI:** `setFastForward(speed)`, `setRewinding(on)`; test hooks `debugRunFramesViaRewind(rom, frames, at, steps)` (headless, like `debugRunFramesViaState`) and `debugRewindStats()` (snapshots kept, packed bytes, steps taken); speeds are measured with the existing `debugFrameCount()`.

## 5. The app

- **Buttons:** ⟲ and ⏩ between L and R upright (64 dp pills, a little lower than L/R); sideways just below L and R in the side bars; symbols drawn at 20 dp; not GBA keys — `TouchLayout` reports them with their own ids, `TouchControlsView` draws ⏩ highlighted while fast mode is switched on.
- **`FastForwardSwitch`** (pure Kotlin): `press(t)`, `release(t)` → whether fast mode is on. Press → fast now. Release within 300 ms of the press → a tap: the switch flips (on if it was off, off if it was on). A longer hold → off on release.
- **Menu:** a row **"Fast-forward: 4×"** below Resume; a tap cycles 2× → 4× → 8× → Max → 2×; the value is stored in the app's preferences (all games). Opening the menu pauses the game as in 5a; fast mode stays as it was when the menu closes.
- **Keys:** gamepad **R2 = fast-forward (hold/tap as the touch button), L2 = rewind (hold)**. Keyboard (user decision after the trial, 2026-09-27): **F = fast-forward, R = rewind**. No laptop key arrives in the phone emulator's window as a clean hold (`getevent`: letters/Tab/Space = a tap, a ~500 ms pause, then a tap every ~33 ms; arrows = a press, then release/press pairs), so `KeyRepeatFilter` turns a repeat stream into one hold: a key-up ends a hold only if no key-down follows within 60 ms. The PC repeats only the last key pressed and letters send no key-up, so **F held while walking with the arrows cannot be seen: on the emulator window F is a tap switch** (user decision, round 2); R held on its own rewinds (it may pause ~0.5 s just after R is pressed). Real keyboards and gamepads on a phone hold normally.
- **Resets of the history:** loading a slot, Restart game and opening a game go through `gba_state_load` / `gba_load_rom`, which clear it.

## 6. Error handling

| Case | Behaviour |
|---|---|
| The rewind memory cannot be reserved | Rewind off for that game (logged); ⟲ does nothing; the game plays normally |
| ⟲ held with an empty history (just after opening, loading or restarting) | The picture stays; letting go plays on |
| The phone cannot reach the chosen speed | It runs as fast as it can; the sound may crackle |
| The sound stream is lost while fast | The existing watchdog (4b) reopens it, as at normal speed |

## 7. Testing

1. **PC unit tests** (test-first): `test_delta_pack.c` — pack/unpack round trips for all-zero, all-changed, random sparse changes, odd sizes, and two real snapshots 4 frames apart; the output never exceeds `delta_pack_bound`. `test_rewind.c` — after k captures, stepping back returns exactly the state saved n captures ago (byte-compared with saved copies); the oldest records are dropped when the budget is small; an empty history → 0 and the machine unchanged; `gba_state_load`, `gba_reset` and re-configuring clear the history; budget 0 → rewind off.
2. **Exact replays through rewind** (the main proof): `testrunner --rewind-at <f>:<s>` runs f frames, rewinds s steps, then runs on with the same key script until the total frame count, counting frames from the restored moment. CMake adds `rewind_<name>` for every `frame_<name>` entry (f = two thirds of its frames, s = small enough to stay inside the recorded history) with the same expected hashes: **32 new CTests** (88 → 120). A sabotage check (skip part of the unpacking) must fail them; restored → all pass.
3. **On-device** (`RewindTest`, emulator only): the 32 replays through rewind on the Android core; fast-forward by the clock: 2× ≈ twice the frames of normal, 4× ≥ 3×, Max ≥ 4×, back to normal; 2× with sound (paced by the sound, the stream still on); rewinding runs no frames, ends on the oldest snapshot, then plays on. JVM `FastForwardSwitchTest`: tap on, tap off, hold then release → off, hold while on → off; `KeyRepeatFilterTest`: a real press, a lone emulator tap, a repeat stream as one hold, keys apart.
4. **Claude's check** (adb, the Mode 7 demo): ⏩ and ⟲ drawn; a tap on ⏩ and the F key light ⏩, the next one turns it off; the menu row cycles 4× → 8× → Max → 2× → 4×; after walking, ⟲ held goes back to the first picture (screenshots compared pixel for pixel); sideways ⟲/⏩ below L/R; no crash.
5. **User check:** game A — hold and tap ⏩ (sped-up sound), each speed from the menu (Max silent), hold ⟲ and play on, a gamepad or the keyboard keys.

## 8. Done when

- All tests in §7 pass; CTest 120/120 on the PC.
- The user's check (§7.5) passes.
