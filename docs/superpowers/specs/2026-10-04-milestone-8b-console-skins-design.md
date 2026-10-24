# Milestone 8b — Console Skins — Design

**Date:** 2026-10-04
**Status:** Implemented — milestone 8b complete 2026-10-04 (tag `milestone-8b`; Claude's check on the emulator passed — no user check, user decision). Design (brainstormed with the user 2026-10-04, with mockups in the visual companion; each of the 3 design parts approved: 'yes, looks right'); the written spec approved by the user 2026-10-04 ('approved, build the prototype'); prototyped (branch `proto-8b`, `75affec`) and checked by Claude on the emulator — the changes it brought are marked "(prototype)" below; plan: `docs/superpowers/plans/2026-10-04-milestone-8b-console-skins.md`
**Parent spec:** `docs/superpowers/specs/2026-09-24-gba-emulator-design.md` (§7 Kotlin app shell, §10 milestones)
**Builds on:** milestone 8a (the 'Cartridge grey' look: `AppTheme.kt`, `SettingsScreens.kt`) and 6c (`TouchLayout.kt`, `TouchControlsView.kt`, `LayoutEditor.kt`), all merged into `main`.

---

## 1. Goal and scope

**8b's goal:** the game screen in the colours of the special-edition handhelds. The skin colours the area around the game picture (the console's body) and the touch buttons (the console's buttons). Colours only: no console, company or game names or logos anywhere in the app.

**Decisions (user, 2026-10-04):**
- **What the skin colours:** B "body + buttons": the area around the picture in the body colour, the D-pad and buttons in that console's button colours (not A buttons only on black, not C a full console face with a frame, power light and speaker holes).
- **Which skins:** all 12 shown, plus today's look kept as **Plain**: 13 in all (§2).
- **One skin for all games**, picked in Settings → Touch buttons (no per-game skin). **Plain is the default.**
- **Sideways, buttons over the picture:** B see-through in the skin's own button colour (not white); Faint / Normal / Strong still sets how see-through.
- **Picking:** B a "Skin" row that opens a page of small phone previews (not A colour dots).
- **Approach:** 1 the native graphics code paints the body colour (exact at the picture's edge in every size mode), skins as a list in Kotlin code (not 2 Kotlin painting around the picture with a copy of the size maths, not 3 a skin data file; milestone 9 brings skin files).
- **No user check:** the milestone is done when Claude has checked everything on the phone emulator and all tests pass (user decision, 2026-10-04).

**Not in 8b:** button pictures (PNG) and skin `.zip` import (milestone 9); per-game skins; the shine on the clear skins (flat colours); a frame, light or speaker holes; changing the app's menus (they stay grey and amber); any change to the emulator core.

**Done when:** Claude's check on the phone emulator passes (§6): every skin seen upright and sideways, the body colour measured right next to the picture, pressed and fast-forward states visible, labels readable, the layout editor in the skin; and every test passes: the new ones, the Kotlin unit tests, the on-device tests, CTest 120 unchanged.

## 2. The skins (`Skin.kt`, new)

```kotlin
data class Skin(
    val id: String, val name: String,
    val body: Int,                          // around the picture
    val dpad: Int,
    val ab: Int, val abText: Int,           // A and B
    val shoulder: Int, val shoulderText: Int, // L, R, rewind, fast-forward
    val small: Int, val smallText: Int,     // Select, Start
    val lit: Int, val litText: Int,         // fast-forward while switched on
    val rim: Int?,                          // a 1.5 dp edge round every button on the body; null = none
    val overPicture: Int?, val overPictureText: Int?, // over the picture; null = each part's own colours
    val seeThroughBesideGame: Boolean = false, // (prototype) Plain only: see §3
)
object Skins { val ALL: List<Skin>; val PLAIN: Skin; fun at(number: Int): Skin; fun byId(id: String): Skin /* unknown → PLAIN */ }
```

Pure Kotlin (colours as ARGB `Int`s, no Android classes), so it is tested on the PC. Milestone 9 can build `Skin`s from a file.

| id | Name | body | dpad | ab / text | shoulder / text | small / text | lit / text |
|---|---|---|---|---|---|---|---|
| `plain` | Plain | `#000000` | `#B4B4B4` | `#B4B4B4` / `#222222` | same | same | `#FFD54A` / `#222222` |
| `ice` | Ice | `#A9D3EA` | `#3B3E45` | `#DFE3E8` / `#333333` | same | same | `#2A1A02` / `#F2A33A` |
| `pink` | Pink | `#E892C0` | `#4A3A44` | `#F6D9E8` / `#5A2A44` | same | same | `#2A1A02` / `#F2A33A` |
| `lime` | Lime | `#8FD07A` | `#2E4A2A` | `#E6F4DF` / `#2E4A2A` | same | same | `#2A1A02` / `#F2A33A` |
| `white` | White | `#EEF0F2` | `#2F3136` | `#C9CCD2` / `#333333` | same | same | `#2A1A02` / `#F2A33A` |
| `black` | Black | `#26272C` | `#0E0E10` | `#4A4C52` / `#DDDDDD` | same | same | `#F2A33A` / `#2A1A02` |
| `orange` | Orange | `#F08A2C` | `#3A2A1E` | `#FDE0BF` / `#5A3510` | same | same | `#2A1A02` / `#F2A33A` |
| `yellow` | Yellow | `#F3CC2E` | `#3A3320` | `#FFF2B8` / `#5A4A10` | same | same | `#2A1A02` / `#F2A33A` |
| `gold` | Gold | `#D2AE55` | `#3A2F14` | `#F2E3B0` / `#5A4410` | same | same | `#2A1A02` / `#F2A33A` |
| `silver` | Silver | `#B9BDC4` | `#3A3D44` | `#E9ECF0` / `#333333` | same | same | `#2A1A02` / `#F2A33A` |
| `redcream` | Red and cream | `#EBE2CD` | `#2B2B2B` | `#A61F2A` / `#FFFFFF` | `#A61F2A` / `#FFFFFF` | `#8D1B22` / `#FFFFFF` | `#F2A33A` / `#2A1A02` |
| `greyred` | Grey and red | `#C9C9C9` | `#1E1E1E` | `#C8202A` / `#FFFFFF` | `#2B2B2B` / `#DDDDDD` | `#2B2B2B` / `#DDDDDD` | `#F2A33A` / `#2A1A02` |
| `classic` | Classic grey | `#C4C0B8` | `#222222` | `#9A2257` / `#FFFFFF` | `#8F8C86` / `#222222` | `#8A8A8A` / `#222222` | `#2A1A02` / `#F2A33A` |

("same" = the ab colours. The gold and silver bodies are flat; the mockups' metallic gradient is dropped.)

**Rules shared by every skin:**
- **Pressed:** the part's colour mixed 39 % toward black; a dark colour (relative luminance < 0.1) mixed 25 % toward white instead. (Plain: `#B4B4B4` → `#6E6E6E`, today's exactly.)
- **Rim:** every skin but Plain: the body mixed 40 % toward black (a dark body: 30 % toward white), drawn 1.5 dp round the D-pad and every button that sits on the body. Found while checking contrast: on several skins the light buttons barely differ from the body (Ice 1.23:1); the rim makes every edge ≥ 2.4:1. No rim over the picture.
- **Over the picture** (see-through, alpha from Faint / Normal / Strong as today: idle `0x33/0x55/0x99`, pressed `0x66/0x99/0xCC`, text `0x88/0xDD/0xFF`): Plain white with white text (as today); the others each part's own colour and text colour.
- **Plain is today's look:** solid `#B4B4B4` / `#6E6E6E` pressed / `#222222` text on black, see-through white over the picture (sideways: every button, as today), `#FFD54A` lit. Two small differences (prototype): ⏩ is drawn as two triangles instead of the emoji (§3), and on a lit ⏩ the symbol is `#222222` (it was the see-through white over the picture).

Contrast (WCAG, computed): every label on its button ≥ 4.6:1 (lowest: Classic grey's Select/Start text 4.6:1); lit text on lit ≥ 8.1:1; every rim on its body ≥ 2.4:1; lit vs the idle shoulder colour ≥ 3.5:1 (Plain: 1.47:1, as today).

## 3. Drawing (`TouchControlsView.kt`, `video_gl.c`, the game screen)

- **Touch buttons:** `TouchControlsView` gets `var skin: Skin = Skins.PLAIN`; `onDraw` takes every colour from it (via a small `colours(part, ink, over)` helper and `Skins.pressed`), instead of today's fixed grey and white; then draws the rim (stroke) round shapes on the body. Shapes, sizes, the D-pad cross path, touch handling and the layout are unchanged.
- **Which buttons are "over the picture"** (prototype): sideways the picture's *box* is the whole screen (the game is fitted inside it), so with the old rule (`overPicture`: the button overlaps the box) every button was see-through — fine in white on black, but on a coloured body the D-pad, A/B and L/R looked washed out. The skins now ask whether a button overlaps **the game itself**: `TouchLayout` gets `fitMode` (Settings.SIZE_*) and `game` = where OpenGL draws the game inside the box (a Kotlin copy of `fit_rect_mode`, used only to pick each button's style — a pixel off at the edge changes nothing), with `overGame(b)` / `dpadOverGame`. Sideways by default only Select and Start are over the game; everything on the side bars is solid in the skin. Plain keeps the old rule (`seeThroughBesideGame = true`), so it looks as before. `TouchControlsView.fitMode`, `GameScreen(fitMode)` and `LayoutEditor(fitMode)` pass the size setting through.
- **⏩** (prototype): the label is a colour emoji, which ignores the text colour (it stayed blue on every skin), so fast-forward's symbol is drawn as two right-pointing triangles in the label's colour (`drawFastForward`). ⟲ is a plain text glyph and keeps its text.
- **The body around the picture** comes from two places, both set to `skin.body`:
  - the screen behind the game surface (`GameScreen`'s `background(Color.Black)` → the body colour): upright the area below the picture, and wherever the layout editor moved the picture;
  - the bars inside the game surface when the game doesn't fill it (sideways, or the Whole steps / Fit sizes): `glClearColor` in `video_gl_draw`, today black.
- **Native path** (as `setPicture` today): `NativeBridge.setBackground(argb: Int)` → `jni_bridge.c` keeps it in `s_background` (so a restarted game or a new surface gets it) and calls `render_set_background(s_render, argb)` → `render_thread.c` keeps it atomically (as `fit_mode`) → `video_gl_draw(..., background)` clears with it. The emulator core is not touched.
- **When it's sent:** wherever `setPicture` is sent today (on start and whenever Settings change), so a new skin shows the moment the game resumes.
- **Hidden buttons** (a keyboard or gamepad in use): the body colour stays; only the buttons go.
- **Layout editor:** its background (`Color.Black`) → the body colour, and its `TouchControlsView` gets the skin, so the editor shows the skin. Its amber selection outline is unchanged.

## 4. Picking the skin (`Settings.kt`, `SettingsScreens.kt`)

- **Setting:** `Settings.skin: Int = 0` (Plain), stored as `touch_skin` = the skin's number (its place in `Skins.ALL`; the settings store keeps whole numbers only — found in the prototype); read through `Skins.at` (unknown → Plain). New skins are only ever added at the end of `Skins.ALL`, so a stored number keeps its skin.
- **Touch buttons page:** a new first row, `NavRow(icon = palette, "Skin", <the skin's name>)`, opening the Skin page. Then today's rows (Sideways over the picture, Buzz, Edit upright / sideways layout). The main Settings page's Touch buttons summary becomes `"<Skin> · <Look> · Buzz on/off"`, e.g. "Ice · Normal · Buzz on".
- **Skin page** (`SettingsPage.SKIN`, `ScreenHeader("Skin")` with the back arrow back to Touch buttons — `SettingsPage.up` says where Back goes: Skin → Touch buttons, a section → the main page, the main page → out): rows of tiles (`SkinGrid`; not a `LazyVerticalGrid` — the page already scrolls, prototype), `max(3, width / 112 dp)` across: 3 upright, more sideways. Each tile (≥ 48 dp, the theme's card colour, 12 dp corners) shows a small phone drawn with Compose `Canvas` (clipped to 8 dp round corners) in the skin's colours: the body, the picture area at the top (a fixed sky-and-grass block), L and R, the D-pad cross, A and B, Select and Start, with the rim; the name under it. The chosen skin has a 2 dp amber border. A tap picks it at once (`onChange`), nothing else to confirm.

## 5. Wording

Skin names are colour words only: Plain, Ice, Pink, Lime, White, Black, Orange, Yellow, Gold, Silver, Red and cream, Grey and red, Classic grey. "and" is spelled out (8a's wording rules: no "&"); the existing `UiTextTest` covers the new strings.

## 6. Testing and Claude's check

**Tests (test-first):**
- **`SkinTest`** (JVM, new, 6 tests): 13 skins, unique ids and names, Plain first; `at` / `byId` of an unknown number / id is Plain; Plain's colours are exactly today's (body black, `#B4B4B4`, `pressed` → `#6E6E6E`, `#222222`, white over the picture, `#FFD54A`, no rim, see-through beside the game; no other skin is); for every skin every label ≥ 4.5:1 on its button and lit text ≥ 4.5:1 on lit; every non-Plain rim ≥ 2.0:1 on the body; every non-Plain lit ≥ 2.0:1 against the idle shoulder colour; `pressed` of a light and a dark colour.
- **`SettingsTest`** (JVM, existing tests extended): the default skin is Plain (0); the skin saves and loads; an unknown stored number (13) reads as Plain.
- **`TouchLayoutTest`** (JVM, +2, prototype): `game` for Fit / Whole steps / Stretch sideways and upright; sideways only Select and Start are over the game (all are over the box), and a button moved onto the game is.
- **`UiTextTest`** (JVM, existing): passes with the new names.
- **`PictureTest`** (on-device, +1): `NativeBridge.debugDrawFrameOn` (new: the offscreen OpenGL drawing through `gl_readback`, which gains a background argument; `debugDrawFrameWith` / `debugDrawFrame` draw on black as before) with a wide surface and Ice's body colour: every bar pixel is `#A9D3EA`, the picture unchanged.
- **`ScreensTest`** (on-device, +1): on the Skin page, tapping "Ice" calls back with skin 1 (Ice); the Touch buttons page shows the Skin row with the current name.
- **Counts:** Kotlin 88 → 96, on-device 56 → 58; CTest 120/120 (the core is not touched). The prototype: Kotlin 96/96, on-device 58/58 on the emulator, 0 C warnings.

**Claude's check on the phone emulator** (replaces a user check; screenshots with `adb exec-out screencap`, pixel values read by a script):
- every one of the 13 skins in a game, upright and sideways: the body colour around the picture, the buttons, labels readable, nothing black left where the body should be;
- with Size = Whole steps, a pixel in the bar inside the game surface and one below it measured equal to the skin's body colour (proves the native part and the screen part both work);
- a held button drawn pressed; fast-forward switched on drawn lit;
- sideways, Select / Start over the picture see-through in the skin's colours, at Faint and Strong;
- the layout editor in the skin; the Skin page with the amber border on the chosen one; Plain looking exactly as before 8b.

**Order (as in 8a, without the user's trial):** a prototype of the whole milestone first (branch `proto-8b`), checked by Claude on the emulator (colours adjusted there if something looks wrong, and this spec updated to match); then the plan is written from the prototype and executed on `milestone-8`; the milestone ends with Claude's check above and the merge question.

## 7. Files

```
app/src/main/java/com/tengen/gbaemu/Skin.kt (new)         Skin, Skins.ALL / PLAIN / at / byId, pressed / rim / contrast colour maths
app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt  colours from the skin, the rim, fitMode, ⏩ drawn
app/src/main/java/com/tengen/gbaemu/TouchLayout.kt        fitMode, game, overGame / dpadOverGame, gameIn
app/src/main/java/com/tengen/gbaemu/Screens.kt            GameScreen: body background, skin to the view
app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt       body background, skin to the view
app/src/main/java/com/tengen/gbaemu/Settings.kt           skin (`touch_skin`, the skin's number)
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt    the Skin row, the Skin page, the summary
app/src/main/java/com/tengen/gbaemu/MainActivity.kt       setBackground with setPicture; the skin to the screens
app/src/main/java/com/tengen/gbaemu/NativeBridge.kt       setBackground(argb); debugDrawFrameOn (a frame drawn on a background colour)
platform/android/jni_bridge.c                            setBackground, s_background
platform/android/render_thread.{c,h}                     render_set_background (atomic, as fit_mode)
platform/android/video_gl.{c,h}                          video_gl_draw clears with the background
platform/android/gl_readback.{c,h}                       the background passed through (for PictureTest)
app/src/test/.../SkinTest.kt (new), SettingsTest.kt (extended), TouchLayoutTest.kt (+2); app/src/androidTest/.../PictureTest.kt (+1), ScreensTest.kt (+1)
```
