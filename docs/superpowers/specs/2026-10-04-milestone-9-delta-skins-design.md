# Milestone 9 — Delta Skins (button pictures + skin import) — Design

**Date:** 2026-10-04
**Status:** Implemented — milestone 9 complete 2026-10-04 (tag `milestone-9`; Claude's check on the emulator and the user's try passed: 'Done and everything worked'). Design (brainstormed with the user 2026-10-04; each of the 3 design parts approved: 'looks right'); the written spec approved by the user 2026-10-04 ('approved, build the prototype'); prototyped (branch `proto-9`, `50d1957`) and checked by Claude on the emulator with both test skins — the changes it brought are marked "(prototype)" below; plan: `docs/superpowers/plans/2026-10-04-milestone-9-delta-skins.md` (9 tasks) (all done).
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell — skins, §10 milestones)
**Builds on:** milestone 8b (`Skin.kt`, the Skin page in `SettingsScreens.kt`, `NativeBridge.setBackground`), 6c (`TouchLayout.kt`, `TouchControlsView.kt`, `LayoutStore`, `LayoutEditor.kt`), 5a (`States.kt`), all merged into `main`.

---

## 1. Goal and scope

**9's goal:** touch buttons drawn from pictures, by importing the skins people already share for the **Delta** emulator (`.deltaskin` files). A Delta skin is one picture of the whole controller (buttons painted on) with invisible touch areas over the buttons; the game is drawn where the skin says.

**Decisions (user, 2026-10-04):**
- **Skin source:** Delta skins (not our own `.zip` format, not both).
- **Button pictures:** Delta skins are enough — the 13 built-in colour skins (8b) stay as they are; no per-button PNGs.
- **Delta's quick save / quick load:** a new **Quick** save-state slot (not slot 1, not ignored).
- **Rewind:** none on Delta skins (Delta has none); keyboard R / gamepad L2 still rewind; the built-in skins keep ⟲.
- **Pressed buttons:** a soft darkening of the pressed area (not nothing, as in Delta).
- **Layout editor:** off while a Delta skin is chosen ("This skin sets its own layout"); the saved layout comes back with a built-in skin.
- **Test skins:** Claude downloads free GBA skins, asking the user first. Allowed (2026-10-04): **Delta's default GBA skin** (`Standard.deltaskin`, github.com/deltaemulator/GBADeltaCore — PDF pictures, no game screen, a see-through sideways layout) and **R3BEL Neumorphius [P+L] Bumble** (github.com/deparsoul/RetroDelta — PNG pictures, its own game screen, menu / quick save / quick load / both fast-forwards, combo buttons); Onyx V2 was declined. Kept in `tests/skins/` (gitignored), never committed.
- **Approach:** 1 — a new view draws the Delta skin over the game; the game's surface is placed in the skin's screen box through the existing picture-box path (not 2 drawing the skin in OpenGL, not 3 converting Delta skins into our button layout).
- **Done when:** Claude's check passes **and** the user imports a skin on the phone emulator and plays with it.

**Not in 9:** our own skin format; per-button pictures; other emulators' skin formats (only Delta); iPad layouts; Delta's thumbstick, touch screen, X/Y/C/Z and other consoles' inputs; screen `inputFrame` cropping and CoreImage `filters`; per-game skins; moving a Delta skin's picture or buttons; any change to the emulator core or the native graphics code.

**Done when:** Claude's check on the phone emulator passes (§7) with the two test skins (plus a non-GBA copy made from one of them), the user imports a skin themselves and plays with it, and every test passes: the new ones, the Kotlin unit tests, the on-device tests, CTest 120 unchanged.

## 2. The Delta skin format (what we read)

Sources: Delta-Docs skins guide (github.com/noah978/Delta-Docs, `skins/README.md`) and the DeltaCore wiki "Skins" page. A `.deltaskin` is a zip holding `info.json` and the picture files.

```json
{
  "name": "My Skin", "identifier": "com.someone.gba.myskin",
  "gameTypeIdentifier": "com.rileytestut.delta.game.gba", "debug": false,
  "representations": { "iphone": { "edgeToEdge": { "portrait": {
      "assets": { "resizable": "portrait.pdf" },            // or "small" / "medium" / "large": PNG files
      "mappingSize": { "width": 414, "height": 896 },        // the picture's size in points
      "items": [
        { "inputs": { "up": "up", "down": "down", "left": "left", "right": "right" },
          "frame": { "x": 20, "y": 560, "width": 140, "height": 140 },
          "extendedEdges": { "top": 10, "bottom": 10, "left": 10, "right": 10 } },
        { "inputs": ["a"], "frame": { "x": 313, "y": 540, "width": 47.5, "height": 47.5 } },
        { "inputs": ["menu"], "frame": { ... } }
      ],
      "screens": [ { "outputFrame": { "x": 0, "y": 103, "width": 414, "height": 276 } } ],  // or "gameScreenFrame": {...}
      "extendedEdges": { "top": 7, "bottom": 7, "left": 7, "right": 7 },
      "translucent": false
  }, "landscape": { ... } }, "standard": { ... } } }
}
```

What we use:
- **Console:** `gameTypeIdentifier` must equal `com.rileytestut.delta.game.gba`, compared ignoring case (the wiki writes `Delta`).
- **Which layout:** for each direction, `representations.iphone.edgeToEdge` (the tall iPhones, closest to Android phones), else `representations.iphone.standard` (plan: per direction, so a skin with only a `standard` landscape still gets one). `ipad` is ignored. `portrait` is used upright, `landscape` sideways; either may be missing.
- **Picture:** `assets.resizable` if present, else the biggest of `large` → `medium` → `small`. PDF or PNG is decided by the file's first bytes (`%PDF`), not its name.
- **Items:** `frame` (x, y, width, height in points, decimals allowed), optional `extendedEdges` (each side the item's own value, else the layout's `extendedEdges`, else 0). `inputs` is a list of names or, for a D-pad, an object `{up, down, left, right}`:

| Delta input | Ours |
|---|---|
| `a`, `b`, `l`, `r`, `select`, `start` | the GBA button (KeyMap) |
| `up`, `down`, `left`, `right` (in a list, or as a D-pad object's values) | the D-pad direction |
| `fastForward`, `toggleFastForward` | ⏩ `KeyMap.FAST` (our tap-or-hold switch, `FastForwardSwitch`) |
| `menu` | open the pause menu |
| `quickSave` / `quickLoad` | save to / load from the Quick slot (§5) |
| anything else (`x`, `y`, `c`, `z`, `mode`, analog stick, touch screen, …) | ignored; an item left with nothing is dropped |

- **Game screen:** the first `screens[].outputFrame`, else `gameScreenFrame`, else none (§3). `inputFrame` and `filters` are ignored (the whole GBA picture is shown).
- **`translucent`:** the picture is drawn see-through (§4).
- Everything else (`debug`, `menuInsets`, unknown keys) is skipped. `info.json` may start with a UTF-8 byte-order mark.

## 3. Placing the skin on the phone (`DeltaLayout.kt`, new, pure Kotlin)

From one layout (portrait or landscape) and the screen size in pixels:
- **Scale** `s = min(W / mappingWidth, H / mappingHeight)` — the whole picture fits. **Upright:** centred across, at the bottom. **Sideways:** centred both ways. Any leftover strip is black.
- **The game's box:** the skin's screen frame scaled and moved like the picture. With no screen frame: upright, the area above the picture (full width; the game centred in it — prototype: Delta's default skin on the Pixel 9 leaves a 1080 × 1499 area); sideways, the whole screen. Inside the box the game is fitted by the existing size setting (Fit / Integer / Stretch) exactly as today (OpenGL, `fit_rect_mode`).
- **Touch areas:** each item's frame scaled, grown by its extended edges (scaled). A finger presses every input of the item whose area holds it and whose frame centre is nearest; several fingers add up (as today).
- **D-pad (an object `inputs`):** its frame split 3 × 3 (Delta's way): the middle presses nothing, the edges one direction, the corners two (diagonals). A finger in the extended edge counts as the nearest cell.
- **Actions** (`menu`, `quickSave`, `quickLoad`) fire once when a finger first lands on the item, never while it slides on or is held (`DeltaTouch`, in `DeltaLayout.kt`, pure: the fingers by pointer id → the held buttons and the actions to fire; plan).

## 4. Drawing and playing (`DeltaSkinView.kt`, new; `Screens.kt`; `MainActivity.kt`)

- **`DeltaSkinView`** (Android `View`, over the whole screen like `TouchControlsView`) draws the skin's picture at the place from §3, then each pressed item darkened with black at 25 %: an item whose frame is nearly square (width / height 0.8–1.25) as the oval inside its frame — round buttons (prototype: a box looked wrong round a round button) — others as a pill (corner radius half the short side), a D-pad its pressed cells (rounded). Fast-forward switched on is not shown on a Delta skin (as in Delta).
- **Leftover strips** (prototype): where the picture does not reach the screen's edge (R3BEL upright on the Pixel 9: 83 px at the top; sideways 41 px each side), an opaque skin fills the strip with the colour of the middle pixel of the picture's edge on that side, except a strip over the game's box (black, as the game's surroundings). A translucent skin leaves them black. Fingers → `onMask` (GBA buttons + FAST, as `TouchControlsView`) and `onAction` (menu / quick save / quick load). Buzz as today (a new press ticks). While `hidden` (keyboard or gamepad in use) nothing is drawn; the next touch shows it again and presses nothing (as today).
- **Translucent skins:** the picture's opacity from Faint / Normal / Strong: 40 % / 70 % / 100 % (70 % is Delta's default). Opaque skins ignore the setting.
- **Pictures, sharp at the phone's size** (`SkinPicture.kt`, its own file — plan), always made on a background thread (the view draws nothing of the skin until it is ready): a PDF's first page drawn by Android's `PdfRenderer` into a bitmap of exactly the on-screen size, transparent where the PDF is; a PNG decoded by `BitmapFactory` (scaled down by powers of 2 while decoding if it is larger than needed) and drawn scaled. **A PDF's drawing is kept** (prototype) beside the skin as `cache-<p|l>-<w>x<h>.png` and read from there next time: Delta's default upright PDF took 605–736 ms to draw on the emulator, its kept copy 13 ms to read. PNGs are not kept: decoding R3BEL's 1290 × 2796 PNG took 46–92 ms, writing a copy 797 ms.
- **The game screen (`GameScreen`):** when the chosen Delta skin has a layout for this direction, the game's surface goes in §3's game box, `DeltaSkinView` replaces `TouchControlsView`, and the body colour (Compose background and `NativeBridge.setBackground`) is black. Otherwise (a built-in skin, or the Delta skin lacks this direction) everything is as today; a Delta skin's missing direction uses **Plain** with the user's own layout.
- **A picture that cannot be drawn** (damaged PDF/PNG): that direction falls back to Plain, with a short note "This skin's picture could not be drawn".
- The emulator core and the native graphics code are not touched.

## 5. The Quick slot (`States.kt`, `Screens.kt`, `MainActivity.kt`)

- `States.QUICK` (file `quick.state` / `quick.png` beside `slot1..10` and `auto`).
- **Quick save:** the game is paused, saved to Quick with its picture (as a menu save), resumed; a short note "Quick save done". **Quick load:** loads Quick and plays on; with nothing saved, the note "No quick save yet". Loading checks as today (another BIOS kind is refused, 6b).
- **The Load page** shows Quick first, with its picture and time, when it is used. The Save page does not offer it (only the skin's button saves there).

## 6. Importing and picking (`SkinFiles.kt`, new; `Settings.kt`; `SettingsScreens.kt`)

- **Storage:** each imported skin in `files/skins/<n>/` (`n` = 1, 2, …, the next free number): its `info.json`, its picture files, `thumb.png` (the tile picture, made at import: the portrait picture, else the landscape one, drawn 240 px wide) and the kept PDF drawings (§4). Removing a skin deletes its folder. Names are shown as the skin writes them (R3BEL's starts with "-- ").
- **The setting:** `touch_skin_import` (an Int, as every setting: the folder number; absent or 0 = none). When it names an existing skin, that skin is used; otherwise the built-in `touch_skin` (8b) as before. Picking a built-in skin sets `touch_skin` and clears `touch_skin_import`; picking an imported one sets `touch_skin_import` and leaves `touch_skin` alone. An imported skin whose folder is gone or unreadable at start quietly gives the built-in one and clears the setting.
- **Import:** the Skin page's last tile, **Import** (an add icon), opens Android's file picker (`ACTION_OPEN_DOCUMENT`, any type — `.deltaskin` has no MIME type). The file is read as a zip into a spare folder (`files/skins/.incoming`), checked, then renamed into place; a failure leaves nothing behind. `info.json` may sit at the zip's top or inside one top folder. Importing a skin whose `identifier` is already imported replaces that copy (same number). After a good import the new skin is chosen.
- **Import errors** (a pop-up with one OK button; nothing changes):

| Problem | Message |
|---|---|
| not a zip, damaged, no `info.json`, `info.json` unreadable | "This file isn't a skin." |
| `gameTypeIdentifier` not GBA | "This skin is for another console. Only GBA skins work." |
| no iPhone portrait or landscape layout | "This skin has no phone layout." |
| a used layout's picture file missing | "This skin's picture is missing." |
| more than 50 MB unpacked, or more than 500 files | "This skin is too big." |
| an entry with `..`, an absolute path or a drive letter | "This file isn't a skin." |

- **The Skin page:** the 13 built-in tiles, then the imported skins (their `thumb.png` and name, in import order), then **Import**. The chosen one has the amber border (8b). **Press and hold** an imported tile → "Remove <name>?" (Remove / Cancel); removing the chosen one goes back to the built-in skin.
- **Touch buttons page:** the Skin row and the main page's summary show the imported skin's name. While an imported skin is chosen, each Edit layout row whose direction the skin covers is greyed out with "This skin sets its own layout".

## 7. Testing and Claude's check

- **`JsonTest`** (JVM, new): `decimal()` reads `47.5`, `-3`, `1e2`; `value()` reads any value as plain Kotlin (maps, lists, strings, Doubles, true/false/null — the reader then skips nothing by hand: `DeltaSkin` looks up the keys it uses in the map; plan); broken JSON throws.
- **`DeltaSkinTest`** (JVM, new): a full sample `info.json` read right; `edgeToEdge` preferred over `standard`; iPad-only → no phone layout; `screens` vs `gameScreenFrame` vs none; `resizable` vs the PNG sizes (biggest wins); D-pad object, list inputs, several inputs on one item, unknown inputs dropped, an empty item dropped; extended edges per side (item over layout); the console check ignoring case; a byte-order mark; broken JSON → an error.
- **`DeltaLayoutTest`** (JVM, new): the scale and the bottom / centred placement on 20:9 and 16:9 screens, upright and sideways; the game box from the screen frame and without one; touch areas with extended edges; nearest centre wins; the D-pad's 9 cells and its extended edge; several fingers; actions fire once per landing.
- **`SkinFilesTest`** (JVM, new; zips built in the test with `java.util.zip`): a good import (files, `thumb.png` skipped on the PC through an injected picture maker); `info.json` in a top folder; re-import replaces the same number; remove; each error in §6's table; nothing left behind after a failure.
- **`SettingsTest`** (+): `touch_skin_import` saved and loaded; picking a built-in clears it. **`StatesTest`** (+): the Quick slot's file names. **`UiTextTest`** (8a's wording test): the new labels and messages pass (no "…" or "&").
- **On-device** (+5): `SkinPictureTest` (new, 3): a PDF made in the test with `PdfDocument` drawn at the asked size, transparent where it is empty, and kept; a PNG decoded no smaller than needed and not kept; a 240-px-wide tile picture. `ScreensTest` (+2): the Skin page's imported tile (pick, press and hold → remove), the Import tile, the greyed Edit layout row; the Load page's Quick slot (and none on the Save page).
- **CTest 120/120 unchanged.**
- **Claude's check on the phone emulator** (the two test skins of §1, and a copy of one with its `gameTypeIdentifier` changed for the console error; the test game `m7_demo`; the user's settings and rotation backed up first and put back after): import each; upright and sideways — the picture placed and sharp, the game exactly in the skin's screen, every button and the D-pad's diagonals pressed by a tap (`adb shell input`), the darkening, menu, ⏩ tap and hold, quick save / load and "No quick save yet", a translucent skin at Faint and Strong, a skin missing one direction falls back to Plain; the import errors with a non-GBA skin and a plain zip; remove; the Edit layout rows greyed; back to a built-in skin, the saved layout back.
- **The user's try:** the user imports a skin on the phone emulator and plays with it.

## 8. Prototype (before the plan)

A throwaway branch `proto-9` (`50d1957`: `DeltaSkin.kt` with org.json, `DeltaSkinView.kt`, a skin unpacked by hand into `files/skins/proto`) checked by Claude on the emulator (Pixel 9, 1080 × 2424) with both test skins, upright and sideways. **Found:** both skins read as the docs say (Delta's default: `standard` + `edgeToEdge`, no screens, `translucent` sideways, partial `extendedEdges` such as only `right`; R3BEL: `edgeToEdge` only, `screens` with an `inputFrame` of the whole GBA picture, the same PNG for small / medium / large, combo items `["a","b"]`, `["l","r"]` overlapping the buttons — nearest centre picks right); the pictures land sharp and the touch areas line up with the painted buttons (measured: Delta's D-pad 20–465 × 1688–2133 px, drawn the same); R3BEL's game sits exactly in its painted screen; the D-pad's corner pressed two directions; Menu opened the pause menu; quick save / load fired; a sideways translucent skin over the game at Normal. **Changed:** pictures made off the UI thread and a PDF's drawing kept (§4); round buttons darkened as ovals (§4); leftover strips in the picture's edge colour, never over the game (§4 — a first try painted the black top strip over Delta's game box and hid the game).

## 9. Files

```
app/src/main/java/com/tengen/gbaemu/Json.kt              decimal(), value()
app/src/main/java/com/tengen/gbaemu/DeltaSkin.kt (new)    reading info.json: layouts, items, inputs, screen, assets
app/src/main/java/com/tengen/gbaemu/DeltaLayout.kt (new)  placing the picture, the game box, touch areas, the 3×3 D-pad
app/src/main/java/com/tengen/gbaemu/SkinFiles.kt (new)    import (checks, spare folder), list, remove, thumbnails
app/src/main/java/com/tengen/gbaemu/SkinPicture.kt (new)  PDF / PNG pictures at a size, kept PDF drawings, tile pictures
app/src/main/java/com/tengen/gbaemu/DeltaSkinView.kt (new) drawing, leftover strips, darkening, touches, actions
app/src/main/java/com/tengen/gbaemu/AppTheme.kt          NavRow(enabled)
app/src/main/java/com/tengen/gbaemu/Settings.kt           touch_skin_import
app/src/main/java/com/tengen/gbaemu/States.kt             QUICK
app/src/main/java/com/tengen/gbaemu/Screens.kt            GameScreen with a Delta skin; the Load page's Quick
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt    imported tiles, Import, remove, greyed Edit layout rows
app/src/main/java/com/tengen/gbaemu/MainActivity.kt       the file picker, the actions (menu, quick save / load), black body
app/src/test/.../JsonTest.kt (new), DeltaSkinTest.kt (new), DeltaLayoutTest.kt (new), SkinFilesTest.kt (new), SettingsTest.kt, StatesTest.kt
app/src/androidTest/.../SkinPictureTest.kt (new), ScreensTest.kt
```
