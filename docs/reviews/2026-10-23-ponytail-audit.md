# Whole-repo audit — 2026-10-23 (ponytail-audit)

Read-only audit of the whole repo at `3cd8aa9`, before the free and open-source release. Five reviewers read the code by area (app data, app network and imports, native layer, core, graphics/sound/tools/repo); every finding below was re-checked against the code. No code was changed.

**What this repo does:** a Game Boy Advance emulator for Android. The emulator itself is C with hand-written ARM64 assembly; a Kotlin app gives the game list, menus, settings, backups, skins and RetroAchievements. **Load assumed:** one player on one phone at 60 fps — but once public, files from other people (shared backups, save states, skins, saves) must be treated as untrusted.

## Must fix

1. **A save-state file can crash the app or corrupt its memory** (`core/src/state.c:106-142`) — ✅ **fixed 2026-10-24** (branch `audit-fixes`)
   - **What this is:** loading a save state copies the saved emulator data straight back in.
   - **Problem:** only the header and chunk sizes are checked, not the values inside. A state with a bad Flash bank, ROM size or sound position makes the emulator read and write far outside its memory; a bad timer table freezes it. States come in through restored backups, so one bad file from someone else is enough.
   - **Fix:** keep `rom_size` and `crc32` from the loaded game (like the `rom` pointer already is), refuse states whose few index fields are out of range (Flash bank ≤ 1, save type, EEPROM position, RTC bit, sound ring and FIFO positions, the 3 always-due events), and add one unit test that breaks each field.
   - **If we skip it:** a shared or damaged state can crash the app or worse.

2. **A save that "doesn't fit" gets overwritten during play** (`platform/android/emu_thread.c:362-370`, `save_file.c:52-71`) — ✅ **fixed 2026-10-24**
   - **What this is:** when a `.sav` is the wrong size for the game, it is not loaded and the game starts with a blank save; the app says the file was left untouched.
   - **Problem:** the first in-game save writes over it (the old one moves to `.bak`), and the second save overwrites `.bak` too. Example: a 64 KiB save imported from the hold menu for a 32 KiB game, or a wrong save type picked in game settings — two saves later the original is gone.
   - **Fix:** in the misfit branch, keep the file aside as `<id>.sav.misfit` before the game starts, and say so in the message.
   - **If we skip it:** imported progress can be destroyed without warning.

3. **Picking a big file by mistake crashes the app** (`app/.../MainActivity.kt:432-436`, used at `:637`, `:780`, `:1299`) — ✅ **fixed 2026-10-24**
   - **What this is:** reading the file the user picked for "Import save" or a BIOS.
   - **Problem:** the whole file is read into memory on the screen's thread before the size is checked. Picking a 2 GB video freezes the app, then crashes it (out of memory is not caught by `catch (e: Exception)`).
   - **Fix:** use the existing `readUpTo(uri, max)` — 128 KiB for saves, `Bios.SIZE` for the BIOS.
   - **If we skip it:** one wrong tap in the file picker crashes the app.

4. **No licence and no README** (repo root) — ✅ **fixed 2026-10-24** (GPL-3.0-or-later; a new clean public repo)
   - **What this is:** the files that say people may use the code and how to build it.
   - **Problem:** without a `LICENSE`, published code is "all rights reserved": nobody may legally copy or fork it, and F-Droid won't take it.
   - **Fix:** add `LICENSE` (the user picks, e.g. GPL-3.0 or MIT) and a short `README.md` (what it is, how to build, "bring your own games and BIOS", link to `third_party/README.md`).
   - **If we skip it:** the "open source" release isn't open source.

## Should fix

5. **A failed save write is never tried again** (`platform/android/emu_thread.c:196-209`) — ✅ **fixed 2026-10-24**
   - **What this is:** the game's save is written to the phone about a second after the game saves.
   - **Problem:** if that write fails (storage full), nothing retries; the "changed" mark is already cleared, so pausing and quitting write nothing. The user frees space, quits, and the in-game save is lost.
   - **Fix:** one line in the failure branch: `save_timer_frame(&e->save_timer, true);`.
   - **If we skip it:** lost progress after a full-storage moment.

6. **Personal details and the user's game list are in tracked files and in git history** (`docs/PROGRESS.md` Notes, three 7a/7b docs, `tests/games/games.txt`, `tests/bench/scenes.txt`) — ✅ **fixed 2026-10-24** (GPL-3.0-or-later; a new clean public repo)
   - **What this is:** working notes written for a private repo.
   - **Problem:** they hold both phones' serial numbers, the user's Downloads folder contents and personal paths, and an exact list (with fingerprints) of the commercial games and hacks the user owns. Deleting the lines now leaves them in every old commit.
   - **Fix:** move the user's own games into a gitignored local list that the tools also read; strip serials, Downloads list and paths from the docs; publish from a fresh one-commit repo (or rewrite history). The user decides how far to go.
   - **If we skip it:** that information is public forever.

7. **A bad backup can crash a restore halfway, and the undo never runs** (`app/.../Restore.kt:68-112`, `Backup.kt:133-166`) — ✅ **fixed 2026-10-24**
   - **What this is:** restoring a backup `.zip`, maybe one from someone else.
   - **Problem:** only the zip's total size is capped (1 GiB). A small text file that unpacks to hundreds of MB is read whole; out of memory is not caught, so the rollback is skipped and the phone is left with half the backup's data.
   - **Fix:** refuse any `backup.json`, `prefs/*`, `boxart/*` or `choices.json` entry over 1 MiB in `unpack`, and catch `Throwable` in `swapOrRollBack` and `unpack`; one test for the per-file cap.
   - **If we skip it:** a damaged or booby-trapped backup leaves a half-restored phone.

8. **A broken Delta skin crashes every game start** (`app/.../DeltaSkin.kt:85-95`, `SkinFiles.kt:53-56`) — ✅ **fixed 2026-10-24**
   - **What this is:** importing a `.deltaskin` and drawing it over the game.
   - **Problem:** a skin whose size is 0 × 0 imports fine (the thumbnail error is swallowed), then the scale becomes infinity and the game screen crashes on every open until the skin is changed. A very tall size or very deeply nested `info.json` crashes the import itself.
   - **Fix:** in `DeltaSkin.face`, refuse sizes that aren't finite, above 0 and within about 1:20; catch `Throwable` on the import thread and show "not a skin".
   - **If we skip it:** one bad skin file means a crash on every game start.

9. **There is no release build; the only app the tools make is the debug one** (`app/build.gradle.kts`, `tools/dev.sh` `apk`) — ✅ **fixed 2026-10-24**
   - **What this is:** the build that would be published.
   - **Problem:** a debug build lets anyone with USB debugging read the RetroAchievements sign-in token and turn on the pretend server; it's signed with this PC's throwaway debug key, which can't be changed later.
   - **Fix:** a `release` signing setup with the key in a gitignored file, and a `dev.sh apk-release` command (release builds are already non-debuggable).
   - **If we skip it:** token exposure and a stuck signing key.

10. **A condition at the end of one cheat also controls the next cheat** (`core/src/cheats/apply.c:60-73`, `decode.c:458-511`) — ✅ **fixed 2026-10-24**
    - **What this is:** all switched-on cheats run as one list.
    - **Problem:** an "if" is only cut off at the end of the whole list. Cheat A ending in "while A is held" makes cheat B (infinite HP) work only while A is held. An Action Replay block with no "end if" runs its codes always.
    - **Fix:** at the end of `cheat_decode`, close an open block and clamp each "if" so it can't reach past the cheat's own codes; one test with two cheats.
    - **If we skip it:** cheats break each other depending on which are on.

11. **Using one folder for games and automatic backups can break backups and fill storage** (`app/.../MainActivity.kt:471-476`, `:1193-1208`) — ✅ **fixed 2026-10-24**
    - **What this is:** changing the games folder, and the automatic backup's clean-up of old copies.
    - **Problem:** changing the games folder gives back the read permission the backup folder still needs. After that, listing the backup folder fails — and because the clean-up is in the same `try` as the backup, a good backup is reported as failed, never counted, and a new full backup is written on every quit with none deleted.
    - **Fix:** don't release the old folder if it is the backup folder (`&& old != backupSchedule.folder`); count the backup as done right after writing and give the clean-up its own `try`.
    - **If we skip it:** storage quietly fills with backups while the app says they fail.

12. **Saves from mGBA for clock games are refused** (`core/src/cart/save.c:111-115`) — ✅ **fixed 2026-10-24**
    - **What this is:** importing a `.sav` made by another emulator.
    - **Problem:** mGBA adds 16 bytes of clock data to saves of games with a clock (game A), so the size is "wrong" and the import is refused. Nothing is lost, but the user's main games can't move over from mGBA. (Medium confidence on mGBA's exact format.)
    - **Fix:** at the top of `gba_load_saveram`, ignore a 16-byte tail: `if ((size & 0xFF) == 16) size -= 16;`.
    - **If we skip it:** players coming from mGBA can't bring their game A saves.

13. **Writing one byte of a timer's start value mixes in the live count** (`core/src/mem/io.c:128`, `:38`) — ✅ **fixed 2026-10-24**
    - **What this is:** games set a timer's start value; some write it one byte at a time.
    - **Problem:** the other byte is taken from the running counter, not the stored start value. Writing 0x34 then 0x12 to a stopped timer at 0 gives 0x1200 instead of 0x1234 — wrong music speed or timing.
    - **Fix:** for the timer start registers, merge with the stored value (`io.raw`) instead of `read16`; one unit test.
    - **If we skip it:** the rare game that does this runs its timer at the wrong speed.

14. **Achievements that watch save memory are switched off on an EEPROM game's first play** (`platform/android/emu_thread.c:226-229`) — ✅ **fixed 2026-10-24**
    - **What this is:** RetroAchievements checks once, at game start, that every address it watches exists.
    - **Problem:** an EEPROM game with no save yet reports a save size of 0, so achievements and leaderboards that read save memory are marked unsupported until the game is reopened.
    - **Fix:** report the full 64 KiB save window (`RA_SRAM_END - RA_SRAM_AT`); the core's buffer is always 128 KiB, so the bytes exist.
    - **If we skip it:** some achievements can't unlock on first play.

15. **`.gitignore` misses the viewer's screenshots and zipped games** (`.gitignore`, `tools/viewer/main.c:224-227`) — ✅ **fixed 2026-10-24**
    - **What this is:** the guard that keeps the user's games and pictures out of commits.
    - **Problem:** F12 in the PC viewer saves `gbaview-0.png` (a screenshot of a commercial game) in the repo root, not ignored; `*.zip` and `*.wav` aren't ignored either. One `git add -A` publishes them.
    - **Fix:** add `gbaview-*.png`, `*.zip`, `*.wav` and `tests/bios/` to `.gitignore`.
    - **If we skip it:** a private screenshot or game zip can be pushed by accident.

16. **The fast ARM64 code is switched on for every ARM64 computer** (`core/CMakeLists.txt:9-15`) — ✅ **fixed 2026-10-24**
    - **What this is:** the build picks the assembly whenever the machine is ARM64.
    - **Problem:** the assembly uses Linux/Android-only syntax, so a contributor on an Apple Silicon Mac can't build; on Linux ARM64 with the warning log on, one unit test and the game check fail because the assembly skips a warning the C version records.
    - **Fix:** only use it on Android or Linux and not with `GBA_NOTES` (one line in the `if`).
    - **If we skip it:** the first Mac contributor can't build.

17. **The Gradle download isn't checked** (`gradle/wrapper/gradle-wrapper.properties`) — ✅ **fixed 2026-10-24**
    - **What this is:** a fresh clone downloads Gradle (the Android build tool) and runs it.
    - **Problem:** every other download in the repo is checked against a fixed fingerprint; this one isn't.
    - **Fix:** add `distributionSha256Sum=` with gradle.org's published SHA-256 for `gradle-8.14.3-all.zip`.
    - **If we skip it:** the one unchecked code download in a public build.

18. **Every return to the app re-reads every zip in the games folder** (`app/.../FolderScan.kt:339-346`, `RomFile.kt:38-43`, `MainActivity.kt:490-520`) — ✅ **fixed 2026-10-24**
    - **What this is:** checking which zips hold a GBA game.
    - **Problem:** a zip with no GBA game is read to its end — on every return to the app, and a new scan starts while the last one still runs. A 2 GB zip of other consoles' games is read again and again.
    - **Fix:** remember each zip's answer by name and size; don't start a scan while one is running.
    - **If we skip it:** battery and a slow game list for people with big zips.

## Nice to have

19. **Slow file work on the screen's thread** (`app/.../MainActivity.kt:557-566` choose box picture, `:657-675` open a game) — ✅ **fixed 2026-10-24** (box picture, then opening a game)
    - **What this is:** choosing a box picture reads up to 50 MiB and decodes it; opening a zipped game unzips up to 32 MiB.
    - **Problem:** both run on the screen's thread; a picture from Google Drive or a zipped game on an SD card freezes the app and can show "app isn't responding".
    - **Fix:** do the reading in a background thread behind the existing spinner, as `importSkin` already does.
    - **If we skip it:** occasional freezes.

20. **Copies that must change together, and a little dead code** (lean) — ✅ **fixed 2026-10-24** (all but folding bus.c's shared access start)
    - **What this is / Problem:** the same code exists in several places, so a fix to one copy misses the others (finding 7's unsafe writer is one that was left out):
      - "write to `.tmp`, then rename" in 9 places (`Library.kt`, `LayoutStore.kt`, `Cheats.kt`, `BoxArt.kt` ×2, `BoxArtHelper.kt`, `Bios.kt`, `RaAccount.kt`, `SaveFiles.kt`) — and `BackupSchedule.save` doesn't use it;
      - `core/src/mem/bus.c` repeats three helpers from `bus_inline.h` and the same 8-line start in six access functions;
      - `Net.get` repeats `Net.send`;
      - the button-script parser is in both `tools/testrunner` and `tools/bench` (the bench copy caps at 512 events; the `game_d` scene has 504);
      - `find_bin_dir`/`tool` in two Python tools;
      - `NativeBridge.romTitle` / `emu_rom_title` have no caller.
    - **Fix:** one `File.writeSafely` helper; use `bus_inline.h`'s helpers; `get` built on `send`; the parser in `tools/common`; import the Python helpers; delete `romTitle`.
    - **If we skip it:** future fixes land in one copy only.

11 smaller findings were left out: automatic backup racing with a restore, "Export save" leaving an empty file after the app restarts, `backup-state.json` written in place, a rare badge-picture name clash, old skin cache pictures piling up (and going into backups), the game picture redrawn every screen refresh while paused, an unchecked render-thread start, an Action Replay "else" right after its "if" flipping the logic, no direct NEON-vs-C test of the picture mixer, the game-check tool's tests not part of `dev.sh test`, and `dev.sh`'s fixed Android Studio path.

**The 11 smaller findings, after (2026-10-24):**
- ✅ Automatic backup racing with a restore — `Restore.backUp` / `apply` / `undo` take turns.
- ✅ "Export save" leaving an empty file after the app restarts — the file is deleted and the user told.
- ✅ `backup-state.json` written in place — written safely since finding 20 (`File.writeSafely`).
- ✅ Old skin cache pictures piling up and going into backups — one per way round, left out of backups.
- ✅ An unchecked render-thread start — `render_create` checks `pthread_create`.
- ✅ An Action Replay "else" right after its "if" flipping the logic — fixed in `decode.c`.
- ✅ The game-check tool's tests not part of `dev.sh test` — CTest `game_check_tools`.
- ✅ `dev.sh`'s fixed Android Studio path — `JBR` and `GBA_AVD` can be set.
- Open: a rare badge-picture name clash (a game icon and a badge with the same number); the game picture redrawn every screen
  refresh while paused (battery); no direct NEON-vs-C test of the picture mixer.

**Verdict:** the emulator itself is solid (no memory errors found in graphics, sound or the assembly); fix 1–3 first (crash and save-loss risks), and 4 and 6 before the code goes public.

**Lean:** about −170 lines possible, no dependencies to drop.

**Not checked:** rcheevos itself; parts of the ARM64 CPU assembly's instruction handlers and `arm.c` / `thumb.c` (covered by the test suites and twin tests, not re-read); most of the Compose screen code (`Screens.kt`, `SettingsScreens.kt`, `AchievementScreens.kt`); nothing was built or run.
