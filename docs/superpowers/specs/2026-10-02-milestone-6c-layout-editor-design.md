# Milestone 6c — Touch Layout Editor — Design

**Date:** 2026-10-02
**Status:** Implemented — milestone 6c complete 2026-10-02 (tag `milestone-6c`); milestone 6 complete. Approved design (brainstormed with the user 2026-10-02); prototyped (branch `proto-6c`) and tried by the user ('Everything works', 2026-10-02); plan: `docs/superpowers/plans/2026-10-02-milestone-6c-layout-editor.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 app shell — skins + layout editor, §10 milestones)
**Builds on:** milestone 6b (tag `milestone-6b`: Settings → Touch buttons, the sideways look, the picture settings), 4c (`TouchLayout`, `TouchControlsView`, `GameScreen`), 6a (`Json.kt`).

---

## 1. Goal and scope

Milestone 6 is split in three (user decision 2026-09-28): 6a game library ✅ → 6b settings + controller remapping ✅ → **6c skins + layout editor**.

**6c finish line:** on the Pixel 9 emulator the user, in game A, moves and resizes touch buttons and the game picture — upright and sideways — saves, and plays with the new layout. Automatic tests prove that with nothing saved the layout is exactly today's, that moving and resizing stay within their limits, that a moved button is pressed at its new place, the see-through rule, and the layout file (saved, read back, damaged); nothing else changes (CTest 120/120, PC 404, the existing on-device tests).

**User decisions (2026-10-02):**
- **The layout editor only** (option A of 4): no button pictures, no skin `.zip` import, no other emulators' skin formats. The parent spec's "PNG assets" and "import skin .zip" are left out of v1 by this decision.
- **Everything can be moved** (option A): every button and the game picture, anywhere on the screen, upright and sideways separately; buttons over the picture are see-through (the 6b Faint / Normal / Strong look), buttons on black are drawn solid. The defaults are today's layouts.
- **Tap to pick, a size slider** (option A of 3, chosen in the visual companion): drag anything with one finger to move it; a tap picks it (dashed outline) and the bottom bar shows its name and a size slider. (Rejected: pinching — fiddly on small buttons and with a mouse on the laptop's phone emulator; a corner handle — small, hidden under the finger.)
- One layout for all games.

| In scope (6c) | Later / not in v1 |
|---|---|
| Move and resize each button (L, R, D-pad, A, B, Select, Start, ⏩, ⟲) and the game picture | Button pictures (PNG), skin `.zip` import, other emulators' skin formats |
| Upright and sideways layouts, Reset all, Cancel, Save | Per-game layouts; hiding buttons; extra buttons (e.g. A+B) |
| The editor from Settings → Touch buttons (library and in-game menu) | |

## 2. What the user sees

- **Settings → Touch buttons** gets two rows: **Edit upright layout…** and **Edit sideways layout…** (from the library and from the in-game menu). The editor turns the screen to its orientation (the phone may be held either way) and gives it back when it closes.
- **The editor:** a black screen with the game picture (the paused game's picture when opened in a game, else a plain block) and every button, drawn as in play. One-finger drag moves the item under the finger; a tap picks it (dashed outline). The bottom bar: the picked item's name and a size slider (½× – 2× of its default size, its shape kept), **Reset all**, **Cancel**, **Save**. Back = Cancel.
- Everything stays inside the screen; items may overlap (e.g. buttons on the picture).
- **In play:** the picture where the layout puts it; buttons overlapping the picture see-through (the 6b look), the others solid as upright today.

## 3. Files

```
app/src/main/java/com/tengen/gbaemu/
  TouchLayout.kt       the layout as data: the picture's box and each item's centre + scale; the defaults computed by
                       today's code; move (clamped), scale (½–2), hit-testing at the edited places, overlaps-picture
  LayoutStore.kt       files/layout.json: the upright and sideways layouts (with Json.kt); damaged → defaults, kept as .bad
  LayoutEditor.kt      the editor screen (Compose + the same drawing as the game), drag / tap / slider, Reset all / Cancel / Save
  TouchControlsView.kt draws from the layout; per button solid or see-through by overlap with the picture
  Screens.kt           GameScreen: a full black screen, the game surface placed at the layout's picture box, the buttons over all
  SettingsScreens.kt   Touch buttons: + Edit upright layout…, Edit sideways layout…
  MainActivity.kt      opens the editor (locks the orientation), saves, gives the layout to the game screen
app/src/test/.../TouchLayoutTest.kt (+ cases), LayoutStoreTest.kt
```

## 4. How it works

- **Coordinates:** a layout is stored per orientation as, for the picture and each item, the centre as a share of the screen's width and height (0..1) and a scale (0.5..2.0, 1 = today's size). Sizes come from today's dp sizes × scale, so shapes and finger sizes stay right on any screen; positions are shares, so a layout carries over between screens.
- **Defaults:** with nothing stored, the positions are today's: upright = the picture full width at the top (3:2) and the buttons where today's code puts them in the area below (converted to whole-screen coordinates); sideways = the picture fitted in the middle and the buttons as today.
- **The picture:** its box keeps the shape of its default box (found while prototyping: upright the default box is 3:2 at the top, sideways it is the whole screen — so that 6b's Stretch still fills the screen sideways); the 6b size mode (fit / whole-number / stretch) and sharp/smooth apply inside the box (the GL surface is the box); never bigger than the screen.
- **Hit-testing** uses the edited positions (the D-pad's directions by angle as today, the nearest touched button otherwise).
- **See-through rule:** a button whose box overlaps the picture's box is drawn with the see-through look; otherwise solid (upright today: solid grey on black).
- **The editor's bar** sits away from the picked item (nothing picked: at the top), so buttons near the bottom stay reachable.
- **Turning the screen** (found while prototyping): on closing, the editor first asks for the orientation the screen had when it opened, and frees it 1 s later — with auto-rotate off, just freeing it left the screen turned.
- **Storage:** `files/layout.json` = `{"upright": {...}, "sideways": {...}}`, each item `{"id": "...", "x": …, "y": …, "scale": …}` with numbers as whole ten-thousandths (Json.kt reads whole numbers); written safely (temporary file, then swapped).

## 5. Error handling

| Case | Behaviour |
|---|---|
| `layout.json` damaged | The defaults; the file kept as `layout.json.bad`; a toast |
| A stored item missing or a value out of range | That item's default (positions clamped into the screen, scale into ½–2) |
| Cancel or Back in the editor | Nothing changes |
| An item dragged past the edge | It stops at the edge |

## 6. Testing

1. **JVM tests** (test-first): `TouchLayoutTest` + the defaults equal today's positions (upright and sideways), moving clamps inside the screen, the scale stays within ½–2 and keeps the shape, a moved A is pressed at its new place and not at its old one, the D-pad works where it was moved, buttons overlapping the picture are see-through; `LayoutStoreTest` — round trip, missing file = defaults, damaged file → `.bad` and defaults, out-of-range values clamped.
2. **No change elsewhere:** PC 404 + CTest 120/120; the on-device tests unchanged (the touch tests still pass with the default layout).
3. **Claude's check** (adb, emulator): Settings → Touch buttons → Edit upright layout…; drag A to a new place, make it bigger, Save; in a demo a tap at the new place presses A (`debugKeysSeen`) and one at the old place doesn't; the picture made smaller and moved shows there; Cancel changes nothing; Reset all gives today's layout; the same sideways. The user's own layout kept aside and put back.
4. **User check:** in game A, move and resize buttons and the picture, upright and sideways, and play.

## 7. Done when

- All tests in §6 pass; CTest 120/120 on the PC.
- The user's check (§6.4) passes.
