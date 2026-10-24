# Milestone 10a — Cheat Files (`.cht` import) — Design

**Date:** 2026-10-04
**Status:** Implemented — milestone 10a complete 2026-10-04 (tag `milestone-10a`): all tests pass and Claude's check on the emulator passed with libretro's game A file ('Added 72 cheats. 2 couldn't be read.'). Brainstormed with the user 2026-10-04 (both design parts approved: 'looks right'); spec approved ('approved, write the plan'). No prototype (user decision). Plan: `docs/superpowers/plans/2026-10-04-milestone-10a-cheat-files.md` (5 tasks).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestone 5c (cheats: `Cheats.kt`, `CheatPanel` / `CheatDialog` in `Screens.kt`, `NativeBridge.checkCheat` / `setCheats`, the core's `core/src/cheats/`), all merged into `main`.

---

## 1. Goal and scope

**Milestone 10** (cheat-file import, cloud sync, box art; added by the user 2026-10-04) is split (user decision 2026-10-04) into **10a cheat files** (this spec; offline) → **10b box art** (the app's first internet use, download only) → **10c cloud sync**, each with its own spec and plan.

**10a's goal:** import the libretro `.cht` cheat files (the format RetroArch uses; libretro's collection has 514 GBA files) into a game's cheat list.

**Decisions (user, 2026-10-04):**
- **Source:** a file the user picks with Android's file picker (they download `.cht` files themselves). No internet in 10a; finding cheat files online may come with 10b (box art and libretro's cheat files use the same game names).
- **Importing:** every cheat of the file is added, **switched off** (the file's own on/off is ignored — real files have them all off); unreadable ones are skipped and counted; cheats already in the list are not added twice. No pick list.
- **Remove all:** a new button empties a game's cheat list, after a question.
- **Approach:** 1 — the file is read in the app's Kotlin code (pure, tested on the PC) and each cheat checked by the core's existing `checkCheat` (not 2, reading it in the C core).
- **No prototype** (the format is clear from real files): spec → plan.
- **Done when:** Claude's check on the phone emulator passes and all tests pass — **no user check** (user decision).

**Not in 10a:** downloading `.cht` files; picking which cheats to import; the file's on/off flags; RetroArch's memory-search cheats (`cheatN_address`, `cheatN_handler`, …) — entries without a code are skipped; any change to the core.

**Done when:** Claude's check (§5) passes with real libretro files, and every test passes: the new ones, the Kotlin unit tests, the on-device tests, CTest 120 unchanged.

## 2. The `.cht` format (what we read)

Real files (libretro-database `cht/Nintendo - Game Boy Advance/`, read 2026-10-04):

```
cheats = 19

cheat0_desc = "Infinite HP"
cheat0_code = "42000538+07CF+00000004+014C"
cheat0_enable = false

cheat1_desc = "Master Code"
cheat1_code = "D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3"
cheat1_enable = false
```

- Lines `key = value`; spaces round `=` optional; values in quotes or bare (`false`, `19`); inside quotes `\"` and `\\` stand for `"` and `\`. Blank lines and lines starting with `#` are skipped; any other line without `=` is skipped too.
- **We read** `cheatN_desc` (the name; empty or missing → "Cheat N+1") and `cheatN_code` (the codes), N = 0, 1, …, in N order. `cheats = …`, `cheatN_enable` and every other key are ignored. A cheat whose code is missing or empty is not a cheat (skipped, not counted).
- **Codes:** the code is split at `+` (spaces and tabs inside each part removed, empty parts dropped). Going through the parts: an 8-hex-digit part followed by a 4- or 8-hex-digit part makes one line `AAAAAAAA BBBB` / `AAAAAAAA BBBBBBBB`; any other part is a line on its own. The lines are joined with `\n` — the same text a user types into the cheat pop-up (5c), which the core reads (`core/src/cheats/decode.c`: a line of 12 or 16 hex digits, or `AAAAAAAA:VV`).
- The file may start with a UTF-8 byte-order mark and use `\r\n` line ends.
- A file with no cheat (none with a code), or that isn't text, gives no cheats.

## 3. What the user sees (`Screens.kt`, `MainActivity.kt`)

- **The cheat panel** (5c's `CheatPanel`): under **Add cheat**, two buttons: **Import cheat file** (an icon `FileUpload`, outlined) and, only when the list isn't empty, **Remove all** (outlined, in the error colour).
- **Import cheat file** opens Android's file picker (`ACTION_OPEN_DOCUMENT`, any type — `.cht` has no MIME type). The game stays paused behind the panel.
- **Adding:** the file's cheats go after the game's own, switched off, named from the file, type **Auto** (`NativeBridge.CHEAT_AUTO`) — the core works out each one's kind as for a typed cheat. A cheat is **unreadable** if `NativeBridge.checkCheat(codes, CHEAT_AUTO)` reports a bad line; it is skipped. A cheat is **already there** if the list has one with the same name and the same codes (codes compared without spaces, case-insensitive); it is skipped.
- **The note** (a toast) after an import: a first sentence, then — only when something was added or already there and some cheats couldn't be read — a second one.

| Result | First sentence |
|---|---|
| some added | "Added 72 cheats." (1: "Added 1 cheat.") |
| none added, some already there | "No new cheats in this file." |
| none added, none already there (all unreadable) | "No cheats could be read from this file." |
| no cheats in the file, not text, bigger than 1 MB, can't be opened | "This file has no cheats." (no merging) |

  Second sentence: "2 couldn't be read." (1: "1 couldn't be read."). E.g. "Added 70 cheats. 2 couldn't be read."
- **Remove all** asks "Remove all 72 cheats?" (1: "Remove the 1 cheat?"), with **Remove** and **Cancel**; Remove empties the list (saved, sent to the game).
- Saving and sending: as a typed cheat today (`changeCheats`: the cheats file is written, `sendCheats`). All imported cheats are off, so nothing changes in the game until the user switches one on.

## 4. The pieces

- **`ChtFile.kt`** (new, pure Kotlin, tested on the PC):
  - `ChtFile.parse(text: String): List<Pair<String, String>>` — (name, codes) per cheat, in file order (§2).
  - `ChtFile.codeLines(code: String): String` — §2's `+` splitting (public, for its own tests).
  - `ChtFile.merge(existing: List<Cheat>, found: List<Pair<String, String>>, readable: (String) -> Boolean): Merge` — `Merge(cheats: List<Cheat>, added: Int, unreadable: Int, already: Int)`: the existing ones then the new ones (off, `CHEAT_AUTO`); a duplicate inside the file itself counts as already there; `readable` is `checkCheat` in the app, a stand-in in the tests.
  - `ChtFile.note(m: Merge): String` — §3's notes (for the "no cheats in the file" case the caller shows "This file has no cheats." without merging).
- **`Screens.kt`:** `CheatPanel(…, onImport: () -> Unit = {}, onRemoveAll: () -> Unit = {})` — the two buttons and the Remove all question (inside the panel, like 5c's delete question).
- **`MainActivity.kt`:** a picker `pickCht`; `importCheats(uri)`: read the file (≤ 1 MB, UTF-8) → `ChtFile.parse` → empty: the "no cheats" note; else `ChtFile.merge(cheats, found) { NativeBridge.checkCheat(it, CHEAT_AUTO)[1] == 0 }` → `changeCheats(m.cheats)` if anything was added → the note. Remove all → `changeCheats(emptyList())`.

## 5. Testing and Claude's check

- **`ChtFileTest`** (JVM, new): parsing the real excerpts above (names, codes as lines: `42000538 07CF\n00000004 014C`, `D8BAE4D9 4864DCE5\nA86CDBA5 19BA49B3`); `\"` in a name; bare values; no spaces round `=`; `\r\n` and a byte-order mark; N order when the file is out of order; missing / empty code skipped; a missing name → "Cheat N+1"; memory-search keys ignored; not a `.cht` (e.g. a ROM's bytes as text) → empty; `codeLines` with a lone part, an odd count, `AAAAAAAA:VV`, spaces inside parts; `merge`: counts, order, off and `CHEAT_AUTO`, duplicates (in the list and inside the file; spaces and case in codes ignored); every note of §3's table.
- **On-device** (`ScreensTest` +1): the panel shows Import cheat file; Remove all only with cheats; Remove all → the question → Remove calls back; Cancel doesn't.
- **CTest 120/120 unchanged.**
- **Claude's check on the phone emulator** (real libretro files, each download asked for first — e.g. `game A - game A Version (USA, Europe) (Code Breaker).cht`; the user's game A hack, `hack-1.gba`): import → the right count and note ("Added N cheats." — N checked against the file); names in the panel; a cheat's pop-up shows its codes as lines; switch one on → no "can't be read" note and the game plays on; import the same file again → "No new cheats in this file."; a non-`.cht` file → "This file has no cheats."; Remove all → the question → an empty list. The user's own cheats (if any) are backed up first and put back after.

## 6. Files

```
app/src/main/java/com/tengen/gbaemu/ChtFile.kt (new)     parse, codeLines, merge, note
app/src/main/java/com/tengen/gbaemu/Screens.kt           CheatPanel: Import cheat file, Remove all and its question
app/src/main/java/com/tengen/gbaemu/MainActivity.kt      pickCht, importCheats, Remove all
app/src/test/.../ChtFileTest.kt (new); app/src/androidTest/.../ScreensTest.kt (+1)
```
