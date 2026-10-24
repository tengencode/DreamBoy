# Milestone 5c — Cheats — Design

**Date:** 2026-09-28
**Status:** Implemented — milestone 5c complete 2026-09-28 (tag `milestone-5c`). Approved design (brainstormed with the user 2026-09-28); prototyped (branch `proto-5c`) and tried by the user ('Everything works', 2026-09-28); plan: `docs/superpowers/plans/2026-09-28-milestone-5c-cheats.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§3 core API, §6.3 cheats, §7 app shell, §10 milestones)
**Builds on:** milestone 5b (tag `milestone-5b`: rewind, fast-forward), 5a (the in-game menu, `States`, "only while paused" for the core), 4c (`GameId`, files per game).

---

## 1. Goal and scope

Milestone 5 is split in three (user decision 2026-09-27): 5a save states ✅ → 5b fast-forward + rewind ✅ → **5c cheats**.

**5c finish line:** on the Pixel 9 emulator the user, in their game A hack, adds real codes copied from a cheat site — Action Replay and CodeBreaker — and sees them work; switching one off stops it; after quitting and reopening the game the switched-on cheats are on again. Automatic tests prove the unscrambling (the TEA cipher against its published values; the keys from mGBA's source), the behaviour of every step kind, auto-detection and error lines; published game A codes show their effect on the user's game in Claude's PC check; with no cheats on, all 120 CTests still match.

**User decisions (2026-09-28):**
- **Codes from game A cheat sites** (option A of 3): **Action Replay v3 / GameShark v1-v2** (8 + 8 hex digits, scrambled or plain) and **CodeBreaker** (8 + 4 hex digits, plain or scrambled); plus the plain **address:value** form (VBA style, nearly free). Master / enable codes are accepted and skipped.
- **Type auto-detected, with a picker to override** (option A): Auto shows what it found; the picker offers Action Replay v3, GameShark v1/v2, CodeBreaker, address:value.
- **Screens: a panel like the save slots, adding in a pop-up** (layout A, chosen in the visual companion).
- **Checked against mGBA** (option A): its source was read online (nothing downloaded) for the formats, keys and auto-detection; our code is our own (mGBA is MPL-2.0 — no copied code). mGBA's tests cover only plain Action Replay steps, so the scrambled formats are proven by the cipher's published values, round trips, and real published codes on the user's game.
- **Engine in the core, in C** (option A of 3). (Rejected: decoding in Kotlin with the core only applying writes — the logic split in two and the PC test runner unable to read real codes; writing memory from Kotlin — not in step with the game's frames, conditions unreliable.)

| In scope (5c) | Later |
|---|---|
| Core: parse, unscramble (AR v3, GS v1/v2, CodeBreaker incl. scrambled), auto-detect, apply once per frame; ROM patches undone when off | Importing `.cht` files, a built-in code list |
| `gba_cheat_check` (type / first bad line) and `gba_cheats_set` | Hook ("master code") timing — cheats run once per frame instead |
| Test runner `--cheat <file>` | "Slowdown" codes (accepted, ignored) |
| Cheat panel + add/edit pop-up, per-game storage, sent at game start | Cheats in `gbaview` |

## 2. Files

```
core/include/gba.h                 + gba_cheat_type_t, gba_cheat_check, gba_cheats_set
core/src/cheats/cheats.h           the internal step list, parse / apply entry points
core/src/cheats/parse.c            lines → codes, auto-detection, error lines
core/src/cheats/decrypt.c          TEA with the AR v3 and GS v1/v2 keys; CodeBreaker's scrambling
core/src/cheats/decode.c           codes of each format → steps (write, fill, if/else blocks, add/and/or, button, ROM patch; master/hook/slowdown skipped)
core/src/cheats/apply.c            the steps run in order at each frame's end; ROM patches applied / undone
core/src/gba.c, gba_internal.h     the cheat list pointer (outside every state chunk); applied in gba_run_frame
core/src/mem/bus.{h,c}             + bus_poke: a write without cycles (RAM, I/O, palette, VRAM, OAM)
tools/testrunner/main.c            + --cheat <file> (a cheat file: optional "type:" line, then the codes), --load-state <file>
tests/unit/test_cheats_*.c         cipher, round trips, behaviour, parsing/detection, ROM patches
platform/android/emu_thread.{h,c}, jni_bridge.c   + set cheats (only while paused / before start), check a cheat's text
app/src/main/java/com/tengen/gbaemu/
  Cheats.kt         a game's cheats in files/cheats/<game id>.json (name, type, codes, on/off)
  Screens.kt        + "Cheats…" in the menu, the cheat panel, the add/edit pop-up, the delete question
  MainActivity.kt   + wiring; the switched-on cheats sent before the game starts and after every change
  NativeBridge.kt   + checkCheat, setCheats
app/src/test/.../CheatsTest.kt, app/src/androidTest/.../CheatTest.kt
```

## 3. The cheat engine (core)

- **API:**
  - `typedef enum { GBA_CHEAT_AUTO, GBA_CHEAT_ACTION_REPLAY, GBA_CHEAT_GAMESHARK, GBA_CHEAT_CODEBREAKER, GBA_CHEAT_RAW } gba_cheat_type_t;`
  - `gba_cheat_type_t gba_cheat_check(const char *text, gba_cheat_type_t type, int *bad_line)` — reads a cheat's text (lines; blank lines and spaces ignored) without a machine: the type used (Auto → the detected one) and `*bad_line` = 0, or `*bad_line` = the first unreadable line (1-based, counting every line as typed, blank ones included — the number the user sees in the box).
  - `gba_err_t gba_cheats_set(gba_t *g, const char *const *texts, const gba_cheat_type_t *types, size_t count)` — replaces the whole list of switched-on cheats: first every ROM patch of the old list is undone, then the new list is read; an unreadable text → `GBA_ERR_CHEAT` (a new error number, appended) and the list is empty. The frontend calls it between frames.
- **Reading:** a line is `XXXXXXXX:VV[VV[VVVV]]` (address:value), `XXXXXXXX YYYY` (CodeBreaker) or `XXXXXXXX YYYYYYYY` (Action Replay / GameShark; spaces between the halves allowed). **Auto:** a line with ':' → address:value; 8 + 4 digits → CodeBreaker; 8 + 8 → the first code is unscrambled with each key set (and taken plain) and scored by whether its address points at real memory (EWRAM/IWRAM best, I/O good, ROM/VRAM/OAM/palette poor, nothing worse) — the best interpretation decides the type for the whole cheat (as mGBA does).
- **Unscrambling:** Action Replay v3 and GameShark v1/v2 = TEA decryption (32 rounds, delta `0x9E3779B9`) with their own 4-word keys; scrambled CodeBreaker codes (after a `9xxxxxxx` "encrypt" line) = CodeBreaker's own method. Keys and methods as in mGBA's source.
- **Steps:** write 1/2/4 bytes; fill (repeat with an address step); add/and/or (CodeBreaker); conditions (==, ≠, <, >, unsigned <, >, and-mask) guarding the next step, the next two, or a block up to "else"/"end if" (Action Replay's forms); only while a button is held; ROM patch 1/2/4 bytes (the original bytes kept to undo it). Game-id, master/hook and slowdown lines are recognised and skipped. **Found while prototyping:** the "button" codes of GameShark / Action Replay mean the cheat device's own button (mGBA: a hotkey), not a GBA button — here they never fire; CodeBreaker's `D0000020` condition reads the real GBA keys. The **DEADFACE re-keying** lines (GS and AR; they need two 256-byte tables per device) are not supported: such a cheat is refused with the line number.
- **Applying:** at the end of every `gba_run_frame` (the frame's VBlank has just started, before the next frame's code runs), before the rewind snapshot. RAM/VRAM/OAM/palette writes go straight into memory (no bus cycles, no timing change); I/O writes go through the I/O register write path. ROM patches are written into the ROM copy once, when the list is set.
- **Not in any state:** the list lives behind a pointer in `struct gba`, outside every chunk of `state.c` — save states and rewind never contain it, and loading one keeps the cheats (and their ROM patches) in place.

## 4. Android

- `emu_cheats_set(e, texts, types, count)` — only while the thread is parked or not started (like `emu_state_save/load`); otherwise refused. Test hook `emu_debug_peek32` / JNI `debugPeek32(addr)` (only while paused).
- JNI: `checkCheat(text, type)` → `[type found, bad line]` (no game needed); `setCheats(texts[], types[])` → 0 or an error number.
- The app calls `setCheats` after `loadRom` (and the automatic state) and before `start()`, and after every change in the panel (the game is paused behind the menu).

## 5. The app

- **Menu:** "Cheats…" between "Load state…" and "Restart game".
- **Cheat panel** (layout A): "Cheats — <title>", one row per cheat (name, on/off switch), "+ Add cheat", "Back" (→ the menu). A switch change is saved and sent at once. Tapping a name opens the pop-up for editing, with "Delete" (→ "Delete <name>?").
- **Add/edit pop-up:** Name; Codes (multi-line, one code per line); Type (Auto / Action Replay v3 / GameShark v1/v2 / CodeBreaker / address:value; with Auto the detected type is shown under the box as the codes are typed). Save checks the codes with `checkCheat`: a bad line → "Line N isn't a code this app can read." and nothing is saved; an empty name → the first code line is used as the name. A new cheat is switched on.
- **Storage:** `files/cheats/<game id>.json` — a list of `{name, type, codes, on}`, written safely (temporary file, then swapped), read when the game opens. The JSON is read/written by our own small code (Android's org.json is not available to the PC-side tests; a library would be a download).

## 6. Error handling

| Case | Behaviour |
|---|---|
| A line that is not a code (or the wrong type picked) | The pop-up names the line; nothing saved |
| A stored cheat that no longer reads (hand-edited file) | Skipped when sent, with a toast "Cheat '<name>' can't be read and was switched off"; it stays in the list, off |
| An unreadable cheats file | A toast; the game starts without cheats; the file is kept (renamed `.bad`) |
| Cheats sent while the game runs | Refused in C (programming error guard); the app always pauses first |

## 7. Testing

1. **PC unit tests** (test-first): TEA against its published test values; the AR v3 / GS v1/v2 keys: plain → scrambled (our encryptor, test-only) → unscrambled round trips, and auto-detection picking the right type for each; CodeBreaker scrambled round trips; the behaviour of each step kind on a small test machine (mGBA's Action Replay step tests rewritten as ours — same codes, same expected memory; plus CodeBreaker, GameShark, address:value); ROM patches applied and undone (the ROM byte-identical after switching off); master/hook/slowdown lines skipped; `gba_cheat_check` bad-line numbers; `gba_cheats_set` refusing a bad text and leaving an empty list.
2. **No cheats, no change:** CTest 120/120 as before.
3. **Real codes on the PC (Claude's check):** `testrunner --cheat <file>` with published game A codes (read from a cheat site, one per format) on the user's ROM, save and automatic state (copied read-only from the emulator) → a screenshot showing each code's visible effect.
4. **On-device** (`CheatTest`, emulator only): cheats set while paused change a test ROM's memory; switched off → the ROM patch undone; refused while running; `checkCheat` from Kotlin. JVM `CheatsTest`: the JSON round trip, a damaged file.
5. **Claude's check** (adb): the menu entry, the panel, a bad code → the line message, a good cheat saved and switched on, off, reopening the game → still on.
6. **User check:** game A — real Action Replay and CodeBreaker codes from a site work; switching off stops them; quit and reopen → the switched-on ones are on.

## 8. Done when

- All tests in §7 pass; CTest 120/120 (plus the new unit tests) on the PC.
- The user's check (§7.6) passes.
