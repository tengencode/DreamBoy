# Milestone 17c — the Game Boy extras — Design

**Date:** 2026-11-13
**Status:** Approved by the user 2026-11-13, with the download in §11 ("Approve both"). Plan for part 1: `docs/superpowers/plans/2026-11-13-milestone-17c1-gb-extras.md`. Reviewed once by a builder-check agent (2026-11-13); its points are folded in. **Part 2 done 2026-11-13: DreamBoy 1.3.0** (its own spec `docs/superpowers/specs/2026-11-13-milestone-17c2-gb-skins-tilt-design.md`, tag `v1.3.0`). **Part 1 done 2026-11-13: DreamBoy 1.2.0 released** (tag `v1.2.0`; results and deviations in `docs/PROGRESS.md` — with Auto a cheat may mix Game Genie and GameShark codes; a code also ends at a space). Part 2 (1.3.0) has its own spec: `docs/superpowers/specs/2026-11-13-milestone-17c2-gb-skins-tilt-design.md` (approved by the user 2026-11-13; plan `docs/superpowers/plans/2026-11-13-milestone-17c2-gb-skins-tilt.md`).
**Parent specs:** `docs/superpowers/specs/2026-11-12-milestone-17-game-boy-design.md` (§2: what was left for 17c; §9.3),
`docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones).
**Previous work:** 17a + 17b — DreamBoy 1.1.0 (2026-11-13) plays Game Boy and Game Boy Color games with our own core
(`core_gb/`) behind the shared layer (`machine/`).

---

## 1. Intent

Game Boy and Game Boy Color games get the extras GBA games already have, so a player no longer meets "Not for Game
Boy games yet". This spec covers **part 1, release 1.2.0**: cheats, RetroAchievements, colours for grey games, rumble.

**Why:** these were promised for "the next release" when 1.1.0 was cut down to what is needed to play (milestone 17
spec §2). Game Boy support is in high demand and the user wants it rolled out fast, so the extras ship in two steps
instead of one long wait.

## 2. Decisions made with the user (2026-11-13)

- **Two releases** ("Two releases"):

| 17c part 1 — release 1.2.0 (this spec) | 17c part 2 — release 1.3.0 (its own spec later) |
|---|---|
| Cheats (Game Genie, GameShark, cheat files) | Imported Delta skins made for Game Boy / Game Boy Color |
| RetroAchievements for these games | Tilt cartridges (the MBC7 chip) |
| Colours for grey Game Boy games | |
| Rumble cartridges | |

- **Colours for grey games: a list of ready-made looks and automatic colours together** ("1 and 2 together"): a grey
  game gets the colours a real Game Boy Color would give it where that console knew the game, and a short list lets
  the player change the look, for all grey games or for one. **No screen to mix your own four colours.**
- From before (milestone 17 spec): our own core; no link cable; no Super Game Boy borders or colours.

Everything else below is Claude's call, made the way the GBA side already works; §8 names the bigger ones.

## 3. Goals, non-goals, anti-goals

**Goals**
- A Game Boy / Game Boy Color game's pause menu has a working **Cheats** entry: Game Genie and GameShark codes typed
  in, libretro `.cht` files imported, and the same "find cheat files online" the GBA side has.
- A signed-in player gets **RetroAchievements** in these games exactly as in GBA games: the game is identified before
  it starts, pop-ups, the achievements and leaderboards lists, the hardcore rules.
- **Grey games in colour**: automatic by default, a list of looks to choose from, per game too.
- **Rumble cartridges** shake the phone, under the setting GBA rumble already uses.
- Nothing changes for GBA games, and nothing is lost on updating from 1.1.0.

**Non-goals**
- Delta skins and tilt cartridges (part 2). Other unsupported cartridge chips. Link cable, Super Game Boy.
- A custom-palette editor. Colour correction that imitates the Game Boy Color's screen. Any change to how Game Boy
  Color games look.
- A cheat search / code finder. GameShark codes for other consoles' formats.
- Turning Hardcore on by default (still waits for RetroAchievements' approval of the app, from 2027-05-12).

**Anti-goals** (failure even if it works)
- A cheat left switched on breaking a save state, a rewind or the in-game save in a way the GBA side would not.
- Achievements unlocking wrongly because the memory RetroAchievements reads is laid out differently from what its
  sets expect.
- A player's grey game suddenly unreadable (a look with too little contrast) with no obvious way back to grey.
- Any test fingerprint, test-ROM result or GBA behaviour changing.

## 4. Cheats

### 4.1 The codes
- **Game Genie** — `ABC-DEF` or `ABC-DEF-GHI` (hex digits, dashes optional when typed): a byte of the cartridge ROM
  reads as a new value. New value = `AB`; address = `F C D E` with the top digit `F` XORed with `0xF`; the 9-digit
  form adds a compare byte = (`G I` as a byte, rotated right by 2) XOR `0xBA` (`H` is a check digit and is not
  checked). Addresses are 0x0000-0x7FFF.
- **GameShark** — 8 hex digits `TTVVLLHH`: every frame the value `VV` is written to address `HHLL`. `TT` is the
  code's type / bank byte: `01` (the common one) and any other value below `0x80` mean the bank mapped now; `8x` /
  `9x` name bank `x` **of whichever memory the address is in** — cartridge-RAM bank `x` for 0xA000-0xBFFF, work-RAM
  bank `x` for 0xD000-0xDFFF on a Game Boy Color (many Color codes are `91…` with a 0xDxxx address; bank 0 there is
  read as bank 1, as the hardware does; on a plain Game Boy the digit is ignored for work RAM). A bank the cartridge
  or console does not have: the write is skipped. Addresses 0xA000-0xDFFF and 0xFF80-0xFFFE are written; a code
  for any other address is "not a code".
- A cheat is its codes as text, one per line, as on GBA (`+` between codes on one line is read too — cheat files join
  them that way). Types offered when typing one: **Auto** (from the first code's shape), **Game Genie**, **GameShark**.

### 4.2 In the core (`core_gb/`)
- New, beside the GBA API's shape: `gb_cheat_check(text, type, &bad_line)` (reads a cheat without a machine) and
  `gb_cheats_set(g, texts, types, count)` (the switched-on cheats, replacing the previous ones; call between frames;
  an unreadable text leaves none on). Builder's names for the type enum.
- **Game Genie is applied by patching the loaded ROM bytes when the cheats are set**, not by checking every ROM read:
  for an address below 0x4000 the byte in bank 0 (the 9-digit form: only if it equals the compare value); for 0x4000-0x7FFF the byte at that offset in **every** bank whose
  byte equals the compare value (the 6-digit form, without a compare: every bank). The original bytes are kept and
  put back when the cheats are set again or cleared. This matches what the real device does (it patches whichever
  bank is mapped when the compare fits) at no cost while running.
- **GameShark writes happen at the end of every `gb_run_frame`**, straight into the memory (the named or current
  work-RAM bank for 0xD000-0xDFFF, the named or current cartridge-RAM bank, high RAM) — not through the bus, so no register side
  effects. A write to cartridge RAM marks the save dirty like a game's own write.
- **Save states and rewind never contain cheats** (as on GBA): a state is saved from the patched-back ROM's point of
  view (the ROM is not in a state anyway), and after a state load or a rewind step the frontend's cheats are still
  on. The cheats are not part of the state format, so "GBCS" does not change.
- `machine/` gains `machine_cheat_check` / `machine_cheats_set` that pass to the right core; cheat type numbers are
  per console (a game's cheats are stored under its own game id, so the two numberings never meet).

### 4.3 In the app
- `emu_cheats_set` stops refusing Game Boy games; the pause menu's Cheats row is a normal row again
  (`NO_CHEATS_YET` goes).
- The cheat screen is the GBA one; only the list of types and the hint text for a new cheat follow the console.
- **Cheat files:** `ChtFile` already reads libretro's format; whether a cheat's code is readable is asked of the
  game's own console. "Find cheats online" looks in libretro's folder for the game's console (`Nintendo - Game Boy`,
  `Nintendo - Game Boy Color`), by the box-art name it already knows from that console's list (17b).
- Hardcore: cheats are off, the row says "Off in hardcore mode", as for GBA. In softcore a Game Genie patch shows
  through the ROM region RetroAchievements reads (§5) — accepted, as GBA ROM patches are today.
- Backup and restore carry these cheats already (they are per game id).

## 5. RetroAchievements

- **The session:** a Game Boy / Game Boy Color game is identified and loaded like a GBA game (`ra.loadGame(rom)` →
  wait → use the set, or play on without one). The hash is the MD5 of the whole ROM file — the same call as today.
  RetroAchievements' server says which console the hash belongs to; nothing is sent that is not sent for GBA games.
- **The memory the achievements read** — the vendored rcheevos (`third_party/rcheevos/src/rcheevos/consoleinfo.c`)
  fixes this layout for both consoles; `platform/ra` must serve exactly it:

| RetroAchievements address | What it is |
|---|---|
| 0x0000-0x7FFF | Cartridge ROM as the game sees it now (bank 0, then the bank mapped at 0x4000) |
| 0x8000-0x9FFF | Video RAM (the bank mapped now) |
| 0xA000-0xBFFF | Cartridge RAM **bank 0**, whatever bank is mapped |
| 0xC000-0xCFFF | Work RAM bank 0 |
| 0xD000-0xDFFF | Work RAM **bank 1**, whatever bank is mapped |
| 0xE000-0xFDFF | Echo of 0xC000-0xDDFF |
| 0xFE00-0xFE9F | Sprite memory; 0xFEA0-0xFEFF reads as 0 |
| 0xFF00-0xFF7F | The hardware registers, read without side effects |
| 0xFF80-0xFFFE, 0xFFFF | High RAM; the interrupt-enable register |
| 0x10000-0x15FFF | Work RAM banks 2-7 (Game Boy Color; nothing on a plain Game Boy) |
| 0x16000-0x33FFF | Cartridge RAM banks 1-15 |

  A read of memory the cartridge or console does not have returns "0 bytes read" for that part, as `ra_peek` does today
  past the GBA save chip.
- **How:** `ra_memory_t` stops being three GBA pointers and becomes "which console + what that console needs"; for
  Game Boy that is the machine (or a small set of pointers / a peek function — builder's choice; `gb_debug_peek` in
  `gb_debug.h` already reads the address space without side effects, the fixed-bank regions come on top). The
  pretend server (`platform/ra/ra_fake.c`) answers console 5 (GBA) for every game today; it has to answer 4 (Game
  Boy) or 6 (Game Boy Color) for the new tests and checks. The register peek must have **no side effects** (reading the serial, joypad, sound
  or palette registers through it must not change the machine) and is called only where `ra_do_frame` / `ra_idle`
  are called today (the emulation thread owns the core).
- **Everything else is the session's and already there:** pop-ups, the lists, leaderboards, rich presence, the
  outbox for unlocks made offline, the hardcore limits (no state loads, no rewind, no cheats, no slow-motion; a
  restart when hardcore is switched on), `ra_reset_progress` after a state load or rewind in softcore.
- `emu_create` keeps the session for Game Boy games; the "No achievements for Game Boy games yet" notice goes.
- Hardcore stays **off by default** and RetroAchievements still answers "Unknown Emulator" until they approve the
  app; nothing about that changes here.

## 6. Colours for grey games

Only games that run as a plain Game Boy (milestone 17 spec §5.6: header byte 0x143 bit 7 clear) are affected. Game
Boy Color games carry their own colours and are left alone.

### 6.1 How a look is applied
- A plain Game Boy has three palettes of four shades: the background (and window), and two for sprites. A **look**
  is three sets of four colours: one for the background's shades 0-3, one for each sprite palette's. This is exactly
  how a real Game Boy Color colours a grey game — backgrounds and sprites can differ.
- The core gets `gb_set_dmg_colours(g, bg[4], obj0[4], obj1[4])` (15-bit colours, the framebuffer's format; builder's
  name); `ppu_draw.c` uses them where it uses its fixed `GREY[4]` today. **Without that call the four greys stay**,
  so every test fingerprint, the test runner, the bench and the game check are untouched.
- The look is **not** part of a save state and not rewound: it is how the screen is shown. It applies from the next
  drawn frame. A state's stored picture keeps the colours it was taken with.
- It can only be done in the core: after a line is drawn, a background grey and a sprite grey cannot be told apart.

### 6.2 The looks
1. **Automatic** (the default) — what a Game Boy Color does for a grey game: it adds up the title bytes in the
   cartridge header (0x134-0x143) — only for cartridges whose licensee is Nintendo (old licensee byte 0x14B = 0x01,
   or 0x33 with the new licensee "01") —, looks the sum up in its table of known games (a few sums need the title's
   fourth letter to tell two games apart) and takes that game's look. **A game the table does not know stays grey**
   (Claude's call: the real console would give it a fixed green-and-red default; grey is the look 1.1.0 players
   already have).
2. **Grey** — today's four greys.
3. **Original green** — the first Game Boy's yellow-green screen.
4. **Pocket** — the Game Boy Pocket's cooler, higher-contrast grey-olive.
5. **The Game Boy Color's own twelve looks** — the ones its owners picked with a button press while it started
   (brown, red, dark brown, pastel mix, orange, yellow, blue, dark blue, grey, green, dark green, inverted; the one
   that equals plain grey is not listed twice).

The automatic table and the twelve looks are facts about the hardware; **their values come from a public source,
not from memory** (§11: one small download the user has to allow). Pan Docs (public domain) describes the method
but not the whole table; the table itself is taken from **SameBoy's boot ROM source** (`BootROMs/cgb_boot.asm`,
MIT — fits our GPL-3.0-or-later; credited in `third_party/README.md` and in a comment), cross-checked against a
second public listing where one can be had. Sources under GPL-2-only or with no licence are not used. Only the
data is taken: the lookup is our own code. The values for 3 and 4 are the
builder's, chosen for contrast (each neighbouring pair of shades clearly apart on a phone screen).

### 6.3 Where the player chooses
- **Settings → Picture: "Grey Game Boy games"** — the list above, each row with its four background colours as a
  small strip so the choice can be seen before it is made. Applies to every grey game without its own choice.
- **Per game:** Game settings is the entry in the game list's hold menu (`Screens.kt`; hidden for Game Boy games in
  1.1.0). It appears for a grey game with one setting, "Colours": **Same as in Settings** (default) or one of the
  looks. For a Game Boy Color game the entry stays hidden — there is nothing to set. Which it is comes from the
  console the list stored for the game (GB = grey); a game with no stored console yet (never opened since 1.0.0 and
  not seen by a folder scan) keeps the GBA settings it has today until it has one. No new pause-menu entry.
- The game is not running when a per-game look is chosen, so it shows the next time the game opens. The app-wide
  choice can be reached from the pause menu (Settings → Picture) and applies from the next drawn frame.
- The setting is stored like the other settings (`Settings` for the app-wide one, `GameSettings` per game id) and so
  travels in backups.
- New UI goes through the impeccable skill and `DESIGN.md`; no new kind of component — a choice list with a colour
  strip.

## 7. Rumble

- MBC5 rumble cartridges (header types 0x1C-0x1E): bit 3 of the RAM-bank register drives the motor (the core already
  keeps it out of the bank number). The core gains `gb_rumble_active(g)` — 1 while the motor bit is set — with the
  meaning `gba_rumble_active` has, and `machine_` passes it on.
- The app treats the game as having rumble hardware, so the existing `Rumble` class, its start / stop with the game
  and the existing vibration setting apply. Games switch the motor on and off many times a second for a weaker
  shake; the phone gets what the GBA path gives it today (builder: sample as the GBA path samples).
- Nothing rumbles while paused, rewinding or in a state load's first frame, as on GBA.

## 8. Approaches considered

**Cheats — where Game Genie is applied.** (a) Check a small list on every ROM read: simple, exact, but it sits on
the core's hottest path and costs speed for everyone who has a cheat on. (b) **Patch the ROM bytes once when cheats
are set, in every bank the compare fits, and undo on change** — the same result for real codes, free while running.
(c) Patch only the bank mapped at that moment: wrong as soon as the game switches banks. **Chosen: (b).**

**Colours — where the look is applied.** (a) In the app, recolouring the finished grey picture: cannot give sprites
their own colours, which is the whole charm of the Game Boy Color's looks. (b) Run grey games on the emulated Game
Boy Color in its compatibility mode: needs the boot ROM's behaviour and changes timing-sensitive hardware under
games that pass today. (c) **Three small colour tables in the core's line renderer, grey when unset.** **Chosen: (c).**

**RetroAchievements — how the memory is served.** (a) Copy the whole address space into a buffer every frame: simple
but wasteful, and wrong for the fixed-bank regions. (b) **Answer each read from the core's own memory by the table in
§5**, with a side-effect-free register peek. **Chosen: (b).**

**One release or two.** The user chose two (§2).

## 9. Done when

1. **Tests (all automatic):** every existing test still passes with unchanged fingerprints (PC unit, CTest, Kotlin,
   on-device, `games --check`, `bench --check`). New: Game Genie and GameShark decoding and application (a made-up
   ROM: a patched byte reads new in the right banks only, is put back on clear; a GameShark byte holds its value
   against the game's writes; neither is in a state); the RetroAchievements address table of §5 against a machine
   with known bytes in every region, both consoles, including "the register peek changes nothing" (a state before
   and after is identical); the automatic colours for several made-up headers (a known sum, a sum needing the fourth
   letter, an unknown one → grey, a non-Nintendo licensee → grey); a look changes the picture and no call leaves it
   grey; rumble follows the motor bit.
2. **Claude's check on the phone emulator**, with the pretend RetroAchievements server (`files/ra-fake`): a Game Boy
   game with a made-up set unlocks an achievement and shows its lists; a cheat typed in, one from a `.cht` file;
   the looks list, light and dark, font 1.3×; a per-game look; hardcore switches cheats and state loads off.
3. **The user's check on their phone** (1.2.0 installed over 1.1.0): a grey game of theirs shows in colour by itself;
   they change the look for all games and for one; a cheat works in a Game Boy and a Game Boy Color game of theirs;
   signed in to RetroAchievements, a Game Boy game shows its achievements and one unlocks (softcore); their GBA
   games, saves, states and settings are as before. Rumble only if they own a rumble game.
4. Released as **1.2.0** (versionCode 3) the way `docs/RELEASE.md` says, with the words in the README, About and
   release notes moved from "later" to "now" for these four things, and the privacy text checked (RetroAchievements
   now also sees Game Boy games' hashes — the same kind of data as for GBA).

## 10. Left to the builder

- All names of new functions, files and tests; whether the Game Boy cheat code lives in one file or two.
- How `ra_memory_t` is reshaped and what the core offers for it.
- The exact colours of "Original green" and "Pocket"; the order of the list; the strip's size.
- How "find cheats online" learns the game's libretro name when box art is switched off (the GBA path already
  solves this — follow it).
- The plan's task order. Suggested: colours (smallest, independent) → rumble → cheats → RetroAchievements → the
  app's words → checks and release.

## 11. The user's steps

- **Approve this spec.**
- **Allow one small download** while building: the table of the Game Boy Color's colours for grey games (the
  automatic table and the twelve looks) from SameBoy's boot ROM source (MIT), plus reading Pan Docs. It becomes a
  small table in our own code, credited in a comment and in `third_party/README.md`. (The user declined SameBoy as
  the *engine* in milestone 17; this is only a table of numbers with credit — say so if that is not wanted either,
  and automatic colours then wait for another source.)
- At the end: the phone check in §9.3, then the OK to release.
