# Milestone 10b — Box Art (+ cheat files online) — Design

**Date:** 2026-10-04
**Status:** Implemented — milestone 10b complete 2026-10-04 (tag `milestone-10b`): all tests pass and Claude's check on the emulator passed with real internet (hack 1 got game A's box by its code; Find cheats online imported game A's Code Breaker file). Brainstormed with the user 2026-10-04 (all three design parts approved: 'looks right'); spec approved ('approved, write the plan'). No prototype (user decision). Plan: `docs/superpowers/plans/2026-10-04-milestone-10b-box-art.md` (9 tasks).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestone 6a (the game list: `Library.kt`, `FolderScan.kt`, `GameId.kt`, `LibraryScreen` / `GameCard` / `GameHoldMenu` in `Screens.kt`), 8a (the look), 10a (`ChtFile.kt`, the cheat panel's import), all merged into `main`.

---

## 1. Goal and scope

**Milestone 10** is split (user decision 2026-10-04) into 10a cheat files ✅ → **10b box art** (this spec) → 10c cloud sync.

**10b's goal:** the game grid shows each game's box, downloaded from libretro; the cheat panel can fetch libretro's cheat files for the game. This is the app's first use of the internet (download only).

**Decisions (user, 2026-10-04):**
- **Where:** the grid's small cards show the box; the big Continue card keeps the picture of where the game was left.
- **When:** automatically, in the background, while the game list is on screen; a Settings switch **Download box art** (on by default) turns it off.
- **No match** (hacks, homebrew, test ROMs): the card falls back to today's picture (where it was left, or the placeholder). The game's hold menu gets **Choose box art** (any picture from the phone) and **Remove box art**.
- **Cheats online:** yes — **Find cheats online** in the cheat panel lists libretro's cheat files for the game; one tap imports it as in 10a.
- **Approach 1:** the app downloads libretro's GBA game list once (refreshed after 30 days) and finds a game by its **fingerprint** (CRC32, our `GameId`), else by the **4-letter game code** in the ROM's header (hacks keep their original's code, so a hack gets its original's box and cheat files). Not 2 (the list built into the app) or 3 (file names only).
- **No prototype**: spec → plan.
- **Done when:** Claude's check on the phone emulator passes and all tests pass — **no user check** (user decision). The user's approval of this design covers the check's downloads (the game list, about 3 boxes, 1–2 cheat files).

**Not in 10b:** searching box art by name; other pictures (title screens, in-game snaps); box art on the Continue card; uploading anything; cloud sync (10c); any change to the core.

## 2. Where things come from

All HTTPS, from libretro only (the URLs below; spaces and other characters in a path part are URL-encoded):

| What | URL | Limit | Kept |
|---|---|---|---|
| Game list | `https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat` (≈ 1 MB, clrmamepro text) | 5 MB | as `files/boxart/names.tsv` (fingerprint, code, name), 30 days |
| A box | `https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/<picture name>.png` (≈ 0.5 MB) | 5 MB | shrunk, as `files/boxart/<id>.jpg` |
| Cheat file list | `https://api.github.com/repos/libretro/libretro-database/contents/cht/Nintendo%20-%20Game%20Boy%20Advance` (JSON; 514 files) | 2 MB | the names, as `files/boxart/cheat-files.txt`, 30 days |
| A cheat file | `https://raw.githubusercontent.com/libretro/libretro-database/master/cht/Nintendo%20-%20Game%20Boy%20Advance/<file>` | 1 MB (10a's) | not kept (imported) |

- **The game list** (read 2026-10-04): blocks `game ( name "…" … rom ( name "….gba" size … crc 1F1C08FB … serial "BPEE" ) )`. We keep, per `rom`: its `crc` (as 8 lowercase hex digits, like `GameId`), its `serial` (may be missing) and the game's `name`.
- **Picture names:** libretro's rule — in the game's name, each of `& * / : `` ` `` < > ? \ |` becomes `_`; then `.png`. (E.g. `game A - game A Version (USA, Europe).png`.)
- **Cheat files for a game:** the listed `.cht` files named `<name>.cht` or `<name> (<label>).cht` with exactly one more bracket part (the game's name as is, or with the picture-name rule), e.g. `game A - game A Version (USA, Europe) (Code Breaker).cht`. So `game A - game A Version (USA, Europe) (Rev 1) (Code Breaker).cht` belongs to `… (USA, Europe) (Rev 1)`, not to `… (USA, Europe)`. Shown by the label ("Code Breaker"), or "Cheats" for `<name>.cht`. Some of libretro's files are named loosely (e.g. `game A - game A Version (Code Breaker)(USA).cht`, `(CodeBreaker)`); those don't match and aren't offered (seen 2026-10-04: most files follow `<name> (Code Breaker).cht`).

## 3. Recognising a game

- **The fingerprint** is the game's `GameId` (CRC32 of the ROM). Today it is worked out only for played games; now the background helper reads every game without one (once; through `RomFile`, so `.zip` too) and saves it in the game list (`Library`) as today.
- **The game code:** the ROM header's 4 bytes at `0xAC`; used only if they are 4 capital letters or digits.
- **Lookup:** the fingerprint first; else the code. Several games with one code (game A: USA, Rev 1, Rev 2…): the **plainest name** wins — the shortest, then A–Z (`game A - game A Version (USA, Europe)` over its `(Rev 1)` and `(Rev 2)`).
- Not found → the game is "not found" (§4) and its card keeps today's picture.

## 4. What the app remembers (`files/boxart/boxart.json`)

Per game (by fingerprint): **state** — `found` (the box is saved), `none` (looked up, not found), `own` (a picture the user chose), `removed` (the user removed the box) — the **name** found (empty for `none`) and **when** it was looked up.

- A game is looked up when it has no entry, or it is `none` and the game list was downloaded after its lookup (so new boxes and new list entries turn up).
- `own` and `removed` are never changed by downloads. Choose box art → `own`; Remove box art → `removed` (its picture deleted). A `found` game whose picture file is missing is looked up again.
- **Pictures:** decoded no bigger than needed and shrunk to fit 400 × 400 (keeping their shape), saved as JPEG (quality 90; ≈ 30–60 KB).

## 5. What the user sees

- **The game grid** (`LibraryScreen`): every small card's picture area becomes **square**. A box fills it as far as it can without cropping (the card's colour round it); without a box, today's picture (where it was left, 3:2) or the placeholder sits centred in it. The big Continue card is unchanged.
- **Downloads:** while the game list is on screen and **Download box art** is on, boxes appear one by one as they arrive (the grid's pictures refresh). No message when it works; **none** when it fails (no internet, server down, a broken picture): the run stops and is tried again the next time the game list shows.
- **Settings** (main page, last row): a switch **Download box art**, help "Pictures of game boxes from libretro, for the game list. Uses the internet." Off: nothing is downloaded; saved and chosen boxes still show.
- **The game's hold menu** (`GameHoldMenu`): **Choose box art** (always; opens the phone's picture picker, `image/*`) and **Remove box art** (only while the card shows a box). A picture that can't be read: the toast "That picture couldn't be read."
- **The cheat panel** (10a's): a third outlined button **Find cheats online** (icon `CloudDownload`) under Import cheat file. It opens a pop-up "Cheats online":
  - while looking: a spinner and "Looking for cheat files";
  - found: one row per file ("Code Breaker", "GameShark", …); a tap downloads it and imports it exactly as 10a's Import cheat file (all switched off, the same notes); the pop-up closes;
  - the game isn't recognised or has no files: "No cheat files found for this game.";
  - no internet / a server error: "Couldn't reach the internet."
  - It works with Download box art off too (it is the user's own tap).

## 6. How it runs

- **`BoxArtHelper`** (one background thread): started when the game list shows (and Download box art is on); stopped when a game starts or the app goes to the background — it checks between steps and stops before the next game, so it never runs while a game plays.
  1. The game list: the kept one if younger than 30 days; else download, read, keep (a failed download with an older kept list: use the old one; with none: stop).
  2. For each game in the list (the grid's order) that needs a lookup (§4): its fingerprint (read the ROM if not known; saved in the game list) and code → the name → none: `none`; a name: download the box → **missing on the server** (HTTP 404): `none`; **any other failure**: stop the run (no entry written) → decode, shrink, save → `found` → the grid refreshes. A downloaded picture Android can't read is skipped (no entry; tried again next time) and the run goes on.
- **`Net`**: one GET (HTTPS only, redirects followed, 15 s to connect and to read, a `User-Agent` naming the app), giving the bytes, **missing** (404) or **failed** (anything else, or bigger than the limit).
- **Find cheats online**: the game's name — the remembered one (§4) or looked up as in §6.2 (the playing game's ROM header gives its code) — then the cheat file list (kept or downloaded), then the pop-up.
- **The manifest** gets `android.permission.INTERNET` (granted without asking). No new libraries (Gradle is offline): `HttpURLConnection`, `BitmapFactory`, `Json`.

## 7. The pieces

Pure Kotlin, tested on the PC:
- **`GameNames.kt`** — `GameNames.fromDat(text)`, `toTsv()` / `fromTsv(text)`, `find(id, code): String?` (§3), `GameNames.code(rom: ByteArray): String` (the header code or ""), `GameNames.pictureName(name)` (§2's rule).
- **`BoxArt.kt`** — `BoxArt(dir: File)`: the memory of §4 (`entry(id)`, `needsLookup(id, listTime)`, `found(id, name, jpeg)`, `none(id)`, `own(id, jpeg)`, `remove(id)`, `picture(id): File?`), written safely like `Cheats`.
- **`CheatSources.kt`** — `CheatSources.fromListing(json): List<String>` (the `.cht` names in the GitHub reply), `forGame(files, name): List<Pair<String, String>>` (label, file name; §2), sorted by label.

Android:
- **`Net.kt`** (§6), **`BoxArtHelper.kt`** (§6; the downloads, the shrinking, callbacks for a found fingerprint and a new picture).
- **`Screens.kt`**: `GameCard`'s square picture area and the box; `GameHoldMenu(…, hasBox, onChooseBox, onRemoveBox)`; `CheatPanel(…, onFindOnline)`; `OnlineCheatsDialog`.
- **`SettingsScreens.kt` / `Settings.kt`**: `Settings.boxArt` (`box_art`, on by default) and its switch.
- **`MainActivity.kt`**: starting / stopping the helper, the grid's picture (`own`/`found` box first for small cards), the picture picker, the online cheats.

## 8. Testing and Claude's check

- **PC (JVM):** `GameNamesTest` — a real excerpt of the game list (game A by its fingerprint `1f1c08fb`; a hack's fingerprint with code `BPEE` → game A; game A's three entries with one code → `game A - game A Version (USA)`; a missing serial; a header code that isn't 4 capital letters or digits → found only by fingerprint), the TSV round trip, `code` from a header, `pictureName` with every replaced character; `BoxArtTest` — each state, `needsLookup` (no entry, `none` before / after the list's time, `own` / `removed` never), `found` with its file missing, a damaged memory file; `CheatSourcesTest` — a real listing excerpt, `forGame` (game A's files and labels; `… (USA, Europe)` doesn't take `… (USA, Europe) (Rev 1) (Code Breaker).cht`; a name with `&` or `:` matched through the picture-name rule; `<name>.cht` → "Cheats").
- **On-device:** `ScreensTest` — square cards with a box, the hold menu's two items (Remove only with a box), the Settings switch, the cheat panel's Find cheats online and the pop-up's states; `NetTest` — a tiny local server in the test (bytes, 404, too big, an unreachable port); a picture shrunk to fit 400 × 400.
- **CTest 120/120 unchanged.**
- **Claude's check on the phone emulator** (real internet): the game list downloaded; the user's `hack 1` gets game A's box (by its code), `m7_demo` / `obj_aff` keep today's pictures (`none`); the Continue card unchanged; Choose box art (a picture pushed to the emulator) and Remove box art; the switch off → nothing downloaded for a new game; airplane mode → no message, nothing written, a box comes when it's off again; Find cheats online in hack 1 → game A's files → one imported ("Added N cheats.", checked against the file), then removed again; the app's data (cheats, settings) backed up first and put back after.

## 9. Files

```
app/src/main/AndroidManifest.xml                         INTERNET
app/src/main/java/com/tengen/gbaemu/GameNames.kt (new)   the game list, lookup, codes, picture names
app/src/main/java/com/tengen/gbaemu/BoxArt.kt (new)      what's remembered per game, the pictures
app/src/main/java/com/tengen/gbaemu/CheatSources.kt (new) libretro's cheat files for a game
app/src/main/java/com/tengen/gbaemu/Net.kt (new)         one HTTPS download
app/src/main/java/com/tengen/gbaemu/BoxArtHelper.kt (new) the background helper
app/src/main/java/com/tengen/gbaemu/Settings.kt, SettingsScreens.kt   Download box art
app/src/main/java/com/tengen/gbaemu/Screens.kt           square cards, hold menu items, Find cheats online, its pop-up
app/src/main/java/com/tengen/gbaemu/MainActivity.kt      the wiring
app/src/test/.../GameNamesTest.kt, BoxArtTest.kt, CheatSourcesTest.kt (new)
app/src/androidTest/.../NetTest.kt (new), ScreensTest.kt (+)
```
