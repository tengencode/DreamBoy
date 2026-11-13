# Milestone 17b — Game Boy and Game Boy Color in the App, Release 1.1.0 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task
> (inline in one session — `CLAUDE.md`; never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DreamBoy opens `.gb` and `.gbc` games from the same game list as GBA games — picture, sound, saves, the
cartridge clock, save states, rewind, fast-forward, touch buttons, console skins, box art and backup all work for
them — and ships as 1.1.0.

**Architecture:** The Android C layer calls `machine_*` (17a) instead of `gba_*`; GBA-only features go through
`machine_gba()` and are simply off when it is NULL. The picture's size travels with the frame (triple buffer →
OpenGL → fit rectangle). Kotlin learns each game's console once (from the file's bytes, as `machine_detect` does) and
keeps it in the game list; everything that depends on the picture's shape or the buttons asks that.

**Tech Stack:** C11 (NDK 27.2, CMake), Kotlin 2.2 + Jetpack Compose, `bash tools/dev.sh` (`unit`, `test`, `jvmtest`,
`apk`, `androidtest`), the impeccable skill for UI.

**Spec:** `docs/superpowers/specs/2026-11-12-milestone-17-game-boy-design.md` (§6, §9.2, §4). 17a's results:
`docs/PROGRESS.md`.

## Global Constraints

- Test-first; one commit per task; commit messages end with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.
- `ponytail` before code, `ponytail-review` per finished task; UI work through the **impeccable** skill, keeping to
  `DESIGN.md` and `PRODUCT.md`.
- After every task: tick boxes, update `docs/PROGRESS.md`, check no document is stale; layman replies.
- Public-safe: no commercial game titles, phone models, serials or personal details in tracked files or commit
  messages; `python tools/public_scan.py` says `clean` before each commit.
- **Nothing changes for GBA games:** every existing test passes (unit 601, CTest 270, Kotlin 269, on-device 93,
  `games --check` 36/36 on 2026-11-13); existing saves, states, layouts, settings and backups keep working.
- UI wording rules (`UiTextTest`): sentence case, buttons start with a verb, no "…", "&", "please", "!".
- No new libraries, integrations or network calls (box art uses the existing download code with new list names).
- No ROMs committed except our own hand-built test ROMs (`tests/own_roms/`).
- The phone emulator: ask before restarting it; drive it by element text, never blind coordinates. The user's real
  phone only when the user asks.
- Pushing, tagging and publishing only on the user's OK at that moment.
- Branch `milestone-17`; merge into `main` (`--no-ff`) only when 1.1.0 is ready.

## Review Focus

1. **A GBA save state or save file opened for a Game Boy game with the same id (or the reverse)** — a CRC clash is
   near impossible, but a restored backup can hold anything: the state is refused with the normal message, the save
   loads by the forgiving rule, nothing crashes — Task 1 (on-device test).
2. **A game list written by 1.0.0** (no console field): every entry reads as GBA and still opens — Task 3.
3. **An unsupported cartridge or a broken `.gb` file:** a plain message, back in the list, the previous game's
   session cleanly gone — Task 3.
4. **Switching between a GBA game and a Game Boy game without leaving the app:** the picture's shape, the buttons,
   rewind and sound follow the new game at once (no stale 240×160 frame, no L / R left over) — Tasks 1, 4, 5.
5. **A Game Boy game with achievements signed in and Hardcore on:** no session, no limits, one notice — Task 6.

## File map (what changes)

| Area | Files |
|---|---|
| Native | `platform/android/CMakeLists.txt` (+ `core_gb`, `machine`), `emu_thread.c/.h`, `jni_bridge.c`, `jni_ra.c`, `triple_buffer.c/.h`, `video_gl.c/.h`, `fit_rect.c/.h`, `gl_readback.c/.h`, `render_thread.c`; `tests/unit/test_platform.c` |
| Test ROM | `tools/build_test_roms.py` → `tests/own_roms/gb_demo.gb`, `gbc_demo.gbc` (hand-assembled: a moving picture, a tone, a counter in cartridge RAM) |
| Kotlin core | `NativeBridge.kt`, new `Console.kt`, `RomFile.kt`, `Library.kt`, `FolderScan.kt`, `MainActivity.kt`, `States.kt` |
| Kotlin UI | `TouchLayout.kt`, `TouchControlsView.kt`, `LayoutStore.kt`, `LayoutEditor.kt`, `KeyMap.kt`, `Screens.kt`, `SettingsScreens.kt`, `GamePak.kt`, `AboutScreens.kt`, `DeltaSkinView.kt` |
| Box art | `BoxArtHelper.kt`, `GameNames.kt` |
| Words | `README.md`, `PRODUCT.md`, `PRIVACY.md` (if it names systems), `docs/RELEASE.md`, `app/build.gradle.kts` |

---

### Task 1: The native layer runs both consoles

**Interfaces — produces (C):** `emu_create(..., int *err)` with `EMU_OK = 0` and the existing `gba_err_t` numbers
kept as they are, plus `EMU_ERR_UNSUPPORTED = 100` (a cartridge chip the Game Boy core does not have);
`int emu_console(const emu_t*)` (0 GBA, 1 Game Boy, 2 Game Boy Color = `machine_kind - 1`);
`void emu_frame_size(const emu_t*, int *w, int *h)`; `triple_buffer_t` gains `int width, height` set by
`tb_init(tb, width, height)` (slots stay 240×160 big; a frame is `width * height` packed pixels);
`video_gl_upload(v, pixels, w, h)`; `video_gl_draw(v, width, height, frame_w, frame_h, fit_mode, smooth, background)`;
`fit_rect(width, height, frame_w, frame_h)`, `fit_rect_mode(width, height, frame_w, frame_h, mode)`;
`gl_readback(pixels, frame_w, frame_h, width, height, …)`.
**(Kotlin, `NativeBridge`):** `external fun console(): Int`, `external fun frameWidth(): Int`,
`external fun frameHeight(): Int` (of the loaded game; 240 / 160 with none); `frame()` returns `width * height`
shorts; `ERR_UNSUPPORTED = 100`.

- [ ] **Step 1: Failing PC unit tests** (`test_platform.c`): `fit_rect` for a 160×144 frame (10:9: a 1080×2400
  surface → 1080×972 centred; a 2400×1080 surface → 1200×1080), whole-number scaling by 160×144, the GBA cases
  unchanged with (240, 160); the triple buffer carries its size.
- [ ] **Step 2:** Implement the picture path (triple buffer, `video_gl` with a `u_size` uniform instead of the
  constants and a 240×160 texture used partly, `fit_rect`, readback, render thread). `dev.sh unit` passes.
- [ ] **Step 3:** `emu_thread.c` / `jni_bridge.c` on `machine_*`: generic calls through the machine; BIOS, forced
  save type, cartridge hardware, sensors, rumble, cheats, achievement memory and `emu_debug_peek32` only when
  `machine_gba()` is not NULL (for a Game Boy game: cheats → `GBA_ERR_CHEAT`, sensors ignored, rumble false, **no
  RetroAchievements: `opt.ra` is dropped**, so no game is identified and no hardcore limit applies); the save file
  through `machine_save_size / write / read` (a Game Boy save of any length loads; `EMU_SAVE_EMPTY` when the
  cartridge has a save and there is no file); the clock from the phone once a second for both; the JNI calls above.
  `platform/android/CMakeLists.txt` adds `core_gb` and `machine`.
- [ ] **Step 4: Own test ROMs.** `tools/build_test_roms.py` builds `tests/own_roms/gb_demo.gb` (plain Game Boy,
  MBC1 + RAM + battery) and `gbc_demo.gbc` (colour flag, MBC5 + RAM + battery) from bytes: a scrolling background, a
  square-wave tone, and a counter written to cartridge RAM every second. Lock their frame + audio fingerprints in
  `tests/expected_frames.json` (so CTest runs them plain, through a state and through a rewind).
- [ ] **Step 5: Failing on-device tests** (`app/src/androidTest`, next to the existing session tests): each demo ROM
  loads (`console()` 1 / 2, frame 160×144), runs at speed with sound, reproduces its PC fingerprint, saves to its
  save file, round-trips a save state, rewinds; a GBA ROM after a Game Boy ROM gives a 240×160 frame again (Review
  Focus 4); a GBA state loaded into the Game Boy demo is refused and the game goes on (Review Focus 1); a ROM with
  cartridge type 0x22 returns `ERR_UNSUPPORTED`.
- [ ] **Step 6:** `dev.sh androidtest` and `jvmtest` pass (the existing 93 + the new ones). Commit
  `android: the native layer runs both consoles`.

### Task 2: Reading game files

**Interfaces — produces:** `Console.kt`: `enum class Console { GBA, GB, GBC }` with
`Console.of(rom: ByteArray, fileName: String?): Console?` (the same rule as `machine_detect`: a GBA header's fixed byte
and checksum; else a Game Boy header checksum with a size byte that fits; else the name's ending; else null) and
`val hasShoulders: Boolean`, `val frameWidth / frameHeight`; `RomFile.GAME_ENDINGS = listOf(".gba", ".gb", ".gbc")`.

- [ ] **Step 1: Failing Kotlin tests:** `ConsoleTest` (content beats name; name fallback; null for neither; the same
  byte cases as `tests/unit/test_machine.c`), `RomFileTest` additions (a zip holding a `.gb`; a zip holding a `.gbc`;
  the first game of any kind wins; "no game inside this zip"), `LibraryTest` additions (`isGameFile` and `nameOf` for
  the new endings; `x.gb` shorter names).
- [ ] **Step 2:** Implement; `FolderScan` lists the new endings and zips holding them. `jvmtest` passes.
- [ ] **Step 3:** Commit `app: .gb and .gbc game files`.

### Task 3: The game list knows each game's console; opening and its errors

**Interfaces — produces:** `GameEntry.console: Console?` (JSON field `"console"`, missing = null = "not known yet",
shown and treated as GBA until the game is opened once — Review Focus 2); set from `Console.of` whenever the game's
bytes are read (open, id computing).

- [ ] **Step 1: Failing tests:** `LibraryTest`: a 1.0.0 game list without the field reads and writes back; the field
  survives a round trip; `MainActivity`'s load-error text function (pure, new: `loadErrorText(code): String`) gives
  "This cartridge type isn't supported yet" for `ERR_UNSUPPORTED`, the existing texts for the others.
- [ ] **Step 2:** Implement: the open flow stores the console, shows the error (as today's errors are shown) and
  returns to the list with no session left (Review Focus 3); the first-start and empty-folder texts name the three
  endings.
- [ ] **Step 3:** On-device test: opening the Game Boy demo from a folder puts it in the list with its console;
  opening an unsupported-chip file shows the message. Commit `app: the game list knows each game's console`.

### Task 4: The picture's shape everywhere

- [ ] **Step 1: Failing tests** (`TouchLayoutTest`, `StatesTest` or their equivalents): `TouchLayout.game(...)` for a
  10:9 frame upright (full width, height `width * 9 / 10`) and sideways, whole-number scaling by 160×144; the GBA
  results unchanged; a state thumbnail of a Game Boy frame is a 160×144 bitmap.
- [ ] **Step 2:** Implement: `TouchLayout` and `TouchControlsView` take the frame size (from `Console`); `States`
  makes thumbnails at the frame's size; pictures in the list, the Continue card and the slot sheets keep **their
  bitmap's own shape** inside the existing frames (no stretching, no cropping; how the leftover space looks is
  impeccable's call); the Settings preview keeps GBA's shape.
- [ ] **Step 3:** `jvmtest`, `androidtest`. Commit `app: the picture's shape follows the console`.

### Task 5: Buttons without L and R; a layout per console family

**Interfaces — produces:** `LayoutStore` keeps layouts under a family key: `"gba"` (today's file and keys,
untouched) and `"gb"` (Game Boy + Game Boy Color; new keys in the same file); default Game Boy layouts = the GBA
defaults without L and R, the picture 10:9.

- [ ] **Step 1: Failing tests:** `LayoutStoreTest`: a 1.0.0 `layout.json` still loads as the GBA layouts; a saved
  Game Boy layout does not change the GBA one and the reverse; defaults for `gb` hold no L / R item;
  `TouchLayoutTest`: no L / R drawn or hit for a Game Boy game; `KeyMap`: L / R bindings send nothing to a Game Boy
  game (the native mask drops them too).
- [ ] **Step 2:** Implement, with the layout editor opened for the family of the game it is opened from (from
  Settings without a game: a choice of the two — impeccable). The key table hides L / R for Game Boy games.
- [ ] **Step 3:** A Delta skin (GBA only) is not used in a Game Boy game: the built-in skin shows instead (test in
  the skin-choosing function). Commit `app: Game Boy buttons and their own layout`.

### Task 6: The console mark, and what is not there yet (impeccable)

- [ ] **Step 1:** impeccable: shape the console mark on the cartridge label and the Continue card (a small printed
  "GB" / "GBC" / "GBA" mark in the label's own type; follows `DESIGN.md`), the "not available yet" rows, and Task
  4's leftover-space look. Record decisions in the surface brief.
- [ ] **Step 2: Failing tests:** the pause-menu model for a Game Boy game: Cheats and Achievements / Leaderboards rows
  are replaced by one quiet line each ("Cheats for Game Boy games come in a later version", and the same for
  achievements, shown only when signed in); game settings entry hidden; the light slider and sensors never shown;
  a signed-in player gets the notice once per game start; `UiTextTest` passes on the new strings.
- [ ] **Step 3:** Implement; on-device test with the pretend achievement server on: a Game Boy game starts, no
  identify request is made, state loading and rewind stay allowed with Hardcore on (Review Focus 5).
- [ ] **Step 4:** Commit `app: the console mark; cheats and achievements wait for a later version on Game Boy games`.

### Task 7: Box art for the new consoles

- [ ] **Step 1: Failing tests** (`BoxArtHelper` / `GameNames` tests): the list name per console ("Nintendo - Game
  Boy", "Nintendo - Game Boy Color", the existing GBA one); a console's list is fetched only when the game list holds
  a game of it; lookup by CRC in the game's own console list, then the other Game Boy list; no 4-letter-code lookup
  for these consoles; the cached `names.tsv` of 1.0.0 keeps working.
- [ ] **Step 2:** Implement (one names file per console beside the old one). Commit `app: box art for Game Boy and
  Game Boy Color games`.

### Task 8: Words and version

- [ ] **Step 1:** `versionName = "1.1.0"`, `versionCode + 1`; About's one-liner ("Game Boy, Game Boy Color and Game
  Boy Advance emulator for Android"); README (first line, features with what is GBA-only for now: cheats,
  RetroAchievements, Delta skins, sensors; the homebrew credits if new screenshots are used), `PRODUCT.md`,
  `PRIVACY.md` if needed, `docs/RELEASE.md` (repo description, topics `gameboy`, `gameboy-color`, `gb-emulator`,
  `gbc-emulator`; post drafts for 1.1.0), the design spec's goal line. Tests that read the version / User-Agent
  updated.
- [ ] **Step 2:** `jvmtest`, `public_scan`. Commit `DreamBoy 1.1.0: the words`.

### Task 9: Claude's check on the phone emulator; the finish review

- [ ] **Step 1:** `dev.sh install`; push the two demo ROMs, two homebrew games (`tests/roms/homebrew/`) and a GBA
  demo into the emulator's games folder. By element text: all three kinds in one list with their marks; each opened,
  played with the touch buttons, saved, a state saved and loaded, rewound, fast-forwarded; upright and sideways,
  light and dark, font 1.3×; the layout editor for both families; box art; a backup made and restored; a zipped
  game; the unsupported-cartridge message; the not-yet rows. Screenshots into `.impeccable/review/`.
- [ ] **Step 2:** impeccable finish review of the changed screens → `ship`; fix what it finds; update `DESIGN.md`
  through the documenter if the design system gained the console mark.
- [ ] **Step 3:** All tests: `unit`, `test`, `jvmtest`, `androidtest`, `games --check`, `bench --check`;
  `ponytail-review` of the 17b diff. Commit `17b: Claude's check and the finish review`.

### Task 10: The user's check and the release

- [ ] **Step 1:** Build the signed release (`dev.sh apk-release`; the user's key), check it (`docs/RELEASE.md` §3
  steps 4-5). **Stop: the user installs it over 1.0.0 on their phone**, checks their GBA saves are still there,
  plays a Game Boy game and a Game Boy Color game of their own, closes and reopens (the save survives).
- [ ] **Step 2:** On the user's OK: merge `milestone-17` into `main` (`--no-ff`), `public_scan --history` clean, tag
  `v1.1.0`, push, `gh release create` with the APK and notes, update the repo description and topics.
- [ ] **Step 3:** Documents: PROGRESS (17b done, Next = 17c's spec), `CLAUDE.md`, both specs. Commit
  `milestone 17b done: DreamBoy 1.1.0`.

## Self-review notes

- Spec §6.1 → Task 1; §6.2 → Tasks 2-3; §6.3 → Tasks 4-5; §6.4, §6.6 → Task 6; §6.5 → Task 7; §6.7 → Tasks 8, 10;
  §9.2 → Tasks 9-10 (the phone-speed line was measured on phone A on 2026-11-13: 7.9-11.4×; phone B gets its turn in
  the user's check if they use it).
- Backup (spec §2, left column): no change needed — saves, states and layouts keep their paths and ids; Task 9
  checks a backup with Game Boy data in it.
