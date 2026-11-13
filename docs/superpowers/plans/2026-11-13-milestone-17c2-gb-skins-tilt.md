# Milestone 17c part 2 — Delta skins and tilt cartridges for Game Boy games (release 1.3.0) — Implementation Plan

> **For agentic workers:** execute with superpowers:executing-plans (inline in one session — `CLAUDE.md`; never
> subagent-driven). Steps use checkbox syntax for tracking.

**Goal:** Game Boy and Game Boy Color games use imported Delta skins, and tilt cartridges (the MBC7 chip) play;
released as DreamBoy 1.3.0.

**Architecture:** skins are Kotlin only — a skin learns its console family when read, the settings keep one chosen
imported skin per family, and the game screen asks for the playing game's family. Tilt is one new chip in `core_gb/`
behind `machine/`; it reports the tilt hardware bit the app already acts on, so no new Kotlin path.

**Tech stack:** C11 (`core_gb/`, `machine/`, `platform/`, `tools/`), Kotlin + Compose (`app/`).

**Spec:** `docs/superpowers/specs/2026-11-13-milestone-17c2-gb-skins-tilt-design.md` (approved by the user
2026-11-13, with the test-skin downloads).

## Global constraints

- Test-first; one commit per task; commit messages end with the Co-Authored-By trailer. Branch `milestone-17c2`
  (exists, from `main`); merged into `main` with `--no-ff` at the end. Push only on the user's OK.
- Every non-static name in `core_gb/` starts with `gb_`. Core code: C11, libc only. The chip is written from Pan
  Docs' MBC7 page (re-read it first; where this plan and Pan Docs differ, Pan Docs wins), never from another
  emulator's code.
- No existing test fingerprint, test-ROM result, `games --check` or `bench --check` result may change. A save state
  made by 1.1.0 or 1.2.0 must still load, and a state of any non-tilt game must stay byte-identical in layout.
- `touch_skin_import` keeps its meaning (the GBA family's chosen imported skin); a 1.2.0 settings file reads as
  before.
- Public-safe: no commercial game titles or personal details in any tracked file or commit message; a tilt game is
  "a tilt game"; made-up names in tests. Test skins live only in the gitignored `tests/skins/`.
- UI text: sentence case, buttons start with a verb, no "…", "&", "please", "!". Changed UI through the impeccable
  skill and `DESIGN.md`. Code through the ponytail skill.
- Build and test only through `bash tools/dev.sh …`; never two Gradle commands at once. Ask before restarting the
  phone emulator.
- After every task: `docs/PROGRESS.md` (Completed, status, Next), this plan's boxes, and a check that no document
  is stale.

## Review focus

1. A 1.2.0 user with an imported GBA skin chosen updates: the same skin is still chosen for GBA games, and their
   Game Boy games still use the built-in skin → Task 2 (`aSettingsFileFrom120ReadsAsBefore`).
2. The chosen Game Boy skin's folder is deleted (a restore, a removed skin) while the GBA one is fine: only the
   Game Boy choice is cleared → Task 2 (`aMissingSkinClearsOnlyItsOwnFamily`).
3. A tilt game's save state taken in the middle of an EEPROM command, then loaded into a fresh machine: the command
   finishes with the right bits → Task 4 (`mbc7_state_restores_a_command_in_flight`).
4. A save file of the wrong length for a tilt cartridge (0 bytes, 512 bytes from another emulator): what fits is
   copied, the rest is `0xFF`, nothing is read or written outside the 256 bytes → Task 4
   (`mbc7_save_of_any_length_loads`).
5. A game hammers the EEPROM pins with chip select low, or sends an unknown bit pattern: no state change, no write,
   no crash → Task 4 (`mbc7_eeprom_ignores_noise`).

---

### Task 1: A skin knows its console family

**Files:** modify `app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt`; test
`app/src/test/java/com/tengen/gbaemu/DeltaSkinTest.kt`.

**Produces:**
- `DeltaSkin.GBC = "com.rileytestut.delta.game.gbc"` beside `DeltaSkin.GBA`.
- `class DeltaSkin(val name, val identifier, val family: String, val upright, val sideways)` — `family` is
  `Console.GBA.family` ("gba") or `Console.GB.family` ("gb").
- A Game Boy skin's items never carry `KeyMap.L` or `KeyMap.R`; an item left with no bits and no actions is dropped.
- `SkinProblem.OTHER_CONSOLE.message` = "This skin is for another console. Only Game Boy, Game Boy Color and GBA
  skins work."

- [ ] **Step 1:** Failing tests in `DeltaSkinTest` (use its `minimal(type = …)` helper):
  `aGameBoyColorSkinIsTheGameBoyFamilys` (`minimal(DeltaSkin.GBC)` → `family == "gb"`; the type in upper case
  too); `aGbaSkinIsTheGbaFamilys` (`family == "gba"`); `shouldersPressNothingInAGameBoySkin` (items `["l"]`,
  `["a","l"]`, `["r","menu"]` → the first dropped, the second `bits == KeyMap.A`, the third `bits == 0` with
  `actions == [MENU]`; the same JSON as a GBA skin keeps L and R); extend `problems()`: type
  `com.rileytestut.delta.game.nes` → `OTHER_CONSOLE`, and its message equals the text above.
- [ ] **Step 2:** `bash tools/dev.sh jvmtest` — the new tests fail.
- [ ] **Step 3:** Implement in `DeltaSkin.parse` (the family from the type; mask L / R out of `bits` for "gb"
  before the empty check). Fix the other constructors' callers and test helpers.
- [ ] **Step 4:** `bash tools/dev.sh jvmtest` passes (`SkinFilesTest`, `UiTextTest` included).
- [ ] **Step 5:** Commit `app: a Delta skin knows its console family (Game Boy skins are read)`.

### Task 2: One chosen imported skin per family, used in the game

**Files:** modify `Settings.kt`, `MainActivity.kt`, `Screens.kt` (only if `GameScreen` assumes a GBA skin anywhere),
`SettingsScreens.kt` (only the `onChange` calls, so it compiles; the page's look is Task 3); tests
`SettingsTest.kt`, `DeltaLayoutTest.kt`, `GameBoyDeviceTest.kt` or `ScreensTest.kt`.

**Consumes:** `DeltaSkin.family` (Task 1).
**Produces (on `Settings`):**
- `val skinImportGb: Int = 0`, stored as `touch_skin_import_gb` (read like `touch_skin_import`: negative → 0).
- `fun skinImportFor(family: String): Int` — `skinImport` for "gba", `skinImportGb` for "gb".
- `fun withImported(family: String, number: Int): Settings` — sets that family's choice only.
- `fun withBuiltIn(skin: Int): Settings` — sets `skin`, clears **both** imported choices.
- `fun withoutImported(number: Int): Settings` — clears whichever choice equals `number` (both, if both do).
**In `MainActivity`:** `activeSkin()` = the imported skin whose number is `settings.skinImportFor(gameConsole.family)`
**and** whose `skin.family` is that family (a number pointing at a skin of the other family counts as missing);
start-up and `refreshSkins` clear a missing choice per family; a good import calls
`withImported(result.skin.family, result.number)`; `removeSkin` calls `withoutImported`; the body colour sent to
`NativeBridge.setBackground` while playing is black when `activeSkin() != null`, and out of a game keeps today's
rule (the GBA choice).

- [ ] **Step 1:** Failing tests in `SettingsTest`: `eachFamilyKeepsItsOwnImportedSkin` (round trip with
  `skinImport = 2`, `skinImportGb = 5`); `aSettingsFileFrom120ReadsAsBefore` (a store holding only
  `touch_skin_import = 3` → `skinImport == 3`, `skinImportGb == 0`); `pickingAnImportedSkinTouchesOnlyItsFamily`;
  `aBuiltInSkinClearsBothImportedChoices`; `removingASkinClearsItsOwnChoice` (`skinImport = 2, skinImportGb = 5`,
  `withoutImported(5)` → `2, 0`). In `DeltaLayoutTest`: `aGameBoySkinsScreenFrameIsTheGamesBox` (a face with
  screen `Rect(27, 60, 360, 324)` on a 1080 × 2400 screen: the layout's game box (whatever `DeltaLayout` calls it) equals `layout.box(screen)` — the box is
  the frame; fitting 160×144 inside it is the native fit's job, checked in Step 4).
- [ ] **Step 2:** `bash tools/dev.sh jvmtest` — they fail.
- [ ] **Step 3:** Implement `Settings`, then `MainActivity` as listed under Produces. Extract the "which imported
  skin is in use" rule as a pure function so it is tested on the PC:
  `fun chosenSkin(settings: Settings, family: String, imported: List<SkinFiles.Imported>): SkinFiles.Imported?`
  (in `SkinFiles.kt`), with tests in `SkinFilesTest`: `aMissingSkinClearsOnlyItsOwnFamily` (GBA choice present,
  Game Boy choice's number not in the list → GBA found, Game Boy null) and `aSkinOfTheOtherFamilyIsNotUsed`.
- [ ] **Step 4:** Download the test skin (allowed by the user 2026-11-13): Delta's standard Game Boy Color skin
  from the Delta project's public GBC core repository on GitHub, into `tests/skins/gbc/Standard.deltaskin`
  (gitignored); note its URL and SHA-256 in `docs/PROGRESS.md` Notes. On-device test
  `aGameBoySkinPutsTheGameInItsScreen` (build a small Game Boy skin zip in the test, as `SkinFilesTest` builds
  zips: one PNG, a screen frame, an A button; import, open `gb_demo.gb`; the readback's picture rectangle lies
  inside the skin's scaled screen frame and has the 10:9 shape; a GBA game opened next uses no imported skin).
- [ ] **Step 5:** `bash tools/dev.sh jvmtest`, `bash tools/dev.sh androidtest` pass.
- [ ] **Step 6:** Commit `app: each console family keeps its own imported skin; Game Boy games use theirs`.

### Task 3: The Skin page and the Touch buttons page

**Files:** modify `SettingsScreens.kt` (`ImportedSkin`, `SkinGrid`, `ImportedTile`, the `TOUCH` and `SKIN` pages),
`MainActivity.kt` (`refreshSkins`); tests `ScreensTest.kt`, `UiTextTest.kt`. Go through the impeccable skill
(`DESIGN.md`; no new kind of component — the game list's printed console mark on a tile).

**Consumes:** Task 2's `Settings` functions.
**Produces:** `ImportedSkin(number, name, thumb, upright, sideways, family: String)`; `SkinGrid` takes
`selectedImported: Set<Int>` (both families' chosen numbers) and reports `onPickImported(number, family)`.

- Each imported tile shows its family's mark, **GBA** or **GB** (`Console.GBA.mark` / `Console.GB.mark`).
- A built-in tile is shown chosen when at least one family has no imported skin; tapping it → `withBuiltIn`.
- The Touch buttons page's Skin row and the main page's summary: one name when both families use the same thing
  (the built-in skin's name); otherwise "GBA: <name> · Game Boy: <name>".
- The four Edit layout rows: each greyed with "This skin sets its own layout" when its own family's chosen skin
  has that direction.
- The Skin page's help line: "Import skins made for the Delta emulator (.deltaskin files) for Game Boy, Game Boy
  Color or GBA. Press and hold an imported skin to remove it."

- [ ] **Step 1:** Failing on-device tests in `ScreensTest`: `importedTilesCarryTheirConsolesMark` (one "gba" and
  one "gb" tile → nodes "GBA" and "GB"); `twoImportedSkinsCanBeChosenAtOnce` (both chosen: both tiles selected;
  tapping the "gb" tile reports `(number, "gb")`); `eachEditLayoutRowFollowsItsOwnFamilysSkin` (Game Boy skin with
  an upright face chosen, no GBA one: "Edit Game Boy upright layout" disabled, the three others enabled);
  `theSkinRowNamesBothFamilies`. `UiTextTest`: the new strings pass.
- [ ] **Step 2:** Implement with impeccable. `bash tools/dev.sh jvmtest`, `androidtest`.
- [ ] **Step 3:** Look at it on the phone emulator, light and dark (screenshots in `.impeccable/review/17c2-*`).
- [ ] **Step 4:** Commit `app: the Skin page marks each imported skin's console and shows both choices`.

### Task 4: The tilt chip in the core

**Files:** create `core_gb/src/mbc7.c`, `tests/unit/test_gb_mbc7.c`; modify `core_gb/include/gb.h`,
`core_gb/src/gb_internal.h`, `cart.c`, `state.c`, `ra_view.c`, `gb.c` (reset), `core_gb/CMakeLists.txt`,
`tests/unit/CMakeLists.txt`.

**Produces:**
- `MBC7` in `mbc_t`; header type `0x22` → `mbc = MBC7`, `battery = true`, `ram_size = 256`, `rumble = false`.
  **The EEPROM's 256 bytes are `g->cart_ram`**, so the existing save-file calls, the dirty flag and the state's
  `CRAM` chunk carry them unchanged. Word *n* is bytes *2n*, *2n+1*, low byte first unless a public document says
  other emulators' files differ (record the choice in `docs/PROGRESS.md`).
- `gb_mbc7_t` in `gb_internal.h` (inside `struct gb`, after the existing members so no existing chunk's offset
  changes its bytes): `ram_on2` (the 0x40 switch), `latch_step`, `x`, `y` (the kept reading, reset `0x8000`), and
  the EEPROM's pins and shift state (`cs`, `clk`, `di`, `dout`, `bits`, `count`, `phase`, `write_on`, `address`).
  The tilt **inputs** (`tilt_in_x`, `tilt_in_y`, `int16_t`) sit outside it: never in a state.
- `uint8_t gb_mbc7_read(const gb_t *g, uint16_t addr)`, `void gb_mbc7_write(gb_t *g, uint16_t addr, uint8_t v)`
  for 0xA000-0xBFFF, called from `gb_cart_read` / `gb_cart_write`; the bank and switch writes below 0x8000 stay in
  `cart.c` (0x0000-0x1FFF `ram_on = v == 0x0A`; 0x2000-0x3FFF `bank_lo = v & 0x7F`; 0x4000-0x5FFF
  `ram_on2 = v == 0x40`).
- `uint8_t gb_mbc7_peek(const gb_t *g, uint16_t addr)` — the registers whatever the switches say, no side
  effects; `gb_ra_peek` uses it for 0xA000-0xAFFF of a tilt cartridge and gives `0xFF` for 0xB000-0xBFFF.
- `void gb_set_tilt(gb_t *g, int16_t x, int16_t y)`, `int gb_has_tilt(const gb_t *g)` in `gb.h`.
- The reading taken on the latch: `X = 0x81D0 - x * 0x70 / 32767`, `Y = 0x81D0 - y * 0x70 / 32767`.
- State: for a tilt cartridge one extra chunk `'MBC7'` (version 1, `sizeof(gb_mbc7_t)`) after the fixed ones, and
  the header's chunk count is the fixed count + 1; for every other cartridge the state is exactly as today.
- `gb_cart_poke` does nothing on a tilt cartridge (GameShark codes for 0xA000-0xBFFF are skipped).
- Registers (bits 4-7 of the address): `0x` write `0x55` → `x = y = 0x8000`, `latch_step = 1`; `1x` write `0xAA`
  with `latch_step == 1` → take a reading, `latch_step = 0`; `2x`/`3x` X low/high; `4x`/`5x` Y low/high; `6x`
  reads `0x00`; `7x` reads `0xFF`; `8x` the EEPROM: write bit 7 = chip select, bit 6 = clock, bit 1 = data in;
  read bit 0 = data out (the pins last written read back in their own bits). Anything else reads `0xFF`.
- The EEPROM (93LC56, 16-bit words, 128 of them): bits are taken on the clock's rising edge while chip select is
  high; a command is a start bit `1`, 2 opcode bits, 8 address bits. `10` read (a dummy `0`, then 16 data bits,
  high bit first, and on into the next word while clocked); `01` write (16 data bits; only when `write_on`);
  `11` erase the word to `0xFFFF` (only when `write_on`); `00` by the address's top two bits: `11` write enable,
  `00` write disable, `01` write-all (16 data bits), `10` erase-all. Chip select falling ends the command; data
  out reads `1` when idle (ready). A write or erase sets the dirty flag.

- [ ] **Step 1:** Failing tests in `test_gb_mbc7.c` (a made-up ROM with type `0x22`; helpers that clock a command
  through register `0xA080`): `mbc7_loads_and_other_chips_are_still_refused` (0x22 loads, `gb_has_tilt` 1,
  `gb_has_rumble` 0, `gb_save_size` 256; type 0x20 → `GB_ERR_UNSUPPORTED_CART`);
  `mbc7_both_switches_guard_the_registers` (either off: reads `0xFF`, a latch write changes nothing;
  0xB000 reads `0xFF` always); `mbc7_rom_bank` ; `mbc7_latch_needs_its_order` (`0xAA` alone: no new reading;
  `0x55` → X and Y read `0x8000`; then `0xAA` → a reading); `mbc7_reading_level_and_one_g` (0,0 → `0x81D0`,
  `0x81D0`; x = 32767 → X `0x8160`; x = -32767 → `0x8240`; the same for Y);
  `mbc7_fixed_registers` (`6x` = 0, `7x` = `0xFF`); `mbc7_eeprom_write_needs_enable`,
  `mbc7_eeprom_write_then_read`, `mbc7_eeprom_read_runs_on_into_the_next_word`, `mbc7_eeprom_erase`,
  `mbc7_eeprom_write_all_and_erase_all`, `mbc7_eeprom_write_disable`, `mbc7_eeprom_ignores_noise` (clocks with
  chip select low, a command cut short by chip select falling: no byte changes, not dirty);
  `mbc7_save_round_trip_and_dirty` (write a word → `gb_save_dirty` 1 then 0; the file into a fresh machine reads
  the word back); `mbc7_save_of_any_length_loads` (0 and 512 bytes); `mbc7_state_restores_a_command_in_flight`
  (state saved after the opcode and half the data bits of a write, loaded into a fresh machine, the rest clocked:
  the word is written); `mbc7_rewind_restores_the_chip`; `mbc7_tilt_input_is_not_in_a_state` (two machines with
  different `gb_set_tilt`: equal state bytes); `mbc7_ra_view_ignores_the_switches` (both switches off: the view
  shows the kept X at 0xA020, `0xFF` at 0xB000; a state before and after the peek is identical);
  `mbc7_gameshark_to_its_registers_is_skipped`. In `test_gb_state.c`: `other_cartridges_states_keep_their_layout`
  (an MBC1 state's size and chunk count equal constants written into the test from today's build, measured before
  any change).
- [ ] **Step 2:** `bash tools/dev.sh unit gb_` — they fail (do not compile, then fail).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `bash tools/dev.sh unit`, `bash tools/dev.sh test` pass; fingerprints unchanged.
- [ ] **Step 5:** Commit `gb core: the tilt cartridge chip (MBC7) with its save chip`.

### Task 5: Tilt through the machine layer and the PC tools; our own test ROM

**Files:** modify `machine/machine.h/.c`, `tools/common/key_script.h/.c`, `tools/testrunner/main.c`,
`tools/bench/main.c` (only if it shares `set_input`), `tools/viewer/main.c`, `tools/build_gb_test_roms.py`,
`tests/expected_frames.json`, the CTest list that runs the fingerprints; tests `tests/unit/test_machine.c`, the key
script's unit test; create `tests/own_roms/gb_tilt.gb` (built by the script, committed like `gb_demo.gb`).

**Consumes:** `gb_set_tilt`, `gb_has_tilt` (Task 4).
**Produces:**
- `void machine_set_tilt(machine_t *m, int16_t x, int16_t y)` (both cores; nothing without the sensor),
  `int machine_has_tilt(const machine_t *m)` (GBA: `gba_cart_hardware & GBA_HW_TILT`).
- Key-script names `TILT_LEFT`, `TILT_RIGHT`, `TILT_UP`, `TILT_DOWN` = bits 10, 11, 12, 13 of `key_frame.keys`
  (`KEY_TILT_LEFT` … in `key_script.h`); held = ∓24000 on x (left −, right +) and on y (up −, down +), as the
  viewer's J / L / I / K. The tools strip these bits before the keys reach the core.
- `gb_tilt.gb`: type `0x22`, made-up title `TILT TEST`; each frame it latches a reading and moves one sprite by
  the reading's distance from level on each axis, so the picture shows where the "ball" went; START writes the
  ball's position to the EEPROM and it is read back at start.

- [ ] **Step 1:** Failing tests: `machine_tilt_reaches_each_core` (`hw_tilt.gba` and a made-up 0x22 ROM:
  `machine_has_tilt` 1, a set tilt changes what the game reads; `gb_demo.gb`: 0 and no effect);
  key-script test `tilt_names_are_read` (a script line with `TILT_LEFT+A` → bits 10 and the A bit).
- [ ] **Step 2:** Implement the machine calls, the key names, `set_input` in the test runner (tilt for both
  consoles through `machine_set_tilt`), and the viewer's `apply_extras` through `machine_` for tilt.
- [ ] **Step 3:** Build `gb_tilt.gb`; run it with a key script (level, `TILT_RIGHT`, `TILT_DOWN`) and look at the
  three pictures (the ball right of centre, then below); approve them as fingerprints (`tools/approve_frame.py`)
  — plain, through a state, through a rewind, as the other `gb_` entries.
- [ ] **Step 4:** `bash tools/dev.sh unit`, `test`, `games --check`, `bench --check` — all pass, nothing old
  changed.
- [ ] **Step 5:** Commit `machine + tools: tilt for both consoles, key scripts can tilt, our own tilt test ROM`.

### Task 6: Tilt in the app

**Files:** modify `platform/android/emu_thread.c` (`apply_sensors`, `emu_cart_hardware`); tests
`GameBoyDeviceTest.kt` (or `MotionTest.kt`), `app/src/androidTest` assets if the test ROMs are copied from a list.

**Consumes:** `machine_set_tilt`, `machine_has_tilt` (Task 5).
**Produces:** `apply_sensors` calls `machine_set_tilt(e->m, x, y)` for every game (gyro and light stay GBA calls);
`emu_cart_hardware` returns `GBA_HW_TILT` for a Game Boy tilt cartridge (or-ed with the rumble bit's rule). No
Kotlin change: `MotionInput` starts the accelerometer on the bit.

- [ ] **Step 1:** Failing on-device test `aGameBoyTiltCartridgeTakesThePhonesTilt` (`gb_tilt.gb`:
  `NativeBridge.cartHardware() and HW_TILT != 0`; `setMotion(24000, 0, 0)` for some frames moves the picture's
  ball right of where it is with `setMotion(0, 0, 0)`; the save file is 256 bytes after START).
- [ ] **Step 2:** Implement. `bash tools/dev.sh androidtest` (and `unit` for the `platform/` PC tests, if any).
- [ ] **Step 3:** Commit `app: tilt cartridges take the phone's tilt`.

### Task 7: The words and the version

**Files:** modify `app/build.gradle.kts` (`versionCode = 4`, `versionName = "1.3.0"`), `README.md`, `PRODUCT.md`,
`docs/RELEASE.md` (§5: release notes for 1.3.0, the post drafts), `Screens.kt` / `Console.kt` if a message still
says tilt or skins are GBA only, `PRIVACY.md` only if its date line is tied to the version.

- [ ] **Step 1:** `grep -rn "GBA today\|GBA only\|Only GBA\|not for Game Boy\|isn't supported yet"` over the app,
  README, `PRODUCT.md`, `DESIGN.md`, `docs/RELEASE.md`, `docs/BUILDING.md`; fix each line that is no longer true.
  The unsupported-cartridge message stays (other chips are still refused).
- [ ] **Step 2:** Release notes for 1.3.0: Delta skins for Game Boy and Game Boy Color games (each console family
  keeps its own); tilt cartridges. If the user owns no tilt game: one line that tilt cartridges are new and
  untried with a real game, with a request for reports.
- [ ] **Step 3:** `bash tools/dev.sh jvmtest` (`UiTextTest`, any version test). Commit
  `1.3.0: the words and the version`.

### Task 8: Claude's check, all tests, the reviews

- [ ] **Step 1:** On the phone emulator (drive it by element text; back up the debug app's settings first and put
  them back after): import `tests/skins/gbc/Standard.deltaskin` (and the optional community skin, if one was
  downloaded) — upright and sideways: the picture sharp, the game exactly in the skin's screen, every button and
  the D-pad's diagonals, menu, quick save / load; a GBA skin and a Game Boy skin chosen at once, each in its own
  games; a built-in tile clears both; remove; the greyed rows; light and dark; a non-phone or other-console skin's
  message. `gb_tilt.gb` steered with the emulator's virtual sensors and with I / J / K / L, upright and sideways
  (the ball must go the way the phone tips in both); its save survives force-closing the app. Screenshots in
  `.impeccable/review/17c2-*`.
- [ ] **Step 2:** impeccable finish review of the Skin page and the Touch buttons page: `ship`, or fix and repeat.
- [ ] **Step 3:** `ponytail-review` of the diff from `main`; fix what it finds.
- [ ] **Step 4:** All tests on a calm emulator (`adb shell uptime` first): `unit`, `test`, `jvmtest`,
  `androidtest`, `games --check`, `bench --check`. Record the counts in `docs/PROGRESS.md`.
- [ ] **Step 5:** Commit `17c part 2: the check on the emulator, the reviews`.

### Task 9: The user's check and the release

- [ ] **Step 1:** Build the signed APK as `docs/RELEASE.md` says; `python tools/public_scan.py --history` says
  `clean`; scan the APK as for 1.2.0.
- [ ] **Step 2:** **Stop for the user:** 1.3.0 installed over 1.2.0 on their phone — they import a Game Boy skin
  and play a Game Boy and a Game Boy Color game with it; their GBA skin choice, games, saves and settings are as
  before; a tilt game only if they own one (if it steers the wrong way on an axis, flip that axis's sign in
  `gb_mbc7`'s reading, fix the tests' expected values, re-approve `gb_tilt`'s fingerprints, and repeat Task 8
  Step 4).
- [ ] **Step 3:** On the user's OK: merge `milestone-17c2` into `main` (`--no-ff`), history scan, tag `v1.3.0`,
  push, `gh release create v1.3.0` with the notes from `docs/RELEASE.md` §5.
- [ ] **Step 4:** Milestone exit: `docs/PROGRESS.md`, `CLAUDE.md` ("Key files", the released line), the three
  specs' status lines, the memory note on the release; confirm no document is stale. Commit
  `milestone 17c part 2 done: DreamBoy 1.3.0`.
