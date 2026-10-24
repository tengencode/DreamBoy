# Milestone 8a — The App's New Look — Design

**Date:** 2026-10-03
**Status:** Implemented — milestone 8a complete 2026-10-03 (tag `milestone-8a`; the user's check on the emulator: 'Everything works'). Design (brainstormed with the user 2026-10-03; each part approved: 'Yes, looks right'); the written spec approved by the user 2026-10-03 ('Approved, build the prototype'); prototyped (branch `proto-8a`, checked by Claude on the emulator upright and sideways); tried by the user on the emulator ('Everything works', 2026-10-03); plan: `docs/superpowers/plans/2026-10-03-milestone-8a-app-look.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestones 4c-6c (the screens: `Screens.kt`, `SettingsScreens.kt`, `LayoutEditor.kt`, `MainActivity.kt`), all merged into `main`.

---

## 1. Goal and scope

**Milestone 8 (user request 2026-10-03): polish the app and add skins.** Split by the user (option 1 of 3) into **8a the new look** (this spec) and **8b console skins** — touch buttons in the colours of the special-edition handhelds, built on 8a's look. 8b gets its own spec after 8a.

**8a's goal:** the app's screens easier to understand and nicer to look at, using the ui-ux-pro-max design guide (asked for by the user): its own colours instead of Android's default purple, clearer menus, real icons, and no "…" in any label.

**Decisions (user, 2026-10-03):**
- **Colours:** A "Cartridge grey" (not B Glacier navy/teal, not C Famicom black/red).
- **Game list:** B a grid with a Continue card (not A a list).
- **Font:** Android's own font only (not a pixel font for titles — no download).
- **Approach:** 1 a shared look — one theme and a few reusable pieces, every screen rebuilt on them (not 2 recolour only, not 3 a new navigation library).

**Not in 8a:** the touch buttons' look over the game (8b: skins), a light theme, a search box, a pixel font, new settings or features, any change to the emulator core (C).

**Done when:** the user opens the app (emulator or phone) and finds the game list, pause menu and Settings easier and nicer, sees no "…" anywhere, and everything works as before; every test passes — the new ones (§6), Kotlin unit tests, on-device tests, CTest 120 unchanged.

## 2. The look (`AppTheme.kt`, new)

One Material 3 dark colour scheme set at the app's root (`MaterialTheme(colorScheme = AppColors)` in `MainActivity`), always dark, whatever the phone's light/dark setting (as today). Components read the scheme's roles; no colour is written inside a screen.

| Role | Colour | Used for |
|---|---|---|
| `background` | `#1A1B1E` | screens |
| `surface` | `#1A1B1E` | screens, sheets |
| `surfaceContainer` | `#2A2C31` | cards, tiles, rows, choice-button tracks |
| `surfaceContainerHigh` | `#33302A` | the Continue card, selected rows (with a `#5A4A2A` 1 dp border) |
| `onSurface` | `#ECEAE4` | text |
| `onSurfaceVariant` | `#9C998F` | hints, times, help notes |
| `primary` | `#F2A33A` | the one main button per screen, chosen choice, switches, icons in tiles |
| `onPrimary` | `#2A1A02` | text on amber |
| `primaryContainer` | `#3A2F1C` | icon squares in Settings, the Continue tag |
| `error` / `errorContainer` | `#F2A0A0` / `#3A1F1F` | Quit, Remove, Delete; error messages |
| `outline` | `#2E3035` | dividers |

Contrast (WCAG, computed): text `#ECEAE4` on `#1A1B1E` 14.3:1 and on cards `#2A2C31` 11.6:1; hints `#9C998F` on `#1A1B1E` 6.0:1 and on cards 4.9:1; `#2A1A02` on amber 8.1:1; amber on `#1A1B1E` 8.3:1; `#F2A0A0` on `#3A1F1F` 7.4:1 — all ≥ 4.5:1.

**Shapes:** 12 dp corners on buttons, cards, tiles and rows; 16 dp on dialogs and the bottom sheet. **Spacing:** 4/8/12/16/24 dp. **Touch:** every tappable thing ≥ 48 dp, with Material's press ripple. **Icons:** Material icons from `androidx.compose.material:material-icons-extended` (1.7.8 — already in the PC's Gradle cache, so `--offline` works), replacing the text symbols ▶ ⋮ ← ☀ in menus. **One amber button per screen** (`Button`); the others `FilledTonalButton` / `OutlinedButton` / `TextButton`.

**Reusable pieces (`AppTheme.kt`):**
- `ScreenHeader(title, onBack?, actions)` — back arrow icon, title, icon buttons on the right.
- `MenuTile(icon, label, onClick)` — the pause menu's tiles.
- `Choices(options, selected, onPick)` — a row of choice buttons (the Material 3 segmented button), for 2-4 short options.
- `SwitchRow(label, help?, checked, onChange)` — a setting with a switch, its help note under it.
- `ChoiceRow(label, help?, options, selected, onPick)` — a label, `Choices` under it, its help note under that.
- `NavRow(icon, title, summary, onClick)` — a Settings section: icon in an amber-tinted square, title, current values, chevron.
- `ActionSheet(title, items, danger?)` — the bottom sheet (`ModalBottomSheet`): icon + label lines, a danger line apart in red.

## 3. The game list and the first start (`Screens.kt`)

- **Header** (`ScreenHeader`): "Your games"; a gear icon button (Settings) and ⋮ (Open one game, Change folder).
- **Continue card:** the most recently played game (largest `lastPlayed` > 0; `Library.continueAndRest`), full width — on a wide screen (over 560 dp: sideways, tablets) the picture sits on the left at 300 dp with the text beside it, so the card doesn't fill the screen — its picture large, name, time and an amber "Continue" tag. Tapping plays it. No card when nothing has been played yet.
- **Grid:** every other game in a `LazyVerticalGrid` with adaptive columns (min 150 dp: two upright, more sideways), each card with the picture (3:2, sharp pixels), name (2 lines) and last played, ordered as today's list. Tap plays; holding **or a small ⋮ icon button on the card** opens the game's sheet (the ⋮ makes the menu findable without knowing about holding).
- **Game sheet** (`ActionSheet`, replacing `GameHoldMenu`'s dialog): Start fresh (from the title screen), Import save, Export save, Game settings; "Remove from list" apart in red, only for games from outside the folder.
- **Empty folder:** "No games in GBA yet" (the folder's name) and an amber "Change folder" button. **Scanning:** a progress spinner and "Looking for games".
- **First start:** the app's name, the short explanation (as today), an amber "Choose games folder", an outlined "Open one game", and the gear icon for Settings in the corner. A lost folder keeps today's red message.

## 4. The pause menu, save slots, cheats, overlays (`Screens.kt`)

- **Pause menu** (`GameMenu`): "Paused" (small, grey) and the game's name; a full-width amber **Resume** (play icon); a 2×2 grid of `MenuTile`s — Save state, Load state, Cheats, Settings; **Fast-forward speed** as `Choices` (2× · 4× · 8× · Max — picks the speed directly, replacing today's tap-to-cycle); then, apart at the bottom, Restart (tonal grey) and Quit (red). **Sideways** (a wide screen under 520 dp tall) the menu has two columns — title, Resume, speed, Restart / Quit on the left, the four tiles on the right — so nothing is below the fold (found in the prototype). Restart still asks first (`RestartDialog`); Quit doesn't (the automatic state keeps the moment, as today).
- **Save / load slots** (`SlotGrid`): `ScreenHeader` ("Save state" / "Load state", back arrow); cards with the picture, "Slot N" and the time; saving shows empty slots as dashed "Empty" cards; loading shows only used slots (as today). Replace and Delete keep their questions, Delete in red.
- **Cheats panel:** `ScreenHeader` ("Cheats", the game's name under it); one row per cheat with its name and a switch; an amber "Add cheat" button; the add/edit dialog keeps its fields, Delete in red.
- **Over the game:** the light slider's button becomes a sun icon (not the ☀ character); the speed label keeps its place with the new colours.

## 5. Settings and the other screens (`SettingsScreens.kt`, `LayoutEditor.kt`)

- **Main page:** `ScreenHeader("Settings")` and one `NavRow` per section, with icons — Picture (photo), Sound (volume), Fast-forward and rewind (fast-forward), Keyboard and controller (gamepad), Touch buttons (touch), BIOS (chip) — and today's summaries.
- **Section pages** (no more pick pop-up for each change): on/off settings → `SwitchRow` (Sound, Show speed, Rewind, Buzz); every other choice has 2-4 options → `ChoiceRow`, with short button labels where today's names are too long for a phone-width row: Pixels (Sharp · Smooth), Size (Fit · Whole steps · Stretch), Delay (Low · Medium · High), Fast-forward speed (2× · 4× · 8× · Max), Rewind length (1 min · 2½ min · 5 min), Sideways, over the picture (Normal · Faint · Strong). The main page's summaries keep the full names. Settings then needs no pick dialog (`ChoiceDialog` is removed if nothing else uses it). Each help note moves under the setting it explains. Changes still apply at once (`onChange`).
- **Touch buttons page:** "Edit upright layout" and "Edit sideways layout" as `NavRow`s.
- **BIOS page:** the current BIOS, an amber "Choose BIOS file" and, when a file is used, "Use built-in BIOS".
- **Key table, key-capture dialog, game settings dialog, the unusual-BIOS question:** the new colours and wording; the capture text "Press the key for A" (no "…").
- **Layout editor:** its blue selection outline → amber; its panel uses the theme's surface; item names without symbols ("Rewind", "Fast-forward"). The touch buttons drawn in it and in play keep today's look (8b).

## 6. Wording and testing

**Wording rules** (from the design guide), for every label, button, title and message the user sees:
- No "…" or "..." — a spinner shows that something is under way. ("Settings…" → "Settings", "Open a game…" → "Open one game", "Import save…" → "Import save", "Looking for games…" → spinner + "Looking for games".)
- No "&" ("Fast-forward & rewind" → "Fast-forward and rewind", "Keyboard & controller" → "Keyboard and controller").
- Sentence case; buttons start with a verb; no "please", no "!".
- Symbols that were icons become real icons (▶ ⋮ ← ☀); the sensor message says "the light slider" instead of "the ☀ slider". The D-pad arrows in key names (← D-pad ←) stay: they name keys.

**Tests:**
- **Wording test** (JVM, new `UiTextTest`): reads every `.kt` file under `app/src/main/java`, takes the string literals outside comments, and fails on any "…", "...", "&" or one of ▶ ⋮ ☀ ⏩ ⟲ — naming the file and line. Key names and the touch buttons' own labels (`KeyMap`, `TouchLayout`'s drawn glyphs, 8b's) are on a short, named allow list.
- **Logic tests** (JVM): which game is the Continue card (most recent `lastPlayed` > 0, none when nothing was played), and the grid's order without it.
- **Screen tests** (on-device, `ScreensTest`, Compose UI test 1.11.4 + Espresso 3.7.0 — Espresso 3.5, the default, fails on Android 14+ with `InputManager.getInstance`; the user allowed downloading Espresso 3.7.0, `kotlinx-coroutines-test-jvm` 1.9.0 and `jsr305` 2.0.2, 2026-10-03): the pause menu shows Resume and the four tiles and picking 8× calls back with 8×; a Settings switch changes its setting; the game list shows the Continue card first and opens the sheet from a card's ⋮.
- **Unchanged:** the old Kotlin unit tests (84 → 88 with the new ones) and on-device tests (53 → 56) still pass; CTest 120/120 (the core is not touched).
- **Visual check by Claude:** screenshots of every screen on the phone emulator (upright and sideways) before the user's trial.

**Order (as in 7a/7b):** a prototype of all screens first (branch `proto-8a`), checked by Claude on the emulator, then the user tries it on the emulator or their phone; then the plan is written from the prototype and executed on `milestone-8`; the milestone ends with the user's check.

## 7. Files

```
app/src/main/java/com/tengen/gbaemu/AppTheme.kt (new)    colours, shapes, the reusable pieces
app/src/main/java/com/tengen/gbaemu/Screens.kt           first start, game list, sheet, pause menu, slots, cheats, overlays
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt   settings pages, key table, dialogs
app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt      amber outline, theme surface, names
app/src/main/java/com/tengen/gbaemu/MainActivity.kt      the theme at the root; the sheet instead of the hold dialog
app/src/main/java/com/tengen/gbaemu/MotionInput.kt       "the light slider"
app/src/main/java/com/tengen/gbaemu/Library.kt           continueAndRest (the Continue card)
app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt  the editor's outline amber, the picture block grey
app/src/main/java/com/tengen/gbaemu/TouchLayout.kt       editor item names without symbols
app/build.gradle.kts                                     material-icons-extended 1.7.8; Compose UI test 1.11.4 + Espresso 3.7.0 (androidTest)
app/src/test/.../UiTextTest.kt (new), LibraryTest.kt (+2); app/src/androidTest/.../ScreensTest.kt (new)
```

Risk (happened): Gradle `--offline` couldn't resolve the screen tests' libraries from the cache; Claude asked, the user allowed the downloads (one online Gradle run each), and the build is offline again.
