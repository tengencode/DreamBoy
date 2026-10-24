# Milestone 11 — Backup and Restore — Design

**Date:** 2026-10-04
**Status:** **Implemented — milestone 11 complete 2026-10-04** (tag `milestone-11`): all tests pass (Kotlin 176, on-device 74, CTest 120) and Claude's emulator check passed (one bug found and fixed: same-minute automatic backups). Before that: brainstormed with the user 2026-10-04 (all three design parts approved: 'looks right'; no user check — "your checks are sufficient"); spec approved by the user ('approved, write the plan'). Plan: `docs/superpowers/plans/2026-10-04-milestone-11-backup.md` (7 tasks).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestone 4c (`SaveFiles.kt`, Export / Import save), 5a (`States.kt`), 5c and 10a (`Cheats.kt`), 6b (`Settings.kt`, `KeyBindings.kt`, `GameSettings.kt`, `Store.kt`), 6c (`LayoutStore.kt`), 9 (`SkinFiles.kt`), 10b (`BoxArt.kt`), all merged into `main`.

---

## 1. Goal and scope

**Milestone 11** (added by the user 2026-10-04, after cancelling 10c cloud sync): the user can back up the app's data to **one file they keep themselves**, and restore it on a new phone or after reinstalling. The app never sends data anywhere; there are no accounts, integrations or app keys (user rule, 2026-10-04).

Today only a game's in-game save can leave the app (Export save, 4c). Save states, settings, key mapping, per-game settings, touch layouts, cheats, imported skins and chosen box art cannot.

**Decisions (user, 2026-10-04):**
- **A full backup**, not per game: one file with everything below.
- **Contents:** saves and save states (always), settings and controls (app settings, key / controller mapping, per-game settings, touch layouts), cheats, imported skins, the box art the user chose (or removed).
- **Restore replaces** everything in those parts; the phone's current data is first kept as a **safety copy** (one), and **Undo restore** puts it back.
- **By hand**, with Android's "save as" / "open" pickers (the user chooses where the file goes), **plus an optional automatic backup** into a folder the user picks.
- **Android's own backup is turned off** (`android:allowBackup="false"`): nothing leaves the phone unless the user exports it.
- **Approach 1:** a plain **`.zip`** (`GBA Emulator backup 2026-10-04 14-02.zip`) with the app's files in their own folders, the settings as small JSON files and a `backup.json`. Not 2 (an own `.gbabackup` type) or 3 (a loose folder copy).
- **No password** on the file (saves aren't sensitive; a lost password would lose the backup).
- **Done when:** all tests pass and Claude's emulator check passes — **no user check** (user decision).

**Left out on purpose (Claude's proposal, not objected to):** the user's **BIOS file** (Nintendo's code — kept out of backup files for licensing; picked again after a restore), the **game list and its folder link** (Android's folder permission is per phone; the folder is picked again), the **ROMs**, and **downloaded box art** (it comes back by itself).

**Not in 11:** per-game export / import of states; choosing parts of a backup to restore; merging; encryption; backing up while a game is open; any change to the core.

## 2. What the user sees

- **Settings → Backup** (a new page; a row on the main Settings page: "Last backup today 14:02", or "No backup yet"; "· automatic" added when automatic backup is on).
- **Back up now** — Android's "save as" picker with the name `GBA Emulator backup <yyyy-MM-dd HH-mm>.zip`; the user picks the place (Downloads, a USB stick, any app they trust). A spinner "Backing up", then "Backup saved (12 MB)."
- **Restore from backup** — the user picks a `.zip`; the app checks it first, then asks: **"Restore this backup?"** — "Made on phone A, 4 Oct 2026 14:02: 6 games, 23 save states, settings, cheats, 2 skins. Everything on this phone is replaced; what's here now is kept as a safety copy." **Restore** / **Cancel**. A spinner "Restoring", then "Restored." — plus "Choose your BIOS file again in Settings → BIOS." when the backup had the BIOS file switched on and this phone has no BIOS file.
- **Undo restore** — shown while a safety copy exists; asks "Put back what was on this phone before the last restore?" and restores the safety copy the same way.
- **Automatic backup** — a switch, **off** by default. Turning it on opens Android's folder picker; the page then shows the folder's name and **Change folder**. A backup is written there after the user **quits** a game, if the last automatic one is more than 24 hours old; the newest **5** are kept (only files the app named `GBA Emulator backup … .zip` in that folder are counted or deleted).
- **During a game** (Settings from the pause menu): the page shows "Quit the game to back up or restore."; Back up now, Restore and Undo are greyed out (the automatic switch and folder can still be changed).
- A help line: "Backups stay where you put them. The app doesn't send them anywhere, and Android's own backup is off for this app."
- Labels follow 8a's rule: no "…" and no "&".

## 3. The backup file

A zip (`java.util.zip`):

```
backup.json                                   what this is (below)
saves/<id>.sav                                in-game saves
states/<id>/<auto|quick|slot1..slot10>.<state|png>
cheats/<id>.json
layout.json                                   touch layouts (6c)
skins/<n>/…                                   imported Delta skins, whole folders (9)
boxart/choices.json                           {"<id>": "own" | "removed"} (10b: only the user's own choices)
boxart/<id>.jpg                               the pictures of "own" entries
prefs/settings.json, prefs/keys.json, prefs/game_settings.json   {"name": number}
```

`backup.json`: `{"version": 1, "app": "0.1", "made": <ms>, "phone": "phone A", "games": 6, "states": 23, "cheats": 4, "skins": 2}` (games = ids with a save or a state; states = `.state` files; cheats = cheat files; skins = skin folders).

Only paths matching these patterns are written or accepted (`<id>` = 8 hex digits, `<n>` = a whole number; inside `skins/<n>/` any relative path, subfolders allowed, as a skin's own files are; nowhere a `..` part, a leading `/` or a `\`).

## 4. How it works

- **Back up:** on a background thread, every file above is streamed into the zip; the three preference files are read through a new **"all values"** read of `IntStore` (all the app's preferences are whole numbers). Nothing on the phone changes. Not offered while a game is open.
- **Restore** (from the game list only):
  1. **Check:** the zip is unpacked into `files/restore/incoming/`; it is refused (and the folder deleted) if `backup.json` is missing, unreadable or of a newer version, if any path doesn't match §3, or if it unpacks to more than 1 GB.
  2. **Safety copy:** the current data is written as `files/restore/safety.zip` (the same writer as Back up now; the previous safety copy is replaced).
  3. **Swap:** each part is replaced — `saves/`, `states/`, `cheats/`, `layout.json`, `skins/` (folders moved), the box-art choices (the phone's own / removed entries and their pictures dropped, the backup's applied; downloaded entries stay), the three preference files (cleared, then the backup's values written). `Settings.biosFile` is switched off if this phone has no BIOS file.
  4. **Roll back:** if a step of 3 fails, the safety copy is applied the same way and the user sees "Couldn't restore; nothing was changed."
  5. **Reload:** the app reads everything again (settings, keys, game settings, layouts, skins, box art, the list's pictures) — no restart.
- **Undo restore** = steps 1, 3–5 with `safety.zip` (no new safety copy is made); afterwards `safety.zip` is deleted, so Undo is offered once per restore.
- **Automatic backup:** after quitting a game (the save and the Continue state are written then), if the switch is on and the last automatic backup is ≥ 24 h old, a backup is written into the folder (Android's folder access) on a background thread; then the app's own backup files there beyond the newest 5 are deleted. Not on Home (a paused game may resume while its save is copied).
- **Android's own backup:** `android:allowBackup="false"` in the manifest, plus `android:dataExtractionRules` excluding every domain from cloud backup and device-to-device transfer (on Android 12+ `allowBackup="false"` alone doesn't stop the transfer).

## 5. When things go wrong

- **The picked place can't be written** (full, a USB stick removed): "Couldn't save the backup." (the half-written document deleted if Android allows).
- **Not a backup, damaged, newer version, or unsafe paths:** "That file isn't a backup this app can restore." — nothing changes.
- **Restore fails part-way:** rolled back (§4); "Couldn't restore; nothing was changed."
- **Automatic backup fails** (folder gone, permission lost, full): one note "Automatic backup failed. Check the folder in Settings → Backup."; tried again after the next quit; games are not affected.
- **A state from an older app version** restores but may refuse to load — as today.

## 6. The pieces

| Piece | What it does |
|---|---|
| `Backup` | writes the zip from `filesDir` + preference maps; reads `backup.json`; checks and unpacks a zip into a folder (§3 patterns, 1 GB limit) — plain Kotlin, tested on the PC |
| `Restore` | the safety copy, the swap of each part, the roll-back — plain Kotlin, tested on the PC |
| `BackupSchedule` | the automatic rule: due (≥ 24 h), which of the folder's files are ours, which to delete (keep 5) — tested on the PC |
| `IntStore.all()` / `clear()` | every value of a preference file, and emptying it (`PrefsStore`, `MemoryStore`) |
| `BackupFolder` | Android's folder access: write a new document, list and delete the app's backup files |
| Screens | the Backup page and row, the restore question, the spinners |
| `MainActivity` | the pickers, the threads, the reload, the automatic backup after quitting |
| `AndroidManifest.xml` | `android:allowBackup="false"` |

The automatic backup's folder (a URI, a string — not for the whole-number settings store) and the times live in `files/backup-state.json` (`folder`, `lastAuto`, `lastBackup`), read and written by `BackupSchedule`; it is not part of a backup (the folder permission is per phone). The Backup row's "Last backup" is `lastBackup` (by hand or automatic).

## 7. Testing and Claude's check

- **PC (Kotlin unit tests):** a backup of a made-up `filesDir` has exactly the §3 paths and a right `backup.json`; back up → restore into an empty `filesDir` gives the same files and preference values; restore replaces (a state not in the backup is gone) and keeps downloaded box art; unsafe zips are refused untouched (`../x`, `/x`, an unknown folder, no `backup.json`, version 2, over the limit); a failure in the middle of the swap rolls back to the safety copy; Undo puts the old data back; `Settings.biosFile` switched off without a BIOS file; the schedule (not due before 24 h, keeps our newest 5, ignores other files).
- **On-device (emulator):** the Backup page (buttons, the in-game note, the switch), the restore question; a real backup to a file and a restore through the app's code.
- **Claude's check on the emulator** (the app's data backed up by adb first and put back after): Back up now to Download; change things (delete a state, change settings and a key, add a cheat, remove a skin); Restore → all back, the game resumes from its Continue state; Undo restore → the changes back; Restore again; automatic backup into a picked folder after quitting a game, not again within 24 h (time moved by editing `backup-state.json`), 6 runs → 5 files; a random `.zip` and a renamed text file refused with nothing changed; Android's backup off (`adb shell dumpsys package com.tengen.gbaemu | grep ALLOW_BACKUP` shows it gone).
- All PC tests (CTest 120) unchanged.

## 8. Files

- New: `Backup.kt`, `Restore.kt`, `BackupSchedule.kt`, `BackupFolder.kt`; tests `BackupTest`, `RestoreTest`, `BackupScheduleTest`; on-device screen tests in `ScreensTest`, a backup-and-restore test in `BackupDeviceTest`.
- Changed: `Store.kt` (`all`, `clear`), `SettingsScreens.kt` (the Backup page and row), `Screens.kt` (the restore question, spinners), `MainActivity.kt`, `AndroidManifest.xml`.
