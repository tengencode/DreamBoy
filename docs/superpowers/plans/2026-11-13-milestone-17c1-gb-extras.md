# Milestone 17c part 1 — the Game Boy extras (release 1.2.0) — Implementation Plan

> **For agentic workers:** execute with superpowers:executing-plans (inline in one session — `CLAUDE.md`; never
> subagent-driven). Steps use checkbox syntax for tracking.

**Goal:** Game Boy and Game Boy Color games get cheats, RetroAchievements, colours for grey games and rumble; released
as DreamBoy 1.2.0.

**Architecture:** each extra is a small addition to `core_gb/` behind `machine/`, then wired through
`platform/android/emu_thread.c` + `jni_bridge.c` into the Kotlin screens the GBA side already has. Nothing in the GBA
core changes.

**Tech stack:** C11 (`core_gb/`, `machine/`, `platform/`), Kotlin + Compose (`app/`), rcheevos 12.5 (vendored).

**Spec:** `docs/superpowers/specs/2026-11-13-milestone-17c-gb-extras-design.md` (approved by the user 2026-11-13, with
the one download: the colour table from SameBoy's boot ROM source, MIT).

## Global constraints

- Test-first; one commit per task; commit messages end with the Co-Authored-By trailer. Branch `milestone-17c`.
- Every non-static name in `core_gb/` starts with `gb_`. Core code: C11, libc only.
- No test fingerprint, test-ROM result, `games --check` or `bench --check` result may change. A core with no look set
  draws the four greys.
- The state format "GBCS" does not change: looks and cheats are never in a state or a rewind snapshot.
- Public-safe: no commercial game titles or personal details in any tracked file or commit message; made-up names in
  tests. The colour table is keyed by header sums, not by titles — it is written without the games' names.
- UI text: sentence case, buttons start with a verb, no "…", "&", "please", "!". New UI through the impeccable skill
  and `DESIGN.md`. Code through the ponytail skill.
- Build and test only through `bash tools/dev.sh …`; never two Gradle commands at once.
- After every task: `docs/PROGRESS.md` (Completed, status, Next), this plan's boxes, and a check that no document is
  stale (`python <scratchpad>/task_done_c.py <n> <next> "<next title>" "<note>" "<status>"` does the bookkeeping).

## Review focus

1. A cheat switched on, then a save state loaded or a rewind step taken: the cheat is still applied, the state is
   byte-identical to one made without cheats → Task 4's tests.
2. A Game Genie code for 0x4000-0x7FFF on a ROM with many banks: only the banks whose byte equals the compare change,
   and all are put back when the cheat is switched off → Task 4.
3. A GameShark code naming a bank the cartridge or console does not have: skipped, no write outside the memory →
   Task 4.
4. RetroAchievements reading across region edges and past what exists (0xBFFF→0xC000, 0x15FFF→0x16000, a cartridge
   with no RAM, a plain Game Boy's 0x10000): the bytes that exist, then stop → Task 6.
5. A look chosen while a state from another look is loaded, and a settings value from a future version (an unknown
   look number): falls back to grey, never reads outside the table → Tasks 1-2.

---

### Task 1: Colours for grey games — the core

**Files:** create `core_gb/src/dmg_looks.c`, `tests/unit/test_gb_looks.c`; modify `core_gb/include/gb.h`,
`core_gb/src/gb_internal.h`, `core_gb/src/ppu_draw.c`, `core_gb/src/gb.c`, `core_gb/CMakeLists.txt`,
`machine/machine.h`, `machine/machine.c`, `third_party/README.md`.

**Produces:**
- `#define GB_LOOK_AUTO (-1)`, `#define GB_LOOK_GREY 0`; `int gb_look_count(void)` (the fixed looks: 0 grey, 1 original
  green, 2 pocket, then the Game Boy Color's own, minus its grey).
- `void gb_look_colours(int look, uint16_t out[12])` — bg[4], obj0[4], obj1[4]; an unknown number gives grey.
- `int gb_look_auto(const uint8_t *rom, size_t size)` — the internal combination a Game Boy Color would pick, or
  `GB_LOOK_GREY` (unknown game, not a Nintendo licensee, too short); may return numbers ≥ `gb_look_count()` for
  combinations that are not in the list (valid for `gb_look_colours` and `gb_set_look`).
- `void gb_set_look(gb_t *g, int look)` — `GB_LOOK_AUTO` resolves against the loaded ROM; kept across `gb_reset`
  and `gb_load_rom` re-resolves auto; never in a state.
- `void machine_set_look(machine_t *m, int look)` (nothing for a GBA machine).

- [x] **Step 1:** Download SameBoy's `BootROMs/cgb_boot.asm` into the scratchpad (allowed by the user); read the title
  checksum table, the fourth-letter table, the palette-combination table and the palette colours.
- [x] **Step 2:** Failing tests in `test_gb_looks.c`: `looks_default_is_grey` (frame of `gb_demo.gb` identical with no
  call and after `gb_set_look(GREY)`); `looks_change_background_and_sprites_apart` (a look whose bg and obj colours
  differ: a made-up ROM drawing one bg tile and one sprite of the same shade shows two colours);
  `looks_auto_known_sum`, `looks_auto_needs_fourth_letter`, `looks_auto_unknown_is_grey`,
  `looks_auto_other_licensee_is_grey` (made-up headers built in the test); `looks_out_of_range_is_grey`;
  `looks_not_in_state` (state bytes equal with two different looks; loading keeps the current look).
- [x] **Step 3:** Implement; `ppu_draw.c` reads `g->look[3][4]` instead of `GREY`.
- [x] **Step 4:** `dev.sh unit`, `dev.sh test` (fingerprints unchanged). Credit SameBoy in `third_party/README.md`.
- [x] **Step 5:** Commit `gb core: looks for grey games (the Game Boy Color's colours)`.

### Task 2: Colours — the app

**Files:** modify `platform/android/emu_thread.c/.h`, `jni_bridge.c`, `NativeBridge.kt`, `Settings.kt`,
`GameSettings.kt`, `SettingsScreens.kt`, `Screens.kt`, `MainActivity.kt`, `Backup`-related tests if they list settings
keys; tests `SettingsTest.kt`, `GameSettingsTest.kt`, `GameBoyDeviceTest.kt`.

**Consumes:** Task 1's calls. **Produces:** `emu_options_t.look`, `void emu_set_look(emu_t *, int)` (applied by the
emulation thread before the next frame); JNI `setLook(look: Int)`, `lookCount(): Int`, `lookColours(look: Int):
ShortArray` (12 colours); `Settings.greyLook: Int` (key `grey_look`, default `LOOK_AUTO = -1`);
`GameSettings.Game.look: Int` (default `LOOK_SAME = -2`, stored as `<id>.look`); `fun lookFor(settings, game): Int`.

- [x] **Step 1:** Failing Kotlin tests: the setting's default and round trip; an unknown stored number reads as
  automatic; `lookFor` prefers the game's own; the per-game value survives `GameSettings` save / load and is absent
  from the file when "same".
- [x] **Step 2:** Native + JNI; on-device test `aLookChangesTheGreyPicture` (gb_demo: frame colours differ between
  grey and original green; GBC demo unchanged).
- [x] **Step 3:** UI (impeccable, existing parts only): Settings → Picture gets the row "Grey Game Boy games" →
  a choice list, each row with its four background colours as a strip; names: Automatic, Grey, Original green,
  Pocket, then the Game Boy Color's (Brown, Red, Dark brown, Pastel mix, Orange, Yellow, Blue, Dark blue, Green,
  Dark green, Inverted). Under Automatic: "The colours a Game Boy Color gives each game. Games it doesn't know stay
  grey." The game list's hold menu shows Game settings for a grey game: one choice, "Colours", first entry "Same as in
  Settings".
- [x] **Step 4:** `jvmtest`, `androidtest`. Commit `app: colours for grey Game Boy games`.

### Task 3: Rumble

**Files:** modify `core_gb/include/gb.h`, `core_gb/src/cart.c`, `machine/machine.h/.c`, `platform/android/emu_thread.c`;
tests `tests/unit/test_gb_mbc.c`, `test_machine.c`.

**Produces:** `int gb_has_rumble(const gb_t *)`, `int gb_rumble_active(const gb_t *)` (1 while bit 3 of the RAM-bank
register is set on a rumble cartridge); `int machine_has_rumble(const machine_t *)`, `int
machine_rumble_active(const machine_t *)`; `emu_cart_hardware` reports `GBA_HW_RUMBLE` for such a game so the app's
`Rumble` starts as for GBA.

- [x] **Step 1:** Failing tests: `mbc5_rumble_follows_the_motor_bit` (type 0x1C: write 0x08 to 0x4000 → active; 0x00 →
  not; a non-rumble MBC5 never); the machine passes it on; a state load keeps the bit.
- [x] **Step 2:** Implement. `unit`. Commit `gb core + app: rumble cartridges shake the phone`.

### Task 4: Cheats — the core

**Files:** create `core_gb/src/cheats.c`, `tests/unit/test_gb_cheats.c`; modify `gb.h` (`GB_ERR_CHEAT`),
`gb_internal.h`, `gb.c` (run at the end of `gb_run_frame`; undo on destroy / load), `machine/machine.h/.c`.

**Produces:**
- `typedef enum { GB_CHEAT_AUTO, GB_CHEAT_GAME_GENIE, GB_CHEAT_GAMESHARK } gb_cheat_type_t;`
- `gb_cheat_type_t gb_cheat_check(const char *text, gb_cheat_type_t type, int *bad_line);`
- `gb_err_t gb_cheats_set(gb_t *g, const char *const *texts, const gb_cheat_type_t *types, size_t count);`
- `int machine_cheat_check(machine_kind_t kind, const char *text, int type, int *bad_line);`
  `int machine_cheats_set(machine_t *m, const char *const *texts, const int *types, size_t count);` (MACHINE_OK or
  a new `MACHINE_ERR_CHEAT`).
- Limits: 256 codes in all (more: `GB_ERR_CHEAT`); lines split on newline and `+`.

- [x] **Step 1:** Failing tests: decoding (`genie_nine_digits`, `genie_six_digits`, `genie_without_dashes`,
  `gameshark_plain`, `auto_detects`, `bad_line_is_reported`, `plus_joins_codes`); application
  (`genie_patches_only_matching_banks`, `genie_bank0_needs_its_compare`, `genie_is_undone_on_clear_and_on_reset_of_the_list`,
  `gameshark_holds_a_byte_against_the_game`, `gameshark_bank_digit_picks_work_ram_bank_on_color`,
  `gameshark_bank_digit_picks_cart_ram_bank`, `gameshark_missing_bank_is_skipped`, `gameshark_other_address_is_not_a_code`,
  `gameshark_cart_ram_write_marks_the_save_dirty`); `cheats_are_not_in_states` and `cheats_survive_state_load_and_rewind`.
- [x] **Step 2:** Implement. `unit`, `test`. Commit `gb core: Game Genie and GameShark cheats`.

### Task 5: Cheats — the app and cheat files

**Files:** modify `emu_thread.c`, `jni_bridge.c` (`checkCheat` / `setCheats` go through `machine_`; `checkCheat`
uses the loaded game's console), `Console.kt` (drop `NO_CHEATS_YET`; `cheatTypes: List<String>`, hint text),
`Screens.kt`, `MainActivity.kt`, `BoxArtHelper.kt` (cheat list / file URLs per console), `ChtFile.kt` only if `+` is
not split yet; tests `ConsoleTest.kt`, `ChtFileTest.kt`, `BoxArtHelperTest.kt`, `GameBoyDeviceTest.kt`.

- [ ] **Step 1:** Failing tests: Kotlin — the type names per console; a `.cht` text with a Game Genie and a
  GameShark cheat merges with `readable` asked per code; the cheat folder URL for GB and GBC. On-device —
  `aGameSharkCheatHoldsMemory` (gb_demo + `debugSaveRam` or a frame difference), `checkCheat` reads a Game Genie code
  only in a Game Boy game.
- [ ] **Step 2:** Implement; the pause menu's Cheats row is a normal row for every console; hardcore still switches
  it off. `jvmtest`, `androidtest`. Commit `app: cheats for Game Boy and Game Boy Color games`.

### Task 6: RetroAchievements

**Files:** modify `core_gb/include/gb.h` + a core file (`uint32_t gb_ra_peek(const gb_t *g, uint32_t address,
uint8_t *buffer, uint32_t count)` — the table in the spec's §5; returns the bytes read), `platform/ra/ra.h/.c`
(`ra_memory_t` gains `uint32_t (*peek)(void *ctx, uint32_t address, uint8_t *buffer, uint32_t count); void *ctx;` —
used when set, else the GBA pointers), `platform/ra/ra_fake.c/.h` (`void ra_fake_set_console(int id)`),
`emu_thread.c` (`memory_of`, keep the session for every console), `jni_ra.c` if the fake's console is set from
Kotlin, `MainActivity.kt` (`ra.loadGame` for every console; the notice goes), `Console.kt` (drop
`NO_ACHIEVEMENTS_YET`), `FakeRaServer.kt` if it also names the console; tests `tests/unit/test_gb_ra.c`, `test_ra.c`,
on-device RA tests.

- [ ] **Step 1:** Failing tests in `test_gb_ra.c`: every row of the spec's table on both consoles with known bytes
  (`ra_rom_follows_the_mapped_bank`, `ra_cart_ram_is_bank0_fixed`, `ra_work_ram_d000_is_bank1_fixed`, `ra_echo`,
  `ra_sprite_memory_and_the_gap`, `ra_registers_peek_changes_nothing` (state before == after), `ra_high_ram_and_ie`,
  `ra_color_banks_2_to_7`, `ra_plain_game_boy_has_no_10000`, `ra_cart_ram_banks_1_to_15`, `ra_no_cart_ram_reads_nothing`,
  `ra_reads_across_region_edges`); in `test_ra.c`: a made-up Game Boy set on the pretend server (console 4) unlocks
  from a byte in work RAM.
- [ ] **Step 2:** Implement. `unit`. On-device: a Game Boy game with the pretend server loads a set and unlocks
  (the existing RA device test's pattern). `androidtest`.
- [ ] **Step 3:** Commit `RetroAchievements for Game Boy and Game Boy Color games`.

### Task 7: The words and the version

- [ ] **Step 1:** `app/build.gradle.kts`: `versionCode` 3, `versionName` "1.2.0". README (features: cheats,
  RetroAchievements, rumble for every console; Delta skins and tilt stay "GBA today"; colours for grey games), the
  RetroAchievements paragraph, About's line if it names what is missing, `PRODUCT.md`, `PRIVACY.md` (RetroAchievements
  also sees Game Boy games' hashes; date), `docs/RELEASE.md` §5 (release notes and a short post for 1.2.0),
  `docs/BUILDING.md` if it lists core APIs.
- [ ] **Step 2:** `jvmtest` (the About / version tests). Commit `DreamBoy 1.2.0: the words`.

### Task 8: Claude's check on the phone emulator; all tests; review

- [ ] **Step 1:** `dev.sh install`. By element text, with the pretend RetroAchievements server (`files/ra-fake`): the
  spec's §9.2 list. Screenshots into `.impeccable/review/17c-*`.
- [ ] **Step 2:** All tests: `unit`, `test`, `jvmtest`, `androidtest`, `games --check`, `bench --check`;
  `ponytail-review` of the diff from `main`; fix what it finds.
- [ ] **Step 3:** Commit `17c part 1: Claude's check and the review`.

### Task 9: The user's check and the release

- [ ] **Step 1:** `python tools/public_scan.py --history` clean; `dev.sh apk-release`; check the APK (`docs/RELEASE.md`
  §3 steps 4-5, and the scan inside the APK as for 1.1.0). **Stop: the user installs it over 1.1.0 and does the spec's
  §9.3 check.**
- [ ] **Step 2:** On the user's OK: documents (PROGRESS: 17c part 1 done, Next = part 2's spec; `CLAUDE.md`; both
  specs), commit `milestone 17c part 1 done: DreamBoy 1.2.0`; merge into `main` (`--no-ff`); history scan; tag
  `v1.2.0`; push; `gh release create`.

## Self-review notes

- Spec §4 → Tasks 4-5; §5 → Task 6; §6 → Tasks 1-2; §7 → Task 3; §9 → Tasks 7-9. §11's download → Task 1 Step 1.
- Order follows the spec's suggestion (colours, rumble, cheats, RetroAchievements).
