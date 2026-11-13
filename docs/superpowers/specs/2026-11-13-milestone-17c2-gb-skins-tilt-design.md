# Milestone 17c part 2 — Delta skins and tilt cartridges for Game Boy games — Design

**Date:** 2026-11-13
**Status:** Approved by the user 2026-11-13, with the test-skin downloads in §11 ("Approve both"). Plan: `docs/superpowers/plans/2026-11-13-milestone-17c2-gb-skins-tilt.md` (9 tasks). Reviewed once by a builder-check agent (2026-11-13); its points are folded in (the tilt directions and scale in §5.1, the RetroAchievements view in §5.2, tilt in key scripts in §5.3).
**Parent specs:** `docs/superpowers/specs/2026-11-13-milestone-17c-gb-extras-design.md` (§2: what was left for part 2),
`docs/superpowers/specs/2026-11-12-milestone-17-game-boy-design.md`,
`docs/superpowers/specs/2026-10-04-milestone-9-delta-skins-design.md` (how Delta skins work in the app today),
`docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones).
**Previous work:** DreamBoy 1.2.0 (2026-11-13): Game Boy and Game Boy Color games have cheats, RetroAchievements,
colours for grey games and rumble.

---

## 1. Intent

The last two extras Game Boy and Game Boy Color games still lack: **imported Delta skins** and **tilt cartridges**.
After this release (**1.3.0**) nothing in the app or the README says "GBA only" about a feature any more.

**Why:** both were promised when 1.1.0 was cut down to what is needed to play (milestone 17 spec §2) and left for
"part 2" when the extras were split into two releases (17c spec §2).

## 2. Decisions made with the user (2026-11-13)

- **Each console family remembers its own imported skin** ("Yes, one each"): the Skin page marks every imported
  skin GBA or Game Boy; picking a Game Boy skin changes only Game Boy and Game Boy Color games, and GBA games keep
  theirs.
- From before: Game Boy and Game Boy Color are one family (one touch layout, 17b); our own core, written from public
  hardware documentation; no link cable, no Super Game Boy.

Everything else below is Claude's call, made the way the GBA side already works; §8 names the bigger ones.

## 3. Goals, non-goals, anti-goals

**Goals**
- A `.deltaskin` made for the Game Boy Color (Delta has one skin type for both Game Boy consoles) imports like a GBA
  one and is used in Game Boy and Game Boy Color games.
- A tilt cartridge (the MBC7 chip) opens, plays, saves, and is steered by tilting the phone — or by the I / J / K / L
  keys — exactly as GBA tilt games are.
- Nothing changes for GBA games or for the skins people have already imported, and nothing is lost on updating from
  1.2.0.
- The words (README, About, `PRODUCT.md`, release notes) stop listing anything as "GBA today".

**Non-goals**
- Skins of other emulators or other consoles. A GBA skin used in a Game Boy game (or the other way round).
- Per-game skins. Moving a Delta skin's buttons. A rewind button on Delta skins (as in milestone 9).
- Other unsupported cartridge chips (MBC6, HuC1, HuC3, MMM01, the camera, TAMA5). MBC1 multi-game cartridges.
- A sensitivity or calibration setting for tilt (GBA tilt has none).
- A rumble motor for tilt cartridges (the header type names one; the real cartridges have none).

**Anti-goals** (failure even if it works)
- An imported GBA skin, or a player's GBA skin choice, changing or disappearing after the update.
- A Game Boy skin showing the game stretched, cropped or outside the skin's painted screen.
- A tilt game whose save is lost, or which steers the wrong way round when the phone is turned sideways.
- Any existing test fingerprint, test-ROM result, save state or GBA behaviour changing.

## 4. Delta skins for Game Boy games

### 4.1 Reading the skin (`DeltaSkin.kt`)
- `gameTypeIdentifier` may be `com.rileytestut.delta.game.gba` (as today) or **`com.rileytestut.delta.game.gbc`**
  (ignoring case). The skin gets a **family**: GBA, or Game Boy (`Console.family`: "gba" / "gb"). Anything else is
  still "another console".
- A Game Boy skin's inputs are read with the same table as today. `l` and `r` press nothing in a Game Boy game (the
  family has no shoulder buttons); an item left with nothing is dropped, as today.
- Everything else of milestone 9's §2 holds: phone layouts only, `edgeToEdge` before `standard`, PDF or PNG
  pictures, `screens[].outputFrame` or `gameScreenFrame`, extended edges, `translucent`.
- The import error's words become: "This skin is for another console. Only Game Boy, Game Boy Color and GBA skins
  work."

### 4.2 Choosing (`Settings.kt`, `SettingsScreens.kt`, `MainActivity.kt`)
- **Two settings:** `touch_skin_import` stays the GBA family's chosen imported skin (so nobody's choice moves); a
  new one (builder's name, e.g. `touch_skin_import_gb`) is the Game Boy family's. 0 or absent = none. The built-in
  skin (`touch_skin`, the 13 colours) stays one choice for every console.
- **In a game** the imported skin used is the one chosen for the game's family (today: never in a Game Boy game).
  With none, the built-in skin and the family's own touch layout, as today.
- **The Skin page:** the 13 built-in tiles, then every imported skin, then Import — as today. Each imported tile
  carries its family as a small printed mark (**GBA** / **GB**, the game list's label mark; impeccable). Tapping an
  imported tile chooses it **for its family only**. Up to two imported tiles can be chosen at once (one per family),
  each with the chosen border.
- **Tapping a built-in tile** sets the colour and clears **both** imported choices — "back to the built-in skin",
  as today. (Claude's call: no second tap to un-choose one imported skin. A player who wants a Delta skin for one
  family and a built-in colour for the other picks the colour first, then the Delta skin.)
- A built-in tile shows as chosen when at least one family has no imported skin (it is what those games use).
- **After a good import** the new skin is chosen for its family. **Removing** a chosen skin clears its family's
  choice. A chosen skin whose folder is gone or unreadable at start is quietly cleared, per family.
- **Touch buttons page:** the Skin row's summary names what is chosen (builder's wording when the two families
  differ, e.g. "GBA: <name> · Game Boy: <name>"). Each of the four Edit layout rows is greyed with "This skin sets
  its own layout" when **its own family's** chosen skin covers that direction.
- **Body colour and accent:** while a game plays with an imported skin, the body is black (as today), by the game's
  family. Out of a game, the accent and body keep today's rule read from the GBA choice (builder may simplify, as
  long as GBA-only users see no change).
- Backups already carry the skins folder and the settings file; restore needs nothing new.

### 4.3 Drawing and playing (`DeltaLayout.kt`, `DeltaSkinView.kt`, `Screens.kt`)
- The game's box is the skin's screen frame (or, with none, the area milestone 9's §3 gives). Inside it the
  **160×144** picture is fitted by the existing size setting (Fit / Whole steps / Stretch), as the 240×160 one is
  today. Nothing else in the placing, touch areas, 3×3 D-pad, darkening, actions (menu, quick save / load) or
  fast-forward changes.
- A Game Boy skin lacking a direction falls back to the built-in Plain skin with the Game Boy family's own layout.

## 5. Tilt cartridges (the MBC7 chip)

### 5.1 The chip (`core_gb/`), from Pan Docs
Header type **0x22**. Facts to build from (the builder re-reads Pan Docs' MBC7 page before coding; where this text
and Pan Docs differ, Pan Docs wins):
- **ROM bank:** a write to 0x2000-0x3FFF picks the bank at 0x4000 (7 bits).
- **Two switches** guard 0xA000-0xAFFF: 0x0000-0x1FFF = `0x0A`, and 0x4000-0x5FFF = `0x40`. Unless both are set,
  reads give `0xFF` and writes are ignored. 0xB000-0xBFFF always reads `0xFF`.
- **Registers**, picked by bits 4-7 of the address: `0x` and `1x` — the latch (write `0x55` to the first: the kept
  reading is erased to `0x8000`; then `0xAA` to the second: a new reading is taken; only in that order); `2x` / `3x`
  — X low / high; `4x` / `5x` — Y low / high; `6x` reads `0x00`; `7x` reads `0xFF`; `8x` — the save chip's pins.
- **The reading:** level is `0x81D0`; 1 g of tilt moves it by `0x70` (Pan Docs: "roughly"; taken as exact,
  straight-line, Claude's call). **Which way each axis counts** (Claude's reading of Pan Docs' "lower values are
  towards the right / the bottom"): right edge down → X goes **down**; bottom edge down (the top tipped away from
  the floor) → Y goes **down**. With `gb_set_tilt`'s signs (§5.2): `X = 0x81D0 − x·0x70/32767`,
  `Y = 0x81D0 − y·0x70/32767`. Pan Docs does not tie its words to a physical tilt, and no free tilt game exists to
  try, so the builder may check this against other public *documentation* (hardware notes, an emulator's written
  docs — not its code). It is proven for real only by a real tilt game (§9.3, §11).
- **The save chip:** a 93LC56 serial EEPROM — **256 bytes** (128 words of 16 bits) — driven bit by bit through
  register `8x` (chip select, clock, data in, data out) with its commands (read, write, erase, write-all,
  erase-all, write enable / disable).
- No cartridge RAM, no clock, no rumble motor.

### 5.2 In the core
- `MBC7` joins the chip list in `cart.c`; its registers and the EEPROM go in their own file (builder's name).
- `gb_set_tilt(g, x, y)` and `gb_has_tilt(g)`, with `gba_set_tilt`'s meaning (±32767 = 1 g along that edge,
  0 = level; x: right edge down is +, y as the GBA call has it); `machine_` passes both on, and the tilt call does
  nothing for a cartridge without the sensor.
- **Save file:** the 256 bytes as a plain `.sav`, through the existing `gb_save_size` / `_write` / `_read` and the
  dirty flag. The byte order inside each 16-bit word is the builder's call, chosen so saves move to and from other
  emulators if a public source says which they use; the choice is written down in `docs/PROGRESS.md`. Loading is
  as forgiving as for the other chips (17 spec §5.5).
- **Save states and rewind:** the chip's registers, the kept reading, the latch step, the EEPROM's bits-in-flight
  and its bytes are part of the state. **States made by 1.1.0 and 1.2.0 must still load** (no such state can be of
  an MBC7 game — those were refused — so only the format's reading has to stay compatible). The phone's tilt itself
  is not in a state: it is an input, like the keys.
- **RetroAchievements' view** (`gb_ra_peek`): 0xA000-0xAFFF of a tilt cartridge shows the registers **whatever the
  two switches say** (as the view shows cartridge RAM today whether or not it is switched on), with no side
  effects; 0xB000-0xBFFF reads `0xFF` (Claude's call: no achievement set can be checked against it). **GameShark codes** for
  0xA000-0xBFFF are skipped on it (there is no cartridge RAM).
- Unsupported chips are still refused with the same plain message.

### 5.3 In the app and the PC tools
- `emu_cart_hardware` reports the tilt bit (`GBA_HW_TILT`) for such a game, as it reports rumble for a rumble
  cartridge. So the existing `MotionInput` switches the accelerometer on, handles the phone being turned sideways,
  takes the I / J / K / L keys, and says so when the phone has no sensor — with no new Kotlin path.
  `emu_thread.c` hands the tilt to the machine layer instead of straight to the GBA core.
- No new setting and no new screen.
- **PC tools:** the viewer's I / J / K / L keys go through `machine_` (today they call the GBA core directly).
  Key scripts (`tools/common/key_script.c`) have no tilt today, only the gyro's `CW` / `CCW`: they gain four tilt
  names (builder's names) for both consoles, so the test ROM can be steered in a locked fingerprint. No existing
  script or fingerprint may change.

## 6. Words and the release

- README, `PRODUCT.md`, About (if it names a gap), `docs/RELEASE.md` (release notes for 1.3.0; the post drafts): Delta
  skins and tilt cartridges work for all three consoles; no "GBA today" list is left. The Skin page's help line
  says skins for Game Boy, Game Boy Color and GBA can be imported.
- Version **1.3.0**, `versionCode` 4, released by `docs/RELEASE.md` — pushing and publishing only on the user's OK.
- Branch `milestone-17c2`, merged into `main` with `--no-ff` when done.

## 7. Constraints

- Everything in `CLAUDE.md`: test-first, one commit per task, ponytail for code, impeccable + `DESIGN.md` for the
  changed Skin page, layman replies, documents updated every task, the public-safe rule (no game titles: the tilt
  games are "a tilt game"; test data uses made-up names).
- The chip is written from Pan Docs, not from other emulators' code. No ROMs committed; our own hand-assembled test
  ROM (`tools/build_test_roms.py`) may be.
- No new third-party code, integrations or network calls. Test skins are downloaded only with the user's OK, kept
  in the gitignored `tests/skins/`, never committed.

## 8. Approaches considered

**Skins — how the two families' choices are kept.**
(a) **One importer, each skin knows its family, one chosen skin per family** — small change to what is there;
existing choices stay put. **Chosen** (the user picked "one each"; the how is Claude's).
(b) One chosen skin for everything, the other family falling back to the built-in skin — offered, declined.
(c) A second Skin page per console — the same result with more screens to build and to explain.

**Skins — what a built-in tile does.** (a) **Clears both imported choices** — today's meaning, nothing to learn.
**Chosen.** (b) Leaves imported choices alone, and a second tap on an imported tile un-chooses it — more exact, but
existing users lose the obvious way back and the page gets a hidden gesture.

**Tilt — where the phone's tilt comes from.** (a) **The GBA path** (the hardware bit switches `MotionInput` on) —
no new app code, same feel as GBA tilt games. **Chosen.** (b) An on-screen tilt pad — playable without a sensor,
but a new control to design, and the keys already cover phones without one.

**Tilt — proving it without a game.** (a) **Our own test ROM plus unit tests that replay the chip's access
routines**, as milestone 3b-2 did for the GBA cartridge extras. **Chosen.** (b) Waiting for the user to own a tilt
game — the release would hang on it.

## 9. Done when

1. **Tests (all automatic):** every existing test still passes with unchanged fingerprints (PC unit, CTest, Kotlin,
   on-device, `games --check`, `bench --check`). New:
   - *Skins:* a Game Boy skin's `info.json` is read and gets its family; a GBA one is unchanged; another console is
     refused with the new words; `l` / `r` press nothing in a Game Boy skin; the two settings are saved and loaded
     apart, and a 1.2.0 settings file reads as before; picking an imported skin touches only its family, a
     built-in clears both, removing clears its own; the 160×144 picture lands inside the skin's screen frame; on
     the device, the Skin page's marks and the per-family greyed Edit layout rows.
   - *Tilt:* the header type loads; both switches guard the registers; the latch order; the reading for level and
     for ±1 g on each axis; every EEPROM command, bit by bit; the save file round trip and the dirty flag; a state
     and a rewind restore the chip mid-command; an old state still loads; the hardware bit reaches the app; our own
     test ROM's picture is locked as a fingerprint.
2. **Claude's check on the phone emulator:** a Game Boy skin imported (Delta's own standard one, and one with PNG
   pictures and its own game screen if the user allows a second download), upright and sideways — the picture
   sharp, the game exactly in the skin's screen, every button and the D-pad's diagonals, menu, quick save / load; a
   GBA skin and a Game Boy skin chosen at once, each showing in its own games; a built-in tile clears both; remove;
   the greyed rows; light and dark. Our tilt test ROM steered with the emulator's virtual sensors and with the
   keys, upright and sideways; its save survives closing the app. impeccable finish review `ship` for the Skin
   page.
3. **The user's check on their phone** (1.3.0 installed over 1.2.0): they import a Game Boy skin and play a Game
   Boy and a Game Boy Color game with it; their GBA skin choice, games, saves and settings are as before. A tilt
   game only if they own one — if they don't, the release notes say tilt cartridges are new and untried with a
   real game, and ask for reports.
4. Released as **1.3.0** (§6).

## 10. Left to the builder

- All names of new functions, files, settings and tests; whether old states need a format step for the new chunk.
- How the family mark sits on a tile and how the Skin row's summary reads with two choices (impeccable,
  `DESIGN.md`).
- The accent and body colour out of a game (§4.2).
- The EEPROM file's byte order (§5.2); what the test ROM shows.
- The plan's task order. Suggested: skins (reading → settings and choosing → the Skin page → in the game) → the
  chip in the core → tilt through the machine layer, the app and the tools → the words and the version → checks and
  release.

## 11. The user's steps

- **Approve this spec.**
- **Allow the test-skin download(s)** while building: Delta's own standard Game Boy Color skin (from the Delta
  project's public GBC core repository on GitHub — the GBA one came from its GBA twin in milestone 9), and
  optionally one free community Game Boy skin with PNG pictures. Used only for testing, never committed.
- At the end: the phone check in §9.3, then the OK to release.
