# Milestone 12 — Game Pak: the UI overhaul — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Every screen of the app rebuilt as a Game Pak collection — moulded-plastic screens (light grey by day, smoked black at night, following the phone), games as cartridges with their picture as the label, the last-played game seated in the slot and "inserted" by Continue, an accent that follows the console skin — plus a real app icon and launch screen. Nothing the app does changes.

**Architecture:** Pure-Kotlin colours (`Palette`, `AccentColors`) tested on the PC; one theme file (`AppTheme.kt`: light/dark schemes, label lettering, shared rows) and one file of cartridge pieces (`GamePak.kt`: the cartridge shape, moulded drawing, label band, slot, plastic key); the screens (`Screens.kt`, `SettingsScreens.kt`) rebuilt on them; Android resources for the icon and the launch theme. Screens are checked by Compose UI tests on the emulator and by Claude's screenshots, then by the impeccable finish review.

**Tech Stack:** Kotlin, Jetpack Compose (Material 3 1.4.0, material-icons-extended 1.7.8), Compose UI test 1.11.4, JUnit 4, Android vector drawables and adaptive icons, Gradle `--offline`, Git Bash `tools/dev.sh`.

**Spec:** `docs/superpowers/specs/2026-10-05-milestone-12-game-pak-ui-design.md` · **Product record:** `PRODUCT.md` · **Direction contract:** `.impeccable/surfaces/app-src-main-java-com-tengen-gbaemu-screens-kt.md`

---

## Conventions

- Git Bash from the repo root, on branch `milestone-12`. Android: `bash tools/dev.sh <apk|jvmtest|install|launch|androidtest>` (the Pixel 9 phone emulator `Pixel_9_API_36_1`; never the user's USB phones unless the user asks). There is no `dev.sh build` for the app (`build` is the PC core). Gradle runs `--offline`; nothing new is downloaded. A missing artifact → stop and ask the user. Ask before restarting or cold-booting the emulator.
- The core (C) is not touched: CTest stays 120/120 (run once in Task 7).
- Counting tests: Kotlin unit tests `ls app/build/test-results/testDebugUnitTest/*.xml | xargs grep -h -o 'tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' | awk -F'"' '{t+=$2; f+=$6} END {print t, f}'` (total, failures); on-device `rm -rf app/build/outputs/androidTest-results` before the run, then `grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml`. Today: Kotlin 176, on-device 74. Known flake: `SessionTest.runsAtGbaSpeed` can miss by a few frames under load — rerun once.
- Test-first where a test can fail first (each task says which test is red first and why). One commit per task; messages end with the `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` trailer; each commit also includes `docs/PROGRESS.md` and this plan with its boxes ticked. Tasks run one after another without stopping; the only stop is Task 8, the user's check. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (branch `proto-12`, commit `715b626`, tried by the user on the emulator 2026-10-05: "It's beautiful, great work") with these changes, found while writing the plan: the accent and the light error red are held to ≥ 4.5:1 as **text** on every ground (the prototype only asked 3:1 of the accent on the plastic — an outlined button's text on the plastic was 3.4:1 with the Ice skin); the save stickers' numbers carry "Slot N" for TalkBack (and the tests find them that way); unused imports dropped.
- impeccable: before the first UI edit (Task 2) run `"~/.claude/plugins/cache/impeccable/impeccable/4.5.0/skills/impeccable/scripts/impeccable" context --target app/src/main/java/com/tengen/gbaemu/Screens.kt` once and read `reference/craft-floor.md` in that skill folder. No web detector applies (native app).
- Emulator settings a check changes (dark mode `cmd uimode night`, `user_rotation` / `accelerometer_rotation`, `font_scale`) are put back afterwards: today light, auto-rotate on, 1.0. Don't change the user's skin choice in the app; to see accents, use the tests (Task 2) or put the choice back exactly.

## File map

```
app/src/main/java/com/tengen/gbaemu/AccentColors.kt (new); app/src/test/.../PaletteTest.kt, AccentColorsTest.kt (new)     Task 1
app/src/main/java/com/tengen/gbaemu/AppTheme.kt, GamePak.kt (new), MainActivity.kt; app/src/androidTest/.../ScreensTest.kt   Task 2
app/src/main/java/com/tengen/gbaemu/Screens.kt (first start, game list), AppTheme.kt; ScreensTest.kt                        Task 3
app/src/main/java/com/tengen/gbaemu/Screens.kt (pause menu, slots, cheats, overlays); ScreensTest.kt                         Task 4
app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt, LayoutEditor.kt, TouchControlsView.kt                              Task 5
app/src/main/AndroidManifest.xml, app/src/main/res/** (new); app/src/test/.../AppIconTest.kt (new)                          Task 6
.impeccable/review/*.png, DESIGN.md + .impeccable/design.json (new), docs                                                   Task 7
docs, merge                                                                                                                 Task 8
```

---
### Task 1: The Game Pak colours and the accent that follows the skin

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/AccentColors.kt` (`Palette.LIGHT` / `Palette.DARK` roles; `AccentColors.seedFor`, `light`, `dark`, `hsl`, `fromHsl`)
- Test: `app/src/test/java/com/tengen/gbaemu/PaletteTest.kt`, `app/src/test/java/com/tengen/gbaemu/AccentColorsTest.kt` (new)

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/PaletteTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertTrue
import org.junit.Test

/** The Game Pak colours (12): every text colour reads at ≥ 4.5:1 on every ground it sits on, in both themes. */
class PaletteTest {
    private val white = 0xFFFFFFFF.toInt()

    private fun readable(what: String, fg: Int, bg: Int) {
        val c = Skins.contrast(fg, bg)
        assertTrue("$what: %.2f:1".format(c), c >= 4.5)
    }

    @Test
    fun textReadsOnEveryGroundInBothThemes() {
        for ((theme, p) in listOf("light" to Palette.LIGHT, "dark" to Palette.DARK)) {
            for ((name, ground) in listOf("plastic" to p.plastic, "cart" to p.cart, "cartHigh" to p.cartHigh)) {
                readable("$theme ink on $name", p.ink, ground)
                readable("$theme inkSoft on $name", p.inkSoft, ground)
                readable("$theme error on $name", p.error, ground)
            }
            readable("$theme text on error", p.onError, p.error)
            readable("$theme error on its soft ground", p.error, p.errorSoft)
            readable("$theme white on the label band", white, p.band)
            readable("$theme white on the slot", white, p.slot)
        }
    }

    @Test
    fun theSlotAndTheLabelBandAreDarkInBothThemes() {
        for (p in listOf(Palette.LIGHT, Palette.DARK)) {
            assertTrue(Skins.luminance(p.slot) < 0.03)
            assertTrue(Skins.luminance(p.band) < 0.03)
        }
        assertTrue(Skins.luminance(Palette.LIGHT.plastic) > 0.4) // grey plastic by day
        assertTrue(Skins.luminance(Palette.DARK.plastic) < 0.02) // smoked plastic at night
    }
}
```

`app/src/test/java/com/tengen/gbaemu/AccentColorsTest.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The accent follows the console skin (12) and stays readable in both themes. */
class AccentColorsTest {
    private val skins: List<String?> = Skins.ALL.map { it.id } + listOf(null) // null: an imported Delta skin

    private fun readable(what: String, fg: Int, bg: Int) {
        val c = Skins.contrast(fg, bg)
        assertTrue("$what: %.2f:1".format(c), c >= 4.5)
    }

    @Test
    fun everySkinsAccentReadsAsAButtonAndAsTextInBothThemes() {
        for (id in skins) {
            val seed = AccentColors.seedFor(id)
            val light = AccentColors.light(seed)
            readable("$id light: text on the accent", light.on, light.color)
            for (ground in listOf(Palette.LIGHT.plastic, Palette.LIGHT.cart, Palette.LIGHT.cartHigh)) {
                readable("$id light: the accent on the ground", light.color, ground)
            }
            val dark = AccentColors.dark(seed)
            readable("$id dark: text on the accent", dark.on, dark.color)
            for (ground in listOf(Palette.DARK.plastic, Palette.DARK.cart, Palette.DARK.cartHigh)) {
                readable("$id dark: the accent on the ground", dark.color, ground)
            }
        }
    }

    @Test
    fun theColouredSkinsGiveTheirOwnHueAndTheOthersTheDefault() {
        for (id in listOf("plain", "white", "black", "silver", null)) assertEquals(AccentColors.DEFAULT_SEED, AccentColors.seedFor(id))
        for (id in listOf("ice", "pink", "lime", "orange", "yellow", "gold", "redcream", "greyred", "classic")) {
            val seed = AccentColors.seedFor(id)
            assertTrue(id, seed != AccentColors.DEFAULT_SEED)
            for (accent in listOf(AccentColors.light(seed), AccentColors.dark(seed))) {
                val dh = abs(AccentColors.hsl(seed).first - AccentColors.hsl(accent.color).first)
                assertTrue("$id keeps its hue (moved ${dh}°)", minOf(dh, 360 - dh) < 6)
            }
        }
    }

    @Test
    fun hslRoundTrips() {
        for (c in listOf(0xFF4B3FB8.toInt(), 0xFFA9D3EA.toInt(), 0xFFF3CC2E.toInt(), 0xFF000000.toInt(), 0xFFFFFFFF.toInt(), 0xFF808080.toInt())) {
            val (h, s, l) = AccentColors.hsl(c)
            val back = AccentColors.fromHsl(h, s, l)
            for (shift in listOf(16, 8, 0)) assertTrue(abs(((c shr shift) and 0xFF) - ((back shr shift) and 0xFF)) <= 1)
        }
    }
}
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest`
Expected: FAIL — compilation: `Unresolved reference 'Palette'` / `'AccentColors'`.

- [x] **Step 3: Write `AccentColors.kt`**

```kotlin
package com.tengen.gbaemu

import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * The Game Pak look's colours (12) as plain ARGB numbers, so the contrast rules are tested on the PC. Light: grey
 * cartridge plastic; dark: smoked plastic. The slot and the label band are dark in both.
 */
object Palette {
    class Roles(
        val plastic: Int, val cart: Int, val cartHigh: Int, val highlight: Int, val shade: Int,
        val slot: Int, val slotMouth: Int, val band: Int, val bandEdge: Int,
        val ink: Int, val inkSoft: Int, val outline: Int, val error: Int, val onError: Int, val errorSoft: Int,
    )

    val LIGHT = Roles(
        plastic = 0xFFC5C7CC.toInt(), cart = 0xFFD7D9DD.toInt(), cartHigh = 0xFFE3E4E7.toInt(),
        highlight = 0xFFEEEFF1.toInt(), shade = 0xFFA3A5AB.toInt(),
        slot = 0xFF26272B.toInt(), slotMouth = 0xFF141518.toInt(), band = 0xFF141518.toInt(), bandEdge = 0xFF141518.toInt(),
        ink = 0xFF16171B.toInt(), inkSoft = 0xFF44464D.toInt(), outline = 0xFF8E9097.toInt(),
        error = 0xFF9E0E27.toInt(), onError = 0xFFFFFFFF.toInt(), errorSoft = 0xFFF0D5D9.toInt(),
    )

    val DARK = Roles(
        plastic = 0xFF141518.toInt(), cart = 0xFF222327.toInt(), cartHigh = 0xFF2C2D32.toInt(),
        highlight = 0xFF34353B.toInt(), shade = 0xFF070708.toInt(),
        slot = 0xFF0A0A0C.toInt(), slotMouth = 0xFF000000.toInt(), band = 0xFF000000.toInt(), bandEdge = 0xFF34353A.toInt(),
        ink = 0xFFECECEF.toInt(), inkSoft = 0xFFA6A8AF.toInt(), outline = 0xFF4A4C52.toInt(),
        error = 0xFFFF8C98.toInt(), onError = 0xFF3A0710.toInt(), errorSoft = 0xFF3A1A1F.toInt(),
    )
}

/**
 * The accent follows the console skin (12), like the see-through coloured cartridges: each built-in skin names a
 * seed colour; the accent keeps its hue and saturation and only its lightness moves until it reads well — as a
 * button with text on it, and as text itself on every ground (the hardest: the plastic in the light theme, the
 * lightest card in the dark one). Plain, White, Black, Silver and imported skins use the default indigo.
 */
object AccentColors {
    const val DEFAULT_SEED = 0xFF4B3FB8.toInt()
    private const val WHITE = 0xFFFFFFFF.toInt()

    private val SEEDS: Map<String, Int> = mapOf(
        "ice" to 0xFFA9D3EA, "pink" to 0xFFE892C0, "lime" to 0xFF8FD07A, "orange" to 0xFFF08A2C,
        "yellow" to 0xFFF3CC2E, "gold" to 0xFFD2AE55, "redcream" to 0xFFA61F2A, "greyred" to 0xFFC8202A,
        "classic" to 0xFF9A2257,
    ).mapValues { it.value.toInt() }

    /** The seed for a built-in skin's id; null (an imported skin) or a skin without one: the default. */
    fun seedFor(skinId: String?): Int = SEEDS[skinId] ?: DEFAULT_SEED

    /** An accent and the colour of text and icons on it. */
    data class Accent(val color: Int, val on: Int)

    /** Light theme: darker until white on it is ≥ 4.5:1 and it is ≥ 4.5:1 on the plastic (the darkest light ground). */
    fun light(seed: Int): Accent {
        val (h, s, l0) = hsl(seed)
        var l = l0
        var c = seed
        while (l > 0f && (Skins.contrast(WHITE, c) < 4.5 || Skins.contrast(c, Palette.LIGHT.plastic) < 4.5)) {
            l -= 0.01f
            c = fromHsl(h, s, l)
        }
        return Accent(c, WHITE)
    }

    /** Dark theme: lighter until it is ≥ 4.5:1 on the lightest card (the lightest dark ground); text on it the same hue, very dark. */
    fun dark(seed: Int): Accent {
        val (h, s, l0) = hsl(seed)
        var l = l0
        var c = seed
        while (l < 1f && Skins.contrast(c, Palette.DARK.cartHigh) < 4.5) {
            l += 0.01f
            c = fromHsl(h, s, l)
        }
        var ol = 0.16f
        var on = fromHsl(h, s, ol)
        while (ol > 0f && Skins.contrast(on, c) < 4.5) {
            ol -= 0.02f
            on = fromHsl(h, s, ol)
        }
        return Accent(c, on)
    }

    /** Hue (0..360), saturation and lightness (0..1). */
    fun hsl(c: Int): Triple<Float, Float, Float> {
        val r = ((c shr 16) and 0xFF) / 255f
        val g = ((c shr 8) and 0xFF) / 255f
        val b = (c and 0xFF) / 255f
        val max = maxOf(r, g, b)
        val min = minOf(r, g, b)
        val l = (max + min) / 2
        val d = max - min
        if (d == 0f) return Triple(0f, 0f, l)
        val s = d / (1 - abs(2 * l - 1))
        val h = when (max) {
            r -> 60 * (((g - b) / d) % 6)
            g -> 60 * ((b - r) / d + 2)
            else -> 60 * ((r - g) / d + 4)
        }
        return Triple((h + 360) % 360, s, l)
    }

    fun fromHsl(h: Float, s: Float, l: Float): Int {
        val lc = l.coerceIn(0f, 1f)
        val c = (1 - abs(2 * lc - 1)) * s
        val x = c * (1 - abs((h / 60) % 2 - 1))
        val m = lc - c / 2
        val (r, g, b) = when ((h / 60).toInt()) {
            0 -> Triple(c, x, 0f)
            1 -> Triple(x, c, 0f)
            2 -> Triple(0f, c, x)
            3 -> Triple(0f, x, c)
            4 -> Triple(x, 0f, c)
            else -> Triple(c, 0f, x)
        }
        fun ch(v: Float) = ((v + m) * 255).roundToInt().coerceIn(0, 255)
        return (0xFF shl 24) or (ch(r) shl 16) or (ch(g) shl 8) or ch(b)
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest`, then the count from Conventions.
Expected: `BUILD SUCCESSFUL`; Kotlin `181 0` (176 + 2 + 3).

- [x] **Step 5: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/java/com/tengen/gbaemu/AccentColors.kt app/src/test/java/com/tengen/gbaemu/PaletteTest.kt app/src/test/java/com/tengen/gbaemu/AccentColorsTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: the Game Pak colours and the accent that follows the skin" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The theme and the cartridge pieces

**Files:**
- Modify (replace): `app/src/main/java/com/tengen/gbaemu/AppTheme.kt` — light/dark schemes from `Palette` and the accent, `LocalPak`, `LocalBrightAccent`, label lettering (`AppTypography`), `AppShapes`, `Scrim`, the bars' colours, `ScreenHeader`, `MenuTile`, `Choices`, `Help`, `SwitchRow`, `ChoiceRow`, `NavRow` (with `last`), `Group`, `SheetItem`, `ActionSheet` (a label band as its title), `chosenBorder`; and for this task only the old game list's `cardBorder` (Task 3 removes it)
- Create: `app/src/main/java/com/tengen/gbaemu/GamePak.kt` — `CartridgeShape`, `moulded`, `recessed`, `gripRidges`, `Cartridge`, `LabelBand`, `SlotBay`, `EmptyCartOutline`, `PlasticKey`
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` — `AppTheme(accentSeed = …)`
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+1)

(The old screens keep working on the new theme: their pieces' names and parameters are unchanged. Note from the prototype: `MainActivity` handles `uiMode` itself (`configChanges`), so it isn't recreated when dark mode changes — `isSystemInDarkTheme()` still recomposes, and `AppTheme` sets the status and navigation bars' colours and icons from Compose.)

- [x] **Step 0: impeccable setup** — run the `impeccable context --target …Screens.kt` command from Conventions and read `reference/craft-floor.md`.

- [x] **Step 1: Write the failing test** — add to `ScreensTest` (imports `androidx.compose.material3.MaterialTheme`, `androidx.compose.ui.graphics.Color`):

```kotlin
    @Test
    fun theThemeFollowsDarkModeAndTheSkinsAccent() {
        val dark = mutableStateOf(false)
        val seed = mutableStateOf(AccentColors.DEFAULT_SEED)
        var background = Color.Unspecified
        var primary = Color.Unspecified
        compose.setContent {
            AppTheme(dark = dark.value, accentSeed = seed.value) {
                background = MaterialTheme.colorScheme.background
                primary = MaterialTheme.colorScheme.primary
            }
        }
        compose.waitForIdle()
        assertEquals(Color(Palette.LIGHT.plastic), background)
        assertEquals(Color(AccentColors.light(AccentColors.DEFAULT_SEED).color), primary)
        dark.value = true
        seed.value = AccentColors.seedFor("orange")
        compose.waitForIdle()
        assertEquals(Color(Palette.DARK.plastic), background)
        assertEquals(Color(AccentColors.dark(AccentColors.seedFor("orange")).color), primary)
    }
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh androidtest`
Expected: FAIL — compilation: `No parameter with name 'dark'` / `'accentSeed'` (today's `AppTheme` takes only content).

- [x] **Step 3: Write `GamePak.kt`**

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.addOutline
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp

/*
 * The Game Pak pieces (12): a cartridge's moulded body, its label band, the slot it sits in, and plastic keys.
 * Original shapes in the spirit of a handheld's cartridge — no logos, no seal.
 */

/**
 * A cartridge seen from the front: rounded corners, and along the top a raised lip a little narrower than the body
 * (the end that goes into the console). [lip]: the lip's height; the body's corners are [corner].
 */
class CartridgeShape(private val corner: Dp = 14.dp, private val lip: Dp = 7.dp) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val c = with(density) { corner.toPx() }
        val l = with(density) { lip.toPx() }
        val inset = l * 2.2f // how much narrower the lip is on each side
        val lr = l * 0.8f // the lip's own corners
        val w = size.width
        val h = size.height
        val p = Path().apply {
            moveTo(inset + lr, 0f)
            lineTo(w - inset - lr, 0f)
            quadraticTo(w - inset, 0f, w - inset, lr)
            lineTo(w - inset, l)
            lineTo(w - c, l)
            quadraticTo(w, l, w, l + c)
            lineTo(w, h - c)
            quadraticTo(w, h, w - c, h)
            lineTo(c, h)
            quadraticTo(0f, h, 0f, h - c)
            lineTo(0f, l + c)
            quadraticTo(0f, l, c, l)
            lineTo(inset, l)
            lineTo(inset, lr)
            quadraticTo(inset, 0f, inset + lr, 0f)
            close()
        }
        return Outline.Generic(p)
    }
}

/** Moulded plastic: [body] in [shape], a 1 dp [highlight] along its top edges and a [shade] lip below it. */
fun Modifier.moulded(shape: Shape, body: Color, highlight: Color, shade: Color): Modifier = drawBehind {
    val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
    translate(top = 1.5.dp.toPx()) { drawPath(path, shade) }
    clipPath(path) {
        drawRect(highlight)
        translate(top = 1.dp.toPx()) { drawPath(path, body) }
    }
}

/** The opposite: a recess — a dark rim along the top, a faint light line along the bottom. */
fun Modifier.recessed(shape: Shape, floor: Color, rim: Color, light: Color): Modifier = drawBehind {
    val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
    clipPath(path) {
        drawRect(light)
        translate(top = -1.dp.toPx()) { drawPath(path, rim) }
        translate(top = 2.dp.toPx()) { drawPath(path, floor) }
        translate(top = 1.dp.toPx()) { drawPath(path, floor) }
    }
}

/** A cartridge's grip: [count] fine ridges across the bottom of the body. */
fun Modifier.gripRidges(color: Color, count: Int = 3): Modifier = drawBehind {
    val gap = 3.dp.toPx()
    val inset = 22.dp.toPx()
    for (i in 0 until count) {
        val y = size.height - 6.dp.toPx() - i * gap
        drawLine(color, Offset(inset, y), Offset(size.width - inset, y), strokeWidth = 1.dp.toPx())
    }
}

/**
 * A cartridge: the moulded body (pressable: tap [onClick], hold [onLongClick]) holding its [content] below the lip.
 * [lip] sets the lip's height; the body's padding is [inset] on the sides.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun Cartridge(modifier: Modifier = Modifier, onClick: (() -> Unit)? = null, onLongClick: (() -> Unit)? = null,
              lip: Dp = 7.dp, inset: Dp = 8.dp, body: Color = LocalPak.current.cart,
              content: @Composable ColumnScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = CartridgeShape(lip = lip)
    var m = modifier.moulded(shape, body, pak.highlight, pak.shade).clip(shape)
    if (onClick != null) m = m.combinedClickable(onClick = onClick, onLongClick = onLongClick, role = Role.Button)
    CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
        Column(m.padding(start = inset, end = inset, top = lip + inset * 0.75f), content = content)
    }
}

/**
 * A label band: the black strip across the top of a cartridge's label with the game's name in white label lettering.
 * [big]: the seated cartridge's larger lettering. [trailing]: e.g. the ⋮ button, white on the band.
 */
@Composable
fun LabelBand(name: String, modifier: Modifier = Modifier, big: Boolean = false, lines: Int = if (big) 1 else 2,
              trailing: @Composable RowScope.() -> Unit = {}) {
    val pak = LocalPak.current
    Row(modifier.fillMaxWidth().clip(RoundedCornerShape(topStart = 4.dp, topEnd = 4.dp)).background(pak.band)
        .heightIn(min = if (big) 48.dp else 40.dp).padding(start = 10.dp, end = 2.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text(name, color = Color.White, maxLines = lines, overflow = TextOverflow.Ellipsis,
            style = if (big) MaterialTheme.typography.titleMedium else MaterialTheme.typography.titleSmall,
            modifier = Modifier.weight(1f).padding(end = 8.dp))
        CompositionLocalProvider(LocalContentColor provides Color.White.copy(alpha = 0.85f)) { trailing() }
    }
}

/** The slot: a dark recessed bay, its mouth (a darker strip with a lit edge) across the top, then [content]. */
@Composable
fun SlotBay(modifier: Modifier = Modifier, content: @Composable BoxScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = RoundedCornerShape(20.dp)
    Box(modifier.fillMaxWidth().recessed(shape, pak.slot, Color.Black, pak.highlight.copy(alpha = 0.5f)).clip(shape)) {
        Box(Modifier.fillMaxWidth().padding(horizontal = 18.dp).padding(top = 10.dp).drawBehind {
            val h = 10.dp.toPx()
            drawRoundRect(pak.slotMouth, size = Size(size.width, h), cornerRadius = CornerRadius(h / 2))
            drawLine(Color.White.copy(alpha = 0.10f), Offset(h / 2, h), Offset(size.width - h / 2, h), strokeWidth = 1.dp.toPx())
        })
        content()
    }
}

/** An empty slot: a dashed cartridge outline with [text] in it. */
@Composable
fun EmptyCartOutline(text: String, modifier: Modifier = Modifier) {
    val color = Color.White.copy(alpha = 0.32f)
    val shape = CartridgeShape(lip = 9.dp)
    Box(modifier.drawBehind {
        val path = Path().apply { addOutline(shape.createOutline(size, layoutDirection, this@drawBehind)) }
        drawPath(path, color, style = Stroke(width = 1.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(14f, 10f))))
    }.padding(24.dp), contentAlignment = Alignment.Center) {
        Text(text, color = Color.White.copy(alpha = 0.78f), style = MaterialTheme.typography.bodyMedium)
    }
}

/** A raised plastic key (the pause menu's tiles): moulded, pressable, its icon and label centred. */
@Composable
fun PlasticKey(onClick: () -> Unit, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = RoundedCornerShape(16.dp)
    CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
        Column(modifier.moulded(shape, pak.cartHigh, pak.highlight, pak.shade).clip(shape)
            .combinedClickable(onClick = onClick, role = Role.Button).padding(10.dp),
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center, content = content)
    }
}
```

- [x] **Step 4: Replace `AppTheme.kt`**

```kotlin
package com.tengen.gbaemu

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.Shapes
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

/**
 * The app's look (milestone 12, "Game Pak"): screens of moulded cartridge plastic — light grey by day, smoked black at
 * night, following the phone — games as cartridges with their picture as the label, the slot dark in both. One
 * see-through-plastic accent from the console skin marks only what can be pressed. Screens read these roles.
 */
@Immutable
class PakColors(
    val cart: Color, val cartHigh: Color, val highlight: Color, val shade: Color,
    val slot: Color, val slotMouth: Color, val band: Color, val bandEdge: Color, val dark: Boolean,
)

val LocalPak = compositionLocalOf { pakColors(Palette.DARK, dark = true) }

/** The accent's bright form (the dark theme's), for marks drawn over the game or a skin, whatever the theme. */
val LocalBrightAccent = compositionLocalOf { Color(AccentColors.dark(AccentColors.DEFAULT_SEED).color) }

private fun pakColors(p: Palette.Roles, dark: Boolean) = PakColors(
    Color(p.cart), Color(p.cartHigh), Color(p.highlight), Color(p.shade),
    Color(p.slot), Color(p.slotMouth), Color(p.band), Color(p.bandEdge), dark,
)

private fun scheme(dark: Boolean, accent: AccentColors.Accent): ColorScheme {
    val p = if (dark) Palette.DARK else Palette.LIGHT
    val a = Color(accent.color)
    val onA = Color(accent.on)
    val base = if (dark) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = a, onPrimary = onA, primaryContainer = a.copy(alpha = 0.18f).compositeOn(Color(p.cart)), onPrimaryContainer = Color(p.ink),
        secondary = a, onSecondary = onA, secondaryContainer = Color(p.cartHigh), onSecondaryContainer = Color(p.ink),
        tertiary = a, onTertiary = onA,
        background = Color(p.plastic), onBackground = Color(p.ink),
        surface = Color(p.plastic), onSurface = Color(p.ink),
        surfaceVariant = Color(p.cartHigh), onSurfaceVariant = Color(p.inkSoft),
        surfaceContainerLowest = Color(p.plastic), surfaceContainerLow = Color(p.cart),
        surfaceContainer = Color(p.cart), surfaceContainerHigh = Color(p.cart), surfaceContainerHighest = Color(p.cartHigh),
        surfaceBright = Color(p.cartHigh), surfaceDim = Color(p.plastic), surfaceTint = Color.Transparent,
        inverseSurface = Color(p.ink), inverseOnSurface = Color(p.plastic), inversePrimary = a,
        error = Color(p.error), onError = Color(p.onError), errorContainer = Color(p.errorSoft), onErrorContainer = Color(p.error),
        outline = Color(p.outline), outlineVariant = Color(p.outline).copy(alpha = 0.45f).compositeOn(Color(p.cart)),
        scrim = Color.Black,
    )
}

private fun Color.compositeOn(under: Color): Color {
    val a = alpha
    return Color(red * a + under.red * (1 - a), green * a + under.green * (1 - a), blue * a + under.blue * (1 - a))
}

/** Roboto Black Italic: the lettering on cartridge labels — screen titles, names on bands, slot numbers. */
val LabelFace = FontFamily.SansSerif
/** Roboto Condensed Bold: small tags. */
val TagFace = FontFamily(android.graphics.Typeface.create("sans-serif-condensed", android.graphics.Typeface.BOLD))

private fun label(size: Int, line: Int, spacing: Float = 0f) = TextStyle(fontFamily = LabelFace, fontWeight = FontWeight.Black,
    fontStyle = FontStyle.Italic, fontSize = size.sp, lineHeight = line.sp, letterSpacing = spacing.sp)

val AppTypography: Typography = Typography().let { t ->
    t.copy(
        displaySmall = label(40, 44, -0.5f),
        headlineMedium = label(30, 36, -0.3f),
        headlineSmall = label(26, 32, -0.2f),
        titleLarge = label(22, 28),
        titleMedium = label(18, 24),
        titleSmall = label(15, 19),
        labelLarge = t.labelLarge.copy(fontWeight = FontWeight.Bold),
        labelMedium = TextStyle(fontFamily = TagFace, fontWeight = FontWeight.Bold, fontSize = 13.sp, lineHeight = 16.sp,
            letterSpacing = 1.2.sp),
    )
}

val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp), small = RoundedCornerShape(10.dp), medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(18.dp), extraLarge = RoundedCornerShape(22.dp),
)

/** The dimmed layer behind menus over the game. */
val Scrim = Color(0xB0000000)

/**
 * The theme: [dark] follows the phone; [accentSeed] is the chosen skin's plastic ([AccentColors.seedFor]). Also sets
 * the status and navigation bars' colours and icons to match (the activity isn't recreated when dark mode changes).
 */
@Composable
fun AppTheme(dark: Boolean = isSystemInDarkTheme(), accentSeed: Int = AccentColors.DEFAULT_SEED, content: @Composable () -> Unit) {
    val accent = remember(dark, accentSeed) { if (dark) AccentColors.dark(accentSeed) else AccentColors.light(accentSeed) }
    val colors = remember(dark, accent) { scheme(dark, accent) }
    val pak = remember(dark) { pakColors(if (dark) Palette.DARK else Palette.LIGHT, dark) }
    val view = LocalView.current
    if (!view.isInEditMode) SideEffect {
        val window = (view.context as? Activity)?.window ?: return@SideEffect
        val plastic = colors.background.toArgb()
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT < 35) {
            window.statusBarColor = plastic
            window.navigationBarColor = plastic
        }
        WindowCompat.getInsetsController(window, view).apply {
            isAppearanceLightStatusBars = !dark
            isAppearanceLightNavigationBars = !dark
        }
    }
    val bright = remember(accentSeed) { Color(AccentColors.dark(accentSeed).color) }
    androidx.compose.runtime.CompositionLocalProvider(LocalPak provides pak, LocalBrightAccent provides bright) {
        MaterialTheme(colorScheme = colors, typography = AppTypography, shapes = AppShapes, content = content)
    }
}

/**
 * A screen's top bar: a back arrow (if [onBack]), the title in label lettering (and a smaller line under it), icon
 * buttons on the right.
 */
@Composable
fun ScreenHeader(title: String, onBack: (() -> Unit)? = null, subtitle: String? = null, actions: @Composable RowScope.() -> Unit = {}) {
    Row(Modifier.fillMaxWidth().heightIn(min = 64.dp).padding(start = if (onBack == null) 20.dp else 4.dp, end = 4.dp, top = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (onBack != null) {
            IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back") }
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.headlineSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (subtitle != null) {
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        actions()
    }
}

/** A plastic key with an icon over its label: the pause menu's main choices. */
@Composable
fun MenuTile(icon: ImageVector, label: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    PlasticKey(onClick = onClick, modifier = modifier.heightIn(min = 76.dp)) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(26.dp))
        Spacer(Modifier.size(6.dp))
        Text(label, style = MaterialTheme.typography.labelLarge, maxLines = 1)
    }
}

/** 2-4 short options side by side; tapping one picks it. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Choices(options: List<String>, selected: Int, onPick: (Int) -> Unit, modifier: Modifier = Modifier) {
    SingleChoiceSegmentedButtonRow(modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected, onClick = { onPick(i) },
                shape = SegmentedButtonDefaults.itemShape(index = i, count = options.size),
                icon = {}, // no tick: the colour shows the choice, and the labels keep their room
                colors = SegmentedButtonDefaults.colors(activeContainerColor = MaterialTheme.colorScheme.primary,
                    activeContentColor = MaterialTheme.colorScheme.onPrimary, inactiveContainerColor = MaterialTheme.colorScheme.background,
                    inactiveContentColor = MaterialTheme.colorScheme.onSurface,
                    activeBorderColor = MaterialTheme.colorScheme.primary, inactiveBorderColor = MaterialTheme.colorScheme.outline),
            ) { Text(label, maxLines = 1, fontSize = 13.sp, fontWeight = FontWeight.Medium) }
        }
    }
}

/** A setting's help: under the setting it explains. */
@Composable
fun Help(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 6.dp))
}

/** An on/off setting: the whole row toggles it. */
@Composable
fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(horizontal = 16.dp, vertical = 14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Switch(checked = checked, onCheckedChange = onChange)
        }
        if (help != null) Help(help)
    }
}

/** A setting with 2-4 options shown at once: its label, the [Choices], its help. */
@Composable
fun ChoiceRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, help: String? = null) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp)) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(bottom = 10.dp))
        Choices(options, selected, onPick)
        if (help != null) Help(help)
    }
}

/**
 * A row that opens a page: an icon, its title, what it is set to now, and a chevron. [enabled] false: greyed out, a
 * tap does nothing. Inside a [Group] the rows are divided by hairlines; [last] leaves the line off.
 */
@Composable
fun NavRow(icon: ImageVector?, title: String, summary: String?, enabled: Boolean = true, last: Boolean = false, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick).alpha(if (enabled) 1f else 0.38f)
        .heightIn(min = 64.dp).padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (icon != null) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(24.dp))
            Spacer(Modifier.width(16.dp))
        }
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
            if (!summary.isNullOrEmpty()) {
                Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.outline)
    }
    if (!last) HorizontalDivider(Modifier.padding(start = if (icon != null) 56.dp else 16.dp),
        color = MaterialTheme.colorScheme.outlineVariant)
}

/** Settings rows grouped on a moulded plastic card. */
@Composable
fun Group(modifier: Modifier = Modifier, body: Color = LocalPak.current.cart, content: @Composable ColumnScope.() -> Unit) {
    val pak = LocalPak.current
    val shape = MaterialTheme.shapes.large
    Column(modifier.fillMaxWidth().padding(vertical = 6.dp).moulded(shape, body, pak.highlight, pak.shade)
        .clip(shape), content = content)
}

/** One line of an [ActionSheet]. [danger]: red, and set apart from the others. */
class SheetItem(val icon: ImageVector, val label: String, val danger: Boolean = false, val onClick: () -> Unit)

/** A panel from the bottom: a label band with [title], then icon + label lines; the danger lines come last, after a divider. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ActionSheet(title: String, items: List<SheetItem>, onClose: () -> Unit) {
    ModalBottomSheet(onDismissRequest = onClose, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = LocalPak.current.cart) {
        Column(Modifier.navigationBarsPadding().padding(bottom = 12.dp)) {
            LabelBand(title, Modifier.padding(horizontal = 16.dp).padding(bottom = 8.dp), big = true)
            val (safe, danger) = items.partition { !it.danger }
            for (item in safe) SheetLine(item)
            if (danger.isNotEmpty()) {
                HorizontalDivider(Modifier.padding(vertical = 4.dp, horizontal = 24.dp), color = MaterialTheme.colorScheme.outlineVariant)
                for (item in danger) SheetLine(item)
            }
        }
    }
}

@Composable
private fun SheetLine(item: SheetItem) {
    val color = if (item.danger) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface
    Row(Modifier.fillMaxWidth().clickable(onClick = item.onClick).heightIn(min = 52.dp).padding(horizontal = 24.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(item.icon, contentDescription = null, tint = if (item.danger) color else MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(16.dp))
        Text(item.label, color = color, style = MaterialTheme.typography.bodyLarge)
    }
}

/** A chosen tile's ring (the Skin page). */
@Composable
fun chosenBorder(chosen: Boolean): BorderStroke? = if (chosen) BorderStroke(3.dp, MaterialTheme.colorScheme.primary) else null

/** The old game list's Continue card border — only until Task 3 rebuilds the list. */
@Composable
fun cardBorder(highlight: Boolean): BorderStroke? = if (highlight) BorderStroke(1.dp, MaterialTheme.colorScheme.primary) else null
```

- [x] **Step 5: The accent from the skin** — in `MainActivity.onCreate`, replace

```kotlin
        setContent {
            AppTheme {
```

with

```kotlin
        setContent {
            // The accent follows the console skin (12); an imported Delta skin uses the default.
            AppTheme(accentSeed = AccentColors.seedFor(if (settings.skinImport != 0) null else Skins.at(settings.skin).id)) {
```

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh jvmtest` and `bash tools/dev.sh androidtest`, then the counts.
Expected: Kotlin `181 0`; on-device `75 0` (74 + 1).

- [x] **Step 7: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/java/com/tengen/gbaemu/AppTheme.kt app/src/main/java/com/tengen/gbaemu/GamePak.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: the Game Pak theme (light and dark, the skin's accent) and the cartridge pieces" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The first start and the game list as a cartridge collection

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` — imports; `FirstStartScreen` (an empty slot); `LibraryScreen` and its parts (`EmptyLibrary`, `ContinueSlot`, `SeatedLabel`, `ContinueButton`, `PlayedText`, `insertMotion`, `GameCartridge`, `LeftPicture`, `Tabular`) replacing `LibraryScreen`, `GameCard`, `GamePicture`, `LeftPicture`, `GameInfo`
- Modify: `app/src/main/java/com/tengen/gbaemu/AppTheme.kt` — delete `cardBorder` (its last user is gone)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (+3, 1 changed)

- [x] **Step 1: Write the failing tests** — in `ScreensTest`:

In `theGameListShowsTheContinueCardAndOpensAGamesSheetFromItsButton`, after `compose.onNodeWithTag("continue").performClick()` add `compose.waitForIdle()` (the cartridge goes into the slot first, then the game opens). Add:

```kotlin
    @Test
    fun theFirstStartShowsAnEmptySlotAndItsButtons() {
        var chose = 0
        var opened = 0
        compose.setContent {
            AppTheme { FirstStartScreen(false, null, onChooseFolder = { chose++ }, onOpenGame = { opened++ }, onSettings = {}) }
        }
        compose.onNodeWithText("No cartridge").assertIsDisplayed()
        compose.onNodeWithText("Choose games folder").performClick()
        compose.onNodeWithText("Open one game").performClick()
        assertEquals(1, chose)
        assertEquals(1, opened)
    }

    @Test
    fun withNothingPlayedTheSlotIsEmptyAndACartridgePlaysAfterGoingIn() {
        var played: GameEntry? = null
        val game = GameEntry("c", "u", "game D")
        compose.setContent {
            AppTheme {
                LibraryScreen(listOf(game), "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    onPlay = { played = it }, onHold = {}, onOpenGame = {}, onChangeFolder = {}, onSettings = {})
            }
        }
        compose.onNodeWithText("Pick a game below to start").assertIsDisplayed()
        compose.onNodeWithTag("game").performClick()
        compose.waitForIdle()
        assertEquals(game, played)
    }

    @Test
    fun anEmptyFolderShowsAnEmptySlotAndChangeFolder() {
        var changed = 0
        compose.setContent {
            AppTheme {
                LibraryScreen(emptyList(), "GBA", scanning = false, message = null, refresh = 0, picture = { null },
                    onPlay = {}, onHold = {}, onOpenGame = {}, onChangeFolder = { changed++ }, onSettings = {})
            }
        }
        compose.onNodeWithText("No games in GBA yet").assertIsDisplayed()
        compose.onNodeWithText("Change folder").performClick()
        assertEquals(1, changed)
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh androidtest`
Expected: FAIL — `theFirstStartShowsAnEmptySlotAndItsButtons` (no node "No cartridge") and `withNothingPlayedTheSlotIsEmpty…` (no node "Pick a game below to start"); the empty-folder test passes already (same words as today).

- [x] **Step 3: Replace the imports of `Screens.kt`** (everything from `import android.content.Context` to the line before `/** The very first start`) with:

```kotlin
import android.content.Context
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ExitToApp
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.CloudDownload
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.HideImage
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.RemoveCircleOutline
import androidx.compose.material.icons.filled.Replay
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.WbSunny
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import kotlin.math.roundToInt
import kotlinx.coroutines.delay
```

- [x] **Step 4: Replace `FirstStartScreen`** (from `/** The very first start` to the line before `/** When a game was last played`) with:

```kotlin
/** The very first start (no games folder yet) or a folder that can't be opened any more: an empty slot waiting. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Box(Modifier.fillMaxSize().systemBarsPadding()) {
            IconButton(onClick = onSettings, modifier = Modifier.align(Alignment.TopEnd).padding(4.dp)) {
                Icon(Icons.Filled.Settings, contentDescription = "Settings")
            }
            Column(
                modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(20.dp))
                SlotBay(Modifier.widthIn(max = 420.dp)) {
                    EmptyCartOutline(if (folderLost) "Folder not found" else "No cartridge",
                        Modifier.fillMaxWidth().padding(start = 28.dp, end = 28.dp, top = 15.dp, bottom = 22.dp).height(150.dp))
                }
                Spacer(Modifier.height(24.dp))
                Text(
                    if (folderLost) "Can't open the games folder any more. Choose it again."
                    else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                        "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                        "Create new folder) and put your games in it.",
                    textAlign = TextAlign.Center,
                    color = if (folderLost) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.widthIn(max = 420.dp),
                )
                Spacer(Modifier.height(28.dp))
                Button(onClick = onChooseFolder, modifier = Modifier.widthIn(min = 260.dp).heightIn(min = 56.dp)) {
                    Icon(Icons.Filled.FolderOpen, contentDescription = null)
                    Spacer(Modifier.width(10.dp))
                    Text("Choose games folder")
                }
                Spacer(Modifier.height(12.dp))
                OutlinedButton(onClick = onOpenGame, modifier = Modifier.widthIn(min = 260.dp).heightIn(min = 56.dp)) {
                    Text("Open one game")
                }
                if (message != null) {
                    Spacer(Modifier.height(16.dp))
                    Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
                }
            }
        }
    }
}
```

- [x] **Step 5: Replace the game list** (from the comment `The game list: the last-played game as a big Continue card` to the line before `/** A game's sheet`) with:

```kotlin
/** Times and counts: even-width figures, so they line up. */
private val Tabular = TextStyle(fontFeatureSettings = "tnum")

/**
 * The game list as a cartridge collection: the last-played game seated in the slot with Continue, then every other
 * game as a cartridge (its box art or the picture of where it was left as the label). A tap pushes the cartridge into
 * the slot, then plays; holding or the cartridge's ⋮ opens [onHold]'s sheet. [refresh] changes whenever the pictures
 * may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, box: (GameEntry) -> Bitmap? = { null }, onPlay: (GameEntry) -> Unit,
                  onHold: (GameEntry) -> Unit, onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    var inserting by remember { mutableStateOf<String?>(null) } // the game being pushed into the slot
    val insert = { g: GameEntry -> if (inserting == null) inserting = g.key }
    val done = { g: GameEntry -> inserting = null; onPlay(g) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            ScreenHeader("Your games", subtitle = folderName) {
                IconButton(onClick = onSettings) { Icon(Icons.Filled.Settings, contentDescription = "Settings") }
                Box {
                    IconButton(onClick = { menuOpen = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "More") }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open one game") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder") }, onClick = { menuOpen = false; onChangeFolder() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                EmptyLibrary(folderName, scanning, onChangeFolder)
            } else {
                val (last, rest) = remember(rows) { Library.continueAndRest(rows) }
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(152.dp), modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 24.dp),
                    horizontalArrangement = Arrangement.spacedBy(14.dp), verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    item(key = "slot:" + (last?.key ?: ""), span = { GridItemSpan(maxLineSpan) }) {
                        ContinueSlot(last, refresh, picture, inserting == last?.key, insert, done, onHold)
                    }
                    items(rest, key = { it.key }) { game ->
                        GameCartridge(game, refresh, picture, box, inserting == game.key, insert, done, onHold)
                    }
                }
            }
        }
    }
}

/** No games: still looking, or the folder is empty. */
@Composable
private fun EmptyLibrary(folderName: String?, scanning: Boolean, onChangeFolder: () -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        SlotBay {
            Box(Modifier.fillMaxWidth().padding(start = 28.dp, end = 28.dp, top = 15.dp, bottom = 22.dp).height(170.dp),
                contentAlignment = Alignment.Center) {
                if (scanning) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator(color = Color.White)
                        Spacer(Modifier.height(14.dp))
                        Text("Looking for games", color = Color.White.copy(alpha = 0.78f))
                    }
                } else {
                    EmptyCartOutline("No games in ${folderName ?: "the folder"} yet", Modifier.fillMaxSize())
                }
            }
        }
        if (!scanning) {
            Spacer(Modifier.height(20.dp))
            Text("Put .gba or .zip files there, or pick another folder.", textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(20.dp))
            Button(onClick = onChangeFolder, modifier = Modifier.heightIn(min = 52.dp)) {
                Icon(Icons.Filled.FolderOpen, contentDescription = null)
                Spacer(Modifier.width(10.dp))
                Text("Change folder")
            }
        }
    }
}

/**
 * The slot at the top of the list: [game] (the last played) seated in it — its label the picture of where it was
 * left, Continue under it. Inserting: the cartridge slides up into the slot's mouth, then [onInserted]. No game played
 * yet: an empty slot.
 */
@Composable
private fun ContinueSlot(game: GameEntry?, refresh: Int, picture: (GameEntry) -> Bitmap?, inserting: Boolean,
                         onInsert: (GameEntry) -> Unit, onInserted: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit) {
    SlotBay {
        // The clip's top edge is the middle of the slot's mouth: what slides above it has gone into the slot.
        BoxWithConstraints(Modifier.fillMaxWidth().padding(top = 15.dp).clipToBounds().padding(bottom = 20.dp),
            contentAlignment = Alignment.TopCenter) {
            if (game == null) {
                EmptyCartOutline("Pick a game below to start",
                    Modifier.padding(horizontal = 28.dp).fillMaxWidth().height(150.dp))
                return@BoxWithConstraints
            }
            val bitmap = remember(game.id, refresh) { picture(game) }
            val travel = with(LocalDensity.current) { 64.dp.toPx() }
            val push = insertMotion(inserting) { onInserted(game) }
            val wide = maxWidth > 560.dp // sideways or a tablet: the label on the left, Continue beside it
            val width = if (wide) minOf(maxWidth - 40.dp, 680.dp) else maxWidth - 40.dp
            Cartridge(Modifier.width(width).graphicsLayer { translationY = -push.value * travel }.testTag("continue"),
                onClick = { onInsert(game) }, onLongClick = { onHold(game) }, lip = 9.dp, inset = 12.dp) {
                if (wide) {
                    Row {
                        SeatedLabel(game, bitmap, push.value, onHold, Modifier.width(300.dp))
                        Column(Modifier.weight(1f).padding(start = 20.dp, top = 8.dp)) {
                            PlayedText(game, Modifier.padding(bottom = 16.dp))
                            ContinueButton { onInsert(game) }
                        }
                    }
                    Spacer(Modifier.fillMaxWidth().height(24.dp).gripRidges(LocalPak.current.shade))
                } else {
                    SeatedLabel(game, bitmap, push.value, onHold, Modifier)
                    Row(Modifier.fillMaxWidth().gripRidges(LocalPak.current.shade).padding(top = 12.dp, bottom = 22.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        ContinueButton { onInsert(game) }
                        Spacer(Modifier.weight(1f))
                        PlayedText(game, Modifier.padding(start = 12.dp))
                    }
                }
            }
        }
    }
}

/** The seated cartridge's label: the band with the name and ⋮, the picture of where the game was left. */
@Composable
private fun SeatedLabel(game: GameEntry, bitmap: Bitmap?, push: Float, onHold: (GameEntry) -> Unit, modifier: Modifier) {
    Column(modifier.clip(RoundedCornerShape(4.dp))) {
        LabelBand(game.name, big = true) {
            IconButton(onClick = { onHold(game) }) { Icon(Icons.Filled.MoreVert, contentDescription = "${game.name}: more") }
        }
        Box {
            LeftPicture(bitmap, big = true, Modifier.fillMaxWidth(), corners = 0.dp)
            Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = push * 0.6f)))
        }
    }
}

@Composable
private fun ContinueButton(onClick: () -> Unit) {
    Button(onClick = onClick, modifier = Modifier.heightIn(min = 48.dp), contentPadding = PaddingValues(start = 18.dp, end = 24.dp)) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null)
        Spacer(Modifier.width(6.dp))
        Text("Continue", style = MaterialTheme.typography.titleSmall.copy(fontSize = 17.sp))
    }
}

@Composable
private fun PlayedText(game: GameEntry, modifier: Modifier) {
    Text(playedText(game.lastPlayed), style = MaterialTheme.typography.bodyMedium.merge(Tabular),
        color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, modifier = modifier)
}

/**
 * The insert (12): 0 → 1 over 260 ms once [inserting] turns true, then [onDone]; back to 0 a moment later (in case the
 * game didn't open, e.g. it asks about a save first). With Android's animations off it is instant.
 */
@Composable
private fun insertMotion(inserting: Boolean, onDone: () -> Unit): Animatable<Float, AnimationVector1D> {
    val push = remember { Animatable(0f) }
    LaunchedEffect(inserting) {
        if (!inserting) return@LaunchedEffect
        push.animateTo(1f, tween(260, easing = CubicBezierEasing(0.3f, 0f, 0.8f, 0.15f)))
        onDone()
        delay(400)
        push.snapTo(0f)
    }
    return push
}

/** One game of the collection: a cartridge with its box (or the picture of where it was left) as the label. */
@Composable
private fun GameCartridge(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, box: (GameEntry) -> Bitmap?,
                          inserting: Boolean, onInsert: (GameEntry) -> Unit, onInserted: (GameEntry) -> Unit,
                          onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    val boxBitmap = remember(game.id, refresh) { box(game) }
    val push = insertMotion(inserting) { onInserted(game) }
    val travel = with(LocalDensity.current) { 14.dp.toPx() }
    Cartridge(Modifier.fillMaxWidth().graphicsLayer {
        translationY = -push.value * travel
        val s = 1f - push.value * 0.04f
        scaleX = s
        scaleY = s
    }.testTag("game"), onClick = { onInsert(game) }, onLongClick = { onHold(game) }, lip = 6.dp, inset = 8.dp) {
        Column(Modifier.clip(RoundedCornerShape(4.dp))) {
            LabelBand(game.name)
            Box(Modifier.fillMaxWidth().aspectRatio(1f).background(LocalPak.current.band), contentAlignment = Alignment.Center) {
                if (boxBitmap != null) {
                    Image(boxBitmap.asImageBitmap(), contentDescription = "Box art", modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit)
                } else {
                    LeftPicture(bitmap, big = false, Modifier.fillMaxWidth(), corners = 0.dp)
                }
                Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = push.value * 0.6f)))
            }
        }
        Row(Modifier.fillMaxWidth().gripRidges(LocalPak.current.shade).padding(start = 2.dp, bottom = 12.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                style = MaterialTheme.typography.bodySmall.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            IconButton(onClick = { onHold(game) }) {
                Icon(Icons.Filled.MoreVert, contentDescription = "${game.name}: more", tint = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

/** The picture of where the game was left (3:2, sharp pixels), or a placeholder. */
@Composable
private fun LeftPicture(bitmap: Bitmap?, big: Boolean, modifier: Modifier, corners: Dp = 8.dp) {
    val frame = modifier.aspectRatio(1.5f).clip(RoundedCornerShape(corners))
    if (bitmap != null) {
        Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
            contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
    } else {
        Box(frame.background(LocalPak.current.slot), contentAlignment = Alignment.Center) {
            Icon(Icons.Filled.SportsEsports, contentDescription = null, tint = Color.White.copy(alpha = 0.35f),
                modifier = Modifier.size(if (big) 48.dp else 32.dp))
        }
    }
}
```

- [x] **Step 6: Delete `cardBorder`** at the end of `AppTheme.kt` (the 3 lines added in Task 2, Step 4).

- [x] **Step 7: Run the tests**

Run: `bash tools/dev.sh jvmtest` (the wording test reads the new strings) and `bash tools/dev.sh androidtest`, then the counts.
Expected: Kotlin `181 0`; on-device `78 0` (75 + 3).

- [x] **Step 8: Look at it** — `bash tools/dev.sh install`, open the app: the seated cartridge in the slot, the grid of cartridges; tap Continue and watch it go up into the slot (then Back, Quit). One screenshot is enough here; the full check is Task 7.

- [x] **Step 9: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/AppTheme.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: the game list as a cartridge collection, the slot and the insert; the first start's empty slot" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The pause menu, save stickers, cheats and the overlays

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt` — `MenuLayer` (a cartridge panel, fades and grows in), `GameMenu` (the label band on top, no "Paused" line), `ResumeButton`, `MenuTiles`, `SpeedChoices`, `RestartQuit`, `SlotGrid` + `SaveSticker` (replacing `SlotCard`), `CheatPanel` (rows on a group), `SpeedLabel`, `SunControl` (white on the scrim)
- Test: `app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt` (1 changed)

- [x] **Step 1: Change the slot test to find the slots by their spoken name** — in `theLoadPageOffersTheQuickSlotFirstAndTheSavePageDoesNot` replace its last six lines (from `compose.onNodeWithText("Slot 2")`) with:

```kotlin
        compose.onNodeWithContentDescription("Slot 2").assertIsDisplayed()
        compose.onNodeWithContentDescription("Quick").performClick()
        assertEquals(States.QUICK, picked)
        saving.value = true
        compose.onNodeWithContentDescription("Quick").assertDoesNotExist()
        compose.onNodeWithContentDescription("Slot 10").performScrollTo().assertIsDisplayed()
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh androidtest`
Expected: FAIL in `theLoadPageOffersTheQuickSlotFirst…` — no node with content description "Slot 2" (today the slot's name is plain text and its picture is described in lower case).

- [x] **Step 3: Replace the in-game menu** (keep `enum class MenuPage`; replace from the comment `A dimmed layer over the paused game` to the line before `/** The type picker's names`) with:

```kotlin
/**
 * A dimmed layer over the paused game (it takes every touch) with a cartridge-plastic panel in the middle; it
 * appears with a quick fade and grow.
 */
@Composable
private fun MenuLayer(maxWidth: Dp = 440.dp, content: @Composable ColumnScope.() -> Unit) {
    val shown = remember { Animatable(0f) }
    LaunchedEffect(Unit) { shown.animateTo(1f, tween(150)) }
    Box(
        Modifier.fillMaxSize().graphicsLayer { alpha = shown.value }.background(Scrim)
            .pointerInput(Unit) { detectTapGestures { } }.systemBarsPadding(),
        contentAlignment = Alignment.Center,
    ) {
        Cartridge(Modifier.padding(16.dp).widthIn(max = maxWidth).graphicsLayer {
            val s = 0.96f + 0.04f * shown.value
            scaleX = s
            scaleY = s
        }, lip = 10.dp, inset = 14.dp, content = content)
    }
}

/**
 * Back while playing: the game is paused behind this menu. [fastSpeed]: 2, 4, 8 or NativeBridge.FF_MAX. Sideways (a
 * short, wide screen) it has two columns, so nothing is below the fold.
 */
@Composable
fun GameMenu(title: String, fastSpeed: Int, onFastSpeed: (Int) -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    BoxWithConstraints {
        val wide = maxWidth > maxHeight && maxHeight < 520.dp
        MenuLayer(maxWidth = if (wide) 720.dp else 420.dp) {
            if (wide) {
                LabelBand(title, Modifier.clip(RoundedCornerShape(4.dp)), big = true)
                Row(Modifier.padding(top = 14.dp, bottom = 16.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column(Modifier.weight(1f)) {
                        ResumeButton(onResume)
                        SpeedChoices(fastSpeed, onFastSpeed)
                        RestartQuit(onRestart, onQuit)
                    }
                    Column(Modifier.weight(1f).align(Alignment.CenterVertically)) { MenuTiles(onSave, onLoad, onCheats, onSettings) }
                }
            } else {
                Column(Modifier.widthIn(min = 280.dp, max = 392.dp).verticalScroll(rememberScrollState()).padding(bottom = 16.dp)) {
                    LabelBand(title, Modifier.clip(RoundedCornerShape(4.dp)), big = true, lines = 2)
                    Spacer(Modifier.height(16.dp))
                    ResumeButton(onResume)
                    Spacer(Modifier.height(14.dp))
                    MenuTiles(onSave, onLoad, onCheats, onSettings)
                    SpeedChoices(fastSpeed, onFastSpeed)
                    RestartQuit(onRestart, onQuit)
                }
            }
        }
    }
}

@Composable
private fun ResumeButton(onResume: () -> Unit) {
    Button(onClick = onResume, modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp)) {
        Icon(Icons.Filled.PlayArrow, contentDescription = null)
        Spacer(Modifier.width(8.dp))
        Text("Resume", style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun MenuTiles(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        MenuTile(Icons.Filled.Save, "Save state", onSave, Modifier.weight(1f))
        MenuTile(Icons.Filled.FolderOpen, "Load state", onLoad, Modifier.weight(1f))
    }
    Spacer(Modifier.height(12.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        MenuTile(Icons.Filled.AutoFixHigh, "Cheats", onCheats, Modifier.weight(1f))
        MenuTile(Icons.Filled.Settings, "Settings", onSettings, Modifier.weight(1f))
    }
}

@Composable
private fun SpeedChoices(fastSpeed: Int, onFastSpeed: (Int) -> Unit) {
    Spacer(Modifier.height(18.dp))
    Text("Fast-forward speed", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    Spacer(Modifier.height(8.dp))
    Choices(Settings.SPEEDS.map(Settings::speedName), Settings.SPEEDS.indexOf(fastSpeed).coerceAtLeast(0),
        onPick = { onFastSpeed(Settings.SPEEDS[it]) })
}

@Composable
private fun RestartQuit(onRestart: () -> Unit, onQuit: () -> Unit) {
    Spacer(Modifier.height(18.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedButton(onClick = onRestart, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.onSurface)) {
            Icon(Icons.Filled.RestartAlt, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Restart")
        }
        FilledTonalButton(onClick = onQuit, modifier = Modifier.weight(1f).heightIn(min = 48.dp),
            colors = ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.error,
                contentColor = MaterialTheme.colorScheme.onError)) {
            Icon(Icons.AutoMirrored.Filled.ExitToApp, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Quit")
        }
    }
}

/**
 * The 10 slots as save stickers (two columns): the slot's number big, the picture, when it was saved. Saving: every
 * slot can be picked; loading: only used ones, the Quick one (a Delta skin's quick save, 9) first. [refresh] changes
 * whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val all = remember(saving) { (if (saving) emptyList() else listOf(States.QUICK)) + (1..States.SLOTS) }
    val used = remember(refresh, all) { all.associateWith { states.used(it) } }
    val pictures = remember(refresh, all) { all.associateWith { if (used.getValue(it)) states.loadPicture(it) else null } }
    val dates = remember(refresh, all) {
        val day = DateFormat.getDateInstance(DateFormat.SHORT)
        val time = DateFormat.getTimeInstance(DateFormat.SHORT)
        all.associateWith { if (used.getValue(it)) Date(states.savedAt(it)).let { d -> day.format(d) to time.format(d) } else null }
    }
    val slots = all.filter { saving || used.getValue(it) }
    MenuLayer {
        Column(Modifier.padding(bottom = 12.dp)) {
            ScreenHeader(if (saving) "Save state" else "Load state", onBack = onBack,
                subtitle = if (saving) "Pick a slot" else null)
            Column(Modifier.verticalScroll(rememberScrollState())) {
                if (slots.isEmpty()) {
                    Text("No saved states yet. Use Save state first.", color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(12.dp))
                }
                for (pair in slots.chunked(2)) {
                    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(vertical = 6.dp)) {
                        for (slot in pair) SaveSticker(slot, pictures[slot], dates[slot], onPick, Modifier.weight(1f))
                        if (pair.size == 1) Spacer(Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

/** One slot as a save sticker: the picture (or a dashed Empty), the number in label lettering, the date and time. */
@Composable
private fun SaveSticker(slot: Int, picture: Bitmap?, date: Pair<String, String>?, onPick: (Int) -> Unit, modifier: Modifier) {
    val name = if (slot == States.QUICK) "Quick" else "Slot $slot"
    val outline = MaterialTheme.colorScheme.outline
    val pak = LocalPak.current
    val shape = RoundedCornerShape(12.dp)
    Column(modifier.moulded(shape, pak.cartHigh, pak.highlight, pak.shade).clip(shape)
        .clickable(onClickLabel = name) { onPick(slot) }.padding(7.dp)) {
        val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(6.dp))
        if (picture != null) {
            Image(picture.asImageBitmap(), contentDescription = name.lowercase(), modifier = frame,
                contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
        } else {
            Box(frame.drawBehind {
                drawRoundRect(outline, cornerRadius = CornerRadius(6.dp.toPx()),
                    style = Stroke(width = 1.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(10f, 8f))))
            }, contentAlignment = Alignment.Center) {
                Text("Empty", color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.labelMedium)
            }
        }
        Row(Modifier.fillMaxWidth().padding(start = 2.dp, top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(if (slot == States.QUICK) "Q" else "$slot", style = MaterialTheme.typography.displaySmall.merge(Tabular),
                modifier = Modifier.semantics { contentDescription = name })
            Spacer(Modifier.weight(1f))
            Column(horizontalAlignment = Alignment.End) {
                Text(date?.first ?: "", style = MaterialTheme.typography.bodySmall.merge(Tabular),
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
                Text(date?.second ?: "", style = MaterialTheme.typography.bodySmall.merge(Tabular),
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
            }
        }
    }
}
```

- [x] **Step 4: Replace `CheatPanel`** (the whole function with its comment) with:

```kotlin
/**
 * The game's cheats: one row each (name, on/off switch); a tap on the name edits it. Below: Add cheat, Import cheat
 * file and (with cheats) Remove all, which asks first. The list shrinks to leave the buttons room on a short screen.
 */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit, onImport: () -> Unit = {}, onRemoveAll: () -> Unit = {},
               onFindOnline: () -> Unit = {}) {
    var askRemoveAll by remember { mutableStateOf(false) }
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            ScreenHeader("Cheats", onBack = onBack, subtitle = title)
            Column(Modifier.weight(1f, fill = false)) {
                Column(Modifier.weight(1f, fill = false).heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                    if (cheats.isEmpty()) {
                        Text("No cheats yet. Add Action Replay, GameShark or CodeBreaker codes.",
                            color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(vertical = 8.dp))
                    }
                    if (cheats.isNotEmpty()) Group(body = LocalPak.current.cartHigh) {
                        cheats.forEachIndexed { i, c ->
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(16.dp),
                                    maxLines = 2, overflow = TextOverflow.Ellipsis)
                                Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 12.dp))
                            }
                            if (i < cheats.lastIndex) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        }
                    }
                }
                Spacer(Modifier.height(10.dp))
                Button(onClick = onAdd, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.Add, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Add cheat")
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onImport, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.FileUpload, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Import cheat file")
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onFindOnline, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Icon(Icons.Filled.CloudDownload, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Find cheats online")
                }
                if (cheats.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(onClick = { askRemoveAll = true }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)) {
                        Text("Remove all")
                    }
                }
            }
        }
    }
    if (askRemoveAll) {
        AlertDialog(
            onDismissRequest = { askRemoveAll = false },
            title = { Text(if (cheats.size == 1) "Remove the 1 cheat?" else "Remove all ${cheats.size} cheats?") },
            text = { Text("The cheats and their codes are removed from this game.") },
            confirmButton = { DangerButton("Remove") { askRemoveAll = false; onRemoveAll() } },
            dismissButton = { TextButton(onClick = { askRemoveAll = false }) { Text("Cancel") } },
        )
    }
}
```

- [x] **Step 5: Replace `SpeedLabel` and `SunControl`** (each whole function with its comment) with:

```kotlin
/** Show speed: "60 fps · 1.0×", measured from the frames the emulator ran, changing once a second. */
@Composable
private fun SpeedLabel(modifier: Modifier) {
    var label by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        val meter = SpeedMeter()
        while (true) {
            label = meter.sample(System.nanoTime(), NativeBridge.debugFrameCount())
            delay(250)
        }
    }
    label?.let {
        Text(it, color = Color.White, style = MaterialTheme.typography.labelMedium.merge(Tabular),
            modifier = modifier.padding(top = 6.dp).background(Scrim, RoundedCornerShape(50))
                .padding(horizontal = 12.dp, vertical = 4.dp))
    }
}

/** The sun button: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surfaceContainer.copy(alpha = 0.94f)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", modifier = Modifier.weight(1f))
                        Text("Auto", modifier = Modifier.padding(end = 8.dp))
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        FilledTonalIconButton(onClick = { open = !open }, modifier = Modifier.size(52.dp),
            colors = IconButtonDefaults.filledTonalIconButtonColors(containerColor = Scrim, contentColor = Color.White)) {
            Icon(Icons.Filled.WbSunny, contentDescription = "Light", modifier = Modifier.size(28.dp))
        }
    }
}
```

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh jvmtest` and `bash tools/dev.sh androidtest`, then the counts.
Expected: Kotlin `181 0`; on-device `78 0`.

- [x] **Step 7: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: the pause menu as a cartridge panel, save stickers, cheats and overlays in the Game Pak look" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Settings, the Skin page and the layout editor's outline

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` — `SettingsScreen` (rows grouped on plastic cards), `KeyTable` (a group), the skin tiles' ring (`chosenBorder`), import `FontWeight`
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt` — `selectColor` (the editor's outline, was amber)
- Modify: `app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt` — the outline in the bright accent

(No test can fail first: this is a change of look only; the existing Settings, Skin, Backup and BIOS screen tests guard that every row still does what it did.)

- [x] **Step 1: Replace `SettingsScreen`** (the whole function with its comment) with:

```kotlin
/**
 * Settings (from the library's gear and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 * [importedSkins]: the imported Delta skins (9); the chosen one ([Settings.skinImport]) sets the layout of the
 * directions it covers.
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit, onEditLayout: (sideways: Boolean) -> Unit = {},
                   importedSkins: List<ImportedSkin> = emptyList(), onImportSkin: () -> Unit = {},
                   onRemoveSkin: (Int) -> Unit = {}, backup: BackupStatus = BackupStatus(), onBackUp: () -> Unit = {},
                   onRestore: () -> Unit = {}, onUndoRestore: () -> Unit = {}, onAutoBackup: (Boolean) -> Unit = {},
                   onChangeFolder: () -> Unit = {}) {
    val s = settings
    val chosen = importedSkins.firstOrNull { it.number == s.skinImport } // an imported skin in use, or null
    val skinName = chosen?.name ?: Skins.at(s.skin).name
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            ScreenHeader(page.title, onBack = onBack)
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)
                .padding(bottom = 24.dp).widthIn(max = 640.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        Group {
                        NavRow(Icons.Filled.Image, "Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        NavRow(Icons.AutoMirrored.Filled.VolumeUp, "Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        NavRow(Icons.Filled.FastForward, "Fast-forward and rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        NavRow(Icons.Filled.SportsEsports, "Keyboard and controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        NavRow(Icons.Filled.TouchApp, "Touch buttons", "$skinName · ${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        NavRow(Icons.Filled.Memory, "BIOS", biosName) { onPage(SettingsPage.BIOS) }
                        NavRow(Icons.Filled.Backup, "Backup", backupSummary(backup), last = true) { onPage(SettingsPage.BACKUP) }
                        }
                        Group {
                            SwitchRow("Download box art", s.boxArt, { onChange(s.copy(boxArt = it)) },
                                help = "Pictures of game boxes from libretro, for the game list. Uses the internet.")
                        }
                    }
                    SettingsPage.PICTURE -> Group {
                        ChoiceRow("Pixels", listOf("Sharp", "Smooth"), if (s.smooth) 1 else 0, { onChange(s.copy(smooth = it == 1)) },
                            help = "Sharp keeps every GBA pixel crisp; Smooth softens the edges.")
                        ChoiceRow("Size", SIZE_SHORT, s.size, { onChange(s.copy(size = it)) },
                            help = "Whole steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                        SwitchRow("Show speed", s.showSpeed, { onChange(s.copy(showSpeed = it)) },
                            help = "Frames per second at the top of the game, and how many times real speed (1.0× = normal).")
                    }
                    SettingsPage.SOUND -> Group {
                        SwitchRow("Sound", s.sound, { onChange(s.copy(sound = it)) })
                        ChoiceRow("Delay", Settings.DELAY_NAMES, s.delay, { onChange(s.copy(delay = it)) },
                            help = "Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> Group {
                        ChoiceRow("Fast-forward speed", Settings.SPEEDS.map(Settings::speedName),
                            Settings.SPEEDS.indexOf(s.fastSpeed).coerceAtLeast(0), { onChange(s.copy(fastSpeed = Settings.SPEEDS[it])) },
                            help = "Max runs as fast as the phone can, without sound.")
                        SwitchRow("Rewind", s.rewind, { onChange(s.copy(rewind = it)) })
                        ChoiceRow("Rewind length", LENGTH_SHORT, s.rewindLength, { onChange(s.copy(rewindLength = it)) },
                            help = "Uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        Group { NavRow(Icons.Filled.Palette, "Skin", skinName, last = true) { onPage(SettingsPage.SKIN) } }
                        Group {
                        ChoiceRow("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook, { onChange(s.copy(touchLook = it)) },
                            help = "How strongly the buttons show over the game when the phone is sideways.")
                        SwitchRow("Buzz", s.buzz, { onChange(s.copy(buzz = it)) },
                            help = "Follows the phone's own touch-vibration setting.")
                        }
                        Group {
                        val own = "This skin sets its own layout"
                        val edit = "Move and size every button and the game picture"
                        NavRow(Icons.Filled.StayCurrentPortrait, "Edit upright layout", if (chosen?.upright == true) own else edit,
                            enabled = chosen?.upright != true) { onEditLayout(false) }
                        NavRow(Icons.Filled.StayCurrentLandscape, "Edit sideways layout", if (chosen?.sideways == true) own else edit,
                            enabled = chosen?.sideways != true, last = true) { onEditLayout(true) }
                        }
                    }
                    SettingsPage.SKIN -> {
                        SkinGrid(if (chosen != null) -1 else s.skin, importedSkins, chosen?.number,
                            onPick = { onChange(s.copy(skin = it, skinImport = 0)) },
                            onPickImported = { onChange(s.copy(skinImport = it)) }, onImport = onImportSkin, onRemove = onRemoveSkin)
                        Help("Import skins made for the Delta emulator (.deltaskin files). Press and hold an imported skin to remove it.")
                    }
                    SettingsPage.BACKUP -> {
                        Spacer(Modifier.height(8.dp))
                        if (backup.inGame) {
                            Text("Quit the game to back up or restore.", color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(vertical = 12.dp))
                        }
                        Button(onClick = onBackUp, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                            Text("Back up now")
                        }
                        Spacer(Modifier.height(8.dp))
                        OutlinedButton(onClick = onRestore, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                            Text("Restore from backup")
                        }
                        if (backup.canUndo) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedButton(onClick = onUndoRestore, enabled = !backup.inGame, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                                Text("Undo restore")
                            }
                        }
                        Help("One file with your saves, save states, settings, controls, cheats, imported skins and chosen box art. Not your BIOS file or the games.")
                        Spacer(Modifier.height(8.dp))
                        Group {
                            SwitchRow("Automatic backup", backup.auto, onAutoBackup,
                                help = "After you quit a game, at most once a day, a backup goes into a folder you pick. The newest 5 are kept.")
                            if (backup.auto) NavRow(Icons.Filled.Folder, "Change folder", backup.folderName ?: "The picked folder", last = true) { onChangeFolder() }
                        }
                        Help("Backups stay where you put them. The app doesn't send them anywhere, and Android's own backup is off for this app.")
                    }
                    SettingsPage.BIOS -> {
                        Group {
                            Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text("In use", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                                Text(biosName, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        Spacer(Modifier.height(12.dp))
                        Button(onClick = onChooseBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Choose BIOS file") }
                        if (s.biosFile) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Use built-in BIOS") }
                        }
                        Help("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                }
            }
        }
    }
}
```

- [x] **Step 2: Replace `KeyTable`** (the whole function with its comment) with:

```kotlin
/** Each GBA button (and fast-forward, rewind) with its keyboard key and controller button; a tap waits for the new one. */
@Composable
private fun KeyTable(keys: KeyBindings, keysChanged: Int, onPickKey: (Int, Int) -> Unit, onReset: () -> Unit) {
    val rows = remember(keysChanged) {
        KeyMap.bindings.map { b ->
            Triple(b, KeyBindings.keyName(keys.key(b.bit, KeyBindings.KEYBOARD)), KeyBindings.keyName(keys.key(b.bit, KeyBindings.CONTROLLER)))
        }
    }
    Help("Tap a key, then press the new one. A key already used moves here; its old button shows \"—\". The phone's touch buttons stay as they are.")
    Group(Modifier.padding(top = 8.dp)) {
        Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp, top = 14.dp, bottom = 6.dp)) {
            for (h in listOf("GBA", "Keyboard", "Controller")) {
                Text(h, Modifier.weight(1f), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        rows.forEachIndexed { i, (b, kbd, pad) ->
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(b.button, Modifier.weight(1f), fontWeight = FontWeight.Medium)
                TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
                TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
            }
            if (i < rows.lastIndex) HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant)
        }
    }
    Spacer(Modifier.height(16.dp))
    OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Reset to defaults") }
}
```

- [x] **Step 3: The skin tiles' ring** — in `ImportedTile` and `SkinTile` replace `border = if (chosen) BorderStroke(2.dp, MaterialTheme.colorScheme.primary) else null` with `border = chosenBorder(chosen)`; add `import androidx.compose.ui.text.font.FontWeight` before `import androidx.compose.ui.text.style.TextAlign`.

- [x] **Step 4: The editor's outline** — in `TouchControlsView.kt` replace `color = 0xFFF2A33A.toInt() // the app's amber (AppTheme)` with `color = AccentColors.dark(AccentColors.DEFAULT_SEED).color // the editor's outline: set from the theme's accent`, and before `private val rim = Paint(` add:

```kotlin
    /** The layout editor's selection outline: the app's accent (12). */
    var selectColor: Int
        get() = outline.color
        set(v) {
            if (outline.color != v) { outline.color = v; invalidate() }
        }
```

In `LayoutEditor.kt` add `import androidx.compose.ui.graphics.toArgb`; before `BoxWithConstraints(Modifier.fillMaxSize().background(Color(skin.body))) {` add `val accent = LocalBrightAccent.current.toArgb()` (the bright form: the outline is drawn over the skin's body, dark or light); in the `update` block after `v.onEdit = { edits = it }` add `v.selectColor = accent`.

- [x] **Step 5: Run the tests**

Run: `bash tools/dev.sh jvmtest` and `bash tools/dev.sh androidtest`, then the counts.
Expected: Kotlin `181 0`; on-device `78 0`.

- [x] **Step 6: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: Settings grouped on plastic cards; the Skin page's ring and the editor's outline in the accent" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: The app icon and the launch screen

**Files:**
- Create: `app/src/main/res/drawable/ic_launcher_background.xml`, `ic_launcher_foreground.xml`, `ic_launcher_monochrome.xml`; `app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`, `ic_launcher_round.xml`; `app/src/main/res/values/colors.xml`, `themes.xml`; `app/src/main/res/values-night/colors.xml`, `themes.xml`; `app/src/main/res/values-v31/themes.xml`
- Modify: `app/src/main/AndroidManifest.xml` — `android:icon`, `android:roundIcon`, `android:theme="@style/Theme.GbaEmu"`
- Test: `app/src/test/java/com/tengen/gbaemu/AppIconTest.kt` (new)

(minSdk is 26, so an adaptive icon in vectors covers every phone: no PNGs. The icon is original artwork — a grey cartridge with a lip, a black label band, a pale label with a play mark, grip ridges, on indigo — no logos.)

- [x] **Step 1: Write the failing test** — `app/src/test/java/com/tengen/gbaemu/AppIconTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

/** The app has its own icon (a cartridge) and launch theme (12), instead of Android's default robot. */
class AppIconTest {
    private fun read(path: String) = File("src/main/$path").readText() // tests run in app/

    @Test
    fun theManifestUsesTheCartridgeIconAndTheLaunchTheme() {
        val manifest = read("AndroidManifest.xml")
        for (attr in listOf("android:icon=\"@mipmap/ic_launcher\"", "android:roundIcon=\"@mipmap/ic_launcher_round\"",
            "android:theme=\"@style/Theme.GbaEmu\"")) assertTrue(attr, manifest.contains(attr))
    }

    @Test
    fun theIconHasEveryLayerAndTheThemeEveryVariant() {
        for (name in listOf("ic_launcher", "ic_launcher_round")) {
            val icon = read("res/mipmap-anydpi-v26/$name.xml")
            for (layer in listOf("background", "foreground", "monochrome")) {
                assertTrue("$name: $layer", icon.contains("@drawable/ic_launcher_$layer"))
                assertTrue(File("src/main/res/drawable/ic_launcher_$layer.xml").exists())
            }
        }
        for (dir in listOf("values", "values-night", "values-v31")) assertTrue(dir, read("res/$dir/themes.xml").contains("Theme.GbaEmu"))
        for (dir in listOf("values", "values-night")) assertTrue(dir, read("res/$dir/colors.xml").contains("name=\"plastic\""))
    }
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest`
Expected: FAIL — both tests (`android:icon=…` missing; `FileNotFoundException` for `res/mipmap-anydpi-v26/ic_launcher.xml`).

- [x] **Step 3: Write the resources**

`app/src/main/res/drawable/ic_launcher_background.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- The icon's ground: the default accent's indigo (12). -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">
    <path android:fillColor="#4B3FB8" android:pathData="M0,0h108v108h-108z" />
</vector>
```

`app/src/main/res/drawable/ic_launcher_foreground.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- The icon (12): a grey cartridge — its lip, a black label band, a pale label with a play mark, grip ridges.
     Original artwork, no logos. Inside the 66 dp safe zone of the 108 dp adaptive icon. -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">
    <!-- shade under the body -->
    <path android:fillColor="#2C2486"
        android:pathData="M33,32.5h42a2,2 0,0 1,2 2v2.5h1a5,5 0,0 1,5 5v34.5a5,5 0,0 1,-5 5h-48a5,5 0,0 1,-5 -5v-34.5a5,5 0,0 1,5 -5h1v-2.5a2,2 0,0 1,2 -2z" />
    <!-- body with its lip -->
    <path android:fillColor="#C5C7CC"
        android:pathData="M33,30.5h42a2,2 0,0 1,2 2v2.5h1a5,5 0,0 1,5 5v34.5a5,5 0,0 1,-5 5h-48a5,5 0,0 1,-5 -5v-34.5a5,5 0,0 1,5 -5h1v-2.5a2,2 0,0 1,2 -2z" />
    <!-- label -->
    <path android:fillColor="#F2F2F4" android:pathData="M31,40h46a1.5,1.5 0,0 1,1.5 1.5v24a1.5,1.5 0,0 1,-1.5 1.5h-46a1.5,1.5 0,0 1,-1.5 -1.5v-24a1.5,1.5 0,0 1,1.5 -1.5z" />
    <!-- label band -->
    <path android:fillColor="#141518" android:pathData="M31,40h46a1.5,1.5 0,0 1,1.5 1.5v5.5h-49v-5.5a1.5,1.5 0,0 1,1.5 -1.5z" />
    <!-- play mark -->
    <path android:fillColor="#4B3FB8" android:pathData="M50,50.5l9.5,6l-9.5,6z" />
    <!-- grip ridges -->
    <path android:fillColor="#A3A5AB" android:pathData="M37,71h34v1.2h-34z M37,74h34v1.2h-34z" />
</vector>
```

`app/src/main/res/drawable/ic_launcher_monochrome.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- Android 13's themed icon: the cartridge's outline with its label cut out. -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">
    <path android:fillColor="#FFFFFFFF" android:fillType="evenOdd"
        android:pathData="M33,30.5h42a2,2 0,0 1,2 2v2.5h1a5,5 0,0 1,5 5v34.5a5,5 0,0 1,-5 5h-48a5,5 0,0 1,-5 -5v-34.5a5,5 0,0 1,5 -5h1v-2.5a2,2 0,0 1,2 -2z M31,47h46v18.5h-46z M50,50.5l9.5,6l-9.5,6z" />
</vector>
```

`app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml` and `ic_launcher_round.xml` (the same):

```xml
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/ic_launcher_background" />
    <foreground android:drawable="@drawable/ic_launcher_foreground" />
    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />
</adaptive-icon>
```

`app/src/main/res/values/colors.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- The plastic every screen is made of (12): grey by day; values-night has the smoked plastic. -->
    <color name="plastic">#C5C7CC</color>
</resources>
```

`app/src/main/res/values-night/colors.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="plastic">#141518</color>
</resources>
```

`app/src/main/res/values/themes.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- The window behind the app while it starts (12): the plastic, its bars to match. Compose draws everything else. -->
    <style name="Theme.GbaEmu.Base" parent="android:Theme.Material.Light.NoActionBar">
        <item name="android:windowBackground">@color/plastic</item>
        <item name="android:statusBarColor">@color/plastic</item>
        <item name="android:navigationBarColor">@color/plastic</item>
        <item name="android:windowLightStatusBar">true</item>
    </style>
    <style name="Theme.GbaEmu" parent="Theme.GbaEmu.Base" />
</resources>
```

`app/src/main/res/values-night/themes.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="Theme.GbaEmu.Base" parent="android:Theme.Material.NoActionBar">
        <item name="android:windowBackground">@color/plastic</item>
        <item name="android:statusBarColor">@color/plastic</item>
        <item name="android:navigationBarColor">@color/plastic</item>
        <item name="android:windowLightStatusBar">false</item>
    </style>
</resources>
```

`app/src/main/res/values-v31/themes.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- Android 12+: the launch screen shows the cartridge icon on the plastic. -->
    <style name="Theme.GbaEmu" parent="Theme.GbaEmu.Base">
        <item name="android:windowSplashScreenBackground">@color/plastic</item>
        <item name="android:windowSplashScreenIconBackgroundColor">#4B3FB8</item>
    </style>
</resources>
```

- [x] **Step 4: The manifest** — in `<application`, after `android:label="GBA Emulator"` add `android:icon="@mipmap/ic_launcher"` and `android:roundIcon="@mipmap/ic_launcher_round"`; replace `android:theme="@android:style/Theme.Material.NoActionBar"` with `android:theme="@style/Theme.GbaEmu"`.

- [x] **Step 5: Run the tests and look**

Run: `bash tools/dev.sh jvmtest`, `bash tools/dev.sh install`, then `adb shell am force-stop com.tengen.gbaemu` and `bash tools/dev.sh launch` with a screenshot about 0.5 s after the launch (`adb exec-out screencap -p`): the cartridge icon on the plastic.
Expected: Kotlin `183 0` (181 + 2); the splash shows the cartridge.

- [x] **Step 6: Commit** (with `docs/PROGRESS.md` and this plan ticked)

```bash
git add app/src/main/AndroidManifest.xml app/src/main/res app/src/test/java/com/tengen/gbaemu/AppIconTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-05-milestone-12-game-pak-ui.md
git commit -m "feat: the cartridge app icon and the plastic launch screen" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Claude's check, the finish review and DESIGN.md

**Files:** `.impeccable/review/*.png` (new), `DESIGN.md` + `.impeccable/design.json` (new, by the documenter), any fixes the review asks for; docs.

- [x] **Step 1: Full test runs** — `bash tools/dev.sh test` (CTest **120/120**, the core untouched), `bash tools/dev.sh jvmtest` (Kotlin **183 0**; after the review's fixes **186 0**), `bash tools/dev.sh androidtest` (on-device **78 0**; the known flake → rerun once).

- [x] **Step 2: Screenshots on the emulator** (`bash tools/dev.sh install`; `adb exec-out screencap -p > .impeccable/review/<name>.png`), one batched round: the launch splash; the game list light and dark (`adb shell cmd uimode night yes|no`), upright and sideways (`settings put system accelerometer_rotation 0`, `user_rotation 1`), and at `font_scale 1.3`; the insert caught mid-way (a screenshot ~0.12 s after tapping Continue); the pause menu light/dark/sideways; save state and load state; cheats with the add dialog; the game sheet; Settings main, Picture, Sound, Fast-forward and rewind, Keyboard and controller, Touch buttons, Skin, BIOS, Backup; the layout editor (its outline); a dialog (Restart). The first start and the empty folder can't be reached without unsetting the user's folder: capture them with a temporary instrumented test that composes `FirstStartScreen` / an empty `LibraryScreen` and saves `compose.onRoot().captureToImage().asAndroidBitmap()` to the test app's cache, `adb pull` them, then delete the temporary test (not committed). Put every emulator setting back (light, auto-rotate, 1.0). Name the files `phone-<screen>-<light|dark>[-side].png`; the main ones also as `phone.png` (game list, light) and `phone-dark.png`.

- [x] **Step 3: Fix what the screenshots show** in one batch (cut-off text, overlaps, a state that reads wrong), rebuild, recapture only what changed. At most one more round.

- [x] **Step 4: The finish review** — spawn the shipped `impeccable:impeccable-finish-reviewer` agent, fresh (no conversation history), with: the request ("overhaul the whole app's UI with the impeccable skill"), the user's confirmed answers (spec §1), the artifact paths (`AppTheme.kt`, `GamePak.kt`, `AccentColors.kt`, `Screens.kt`, `SettingsScreens.kt`, `res/`), the screenshot paths from Step 2 (all required), the direction contract (`impeccable surface-brief read app/src/main/java/com/tengen/gbaemu/Screens.kt`), the QUALITY BAR: none (a code-led build with no comps — the user picked the card without images), the craft-floor reference path, the Android platform reference path (`reference/android.md` in the skill folder), and one line: "native Android app: no detector ran; judge in Material 3 / Android conventions". Act on its disposition: `fix` → one batch of fixes, recapture the same files, send them back for a verdict on each fix; `recapture` → recapture and a full review; `rebuild` → tell the user, then rebuild the named regions; `ship` → go on. Two rounds at most, then put any open items in front of the user in Task 8.

- [x] **Step 5: DESIGN.md** — spawn the shipped `impeccable:impeccable-documenter` agent with the project root, the artifact paths, the direction contract, `PRODUCT.md`, the skill's `reference/document.md`, and the write boundary (`DESIGN.md`, `.impeccable/design.json` only). Check both files exist and carry the tokens (the palette roles, the accent rule, type, shapes, the cartridge pieces).

- [x] **Step 6: Docs** — the spec's status line (implemented apart from the user's check) and any decision the review changed; the parent spec's status line and §10 row 12; `CLAUDE.md`'s key files; `PROGRESS.md` (Task 7's Completed entry with the counts, the review's verdict, what was fixed); this plan ticked. Check that nothing is stale.

- [x] **Step 7: Commit**

```bash
git add -A
git commit -m "docs: milestone 12 checked — screenshots, the finish review, DESIGN.md" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The user's check and milestone 12 exit

- [x] **Step 1: Stop for the user's check** — the app on the emulator (`bash tools/dev.sh install`): the game list, Continue and the insert, the pause menu, save and load, cheats, Settings, a skin's accent, light and dark, sideways, the icon. Offer their phones too (`bash tools/dev.sh phone-install`, only if they say so). Report the finish review's verdict at its scope, and any open items.

- [x] **Step 2: Fix what they find** (test-first where it is behaviour), one commit per fix.

- [x] **Step 3: Exit** — tag `milestone-12`; mark the spec implemented, the parent spec's §10 row 12 done, `CLAUDE.md` key files (no current milestone), `PROGRESS.md` (status ✅, Completed, Next: ask the user what's next). Ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test`, `jvmtest` and `androidtest` on `main`; record the merge. With the user's OK delete the branches `proto-12` and `milestone-12`.
