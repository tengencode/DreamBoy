# Milestone 6c — Touch Layout Editor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** A layout editor for the touch buttons and the game picture: move anything with one finger, tap to pick and size it ½×–2× with a slider, upright and sideways layouts separately, Reset all / Cancel / Save; the game screen uses the layout (buttons over the picture see-through).

**Architecture:** App code only (the core and the C platform layer do not change). `TouchLayout` becomes data over the whole screen (`LayoutItem`, `Place`; the defaults are 4c's positions); `LayoutStore` keeps the edits in `files/layout.json`; `TouchControlsView` draws from the layout (and edits it in the editor); `GameScreen` places the game's surface at the layout's picture box; `LayoutEditor` is the editor screen, opened from Settings → Touch buttons; `MainActivity` saves and turns the screen.

**Tech Stack:** Kotlin + Jetpack Compose, Android Views (Canvas), JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-10-02-milestone-6c-layout-editor-design.md`

---

## Conventions (same as 6b)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test [ctest args]|run>` (a fresh worktree needs `configure` first). Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse without an emulator. `androidtest` leaves the app and its saves installed. Never uninstall the app, `pm clear` it or delete its files or preferences (except the test files named in a step, and the layout only as the check in Task 4 says, which puts it back) without the user's OK. Ask before restarting/cold-booting the emulator.
- `adb shell run-as <pkg> sh -c "…"` does not work (adb re-splits the words): plain `run-as <pkg> <command>` calls; adb passes the exit status back.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-6c` (already created; the spec commits are on it). The tasks run one after another without stopping (`CLAUDE.md`); the only stop is Task 5, the user's check and the 6c exit. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p6c`, branch `proto-6c`), which the user tried on 2026-10-02 ('Everything works').

## File map

```
app/src/main/java/com/tengen/gbaemu/TouchLayout.kt; app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt                                  Task 1
app/src/main/java/com/tengen/gbaemu/LayoutStore.kt (new); app/src/test/java/com/tengen/gbaemu/LayoutStoreTest.kt (new)                      Task 2
app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt, Screens.kt                                                             Task 3
app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt (new), SettingsScreens.kt, MainActivity.kt                                   Task 4
```

---
### Task 1: The touch layout as data (moved and sized items over the whole screen)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`, `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`

- [x] **Step 1: Write the failing tests**

Replace `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutTest {
    private val density = 2.625f // Pixel 9
    private val upright = TouchLayout(1080f, 1700f, density, sideways = false)
    private val sideways = TouchLayout(2400f, 1080f, density, sideways = true)
    private val delta = 0.01f

    private fun TouchLayout.button(bit: Int) = buttons.first { it.bit == bit }

    @Test
    fun dpadGivesEightDirections() {
        val d = TouchLayout.Companion
        assertEquals(KeyMap.RIGHT, d.dpadDirection(10f, 0f))
        assertEquals(KeyMap.RIGHT or KeyMap.UP, d.dpadDirection(10f, -10f))
        assertEquals(KeyMap.UP, d.dpadDirection(0f, -10f))
        assertEquals(KeyMap.UP or KeyMap.LEFT, d.dpadDirection(-10f, -10f))
        assertEquals(KeyMap.LEFT, d.dpadDirection(-10f, 0f))
        assertEquals(KeyMap.LEFT or KeyMap.DOWN, d.dpadDirection(-10f, 10f))
        assertEquals(KeyMap.DOWN, d.dpadDirection(0f, 10f))
        assertEquals(KeyMap.DOWN or KeyMap.RIGHT, d.dpadDirection(10f, 10f))
        assertEquals(KeyMap.UP, d.dpadDirection(3f, -10f)) // 17° off up: still up
    }

    @Test
    fun dpadMiddleIsDeadAndItsEdgeWorks() {
        for (l in listOf(upright, sideways)) {
            assertEquals(0, l.buttonsAt(l.dpadX, l.dpadY))
            assertEquals(0, l.buttonsAt(l.dpadX + l.dpadRadius * 0.2f, l.dpadY))
            assertEquals(KeyMap.RIGHT, l.buttonsAt(l.dpadX + l.dpadRadius * 0.9f, l.dpadY))
            assertEquals(KeyMap.UP, l.buttonsAt(l.dpadX, l.dpadY - l.dpadRadius))
        }
    }

    @Test
    fun theMiddleOfEveryButtonPressesIt() {
        for (l in listOf(upright, sideways)) {
            assertEquals(8, l.buttons.size) // 6 GBA buttons, rewind, fast-forward
            for (b in l.buttons) assertEquals("${b.label} sideways=${l.sideways}", b.bit, l.buttonsAt(b.cx, b.cy))
        }
    }

    @Test
    fun theGapBetweenAAndBPressesNothing() {
        for (l in listOf(upright, sideways)) {
            val a = l.button(KeyMap.A)
            val b = l.button(KeyMap.B)
            assertEquals("sideways=${l.sideways}", 0, l.buttonsAt((a.cx + b.cx) / 2, (a.cy + b.cy) / 2))
        }
    }

    @Test
    fun buttonsStayInsideTheScreen() {
        for (l in listOf(upright, sideways)) {
            for (b in l.buttons) {
                assertTrue("${b.label}", b.left >= 0f && b.top >= 0f && b.right <= l.width && b.bottom <= l.height)
            }
            assertTrue(l.dpadX - l.dpadRadius >= 0f && l.dpadY + l.dpadRadius <= l.height)
        }
    }

    @Test
    fun farFromEverythingPressesNothing() {
        assertEquals(0, upright.buttonsAt(upright.width / 2, upright.picture.bottom / 2)) // the middle of the game
        assertEquals(0, sideways.buttonsAt(sideways.width / 2, sideways.height / 2))
    }

    @Test
    fun withNothingEditedTheLayoutIsTodays() {
        // upright: the picture full width at the top (3:2), the buttons where 4c put them in the area below it
        assertEquals(TouchLayout.Box(0f, 0f, 1080f, 720f), upright.picture)
        val a = upright.button(KeyMap.A) // 4c: area 1080×980, A at (1080 − 16dp − 36dp, dpadY − 28dp)
        assertEquals(943.5f, a.cx, delta)
        assertEquals(720f + 427f, a.cy, delta)
        assertEquals(189f, a.right - a.left, delta) // 72 dp
        // sideways: the picture's box is the whole screen (the game is fitted inside it), the buttons as in 4c
        assertEquals(TouchLayout.Box(0f, 0f, 2400f, 1080f), sideways.picture)
        val sa = sideways.button(KeyMap.A)
        assertEquals(2400f - 42f - 78.75f, sa.cx, delta)
        assertEquals(1080f * 0.45f, sa.cy, delta)
    }

    @Test
    fun aMovedButtonIsPressedAtItsNewPlaceAndNotTheOld() {
        val old = upright.button(KeyMap.A)
        val moved = upright.with(LayoutItem.A, Place(0.25f, 0.9f))
        val a = moved.button(KeyMap.A)
        assertEquals(270f, a.cx, delta)
        assertEquals(1530f, a.cy, delta)
        assertEquals(KeyMap.A, moved.buttonsAt(a.cx, a.cy))
        assertEquals(0, moved.buttonsAt(old.cx, old.cy))
    }

    @Test
    fun aMovedDpadWorksWhereItIs() {
        val moved = sideways.with(LayoutItem.DPAD, Place(0.5f, 0.5f, 1.5f))
        assertEquals(1200f, moved.dpadX, delta)
        assertEquals(540f, moved.dpadY, delta)
        assertEquals(sideways.dpadRadius * 1.5f, moved.dpadRadius, delta)
        assertEquals(KeyMap.UP, moved.buttonsAt(1200f, 540f - moved.dpadRadius * 0.8f))
    }

    @Test
    fun movingKeepsThingsInsideTheScreenAndTheSizeWithinHalfToDouble() {
        val edge = upright.with(LayoutItem.A, Place(1.3f, -0.4f))
        val a = edge.button(KeyMap.A)
        assertEquals(1080f, a.right, delta)
        assertEquals(0f, a.top, delta)
        val big = upright.with(LayoutItem.B, Place(0.5f, 0.8f, 5f))
        assertEquals(2f, big.place(LayoutItem.B).scale, delta)
        assertEquals(378f, big.button(KeyMap.B).right - big.button(KeyMap.B).left, delta) // 2 × 72 dp, still round
        val small = upright.with(LayoutItem.START, Place(0.5f, 0.8f, 0.1f))
        val s = small.button(KeyMap.START)
        assertEquals(0.5f, small.place(LayoutItem.START).scale, delta)
        assertEquals(105f, s.right - s.left, delta) // half of 80 dp
        assertEquals(42f, s.bottom - s.top, delta) // half of 32 dp: the shape kept
    }

    @Test
    fun thePictureCanBeMovedAndShrunk() {
        val l = upright.with(LayoutItem.PICTURE, Place(0.5f, 0.25f, 0.5f))
        assertEquals(TouchLayout.Box(270f, 245f, 810f, 605f), l.picture)
        val tooBig = sideways.with(LayoutItem.PICTURE, Place(0.9f, 0.5f, 1.5f)) // bigger than the screen: no bigger
        assertEquals(TouchLayout.Box(0f, 0f, 2400f, 1080f), tooBig.picture)
    }

    @Test
    fun buttonsOverThePictureAreSeeThrough() {
        assertTrue(upright.buttons.none { upright.overPicture(it) }) // on black below the picture: solid
        assertFalse(upright.dpadOverPicture)
        assertTrue(sideways.buttons.all { sideways.overPicture(it) }) // over the whole-screen box: see-through
        val moved = upright.with(LayoutItem.A, Place(0.5f, 0.2f))
        assertTrue(moved.overPicture(moved.button(KeyMap.A)))
        assertFalse(moved.overPicture(moved.button(KeyMap.B)))
    }

    @Test
    fun theEditorFindsTheItemUnderAFinger() {
        val a = upright.button(KeyMap.A)
        assertEquals(LayoutItem.A, upright.itemAt(a.cx, a.cy))
        assertEquals(LayoutItem.DPAD, upright.itemAt(upright.dpadX, upright.dpadY))
        assertEquals(LayoutItem.PICTURE, upright.itemAt(540f, 300f))
        assertNull(upright.itemAt(540f, 1690f)) // black, below Select and Start
        assertEquals(LayoutItem.A, sideways.itemAt(sideways.button(KeyMap.A).cx, sideways.button(KeyMap.A).cy)) // buttons before the picture
    }

    @Test
    fun aPlaceIsWhereTheItemIsNow() {
        val a = upright.button(KeyMap.A)
        val p = upright.place(LayoutItem.A)
        assertEquals(a.cx / 1080f, p.x, 0.0001f)
        assertEquals(a.cy / 1700f, p.y, 0.0001f)
        assertEquals(1f, p.scale, 0.0001f)
        val again = upright.with(LayoutItem.A, p).button(KeyMap.A) // nothing moves
        assertEquals(a.cx, again.cx, delta)
        assertEquals(a.cy, again.cy, delta)
        assertEquals(a.right - a.left, again.right - again.left, delta)
    }
}
```

(One old test changed: `farFromEverythingPressesNothing` now presses the middle of the picture — on the whole screen the old point, the middle of 1080 × 1700, is at ⟲'s edge.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'Box'`, `'LayoutItem'`, `'Place'`, `'dpadOverPicture'`, `'itemAt'`, `'overPicture'`, `'picture'`, `'place'`, `'with'`.

- [x] **Step 3: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/** What the layout editor can move: the game picture, the D-pad (as one) and each button. */
enum class LayoutItem(val id: String, val label: String) {
    PICTURE("picture", "Game picture"), DPAD("dpad", "D-pad"), L("l", "L"), R("r", "R"), REWIND("rewind", "⟲ Rewind"),
    FAST("fast", "⏩ Fast-forward"), A("a", "A"), B("b", "B"), SELECT("select", "Select"), START("start", "Start");

    companion object {
        fun byId(id: String): LayoutItem? = entries.firstOrNull { it.id == id }
    }
}

/** An edited item: its centre as shares of the screen's width and height (0..1) and its size (1 = the default). */
data class Place(val x: Float, val y: Float, val scale: Float = 1f)

/**
 * Where the game picture and the touch buttons are, in pixels, on a [width] × [height] screen. With no [edits] it is
 * the layout of 4c: upright (layout A) the picture full width at the top (3:2) and the buttons in the area below it;
 * sideways the picture's box is the whole screen (the game fitted inside) and the buttons over the side bars —
 * ⟲ rewind and ⏩ fast-forward (KeyMap.REWIND / FAST, not GBA buttons) upright between L and R, sideways just below L
 * and R. [edits] (the layout editor, 6c) move items anywhere and size them ½×–2×, always inside the screen.
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, private val density: Float, val sideways: Boolean,
                  val edits: Map<LayoutItem, Place> = emptyMap()) {
    data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2

        fun overlaps(o: Box) = left < o.right && o.left < right && top < o.bottom && o.top < bottom
    }

    /** One button: [round] ones are the circle inside the box, the others a rounded box; [scale] its size. */
    data class Button(val bit: Int, val label: String, val round: Boolean,
                      val left: Float, val top: Float, val right: Float, val bottom: Float, val scale: Float = 1f) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2
        val box get() = Box(left, top, right, bottom)
    }

    private val dp = density
    private val slop = 8 * dp // fingers are imprecise: a button reacts a little outside its drawing

    val picture: Box
    val dpadX: Float
    val dpadY: Float
    val dpadRadius: Float
    val buttons: List<Button>
    private val scales = HashMap<LayoutItem, Float>()

    init {
        val defaults = defaultBoxes()
        picture = placed(LayoutItem.PICTURE, defaults.getValue(LayoutItem.PICTURE))
        val dpad = placed(LayoutItem.DPAD, defaults.getValue(LayoutItem.DPAD))
        dpadX = dpad.cx
        dpadY = dpad.cy
        dpadRadius = (dpad.right - dpad.left) / 2
        buttons = BUTTONS.map { (item, bit, label) ->
            val b = placed(item, defaults.getValue(item))
            Button(bit, label, bit == KeyMap.A || bit == KeyMap.B, b.left, b.top, b.right, b.bottom, scales.getValue(item))
        }
    }

    /** 4c's boxes: upright the area below the picture, sideways the whole screen. */
    private fun defaultBoxes(): Map<LayoutItem, Box> {
        val m = 16 * dp
        val boxes = HashMap<LayoutItem, Box>()
        if (!sideways) {
            val top = width * 2 / 3 // the picture: full width, 3:2, at the top
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, top)
            val h = height - top // the controls area below it
            val shoulderH = 40 * dp
            val smallW = 80 * dp
            val smallH = 32 * dp
            val smallTop = h - m - smallH
            val r = 70 * dp
            val dx = m + r
            val dy = (m + shoulderH + smallTop) / 2
            boxes[LayoutItem.DPAD] = Box(dx - r, top + dy - r, dx + r, top + dy + r)
            val ar = 36 * dp
            val ax = width - m - ar
            val ay = dy - 28 * dp
            val bx = ax - 84 * dp
            val by = ay + 56 * dp
            val extraW = 64 * dp
            val extraTop = m + 4 * dp
            val extraBottom = m + shoulderH - 4 * dp
            boxes[LayoutItem.L] = Box(0f, top + m, 96 * dp, top + m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, top + m, width, top + m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(width / 2 - 8 * dp - extraW, top + extraTop, width / 2 - 8 * dp, top + extraBottom)
            boxes[LayoutItem.FAST] = Box(width / 2 + 8 * dp, top + extraTop, width / 2 + 8 * dp + extraW, top + extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, top + ay - ar, ax + ar, top + ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, top + by - ar, bx + ar, top + by + ar)
            boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, top + smallTop, width / 2 - 8 * dp, top + smallTop + smallH)
            boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, top + smallTop, width / 2 + 8 * dp + smallW, top + smallTop + smallH)
        } else {
            boxes[LayoutItem.PICTURE] = Box(0f, 0f, width, height) // the game is fitted inside
            val shoulderH = 40 * dp
            val smallW = 72 * dp
            val smallH = 28 * dp
            val smallTop = height - m - smallH
            val r = 60 * dp
            val dx = m + r
            val dy = height * 0.55f
            boxes[LayoutItem.DPAD] = Box(dx - r, dy - r, dx + r, dy + r)
            val ar = 30 * dp
            val ax = width - m - ar
            val ay = height * 0.45f
            val bx = ax - 66 * dp
            val by = ay + 52 * dp
            val extraTop = m + shoulderH + 12 * dp
            val extraBottom = extraTop + 32 * dp
            boxes[LayoutItem.L] = Box(0f, m, 96 * dp, m + shoulderH)
            boxes[LayoutItem.R] = Box(width - 96 * dp, m, width, m + shoulderH)
            boxes[LayoutItem.REWIND] = Box(16 * dp, extraTop, 80 * dp, extraBottom)
            boxes[LayoutItem.FAST] = Box(width - 80 * dp, extraTop, width - 16 * dp, extraBottom)
            boxes[LayoutItem.A] = Box(ax - ar, ay - ar, ax + ar, ay + ar)
            boxes[LayoutItem.B] = Box(bx - ar, by - ar, bx + ar, by + ar)
            boxes[LayoutItem.SELECT] = Box(width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH)
            boxes[LayoutItem.START] = Box(width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH)
        }
        return boxes
    }

    /** [default] moved and sized by the item's edit, kept inside the screen (never bigger than it). */
    private fun placed(item: LayoutItem, default: Box): Box {
        val e = edits[item]
        val w0 = default.right - default.left
        val h0 = default.bottom - default.top
        val s = min(clampScale(e?.scale ?: 1f), min(width / w0, height / h0))
        scales[item] = s
        val w = w0 * s
        val h = h0 * s
        val cx = (e?.x?.times(width) ?: default.cx).coerceIn(w / 2, width - w / 2)
        val cy = (e?.y?.times(height) ?: default.cy).coerceIn(h / 2, height - h / 2)
        return Box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    }

    /** The same layout with [item] at [place] (its size brought into ½–2). */
    fun with(item: LayoutItem, place: Place): TouchLayout =
        TouchLayout(width, height, density, sideways, edits + (item to place.copy(scale = clampScale(place.scale))))

    /** Where [item] is now, as a Place (for the editor). */
    fun place(item: LayoutItem): Place {
        val b = box(item)
        return Place(b.cx / width, b.cy / height, scales.getValue(item))
    }

    fun box(item: LayoutItem): Box = when (item) {
        LayoutItem.PICTURE -> picture
        LayoutItem.DPAD -> Box(dpadX - dpadRadius, dpadY - dpadRadius, dpadX + dpadRadius, dpadY + dpadRadius)
        else -> buttons[BUTTONS.indexOfFirst { it.first == item }].box
    }

    /** Over the game picture: drawn see-through; on black: solid. */
    fun overPicture(b: Button) = b.box.overlaps(picture)
    val dpadOverPicture get() = box(LayoutItem.DPAD).overlaps(picture)

    /** The editor: the item under a finger at (x, y) — a button, else the D-pad, else the picture — or null. */
    fun itemAt(x: Float, y: Float): LayoutItem? {
        buttons.forEachIndexed { i, b -> if (inside(b, x, y, 0f)) return BUTTONS[i].first }
        if (hypot(x - dpadX, y - dpadY) <= dpadRadius) return LayoutItem.DPAD
        if (x >= picture.left && x <= picture.right && y >= picture.top && y <= picture.bottom) return LayoutItem.PICTURE
        return null
    }

    /** The GBA buttons a finger at (x, y) presses: D-pad directions, or the nearest button it touches, or 0. */
    fun buttonsAt(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val dist = hypot(dx, dy)
        if (dist <= dpadRadius + slop) return if (dist < dpadRadius * DEAD_ZONE) 0 else dpadDirection(dx, dy)
        var best = 0
        var bestDist = Float.MAX_VALUE
        for (b in buttons) {
            if (!inside(b, x, y, slop)) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDist) {
                best = b.bit
                bestDist = d
            }
        }
        return best
    }

    private fun inside(b: Button, x: Float, y: Float, margin: Float): Boolean =
        if (b.round) hypot(x - b.cx, y - b.cy) <= (b.right - b.left) / 2 + margin
        else x >= b.left - margin && x <= b.right + margin && y >= b.top - margin && y <= b.bottom + margin

    companion object {
        const val DEAD_ZONE = 0.25f // no direction in the middle quarter of the D-pad's radius
        const val MIN_SCALE = 0.5f
        const val MAX_SCALE = 2f

        /** The buttons in drawing order: the item, its bit, its label. */
        private val BUTTONS = listOf(
            Triple(LayoutItem.L, KeyMap.L, "L"), Triple(LayoutItem.R, KeyMap.R, "R"),
            Triple(LayoutItem.REWIND, KeyMap.REWIND, "⟲"), Triple(LayoutItem.FAST, KeyMap.FAST, "⏩"),
            Triple(LayoutItem.A, KeyMap.A, "A"), Triple(LayoutItem.B, KeyMap.B, "B"),
            Triple(LayoutItem.SELECT, KeyMap.SELECT, "SELECT"), Triple(LayoutItem.START, KeyMap.START, "START"),
        )

        fun clampScale(s: Float) = s.coerceIn(MIN_SCALE, MAX_SCALE)

        /** 8 sectors of 45°, from the D-pad's centre (screen y grows downwards): diagonals press two directions. */
        fun dpadDirection(dx: Float, dy: Float): Int {
            val angle = Math.toDegrees(atan2(-dy, dx).toDouble()) // 0 = right, 90 = up
            return when (((angle / 45.0).roundToInt() + 8) % 8) {
                0 -> KeyMap.RIGHT
                1 -> KeyMap.RIGHT or KeyMap.UP
                2 -> KeyMap.UP
                3 -> KeyMap.UP or KeyMap.LEFT
                4 -> KeyMap.LEFT
                5 -> KeyMap.LEFT or KeyMap.DOWN
                6 -> KeyMap.DOWN
                else -> KeyMap.DOWN or KeyMap.RIGHT
            }
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **77 tests** (69 − 6 + 14), 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchLayout.kt app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-02-milestone-6c-layout-editor.md
git commit -m "feat(android): the touch layout as data - items moved and sized over the whole screen" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The layout file

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/LayoutStore.kt`, `app/src/test/java/com/tengen/gbaemu/LayoutStoreTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/LayoutStoreTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LayoutStoreTest {
    @Test
    fun noFileMeansTheDefaults() {
        val dir = Files.createTempDirectory("layout").toFile()
        assertEquals(LayoutStore.Layouts(), LayoutStore(dir).load())
        dir.deleteRecursively()
    }

    @Test
    fun bothLayoutsAreSavedAndReadBack() {
        val dir = Files.createTempDirectory("layout").toFile()
        val store = LayoutStore(dir)
        val saved = LayoutStore.Layouts(
            upright = mapOf(LayoutItem.A to Place(0.25f, 0.9f, 1.5f), LayoutItem.PICTURE to Place(0.5f, 0.2f, 0.75f)),
            sideways = mapOf(LayoutItem.DPAD to Place(0.1f, 0.6f, 0.5f)),
        )
        assertTrue(store.save(saved))
        assertEquals(saved, LayoutStore(dir).load())
        dir.deleteRecursively()
    }

    @Test
    fun valuesOutOfRangeAreBroughtIn() {
        val dir = Files.createTempDirectory("layout").toFile()
        File(dir, "layout.json").writeText(
            "{\"upright\": [{\"id\": \"a\", \"x\": 15000, \"y\": -20, \"scale\": 90000}, {\"id\": \"nothing\", \"x\": 1, \"y\": 1, \"scale\": 1}], \"sideways\": []}")
        val l = LayoutStore(dir).load()
        assertEquals(mapOf(LayoutItem.A to Place(1f, 0f, 2f)), l.upright) // an unknown item is left out
        assertEquals(emptyMap<LayoutItem, Place>(), l.sideways)
        dir.deleteRecursively()
    }

    @Test
    fun aDamagedFileIsKeptAsBadAndTheDefaultsAreUsed() {
        val dir = Files.createTempDirectory("layout").toFile()
        File(dir, "layout.json").writeText("{\"upright\": [{\"id\": ")
        val store = LayoutStore(dir)
        assertEquals(LayoutStore.Layouts(), store.load())
        assertTrue(store.wasDamaged)
        assertTrue(File(dir, "layout.json.bad").isFile)
        assertFalse(File(dir, "layout.json").exists())
        dir.deleteRecursively()
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'LayoutStore'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/LayoutStore.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * The edited touch layouts in `files/layout.json`: `{"upright": [...], "sideways": [...]}`, each item
 * `{"id": "a", "x": 2500, "y": 9000, "scale": 15000}` in ten-thousandths ([Json] reads whole numbers). Items not
 * stored are where the default layout puts them. Written safely (a temporary file, then swapped).
 */
class LayoutStore(filesDir: File) {
    data class Layouts(val upright: Map<LayoutItem, Place> = emptyMap(), val sideways: Map<LayoutItem, Place> = emptyMap()) {
        fun of(sideways: Boolean) = if (sideways) this.sideways else upright
        fun with(sideways: Boolean, edits: Map<LayoutItem, Place>) = if (sideways) copy(sideways = edits) else copy(upright = edits)
    }

    private val file = File(filesDir, "layout.json")

    /** True after a [load] that found the file damaged (it is then kept as `layout.json.bad`). */
    var wasDamaged = false
        private set

    fun load(): Layouts {
        wasDamaged = false
        if (!file.isFile) return Layouts()
        return try {
            fromJson(file.readText())
        } catch (e: Exception) {
            file.renameTo(File(file.path + ".bad"))
            wasDamaged = true
            Layouts()
        }
    }

    fun save(layouts: Layouts): Boolean = try {
        val tmp = File(file.path + ".tmp")
        tmp.writeText(toJson(layouts))
        if (!tmp.renameTo(file)) {
            file.delete()
            tmp.renameTo(file)
        } else true
    } catch (e: Exception) {
        false
    }

    companion object {
        private const val ONE = 10000f

        fun toJson(l: Layouts): String {
            fun list(edits: Map<LayoutItem, Place>) = edits.entries.sortedBy { it.key.ordinal }.joinToString(",\n", "[\n", "\n  ]") { (item, p) ->
                "    {\"id\": ${Json.str(item.id)}, \"x\": ${(p.x * ONE).toInt()}, \"y\": ${(p.y * ONE).toInt()}, \"scale\": ${(p.scale * ONE).toInt()}}"
            }.replace("[\n\n  ]", "[]")
            return "{\n  \"upright\": ${list(l.upright)},\n  \"sideways\": ${list(l.sideways)}\n}\n"
        }

        /** Reads what [toJson] writes; values out of range are brought in, unknown items left out; throws on bad JSON. */
        fun fromJson(json: String): Layouts = Json.Reader(json).run {
            var upright = emptyMap<LayoutItem, Place>()
            var sideways = emptyMap<LayoutItem, Place>()
            obj { key ->
                val edits = LinkedHashMap<LayoutItem, Place>()
                list {
                    var id = ""
                    var x = 0L
                    var y = 0L
                    var scale = ONE.toLong()
                    obj { field ->
                        when (field) {
                            "id" -> id = string()
                            "x" -> x = number()
                            "y" -> y = number()
                            "scale" -> scale = number()
                            else -> error("unknown key $field")
                        }
                    }
                    LayoutItem.byId(id)?.let {
                        edits[it] = Place((x / ONE).coerceIn(0f, 1f), (y / ONE).coerceIn(0f, 1f),
                            TouchLayout.clampScale(scale / ONE))
                    }
                }
                when (key) {
                    "upright" -> upright = edits
                    "sideways" -> sideways = edits
                    else -> error("unknown key $key")
                }
            }
            end()
            Layouts(upright, sideways)
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **81 tests** (77 + 4), 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/LayoutStore.kt app/src/test/java/com/tengen/gbaemu/LayoutStoreTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-02-milestone-6c-layout-editor.md
git commit -m "feat(android): the touch layouts in files/layout.json" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Drawing from the layout and the game screen

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt` (draws from the layout: solid on black, see-through over the picture; the editing mode), `app/src/main/java/com/tengen/gbaemu/Screens.kt` (`GameScreen`: the game's surface at the layout's picture box, the buttons over the whole screen)

(No new automated test: `TouchTest` (on-device) checks the touch buttons with the default layout; the editing mode is checked in Task 4.)

- [x] **Step 1: Write the code**

Replace `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`:

```kotlin
package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The on-screen GBA buttons over the whole screen, placed by [TouchLayout] (the default layout, or the user's [edits]).
 * Every finger is followed; the pressed buttons go to [onMask] whenever they change, each newly pressed one with a
 * vibration tick (Android's own touch feedback, so the phone's setting applies) unless [buzz] is off. Buttons over the
 * game picture are see-through ([look]: Settings.LOOK_*), the others solid. While [hidden] (a keyboard or gamepad is in
 * use) nothing is drawn; the next touch shows the buttons again.
 *
 * [editing] (the layout editor): nothing is pressed; a finger picks the item under it ([onSelect]) and drags it
 * ([onEdit] with the new edits); the picture is drawn too ([picture], or a plain block) and the picked one outlined.
 */
class TouchControlsView(context: Context) : View(context) {
    /** Which default layout: sideways (buttons over the side bars) or upright (the area below the picture). */
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    /** The user's layout (6c): items moved and sized; empty = the default. */
    var edits: Map<LayoutItem, Place> = emptyMap()
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) setMask(0)
            invalidate()
        }

    /** Fast mode switched on: ⏩ is drawn lit. */
    var fastOn = false
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var look = Settings.LOOK_NORMAL
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }

    var buzz = true

    var onMask: (Int) -> Unit = {}
    var onShow: () -> Unit = {}

    var editing = false
    var selected: LayoutItem? = null
        set(value) {
            if (field != value) {
                field = value
                invalidate()
            }
        }
    var picture: Bitmap? = null // the editor: the paused game's picture
        set(value) {
            field = value
            invalidate()
        }
    var onSelect: (LayoutItem?) -> Unit = {}
    var onEdit: (Map<LayoutItem, Place>) -> Unit = {}

    var touchLayout: TouchLayout? = null
        private set

    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the buttons presses nothing
    private var dragStart: Place? = null // the editor: where the dragged item was when the finger came down
    private var downX = 0f
    private var downY = 0f

    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = 0xFF4A8CFF.toInt()
        strokeWidth = 2 * resources.displayMetrics.density
        pathEffect = DashPathEffect(floatArrayOf(10f, 8f), 0f)
    }

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways, edits) else null
        touchLayout = l
        dpadShape.reset()
        if (l != null) {
            val r = l.dpadRadius
            val a = r * ARM
            val across = Path().apply { addRoundRect(l.dpadX - r, l.dpadY - a, l.dpadX + r, l.dpadY + a, a / 3, a / 3, Path.Direction.CW) }
            val down = Path().apply { addRoundRect(l.dpadX - a, l.dpadY - r, l.dpadX + a, l.dpadY + r, a / 3, a / 3, Path.Direction.CW) }
            dpadShape.op(across, down, Path.Op.UNION)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val layout = touchLayout ?: return false
        val action = event.actionMasked
        if (editing) return edit(layout, event)
        if (hidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                hidden = false
                ignoreUntilUp = true
                onShow()
            }
            return true
        }
        if (ignoreUntilUp) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ignoreUntilUp = false
            return true
        }
        var pressed = 0
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) pressed = pressed or layout.buttonsAt(event.getX(i), event.getY(i))
            }
        }
        setMask(pressed)
        return true
    }

    /** The editor: a finger picks the item under it and drags it (the first finger only). */
    private fun edit(layout: TouchLayout, event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val item = layout.itemAt(event.x, event.y)
                selected = item
                onSelect(item)
                dragStart = item?.let(layout::place)
                downX = event.x
                downY = event.y
            }
            MotionEvent.ACTION_MOVE -> {
                val item = selected ?: return true
                val start = dragStart ?: return true
                val moved = layout.with(item, start.copy(x = start.x + (event.x - downX) / width, y = start.y + (event.y - downY) / height))
                onEdit(edits + (item to moved.place(item))) // kept inside the screen
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> dragStart = null
        }
        return true
    }

    private fun setMask(pressed: Int) {
        if (pressed == mask) return
        if (buzz && pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val layout = touchLayout ?: return
        if (hidden && !editing) return
        if (editing) drawPicture(canvas, layout)
        // On black: solid grey (as upright since 4c). Over the game picture: see-through white (Faint / Normal / Strong).
        val (idleA, downA, textA) = when (look) {
            Settings.LOOK_FAINT -> Triple(0x33, 0x66, 0x88)
            Settings.LOOK_STRONG -> Triple(0x99, 0xCC, 0xFF)
            else -> Triple(0x55, 0x99, 0xDD)
        }
        fun colours(over: Boolean) = if (over) {
            Triple((idleA shl 24) or 0xFFFFFF, (downA shl 24) or 0xFFFFFF, (textA shl 24) or 0xFFFFFF)
        } else {
            Triple(0xFFB4B4B4.toInt(), 0xFF6E6E6E.toInt(), 0xFF222222.toInt())
        }

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker (see-through: lighter over it).
        val (dIdle, dDown, _) = colours(layout.dpadOverPicture)
        fill.color = dIdle
        canvas.drawPath(dpadShape, fill)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = if (layout.dpadOverPicture) dIdle else dDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            val (idle, down, ink) = colours(layout.overPicture(b))
            fill.color = when {
                mask and b.bit != 0 -> down
                b.bit == KeyMap.FAST && fastOn -> LIT
                else -> idle
            }
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
            }
            text.color = ink
            text.textSize = b.scale * when {
                b.round -> 22 * density
                b.bit == KeyMap.FAST || b.bit == KeyMap.REWIND -> 20 * density // symbols, not words
                else -> 13 * density
            }
            canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
        if (editing) selected?.let { item ->
            val box = layout.box(item)
            val pad = 4 * density
            canvas.drawRect(box.left - pad, box.top - pad, box.right + pad, box.bottom + pad, outline)
        }
    }

    /** The editor: the paused game's picture fitted in the picture's box, or a plain block with its name. */
    private fun drawPicture(canvas: Canvas, layout: TouchLayout) {
        val box = layout.picture
        val frame = picture
        if (frame != null) {
            val fit = run { // 3:2 inside the box
                val w = box.right - box.left
                val h = box.bottom - box.top
                val fw = if (w * 2 <= h * 3) w else h * 3 / 2
                val fh = fw * 2 / 3
                RectF(box.left + (w - fw) / 2, box.top + (h - fh) / 2, box.left + (w + fw) / 2, box.top + (h + fh) / 2)
            }
            canvas.drawBitmap(frame, Rect(0, 0, frame.width, frame.height), fit, null)
        } else {
            fill.color = PICTURE_BLOCK
            canvas.drawRect(box.left, box.top, box.right, box.bottom, fill)
            text.color = 0xFF9BBCFF.toInt()
            text.textSize = 16 * density
            canvas.drawText("Game picture", box.cx, box.cy, text)
        }
    }

    private companion object {
        const val ARM = 0.36f // the D-pad's arm half-width, as a part of its radius
        const val LIT = 0xFFFFD54A.toInt() // ⏩ while fast mode is switched on
        const val PICTURE_BLOCK = 0xFF1B2A40.toInt()
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import kotlin.math.roundToInt

/** The very first start (no games folder yet) or a folder that can't be opened any more. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().systemBarsPadding().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(
                if (folderLost) "Can't open the games folder any more — choose it again."
                else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                    "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                    "Create new folder) and put your games in it.",
                textAlign = TextAlign.Center,
                color = if (folderLost) MaterialTheme.colorScheme.error else Color.Unspecified,
            )
            Spacer(Modifier.height(24.dp))
            Button(onClick = onChooseFolder) { Text("Choose games folder") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onOpenGame) { Text("Open a single game…") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onSettings) { Text("Settings") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
            }
        }
    }
}

/** When a game was last played, as the list shows it: "Today 14:00", "Yesterday 09:12", a date, or "Never played". */
fun playedText(lastPlayed: Long, now: Long = System.currentTimeMillis()): String {
    if (lastPlayed <= 0) return "Never played"
    val day = { t: Long -> Calendar.getInstance().apply { timeInMillis = t }.let { it.get(Calendar.YEAR) * 1000 + it.get(Calendar.DAY_OF_YEAR) } }
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(lastPlayed))
    val yesterday = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -1) }.timeInMillis
    return when (day(lastPlayed)) {
        day(now) -> "Today $time"
        day(yesterday) -> "Yesterday $time"
        else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(lastPlayed))
    }
}

/**
 * The game list: one row per game (the picture of where it was left, its name, when it was last played). A tap
 * plays it, holding opens [onHold]'s menu. [refresh] changes whenever the pictures may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit,
                  onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Text("Games", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                Box {
                    TextButton(onClick = { menuOpen = true }) { Text("⋮", fontSize = 22.sp) }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open a game…") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder…") }, onClick = { menuOpen = false; onChangeFolder() })
                        DropdownMenuItem(text = { Text("Settings") }, onClick = { menuOpen = false; onSettings() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    if (scanning) Text("Looking for games…")
                    else {
                        Text("No games found in ${folderName ?: "the folder"}.", textAlign = TextAlign.Center)
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = onChangeFolder) { Text("Change folder") }
                    }
                }
            } else {
                LazyColumn(Modifier.fillMaxSize()) {
                    items(rows, key = { it.key }) { game -> GameRow(game, refresh, picture, onPlay, onHold) }
                }
            }
        }
    }
}

@Composable
private fun GameRow(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit,
                    onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    Row(
        Modifier.fillMaxWidth()
            .pointerInput(game) { detectTapGestures(onTap = { onPlay(game) }, onLongPress = { onHold(game) }) }
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val frame = Modifier.width(96.dp).aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
        if (bitmap != null) {
            Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
                contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
        } else {
            Box(frame.background(MaterialTheme.colorScheme.surfaceVariant))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                fontSize = 12.sp, color = Color.Gray)
        }
    }
}

/** Holding a game in the list. [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onGameSettings: () -> Unit, onRemove: (() -> Unit)?, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column {
                val items = listOf("Start fresh (from the title screen)" to onStartFresh, "Import save…" to onImport,
                    "Export save…" to onExport, "Game settings…" to onGameSettings) +
                    listOfNotNull(onRemove?.let { "Remove from list" to it })
                for ((label, action) in items) {
                    TextButton(onClick = action, modifier = Modifier.fillMaxWidth()) {
                        Text(label, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Cancel") } },
    )
}

/** Import over a save that exists: the old one is kept as .sav.bak. */
@Composable
fun ReplaceSaveDialog(name: String, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace this game's save?") },
        text = { Text("$name already has a save. It will be kept as a backup, and the game will start from the title screen with the imported save.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** A game with a save chip and no save yet: start fresh, or bring in a .sav file. Dismissing cancels the opening. */
@Composable
fun ImportDialog(error: String?, onStartFresh: () -> Unit, onImport: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("No save for this game yet") },
        text = {
            Column {
                Text("Start a fresh game, or import a .sav file (for example from a PC emulator)?")
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { TextButton(onClick = onImport) { Text("Import a .sav") } },
        dismissButton = { TextButton(onClick = onStartFresh) { Text("Start fresh") } },
    )
}

/** Which page of the in-game menu is open. */
enum class MenuPage { MAIN, SAVE, LOAD, CHEATS }

/** A dimmed layer over the paused game (it takes every touch) with a card in the middle. */
@Composable
private fun MenuLayer(content: @Composable () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center,
    ) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(16.dp).widthIn(max = 440.dp)) {
            content()
        }
    }
}

/** Back while playing: the game is paused behind this menu. */
@Composable
fun GameMenu(title: String, fastSpeed: String, onFastSpeed: () -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad, "Cheats…" to onCheats, "Settings…" to onSettings,
                "Restart game" to onRestart, "Quit" to onQuit)) {
                FilledTonalButton(onClick = action, modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                    Text(label)
                }
            }
        }
    }
}

/**
 * The 10 slots as a grid of pictures (two columns). Saving: every slot can be picked; loading: only used ones.
 * [refresh] changes whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val pictures = remember(refresh) { (1..States.SLOTS).map { if (states.used(it)) states.loadPicture(it) else null } }
    val used = remember(refresh) { (1..States.SLOTS).map { states.used(it) } }
    val dates = remember(refresh) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        (1..States.SLOTS).map { if (states.used(it)) format.format(Date(states.savedAt(it))) else "empty" }
    }
    MenuLayer {
        Column(Modifier.padding(12.dp).verticalScroll(rememberScrollState())) {
            Text(if (saving) "Save state" else "Load state", style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(4.dp))
            for (row in 0 until States.SLOTS / 2) {
                Row {
                    for (col in 0..1) {
                        val slot = row * 2 + col + 1
                        val enabled = saving || used[slot - 1]
                        Column(
                            Modifier.weight(1f).padding(4.dp).clip(RoundedCornerShape(8.dp))
                                .background(MaterialTheme.colorScheme.surfaceVariant)
                                .clickable(enabled = enabled) { onPick(slot) }.padding(6.dp),
                        ) {
                            val picture = pictures[slot - 1]
                            val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
                            if (picture != null) {
                                Image(picture.asImageBitmap(), contentDescription = "slot $slot", modifier = frame,
                                    contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
                            } else {
                                Box(frame.border(1.dp, Color.Gray, RoundedCornerShape(4.dp)))
                            }
                            Text("$slot · ${dates[slot - 1]}", fontSize = 12.sp,
                                color = if (enabled) Color.Unspecified else Color.Gray,
                                modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/** The type picker's names, by NativeBridge.CHEAT_* value. */
val CHEAT_TYPES = listOf("Auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value")

/** The game's cheats: one row each (name, on/off switch); a tap on the name edits it. */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(300.dp)) {
            Text("Cheats — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Column(Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                if (cheats.isEmpty()) Text("No cheats yet.", Modifier.padding(vertical = 8.dp))
                cheats.forEachIndexed { i, c ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(12.dp))
                        Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 8.dp))
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = onAdd, modifier = Modifier.fillMaxWidth()) { Text("+ Add cheat") }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/**
 * Adding (editing == null) or editing a cheat: name, codes (one per line), type. [check] reads the codes:
 * [the type found, the first bad line (0 = none)]. Save refuses unreadable codes and names the line.
 */
@Composable
fun CheatDialog(editing: Cheat?, check: (String, Int) -> IntArray, onSave: (Cheat) -> Unit, onDelete: (() -> Unit)?,
                onCancel: () -> Unit) {
    var name by remember { mutableStateOf(editing?.name ?: "") }
    var codes by remember { mutableStateOf(editing?.codes ?: "") }
    var type by remember { mutableStateOf(editing?.type ?: NativeBridge.CHEAT_AUTO) }
    var picking by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val found = remember(codes, type) { if (codes.isBlank()) null else check(codes, type) }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(if (editing == null) "Add cheat" else "Edit cheat") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(value = codes, onValueChange = { codes = it; error = null },
                    label = { Text("Codes (one per line)") }, minLines = 4, maxLines = 10,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Box {
                    OutlinedButton(onClick = { picking = true }) { Text("Type: ${CHEAT_TYPES[type]} ▾") }
                    DropdownMenu(expanded = picking, onDismissRequest = { picking = false }) {
                        CHEAT_TYPES.forEachIndexed { i, t ->
                            DropdownMenuItem(text = { Text(t) }, onClick = { type = i; picking = false; error = null })
                        }
                    }
                }
                val hint = when {
                    error != null -> error
                    found == null -> null
                    found[1] != 0 -> "Line ${found[1]} isn't a code this app can read."
                    type == NativeBridge.CHEAT_AUTO -> "Found: ${CHEAT_TYPES[found[0]]}"
                    else -> null
                }
                if (hint != null) {
                    Text(hint, color = if (found != null && found[1] == 0 && error == null) Color.Unspecified
                        else MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 6.dp))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val result = check(codes, type)
                if (codes.isBlank()) error = "Type or paste the codes first."
                else if (result[1] != 0) error = "Line ${result[1]} isn't a code this app can read."
                else {
                    val shown = name.trim().ifEmpty { codes.lineSequence().first { it.isNotBlank() }.trim() }
                    onSave(Cheat(shown, type, codes, editing?.on ?: true))
                }
            }) { Text("Save") }
        },
        dismissButton = {
            Row {
                if (onDelete != null) TextButton(onClick = onDelete) { Text("Delete") }
                TextButton(onClick = onCancel) { Text("Cancel") }
            }
        },
    )
}

@Composable
fun DeleteCheatDialog(name: String, onDelete: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Delete $name?") },
        text = { Text("The cheat and its codes are removed from this game.") },
        confirmButton = { TextButton(onClick = onDelete) { Text("Delete") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ReplaceDialog(slot: Int, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace slot $slot?") },
        text = { Text("The state saved there is overwritten.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun RestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart from the title screen?") },
        text = { Text("The current moment is lost unless it's saved in a slot.") },
        confirmButton = { TextButton(onClick = onRestart) { Text("Restart") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the ☀ button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}, touchLook: Int = Settings.LOOK_NORMAL, buzz: Boolean = true,
               layouts: LayoutStore.Layouts = LayoutStore.Layouts()) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val sideways = maxWidth > maxHeight
        val edits = layouts.of(sideways)
        // The game's surface in the layout's picture box (upright by default: full width at the top; sideways: the whole
        // screen, the game fitted inside); the buttons over the whole screen.
        val density = LocalDensity.current
        val pic = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), density.density, sideways, edits).picture
        AndroidView(
            modifier = Modifier.offset { IntOffset(pic.left.roundToInt(), pic.top.roundToInt()) }
                .requiredSize(with(density) { (pic.right - pic.left).toDp() }, with(density) { (pic.bottom - pic.top).toDp() }),
            factory = { gameSurface(it, onDrawFailed) },
        )
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.hidden = controlsHidden
                v.fastOn = fastOn
                v.look = touchLook
                v.buzz = buzz
                v.onMask = onTouchMask
                v.onShow = onShowControls
            },
        )
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
    }
}

/** ☀: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = RoundedCornerShape(12.dp), color = Color(0xE0202020)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", color = Color.White, modifier = Modifier.weight(1f))
                        Text("Auto ", color = Color.White)
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        TextButton(onClick = { open = !open }) { Text("☀", fontSize = 28.sp, color = Color(0xFFFFD54F)) }
    }
}

private fun gameSurface(context: Context, onDrawFailed: () -> Unit) = SurfaceView(context).apply {
    holder.addCallback(object : SurfaceHolder.Callback {
        override fun surfaceCreated(holder: SurfaceHolder) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            NativeBridge.setSurface(null)
        }
    })
}
```

(`SettingsScreens.kt` is changed in Task 4; `GameScreen`'s new `layouts` parameter has a default, so `MainActivity` still builds.)

- [x] **Step 2: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **81 tests**, 0 failures.

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
```
Expected: `tests="53" failures="0"` three times.

- [x] **Step 3: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/main/java/com/tengen/gbaemu/Screens.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-02-milestone-6c-layout-editor.md
git commit -m "feat(android): the game screen and the touch buttons follow the layout" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done** — report briefly and carry on (no stop).

---
### Task 4: The layout editor and the wiring

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (Touch buttons: Edit upright layout…, Edit sideways layout…), `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (layouts loaded and saved, the editor opened and closed, the screen turned)

- [x] **Step 1: Write the code**

`app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlin.math.roundToInt

/**
 * The touch layout editor (Settings → Touch buttons → Edit … layout): the buttons and the game picture as in play
 * ([picture]: the paused game's, or a plain block), dragged with one finger; a tap picks one and the bar shows its
 * size slider (½×–2×). Reset all / Cancel / Save; Back = Cancel. The caller turns the screen to [sideways] first.
 */
@Composable
fun LayoutEditor(sideways: Boolean, start: Map<LayoutItem, Place>, picture: Bitmap?, look: Int,
                 onSave: (Map<LayoutItem, Place>) -> Unit, onCancel: () -> Unit) {
    var edits by remember { mutableStateOf(start) }
    var selected by remember { mutableStateOf<LayoutItem?>(null) }
    BackHandler { onCancel() }
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val turnedRight = (maxWidth > maxHeight) == sideways // the screen has turned to the edited orientation
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).apply { editing = true } },
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.look = look
                v.picture = picture
                v.selected = selected
                v.onSelect = { selected = it }
                v.onEdit = { edits = it }
            },
        )
        if (!turnedRight) return@BoxWithConstraints // a moment while the screen turns
        val layout = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(),
            LocalDensity.current.density, sideways, edits)
        // The bar away from the picked item (nothing picked: at the top, where upright the picture is big enough).
        val atTop = selected?.let { layout.box(it).cy > layout.height / 2 } ?: true
        Surface(
            color = Color(0xE62B2B33), shape = RoundedCornerShape(12.dp),
            modifier = Modifier.align(if (atTop) Alignment.TopCenter else Alignment.BottomCenter)
                .systemBarsPadding().padding(8.dp).widthIn(max = 480.dp).fillMaxWidth(),
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                val item = selected
                if (item == null) {
                    Text("Drag anything to move it. Tap one to change its size.", fontSize = 13.sp)
                } else {
                    val scale = layout.place(item).scale
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("${item.label} — size ${(scale * 100).roundToInt()}%", fontSize = 13.sp, modifier = Modifier.weight(1f))
                    }
                    Slider(
                        value = scale, valueRange = TouchLayout.MIN_SCALE..TouchLayout.MAX_SCALE,
                        onValueChange = { s ->
                            val sized = layout.with(item, layout.place(item).copy(scale = s))
                            edits = edits + (item to sized.place(item)) // kept inside the screen
                        },
                    )
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { edits = emptyMap(); selected = null }) { Text("Reset all") }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onCancel) { Text("Cancel") }
                    Button(onClick = { onSave(edits) }) { Text("Save") }
                }
                Text(if (sideways) "Sideways layout" else "Upright layout", fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
            }
        }
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The Settings screen's pages: the categories, then one page each. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward & rewind"), KEYS("Keyboard & controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"),
}

/** A pick list waiting to be shown: its title, the choices, the one chosen now, and what picking does. */
private class Choice(val title: String, val options: List<String>, val selected: Int, val onPick: (Int) -> Unit)

/**
 * Settings (from the library's ⚙ / ⋮ and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit, onEditLayout: (sideways: Boolean) -> Unit = {}) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val s = settings
    val onOff = listOf("On", "Off")
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = onBack) { Text("←", fontSize = 22.sp) }
                Text(page.title, style = MaterialTheme.typography.titleLarge)
            }
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        Category("Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        Category("Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        Category("Fast-forward & rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        Category("Keyboard & controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        Category("Touch buttons", "${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        Category("BIOS", biosName) { onPage(SettingsPage.BIOS) }
                    }
                    SettingsPage.PICTURE -> {
                        val pixels = listOf("Sharp", "Smooth")
                        Setting("Pixels", pixels[if (s.smooth) 1 else 0]) {
                            choosing = Choice("Pixels", pixels, if (s.smooth) 1 else 0) { onChange(s.copy(smooth = it == 1)) }
                        }
                        Setting("Size", Settings.SIZE_NAMES[s.size]) {
                            choosing = Choice("Size", Settings.SIZE_NAMES, s.size) { onChange(s.copy(size = it)) }
                        }
                        Note("Whole-number steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                    }
                    SettingsPage.SOUND -> {
                        Setting("Sound", onOff[if (s.sound) 0 else 1]) {
                            choosing = Choice("Sound", onOff, if (s.sound) 0 else 1) { onChange(s.copy(sound = it == 0)) }
                        }
                        Setting("Delay", Settings.DELAY_NAMES[s.delay]) {
                            choosing = Choice("Delay", Settings.DELAY_NAMES, s.delay) { onChange(s.copy(delay = it)) }
                        }
                        Note("Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> {
                        val speeds = Settings.SPEEDS.map(Settings::speedName)
                        Setting("Fast-forward speed", Settings.speedName(s.fastSpeed)) {
                            choosing = Choice("Fast-forward speed", speeds, Settings.SPEEDS.indexOf(s.fastSpeed)) {
                                onChange(s.copy(fastSpeed = Settings.SPEEDS[it]))
                            }
                        }
                        Setting("Rewind", onOff[if (s.rewind) 0 else 1]) {
                            choosing = Choice("Rewind", onOff, if (s.rewind) 0 else 1) { onChange(s.copy(rewind = it == 0)) }
                        }
                        Setting("Rewind length", Settings.LENGTH_NAMES[s.rewindLength]) {
                            choosing = Choice("Rewind length", Settings.LENGTH_NAMES, s.rewindLength) { onChange(s.copy(rewindLength = it)) }
                        }
                        Note("Rewind uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        Setting("Sideways, over the picture", Settings.LOOK_NAMES[s.touchLook]) {
                            choosing = Choice("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook) { onChange(s.copy(touchLook = it)) }
                        }
                        Setting("Buzz", onOff[if (s.buzz) 0 else 1]) {
                            choosing = Choice("Buzz", onOff, if (s.buzz) 0 else 1) { onChange(s.copy(buzz = it == 0)) }
                        }
                        Setting("Edit upright layout…", "") { onEditLayout(false) }
                        Setting("Edit sideways layout…", "") { onEditLayout(true) }
                        Note("Buzz follows the phone's own touch-vibration setting. The layouts: move and size every button and the game picture.")
                    }
                    SettingsPage.BIOS -> {
                        Setting("BIOS", biosName) {}
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(onClick = onChooseBios, modifier = Modifier.fillMaxWidth()) { Text("Choose a BIOS file…") }
                        if (s.biosFile) {
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth()) { Text("Use the built-in BIOS") }
                        }
                        Note("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                }
            }
        }
    }
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}

@Composable
private fun Category(title: String, summary: String, onClick: () -> Unit) {
    Column(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 12.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        Text(summary, fontSize = 13.sp, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Setting(label: String, value: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyLarge)
        Text(value, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Note(text: String) {
    Text(text, fontSize = 13.sp, color = Color.Gray, modifier = Modifier.padding(vertical = 12.dp))
}

/** A pick list: tapping a choice picks it. */
@Composable
fun ChoiceDialog(title: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(title) },
        text = {
            Column {
                options.forEachIndexed { i, label ->
                    Row(Modifier.fillMaxWidth().selectable(selected = i == selected, onClick = { onPick(i) }).padding(vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = i == selected, onClick = null)
                        Text(label, modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Each GBA button (and ⏩ ⟲) with its keyboard key and controller button; a tap waits for the new one. */
@Composable
private fun KeyTable(keys: KeyBindings, keysChanged: Int, onPickKey: (Int, Int) -> Unit, onReset: () -> Unit) {
    val rows = remember(keysChanged) {
        KeyMap.bindings.map { b ->
            Triple(b, KeyBindings.keyName(keys.key(b.bit, KeyBindings.KEYBOARD)), KeyBindings.keyName(keys.key(b.bit, KeyBindings.CONTROLLER)))
        }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        for (h in listOf("GBA", "Keyboard", "Controller")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold)
    }
    for ((b, kbd, pad) in rows) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(b.button, Modifier.weight(1f))
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
        }
        HorizontalDivider()
    }
    Spacer(Modifier.height(12.dp))
    OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth()) { Text("Reset to defaults") }
    Note("Tap a key, then press the new one. A key already used moves here; its old button shows \"—\". The phone's touch buttons stay as they are.")
}

/**
 * Waiting for the key for one GBA button; Back cancels (Back itself can't be a game key). A layer on the same window,
 * not a dialog: a dialog's window would get the key instead of MainActivity.dispatchKeyEvent.
 */
@Composable
fun KeyWaitLayer(button: String, device: Int, onCancel: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(24.dp)) {
            Column(Modifier.padding(20.dp)) {
                Text("$button — ${if (device == KeyBindings.KEYBOARD) "keyboard" else "controller"}",
                    style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(12.dp))
                Text("Press the ${if (device == KeyBindings.KEYBOARD) "key" else "controller button"} for $button…")
                TextButton(onClick = onCancel, modifier = Modifier.align(Alignment.End)) { Text("Cancel") }
            }
        }
    }
}

/** A 16 KB file that isn't the usual GBA BIOS. */
@Composable
fun UnusualBiosDialog(onUse: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Not the usual GBA BIOS") },
        text = { Text("This isn't the usual GBA BIOS; games may not start. Use it anyway?") },
        confirmButton = { TextButton(onClick = onUse) { Text("Use it") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Game settings (hold a game → Game settings…): save type and clock, Auto showing what was detected. */
@Composable
fun GameSettingsDialog(name: String, game: GameSettings.Game, detected: Pair<Int, Boolean>?, onChange: (GameSettings.Game) -> Unit,
                       onDone: () -> Unit) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val autoSave = "Auto" + (detected?.let { " (${GameSettings.saveName(it.first)})" } ?: "")
    val autoClock = "Auto" + (detected?.let { if (it.second) " (has one)" else " (none)" } ?: "")
    val saveOptions = listOf(autoSave) + GameSettings.SAVE_CHOICES.map(GameSettings::saveName)
    val saveIndex = if (game.saveType == GameSettings.AUTO) 0 else GameSettings.SAVE_CHOICES.indexOf(game.saveType) + 1
    val clockOptions = listOf(autoClock, "On", "Off")
    val clockIndex = when (game.clock) {
        GameSettings.CLOCK_ON -> 1
        GameSettings.CLOCK_OFF -> 2
        else -> 0
    }
    AlertDialog(
        onDismissRequest = onDone,
        title = { Text("Game settings — $name") },
        text = {
            Column {
                Setting("Save type", saveOptions[saveIndex]) {
                    choosing = Choice("Save type", saveOptions, saveIndex) {
                        onChange(game.copy(saveType = if (it == 0) GameSettings.AUTO else GameSettings.SAVE_CHOICES[it - 1]))
                    }
                }
                Setting("Clock", clockOptions[clockIndex]) {
                    choosing = Choice("Clock", clockOptions, clockIndex) {
                        onChange(game.copy(clock = when (it) {
                            1 -> GameSettings.CLOCK_ON
                            2 -> GameSettings.CLOCK_OFF
                            else -> GameSettings.AUTO
                        }))
                    }
                }
                Note("Used the next time this game opens. The save file is kept either way; it only loads if it fits the save type.")
            }
        },
        confirmButton = { TextButton(onClick = onDone) { Text("Done") } },
    )
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private lateinit var library: Library
    private var rows by mutableStateOf(listOf<GameEntry>()) // the list as shown
    private var hasFolder by mutableStateOf(false) // library.folder is set (the screen watches this, not the Library)
    private var folderName by mutableStateOf<String?>(null)
    private var folderLost by mutableStateOf(false)
    private var scanning by mutableStateOf(false)
    private var scanRun = 0 // only the newest scan's result is used
    private var libraryRefresh by mutableStateOf(0) // re-reads the list's pictures
    private var holdGame by mutableStateOf<GameEntry?>(null)
    private var replaceSave by mutableStateOf<Pair<GameEntry, ByteArray>?>(null)
    private var saveGame: GameEntry? = null // the game whose save is being exported / imported
    private var exportBytes: ByteArray? = null
    private lateinit var settingsStore: IntStore
    private var settings by mutableStateOf(Settings())
    private var settingsPage by mutableStateOf<SettingsPage?>(null) // the Settings screen, or null
    private lateinit var keys: KeyBindings
    private var keysChanged by mutableStateOf(0) // re-reads the key table
    private var waitingKey by mutableStateOf<Pair<Int, Int>?>(null) // (button bit, KeyBindings device) being remapped
    private var unusualBios by mutableStateOf<ByteArray?>(null) // a 16 KB file that isn't the usual BIOS, to confirm
    private lateinit var gameSettings: GameSettings
    private lateinit var layoutStore: LayoutStore
    private var layouts by mutableStateOf(LayoutStore.Layouts()) // the user's touch layouts (6c)
    private var editLayout by mutableStateOf<Boolean?>(null) // the layout editor: sideways, upright, or closed
    private var editPicture: Bitmap? = null // the paused game's picture behind the editor
    private var orientationBeforeEditor = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    private var editGame by mutableStateOf<Triple<String, String, GameSettings.Game>?>(null) // name, id, its settings
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var menu by mutableStateOf<MenuPage?>(null) // the in-game menu (the game paused) or null
    private var replaceSlot by mutableStateOf<Int?>(null)
    private var askRestart by mutableStateOf(false)
    private var slotsChanged by mutableStateOf(0) // re-reads the slot pictures
    private var session by mutableStateOf(0) // a new game session gets a new game screen (see startGame)
    private var states: States? = null // the running game's save states
    private var cheatStore: Cheats? = null // the running game's cheats file
    private var cheats by mutableStateOf(listOf<Cheat>())
    private var editCheat by mutableStateOf<Int?>(null) // -1 = a new one
    private var deleteCheat by mutableStateOf<Int?>(null)
    private var current: GameEntry? = null // the running game's row
    private var currentTitle = ""
    private var controlsHidden by mutableStateOf(false)
    private var manualLight by mutableStateOf<Int?>(null)
    private var hardware = 0 // the cartridge's extras (NativeBridge.HW_*)
    private var motion: MotionInput? = null
    private var rumble: Rumble? = null
    private var keyboardMask = 0
    private var touchMask = 0
    private val fastSwitch = FastForwardSwitch()
    private val repeatFilter = KeyRepeatFilter() // F / R held in the phone emulator's window
    private val handler = Handler(Looper.getMainLooper())
    private var fastOn by mutableStateOf(false) // ⏩ lit
    private var fastSpeed by mutableStateOf(4) // 2, 4, 8 or NativeBridge.FF_MAX; remembered for all games

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }
    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useFolder(uri)
    }
    private val exportTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) writeExport(uri)
    }
    private val pickBios = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) biosPicked(uri)
    }
    private val importFor = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPicked(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = Library(filesDir)
        if (!library.load()) toast("The game list couldn't be read; it was kept aside and is built again from the folder.")
        carryOverLastGame()
        rows = library.rows()
        hasFolder = library.folder != null
        settingsStore = PrefsStore(getSharedPreferences("settings", MODE_PRIVATE))
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        applySettings(settings)
        keys = KeyBindings(PrefsStore(getSharedPreferences("keys", MODE_PRIVATE)))
        keys.use()
        gameSettings = GameSettings(PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE)))
        layoutStore = LayoutStore(filesDir)
        layouts = layoutStore.load()
        if (layoutStore.wasDamaged) toast("The touch layout couldn't be read; it was kept aside and the default layout is used.")
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) key(session) {
                    GameScreen(
                        controlsHidden = controlsHidden,
                        fastOn = fastOn,
                        onTouchMask = { touchMask = it; sendKeys() },
                        onShowControls = { controlsHidden = false },
                        onDrawFailed = {
                            stopGame()
                            message = "This device cannot show the game: OpenGL ES 3.0 failed."
                        },
                        solar = hardware and NativeBridge.HW_SOLAR != 0,
                        manualLight = manualLight,
                        onManualLight = {
                            manualLight = it
                            motion?.manualLight = it
                        },
                        touchLook = settings.touchLook,
                        buzz = settings.buzz,
                        layouts = layouts,
                    )
                    LaunchedEffect(Unit) { // a failed save-file write: tell once per game
                        while (true) {
                            delay(1000)
                            if (NativeBridge.saveError()) {
                                toast("Could not write the save file.")
                                break
                            }
                        }
                    }
                } else if (folderLost || !hasFolder) { // no folder yet, or it can't be opened
                    FirstStartScreen(folderLost, message, onChooseFolder = { pickFolder.launch(null) },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onSettings = { settingsPage = SettingsPage.MAIN })
                } else {
                    LibraryScreen(rows, folderName, scanning, message, libraryRefresh,
                        picture = { g -> if (g.id.isEmpty()) null else States(filesDir, g.id).loadPicture(States.AUTO) },
                        onPlay = { open(it) }, onHold = { holdGame = it },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onChangeFolder = { pickFolder.launch(null) },
                        onSettings = { settingsPage = SettingsPage.MAIN })
                }
                holdGame?.let { g ->
                    GameHoldMenu(g,
                        onStartFresh = { holdGame = null; open(g, resumeAuto = false) },
                        onImport = { holdGame = null; saveGame = g; importFor.launch(arrayOf("*/*")) },
                        onExport = { holdGame = null; exportSave(g) },
                        onGameSettings = {
                            holdGame = null
                            idOf(g)?.let { id -> editGame = Triple(g.name, id, gameSettings.of(id)) }
                        },
                        onRemove = if (g.outside) ({ holdGame = null; removeGame(g) }) else null,
                        onClose = { holdGame = null })
                }
                replaceSave?.let { (g, bytes) ->
                    ReplaceSaveDialog(g.name, onReplace = { replaceSave = null; importSave(g, bytes) },
                        onCancel = { replaceSave = null })
                }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                val st = states
                when (menu) {
                    MenuPage.MAIN -> GameMenu(currentTitle, speedLabel(fastSpeed), onFastSpeed = ::nextFastSpeed,
                        onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
                        onSettings = { settingsPage = SettingsPage.MAIN },
                        onRestart = { askRestart = true }, onQuit = ::quitGame)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN })
                    null -> {}
                }
                editCheat?.let { i ->
                    CheatDialog(editing = cheats.getOrNull(i), check = NativeBridge::checkCheat,
                        onSave = { c ->
                            changeCheats(cheats.toMutableList().also { if (i in it.indices) it[i] = c else it += c })
                            editCheat = null
                        },
                        onDelete = if (i in cheats.indices) ({ deleteCheat = i }) else null,
                        onCancel = { editCheat = null })
                }
                deleteCheat?.let { i ->
                    DeleteCheatDialog(cheats[i].name,
                        onDelete = {
                            changeCheats(cheats.toMutableList().also { it.removeAt(i) })
                            deleteCheat = null
                            editCheat = null
                        },
                        onCancel = { deleteCheat = null })
                }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                settingsPage?.let { page ->
                    SettingsScreen(page, settings, keys, keysChanged, if (settings.biosFile) "BIOS file" else "Built-in",
                        onPage = { settingsPage = it },
                        onBack = { settingsPage = if (page == SettingsPage.MAIN) null else SettingsPage.MAIN },
                        onChange = ::changeSettings,
                        onPickKey = { bit, device -> waitingKey = bit to device },
                        onResetKeys = { keys.reset(); keys.use(); keysChanged++ },
                        onChooseBios = { pickBios.launch(arrayOf("*/*")) },
                        onBuiltInBios = {
                            Bios.remove(filesDir)
                            changeSettings(settings.copy(biosFile = false))
                            toast("The built-in BIOS is used from the next game on.")
                        },
                        onEditLayout = ::openLayoutEditor)
                }
                editLayout?.let { side ->
                    LayoutEditor(side, layouts.of(side), editPicture, settings.touchLook,
                        onSave = { edits ->
                            layouts = layouts.with(side, edits)
                            if (!layoutStore.save(layouts)) toast("Couldn't save the layout.")
                            closeLayoutEditor()
                        },
                        onCancel = ::closeLayoutEditor)
                }
                waitingKey?.let { (bit, device) ->
                    KeyWaitLayer(KeyMap.bindings.first { it.bit == bit }.button, device, onCancel = { waitingKey = null })
                }
                unusualBios?.let { bytes ->
                    UnusualBiosDialog(onUse = { unusualBios = null; installBios(bytes) }, onCancel = { unusualBios = null })
                }
                editGame?.let { (name, id, game) ->
                    GameSettingsDialog(name, game, gameSettings.detected(id),
                        onChange = { gameSettings.set(id, it); editGame = Triple(name, id, it) },
                        onDone = { editGame = null })
                }
                BackHandler(enabled = playing && menu == null && settingsPage == null) { openMenu() }
                BackHandler(enabled = playing && menu != null && settingsPage == null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
                }
                BackHandler(enabled = settingsPage != null && waitingKey == null && editLayout == null) {
                    settingsPage = if (settingsPage == SettingsPage.MAIN) null else SettingsPage.MAIN
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    /** The file's name as the picker showed it and its size (0 = unknown), or null. */
    private fun fileInfo(uri: Uri): Pair<String, Long>? = try {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) to (if (it.isNull(1)) 0L else it.getLong(1)) else null
        }
    } catch (e: Exception) {
        null
    }

    /** "Open a game…": the game's row if the list has it (e.g. it is in the folder), else a new outside one. */
    private fun openFromPicker(uri: Uri) {
        try { /* so the list can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            uri.toString()
        }
        val info = fileInfo(uri)
        val entry = library.games.firstOrNull { it.key == key }
            ?: GameEntry(key, uri.toString(), Library.nameOf(info?.first ?: uri.lastPathSegment ?: "game"), outside = true,
                size = info?.second ?: 0)
        open(entry)
    }

    /** The games folder picked: its permission kept (the old folder's given back), the list read from it. */
    private fun useFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        library.folder?.let { old ->
            if (old != tree.toString()) try {
                contentResolver.releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: Exception) {
            }
        }
        library.folder = tree.toString()
        library.save()
        hasFolder = true
        folderLost = false
        message = null
        refreshLibrary()
    }

    /**
     * Reads the folder again (off the main thread) and shows the list. Played games whose id is not known yet (the
     * last game carried over from before the list) are read once to learn it, so their picture shows. Outside games
     * of unknown size get it, so [Library.merge] can match them with the folder's copy by name and size.
     */
    private fun refreshLibrary() {
        rows = library.rows()
        val folder = library.folder?.let(Uri::parse)
        val unknown = library.games.filter { it.lastPlayed > 0 && it.id.isEmpty() }
        val unsized = library.games.filter { it.outside && it.size <= 0 }
        if (folder == null && unknown.isEmpty() && unsized.isEmpty()) return
        val run = ++scanRun
        scanning = true
        thread(name = "library-scan") {
            val found = folder?.let { FolderScan.scan(contentResolver, it) }
            val name = folder?.let { FolderScan.folderName(contentResolver, it) }
            val ids = unknown.mapNotNull { g -> readGame(Uri.parse(g.uri))?.let { g.key to GameId.of(it) } }.toMap()
            val sizes = unsized.mapNotNull { g -> fileInfo(Uri.parse(g.uri))?.second?.takeIf { it > 0 }?.let { g.key to it } }.toMap()
            runOnUiThread {
                if (run != scanRun) return@runOnUiThread
                scanning = false
                for (g in library.games) ids[g.key]?.let { library.played(g.copy(id = it)) }
                for (g in library.games) sizes[g.key]?.let { library.played(g.copy(size = it)) }
                if (folder != null) {
                    folderLost = found == null
                    if (found != null) library.merge(found)
                }
                folderName = name
                library.save()
                rows = library.rows()
                libraryRefresh++
            }
        }
    }

    /** Before the list (5c and earlier): the last game opened becomes the list's first game. */
    private fun carryOverLastGame() {
        val old = getSharedPreferences("last_game", MODE_PRIVATE)
        val u = old.getString("uri", null) ?: return
        val uri = Uri.parse(u)
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            u
        }
        if (library.games.none { it.key == key }) {
            val info = fileInfo(uri)
            val name = info?.first?.let(Library::nameOf) ?: old.getString("title", null) ?: "last game"
            library.played(GameEntry(key, u, name, lastPlayed = System.currentTimeMillis(), outside = true, size = info?.second ?: 0))
            library.save()
        }
        old.edit().clear().apply()
    }

    /** The game's bytes (from a .gba or a .zip), or null. */
    private fun readGame(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use(RomFile::read)
    } catch (e: Exception) {
        null
    }

    /** The game's id, reading the game if the list doesn't know it yet (then remembered); null if it can't be read. */
    private fun idOf(g: GameEntry): String? {
        if (g.id.isNotEmpty()) return g.id
        val bytes = readGame(Uri.parse(g.uri)) ?: run { toast("Can't open ${g.name}."); return null }
        val id = GameId.of(bytes)
        library.played(g.copy(id = id))
        library.save()
        rows = library.rows()
        return id
    }

    private fun removeGame(g: GameEntry) {
        library.remove(g.key)
        library.save()
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(g.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: Exception) {
        }
        rows = library.rows()
    }

    private fun exportSave(g: GameEntry) {
        val id = idOf(g) ?: return
        val bytes = SaveFiles.export(filesDir, id) ?: run { toast("No save for this game yet."); return }
        exportBytes = bytes
        exportTo.launch("${g.name}.sav")
    }

    private fun writeExport(uri: Uri) {
        val bytes = exportBytes ?: return
        exportBytes = null
        try {
            contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } ?: error("no stream")
            toast("Save exported.")
        } catch (e: Exception) {
            toast("Couldn't write the save there.")
        }
    }

    private fun importPicked(uri: Uri) {
        val g = saveGame ?: return
        saveGame = null
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        if (!SaveFiles.validSize(bytes.size)) {
            toast("That file isn't a save for this game.")
            return
        }
        val id = idOf(g) ?: return
        if (SaveFiles.file(filesDir, id).isFile) replaceSave = g to bytes else importSave(g, bytes)
    }

    private fun importSave(g: GameEntry, bytes: ByteArray) {
        val id = idOf(g) ?: return
        when (SaveFiles.import(filesDir, id, bytes)) {
            SaveFiles.Import.OK -> toast("Save imported. ${g.name} will start from the title screen.")
            SaveFiles.Import.WRONG_SIZE -> toast("That file isn't a save for this game.")
            SaveFiles.Import.FAILED -> toast("Couldn't write the save.")
        }
        libraryRefresh++ // the picture of where it was left is gone
    }

    /** Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true) {
        val uri = Uri.parse(entry.uri)
        val bytes = try {
            contentResolver.openInputStream(uri)?.use(RomFile::read)
        } catch (e: RomFile.NotAGame) {
            message = "Not a GBA game."
            return
        } catch (e: Exception) {
            null
        }
        if (bytes == null) {
            message = "Can't open ${entry.name}."
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        val gameId = GameId.of(bytes)
        val bios = if (settings.biosFile) {
            Bios.load(filesDir) ?: null.also { toast("The BIOS file is missing; the built-in BIOS is used.") }
        } else null
        NativeBridge.setBios(bios)
        val own = gameSettings.of(gameId)
        NativeBridge.setGameOverrides(own.saveType, own.clock)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA game."
            return
        }
        if (own == GameSettings.Game()) NativeBridge.cartSetup().let { gameSettings.remember(gameId, it[0], it[1]) }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val played = entry.copy(id = gameId, lastPlayed = System.currentTimeMillis())
        library.played(played)
        library.save()
        rows = library.rows()
        message = null
        val st = States(filesDir, gameId)
        states = st
        cheatStore = Cheats(filesDir, gameId).also { store ->
            cheats = store.load() ?: emptyList<Cheat>().also {
                toast("This game's cheats file couldn't be read; it was kept aside and the game starts without cheats.")
            }
        }
        current = played
        currentTitle = played.name
        if (resumeAuto && st.used(States.AUTO)) {
            if (st.load(States.AUTO)) {
                startGame()
                return
            }
            toast("Couldn't resume where you left off; starting the game.")
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    private fun importFrom(uri: Uri) {
        val bytes = readBytes(uri)
        when {
            bytes == null -> importError = "Could not read that file."
            NativeBridge.importSave(bytes) != 0 -> importError = "This .sav doesn't fit this game."
            else -> {
                askImport = false
                startGame()
            }
        }
    }

    private fun startGame() {
        keyboardMask = 0
        touchMask = 0
        fastSwitch.reset()
        fastOn = false
        repeatFilter.clear()
        controlsHidden = false
        if (hardware and (NativeBridge.HW_TILT or NativeBridge.HW_GYRO or NativeBridge.HW_SOLAR) != 0) {
            motion = MotionInput(this, hardware) { toast(it) }
        }
        if (hardware and NativeBridge.HW_RUMBLE != 0) rumble = Rumble(this)
        // A fresh SurfaceView for every session: Restart stops and starts within one frame, and the new render
        // thread only gets a window when the view's surface is created (the old one kept the last frame).
        session++
        sendCheats() // after the ROM, its save file and the automatic state: before the first frame
        NativeBridge.start()
        playing = true
        setFullScreen(true)
        startExtras()
    }

    /** The cartridge's sensors and rumble run only while the game does. */
    private fun startExtras() {
        motion?.start()
        rumble?.start()
    }

    private fun stopExtras() {
        motion?.stop()
        rumble?.stop()
    }

    private fun stopGame() {
        stopExtras()
        motion = null
        rumble = null
        NativeBridge.stop() // writes the save
        playing = false
        menu = null
        setFullScreen(false)
    }

    /** Back: the game pauses (sound, sensors, rumble too; the save file is written) behind the menu. */
    private fun openMenu() {
        stopExtras()
        NativeBridge.pause()
        slotsChanged++
        menu = MenuPage.MAIN
    }

    private fun resumeGame() {
        menu = null
        NativeBridge.resume()
        startExtras()
    }

    private fun saveTo(slot: Int) {
        replaceSlot = null
        if (states?.save(slot) == true) toast("Saved to slot $slot") else toast("Couldn't save the state.")
        slotsChanged++
        resumeGame()
    }

    private fun loadFrom(slot: Int) {
        if (states?.load(slot) == true) resumeGame()
        else toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The moment the player left, for resuming it next time (the game must be paused). */
    private fun saveAuto() {
        if (playing) states?.save(States.AUTO)
    }

    private fun quitGame() {
        saveAuto()
        stopGame()
        refreshLibrary() // it is now first, with the picture of where it was left
    }

    private fun restartGame() {
        askRestart = false
        val g = current ?: return
        stopGame()
        open(g, resumeAuto = false)
    }

    /** Buttons to the game; ⏩ and ⟲ (touch or keys) to the switch and the emulation thread. */
    private fun sendKeys(now: Long = SystemClock.uptimeMillis()) {
        val all = KeyMap.merge(keyboardMask, touchMask)
        NativeBridge.setKeys(all and KeyMap.GBA_BUTTONS)
        val fastDown = all and KeyMap.FAST != 0
        if (fastDown != fastSwitch.held) {
            if (fastDown) fastSwitch.press(now) else fastSwitch.release(now)
        }
        fastOn = fastSwitch.switchedOn
        NativeBridge.setFastForward(if (fastSwitch.fast) fastSpeed else 0)
        NativeBridge.setRewinding(all and KeyMap.REWIND != 0)
    }

    private fun releaseRepeatedKeys() {
        for ((bit, upAt) in repeatFilter.due(SystemClock.uptimeMillis())) {
            keyboardMask = keyboardMask and bit.inv()
            sendKeys(upAt) // the hold ended at the key-up, which decides tap or hold
        }
    }

    /** The panel changed the list (the game is paused behind the menu): keep it and switch the cheats. */
    private fun changeCheats(list: List<Cheat>) {
        cheats = list
        if (cheatStore?.save(list) == false) toast("Couldn't save the cheats.")
        sendCheats()
    }

    /** The switched-on cheats into the emulator; one that no longer reads is switched off (and said so). */
    private fun sendCheats() {
        val bad = cheats.filter { it.on && NativeBridge.checkCheat(it.codes, it.type)[1] != 0 }
        if (bad.isNotEmpty()) {
            for (c in bad) toast("Cheat '${c.name}' can't be read and was switched off.")
            cheats = cheats.map { if (it in bad) it.copy(on = false) else it }
            cheatStore?.save(cheats)
        }
        val on = cheats.filter { it.on }
        if (NativeBridge.setCheats(on.map { it.codes }.toTypedArray(), on.map { it.type }.toIntArray()) != 0 && on.isNotEmpty()) {
            toast("The cheats couldn't be switched on.")
        }
    }

    private fun speedLabel(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

    /** The menu's fast-forward row: 2× → 4× → 8× → Max → 2×, remembered for all games. */
    private fun nextFastSpeed() {
        changeSettings(settings.copy(fastSpeed = when (fastSpeed) {
            2 -> 4
            4 -> 8
            8 -> NativeBridge.FF_MAX
            else -> 2
        }))
    }

    /** A setting changed (Settings screen, menu row): kept, and used at once where it can be. */
    private fun changeSettings(s: Settings) {
        settings = s
        s.save(settingsStore)
        fastSpeed = s.fastSpeed
        applySettings(s)
        if (playing) sendKeys() // a switched-on fast mode uses the new speed
    }

    /** Picture and sound at once; rewind for the next game (the touch buttons read [settings] themselves). */
    private fun applySettings(s: Settings) {
        NativeBridge.setPicture(s.size, s.smooth)
        NativeBridge.setSound(s.sound, s.soundBursts)
        NativeBridge.setRewindBudget(s.rewindBudget)
    }

    /** The layout editor turns the screen to the edited orientation (the phone may be held either way). */
    private fun openLayoutEditor(sideways: Boolean) {
        editPicture = if (playing) NativeBridge.frameBgr555()?.let(States::bitmapOf) else null // paused behind the menu
        orientationBeforeEditor = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        requestedOrientation = if (sideways) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        editLayout = sideways
    }

    /**
     * Back the way the screen was, then free again. Found while prototyping: with auto-rotate off, Android keeps the
     * editor's orientation as the locked one, so just freeing the screen would leave it turned.
     */
    private fun closeLayoutEditor() {
        editLayout = null
        editPicture = null
        requestedOrientation = orientationBeforeEditor
        handler.postDelayed({
            if (editLayout == null) requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }, 1000)
    }

    private fun biosPicked(uri: Uri) {
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        when (Bios.check(bytes)) {
            Bios.Check.WRONG_SIZE -> toast("That isn't a GBA BIOS (it must be 16 KB).")
            Bios.Check.UNUSUAL -> unusualBios = bytes
            Bios.Check.USUAL -> installBios(bytes)
        }
    }

    private fun installBios(bytes: ByteArray) {
        if (!Bios.install(filesDir, bytes)) {
            toast("Couldn't copy the BIOS file.")
            return
        }
        changeSettings(settings.copy(biosFile = true))
        toast("BIOS file chosen: it is used the next time a game opens.")
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()

    private fun setFullScreen(on: Boolean) {
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (on) {
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        waitingKey?.let { (bit, device) -> // Settings → Keyboard & controller: this key is the button's now
            if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_ESCAPE) {
                if (event.action == KeyEvent.ACTION_UP) waitingKey = null
            } else if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                keys.assign(bit, device, event.keyCode)
                keys.use()
                keysChanged++
                waitingKey = null
            }
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_ESCAPE) { // Esc on a keyboard = Back (the menu)
            if (event.action == KeyEvent.ACTION_UP) onBackPressedDispatcher.onBackPressed()
            return true
        }
        val motionKey = MotionMath.motionKey(event.keyCode)
        val m = motion
        if (playing && motionKey != 0 && m != null) { // I/J/K/L tilt, Q/E turn
            when (event.action) {
                KeyEvent.ACTION_DOWN -> m.setKey(motionKey, true)
                KeyEvent.ACTION_UP -> m.setKey(motionKey, false)
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.isKeyboardExtra(event.keyCode)) { // F / R: see KeyRepeatFilter
            val bit = KeyMap.bitFor(event.keyCode)
            when (event.action) {
                KeyEvent.ACTION_DOWN -> if (repeatFilter.down(bit, event.eventTime)) {
                    keyboardMask = keyboardMask or bit
                    sendKeys(event.eventTime)
                }
                KeyEvent.ACTION_UP -> {
                    val wait = repeatFilter.up(bit, event.eventTime)
                    if (wait >= 0) handler.postDelayed(::releaseRepeatedKeys, wait)
                }
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.bitFor(event.keyCode) != 0) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, true)
                KeyEvent.ACTION_UP -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, false)
            }
            controlsHidden = true // a keyboard or gamepad is in use
            sendKeys()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        stopExtras()
        NativeBridge.pause() // returns once the save is written
        saveAuto() // Home, another app, screen off: keep the moment
    }

    override fun onResume() {
        super.onResume()
        if (playing && menu == null && replaceSlot == null && !askRestart) {
            NativeBridge.resume()
            startExtras()
        }
        if (!playing) refreshLibrary() // games added to the folder meanwhile, back from a picker
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

(Found while prototyping: with auto-rotate off, Android kept the sideways editor's orientation after it freed the screen — `closeLayoutEditor` first asks for the orientation the screen had when the editor opened, then frees it 1 s later.)

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit && bash tools/dev.sh test
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: Kotlin 81/81, `404 tests, 0 failed` (the core unchanged), CTest 120/120, on-device `tests="53" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)** — in the user's library (games folder `Download/GBA` with Claude's test file `m7_demo.gba`), upright on the Pixel 9 (1080 × 2424; the default A at (943, 1509)); the user's `layout.json` is kept aside and put back at the end. Compare the user's settings files' md5 before and after (they must not change).

Use a scratchpad folder `$SP`. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
shot() { ADB exec-out screencap -p > "$SP/$1.png"; }
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
tap_on() {  # tap the centre of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            ADB shell input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
slider_to() {  # tap the editor's size slider at $1 percent of its track (0 = ½×, 100 = 2×)
    ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
    set -- $1 $(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F 'class="android.widget.SeekBar"' | grep -oE 'bounds="[^"]*"' | grep -oE '[0-9]+')
    ADB shell input tap $(($2 + 40 + ($4 - $2 - 80) * $1 / 100)) $((($3 + $5) / 2))
}
press() { ADB shell input swipe $1 $2 $1 $2 250; }  # a finger held 250 ms (a plain tap can fall between two frames)
same_game() {  # the top 1080x720 of two upright screenshots: identical?
    python -c "from PIL import Image; import sys; a=Image.open(sys.argv[1]).crop((0,0,1080,720)); b=Image.open(sys.argv[2]).crop((0,0,1080,720)); print('same' if a.tobytes()==b.tobytes() else 'DIFFERENT')" "$(cygpath -w "$SP/$1.png")" "$(cygpath -w "$SP/$2.png")"
}
colour_at() { python -c "from PIL import Image; import sys; print(Image.open(sys.argv[1]).convert('RGB').getpixel((int(sys.argv[2]), int(sys.argv[3]))))" "$(cygpath -w "$SP/$1.png")" $2 $3; }
rotation() { echo "display $(ADB shell dumpsys window | grep -oE 'mRotation=[0-9]' | head -1), locked $(ADB shell settings get system user_rotation)"; }
layout_md5() { ADB shell run-as com.tengen.gbaemu md5sum files/layout.json 2>/dev/null | cut -c1-12; }
```

Then (as a script, `bash <file>`; it stops if m7_demo is not in the library):

```bash
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu
bash tools/dev.sh install
# the user's layout kept aside (put back at the end); the check starts from the default layout
ADB shell run-as com.tengen.gbaemu cp files/layout.json files/layout.json.keep 2>/dev/null
ADB shell run-as com.tengen.gbaemu rm -f files/layout.json
bash tools/dev.sh launch; sleep 4
tap_on text "m7_demo" || { echo "STOP: m7_demo is not in the library"; exit 1; }
sleep 4; shot default; colour_at default 943 1509                              # A of the default upright layout (grey)
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5; tap_on text "Touch buttons"; sleep 1
texts | tail -4                                                                # … Edit upright layout…, Edit sideways layout…, the note
tap_on text "Edit upright layout…"; sleep 2; texts | head -2
ADB shell input swipe 943 1509 540 1100 800; sleep 1.5; texts | head -1       # A dragged to the middle: 'A — size 100%'
slider_to 67; sleep 1; texts | head -1                                         # 'A — size 15x%'
tap_on text "Save"; sleep 2; ADB shell run-as com.tengen.gbaemu cat files/layout.json
ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "▶  Resume"; sleep 2
shot p0; press 943 1509; sleep 1.5; shot p1; echo "old place: $(same_game p0 p1)"
press 540 1100; sleep 1.5; shot p2; echo "new place: $(same_game p1 p2)"
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5; tap_on text "Touch buttons"; sleep 1
tap_on text "Edit upright layout…"; sleep 2
ADB shell input swipe 540 600 540 1000 800; sleep 1.5; texts | head -1       # 'Game picture — size 100%'
slider_to 4; sleep 1; texts | head -1                                          # about 56 %
tap_on text "Save"; sleep 2; ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1
tap_on text "▶  Resume"; sleep 2; shot small; colour_at small 540 100           # the top is black now: the picture moved down
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5; tap_on text "Touch buttons"; sleep 1
before=$(layout_md5); tap_on text "Edit upright layout…"; sleep 2
ADB shell input swipe 720 1656 300 1900 800; sleep 1; tap_on text "Cancel"; sleep 1.5
echo "cancel: $before -> $(layout_md5)"
echo "before: $(rotation)"; tap_on text "Edit sideways layout…"; sleep 3; echo "sideways editor: $(rotation)"
tap_on text "Cancel"; sleep 3; echo "after: $(rotation)"
tap_on text "Edit upright layout…"; sleep 2; tap_on text "Reset all"; sleep 1; tap_on text "Save"; sleep 2
ADB shell run-as com.tengen.gbaemu cat files/layout.json
ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "▶  Resume"; sleep 2
shot reset; colour_at reset 943 1509                                           # A back in its place
ADB logcat -d -s AndroidRuntime:E | tail -3
ADB shell am force-stop com.tengen.gbaemu
ADB shell run-as com.tengen.gbaemu rm -f files/layout.json
if ADB shell run-as com.tengen.gbaemu ls files/layout.json.keep >/dev/null 2>&1; then
    ADB shell run-as com.tengen.gbaemu mv files/layout.json.keep files/layout.json
fi
```

Expected:
- `default`'s A pixel grey `(180, 180, 180)`; the Touch buttons page lists 'Edit upright layout…', 'Edit sideways layout…'; the editor's bar 'Drag anything to move it. Tap one to change its size.', 'Reset all', 'Cancel', 'Save', 'Upright layout'.
- After the drag 'A — size 100%', after the slider about 'A — size 150%'; `layout.json` `{"id": "a", "x": ≈5000, "y": ≈4530, "scale": ≈15000}`; `old place: same`, `new place: DIFFERENT`.
- 'Game picture — size 100%', then about 56 %; `small`'s pixel (540, 100) black `(0, 0, 0)`.
- `cancel: X -> X` (unchanged); `before: display mRotation=0, locked 0`, `sideways editor: display mRotation=1, …`, `after: display mRotation=0, locked 0`.
- After Reset all: `"upright": []`; `reset`'s A pixel grey again. No `AndroidRuntime` crash; the user's layout back.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/LayoutEditor.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-02-milestone-6c-layout-editor.md
git commit -m "feat(android): the touch layout editor - move and size buttons and the picture, upright and sideways" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: User check and milestone 6c exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 120/120 (sanitizers on, no reports), 404 unit tests, Kotlin 81, on-device 53 (emulator only).

- [x] **Step 2: The user's check** (the app installed on the emulator)

In game A: Settings… → Touch buttons → Edit upright / sideways layout…: buttons and the picture moved and sized, saved, played with; Cancel and Reset all.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (6c ✅ with the numbers and the user's check — **milestone 6 complete**; Next = milestone 7 brainstorming — ARM64 assembly speedups, which need the user's real phone (the emulator is x86_64)); spec status → `Implemented — milestone 6c complete <date> (tag milestone-6c)`; parent spec §10 row 6 → 6c ✅, milestone 6 ✅; `CLAUDE.md` key files → milestone 7 needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-6c replay-6c`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 6c complete - touch layout editor (milestone 6 complete)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-6c
```
