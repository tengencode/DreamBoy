# Milestone 6a — Game Library — Design

**Date:** 2026-09-28
**Status:** Implemented — milestone 6a complete 2026-09-28 (tag `milestone-6a`). Approved design (brainstormed with the user 2026-09-28); prototyped (branch `proto-6a`) and tried by the user ('Everything works', 2026-09-28); plan: `docs/superpowers/plans/2026-09-28-milestone-6a-library.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 app shell, §10 milestones)
**Builds on:** milestone 5 (tag `milestone-5c`: the in-game menu, `States` and their pictures, cheats), 4c (`GameId`, `files/saves/<id>.sav`, the import question, `LastGame`).

---

## 1. Goal and scope

Milestone 6 (the Kotlin app shell) is split in three (user decision 2026-09-28): **6a game library** → 6b settings + controller remapping → 6c skins + layout editor.

**6a finish line:** on the Pixel 9 emulator the user picks their games folder once; the app lists its games; they open game A from the list, quit, and see it at the top with its picture and time; they export their save to Download. Automatic tests prove the list's order, its file (saved, read back, damaged), reading games from `.zip` files, the save-size check, and export → import giving back the same bytes; the core does not change (CTest 120/120).

**User decisions (2026-09-28):**
- **Split:** 6a library → 6b settings + remapping → 6c skins + layout editor (option A of 3).
- **Folder + single files** (option A): the user picks a games folder once (with its subfolders); games dropped in later appear by themselves. "Open a game…" stays for one file elsewhere, and such a game **stays in the list** (marked "outside the folder") until "Remove from list".
- **A list** (layout B of 3, chosen in the visual companion): one game per row — a small picture, the name, when it was last played.
- **Hold menu** (option A): Start fresh, Import save…, Export save… (+ Remove from list for outside games).
- **Names from the file name** (option A): the display name without `.gba` / `.zip` (hacks of one game differ; the header title is 12 capitals and the same for every game A hack). Also used for the in-game menu's title.
- **Download itself can't be the games folder** (found while prototyping; user decision 2026-09-28, option A of 3): Android's folder picker refuses the Download folder itself and the storage's top folder ("Can't use this folder"); any folder inside them works. The first-start screen says so: make a folder such as GBA (the picker's Create new folder) and put the games in it. (Rejected: a "skip" button to use the app without a folder; Android's "All files access" permission.)
- **A light list** (option A of 3): the folder's file names are asked for each time the library shows; a game's id (the CRC-32 of its bytes, the key of its saves and states) is learnt only when it is opened. No game is read ahead. (Rejected: reading every game once in the background — slow, battery, and a never-played game has no picture anyway; copying games into the app — double the space.)

| In scope (6a) | Later |
|---|---|
| Games folder (tree permission kept), subfolders, `.gba` and `.zip` | `.7z` / `.rar` (a library download); box art (out of scope v1) |
| The list: picture, name, last played; tap to play | Search, sorting options, renaming in the app |
| Hold menu: Start fresh, Import save…, Export save…, Remove from list | Deleting saves from the app; settings (6b) |
| First-start screen; ⋮ menu: Open a game…, Change folder…, Controls | ⚙ settings (6b) |

## 2. Files

```
app/src/main/java/com/tengen/gbaemu/
  Json.kt           the small JSON reader/writer, moved out of Cheats.kt and shared (Cheats.kt uses it; its file format unchanged)
  Library.kt        the list: stored entries + the folder's files → rows (sorted); files/library.json (own small JSON code, as Cheats.kt)
  FolderScan.kt     the folder and its subfolders through DocumentsContract (no androidx.documentfile: a download); runs off the main thread
  RomFile.kt        a game's bytes from a .gba, or from the first .gba inside a .zip (java.util.zip); the 32 MiB limit
  SaveFiles.kt      export (copy files/saves/<id>.sav out) and import (size check, .sav.bak, auto state set aside)
  Screens.kt        + LibraryScreen (rows, ⋮ menu, hold menu), FirstStartScreen; StartScreen goes
  MainActivity.kt   + the folder picker (OpenDocumentTree), the list shown when no game runs, opening from a row
  LastGame.kt       removed: its entry is carried into the list once (below)
app/src/test/.../LibraryTest.kt, RomFileTest.kt, SaveFilesTest.kt
app/src/androidTest/.../LibraryDeviceTest.kt
```

## 3. The list

- **Entry** (in `files/library.json`): `{key, uri, name, id, lastPlayed, outside, size}` — `key` = the file's provider + document id (the same file reached through the folder and through the picker has different URIs but, from the same provider, the same key); `id` empty until the game is opened; `lastPlayed` 0 until then; `size` in bytes (0 = unknown). The folder's tree URI is stored beside the entries (`folder`).
- **Rows** = every game file found in the folder now (joined with its entry by key when there is one) + every `outside` entry. **Found while prototyping:** files picked through the picker's "Downloads" come from another provider (keys like `…downloads.documents/msf:51`), so an outside game with the **same name and size** as a folder file is taken to be that file: it joins the folder's entry (the more recently played of the two decides the id and time), also when the folder's copy was listed before; an outside game whose size is unknown gets it when the list is read (no duplicate row after copying or moving a game into the folder). In-folder entries whose file is gone are dropped. Order: played games by `lastPlayed`, newest first; then the rest A–Z by name (case-insensitive).
- **Row text:** the name; under it "Today 14:00" / "Yesterday" / a date, or "Never played"; "· outside the folder" for outside games. **Picture:** `files/states/<id>/auto.png` when the id is known and the file exists; else a plain tile.
- **Found files:** names ending `.gba` (any case), and `.zip` files — a zip is listed only if it holds an entry ending `.gba`; checking that means reading its table of contents, so zips are checked in the background and appear when checked.
- **When it is refreshed:** each time the list shows (app start, returning from a game, back from the pickers).
- **Opening a game** (tap): read with `RomFile`; its id computed (as today); the entry gets `id`, `lastPlayed` = now; then as today (the import question for a game with a save chip and no save; the automatic state resumed). **Start fresh** = the same with the automatic state not resumed (as Restart game).
- **The carried-over last game:** at the first start of this version, `LastGame`'s URI and title become an entry (outside the folder unless the folder, when picked, contains it; `lastPlayed` = now), and `LastGame`'s preferences are cleared.
- **Games already played before 6a** (e.g. game A in Download) show "Never played" with no picture until opened once from the list — their saves and states are found by id, so a tap continues exactly where they left off.

## 4. Saves in the hold menu

- **Export save…:** the id (computed by reading the game if not known yet) → `files/saves/<id>.sav`; none → "No save for this game yet." Else the system's "create file" picker, suggested name `<name>.sav`, and the bytes copied.
- **Import save…:** the id (computed by reading the game if not known yet); the system's file picker; the size must be one a save chip can have (512, 8192, 32768, 65536 or 131072 bytes; `SaveFiles.validSize`) — else "That file isn't a save for this game." If a save exists: "Replace this game's save?" (Replace / Cancel). The old save is kept as `<id>.sav.bak`; the new one written safely (temporary file, then swapped). **The automatic state is set aside** (renamed `auto.state.old`, its picture `auto.png.old`): since 5a a state carries the save RAM, so resuming it would bring the old save back. The next start is from the title with the imported save.
- **Remove from list** (outside games only): the entry is dropped; nothing is deleted.

## 5. The screens

- **First start** (no folder, or the folder can no longer be opened): "GBA Emulator", a line of help, **Choose games folder**, **Open a single game…**, **Controls**; with a lost folder the line reads "Can't open the games folder any more — choose it again."
- **Library:** top bar "Games" and ⋮ (Open a game…, Change folder…, Controls); the rows; an empty folder → "No games found in <folder name>" and **Change folder**.
- **Hold menu:** a sheet titled with the game's name: Start fresh (from the title screen), Import save…, Export save…, Remove from list (outside games only).
- The in-game menu's title and the cheat panel use the row's name.

## 6. Error handling

| Case | Behaviour |
|---|---|
| Folder deleted or permission lost | First-start screen with the "choose it again" line |
| No games in the folder | "No games found in <folder>" + Change folder |
| A listed game can't be read | "Can't open <name>."; for outside games the hold menu offers Remove from list |
| A `.zip` without a `.gba` | Not listed |
| Over 32 MiB or not a GBA game | "Not a GBA game." (as today) |
| Import: wrong size | "That file isn't a save for this game."; nothing changes |
| `library.json` damaged | Renamed `library.json.bad`; the list rebuilt from the folder (only the last-played times are lost); a toast |

## 7. Testing

1. **JVM tests** (test-first): `LibraryTest` — the order (played by time, then A–Z, case-insensitive), joining folder files with entries, dropping in-folder entries whose file is gone, keeping outside ones, the JSON round trip, a damaged file; `RomFileTest` — a `.gba` in a zip (built in the test), the first `.gba` of several, a zip without one, the 32 MiB limit; `SaveFilesTest` — the valid sizes, import keeps `.bak` and sets the automatic state aside, a wrong size changes nothing.
2. **No change in the core:** CTest 120/120, the PC unit tests as before.
3. **On-device** (`LibraryDeviceTest`, emulator only): a game opened from a zip runs (its id = the CRC of the `.gba` inside); export then import gives back the same save bytes.
4. **Claude's check** (adb, emulator; the demos only — the user's saves untouched): a test folder `Download/GBA` (m7_demo.gba, a zip holding obj_aff.gba, a zip without a game); first start → Choose games folder → Download → GBA → Use this folder → Allow → the two games listed, the gameless zip not; open `m7_demo` → quit → it is on top with its picture and "Today"; the hold menu; Export save… of a demo (no save chip) → "No save for this game yet." (the export → import byte round trip is proven by `LibraryDeviceTest`).
5. **User check:** pick their folder (a folder inside Download), open game A from the list, quit → on top with its picture and time; export their save to Download.

## 8. Done when

- All tests in §7 pass; CTest 120/120 on the PC.
- The user's check (§7.5) passes.
