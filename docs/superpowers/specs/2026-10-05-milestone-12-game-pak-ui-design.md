# Milestone 12 — Game Pak: the UI overhaul — Design

**Date:** 2026-10-05
**Status:** Design (shaped with the user 2026-10-05 through the impeccable skill; direction picked on the decision page: **Game Pak Labels**); spec approved by the user 2026-10-05 ("Go ahead with the prototype"); prototype built on branch `proto-12` (commit `715b626`) and checked by Claude on the emulator 2026-10-05; the user tried it 2026-10-05 ("It's beautiful, great work"); plan `docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md` (8 tasks), executed on `milestone-12`; **implemented** — the impeccable finish review returned `ship` after two fix rounds (§12), `DESIGN.md` written; the user's check on the emulator passed 2026-10-05 ("It all works"); milestone 12 complete (tag `milestone-12`).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Replaces the look of:** milestone 8a (`2026-10-03-milestone-8a-app-look-design.md`, "Cartridge grey" charcoal + amber). 8a's wording rules and its reusable-piece approach stay; its colours, type and card shapes are replaced.
**Product record:** `PRODUCT.md` (new, 2026-10-05). **Direction record:** the surface brief written by `impeccable surface-brief` (`.impeccable/`), seed `3bf0b5f2`. **Design system record:** `DESIGN.md`, written at the end from the finished build.

---

## 1. Goal and scope

**User request (2026-10-05):** "an overhaul of the UI", using the impeccable design skill. The user's answers:
- Audience: "me now, public later".
- What's wrong today: the app **looks generic** and has **no personality**.
- Depth: **replace the whole look** (new colours, type, shapes, layouts; every feature stays).
- Scope: **game list + first start, in-game screens, Settings pages, app icon + launch screen** (all four).
- Direction: **Game Pak Labels** (picked over the dealt "Instruction Booklet", the category standard and five declined ideas).
- Light/dark: **follow the phone** — light grey plastic by day, smoked black plastic at night; both designed.
- Accent colour: **follows the console skin** (like the see-through coloured game A carts); a fixed default with no skin.
- Fonts: **the phone's own** (Roboto, its Black Italic and Roboto Condensed); nothing downloaded.
- Process: **prototype first, the user tries it**, then the plan (as in 8a).

**The idea:** the app is your Game Pak collection. Every game is a moulded cartridge whose label is its own picture; the game you were playing sits **seated in the slot** at the top. Tapping Continue pushes that cartridge into the slot and the game starts.

**Not in 12:** the emulator core (C/asm) — untouched; the touch buttons drawn over the game (8b skins and 9 Delta skins keep their look); new settings or features; a downloaded font; the app's name (stays "GBA Emulator" — the public name is an open decision in `PRODUCT.md`); anything that prints Nintendo's logos or seal.

**Done when:** the user tries the prototype and the final build and finds the app distinctive and easy; Claude's check on the emulator passes (light and dark, upright and sideways, font size 1.3×, every screen in §4-§7); the impeccable finish review returns `ship`; `DESIGN.md` is written; every test passes — the new ones (§9), Kotlin unit tests, on-device tests, CTest 120 unchanged.

## 2. The world

Operate mode (an app people use to get something done): the world lends **type, palette, density and one signature move**; navigation and controls stay standard Material 3 components, themed.

**Material:** moulded cartridge plastic. Screens are the plastic; cards are cartridges standing on it — a raised body with a top lip, a 1 dp moulded highlight on the top edge and a 1 dp shade on the bottom edge, grip ridges along the bottom. A cartridge's **label** is the game's art (box art, else the picture of where it was left) under a black **label band** carrying the name in white heavy italic. The **slot** is a dark recessed bay (dark in both themes, like the console's real slot).

**Never:** Nintendo's logos, the "GAME BOY ADVANCE" logotype or the seal; drop shadows other than Material's tonal elevation; gradients or glass; pixel fonts.

## 3. Colours, type, shapes, motion (`AppTheme.kt`, `GamePak.kt` new)

### 3.1 Colours

Colour strategy: **committed** — the plastic owns every screen, the label bands are black, and one see-through-plastic accent marks **only what you can press** (and switches that are on). Game art carries the colour on the game list.

| Role | Light (grey plastic) | Dark (smoked plastic) | Used for |
|---|---|---|---|
| `background` / `surface` | `#C5C7CC` | `#141518` | the plastic every screen is made of |
| cartridge (`surfaceContainer`) | `#D7D9DD` | `#222327` | carts, tiles, grouped settings, sheets, dialogs |
| cart highlight / shade | `#EEEFF1` / `#A3A5AB` | `#34353B` / `#070708` | the moulded edges (1 dp highlight, 1.5 dp shade) |
| label stock | `#F2F2F4` | `#F2F2F4` (print: the same) | labels without art, filled save stickers — with the light theme's inks |
| slot bay | `#26272B` | `#0A0A0C` | the recessed slot (Continue, first start, empty folder) |
| label band | `#141518` + white text | `#000000` + white text, 1 dp `#34353A` edge | names on cartridges, screen titles on game screens |
| `onSurface` (ink) | `#16171B` | `#ECECEF` | text |
| `onSurfaceVariant` | `#44464D` | `#A6A8AF` | times, help notes |
| `outline` | `#8E9097` | `#4A4C52` | dividers, empty-slot dashes |
| accent `primary` / `onPrimary` | default `#4B3FB8` / `#FFFFFF` | default `#928BD7` / the same hue, very dark (computed, §3.2) | buttons, chosen choice, switches on, focus — nothing else |
| `error` | `#9E0E27` | `#FF8C98` | Quit, Remove, Delete, error text (light: `#A8102A` was 4.48:1 on the plastic — darkened while writing the plan) |

Contrast (WCAG, computed): light — ink on plastic 10.6:1, on cart 12.7:1; variant on plastic 5.6:1, on cart 6.7:1; white on accent 7.8:1; accent on cart 5.5:1; error on cart 5.4:1. Dark — ink on plastic 15.5:1, on cart 13.3:1; variant 7.7:1 / 6.6:1; on-accent on accent 7.2:1; accent on cart 6.8:1; error on cart 7.1:1. All ≥ 4.5:1.

### 3.2 The accent follows the skin (`AccentColors.kt`, new, pure Kotlin)

Each built-in skin names a seed colour — its see-through plastic: Ice `#A9D3EA`, Pink `#E892C0`, Lime `#8FD07A`, Orange `#F08A2C`, Yellow `#F3CC2E`, Gold `#D2AE55`, Red and cream `#A61F2A`, Grey and red `#C8202A`, Classic grey `#9A2257` (its A/B colour). Plain, White, Black, Silver and imported Delta skins use the default indigo. From a seed, the accent for each theme keeps the seed's hue and saturation and moves its lightness until it reads both as a button and as **text** on every ground: light — white text on it ≥ 4.5:1 and it is ≥ 4.5:1 on the plastic (the darkest light ground); dark — it is ≥ 4.5:1 on the lightest card, with dark text (`onPrimary`, the same hue very dark) on it ≥ 4.5:1. (The prototype asked only 3:1 on the plastic in the light theme; an outlined button's text with the Ice skin was 3.4:1 — tightened while writing the plan.) Picking a skin on the Skin page recolours the app at once.

### 3.3 Type (the phone's own fonts)

| Use | Face | Material role |
|---|---|---|
| Screen titles ("Your games", "Settings", "Paused" game name) | Roboto Black Italic | `headlineSmall` / `titleLarge` |
| Names on label bands | Roboto Black Italic | `titleSmall` (small carts), `titleMedium` (seated cart) |
| Slot numbers on save stickers | Roboto Black Italic, 84 sp | `displayLarge` |
| Small labels ("Fast-forward speed", "Empty", the key table's heads) | Roboto Condensed Bold, sentence case, 0.2 sp spacing (the review: wide tracking read as an eyebrow costume) | `labelMedium` |
| Everything else | Roboto | the default scale |

Times and counts use even-width (tabular) figures and sit right-aligned. All sizes in sp.

### 3.4 Shapes and spacing

Cartridge: 14 dp corners and a top lip (6-10 dp tall by size, a little narrower than the body, like the real cart) — a custom `Shape`. Label: 4 dp corners (a printed sticker). Buttons: pill (fully round), like the console's Start/Select. Dialogs and sheets: 20 dp top corners. Spacing 4/8/12/16/24 dp; touch targets ≥ 48 dp.

### 3.5 Motion

- **The insert (signature):** tapping the seated cart's Continue (or the cart) slides it **up into the slot's mouth** — 260 ms, an accelerating curve, the cart clipped at the mouth's middle line as it goes in and its label darkening — then the game opens. A cart in the grid lifts 14 dp, shrinks 4 % and darkens the same way. Other carts stay still. (Prototype: "up", not "down" as first written — the cartridge's lip end is the end that goes into a console.)
- Pause menu: fade + scale 0.96 → 1, 150 ms. Sheets and dialogs: Material defaults.
- With Android's "Remove animations" on, every animation is an instant cut.

## 4. The game list and the first start (`Screens.kt`)

- **Header:** "Your games" in heavy italic, the folder's name under it; Settings (gear) and More (⋮: Open one game, Change folder) icon buttons.
- **The slot:** a full-width slot bay. In it, the **seated cartridge**: the label shows where the game was left (3:2, sharp pixels; box art is not used here — the picture of your moment is the point), the label band with the name; under the label, the cart's grip strip: a pill **Continue** button in the accent (play icon) and when it was last played, right-aligned. ⋮ on the band opens the game's sheet. Sideways or on a tablet (over 560 dp wide) the seated cart turns landscape (up to 680 dp wide): the label on the left, sized from the screen height ((height − 270 dp) × 1.5, 180-300 dp) so the whole slot fits under the header, its band on up to 2 lines; when it was played and Continue at the bottom of the right column (prototype: upright proportions filled the screen; review: sized to fit, name never cut).
- **The collection:** every other game as a cartridge in an adaptive grid (min 152 dp: two upright, more sideways), ordered as today. Label = box art if any (Fit, on printed label stock), else the picture of where it was left filling the square label (cropped), else blank label stock with a faint controller mark; band with the name (2 lines); grip strip with the day it was last played ("Today", "Yesterday", a date — one line, so large text doesn't wrap; the seated cart keeps the time) and ⋮. Tap plays (the insert), hold or ⋮ opens the sheet.
- **Game sheet:** its header is the game's label band (the name in label lettering); then the same lines as today (Start fresh, Import save, Export save, Game settings, Choose box art, Remove box art); Remove from list apart in red.
- **Nothing played yet:** the slot bay shows an empty slot outline and "Pick a game below to start"; the grid follows.
- **Empty folder:** the empty slot bay, "No games in GBA yet", "Put .gba or .zip files there, or pick another folder", accent "Change folder". **Scanning:** spinner + "Looking for games".
- **First start:** the app name in heavy italic, the empty slot bay with a cartridge outline, today's explanation, accent "Choose games folder", outlined "Open one game", Settings gear in the corner. A lost folder keeps today's red message.

## 5. In-game screens (`Screens.kt`)

- **Pause menu** (over the dimmed, paused game — the picture stays the main thing): a cartridge-plastic panel (with the lip) whose top is the game's **label band** (the name in heavy italic; no "Paused" line above it — the frozen game and Resume already say so); a full-width accent pill **Resume**; Save state, Load state, Cheats and Settings as flat icon rows with hairlines (the review: raised tiles inside the panel were nested cards and made the menu dense); **Fast-forward speed** as the choice buttons (2× · 4× · 8× · Max); Restart (tonal) and Quit (red) apart at the bottom. Sideways: two columns as today. Restart still asks first; Quit doesn't.
- **Save / load slots:** a header band ("Save state" / "Load state", back arrow); each filled slot a **save sticker** printed on label stock: the picture, when it was saved, and the slot number in 84 sp heavy italic (spoken "Slot N" / "Quick"); an empty slot has no sticker yet — a small "Empty" and its big number on the panel's plastic, so the page stays dark at night. Saving shows every slot; loading shows only used ones and the Quick slot first. Replace and Delete keep their questions.
- **Cheats:** header ("Cheats", the game's name); one flat row per cheat with its switch, hairlines between; accent "Add cheat"; "Import cheat file", "Find cheats online", "Remove all" as today; dialogs themed.
- **Over the game:** the speed label and the light slider's sun button as small plastic pills; nothing else changes over the picture.

## 6. Settings and the other screens (`SettingsScreens.kt`, `LayoutEditor.kt`)

- **Main page:** header "Settings"; the sections grouped on cartridge-plastic cards (no tinted icon squares): Picture, Sound, Fast-forward and rewind, Keyboard and controller, Touch buttons, BIOS, Backup (an archive icon — backups are local; not a cloud) — each an icon, its title, today's summary and a chevron; the box-art switch in its own group. A row that is off fades its icon, title and chevron, but keeps its summary (often the reason) readable.
- **Section pages:** the same `SwitchRow` / `ChoiceRow` content on grouped plastic, each help under its setting.
- **Skin page:** the skin tiles show each skin's colours; the chosen one is marked by the accent ring and the app's accent changes with it at once; imported Delta skins and Import as today.
- **Key table, key capture, game settings, BIOS, Backup page, restore and undo questions, busy spinner, layout editor:** the new colours, shapes and type; the layout editor's selection outline in the accent. No change to what they do.

## 7. App icon and launch screen (`res/`)

- **Icon:** an adaptive icon in vectors — a grey cartridge (with its top lip and grip ridges) carrying a black label band and an indigo label with a simple white play mark, on an indigo background; a monochrome layer for Android 13's themed icons. Original artwork; no Nintendo marks.
- **Launch screen:** the app's own theme (`res/values/themes.xml`, `values-night/`, `values-v31/`): the plastic colour as the window background (light/dark), and on Android 12+ the splash shows the icon on it. The manifest uses the new theme and icon.

## 8. Wording

8a's rules stay (no "…", no "&", sentence case, buttons start with a verb, no "please", no "!", real icons instead of symbols) and `UiTextTest` keeps enforcing them. New text: "Pick a game below to start". Tags shown in Roboto Condensed capitals are written in sentence case in the code and capitalised by the style.

## 9. Testing

- **New JVM tests:** `AccentColorsTest` — every skin's accent passes §3.2's contrast rules in both themes, and Plain / White / Black / Silver / imported get the default; `PaletteTest` — every pairing in §3.1 is ≥ 4.5:1 (the palette lives in a pure-Kotlin object so it runs on the PC).
- **On-device:** `ScreensTest` still finds the Continue card, the grid's ⋮ and the pause menu's parts (test tags kept); new: the theme follows dark mode (`uiMode`), and choosing a skin changes the accent.
- **Unchanged:** Kotlin unit tests (176 + new), on-device tests (74 + new), CTest 120/120 (the core is not touched).
- **Claude's check:** screenshots on the Pixel 9 emulator of every screen in §4-§7, light and dark, upright and sideways, at font size 1.3× (then back to 1.0); then the impeccable finish review (a fresh reviewer agent) against the direction record; then `DESIGN.md`.

## 10. Order

1. This spec → the user approves it.
2. **Prototype** on branch `proto-12` (all screens, the theme, the accent, the icon) — Claude checks it on the emulator, then the user tries it.
3. The plan (`docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md`) written from the prototype, executed inline on branch `milestone-12`, test-first, one commit per task.
4. Claude's check, the finish review, `DESIGN.md`, the user's check, merge.

## 11. Files

```
PRODUCT.md (new)                                          product record (impeccable)
DESIGN.md + .impeccable/design.json (new, at the end)     the design system, from the build
app/src/main/java/com/tengen/gbaemu/AppTheme.kt           light + dark schemes, type, shapes, the reusable pieces rebuilt
app/src/main/java/com/tengen/gbaemu/GamePak.kt (new)      cartridge shape and drawing, label band, slot bay, save sticker, the insert
app/src/main/java/com/tengen/gbaemu/AccentColors.kt (new) skin → accent, contrast rules (pure Kotlin)
app/src/main/java/com/tengen/gbaemu/Screens.kt            first start, game list, sheet, pause menu, slots, cheats, overlays
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt    settings pages, skin page, key table, dialogs
app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt       accent outline
app/src/main/java/com/tengen/gbaemu/MainActivity.kt       theme follows dark mode and the skin's accent
app/src/main/AndroidManifest.xml                          icon, theme
app/src/main/res/{drawable,mipmap-anydpi-v26,values,values-night,values-v31}/  icon vectors, launch theme
app/src/test/.../AccentColorsTest.kt, PaletteTest.kt (new); app/src/androidTest/.../ScreensTest.kt (+2)
```

## 12. Review results (Task 7)

The impeccable finish review (a fresh reviewer agent, screenshots of every reachable screen light/dark, upright/sideways, font 1.3×; the first start and empty screens captured by a temporary compose test) returned **`fix`** with 8 items, all applied: (1) the sideways seated cart sized to fit, Continue at the bottom of its column; (2) labels without box art on printed label stock, screenshots cropped to fill; (3) save stickers with dominant numbers, no dashed boxes; (4) pause menu and cheats as flat rows (no cards inside the panel); (5) the Backup icon an archive, not a cloud; (6) small labels' tracking 1.2 → 0.2 sp; (7) the grid's one-line day; (8) an off row's reason stays readable. Its second pass scored them 7 resolved, 1 partial (the sideways pause wasn't recaptured) and found 2 new issues — the sideways name cut short (now 2 lines) and a bright wall of stickers at night (now only filled slots are printed). Third pass: all resolved, **`ship`** (scoped to these fixes). Open ceiling notes, not fixed: the accent is a flat fill rather than see-through plastic; the cartridge vocabulary is thin in Settings, the sheet and dialogs. `DESIGN.md` + `.impeccable/design.json` were then written by the documenter agent from the build.
