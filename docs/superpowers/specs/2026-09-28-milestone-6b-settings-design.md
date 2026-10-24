# Milestone 6b — Settings and Controller Remapping — Design

**Date:** 2026-09-28
**Status:** Implemented — milestone 6b complete 2026-10-02 (tag `milestone-6b`). Approved design (brainstormed with the user 2026-09-28); prototyped (branch `proto-6b`) and tried by the user ('Everything works', 2026-09-28); plan: `docs/superpowers/plans/2026-09-28-milestone-6b-settings.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§5 platform layer, §7 app shell, §10 milestones)
**Builds on:** milestone 6a (tag `milestone-6a`: the library, its hold menu), 5b (fast-forward speed, rewind 64 MiB / 4 frames), 4a-4d (video, sound, keys, touch buttons).

---

## 1. Goal and scope

Milestone 6 is split in three (user decision 2026-09-28): 6a game library ✅ → **6b settings + controller remapping** → 6c skins + layout editor.

**6b finish line:** on the Pixel 9 emulator the user, in game A, changes settings from the in-game menu and sees them take effect — smooth pixels, a picture size, a remapped key, the sideways button look — and the game settings and BIOS choice work. Automatic tests prove the size modes, a state of the other BIOS kind being refused, the settings / key bindings / game settings files, and on the phone the drawing modes, the overrides and the BIOS reaching the emulator; with no settings changed nothing differs (CTest 120/120).

**User decisions (2026-09-28):**
- **All seven groups** (of 7 offered): picture, sound, fast-forward + rewind, remapping, touch buttons, BIOS file, per-game fixes.
- **Categories, each its own page** (layout B of 3, chosen in the visual companion); a row tapped → a small pick list; changes saved at once.
- **Also from the in-game menu** (option A): "Settings…" there too, the game paused behind it. Everything applies at once except the BIOS, the rewind length and the game settings, which apply the next time a game opens (and say so).
- **Remapping: one keyboard key and one controller button per GBA button** (option A of 3), ⏩ and ⟲ included; a key already used moves to the new button and leaves "—"; "Reset to defaults".
- **A real BIOS for testing:** the user's own dump at `tests/bios/gba_bios.bin` (16 KiB, CRC-32 `81977335`, gitignored by `*.bin` — never committed).

| In scope (6b) | Later |
|---|---|
| The settings below; Settings from the library (⚙) and the in-game menu | Skins, layout editor (6c) |
| Remapping keyboard + controller (one each per button) | Several keys per button; analog sticks; per-game key maps |
| BIOS file (copied into the app) | Several BIOS files |
| Game settings: save type, clock | Forcing other cartridge extras (sensors, rumble) |

## 2. The settings

| Group | Setting | Choices (default first) | Applies |
|---|---|---|---|
| Picture | Pixels | Sharp / Smooth | at once |
| | Size | Fit screen / Whole-number steps / Stretch to fill | at once |
| Sound | Sound | On / Off | at once |
| | Delay | Low (as now) / Medium / High | at once |
| Fast-forward & rewind | Speed | 4× / 2× / 8× / Max (the existing setting, shared with the menu row; the stored value is kept) | at once |
| | Rewind | On / Off | next game |
| | Length | about 2½ min (64 MB, as now) / about 1 min (24 MB) / about 5 min (128 MB) | next game |
| Keyboard & controller | per GBA button (A B L R Start Select Up Down Left Right ⏩ ⟲) | one keyboard key, one controller button; defaults = today's | at once |
| Touch buttons | Sideways look | Normal (as now) / Faint / Strong | at once |
| | Buzz | On (follows the phone's setting) / Off | at once |
| BIOS | BIOS | Built-in / a BIOS file | next game |
| Game settings (per game) | Save type | Auto (shows the detected type) / None / SRAM / Flash 64K / Flash 128K / EEPROM 512 B / EEPROM 8K | next time it opens |
| | Clock | Auto (shows whether it has one) / On / Off | next time it opens |

## 3. Files

```
core/src/state.c                    a state of the other BIOS kind (built-in vs file) → GBA_ERR_STATE, nothing changed
tests/unit/test_state.c             + that refusal
platform/android/fit_rect.{h,c}     + fit modes: FIT, INTEGER (largest whole multiple of 240x160 that fits; FIT if none), STRETCH
tests/unit/test_platform.c          + the fit modes (where fit_rect is tested)
platform/android/video_gl.{h,c}     + smooth: the fragment shader blends the 4 nearest texels (the texture stays an integer one); mode + smooth set per draw
platform/android/render_thread.c    + the picture settings (atomics, read each frame)
platform/android/audio_out.c        + buffer = burst x 2 / 4 / 8
platform/android/emu_thread.{h,c}   + sound on/off and delay at any time (the stream closed / reopened by the emulation thread, as after a device change);
                                      rewind budget and the BIOS before start; save type and clock overrides applied before the save file is read
platform/android/jni_bridge.c       + setPicture, setSound, setRewind, setBios, loadRom(…, saveType, clock)
app/src/main/java/com/tengen/gbaemu/
  Settings.kt       the app-wide settings (one data class) in the "settings" preferences; fast_speed kept
  KeyBindings.kt    the key map: defaults, assign (a used key moves), reset, stored in the "keys" preferences; KeyMap uses it
  GameSettings.kt   a game's save type and clock, by game id
  Bios.kt           the BIOS file: check (16 KiB; the usual one by CRC-32 81977335), copy to files/bios/gba_bios.bin
  Store.kt          IntStore (MemoryStore for the PC-side tests, PrefsStore = SharedPreferences)
  SettingsScreens.kt  SettingsScreen (categories + a page each), ChoiceDialog (pick list), the key table, KeyWaitLayer, UnusualBiosDialog, GameSettingsDialog
  Screens.kt        "Settings…" in the in-game menu, "Game settings…" in the hold menu, Controls → Settings (the Controls pop-up removed)
  TouchControlsView.kt  + the sideways look and buzz
  MainActivity.kt   + wiring
app/src/test/.../SettingsTest.kt, KeyBindingsTest.kt, GameSettingsTest.kt, BiosTest.kt
app/src/androidTest/.../SettingsDeviceTest.kt
```

The PC settings store behind `Settings`, `KeyBindings` and `GameSettings` is a small key/value interface, so the JVM tests use a map and the app uses SharedPreferences.

## 4. How it works

- **Picture:** `fit_rect(width, height, mode)` — FIT as now; INTEGER = the largest n with 240n x 160n inside (centred; FIT if n = 0); STRETCH = the whole surface. Smooth = in the shader, the frame read at the 4 texels around the sample point and mixed by their distances (bilinear); Sharp = the current nearest texel, unchanged.
- **Sound:** Off = no AAudio stream, the emulation paced by the clock (the existing muted path); the switch works at any time: the emulation thread closes or opens the stream between frames. Delay = the stream's buffer size 2 / 4 / 8 bursts, applied by reopening the stream.
- **Rewind:** `emu_set_rewind(budget)` before start (0 = off); used by the next game.
- **BIOS:** the app keeps `files/bios/gba_bios.bin`; `setBios(bytes or null)` before `loadRom`; the core boots through it (the Nintendo logo). A state of the other BIOS kind is refused by the core (the automatic state then fails to resume and the game starts from the title — the existing path).
- **The JNI calls** (found while prototyping: simpler than changing `loadRom`): `setBios`, `setRewindBudget`, `setSound`, `setPicture` stay until changed; `setGameOverrides(saveType, clock)` applies to the next `loadRom` only; `cartSetup()` gives the loaded game's save type and extras (the app remembers them to show beside Auto).
- **Game settings:** `emu_create(…, emu_options_t)`: the BIOS, then the ROM, then the save type and the clock overridden (`gba_set_save_type` blanks the save RAM, so this is before the save file is read), then the save file. Auto = no override.
- **Remapping:** the key page listens for the next key or controller button (Back cancels; the key is not sent to a game). **Found while prototyping:** the "press a key" prompt is a layer on the app's own window, not a dialog — a dialog's window would get the key instead of `MainActivity.dispatchKeyEvent`; `KeyBindings.assign(bit, device, keyCode)` removes that key from any other button. `KeyMap.bitFor` looks in the current bindings.
- **Touch buttons:** sideways alpha Faint 0x33 / Normal 0x55 (as now) / Strong 0x99; buzz Off skips `performHapticFeedback`.

## 5. Error handling

| Case | Behaviour |
|---|---|
| A BIOS file not 16 KiB | "That isn't a GBA BIOS (it must be 16 KB)."; nothing changes |
| 16 KiB but not the usual GBA BIOS (CRC-32 ≠ 81977335) | "This isn't the usual GBA BIOS; games may not start. Use it anyway?" |
| The copied BIOS file missing | The built-in BIOS is used; a toast |
| A save state of the other BIOS kind | Refused by the core; the game starts from the title (existing message) |
| A key already used | Moves to the new button; the old one shows "—" |
| A GBA button with no key | Allowed ("—") |
| A forced save type the save file doesn't fit | The file kept, not loaded (existing "doesn't fit" toast) |
| A stored setting that doesn't read | Its default |

## 6. Testing

1. **PC unit tests** (test-first): `fit_rect` modes (FIT as before, INTEGER on several surfaces incl. too small, STRETCH); a state saved with the built-in BIOS refused after `gba_load_bios` and vice versa, the machine unchanged.
2. **Real BIOS on the PC** (Claude's check, only where `tests/bios/gba_bios.bin` exists): `testrunner --bios tests/bios/gba_bios.bin` on a demo boots through the BIOS to the demo.
3. **No change without settings:** CTest 120/120.
4. **JVM tests:** `SettingsTest` (defaults, round trip, the old `fast_speed` kept, an unreadable value → default), `KeyBindingsTest` (defaults = today's map, assign moves, reset, round trip, `bitFor`), `GameSettingsTest` (Auto by default, round trip, per game), `BiosTest` (size, CRC check).
5. **On-device** (`SettingsDeviceTest`, emulator only): sharp vs smooth draw different pixels of the same frame (`debugDrawFrame`), the size modes' rectangles; a forced save type and clock reach the core; a BIOS handed over is used (a 16 KiB test BIOS of our own that jumps to the cartridge).
6. **Claude's check** (adb): each category page; a key remapped and working in a demo; the sideways look; game settings; the user's BIOS pushed to the emulator, chosen, and a demo booting through it.
7. **User check:** in game A, from the in-game menu: smooth pixels, a size, a remapped key, the sideways look take effect.

## 7. Done when

- All tests in §6 pass; CTest 120/120 on the PC.
- The user's check (§6.7) passes.
