# Milestone 17 — Game Boy and Game Boy Color — Design

**Date:** 2026-11-12
**Status:** Approved by the user 2026-11-12 (reviewed once by a builder-check agent; its points are folded in); the test-ROM downloads are allowed (the user, 2026-11-12). Plan for 17a: `docs/superpowers/plans/2026-11-12-milestone-17a-gb-core.md` — **17a done 2026-11-12** (the engine on the PC; results and deviations in `docs/PROGRESS.md`: 135 public test ROMs pass, both acid2 pictures exact; the PC stand-in for the speed gate in §5.7 was missed at 3.4-4.9× instead of 5× — the phone measurement at the start of 17b decides). Plan for 17b: `docs/superpowers/plans/2026-11-13-milestone-17b-gb-app.md` — **17b done 2026-11-13** (the app; the phone speed in §5.7 measured on phone A's small core at 7.9-11.4× against the 4× goal; the user's phone check passed; **DreamBoy 1.1.0 released**, tag `v1.1.0`). Next: 17c the extras, with its own spec.
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§10 milestones)
**Previous work:** milestone 16 (DreamBoy 1.0.0, GBA only, public at `https://github.com/tengencode/DreamBoy`).

---

## 1. Intent

DreamBoy plays **Game Boy (`.gb`) and Game Boy Color (`.gbc`)** games as well as GBA games, in the same app, the same
game list, with the same Continue / pause menu / save states people already know.

**Why now (the user, 2026-11-12):** it is in high demand and has to roll out fast. It is also the long-term goal the
name DreamBoy was chosen for (milestone 16 spec §1: "a one stop shop").

## 2. Decisions made with the user (2026-11-12)

- **Our own Game Boy engine**, not an existing one (SameBoy was offered as the faster route; the user chose "Write our
  own"). It keeps the project's "own core" story and adds no third-party code.
- **Two releases.** The first (1.1.0) has what is needed to play; the extras follow:

| First release — 1.1.0 (17a + 17b) | Next release (17c) |
|---|---|
| Play `.gb` / `.gbc` games, with sound | Cheats (Game Genie, GameShark) |
| Saves, and the cartridge clock | RetroAchievements for these games |
| Save states, rewind, fast-forward | Imported Delta skins made for Game Boy / Game Boy Color |
| Touch buttons, the 13 console skins, keyboard, gamepad | Colour choices for grey Game Boy games |
| Box art, backup | Rumble and tilt cartridges |

- **Not planned for either:** link cable, Super Game Boy borders and colours.

## 3. The parts

| Part | What | Done when |
|---|---|---|
| **17a** | The engine on the PC: one core for both consoles, proven by public test ROMs; the shared "machine" layer; the PC tools run both consoles | §9.1 |
| **17b** | The engine in the app: every screen handles both consoles; release 1.1.0 | §9.2 |
| **17c** | The extras (right column above) | its own spec after 17b |

Each part gets its own plan. 17c gets its own spec.

## 4. Goals, non-goals, anti-goals

**Goals**
1. A Game Boy / Game Boy Color core, `gbcore`, as careful as the GBA one: test-first, proven by public test ROMs and
   locked fingerprints, never crashing on anything a game does.
2. One app, one list: a `.gb` or `.gbc` file in the games folder (or inside a `.zip`) shows up next to the GBA games and
   opens the same way.
3. Everything in the left column of §2 works for these games as it does for GBA games.
4. **Nothing changes for GBA games:** every existing test, fingerprint, locked game, save file and save state keeps
   working, and GBA speed stays within the milestone 13 floors (97 %).
5. Fast on old phones: at least **4× real speed on phone B's small core** for both consoles (the project's usual aim),
   measured before release.
6. The public pages say what the app now plays (README, About, repo description and topics, the post drafts).

**Non-goals**
- Everything in §2's right column (17c) and "not planned".
- A boot ROM: no start-up logo, no option to load one (the core starts where the boot ROM would leave off).
- Pixel-by-pixel drawing (see §5.3); sound exact to the cycle; the infrared port; the Game Boy Printer and Camera.
- Rare cartridge chips (anything beyond §5.5): such a game is refused with a plain message, not half-run.
- ARM64 assembly for this core, unless goal 5 is missed (§5.7).
- Renaming the Kotlin package, the `gba_` API or the folder `core/`.

**Anti-goals**
- **Breaking GBA.** A GBA regression is a failed milestone even if Game Boy games work.
- **Fast but wrong.** "Rolled out fast" must not mean games that glitch: nothing ships until §9's test lists pass.
- **A Game Boy game that half-works silently:** unsupported cartridges and broken files get a clear message.
- **Promising the extras early:** the app and README must not suggest cheats or achievements work for these games
  before 17c (the pause menu says so plainly, §6.6).

## 5. The core (17a)

### 5.1 Where it lives and its API
A new library next to the GBA one: `core_gb/` (`include/gb.h`, `src/…`), CMake target `gbcore`, depending only on libc,
with the same layering rules as `core/` (parent spec §2.1: no OS, threads or files; pull-based; heap only at create /
load / rewind setup).

`gb.h` mirrors `gba.h` so the two are easy to put behind one layer: create / destroy / load ROM / reset, run one frame,
framebuffer, audio read, set keys, ROM title and CRC-32, cartridge RAM (+ dirty flag), clock input, state size / save /
load, rewind configure / step back, RAM views (for 17c's achievements), a debug header for tests.

Three numbers are chosen to match the GBA core, so the platform's pacing and sound code need no second path:
- **Frame:** 160×144, **BGR555** (`uint16_t`) — the same pixel format, so the same shader.
- **Sound:** stereo int16 at **32,768 Hz** (the Game Boy clock ÷ 128, exactly).
- **Frame rate:** 70,224 clock ticks at 4,194,304 Hz = **59.7275 Hz**, exactly the GBA's rate.

`gb_run_frame` runs exactly 70,224 ticks (double speed: the same wall time), also while the screen is switched off
(then the last picture stays, or white — builder's call, as the hardware shows blank).

### 5.2 CPU and timing
The SM83 CPU as a plain interpreter (all 500 opcodes, HALT with its bug, STOP for speed switching, EI's one-instruction
delay, interrupts). **Timing by machine cycle:** every memory access advances the timer, screen, sound and serial
hardware by 4 ticks before the access happens — the simple scheme that passes the instruction- and memory-timing test
ROMs. No event scheduler (the GBA core's is not reused).

Timer and divider with the real hardware's falling-edge behaviour (writes to DIV / TAC can tick the timer; the
overflow's reload comes one cycle late). Serial port: no cable — a transfer with the internal clock finishes with
`0xFF` and its interrupt, so games don't hang. Joypad register and interrupt.

### 5.3 Screen
A **line renderer**: each line is drawn once, when its drawing period starts, from the registers as they are then
(background, window with its own line counter, 10 sprites per line with the right priority rules for each console).
Mode and interrupt timing (the STAT register, LY = LYC, the blocking of one STAT source by another) is by tick; the
drawing period's length varies with sprites and scroll as on hardware (the usual estimate). Changes in the middle of a
line are not drawn mid-line — the same decision as the GBA core. The exact moment within the line at which the
registers are read is the builder's call, with one rule: dmg-acid2 and cgb-acid2 must match exactly (§9.1) — if the
chosen moment fails them, the moment changes, not the finish line.

Colours go to the screen as the game sets them: no colour correction (the app has none for GBA either) and no blending
of one frame into the next. Both are candidates for 17c, not 1.1.0.

Game Boy Color: two tile banks, tile attributes (palette, flip, bank, priority), 8 + 8 palettes of 15-bit colour, the
"master priority" bit, and the colour rules for old Game Boy games (they run as a plain Game Boy, below).

### 5.4 Sound
The four channels (two squares, one with sweep; the wave channel; noise), the 512 Hz sequencer driven from the divider,
the length / envelope / sweep quirks the test ROMs check, stereo routing and master volume; the wave RAM's access
rules. Output sampled to 32,768 Hz with a simple box average over each output sample (so high notes don't whistle);
the output filter that removes the DC offset.

### 5.5 Cartridges
- **Chips:** none (32 KiB), **MBC1**, **MBC2**, **MBC3** (with the clock; the bigger MBC30 variant runs as MBC3 with
  more banks), **MBC5**. These cover nearly every game. MBC5's rumble bit is accepted and ignored until 17c. MBC1
  multi-game cartridges are not detected (non-goal). Anything else (MBC6, MBC7 — tilt, 17c —, HuC1, HuC3, MMM01, the
  camera, TAMA5): `GB_ERR_UNSUPPORTED_CART`.
- **Loading:** size 32 KiB – 8 MiB; the header's size bytes are trusted only when they fit the file; a wrong header
  checksum is a warning, not an error (homebrew and hacks). ROM title and CRC-32 as on GBA.
- **Save:** only cartridges with a battery save. The file is the cartridge RAM as a plain `.sav` (MBC2: 512 bytes,
  the unused high half of each byte written as 1s); for MBC3 with a clock, the footer other emulators use is appended:
  40 bytes of clock registers (5 live + 5 latched, 4 bytes each) + an 8-byte Unix timestamp = **48 bytes** written;
  a 44-byte footer (4-byte timestamp) is read too. So saves move both ways. **The core builds and reads the whole
  file** (`gb_save_size` / `gb_save_write` / `gb_save_read`, builder's names), because the footer is the core's
  state. Loading is forgiving, unlike GBA: RAM size, +44 or +48 load as they are; any other length is copied as far
  as it fits and the rest filled with `0xFF` (saves from other emulators come in odd sizes). Dirty flag as on GBA.
- **The clock:** the core holds the cartridge's own clock registers (seconds … day counter, halt and carry bits) —
  the game sets them. The frontend only says what time it is now (`gb_set_rtc(unix_seconds)`, once a second, as for
  GBA); the core moves the registers forward by the real seconds that passed since the last call, unless the game
  halted the clock. On loading a save, the seconds since the footer's timestamp are added the same way, so time
  passes while the app is closed. A save state restores the registers and then catches up to now likewise.

### 5.6 Which console a game runs on
From the header's colour byte (0x143): **bit 7 set → Game Boy Color** (colour-only and colour-enhanced games both);
otherwise **a plain Game Boy** with four greys. The models imitated are the original Game Boy ("DMG-ABC" in the test ROMs' names) and a late Game Boy Color ("CGB-E"); tests written for other models are out of scope. No per-game switch and no palette choice in 1.1.0 (17c: colour choices).
The core starts with the registers and memory as each console's boot ROM leaves them (the documented values).

Game Boy Color extras: double speed, 8 work-RAM banks, 2 video-RAM banks, palettes, general and per-line memory copy
(HDMA), the undocumented registers games poke.

### 5.7 States, rewind, speed
- **Save states:** the GBA core's scheme (header + versioned chunks, atomic load, refused for another ROM) with its own
  magic so a state can never load into the wrong core. **Rewind:** the same packed-difference ring; `delta_pack.c` is
  shared rather than copied if that is clean (builder's call).
- **Speed:** measured with the bench tool. The plain C interpreter is expected to pass 4× on phone B easily (a Game
  Boy is far less work than a GBA). **17a's stand-in for the phone number:** on the PC every Game Boy bench scene runs
  at least **5× the frames per second of the slowest GBA scene** in the same session (that GBA scene runs 3.4× on
  phone B's small core, so this leaves a wide margin). The phone itself is measured in 17b; assembly is added only if
  a gate is missed.

### 5.8 The shared layer ("machine")
A small C layer, `machine/` (builder's name), with one handle and one set of calls — load, reset, run frame, frame
size + pixels, audio, keys, save RAM, clock, state, rewind, title / CRC, console kind — forwarding to `gba_*` or
`gb_*`. It picks the core from the file's **content first** (a GBA header vs a Game Boy header checksum), the file
extension only as a tie-breaker, so a misnamed file still opens.

GBA-only things (cheats, tilt / gyro / light / rumble, the BIOS file, save-type override, achievement memory) stay
reachable through it as "this console doesn't have that" answers, not crashes.

In 17a the PC tools move onto it: `testrunner`, `gbabench`, the viewer, the frame hash (any size) and the game check
run both consoles. In 17b the Android layer moves onto it (§6.1).

## 6. The app (17b)

### 6.1 Native layer
`emu_thread.c` and `jni_bridge.c` call the machine layer instead of `gba_*`. The frame size comes from the machine:
the triple buffer, texture, shader, fit rectangle (whole-number scaling included), screenshots and the readback check
stop assuming 240×160. Pacing, sound conversion and rate control are unchanged (§5.1). `libgbaemu.so` links both
cores.

### 6.2 Finding and opening games
`.gb` and `.gbc` join `.gba` and `.zip` in the folder scan, the zip reader (a zip holding one game of any of the three
kinds) and the first-start text. Game id stays the ROM's CRC-32, so saves (`files/saves/<id>.sav`), states, box-art
choices and backups need no new layout. Opening an unsupported or broken file shows the core's reason in plain words.

### 6.3 Picture and buttons
- The picture is 160×144 (10:9) for these games everywhere it is shown: in the game, the layout editor, state slots,
  the Continue card and the cartridge labels (no stretched or cropped pictures).
- **No L and R** for these games: not drawn, not in the key table. The touch layout is **kept per console family**
  (GBA; Game Boy + Game Boy Color), upright and sideways, each with its own defaults and its own editor entry;
  existing GBA layouts are untouched. Keyboard and controller bindings are shared (L / R simply do nothing).
- The 13 console skins colour these games too. An imported Delta skin is a GBA skin: in a Game Boy game the app falls
  back to the built-in skin (Delta skins for these consoles are 17c).

### 6.4 The game list
Each game shows which console it is for (a small printed mark on the cartridge label — how it looks is impeccable's
job, keeping to `DESIGN.md`). No filter or tabs in 1.1.0 unless the design work finds the list needs it.

### 6.5 Box art
libretro's "Nintendo - Game Boy" and "Nintendo - Game Boy Color" lists, found by the ROM's fingerprint (these consoles
have no 4-letter code to fall back on, so hacks get no automatic box; Choose box art still works). A console's name
list is downloaded only when the game list holds a game of that console. A game is looked up in its own console's list
first (by the header's colour byte) and then in the other one, since colour-enhanced games appear in either.

### 6.6 What is switched off for these games until 17c
- **Cheats:** the pause menu's Cheats entry says they aren't available for Game Boy games yet.
- **RetroAchievements:** these games run with no RetroAchievements session at all — no identify call, nothing sent,
  so no hardcore limits either (the limits belong to a session) — with one short notice so a signed-in player isn't
  left wondering.
- Game settings: nothing to set for these games in 1.1.0 (the cartridge header gives the save type, and a clock
  cartridge's clock is always on); the entry is hidden or says so.
- State pictures and the Continue picture take their size from the game's console (the picture files carry their own
  size; nothing new is stored).
- BIOS settings, tilt / light / rumble: GBA only, untouched.

### 6.7 Words and the release
- Text in the app, README, `PRODUCT.md`, About, the repo description and topics, and the post drafts say "Game Boy,
  Game Boy Color and Game Boy Advance" and list what isn't there yet for the first two.
- Version **1.1.0** (`versionCode` + 1), released by `docs/RELEASE.md` — pushing and publishing only on the user's OK.

## 7. Constraints

- Everything in `CLAUDE.md`: test-first, one commit per task, ponytail for code, impeccable + `DESIGN.md` for UI,
  layman replies, documents updated every task, the public-safe rule (test data uses made-up names; the user's own
  games only in the gitignored local lists — "game A" and so on in documents).
- **No ROMs, boot ROMs or copyrighted test material committed.** Test ROMs are downloaded by
  `tools/fetch_test_roms.py` with pinned SHA-256 sums, **after the user's OK**; our own test ROMs (hand-assembled
  bytes built by `tools/build_test_roms.py`, as for GBA) may be committed.
- No new third-party code, no new integrations or network calls (box art uses the existing ones with new list names).
- The core is written from public hardware documentation (Pan Docs and the test ROMs' own notes), not from other
  emulators' code.
- Branch `milestone-17`, merged into `main` with `--no-ff` when a part is done.

## 8. Approaches considered

**The engine**
1. **SameBoy** (MIT) wired in — fastest, very accurate; adds a large third-party part and its boot ROMs. Offered as
   the pick for speed.
2. **Our own core** — more work and our own bugs to find; no third-party code, keeps the project's story. **Chosen by
   the user.**
3. mGBA's or Gambatte's Game Boy core — dropped: tangled into a bigger program / a licence (GPL-2.0-only) that does not
   fit GPL-3.0-or-later.

**Fitting two cores into the app** (Claude's call; the user left the how to Claude)
1. **A thin shared layer** the tools and the Android code call — one place knows there are two consoles. **Chosen.**
2. "If Game Boy, else GBA" at each of the ~130 call sites — quicker to start, easy to miss one, worse with every
   feature.
3. A second native library and game screen for Game Boy — doubles the sound / picture / thread code.

**Timing inside the core** (Claude's call)
1. **Machine-cycle stepping with a line renderer** — passes the timing tests that matter, small, fast. **Chosen.**
2. A pixel-by-pixel screen — the most exact, needed by a handful of demos; much more work. Possible later behind the
   same API.
3. Instruction-level stepping — simplest, but fails the timing tests and breaks real games.

## 9. Done when

### 9.1 Part 17a
- Unit tests for every piece (CPU opcodes and flags, timer edges, each cartridge chip, the clock, each sound channel,
  screen scenes exact to the pixel, state round trip, rewind).
- Public test ROMs (downloaded with the user's OK), all through `dev.sh test`:
  - **blargg:** `cpu_instrs`, `instr_timing`, `mem_timing`, `mem_timing-2`, `halt_bug` pass; `dmg_sound` and
    `cgb_sound` as far as a time-boxed task gets them (each sub-test's result recorded; aim: at least 8 of 12 each).
  - **mooneye:** from `acceptance/`, the timer, interrupt, instruction-timing and `bits` groups and the boot-state
    tests for our two models (`boot_regs-dmgABC`, `boot_div-dmgABC`, `boot_hwio-dmgABC`, and the CGB ones the suite
    has for a late Game Boy Color) pass; from `emulator-only/`, the MBC1, MBC2 and MBC5 groups pass, except
    `multicart_rom_8Mb` (multi-game cartridges are a non-goal); the screen-timing group (`acceptance/ppu`) as far as
    the line renderer allows (each result recorded). Tests named for other models (mgb, sgb, sgb2, agb, ags) are out.
  - **dmg-acid2 and cgb-acid2:** the picture matches the published reference exactly.
- A handful of free homebrew Game Boy and Game Boy Color games added to the game check and locked in
  (`dev.sh games --check`), next to the 31 GBA entries.
- The PC viewer plays a Game Boy game with sound; the bench reports both consoles.
- Every GBA test, fingerprint and locked game unchanged; GBA PC speed within the 97 % floors.

### 9.2 Part 17b
- All tests pass (unit, CTest, Kotlin, on-device, `games --check`), with new ones for both consoles.
- Claude's check on the phone emulator: a `.gb`, a `.gbc` and a `.gba` game in one list, each opened, saved, a state
  saved and loaded, rewound, fast-forwarded; upright and sideways, light and dark; the layout editor for both
  families; box art; a backup made and restored; a zipped game; an unsupported cartridge's message. impeccable finish
  review `ship` for the changed screens.
- Speed on phone B's small core at least 4× for both consoles; GBA speed there unchanged.
- **The user's check:** a Game Boy game and a Game Boy Color game of their own played on their phone, with a save that
  survives closing the app.
- Release 1.1.0 built, checked and — on the user's OK — published.

## 10. Left to the builder

- File layout inside `core_gb/`, the opcode decoder's shape, the state chunk list.
- The machine layer's exact shape (a struct of function pointers or a switch) and name.
- Which homebrew games go into the game check (free licences, pinned sums), and which become README screenshots.
- How the console mark, the per-family layouts and the "not yet" notes look (impeccable).
- The Game Boy grey palette's four values (plain greys; no green tint unless the design work argues for it).
- The order of tasks, and splitting a part further if it grows.

## 11. The user's steps

1. **OK the test-ROM downloads** (blargg's and mooneye's test ROMs, dmg-acid2, cgb-acid2, a few homebrew games; all
   free to download, none committed).
2. The check in §9.2 on their own phone.
3. The OK to push and publish 1.1.0.
