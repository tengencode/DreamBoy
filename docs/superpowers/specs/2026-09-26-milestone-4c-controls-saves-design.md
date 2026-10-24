# Milestone 4c — Controls, save files, leaving and returning — Design

**Date:** 2026-09-26
**Status:** Implemented — milestone 4c complete 2026-09-26 (tag `milestone-4c`). Approved design (brainstormed with the user 2026-09-26); prototyped and tried by the user; plan: `docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§4 save RAM, §5.4 input, §5.5 lifecycle, §7 app shell, §8 errors, §10 milestones)
**Builds on:** 4a (tag `milestone-4a`: app, emulation thread, render thread, `KeyMap`, `SurfaceView` game screen) and 4b (tag `milestone-4b`: sound, sound pacing, key latch `platform/android/key_latch.{h,c}`); the core's save API from 3b (`gba_saveram`, `gba_saveram_size`, `gba_load_saveram`, `gba_saveram_dirty`, `gba_save_type`).

---

## 1. Goal and scope

**Finish line:** on the Pixel 9 phone emulator the user plays their game A hack **with only the on-screen buttons**, saves in the game, quits, reopens it with **Continue** and the save is there; imports their PC `.sav` and carries on from it; leaves the app (Home) and returns without losing anything. All PC, Kotlin and on-device tests pass.

**User decisions (2026-09-26):**
- Milestone 4's last part is split again: **4c** = touch buttons, key help, vibrate on touch, save files + import, leaving/returning; **4d** = rumble + tilt/gyro/light sensors.
- **Layout A:** upright — the game at the top, the buttons in the space below (the picture is never covered); sideways — the game in the middle, see-through buttons over the side bars.
- The buttons **hide when a keyboard/gamepad key is pressed** and come back on the next touch.
- **Import** is offered when a game that has a save chip has no save yet ("Start fresh" / "Import a .sav") — no separate import button.
- The start screen gets **Continue: <game name>** (reopens the last ROM without the picker; the game restarts from its title screen — the exact moment needs save states, milestone 5).
- **Back asks first** ("Quit the game? Progress since your last in-game save is lost." — Quit / Keep playing).
- **Key help:** a **Controls** button on the start screen opens a table (GBA button → keyboard key → gamepad button).
- **Save files are written by the C emulation thread** (it already owns the core and sees `gba_saveram_dirty`); Kotlin only chooses the path. (Rejected: Kotlin writes the files — save bytes would have to cross threads through JNI on every save; a mixed version — more parts, no gain.)
- Vibrate on touch follows the phone's own touch-vibration setting (`performHapticFeedback`); no switch of our own until the settings screen (milestone 6).

| In scope (4c) | Later |
|---|---|
| Touch buttons (layout A), multi-touch, 8-way D-pad, slide between buttons, hide on keyboard/gamepad | Skins, moving/resizing buttons, opacity setting (milestone 6) |
| Vibrate on touch | Rumble, tilt, gyro, light sensor (**4d**) |
| Save files in app storage, written ~1 s after the game saves and on pause/quit/game switch; `.sav.bak` | Save states, auto-save state, "resume the exact moment" (milestone 5) |
| Import a `.sav` when a game has none | Export/import from a menu, game library, settings (milestone 6) |
| Start screen: Continue, Open ROM, Controls; Back question; pause/resume that keeps the save safe | In-game menu (milestone 6) |

## 2. Files

```
platform/android/
  save_file.{h,c}      plain C: load a save file; write one safely (temp file → keep old as .bak → swap in)
  save_timer.{h,c}     plain C: when to write — ~1 s (60 frames) after the game's last save; at once on flush
  emu_thread.{h,c}     + save path, load at create, timer each frame, write on pause/stop; pause waits for it
  jni_bridge.c         + loadRom(rom, savePath), romTitle(), saveStatus(), importSave(bytes), saveError();
                       test hook debugSaveRam()
tests/unit/test_saves.c                          PC tests of save_file and save_timer
app/src/main/java/com/tengen/gbaemu/
  TouchLayout.kt       pure geometry: button areas for upright/sideways at a given size; D-pad direction
  TouchControlsView.kt the View: multi-touch → button mask, drawing, vibration, hide/show
  GameId.kt            CRC32 of the ROM bytes → "1a2b3c4d" (the save file name)
  LastGame.kt          remembers the last ROM (its content URI with persisted read permission + title)
  Screens.kt           start screen (Continue / Open ROM / Controls), Controls table, import question,
                       Back question, game screen in both layouts
  MainActivity.kt      the flow: open → id → load → import question → play; key + touch masks merged;
                       Back question; pause/resume
  NativeBridge.kt      + the new functions
app/src/test/.../TouchLayoutTest.kt, GameIdTest.kt, KeyMapTest.kt (+ merge)   JVM tests
app/src/androidTest/.../SaveTest.kt, TouchTest.kt                              on-device tests
```

`platform/android/CMakeLists.txt` already compiles every `platform/android/*.c`; `tests/unit/CMakeLists.txt` gains `save_file.c` and `save_timer.c` (they use no Android API; file replacement uses `rename` on Android and `MoveFileExA` on the PC).

## 3. Save files

- **Place and name:** `<app files>/saves/<game id>.sav`, game id = CRC32 of the ROM bytes as 8 lowercase hex digits (`GameId.kt`, `java.util.zip.CRC32`). A changed ROM (e.g. a new hack version) gets its own save — importing covers moving an old one over.
- **Load (in `emu_create`, before the thread starts):** if the file exists, `gba_load_saveram`; a size that does not fit the chip → the file is left untouched and `saveStatus` reports it (the UI says "The save file for this game doesn't fit it and was not loaded."). Saves already written by the game later go to the same path (the old file becomes `.sav.bak` first — nothing is lost).
- **`saveStatus()`** after `loadRom`: `NONE` (no save chip, or an unknown chip type — no import offer), `LOADED`, `EMPTY` (a known chip — SRAM, Flash, EEPROM — and no file yet → the import question), `MISFIT` (file present but wrong size).
- **Timing (`save_timer`, emulation thread, once per frame):** `gba_saveram_dirty` → start/restart a 60-frame countdown; at 0 → write. Pause, stop and loading another game **flush** (write now if anything is pending). A game writing its save over several frames therefore produces one file write.
- **Writing (`save_file_write`):** write `<name>.sav.tmp` and flush it to disk (`fsync`), rename the current `.sav` to `.sav.bak`, rename `.tmp` to `.sav`. Any step failing → the old `.sav` stays as it was, `saveError()` becomes true (the game screen checks it once a second); the UI shows "Could not write the save file." once and the game keeps running (the next save tries again).
- **Pause waits:** `emu_pause` returns only after the emulation thread has stopped at a frame boundary and finished the flush (or after 1 s, so the UI never freezes) — so when Android is told the app has paused, the save is on disk.
- **Import (before `start`, so no thread is running):** the picker returns a file; its bytes go to `importSave(bytes)` → `gba_load_saveram`; wrong size → `GBA_ERR_SAVE_SIZE`, nothing changes, the UI says "This .sav doesn't fit this game." and asks again; success → written to the save path at once, then the game starts.

## 4. Touch buttons and keys

- **Layout A** (`TouchLayout.kt`, all sizes in dp, positions from the view's width/height):
  - **Upright:** the game `SurfaceView` takes the full width at 3:2 at the top; `TouchControlsView` fills the rest. L and R at the top corners of that area, the D-pad left, A (upper) and B (lower-left of A) right, Select and Start centred at the bottom. Opaque-looking light buttons on black.
  - **Sideways:** the `SurfaceView` fills the screen (the game centred, 4a's black side bars); `TouchControlsView` on top, transparent, buttons drawn see-through (about 40 % white) over the side bars: L/R top corners, D-pad left, A/B right, Select/Start bottom centre.
- **D-pad:** one circle; a touch inside it gives a direction from its angle — 8 sectors of 45° (so diagonals press two bits) — and nothing inside a small centre dead spot (25 % of the radius). The touch area is a bit larger than the drawing (fingers are imprecise).
- **Touches:** every pointer is tracked; the mask = OR of the buttons under all pointers, recomputed on down/move/up; a finger sliding from B to A releases B and presses A. On a change of the mask: `onTouchMask(mask)`, redraw (pressed buttons darker), and a vibration tick for each **newly** pressed button (`performHapticFeedback(VIRTUAL_KEY)` — follows the phone's setting).
- **Merging:** `MainActivity` keeps `keyboardMask` (from `dispatchKeyEvent` + `KeyMap`, as in 4a) and `touchMask`; it sends `keyboardMask or touchMask` to `setKeys` whenever either changes. 4b's key latch makes even very short touch taps reach the game.
- **Hide/show:** a mapped keyboard/gamepad key → the buttons are hidden (the view stays, draws nothing, the touch mask is cleared); the next touch on the screen shows them again (that touch is not a button press). The game does not move.
- **Controls table** (start screen → Controls): A = X / gamepad A, B = Z / gamepad B, L = A / L1, R = S / R1, Start = Enter / Start, Select = Backspace / Select, D-pad = arrow keys / D-pad. Generated from `KeyMap` so it cannot go out of date.

## 5. App flow and leaving/returning

- **Start screen:** "GBA Emulator"; **Continue: <title>** (only when `LastGame` has one); **Open ROM**; **Controls**; the last message, if any.
- **Opening** (picker or Continue): read the bytes (Continue: from the remembered URI — gone or unreadable → "The last game's file can't be opened any more." and `LastGame` is cleared) → game id → `loadRom(rom, savePath)` → `saveStatus`: `EMPTY` → import question; `MISFIT` → message, play on; then `start`. After a successful open from the picker, `takePersistableUriPermission` + `LastGame` stores the URI and the ROM title (`gba_rom_title` via a new `romTitle()`; falls back to the file name).
- **Back while playing:** `pause()`; dialog "Quit the game? Progress since your last in-game save is lost." — **Quit** → `stop()` (writes the save) → start screen; **Keep playing** / dismiss → `resume()`.
- **`onPause`** (Home, other app, screen off): `pause()` — returns after the save flush. **`onResume`**: `resume()` unless a dialog (Back question, import question) is open. **`onDestroy`** while finishing: `stop()`.
- **Android closed the app in the background:** the next launch shows the start screen with Continue; the save is on disk (written at pause).
- **Rotation:** keeps running (4a's `configChanges`); the layout switches between upright and sideways.
- **One game screen only (found in the user's trial, 2026-09-26):** `MainActivity` is `launchMode="singleTask"`. Without it, reopening the app from its icon after it had been started another way (the developer's `am start -n`, a file manager, …) stacked a new, empty start screen on top of the paused game — the user saw the start screen after Home instead of their game. `dev.sh launch` now starts the app exactly like the icon (`MAIN` + `LAUNCHER`).

## 6. Error handling

| Case | Behaviour |
|---|---|
| Save file of the wrong size at load | Not loaded, not deleted; message; the game's next save replaces it (old one kept as `.sav.bak`) |
| Save write fails (full storage, …) | Old `.sav` untouched; "Could not write the save file." once; the game runs on; next save retries |
| Imported `.sav` of the wrong size | "This .sav doesn't fit this game."; nothing changes; the question is asked again |
| Last game's file gone / permission lost | Message; Continue removed |
| Pause flush takes over 1 s | `pause()` returns anyway (the UI never hangs); the write finishes on the emulation thread |

## 7. Testing

1. **PC unit tests** (`bash tools/dev.sh unit`, test-first, `tests/unit/test_saves.c`):
   - `save_file`: write then load gives the same bytes; a second write keeps the first as `.bak`; a write into a missing folder fails and leaves the existing `.sav` unchanged; loading a missing file reports "none".
   - `save_timer`: one dirty frame → write after exactly 60 quiet frames; dirty frames keep pushing it back; flush with something pending → write now; flush with nothing pending → nothing.
2. **JVM tests** (`bash tools/dev.sh jvmtest`): D-pad sectors (all 8 directions, diagonals = two bits, the dead spot, outside = none); button hit-testing in both layouts (the centre of each button hits it, the gap between A and B hits neither); `keyboardMask or touchMask`; `GameId` of known bytes (`"123456789"` → `cbf43926`).
3. **On-device tests** (emulator only), new `SaveTest` and `TouchTest`:
   - a save-chip ROM (jsmolka `sram.gba`) writes its file within 1.5 s of start, byte-equal to the core's save RAM (hook `debugSaveRam()`); a second `loadRom` of the same ROM reports `LOADED` with the same bytes;
   - `pause()` with a pending save → the file is on disk when `pause()` returns;
   - `importSave` accepts a 32 KiB file for an SRAM game (written at once), rejects 1,000 bytes (`saveStatus` and the file unchanged);
   - `TouchControlsView` given synthetic `MotionEvent`s: one finger on A → A; a second finger on the D-pad's up-right → A | UP | RIGHT; lifting the first → UP | RIGHT; a key event hides the buttons and clears the mask;
   - all earlier on-device tests keep passing.
4. **Claude's check** (adb on the emulator): start screen with Controls; open a ROM; Back → question → Keep playing → still running; Back → Quit; Continue after `am force-stop`; Home and back; rotation; `logcat` clean.
5. **User check:** their game A hack with only the touch buttons; save in the game → Back → Quit → Continue → the save is there; import their PC `.sav` (copied to the emulator) on a fresh install/ROM and continue from it; Home and back; turning the phone.

## 8. Done when

- All tests in §7 pass; CTest 56/56 on the PC.
- The user's check (§7.5) passes.
